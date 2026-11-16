#include "fm1/midi.hpp"
#include <cassert>
#include <cstring>
#include <vector>

std::vector<uint8_t> identityReply(const char* product) {
 uint8_t body[34]{};body[1]=0x59;body[2]=0x11;body[3]=28;
 std::memcpy(body+6,product,std::strlen(product));
 std::vector<uint8_t> wire{0xf0};uint32_t accumulator=0;unsigned bits=0;
 for(uint8_t byte:body) {
  accumulator|=uint32_t(byte)<<bits;bits+=8;
  while(bits>=7){wire.push_back(accumulator&0x7f);accumulator>>=7;bits-=7;}
 }
 if(bits)wire.push_back(accumulator&0x7f);
 wire.push_back(0xf7);return wire;
}
void roundTrip(const std::vector<uint8_t>& wire) {
 uint8_t encoded[700],decoded[fm1::MaxSysExBytes];
 size_t bytes=fm1::encodeUsbMidiSysEx(wire.data(),wire.size(),encoded,sizeof encoded);
 assert(bytes!=0 && bytes%4==0);
 fm1::UsbMidiSysExDecoder decoder;size_t length=0;
 for(size_t offset=0;offset<bytes;offset+=4) {
  auto result=decoder.feed(encoded+offset,decoded,sizeof decoded,length);
  assert(result==(offset+4==bytes?fm1::FeedResult::Complete:fm1::FeedResult::Incomplete));
 }
 assert(length==wire.size() && !std::memcmp(decoded,wire.data(),length));
}
int main() {
 roundTrip({0xf0,0xf7});roundTrip({0xf0,1,0xf7});roundTrip({0xf0,1,2,0xf7});
 roundTrip(std::vector<uint8_t>(fm1::IdentityQuery,fm1::IdentityQuery+10));
 auto wire=identityReply("FM-1_918");roundTrip(wire);
 fm1::FirmwareIdentity identity;
 assert(fm1::parseFirmwareIdentity(wire.data(),wire.size(),identity));
 assert(!std::strcmp(identity.product,"FM-1_918") && identity.version==918 && !identity.updateMode);
 auto ota=identityReply("ota-FM-1_015");
 assert(fm1::parseFirmwareIdentity(ota.data(),ota.size(),identity) && identity.updateMode && identity.version==15);
 for(const char* other: {"SMK-37 Elite_961","FM-1_bad","FM-1_4294967296","OTHER_015"}) {
  auto wrong=identityReply(other);assert(!fm1::parseFirmwareIdentity(wrong.data(),wrong.size(),identity));
 }
 auto corrupt=wire;corrupt[2]=0xff;assert(!fm1::parseFirmwareIdentity(corrupt.data(),corrupt.size(),identity));
 corrupt=wire;corrupt[0]=0;assert(!fm1::parseFirmwareIdentity(corrupt.data(),corrupt.size(),identity));
 assert(!fm1::parseFirmwareIdentity(wire.data(),wire.size()-1,identity));
 uint8_t out[700];assert(!fm1::encodeUsbMidiSysEx(wire.data(),wire.size(),out,4));
 const uint8_t invalid[]={0xf0,0x80,0xf7};assert(!fm1::encodeUsbMidiSysEx(invalid,3,out,sizeof out));
 std::vector<uint8_t> max(fm1::MaxSysExBytes,0x7f);max.front()=0xf0;max.back()=0xf7;roundTrip(max);
 max.push_back(0xf7);assert(!fm1::encodeUsbMidiSysEx(max.data(),max.size(),out,sizeof out));
 fm1::UsbMidiSysExDecoder decoder;size_t n=0;
 uint8_t first[]={4,0xf0,1,2},otherCable[]={0x14,0xf0,3,4},realtime[]={15,0xf8,0,0},last[]={5,0xf7,0,0};
 assert(decoder.feed(first,out,sizeof out,n)==fm1::FeedResult::Incomplete);
 assert(decoder.feed(otherCable,out,sizeof out,n)==fm1::FeedResult::Ignored);
 assert(decoder.feed(realtime,out,sizeof out,n)==fm1::FeedResult::Ignored);
 assert(decoder.feed(last,out,sizeof out,n)==fm1::FeedResult::Complete && n==4 && out[0]==0xf0 && out[3]==0xf7);
 assert(decoder.feed(last,out,sizeof out,n)==fm1::FeedResult::Malformed);
 uint8_t badContinuation[]={4,1,2,3};assert(decoder.feed(badContinuation,out,sizeof out,n)==fm1::FeedResult::Malformed);
 decoder.feed(first,out,sizeof out,n);assert(decoder.feed(last,out,2,n)==fm1::FeedResult::Malformed);
 decoder.feed(first,out,sizeof out,n);uint8_t noEnd[]={6,1,2,0};assert(decoder.feed(noEnd,out,sizeof out,n)==fm1::FeedResult::Malformed);
 return 0;
}
