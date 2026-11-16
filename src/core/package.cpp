// SPDX-License-Identifier: GPL-3.0-only
// Container algorithm adapted from konsumer/fm1buddy src/app/fm1.cpp,
// commit 65b8760d1365e84e5bd799c7fa1d3e0d7c3b4fe2, itself a port of
// foma web/fm1ota.js and web/fm1pkg.js.
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments.
// See LICENSE and docs/package-format.md for provenance.
#include "fm1/package.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace fm1 {
namespace {
constexpr uint32_t kIdentityBlocks = 20;
constexpr uint32_t kMaxBlocks = 64;
constexpr uint32_t kBlockRaw = 0x30;
constexpr uint32_t kBlockKeep = 0x2f;
constexpr uint64_t kMaxPackageSize = 16u * 1024u * 1024u;
constexpr uint32_t kHeaderSize = 0x40;
constexpr uint32_t kEntrySize = 0x50;
constexpr uint32_t kMaxEntries = 8;
constexpr uint32_t kSector = 0x1000;
constexpr uint32_t kWriteFrom = 0x4000;
constexpr uint32_t kWriteTo = 0xff000;
constexpr uint32_t kFlashSize = 0x100000;

bool readRaw(Reader& r, uint64_t offset, uint8_t* out, size_t length) {
  const uint64_t n = r.size();
  if (offset > n || length > n - offset) return false;
  return length == 0 || r.read(offset, out, length);
}

int marker(Reader& r, uint64_t n, uint32_t block) {
  const uint64_t off = (uint64_t(block) + 1) * kBlockRaw - 1;
  uint8_t m;
  return off < n && readRaw(r, off, &m, 1) ? m : -1;
}

bool isIdentityByte(uint8_t m, uint32_t i) {
  const uint8_t c = uint8_t(m - i - 1);
  return c >= 0x20 && c < 0x7f;
}

bool blockCounts(Reader& r, uint64_t n, uint32_t& identityBlocks, uint32_t& blocks) {
  if (n < uint64_t(kIdentityBlocks) * kBlockRaw) return false;
  uint32_t k = kIdentityBlocks;
  while (k < kMaxBlocks) {
    int m = marker(r, n, k);
    if (m < 0 || m == 0x7d || !isIdentityByte(uint8_t(m), k)) break;
    ++k;
  }
  identityBlocks = (k > kIdentityBlocks && marker(r, n, k) == 0x7d) ? k : kIdentityBlocks;
  blocks = identityBlocks;
  while (blocks < kMaxBlocks && marker(r, n, blocks) == 0x7d) ++blocks;
  return true;
}

bool productText(Reader& r, uint32_t identities, char* out, size_t capacity) {
  if (!capacity) return false;
  size_t used = 0;
  for (uint32_t i = 0; i < identities; ++i) {
    int m = marker(r, r.size(), i);
    if (m < 0) return false;
    if (m == 0x7d) continue;
    if (!isIdentityByte(uint8_t(m), i) || used + 1 >= capacity) return false;
    out[used++] = char(uint8_t(m - i - 1));
  }
  out[used] = '\0';
  return used != 0;
}

bool logicalRead(Reader& r, uint32_t blocks, uint64_t logicalSize,
                 uint64_t offset, uint8_t* out, size_t length) {
  if (offset > logicalSize || length > logicalSize - offset) return false;
  while (length) {
    uint64_t rawOffset;
    size_t span;
    const uint64_t interleavedSize = uint64_t(blocks) * kBlockKeep;
    if (offset < interleavedSize) {
      const uint64_t within = offset % kBlockKeep;
      span = size_t(std::min<uint64_t>(length, kBlockKeep - within));
      rawOffset = (offset / kBlockKeep) * kBlockRaw + within;
    } else {
      rawOffset = uint64_t(blocks) * kBlockRaw + (offset - interleavedSize);
      span = length;
    }
    if (!readRaw(r, rawOffset, out, span)) return false;
    offset += span;
    out += span;
    length -= span;
  }
  return true;
}

void decrypt(uint8_t* bytes, size_t length) {
  uint16_t key = 0xffff;
  for (size_t i = 0; i < length; ++i) {
    bytes[i] ^= uint8_t(key);
    key = uint16_t(key << 1) ^ ((key & 0x8000) ? 0x1021 : 0);
  }
}

uint16_t crcUpdate(uint16_t crc, const uint8_t* bytes, size_t length) {
  while (length--) {
    crc ^= uint16_t(*bytes++) << 8;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
  }
  return crc;
}

uint16_t crc16(const uint8_t* bytes, size_t length) { return crcUpdate(0, bytes, length); }

uint16_t get16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t get32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

bool crcLogical(Reader& r, uint32_t blocks, uint64_t logicalSize, uint64_t offset,
                uint64_t length, uint16_t& result) {
  uint8_t buffer[512];
  uint16_t crc = 0;
  while (length) {
    const size_t take = size_t(std::min<uint64_t>(length, sizeof buffer));
    if (!logicalRead(r, blocks, logicalSize, offset, buffer, take)) return false;
    crc = crcUpdate(crc, buffer, take);
    offset += take;
    length -= take;
  }
  result = crc;
  return true;
}

bool flashEntry(Reader& r, uint32_t blocks, uint64_t logicalSize,
                uint32_t& flashOffset, uint32_t& flashSize) {
  uint8_t encodedHeader[kHeaderSize];
  if (logicalSize < kHeaderSize || !logicalRead(r, blocks, logicalSize, 0, encodedHeader, sizeof encodedHeader))
    return false;
  uint8_t header[kHeaderSize];
  memcpy(header, encodedHeader, sizeof header);
  decrypt(header, sizeof header);
  const uint32_t count = get16(header + 8);
  const uint64_t tableEnd = kHeaderSize + uint64_t(kEntrySize) * count;
  if (count < 1 || count > kMaxEntries || get32(header + 4) != logicalSize || logicalSize < tableEnd ||
      crc16(header + 2, 0x3e) != get16(header)) return false;

  uint16_t tableCrc;
  if (!crcLogical(r, blocks, logicalSize, kHeaderSize, uint64_t(kEntrySize) * count, tableCrc) ||
      tableCrc != get16(header + 2)) return false;

  bool foundFlash = false;
  uint32_t ranges[kMaxEntries][2]{};
  for (uint32_t i = 0; i < count; ++i) {
    uint8_t entry[kEntrySize];
    if (!logicalRead(r, blocks, logicalSize, kHeaderSize + uint64_t(kEntrySize) * i, entry, sizeof entry))
      return false;
    decrypt(entry, sizeof entry);
    const uint32_t type = get16(entry);
    const uint32_t expectedCrc = get16(entry + 4);
    const uint32_t offset = get32(entry + 8);
    const uint32_t length = get32(entry + 12);
    if (!length || offset < tableEnd || offset > logicalSize || length > logicalSize - offset) return false;
    for (uint32_t j = 0; j < i; ++j) {
      const uint64_t aEnd = uint64_t(offset) + length;
      const uint64_t bEnd = uint64_t(ranges[j][0]) + ranges[j][1];
      if (offset < bEnd && ranges[j][0] < aEnd) return false;
    }
    ranges[i][0] = offset;
    ranges[i][1] = length;
    if (type == 0 || type == 100) {
      uint16_t actualCrc;
      if (!crcLogical(r, blocks, logicalSize, offset, length, actualCrc) || actualCrc != expectedCrc)
        return false;
    }
    if (type == 0 && foundFlash) return false;
    if (type == 0) {
      flashOffset = offset;
      flashSize = length;
      foundFlash = true;
    }
  }
  return foundFlash;
}

bool modelIdentity(const char* product, char* model, size_t capacity, uint32_t& version) {
  const char* begin = product;
  if (!strncmp(begin, "ota-", 4)) begin += 4;
  const char* underscore = strrchr(begin, '_');
  if (!underscore || underscore == begin || !underscore[1]) return false;
  uint64_t v = 0;
  for (const char* p = underscore + 1; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + uint32_t(*p - '0');
    if (v > std::numeric_limits<uint32_t>::max()) return false;
  }
  const size_t modelLength = size_t(underscore - begin);
  if (modelLength >= capacity) return false;
  memcpy(model, begin, modelLength);
  model[modelLength] = '\0';
  version = uint32_t(v);
  return true;
}

char upperAscii(char c) { return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c; }

bool isSmkModel(const char* model) {
  char upper[48];
  size_t n = strlen(model);
  if (n >= sizeof upper) return false;
  for (size_t i = 0; i <= n; ++i) upper[i] = upperAscii(model[i]);
  return !strncmp(upper, "SMK-37 ", 7) || !strncmp(upper, "SMK-37+", 7) ||
         !strcmp(upper, "MKE-P37") || !strncmp(upper, "STARRYKEY-37", 12);
}

bool matches(DeviceModel target, const char* model) {
  if (target == DeviceModel::Fm1) {
    static constexpr char expected[] = "FM-1";
    if (strlen(model) != sizeof(expected) - 1) return false;
    for (size_t i = 0; i < sizeof(expected) - 1; ++i)
      if (upperAscii(model[i]) != expected[i]) return false;
    return true;
  }
  return isSmkModel(model);
}

PackageError validateBlocks(Reader& r, uint32_t blocks, uint32_t identities,
                            DeviceModel target, PackageInfo& out) {
  char product[sizeof out.product]{};
  char model[sizeof out.model]{};
  uint32_t version = 0;
  const uint64_t logicalSize = r.size() - blocks;
  if (!productText(r, identities, product, sizeof product) ||
      !modelIdentity(product, model, sizeof model, version)) return PackageError::InvalidIdentity;

  uint32_t flashOffset = 0, flashSize = 0;
  if (!flashEntry(r, blocks, logicalSize, flashOffset, flashSize)) return PackageError::InvalidPackage;
  if (flashSize <= kWriteFrom || flashSize > kFlashSize) return PackageError::InvalidFlashImage;
  if (flashOffset > logicalSize || flashSize > logicalSize - flashOffset) return PackageError::InvalidPackage;
  if (!matches(target, model)) return PackageError::WrongDevice;
  memcpy(out.product, product, strlen(product) + 1);
  memcpy(out.model, model, strlen(model) + 1);
  out.version = version;
  out.rawSize = r.size();
  out.logicalSize = logicalSize;
  out.interleaveBlocks = uint8_t(blocks);
  out.flashOffset = flashOffset;
  out.flashSize = flashSize;
  return PackageError::None;
}
}  // namespace

PackageError validatePackage(Reader& reader, DeviceModel target, PackageInfo& out) {
  out = {};
  const uint64_t size = reader.size();
  if (size < uint64_t(kIdentityBlocks) * kBlockRaw) return PackageError::TooSmall;
  if (size > kMaxPackageSize) return PackageError::TooLarge;
  uint32_t identityBlocks = 0, blocks = 0;
  if (!blockCounts(reader, size, identityBlocks, blocks)) return PackageError::InvalidIdentity;
  PackageError result = validateBlocks(reader, blocks, identityBlocks, target, out);
  if (result == PackageError::None) return result;
  if (blocks != kIdentityBlocks) {
    out = {};
    // Upstream format has a 20-block fallback because an unused marker can
    // occur by chance in a package with the original 20 identity blocks.
    return validateBlocks(reader, kIdentityBlocks, kIdentityBlocks, target, out);
  }
  return result;
}

PackageError readFlashImage(Reader& reader, const PackageInfo& package, uint32_t address,
                            uint8_t* destination, size_t length) {
  if (package.rawSize != reader.size() || package.flashOffset > package.logicalSize ||
      package.flashSize > package.logicalSize - package.flashOffset || address > package.flashSize ||
      length > package.flashSize - address) return PackageError::InvalidFlashImage;
  const uint64_t logicalOffset = uint64_t(package.flashOffset) + address;
  return logicalRead(reader, package.interleaveBlocks, package.logicalSize, logicalOffset, destination, length)
             ? PackageError::None
             : PackageError::ReadFailure;
}

bool writableSector(uint32_t address, uint32_t length) {
  if (length != kSector || address % kSector != 0) return false;
  if (address > std::numeric_limits<uint32_t>::max() - length) return false;
  return address >= kWriteFrom && address + length <= kWriteTo;
}

const char* errorMessage(PackageError error) {
  switch (error) {
    case PackageError::None: return "valid package";
    case PackageError::TooSmall: return "package is too small";
    case PackageError::TooLarge: return "package exceeds the size limit";
    case PackageError::ReadFailure: return "package read failed or returned short data";
    case PackageError::InvalidIdentity: return "invalid package identity";
    case PackageError::WrongDevice: return "package is for a different device model";
    case PackageError::InvalidPackage: return "package structure or checksum is invalid";
    case PackageError::InvalidFlashImage: return "flash image size or range is invalid";
    case PackageError::UnsafeFlashRegion: return "flash sector is protected";
  }
  return "unknown package error";
}
}  // namespace fm1
