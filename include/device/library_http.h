#pragma once
#include <FS.h>
#include <WebServer.h>
#include "device/transport.h"
namespace device {
void serveDeviceRecord(WebServer& server);
void serveStoredPackages(WebServer& server,fs::FS& filesystem,const char* storageLabel);
}
