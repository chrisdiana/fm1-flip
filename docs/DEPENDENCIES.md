# Dependency and reuse decisions

The initial build pins pioarduino 55.03.37 (Arduino-ESP32 3.3.7), matching the installed toolchain and retaining ESP-IDF native USB host support. This deliberately differs from fm1buddy 55.03.312: the selected transport uses ESP-IDF rather than TinyUSB. M5Unified 0.2.13, M5GFX 0.3.0, M5Cardputer 1.1.1, IRremote 5.0.0 and ArduinoJson 7.2.1 are pinned in platformio.ini; builds must validate these pins together.

ROM transport and USB boot key files in src/transport are copied from jvitkauskas/cardputer-synth-rescue revision 4dc12059c3ce971086ff3244aef26d7efd4d0b74. Preserve the upstream MIT license and THIRD-PARTY.md in third_party/cardputer-synth-rescue. Bundled M-Flasher loader is MIT; restart code is MIT-0. No fixed rescue images or upstream UI are linked. The adapted host adds cooperative cleanup, MIDI framing and descriptor/transfer bounds. Its IN buffer and loader reads are capped at 4 KiB, matching the application sector buffers and avoiding a redundant 12 KiB USB allocation.

Core package parsing and MIDI handshake/identity format follow fm1buddy revision 65b8760d1365e84e5bd799c7fa1d3e0d7c3b4fe2 (GPL-3.0-only), with provenance and original notices in src/core/package.cpp. Project code is distributed under GPL-3.0-only; preserve all imported MIT and other notices. No vendor firmware package is bundled.

Each board uses one 3 MiB application partition and a 4.9375 MiB filesystem on 8 MiB flash. There is no OTA app slot. Preserve backup/scratch reserves when reporting package capacity. Cardputer packages use SD.

## USB enumeration SDK configuration

The same pinned pioarduino platform now uses its Arduino HybridCompile support to rebuild ESP-IDF 5.5.2 with `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=1024`. The precompiled default is 256 bytes and rejects larger composite configuration descriptors before the application's MIDI interface selection. Patchwerk's published USB layouts motivated this change. The app has a compile-time minimum-limit guard; `tools/check_usb_descriptor_limit.py` exercises the installed IDF parser under the stock and configured limits.

Explicit octal PSRAM settings for StickS3 and no-PSRAM settings for Cardputer distinguish the platform's SDK cache configurations. Cloud components/wrappers unused by FM-1 Flip are excluded, their stale Arduino logging-wrap flag is removed, and the unused speech-recognition wrapper is disabled to avoid selective-build dependency issues in the pinned toolchain. Generated SDK/CMake/component files stay out of Git. Source project options remain in `platformio.ini`.

If changing SDK options in an existing working tree, regenerate the target's generated `sdkconfig.<environment>` rather than retaining settings that override new defaults. The first custom SDK build can take several minutes and downloads additional build dependencies.

References: [ESP-IDF 5.5.2 enumeration implementation](https://github.com/espressif/esp-idf/blob/v5.5.2/components/usb/enum.c), [Patchwerk USB implementation](https://github.com/MattKuebrich/patchwerk/blob/027cb96b65f3744900e7925e06895b7da456cb7c/base/bsp/chip/usb.c), [pioarduino framework builder](https://github.com/pioarduino/platform-espressif32/tree/55.03.37/builder/frameworks).


Cardputer's rebuilt SDK uses asymmetric mbedTLS records (16 KiB incoming, 4 KiB outgoing) and dynamic TX/RX buffers. The IDF `components/mbedtls/CMakeLists.txt` interface linker wrappers are explicitly carried into the Arduino build because HybridCompile exports archives without those link options. `tools/check_tls_memory.py --elf .pio/build/cardputer/firmware.elf` verifies the policy and linked hooks. The default PEM trust text stays in flash; a user-supplied CA file remains supported. Certificate verification is unchanged.

HybridCompile also omits nested mbedTLS archives from its copy step. The project post-link hook exports rebuilt TLS/crypto/X.509/curve archives under Arduino's expected names before IDF cleanup. In particular, the nested TLS archive must be `libmbedtls_2.a`, while the top-level IDF component remains `libmbedtls.a`. Archive hashes in the ignored `.cache/tls-sdk-cardputer.json` are checked by the TLS validation command so new configuration cannot silently link stock TLS code. DTLS is disabled on Cardputer because the dynamic TLS implementation requires it; this application uses HTTPS, not DTLS.
