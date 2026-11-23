#include "crash_record.h"

#include <stdint.h>
#include <stdio.h>

#include <Arduino.h>
#include <esp32-hal.h>
#include <esp_attr.h>

namespace {
constexpr uint32_t MAGIC = 0xC7A5E001;
constexpr unsigned DEPTH = 16;
struct Record {
    uint32_t magic;
    char reason[72];
    uint32_t pc;
    int32_t core;
    uint32_t depth;
    bool corrupt, continues;
    uint32_t backtrace[DEPTH];
};
RTC_NOINIT_ATTR Record record;
RTC_NOINIT_ATTR uint32_t crumb_magic;
RTC_NOINIT_ATTR const char *crumb;
RTC_NOINIT_ATTR uint32_t crumb_ms;

// Called by the Arduino core's panic wrapper before the normal IDF handler:
// no allocation, no locks, no printf.
IRAM_ATTR void on_panic(arduino_panic_info_t *info, void *) {
    record.magic = 0;
    size_t i = 0;
    if (info->reason)
        for (; i + 1 < sizeof record.reason && info->reason[i]; i++) record.reason[i] = info->reason[i];
    record.reason[i] = 0;
    record.pc = (uint32_t)info->pc;
    record.core = info->core;
    record.corrupt = info->backtrace_corrupt;
    record.continues = info->backtrace_continues || info->backtrace_len > DEPTH;
    record.depth = info->backtrace_len < DEPTH ? info->backtrace_len : DEPTH;
    for (uint32_t k = 0; k < record.depth; k++) record.backtrace[k] = info->backtrace[k];
    record.magic = MAGIC;
}
}  // namespace

namespace crashrec {
void install() { set_arduino_panic_handler(on_panic, nullptr); }

void mark(const char *step) {
    crumb = step;
    crumb_ms = millis();
    crumb_magic = MAGIC;
}

bool take(char *out, size_t n) {
    if (!n) return false;
    out[0] = 0;
    size_t k = 0;
    if (crumb_magic == MAGIC) {
        crumb_magic = 0;
        k = snprintf(out, n, "Last step before restart: %s (at %lu ms)%s", crumb, (unsigned long)crumb_ms,
                     record.magic == MAGIC ? "\n" : "");
        if (k >= n) k = n - 1;
    }
    if (record.magic != MAGIC) return k > 0;
    out += k;
    n -= k;
    record.magic = 0;
    record.reason[sizeof record.reason - 1] = 0;
    if (record.depth > DEPTH) record.depth = DEPTH;
    k = snprintf(out, n, "Crash on core %ld: %s\nCrash PC 0x%08lx\nBacktrace%s:", (long)record.core,
                        record.reason, (unsigned long)record.pc, record.corrupt ? " (corrupt)" : "");
    for (uint32_t i = 0; i < record.depth && k < n; i++)
        k += snprintf(out + k, n - k, " 0x%08lx", (unsigned long)record.backtrace[i]);
    if (record.continues && k < n) snprintf(out + k, n - k, " ...");
    return true;
}
}  // namespace crashrec
