# FM-1 `.fwsc` package validation

The parser in `src/core/package.cpp` adapts the `.fwsc` container behavior in
[`konsumer/fm1buddy` `src/app/fm1.cpp`](https://github.com/konsumer/fm1buddy/blob/65b8760d1365e84e5bd799c7fa1d3e0d7c3b4fe2/src/app/fm1.cpp),
pinned at commit `65b8760d1365e84e5bd799c7fa1d3e0d7c3b4fe2`.
The exact source file SHA-256 is
`5af39e287027ac1b8186f3f29a7a025b78c39573a8b3a8a0ceaebcb03559dcf3`.
Its [licensing record](https://github.com/konsumer/fm1buddy/blob/65b8760d1365e84e5bd799c7fa1d3e0d7c3b4fe2/LICENSING.md)
credits foma's `web/fm1ota.js` and `web/fm1pkg.js`, Copyright (C) 2026
Leo Kuroshita (@kurogedelic), Hügelton Instruments. Upstream and this
port use GPL-3.0-only; the complete license is at the project root.

## Container rules implemented

- Input is bounded to 960 bytes through 16 MiB. The first twenty 48-byte
  interleave blocks each contain 47 logical bytes and one identity marker.
  Some devices use more identity blocks (up to 64); extra blocks count only
  when the following marker is the upstream unused marker (`0x7d`).
- Identity markers decode to a product string such as `FM-1_015` or
  `SMK-37 Elite_016`. `ota-` is accepted as the loader prefix. The target
  identity is checked before the package is accepted. FM-1 and SMK-37 class
  products are never interchangeable.
- The logical image starts with a 64-byte encrypted header followed by one to
  eight 80-byte encrypted table entries. The stream cipher and CRC-16/XMODEM
  checks match upstream: header, table, and data CRCs for type 0 and type 100
  entries are checked. All file ranges use overflow-safe bounds checks.
- File-table type 0 is treated as `flash.bin`, as upstream `Pkg::flash` does.
  Its file offset addresses bytes in the logical package; those bytes map to
  flash addresses starting at zero. The core streams requested bytes from the
  `Reader` and never buffers the full package or pads beyond the payload.

The CRCs detect truncation and accidental corruption; they are not signatures
and do not establish package provenance. A caller must keep the validated
reader stable between validation and subsequent reads (or independently bind
the reads to a cryptographic hash).

## Flash write boundary

`writableSector` permits only one aligned 4 KiB sector fully contained in
`[0x4000, 0xFF000)`. This excludes the boot range `[0, 0x4000)` and final
calibration/identity range `[0xFF000, 0x100000)`. It rejects unaligned writes,
partial sectors, and integer overflow. `readFlashImage` returns an error for missing bytes and never substitutes
 erased (`0xff`) data. `writePackage` preserves existing bytes after a partial
 final payload sector and leaves all other uncovered bytes untouched. It
 requires a complete 1 MiB verified backup and matching live flash CRC32 before
 erasing, writes changed writable sectors only, and reads each changed sector
 back. It checks both protected regions against the backup after the writes.

The host fixture in `test/test_core.cpp` is synthetic but encodes the documented
upstream format: identity/interleave markers, encrypted table/header, a type-0
flash payload, and matching CRC-16/XMODEM values. It is suitable for parser
regression tests, not evidence of hardware compatibility.

## Guarded backup restore

`restoreBackup` requires a desired backup and a fresh current verified backup,
each exactly 1 MiB, with independently recorded complete IEEE CRC32 values.
Both stored CRCs and the complete live target CRC are checked before writes.
Every desired byte in `[0, 0x4000)` and `[0xFF000, 0x100000)` must match the
current backup before the first erase. It restores changed writable sectors,
reads them back, then checks protected regions again. Errors stop immediately;
there is no restart operation in the core. Callers must keep both readers
immutable throughout the operation and retain both files after any failure.

## Real release evidence (2026-11-15)

These packages were downloaded directly from their makers' GitHub release
assets into `.cache/` (no Firmware Hub package or source mirrors were used):

| Maker release asset | SHA-256 | Parser result |
|---|---|---|
| [Felucca v1.8.2](https://github.com/hugelton/Felucca/releases/download/v1.8.2/felucca-1.8.2.fwsc) | `770ea7c756088d08ac59a193f1ae78126476c3c8251a86938c837ead5ab0048a` | `FM-1_918`, flash 602112 bytes, 20 blocks |
| [FOMAJAM SMK-37 Elite v0.1.12](https://github.com/konsumer/fomajam/releases/download/v0.1.12/fomajam-smk37-elite-0.1.12.fwsc) | `222bf8153e96ecfda822043342b3bd4d86a93eee633bdb9bf021a9ec00705913` | `SMK-37 Elite_961`, flash 643072 bytes, 20 blocks |

The Felucca hash also matched the already cached Firmware Hub metadata.
Both accepted their own target and rejected the other target; corrupting their
last byte failed validation. Reproduce with the host parser test binary and
these two local paths as arguments. The synthetic tests remain offline and
cover normal container behavior; the release assets are optional evidence,
not redistributed fixtures. Hardware compatibility still requires device tests.

### Optional retained backups

As of 2026-11-16, direct firmware installation uses the shared `writePackageWithoutBackup` path. It validates all used package data before erasing, captures CRC32 for boot/calibration sectors in RAM, compares live sectors, writes only changed application sectors and verifies readback and protected CRCs. It does not require a saved full-flash baseline. The explicit-backup writer and restoration API keep their verified-backup checks. Saving a complete 1 MiB recovery image is an optional Create backup action.
