#pragma once
#include <cstdint>
#include <cstring>
#include <cctype>
namespace device { namespace gallery {
enum class Compatibility { Unknown, DeclaredMatch, Incompatible };
inline Compatibility compatibility(const char* chip,uint32_t flash){
 if(flash!=0x856014&&flash!=0xb36014)return Compatibility::Unknown;
 if(!strcmp(chip,"new"))return Compatibility::DeclaredMatch;
 if(!strcmp(chip,"old"))return flash==0x856014?Compatibility::DeclaredMatch:Compatibility::Incompatible;
 return Compatibility::Unknown;
}
inline const char* status(Compatibility value){return value==Compatibility::DeclaredMatch?"declared":value==Compatibility::Incompatible?"incompatible":"unknown";}
inline bool releaseMatches(bool beta,const char* filter){return !strcmp(filter,"beta")?beta:!strcmp(filter,"stable")?!beta:true;}
inline bool categoryMatches(const char* tags,const char* category){
 if(!*category||!strcmp(category,"all"))return true;size_t length=strlen(category);
 for(const char* match=strstr(tags,category);match;match=strstr(match+1,category))if((match==tags||match[-1]==' ')&&(match[length]==0||match[length]==' '))return true;
 return false;
}
// Fixed prefixes keep sorting independent of filesystem reads and bound RAM.
// Equal prefixes use the source order as their deterministic tie breaker.
enum class SortMode { Popular, Name, Newest };
inline SortMode sortMode(const char* value){return !strcmp(value,"newest")?SortMode::Newest:!strcmp(value,"name")?SortMode::Name:SortMode::Popular;}
struct SortKey {char name[25]={};char date[11]={};uint32_t popularity=0;bool popularityKnown=false;};
inline SortKey sortKey(const char* name,const char* date,uint32_t popularity=0,bool popularityKnown=false){
 SortKey key;for(size_t i=0;i<24&&name[i];i++)key.name[i]=tolower((unsigned char)name[i]);
 if(strlen(date)>=10){bool valid=true;for(size_t i=0;i<10;i++)if(i==4||i==7){if(date[i]!='-')valid=false;}else if(!isdigit((unsigned char)date[i]))valid=false;if(valid)memcpy(key.date,date,10);}
 key.popularity=popularity;key.popularityKnown=popularityKnown;
 return key;
}
inline bool less(const SortKey& a,const SortKey& b,SortMode mode,size_t left,size_t right){
 if(mode==SortMode::Popular){if(a.popularityKnown!=b.popularityKnown)return a.popularityKnown;if(a.popularity!=b.popularity)return a.popularity>b.popularity;}
 if(mode==SortMode::Newest){int date=strcmp(a.date,b.date);if(date)return date>0;}
 int name=strcmp(a.name,b.name);return name?name<0:left<right;
}
inline bool less(const SortKey& a,const SortKey& b,bool newest,size_t left,size_t right){return less(a,b,newest?SortMode::Newest:SortMode::Name,left,right);}
}}
