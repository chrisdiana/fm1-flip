#!/usr/bin/env python3
"""Run the actual catalogue/GitHub response downloader with decoded HTTP fixtures."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/device/catalogue.cpp').read_text()
fn=s[s.index('String get('):s.index('\n} // namespace',s.index('String get('))]
with tempfile.TemporaryDirectory(prefix='fm1-metadata-') as name:
 p=Path(name)
 (p/'Stream.h').write_text('''#pragma once
#include <cstdint>
#include <cstddef>
class Stream {public:virtual ~Stream()=default;virtual int available()=0;virtual int read()=0;virtual int peek()=0;virtual void flush()=0;virtual size_t write(uint8_t)=0;virtual size_t write(const uint8_t*,size_t)=0;};
''')
 (p/'main.cpp').write_text(r'''
#include <string>
#include <functional>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <cassert>
#include <iostream>
#include "device/download_stream.h"
using device::DownloadStream;
class String:public std::string {public:using std::string::string;String(std::string s):std::string(s){}String(int n):std::string(std::to_string(n)){}String(unsigned long n):std::string(std::to_string(n)){}bool isEmpty()const{return empty();}};
using ScratchSpace=std::function<uint64_t()>;using NetworkProgress=void(*)(const String&,const String&,uint32_t,uint32_t);
String payload="{\"ok\":true}";int declared=-1,replyCode=200;bool interrupted=false;
size_t offset=0;uint64_t freeBytes=1000;bool removed=false;String saved;
constexpr int FILE_WRITE=1;const char* AGENT="fixture";const char* FM1_TRUST_ROOTS="public roots";
struct File {explicit operator bool()const{return true;}size_t write(const uint8_t* p,size_t n){saved.append((const char*)p,n);freeBytes-=n;return n;}void close(){}};
namespace fs {struct FS {File open(const String&,int){saved.clear();removed=false;return {};}void remove(const String&){removed=true;saved.clear();}};}
struct Preferences {int writes=0;String getString(const char*){return "";}void putString(const char*,const String&){++writes;} };
struct WiFiClientSecure {void setCACert(const char*){}void setHandshakeTimeout(int){}};
struct HttpStream {int available(){return payload.size()-offset;}int readBytes(uint8_t* p,size_t n){n=std::min(n,payload.size()-offset);memcpy(p,payload.data()+offset,n);offset+=n;return n;}};
struct HTTPClient {
 void setConnectTimeout(int){}void setTimeout(int){}void setUserAgent(const char*){}void setFollowRedirects(int){}
 bool begin(WiFiClientSecure&,const String&){offset=0;return true;}void collectHeaders(const char**,int){}void addHeader(const char*,const String&){}
 int GET(){return replyCode;}int getSize(){return declared;}String header(const char*){return "fixture-tag";}void end(){}bool connected(){return offset<payload.size();}HttpStream* getStreamPtr(){static HttpStream s;return &s;}
 int writeToStream(Stream* stream){int total=0;while(offset<payload.size()){size_t n=std::min<size_t>(3,payload.size()-offset);size_t written=stream->write((const uint8_t*)payload.data()+offset,n);if(written!=n)return -1;offset+=n;total+=n;if(interrupted)return -1;}return total;}
};
constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS=0;
String networkPreflight(){return "";}String caBundle(fs::FS&){return "";}String clip(String s,size_t){return s;}
String httpsFailure(int,WiFiClientSecure&,const String&){return "HTTP failed";}
uint32_t millis(){static uint32_t n=0;return ++n;}void vTaskDelay(int){}
void progress(const String&,const String&,uint32_t,uint32_t){}
'''+fn+r'''
int main(){fs::FS fs;
 for(int scenario=0;scenario<8;++scenario){
  declared=scenario==1?int(payload.size()):-1;interrupted=scenario==2;
  freeBytes=scenario==4?4:1000;replyCode=scenario==6?304:200;removed=false;saved.clear();
  uint32_t limit=scenario==3?4:100;
  if(scenario==5)declared=101;
  if(scenario==7)declared=payload.size()+1;
  Preferences cache;
  String error=get(fs,"https://api.github.com/fixture","/fm1/release.part",limit,progress,&cache,true,[]{return freeBytes;});
  if(scenario==0||scenario==1){assert(error.empty()&&"Valid metadata without Content-Length must stream successfully");assert(saved==payload&&cache.writes==2);}
  else if(scenario==6)assert(error=="NOT_MODIFIED"&&cache.writes==0);
  else {assert(!error.empty()&&cache.writes==0);if(scenario==2||scenario==3||scenario==4||scenario==7)assert(removed&&saved.empty());}
 }
 std::cout<<"Production metadata downloader: unknown/known length, decoded chunks, interruption, size/storage caps, 304 and pending-file cleanup passed\n";
}
''')
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(p),'-I'+str(root/'include'),str(p/'main.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
