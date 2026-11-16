} // namespace host
int main(){
 using namespace host;
 uint16_t vid=0,pid=0;char error[160]{};
 assert(wait_reconnect(100,&vid,&pid,trace,error,sizeof error));
 assert(closes==1&&waits==1&&remaining==90&&vid==0x4c4a&&pid==0x8057);
 // A late removal for the old handle must never erase an already queued new address.
 dev=reinterpret_cast<void*>(1);claimed_device.store(dev);gone.store(false);new_addr.store(2);
 usb_host_client_event_msg_t event{};event.event=USB_HOST_CLIENT_EVENT_DEV_GONE;event.dev_gone.dev_hdl=dev;
 client_cb(&event,nullptr);assert(gone.load()&&new_addr.load()==2);
 gone.store(false);event.dev_gone.dev_hdl=reinterpret_cast<void*>(3);
 client_cb(&event,nullptr);assert(!gone.load()&&new_addr.load()==2);
 // Disconnect observed before entering the waiter still keeps replacement enumeration.
 gone.store(true);clockMs=0;closes=waits=0;
 assert(wait_reconnect(100,&vid,&pid,trace,error,sizeof error));assert(waits==1&&remaining==100);
 // A firmware which ignores reboot must time out, never claim its old interface as ROM.
 dev=reinterpret_cast<void*>(1);claimed_device.store(dev);gone.store(false);removeAt=1000;clockMs=0;waits=closes=0;
 assert(!wait_reconnect(20,&vid,&pid,trace,error,sizeof error));assert(waits==0&&closes==0&&vid==0&&pid==0);
 // Cleanup failures and in-flight transfers must prevent interface reuse.
 gone.store(true);failClose=true;
 assert(!wait_reconnect(100,&vid,&pid,trace,error,sizeof error));assert(cleanup_failed&&waits==0);
 cleanup_failed=false;failClose=false;inflight=true;
 assert(!wait_reconnect(100,&vid,&pid,trace,error,sizeof error));assert(waits==0);
 std::cout<<"Production USB handoff and event-race tests passed\n";
}
