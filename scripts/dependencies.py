#!/usr/bin/env python3
"""Fetch pinned upstream sources and build locally; never install into /opt."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
BUILD_ROOT = Path(os.environ.get('BUILD_ROOT', Path.home() / '.cache/nx-gamehide'))
DEPS = BUILD_ROOT / 'deps'
METADATA = json.loads((ROOT / 'scripts/dependencies.json').read_text())
LOCK = METADATA['sources']
OUTPUTS = ('COPYING.NEWLIB', 'libnx-install/lib/libnx.a', 'libnx-install/switch.specs',
           'libnx-install/switch.ld',
           'Atmosphere/libraries/libstratosphere/lib/nintendo_nx_arm64_armv8a/release/libstratosphere.a')

def digest(path):
    with path.open('rb') as stream:
        result = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()

def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)

def check_sources(name):
    path = DEPS / name
    revision = subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != LOCK[name]['revision']:
        raise RuntimeError(f'{name}: expected {LOCK[name]["revision"]}, found {revision}')
    if subprocess.check_output(['git', '-C', str(path), 'status', '--porcelain', '--untracked-files=no']):
        raise RuntimeError(f'{name}: upstream source has local modifications')
    return path

def verify_vendored():
    manifest = METADATA['vendored']
    for name, expected in manifest.items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'vendored file changed: {name}; review provenance before rebuilding')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 2))
    args = parser.parse_args()
    if not args.check and (ROOT / '.git').exists():
        run('git', '-C', str(ROOT), 'submodule', 'update', '--init', '--recursive')
    verify_vendored()
    if args.check:
        for name in ('Atmosphere', 'libnx'):
            check_sources(name)
        stamp = DEPS / 'build-info.json'
        if not stamp.is_file():
            raise RuntimeError('missing dependency build record; run make dependencies')
        manifest = json.loads(stamp.read_text())
        if manifest['revisions'] != {name: LOCK[name]['revision'] for name in ('Atmosphere', 'libnx')}:
            raise RuntimeError('stale dependency build record; run make dependencies')
        for name in OUTPUTS:
            if not (DEPS / name).is_file() or digest(DEPS / name) != manifest['sha256'].get(name):
                raise RuntimeError(f'missing or changed dependency output: {name}; run make dependencies')
        return
    DEPS.mkdir(parents=True, exist_ok=True)
    notice = DEPS / 'COPYING.NEWLIB'
    info = METADATA['newlib_notice']
    if not notice.exists():
        with urllib.request.urlopen(info['url'], timeout=30) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != info['sha256']:
            raise RuntimeError('newlib copyright download failed checksum verification')
        notice.write_bytes(data)
    if digest(notice) != info['sha256']:
        raise RuntimeError('newlib copyright notice has changed')
    for name in ('Atmosphere', 'libnx'):
        path = DEPS / name
        if not path.exists():
            run('git', 'init', str(path))
            run('git', '-C', str(path), 'remote', 'add', 'origin', LOCK[name]['url'])
            run('git', '-C', str(path), 'fetch', '--depth', '1', 'origin', LOCK[name]['revision'])
            run('git', '-C', str(path), 'checkout', '-b', 'codex/pinned', 'FETCH_HEAD')
        check_sources(name)
    env = os.environ.copy()
    devkit = Path(env.get('DEVKITPRO', '/opt/devkitpro'))
    env['DEVKITPRO'] = str(devkit)
    env['PATH'] = f'{devkit}/devkitA64/bin:{devkit}/tools/bin:' + env['PATH']
    run('make', '-C', str(DEPS / 'libnx'), f'-j{args.jobs}', env=env)
    source, target = DEPS / 'libnx/nx', DEPS / 'libnx-install'
    target.mkdir(exist_ok=True)
    for name in ('include', 'lib'):
        shutil.copytree(source / name, target / name, dirs_exist_ok=True)
    shutil.copytree(source / 'external/bsd/include', target / 'include', dirs_exist_ok=True)
    for name in ('switch_rules', 'switch.specs', 'switch.ld', 'default_icon.jpg'):
        shutil.copy2(source / name, target / name)
    # Upstream specs hard-code DEVKITPRO/libnx. Redirect only the staged copy
    # so linking uses this exact revision's linker script too.
    specs = target / 'switch.specs'
    specs.write_text(specs.read_text().replace('DEVKITPRO /libnx/switch.ld', 'LIBNX /switch.ld'))
    env['LIBNX'] = str(target)
    run('make', '-C', str(DEPS / 'Atmosphere/libraries/libstratosphere'), f'-j{args.jobs}', env=env)
    manifest = {
        'revisions': {name: LOCK[name]['revision'] for name in ('Atmosphere', 'libnx')},
        'compiler': subprocess.check_output([str(devkit / 'devkitA64/bin/aarch64-none-elf-g++'), '--version'], text=True).splitlines()[0],
        'sha256': {name: digest(DEPS / name) for name in OUTPUTS},
    }
    (DEPS / 'build-info.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Pinned dependencies built successfully.')

if __name__ == '__main__':
    main()
