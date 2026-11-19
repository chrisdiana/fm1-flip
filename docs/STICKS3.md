# StickS3

[Project overview](../README.md) · [Shared usage guide](USAGE.md) · [Cardputer / ADV guide](CARDPUTER.md)

This build targets **M5Stack StickS3**. It does not support StickC or StickC-Plus.

## Installation and storage

1. Download **`fm1-flip-sticks3.bin`** from [GitHub Releases](https://github.com/chrisdiana/fm1-flip/releases).
2. Install through M5Launcher's **WUI**, or its SD installation workflow if your setup provides SD access. Use a current dynamic-partition Launcher build.
3. StickS3 stores firmware packages, catalogue data, logs and backups in internal **LittleFS** storage. If storage does not mount on first use, choose **Home → Storage & export** and physically confirm initialization. This erases internal saved files; formatting never happens automatically.
4. For standalone use, flash the same merged image at **`0x0`**. This replaces Launcher and its partition layout/settings.

See [M5Launcher installation and packaging](M5LAUNCHER.md) for details. The standalone layout reserves 3 MiB for the app and 4.9375 MiB for LittleFS, without an OTA app slot. Packages and optional 1 MiB backups share that storage.

## Quick start

1. Open **Home → Wi-Fi setup** with **A**, select your 2.4 GHz network, enter its password with the character picker and choose **OK**. Use **SHOW / HIDE** in the edit actions to check the password. A small green header icon indicates a connection; failures appear as a message.
2. Open **Discover**, select firmware with **A**, select again for options and choose **Download package**. To import a local `.fwsc`, use the [web interface](USAGE.md#optional-web-interface); no Hub listing or JSON sidecar is required.
3. Open **Home → Downloaded**, select a saved package, open its options and choose **Install on FM-1**.
4. Connect and power the FM-1 using a verified data cable and power arrangement. Review the firmware and hold **B for two seconds** to confirm. Install connects and identifies the FM-1 automatically.
5. Follow the progress screen. If requested, switch FM-1 **off, then on** with the cable attached for its boot-key connection. Keep power and USB connected while writing, then inspect FM-1 operation after restart.

## Button map

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

During a transport operation, **hold A** requests cancellation while the screen says cancellation is available. During flash writing it finishes and verifies the current sector, then stops without restarting FM-1. A partial installation must be completed or restored before restarting the FM-1. **A+B** selects Home as the return view; progress remains visible until the operation finishes.

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

## USB download mode

If StickS3 is missing from the computer's serial ports, connect a data-capable USB cable, hold its power button until the green LED blinks, then release for download mode.

## Everyday use

See the [shared usage guide](USAGE.md) for Home menu actions, browsing and filters, optional backups and restore, the web interface, Wi-Fi power controls and troubleshooting.

Installs do not create or require a backup. Use **Home → Create backup** if you want a recovery image. Firmware switching does not migrate songs, sounds or settings between firmware families.
