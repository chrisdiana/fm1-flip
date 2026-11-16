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
