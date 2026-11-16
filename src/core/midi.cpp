// SPDX-License-Identifier: GPL-3.0-only
// FM-1 handshake constants/identity format after fm1buddy revision
// 65b8760d1365e84e5bd799c7fa1d3e0d7c3b4fe2. See docs/DEPENDENCIES.md.
#include "fm1/midi.hpp"
#include <cstring>
#include <limits>

namespace fm1 {
const uint8_t IdentityQuery[10] = {0xf0,0x00,0x32,0x45,0x00,0x00,0x00,0x40,0x7f,0xf7};
const uint8_t RomEntryRequest[6] = {0xf0,0x22,0x24,0x35,0x7d,0xf7};
size_t encodeUsbMidiSysEx(const uint8_t* wire,size_t length,uint8_t* output,size_t capacity) {
 if(!wire || !output || length<2 || length>MaxSysExBytes || wire[0]!=0xf0 || wire[length-1]!=0xf7) return 0;
 for(size_t i=1;i+1<length;i++) if(wire[i]&0x80) return 0;
 const size_t required=((length+2)/3)*4;
 if(capacity<required) return 0;
 size_t written=0;
 for(size_t offset=0;offset<length;offset+=3) {
  const size_t remaining=length-offset;
  const size_t take=remaining>3?3:remaining;
  output[written++]=remaining>3?4:static_cast<uint8_t>(4+take);
  for(size_t i=0;i<3;i++) output[written++]=i<take?wire[offset+i]:0;
 }
 return written;
}
void UsbMidiSysExDecoder::reset(){used_=0;active_=false;}
FeedResult UsbMidiSysExDecoder::feed(const uint8_t packet[4],uint8_t* output,size_t capacity,size_t& length) {
 length=0;
 if(!packet || !output) {reset();return FeedResult::Malformed;}
 if(packet[0]>>4) return FeedResult::Ignored;
 const uint8_t cin=packet[0]&15;
 if(cin!=4 && cin!=5 && cin!=6 && cin!=7) return FeedResult::Ignored;
 const size_t count=cin==4?3:cin-4;
 // CIN 5 also represents single-byte system common messages. Ignore those
 // rather than poisoning an unrelated in-progress SysEx message.
 if(cin==5 && packet[1]!=0xf7) return FeedResult::Ignored;
 for(size_t i=0;i<count;i++) {
  uint8_t byte=packet[i+1];
  if(!active_) {
   if(i!=0 || byte!=0xf0) {reset();return FeedResult::Malformed;}
   active_=true;
  } else if(byte&0x80) {
   if(byte!=0xf7 || cin==4 || i+1!=count) {reset();return FeedResult::Malformed;}
  }
  if(used_>=MaxSysExBytes) {reset();return FeedResult::Malformed;}
  bytes_[used_++]=byte;
 }
 if(cin==4) return FeedResult::Incomplete;
 if(used_<2 || bytes_[used_-1]!=0xf7 || used_>capacity) {reset();return FeedResult::Malformed;}
 length=used_;std::memcpy(output,bytes_,used_);reset();return FeedResult::Complete;
}
bool parseFirmwareIdentity(const uint8_t* wire,size_t length,FirmwareIdentity& identity) {
 identity={};
 if(!wire || length<2 || length>MaxSysExBytes || wire[0]!=0xf0 || wire[length-1]!=0xf7) return false;
 uint8_t decoded[34]{};size_t count=0;uint32_t accumulator=0;unsigned bits=0;
 for(size_t i=1;i+1<length;i++) {
  if(wire[i]&0x80) return false;
  accumulator|=uint32_t(wire[i])<<bits;bits+=7;
  while(bits>=8) {
   if(count>=sizeof decoded) return false;
   decoded[count++]=uint8_t(accumulator);accumulator>>=8;bits-=8;
  }
 }
 if(count!=sizeof decoded || decoded[0]!=0 || decoded[1]!=0x59 || decoded[2]!=0x11) return false;
 size_t used=0;
 for(size_t i=6;i<33 && decoded[i];i++) {
  if(decoded[i]<0x20 || decoded[i]>0x7e || used+1>=sizeof identity.product) return false;
  identity.product[used++]=char(decoded[i]);
 }
 identity.product[used]=0;
 const char* product=identity.product;
 if(!std::strncmp(product,"ota-",4)){identity.updateMode=true;product+=4;}
 if(std::strncmp(product,"FM-1_",5) || !product[5]) {identity={};return false;}
 uint64_t version=0;
 for(const char* p=product+5;*p;p++) {
  if(*p<'0'||*p>'9') {identity={};return false;}
  version=version*10+uint32_t(*p-'0');
  if(version>std::numeric_limits<uint32_t>::max()){identity={};return false;}
 }
 identity.version=uint32_t(version);return true;
}
}
