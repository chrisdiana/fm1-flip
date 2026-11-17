#!/usr/bin/env python3
"""Execute the production backup deletion policy against a one-backup filesystem."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/transport/device_transport.cpp').read_text()
policy=s[s.index('bool transportCanDeleteBackup('):s.rfind('\n}')]
with tempfile.TemporaryDirectory(prefix='fm1-delete-') as name:
 p=Path(name)
 (p/'main.cpp').write_text(r'''
#include <string>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <cctype>
#include <iostream>
struct String:std::string {using std::string::string;String(std::string s):std::string(s){}bool startsWith(const char* p)const{return rfind(p,0)==0;}bool endsWith(const char* p)const{return size()>=strlen(p)&&compare(size()-strlen(p),strlen(p),p)==0;}int indexOf(const char* p)const{auto n=find(p);return n==npos?-1:int(n);}int lastIndexOf(char c)const{auto n=rfind(c);return n==npos?-1:int(n);}String substring(size_t a,size_t b=npos)const{return substr(a,b==npos?npos:b-a);}bool isEmpty()const{return empty();}uint32_t toInt()const{return std::stoul(*this);}};
constexpr uint32_t FlashSize=1024;
struct Status {bool busy=false,backupVerified=true;String lastBackupPath="/fm1/backups/backup-1.bin";} state;
Status transportStatus(){return state;}
void xSemaphoreTake(int,int){}void xSemaphoreGive(int){}int lock=0,portMAX_DELAY=0;
bool present=true;
struct File {int kind=0;operator bool()const{return kind!=0;}bool isDirectory(){return false;}size_t size(){return kind==1?FlashSize:100;}String name(){return "backup-1.bin";}File openNextFile(){if(kind==3){kind=4;return {1};}return {};}};
struct Disk {File open(const String& p){if(p=="/fm1/backups")return {3};if(!present)return {};return {p.endsWith(".json")?2:1};}bool exists(const String&){return present;}bool remove(const String& p){if(p.endsWith(".json"))return true;if(!present)return false;present=false;return true;}} fs,*disk=&fs;
struct Value {bool operator!=(bool)const{return false;}bool operator!=(const char*)const{return false;}};
struct JsonDocument {Value operator[](const char*){return {};}};
bool deserializeJson(JsonDocument&,File&){return false;}
'''+'\n'+policy+r'''
int main(){
 assert(transportCanDeleteBackup("/fm1/backups/backup-1.bin")&&"Explicit deletion must allow the only/latest backup");
 state.busy=true;assert(!transportCanDeleteBackup("/fm1/backups/backup-1.bin"));state.busy=false;
 assert(!transportCanDeleteBackup("/fm1/backups/../backup-1.bin"));
 assert(!transportCanDeleteBackup("/fm1/packages/backup-1.bin"));
 assert(!transportCanDeleteBackup("/fm1/backups/backup-x.bin"));
 present=false;assert(!transportCanDeleteBackup("/fm1/backups/backup-1.bin"));present=true;
#ifdef DELETE_OPERATION
 assert(transportDeleteBackup("/fm1/backups/backup-1.bin"));
 assert(!present&&!state.backupVerified&&state.lastBackupPath.empty());
 assert(!transportDeleteBackup("/fm1/backups/backup-1.bin"));
#endif
 std::cout<<"Backup deletion: only/latest backup, busy guard, invalid paths, missing file and status cleanup passed\n";
}
''')
 flags=['-DDELETE_OPERATION'] if 'bool transportDeleteBackup(' in policy else []
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra',*flags,str(p/'main.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
