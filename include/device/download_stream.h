#pragma once
#include <Stream.h>
#include <functional>
#include <algorithm>
#include <string>

namespace device {
// Receives decoded HTTP bytes, including bodies without Content-Length.
class DownloadStream : public Stream {
    size_t limit, reserve, count = 0;
    const char* limitError;
    std::function<uint64_t()> space;
    std::function<size_t(const uint8_t*, size_t)> store;
    std::function<void(const uint8_t*, size_t)> hash;
    std::function<void(size_t)> progress;
    std::function<bool()> cancelled;
public:
    std::string error;
    DownloadStream(size_t maximum, size_t workingSpace, std::function<uint64_t()> available,
                   std::function<size_t(const uint8_t*, size_t)> writeFile,
                   std::function<void(const uint8_t*, size_t)> updateHash,
                   std::function<void(size_t)> report, std::function<bool()> stop = {}, const char* tooLarge = "Package exceeds 4 MB download limit")
        : limit(maximum), reserve(workingSpace), limitError(tooLarge), space(available), store(writeFile), hash(updateHash), progress(report), cancelled(stop) {}
    size_t bytesWritten() const { return count; }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t* data, size_t size) override {
        if (cancelled && cancelled()) error = "Download cancelled";
        if (!error.empty()) return 0;
        if (size > limit - count) error = limitError;
        else if (space() < uint64_t(reserve) + size) error = "Not enough storage; keep 64 KB working space";
        if (!error.empty()) return 0;
        size_t written=0;
        while(written<size) {
            if(cancelled && cancelled()){error="Download cancelled";return written;}
            size_t portion=std::min<size_t>(512,size-written);
            size_t got=store(data+written,portion);
            if(!got || got>portion){error="Download storage write failed";return written;}
            hash(data+written,got);written+=got;count+=got;
        }
        progress(count);
        return size;
    }
};
}
