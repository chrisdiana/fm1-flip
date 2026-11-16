#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <atomic>
#include <algorithm>
#include <iostream>
using String=std::string;
std::vector<std::string> events;
int lock=0,portMAX_DELAY=0;
void xSemaphoreTake(int,int){} void xSemaphoreGive(int){}
int pdMS_TO_TICKS(int n){return n;} void vTaskDelay(int){}
enum class TransportMode { Unknown, Rom, RunningFirmware };
struct {bool connected=false,backupVerified=false,firmwareLive=false;TransportMode mode=TransportMode::Unknown;uint32_t flashId=0;String uniqueId;}state;
bool identifiedFm1=false,hostStarted=false;
String observedFirmware;
std::atomic<int> cancellation{0};
void update(const char*,const String&,uint32_t=0,uint32_t=0){}
void trace(const char*,...){}
String uniqueId(){return "test-peer";}
void setMode(TransportMode mode){state.mode=mode;}
bool noCancel(){return false;}
namespace usbkey {
struct KeyStats{};
enum KeyResult { TIMEOUT,CANCELLED,DP_HIGH_AT_START,ANSWER_ACK };
struct PulseResult {bool cancelled=false,rmt_error=false;};
void init(){events.push_back("key-init");}
KeyResult send_key(KeyStats*,uint32_t,bool(*)(const KeyStats*));
PulseResult calibrate(bool(*)());
void bus_float(){events.push_back("float");}
}
bool cancelled(const usbkey::KeyStats*){return cancellation.load()==2;}
namespace fm1 {constexpr uint8_t RomEntryRequest[6]={0xf0,0x22,0x24,0x35,0x7d,0xf7};}
namespace jl {
struct Info {uint32_t chip_key=0,flash_id=0;const char* loader="fixture";}chipType;
bool start(Info* info,void(*)(const char*,...),char*,size_t){events.push_back("loader");info->chip_key=0x980f;info->flash_id=0xb36014;return true;}
bool read(uint32_t,uint8_t* data,size_t n,char*,size_t){memset(data,0,n);return true;}
}
jl::Info chip;
enum class Peer { Running,DirectRom,Cold,Unknown,Missing,NoIdentity,NoRom };
Peer peer=Peer::Running;
bool enumerated=false,romRequested=false,keyed=false,calibrated=false,interrupted=false;
namespace host {
bool stop(){events.push_back("stop");if(romRequested)interrupted=true;enumerated=false;return true;}
bool connected(){return enumerated;}
bool is_midi(){return enumerated && !romRequested && (peer==Peer::Running||peer==Peer::NoIdentity||peer==Peer::NoRom);}
bool wait_reconnect(uint32_t,uint16_t* vid,uint16_t* pid,void(*)(const char*,...),char* error,size_t n){
 events.push_back("reconnect");
 if(peer==Peer::NoRom||interrupted){snprintf(error,n,"no ROM response");return false;}
 enumerated=true;*vid=0x4c4a;*pid=0x8057;return true;
}
bool midi_send_sysex(const uint8_t*,size_t,char*,size_t){events.push_back("rom-request");romRequested=true;return true;}
}
bool queryFirmware(String& product,String& error){events.push_back("identity");if(peer==Peer::NoIdentity){error="No valid identity";return false;}product="Fixture";return true;}
bool startAndWait(uint16_t& vid,uint16_t& pid,String& error){
 events.push_back("enumerate");hostStarted=true;
 if(peer==Peer::Missing || (peer==Peer::Cold&&!calibrated) || interrupted || (peer==Peer::NoRom&&romRequested)){error="no USB device";return false;}
 enumerated=true;vid=peer==Peer::Unknown?0x1234:host::is_midi()?0x1235:0x4c4a;pid=host::is_midi()?0x1111:0x8057;return true;
}
namespace usbkey {
KeyResult send_key(KeyStats*,uint32_t,bool(*)(const KeyStats*)){
 events.push_back("key");
 if(peer==Peer::Missing||peer==Peer::NoRom)return TIMEOUT;
 if(peer==Peer::Cold||romRequested){keyed=true;return ANSWER_ACK;}
 return DP_HIGH_AT_START;
}
PulseResult calibrate(bool(*)()){events.push_back("calibrate");calibrated=true;return {};}
}
