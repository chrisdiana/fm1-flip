#!/usr/bin/env python3
"""Exercise the production bounded HTTP download sink, offline."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='fm1-download-') as name:
 p=Path(name)
 (p/'Stream.h').write_text('''#pragma once
#include <cstddef>
#include <cstdint>
class Stream { public: virtual ~Stream()=default; virtual int available()=0; virtual int read()=0; virtual int peek()=0; virtual void flush()=0; virtual size_t write(uint8_t)=0; virtual size_t write(const uint8_t*,size_t)=0; };
''')
 (p/'test.cpp').write_text('''#include "device/download_stream.h"
#include <cassert>
#include <vector>
#include <iostream>
int main(){
 uint64_t free=100;size_t hashed=0,progress=0;std::vector<uint8_t> bytes;
 auto sink=[&](size_t limit){return device::DownloadStream(limit,16,[&]{return free;},[&](const uint8_t* p,size_t n){bytes.insert(bytes.end(),p,p+n);free-=n;return n;},[&](const uint8_t*,size_t n){hashed+=n;},[&](size_t n){progress=n;});};
 uint8_t data[]={1,2,3,4};auto stream=sink(8);
 // Unknown-length response arrives in decoded chunks, without preallocation.
 assert(stream.write(data,4)==4);assert(stream.write(data,4)==4);
 assert(bytes.size()==8&&hashed==8&&progress==8&&stream.error.empty());
 assert(stream.write(data,1)==0&&stream.error=="Package exceeds 4 MB download limit");
 assert(stream.write(data,1)==0&&bytes.size()==8);
 auto low=sink(100);free=19;assert(low.write(data,4)==0);
 assert(low.error=="Not enough storage; keep 64 KB working space"&&bytes.size()==8);
 auto disk=device::DownloadStream(100,0,[]{return 100;},[](const uint8_t*,size_t){return 0;},[](const uint8_t*,size_t){assert(false);},[](size_t){assert(false);});
 assert(disk.write(data,4)==0&&disk.error=="Download storage write failed");
 std::vector<uint8_t> shortBytes;size_t shortHash=0;
 auto shortSink=device::DownloadStream(100,0,[]{return 100;},[&](const uint8_t* p,size_t n){n=std::min<size_t>(2,n);shortBytes.insert(shortBytes.end(),p,p+n);return n;},[&](const uint8_t*,size_t n){shortHash+=n;},[](size_t){});
 assert(shortSink.write(data,4)==4&&shortSink.error.empty()&&"Positive short storage writes must be completed, not rejected");
 assert(shortBytes==std::vector<uint8_t>(data,data+4)&&shortHash==4);
 auto cancelled=device::DownloadStream(100,0,[]{return 100;},[](const uint8_t*,size_t)->size_t{assert(false);return 0;},[](const uint8_t*,size_t){assert(false);},[](size_t){assert(false);},[]{return true;});
 assert(cancelled.write(data,4)==0&&cancelled.error=="Download cancelled");
 std::cout<<"Download: unknown-length chunks, size bound, working space, sticky failure and disk failure passed\\n";
}
''')
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(p),'-I'+str(root/'include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
s=(root/'src/device/app.cpp').read_text()
assert 'http.writeToStream(&sink)' in s and 'Missing/oversized length' not in s
local=(root/'src/device/local_app.inc').read_text()
assert '"Last operation", "Loaded on FM-1", "Home"' in local
assert 'case 19: setMainTab(2); return;' in s
assert 'path.isEmpty() ? 4 : 5' in local
assert 'case 4: downloadSelected(); break;' in s
assert 'case 5: installConfirm(); break;' in s
print('Decoded HTTP integration and requested menu dispatch/order passed')
