#pragma once
#include <Arduino.h>
#include <FS.h>
namespace device {
enum class TransportMode : uint8_t { Disconnected, Rom, RunningFirmware, Unknown };
const char* transportModeName(TransportMode mode);
struct TransportStatus {
 // connected means identified ROM/loader ready for guarded flash operations.
 bool busy=false, connected=false, backupVerified=false;
 // A cached product remains known, but is live only after a successful MIDI
 // query in this session and while that USB connection remains present.
 bool firmwareLive=false, firmwareKnown=false;
 TransportMode mode=TransportMode::Disconnected;
 String firmwareProduct, uniqueId, lastBackupPath, persistenceError;
 uint32_t flashId=0;
 uint32_t completed=0,total=0;
 String stage="Idle",detail="Hardware validation pending";
};
void transportBegin(fs::FS&);
void transportTick();
TransportStatus transportStatus();
// Cached bounded JSON; never reads the filesystem from the calling task.
// schemaVersion 1: lastObserved identity + up to 16 boot-confirmed history items.
String transportDeviceRecordJson();
bool transportConnect();
bool transportBackup();
uint32_t transportFlashId();
uint64_t transportBackupReserve();
bool transportCancel();
bool transportCanCancel();
bool transportCanDeleteBackup(const String& path);
bool transportDeleteBackup(const String& path);
// Call after storage was erased (transport idle): drops cached device record,
// backup baseline and the session log path.
void transportStorageReset();
bool transportRestore(const String& path);
uint32_t transportStackWatermark();
bool transportInstall(const String& path);
}
