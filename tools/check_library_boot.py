#!/usr/bin/env python3
"""Count startup SD operations in the production library loader."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/device/app.cpp').read_text()
start=s.index('uint32_t packagePathKey(') if 'uint32_t packagePathKey(' in s else s.index('void loadCatalogue(){')
fn=s[start:s.index('void saveFavorites(){')]
fn=fn.replace('.isEmpty()', '.empty()').replace('name.endsWith(".fwsc")','endsWith(name,".fwsc")').replace('name.startsWith(".")', "(!name.empty()&&name.front()=='.')").replace("name.substring(name.lastIndexOf('/')+1)","basename(name)")
ref=s[s.index('struct EntryIndex {'):s.index('EntryIndex entryIndex[')]
headers=next((root/'.pio/libdeps').glob('*/ArduinoJson/src/ArduinoJson.h'))
with tempfile.TemporaryDirectory(prefix='fm1-boot-') as name:
 p=Path(name)
 (p/'main.cpp').write_text(r'''
#include <string>
#include <map>
#include <memory>
#include <vector>
#include <functional>
#include <cassert>
#include <iostream>
#include <cstdio>
#include <ArduinoJson.h>
#include "device/gallery.h"
using String=std::string;namespace gallery=device::gallery;
constexpr size_t MAX_ENTRIES=512;uint64_t validationBytes=0;size_t packageStats=0,catalogueReads=0;
struct File {
 String path,bytes;size_t pos=0,dirPos=0;bool valid=false,directory=false;std::vector<String> names;
 explicit operator bool()const{return valid;}bool isDirectory(){return directory;}String name(){return path;}
 size_t size(){return path.find(".fwsc")!=String::npos?1048576:bytes.size();}
 int read(){if(pos==bytes.size())return -1;++catalogueReads;return (uint8_t)bytes[pos++];}
 size_t readBytes(char* p,size_t n){size_t count=0;for(;count<n;++count){int c=read();if(c<0)break;p[count]=c;}return count;}
 bool seek(uint32_t n){pos=n;return n<=bytes.size();}void close(){}File openNextFile();
};
enum{FILE_READ=0};
struct Disk {std::map<String,String> files;std::function<void()> directoryHook;
 bool exists(const String& p){if(p.rfind("/fm1/packages/",0)==0)++packageStats;return files.count(p);}
 File open(const String& p,int=0){File f;f.path=p;if(p=="/fm1/packages"){f.valid=f.directory=true;if(directoryHook)directoryHook();for(const auto& kv:files)if(kv.first.rfind(p+"/",0)==0)f.names.push_back(kv.first);return f;}auto at=files.find(p);if(at!=files.end()){f.bytes=at->second;f.valid=true;}return f;}
 bool rename(const String&,const String&){return false;}
}storage,*disk=&storage;
File File::openNextFile(){return dirPos<names.size()?storage.open(names[dirPos++]):File{};}
bool endsWith(const String& s,const String& suffix){return s.size()>=suffix.size()&&s.compare(s.size()-suffix.size(),suffix.size(),suffix)==0;}
String basename(const String& s){return s.substr(s.rfind('/')+1);}
String fileId(const String& text){uint32_t h=2166136261;for(unsigned char c:text){h^=c;h*=16777619;}char hex[9];snprintf(hex,sizeof hex,"%08x",h);String result;for(int i=0;i<8;++i)result+=hex;return result;}
String validate(const String&){validationBytes+=1048576;return "";}
void enrichBundledPresentation(JsonObject){}void ensureBundledCatalogue(Disk&){}
String notice,retrievedAt,catalogueSource,catalogueAttribution,catalogueLicense;bool dirty=false;
size_t entryCount=0,categoryCount=0;uint32_t favoriteGeneration=0;JsonDocument favorites;
void addCategory(String){}
String clipped(String s,size_t n){if(s.size()>n)s.resize(n);return s;}
struct{String getString(const char*,const char*){return "{}";}}preferences;
struct CatalogueMetadata {String retrievedAt,sourceUrl,attribution,license;};
using CatalogueVisitor=std::function<void(uint32_t,JsonObject)>;
String scanCatalogue(File& file,const CatalogueVisitor& visit,CatalogueMetadata* meta=nullptr){
 JsonDocument doc;if(deserializeJson(doc,file))return "Invalid fixture";
 if(meta){meta->retrievedAt=doc["retrievedAt"]|"";meta->sourceUrl=doc["sourceUrl"]|"";meta->attribution=doc["attribution"]|"";meta->license=doc["license"]|"";}
 for(JsonObject entry:doc["entries"].as<JsonArray>())visit(uint32_t(file.bytes.find("{\"id\":\""+entry["id"].as<String>()+"\"")),entry);
 return "";
}
'''+ref+r'''
EntryIndex entryIndex[MAX_ENTRIES];
'''+fn+r'''
int main(){
 JsonDocument doc;doc["retrievedAt"]="fixture-date";doc["sourceUrl"]="https://thefirmwarehub.com/catalog.json";doc["attribution"]="Fixture";doc["license"]="CC-BY-4.0";
 auto entries=doc["entries"].to<JsonArray>();
 for(int i=0;i<217;++i){auto e=entries.add<JsonObject>();e["id"]="fixture-"+std::to_string(i);e["sha256"]=std::string(64,'a');e["name"]="Firmware "+std::to_string(i);e["fixtureOffset"]=i+1;}
 serializeJson(doc,storage.files["/fm1/catalogue.json"]);
 for(int i=0;i<3;++i)storage.files["/fm1/packages/manual-"+std::to_string(i)+".fwsc"]="fixture";
 for(int i=1;i<=2;++i)storage.files["/fm1/packages/"+fileId("fixture-"+std::to_string(i)+"|"+String(64,'a'))+".fwsc"]="fixture";
 storage.files["/fm1/packages/._manual.fwsc"]="ignored AppleDouble metadata";
 storage.directoryHook=[]{entryIndex[0].packageKey=entryIndex[1].packageKey;};
 loadCatalogue();size_t saved=0;for(size_t i=0;i<entryCount;++i)saved+=!entryIndex[i].localPath.empty();
 std::cout<<"Library boot: "<<packageStats<<" package stats, "<<validationBytes<<" package bytes validated, "<<catalogueReads<<" catalogue bytes read, "<<saved<<" SD packages\n"<<std::flush;
 assert(saved==5&&entryCount==220&&entryIndex[0].localPath.empty());
 assert(catalogueReads<storage.files["/fm1/catalogue.json"].size()+2048);
 assert(validationBytes==0&&"Do not read/validate entire SD firmware images during startup");
 assert(packageStats==0&&"Use one directory scan instead of stat for every catalogue entry");
 assert(retrievedAt=="fixture-date");
}
''')
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(root/'include'),'-I'+str(headers.parent),str(p/'main.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
