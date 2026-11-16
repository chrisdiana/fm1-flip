#!/usr/bin/env python3
"""Run host boundary tests without installing any Python dependencies."""
import pathlib, subprocess, tempfile, sys
root = pathlib.Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', 'test', '-p', 'test_*.py', '-v'], cwd=root, check=True)
with tempfile.TemporaryDirectory(prefix='fm1-check-') as tmp:
    for name, sources in [('gallery', ['test/test_gallery.cpp']), ('core', ['src/core/package.cpp', 'test/test_core.cpp']), ('confirmation', ['test/test_confirmation.cpp']), ('midi', ['src/core/midi.cpp', 'test/test_midi.cpp']), ('writer', ['src/core/package.cpp', 'src/core/writer.cpp', 'test/test_writer.cpp'])]:
        if not all((root / source).exists() for source in sources):
            raise SystemExit('Missing required test sources for ' + name)
        binary = str(pathlib.Path(tmp) / name)
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Iinclude', *sources, '-o', binary], cwd=root, check=True)
        subprocess.run([binary], cwd=root, check=True)
        print(name + ' boundary tests passed', flush=True)

# The real device parser needs the same ArduinoJson headers as the firmware.
# The fixture is generated offline; no Hub request is made by this check.
if next((root / '.pio/libdeps').glob('*/ArduinoJson/src/ArduinoJson.h'), None):
    subprocess.run([sys.executable, 'tools/check_device_catalogue.py'], cwd=root, check=True)
else:
    print('Device catalogue parser test: install PlatformIO firmware dependencies, then run tools/check_device_catalogue.py', flush=True)

headers = next((root / '.pio/libdeps').glob('*/ArduinoJson/src/ArduinoJson.h'), None)
if headers:
    with tempfile.TemporaryDirectory(prefix='fm1-library-check-') as name:
        binary = str(pathlib.Path(name) / 'library')
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Iinclude', '-I'+str(headers.parent), 'test/test_library.cpp', '-o', binary], cwd=root, check=True)
        subprocess.run([binary], cwd=root, check=True)
        print('Saved library projection tests passed', flush=True)

subprocess.run([sys.executable, 'tools/check_presentation.py'], cwd=root, check=True)

subprocess.run([sys.executable, 'tools/check_transport_flow.py'], cwd=root, check=True)

subprocess.run([sys.executable, 'tools/check_navigation.py'], cwd=root, check=True)

subprocess.run([sys.executable, 'tools/check_local_search.py'], cwd=root, check=True)
