// USB host side of the JieLi ROM download mode: the ESP32-S3's USB host on the
// Cardputer's USB-C port, and the mass storage Bulk-Only Transport the ROM
// (WL82 UBOOT1.00, 4C4A:8057) and M-Flasher's loader speak.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace host {

enum Result { OK = 0, REFUSED = 1, FAILED = 2 };

// Installs the USB host on the internal PHY (takes GPIO19/20 from the app).
// Idempotent after a successful start; cleans partial allocations on failure.
bool start();
// Call exclusively from the transport worker between complete commands. Stops
// event tasks cooperatively, releases USB/PHY resources, and resets enumeration
// state so usbkey::init() can begin another ROM boot session. A false return
// means cleanup is incomplete: require an M5 restart and do not reuse USB.
// Software waits are bounded; ESP-IDF resource APIs retain their own semantics.
bool stop();
typedef void (*Log)(const char *fmt, ...);

// Waits for a device to enumerate; fills vid/pid. Claims its bulk interface.
// Logs the root port's state as it changes, and ESP-IDF's own USB messages.
bool wait_device(uint32_t timeout_ms, uint16_t *vid, uint16_t *pid, Log log, char *err, size_t errlen);
// Wait for the current device to disconnect and claim its replacement without
// tearing down the host/root port (MIDI -> ROM and ROM -> firmware handoffs).
bool wait_reconnect(uint32_t timeout_ms, uint16_t *vid, uint16_t *pid, Log log, char *err, size_t errlen);
// Electrical root-port presence only; does not establish device identity.
bool port_attached();
bool connected();
// USB-MIDI 1.0 interface (Audio class 1 / MIDIStreaming subclass 3).
// These APIs share the same exclusive worker as execute(); replies are capped
// at 512 SysEx bytes and malformed USB MIDI framing fails closed.
bool is_midi();
bool midi_send_sysex(const uint8_t* bytes, size_t length, char* err, size_t errlen);
bool midi_receive_sysex(uint8_t* bytes, size_t capacity, uint32_t* length,
                        uint32_t timeout_ms, char* err, size_t errlen);

// One Bulk-Only Transport command. in: data from the device into buf.
// moved: data bytes actually transferred. err: text for a non-OK result.
Result execute(const uint8_t *cdb, uint8_t cdb_len, uint8_t *buf, uint32_t len, bool in,
               uint32_t *moved, char *err, size_t errlen);

}  // namespace host
