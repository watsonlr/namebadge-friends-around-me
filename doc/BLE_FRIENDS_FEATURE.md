# BLE Friends Around Me Feature

## Overview

Friends Around Me is an OTA app for the BYUI eBadge platform (ESP32-S3 + NimBLE).
Each badge advertises its nickname and listens for other badges using the same
manufacturer payload format. The app supports a bilateral meet handshake so both
people intentionally confirm they met.

## Current Behavior (Implemented)

1. Reads nickname from NVS partition user_data, namespace badge_cfg, key nick.
2. Falls back to badge-XXYY (BT MAC tail) if nickname is missing.
3. Advertises custom manufacturer data with:
   - company id: 0xFFFF
   - magic: BADG
   - target kind: NONE, MEET, or FIND
   - target bytes: 2-byte MAC tail target
   - nickname payload: up to 17 bytes
4. Scans continuously for matching manufacturer payloads.
5. Tracks nearby entries with RSSI, timestamp, BLE address, and request flags.
6. Removes stale nearby entries after 5 seconds without sightings.
7. Uses Right button to send MEET request to currently selected friend.
8. Marks friend as met only on bilateral MEET handshake:
   - I am MEET-targeting them, and
   - their advertisement is MEET-targeting me.

## Important Current Limitation

Met-list persistence is currently disabled in practice:

- met_tracker_init initializes in-memory structures.
- It also erases old met_count and met_list keys in user_data/friends_app.
- Result: met state does not survive reboot right now.

The nickname and bootloader-owned settings still persist normally.

## BLE Payload Format

Advertisement uses flags + manufacturer data only (legacy 31-byte payload budget).

Manufacturer payload layout:

- bytes 0-1: company id (0xFFFF)
- bytes 2-5: ASCII magic BADG
- byte 6: target kind
- bytes 7-8: target MAC tail bytes
- bytes 9+: nickname bytes

Because of payload limits, BLE_ADV_MAX_NICKNAME_LEN is 17.

## Meet and Find Semantics

- MEET means "I want to confirm meeting this person."
- FIND means "help me find this person" and auto-expires after 5 seconds.
- Current button mapping sends MEET requests from Right click.
- FIND support exists in payload and scanning logic, and is visualized by UI/LED if seen.

## UI Behavior

- Header shows list mode and local nickname.
- Two body modes:
  - Friends to Meet
  - Friends I Have Met
- Rows can flash by state:
  - red: my outgoing MEET target
  - yellow: incoming MEET request toward me
  - green: incoming FIND request toward me
- Footer shows Seen and Met counters.
- LED feedback blinks with incoming requests:
  - green for FIND
  - yellow for MEET

## Buttons (Current)

- Up: move selection up
- Down: move selection down
- Right: send MEET request to selected friend
- Left: reserved
- A: reserved
- B: reserved

Main event handler currently processes click events only.

## Storage Model

### Bootloader-owned values

- partition: user_data
- namespace: badge_cfg
- key used by this app: nick

### App-owned values

- namespace: friends_app
- historical keys: met_count and met_list
- current behavior clears these at startup

## Notes for Future Changes

If persistence is re-enabled, update this file and README together, and clearly
document migration behavior for existing badges.
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
