int main(){
 for(auto scenario:{Peer::Running,Peer::DirectRom,Peer::Cold,Peer::Unknown,Peer::Missing,Peer::NoIdentity,Peer::NoRom}) {
  peer=scenario;state={};chip={};hostStarted=false;identifiedFm1=false;observedFirmware="";
  events.clear();enumerated=romRequested=keyed=calibrated=interrupted=false;
  String error;bool ok=connectDevice(error);
  std::cout<<"Peer "<<int(peer)<<":";for(const auto& e:events)std::cout<<' '<<e;std::cout<<" -> "<<(ok?"OK":error)<<"\n";
  auto contains=[](const char* e){return std::find(events.begin(),events.end(),e)!=events.end();};
  bool expected=peer==Peer::Running||peer==Peer::DirectRom||peer==Peer::Cold;
  if(ok!=expected || events.empty() || events.front()!="enumerate" || interrupted) return 1;
  if(peer==Peer::Running && (!contains("identity")||!contains("reconnect")||contains("key")))return 1;
  if(peer==Peer::DirectRom && (contains("key")||contains("rom-request")))return 1;
  if(peer==Peer::Cold && (!keyed||!calibrated))return 1;
  if((peer==Peer::NoIdentity||peer==Peer::Unknown) && (contains("key")||contains("rom-request")))return 1;
  if(ok && (!identifiedFm1||!state.connected))return 1;
  if(!ok && identifiedFm1)return 1;
 }
 std::cout<<"Production connection flow passed (running, ROM, cold boot, absent, unknown, identity and handoff failures)\n";
}
