#pragma once
#include <FS.h>
#include <ArduinoJson.h>
namespace device {
// Installs the attributed offline snapshot only when no catalogue exists.
// Existing user/custom caches, firmware packages and settings are untouched.
bool ensureBundledCatalogue(fs::FS& filesystem);
// Fill absent presentation fields only for an exact bundled project/package hash.
// This upgrades older cached metadata in memory without replacing its files.
void enrichBundledPresentation(JsonObject entry);
}
