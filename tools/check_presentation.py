#!/usr/bin/env python3
"""Execute the real presentation path against a display that records visible clears."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def legacy_source():
    source = (ROOT / 'src/device/app.cpp').read_text()
    draw = source[source.index('void draw(){'):source.index('void selectStep(')]
    draw = draw.replace('.isEmpty()', '.empty()').replace('.substring(', '.substr(')
    scheduler = 'if(dirty||millis()-lastPaint>1000)draw();'
    if scheduler not in source:
        raise SystemExit('Legacy scheduler changed without updating the regression seam')
    stub = r'''
#include <string>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include "device/gallery.h"
using String=std::string;
using namespace device;
uint32_t virtualTime=0;
uint32_t millis(){return virtualTime;}
struct Display {int clears=0;void fillScreen(int){clears++;}void setTextColor(int,int){}void setTextSize(int){}void setCursor(int,int){}template<class T>void println(T){}template<class...T>void printf(const char*,T...){}void fillRect(int,int,int,int,int){}int width(){return 240;}int height(){return 135;}} DISPLAY;
enum {TFT_BLACK,TFT_WHITE,TFT_YELLOW,TFT_CYAN,TFT_GREEN};
namespace fm1 {enum class ActionKind{Install,Restore};}
struct Snapshot{String label="Example";fm1::ActionKind kind=fm1::ActionKind::Install;};
struct Pending{Snapshot snapshot(){return {};}}pendingAction;
struct Status{bool busy=false;String stage="Idle",detail="Ready";uint32_t completed=0,total=0;};
Status transportStatus(){return {};}bool transportCanCancel(){return false;}uint32_t transportFlashId(){return 0;}
void xSemaphoreTake(int,int){}void xSemaphoreGive(int){}int jobMutex=0,portMAX_DELAY=0;
struct Entry {String name="Hortator",author="Maker",version="1.2",date="2026-11-15",chip="new",path,description="Firmware";bool beta=false,favorite=false;uint32_t size=1024;};
Entry readEntry(int){return {};}int selectedIndex(){return 0;}size_t matchCount(){return 1;}
String notice="Ready",query,categoryFilter="all",releaseFilter="all",compatibilityFilter="all",sortOrder="name",apPassword="test-password",jobStage="Idle",jobDetail;
String ACTIONS[]={"Favorite"};const char CHARACTERS[]=" abc";
bool savedOnly=false,favoritesOnly=false,searchPicker=false,actionMenu=false,confirmInstall=false,detail=false,searching=false,dirty=true;
std::atomic<bool> jobBusy(false);size_t characterIndex=0,actionIndex=0,selected=0,detailOffset=0;uint32_t lastPaint=0;
'''
    return stub + draw + '\nvoid tick(){' + scheduler + r'''}
int main(){for(uint32_t now:{0u,1100u,2200u}){virtualTime=now;tick();}std::cout<<"visible full-screen clears="<<DISPLAY.clears<<"; idle draws="<<DISPLAY.clears<<"\n";return DISPLAY.clears==0?0:1;}
'''


def main():
    with tempfile.TemporaryDirectory(prefix='fm1-presentation-') as directory:
        tmp = Path(directory)
        if (ROOT / 'src/device/presentation.cpp').exists():
            sources = [str(ROOT / 'src/device/presentation.cpp'), str(ROOT / 'src/device/local_controller.cpp'), str(ROOT / 'test/test_presentation.cpp')]
        else:
            (tmp / 'legacy.cpp').write_text(legacy_source())
            sources = [str(tmp / 'legacy.cpp')]
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-I'+str(ROOT/'include'), *sources, '-o', str(tmp/'test')], check=True)
        subprocess.run([str(tmp/'test'), str(ROOT/'.cache/ui-previews')], check=True)


if __name__ == '__main__':
    main()
