#!/usr/bin/env python3
"""Compile actual device catalogue parser with desktop adapters; never uses network.

Install firmware dependencies with PlatformIO first. --cache optionally checks an
existing Firmware Hub cache; the default fixture is synthetic and fully offline.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def json_headers():
    return next((ROOT / '.pio/libdeps').glob('*/ArduinoJson/src/ArduinoJson.h'), None)


def device_parser():
    source = (ROOT / 'src/device/catalogue.cpp').read_text()
    parser = source[source.index('String compileAssetPattern('):source.index('String normalize(')]
    parser += source[source.index('String normalize('):source.index('String get(')]
    parser += source[source.index('String scanCatalogue('):source.index('String refreshHub(')]
    # Only adapt Arduino String calls to their std::string equivalents. The
    # production tokenization, streaming, validation and normalization execute.
    replacements = {
        '.isEmpty()': '.empty()',
        'prefix.remove(prefix.length()-1)': 'prefix.erase(prefix.length()-1)',
        'release.startsWith(base)': '(release.rfind(base,0)==0)',
        "release.indexOf('?')<0": "release.find('?')==String::npos",
        "release.indexOf('#')<0": "release.find('#')==String::npos",
        'release.substring(base.length())': 'release.substr(base.length())',
        "actual.indexOf('/')<0": "actual.find('/')==String::npos",
    }
    for old, new in replacements.items():
        parser = parser.replace(old, new)
    return '#include "device_catalogue_adapter.hpp"\n' + parser + '\n#include "device_catalogue_main.hpp"\n'


def fixture():
    # 217 entries deliberately crosses both the former 96-entry cap and the
    # 16-entry web page; optional metadata crosses the old 16 KiB parser limit.
    return {'license': {'license': 'CC-BY-4.0'}, 'updated': '2026-11-15T00:00:00Z',
            'firmwares': [dict(id=f'firmware-{i}', name=f'Firmware {i}',
                repo='maker/project', author='Maker', what='Firmware description',
                release='https://github.com/maker/project/releases/'+('tag/v1' if i < 196 else 'latest'),
                tag='v1', assetPkg=r'firmware-[\d.]+\.fwsc', kind=['effect'], size=128, pkg='fw/package.fwsc', sha256='a'*64, card='fw/cards/test.w640.webp' if i==0 else None, installs=42 if i==0 else True if i==1 else -1 if i==2 else 2147483648 if i==3 else None)
                for i in range(217)], 'creators': {'ignored': 'x'*20000}}


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument('--cache', type=Path)
    options = args.parse_args()
    headers = json_headers()
    if not headers:
        raise SystemExit('ArduinoJson headers unavailable; run PlatformIO once to install firmware dependencies')
    raw = json.loads(options.cache.read_text())['catalogue'] if options.cache else fixture()
    with tempfile.TemporaryDirectory(prefix='fm1-device-parser-') as name:
        tmp = Path(name)
        (tmp / 'parser.cpp').write_text(device_parser())
        (tmp / 'source.json').write_text(json.dumps(raw))
        subprocess.run(['c++', '-std=c++17', '-I'+str(headers.parent), '-I'+str(ROOT/'test'),
                        str(tmp/'parser.cpp'), '-o', str(tmp/'parser')], check=True)
        subprocess.run([str(tmp/'parser'), str(tmp/'source.json'), str(len(raw['firmwares']))], check=True)


if __name__ == '__main__':
    main()
