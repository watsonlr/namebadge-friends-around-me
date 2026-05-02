/**
 * @file ble_scanning.h
 * @brief BLE Scanning Component for Friends Around Me
 * 
 * Handles BLE scanning to detect nearby namebadges and extract their nicknames.
 * Maintains a list of nearby friends with RSSI-based proximity tracking.
 */

#ifndef BLE_SCANNING_H
#define BLE_SCANNING_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* Maximum number of nearby friends we can track simultaneously */
#define MAX_NEARBY_FRIENDS 20

/* Maximum nickname length */
#define MAX_NICKNAME_LEN 32

/* Timeout for removing stale entries (milliseconds) */
#define NEARBY_FRIEND_TIMEOUT_MS 5000

/**
 * @brief Structure representing a nearby friend
 */
typedef struct {
    char nickname[MAX_NICKNAME_LEN + 1];  /**< Friend's nickname */
    int8_t rssi;                          /**< Signal strength (dBm) */
    int64_t last_seen;                    /**< Last seen timestamp (microseconds) */
    uint8_t addr[6];                      /**< BLE address (NimBLE order: addr[0]=LSB) */
    bool is_met;                          /**< True if already met (filtered from display) */
    bool is_active;                       /**< True if slot is in use */
    bool they_request_me;                 /**< True if their adv targets my MAC w/ MEET kind */
    bool they_find_me;                    /**< True if their adv targets my MAC w/ FIND kind */
} nearby_friend_t;

/**
 * @brief Callback function type for nearby friends list updates
 *
 * Called whenever the nearby friends list changes (friend added, removed, or updated).
 * Use ble_scanning_get_nearby_friends() to retrieve the updated list.
 */
typedef void (*ble_scan_update_cb_t)(void);

/**
 * @brief Callback fired the moment a bilateral meet handshake completes.
 *
 * Receives the freshly-met friend's nickname. Invoked from the BLE host
 * task — keep handlers cheap (no SPI / display work).
 */
typedef void (*ble_scan_meet_cb_t)(const char *nickname);

/**
 * @brief Initialize BLE scanning
 * 
 * Initializes the BLE scanning subsystem. Must be called after ble_advertising_init()
 * and before ble_scanning_start().
 * 
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t ble_scanning_init(void);

/**
 * @brief Start BLE scanning for nearby namebadges
 * 
 * Begins continuous scanning for BLE advertisements from other namebadges.
 * Filters by the custom namebadge service UUID and extracts nicknames.
 * 
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t ble_scanning_start(void);

/**
 * @brief Stop BLE scanning
 * 
 * Stops active BLE scanning. The nearby friends list is preserved.
 * Scanning can be restarted with ble_scanning_start().
 * 
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t ble_scanning_stop(void);

/**
 * @brief Deinitialize BLE scanning and free resources
 * 
 * Stops scanning and clears the nearby friends list.
 * After calling this, ble_scanning_init() must be called again.
 * 
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t ble_scanning_deinit(void);

/**
 * @brief Check if BLE scanning is currently active
 * 
 * @return true if scanning is active, false otherwise
 */
bool ble_scanning_is_active(void);

/**
 * @brief Get the list of nearby friends
 * 
 * Returns a pointer to the internal nearby friends array.
 * The array contains MAX_NEARBY_FRIENDS entries. Check is_active
 * field to determine which entries are valid.
 * 
 * @param count Output: number of active (non-met) friends
 * @return Pointer to nearby friends array (read-only)
 */
const nearby_friend_t* ble_scanning_get_nearby_friends(int *count);

/**
 * @brief Mark a friend as met
 * 
 * Updates the friend's is_met flag. Met friends will be filtered
 * from the active friends count returned by ble_scanning_get_nearby_friends().
 * 
 * @param nickname Nickname of the friend to mark as met
 * @return ESP_OK if found and marked, ESP_ERR_NOT_FOUND if not in list
 */
esp_err_t ble_scanning_mark_as_met(const char *nickname);

/**
 * @brief Check if a friend is currently nearby
 * 
 * @param nickname Nickname to check
 * @return true if the friend is in the nearby list, false otherwise
 */
bool ble_scanning_is_friend_nearby(const char *nickname);

/**
 * @brief Register a callback for nearby friends list updates
 * 
 * The callback will be invoked whenever a friend is added, removed,
 * or their RSSI is updated. Useful for triggering UI refreshes.
 * 
 * @param callback Callback function (NULL to unregister)
 */
void ble_scanning_register_update_callback(ble_scan_update_cb_t callback);

/**
 * @brief Register a callback invoked when a bilateral meet completes.
 */
void ble_scanning_register_meet_callback(ble_scan_meet_cb_t callback);

/**
 * @brief Callback fired when an incoming FIND request arrives (rising edge).
 *
 * Receives the nickname of the friend who pressed Right on us in their
 * "Friends I've Met" view.  Invoked from the BLE host task — keep handlers
 * cheap (no SPI / display work).
 */
typedef void (*ble_scan_find_cb_t)(const char *nickname);
void ble_scanning_register_find_callback(ble_scan_find_cb_t callback);

/**
 * @brief Clear all nearby friends from the list
 * 
 * Removes all entries from the nearby friends list.
 * Useful for testing or manual refresh.
 */
void ble_scanning_clear_all(void);

/**
 * @brief Cleanup stale entries from the nearby friends list
 * 
 * Removes friends who haven't been seen within NEARBY_FRIEND_TIMEOUT_MS.
 * This is called automatically during scanning, but can be invoked manually.
 */
void ble_scanning_cleanup_stale_entries(void);

#endif /* BLE_SCANNING_H */
