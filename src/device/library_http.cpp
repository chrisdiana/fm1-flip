#include "device/library_http.h"
#include "device/library.hpp"
#include "device/gallery.h"
#include "device/catalogue_seed.h"
#include <ArduinoJson.h>

namespace device {
void serveDeviceRecord(WebServer& server) {
 const auto snapshot=transportStatus();
 JsonDocument document;
 auto current=document["current"].to<JsonObject>();
 current["firmwareProduct"]=snapshot.firmwareProduct;
 current["firmwareKnown"]=snapshot.firmwareKnown;
 current["firmwareLive"]=snapshot.firmwareLive;
 current["mode"]=transportModeName(snapshot.mode);
 current["flashId"]=snapshot.flashId;
 current["persistenceError"]=snapshot.persistenceError;
 current["romReady"]=snapshot.connected;
 String prefix;serializeJson(document,prefix);prefix.remove(prefix.length()-1);
 server.setContentLength(CONTENT_LENGTH_UNKNOWN);
 server.send(200,"application/json","");
 server.sendContent(prefix+",\"record\":");
 String record=transportDeviceRecordJson();
 server.sendContent(record.length()?record:String("{\"schemaVersion\":1,\"history\":[]}"));
 server.sendContent("}");server.sendContent("");
}
void serveStoredPackages(WebServer& server,fs::FS& filesystem,const char* storageLabel) {
 String query=server.arg("q");if(query.length()>96)query.remove(96);query.toLowerCase();
 uint32_t offset=server.arg("offset").toInt();if(offset>2048)offset=2048;
 const auto snapshot=transportStatus();
 File directory=filesystem.open("/fm1/packages");
 if(!directory || !directory.isDirectory()){server.send(200,"application/json","{\"entries\":[],\"total\":0,\"offset\":0,\"limit\":16}");return;}
 server.setContentLength(CONTENT_LENGTH_UNKNOWN);server.send(200,"application/json","");server.sendContent("{\"entries\":[");
 uint32_t total=0,sent=0,scanned=0;
 for(File file=directory.openNextFile();file&&scanned<2048;file=directory.openNextFile()) {
  if(file.isDirectory())continue;
  String filename=file.name();filename=filename.substring(filename.lastIndexOf('/')+1);
  if(!filename.endsWith(".fwsc") || filename.indexOf("..")>=0)continue;
  ++scanned;
  JsonDocument metadata,row;
  File sidecar=filesystem.open("/fm1/packages/"+filename+".json");
  if(sidecar&&sidecar.size()<=8192){if(deserializeJson(metadata,sidecar))metadata.clear();}
  sidecar.close();
  enrichBundledPresentation(metadata.as<JsonObject>());
  storedPackageEntry(row.to<JsonObject>(),metadata.as<JsonObjectConst>(),filename.c_str(),file.size(),snapshot.firmwareProduct.c_str(),snapshot.firmwareLive);
  String haystack=row["name"].as<String>()+" "+row["author"].as<String>()+" "+row["description"].as<String>()+" "+row["version"].as<String>();haystack.toLowerCase();
  if(haystack.indexOf(query)<0)continue;
  ++total;
  if(total>offset&&sent<16){
   if(sent++)server.sendContent(",");
   String response;serializeJson(row,response);server.sendContent(response);
  }
  vTaskDelay(1);
 }
 JsonDocument summary;summary["total"]=total;summary["offset"]=offset;summary["limit"]=16;summary["storageLabel"]=storageLabel;summary["scanLimitReached"]=scanned>=2048;
 String suffix;serializeJson(summary,suffix);suffix.remove(0,1);
 server.sendContent("],"+suffix);server.sendContent("");
}
}
