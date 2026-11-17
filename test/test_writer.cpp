#include "fm1/writer.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

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
uint32_t crc32(uint32_t crc, const uint8_t* p, size_t n) {
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int bit = 0; bit < 8; ++bit) crc = (crc & 1) ? 0xedb88320u ^ (crc >> 1) : crc >> 1;
  }
  return ~crc;
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
std::vector<uint8_t> packageFixture() {
  constexpr size_t imageSize = 0x90 + 0x5000;
  std::vector<uint8_t> image(imageSize, 0x5a);
  uint8_t* entry = image.data() + 0x40;
  put16(entry, 0);
  put16(entry + 4, crc16(image.data() + 0x90, 0x5000));
  put32(entry + 8, 0x90);
  put32(entry + 12, 0x5000);
  encrypt(entry, 0x50);
  put16(image.data() + 2, crc16(entry, 0x50));
  put32(image.data() + 4, image.size());
  image[8] = 1;
  image[9] = 0;
  put16(image.data(), crc16(image.data() + 2, 0x3e));
  encrypt(image.data(), 0x40);
  std::vector<uint8_t> raw(image.size() + 20);
  for (size_t i = 0; i < 20; ++i) {
    std::memcpy(raw.data() + i * 0x30, image.data() + i * 0x2f, 0x2f);
    raw[i * 0x30 + 0x2f] = 0x7d;
  }
  std::memcpy(raw.data() + 20 * 0x30, image.data() + 20 * 0x2f, image.size() - 20 * 0x2f);
  const char* id = "FM-1_015";
  for (size_t i = 0; i < std::strlen(id); ++i) raw[i * 0x30 + 0x2f] = uint8_t(id[i] + i + 1);
  return raw;
}
class Reader final : public fm1::Reader {
 public:
  explicit Reader(std::vector<uint8_t> bytes) : bytes(std::move(bytes)) {}
  uint64_t size() const override { return bytes.size(); }
  bool read(uint64_t offset, uint8_t* out, size_t n) override {
    if (fail || offset > bytes.size() || n > bytes.size() - offset) return false;
    std::memcpy(out, bytes.data() + offset, n);
    return true;
  }
  std::vector<uint8_t> bytes;
  bool fail = false;
};
class FakeFlash final : public fm1::Flash {
 public:
  uint32_t size() const override { return uint32_t(bytes.size()); }
  bool read(uint32_t a, uint8_t* out, size_t n) override {
    ++readCalls;
    if (readFail || readCalls == failReadAt || a > bytes.size() || n > bytes.size() - a) return false;
    std::memcpy(out, bytes.data() + a, n);
    return true;
  }
  bool eraseSector(uint32_t a) override {
    erased.push_back(a);
    if (eraseFail) return false;
    if(cancelDuringErase)*cancelDuringErase=true;
    std::fill(bytes.begin() + a, bytes.begin() + a + 0x1000, 0xff);
    return true;
  }
  bool program(uint32_t a, const uint8_t* p, size_t n) override {
    if (programFail) return false;
    std::memcpy(bytes.data() + a, p, n);
    if (badReadback) bytes[a] ^= 1;
    if (corruptProtected) bytes[0xff010] ^= 1;
    if(cancelDuringProgram)*cancelDuringProgram=true;
    return true;
  }
  std::vector<uint8_t> bytes = std::vector<uint8_t>(0x100000, 0);
  std::vector<uint32_t> erased;
  uint32_t readCalls = 0, failReadAt = 0;
  bool* cancelDuringErase=nullptr;
  bool* cancelDuringProgram=nullptr;
  bool corruptProtected=false;
  bool readFail = false, eraseFail = false, programFail = false, badReadback = false;
};
uint32_t expectedCrc(const FakeFlash& flash) { return crc32(0, flash.bytes.data(), flash.bytes.size()); }
fm1::WriteReport run(FakeFlash& flash, Reader& package, Reader& backup, uint32_t crc) {
  return fm1::writePackage(flash, package, fm1::DeviceModel::Fm1, backup, crc);
}
}

int main() {
  FakeFlash flash;
  Reader backup(flash.bytes);
  Reader package(packageFixture());
  const uint32_t crc = expectedCrc(flash);
  auto result = run(flash, package, backup, crc);
  assert(result.error == fm1::WriteError::None);
  assert(result.changedSectors == 1);
  assert(flash.erased.size() == 1 && flash.erased[0] == 0x4000);
  for (uint32_t addr : flash.erased) assert(fm1::writableSector(addr, 0x1000));
  for (uint32_t i = 0; i < 0x4000; ++i) assert(flash.bytes[i] == backup.bytes[i]);
  for (uint32_t i = 0xff000; i < 0x100000; ++i) assert(flash.bytes[i] == backup.bytes[i]);

  FakeFlash mismatch;
  Reader oldBackup(mismatch.bytes);
  mismatch.bytes[0x4000] = 1;
  Reader package2(packageFixture());
  auto mismatchResult = run(mismatch, package2, oldBackup, crc);
  assert(mismatchResult.error == fm1::WriteError::BackupMismatch);
  assert(mismatch.erased.empty());

  FakeFlash eraseFailure;
  Reader backup3(eraseFailure.bytes), package3(packageFixture());
  eraseFailure.eraseFail = true;
  auto eraseResult = run(eraseFailure, package3, backup3, expectedCrc(eraseFailure));
  assert(eraseResult.error == fm1::WriteError::EraseFailure);
  assert(eraseFailure.erased.size() == 1 && eraseFailure.erased[0] == 0x4000);
  assert(eraseFailure.bytes[0x4000] == 0);

  FakeFlash programFailure;
  Reader backup4(programFailure.bytes), package4(packageFixture());
  programFailure.programFail = true;
  auto programResult = run(programFailure, package4, backup4, expectedCrc(programFailure));
  assert(programResult.error == fm1::WriteError::ProgramFailure);
  assert(programFailure.erased.size() == 1 && programFailure.erased[0] == 0x4000);

  FakeFlash verifyFailure;
  Reader backup5(verifyFailure.bytes), package5(packageFixture());
  verifyFailure.badReadback = true;
  auto verifyResult = run(verifyFailure, package5, backup5, expectedCrc(verifyFailure));
  assert(verifyResult.error == fm1::WriteError::ReadbackMismatch);
  assert(verifyFailure.erased.size() == 1 && verifyFailure.erased[0] == 0x4000);

  FakeFlash disconnected;
  Reader backupDisconnect(disconnected.bytes), packageDisconnect(packageFixture());
  disconnected.failReadAt = 2051;  // disconnect on the first sector readback, after the write
  auto disconnectResult = run(disconnected, packageDisconnect, backupDisconnect, expectedCrc(disconnected));
  assert(disconnectResult.error == fm1::WriteError::ReadFailure);
  assert(disconnected.erased.size() == 1 && disconnected.erased[0] == 0x4000);
  assert(disconnected.bytes[0x4000] == 0x5a);  // backup is required for this interrupted operation

  FakeFlash noChange;
  std::fill(noChange.bytes.begin() + 0x4000, noChange.bytes.begin() + 0x5000, 0x5a);
  Reader backup6(noChange.bytes), package6(packageFixture());
  auto noChangeResult = run(noChange, package6, backup6, expectedCrc(noChange));
  assert(noChangeResult.error == fm1::WriteError::None);
  assert(noChangeResult.changedSectors == 0 && noChange.erased.empty());

  FakeFlash packageReadFailure;
  Reader backup7(packageReadFailure.bytes), package7(packageFixture());
  package7.fail = true;
  auto packageReadResult = run(packageReadFailure, package7, backup7, expectedCrc(packageReadFailure));
  assert(packageReadResult.error == fm1::WriteError::InvalidPackage);
  assert(packageReadFailure.erased.empty());
  // No-backup installs retain bounded preflight, protected-region and readback checks.
  for (int fault = 0; fault < 7; ++fault) {
    FakeFlash direct;
    Reader source(packageFixture());
    const auto original = direct.bytes;
    if(fault==1)direct.failReadAt=1; // Protected snapshot failure, before erasing.
    if(fault==2)direct.failReadAt=6; // Complete replacement preflight failure.
    if(fault==3)direct.eraseFail=true;
    if(fault==4)direct.programFail=true;
    if(fault==5)direct.badReadback=true;
    if(fault==6)direct.corruptProtected=true;
    auto report=fm1::writePackageWithoutBackup(direct,source,fm1::DeviceModel::Fm1);
    if(fault==0) {
      assert(report.error==fm1::WriteError::None&&report.changedSectors==1);
      assert(direct.erased==std::vector<uint32_t>{0x4000});
      assert(std::equal(original.begin(),original.begin()+0x4000,direct.bytes.begin()));
      assert(std::equal(original.begin()+0xff000,original.end(),direct.bytes.begin()+0xff000));
      direct.erased.clear();
      report=fm1::writePackageWithoutBackup(direct,source,fm1::DeviceModel::Fm1);
      assert(report.error==fm1::WriteError::None&&report.changedSectors==0&&direct.erased.empty());
    } else {
      assert(report.error!=fm1::WriteError::None);
      if(fault<3)assert(direct.erased.empty());
      if(fault==5)assert(report.error==fm1::WriteError::ReadbackMismatch);
      if(fault==6)assert(report.error==fm1::WriteError::ProtectedRegionMismatch);
    }
  }
  // Cancel before erase, or finish an in-flight sector and stop before the next.
  for(bool duringWrite : {false,true}) {
    FakeFlash stopped;Reader source(packageFixture());bool stop=!duringWrite;
    if(duringWrite)stopped.cancelDuringProgram=&stop;
    auto cancelled=fm1::writePackageWithoutBackup(stopped,source,fm1::DeviceModel::Fm1,nullptr,&stop,
      [](void* p){return *static_cast<bool*>(p);});
    assert(cancelled.error==fm1::WriteError::Cancelled);
    assert(stopped.erased.size()==(duringWrite?1u:0u));
    assert(cancelled.changedSectors==(duringWrite?1u:0u));
    if(duringWrite)assert(stopped.bytes[0x4000]==0x5a&&stopped.bytes[0x5000]==0);
    assert(stopped.bytes[0]==0&&stopped.bytes[0xff000]==0);
  }
  FakeFlash invalidDirect;Reader invalidSource(packageFixture());invalidSource.fail=true;
  assert(fm1::writePackageWithoutBackup(invalidDirect,invalidSource,fm1::DeviceModel::Fm1).error==fm1::WriteError::InvalidPackage);
  assert(invalidDirect.erased.empty());
  // Restore only changed writable sectors and require two full verified backups.
  FakeFlash restore;
  Reader restoreCurrent(restore.bytes), restoreDesired(restore.bytes);
  restoreDesired.bytes[0x4000] = 0x78;
  restoreDesired.bytes[0xfe000] = 0x34;
  const uint32_t desiredCrc = crc32(0, restoreDesired.bytes.data(), restoreDesired.bytes.size());
  auto restored = fm1::restoreBackup(restore, restoreDesired, restoreCurrent, desiredCrc, crc);
  assert(restored.error == fm1::WriteError::None && restored.changedSectors == 2);
  assert(restore.erased == std::vector<uint32_t>({0x4000, 0xfe000}));
  assert(restore.bytes == restoreDesired.bytes);
  // A stop arriving during erase still programs/verifies that sector fully.
  for(bool restoring : {false,true}) {
    FakeFlash stopped;Reader current(stopped.bytes), desired(stopped.bytes), source(packageFixture());
    desired.bytes[0x4000]=0x78;desired.bytes[0x5000]=0x34;
    bool stop=false;stopped.cancelDuringErase=&stop;
    auto check=[](void* p){return *static_cast<bool*>(p);};
    auto report=restoring
      ? fm1::restoreBackup(stopped,desired,current,crc32(0,desired.bytes.data(),desired.bytes.size()),expectedCrc(stopped),nullptr,&stop,check)
      : fm1::writePackageWithoutBackup(stopped,source,fm1::DeviceModel::Fm1,nullptr,&stop,check);
    assert(report.error==fm1::WriteError::Cancelled&&report.changedSectors==1);
    assert(stopped.erased==std::vector<uint32_t>{0x4000});
    assert(stopped.bytes[0x4000]==(restoring?0x78:0x5a)&&stopped.bytes[0x5000]==0);
    assert(stopped.bytes[0]==0&&stopped.bytes[0xff000]==0);
  }
  // A completed restore is idempotent with a fresh current verified backup.
  Reader freshCurrent(restore.bytes);
  restore.erased.clear();
  auto repeat = fm1::restoreBackup(restore, restoreDesired, freshCurrent, desiredCrc, desiredCrc);
  assert(repeat.error == fm1::WriteError::None && repeat.changedSectors == 0 && restore.erased.empty());

  for (uint32_t protectedAddress : {0u, 0x3fffu, 0xff000u, 0xfffffu}) {
    FakeFlash guarded;
    Reader before(guarded.bytes), desired(guarded.bytes);
    desired.bytes[protectedAddress] = 1;
    desired.bytes[0x4000] = 1;
    auto rejected = fm1::restoreBackup(guarded, desired, before,
        crc32(0, desired.bytes.data(), desired.bytes.size()), crc);
    assert(rejected.error == fm1::WriteError::ProtectedRegionMismatch && guarded.erased.empty());
  }
  for (int mode = 0; mode < 8; ++mode) {
    FakeFlash faulty;
    Reader before(faulty.bytes), desired(faulty.bytes);
    desired.bytes[0x4000] = 1;
    desired.bytes[0x5000] = 2;
    uint32_t want = crc32(0, desired.bytes.data(), desired.bytes.size());
    if (mode == 0) want ^= 1;                         // desired CRC mismatch
    if (mode == 1) before.bytes[7] = 1;              // current backup CRC mismatch
    if (mode == 2) faulty.bytes[0x9000] = 1;         // live target mismatch
    if (mode == 3) desired.bytes.pop_back();         // truncated backup
    if (mode == 4) faulty.eraseFail = true;
    if (mode == 5) faulty.programFail = true;
    if (mode == 6) faulty.badReadback = true;
    if (mode == 7) faulty.failReadAt = 2050;          // disconnect on first readback
    auto rejected = fm1::restoreBackup(faulty, desired, before, want, crc);
    assert(rejected.error != fm1::WriteError::None);
    assert(faulty.erased.size() == (mode < 4 ? 0u : 1u));
    if (mode == 4) assert(rejected.error == fm1::WriteError::EraseFailure);
    if (mode == 5) assert(rejected.error == fm1::WriteError::ProgramFailure);
    if (mode == 6) assert(rejected.error == fm1::WriteError::ReadbackMismatch);
    if (mode == 7) assert(rejected.error == fm1::WriteError::ReadFailure);
  }
  return 0;
}
