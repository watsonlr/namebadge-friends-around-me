# Development Guide

## Prerequisites

### ESP-IDF Installation

This project requires ESP-IDF v5.0 or later.

**Install ESP-IDF:**

```bash
# Clone ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.3  # or latest stable release

# Install tools
./install.sh esp32s3

# Set up environment (add to ~/.bashrc for persistence)
. ~/esp/esp-idf/export.sh
```

## Building the Project

### 1. Set Up Environment

```bash
# Source ESP-IDF environment (if not in ~/.bashrc)
. ~/esp/esp-idf/export.sh

# Navigate to project
cd /path/to/namebadge-friends-around-me
```

### 2. Set Target

```bash
idf.py set-target esp32s3
```

### 3. Configure (Optional)

```bash
idf.py menuconfig
```

### 4. Build

```bash
idf.py build
```

The output binary will be in `build/friends_around_me.bin`

### 5. Flash (Development)

**Via USB:**
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

**Via Bootloader OTA** (recommended):
1. Upload `build/friends_around_me.bin` to a web server
2. Add entry to your app manifest JSON
3. Use bootloader's "OTA App Download" to install

## Project Status

### ✅ Completed

- [x] Project structure and build configuration
- [x] Documentation (README, ARCHITECTURE, BLE_FRIENDS_FEATURE, HARDWARE)
- [x] Basic main.c with NVS initialization
- [x] Badge configuration checking
- [x] Met people tracking initialization

### 🚧 To Do (Implementation)

- [ ] BLE component (advertising and scanning)
- [ ] Display component (ILI9341 driver)
- [ ] Button component (GPIO input handling)
- [ ] UI rendering (nearby people list)
- [ ] Met people management (add/remove/check)
- [ ] Power management
- [ ] Testing and debugging

## Development Workflow

### Adding BLE Support

1. Create `components/ble_friends/` directory
2. Implement NimBLE advertising with custom service UUID
3. Implement scanning and name extraction
4. Filter met people from scan results

### Adding Display Support

1. Create `components/display/` directory
2. Port ILI9341 SPI driver (may reuse from bootloader)
3. Implement UI rendering functions
4. Add nearby people list display

### Adding Button Support

1. Create `components/buttons/` directory
2. Implement GPIO button handling with debounce
3. Add navigation (Up/Down) logic
4. Add check-off (Right) logic

## Debugging

### Serial Monitor

```bash
idf.py monitor -p /dev/ttyUSB0
```

Exit with Ctrl+]

### Common Issues

**Badge not configured:**
- Enter bootloader (RESET + BOOT)
- Complete setup wizard
- Enter nickname

**Build errors:**
- Ensure ESP-IDF v5.0+ is installed
- Run `idf.py set-target esp32s3`
- Check sdkconfig.defaults matches your flash size

**Flash errors:**
- Try lower baud: `idf.py -p /dev/ttyUSB0 -b 115200 flash`
- Check USB cable (must support data, not charge-only)
- Verify port permissions: `sudo usermod -a -G dialout $USER`

## Testing

### Unit Testing
```bash
idf.py build
# TODO: Add component tests
```

### Hardware Testing
1. Flash firmware to badge
2. Configure nickname via bootloader
3. Test BLE broadcasting (use phone BLE scanner)
4. Test nearby people display
5. Test check-off functionality

## Contributing

See main [README.md](../README.md) for contribution guidelines.
