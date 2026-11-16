#include "device/gallery.h"
#include <cassert>
using namespace device::gallery;
int main(){
 assert(compatibility("new",0x856014)==Compatibility::DeclaredMatch);
 assert(compatibility("new",0xb36014)==Compatibility::DeclaredMatch);
 assert(compatibility("old",0xb36014)==Compatibility::Incompatible);
 assert(compatibility("old",0x856014)==Compatibility::DeclaredMatch);
 assert(compatibility("any",0xb36014)==Compatibility::Unknown);
 assert(compatibility("new",0)==Compatibility::Unknown);
 assert(releaseMatches(true,"beta")&&!releaseMatches(true,"stable"));
 assert(releaseMatches(false,"stable")&&releaseMatches(true,"all"));
 assert(categoryMatches("synth groove","groove")&&!categoryMatches("groove","gro"));
 auto older=sortKey("Alpha","2026-01-01"),newer=sortKey("Zulu","2026-11-15"),unknown=sortKey("No date","");
 assert(less(older,newer,false,0,1));assert(less(newer,older,true,1,0));assert(less(older,unknown,true,0,2));
 assert(sortKey("Alpha","invalid").date[0]==0);assert(less(older,older,false,0,1));
 auto popular=sortKey("Zulu","2026-01-01",4200,true);
 auto obscure=sortKey("Alpha","2026-11-15",12,true);
 auto zero=sortKey("Known zero","",0,true);
 auto missing=sortKey("Unknown","",0,false);
 assert(less(popular,obscure,SortMode::Popular,1,0));
 assert(less(zero,missing,SortMode::Popular,0,1));
 assert(less(obscure,popular,SortMode::Name,0,1));
 assert(less(obscure,popular,SortMode::Newest,0,1));
 assert(sortMode("popular")==SortMode::Popular && sortMode("")==SortMode::Popular);
}
