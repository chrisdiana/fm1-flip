#!/usr/bin/env python3
"""Replay Cardputer's cross-file global initialization order with the real sprite declaration."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/device/local_app.inc').read_text()
declaration=next(line for line in s.splitlines() if line.startswith('M5Canvas screenBuffer'))
with tempfile.TemporaryDirectory(prefix='fm1-display-startup-') as name:
 p=Path(name)
 (p/'main.cpp').write_text('''#include <cassert>
#include <iostream>
struct Display {};
struct Unified { Display Display; };
Unified M5;
struct Cardputer {struct Display& Display;Cardputer():Display(M5.Display){}};
extern Cardputer M5Cardputer;
#define DISPLAY M5Cardputer.Display
struct M5Canvas {struct Display* parent;explicit M5Canvas(struct Display* p):parent(p){}void present(){assert(parent&&"Cardputer display reference was read before its constructor");}};
'''+declaration+'''
// Library globals may be constructed after the application's globals.
Cardputer M5Cardputer;
int main(){screenBuffer.present();assert(screenBuffer.parent==&M5.Display);std::cout<<"Display startup: first frame survives late Cardputer global initialization\\n";}
''')
 subprocess.run(['c++','-std=c++17',str(p/'main.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
