#pragma once
#include <cstdint>
#include <cstring>

namespace fm1 {
enum class ActionKind { Install, Restore };
struct ActionSnapshot {
 ActionKind kind = ActionKind::Install;
 char path[160]{};
 char label[96]{};
};
// UI-thread owned. An armed action cannot be replaced by a second request;
// consume returns exactly the reviewed path, independent of gallery selection.
class PendingAction {
 public:
  bool arm(ActionKind kind,const char* path,const char* label) {
   if(active_ || !path || !label || !*path || std::strlen(path)>=sizeof action_.path)return false;
   action_={};action_.kind=kind;std::strcpy(action_.path,path);
   std::strncpy(action_.label,label,sizeof action_.label-1);
   active_=true;++generation_;return true;
  }
  bool active()const{return active_;}
  uint32_t generation()const{return generation_;}
  const ActionSnapshot& snapshot()const{return action_;}
  bool consume(ActionSnapshot& action){if(!active_)return false;action=action_;cancel();return true;}
  void cancel(){active_=false;action_={};}
 private:
  ActionSnapshot action_{};
  bool active_=false;
  uint32_t generation_=0;
};
}
