# Dependency and reuse decisions

The initial build pins pioarduino 55.03.37 (Arduino-ESP32 3.3.7), matching the installed toolchain and retaining ESP-IDF native USB host support. This deliberately differs from fm1buddy 55.03.312: the selected transport uses ESP-IDF rather than TinyUSB. M5Unified 0.2.13, M5GFX 0.3.0, M5Cardputer 1.1.1, IRremote 5.0.0 and ArduinoJson 7.2.1 are pinned in platformio.ini; builds must validate these pins together.

ROM transport and USB boot key files in src/transport are copied from jvitkauskas/cardputer-synth-rescue revision 4dc12059c3ce971086ff3244aef26d7efd4d0b74. Preserve the upstream MIT license and THIRD-PARTY.md in third_party/cardputer-synth-rescue. Bundled M-Flasher loader is MIT; restart code is MIT-0. No fixed rescue images or upstream UI are linked. The adapted host adds cooperative cleanup, MIDI framing and descriptor/transfer bounds. Its IN buffer and loader reads are capped at 4 KiB, matching the application sector buffers and avoiding a redundant 12 KiB USB allocation.

Core package parsing and MIDI handshake/identity format follow fm1buddy revision 65b8760d1365e84e5bd799c7fa1d3e0d7c3b4fe2 (GPL-3.0-only), with provenance and original notices in src/core/package.cpp. Project code is distributed under GPL-3.0-only; preserve all imported MIT and other notices. No vendor firmware package is bundled.

Each board uses one 3 MiB application partition and a 4.9375 MiB filesystem on 8 MiB flash. There is no OTA app slot. Preserve backup/scratch reserves when reporting package capacity. Cardputer packages use SD.
