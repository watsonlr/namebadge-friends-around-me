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

/* Manufacturer ID (unassigned IEEE range) */
#define BLE_NAMEBADGE_MANUFACTURER_ID   0xFFFF

/* 4-byte magic prefix that follows the company ID inside manufacturer data,
 * so we can distinguish our beacons from other devices that also use 0xFFFF. */
#define BLE_NAMEBADGE_MAGIC             "BADG"
#define BLE_NAMEBADGE_MAGIC_LEN         4

/* Kind tag (1 byte) and 2-byte target that follow the magic. */
#define BLE_TARGET_NONE                 0  /* no outstanding request */
#define BLE_TARGET_MEET                 1  /* I want to meet this badge */
#define BLE_TARGET_FIND                 2  /* I want this badge to flash so I can find them */

#define BLE_NAMEBADGE_TARGET_KIND_LEN   1
#define BLE_NAMEBADGE_TARGET_LEN        2

/* Header inside manufacturer data: company ID + magic + kind + target. */
#define BLE_NAMEBADGE_MFG_HDR_LEN       (2 + BLE_NAMEBADGE_MAGIC_LEN + \
                                         BLE_NAMEBADGE_TARGET_KIND_LEN + \
                                         BLE_NAMEBADGE_TARGET_LEN)

/* Maximum nickname length the advertising payload can carry.
 * Legacy adv PDU = 31 bytes total. We use:
 *   3 bytes for Flags (T+L+V), and
 *   (2 bytes T+L) + BLE_NAMEBADGE_MFG_HDR_LEN + nickname for mfg data.
 * → 31 - 3 - 2 - 9 = 17 bytes available for the nickname. */
#define BLE_ADV_MAX_NICKNAME_LEN        17

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

/**
 * @brief Set the outgoing target broadcast in our advertisement.
 *
 * Restarts advertising so the new target appears in the next adv interval.
 * Pass kind=BLE_TARGET_NONE to clear.
 *
 * @param kind BLE_TARGET_NONE/MEET/FIND
 * @param b0   first target byte (esp_read_mac order; mac[4])
 * @param b1   second target byte (mac[5])
 */
void ble_advertising_set_target(uint8_t kind, uint8_t b0, uint8_t b1);

/**
 * @brief Read the current outgoing target.
 *
 * @param kind output kind (BLE_TARGET_NONE if no request)
 * @param out  two-byte target output (mac[4], mac[5])
 */
void ble_advertising_get_target(uint8_t *kind, uint8_t out[2]);

/**
 * @brief Auto-clear an outgoing FIND request once it has been broadcasting
 * for the built-in send window. Safe to call frequently from any task.
 */
void ble_advertising_check_target_timeout(void);

#endif /* BLE_ADVERTISING_H */
