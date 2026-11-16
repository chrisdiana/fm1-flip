#pragma once
#include <cstddef>
#include <cstdint>

namespace fm1 {
constexpr size_t MaxSysExBytes = 512;
extern const uint8_t IdentityQuery[10];
extern const uint8_t RomEntryRequest[6];

// USB MIDI 1.0 cable zero. Returns zero for malformed SysEx or insufficient space.
size_t encodeUsbMidiSysEx(const uint8_t* wire, size_t length, uint8_t* output, size_t capacity);
enum class FeedResult { Ignored, Incomplete, Complete, Malformed };
class UsbMidiSysExDecoder {
 public:
  FeedResult feed(const uint8_t packet[4], uint8_t* output, size_t capacity, size_t& length);
  void reset();
 private:
  uint8_t bytes_[MaxSysExBytes]{};
  size_t used_ = 0;
  bool active_ = false;
};

struct FirmwareIdentity {
 char product[32]{};
 uint32_t version = 0;
 bool updateMode = false;
};
// FM-1 handshake response format, after fm1buddy 65b8760d (GPL-3.0-only).
// This is a protocol identity, not an authenticity/signature check.
bool parseFirmwareIdentity(const uint8_t* wire, size_t length, FirmwareIdentity& identity);
}
