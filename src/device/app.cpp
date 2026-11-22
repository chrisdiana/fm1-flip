#include "device/app.h"
#include "device/gallery.h"
#include "device/presentation.h"
#include "device/display_canvas.h"
#include "device/local_controller.h"
#include <algorithm>
#include "device/transport.h"
#include <vector>
#include "device/web_asset.h"
#include "device/trust.h"
#include "device/catalogue.h"
#include "device/catalogue_seed.h"
#include "device/library_http.h"
#include "fm1/package.hpp"
#include "fm1/confirmation.hpp"
#include "device/download_stream.h"
#include "device/network_diagnostics.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <mbedtls/sha256.h>
#include <atomic>
#include <cerrno>
#ifdef FM1_CARDPUTER
#include <M5Cardputer.h>
#undef DISPLAY
#define DISPLAY M5Cardputer.Display
#else
#include <M5Unified.h>
#undef DISPLAY
#define DISPLAY M5.Display
#endif
namespace device { namespace {
constexpr size_t MAX_ENTRIES=512, CATALOGUE_LIMIT=1024*1024, PACKAGE_LIMIT=4*1024*1024;
uint64_t reserveBytes(){return transportBackupReserve();}
struct Entry { String id,name,author,description,tags,version,hubUrl,sourceUrl,projectUrl,releaseUrl,license,packageUrl,sha256,path,chip,date,artworkUrl; uint32_t size=0,popularity=0; bool favorite=false,downloadable=false,beta=false,popularityKnown=false; };
struct EntryIndex { uint32_t offset=0,packageKey=0; String localPath;gallery::SortKey sort; };
EntryIndex entryIndex[MAX_ENTRIES]; size_t entryCount=0;JsonDocument favorites;uint32_t favoriteGeneration=0;
fs::FS* disk=nullptr; bool storageReady=false; WebServer server(80); Preferences preferences;
String catalogueSource="https://thefirmwarehub.com/catalog.json",catalogueAttribution="The Firmware Hub",catalogueLicense="CC-BY-4.0";
bool wifiEnabled=false, webConfigured=false;int pendingWifiPower=-1;
bool apEnabled=false;int pendingHotspot=-1;uint32_t hotspotChangeAt=0;
std::atomic<uint16_t> wifiDisconnectReason{0};
String wifiResult="Not connected";
String retrievedAt,query,notice="Hardware validation pending",apPassword;
fm1::PendingAction pendingAction;
String categoryFilter="all",releaseFilter="all",compatibilityFilter="all",sortOrder="popular";
String categories[24];size_t categoryCount=0;
bool savedOnly=false,favoritesOnly=false,detail=false,confirmInstall=false,dirty=true;
bool actionMenu=true,searchPicker=false,exportActive=false;size_t actionIndex=0,characterIndex=0,detailOffset=0;
File exportFile;WiFiClient exportClient;uint32_t exportLast=0;
size_t selected=0; uint32_t lastPaint=0; std::atomic<bool> jobBusy(false),reload(false),jobCancel(false);
SemaphoreHandle_t jobMutex; String jobStage="Idle",jobDetail; uint32_t jobCompleted=0,jobTotal=0;std::atomic<uint32_t> workerStack(0);
struct Job { String url,hash,path; bool download; bool catalogue=false,refresh=false; String id,metadata; }; Job job;
Job pendingNetworkJob;bool networkPrompt=false,networkWaiting=false;size_t networkPromptIndex=0;uint32_t networkReadyStarted=0;
File upload; String uploadError; bool uploadOwned=false,uploadTrust=false; uint32_t uploadBytes=0;
uint64_t freeSpace(){
 if(!storageReady)return 0;
#ifdef FM1_CARDPUTER
 return SD.totalBytes()-SD.usedBytes();
#else
 return LittleFS.totalBytes()-LittleFS.usedBytes();
#endif
}
uint64_t totalSpace(){
 if(!storageReady)return 0;
#ifdef FM1_CARDPUTER
 return SD.totalBytes();
#else
 return LittleFS.totalBytes();
#endif
}
extern bool localReview;
extern bool wifiConnecting;
bool locked(){return networkPrompt||networkWaiting||localReview||pendingAction.active()||jobBusy.load()||exportActive||transportStatus().busy;}
bool cancelOperation(){if(jobBusy.load()&&!uploadOwned){jobCancel.store(true);return true;}return transportCancel();}
String clipped(String v,size_t max){if(v.length()>max)v.remove(max);return v;}
String fileId(const String& id){uint8_t hash[32];mbedtls_sha256((const uint8_t*)id.c_str(),id.length(),hash,0);String out;char hex[3];for(auto b:hash){snprintf(hex,sizeof(hex),"%02x",b);out+=hex;}return out;}
String safeId(String v){String o;for(size_t i=0;i<v.length()&&o.length()<96;i++){char c=v[i];if(isalnum((unsigned char)c)||c=='-'||c=='_'||c=='.')o+=c;}return o;}
bool safePath(const String& p){return p.startsWith("/fm1/")&&p.indexOf("..")<0&&p.indexOf('\\')<0;}
// Interrupted downloads, uploads and backups leave *.part files that nothing
// lists or reuses. Nothing is in progress at boot, so they are safe to drop.
void removeStaleParts(){
 for(const char* dirName:{"/fm1","/fm1/packages","/fm1/backups"}){
  std::vector<String> stale;File dir=disk->open(dirName);
  for(File f=dir.openNextFile();f;f=dir.openNextFile()){String name=f.name();if(!f.isDirectory()&&name.endsWith(".part"))stale.push_back(String(dirName)+"/"+name.substring(name.lastIndexOf('/')+1));}
  dir.close();
  for(auto& path:stale)disk->remove(path);
 }
}
Entry readEntry(size_t index);
// Download name for an exported file: "fm1-" + its file name; saved packages
// (stored under a hash) use their catalogue name. Only [A-Za-z0-9._-] survive,
// so the Content-Disposition value needs no escaping.
String exportName(const String& path){
 String base=path.substring(path.lastIndexOf('/')+1),ext=base.substring(base.lastIndexOf('.'));
 if(path.startsWith("/fm1/packages/"))for(size_t i=0;i<entryCount;i++)if(entryIndex[i].localPath==path){String n=readEntry(i).name;n.replace(' ','-');n=safeId(n);if(n.length())base=n+"-"+readEntry(i).version+ext;break;}
 base.replace(' ','-');base=safeId(base);
 if(base.isEmpty()||base[0]=='.')base="export"+ext;
 return base.startsWith("fm1")?base:"fm1-"+base;
}
const char* exportContentType(const String& name){
 if(name.endsWith(".txt"))return "text/plain; charset=utf-8";
 if(name.endsWith(".json"))return "application/json";
 return "application/octet-stream";
}
class FileReader:public fm1::Reader { File& f; public: explicit FileReader(File& file):f(file){} uint64_t size()const override{return f.size();} bool read(uint64_t offset,uint8_t* out,size_t n)override{return offset<=UINT32_MAX&&f.seek((uint32_t)offset)&&f.read(out,n)==n;} };
String validate(const String& path){File f=disk->open(path,FILE_READ);if(!f)return "Cannot open package";FileReader reader(f);fm1::PackageInfo info;auto e=fm1::validatePackage(reader,fm1::DeviceModel::Fm1,info);return e==fm1::PackageError::None?String():String(fm1::errorMessage(e));}
void jobStatus(const String& stage,const String& message,uint32_t done=0,uint32_t total=0){xSemaphoreTake(jobMutex,portMAX_DELAY);jobStage=stage;jobDetail=message;jobCompleted=done;jobTotal=total;xSemaphoreGive(jobMutex);}
void finishWorker(){workerStack.store(uxTaskGetStackHighWaterMark(nullptr));jobBusy.store(false);vTaskDelete(nullptr);}
void networkJob(void*){
 String error; bool ok=true; const String pending=job.path+".part";
 if(job.refresh){error=refreshHub(*disk,pending,jobStatus,[]{return freeSpace()>reserveBytes()?freeSpace()-reserveBytes():0;});if(error=="NOT_MODIFIED"){jobStatus("Catalogue unchanged","Last valid cache retained");finishWorker();return;}ok=error.isEmpty();}
 if(job.download){error=networkPreflight();ok=error.isEmpty();}
 if(job.download&&ok&&!job.id.isEmpty()){error=resolveMakerAsset(*disk,job.id,job.url,job.hash,job.metadata,jobStatus);ok=error.isEmpty();}
 if(jobCancel.load()){ok=false;error="Import cancelled";}
 if(job.download&&ok){
  File cert=disk->open("/fm1/ca.pem",FILE_READ);String ca;
  if(cert&&cert.size()<=32768)ca=cert.readString();
  WiFiClientSecure client;HTTPClient http;
  if(ok){error=networkPreflight();ok=error.isEmpty();}
  if(ok){client.setCACert(ca.isEmpty()?FM1_TRUST_ROOTS:ca.c_str());http.setConnectTimeout(15000);http.setTimeout(15000);client.setHandshakeTimeout(15);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);if(!http.begin(client,job.url)){ok=false;error="Invalid HTTPS URL";}}
  const char* redirectHeaders[]={"Location"};http.collectHeaders(redirectHeaders,1);http.setUserAgent("FM1-Flip/0.1");
  int code=ok?http.GET():0;String currentUrl=job.url;
  for(unsigned redirect=0;ok&&(code==301||code==302||code==303||code==307||code==308);redirect++){
   String next=http.header("Location");int slash=next.indexOf('/',8);String host=slash<0?next.substring(8):next.substring(8,slash);host.toLowerCase();
   bool allowed=next.startsWith("https://")&&(host=="github.com"||host=="release-assets.githubusercontent.com"||host=="objects.githubusercontent.com"||host=="github-releases.githubusercontent.com");
   if(redirect>=5||!allowed){ok=false;error="Rejected non-HTTPS or non-maker/CDN redirect";break;}http.end();if(!http.begin(client,next)){ok=false;error="Invalid redirect";break;}http.collectHeaders(redirectHeaders,1);code=http.GET();currentUrl=next;
  }
  if(ok&&code!=HTTP_CODE_OK){ok=false;error=httpsFailure(code,client,currentUrl);}
  int expected=ok?http.getSize():0;
  if(ok&&expected>int(PACKAGE_LIMIT)){ok=false;error="Package exceeds 4 MB download limit";}
  if(ok&&expected>=0&&uint64_t(expected)+reserveBytes()>freeSpace()){ok=false;error="Not enough storage: need "+String((unsigned long)expected)+" bytes plus 64 KB working space; free "+String((unsigned long)freeSpace())+" bytes";}
  File out;if(ok){out=disk->open(pending,FILE_WRITE);if(!out){ok=false;error="Cannot create pending package";}}
  mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);mbedtls_sha256_starts(&sha,0);
  if(ok){
   int writeErrno=0;
   DownloadStream sink(PACKAGE_LIMIT,reserveBytes(),freeSpace,
    [&](const uint8_t* data,size_t size){errno=0;auto wrote=out.write(data,size);if(wrote<size)writeErrno=errno;return wrote;},
    [&](const uint8_t* data,size_t size){mbedtls_sha256_update(&sha,data,size);},
    [&](size_t count){jobStatus("Downloading",job.path,count,expected>0?expected:0);vTaskDelay(1);},[]{return jobCancel.load();});
   // HTTPClient decodes chunk framing; raw getStreamPtr() does not.
   int received=http.writeToStream(&sink);
   if(!sink.error.empty()){ok=false;error=sink.error.c_str();if(sink.error=="Download storage write failed")error+=" at byte "+String((unsigned long)sink.bytesWritten())+", errno "+String(writeErrno)+" ("+(writeErrno?String(strerror(writeErrno)):String("driver supplied no error code"))+"), free storage "+String((unsigned long)freeSpace())+" bytes, heap "+String(ESP.getFreeHeap());}
   else if(received<0||(expected>=0&&received!=expected)){ok=false;error=downloadFailure(received,sink.bytesWritten(),expected,client,currentUrl);}
  }
  uint8_t digest[32];mbedtls_sha256_finish(&sha,digest);mbedtls_sha256_free(&sha);if(out)out.close();http.end();
  String hash;char hex[3];for(auto b:digest){snprintf(hex,sizeof(hex),"%02x",b);hash+=hex;}
  if(ok&&!job.hash.isEmpty()&&!hash.equalsIgnoreCase(job.hash)){ok=false;error="Source SHA-256 mismatch";}
 }
 if(jobCancel.load()){ok=false;error="Import cancelled";}
 if(ok){jobStatus("Validating","Checking import integrity");if(job.catalogue){File f=disk->open(pending);error=scanCatalogue(f);}else error=validate(pending);ok=error.isEmpty();}
 if(jobCancel.load()){ok=false;error="Import cancelled";}
 if(ok){if(job.catalogue){String old=job.path+".old";disk->remove(old);bool hadOld=disk->exists(job.path);if(hadOld&&!disk->rename(job.path,old)){ok=false;error="Cannot preserve catalogue";}if(ok&&!disk->rename(pending,job.path)){if(hadOld)disk->rename(old,job.path);ok=false;error="Cannot commit catalogue";}if(ok){disk->remove(old);if(job.refresh)commitCatalogueCache();}}else if(disk->exists(job.path)||!disk->rename(pending,job.path)){ok=false;error="Cannot commit package (already saved?)";}}
 if(ok&&job.download&&!job.metadata.isEmpty()){File metadata=disk->open(job.path+".json.part",FILE_WRITE);if(metadata){metadata.print(job.metadata);metadata.close();disk->rename(job.path+".json.part",job.path+".json");}}
 if(!ok)disk->remove(pending);
 jobStatus(ok?"Imported":jobCancel.load()?"Import cancelled":"Import failed",ok?"Package integrity checked; hardware compatibility remains unknown":error);
 reload.store(true);finishWorker();
}
bool startJob(const Job& next){if(locked())return false;job=next;jobCancel.store(false);jobBusy.store(true);jobStatus("Queued",next.path);if(xTaskCreate(networkJob,"fm1-import",12288,nullptr,1,nullptr)!=pdPASS){jobBusy.store(false);jobStatus("Error","Cannot allocate import worker");return false;}return true;}
size_t matchCount();
String downloadReason(const Entry& e);
Entry readEntry(size_t index){
 Entry out;if(index>=entryCount)return out;auto& ref=entryIndex[index];
 if(ref.offset){File file=disk->open("/fm1/catalogue.json");if(!file||!file.seek(ref.offset))return out;JsonDocument doc;if(deserializeJson(doc,file))return out;JsonObject e=doc.as<JsonObject>();enrichBundledPresentation(e);out.artworkUrl=clipped(e["artworkUrl"]|"",768);out.popularityKnown=e["hubInstalls"].is<uint32_t>()&&e["hubInstalls"].as<uint32_t>()<=2147483647u;out.popularity=e["hubInstalls"]|0;out.id=e["id"]|"";out.name=clipped(e["name"]|"Unnamed",80);out.author=clipped(e["author"]|"",80);out.description=clipped(e["description"]|"",640);out.version=clipped(e["version"]|"",40);out.hubUrl=clipped(e["hubUrl"]|"",256);out.sourceUrl=clipped(e["sourceUrl"]|"",256);out.projectUrl=clipped(e["projectUrl"].is<const char*>()?e["projectUrl"].as<String>():e["sourceUrl"].as<String>(),256);out.releaseUrl=clipped(e["releaseUrl"]|"",256);out.license=clipped(e["license"]|"",96);out.sha256=clipped(e["sha256"]|"",64);out.chip=e["chip"]|"";out.date=e["date"]|"";out.beta=e["beta"]|false;out.size=e["size"]|0;out.downloadable=!e["external_only"].as<bool>()&&out.sha256.length()==64&&!out.releaseUrl.isEmpty();for(JsonVariant tag:e["tags"].as<JsonArray>()){if(out.tags.length()<160){if(out.tags.length())out.tags+=' ';out.tags+=clipped(tag.as<String>(),32);}}out.path=ref.localPath;
 }else{out.path=ref.localPath;String name=out.path.substring(out.path.lastIndexOf('/')+1);out.id="local-"+name;out.name=name;out.description="SD package; integrity and compatibility are checked before installation.";File f=disk->open(out.path);out.size=f?f.size():0;File side=disk->open(out.path+".json");JsonDocument metadata;if(side&&side.size()<8192&&!deserializeJson(metadata,side)){enrichBundledPresentation(metadata.as<JsonObject>());out.sha256=metadata["sha256"]|"";out.artworkUrl=clipped(metadata["artworkUrl"]|"",768);out.popularityKnown=metadata["hubInstalls"].is<uint32_t>()&&metadata["hubInstalls"].as<uint32_t>()<=2147483647u;out.popularity=metadata["hubInstalls"]|0;out.name=metadata["name"]|out.name;out.author=metadata["author"]|"";out.version=metadata["version"]|"";out.description=clipped(metadata["description"]|out.description,640);out.sourceUrl=metadata["sourceUrl"]|"";out.hubUrl=metadata["hubUrl"]|"";out.projectUrl=metadata["projectUrl"]|"";out.releaseUrl=metadata["releaseUrl"]|"";out.license=metadata["license"]|"";out.chip=metadata["chip"]|"";out.date=metadata["date"]|"";out.beta=metadata["beta"]|false;for(JsonVariant tag:metadata["tags"].as<JsonArray>()){if(out.tags.length()<160){if(out.tags.length())out.tags+=' ';out.tags+=clipped(tag.as<String>(),32);}}}}
 out.favorite=favorites[out.id].as<bool>();return out;
}
void addCategory(String tag){tag=clipped(tag,32);if(tag.isEmpty()||tag=="all")return;for(size_t i=0;i<categoryCount;i++)if(categories[i]==tag)return;if(categoryCount<24)categories[categoryCount++]=tag;}
uint32_t packagePathKey(const String& path){uint32_t key=2166136261;for(unsigned char c:path){key^=c;key*=16777619;}return key;}
String expectedPackagePath(uint32_t offset){
 File file=disk->open("/fm1/catalogue.json",FILE_READ);if(!file||!file.seek(offset))return String();
 JsonDocument doc;if(deserializeJson(doc,file))return String();
 return "/fm1/packages/"+fileId(doc["id"].as<String>()+"|"+doc["sha256"].as<String>())+".fwsc";
}
void loadCatalogue(){
 if(!disk->exists("/fm1/catalogue.json")&&disk->exists("/fm1/catalogue.json.old"))disk->rename("/fm1/catalogue.json.old","/fm1/catalogue.json");
 if(!disk->exists("/fm1/catalogue.json"))ensureBundledCatalogue(*disk);
 for(size_t i=0;i<entryCount;i++){entryIndex[i].localPath="";entryIndex[i].packageKey=0;}
 entryCount=0;categoryCount=0;retrievedAt="unknown";File file=disk->open("/fm1/catalogue.json");CatalogueMetadata metadata;
 String error=scanCatalogue(file,[&](uint32_t offset,JsonObject e){
  if(entryCount>=MAX_ENTRIES)return;
  enrichBundledPresentation(e);auto& ref=entryIndex[entryCount++];
  ref.sort=gallery::sortKey(e["name"]|"",e["date"]|"",e["hubInstalls"]|0,e["hubInstalls"].is<uint32_t>());
  for(JsonVariant tag:e["tags"].as<JsonArray>())addCategory(tag.as<String>());
  ref.offset=offset;ref.localPath="";
  ref.packageKey=packagePathKey("/fm1/packages/"+fileId(e["id"].as<String>()+"|"+e["sha256"].as<String>())+".fwsc");
 },&metadata);file.close();
 if(!error.isEmpty()){entryCount=0;categoryCount=0;notice=error;}
 else {retrievedAt=metadata.retrievedAt.isEmpty()?String("unknown"):metadata.retrievedAt;catalogueSource=metadata.sourceUrl;catalogueAttribution=metadata.attribution;catalogueLicense=metadata.license;}
 // One directory scan replaces a stat per catalogue entry. The compact key is
 // only a lookup accelerator: confirm the complete path before linking a file.
 File dir=disk->open("/fm1/packages");size_t scanned=0;
 for(File f=dir.openNextFile();f&&scanned<2048;f=dir.openNextFile()){
  ++scanned;String name=f.name();name=name.substring(name.lastIndexOf('/')+1);if(f.isDirectory()||name.startsWith(".")||!name.endsWith(".fwsc"))continue;
  String path="/fm1/packages/"+name;uint32_t key=packagePathKey(path);bool found=false;
  for(size_t i=0;i<entryCount;i++)if(entryIndex[i].offset&&entryIndex[i].packageKey==key&&expectedPackagePath(entryIndex[i].offset)==path){entryIndex[i].localPath=path;found=true;break;}
  if(found||entryCount>=MAX_ENTRIES)continue;
  File side=disk->open(path+".json");JsonDocument info;
  if(side&&side.size()<8192&&!deserializeJson(info,side))enrichBundledPresentation(info.as<JsonObject>());
  auto& ref=entryIndex[entryCount++];ref.sort=gallery::sortKey(info["name"]|name.c_str(),info["date"]|"",info["hubInstalls"]|0,info["hubInstalls"].is<uint32_t>());
  for(JsonVariant tag:info["tags"].as<JsonArray>())addCategory(tag.as<String>());
  // Packages are fully validated and hashed by installDevice before any write,
  // rather than rereading every firmware image on each boot.
  ref.offset=0;ref.packageKey=0;ref.localPath=path;
 }
 favoriteGeneration++;favorites.clear();deserializeJson(favorites,preferences.getString("favorites","{}"));dirty=true;
}
void saveFavorites(){String value;serializeJson(favorites,value);if(value.length()>12288||!preferences.putString("favorites",value))notice="Cannot save favorites";}
void toggleFavorite(size_t index){favoriteGeneration++;Entry e=readEntry(index);if(e.favorite)favorites.remove(e.id);else favorites[e.id]=true;saveFavorites();}
bool filterMatches(const Entry& e,const String& category,const String& release,const String& compatibility,uint32_t flash){return gallery::categoryMatches(e.tags.c_str(),category.c_str())&&gallery::releaseMatches(e.beta,release.c_str())&&(compatibility=="all"||compatibility==gallery::status(gallery::compatibility(e.chip.c_str(),flash)));}
bool matches(const Entry& e){if(savedOnly&&e.path.isEmpty())return false;if(favoritesOnly&&!e.favorite)return false;if(!filterMatches(e,categoryFilter,releaseFilter,compatibilityFilter,transportFlashId()))return false;String text=e.name+" "+e.author+" "+e.description+" "+e.tags;text.toLowerCase();String q=query;q.toLowerCase();return text.indexOf(q)>=0;}
void sortIndices(size_t* indices,size_t count,const String& order){std::sort(indices,indices+count,[&](size_t a,size_t b){return gallery::less(entryIndex[a].sort,entryIndex[b].sort,gallery::sortMode(order.c_str()),a,b);});}
size_t matched[MAX_ENTRIES],matchedCount=0,matchScan=0;bool searching=false;String matchedQuery,matchedControls;bool matchedSaved=false,matchedFavorites=false;uint32_t matchedGeneration=UINT32_MAX;size_t matchedEntries=SIZE_MAX;
void ensureMatches(){String controls=categoryFilter+"|"+releaseFilter+"|"+compatibilityFilter+"|"+sortOrder+"|"+String(transportFlashId());if(matchedControls==controls&&matchedQuery==query&&matchedSaved==savedOnly&&matchedFavorites==favoritesOnly&&matchedGeneration==favoriteGeneration&&matchedEntries==entryCount)return;matchedControls=controls;matchedCount=0;matchScan=0;searching=true;matchedQuery=query;matchedSaved=savedOnly;matchedFavorites=favoritesOnly;matchedGeneration=favoriteGeneration;matchedEntries=entryCount;}
void searchTick(){
 ensureMatches();if(!searching)return;
 // The default lists need only cached membership and precomputed sort keys.
 const bool metadataNeeded=!query.isEmpty()||favoritesOnly||categoryFilter!="all"||releaseFilter!="all"||compatibilityFilter!="all";
 if(!metadataNeeded){
  while(matchScan<entryCount){size_t index=matchScan++;if(!savedOnly||!entryIndex[index].localPath.isEmpty())matched[matchedCount++]=index;}
 }else{
  for(unsigned batch=0;batch<8&&matchScan<entryCount;batch++,matchScan++){
   if(savedOnly&&entryIndex[matchScan].localPath.isEmpty())continue;
   if(matches(readEntry(matchScan)))matched[matchedCount++]=matchScan;
  }
 }
 if(matchScan>=entryCount){searching=false;sortIndices(matched,matchedCount,sortOrder);selected=std::min<size_t>(selected,matchedCount?matchedCount-1:0);}
 dirty=true;
}
int selectedIndex(){ensureMatches();return !searching&&selected<matchedCount?(int)matched[selected]:-1;}
size_t matchCount(){ensureMatches();return matchedCount;}
void sendJson(JsonDocument& doc){String s;serializeJson(doc,s);server.send(200,"application/json",s);}
void reject(int code,const String& message){server.send(code,"text/plain",message);}
void onUpload(bool trust,bool catalogue=false){auto& u=server.upload();if(u.status==UPLOAD_FILE_START){uploadError="";uploadBytes=0;uploadOwned=false;uploadTrust=trust;if(!storageReady||locked()){uploadError="Storage unavailable or busy";return;}String name=u.filename;name=name.substring(name.lastIndexOf('/')+1);if(!trust&&!catalogue&&(!name.endsWith(".fwsc")||safeId(name)!=name||name.indexOf("..")>=0)){uploadError="Use a simple .fwsc filename";return;}String target=catalogue?"/fm1/catalogue.json":trust?"/fm1/ca.pem":"/fm1/packages/"+name;if(disk->exists(target)&&!trust&&!catalogue){uploadError="Package already exists";return;}job.path=target;job.download=false;job.catalogue=catalogue;job.refresh=false;job.id="";job.metadata="";job.url="";job.hash="";jobBusy.store(true);uploadOwned=true;upload=disk->open(target+".part",FILE_WRITE);if(!upload)uploadError="Cannot create upload";}
 else if(u.status==UPLOAD_FILE_WRITE&&uploadOwned&&uploadError.isEmpty()){
  uploadBytes+=u.currentSize;uint32_t max=catalogue?CATALOGUE_LIMIT:trust?32768:PACKAGE_LIMIT;if(uploadBytes>max||freeSpace()<reserveBytes()+u.currentSize){uploadError="File too large or insufficient working space";return;}if(upload.write(u.buf,u.currentSize)!=u.currentSize)uploadError="Storage write failure";
 }else if((u.status==UPLOAD_FILE_END||u.status==UPLOAD_FILE_ABORTED)&&uploadOwned){upload.close();if(u.status==UPLOAD_FILE_ABORTED)uploadError="Upload interrupted";if(!uploadError.isEmpty()){disk->remove(job.path+".part");jobBusy.store(false);uploadOwned=false;return;}if(trust){File pem=disk->open(job.path+".part");String data=pem.readString();pem.close();if(data.indexOf("-----BEGIN CERTIFICATE-----")<0||!disk->rename(job.path+".part",job.path)){uploadError="Invalid certificate bundle or commit failed";disk->remove(job.path+".part");}jobBusy.store(false);}else{jobBusy.store(false);if(!startJob(job))uploadError="Cannot start validation";}uploadOwned=false;}
}
void startWifiJoin(const String& ssid,const String& password);
bool setWifiPower(bool enabled,bool reconnect=true);
bool requestNetworkJob(const Job& next);
bool setHotspot(bool enabled) {
 if(enabled&&!wifiEnabled&&!setWifiPower(true,false))return false;
 bool ok=enabled?WiFi.softAP("FM1-Flip",apPassword.c_str()):WiFi.softAPdisconnect(true);
 if(ok){apEnabled=enabled;preferences.putBool("apEnabled",enabled);}
 return ok;
}
bool setWifiPower(bool enabled,bool reconnect) {
 if(jobBusy.load()||transportStatus().busy)return false;
 if(!enabled){
  if(webConfigured)server.stop();
  WiFi.disconnect(false);
  if(!WiFi.mode(WIFI_OFF))return false;
  wifiEnabled=false;apEnabled=false;wifiConnecting=false;wifiResult="Wi-Fi radio off";
 }else{
  if(!WiFi.mode(WIFI_STA))return false;
  wifiEnabled=true;wifiResult="Wi-Fi on; not connected";
  configTime(0,0,"pool.ntp.org","time.cloudflare.com");
  if(preferences.getBool("apEnabled",false))setHotspot(true);
  if(webConfigured)server.begin();
  String ssid=preferences.getString("ssid","");
  if(reconnect&&!ssid.isEmpty())startWifiJoin(ssid,preferences.getString("password",""));
 }
 preferences.putBool("wifiEnabled",enabled);dirty=true;return true;
}
void configureWeb(){
 server.on("/",HTTP_GET,[]{server.send_P(200,"text/html",WEB_INDEX);});
 server.on("/api/device",HTTP_GET,[]{serveDeviceRecord(server);});
 server.on("/api/library",HTTP_GET,[]{if(!storageReady||locked())return reject(409,"Storage unavailable or busy");
#ifdef FM1_CARDPUTER
  serveStoredPackages(server,*disk,"Cardputer SD card");
#else
  serveStoredPackages(server,*disk,"StickS3 internal storage");
#endif
 });
 server.on("/api/status",HTTP_GET,[]{JsonDocument d;auto s=transportStatus();d["firmwareProduct"]=s.firmwareProduct;d["firmwareLive"]=s.firmwareLive;d["firmwareKnown"]=s.firmwareKnown;d["mode"]=transportModeName(s.mode);d["stage"]=s.stage;d["detail"]=s.detail;d["completed"]=s.completed;d["total"]=s.total;{xSemaphoreTake(jobMutex,portMAX_DELAY);d["importStage"]=jobStage;d["importDetail"]=jobDetail;if(jobBusy.load()){d["stage"]=jobStage;d["detail"]=jobDetail;d["completed"]=jobCompleted;d["total"]=jobTotal;}xSemaphoreGive(jobMutex);}if(pendingAction.active()){d["stage"]="Confirm on device";d["detail"]=pendingAction.snapshot().label;}d["confirmationPending"]=pendingAction.active();d["heapFree"]=ESP.getFreeHeap();d["heapMinimum"]=ESP.getMinFreeHeap();d["workerStackWatermark"]=workerStack.load();d["transportStackWatermark"]=transportStackWatermark();d["targetFlashId"]=transportFlashId();d["wifiEnabled"]=wifiEnabled;d["networkConfirmationPending"]=networkPrompt;d["apEnabled"]=apEnabled;d["wifiResult"]=wifiResult;d["wifiJoining"]=wifiConnecting;d["wifiDisconnectReason"]=wifiDisconnectReason.load();d["canCancel"]=transportCanCancel()||(jobBusy.load()&&!uploadOwned);d["busy"]=locked();d["storageReady"]=storageReady;d["free"]=freeSpace();d["backupReserve"]=reserveBytes();d["ap"]=WiFi.softAPIP().toString();d["station"]=WiFi.localIP().toString();sendJson(d);});
 server.on("/api/catalogue",HTTP_GET,[]{
 if(!storageReady||jobBusy.load()||transportStatus().busy)return reject(409,"Storage unavailable or catalogue busy");
 size_t result[MAX_ENTRIES],total=0,offset=std::max<long>(0,server.arg("offset").toInt());String q=server.arg("q");q.toLowerCase();String mode=server.arg("filter"),category=server.arg("category"),release=server.arg("release"),compatibility=server.arg("compatibility");if(category.isEmpty())category="all";if(release.isEmpty())release="all";if(compatibility.isEmpty())compatibility="all";bool onlySaved=server.arg("view")=="saved"||mode=="saved";uint32_t flash=transportFlashId();
 for(size_t i=0;i<entryCount;i++){auto e=readEntry(i);String text=e.name+" "+e.author+" "+e.description+" "+e.tags;text.toLowerCase();if((onlySaved&&e.path.isEmpty())||(mode=="favorite"&&!e.favorite)||!filterMatches(e,category,release,compatibility,flash)||text.indexOf(q)<0)continue;result[total++]=i;}
 sortIndices(result,total,server.arg("sort"));JsonDocument d;d["schemaVersion"]=1;d["retrievedAt"]=retrievedAt;d["sourceUrl"]=catalogueSource;d["attribution"]=catalogueAttribution;d["license"]=catalogueLicense;d["targetFlashId"]=flash;auto cats=d["categories"].to<JsonArray>();for(size_t i=0;i<categoryCount;i++)cats.add(categories[i]);auto a=d["entries"].to<JsonArray>();
 for(size_t n=offset;n<total&&a.size()<16;n++){auto e=readEntry(result[n]);auto o=a.add<JsonObject>();o["id"]=e.id;o["name"]=e.name;o["author"]=e.author;o["description"]=e.description;o["version"]=e.version;o["hubUrl"]=e.hubUrl;o["sourceUrl"]=e.sourceUrl;o["projectUrl"]=e.projectUrl;o["releaseUrl"]=e.releaseUrl;o["license"]=e.license;o["downloadable"]=e.downloadable;o["downloadReason"]=e.downloadable?String():downloadReason(e);o["size"]=e.size;o["saved"]=!e.path.isEmpty();o["favorite"]=e.favorite;o["tags"].to<JsonArray>().add(e.tags);o["chip"]=e.chip;o["beta"]=e.beta;o["date"]=e.date;o["compatibility"]=gallery::status(gallery::compatibility(e.chip.c_str(),flash));o["hardwareTested"]=false;o["artworkUrl"]=e.artworkUrl;if(e.popularityKnown)o["hubInstalls"]=e.popularity;else o["hubInstalls"]=nullptr;o["path"]=e.path;}
 d["total"]=total;d["offset"]=offset;d["limit"]=16;sendJson(d);});
 server.on("/api/favorite",HTTP_POST,[]{if(!storageReady||locked())return reject(409,"Storage busy");for(size_t i=0;i<entryCount;i++)if(readEntry(i).id==server.arg("id")){toggleFavorite(i);dirty=true;return reject(200,"Favorite updated");}reject(404,"Unknown entry");});
 server.on("/api/download",HTTP_POST,[]{if(!storageReady||locked())return reject(409,"Storage busy");for(size_t i=0;i<entryCount;i++)if(readEntry(i).id==server.arg("id")){auto e=readEntry(i);if(!e.downloadable||!e.path.isEmpty())return reject(400,"No supported maker package or already saved");String id=fileId(e.id+"|"+e.sha256);if(id.isEmpty())return reject(400,"Invalid package ID");Job next;next.path="/fm1/packages/"+id+".fwsc";next.download=true;next.id=e.id;if(requestNetworkJob(next))return reject(202,"Maker download queued; inspect import status for validation result");return reject(503,"Worker unavailable");}reject(404,"Unknown entry");});
 server.on("/api/refresh",HTTP_POST,[]{if(!storageReady||locked())return reject(409,"Storage busy");if(wifiEnabled&&WiFi.status()!=WL_CONNECTED)return reject(409,"Connect station Wi-Fi first");if(freeSpace()<reserveBytes()+64*1024)return reject(409,"Insufficient catalogue working space");Job next;next.path="/fm1/catalogue.json";next.download=false;next.catalogue=true;next.refresh=true;if(requestNetworkJob(next))return reject(202,"Catalogue refresh queued (at most once per day)");reject(503,"Worker unavailable");});
 server.on("/api/upload",HTTP_POST,[]{reject(uploadError.isEmpty()?202:400,uploadError.isEmpty()?"Upload received; inspect import status for validation result":uploadError);},[]{onUpload(false);});
 server.on("/api/catalogue-upload",HTTP_POST,[]{reject(uploadError.isEmpty()?202:400,uploadError.isEmpty()?"Catalogue queued for validation":uploadError);},[]{onUpload(false,true);});
 server.on("/api/trust",HTTP_POST,[]{reject(uploadError.isEmpty()?200:400,uploadError.isEmpty()?"CA bundle saved":uploadError);},[]{onUpload(true);});
 server.on("/api/wifi",HTTP_POST,[]{String ssid=server.arg("ssid"),pass=server.arg("password");if(ssid.isEmpty()||ssid.length()>32||pass.length()>63)return reject(400,"Invalid network credentials");startWifiJoin(ssid,pass);reject(202,"Joining Wi-Fi; the result will appear on the device and in status");});
 server.on("/api/wifi-power",HTTP_POST,[]{if(locked())return reject(409,"Operation in progress");String value=server.arg("enabled");if(value!="true"&&value!="false")return reject(400,"Use enabled=true or false");reject(202,value=="true"?"Turning Wi-Fi on":"Turning Wi-Fi radio off; browser connection will close");pendingWifiPower=value=="true"?1:0;hotspotChangeAt=millis()+300;if(pendingWifiPower==0)pendingHotspot=-1;});
 server.on("/api/hotspot",HTTP_POST,[]{if(locked())return reject(409,"Operation in progress");String enabled=server.arg("enabled");if(enabled!="true"&&enabled!="false")return reject(400,"Use enabled=true or false");bool next=enabled=="true";reject(202,next?"Enabling hotspot":"Disabling hotspot; use the home-network address or on-device controls to reconnect");pendingHotspot=next?1:0;hotspotChangeAt=millis()+300;});
 server.on("/api/files",HTTP_GET,[]{if(!storageReady||locked())return reject(409,"Storage unavailable or busy");JsonDocument d;auto a=d.to<JsonArray>();for(auto dirName:{"/fm1/packages","/fm1/backups","/fm1/logs"}){File dir=disk->open(dirName);for(File f=dir.openNextFile();f&&a.size()<96;f=dir.openNextFile()){if(f.isDirectory())continue;String name=f.name();if(name.endsWith(".part"))continue;auto o=a.add<JsonObject>();o["path"]=String(dirName)+"/"+name.substring(name.lastIndexOf('/')+1);o["size"]=f.size();o["deletableBackup"]=String(dirName)=="/fm1/backups"&&name.endsWith(".bin")&&transportCanDeleteBackup(o["path"].as<String>());}}sendJson(d);});
 server.on("/api/export",HTTP_GET,[]{String path=server.arg("path");if(!storageReady||locked())return reject(409,"Storage unavailable or busy");if(!safePath(path)||path.endsWith(".part")||path=="/fm1/ca.pem")return reject(400,"Invalid path");File f=disk->open(path);if(!f||f.isDirectory())return reject(404,"File missing");String name=exportName(path);server.sendHeader("Content-Disposition","attachment; filename=\""+name+"\"");server.sendHeader("X-Content-Type-Options","nosniff");server.sendHeader("Cache-Control","no-store");server.setContentLength(f.size());server.send(200,exportContentType(name),"");exportFile=f;exportClient=server.client();exportLast=millis();exportActive=true;});
 server.on("/api/cancel",HTTP_POST,[]{if(cancelOperation())reject(202,"Cancellation requested; finishing the current sector");else reject(409,"Operation is not cancellable at this stage");});
 server.on("/api/restore",HTTP_POST,[]{String path=server.arg("path");if(!storageReady||locked())return reject(409,"Storage unavailable or busy");if(!safePath(path)||!path.startsWith("/fm1/backups/")||!path.endsWith(".bin")||!disk->exists(path))return reject(400,"Select an existing backup .bin file");if(!pendingAction.arm(fm1::ActionKind::Restore,path.c_str(),path.c_str()))return reject(409,"Another confirmation is already armed");actionMenu=false;searchPicker=false;detail=false;confirmInstall=true;dirty=true;reject(202,"Restore armed. Hold the on-device confirmation control for two seconds to start; cancel on-device to dismiss.");});
 server.on("/api/delete-backup",HTTP_POST,[]{String path=server.arg("path");if(!storageReady||locked())return reject(409,"Storage unavailable or busy");if(!safePath(path)||!path.startsWith("/fm1/backups/")||!path.endsWith(".bin"))return reject(400,"Select a backup .bin file");if((pendingAction.active()&&path==pendingAction.snapshot().path)||!transportCanDeleteBackup(path))return reject(409,"Backup unavailable or operation in progress");if(!transportDeleteBackup(path))return reject(404,"Cannot remove backup");reject(200,"Backup deleted");});
 server.on("/api/delete",HTTP_POST,[]{String path=server.arg("path");if(!storageReady||locked())return reject(409,"Storage unavailable or busy");if(!safePath(path)||!path.startsWith("/fm1/packages/")||!path.endsWith(".fwsc"))return reject(400,"Only saved packages may be deleted");if(!disk->remove(path))return reject(404,"Cannot remove file");disk->remove(path+".json");loadCatalogue();reject(200,"Package deleted");});
#ifndef FM1_CARDPUTER
 server.on("/api/format",HTTP_POST,[]{if(storageReady||locked())return reject(409,"Initialization is available only when internal storage failed to mount");if(server.arg("confirm")!="FORMAT INTERNAL STORAGE")return reject(400,"Explicit FORMAT INTERNAL STORAGE confirmation required");if(!LittleFS.format()||!LittleFS.begin(false))return reject(500,"Filesystem initialization failed");storageReady=true;for(auto path:{"/fm1","/fm1/packages","/fm1/backups","/fm1/logs"})disk->mkdir(path);loadCatalogue();reject(200,"Internal storage initialized");});
#endif
 webConfigured=true;if(wifiEnabled)server.begin();
}
void selectStep(int amount);
void chooseAction();
#include "local_app.inc"
void selectStep(int amount){size_t n=matchCount();if(n){selected=(selected+n+amount)%n;dirty=true;}}
void installConfirm(){if(locked())return;int idx=selectedIndex();if(idx<0){notice="Save a validated package first";return;}auto selectedEntry=readEntry(idx);if(selectedEntry.path.isEmpty()){notice="Save a validated package first";return;}String label=selectedEntry.name+" "+selectedEntry.version;if(!pendingAction.arm(fm1::ActionKind::Install,selectedEntry.path.c_str(),label.c_str()))return;confirmInstall=true;dirty=true;}
String downloadReason(const Entry& e){
 if(e.id=="jiant")return "JIANT requires its maker installer for additional components. See the project manual; restore official firmware first when coming from other custom firmware.";
 return "No supported standalone maker package is available in this catalogue listing. Use the maker project or installer.";
}
void downloadSelected(){int i=selectedIndex();if(i<0||!storageReady||locked())return;auto e=readEntry(i);if(!e.downloadable){notice=downloadReason(e);dirty=true;return;}if(!e.path.isEmpty()){notice="Already saved offline";dirty=true;return;}Job next;next.path="/fm1/packages/"+fileId(e.id+"|"+e.sha256)+".fwsc";next.download=true;next.id=e.id;requestNetworkJob(next);dirty=true;}
void refreshSelected(){if(!storageReady||locked())return;if(freeSpace()<reserveBytes()+64*1024){notice="Catalogue scratch reserve unavailable";dirty=true;return;}Job next;next.path="/fm1/catalogue.json";next.download=false;next.catalogue=true;next.refresh=true;requestNetworkJob(next);dirty=true;}
void connectOrBackup(){if(locked())return;if(!storageReady){notice="Storage unavailable; Wi-Fi Files for setup";dirty=true;return;}auto s=transportStatus();if(s.connected)transportBackup();else transportConnect();dirty=true;}
void chooseAction() {
    switch (actionIndex) {
    case 0: setMainTab(0); return;
    case 1: setMainTab(1); return;
    case 2: openEditor(local::EditTarget::Search, query, 48); return;
    case 3: {
        int index = selectedIndex();
        if (index >= 0 && !locked()) toggleFavorite(index);
        break;
    }
    case 4: downloadSelected(); break;
    case 5: installConfirm(); break;
    case 6:
        if (!locked()) transportConnect();
        break;
    case 7:
        if (!locked() && storageReady) transportBackup();
        else notice = "Storage unavailable or busy";
        break;
    case 8: backupsFromStorage = false; openBackups(); return;
    case 9: beginWifiScan(); return;
    case 10: refreshSelected(); break;
    case 11: {
        size_t next = 0;
        if (categoryFilter != "all") {
            for (size_t i = 0; i < categoryCount; ++i) if (categories[i] == categoryFilter) next = i + 1;
        }
        categoryFilter = next < categoryCount ? categories[next] : "all";
        selected = 0;
        dirty = true;
        return;
    }
    case 12:
        releaseFilter = releaseFilter == "all" ? "stable" : releaseFilter == "stable" ? "beta" : "all";
        selected = 0; dirty = true; return;
    case 13:
        compatibilityFilter = compatibilityFilter == "all" ? "declared" : compatibilityFilter == "declared" ? "incompatible" : compatibilityFilter == "incompatible" ? "unknown" : "all";
        selected = 0; dirty = true; return;
    case 14:
        sortOrder = sortOrder == "popular" ? "name" : sortOrder == "name" ? "newest" : "popular";
        selected = 0; dirty = true; return;
    case 15: favoritesOnly = !favoritesOnly; selected = 0; dirty = true; return;
    case 16: openStorage(true); return;
    case 17: localPage=LocalPage::WebUi;actionMenu=false;hotspotSelected=0;dirty=true;return;
    case 18: {
        auto status = transportStatus();
        String message = status.stage + ": " + status.detail;
        xSemaphoreTake(jobMutex, portMAX_DELAY);
        message += " / " + jobStage + ": " + jobDetail;
        xSemaphoreGive(jobMutex);
        showLocalInfo("Last result", message);
        return;
    }
    case 19: setMainTab(2); return;
    default: showHome(); return;
    }
    if (!confirmInstall) actionMenu = false;
    dirty = true;
}
// NetworkClient does not override Print::availableForWrite() (always 0), so gating on it
// never sent a byte. write() blocks until the stack accepts the data or times out.
void exportTick(){if(!exportActive)return;uint32_t tickStart=millis();for(int chunk=0;chunk<8&&exportFile.available()&&exportClient.connected()&&millis()-tickStart<20;chunk++){uint8_t buffer[1024];size_t got=exportFile.read(buffer,sizeof(buffer));if(!got)break;size_t sent=exportClient.write(buffer,got);if(sent!=got){exportClient.stop();exportFile.close();exportActive=false;return;}exportLast=millis();}if(!exportFile.available()||!exportClient.connected()||millis()-exportLast>10000){exportFile.close();exportClient.stop();exportActive=false;}}
} // namespace
void appSetup(){
 auto cfg=M5.config();cfg.internal_spk=false;cfg.internal_mic=false;
#ifdef FM1_CARDPUTER
 // M5Unified/M5GFX detect original versus ADV before using this fallback.
 // M5Cardputer then selects the matching keyboard reader at runtime.
 cfg.fallback_board=m5::board_t::board_M5Cardputer;
#else
 cfg.fallback_board=m5::board_t::board_M5StickS3;
 // Grove/Hat 5 V (M5PM1 reg 0x06 bit 3) stays off. It does not reach USB-C
 // (VBUS there measures ~0 V either way); the FM-1 runs on its own battery.
 cfg.output_power=false;
#endif
#ifdef FM1_CARDPUTER
 M5Cardputer.begin(cfg,true);DISPLAY.setRotation(0); // Panel already has a 90-degree mounting offset.
#else
 M5.begin(cfg);DISPLAY.setRotation(1);
#endif
 beginLocalDisplay();
 jobMutex=xSemaphoreCreateMutex();preferences.begin("fm1-switcher",false);
#ifdef FM1_CARDPUTER
 SPI.begin(40,39,14,12);storageReady=SD.begin(12,SPI,20000000);disk=&SD;
#else
 storageReady=LittleFS.begin(false);disk=&LittleFS;
#endif
 if(storageReady){for(auto path:{"/fm1","/fm1/packages","/fm1/backups","/fm1/logs"})disk->mkdir(path);loadCatalogue();}else { notice="Storage setup needed"; localPage=LocalPage::Setup; }
 if(storageReady)removeStaleParts();
 transportBegin(*disk);
 char pass[20];snprintf(pass,sizeof(pass),"fm1-%08lx",(unsigned long)(ESP.getEfuseMac()&0xffffffff));apPassword=pass;
 WiFi.onEvent([](arduino_event_id_t event,arduino_event_info_t info){
  if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED)wifiDisconnectReason.store(info.wifi_sta_disconnected.reason);
 });
 setWifiPower(preferences.getBool("wifiEnabled",true));
 configureWeb();dirty=true;
}
#ifdef FM1_CARDPUTER
namespace {
bool cardputerBackPressed() {
 auto& keyboard=M5Cardputer.Keyboard;
 const auto& keys=keyboard.keysState();
 // The physical top-left ESC key is reported as backtick by M5Cardputer.
 return keyboard.isKeyPressed('`') || keyboard.isKeyPressed(char(27)) ||
  (!searchPicker && keyboard.isKeyPressed(',')) ||
  std::find(keys.hid_keys.begin(),keys.hid_keys.end(),uint8_t(0x29))!=keys.hid_keys.end();
}
}
#endif
void appLoop() {
#ifdef FM1_CARDPUTER
    M5Cardputer.update();
#else
    M5.update();
#endif
    if(pendingWifiPower>=0 && int32_t(millis()-hotspotChangeAt)>=0){setWifiPower(pendingWifiPower!=0);pendingWifiPower=-1;}
    if(pendingHotspot>=0 && int32_t(millis()-hotspotChangeAt)>=0){setHotspot(pendingHotspot!=0);pendingHotspot=-1;dirty=true;}
    transportTick();
    if (exportActive) exportTick();
    else server.handleClient();
    if (!jobBusy.load() && reload.exchange(false)) loadCatalogue();
    pollWifiScan();

#ifndef FM1_CARDPUTER
    static local::StickButtons stickButtons;
    const bool a = M5.BtnA.isPressed(), b = M5.BtnB.isPressed();
    const auto stickAction = stickButtons.update(a, b, millis(), searchPicker ? 150 : 300);
#endif
    auto transport = transportStatus();
    if (jobBusy.load() || transport.busy) {
#ifdef FM1_CARDPUTER
        if (cardputerBackPressed()) cancelOperation();
#else
        // The progress display stays visible, but Home becomes the return view.
        if (stickAction == local::StickAction::Actions) showHome();
        if (stickAction == local::StickAction::Back) cancelOperation();
#endif
        paintLocalTick();
        delay(2);
        return;
    }
    searchTick();
    static uint32_t heldSince = 0, heldAction = 0;
    static bool holdFired = false;
    bool confirmHeld = false;
#ifdef FM1_CARDPUTER
    auto& keyboard = M5Cardputer.Keyboard;
    if (keyboard.isChange() && keyboard.isPressed()) {
        auto keys = keyboard.keysState();
        if (cardputerBackPressed()) backLocal();
        else if (confirmInstall || localConfirmation != LocalConfirmation::None) {
            // The physical snapshot is frozen: no navigation or typing while reviewing.
        } else if (searchPicker) {
            if (keys.tab && textEditor.target() == local::EditTarget::Password) {textEditor.togglePasswordVisibility();dirty=true;}
            else if (keys.enter) completeEditor();
            else if (keys.del) { textEditor.backspace(); dirty = true; }
            else for (char character : keys.word) {
                if (textEditor.type(character)) dirty = true;
            }
        } else if (keys.fn && keyboard.isKeyPressed('f')) showHome();
        else if (keys.enter || keyboard.isKeyPressed('/')) activateLocal();
        else if (keys.tab && !actionMenu && localPage == LocalPage::Home) setMainTab(mainTab + 1);
        else if (keys.del && !query.isEmpty() && localPage == LocalPage::Home && !actionMenu) {
            query.remove(query.length() - 1); selected = 0; dirty = true;
        } else for (char character : keys.word) {
            if (character == ';') moveLocal(-1);
            else if (character == '.') moveLocal(1);
            else if (actionMenu || localPage != LocalPage::Home) continue;
            else if (character == '[') {
                int index = selectedIndex();
                if (index >= 0 && !locked()) toggleFavorite(index);
                dirty = true;
            } else if (character == ']') { connectOrBackup(); dirty = true; }
            else if (character == '{') downloadSelected();
            else if (character == '}') refreshSelected();
            else if (character == '\\' && detail) installConfirm();
            else if (character >= 32 && character < 127 && mainTab != 2) {
                query += character; query = clipped(query, 48);
                selected = 0; detail = false; dirty = true;
            }
        }
    }
    confirmHeld = keyboard.keysState().enter || (!searchPicker && keyboard.isKeyPressed('/'));
    if (!confirmInstall) { heldSince = 0; holdFired = false; }
#else
    switch (stickAction) {
    case local::StickAction::Choose:
        if (!confirmInstall && localConfirmation == LocalConfirmation::None) activateLocal();
        break;
    case local::StickAction::Next: moveLocal(1); break;
    case local::StickAction::Previous: moveLocal(-1); break;
    case local::StickAction::Back: backLocal(); break;
    case local::StickAction::Actions: showHome(); break;
    default: break;
    }
    confirmHeld = stickButtons.confirmHeld(a, b);
#endif

    if (confirmInstall && heldAction != pendingAction.generation()) {
        heldAction = pendingAction.generation(); heldSince = 0; holdFired = false;
    }
    if (confirmInstall) {
        if (confirmHeld) {
            if (!heldSince) heldSince = millis();
            if (millis() - heldSince >= 2000 && !holdFired) {
                fm1::ActionSnapshot action;
                if (!jobBusy.load() && !exportActive && !transportStatus().busy && pendingAction.consume(action)) {
                    bool queued = action.kind == fm1::ActionKind::Restore ? transportRestore(action.path) : transportInstall(action.path);
                    if (queued) setMainTab(2);
                    else notice = "Could not queue operation; review and retry";
                }
                holdFired = true; confirmInstall = false; dirty = true;
            }
        } else { heldSince = 0; holdFired = false; }
    } else { heldSince = 0; holdFired = false; pendingAction.cancel(); }

    if (localConfirmation != LocalConfirmation::None) {
        if (!confirmHeld) { localReleased = true; localHeldSince = 0; localHeldFired = false; }
        else if (localReleased) {
            if (!localHeldSince) localHeldSince = millis();
            if (millis() - localHeldSince >= 2000 && !localHeldFired) {
                localHeldFired = true;
                completeLocalConfirmation();
            }
        }
    }
    paintLocalTick();
    delay(2);
}
} // namespace device
