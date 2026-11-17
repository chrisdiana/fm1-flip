#include "device/catalogue.h"
#include "device/catalogue_seed.h"
#include "device/trust.h"
#include "device/network_diagnostics.h"
#include "device/download_stream.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <time.h>
#include <regex.h>
#include <cerrno>
namespace device { namespace {
constexpr char HUB[]="https://thefirmwarehub.com/catalog.json";
constexpr char AGENT[]="FM1-Flip/0.1 (portable FM-1 firmware manager; The Firmware Hub client)";
constexpr uint32_t RAW_LIMIT=1024*1024;
String clip(String value,size_t n){if(value.length()>n)value.remove(n);return value;}
String caBundle(fs::FS& fs){File f=fs.open("/fm1/ca.pem");if(f&&f.size()<=32768)return f.readString();return String();}
bool validRepo(const String& repo){int slash=repo.indexOf('/');if(slash<=0||slash==(int)repo.length()-1||repo.indexOf('/',slash+1)>=0)return false;for(auto c:repo)if(!isalnum((unsigned char)c)&&c!='_'&&c!='-'&&c!='.'&&c!='/')return false;return repo.indexOf("..")<0;}
String encode(const String& s){String out;char b[4];for(auto c:s){if(isalnum((unsigned char)c)||c=='-'||c=='_'||c=='.'||c=='~')out+=c;else{snprintf(b,sizeof(b),"%%%02X",(unsigned char)c);out+=b;}}return out;}
String decode(const String& s){String out;for(size_t i=0;i<s.length();i++){if(s[i]=='%'&&i+2<s.length()&&isxdigit(s[i+1])&&isxdigit(s[i+2])){char v[3]={s[i+1],s[i+2],0};out+=(char)strtoul(v,nullptr,16);i+=2;}else out+=s[i];}return out;}
bool hashValid(const String& hash){if(hash.length()!=64)return false;for(auto c:hash)if(!isxdigit(c))return false;return true;}
String compileAssetPattern(const String& input,String& output,int& flags){
 if(input.length()>256)return "Maker asset pattern exceeds budget";output="^(";flags=REG_EXTENDED|REG_NOSUB;size_t start=0;bool inClass=false;
 if(strncmp(input.c_str(),"(?i)",4)==0){flags|=REG_ICASE;start=4;}
 for(size_t i=start;i<input.length();i++){char c=input[i];
  if(c=='\\'){if(++i>=input.length())return "Incomplete maker asset escape";char next=input[i];
   if(next=='d')output+=inClass?"0-9":"[0-9]";
   else if(next=='w')output+=inClass?"A-Za-z0-9_":"[A-Za-z0-9_]";
   else if(next=='s')output+=inClass?"[:space:]":"[[:space:]]";
   else if(isalnum((unsigned char)next))return "Unsupported maker asset escape";
   else if(next=='"'||next=='\''||next=='/')output+=next;
   else{output+='\\';output+=next;}
  }else if(!inClass&&c=='('&&i+1<input.length()&&input[i+1]=='?'){
   if(i+2>=input.length()||input[i+2]!=':')return "Unsupported maker asset group";output+='(';i+=2;
  }else{if(c=='['&&!inClass)inClass=true;else if(c==']'&&inClass)inClass=false;output+=c;}
 }
 if(inClass)return "Unclosed maker asset character class";output+=")$";return String();
}
String hubArtworkUrl(const String& path){
 const char* value=path.c_str();size_t length=path.length();
 if(length<12||length>220||strncmp(value,"fw/cards/",9)||strstr(value,"..")||strchr(value+9,'/'))return String();
 for(size_t i=0;i<length;i++)if(!isalnum((unsigned char)value[i])&&value[i]!='/'&&value[i]!='.'&&value[i]!='_'&&value[i]!='-')return String();
 const char* extension=strrchr(value,'.');
 if(!extension||extension<=value+9||extension-(value+9)>180||strlen(extension)>5)return String();
 char suffix[6]{};for(size_t i=0;extension[i];i++)suffix[i]=tolower((unsigned char)extension[i]);
 if(strcmp(suffix,".webp")&&strcmp(suffix,".png")&&strcmp(suffix,".jpg")&&strcmp(suffix,".jpeg"))return String();
 return String("https://thefirmwarehub.com/")+path;
}
class LimitedStream:public Stream{Stream& source;size_t remaining;public:LimitedStream(Stream& s,size_t limit):source(s),remaining(limit){}int available()override{return remaining?source.available():0;}int read()override{if(!remaining)return -1;int b=source.read();if(b>=0)--remaining;return b;}int peek()override{return remaining?source.peek():-1;}void flush()override{}size_t write(uint8_t)override{return 0;}};
int nextNonSpace(File& f){int c;do{c=f.read();}while(c>=0&&isspace(c));return c;}
bool readMember(File& file,JsonDocument& value,size_t limit){
 int first=nextNonSpace(file);if(first<0)return false;
 if(first=='-'||isdigit(first)){String token;int c=first;do{if(token.length()>=63)return false;token+=(char)c;c=file.read();}while(c>=0&&!isspace(c)&&c!=','&&c!='}');if(c>=0)file.seek(file.position()-1);return !deserializeJson(value,token);}
 file.seek(file.position()-1);LimitedStream bounded(file,limit);return !deserializeJson(value,bounded,DeserializationOption::NestingLimit(12));
}
String normalize(File& in,File& out,const ScratchSpace& space){
 if(nextNonSpace(in)!='{')return "Catalogue root must be an object";
 JsonDocument root;root["schemaVersion"]=1;root["sourceUrl"]=HUB;root["attribution"]="The Firmware Hub — https://thefirmwarehub.com";root["license"]="CC-BY-4.0";time_t now=time(nullptr);char timestamp[32];struct tm tm;gmtime_r(&now,&tm);strftime(timestamp,sizeof(timestamp),"%Y-%m-%dT%H:%M:%SZ",&tm);root["retrievedAt"]=timestamp;
 String prefix;serializeJson(root,prefix);prefix.remove(prefix.length()-1);out.print(prefix);out.print(",\"entries\":[");bool haveLicense=false,haveUpdated=false,haveEntries=false;size_t count=0,total=0;uint32_t seen[512];
 while(true){int peek=nextNonSpace(in);if(peek=='}')break;if(peek!='"')return "Invalid catalogue key";in.seek(in.position()-1);JsonDocument key;LimitedStream keyStream(in,256);if(deserializeJson(key,keyStream)||!key.is<const char*>())return "Invalid catalogue key";String name=key.as<String>();if(nextNonSpace(in)!=':')return "Invalid catalogue delimiter";
 if(name=="firmwares"){
  if(haveEntries||nextNonSpace(in)!='[')return "Invalid or duplicate firmwares array";haveEntries=true;
  while(true){int c=nextNonSpace(in);if(c==']')break;if(c!='{'||total>=512)return "Invalid or excessive firmware entries";in.seek(in.position()-1);JsonDocument e;LimitedStream item(in,16384);if(deserializeJson(e,item,DeserializationOption::NestingLimit(12)))return "Malformed or oversized firmware entry";
   String id=e["id"]|"",title=e["name"]|"",repo=e["repo"]|"",hash=e["sha256"]|"",tag=e["tag"]|"",release=e["release"]|"";
   if(id.isEmpty()||id.length()>96||title.isEmpty()||title.length()>256||!validRepo(repo)||(!hash.isEmpty()&&!hashValid(hash)))return "Invalid firmware identity, repository or hash";
   uint32_t fingerprint=2166136261;for(auto v:id){fingerprint^=(uint8_t)v;fingerprint*=16777619;}for(size_t i=0;i<total;i++)if(seen[i]==fingerprint)return "Duplicate firmware identity";seen[total++]=fingerprint;
   bool makerRelease=false;if(!release.isEmpty()){String base="https://github.com/"+repo+"/releases/tag/";if(release.startsWith(base)&&release.indexOf('?')<0&&release.indexOf('#')<0){String actual=decode(release.substring(base.length()));if(!actual.isEmpty()&&actual.indexOf('/')<0&&(tag.isEmpty()||actual==tag)){tag=actual;makerRelease=true;}}}

   if(count<512){JsonDocument n;n["id"]=id;n["name"]=clip(title,80);n["author"]=clip(e["author"]|"",80);n["description"]=clip(e["what"]|"",640);n["target"]="FM-1";n["version"]=clip(e["version"]|"",40);n["date"]=clip(e["date"]|"",64);n["license"]=clip(e["lic"]|"",96);n["repo"]=repo;n["sourceUrl"]="https://github.com/"+repo;n["projectUrl"]=e["site"];n["hubUrl"]="https://thefirmwarehub.com/";n["releaseUrl"]=release;n["tag"]=tag;n["asset_pattern"]=clip(e["assetPkg"]|"",256);n["identity"]=clip(e["product"]|"",128);n["identity_pattern"]=clip(e["ident"]|"",256);n["size"]=e["size"];n["sha256"]=hash;n["chip"]=e["chip"];n["beta"]=e["beta"]|false;n["external_only"]=!hashValid(hash)||!makerRelease||e["size"].isNull()||e["pkg"].isNull();String artwork=hubArtworkUrl(e["card"]|"");if(artwork.length()){n["artworkUrl"]=artwork;n["artworkKind"]="hub-card";}else{n["artworkUrl"]=nullptr;n["artworkKind"]=nullptr;}if(e["installs"].is<uint32_t>()&&e["installs"].as<uint32_t>()<=2147483647u)n["hubInstalls"]=e["installs"].as<uint32_t>();else n["hubInstalls"]=nullptr;auto tags=n["tags"].to<JsonArray>();if(e["kind"].is<const char*>())tags.add(clip(e["kind"].as<String>(),32));else for(JsonVariant t:e["kind"].as<JsonArray>())if(tags.size()<12)tags.add(clip(t.as<String>(),32));if(space&&space()<8192)return "Catalogue scratch space reached backup reserve";if(count++)out.print(',');serializeJson(n,out);if(out.position()>1024*1024)return "Normalized catalogue exceeds metadata budget";}
   c=nextNonSpace(in);if(c==']')break;if(c!=',')return "Malformed firmware array";vTaskDelay(1);
  }
 }else{JsonDocument value;if(name=="license"||name=="updated"){LimitedStream member(in,2048);if(deserializeJson(value,member,DeserializationOption::NestingLimit(12)))return "Malformed catalogue metadata";if(name=="license")haveLicense=value["license"]=="CC-BY-4.0";if(name=="updated")haveUpdated=value.is<const char*>();}else{JsonDocument discard;discard.set(false);if(deserializeJson(value,in,DeserializationOption::Filter(discard),DeserializationOption::NestingLimit(16)))return "Malformed optional catalogue metadata";}}
 int c=nextNonSpace(in);if(c=='}')break;if(c!=',')return "Malformed catalogue root";
 }
 if(!haveLicense||!haveUpdated||!haveEntries)return "Unsupported catalogue schema or license";
 out.printf("],\"sourceEntryCount\":%u,\"omittedEntries\":%u}",(unsigned)total,(unsigned)(total-count));return out.getWriteError()?"Catalogue write failed":String();
}
String get(fs::FS& fs,const String& url,const String& path,uint32_t limit,NetworkProgress progress,Preferences* cache=nullptr,bool conditional=true,ScratchSpace space=ScratchSpace()){
 String ready=networkPreflight();if(!ready.isEmpty())return ready;
 String ca=caBundle(fs);WiFiClientSecure client;client.setCACert(ca.isEmpty()?FM1_TRUST_ROOTS:ca.c_str());HTTPClient http;http.setConnectTimeout(15000);http.setTimeout(15000);client.setHandshakeTimeout(15);http.setUserAgent(AGENT);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);if(!http.begin(client,url))return "Cannot initialize HTTPS";
 const char* headers[]={"ETag","Last-Modified"};http.collectHeaders(headers,2);http.addHeader("Accept","application/json");http.addHeader("Accept-Encoding","identity");if(cache&&conditional){String etag=cache->getString("etag"),modified=cache->getString("modified");if(etag.length())http.addHeader("If-None-Match",etag);if(modified.length())http.addHeader("If-Modified-Since",modified);}
 int code=http.GET();if(code==304){http.end();return "NOT_MODIFIED";}if(code!=200){String error=httpsFailure(code,client,url);http.end();return error;}int expected=http.getSize();
 if(expected>=0&&uint32_t(expected)>limit){http.end();return "Metadata response exceeds size limit";}
 if(expected>=0&&space&&uint64_t(expected)>space()){http.end();return "Insufficient metadata scratch space";}
 File out=fs.open(path,FILE_WRITE);if(!out){http.end();return "Cannot create pending response";}
 int writeErrno=0;
 DownloadStream sink(limit,0,[&]()->uint64_t{return space?space():UINT64_MAX;},
  [&](const uint8_t* data,size_t size){errno=0;auto wrote=out.write(data,size);if(wrote<size)writeErrno=errno;return wrote;},
  [](const uint8_t*,size_t){},
  [&](size_t count){if(progress)progress("Catalogue/network",url,count,expected>0?expected:0);vTaskDelay(1);},
  {},"Metadata response exceeds size limit");
 // Decode chunk framing through HTTPClient instead of reading its raw socket.
 int received=http.writeToStream(&sink);String error;
 if(!sink.error.empty()){error=sink.error.c_str();if(sink.error=="Download storage write failed")error+=" at byte "+String((unsigned long)sink.bytesWritten())+", errno "+String(writeErrno)+" ("+(writeErrno?String(strerror(writeErrno)):String("driver supplied no error code"))+")";}
 else if(received<0||(expected>=0&&received!=expected))error="Interrupted metadata response";
 out.close();if(error.isEmpty()&&cache){cache->putString("nextEtag",clip(http.header("ETag"),256));cache->putString("nextModified",clip(http.header("Last-Modified"),128));}http.end();if(!error.isEmpty())fs.remove(path);return error;
}
} // namespace
void commitCatalogueCache(){Preferences cache;cache.begin("fm1-catalogue",false);cache.putString("etag",cache.getString("nextEtag"));cache.putString("modified",cache.getString("nextModified"));}
String scanCatalogue(File& file,const CatalogueVisitor& visitor){
 if(!file||file.size()>1024*1024||nextNonSpace(file)!='{')return "Invalid catalogue root or size";
 bool schema=false,entries=false;size_t count=0;uint32_t ids[512];
 while(true){int c=nextNonSpace(file);if(c=='}')break;if(c!='"')return "Invalid catalogue key";file.seek(file.position()-1);JsonDocument key;LimitedStream ks(file,128);if(deserializeJson(key,ks)||nextNonSpace(file)!=':')return "Invalid catalogue member";String name=key.as<String>();
 if(name=="entries"){if(entries||nextNonSpace(file)!='[')return "Invalid entries";entries=true;
 while(true){c=nextNonSpace(file);if(c==']')break;if(c!='{'||count>=512)return "Too many or invalid entries";uint32_t offset=file.position()-1;file.seek(offset);JsonDocument item;LimitedStream bounded(file,8192);if(deserializeJson(item,bounded,DeserializationOption::NestingLimit(8)))return "Oversized or invalid entry";String id=item["id"]|"";String title=item["name"]|"";if(id.isEmpty()||id.length()>96||title.isEmpty()||title.length()>256)return "Invalid entry identity";uint32_t fingerprint=2166136261;for(auto b:id){fingerprint^=(uint8_t)b;fingerprint*=16777619;}for(size_t i=0;i<count;i++)if(ids[i]==fingerprint)return "Duplicate entry identity";ids[count++]=fingerprint;if(visitor)visitor(offset,item.as<JsonObject>());c=nextNonSpace(file);if(c==']')break;if(c!=',')return "Invalid entry delimiter";vTaskDelay(1);}
 }else{JsonDocument value;if(!readMember(file,value,2048))return "Oversized catalogue metadata";if(name=="schemaVersion")schema=value.as<int>()==1;}
 c=nextNonSpace(file);if(c=='}')break;if(c!=',')return "Invalid catalogue delimiter";
 }
 if(nextNonSpace(file)!=-1)return "Trailing catalogue data";
 return schema&&entries?String():String("Unsupported catalogue schema");
}
String refreshHub(fs::FS& fs,const String& pending,NetworkProgress progress,ScratchSpace space){
 time_t now=time(nullptr);if(now<1700000000)return "Clock not synchronized; wait for NTP after connecting Wi-Fi";Preferences cache;cache.begin("fm1-catalogue",false);uint64_t last=cache.getULong64("attempt",0);if(last&&uint64_t(now)<last+86400)return "Catalogue requests are limited to once per 24 hours (including failed attempts)";cache.putULong64("attempt",now);
 File valid=fs.open("/fm1/catalogue.json");bool haveCache=scanCatalogue(valid).isEmpty();valid.close();String raw="/fm1/catalogue.raw.part";String error=get(fs,HUB,raw,RAW_LIMIT,progress,&cache,haveCache,space);if(error=="NOT_MODIFIED")return error;if(!error.isEmpty())return error;File in=fs.open(raw);File out=fs.open(pending,FILE_WRITE);if(!in||!out)error="Cannot open catalogue cache";else error=normalize(in,out,space);in.close();out.close();fs.remove(raw);if(!error.isEmpty())fs.remove(pending);return error;
}
String resolveMakerAsset(fs::FS& fs,const String& id,String& url,String& hash,String& metadata,NetworkProgress progress){
 File cat=fs.open("/fm1/catalogue.json");JsonDocument doc;
 String error=scanCatalogue(cat,[&](uint32_t,JsonObject e){if(e["id"]==id){enrichBundledPresentation(e);doc.set(e);}});cat.close();if(!error.isEmpty())return error;JsonObject entry=doc.as<JsonObject>();if(entry.isNull())return "Unknown catalogue entry";
 String repo=entry["repo"]|"",tag=entry["tag"]|"",release=entry["releaseUrl"]|"";hash=entry["sha256"]|"";if(!validRepo(repo)||tag.isEmpty()||tag.indexOf('/')>=0||!hashValid(hash)||release!="https://github.com/"+repo+"/releases/tag/"+encode(tag))return "No supported maker release with a source SHA-256";serializeJson(entry,metadata);String pattern=entry["asset_pattern"]|"";uint32_t expectedSize=entry["size"]|0;doc.clear();
 String raw="/fm1/release.part";error=get(fs,"https://api.github.com/repos/"+repo+"/releases/tags/"+encode(tag),raw,256*1024,progress);if(!error.isEmpty())return error;File f=fs.open(raw);JsonDocument filter;filter["assets"][0]["name"]=true;filter["assets"][0]["browser_download_url"]=true;filter["assets"][0]["size"]=true;if(deserializeJson(doc,f,DeserializationOption::Filter(filter)))error="Malformed maker release response";f.close();fs.remove(raw);if(!error.isEmpty())return error;
 regex_t regex;bool compiled=false;if(pattern.length()){String translated;int flags;String patternError=compileAssetPattern(pattern,translated,flags);if(!patternError.isEmpty())return patternError;if(regcomp(&regex,translated.c_str(),flags)!=0)return "Unsupported maker asset regex";compiled=true;}
 int matches=0;for(JsonObject asset:doc["assets"].as<JsonArray>()){String name=asset["name"]|"",candidate=asset["browser_download_url"]|"";if(!name.endsWith(".fwsc")||name.indexOf('/')>=0||name.length()>192)continue;if(expectedSize&&asset["size"].as<uint32_t>()!=expectedSize)continue;if(compiled&&regexec(&regex,name.c_str(),0,nullptr,0)!=0)continue;String base="https://github.com/"+repo+"/releases/download/"+encode(tag)+"/";if(!candidate.startsWith(base)||decode(candidate.substring(base.length()))!=name)continue;url=candidate;matches++;}
 if(compiled)regfree(&regex);return matches==1?String():"Expected exactly one matching maker .fwsc release asset";
}
} // namespace device
