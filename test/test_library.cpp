#include "device/library.hpp"
#include <cassert>
#include <string>
int main(){
 JsonDocument meta,row;
 meta["id"]="maker-project";meta["name"]="Older saved firmware";meta["version"]="0.7";meta["identity"]="FM-1_007";meta["size"]=99999;meta["hubInstalls"]=42;meta["artworkUrl"]="https://thefirmwarehub.com/fw/cards/old.webp";
 device::storedPackageEntry(row.to<JsonObject>(),meta.as<JsonObjectConst>(),"older.fwsc",1234,"FM-1_007",false);
 assert(row["saved"]==true && row["loaded"]==false && row["size"]==1234);
 assert(row["version"]=="0.7" && row["hubInstalls"]==42);
 assert(row["path"]=="/fm1/packages/older.fwsc");
 row.clear();device::storedPackageEntry(row.to<JsonObject>(),meta.as<JsonObjectConst>(),"older.fwsc",1234,"FM-1_007",true);
 assert(row["loaded"]==true);
 row.clear();device::storedPackageEntry(row.to<JsonObject>(),meta.as<JsonObjectConst>(),"older.fwsc",1234,"FM-1_008",true);
 assert(row["loaded"]==false);
 meta.clear();row.clear();device::storedPackageEntry(row.to<JsonObject>(),meta.as<JsonObjectConst>(),"user-supplied.fwsc",512,"FM-1_007",true);
 assert(row["saved"]==true&&row["local"]==true&&row["loaded"]==false);
 assert(row["name"]=="user-supplied.fwsc"&&row["hubInstalls"].isNull());
 meta["hubInstalls"]=-1;row.clear();device::storedPackageEntry(row.to<JsonObject>(),meta.as<JsonObjectConst>(),"local.fwsc",512,"",false);
 assert(row["hubInstalls"].isNull());
 return 0;
}
