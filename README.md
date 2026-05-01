# Friends Around Me

BLE proximity app for the BYUI eBadge (ESP32-S3), designed for event networking.
Each badge advertises a nickname, scans nearby badges running the same app, and lets users confirm "met" status through a bilateral handshake.

## What It Does

- Reads your nickname from bootloader-managed NVS (`user_data` partition, `badge_cfg.nick`)
- Advertises custom manufacturer payloads with:
	- Company ID (`0xFFFF`)
	- Magic (`BADG`)
	- Target kind (`NONE`, `MEET`, `FIND`)
	- 2-byte target (MAC tail)
	- Nickname (up to 17 chars in advertising payload)
- Scans for the same payload format from nearby badges
- Shows two views on the display:
	- Friends to Meet
	- Friends I Have Met
- Supports mutual "meet" confirmation:
	- Press Right on a selected friend to broadcast a MEET request to that badge
	- They are marked met only when both badges request each other

## Important Current Behavior

- "Met" tracking is currently in-memory for this app session.
- On startup, `met_tracker_init()` clears any previous met list keys from NVS.
- Nickname fallback: if `badge_cfg.nick` is missing, app uses `badge-<MAC tail>`.
- Entry timeout: nearby friends are removed if not seen for 5 seconds.

## Platform

- Target MCU: ESP32-S3
- Bootloader ecosystem: BYUI Namebadge OTA
- Display: ILI9341, landscape 320x240 over SPI2
- BLE stack: NimBLE (ESP-IDF v5.x)

## Controls

| Button | Current behavior |
|---|---|
| Up | Move selection up |
| Down | Move selection down |
| Right | Send MEET request to selected friend |
| Left | Reserved (not implemented) |
| A | Reserved (not implemented) |
| B | Reserved (not implemented) |

Note: button handler currently reacts only to click events in `main.c`.

## Build and Flash (Development)

```bash
git clone https://github.com/watsonlr/namebadge-friends-around-me.git
cd namebadge-friends-around-me

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Install via OTA Catalog

If your catalog includes this app:

1. Reboot to bootloader (RESET then BOOT timing flow)
2. Open OTA App Download
3. Select Friends Around Me
4. Download and reboot into app

For publishing steps, see [PUBLISHING.md](PUBLISHING.md) and helper script [publish_friends_app.sh](publish_friends_app.sh).

## Project Layout

```text
namebadge-friends-around-me/
├── main/
│   ├── main.c
│   ├── ble_advertising.c
│   ├── ble_scanning.c
│   ├── ui.c
│   ├── display.c
│   ├── buttons.c
│   ├── leds.c
│   └── met_tracker.c
├── doc/
├── partitions.csv
├── PUBLISHING.md
└── README.md
```

## Notes for Contributors

- Verify docs against implementation before merging, especially around BLE payload format and met-state persistence.
- Prefer small hardware-tested changes because timing and BLE/display concurrency matter.

## Related Docs

- [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md)
- [doc/BLE_FRIENDS_FEATURE.md](doc/BLE_FRIENDS_FEATURE.md)
- [doc/HARDWARE.md](doc/HARDWARE.md)
- [CHANGES_OTA_INTEGRATION.md](CHANGES_OTA_INTEGRATION.md)
