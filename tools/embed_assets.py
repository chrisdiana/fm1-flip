#!/usr/bin/env python3
"""Embed the local web UI; also usable as a PlatformIO pre-script."""
from pathlib import Path
if "__file__" in globals():
    root = Path(__file__).resolve().parents[1]
else:  # PlatformIO runs pre-scripts through SCons without __file__.
    Import("env")
    root = Path(env.subst("$PROJECT_DIR"))
source = (root / "web/index.html").read_text()
output = '#pragma once\n#include <pgmspace.h>\nstatic const char WEB_INDEX[] PROGMEM = R"FM1WEB(' + source + ')FM1WEB";\n'
target = root / "include/device/web_asset.h"
if not target.exists() or target.read_text() != output:
    target.write_text(output)

# __FILE__ appears in runtime diagnostics too: do not publish the builder's
# home directory in firmware images. GCC maps both file strings and debug paths.
if 'env' in globals():
    env.Append(CCFLAGS=[
        '-ffile-prefix-map=' + str(Path.home()) + '=/local',
        '-ffile-prefix-map=' + str(root) + '=.',
    ])

if 'env' in globals():
    import importlib.util
    spec = importlib.util.spec_from_file_location('fm1_tls_sdk', root / 'tools/tls_sdk.py')
    tls_sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tls_sdk)

    def export_rebuilt_tls(source, target, env, exporter=tls_sdk.export_archives):
        import json
        build = Path(env.subst('$BUILD_DIR'))
        sdk = Path(env.PioPlatform().get_package_dir('framework-arduinoespressif32-libs'))
        chip = env.BoardConfig().get('build.mcu')
        hashes = exporter(build, sdk / chip / 'lib')
        if hashes:
            cache = Path(env.subst('$PROJECT_DIR')) / '.cache'
            cache.mkdir(exist_ok=True)
            (cache / ('tls-sdk-' + env.subst('$PIOENV') + '.json')).write_text(json.dumps(hashes, indent=2) + '\n')
            print('Exported five rebuilt TLS archives matching this SDK configuration')

    # Runs after IDF has built all archives, before HybridCompile deletes them.
    # In a regular Arduino app build the nested IDF directory is absent.
    env.AddPostAction('$BUILD_DIR/${PROGNAME}.elf', export_rebuilt_tls)
