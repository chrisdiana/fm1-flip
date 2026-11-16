#pragma once

#include <cstddef>
#include <cstdint>

namespace fm1 {

// Reader::read succeeds only when all requested bytes were read. Implementations
// must return false on EOF, I/O failure, or a short read.
class Reader {
 public:
  virtual ~Reader() = default;
  virtual uint64_t size() const = 0;
  virtual bool read(uint64_t offset, uint8_t* destination, size_t length) = 0;
};

enum class DeviceModel : uint8_t { Fm1, Smk37 };

enum class PackageError : uint8_t {
  None,
  TooSmall,
  TooLarge,
  ReadFailure,
  InvalidIdentity,
  WrongDevice,
  InvalidPackage,
  InvalidFlashImage,
  UnsafeFlashRegion,
};

struct PackageInfo {
  char product[64]{};       // package identity, e.g. "FM-1_015"
  char model[48]{};         // model without version, e.g. "FM-1"
  uint32_t version = 0;
  uint64_t rawSize = 0;
  uint64_t logicalSize = 0; // .fwsc with interleave marker bytes removed
  uint8_t interleaveBlocks = 0;
  uint32_t flashOffset = 0; // offset in logical .fwsc container of flash.bin
  uint32_t flashSize = 0;   // length of image bytes, addressed from target flash address 0
};

// Parses and checks the complete upstream .fwsc container without allocating
// the package. Includes header, file-table, flash.bin CRC checks and target
// identity validation.
PackageError validatePackage(Reader& reader, DeviceModel target, PackageInfo& out);

// Reads bytes from flash.bin by target flash address. It never pads absent
// payload bytes; callers must not write sectors that are not fully present.
PackageError readFlashImage(Reader& reader, const PackageInfo& package,
                            uint32_t address, uint8_t* destination, size_t length);

// FM-1 flash writes are sector based. Only aligned 4 KiB sectors fully inside
// [0x4000, 0xFF000) are writable. This protects the boot region and the final
// 4 KiB calibration/identity region.
bool writableSector(uint32_t address, uint32_t length);

const char* errorMessage(PackageError error);

}  // namespace fm1
