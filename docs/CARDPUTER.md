# Cardputer and Cardputer ADV

[Project overview](../README.md) · [Shared usage guide](USAGE.md) · [StickS3 guide](STICKS3.md)

Original Cardputer and Cardputer ADV use the same firmware image and keyboard controls. The board variant is detected at startup.

## Installation and storage

1. Download **`fm1-flip-cardputer.bin`** from [GitHub Releases](https://github.com/chrisdiana/fm1-flip/releases).
2. Prepare a **FAT32 microSD card**. Firmware packages, catalogue data, logs and backups are stored on this card; internal filesystem initialization is not required.
3. Copy the binary to SD and choose **SD → file → Install** in M5Launcher, or upload through Launcher's **WUI**. The display runs horizontally.
4. For standalone use, flash the same merged image at **`0x0`**. This replaces Launcher and its partition layout/settings.

See [M5Launcher installation and packaging](M5LAUNCHER.md) for details. Power down before removing the SD card.

## Quick start

1. Open **Home → Wi-Fi setup**, select your 2.4 GHz network and type its password. Press **Tab** to show/hide it and **Enter** to join. A small green header icon indicates a connection; failures appear as a message.
2. Open **Discover**, select firmware with **Enter** or **`/`**, select again for options, then choose **Download package**. Alternatively, copy a `.fwsc` onto SD as described below.
3. Open **Home → Downloaded**, select the saved package, open its options and choose **Install on FM-1**.
4. Connect and power the FM-1 using a verified data cable and power arrangement. Review the firmware and hold **Enter** or **`/` for two seconds** to confirm. Install connects and identifies the FM-1 automatically.
5. Follow the progress screen. If requested, switch FM-1 **off, then on** with the cable attached for its boot-key connection. Keep power and USB connected while writing, then inspect FM-1 operation after restart.

## Keyboard map

These controls apply to Cardputer ADV and original Cardputer.

| Key | Action |
| --- | --- |
| **`;`** | Previous item / scroll up |
| **`.`** | Next item / scroll down |
| **Enter** or **`/`** | Choose/open the highlighted item (slash types normally in text entry) |
| **Esc** or **`,`** | Back one level; cancel a pending confirmation or active operation (comma types normally in text entry) |
| **Fn + F** | Go Home during normal navigation |
| **Tab** | Cycle Discover → Downloaded → Loaded on FM-1 while browsing |
| **Type printable text** | Search the firmware list while browsing |
| **Backspace/Delete** | Remove the last search character |
| **Hold Enter or `/` for 2 seconds** | Confirm a reviewed install, restore, initialization or deletion |

The top-left **Esc / backtick** key goes Back. During a transport operation, **Esc** requests cancellation during connection, backups, imports and flash writing. Writes stop after completing and verifying the current sector; the FM-1 is not restarted after cancellation. During text entry, type the value directly, use Backspace/Delete to edit, **Enter** to submit, and **Esc** to cancel. Use the cancel key before Home if you are in the editor or reviewing a confirmation.

Additional shortcuts in firmware browsing/detail views:

| Key | Action |
| --- | --- |
| **`[`** | Toggle the selected firmware's favorite status |
| **`]`** | Connect FM-1, or create a backup if already connected in ROM mode |
| **`{`** | Download the selected package |
| **`}`** | Refresh the catalogue |
| **Backslash** | Review installation of the selected saved package from its detail view |

## Copy downloaded firmware to Cardputer's SD card

Original Cardputer and Cardputer ADV can discover firmware packages copied from a computer:

1. Power off the Cardputer before removing its FAT32 microSD card.
2. On your computer, create `/fm1/packages/` at the card's root if needed, and copy your downloaded FM-1 **`.fwsc` packages** into it. Keep the original filenames; no special hashed name is required. Extract ZIP downloads first. Raw `.bin` files are not imported as firmware packages.
3. Safely eject the card, reinsert it and restart FM-1 Flip. Packages are listed at startup; their contents are fully validated before installation, rather than reread on every boot.
4. Open **Home → Downloaded**, select a package, open its options and choose **Install on FM-1**. Physically confirm the installation on the device.

Example SD-card layout:

```text
/fm1/packages/patchwerk.fwsc
/fm1/packages/another-firmware.fwsc
```

No `.json` sidecar or Hub listing is required. Copy any valid FM-1 `.fwsc` package into this folder and install it from **Downloaded**, using the existing physical confirmation. The installer uses an exact SHA-256 match in optional sidecar or catalogue metadata when available; filenames do not establish compatibility. Missing, outdated or invalid catalogue metadata does not prevent installing a local package. A known older-flash-only build is still refused on newer FM-1 hardware.

For unlisted files, chip support and any special installation prerequisites are your responsibility: passing package checks does not guarantee the firmware will run on your FM-1 revision. Package structure, checksums, FM-1 model, protected flash regions and sector readback are still checked. Flip does not convert raw binaries or replace special multi-part maker installers. Use **Create backup** first if you want a recovery image; backups remain optional. Search and filters also apply to manually copied packages.

Some Hub entries require a maker installer and cannot be downloaded or installed through Flip. JIANT FM is one example: its installer handles additional components. Follow its [installation manual](https://github.com/juanjiant-bit/JIANT/blob/main/docs/MANUAL.md), including restoring official firmware first when switching from other custom firmware.

## Everyday use

See the [shared usage guide](USAGE.md) for Home menu actions, browsing and filters, optional backups and restore, the web interface, Wi-Fi power controls and troubleshooting.

Installs do not create or require a backup. Use **Home → Create backup** if you want a recovery image. Firmware switching does not migrate songs, sounds or settings between firmware families.
