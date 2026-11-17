# Hardware validation still required

StickS3 has been flashed and its display observed by the user. The user reported a dark FM-1 screen and failed connection with the earlier transport. The latest connection/install changes have host regression coverage but still need actual USB/power/backup/install validation. Successful compilation alone is not hardware validation.

## Before connecting

1. Record M5 model/revision, FM-1 flash revision, installed firmware, cable, and power source.
2. Check official schematics against the actual revision. Measure USB-C VBUS on battery and external power and establish a safe connection without conflicting sources. Do not assume ESP32-S3 host support supplies VBUS.
3. Keep a proven computer recovery route and independent copies of any songs/settings and a full FM-1 backup. Confirm how to put each M5 into download mode after the application takes over USB.
4. Obtain explicit authorization before running any device upload or FM-1 flash operation. These instructions are not authorization.

## Read-only session

- Boot M5 without FM-1. Verify display, buttons/keyboard, storage, filesystem initialization, and AP access.
- Verify station credentials, TLS clock synchronization, catalogue refresh throttle/attribution, cached offline search, and source-release resolution.
- Verify free-space accounting and interrupted imports, SD removal, and last-valid-cache recovery.
- With the checked power arrangement, leave FM-1 on and choose Connect or a physically confirmed Install. Normal USB enumeration is tried first. If absent, follow the displayed OFF/ON boot-key fallback with the cable attached. Capture descriptors, chip key and flash ID. ROM entry uploads a helper into RAM; it does not intentionally write flash.
- Back up and export the full 1 MiB flash. Verify independently against another tool and repeat without intervening target state changes. Record CRC and unique ID where available.

## Authorized write session

- Use a known-compatible firmware with matching maker release hash and catalogue chip metadata. Check that an unknown/old-only destination is refused on incompatible hardware.
- Verify Install creates no automatic backup file. Optionally choose Create backup first and verify its retained CRC-checked image. Record changed-sector count and readback results.
- Inspect protected boot/calibration ranges independently before and after. Verify sound, controls, and startup on the FM-1 after the requested reboot. A completed transfer is not a confirmed boot.
- Return to the baseline through a supported route and test at least three packages for each declared board/flash revision. Keep source-to-target route evidence.
- Test faults with fake transports first. Deliberate flash power-loss tests require a recoverable test device and independent recovery proof.

## Validation record

| Board | Compile | UI/storage | USB/power | Backup | Install/boot |
| --- | --- | --- | --- | --- | --- |
| Cardputer ADV | Passed | Pending | Pending | Pending | Pending |
| StickS3 | Passed | Pending | Pending | Pending | Pending |
| Original Cardputer | Passed | Pending | Pending | Pending | Pending |

## Connection diagnostics and Home navigation update

- Install now validates metadata before automatically connecting; unknown compatibility or missing hardware must prevent writing; no retained backup is required for Install. Confirm this with an actual known-compatible saved package.
- MIDI-to-ROM and post-install handoffs keep the USB host/root port running, release the old interface after its removal event, and claim the replacement. No firmware restart or ROM request is considered proof of a working USB handoff.
- Export `/fm1/logs/session-N.txt` through the optional web Files view after a failure. Logs retain bounded USB port/PHY messages and VID/PID; distinguish no device, unsupported identity, missing ROM reconnect, RAM loader failure and flash identification failure.
- The FM-1 screen may be dark in ROM mode. Determine actual loss of power separately; record the cable/adapter, FM-1 firmware, power sources and exact last error. Do not infer a power shutdown from the screen alone.
- The StickS3 schematic shows USB-C CC pull-downs and a VUSB input path. Host controller support does not establish USB-C power sourcing. Its `setExtOutput` controls Grove/Hat power; this update does not change it or claim USB VBUS output. References: [official schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150_Stick_S3_PRJ_V0.6_20251111_2025_11_17_16_10_24.pdf), [official power notes](https://docs.m5stack.com/en/core/StickS3). Check actual board revision and measured VBUS for the chosen arrangement.
- Navigation checks: boot into FM-1 Flip Home; Downloaded -> firmware details -> hold A returns to the same list item -> hold A returns Home. A+B jumps Home from lists/editors/reviews, cancelling any armed confirmation. During active transfer the progress/critical operation display takes precedence.

## Optional backup and input changes (2026-11-16)

Install no longer calls the backup workflow or reserves space for a full image. Its direct writer validates the package, preflights replacement-sector reads, records five boot/calibration sector CRCs in RAM, verifies changed-sector readback and rechecks protected CRCs. Create backup is a separate explicit Home action and auto-connects when needed. Restore remains a distinct operation with its existing rollback-backup requirements. A direct install has no automatically retained rollback image. Existing backup files are not removed.

Hold B repeats every 300 ms on lists and 150 ms in text editors after the initial 500 ms hold. Search entry skips uppercase and normalizes keyboard input to lowercase; matching remains case-insensitive. SSID/password entry retains exact printable ASCII.

## 2026-11-16 connection confirmation

The user confirmed successful connection between StickS3 and an FM-1 running Patchwerk over a direct USB-C cable after the test build with the rebuilt 1024-byte enumeration buffer. The upload was app-only and its data hash verified. This confirms that connection scenario; it does not establish FM-1 installation/backup/restore, Cardputer/ADV operation or physical M5Launcher installation. No FM-1 flash write was performed by the development agent.
