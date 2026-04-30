# Friends Around Me - BYUI eBadge OTA Application

A Bluetooth Low Energy (BLE) proximity application for the BYUI eBadge V3.0 namebadge platform. This app helps students meet each other at events by showing nearby badge wearers and tracking who you've already met.

## Overview

**Application Type**: OTA-installable student application  
**Platform**: BYUI eBadge V3.0 (ESP32-S3)  
**Bootloader Required**: [BYUI-Namebadge-OTA](https://github.com/watsonlr/BYUI-Namebadge4-OTA)

### Features

- 📡 **BLE Broadcasting**: Advertise your name to nearby badges
- 👀 **Proximity Detection**: See who's around you in real-time
- ✅ **Check-off System**: Mark people as "met" to focus on meeting new friends
- 💾 **Persistent Storage**: Met people list survives app updates
- 📊 **Signal Strength**: Visual indicators show proximity
- 🎨 **Interactive Display**: Navigate with Up/Down, check off with Right button

## Installation

### Prerequisites

1. BYUI eBadge with bootloader installed ([BYUI-Namebadge4-OTA](https://github.com/watsonlr/BYUI-Namebadge4-OTA))
2. Badge configured with your nickname (via bootloader portal)
3. WiFi configured for OTA downloads

### Option 1: Install via Bootloader OTA Menu (Recommended)

The easiest way to install Friends Around Me:

1. Press **RESET** on your badge
2. Within 500ms, press and hold **BOOT** button
3. Navigate to "OTA App Download"
4. Select "Friends Around Me" from the app list
5. Wait for download to complete (~565 KB)
6. Badge automatically reboots into the app

The app will appear in your bootloader's OTA catalog if your instructor has published it.

### Option 2: Direct Flash (Development)

```bash
# Clone repository
git clone https://github.com/watsonlr/namebadge-friends-around-me.git
cd namebadge-friends-around-me

# Set target
idf.py set-target esp32s3

# Build and flash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Usage

### First Launch

The app automatically reads your configured nickname from the bootloader. If not configured, you'll see an error message prompting you to enter the bootloader setup.

### Meeting People

1. **Scan**: Badge automatically scans for nearby friends
2. **Navigate**: Use **Up/Down** buttons to select a person
3. **Check Off**: Press **Right** button when you've met someone
4. **Repeat**: Met people disappear from the list, focus on new friends!

### Button Controls

| Button | Function |
|--------|----------|
| Up     | Move selection up |
| Down   | Move selection down |
| Right  | Check off selected person as "met" |
| Left   | *(Future: undo/view met list)* |
| A      | *(Future: reset met list)* |
| B      | *(Future: toggle modes)* |

### Returning to Bootloader

- Press **RESET**, then hold **BOOT** within 500ms
- Or use the bootloader menu to select a different app

## Technical Details

See [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md) and [doc/BLE_FRIENDS_FEATURE.md](doc/BLE_FRIENDS_FEATURE.md) for detailed technical documentation.

### Key Components

- **BLE Stack**: NimBLE for advertising and scanning
- **Display**: ILI9341 TFT (240×320) via SPI
- **Storage**: NVS in `user_data` partition (survives OTA)
- **Flash Usage**: ~700 KB (fits in 960 KB OTA partition)

### Hardware Specifications

See [doc/HARDWARE.md](doc/HARDWARE.md) for complete pinout and peripheral details.

## Development

### Build Requirements

- ESP-IDF v5.x or later
- Python 3.8+
- ESP32-S3 target configured

### Project Structure

```
namebadge-friends-around-me/
├── main/               # Application source code
├── components/         # Reusable components (BLE, display, buttons)
├── doc/               # Documentation
│   ├── ARCHITECTURE.md
│   ├── BLE_FRIENDS_FEATURE.md
│   ├── HARDWARE.md
│   └── NAMEBADGE_BOOTING.md
└── README.md
```

### Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test on hardware
5. Submit a pull request

## License

See [LICENSE](LICENSE) file for details.

## Related Projects

- [BYUI-Namebadge4-OTA](https://github.com/watsonlr/BYUI-Namebadge4-OTA) - Bootloader and OTA framework
- [namebadge-apps](https://github.com/watsonlr/namebadge-apps) - App catalog and manifest

## Publishing to OTA Catalog

Want to make this app available for OTA download? See [PUBLISHING.md](PUBLISHING.md) for complete instructions on:
- Building and preparing the binary
- Calculating size and SHA-256 hash
- Uploading to GitHub Pages
- Creating/updating the manifest JSON
- Using the automated `publish_friends_app.sh` script

## Support

For issues or questions:
- Open an issue on GitHub
- Check the [hardware documentation](doc/HARDWARE.md)
- Review the [architecture guide](doc/ARCHITECTURE.md)

---

**Last Updated**: April 2026

A project for name badges with friends around me functionality.

## Getting Started

This project is currently in development.
