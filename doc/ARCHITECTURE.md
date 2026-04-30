# System Architecture

> **Note**: This is an OTA application designed to run on the BYUI eBadge bootloader platform. It is installed via the bootloader's OTA menu and runs in an OTA partition (ota_0, ota_1, or ota_2). Configuration (user's nickname) is provided by the bootloader.

## Boot Flow

```
Power On / Reset
      │
      ▼
ROM Bootloader  (permanent in ESP32-S3 silicon)
  • Checks strapping pins
  • Enters UART download mode if IO0 held LOW
  • Loads 2nd-stage bootloader from 0x1000
      │
      ▼
ESP-IDF Bootloader  (bootloader.bin @ 0x1000)
  • Reads partition table at 0x8000
  • Reads otadata to select boot partition
  • Verifies app image integrity
  • Jumps to selected partition
      │
      ▼
Your Application  (factory or OTA partition)
  • app_main() runs
  • Factory partition is never overwritten by OTA
```

## Flash Memory Map (8 MB)

```
Address       Size    Partition       Purpose
─────────────────────────────────────────────
0x0000          4 KB  (reserved)      Boot vectors
0x1000        ~40 KB  bootloader      ESP-IDF 2nd-stage bootloader
─────────────────────────────────────────────
0x8000          4 KB  partition table Partition layout
0x9000         24 KB  nvs             NVS: Wi-Fi credentials, settings
0xF000          8 KB  otadata         OTA boot state
0x11000         4 KB  phy_init        RF calibration data
─────────────────────────────────────────────
0x20000       960 KB  factory  (app)  Your permanent application
                                      ✓ Never overwritten by OTA
─────────────────────────────────────────────
0x110000      960 KB  ota_0    (app)  OTA download slot 0
0x200000      960 KB  ota_1    (app)  OTA download slot 1
0x2F0000      960 KB  ota_2    (app)  OTA download slot 2
─────────────────────────────────────────────
Total used: ~4 MB of 8 MB flash
```

> The OTA slots are optional. If you do not need OTA updates, the factory partition can be enlarged to fill more of the flash — adjust `partitions.csv` accordingly.

## OTA Update Flow (Optional)

If you add OTA capability to your application:

```
ESP32-S3 app_main()
      │
      ├─ Connect to Wi-Fi
      │
      ├─ HTTP GET  manifest.json
      │     {
      │       "apps": [
      │         { "name": "LED App", "version": "1.0", "url": "http://…/led.bin" }
      │       ]
      │     }
      │
      ├─ User selects an app
      │
      ├─ HTTP GET  led.bin  (binary stream)
      │     esp_https_ota_begin()
      │     Write chunks → ota_0 partition
      │     esp_https_ota_end()  (verifies image)
      │
      ├─ esp_ota_set_boot_partition(ota_0)
      │
      └─ esp_restart()  →  boots into downloaded app
```

To return to the factory partition from the OTA app:

```c
const esp_partition_t *factory =
    esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                             ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
esp_ota_set_boot_partition(factory);
esp_restart();
```

## Component Interaction

```
app_main()
  ├── NVS Flash init
  ├── GPIO init  (LEDs, buttons)
  ├── (optional) Wi-Fi Manager
  ├── (optional) Display driver  (SPI2)
  ├── (optional) LED strip  (RMT → GPIO7)
  ├── (optional) I2C  (accelerometer GPIO47/21)
  └── Main application task
```

## BLE Friends Around Me Application Flow

For the "Friends Around Me" application:

```
app_main()
  ├── NVS init
  │    ├── Initialize system NVS partition
  │    ├── Initialize user_data NVS partition (0x7E0000)
  │    ├── Load user's nickname from badge_cfg namespace
  │    │    Key: "nick" (set by bootloader portal)
  │    └── Load "met people" list from friends_app namespace
  │         Key: "met_list" (array of MAC addresses)
  │
  ├── BLE init
  │    ├── Start advertising (broadcast name)
  │    │    Service UUID: custom namebadge identifier
  │    │    Service Data: user's nickname (up to 25 chars)
  │    └── Start scanning (discover others)
  │         Filter: match namebadge service UUID
  │         Parse: extract name from service data
  │
  ├── Display init (ILI9341 on SPI2)
  │    └── Render UI: name header + nearby list
  │
  ├── Button handler task
  │    ├── Up/Down: navigate nearby people list
  │    ├── Right: mark selected person as "met"
  │    │    ├── Add MAC to met_list in NVS
  │    │    │    Partition: user_data
  │    │    │    Namespace: friends_app
  │    │    │    Key: met_list (blob)
  │    │    ├── nvs_commit() to persist
  │    │    └── Remove from display
  │    └── (optional) Left/A/B: auxiliary functions
  │
  └── BLE event handler
       ├── On scan result:
       │    ├── Parse name from advertisement
       │    ├── Extract MAC address
       │    ├── Check if already met (compare with met_list)
       │    ├── Skip if met == true
       │    ├── Add to nearby list (if new)
       │    └── Update display
       │
       └── On timeout (person left range):
            └── Remove from nearby list
```

**Key Interaction:**  
When user presses **Right** on a selected person, that person's MAC address is stored in the `user_data` NVS partition under namespace `friends_app`, and they are filtered out of future scan results, allowing users to focus on meeting new people.

**NVS Storage Schema:**

| Partition | Namespace | Key | Type | Purpose |
|-----------|-----------|-----|------|---------|
| user_data | badge_cfg | nick | string | User's display name (from bootloader) |
| user_data | badge_cfg | email | string | Email (from bootloader, optional) |
| user_data | badge_cfg | ssid | string | WiFi SSID (from bootloader) |
| user_data | badge_cfg | pass | string | WiFi password (from bootloader) |
| user_data | friends_app | met_list | blob | Array of met people's MAC addresses |
| user_data | friends_app | met_count | uint16 | Number of people met |

## Development Workflow

### As an OTA Application

```
1. Clone this repo
2. Set up ESP-IDF environment
   idf.py set-target esp32s3
3. Build the application
   idf.py build
4. Publish to app manifest (for OTA download)
   - Upload build/friends_around_me.bin to web server
   - Add entry to manifest.json
5. Install via bootloader on badge:
   - Press RESET + BOOT on badge
   - Select "OTA App Download"
   - Choose "Friends Around Me"
```

### For Direct Development (USB Flash)

```
1. Build application
   idf.py build
2. Flash to OTA partition
   idf.py -p /dev/ttyUSB0 flash
3. Monitor serial output
   idf.py -p /dev/ttyUSB0 monitor
```

**Note**: Direct USB flashing requires the bootloader already installed in the factory partition. See BYUI-Namebadge-OTA repository for bootloader setup.

## Returning to Factory Loader

Users can return to the bootloader menu at any time:

**Method 1: Hardware (from running app)**
- Press RESET button
- Within 500ms, press and hold BOOT button
- Factory loader menu appears

**Method 2: Programmatic (from code)**
```c
const esp_partition_t *factory =
    esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                             ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
esp_ota_set_boot_partition(factory);
esp_restart();
```
