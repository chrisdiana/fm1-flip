int main(){
 auto has=[](const char* e){return std::find(events.begin(),events.end(),e)!=events.end();};
 for(int scenario=0;scenario<15;++scenario){
  events.clear();state={};chip={};hostStarted=false;identifiedFm1=false;
  enumerated=romRequested=keyed=calibrated=interrupted=portSignal=false;peer=Peer::Running;
  packageValid=packagePresent=backupOk=true;metadataSupport="new";cancellation.store(1);
  cancelDuringWrite=scenario==8;sidecarPresent=true;catalogueHash.clear();catalogueSupport.clear();catalogueFailure.clear();
  if(scenario>=9){sidecarPresent=false;catalogueHash=fixtureHash;catalogueSupport="new";}
  if(scenario==10)catalogueHash=String(64,'b');
  if(scenario==11){sidecarPresent=true;metadataSupport="unknown";}
  if(scenario==13){catalogueHash.clear();catalogueFailure="Catalogue missing";}
  if(scenario==14)catalogueFailure="Invalid catalogue";
  if(scenario==12){sidecarPresent=true;metadataSupport="old";}
  if(scenario==1)packageValid=false;
  if(scenario==2)metadataSupport="unknown";
  if(scenario==3)metadataSupport="old";
  if(scenario==4)backupOk=false;
  if(scenario==5)cancellation.store(2);
  if(scenario==6)packagePresent=false;
  if(scenario==7){identifiedFm1=true;enumerated=true;hostStarted=true;chip.flash_id=0xb36014;}
  String error;bool ok=installDevice("/fm1/packages/test.fwsc",error);
  std::cout<<"Install "<<scenario<<":";for(const auto& e:events)std::cout<<' '<<e;std::cout<<" -> "<<(ok?"OK":error)<<"\n";
  assert(ok==(scenario==0||scenario==2||scenario==4||scenario==7||scenario==9||scenario==10||scenario==11||scenario==13||scenario==14));
  if(scenario==2||scenario==10||scenario==13||scenario==14)assert(has("write")&&has("restart")&&has("record"));
  if(scenario==8)assert(has("write")&&!has("restart")&&!has("record"));
  if(scenario==0){assert(has("enumerate")&&has("loader"));assert(events.front()=="validate");}
  if(scenario==7)assert(!has("enumerate"));
  if(!ok&&scenario!=8)assert(!has("commit")&&!has("write")&&!has("restart")&&!has("record"));
  if(scenario==1||scenario==5||scenario==6)assert(!has("enumerate"));
  if(scenario==3)assert(!has("backup"));
  if(ok){auto at=[&](const char* e){return std::find(events.begin(),events.end(),e)-events.begin();};assert(!has("backup")&&at("commit")<at("write")&&at("write")<at("restart")&&at("restart")<at("record"));}
 }
 std::cout<<"Production automatic install orchestration passed\n";
}
