# Board bring-up notes

## Confirmed from source and published documentation

Both Cardputer variants use 8 MiB ESP32-S3 modules. Their SD pins match: CS12, MOSI14, CLK40, MISO39. Their keyboard hardware differs: the original uses a matrix reader, while ADV uses a TCA8418 over I2C. Pinned M5Cardputer 1.1.1 contains both readers. M5Unified/M5GFX identify the board, with a per-target fallback selected by this app.

- [Original Cardputer documentation and pin map](https://docs.m5stack.com/en/core/Cardputer)
- [ADV documentation and pin map](https://docs.m5stack.com/en/core/Cardputer-Adv)
- [Stamp-S3 schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/522/Sch_M5StampS3_v0.2.pdf)
- [Stamp-S3A schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1150/Sch_StampS3_v0.3.3.pdf)

StickS3 has 8 MiB flash and 8 MiB PSRAM. Its schematic labels the USB connector data nets USB_P/USB_N and includes separate USB/input and power-management circuitry. This is evidence to investigate its native USB path, not proof that battery-powered host mode sources VBUS or that two independently powered devices can be connected safely.

- [StickS3 documentation](https://docs.m5stack.com/en/core/StickS3)
- [StickS3 V0.6 schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150_Stick_S3_PRJ_V0.6_20251111_2025_11_17_16_10_24.pdf)

## USB implementation and limits

The reused ESP32-S3 transport uses GPIO20 (D+) and GPIO19 (D−), sends the proven ROM boot-key sequence, restores the pins to the host peripheral, and talks to ROM device 4C4A:8057. The initial connection therefore asks for the FM-1 to be off before the user turns it on. An already running USB-MIDI device can also be queried with the FM-1 identity handshake. If it reports an FM-1, the app requests ROM entry, tears down the old host session, and reconnects. Unsupported firmwares retain the off/on boot-key route.

USB serial on boot is disabled because the port belongs to the FM-1 host connection. Re-enter the M5's hardware download mode for computer reflashing. The host now cooperatively drains and releases USB resources before reconnecting; cleanup failure remains an explicit M5-restart-required condition. Actual reconnect and firmware response timing still need bench validation.

The selected upstream Cardputer recovery project reports ADV hardware tests. Those results do not validate this application's integration or the StickS3/original Cardputer targets. Actual VBUS measurements, cable role behavior, board-revision checks, runtime memory, and USB tests remain required for every declared target.
