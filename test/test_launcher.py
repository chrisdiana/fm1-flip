"""Release format validation uses synthetic data and never contacts hardware."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch
import tempfile
import json

spec = importlib.util.spec_from_file_location('package_launcher', Path(__file__).resolve().parents[1] / 'tools/package_launcher.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


def app_image():
    header = bytearray(24)
    header[0:2] = bytes([0xE9, 1])
    struct.pack_into('<H', header, 12, 9)
    header[23] = 1
    image = header + struct.pack('<II', 0x3C000000, 4) + b'abcd'
    checksum = 0xEF
    for value in b'abcd':
        checksum ^= value
    image += bytes(((len(image) | 15) - len(image))) + bytes([checksum])
    return bytes(image + hashlib.sha256(image).digest())


def merged_image(app, storage=True):
    data = bytearray(b'\xff' * (0x10000 + len(app)))
    data[:len(app)] = app  # Synthetic checksum-valid ESP32-S3 bootloader.
    table = b''
    for kind, subtype, offset, size, name in [(1, 2, 0x9000, 0x5000, b'nvs'), (1, 1, 0xE000, 0x1000, b'phy_init'), (0, 0, 0x10000, 0x300000, b'app0'), (1, 0x82, 0x310000, 0x4F0000, b'spiffs')]:
        if name == b'spiffs' and not storage:
            continue
        table += struct.pack('<HBBII16sI', 0x50AA, kind, subtype, offset, size, name, 0)
    table += b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(table).digest()
    data[0x8000:0x8000 + len(table)] = table
    data[0x10000:] = app
    return data


class LauncherPackageTests(unittest.TestCase):
    def test_valid_app_and_merged_layout(self):
        app = app_image()
        self.assertEqual(pack.verify_app(app), len(app))
        self.assertEqual(pack.verify_merged(merged_image(app), app)['label'], 'spiffs')

    def test_cardputer_is_merged_without_internal_library_partition(self):
        app = app_image()
        merged = merged_image(app, storage=False)
        self.assertIsNone(pack.verify_merged(merged, app, requires_storage=False))
        with self.assertRaisesRegex(ValueError, 'layout'):
            pack.verify_merged(merged, app)

    def test_release_has_only_two_universal_binaries_and_no_zip(self):
        with tempfile.TemporaryDirectory(prefix='fm1-release-') as name:
            root = Path(name)
            release = root / 'build/releases'
            release.mkdir(parents=True)
            (root / 'docs').mkdir()
            (root / 'docs/M5LAUNCHER.md').write_text('Install guide')
            app = app_image()
            source_manifest = {}
            for board in ('cardputer', 'sticks3'):
                build = root / '.pio/build' / board
                build.mkdir(parents=True)
                (build / 'firmware.bin').write_bytes(app)
                filename = f'fm1-flip-{board}.bin'
                (release / filename).write_bytes(merged_image(app, storage=board == 'sticks3'))
                source_manifest[filename] = {'sourceSha256': 'fixture'}
            (release / 'manifest.json').write_text(json.dumps(source_manifest))
            (release / 'fm1-flip-cardputer-factory.bin').write_bytes(b'old')
            (release / 'fm1-flip-m5launcher.zip').write_bytes(b'old')
            (release / 'm5launcher').mkdir()
            with patch.object(pack, 'ROOT', root), patch.object(pack.subprocess, 'run'):
                pack.main()
            self.assertEqual(sorted(p.name for p in release.glob('*.bin')),
                             ['fm1-flip-cardputer.bin', 'fm1-flip-sticks3.bin'])
            self.assertFalse(list(release.rglob('*.zip')))
            manifest = json.loads((release / 'manifest.json').read_text())
            for image in manifest['images'].values():
                self.assertEqual(image['flashOffset'], 0)
                self.assertEqual(image['appOffsetInFile'], 0x10000)
                self.assertEqual(image['imageFormat'], 'merged')

            saved_stick = manifest['images']['fm1-flip-sticks3.bin'].copy()
            def merge_selected(*args, **kwargs):
                (release / 'manifest.json').write_text(json.dumps({
                    'fm1-flip-cardputer.bin': {'sourceSha256': 'cardputer-update'}
                }))
            with patch.object(pack, 'ROOT', root), patch.object(pack.subprocess, 'run', side_effect=merge_selected):
                pack.main(('cardputer',))
            updated = json.loads((release / 'manifest.json').read_text())
            self.assertEqual(updated['images']['fm1-flip-sticks3.bin'], saved_stick)
            self.assertEqual(updated['images']['fm1-flip-cardputer.bin']['sourceSha256'], 'cardputer-update')

    def test_reject_app_only_for_standalone_release(self):
        app = app_image()
        with self.assertRaisesRegex(ValueError, 'bootloader'):
            pack.verify_merged(app, app, requires_storage=False)

    def test_reject_missing_bootloader(self):
        app = app_image()
        merged = merged_image(app)
        merged[0] = 0xFF
        with self.assertRaisesRegex(ValueError, 'header'):
            pack.verify_merged(merged, app)

    def test_reject_wrong_chip(self):
        app = bytearray(app_image()); app[12] = 0
        with self.assertRaisesRegex(ValueError, 'ESP32-S3'):
            pack.verify_app(app)

    def test_reject_truncated_segment(self):
        with self.assertRaises(ValueError):
            pack.verify_app(app_image()[:34])

    def test_reject_checksum(self):
        app = bytearray(app_image()); app[32] ^= 1
        with self.assertRaisesRegex(ValueError, 'checksum'):
            pack.verify_app(app)

    def test_reject_digest(self):
        app = bytearray(app_image()); app[-1] ^= 1
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            pack.verify_app(app)

    def test_reject_partition_corruption(self):
        app = app_image(); merged = merged_image(app); merged[0x8010] ^= 1
        with self.assertRaisesRegex(ValueError, 'MD5'):
            pack.verify_merged(merged, app)

    def test_reject_device_nvs(self):
        app = app_image(); merged = merged_image(app); merged[0x9000] = 1
        with self.assertRaisesRegex(ValueError, 'NVS'):
            pack.verify_merged(merged, app)

    def test_reject_filesystem_dump(self):
        app = app_image(); merged = merged_image(app)
        merged += b'\xff' * (0x310001 - len(merged))
        with self.assertRaisesRegex(ValueError, 'filesystem payload'):
            pack.verify_merged(merged, app)


if __name__ == '__main__':
    unittest.main()
