#!/usr/bin/env python3
"""Validate the two universal release images; never read or flash a device."""
import hashlib
import json
import re
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BOARDS = ('sticks3', 'cardputer')


def verify_app(data, offset=0):
    """Validate ESP32-S3 segments, XOR checksum and appended SHA-256."""
    if offset + 24 > len(data) or data[offset] != 0xE9:
        raise ValueError('Missing ESP application header')
    if struct.unpack_from('<H', data, offset + 12)[0] != 9:
        raise ValueError('Application is not built for ESP32-S3')
    segments = data[offset + 1]
    if not 1 <= segments <= 16:
        raise ValueError('Invalid segment count')
    cursor, checksum = offset + 24, 0xEF
    for _ in range(segments):
        if cursor + 8 > len(data):
            raise ValueError('Truncated segment header')
        length = struct.unpack_from('<I', data, cursor + 4)[0]
        cursor += 8
        if cursor + length > len(data):
            raise ValueError('Truncated segment')
        for byte in data[cursor:cursor + length]:
            checksum ^= byte
        cursor += length
    checksum_at = offset + ((cursor - offset) | 15)
    if checksum_at >= len(data) or data[checksum_at] != checksum:
        raise ValueError('Invalid application checksum')
    end = checksum_at + 1
    if data[offset + 23] != 1 or end + 32 > len(data):
        raise ValueError('Missing application SHA-256')
    if hashlib.sha256(data[offset:end]).digest() != data[end:end + 32]:
        raise ValueError('Invalid application SHA-256')
    return end + 32 - offset


def verify_merged(data, app, requires_storage=True):
    if len(data) < 0x10000:
        raise ValueError('Merged image requires bootloader, partition table and app')
    verify_app(data, 0)
    table = data[0x8000:0x9000]
    entries, table_end = [], None
    for position in range(0, len(table), 32):
        magic = struct.unpack_from('<H', table, position)[0]
        if magic == 0xEBEB:
            if hashlib.md5(table[:position]).digest() != table[position + 16:position + 32]:
                raise ValueError('Invalid partition-table MD5')
            table_end = position
            break
        if magic != 0x50AA:
            raise ValueError('Invalid partition-table entry')
        _, kind, subtype, start, size, label, _ = struct.unpack_from('<HBBII16sI', table, position)
        entries.append(dict(type=kind, subtype=subtype, offset=start, size=size,
                            label=label.split(b'\0')[0].decode()))
    if table_end is None:
        raise ValueError('Missing partition-table MD5')
    apps = [entry for entry in entries if entry['type'] == 0]
    storage = [entry for entry in entries if entry['type'] == 1 and entry['label'] == 'spiffs']
    if len(apps) != 1 or apps[0]['offset'] != 0x10000 or len(storage) != int(requires_storage):
        raise ValueError('Unexpected application/storage layout')
    if data[0x10000:0x10000 + len(app)] != app:
        raise ValueError('Merged application differs from this build')
    verify_app(data, 0x10000)
    if any(byte != 0xFF for byte in data[0x9000:0xE000]):
        raise ValueError('NVS area is not erased: do not package device settings')
    # Deliberately no runtime filesystem dump: Launcher creates this partition.
    if requires_storage and len(data) > storage[0]['offset']:
        raise ValueError('Unexpected filesystem payload in clean merged build')
    return storage[0] if storage else None


def main(boards=BOARDS):
    releases = ROOT / 'build/releases'
    previous_path = releases / 'manifest.json'
    previous = json.loads(previous_path.read_text()) if previous_path.exists() else {}
    subprocess.run([sys.executable, str(ROOT / 'tools/package_release.py'), *boards], check=True)
    source_manifest = json.loads((releases / 'manifest.json').read_text())
    output = releases
    # Remove obsolete generated release variants; publish exactly two .bin files.
    for pattern in ('fm1-flip-*-factory.bin', 'fm1-switcher-*-factory.bin'):
        for old in output.glob(pattern):
            old.unlink()
    (output / 'fm1-flip-m5launcher.zip').unlink(missing_ok=True)
    legacy = output / 'm5launcher'
    if legacy.is_dir():
        import shutil
        shutil.rmtree(legacy)
    manifest = {'project': 'FM-1 Flip', 'formatVersion': 1,
                'launcherHardwareValidated': False, 'images': previous.get('images', {})}
    for board in boards:
        app = (ROOT / '.pio/build' / board / 'firmware.bin').read_bytes()
        verify_app(app)
        factory_name = f'fm1-flip-{board}.bin'
        data = (releases / factory_name).read_bytes()
        storage = verify_merged(data, app, requires_storage=board == 'sticks3')
        for marker in (str(Path.home()).encode(), b'/Users/', b'/home/'):
            if marker in data:
                raise ValueError(f'{board}: private build path/key marker in release image; rebuild with path mapping')
        # TLS libraries contain the literal PEM header as parser code. Reject
        # encoded key material, rather than that harmless delimiter by itself.
        if re.search(rb'-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----\r?\n[A-Za-z0-9+/=\r\n]{32,}-----END ', data):
            raise ValueError(f'{board}: private key material in release image')
        name = f'fm1-flip-{board}.bin'
        (output / name).write_bytes(data)
        manifest['images'][name] = {
            'board': board, 'supportedBoards': ['cardputer', 'cardputer-adv'] if board == 'cardputer' else ['sticks3'],
            'chip': 'esp32s3', 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest(),
            'sourceSha256': source_manifest[factory_name]['sourceSha256'],
            'imageFormat': 'merged',
            'flashOffset': 0, 'appOffsetInFile': 0x10000,
            'appBytes': len(app),
            'storage': {'label': storage['label'], 'subtype': storage['subtype'],
                        'allocation': 'remaining free space; managed by Launcher',
                        'payloadIncluded': False, 'firstUse': 'Initialize through Home > Storage & export'}
                        if storage else {'type': 'FAT32 microSD', 'internalDataPartitionRequired': False},
        }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'SHA256SUMS').write_text(''.join(
        f"{entry['sha256']}  {name}\n" for name, entry in manifest['images'].items()))
    (output / 'README.md').write_text((ROOT / 'docs/M5LAUNCHER.md').read_text())
    print('Release images:', output.relative_to(ROOT))
    for name, entry in manifest['images'].items():
        print(name, entry['bytes'], 'bytes;', entry['imageFormat'])


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('boards', nargs='*', choices=BOARDS, default=list(BOARDS))
    main(tuple(parser.parse_args().boards))
