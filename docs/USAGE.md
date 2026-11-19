# Using FM-1 Flip

[Project overview](../README.md) · [Cardputer / ADV guide](CARDPUTER.md) · [StickS3 guide](STICKS3.md)

These features apply to both boards. Start with your board guide for installation, storage and controls.

## Home and everyday use

| Home item | Purpose |
| --- | --- |
| **Discover** | Browse cached Firmware Hub listings, sorted by popularity by default |
| **Downloaded** | Browse packages actually saved on the M5, including retained older releases |
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
| **Storage & export** | See free space and storage setup |
| **Web UI / hotspot** | Toggle Wi-Fi radio power or hotspot broadcast and view browser addresses/password |
| **Last operation** | Read the last transport/import result or error |
| **Loaded on FM-1** | See live or last-observed firmware identity; choose to open confirmed installation history |
| **Home** | Return to Home |

Search and filters can remain active when switching between Discover and Downloaded. Clear Search, set filters to **all**, and turn **Favorites only** off if an expected entry is missing. Popularity uses Hub install counts; it is not a hardware compatibility guarantee.

### Download and install

A catalogue listing is not a downloaded package. Open its firmware options and choose **Download package** first. The app downloads from the maker's release and checks package structure and supplied hashes. External-installer-only listings cannot be downloaded by the app. Older downloaded releases remain separately selectable after a catalogue update. Downloads support responses without a declared length, including chunked HTTP responses, and are capped at 4 MB. Storage keeps 64 KB of working space; no full-backup space is reserved.

Downloaded packages can be installed offline. Installation requires a recognized FM-1, a valid FM-1 package and physical confirmation. Catalogue membership and JSON sidecars are optional. Unlisted builds can be installed with unknown chip support; a known older-flash-only build is still refused on newer FM-1 hardware. The writer preserves boot/calibration ranges, writes changed application sectors and checks readback. A reported MIDI boot identity is a further check; inspect actual FM-1 operation after restart.

### Optional backups and restore

Choose **Home → Create backup** to save a complete **1 MiB** FM-1 image, checked against device CRC and reread from storage. This connects automatically when needed. Existing backups are not removed during installation.

To restore, first use **Connect FM-1** to identify the target, then choose **Backups → selected backup → Restore on FM-1** and physically confirm. Restoration creates a fresh rollback backup and needs additional free space. It preserves boot/calibration sectors and refuses an image with mismatched protected bytes or device identity. It is not a bootloader replacement tool.

Backup actions also provide export instructions and explicit deletion of any saved backup, including the original or last copy. Select **Delete backup**, then hold B on StickS3 or Enter on Cardputer for two seconds to confirm. Deletion is blocked while an operation or restore confirmation is active. Use the web file view or power down before removing a Cardputer SD card to copy files.

## Optional web interface

Open **Home → Web UI / hotspot**. The hotspot defaults **off**; turn it on when needed, then open **Browser access details** for the password and **http://192.168.4.1/**. Turning it off stops broadcasting FM1-Flip and does not disconnect home Wi-Fi. The setting is saved across restarts. If already on home Wi-Fi, use the station address shown in Browser access details instead. The browser Settings page also has the hotspot toggle.

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

Export `/fm1/logs/session-N.txt` through **Files & backups** when investigating a connection failure. Include the exact error, M5 model, FM-1 firmware, cable/adapter and power arrangement.

## Wi-Fi and download diagnostics

In the on-device Wi-Fi password editor, press **Tab** on Cardputer to show/hide the password. On StickS3, select **SHOW / HIDE** in the character picker's edit actions. Passwords start hidden each time the editor opens. The browser's Wi-Fi form also has a **Show password** button.


Wi-Fi setup uses 2.4 GHz networks. After entering credentials, a small green connection indicator appears in the header when connected; successful joins do not show a popup. Failures still show authentication/network/timeout guidance. Browser addresses are under Web UI / hotspot. HTTPS imports require a working Internet connection and a synchronized clock. Connection failures now include TLS errors and free/largest-block RAM diagnostics so they can be distinguished from FM-1 USB hardware failures.


Use **Home → Web UI / hotspot → Turn Wi-Fi off** to shut down the radio (both home-network Wi-Fi and hotspot) for lower power use. The power setting is saved. Choosing Download or Refresh while it is off asks **Connect Wi-Fi and continue?**. Confirm to reconnect with saved credentials, wait for an IP/clock, and continue automatically; Cancel keeps Wi-Fi off. Hotspot broadcast remains a separate saved preference. The browser also has a Wi-Fi power toggle; switching off closes the connection.

Downloads write in SD-sector-sized pieces and complete positive partial writes. A zero-progress storage write still aborts the import and removes the partial file; its error includes byte offset and filesystem errno instead of guessing that it is a network problem.

## Preparing a catalogue on a computer

The device includes an offline catalogue snapshot. To prepare an import from the documented feed/cache:

```sh
python3 tools/sync_catalogue.py --cache .cache/catalogue-cache.json --output .cache/catalogue.json
```

Import the JSON through the web page or copy it to `/fm1/catalogue.json` on SD. The helper exports 96 entries per page; pass `--page N` for another page. Device refresh supports the full feed up to its bounded limit. Firmware package bytes come from makers' releases.
