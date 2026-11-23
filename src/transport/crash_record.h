#pragma once
#include <stddef.h>

// Keeps the last crash (reason, PC and a short backtrace) in RTC memory across
// the panic reboot, via the Arduino core's panic hook. The normal handler prints
// to a console that, on StickS3, is not reachable while hosting the FM-1.
namespace crashrec {
// Registers the panic hook; call once at startup.
void install();
// Breadcrumb: the last step reached, kept across a crash. `step` must be a
// string literal (only its address is stored).
void mark(const char *step);
// Formats the saved crash as text lines separated by '\n' (empty when there is
// none) and clears it, so each crash is reported once.
bool take(char *out, size_t n);
}  // namespace crashrec
