#pragma once
#include <Arduino.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <functional>
namespace device {
using NetworkProgress = void (*)(const String&,const String&,uint32_t,uint32_t);
using CatalogueVisitor = std::function<void(uint32_t,JsonObject)>;
String scanCatalogue(File&,const CatalogueVisitor& visitor = CatalogueVisitor());
void commitCatalogueCache();
using ScratchSpace = std::function<uint64_t()>;
String refreshHub(fs::FS&, const String& pending, NetworkProgress, ScratchSpace);
String resolveMakerAsset(fs::FS&,const String& id,String& url,String& hash,String& metadata,NetworkProgress);
}
