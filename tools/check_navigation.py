#!/usr/bin/env python3
"""Exercise production Home/Back navigation without a display or hardware."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/device/local_app.inc').read_text()
app=(root/'src/device/app.cpp').read_text()
enums=source[source.index('enum class LocalPage'):source.index('struct WifiChoice')]
functions=source[source.index('void setMainTab('):source.index('void openEditor(')]
functions+=source[source.index('void showHome() {'):source.index('void moveLocal(')]
functions=functions.replace('.isEmpty()', '.empty()')
startup=next(line for line in app.splitlines() if line.startswith('bool actionMenu='))
with tempfile.TemporaryDirectory(prefix='fm1-navigation-') as name:
    path=Path(name)
    (path/'main.cpp').write_text('#include <string>\n#include <cassert>\n#include <cstdint>\n#include <iostream>\nusing String=std::string;\n'+enums+'\n'+startup+'\n'+(root/'test/navigation_adapter.hpp').read_text()+'\n'+functions+'\n'+(root/'test/navigation_main.hpp').read_text())
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(path/'main.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
