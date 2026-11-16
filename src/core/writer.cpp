#include "fm1/writer.hpp"

#include <algorithm>
#include <cstring>

namespace fm1 {
namespace {
constexpr uint32_t kFlashBytes = 0x100000;
constexpr uint32_t kSectorBytes = 0x1000;
constexpr uint32_t kFirstWritable = 0x4000;
constexpr uint32_t kLastWritable = 0xff000;

uint32_t crc32Update(uint32_t crc, const uint8_t* bytes, size_t length) {
  // Matches m-bootloader's IEEE CRC32 convention, including complementing the
  // prior/final value so calls can be streamed one bounded buffer at a time.
  crc = ~crc;
  while (length--) {
    crc ^= *bytes++;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 1) ? 0xedb88320u ^ (crc >> 1) : crc >> 1;
  }
  return ~crc;
}

bool crcReader(Reader& reader, uint32_t& crc) {
  uint8_t buffer[512];
  crc = 0;
  for (uint32_t offset = 0; offset < reader.size();) {
    const size_t count = size_t(std::min<uint64_t>(sizeof buffer, reader.size() - offset));
    if (!reader.read(offset, buffer, count)) return false;
    crc = crc32Update(crc, buffer, count);
    offset += uint32_t(count);
  }
  return true;
}

bool crcFlash(Flash& flash, uint32_t& crc) {
  uint8_t buffer[512];
  crc = 0;
  for (uint32_t offset = 0; offset < flash.size(); offset += sizeof buffer) {
    if (!flash.read(offset, buffer, sizeof buffer)) return false;
    crc = crc32Update(crc, buffer, sizeof buffer);
  }
  return true;
}

WriteReport fail(WriteError error, uint32_t address = 0, PackageError packageError = PackageError::None) {
  return {error, packageError, address, 0};
}
}  // namespace

static WriteReport writePackageImpl(Flash& flash, Reader& packageReader, DeviceModel target,
                                    Reader* verifiedBackup, uint32_t expectedBackupCrc,
                                    WriteProgress progress, void* context) {
  if (flash.size() != kFlashBytes) return fail(WriteError::InvalidFlash);
  if (verifiedBackup && verifiedBackup->size() != kFlashBytes) return fail(WriteError::InvalidBackup);

  PackageInfo packageInfo;
  PackageError packageError = validatePackage(packageReader, target, packageInfo);
  if (packageError != PackageError::None) {
    return fail(packageError == PackageError::WrongDevice ? WriteError::WrongDevice : WriteError::InvalidPackage,
                0, packageError);
  }

  if (verifiedBackup) {
    uint32_t backupCrc = 0, currentCrc = 0;
    if (!crcReader(*verifiedBackup, backupCrc)) return fail(WriteError::ReadFailure);
    if (backupCrc != expectedBackupCrc) return fail(WriteError::BackupMismatch);
    if (!crcFlash(flash, currentCrc)) return fail(WriteError::ReadFailure);
    if (currentCrc != expectedBackupCrc) return fail(WriteError::BackupMismatch);
  }

  const uint32_t end = std::min<uint32_t>(packageInfo.flashSize, kLastWritable);
  const uint32_t total = end > kFirstWritable ? end - kFirstWritable : 0;
  uint8_t current[kSectorBytes], desired[kSectorBytes], source[kSectorBytes];
  uint32_t protectedCrc[5]{};
  if (!verifiedBackup) {
    size_t index = 0;
    for (uint32_t address = 0; address < kFlashBytes; address += kSectorBytes) {
      if (writableSector(address, kSectorBytes)) continue;
      if (!flash.read(address, current, sizeof current)) return fail(WriteError::ReadFailure, address);
      protectedCrc[index++] = crc32Update(0, current, sizeof current);
    }
  }

  // Validate every package byte that the operation may use before the first
  // erase. Each write still reads its sector before erasing, so a later I/O
  // failure cannot erase that sector without having a complete replacement.
  for (uint32_t address = kFirstWritable; address < end; address += kSectorBytes) {
    if (!writableSector(address, kSectorBytes)) return fail(WriteError::UnsafeSector, address);
    const size_t count = std::min<uint32_t>(kSectorBytes, end - address);
    if (readFlashImage(packageReader, packageInfo, address, source, count) != PackageError::None)
      return fail(WriteError::ReadFailure, address);
    if (!flash.read(address, current, sizeof current)) return fail(WriteError::ReadFailure, address);
  }

  uint32_t changed = 0;
  for (uint32_t address = kFirstWritable; address < end; address += kSectorBytes) {
    const size_t count = std::min<uint32_t>(kSectorBytes, end - address);
    if (!flash.read(address, current, sizeof current)) return fail(WriteError::ReadFailure, address);
    memcpy(desired, current, sizeof desired);
    if (readFlashImage(packageReader, packageInfo, address, desired, count) != PackageError::None)
      return fail(WriteError::ReadFailure, address);
    if (memcmp(current, desired, sizeof current) != 0) {
      if (!writableSector(address, kSectorBytes)) return fail(WriteError::UnsafeSector, address);
      if (!flash.eraseSector(address)) return fail(WriteError::EraseFailure, address);
      if (!flash.program(address, desired, sizeof desired)) return fail(WriteError::ProgramFailure, address);
      if (!flash.read(address, current, sizeof current)) return fail(WriteError::ReadFailure, address);
      if (memcmp(current, desired, sizeof current) != 0) return fail(WriteError::ReadbackMismatch, address);
      ++changed;
    }
    if (progress) progress(std::min<uint32_t>(address + kSectorBytes - kFirstWritable, total), total,
                           address, context);
  }

  // Check every boot/calibration sector against its pre-write snapshot.
  size_t protectedIndex = 0;
  for (uint32_t address = 0; address < kFlashBytes; address += kSectorBytes) {
    if (writableSector(address, kSectorBytes)) continue;
    if (!flash.read(address, current, sizeof current)) return fail(WriteError::ReadFailure, address);
    if (verifiedBackup) {
      if (!verifiedBackup->read(address, desired, sizeof desired)) return fail(WriteError::ReadFailure, address);
      if (memcmp(current, desired, sizeof current)) return fail(WriteError::ProtectedRegionMismatch, address);
    } else if (crc32Update(0, current, sizeof current) != protectedCrc[protectedIndex++]) {
      return fail(WriteError::ProtectedRegionMismatch, address);
    }
  }
  return {WriteError::None, PackageError::None, 0, changed};
}

WriteReport writePackage(Flash& flash, Reader& packageReader, DeviceModel target,
                         Reader& verifiedBackup, uint32_t expectedBackupCrc,
                         WriteProgress progress, void* context) {
  return writePackageImpl(flash, packageReader, target, &verifiedBackup, expectedBackupCrc, progress, context);
}

WriteReport writePackageWithoutBackup(Flash& flash, Reader& packageReader, DeviceModel target,
                                     WriteProgress progress, void* context) {
  return writePackageImpl(flash, packageReader, target, nullptr, 0, progress, context);
}

WriteReport restoreBackup(Flash& flash, Reader& desiredBackup, Reader& currentVerifiedBackup,
                          uint32_t desiredCrc, uint32_t currentCrc,
                          WriteProgress progress, void* context) {
  if (flash.size() != kFlashBytes) return fail(WriteError::InvalidFlash);
  if (desiredBackup.size() != kFlashBytes || currentVerifiedBackup.size() != kFlashBytes)
    return fail(WriteError::InvalidBackup);
  uint32_t actual = 0;
  if (!crcReader(desiredBackup, actual)) return fail(WriteError::ReadFailure);
  if (actual != desiredCrc) return fail(WriteError::BackupMismatch);
  if (!crcReader(currentVerifiedBackup, actual)) return fail(WriteError::ReadFailure);
  if (actual != currentCrc) return fail(WriteError::BackupMismatch);
  if (!crcFlash(flash, actual)) return fail(WriteError::ReadFailure);
  if (actual != currentCrc) return fail(WriteError::BackupMismatch);

  uint8_t current[kSectorBytes], desired[kSectorBytes];
  // Check every protected byte before making any change. A valid backup from
  // a different unit is not a license to overwrite this unit's boot/identity.
  for (uint32_t address = 0; address < kFlashBytes; address += kSectorBytes) {
    if (writableSector(address, kSectorBytes)) continue;
    if (!desiredBackup.read(address, desired, sizeof desired) ||
        !currentVerifiedBackup.read(address, current, sizeof current))
      return fail(WriteError::ReadFailure, address);
    if (memcmp(desired, current, sizeof current))
      return fail(WriteError::ProtectedRegionMismatch, address);
  }
  uint32_t changed = 0;
  const uint32_t total = kLastWritable - kFirstWritable;
  for (uint32_t address = kFirstWritable; address < kLastWritable; address += kSectorBytes) {
    // Obtain a complete replacement before erasing this sector.
    if (!desiredBackup.read(address, desired, sizeof desired) ||
        !flash.read(address, current, sizeof current))
      return {WriteError::ReadFailure, PackageError::None, address, changed};
    if (memcmp(current, desired, sizeof current)) {
      if (!flash.eraseSector(address))
        return {WriteError::EraseFailure, PackageError::None, address, changed};
      if (!flash.program(address, desired, sizeof desired))
        return {WriteError::ProgramFailure, PackageError::None, address, changed};
      if (!flash.read(address, current, sizeof current))
        return {WriteError::ReadFailure, PackageError::None, address, changed};
      if (memcmp(current, desired, sizeof current))
        return {WriteError::ReadbackMismatch, PackageError::None, address, changed};
      ++changed;
    }
    if (progress) progress(address + kSectorBytes - kFirstWritable, total, address, context);
  }
  // Also detect an adapter/device fault which changes a protected sector.
  for (uint32_t address = 0; address < kFlashBytes; address += kSectorBytes) {
    if (writableSector(address, kSectorBytes)) continue;
    if (!currentVerifiedBackup.read(address, desired, sizeof desired) ||
        !flash.read(address, current, sizeof current))
      return {WriteError::ReadFailure, PackageError::None, address, changed};
    if (memcmp(desired, current, sizeof current))
      return {WriteError::ProtectedRegionMismatch, PackageError::None, address, changed};
  }
  return {WriteError::None, PackageError::None, 0, changed};
}

const char* errorMessage(WriteError error) {
  switch (error) {
    case WriteError::None: return "write verified";
    case WriteError::InvalidPackage: return "package validation failed";
    case WriteError::WrongDevice: return "package is for a different device";
    case WriteError::InvalidFlash: return "target flash size is unsupported";
    case WriteError::InvalidBackup: return "verified backup has the wrong size";
    case WriteError::BackupMismatch: return "live flash and verified backup do not match";
    case WriteError::ReadFailure: return "read failed";
    case WriteError::UnsafeSector: return "requested sector is protected";
    case WriteError::EraseFailure: return "sector erase failed";
    case WriteError::ProgramFailure: return "sector program failed";
    case WriteError::ReadbackMismatch: return "sector readback mismatch";
    case WriteError::ProtectedRegionMismatch: return "protected region changed";
  }
  return "unknown write error";
}
}  // namespace fm1
