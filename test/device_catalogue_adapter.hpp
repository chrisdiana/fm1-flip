// Desktop filesystem and Arduino String adapters for compiling the device parser.
// Parser functions themselves are extracted unchanged except String method spelling.
#pragma once

#include <ArduinoJson.h>
#include <regex.h>
#include <cstring>
#include <fstream>
#include <string>
#include <functional>
#include <iostream>
#include <vector>
#include <cassert>
#include <regex>
#include <ctime>
#include <cstdarg>
using String=std::string;
struct Stream {virtual int available()=0;virtual int read()=0;virtual int peek()=0;virtual void flush()=0;virtual size_t write(uint8_t)=0;size_t readBytes(char* p,size_t n){size_t i=0;for(;i<n;i++){int c=read();if(c<0)break;p[i]=c;}return i;}};
struct File:Stream {std::string bytes;size_t pos=0;File(std::string b):bytes(b){}operator bool()const{return true;}size_t size(){return bytes.size();}size_t position(){return pos;}bool seek(size_t p){pos=p;return p<=bytes.size();}int available()override{return bytes.size()-pos;}int read()override{return pos<bytes.size()?(unsigned char)bytes[pos++]:-1;}int peek()override{return pos<bytes.size()?(unsigned char)bytes[pos]:-1;}void flush()override{}size_t write(uint8_t c)override{if(pos==bytes.size())bytes.push_back(c);else bytes[pos]=c;pos++;return 1;}size_t write(const uint8_t* p,size_t n){for(size_t i=0;i<n;i++)write(p[i]);return n;}size_t print(const std::string& s){return write((const uint8_t*)s.data(),s.size());}size_t print(const char* s){return print(std::string(s));}size_t print(char c){return write(c);}void printf(const char* format,...){char b[256];va_list args;va_start(args,format);vsnprintf(b,sizeof b,format,args);va_end(args);print(b);}bool getWriteError(){return false;}};
using CatalogueVisitor=std::function<void(uint32_t,JsonObject)>;
void vTaskDelay(int){}

constexpr char HUB[]="https://thefirmwarehub.com/catalog.json";
using ScratchSpace=std::function<uint64_t()>;
String clip(String s,size_t n){if(s.size()>n)s.resize(n);return s;}
bool validRepo(const String& s){return std::regex_match(s,std::regex("[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+"));}
bool hashValid(const String& s){return std::regex_match(s,std::regex("[a-fA-F0-9]{64}"));}
String decode(String s){String out;for(size_t i=0;i<s.size();i++){if(s[i]=='%'&&i+2<s.size()){out+=(char)std::stoi(s.substr(i+1,2),nullptr,16);i+=2;}else out+=s[i];}return out;}
