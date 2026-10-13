import importlib.util
import sys
sys.dont_write_bytecode = True
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('package', ROOT / 'scripts/package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)

class ReleaseTests(unittest.TestCase):
    def test_deterministic_zip_and_no_traversal(self):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp) / 'a.zip', Path(tmp) / 'b.zip'
            package.write_zip(a, {'z': b'one', 'a': b'two'})
            package.write_zip(b, {'a': b'two', 'z': b'one'})
            self.assertEqual(a.read_bytes(), b.read_bytes())
            with zipfile.ZipFile(a) as archive:
                self.assertEqual({name: archive.read(name) for name in archive.namelist()},
                                 {'a': b'two', 'z': b'one'})
                self.assertIsNone(archive.testzip())
            with self.assertRaises(ValueError):
                package.write_zip(b, {'../secret': b''})

    def test_exefs_validation(self):
        strings = b'main\0main.npdm\0'
        header = b'PFS0' + struct.pack('<III', 2, len(strings), 0)
        table = struct.pack('<QQII', 0, 4, 0, 0) + struct.pack('<QQII', 4, 4, 5, 0)
        valid = header + table + strings + b'NSO0META'
        self.assertEqual(package.validate_pfs0(valid), {'main': b'NSO0', 'main.npdm': b'META'})
        for bad in (b'', valid[:-1], valid.replace(b'NSO0', b'xxxx'), valid.replace(b'main\0', b'xxxx\0')):
            with self.assertRaises(ValueError):
                package.validate_pfs0(bad)

    def test_program_id_and_version(self):
        npdm = bytearray(0xA0)
        npdm[:4] = b'META'
        struct.pack_into('<II', npdm, 0x70, 0x80, 0x20)
        npdm[0x80:0x84] = b'ACI0'
        struct.pack_into('<Q', npdm, 0x90, 0x00FF48474D000001)
        exefs = (b'PFS0' + struct.pack('<III', 2, 15, 0)
                 + struct.pack('<QQII', 0, 4, 0, 0)
                 + struct.pack('<QQII', 4, len(npdm), 5, 0)
                 + b'main\0main.npdm\0' + b'NSO0' + npdm)
        overlay = bytearray(0x80 + 0x38 + 0x4000)
        overlay[0x10:0x14] = b'NRO0'
        struct.pack_into('<I', overlay, 0x18, 0x80)
        overlay[0x80:0x84] = b'ASET'
        struct.pack_into('<QQ', overlay, 0x98, 0x38, 0x4000)
        overlay[0xB8 + 0x3060:0xB8 + 0x3065] = b'0.1.0'
        package.validate_binaries(exefs, overlay, '0.1.0')
        with self.assertRaises(ValueError):
            package.validate_binaries(exefs, overlay, '0.1.1')
        bad = exefs.replace(struct.pack('<Q', 0x00FF48474D000001), b'\0' * 8)
        with self.assertRaises(ValueError):
            package.validate_binaries(bad, overlay, '0.1.0')
        with self.assertRaises(ValueError):
            package.validate_binaries(exefs, overlay[:0x80], '0.1.0')

if __name__ == '__main__':
    unittest.main()
