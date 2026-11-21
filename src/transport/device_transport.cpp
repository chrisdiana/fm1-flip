// Application adapter for the MIT Cardputer Synth Rescue ROM transport.
#include <device/transport.h>
#include <device/catalogue.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "jl.h"
#include "uboot_host.h"
#include "usbkey.h"
#include "usb_bus.h"
#include <fm1/package.hpp>
#include <fm1/writer.hpp>
#include <fm1/midi.hpp>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <atomic>
#include <algorithm>
#include <vector>
#include <time.h>

namespace device {
namespace {
fs::FS* disk;
SemaphoreHandle_t lock;
QueueHandle_t commands;
TaskHandle_t workerTask;
TransportStatus state;
jl::Info chip{};
bool hostStarted = false;
bool identifiedFm1 = false;
uint32_t backupCrc = 0;
String backupPath;
String observedFirmware;
String deviceRecordJson="{\"schemaVersion\":1,\"history\":[]}";
constexpr size_t MaxDeviceRecordBytes=24576;
constexpr size_t MaxHistoryEntries=16;
String logPath;
constexpr size_t MaxSessionLogs = 10;
String lastLoggedStage;
// 0 idle, 1 preflight, 2 stop requested, 3 writing (sector-boundary stop), 4 rebooting.
std::atomic<uint8_t> cancellation{0};
std::atomic<bool> baselineExists{false};
constexpr uint32_t FlashSize = 0x100000;
uint8_t buffer[4096];
struct Command { uint8_t kind; char path[160]; };
void update(const char* stage, const String& detail, uint32_t done=0, uint32_t total=0) {
 xSemaphoreTake(lock, portMAX_DELAY);
 state.stage=stage; state.detail=detail; state.completed=done; state.total=total;
 const bool mayWriteLog=state.busy;
 xSemaphoreGive(lock);
 if (mayWriteLog && logPath.length() && (lastLoggedStage != stage || !strcmp(stage,"Error") || !strcmp(stage,"USB"))) {
  File log = disk->open(logPath, FILE_APPEND);
  if (log && log.size() < 65536) log.printf("%lu %s: %s\n", (unsigned long)millis(), stage, detail.c_str());
  lastLoggedStage = stage;
 }
}
String uniqueId() {
 if (!chip.has_unique_id) return "";
 char value[17]; for (size_t i=0;i<8;i++) snprintf(value+i*2,3,"%02x",chip.unique_id[i]);
 return value;
}
void setMode(TransportMode mode,bool romReady=false) {
 xSemaphoreTake(lock,portMAX_DELAY);
 state.mode=mode;state.connected=romReady;
 if(mode!=TransportMode::RunningFirmware)state.firmwareLive=false;
 xSemaphoreGive(lock);
}
void addTimestamp(JsonObject record) {
 record["uptimeMs"]=uint32_t(millis());
 const time_t now=time(nullptr);
 if(now>=1704067200)record["timeUnix"]=int64_t(now);
}
void addChipIdentity(JsonObject record) {
 record["model"]="FM-1";
 if(chip.flash_id) {
  char value[9];snprintf(value,sizeof value,"%06lx",(unsigned long)chip.flash_id);
  record["flashId"]=value;
  char key[5];snprintf(key,sizeof key,"%04x",chip.chip_key);record["chipKey"]=key;
 }
 const String uid=uniqueId();
 if(uid.length())record["uniqueId"]=uid;
}
bool validDeviceRecord(JsonDocument& document) {
 if(document["schemaVersion"].as<int>()!=1 || !document["history"].is<JsonArray>())return false;
 JsonArray history=document["history"].as<JsonArray>();
 if(history.size()>MaxHistoryEntries)return false;
 if(!document["lastObserved"].isNull()) {
  JsonObject last=document["lastObserved"].as<JsonObject>();
  String product=last["product"]|"";
  if(!last || product.isEmpty() || product.length()>63 || last["model"]!="FM-1")return false;
 }
 for(JsonObject event:history) {
  String kind=event["kind"]|"",product=event["product"]|"",path=event["path"]|"",hash=event["sha256"]|"";
  if((kind!="install" && kind!="restore") || product.isEmpty() || product.length()>63 || path.length()>159 ||
     hash.length()!=64 || event["bootConfirmed"]!=true || event["model"]!="FM-1")return false;
  for(char c:hash)if(!isxdigit((unsigned char)c))return false;
 }
 return true;
}
void publishRecord(const String& json,const String& persistenceError) {
 xSemaphoreTake(lock,portMAX_DELAY);
 deviceRecordJson=json;state.persistenceError=persistenceError;
 xSemaphoreGive(lock);
}
void persistRecord(JsonDocument& document) {
 String json;serializeJson(document,json);
 if(json.length()>MaxDeviceRecordBytes) {
  xSemaphoreTake(lock,portMAX_DELAY);state.persistenceError="Device history exceeds storage limit";xSemaphoreGive(lock);return;
 }
 // All callers execute inside an accepted busy worker command. The temporary
 // file is read back before committing; .prev recovers a loss between renames.
 const char* final="/fm1/device.json";
 const char* part="/fm1/device.part";
 const char* previous="/fm1/device.prev";
 disk->mkdir("/fm1");disk->remove(part);
 File file=disk->open(part,FILE_WRITE);
 bool ok=file && file.print(json)==json.length();
 if(file){file.flush();file.close();}
 if(ok) {
  file=disk->open(part,FILE_READ);JsonDocument verified;
  ok=file && file.size()==json.length() && !deserializeJson(verified,file) && validDeviceRecord(verified);
  file.close();
 }
 if(ok && disk->exists(final)) {
  if(disk->exists(previous) && !disk->remove(previous))ok=false;
  if(ok)ok=disk->rename(final,previous);
 }
 if(ok)ok=disk->rename(part,final);
 if(ok)disk->remove(previous);
 else if(!disk->exists(final) && disk->exists(previous))disk->rename(previous,final);
 publishRecord(json,ok?String():String("Could not save device history; current session record retained in memory"));
}
void recordObservation(const String& product) {
 JsonDocument record;
 if(deserializeJson(record,deviceRecordJson) || !validDeviceRecord(record)) {
  record.clear();record["schemaVersion"]=1;record["history"].to<JsonArray>();
 }
 JsonObject observed=record["lastObserved"].to<JsonObject>();
 observed["product"]=product;addChipIdentity(observed);addTimestamp(observed);
 persistRecord(record);
 observedFirmware=product;
 xSemaphoreTake(lock,portMAX_DELAY);
 state.firmwareProduct=product;state.firmwareKnown=true;state.firmwareLive=true;
 state.mode=TransportMode::RunningFirmware;state.connected=false;state.backupVerified=false;
 xSemaphoreGive(lock);
}
void recordSuccessfulOperation(const char* kind,const String& path,const String& hash) {
 JsonDocument record;
 if(deserializeJson(record,deviceRecordJson) || !validDeviceRecord(record)) {
  record.clear();record["schemaVersion"]=1;record["history"].to<JsonArray>();
 }
 JsonArray history=record["history"].as<JsonArray>();
 while(history.size()>=MaxHistoryEntries)history.remove(0);
 JsonObject event=history.add<JsonObject>();
 event["kind"]=kind;event["product"]=observedFirmware;
 event["path"]=path;event["sha256"]=hash;event["bootConfirmed"]=true;
 addChipIdentity(event);addTimestamp(event);
 persistRecord(record);
}
void loadDeviceRecord() {
 // Called before starting the worker or exposing web handlers. Never promotes
 // an old observation to a live/installed claim after an M5 restart.
 for(const char* path:{"/fm1/device.json","/fm1/device.prev"}) {
  File file=disk->open(path,FILE_READ);JsonDocument document;
  if(!file || file.size()>MaxDeviceRecordBytes || deserializeJson(document,file) || !validDeviceRecord(document))continue;
  String json;serializeJson(document,json);deviceRecordJson=json;
  state.firmwareProduct=document["lastObserved"]["product"]|"";
  state.firmwareKnown=state.firmwareProduct.length()>0;
  state.firmwareLive=false;state.mode=TransportMode::Disconnected;
  return;
 }
}

void trace(const char* fmt, ...) {
 char line[192]; va_list ap; va_start(ap,fmt); vsnprintf(line,sizeof line,fmt,ap); va_end(ap);
 update("USB",line);
}
bool cancelled(const usbkey::KeyStats*) { return cancellation.load()==2; }
bool noCancel() { return cancellation.load()==2; }
bool commitWrite(String& error) {
 uint8_t expected=1;
 if(cancellation.compare_exchange_strong(expected,3))return true;
 error="Cancelled before flash writes";return false;
}
bool queryFirmware(String& product,String& error) {
 uint8_t response[fm1::MaxSysExBytes];char err[160]{};
 for(unsigned attempt=0;attempt<3;attempt++) {
  if(cancellation.load()==2){error="Identity query cancelled";return false;}
  if(!host::midi_send_sysex(fm1::IdentityQuery,sizeof fm1::IdentityQuery,err,sizeof err)){error=err;return false;}
  uint32_t length=0;
  if(host::midi_receive_sysex(response,sizeof response,&length,1800,err,sizeof err)) {
   fm1::FirmwareIdentity identity;
   if(fm1::parseFirmwareIdentity(response,length,identity) && !identity.updateMode) {
    product=identity.product;recordObservation(product);return true;
   }
  }
  vTaskDelay(pdMS_TO_TICKS(200));
 }
 error="No valid running FM-1 identity response";return false;
}
// A USB session spans Connect through to disconnection: 5 V output on, pins and
// PHY reset to a known state before the host starts, and returned to idle (5 V
// off, USB-Serial-JTAG restored) when the session ends or fails.
bool usbSession = false;
// Session log only: teardown runs after the Error / Disconnected status is shown
// and must not replace it on screen.
void logOnly(const char* fmt, ...) {
 char line[192]; va_list ap; va_start(ap,fmt); vsnprintf(line,sizeof line,fmt,ap); va_end(ap);
 xSemaphoreTake(lock, portMAX_DELAY); const bool mayWriteLog=state.busy; xSemaphoreGive(lock);
 if(!mayWriteLog || !logPath.length())return;
 File log=disk->open(logPath,FILE_APPEND);
 if(log && log.size()<65536) log.printf("%lu USB: %s\n",(unsigned long)millis(),line);
}
bool beginUsbSession(String& error) {
 usbSession=true;
 if(!usbbus::request_power(true)){error="5 V output did not switch on; restart M5 and retry";return false;}
 usbbus::prepare_for_host(trace);
 return true;
}
void endUsbSession() {
 if(!usbSession && !hostStarted)return;
 bool clean=true;
 if(hostStarted) {clean=host::stop();if(clean)hostStarted=false;}
 if(clean) usbbus::restore_idle(logOnly);
 else logOnly("USB host cleanup incomplete; pins left as-is. Restart M5 before reconnecting.");
 if(!usbbus::request_power(false)) logOnly("5 V output did not confirm switching off");
 usbSession=false;
}
bool startAndWait(uint16_t& vid,uint16_t& pid,String& error) {
 char err[160]{};hostStarted=true;
 if(!host::start()){error="USB host initialization failed";return false;}
 if(!host::wait_device(12000,&vid,&pid,trace,err,sizeof err)){error=err;return false;}
 return true;
}
bool connectDevice(String& error) {
 identifiedFm1=false;
 observedFirmware="";
 chip={};
 xSemaphoreTake(lock,portMAX_DELAY);
 state.connected=false;state.backupVerified=false;state.firmwareLive=false;
 state.mode=TransportMode::Unknown;state.flashId=0;state.uniqueId="";
 xSemaphoreGive(lock);
 if(hostStarted) {
  if(!host::stop()){error="USB cleanup failed; restart M5 before reconnecting";return false;}
  hostStarted=false;
 }
 uint16_t vid=0,pid=0; char err[160]{};
 if(!beginUsbSession(error))return false;
 auto presence=usbbus::probe(trace);
 if(presence==usbbus::Presence::Fault){error="Both USB data lines read high: check the cable or adapter for a short, then retry.";return false;}
 if(presence==usbbus::Presence::Unstable){error="FM-1 USB signal unstable (pull-up dropping or lines toggling). Its battery may be low: charge the FM-1, then retry.";return false;}
 update("Connect",presence==usbbus::Presence::Device?"FM-1 USB detected; enumerating.":"Looking for FM-1 USB. Leave it powered on and connected. A low FM-1 battery stops its USB.");
 bool enumerated=startAndWait(vid,pid,error);
 if(!enumerated) {
  // A visible unsupported device is not a reason to drive raw recovery pulses,
  // and neither is a bus that toggles with nothing attached.
  if(vid || cancellation.load()==2 || host::port_attached() || host::last_wait_unstable())return false;
  if(!host::stop()){error="USB cleanup failed; restart M5 before reconnecting";return false;}
  hostStarted=false;
  update("Boot-key connection","No USB device found. Keep cable attached; turn FM-1 OFF then ON now. If its battery is low, charge it first.");
  usbkey::init();
  usbkey::KeyStats stats{};
  auto result=usbkey::send_key(&stats,20000,cancelled);
  if(result==usbkey::TIMEOUT || result==usbkey::CANCELLED) {
   usbkey::bus_float();
   error=cancellation.load()==2?"Connection cancelled; flash unchanged":"No ROM boot-key response. Charge the FM-1 if its battery is low, check the data cable, then retry.";
   return false;
  }
  if(result!=usbkey::DP_HIGH_AT_START) {
   update("Clock calibration","FM-1 ROM answered; synchronizing its USB clock");
   auto pulse=usbkey::calibrate(noCancel);
   if(pulse.cancelled || pulse.rmt_error) {usbkey::bus_float();error="USB clock calibration failed or cancelled";return false;}
  }
  if(cancellation.load()==2){usbkey::bus_float();error="Connection cancelled before USB host startup";return false;}
  usbkey::bus_float();
  // The boot key drove the pins as GPIOs and routed D+ to the RMT: clear that
  // before the USB host takes the PHY again.
  usbbus::prepare_for_host(trace);
  if(!startAndWait(vid,pid,error))return false;
 }
 if(host::is_midi()) {
  update("Identify","Querying running FM-1 firmware before requesting ROM mode");
  if(!queryFirmware(observedFirmware,error))return false;
  if(cancellation.load()==2){error="Connection cancelled; flash unchanged";return false;}
  setMode(TransportMode::Unknown);
  update("Entering ROM","Requesting FM-1 boot mode. Its screen may go dark; waiting for USB to reconnect.");
  if(!host::midi_send_sysex(fm1::RomEntryRequest,sizeof fm1::RomEntryRequest,err,sizeof err)){error=err;return false;}
  if(!host::wait_reconnect(12000,&vid,&pid,trace,err,sizeof err)) {
   error=String("FM-1 ROM handoff failed: ")+err+". Power-cycle FM-1 and retry; flash unchanged.";
   return false;
  }
 }
 char descriptor[96];snprintf(descriptor,sizeof descriptor,"Enumerated %04x:%04x (%s)",vid,pid,host::is_midi()?"USB MIDI":"bulk interface");
 trace("%s",descriptor);
 if(vid!=0x4c4a || pid!=0x8057) {error=String(descriptor)+" is not FM-1 ROM 4c4a:8057. Check the connected device.";return false;}
 update("Identify hardware","FM-1 ROM found; loading RAM helper and checking model / flash chip");
 if(!jl::start(&chip,trace,err,sizeof err)) { error=err; return false; }
 if(cancellation.load()==2){error="Connection cancelled; flash unchanged. Restart M5 to reconnect.";return false;}
 uint8_t a[32],b[32];
 if(!jl::read(0xc1220,a,sizeof a,err,sizeof err) || !jl::read(0xff000,b,sizeof b,err,sizeof err)) { error=err; return false; }
 if(chip.chip_key!=0x980f || (chip.flash_id!=0x856014 && chip.flash_id!=0xb36014) ||
    !memcmp(a+4,"trimdat\0",8) || !memcmp(b,"TRIMCOPY",8)) { error="Unknown device or SMK-37 detected: FM-1 operations refused"; return false; }
 identifiedFm1=true;
 xSemaphoreTake(lock,portMAX_DELAY);
 state.connected=true;state.backupVerified=false;state.firmwareLive=false;state.mode=TransportMode::Rom;
 state.flashId=chip.flash_id;state.uniqueId=uniqueId();
 xSemaphoreGive(lock);
 char text[120]; snprintf(text,sizeof text,"FM-1 flash %06lx; RAM loader %s. Hardware behavior unverified on this build.",(unsigned long)chip.flash_id,chip.loader);
 update("Connected",text); return true;
}
bool backupDevice(String& error) {
 char err[160]{};
 if(!identifiedFm1 || !host::connected()) { error="Connect and identify an FM-1 first"; return false; }
 if(!disk->exists("/fm1")) disk->mkdir("/fm1");
 disk->mkdir("/fm1/backups");
 String base;
 for(uint32_t n=1;n<100000;n++) {
  base=String("/fm1/backups/backup-")+n;
  if(!disk->exists(base+".bin") && !disk->exists(base+".part") && !disk->exists(base+".json")) break;
  base="";
 }
 if(!base.length()) { error="Backup filenames exhausted"; return false; }
 String part=base+".part", final=base+".bin";
 uint32_t expected=0,crc=0;
 if(!jl::crc32(0,FlashSize,&expected,err,sizeof err)) { error=err; return false; }
 File file=disk->open(part,FILE_WRITE);
 if(!file) { error="Cannot create backup; insert SD/check space"; return false; }
 for(uint32_t off=0;off<FlashSize;off+=sizeof buffer) {
  if(cancellation.load()==2){file.close();disk->remove(part);error="Backup cancelled; FM-1 flash unchanged";return false;}
  if(!jl::read(off,buffer,sizeof buffer,err,sizeof err)) { file.close(); disk->remove(part); error=err; return false; }
  if(file.write(buffer,sizeof buffer)!=sizeof buffer) { file.close(); disk->remove(part); error="Backup storage full or removed"; return false; }
  crc=jl::crc32_update(crc,buffer,sizeof buffer);
  update("Backup","Reading FM-1 flash",off+sizeof buffer,FlashSize*2);
  vTaskDelay(1);
 }
 file.flush();file.close();
 if(crc!=expected) { disk->remove(part); error="Backup CRC differs from device"; return false; }
 file=disk->open(part,FILE_READ); crc=0;
 if(!file || file.size()!=FlashSize) { error="Backup size verification failed"; return false; }
 for(uint32_t off=0;off<FlashSize;off+=sizeof buffer) {
  if(cancellation.load()==2){file.close();disk->remove(part);error="Backup verification cancelled; FM-1 flash unchanged";return false;}
  if(file.read(buffer,sizeof buffer)!=sizeof buffer) { file.close(); error="Backup reread failed"; return false; }
  crc=jl::crc32_update(crc,buffer,sizeof buffer);update("Backup","Verifying stored backup",FlashSize+off+sizeof buffer,FlashSize*2);vTaskDelay(1);
 }
 file.close();
 if(crc!=expected || !disk->rename(part,final)) { error="Backup verification/commit failed"; return false; }
 File metadata=disk->open(base+".json",FILE_WRITE);
 if(!metadata) { error="Backup saved but metadata failed; cannot mark verified"; return false; }
 char json[384];snprintf(json,sizeof json,"{\"model\":\"FM-1\",\"size\":%lu,\"crc32\":\"%08lx\",\"flash_id\":\"%06lx\",\"chip_key\":\"%04x\",\"unique_id\":\"%s\",\"firmware\":\"%s\",\"verified\":true}\n",(unsigned long)FlashSize,(unsigned long)crc,(unsigned long)chip.flash_id,chip.chip_key,uniqueId().c_str(),observedFirmware.c_str());
 bool wrote=metadata.print(json)==strlen(json);metadata.close();
 if(!wrote) { error="Backup metadata write failed";return false; }
 backupPath=final;backupCrc=crc;
 baselineExists.store(true);
 xSemaphoreTake(lock,portMAX_DELAY); state.backupVerified=true;state.lastBackupPath=backupPath;xSemaphoreGive(lock);
 update("Backup verified",final,FlashSize*2,FlashSize*2);return true;
}
class PackageReader final : public fm1::Reader {
 File& f;
 public: explicit PackageReader(File& file):f(file){}
 uint64_t size()const override{return f.size();}
 bool read(uint64_t off,uint8_t* out,size_t n)override{return off<=UINT32_MAX&&f.seek(uint32_t(off))&&f.read(out,n)==n;}
};
String packageHash(File& file) {
 mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);mbedtls_sha256_starts(&sha,0);
 file.seek(0);
 while(file.available()) {int n=file.read(buffer,sizeof buffer);if(n<=0){mbedtls_sha256_free(&sha);return "";}mbedtls_sha256_update(&sha,buffer,n);vTaskDelay(1);}
 uint8_t digest[32];mbedtls_sha256_finish(&sha,digest);mbedtls_sha256_free(&sha);
 char text[65];for(int i=0;i<32;i++)snprintf(text+i*2,3,"%02x",digest[i]);return text;
}
class RomFlash final : public fm1::Flash {
 public:
 char error[160]{};
 uint32_t size()const override{return FlashSize;}
 bool read(uint32_t address,uint8_t* out,size_t count)override{return address<=FlashSize && count<=FlashSize-address && jl::read(address,out,count,error,sizeof error);}
 bool eraseSector(uint32_t address)override{return fm1::writableSector(address,4096)&&jl::erase(address,4096,error,sizeof error);}
 bool program(uint32_t address,const uint8_t* source,size_t count)override{return fm1::writableSector(address,count)&&jl::program(address,source,count,error,sizeof error);}
};
bool writeCancelled(void*) {return cancellation.load()==2;}
void writeProgress(uint32_t completed,uint32_t total,uint32_t,void*) {
 update("Installing / verifying","Keep power connected; changed sectors read back",completed,total);
 vTaskDelay(1);
}
bool restartAndConfirm(const String& expected,String& error) {
 uint8_t expectedPhase=3;
 if(!cancellation.compare_exchange_strong(expectedPhase,4)){error="Cancelled; FM-1 remains in loader mode. Reinstall or restore before restarting.";return false;}
 char err[160]{};
 bool acknowledged=jl::restart(err,sizeof err);
 identifiedFm1=false;
 xSemaphoreTake(lock,portMAX_DELAY);state.connected=false;state.backupVerified=false;
 state.firmwareLive=false;state.mode=TransportMode::Unknown;xSemaphoreGive(lock);
 update("Checking boot",acknowledged?"Restart acknowledged; waiting for firmware USB MIDI":"Restart unconfirmed; checking for firmware USB MIDI");
 uint16_t vid=0,pid=0;
 if(!host::wait_reconnect(12000,&vid,&pid,trace,err,sizeof err)){error=String("Transfer verified; boot unconfirmed: ")+err;return false;}
 if(!host::is_midi()){error="Transfer verified; normal USB MIDI firmware did not enumerate";return false;}
 String actual;
 if(!queryFirmware(actual,error)){error="Transfer verified; boot identity unconfirmed: "+error;return false;}
 observedFirmware=actual;
 if(!expected.isEmpty() && actual!=expected){error="Transfer verified, but FM-1 reports "+actual+" instead of "+expected;return false;}
 return true;
}
bool installDevice(const char* path,String& error) {
 String selected=path;
 if(!selected.startsWith("/fm1/packages/") || !selected.endsWith(".fwsc") || selected.indexOf("..")>=0) {error="Invalid package path";return false;}
 File file=disk->open(path,FILE_READ);if(!file){error="Package missing";return false;}
 PackageReader reader(file);fm1::PackageInfo package;
 update("Preflight","Validating complete package before writes");
 auto valid=fm1::validatePackage(reader,fm1::DeviceModel::Fm1,package);
 if(valid!=fm1::PackageError::None){error=fm1::errorMessage(valid);return false;}
 String hash=packageHash(file);if(hash.length()!=64){error="Cannot hash package";return false;}
 JsonDocument document;bool matched=false;String support;
 File sidecar=disk->open(selected+".json",FILE_READ);
 if(sidecar && sidecar.size()<=16384 && !deserializeJson(document,sidecar)) {
  String expected=document["sha256"]|"";
  if(expected.length()==64 && expected.equalsIgnoreCase(hash)) {support=document["chip"]|"";matched=support=="new"||support=="old";}
 }
 sidecar.close();document.clear();
 if(!matched) {
  File catalogue=disk->open("/fm1/catalogue.json");
  String catalogueError=scanCatalogue(catalogue,[&](uint32_t,JsonObject entry) {
   String expected=entry["sha256"]|"";
   if(expected.length()==64 && expected.equalsIgnoreCase(hash)) {String candidate=entry["chip"]|"";if(candidate=="new"||candidate=="old"){support=candidate;matched=true;}}
  });
  catalogue.close();
  // Catalogue metadata is optional for local packages. Discard partial results
  // from an invalid catalogue rather than treating them as compatibility evidence.
  if(!catalogueError.isEmpty()){matched=false;support="";}
 }
 // Unknown builds are allowed after the existing physical install confirmation.
 // Package validation, live hardware identification and writer checks still apply.
 if(!matched)update("Preflight","Unlisted or unknown chip support; checking package and connected FM-1");
 if(cancellation.load()==2){error="Install cancelled before connection; flash unchanged";return false;}
 if(!identifiedFm1 || !host::connected()) {
  if(!connectDevice(error))return false;
 }
 if(chip.flash_id==0xb36014 && support=="old") {error="Package supports older flash only; newer FM-1 flash detected";return false;}
 // Retaining a full backup is an explicit Home > Create backup operation.
 // The direct writer keeps protected-region checks and sector readback in RAM.
 RomFlash flash;
 if(packageHash(file)!=hash){error="Package changed before installation; no writes performed";return false;}
 if(!commitWrite(error))return false;
 update("Preflight","Checking live sectors and protected regions; no backup file will be saved");
 auto report=fm1::writePackageWithoutBackup(flash,reader,fm1::DeviceModel::Fm1,writeProgress,nullptr,writeCancelled);
 if(report.error==fm1::WriteError::Cancelled){error="Install cancelled. FM-1 remains in loader mode; reinstall or restore before restarting.";return false;}
 if(report.error!=fm1::WriteError::None) {
  error=String(fm1::errorMessage(report.error))+" at "+String(report.address,HEX)+"; "+flash.error+". Restart withheld.";
  return false;
 }
 uint32_t changed=report.changedSectors;
 uint32_t end=std::min<uint32_t>(package.flashSize,0xff000);
 if(packageHash(file)!=hash){error="Package changed during installation; restart withheld";return false;}
 file.close();
 if(!restartAndConfirm(package.product,error))return false;
 recordSuccessfulOperation("install",selected,hash);
 update("Boot identity confirmed",observedFirmware+"; "+String(changed)+" sectors changed; no backup saved",end-0x4000,end-0x4000);
 return true;
}
bool restoreDevice(const char* path,String& error) {
 if(!identifiedFm1 || !host::connected()) {error="Connect and identify the FM-1 first";return false;}
 String selected=path;
 if(!selected.startsWith("/fm1/backups/") || !selected.endsWith(".bin") || selected.indexOf("..")>=0 || selected.indexOf('\\')>=0) {error="Invalid backup path";return false;}
 File desired=disk->open(selected,FILE_READ);
 String metaPath=selected.substring(0,selected.length()-4)+".json";
 File metadata=disk->open(metaPath,FILE_READ);JsonDocument meta;
 if(!desired || desired.size()!=FlashSize || !metadata || metadata.size()>4096 || deserializeJson(meta,metadata) ||
    meta["model"]!="FM-1" || meta["verified"]!=true || meta["size"].as<uint32_t>()!=FlashSize) {
  error="Restore requires a complete backup with verified FM-1 metadata";return false;
 }
 String flashId=meta["flash_id"]|"",key=meta["chip_key"]|"",crcText=meta["crc32"]|"",uid=meta["unique_id"]|"";
 char expectedFlash[9],expectedKey[5];snprintf(expectedFlash,sizeof expectedFlash,"%06lx",(unsigned long)chip.flash_id);snprintf(expectedKey,sizeof expectedKey,"%04x",chip.chip_key);
 bool validCrc=crcText.length()==8;for(auto c:crcText)validCrc=validCrc&&isxdigit((unsigned char)c);
 if(!validCrc || flashId!=expectedFlash || key!=expectedKey || (chip.has_unique_id && uid!=uniqueId())) {
  error="Backup identity/flash metadata differs from connected FM-1";return false;
 }
 String expectedFirmware=meta["firmware"]|"";
 String restoreHash=packageHash(desired);
 if(restoreHash.length()!=64){error="Cannot hash restore backup";return false;}
 uint32_t desiredCrc=strtoul(crcText.c_str(),nullptr,16);meta.clear();metadata.close();
 update("Restore preflight","Saving a fresh backup before restoring application data");
 if(!backupDevice(error))return false;
 File current=disk->open(backupPath,FILE_READ);
 if(!current){error="Fresh backup unavailable";return false;}
 PackageReader desiredReader(desired),currentReader(current);RomFlash flash;
 if(!commitWrite(error))return false;
 auto report=fm1::restoreBackup(flash,desiredReader,currentReader,desiredCrc,backupCrc,writeProgress,nullptr,writeCancelled);
 if(report.error==fm1::WriteError::Cancelled){error="Restore cancelled. Backups retained; reinstall or restore before restarting FM-1.";return false;}
 if(report.error!=fm1::WriteError::None) {error=String(fm1::errorMessage(report.error))+"; "+flash.error+". Backups retained; restart withheld.";return false;}
 desired.close();current.close();
 if(!restartAndConfirm(expectedFirmware,error))return false;
 recordSuccessfulOperation("restore",selected,restoreHash);
 update("Restore boot confirmed",observedFirmware+"; "+String(report.changedSectors)+" application sectors restored; boot/calibration preserved.");
 return true;
}
void worker(void*) {
 Command cmd;
 for(;;) {
  if(xQueueReceive(commands,&cmd,pdMS_TO_TICKS(1000))==pdTRUE) {
   if (!logPath.length()) {
    disk->mkdir("/fm1"); disk->mkdir("/fm1/logs");
    // Keep the newest MaxSessionLogs-1 logs (each capped at 64 KB), then number
    // the new one after the highest so ordering survives deletions.
    std::vector<uint32_t> numbers;
    File dir=disk->open("/fm1/logs");
    for(File f=dir.openNextFile();f;f=dir.openNextFile()) {
     String name=f.name();name=name.substring(name.lastIndexOf('/')+1);
     if(!f.isDirectory() && name.startsWith("session-") && name.endsWith(".txt")) {
      uint32_t n=name.substring(8,name.length()-4).toInt();
      if(n)numbers.push_back(n);
     }
    }
    dir.close();
    std::sort(numbers.begin(),numbers.end());
    while(numbers.size()>=MaxSessionLogs) {
     disk->remove(String("/fm1/logs/session-")+numbers.front()+".txt");
     numbers.erase(numbers.begin());
    }
    logPath=String("/fm1/logs/session-")+(numbers.empty()?1:numbers.back()+1)+".txt";
   }
   String error; bool ok=false;
   if(cmd.kind==1) ok=connectDevice(error);
   else if(cmd.kind==2) {ok=identifiedFm1 && host::connected();if(!ok)ok=connectDevice(error);if(ok)ok=backupDevice(error);}
   else if(cmd.kind==3) ok=installDevice(cmd.path,error);
   else if(cmd.kind==4) ok=restoreDevice(cmd.path,error);
   if(!ok) update(cancellation.load()==2?"Cancelled":"Error",error);
   // Nothing left on the bus: stop the host, return the pins to idle, 5 V off.
   if(!host::connected()) endUsbSession();
   cancellation.store(0);
   xSemaphoreTake(lock,portMAX_DELAY);
   state.busy=false;state.connected=identifiedFm1 && host::connected();
   if(!host::connected()) {state.firmwareLive=false;state.mode=TransportMode::Disconnected;}
   else if(state.connected)state.mode=TransportMode::Rom;
   else if(!state.firmwareLive)state.mode=TransportMode::Unknown;
   xSemaphoreGive(lock);
  } else if(identifiedFm1) {
   if(!host::connected() || !jl::ping()) {identifiedFm1=false;xSemaphoreTake(lock,portMAX_DELAY);
    state.connected=false;state.backupVerified=false;state.firmwareLive=false;state.mode=TransportMode::Disconnected;
    state.stage="Disconnected";state.detail="FM-1 connection lost; choose Connect to begin a new session";xSemaphoreGive(lock);
    if(!host::connected()) endUsbSession();
   }
  } else if(!host::connected()) {
   endUsbSession();
   xSemaphoreTake(lock,portMAX_DELAY);
   if(state.firmwareLive) {state.firmwareLive=false;state.mode=TransportMode::Disconnected;
    state.stage="Disconnected";state.detail="Last observed firmware retained; reconnect to verify the current FM-1";}
   xSemaphoreGive(lock);
  }
 }
}
bool submit(uint8_t kind,const String& path="") {
 if(!commands || path.length()>=sizeof(Command::path)) return false;
 xSemaphoreTake(lock,portMAX_DELAY);
 if(state.busy) {xSemaphoreGive(lock);return false;}
 state.busy=true; xSemaphoreGive(lock);
 cancellation.store(1);
 Command cmd{};cmd.kind=kind;strlcpy(cmd.path,path.c_str(),sizeof cmd.path);
 if(xQueueSend(commands,&cmd,0)==pdTRUE) return true;
 cancellation.store(0);xSemaphoreTake(lock,portMAX_DELAY);state.busy=false;xSemaphoreGive(lock);return false;
}
}
namespace {
void scanBaseline() {
 bool found=false;
 File backups=disk->open("/fm1/backups");
 for(File file=backups.openNextFile();file;file=backups.openNextFile()) {
  String path=file.name();path=path.substring(path.lastIndexOf('/')+1);
  if(file.isDirectory() || !path.endsWith(".json") || file.size()>4096)continue;
  JsonDocument record;
  if(!deserializeJson(record,file) && record["model"]=="FM-1" && record["verified"]==true && record["size"].as<uint32_t>()==FlashSize && disk->exists("/fm1/backups/"+path.substring(0,path.length()-5)+".bin")) {found=true;break;}
 }
 baselineExists.store(found);
}
}
void transportStorageReset() {
 // Storage was erased while idle: forget anything cached from the old files.
 xSemaphoreTake(lock,portMAX_DELAY);
 deviceRecordJson="{\"schemaVersion\":1,\"history\":[]}";
 state.firmwareProduct="";state.firmwareKnown=false;state.lastBackupPath="";state.backupVerified=false;
 logPath="";
 xSemaphoreGive(lock);
 scanBaseline();
 loadDeviceRecord();
}
void transportBegin(fs::FS& fs) {
 disk=&fs;lock=xSemaphoreCreateMutex();commands=xQueueCreate(1,sizeof(Command));
 scanBaseline();
 loadDeviceRecord();
 state.stage="Idle";state.detail="USB power/wiring and hardware validation required before use.";
 if(!lock || !commands || xTaskCreatePinnedToCore(worker,"fm1_engine",24576,nullptr,2,&workerTask,1)!=pdPASS) {
  commands=nullptr;state.stage="Unavailable";state.detail="Insufficient memory for FM-1 worker";
 }
}
const char* transportModeName(TransportMode mode) {
 switch(mode) {
  case TransportMode::Disconnected:return "disconnected";
  case TransportMode::Rom:return "rom";
  case TransportMode::RunningFirmware:return "firmware";
  case TransportMode::Unknown:return "unknown";
 }
 return "unknown";
}
void transportTick() {}
String transportDeviceRecordJson() {
 if(!lock)return deviceRecordJson;
 xSemaphoreTake(lock,portMAX_DELAY);String copy=deviceRecordJson;xSemaphoreGive(lock);return copy;
}
TransportStatus transportStatus() {if(!lock)return state;xSemaphoreTake(lock,portMAX_DELAY);auto copy=state;xSemaphoreGive(lock);return copy;}
uint32_t transportStackWatermark(){return workerTask?uxTaskGetStackHighWaterMark(workerTask):0;}
bool transportConnect(){return submit(1);}
bool transportBackup(){return submit(2);}
bool transportInstall(const String& path){return submit(3,path);}
bool transportRestore(const String& path){return submit(4,path);}
bool transportCanCancel(){const auto phase=cancellation.load();return phase==1||phase==3;}
bool transportCancel(){
 uint8_t phase=cancellation.load();
 while(phase==1||phase==3){if(cancellation.compare_exchange_weak(phase,2))return true;}
 return phase==2;
}
uint64_t transportBackupReserve(){return 65536;} // Working space only; manual backups use actual free storage.
uint32_t transportFlashId(){const auto snapshot=transportStatus();return snapshot.connected?snapshot.flashId:0;}
bool transportCanDeleteBackup(const String& path) {
 if(transportStatus().busy || !path.startsWith("/fm1/backups/backup-") || !path.endsWith(".bin") || path.indexOf("..")>=0) return false;
 String number=path.substring(strlen("/fm1/backups/backup-"),path.length()-4);
 if(number.isEmpty())return false;
 for(auto c:number)if(!isdigit((unsigned char)c))return false;
 File file=disk->open(path);
 return file && !file.isDirectory();
}
bool transportDeleteBackup(const String& path) {
 if(!transportCanDeleteBackup(path) || !disk->remove(path))return false;
 disk->remove(path.substring(0,path.length()-4)+".json");
 xSemaphoreTake(lock,portMAX_DELAY);
 if(state.lastBackupPath==path){state.lastBackupPath="";state.backupVerified=false;}
 xSemaphoreGive(lock);
 return true;
}
}
