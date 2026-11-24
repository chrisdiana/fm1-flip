#include "fm1/package.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>

namespace {
uint16_t crc16(const uint8_t* p, size_t n) {
  uint16_t crc = 0;
  while (n--) {
    crc ^= uint16_t(*p++) << 8;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
  }
  return crc;
}
void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void put32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24);
}
void encrypt(uint8_t* p, size_t n) {
  uint16_t key = 0xffff;
  for (size_t i = 0; i < n; ++i) {
    p[i] ^= uint8_t(key);
    key = uint16_t(key << 1) ^ ((key & 0x8000) ? 0x1021 : 0);
  }
}
// emptyEntry adds a zero-length type-2 file after flash.bin, as some real maker
// packages do. emptyFlash makes flash.bin itself zero-length.
std::vector<uint8_t> fixture(const char* product = "FM-1_015", bool emptyEntry = false,
                             bool emptyFlash = false) {
  const uint32_t count = emptyEntry ? 2 : 1;
  const uint32_t data = 0x40 + 0x50 * count;
  const uint32_t flashLength = emptyFlash ? 0 : 0x5000;
  const size_t imageSize = data + 0x5000;
  std::vector<uint8_t> image(imageSize, 0x5a);
  uint8_t* entry = image.data() + 0x40;
  put16(entry, 0);  // upstream file type 0 is flash.bin
  put16(entry + 4, crc16(image.data() + data, flashLength));
  put32(entry + 8, data);
  put32(entry + 12, flashLength);
  if (emptyEntry) {
    uint8_t* empty = entry + 0x50;
    put16(empty, 2);
    put32(empty + 8, data + flashLength);
    put32(empty + 12, 0);
  }
  put16(image.data() + 2, [&] {
    for (uint32_t i = 0; i < count; ++i) encrypt(entry + 0x50 * i, 0x50);
    return crc16(entry, 0x50 * count);
  }());
  put32(image.data() + 4, image.size());
  image[8] = uint8_t(count);
  image[9] = 0;
  put16(image.data(), crc16(image.data() + 2, 0x3e));
  encrypt(image.data(), 0x40);

  std::vector<uint8_t> raw(image.size() + 20, 0);
  for (size_t i = 0; i < 20; ++i) {
    std::memcpy(raw.data() + i * 0x30, image.data() + i * 0x2f, 0x2f);
    raw[i * 0x30 + 0x2f] = 0x7d;
  }
  std::memcpy(raw.data() + 20 * 0x30, image.data() + 20 * 0x2f, image.size() - 20 * 0x2f);
  for (size_t i = 0; i < std::strlen(product); ++i)
    raw[i * 0x30 + 0x2f] = uint8_t(product[i] + i + 1);
  return raw;
}
class MemoryReader final : public fm1::Reader {
 public:
  explicit MemoryReader(std::vector<uint8_t> bytes) : bytes(std::move(bytes)) {}
  uint64_t size() const override { return bytes.size(); }
  bool read(uint64_t offset, uint8_t* out, size_t length) override {
    if (failReads || offset > bytes.size() || length > bytes.size() - offset) return false;
    std::memcpy(out, bytes.data() + offset, length);
    return true;
  }
  std::vector<uint8_t> bytes;
  bool failReads = false;
};
}

int main(int argc, char** argv) {
  // Optional real maker release assets stay in .cache, outside the source tree.
  for (int i = 1; i < argc; ++i) {
    std::ifstream file(argv[i], std::ios::binary);
    assert(file);
    MemoryReader release(std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), {}));
    fm1::PackageInfo actual;
    const bool smk = std::strstr(argv[i], "smk37") != nullptr;
    const auto target = smk ? fm1::DeviceModel::Smk37 : fm1::DeviceModel::Fm1;
    auto error = fm1::validatePackage(release, target, actual);
    std::printf("%s: %s; product=%s flash=%u blocks=%u\n", argv[i], fm1::errorMessage(error),
                actual.product, actual.flashSize, actual.interleaveBlocks);
    assert(error == fm1::PackageError::None);
    // Corrupt a byte inside flash.bin, which is always checksummed. The file's
    // last bytes can belong to an entry type the parser does not checksum.
    // The first interleaveBlocks raw 0x30-byte blocks each carry 0x2f logical
    // bytes plus a marker; the rest of the file maps 1:1.
    const uint64_t logical = actual.flashOffset + actual.flashSize / 2;
    const uint64_t interleaved = uint64_t(actual.interleaveBlocks) * 0x2f;
    const uint64_t raw = logical < interleaved
                             ? logical / 0x2f * 0x30 + logical % 0x2f
                             : uint64_t(actual.interleaveBlocks) * 0x30 + (logical - interleaved);
    assert(raw < release.bytes.size());
    assert(fm1::validatePackage(release, smk ? fm1::DeviceModel::Fm1 : fm1::DeviceModel::Smk37, actual)
           == fm1::PackageError::WrongDevice);
    release.bytes[raw] ^= 1;
    assert(fm1::validatePackage(release, target, actual) != fm1::PackageError::None);
  }
  MemoryReader valid(fixture());
  fm1::PackageInfo info;
  const auto validation = fm1::validatePackage(valid, fm1::DeviceModel::Fm1, info);
  if (validation != fm1::PackageError::None) std::fprintf(stderr, "%s\n", fm1::errorMessage(validation));
  assert(validation == fm1::PackageError::None);
  assert(std::strcmp(info.product, "FM-1_015") == 0);
  assert(info.version == 15 && info.flashSize == 0x5000);
  assert(info.interleaveBlocks == 20);

  uint8_t data[4]{};
  assert(fm1::readFlashImage(valid, info, 0x4000, data, sizeof data) == fm1::PackageError::None);
  assert(data[0] == 0x5a && data[3] == 0x5a);
  assert(fm1::readFlashImage(valid, info, 0x5000, data, 1) == fm1::PackageError::InvalidFlashImage);

  MemoryReader withEmpty(fixture("FM-1_015", true));
  assert(fm1::validatePackage(withEmpty, fm1::DeviceModel::Fm1, info) == fm1::PackageError::None);
  assert(info.flashSize == 0x5000);
  MemoryReader emptyFlash(fixture("FM-1_015", false, true));
  assert(fm1::validatePackage(emptyFlash, fm1::DeviceModel::Fm1, info) == fm1::PackageError::InvalidPackage);

  MemoryReader wrong(fixture("SMK-37 Elite_016"));
  assert(fm1::validatePackage(wrong, fm1::DeviceModel::Fm1, info) == fm1::PackageError::WrongDevice);
  assert(fm1::validatePackage(wrong, fm1::DeviceModel::Smk37, info) == fm1::PackageError::None);

  MemoryReader shortRead(fixture());
  shortRead.failReads = true;
  assert(fm1::validatePackage(shortRead, fm1::DeviceModel::Fm1, info) != fm1::PackageError::None);

  MemoryReader corrupt(fixture());
  corrupt.bytes.back() ^= 0x80;
  assert(fm1::validatePackage(corrupt, fm1::DeviceModel::Fm1, info) != fm1::PackageError::None);

  assert(!fm1::writableSector(0x0000, 0x1000));
  assert(!fm1::writableSector(0x3000, 0x1000));
  assert(fm1::writableSector(0x4000, 0x1000));
  assert(fm1::writableSector(0xfe000, 0x1000));
  assert(!fm1::writableSector(0xff000, 0x1000));
  assert(!fm1::writableSector(0x4001, 0x1000));
  assert(!fm1::writableSector(0x4000, 0x1001));
  assert(!fm1::writableSector(0xfffff000u, 0x1000));
  return 0;
}
