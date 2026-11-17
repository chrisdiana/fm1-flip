from pathlib import Path
import importlib.util
import tempfile
import unittest
import hashlib
spec=importlib.util.spec_from_file_location('tls_sdk',Path(__file__).resolve().parents[1]/'tools/tls_sdk.py')
sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk)
class TlsSdkTests(unittest.TestCase):
    def test_exports_rebuilt_nested_archives_using_arduino_names(self):
        with tempfile.TemporaryDirectory() as name:
            root=Path(name);build=root/'build';dest=root/'sdk'
            self.assertEqual(sdk.export_archives(build,dest),{})
            for path in sdk.ARCHIVES:
                source=build/'esp-idf/mbedtls/mbedtls'/path
                source.parent.mkdir(parents=True,exist_ok=True);source.write_bytes(path.encode())
            hashes=sdk.export_archives(build,dest)
            self.assertEqual(set(hashes),set(sdk.ARCHIVES.values()))
            for path,target in sdk.ARCHIVES.items():
                self.assertEqual((dest/target).read_bytes(),path.encode())
                self.assertEqual(hashes[target],hashlib.sha256(path.encode()).hexdigest())
    def test_rejects_incomplete_sdk_before_copying(self):
        with tempfile.TemporaryDirectory() as name:
            root=Path(name);build=root/'build';dest=root/'sdk'
            (build/'esp-idf/mbedtls/mbedtls/library').mkdir(parents=True)
            with self.assertRaises(RuntimeError):sdk.export_archives(build,dest)
            self.assertFalse(dest.exists())
