/**
 * @file ble_advertising.h
 * @brief BLE Advertising Component for Friends Around Me
 * 
 * Handles BLE advertisement of the user's nickname to nearby badges.
 * Uses NimBLE stack for lightweight BLE operations.
 */

#ifndef BLE_ADVERTISING_H
#define BLE_ADVERTISING_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Custom UUID for Friends Around Me namebadge service
 * 
 * This 128-bit UUID identifies our custom namebadge service.
 * Format: XXXXXXXX-0000-1000-8000-00805F9B34FB
 * Base: 0000XXXX-0000-1000-8000-00805F9B34FB (Bluetooth Base UUID)
 * Custom: We use a unique prefix for our application
 */
#define BLE_NAMEBADGE_SERVICE_UUID_128 \
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, \
    0x00, 0x10, 0x00, 0x00, 0x0F, 0x18, 0x00, 0x00

/* Manufacturer ID for custom manufacturer data (use unassigned range) */
#define BLE_NAMEBADGE_MANUFACTURER_ID   0xFFFF

/* Maximum nickname length in BLE advertisement */
#define BLE_ADV_MAX_NICKNAME_LEN        31

/**
 * @brief Initialize BLE advertising
 * 
 * Initializes the NimBLE stack and prepares for advertising.
 * Must be called before ble_advertising_start().
 * 
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t ble_advertising_init(void);

/**
 * @brief Start BLE advertising with the given nickname
 * 
 * Configures advertisement data with the user's nickname and begins
 * continuous BLE advertising. The nickname is embedded in the manufacturer
 * data field of the advertisement packet.
 * 
 * Advertisement structure:
 *   - Flags (0x06 = General Discoverable + BR/EDR Not Supported)
 *   - Complete Local Name (device name)
 *   - Service UUID (128-bit namebadge service UUID)
 *   - Manufacturer Data (Company ID + nickname)
 * 
 * @param nickname User's nickname to advertise (max 31 chars)
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t ble_advertising_start(const char *nickname);

/**
 * @brief Stop BLE advertising
 * 
 * Stops active BLE advertising. Can be restarted with ble_advertising_start().
 * 
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t ble_advertising_stop(void);

/**
 * @brief Deinitialize BLE advertising and free resources
 * 
 * Stops advertising and deinitializes the NimBLE stack.
 * After calling this, ble_advertising_init() must be called again
 * before advertising can resume.
 * 
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t ble_advertising_deinit(void);

/**
 * @brief Check if BLE advertising is currently active
 * 
 * @return true if advertising is active, false otherwise
 */
bool ble_advertising_is_active(void);

#endif /* BLE_ADVERTISING_H */
