#!/usr/bin/env python3
"""Offline checks for production Wi-Fi/TLS explanations and hotspot policy."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='fm1-network-') as name:
 p=Path(name)
 (p/'Arduino.h').write_text(r'''#pragma once
#include <string>
#include <cstdint>
class String:public std::string {public:using std::string::string;String(std::string s):std::string(s){}String(int n):std::string(std::to_string(n)){}String(unsigned n):std::string(std::to_string(n)){}String(unsigned long n):std::string(std::to_string(n)){}bool isEmpty()const{return empty();}int indexOf(const char* s)const{auto n=find(s);return n==npos?-1:int(n);}int indexOf(char c,int start)const{auto n=find(c,start);return n==npos?-1:int(n);}String substring(size_t a,size_t b=npos)const{return substr(a,b==npos?npos:b-a);}};
struct {unsigned getFreeHeap(){return 32768;}}ESP;
''')
 (p/'WiFi.h').write_text('''#pragma once
#include "Arduino.h"
enum{WL_CONNECTED=3,WL_CONNECT_FAILED=4,WL_NO_SSID_AVAIL=1};
struct IP{uint32_t value=1;operator uint32_t()const{return value;}};
struct {int state=3;IP ip;int status(){return state;}IP localIP(){return ip;}}WiFi;
''')
 (p/'WiFiClientSecure.h').write_text('''#pragma once
#include <cstring>
struct WiFiClientSecure{int code=0;int lastError(char* text,size_t n){if(code)strncpy(text,"TLS allocation failed",n);return code;}};
''')
 (p/'HTTPClient.h').write_text('#pragma once\n')
 (p/'esp_heap_caps.h').write_text('#pragma once\n#define MALLOC_CAP_8BIT 1\ninline unsigned heap_caps_get_largest_free_block(int){return 8192;}\n')
 (p/'main.cpp').write_text(r'''
#include <time.h>
#include <cassert>
#include <iostream>
time_t now=1800000000;time_t fakeTime(time_t*){return now;}
#define time fakeTime
#include "device/network_diagnostics.h"
int main(){using namespace device;
 WiFi.state=0;assert(networkPreflight().find("not connected")!=String::npos);
 WiFi.state=3;WiFi.ip.value=0;assert(networkPreflight().find("no IP")!=String::npos);
 WiFi.ip.value=1;now=0;assert(networkPreflight().find("clock not synchronized")!=String::npos);
 now=1800000000;assert(networkPreflight().empty());
 WiFiClientSecure tls;tls.code=-32512;
 auto failure=httpsFailure(-1,tls,"https://api.github.com/example?private=fixture");
 assert(failure.find("TLS allocation failed")!=String::npos&&failure.find("8192")!=String::npos);
 assert(failure.find("private")==String::npos&&failure.find("api.github.com")!=String::npos);
 assert(wifiJoinFailure(0,202).find("Authentication")!=String::npos);
 assert(wifiJoinFailure(1,201).find("2.4 GHz")!=String::npos);
 std::cout<<"Network diagnostics: offline, no IP, NTP clock, TLS allocation detail and safe hostname passed\n";
}
''')
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(p),'-I'+str(root/'include'),str(p/'main.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
source=(root/'src/device/app.cpp').read_text()
policy=source[source.index('bool setHotspot('):source.index('void configureWeb()')].replace('.isEmpty()', '.empty()')
with tempfile.TemporaryDirectory(prefix='fm1-hotspot-') as name:
 p=Path(name)
 (p/'main.cpp').write_text(r'''
#include <string>
#include <cassert>
#include <iostream>
bool apEnabled=false,wifiEnabled=false,webConfigured=true,wifiConnecting=false,dirty=false;std::string apPassword="fixture-password",wifiResult;
struct Flag{bool load(){return false;}}jobBusy;
struct Status{bool busy=false;};Status transportStatus(){return {};}
constexpr int WIFI_OFF=0,WIFI_STA=1;
struct {bool station=true,ap=false,stoppedRadio=false;bool mode(int mode){station=mode!=WIFI_OFF;if(!mode)ap=false;return true;}void disconnect(bool){}bool softAP(const char*,const char*){ap=true;return true;}bool softAPdisconnect(bool stop){stoppedRadio=stop;ap=false;return true;}}WiFi;
using String=std::string;
struct{bool saved=false,power=true;void putBool(const char* key,bool value){if(std::string(key)=="apEnabled")saved=value;else power=value;}bool getBool(const char*,bool){return saved;}String getString(const char*,const char*){return "";}}preferences;
struct{void stop(){}void begin(){}}server;
void configTime(int,int,const char*,const char*){}void startWifiJoin(const String&,const String&){}
bool setWifiPower(bool,bool=true);
'''+policy+r'''
int main(){assert(setHotspot(true)&&apEnabled&&preferences.saved);assert(setHotspot(false)&&!apEnabled&&!preferences.saved&&WiFi.stoppedRadio&&WiFi.station);assert(setWifiPower(false)&&!wifiEnabled&&!WiFi.station&&!WiFi.ap&&!preferences.power);assert(setWifiPower(true)&&wifiEnabled&&WiFi.station&&preferences.power);assert(setHotspot(true)&&preferences.saved);assert(setWifiPower(false)&&!WiFi.ap&&preferences.saved);assert(setWifiPower(true)&&WiFi.ap&&preferences.saved);std::cout<<"Hotspot: saved on/off, AP radio stopped, station preserved\n";}
''')
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(p/'main.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
assert 'preferences.getBool("apEnabled",false)' in source
