# Implementation status

Development milestone: software implemented and compiled on 2026-11-15. StickS3 has been uploaded and the previous display was observed running by the user. Electrical tests, USB transfers and FM-1 flashing remain unverified. The complete plan's hardware acceptance criteria remain open.

## Implemented

- Three pinned PlatformIO targets: Cardputer ADV, original Cardputer, and StickS3; shared package, USB, backup and flash engine.
- Bounded streaming `.fwsc` parsing with identity, range and checksum checks. Model mismatch and protected sectors are rejected.
- ROM boot-key connection and RAM loader, normal MIDI identity queries and ROM-entry requests, cooperative USB teardown/reconnection, FM-1 model/flash identification, complete 1 MiB backups checked against device CRC and reread from storage. Post-install MIDI identity must match the selected package before boot is reported confirmed; unsupported or missing replies remain unconfirmed.
- Installation binds the package SHA-256 to catalogue/retained release metadata and chip support, optionally retains a backup through an explicit Create backup action, checks protected regions in RAM for direct installs, writes only changed application sectors, reads them back, and checks protected regions. Hash is checked again around installation. Unknown chip compatibility is refused.
- Backup restoration with source device metadata, fresh rollback backup, full CRC preflight, protected-region equality, changed-sector writes and readback. Original baseline and latest/active verified backups cannot be deleted through file management; intermediate backups can be explicitly exported and deleted.
- An immutable confirmation snapshot binds physical confirmation to the reviewed package/backup path, blocks concurrent file mutation and re-arming, and resets hold timing for each new action. Atomic cancellation gate before flash writes; status/error logs, storage reservations, heap and worker-stack diagnostics. No automatic device flashing at startup or through a gallery/web selection.
- Full Firmware Hub catalogue normalization and offset indexing up to 512 entries; incremental device search, 16-entry web pages, favorites, saved views, details, category/channel/chip-declaration filters, popularity/name/newest sorting, keyboard/button controls, and a StickS3 character picker. Name sorting uses a fixed 24-character lowercase key with stable source-order ties to bound memory and avoid filesystem access in the comparator.
- Daily throttled HTTPS catalogue refresh, last-valid-cache preservation, committed ETag validators, maker-release asset resolution, bounded Python-pattern-to-POSIX matching, controlled GitHub/CDN redirects, hash-checked imports, and release-specific filenames. Old downloaded releases remain separately selectable.
- Local Wi-Fi access point and station setup, gallery/file management, streaming exports, explicit first-use StickS3 filesystem initialization, and optional CA trust override. Certificate validation is always enabled.
- GPL-3.0-only project license, pinned upstream provenance and retained third-party notices. No maker firmware package is linked into the application.

## Verification evidence

`python3 tools/check.py` passes: 14 Python catalogue tests, native C++ package tests, native C++ writer/restore fault tests, confirmation snapshot replacement/cancellation tests, pure USB MIDI framing/identity tests, gallery filter/sort tests, and the production device catalogue parser through desktop adapters. Core tests also passed AddressSanitizer/UBSan in the core agent. Web JavaScript syntax passed `node --check`. `python3 tools/check_web.py` passed in headless Chrome against a local fixture API: pagination, description search, category/channel/date sorting, safe metadata rendering, details, download queueing, and restore selection requiring physical confirmation. This browser test does not exercise the device HTTP server, real downloads, or flashing.

The actual documented Hub feed was fetched once with an identifiable User-Agent and retained at `.cache/catalogue-cache.json`. It contains 217 entries. `python3 tools/check_device_catalogue.py --cache .cache/catalogue-cache.json` verified all 217 records, 136 supplied filename patterns and 21 external-only entries, plus offsets, schema rejection, duplicate/trailing-data rejection, bounds and backup reserves. This command uses cached bytes and makes no network request. Maker release asset URLs were resolved independently for Felucca, Jangada and Ghoulbox. A separate Hortator lookup failed exact release validation and remained a visible error rather than using an unverified URL.

Real Felucca and FOMAJAM SMK-37 Elite maker packages were downloaded from GitHub releases and checked by the parser. Wrong-target and corruption checks passed; the Felucca hash matched the cached catalogue. See package-format.md for exact asset URLs and hashes. These tests prove parsing, not hardware compatibility.

All three final board builds succeeded, followed by an independent PlatformIO size check. Factory binaries are in `build/releases/`, along with SHA-256/source fingerprints and `hardwareValidated: false` in manifest.json. Packaging rejects source newer than the corresponding binary.

| Target | App `.bin` bytes | Static DRAM data + BSS bytes | Hardware runtime |
| --- | ---: | ---: | --- |
| Cardputer ADV | 1,942,944 | See build size report | Updated Home/USB flow pending runtime validation |
| Original Cardputer | 1,942,912 | See build size report | Updated Home/USB flow pending runtime validation |
| StickS3 | 1,984,336 | See build size report | Updated Home/USB flow pending runtime validation |

Static data/BSS excludes runtime allocations, task stacks, Wi-Fi/TLS heaps and measured peak usage. Runtime heap/latency remains unmeasured; the application exposes diagnostics for bench testing.

## Plan gates still open

| Plan stage | Evidence available | Remaining acceptance evidence |
| --- | --- | --- |
| 1: feasibility | Pinned upstream transport, official board notes, all builds | Actual revision/power measurements and repeated USB enumeration on each board |
| 2: backups | Implemented CRC/readback workflow; native failure tests | Independent equivalence of real device backups and storage behavior |
| 3: install | Shared engine and fault tests; real package parsing | Authorized install, verification, baseline return and confirmed boot |
| 4: offline UI | Compiled device interfaces and saved-release storage | Physical controls, screen layout, offline install of three tested packages |
| 5: gallery/network | Live feed and asset-pattern tests; compiled TLS/web integration | On-device Wi-Fi/TLS downloads, HTTP interactions, memory/latency and interruption tests |
| 6: recovery/release | Restore tests, factory binaries, hashes and docs | Actual recovery/restore, M5Launcher checks, board/flash revision compatibility matrix |

## Limits and follow-up work

The write route uses ROM entry rather than firmware-specific USB-MIDI OTA updating. Normal MIDI identity discovery, a ROM-entry request and post-boot identity confirmation are implemented, with the boot-key/power-cycle route retained for firmwares that do not answer. Cooperative host cleanup permits reconnect attempts; cleanup failure requires an M5 restart. These paths are compiled and the pure MIDI protocol is tested, but actual USB lifecycle and firmware responses require hardware validation. Restoring application sectors is not a bootloader replacement or proof of complete brick recovery.

Web gallery artwork loads directly in the browser where supplied by the Hub; image caching on the M5 is not implemented. Firmware-specific settings migration and universal source-to-target compatibility are not provided. The Hub's chip declaration is not a tested transition matrix. Name-prefix sorting uses source-order ties; the current cached feed has no 24-character prefix collisions.

The previous StickS3 upload is recorded in FLASH-LOG.md. At the UI update attempt the available serial port identified as Mote Device; no update data was written. Reconnect and identify StickS3 before retrying. Next step is the read-only connection and backup procedure in HARDWARE-VALIDATION.md, starting on Cardputer ADV, then StickS3/original Cardputer. Device writes require explicit authorization for the identified hardware and operation.

## Interface update

Buffered, changed-only device rendering replaces periodic screen clearing. A production-renderer regression reproduces the former three idle clears and now observes zero visible full-screen clears and one idle presentation. Full printable-ASCII entry, password masking and picker navigation pass host tests. On-device setup, search, filters, download, backup and physically confirmed install/restore are implemented.

A bundled dated 217-entry catalogue supports immediate offline browsing. Existing caches receive missing artwork/popularity presentation metadata only for exact ID/hash matches. Default popularity uses Hub install counts; unknown values follow known counts. Separate web library and FM-1 views distinguish stored packages, live/cached identity and persisted confirmed installation history. Browser fixture tests cover these distinctions, artwork, filtering, paging and physical confirmation gating. Actual device screen stability and USB workflows still require runtime observation.

## Automatic install and Home navigation update

Install validates the saved package and compatibility metadata, connects/identifies FM-1 automatically when needed, then checks protected regions and replacement sectors before committing writes. Retained backups are optional through Home > Create backup. Direct ROM devices are accepted; running MIDI firmware is queried before the ROM request; boot-key pulses are a fallback after USB absence. MIDI/ROM/normal-firmware handoffs retain the host rather than cycling its root port. USB trace lines and VID/PID remain in the bounded session log.

`python3 tools/check_transport_flow.py` runs production connection and complete install orchestration against simulated USB/filesystem/parser/writer boundaries, plus production handoff/event callback tests. It covers running/ROM/cold/missing/unknown peers, identity and handoff failures, compatibility refusal, absence of a backup requirement, cancellation, already-connected installation and replacement-address races. These tests do not establish actual electrical behavior. Upstream `fm1buddy/src/app/uboot.cpp` uses a persistent host for the same ROM request; the earlier missing-calibration hypothesis was not supported for this MIDI route. Cold boot still calibrates as before.

Home is the default menu, titled FM-1 Flip. Hold A unwinds details to the retained list selection and then Home; A+B jumps Home and clears armed confirmations. Production navigation regression checks also cover editors, backup actions, history and contextual firmware options. Hardware confirmation of these changes remains pending.

## Optional backups and input tuning (2026-11-16)

Default installation uses `writePackageWithoutBackup`; no full backup file or full-image storage reserve is required. The shared sector writer retains package validation, protected address bounds, complete replacement reads before erase, changed-sector readback, and protected-sector CRC snapshots in RAM. Its explicit-backup variant and restore workflow retain their existing checks. Manual Create backup auto-connects; saved backups remain untouched. Working storage reserve is 64 KiB.

Native writer tests cover successful/idempotent direct installation, protected/preflight read failure before erase, erase/program/readback faults and protected-region corruption. Production install tests assert no backup call and successful installation even when backup storage is simulated unavailable. Input tests check 300 ms list and 150 ms editor repeats, lowercase-only search traversal and keyboard normalization, with full exact-case printable ASCII still available for network credentials. Hardware operation remains unverified for these changes.

## Downloaded list latency fix (2026-11-16)

The handheld previously reopened and parsed every catalogue entry in batches of eight when switching to Downloaded, then checked each package path. A production search-scheduler harness with 217 catalogue entries and two saved files reproduced 217 metadata reads over 28 search ticks. Saved package membership is now cached in the offset index at library load and invalidated by the existing import/delete/catalogue reload path. Default Discover/Downloaded list construction uses cached membership and sort keys only: zero metadata reads in one scheduler tick in the same fixture. Drawing still reads the visible row metadata; this is not a claim of zero total display I/O or measured hardware latency.

Search/filter operations skip unsaved entries before reading metadata. Stored paths, rather than a short metadata fingerprint, deduplicate catalogue-linked files; old/local-only releases remain indexed. `tools/check_local_search.py` tests the actual scheduler fast paths, saved-only filtered reads and clearing saved membership. Actual device timing remains to be observed.

## M5Launcher release packaging (2026-11-16)

`tools/package_launcher.py` produces exact-board images and a ZIP with installation guide, manifest and checksums. Cardputer and Cardputer ADV use app-only images with SD storage. StickS3 uses the clean merged build with `spiffs` data metadata and no filesystem dump; current Launcher allocates remaining free storage and first-use LittleFS initialization remains explicit. Installer reference revision: `6b432e4e185ceaebad14fb902d750d699b380bbd`.

All three boards rebuilt with compiler file-prefix mapping to remove local home-directory paths from firmware diagnostics. Packaging verified ESP32-S3 app segments/checksum/SHA-256, partition-table MD5, exact app bytes, erased NVS and no filesystem payload. Release hashes, binary path scans and ZIP integrity passed. Eight synthetic negative/positive format tests and the complete host suite pass. Physical M5Launcher installation/boot/data allocation remains unverified. See M5LAUNCHER.md.

## Unified Cardputer release (2026-11-16)

Original Cardputer and Cardputer ADV now share the `cardputer` build and release binary. Pinned M5GFX/M5Unified auto-detection runs before the generic Cardputer fallback; M5Cardputer 1.1.1 selects IOMatrix or TCA8418 keyboard input from `M5.getBoard()`. Both variants share the SD pin configuration and display size used here. `cardputer-adv` remains a backwards-compatible PlatformIO alias; packaging always emits only canonical StickS3 and Cardputer targets, removing obsolete generated ADV-only artifacts. The Launcher manifest explicitly lists both Cardputer variants as supported by the shared image. Both physical variants still require hardware validation.
