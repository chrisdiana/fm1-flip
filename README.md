# FM-1 Flip

A portable M-VAVE FM-1 firmware manager for **M5Stack StickS3, Cardputer ADV, and original Cardputer**. Browse firmware, download packages, install them on an FM-1, and optionally save backups without a computer. A local web interface is also available.

Development firmware: StickS3 uploads, basic UI operation and connection to an FM-1 running Patchwerk have been observed. FM-1 backup, install and restore flows, both Cardputer variants, and physical M5Launcher installation still need hardware validation. See [hardware validation](docs/HARDWARE-VALIDATION.md) for the cable/power checks.

## Install with M5Launcher

Get precompiled binaries from [GitHub Releases](https://github.com/chrisdiana/fm1-flip/releases).

Use the matching file in `build/releases/m5launcher/`:

| Board | M5Launcher binary |
| --- | --- |
| StickS3 | `fm1-flip-sticks3-m5launcher.bin` |
| Cardputer / Cardputer ADV | `fm1-flip-cardputer-m5launcher.bin` |

Copy the file to SD and choose **SD → file → Install**, or upload through Launcher's **WUI**. StickS3 uses a merged image so Launcher can create its internal data partition; initialize storage through **Home → Storage & export** on first use if it does not mount. Cardputer uses an app-only image and a FAT32 microSD card. These packages target current dynamic-partition Launcher builds and still need physical Launcher installation testing.

See [M5Launcher installation and packaging](docs/M5LAUNCHER.md) for exact setup, storage behavior and checksums. `python3 tools/package_launcher.py` creates the two files and `build/releases/fm1-flip-m5launcher.zip` from fresh builds. This packaging command never reads or flashes a device.

## Quick start

1. Install the build for your exact M5 board. Cardputer needs a FAT32 microSD card; StickS3 uses internal storage.
2. The device opens **Home**, titled **FM-1 Flip**. On a new StickS3 with unmounted storage, choose **Storage & export**, then physically confirm initialization. This erases internal saved files; storage is never formatted automatically.
3. Choose **Wi-Fi setup**, select your network, and enter its password. Choose **OK** in the StickS3 character picker, or press **Enter** on Cardputer, to join.
4. Choose **Discover** to browse the bundled 217-entry catalogue. Select a firmware to read its details. Select again for firmware options, then choose **Download package**. Downloads require Internet access.
5. Choose **Downloaded** on Home to browse packages saved on the M5. Select a firmware, select again for options, and choose **Install on FM-1**.
6. Connect and power the FM-1 using a verified data cable and power arrangement. Review the chosen firmware, then hold **B for two seconds** on StickS3 or **Enter for two seconds** on Cardputer to begin. Install connects and identifies the FM-1 automatically; a separate **Connect FM-1** step is optional.
7. Follow the progress screen. If normal USB is unavailable, the device asks you to switch FM-1 **off, then on**, keeping the cable attached, for its boot-key connection. Keep power and USB connected while writing. Inspect the FM-1 after it restarts.

**Installs do not create or require a backup.** If you want a retained recovery image, choose **Home → Create backup** before installing. Firmware switching does not migrate songs, sounds or settings between firmware families.

## StickS3 button map

| Input | Normal navigation | Text entry |
| --- | --- | --- |
| **Tap A** | Choose/open the highlighted item | Add the highlighted character or choose an edit action |
| **Tap B** | Next item; scroll down in a detail view | Next character |
| **Hold A** | Back one level | Cancel entry and return |
| **Hold B** | Previous item; keep holding to scroll up | Previous character; keep holding to move faster |
| **A+B together** | Go directly Home | Cancel entry and go directly Home |

A hold starts after **0.5 seconds**. Held-B scrolling repeats every **300 ms in lists** and **150 ms in text entry**. Release both buttons after the Home chord before choosing another item.

| Input on a confirmation screen | Action |
| --- | --- |
| **Hold B for 2 seconds** | Confirm the reviewed install, restore, storage initialization or deletion |
| **Hold A** | Cancel the confirmation |
| **A+B** | Cancel the confirmation and go Home |

During a transport operation, **hold A** requests cancellation while the screen says cancellation is available. Once flash writing begins, cancellation is disabled. **A+B** selects Home as the return view; progress remains visible until the operation finishes.

### How Back works

**Home → Downloaded → Firmware details → Firmware options**

Each **hold A** goes back one level: options → details → the same list selection → Home. Holding A on Home stays on Home. **A+B** jumps directly Home. The same hierarchy applies to Discover; backup actions return to the backup list, and history returns to the FM-1 identity view.

### StickS3 text entry

Use **B** to advance and **hold B** to move backward through characters. Character groups follow each other automatically. Use **A** to insert a character.

The final group contains these actions:

| Action | Effect |
| --- | --- |
| **DEL** | Delete the last character |
| **CLR** | Clear the entire entry |
| **OK** | Save the search, continue SSID entry, or join Wi-Fi after password entry |
| **ESC** | Cancel entry |

Search skips uppercase letters and matches **case-insensitively**. Wi-Fi names and passwords retain uppercase, lowercase, digits, spaces and symbols; password characters are masked. For a hidden network, choose **Enter SSID manually**. Press **OK** with an empty password when the network permits it.

## Cardputer keyboard map

These controls apply to Cardputer ADV and original Cardputer.

| Key | Action |
| --- | --- |
| **`;`** | Previous item / scroll up |
| **`.`** | Next item / scroll down |
| **Enter** | Choose/open the highlighted item |
| **Fn + backtick** | Back one level; cancel text entry or a pending confirmation |
| **Fn + F** | Go Home during normal navigation |
| **Tab** | Cycle Discover → Downloaded → Loaded on FM-1 while browsing |
| **Type printable text** | Search the firmware list while browsing |
| **Backspace/Delete** | Remove the last search character |
| **Hold Enter for 2 seconds** | Confirm a reviewed install, restore, initialization or deletion |

**Backtick** is the grave-accent key. During a transport operation, **Fn + backtick** requests cancellation only before the write gate closes. During text entry, type the value directly, use Backspace/Delete to edit, **Enter** to submit, and **Fn + backtick** to cancel. Use the cancel key before Home if you are in the editor or reviewing a confirmation.

Additional shortcuts in firmware browsing/detail views:

| Key | Action |
| --- | --- |
| **`[`** | Toggle the selected firmware's favorite status |
| **`]`** | Connect FM-1, or create a backup if already connected in ROM mode |
| **`{`** | Download the selected package |
| **`}`** | Refresh the catalogue |
| **Backslash** | Review installation of the selected saved package from its detail view |

## Home and everyday use

| Home item | Purpose |
| --- | --- |
| **Discover** | Browse cached Firmware Hub listings, sorted by popularity by default |
| **Downloaded** | Browse packages actually saved on the M5, including retained older releases |
| **Loaded on FM-1** | See live or last-observed firmware identity; choose to open confirmed installation history |
| **Search** | Enter a search across names, makers, descriptions and tags |
| **Favorite / unfavorite** | Pin or unpin the selected firmware |
| **Download package** | Save the selected maker package for offline installation |
| **Install on FM-1** | Review installation of the selected saved package; connects automatically after confirmation |
| **Connect FM-1** | Connect and identify FM-1 in its ROM/loader mode |
| **Create backup** | Connect automatically if needed and save a verified full-flash backup |
| **Backups** | Select a backup for restore, export information or permitted deletion |
| **Wi-Fi setup** | Scan networks or enter a hidden SSID and password |
| **Refresh catalogue** | Refresh Hub metadata when Internet access and the daily cache policy permit |
| **Category / Release channel / Chip support** | Cycle catalogue filters |
| **Sort** | Cycle popularity, name and newest release |
| **Favorites only** | Toggle favorite filtering |
| **Storage & export** | See free space, storage setup and web export instructions |
| **Last operation** | Read the last transport/import result or error |
| **Home** | Return to Home |

Search and filters can remain active when switching between Discover and Downloaded. Clear Search, set filters to **all**, and turn **Favorites only** off if an expected entry is missing. Popul-arity uses Hub install counts; it is not a hardware compatibility guarantee.

### Download and install

A catalogue listing is not a downloaded package. Open its firmware options and choose **Download package** first. The app downloads from the maker's release and checks package structure and supplied hashes. External-installer-only listings cannot be downloaded by the app. Older downloaded releases remain separately selectable after a catalogue update.

Downloaded packages can be installed offline. Installation still requires a recognized FM-1, valid package/hash/chip metadata, and physical confirmation. Unknown compatibility is refused. The writer preserves boot/calibration ranges, writes changed application sectors and checks readback. A reported MIDI boot identity is a further check; inspect actual FM-1 operation after restart.

### Optional backups and restore

Choose **Home → Create backup** to save a complete **1 MiB** FM-1 image, checked against device CRC and reread from storage. This connects automatically when needed. Existing backups are not removed during installation.

To restore, first use **Connect FM-1** to identify the target, then choose **Backups → selected backup → Restore on FM-1** and physically confirm. Restoration creates a fresh rollback backup and needs additional free space. It preserves boot/calibration sectors and refuses an image with mismatched protected bytes or device identity. It is not a bootloader replacement tool.

Backup actions also provide export instructions and deletion of eligible older backups. The original baseline, latest verified backup and active/armed backup remain protected from deletion. Use the web file view or power down before removing a Cardputer SD card to copy files.

## Optional web interface

On the M5, open **Storage & export** for access details. Connect your phone/computer to the **FM1-Flip** access point using its displayed password, then open **http://192.168.4.1/**. The access point remains available when the M5 also joins your home Wi-Fi.

| Web tab | Contents |
| --- | --- |
| **Discover** | Searchable gallery, filters, popularity sorting and maker artwork where supplied |
| **On your switcher** | Packages saved on the M5, separate from the catalogue |
| **FM-1 device** | Live versus cached identity and confirmed installation history |
| **Files & backups** | File/backup listing, exports and restore selection |
| **Settings** | Wi-Fi setup, imports and diagnostics |

The browser can queue downloads and imports; installation uses handheld controls and physical confirmation. Web restore selection also requires confirmation on the handheld. Artwork loads in the browser and may be unavailable without Internet access. Files and cached metadata remain usable offline.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| No firmware in Downloaded | Download a supported package first; clear search and filters |
| Download or refresh fails | Connect station Wi-Fi, allow clock synchronization, check storage and read **Last operation** |
| Catalogue will not refresh again | Hub reads are throttled to once per 24 hours; cached browsing still works |
| No USB attachment | Check the data cable and FM-1 power arrangement; follow the displayed off/on fallback if requested |
| USB detected; enumeration timed out | The root port detects a peer but has no usable USB device yet. Read Last operation and export the USB session log; record the FM-1 firmware and cable/power setup. No raw boot-key pulses are sent to this attached peer |
| FM-1 screen goes dark during connection | It may be in ROM mode. Read the M5's connection stage/error; darkness alone does not establish loss of power |
| Install is refused | Read **Last operation** for package, hash, chip compatibility or hardware identification details |
| Storage is full | Export/remove unwanted packages or eligible backups; installs do not automatically save backups |
| StickS3 is missing from the computer's serial ports | Connect a data-capable USB cable; hold the power button until the green LED blinks, then release for download mode |

Export `/fm1/logs/session-N.txt` through **Files & backups** when investigating a connection failure. Include the exact error, M5 model, FM-1 firmware, cable/adapter and power arrangement.

## Build and checks

Install PlatformIO, then run from this directory. The first build downloads ESP-IDF and rebuilds the SDK through pioarduino HybridCompile so composite USB descriptors larger than the stock 256-byte limit can enumerate; allow additional build time and disk space:

```sh
pio run -e cardputer -e sticks3
python3 tools/check.py
# Optional real-browser frontend check with a local fixture API:
python3 tools/check_web.py
python3 tools/package_release.py sticks3 cardputer
```

On this host, the PlatformIO executable is `~/.platformio/penv/bin/pio` if `pio` is not on PATH.

Pinned dependencies and partition layouts are in `platformio.ini` and `partitions/`. Factory binaries and SHA-256 manifest are generated in `build/releases/`; factory images are merged for offset `0x0`. App-only `.pio/build/<board>/firmware.bin` images belong at the application partition offset `0x10000`. No command above uploads to hardware. USB download-mode and M5Launcher installation must be checked on the actual board before use.

The StickS3 uses a single 3 MiB application partition and a 4.9375 MiB LittleFS partition, with no OTA app slot. Packages share that filesystem with catalogue data and backups. Cardputer stores these files on a FAT32 microSD card. Imports leave 64 KiB of working space. Firmware installs do not automatically create or require a backup. Choose **Create backup** on Home to retain a verified 1 MiB image when wanted; existing backups remain stored and consume actual space. Backup restoration still creates its fresh rollback backup. Full storage causes operations to refuse or fail rather than deleting saved backups.

## Preparing a catalogue on a computer

The device includes an offline catalogue snapshot. To prepare an import from the documented feed/cache:

```sh
python3 tools/sync_catalogue.py --cache .cache/catalogue-cache.json --output .cache/catalogue.json
```

Import the JSON through the web page or copy it to `/fm1/catalogue.json` on SD. The helper exports 96 entries per page; pass `--page N` for another page. Device refresh supports the full feed up to its bounded limit. Firmware package bytes come from makers' releases.

## Project records

- [Implementation status and evidence](docs/IMPLEMENTATION-STATUS.md)
- [Hardware validation procedure](docs/HARDWARE-VALIDATION.md)
- [Package format and real release checks](docs/package-format.md)
- [Firmware Hub access, attribution and cache policy](docs/firmware-hub.md)
- [Dependencies and upstream provenance](docs/DEPENDENCIES.md)

Project code is GPL-3.0-only; imported MIT and other third-party components retain their licenses. See `LICENSE` and `third_party/cardputer-synth-rescue/THIRD-PARTY.md`. Firmware list: [The Firmware Hub](https://thefirmwarehub.com/), CC BY 4.0 compilation; maker descriptions/images remain their makers' work. The local catalogue normalizes and bounds metadata for embedded use, and links to the maker sources.
