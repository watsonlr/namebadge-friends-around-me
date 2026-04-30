# BLE Friends Around Me Feature

## Overview

This is an **OTA application** for the BYUI eBadge bootloader platform. It enables namebadges to discover and display other nearby namebadges using Bluetooth Low Energy (BLE) advertising and scanning. The primary objective is to **facilitate meeting others** at events — users can see who's nearby and mark people as "met" once they've introduced themselves.

**Application Type**: OTA-installable student application  
**Bootloader Required**: BYUI-Namebadge-OTA (factory partition)  
**Configuration**: Uses nickname configured via bootloader's captive portal

## Requirements

### Core Functionality

1. **Personalization**: Each board is customized with a user's name
2. **Name Broadcasting**: Advertise the user's name via BLE
3. **Proximity Detection**: Scan for and display other nearby namebadges
4. **Interactive Check-off**: Mark people as met so they're removed from the "around me" list

## Technical Approach

### BLE Advertising

Each namebadge will:
- Broadcast its name in BLE advertisement packets
- Use a custom service UUID to identify namebadge devices
- Include the user's name in the advertising data payload

### BLE Scanning

Each namebadge will:
- Continuously scan for BLE advertisements from other namebadges
- Filter advertisements by the custom namebadge service UUID
- Parse received advertisements to extract names
- Track RSSI (signal strength) to estimate proximity
- Maintain a list of nearby friends

### Display

The namebadge display will show:
- User's own name (top/header)
- List of detected nearby friends (not yet met)
- Visual indicator for the currently selected person
- Optional: signal strength indicator or distance estimate
- Optional: count of total people met

### User Interaction

**Meeting People Workflow:**

1. Display shows list of nearby people broadcasting their names
2. User navigates the list with **Up/Down** buttons
3. When user meets someone, they select that person's name and press **Right** button
4. Selected person is marked as "met" and removed from the "Friends Around Me" list
5. Met status persists in NVS storage

**Button Mapping:**
- **Up** (GPIO 17): Move selection up in the list
- **Down** (GPIO 16): Move selection down in the list  
- **Right** (GPIO 15): Check off selected person as "met"
- **Left** (GPIO 14): Optional - undo last check-off or view met list
- **A** (GPIO 38): Optional - reset all met status
- **B** (GPIO 18): Optional - toggle display modes

## Implementation Notes

### NVS Initialization

Before accessing any NVS data, initialize both the system NVS and the `user_data` partition:

```c
#include "nvs_flash.h"

void app_main(void) {
    // Initialize default NVS partition (required by ESP-IDF)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || 
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    // Initialize user_data partition (created by bootloader)
    // This partition is NEVER erased by OTA updates
    ret = nvs_flash_init_partition(WIFI_CONFIG_NVS_PARTITION);
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || 
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase_partition(WIFI_CONFIG_NVS_PARTITION);
        nvs_flash_init_partition(WIFI_CONFIG_NVS_PARTITION);
    }
    
    // Check if badge has been configured with a name via bootloader
    if (!is_badge_configured()) {
        // Display error - user must configure via bootloader first
        display_error("Not Configured!",
                     "Press RESET, then hold BOOT",
                     "to enter bootloader setup");
        
        // Or optionally: return to factory partition
        // const esp_partition_t *factory = 
        //     esp_partition_find_first(ESP_PARTITION_TYPE_APP,
        //                            ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
        // esp_ota_set_boot_partition(factory);
        // esp_restart();
        return;
    }
    
    // Continue with app initialization...
}

bool is_badge_configured(void) {
    nvs_handle_t h;
    if (nvs_open_from_partition(WIFI_CONFIG_NVS_PARTITION,
                                WIFI_CONFIG_NVS_NAMESPACE,
                                NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    
    char nick[33] = {0};
    size_t len = sizeof(nick);
    esp_err_t err = nvs_get_str(h, WIFI_CONFIG_NVS_KEY_NICK, nick, &len);
    nvs_close(h);
    
    return (err == ESP_OK && nick[0] != '\0');
}
```

### BLE Configuration

- **Advertisement Interval**: 100-200 ms (balance between discoverability and power)
- **Scan Interval**: Continuous or periodic scanning
- **Scan Window**: Optimize for power vs. responsiveness
- **TX Power**: Adjustable based on desired range

### Data Storage

**NVS Partition: `user_data`**

The bootloader creates a dedicated `user_data` NVS partition at the top of flash memory that is **never overwritten by OTA updates**. This ensures user data persists even when firmware is updated.

**User's Name:**

Read from the existing bootloader configuration:

```c
#define WIFI_CONFIG_NVS_PARTITION  "user_data"
#define WIFI_CONFIG_NVS_NAMESPACE  "badge_cfg"
#define WIFI_CONFIG_NVS_KEY_NICK   "nick"

// Initialize partition
nvs_flash_init_partition(WIFI_CONFIG_NVS_PARTITION);

// Read nickname
nvs_handle_t h;
nvs_open_from_partition(WIFI_CONFIG_NVS_PARTITION,
                       WIFI_CONFIG_NVS_NAMESPACE,
                       NVS_READONLY, &h);

char nickname[33] = {0};
size_t len = sizeof(nickname);
nvs_get_str(h, WIFI_CONFIG_NVS_KEY_NICK, nickname, &len);
nvs_close(h);
```

**Met People List:**

Store met people in the same `user_data` partition using a dedicated namespace:

```c
#define FRIENDS_NVS_NAMESPACE  "friends_app"
#define FRIENDS_NVS_KEY_MET    "met_list"

// Store met people as a blob (array of MAC addresses)
typedef struct {
    uint8_t mac[6];
} met_person_t;

nvs_handle_t h;
nvs_open_from_partition(WIFI_CONFIG_NVS_PARTITION,
                       FRIENDS_NVS_NAMESPACE,
                       NVS_READWRITE, &h);

met_person_t met_list[256];  // Max 256 people
size_t count = load_met_count();
nvs_set_blob(h, FRIENDS_NVS_KEY_MET, met_list, count * sizeof(met_person_t));
nvs_commit(h);
nvs_close(h);
```

**In-Memory Tracking:**  
- Active nearby people (currently visible on scan)
- Selection cursor position
- RSSI values for proximity sorting
- Loaded met people MAC addresses for filtering

### Data Format

**BLE Advertisement Packet Structure:**

Use a custom 128-bit UUID to identify namebadge devices, and include the nickname in the advertisement data:

```c
// Custom UUID for BYUI Namebadge (example)
// Generate your own at https://www.uuidgenerator.net/
#define NAMEBADGE_SERVICE_UUID  "12345678-1234-5678-1234-56789abcdef0"

// Advertisement data structure:
// - Flags (3 bytes)
// - Complete 128-bit Service UUID (19 bytes)
// - Service Data: UUID + name (up to 31 bytes total in name)
//
// BLE advertisement max payload: 31 bytes
// After flags + UUID headers: ~20-25 bytes available for name

// Example using ESP-IDF NimBLE:
static void set_advertisement_data(const char *nickname) {
    struct ble_hs_adv_fields fields = {0};
    
    // Standard BLE flags
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    
    // Custom service UUID (identifies this as a namebadge)
    ble_uuid128_t svc_uuid;
    ble_uuid_from_str(NAMEBADGE_SERVICE_UUID, &svc_uuid.u);
    fields.uuids128 = &svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    
    // Include name in service data (truncate if needed)
    uint8_t svc_data[32];
    size_t name_len = strlen(nickname);
    if (name_len > 25) name_len = 25;  // Leave room for UUID
    memcpy(svc_data, nickname, name_len);
    
    fields.svc_data_uuid128 = svc_data;
    fields.svc_data_uuid128_len = name_len;
    
    ble_gap_adv_set_fields(&fields);
}
```

**Scan Result Parsing:**

```c
// Extract name from advertisement data when scanning
static void scan_callback(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        struct ble_gap_disc_desc *disc = &event->disc;
        
        // Check if this is a namebadge (has our service UUID)
        if (has_namebadge_uuid(disc)) {
            // Extract MAC address
            uint8_t mac[6];
            memcpy(mac, disc->addr.val, 6);
            
            // Check if already met
            if (is_already_met(mac)) {
                return;  // Skip this person
            }
            
            // Extract name from service data
            char name[32];
            extract_name_from_adv(disc->data, disc->length_data, name);
            
            // Add to nearby list
            add_nearby_person(name, mac, disc->rssi);
        }
    }
}
```

### UI Considerations

- Display shows list of nearby friends (scrollable if > 8-10 names)
- Names fade out or are removed after not seen for X seconds
- Optional: sort by signal strength (closest first)

## ESP32-S3 BLE Support

The ESP32-S3 includes BLE 5.0 support:
- Supports both BLE advertising and scanning simultaneously
- Low power consumption in BLE mode
- Compatible with ESP-IDF BLE stack (Bluedroid or NimBLE)

## Power Considerations

| Mode | Current Draw |
|------|-------------|
| BLE advertising + scanning | ~20-30 mA |
| BLE with display active | ~50-80 mA |
| Sleep between scans | Can reduce to <1 mA |

## Privacy & Security

- Names are broadcast publicly (no pairing required)
- BLE MAC addresses are used to track met people
- Optional: allow user to disable broadcasting (stealth mode)
- Optional: use MAC address randomization (requires BLE privacy feature)
- Consider: nickname vs. full name for privacy

**Note**: The bootloader's configuration portal collects the nickname during initial setup. Users should be advised to use a display name they're comfortable broadcasting publicly.

## OTA Application Details

This is an **OTA application** that runs on the BYUI eBadge bootloader platform:

### Installation

1. Badge must be configured via bootloader (nickname, WiFi) first
2. Install via bootloader's "OTA App Download" menu:
   - Press RESET + BOOT to enter factory loader
   - Select "OTA App Download"
   - Choose "Friends Around Me" from the app catalog
   - Download completes, badge reboots into this app

### Bootloader Integration

- **Partition Layout**: Installed in `ota_0`, `ota_1`, or `ota_2` partition (960 KB)
- **NVS Partition**: Uses existing `user_data` at 0x7E0000 (8MB) or 0x3E0000 (4MB)
- **Configuration**: Reads nickname from bootloader's `badge_cfg` namespace (no setup needed)
- **Persistent Storage**: Met people list survives OTA updates (stored in `user_data`)
- **Return to Loader**: User can re-enter bootloader anytime (RESET + hold BOOT)

### Flash Space Requirements

| Component | Size | Notes |
|-----------|------|-------|
| Application binary | ~500-700 KB | BLE + display + UI |
| Met people storage | ~1.5 KB | 256 people × 6 bytes MAC |
| **Total** | **~700 KB** | **Fits in 960 KB OTA slot** |

### Prerequisites

**Required before installation:**
- BYUI-Namebadge-OTA bootloader in factory partition
- Badge nickname configured (via bootloader portal)
- WiFi configured (for OTA download)

**Optional:**
- Email address (stored but not used by this app)

## Display Layout Example

```
┌──────────────────────────┐
│  Your Name: Lynn         │
│──────────────────────────│
│  Friends Around Me:      │
│                          │
│  > Alex         ●●●○○    │ ← selected
│    Jordan       ●●●●○    │
│    Sam          ●●○○○    │
│    Taylor       ●●●●●    │
│                          │
│  Met: 12  |  Nearby: 4   │
└──────────────────────────┘

 ●●●●● = signal strength
   >   = current selection
```

**Press RIGHT to mark Alex as met →** Alex removed from list

## Future Enhancements

- Add custom status messages
- View list of all people met (with timestamps)
- Export met list via WiFi/SD card
- Leaderboard for most people met
- Mesh networking for extended range
- BLE-based messaging between badges
- Undo check-off functionality
- "Nearby but already met" indicator

---

**Status**: Design phase  
**Last Updated**: April 2026
