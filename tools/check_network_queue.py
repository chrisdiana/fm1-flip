#!/usr/bin/env python3
"""Replay the production radio-off confirmation without a Wi-Fi adapter."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/device/local_app.inc').read_text()
fn=s[s.index('bool requestNetworkJob('):s.index('void beginWifiScan()')].replace('.isEmpty()', '.empty()')
with tempfile.TemporaryDirectory(prefix='fm1-network-queue-') as name:
 p=Path(name)
 (p/'main.cpp').write_text(r'''
#include <string>
#include <cstdint>
#include <cassert>
#include <iostream>
using String=std::string;
struct Job{int id=0;};Job pendingNetworkJob;
bool wifiEnabled=false,networkPrompt=false,networkWaiting=false,actionMenu=true,searchPicker=false,detail=false,dirty=false;
size_t networkPromptIndex=0;uint32_t networkReadyStarted=0;int jobs=0,joins=0,powerChanges=0;
enum class LocalPage{Home};LocalPage localPage=LocalPage::Home;
String savedSsid="fixture-network",title;
struct{String getString(const char* key,const char*){return String(key)=="ssid"?savedSsid:"fixture-password";}}preferences;
bool locked(){return networkPrompt||networkWaiting;}
bool startJob(const Job&){++jobs;return true;}
bool setWifiPower(bool enabled,bool){++powerChanges;wifiEnabled=enabled;return true;}
void startWifiJoin(const String&,const String&){++joins;}
void showHome(){networkPrompt=false;networkWaiting=false;pendingNetworkJob={};}
void showLocalInfo(const String& t,const String&){title=t;}
uint32_t millis(){return 1234;}
'''+fn+r'''
int main(){
 assert(requestNetworkJob({7})&&networkPrompt&&!wifiEnabled&&powerChanges==0&&jobs==0);
 assert(!requestNetworkJob({8})&&pendingNetworkJob.id==7);
 networkPromptIndex=1;chooseNetworkPrompt();assert(!wifiEnabled&&!networkPrompt&&!networkWaiting&&jobs==0);
 assert(requestNetworkJob({9}));chooseNetworkPrompt();assert(wifiEnabled&&networkWaiting&&!networkPrompt&&joins==1&&jobs==0&&pendingNetworkJob.id==9);
 showHome();wifiEnabled=false;savedSsid="";assert(requestNetworkJob({10}));chooseNetworkPrompt();assert(!wifiEnabled&&!networkWaiting&&title=="Wi-Fi setup needed");
 wifiEnabled=true;assert(requestNetworkJob({11})&&jobs==1);
 std::cout<<"Network queue: no radio activity before confirmation, cancel leaves power off, reconnect queues once, missing profile and online path passed\n";
}
''')
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(p/'main.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
