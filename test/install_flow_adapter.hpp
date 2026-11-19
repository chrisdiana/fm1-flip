#include <ArduinoJson.h>
#include <functional>
#include <cassert>
bool packageValid=true,backupOk=true,packagePresent=true;
String metadataSupport="new";
bool cancelDuringWrite=false;
constexpr uint32_t FlashSize=0x100000;
uint32_t backupCrc=1;
String backupPath="/fm1/backups/backup-1.bin";
const String fixtureHash(64,'a');
bool sidecarPresent=true;String catalogueHash,catalogueSupport,catalogueFailure;
struct File {
 String bytes;size_t offset=0,length=0;bool present=true;
 explicit operator bool()const{return present;}
 size_t size(){return length?length:bytes.size();}
 int read(){return offset<bytes.size()?static_cast<unsigned char>(bytes[offset++]):-1;}
 size_t readBytes(char* data,size_t n){size_t i=0;for(;i<n;++i){int c=read();if(c<0)break;data[i]=c;}return i;}
 void close(){}
};
constexpr int FILE_READ=0;
struct Disk {
 File open(const String& path,int=0){
  if(path==backupPath)return {"",0,FlashSize,true};
  if(path=="/fm1/packages/test.fwsc")return {"package",0,0,packagePresent};
  if(path=="/fm1/packages/test.fwsc.json"&&!sidecarPresent)return {"",0,0,false};
  if(path=="/fm1/packages/test.fwsc.json")return {"{\"sha256\":\""+fixtureHash+"\",\"chip\":\""+metadataSupport+"\"}"};
  return {"",0,0,false};
 }
}fakeDisk;
Disk* disk=&fakeDisk;
struct PackageReader {explicit PackageReader(File&){};};
struct RomFlash {String error;};
namespace fm1 {
enum class DeviceModel{Fm1};enum class PackageError{None,Invalid};enum class WriteError{None,Cancelled};
struct PackageInfo {String product="Fixture";uint32_t flashSize=FlashSize;};
struct WriteReport {WriteError error=WriteError::None;uint32_t address=0,changedSectors=1;};
PackageError validatePackage(PackageReader&,DeviceModel,PackageInfo&){events.push_back("validate");return packageValid?PackageError::None:PackageError::Invalid;}
const char* errorMessage(PackageError){return "Invalid package";}
const char* errorMessage(WriteError){return "Write error";}
WriteReport writePackageWithoutBackup(RomFlash&,PackageReader&,DeviceModel,void(*)(uint32_t,uint32_t,uint32_t,void*),void*,bool(*)(void*)) {events.push_back("write");if(cancelDuringWrite){cancellation.store(2);return {WriteError::Cancelled,0,1};}return {};}
}
String packageHash(File&){events.push_back("hash");return fixtureHash;}
String scanCatalogue(File&,std::function<void(uint32_t,JsonObject)> visit){if(!catalogueHash.empty()){JsonDocument entry;entry["sha256"]=catalogueHash;entry["chip"]=catalogueSupport;visit(1,entry.as<JsonObject>());}return catalogueFailure;}
bool backupDevice(String& error){events.push_back("backup");if(!backupOk)error="Backup failed";return backupOk;}
bool commitWrite(String&){events.push_back("commit");return true;}
void writeProgress(uint32_t,uint32_t,uint32_t,void*){}
bool restartAndConfirm(const String&,String&){events.push_back("restart");return true;}
void recordSuccessfulOperation(const String&,const String&,const String&){events.push_back("record");}

bool writeCancelled(void*){return cancellation.load()==2;}
