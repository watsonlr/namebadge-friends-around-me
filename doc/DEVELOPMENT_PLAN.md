# Development Plan - Friends Around Me

## Current Status

The project is in a functional integration state, not an early scaffold state.

### Implemented

- NVS initialization for default and user_data partitions
- Nickname read from user_data/badge_cfg nick key
- Fallback nickname using local MAC tail
- NimBLE advertising and scanning running together
- Custom manufacturer payload parsing and filtering
- Nearby-friends table with stale-entry cleanup
- Bilateral MEET handshake logic
- Display driver and UI rendering loop
- Button ISR + queue + click/long-press event generation
- LED signaling tied to incoming request states

### Partially Implemented or Deferred

- FIND request flow exists in protocol and UI, but no dedicated user control path
- Left, A, and B button actions are placeholders in main handler
- Meet persistence is intentionally not retained across reboot in current code

## Next Milestones

### Milestone 1: Decide and Implement Met Persistence

Goal: choose one product behavior and make code/docs consistent.

Option A (session-only):
- Keep current in-memory behavior.
- Remove stale comments and old persistence assumptions in docs.

Option B (persistent):
- Stop erasing met_count/met_list in met_tracker_init.
- Add load/save paths with schema versioning.
- Provide a user-triggered clear action.

Acceptance:
- Behavior remains consistent across reboot according to selected option.
- README and feature docs explicitly match behavior.

### Milestone 2: Complete Button UX

Goal: make all six buttons meaningful and documented.

Tasks:
- Left: toggle list view (to meet vs met) or back action.
- A long-press: clear met list with confirmation.
- B: optional mode toggle (for example, send FIND instead of MEET).
- Wire long-press behavior where appropriate.

Acceptance:
- Every button has deterministic behavior and on-screen feedback.

### Milestone 3: Handshake and Presence Robustness

Goal: reduce edge-case inconsistencies during radio churn.

Tasks:
- Add cooldown or duplicate suppression for repeated meet events.
- Improve logging around request transitions.
- Validate MAC-tail byte-order assumptions with on-device traces.
- Add explicit tests for simultaneous button press on two badges.

Acceptance:
- No duplicate met increments for a single bilateral handshake.
- Stable behavior with intermittent RSSI fluctuations.

### Milestone 4: Validation Matrix

Goal: repeatable hardware verification before releases.

Test matrix:
- 2 badges: baseline meet flow
- 3-6 badges: list churn and selection stability
- Out-of-range return within timeout window
- Missing nickname fallback behavior
- OTA-installed app across multiple boot cycles

Artifacts:
- Record expected serial log signatures per scenario.
- Keep a concise regression checklist in this repository.

## Suggested Work Order

1. Lock persistence decision (Milestone 1)
2. Complete button UX (Milestone 2)
3. Harden handshake/presence edges (Milestone 3)
4. Run and document validation matrix (Milestone 4)

## Notes

- Keep documentation aligned with code after each milestone.
- Prefer hardware-tested increments over large refactors due to BLE/UI timing coupling.
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
