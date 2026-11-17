#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <time.h>
#if defined(FM1_CARDPUTER)
#include <sdkconfig.h>
static_assert(CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN == 16384, "Cardputer must accept full TLS records");
static_assert(CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN <= 4096, "Rebuild Cardputer SDK with asymmetric TLS buffers");
#ifndef CONFIG_MBEDTLS_DYNAMIC_BUFFER
#error "Rebuild Cardputer SDK with dynamic TLS buffers"
#endif
#endif

namespace device {
inline String wifiJoinFailure(int status, uint16_t reason) {
 if(status==WL_NO_SSID_AVAIL || reason==201) return "Network not found. Check range and use a 2.4 GHz network.";
 if(reason==202 || reason==15 || reason==204 || status==WL_CONNECT_FAILED) return "Authentication failed or timed out. Check the password and router security; use 2.4 GHz Wi-Fi.";
 return "Connection timed out or no IP address was assigned. Check the network, password and router DHCP, then retry. Wi-Fi reason: "+String(reason);
}
inline String networkPreflight() {
 if(WiFi.status()!=WL_CONNECTED) return "Wi-Fi is not connected. Open Home > Wi-Fi setup and wait for the connection result.";
 if(uint32_t(WiFi.localIP())==0) return "Wi-Fi joined, but no IP address yet. Wait for the router, then retry.";
 if(time(nullptr)<1704067200) return "Wi-Fi connected; clock not synchronized yet. Wait for Internet/NTP access, then retry HTTPS.";
 return String();
}
inline String httpsFailure(int code, WiFiClientSecure& client, const String& url) {
 String ready=networkPreflight();if(!ready.isEmpty())return ready;
 int start=url.indexOf("://");start=start<0?0:start+3;
 int end=url.indexOf('/',start);String host=end<0?url.substring(start):url.substring(start,end);
 if(code>0)return "HTTP "+String(code)+" from "+host;
 char detail[192]{};int tls=client.lastError(detail,sizeof detail);
 String result="HTTPS connection failed to "+host+" ("+String(code)+"). ";
 if(tls){result+="TLS "+String(tls)+": "+String(detail)+". ";}
 else result+="Check Internet/DNS or server reachability. ";
 result+="Free RAM "+String(ESP.getFreeHeap())+", largest block "+String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT))+" bytes.";
 return result;
}
}
