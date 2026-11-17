#!/usr/bin/env python3
"""Run the installed IDF descriptor parser with the configured rebuilt USB limit."""
from pathlib import Path
import argparse,configparser,re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--env',default='sticks3',choices=['sticks3','cardputer']);a=p.parse_args()
config=configparser.ConfigParser(interpolation=None);config.read(root/'platformio.ini')
option=config['env:'+a.env]['custom_sdkconfig']
expected=re.search(r'CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=(\d+)',option)
if not expected:raise SystemExit('Target must rebuild USB through custom_sdkconfig')
limit=int(expected.group(1))
if limit<1024:raise SystemExit('Composite USB descriptor support requires a limit of at least 1024')
# HybridCompile removes temporary CMake settings after exporting its SDK. If
# they are present, validate the resolved value too. The production static_assert
# prevents building the app against the original 256-byte SDK headers.
for header in [root/'.pio/build'/a.env/'config/sdkconfig.h',root/('sdkconfig.'+a.env)]:
 if not header.exists():continue
 match=re.search(r'^(?:#define CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE |CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=)(\d+)$',header.read_text(),re.M)
 if not match or int(match.group(1))!=limit:raise SystemExit('Resolved SDK limit does not match the target configuration')
 break
sdk=next((Path.home()/'.platformio/packages').glob('framework-espidf*/components/usb/enum.c'),None)
if sdk is None:raise SystemExit('Installed ESP-IDF enumeration source missing')
s=sdk.read_text();begin=s.index('static esp_err_t parse_short_config_desc(void)');end=s.index('\n/**',begin)
function=s[begin:end]
adapter=r'''
#include <cstdint>
#include <cassert>
#include <iostream>
#define ESP_LOGE(...) ((void)0)
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_ERR_INVALID_RESPONSE=1,ESP_ERR_INVALID_SIZE=2,USB_B_DESCRIPTOR_TYPE_CONFIGURATION=2;
struct usb_setup_packet_t{uint8_t bytes[8];};
struct __attribute__((packed)) usb_config_desc_t{uint8_t bLength,bDescriptorType;uint16_t wTotalLength;};
alignas(8) uint8_t bytes[32]{};
struct usb_transfer_t{uint8_t* data_buffer=bytes;};
struct Urb{usb_transfer_t transfer;};Urb urb;
struct EnumDriver{struct{Urb* urb;}constant;struct{struct{uint16_t wTotalLength=0;}dev_params;}single_thread;}driver;
EnumDriver* p_enum_driver=&driver;
'''
main=r'''
int main(){
 driver.constant.urb=&urb;
 auto* desc=reinterpret_cast<usb_config_desc_t*>(bytes+sizeof(usb_setup_packet_t));
 desc->bLength=9;desc->bDescriptorType=USB_B_DESCRIPTOR_TYPE_CONFIGURATION;
 // Composite audio/MIDI/CDC configuration sizes derived from the public
 // Patchwerk/felucca descriptor layouts; no device-private bytes are used.
 for(uint16_t length:{uint16_t(282),uint16_t(288),uint16_t(326)}){
  desc->wTotalLength=length;
  int result=parse_short_config_desc();
  std::cout<<"IDF parser: descriptor "<<length<<" bytes, limit "<<ENUM_CTRL_TRANSFER_MAX_DATA_LEN<<", result "<<result<<"\n";
  if(result!=ESP_OK)return 1;
  assert(driver.single_thread.dev_params.wTotalLength==length);
 }
 desc->wTotalLength=ENUM_CTRL_TRANSFER_MAX_DATA_LEN+1;
 assert(parse_short_config_desc()==ESP_ERR_INVALID_SIZE);
 desc->wTotalLength=64;desc->bDescriptorType=1;
 assert(parse_short_config_desc()==ESP_ERR_INVALID_RESPONSE);
}
'''
with tempfile.TemporaryDirectory(prefix='fm1-usb-descriptor-') as name:
 path=Path(name);(path/'test.cpp').write_text(f'#define ENUM_CTRL_TRANSFER_MAX_DATA_LEN {limit}\n'+adapter+function+main)
 # The same installed driver rejects these configurations under the old SDK.
 (path/'baseline.cpp').write_text('#define ENUM_CTRL_TRANSFER_MAX_DATA_LEN 256\n'+adapter+function+main)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(path/'baseline.cpp'),'-o',str(path/'baseline')],check=True)
 baseline=subprocess.run([str(path/'baseline')],capture_output=True,text=True)
 if baseline.returncode!=1:raise SystemExit('Expected stock 256-byte parser to reject composite descriptor')
 print('Confirmed baseline: installed IDF parser rejects 282-byte configuration with limit 256.',flush=True)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True)
print('Installed IDF parser accepts composite configurations and still rejects over-limit/corrupt headers.')
