#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <iostream>
namespace host {
using Log=void(*)(const char*,...);
using TickType_t=uint32_t;
constexpr uint32_t portTICK_PERIOD_MS=1;
uint32_t pdMS_TO_TICKS(uint32_t n){return n;}
uint32_t clockMs=0,removeAt=10;
bool ready=true,cleanup_failed=false,claimed=true,failClose=false,inflight=false;
void* dev=reinterpret_cast<void*>(1);
std::atomic<void*> claimed_device{dev};
std::atomic<int> new_addr{-1};
std::atomic<bool> gone{false},executing{false};
constexpr int USB_HOST_CLIENT_EVENT_NEW_DEV=1,USB_HOST_CLIENT_EVENT_DEV_GONE=2;
struct usb_host_client_event_msg_t {int event;struct {int address;}new_dev;struct{void* dev_hdl;}dev_gone;};
bool pending(){return inflight;}
uint32_t xTaskGetTickCount(){return clockMs;}
void vTaskDelay(uint32_t n){clockMs+=n;if(clockMs>=removeAt){gone.store(true);new_addr.store(2);}}
int closes=0,waits=0;uint32_t remaining=0;
bool close_device(){++closes;if(failClose)return false;dev=nullptr;claimed_device.store(nullptr);gone.store(false);return true;}
bool wait_device(uint32_t ms,uint16_t* vid,uint16_t* pid,Log,char*,size_t){++waits;remaining=ms;if(new_addr.load()!=2)return false;*vid=0x4c4a;*pid=0x8057;return true;}
void trace(const char*,...){}
