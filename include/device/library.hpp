#pragma once
#include <ArduinoJson.h>
#include <cstdint>
#include <string>

namespace device {
inline std::string libraryText(JsonVariantConst value, size_t limit) {
 if(!value.is<const char*>())return {};
 std::string result=value.as<const char*>();
 if(result.size()>limit)result.resize(limit);
 return result;
}
// A library entry is projected from a real file, independently of the current
// remote catalogue. A live badge means a reported identity match, never that
// merely downloading a package installed it on the FM-1.
inline void storedPackageEntry(JsonObject output, JsonObjectConst metadata,
                               const std::string& filename, uint32_t bytes,
                               const std::string& observed, bool live) {
 auto id=libraryText(metadata["id"],96);
 auto name=libraryText(metadata["name"],80);
 output["id"]=id.empty()?"local-"+filename:id;
 output["local"]=id.empty();
 output["name"]=name.empty()?filename:name;
 output["path"]="/fm1/packages/"+filename;
 output["size"]=bytes;
 output["saved"]=true;
 output["downloadable"]=false;
 output["author"]=libraryText(metadata["author"],80);
 output["description"]=libraryText(metadata["description"],640);
 output["version"]=libraryText(metadata["version"],40);
 output["date"]=libraryText(metadata["date"],64);
 output["license"]=libraryText(metadata["license"],96);
 output["sourceUrl"]=libraryText(metadata["sourceUrl"],768);
 output["projectUrl"]=libraryText(metadata["projectUrl"],768);
 output["releaseUrl"]=libraryText(metadata["releaseUrl"],768);
 output["hubUrl"]=libraryText(metadata["hubUrl"],256);
 output["artworkUrl"]=libraryText(metadata["artworkUrl"],768);
 output["sha256"]=libraryText(metadata["sha256"],64);
 output["chip"]=libraryText(metadata["chip"],16);
 output["beta"]=metadata["beta"]|false;
 if(metadata["hubInstalls"].is<uint32_t>()&&metadata["hubInstalls"].as<uint32_t>()<=2147483647u)
  output["hubInstalls"]=metadata["hubInstalls"].as<uint32_t>();
 else output["hubInstalls"]=nullptr;
 auto identity=libraryText(metadata["identity"],64);
 output["identity"]=identity;
 output["loaded"]=live&&!identity.empty()&&identity==observed;
 output["loadedMatch"]="reported-identity";
 auto tags=output["tags"].to<JsonArray>();
 for(JsonVariantConst value:metadata["tags"].as<JsonArrayConst>()) {
  auto tag=libraryText(value,32);
  if(tags.size()>=12)break;
  if(!tag.empty())tags.add(tag);
 }
}
}
