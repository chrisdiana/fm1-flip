#include "fm1/confirmation.hpp"
#include <cassert>
#include <cstring>
#include <string>
int main(){
 fm1::PendingAction pending;fm1::ActionSnapshot action;
 assert(!pending.consume(action));
 assert(pending.arm(fm1::ActionKind::Install,"/fm1/packages/reviewed.fwsc","Reviewed firmware 1"));
 auto generation=pending.generation();
 assert(!pending.arm(fm1::ActionKind::Restore,"/fm1/backups/other.bin","Different action"));
 assert(pending.generation()==generation);
 assert(!std::strcmp(pending.snapshot().path,"/fm1/packages/reviewed.fwsc"));
 assert(pending.consume(action) && action.kind==fm1::ActionKind::Install);
 assert(!std::strcmp(action.path,"/fm1/packages/reviewed.fwsc") && !pending.active());
 assert(!pending.consume(action));
 assert(pending.arm(fm1::ActionKind::Restore,"/fm1/backups/backup-1.bin","Backup 1"));
 assert(pending.generation()!=generation);pending.cancel();assert(!pending.consume(action));
 assert(!pending.arm(fm1::ActionKind::Install,std::string(160,'x').c_str(),"Oversized"));
 assert(!pending.arm(fm1::ActionKind::Install,"","Empty"));
 assert(!pending.active());
 return 0;
}
