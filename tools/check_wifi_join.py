#!/usr/bin/env python3
"""Replay the production Wi-Fi join poller without radio hardware."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/device/local_app.inc').read_text()
fn=s[s.index('void pollWifiScan()'):s.index('void chooseWifi()')].replace('.isEmpty()', '.empty()')
with tempfile.TemporaryDirectory(prefix='fm1-wifi-') as name:
 p=Path(name)
 (p/'main.cpp').write_text(r'''
#include <string>
#include <vector>
#include <cstdint>
#include <cassert>
#include <atomic>
#include <iostream>
using String=std::string;
enum class LocalPage{Home,Wifi};LocalPage localPage=LocalPage::Wifi;
struct Job{};Job pendingNetworkJob;int starts=0;bool ready=true;bool startJob(const Job&){++starts;return true;}String networkPreflight(){return ready?"":"Waiting for clock";}
bool networkWaiting=false;uint32_t networkReadyStarted=0;
bool wifiConnecting=true,wifiScanning=false,dirty=false,actionMenu=false,homeMenu=false;uint32_t wifiConnectStarted=0,now=1000;
std::atomic<uint16_t> wifiDisconnectReason{0};
String notice,title,body,editingSsid="fixture-network",wifiResult="Not connected";
uint32_t millis(){return now;}
enum{WL_CONNECTED=3,WL_CONNECT_FAILED=4,WL_NO_SSID_AVAIL=1,WIFI_SCAN_RUNNING=-1,WIFI_AUTH_OPEN=0};
struct IP {operator uint32_t()const{return 1;}String toString(){return "192.0.2.10";}};
struct {int state=WL_CONNECTED;int status(){return state;}IP localIP(){return {};}void disconnect(bool){}int scanComplete(){return -1;}String SSID(int=0){return "fixture-network";}int RSSI(int){return -40;}int encryptionType(int){return 1;}void scanDelete(){}}WiFi;
struct Choice{String ssid;int rssi;bool open;};std::vector<Choice>wifiChoices;
void showLocalInfo(const String& t,const String& b){title=t;body=b;}
String wifiJoinFailure(int,uint16_t){return "Join failed; retry Wi-Fi setup";}
'''+fn+r'''
int main(){
 pollWifiScan();assert(!wifiConnecting&&title.empty()&&wifiResult.find("Connected")!=String::npos&&"Successful joining must not interrupt navigation");
 wifiConnecting=true;WiFi.state=WL_CONNECT_FAILED;title.clear();pollWifiScan();assert(!wifiConnecting&&title=="Wi-Fi not joined");
 wifiConnecting=true;WiFi.state=0;now=31000;title.clear();pollWifiScan();assert(!wifiConnecting&&title=="Wi-Fi not joined");
 networkWaiting=true;wifiConnecting=false;WiFi.state=WL_CONNECTED;ready=false;now=31001;networkReadyStarted=now;pollWifiScan();assert(networkWaiting&&starts==0);
 ready=true;pollWifiScan();assert(!networkWaiting&&starts==1);
 std::cout<<"Wi-Fi joining: quiet success, failure/timeout and deferred download after clock sync passed\n";
}
''')
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra',str(p/'main.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
