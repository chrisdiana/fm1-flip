#pragma once

#include "fm1/package.hpp"

namespace fm1 {

class Flash {
 public:
  virtual ~Flash() = default;
  virtual uint32_t size() const = 0;
  virtual bool read(uint32_t address, uint8_t* destination, size_t length) = 0;
  virtual bool eraseSector(uint32_t address) = 0;
  virtual bool program(uint32_t address, const uint8_t* source, size_t length) = 0;
};

enum class WriteError : uint8_t {
  None,
  Cancelled,
  InvalidPackage,
  WrongDevice,
  InvalidFlash,
  InvalidBackup,
  BackupMismatch,
  ReadFailure,
  UnsafeSector,
  EraseFailure,
  ProgramFailure,
  ReadbackMismatch,
  ProtectedRegionMismatch,
};

struct WriteReport {
  WriteError error = WriteError::None;
  PackageError packageError = PackageError::None;
  uint32_t address = 0;
  uint32_t changedSectors = 0;
};

// Cancellation is checked between sectors, never during erase/program/readback.
using WriteCancel = bool (*)(void* context);

using WriteProgress = void (*)(uint32_t completed, uint32_t total, uint32_t address, void* context);

// expectedBackupCrc is the verified FM-1 CRC32 recorded when backup was read.
// Both the stored backup and the live target must match it before any erase.
WriteReport writePackage(Flash& flash, Reader& package, DeviceModel target,
                         Reader& verifiedBackup, uint32_t expectedBackupCrc,
                         WriteProgress progress = nullptr, void* context = nullptr, WriteCancel cancel = nullptr);

// Install without retaining a backup. Validates the package before erasing,
// checks all replacement reads, compares live sectors, verifies readback, and
// checks boot/calibration sector CRCs captured in RAM. No filesystem is needed.
WriteReport writePackageWithoutBackup(Flash& flash, Reader& package, DeviceModel target,
                                     WriteProgress progress = nullptr, void* context = nullptr, WriteCancel cancel = nullptr);

// Both backups must be exactly 1 MiB and match their recorded complete CRC32.
// Live flash must match currentVerifiedBackup. Protected desired bytes must
// equal the current backup before any erase. Readers must remain immutable.
// Restores changed writable sectors only, verifies readback, and never restarts.
WriteReport restoreBackup(Flash& flash, Reader& desiredBackup, Reader& currentVerifiedBackup,
                          uint32_t desiredCrc, uint32_t currentCrc,
                          WriteProgress progress = nullptr, void* context = nullptr, WriteCancel cancel = nullptr);

const char* errorMessage(WriteError error);

}  // namespace fm1
