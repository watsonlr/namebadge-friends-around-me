# Development Plan - Friends Around Me Application

## Current Status

✅ **Completed:**
- Project structure and documentation
- NVS initialization (default + user_data partition)
- Badge configuration validation
- Nickname retrieval from bootloader
- Met people tracking initialization

❌ **Not Started:**
- BLE advertising component
- BLE scanning component  
- Display driver and UI
- Button input handlers
- Met people tracking logic
- Main application loop
- Integration and testing

## Implementation Phases

### Phase 1: BLE Advertising (Priority 1)

**Goal:** Broadcast the user's nickname to nearby badges

**Tasks:**
1. Initialize NimBLE stack
   - Configure BLE controller and host
   - Set device name to badge nickname
   
2. Create custom BLE service
   - Define UUID for namebadge service (e.g., `0x180F` or custom)
   - Create characteristic for nickname
   
3. Configure advertisement data
   - Include service UUID in advertisement
   - Add nickname in manufacturer data or service data field
   - Set advertisement interval (recommend: 100-500ms for good discovery)
   
4. Start advertising
   - Begin continuous BLE advertising
   - Log advertising status

**Files to Create/Modify:**
- `main/ble_advertising.c` (new)
- `main/ble_advertising.h` (new)
- `main/CMakeLists.txt` (add BLE components)
- `main/main.c` (integrate BLE advertising)

**Testing:**
- Use nRF Connect or similar BLE scanner app
- Verify badge appears with correct nickname
- Check advertisement packet structure

---

### Phase 2: BLE Scanning (Priority 1)

**Goal:** Detect nearby badges and extract their nicknames

**Tasks:**
1. Configure BLE scanning
   - Set scan parameters (window, interval)
   - Enable active or passive scanning
   
2. Implement scan result callback
   - Filter by namebadge service UUID
   - Parse advertisement data to extract nickname
   - Calculate RSSI-based proximity
   
3. Maintain nearby friends list
   - Create data structure for detected friends
   - Track: nickname, RSSI, last seen timestamp
   - Implement timeout for stale entries (e.g., 5 seconds)
   - Auto-remove friends who move out of range

4. Filter met people
   - Cross-reference with met list from NVS
   - Exclude met people from nearby friends display

**Files to Create/Modify:**
- `main/ble_scanning.c` (new)
- `main/ble_scanning.h` (new)
- `main/main.c` (integrate BLE scanning)

**Data Structures:**
```c
typedef struct {
    char nickname[33];
    int8_t rssi;
    int64_t last_seen;  // timestamp in microseconds
    bool is_met;
} nearby_friend_t;

// Dynamic list or fixed-size array
#define MAX_NEARBY_FRIENDS 20
nearby_friend_t nearby_friends[MAX_NEARBY_FRIENDS];
```

**Testing:**
- Test with multiple badges advertising
- Verify RSSI updates correctly
- Confirm stale entries are removed
- Test met people filtering

---

### Phase 3: Display Driver and UI (Priority 2)

**Goal:** Show nearby friends and UI on ILI9341 display

**Tasks:**
1. Initialize ILI9341 SPI display
   - Configure SPI2 bus (see HARDWARE.md for pins)
   - Initialize ILI9341 driver
   - Set orientation and resolution (240×320)
   
2. Implement graphics primitives
   - Text rendering (use built-in fonts or custom)
   - Draw rectangles, lines
   - Fill background
   
3. Design UI layout
   ```
   ┌─────────────────────────┐
   │  Your Name: <nickname>  │ ← Header
   ├─────────────────────────┤
   │  Friends Around You:    │
   │                         │
   │  ▶ Alice        [▮▮▮▮]  │ ← Selected (strong signal)
   │    Bob          [▮▮▮░]  │
   │    Charlie      [▮▮░░]  │ ← Weaker signal
   │    Diana        [▮░░░]  │
   │                         │
   ├─────────────────────────┤
   │  Met: 12  |  Nearby: 4  │ ← Footer stats
   └─────────────────────────┘
   ```
   
4. Implement UI state machine
   - Track selected index
   - Handle scrolling when list > screen capacity
   - Update display when nearby friends change
   - Show signal strength indicator
   
5. Implement refresh logic
   - Update display at regular intervals (e.g., 500ms)
   - Optimize to reduce flicker
   - Clear and redraw only changed areas

**Files to Create/Modify:**
- `main/display.c` (new)
- `main/display.h` (new)
- `main/ui.c` (new) 
- `main/ui.h` (new)
- `main/CMakeLists.txt` (add display component dependencies)

**Testing:**
- Verify display initialization
- Test text rendering at various sizes
- Simulate friend list updates
- Test scrolling behavior
- Verify signal strength indicators

---

### Phase 4: Button Input (Priority 2)

**Goal:** Handle user button presses for navigation and check-off

**Tasks:**
1. Initialize GPIO for buttons
   - Configure GPIOs with pull-ups
   - Enable interrupts for button presses
   - Implement debouncing (hardware or software)
   
2. Create button event queue
   - Use FreeRTOS queue for button events
   - ISR posts events, task processes them
   
3. Implement button handlers
   - **Up (GPIO 17):** Move selection up, wrap to bottom
   - **Down (GPIO 16):** Move selection down, wrap to top
   - **Right (GPIO 15):** Mark selected person as met
   - **Left (GPIO 14):** (Future) Undo or show met list
   - **A (GPIO 38):** (Future) Reset all met status
   - **B (GPIO 18):** (Future) Toggle modes
   
4. Integrate with UI
   - Update selected index on Up/Down
   - Trigger check-off on Right press
   - Refresh display after button action

**Files to Create/Modify:**
- `main/buttons.c` (new)
- `main/buttons.h` (new)
- `main/main.c` (integrate button handling)

**Testing:**
- Test each button individually
- Verify debouncing works correctly
- Test rapid button presses
- Confirm UI updates on button events

---

### Phase 5: Met People Tracking (Priority 2)

**Goal:** Persist met people across app restarts and OTA updates

**Tasks:**
1. Implement "mark as met" function
   - Get selected friend's nickname
   - Add to met list in NVS
   - Increment met count
   - Remove from nearby friends display
   
2. Implement NVS storage format
   - Option A: Store as blob (array of strings)
   - Option B: Store each name with unique key (e.g., `met_0`, `met_1`)
   - Recommend Option A for simplicity:
     ```c
     char met_names[MAX_MET_PEOPLE][33];
     nvs_set_blob(h, "met_list", met_names, count * 33);
     ```
   
3. Implement "load met list" function
   - Read from NVS on startup
   - Populate in-memory met list
   
4. Implement filtering logic
   - When scanning results arrive, check against met list
   - Mark friends as `is_met = true` if in list
   - UI displays only non-met friends
   
5. (Optional) Implement reset function
   - Clear all met people from NVS
   - Triggered by Button A (long press)
   - Show confirmation prompt

**Files to Create/Modify:**
- `main/met_tracker.c` (new)
- `main/met_tracker.h` (new)
- Update `main/main.c` to load met list on startup

**NVS Keys:**
- `met_count` (uint16): Number of people met
- `met_list` (blob): Array of nicknames

**Testing:**
- Test marking a friend as met
- Verify persistence across reboot
- Test OTA update (met list should survive)
- Test with full met list (MAX_MET_PEOPLE)
- Test reset functionality

---

### Phase 6: Main Application Loop (Priority 3)

**Goal:** Integrate all components into cohesive application

**Tasks:**
1. Create FreeRTOS tasks
   - **BLE Task:** Handle advertising + scanning
   - **UI Task:** Update display periodically
   - **Button Task:** Process button events
   - Use queues/semaphores for inter-task communication
   
2. Implement event-driven architecture
   ```
   BLE Scan Results → Queue → UI Update
   Button Press → Queue → Met Tracker → UI Update
   Timer → UI Refresh
   ```
   
3. Implement state machine
   - **State 1:** Normal operation (show nearby friends)
   - **State 2:** (Future) Show met list
   - **State 3:** (Future) Settings/config
   
4. Add power management
   - Use light sleep when idle
   - Wake on button press or BLE event
   
5. Error handling
   - Handle BLE stack failures gracefully
   - Display errors on screen
   - Implement watchdog timer

**Files to Modify:**
- `main/main.c` (replace placeholder loop)

**Task Structure:**
```c
void ble_task(void *param) {
    while (1) {
        // BLE operations run in NimBLE callbacks
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void ui_task(void *param) {
    while (1) {
        ui_refresh();  // Update display
        vTaskDelay(pdMS_TO_TICKS(500));  // 2 FPS
    }
}

void button_task(void *param) {
    button_event_t evt;
    while (1) {
        if (xQueueReceive(button_queue, &evt, portMAX_DELAY)) {
            handle_button_event(evt);
        }
    }
}
```

**Testing:**
- Test all features together
- Verify task synchronization
- Test under load (many nearby friends)
- Check CPU and memory usage

---

### Phase 7: Testing and Polish (Priority 4)

**Goal:** Ensure robust, user-friendly application

**Tasks:**
1. **Integration testing**
   - Test with multiple badges in proximity
   - Test rapid movement in/out of range
   - Test concurrent button presses
   - Test edge cases (empty list, full met list, etc.)
   
2. **Performance testing**
   - Measure BLE scan latency
   - Measure UI refresh rate
   - Check memory leaks
   - Profile CPU usage
   
3. **User experience**
   - Add sound feedback (buzzer) for button presses
   - Add animations or transitions
   - Improve signal strength visualization
   - Add "no friends nearby" message
   
4. **Error handling**
   - Handle BLE initialization failures
   - Handle display initialization failures  
   - Handle NVS corruption
   - Implement auto-recovery
   
5. **Documentation**
   - Update README with usage instructions
   - Add troubleshooting section
   - Document known issues/limitations
   
6. **Code cleanup**
   - Remove debug logs
   - Add comments
   - Follow ESP-IDF coding style
   - Run static analysis

**Testing Scenarios:**
- ✓ Single badge (should show empty list)
- ✓ Two badges (should see each other)
- ✓ 10+ badges in range
- ✓ Mark all as met, verify list empties
- ✓ Reboot after marking people as met
- ✓ Perform OTA update, verify met list survives
- ✓ Badge not configured (should show error)
- ✓ Button spam/rapid presses
- ✓ Stay in range for extended period (hours)

---

## Component Dependencies

```
┌─────────────────────────────────────────┐
│              app_main()                 │
└───────────┬─────────────────────────────┘
            │
            ├── NVS Init
            ├── Badge Config Check
            │
            ├── BLE Advertising ─────┐
            ├── BLE Scanning ────────┤
            │                        │
            ├── Display Driver       │
            ├── UI Manager ──────────┤
            │                        │
            ├── Button Handler ──────┤
            │                        │
            └── Met Tracker ─────────┘
                        │
                    Main Loop
```

## Build Order (Recommended)

1. **BLE Advertising** → Test with phone app
2. **BLE Scanning** → Test with multiple badges
3. **Display** → Test UI rendering
4. **Buttons** → Test navigation
5. **Met Tracker** → Test persistence
6. **Integration** → Combine all components
7. **Polish** → UX improvements

## Estimated Timeline

| Phase | Component | Effort | Priority |
|-------|-----------|--------|----------|
| 1 | BLE Advertising | 4-6 hours | High |
| 2 | BLE Scanning | 6-8 hours | High |
| 3 | Display & UI | 8-12 hours | Medium |
| 4 | Button Input | 3-4 hours | Medium |
| 5 | Met Tracker | 4-6 hours | Medium |
| 6 | Main Loop | 4-6 hours | Medium |
| 7 | Testing & Polish | 6-8 hours | Low |
| **Total** | | **35-50 hours** | |

## Key Design Decisions

### BLE Configuration
- **Service UUID:** Use custom 128-bit UUID or 16-bit from SIG
- **Advertisement Interval:** 100-500ms (balance power vs discovery speed)
- **Scan Window/Interval:** Match advertisement rate
- **Connection:** Not required (advertisement-only)

### Data Structures
- **Nearby Friends:** Fixed array of 20 entries, FIFO replacement
- **Met People:** NVS blob, up to 256 entries
- **Thread Safety:** Use mutexes for shared data

### Display Updates
- **Refresh Rate:** 2 FPS (500ms interval)
- **Partial Updates:** Clear only changed regions
- **Font:** Use ESP-IDF default or integrate lvgl for better UI

### Power Consumption
- **Active Mode:** BLE scanning + display on
- **Light Sleep:** When idle >5s (optional future feature)
- **Estimated Battery Life:** ~8-12 hours continuous use

## Future Enhancements (Post-MVP)

- [ ] Add friend profile pictures (stored in bootloader)
- [ ] Implement friend messages/chat
- [ ] Add QR code for quick friend exchange
- [ ] Track proximity time (how long near each person)
- [ ] Leaderboard: who met the most people
- [ ] Export met list to cloud/phone
- [ ] Group detection (clusters of friends)
- [ ] Privacy mode (stop broadcasting)
- [ ] Custom avatar/emoji selection
- [ ] Integration with event check-in system

## Notes

- **OTA Compatibility:** Met list stored in `user_data` partition survives OTA
- **Bootloader Dependency:** Requires BYUI-Namebadge-OTA bootloader for nickname config
- **Flash Usage:** Target <960 KB to fit in OTA partition
- **Memory:** ESP32-S3 has sufficient RAM for this application
- **BLE Stack:** Use NimBLE (lighter than Bluedroid)

---

**Last Updated:** 2026-04-30  
**Author:** GitHub Copilot  
**Version:** 1.0
