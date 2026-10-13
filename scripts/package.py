#!/usr/bin/env python3
"""Package validated installation files and required license notices."""
from pathlib import Path
import json
import os
import re
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD_ROOT = Path(os.environ.get('BUILD_ROOT', Path.home() / '.cache/nx-gamehide'))
def write_zip(path, entries):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(entries.items()):
            if name.startswith('/') or '..' in Path(name).parts:
                raise ValueError(f'unsafe archive name: {name}')
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)

def validate_pfs0(data):
    if len(data) < 16 or data[:4] != b'PFS0':
        raise ValueError('invalid ExeFS PFS0 header')
    count, string_size, reserved = struct.unpack_from('<III', data, 4)
    if count != 2 or reserved or 16 + count * 24 + string_size > len(data):
        raise ValueError('unexpected ExeFS table')
    string_start = 16 + count * 24
    content_start = string_start + string_size
    files, ranges = {}, []
    for i in range(count):
        offset, size, name_offset, entry_reserved = struct.unpack_from('<QQII', data, 16 + i * 24)
        if entry_reserved or name_offset >= string_size or not size or content_start + offset + size > len(data):
            raise ValueError('invalid ExeFS entry')
        end = data.find(b'\0', string_start + name_offset, content_start)
        if end < 0:
            raise ValueError('unterminated ExeFS filename')
        name = data[string_start + name_offset:end].decode('ascii')
        if name in files:
            raise ValueError('duplicate ExeFS entry')
        files[name] = data[content_start + offset:content_start + offset + size]
        ranges.append((offset, offset + size))
    ranges.sort()
    if ranges[0][1] > ranges[1][0] or set(files) != {'main', 'main.npdm'}:
        raise ValueError('unexpected ExeFS contents')
    if files['main'][:4] != b'NSO0' or files['main.npdm'][:4] != b'META':
        raise ValueError('invalid NSO/NPDM')
    return files

def validate_binaries(exefs, overlay, version):
    files = validate_pfs0(exefs)
    npdm = files['main.npdm']
    if len(npdm) < 0x80:
        raise ValueError('truncated NPDM')
    aci_offset, aci_size = struct.unpack_from('<II', npdm, 0x70)
    if (aci_size < 0x18 or aci_offset + aci_size > len(npdm)
            or npdm[aci_offset:aci_offset + 4] != b'ACI0'
            or struct.unpack_from('<Q', npdm, aci_offset + 0x10)[0] != 0x00FF48474D000001):
        raise ValueError('invalid NPDM program ID')
    if len(overlay) < 0x20 or overlay[0x10:0x14] != b'NRO0':
        raise ValueError('invalid overlay NRO')
    nro_size = struct.unpack_from('<I', overlay, 0x18)[0]
    if nro_size < 0x80 or nro_size + 0x38 > len(overlay) or overlay[nro_size:nro_size + 4] != b'ASET':
        raise ValueError('invalid overlay asset header')
    offset, size = struct.unpack_from('<QQ', overlay, nro_size + 0x18)
    nacp = overlay[nro_size + offset:nro_size + offset + size]
    if size != 0x4000 or len(nacp) != size or nacp[0x3060:0x3070].split(b'\0')[0].decode() != version:
        raise ValueError('overlay version does not match source')
    return files

def third_party_notice():
    metadata = json.loads((ROOT / 'scripts/dependencies.json').read_text())
    lines = ['Third-party software (combined distribution: GPL-2.0-only)']
    for name, info in metadata['sources'].items():
        lines.append(f"{name}: {info['url']} @ {info['revision']} ({info['license']})")
    lines += ['libtesla copyright (C) 2020 werwolv; GPLv2 selected.',
              'GCC/libstdc++: https://www.gnu.org/licenses/gcc-exception-3.1.html',
              'newlib copyright source: ' + metadata['newlib_notice']['url']]
    stb = (ROOT / 'overlay/libtesla/include/stb_truetype.h').read_text()
    mit = stb.split('ALTERNATIVE A - MIT License', 1)[1].split('ALTERNATIVE B - Public Domain', 1)[0]
    return ('\n'.join(lines) + '\n\nlibnx (ISC)\n' +
            (BUILD_ROOT / 'deps/libnx/LICENSE.md').read_text() + '\n\nstb_truetype (MIT)\n' +
            mit + '\n\nnewlib\n' + (BUILD_ROOT / 'deps/COPYING.NEWLIB').read_text()).encode()

def main():
    subprocess.run(['python3', str(ROOT / 'scripts/dependencies.py'), '--check'], check=True)
    version = re.search(r'Version = "([^"]+)"', (ROOT / 'common/protocol.hpp').read_text()).group(1)
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-rc[0-9]+)?', version):
        raise ValueError('invalid version')
    exefs = (BUILD_ROOT / 'backend/exefs.nsp').read_bytes()
    overlay = (BUILD_ROOT / 'overlay/nx-gamehide.ovl').read_bytes()
    files = validate_binaries(exefs, overlay, version)
    if files['main'] != (BUILD_ROOT / 'backend/main').read_bytes() or files['main.npdm'] != (BUILD_ROOT / 'backend/main.npdm').read_bytes():
        raise ValueError('stale ExeFS; rebuild first')
    notice = third_party_notice()
    sd = {
        'switch/.overlays/nx-gamehide.ovl': overlay,
        'atmosphere/contents/00FF48474D000001/exefs.nsp': exefs,
        'atmosphere/contents/00FF48474D000001/flags/boot2.flag': b'',
        'atmosphere/contents/00FF48474D000001/LICENSE': (ROOT / 'LICENSE').read_bytes(),
        'atmosphere/contents/00FF48474D000001/NOTICE.txt': notice,
    }
    dist = ROOT / 'dist'
    dist.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=dist) as stage:
        name = f'NX-GameHide-v{version}.zip'
        staged = Path(stage) / name
        write_zip(staged, sd)
        with zipfile.ZipFile(staged) as archive:
            if archive.testzip() is not None:
                raise ValueError(f'archive integrity check failed: {name}')
        os.replace(staged, dist / name)
        print(f'{name}: {(dist / name).stat().st_size} bytes')

if __name__ == '__main__':
    main()
