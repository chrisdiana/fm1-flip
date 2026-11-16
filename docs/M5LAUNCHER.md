# FM-1 Flip for M5Launcher

Use the StickS3 binary for StickS3, or the shared Cardputer binary for either original Cardputer or Cardputer ADV. Both targets are **ESP32-S3 with 8 MiB flash**; these builds do not support StickC, StickC Plus or StickC Plus2.

| Board | File | Format |
| --- | --- | --- |
| StickS3 | `fm1-flip-sticks3-m5launcher.bin` | Merged image with app and internal-storage partition metadata |
| Cardputer / Cardputer ADV | `fm1-flip-cardputer-m5launcher.bin` | App-only image; firmware library uses microSD |

`fm1-flip-m5launcher.zip` contains these images, this guide, a manifest and `SHA256SUMS`. Extract it before installing. Generated files are under `build/releases/m5launcher/` in a source checkout.

## Install

1. Use a current M5Launcher build for the exact board with dynamic partition management. These packages follow the current installer's formats; old fixed-layout Launcher versions are not the compatibility target.
2. Copy the matching `.bin` to your FAT32 SD card and select **SD → file → Install** in Launcher. Alternatively, open Launcher's **WUI** and upload/install the matching file from a browser. StickS3 without an SD accessory can use WUI.
3. Let Launcher choose/create a suitable app partition. Each app needs about 2 MiB; the exact bytes are in `manifest.json`. Other installed apps may need removal if there is insufficient flash space. Launcher manages its own bootloader and partition layout; do not raw-flash an app-only image at address zero.
4. **StickS3:** the merged image declares a `spiffs` data partition. Leave that partition enabled. Current Launcher recognizes its absent payload and uses remaining available flash space rather than reserving the standalone build's full storage size. After launch, use **Home → Storage & export** to initialize LittleFS if it does not mount. Initialization erases that data partition; export any wanted contents first. No preexisting device filesystem, credentials or backups are included in this download.
5. **Cardputer / Cardputer ADV:** keep a FAT32 microSD card inserted when FM-1 Flip boots. No internal data partition is required; the app stores packages and backups under `/fm1` on SD.
6. FM-1 Flip opens Home. Set Wi-Fi, browse Discover, download a supported maker package, then choose Install on FM-1 and physically confirm. Retained FM-1 backups are optional through Create backup.

StickS3 controls: **A** choose, **B** next, **hold A** back, **hold B** scroll up continuously, **A+B** Home. Hold **B for two seconds** on a confirmation screen to proceed. Cardputer: **Enter** choose, **`;` / `.`** previous/next, **Fn + backtick** back, **Fn + F** Home; hold **Enter for two seconds** to confirm.

## Storage and updates

StickS3's available package space under Launcher depends on its other app/data partitions and can be smaller than a standalone installation. The default firmware-install path does not allocate a full-flash backup; explicit Create backup still needs 1 MiB plus working space. Launcher data backup/restore is a separate feature. Use Launcher's data-backup facilities before repartitioning or replacing a data partition you want to retain. Do not initialize storage merely to perform an ordinary update if it already mounts.

## Build and validate

From the project root:

```sh
pio run -e sticks3 -e cardputer
python3 tools/package_launcher.py
```

Packaging verifies app magic/chip, segment bounds, checksum and appended SHA-256. It checks merged partition-table MD5, app equality, erased NVS, absent filesystem payload and private-path/key markers. Inputs are clean build artifacts, never a device flash dump. The source build maps local compiler paths before they can be embedded in the firmware.

These files are **format-validated, not yet tested through M5Launcher on physical hardware**. First acceptance checks are install, boot, keyboard/buttons, Wi-Fi and storage mounting on each board. FM-1 USB/power/install behavior still needs independent hardware validation.

Installer behavior checked against upstream revision `6b432e4e185ceaebad14fb902d750d699b380bbd`, especially `src/sd_functions.cpp` and `src/partition_install_layout.cpp`.

References: [Launcher binary/install guide](https://github.com/bmorcelli/Launcher/wiki/Obtaining-binaries-to-launch), [Launcher formats and partitions](https://github.com/bmorcelli/Launcher/wiki/Explaining-the-project), [installer source](https://github.com/bmorcelli/Launcher/blob/6b432e4e185ceaebad14fb902d750d699b380bbd/src/sd_functions.cpp).
