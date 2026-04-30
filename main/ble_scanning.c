/**
 * @file ble_scanning.c
 * @brief BLE Scanning Implementation
 * 
 * Implements BLE scanning to detect nearby namebadges using the NimBLE stack.
 */

#include "ble_scanning.h"
#include "ble_advertising.h"
#include "met_tracker.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "esp_timer.h"

static const char *TAG = "BLE_SCAN";

/* Nearby friends tracking */
static nearby_friend_t nearby_friends[MAX_NEARBY_FRIENDS];
static SemaphoreHandle_t friends_mutex = NULL;
static bool is_scanning = false;
static ble_scan_update_cb_t update_callback = NULL;

/* Scan parameters */
#define BLE_SCAN_INTERVAL_MS 100  /* How often to scan (100 ms) */
#define BLE_SCAN_WINDOW_MS   50   /* How long each scan lasts (50 ms) */

/* Custom service UUID for namebadge (must match advertising) */
static const uint8_t namebadge_uuid128[16] = {BLE_NAMEBADGE_SERVICE_UUID_128};

/**
 * @brief Compare two BLE addresses
 */
static bool addr_equal(const uint8_t *addr1, const uint8_t *addr2)
{
    return memcmp(addr1, addr2, 6) == 0;
}

/**
 * @brief Find a friend in the nearby list by address
 * 
 * @return Index if found, -1 if not found
 */
static int find_friend_by_addr(const uint8_t *addr)
{
    for (int i = 0; i < MAX_NEARBY_FRIENDS; i++) {
        if (nearby_friends[i].is_active && addr_equal(nearby_friends[i].addr, addr)) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Find an empty slot in the nearby friends list
 * 
 * @return Index if found, -1 if list is full
 */
static int find_empty_slot(void)
{
    for (int i = 0; i < MAX_NEARBY_FRIENDS; i++) {
        if (!nearby_friends[i].is_active) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Extract nickname from advertisement data
 * 
 * Looks for the nickname in:
 * 1. Complete Local Name field
 * 2. Manufacturer data (after 2-byte company ID)
 * 
 * @param fields Advertisement fields
 * @param nickname Output buffer for nickname
 * @param max_len Maximum nickname length
 * @return true if nickname found, false otherwise
 */
static bool extract_nickname(const struct ble_hs_adv_fields *fields, char *nickname, size_t max_len)
{
    /* Try Complete Local Name first */
    if (fields->name != NULL && fields->name_len > 0) {
        size_t len = fields->name_len < max_len ? fields->name_len : max_len - 1;
        memcpy(nickname, fields->name, len);
        nickname[len] = '\0';
        return true;
    }
    
    /* Try manufacturer data (skip 2-byte company ID) */
    if (fields->mfg_data != NULL && fields->mfg_data_len > 2) {
        /* Check if it's our manufacturer ID */
        uint16_t company_id = fields->mfg_data[0] | (fields->mfg_data[1] << 8);
        if (company_id == BLE_NAMEBADGE_MANUFACTURER_ID) {
            size_t len = fields->mfg_data_len - 2;
            if (len > max_len - 1) {
                len = max_len - 1;
            }
            memcpy(nickname, &fields->mfg_data[2], len);
            nickname[len] = '\0';
            return true;
        }
    }
    
    return false;
}

/**
 * @brief Check if advertisement contains our namebadge service UUID
 */
static bool has_namebadge_service(const struct ble_hs_adv_fields *fields)
{
    /* Check 128-bit UUIDs */
    if (fields->uuids128 != NULL && fields->num_uuids128 > 0) {
        for (int i = 0; i < fields->num_uuids128; i++) {
            if (memcmp(fields->uuids128[i].value, namebadge_uuid128, 16) == 0) {
                return true;
            }
        }
    }
    
    return false;
}

/**
 * @brief BLE GAP event callback for scan results
 */
static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    struct ble_hs_adv_fields fields;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        /* Parse advertisement data */
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        if (rc != 0) {
            return 0;
        }

        /* Check if this is a namebadge advertisement */
        if (!has_namebadge_service(&fields)) {
            return 0;
        }

        /* Extract nickname */
        char nickname[MAX_NICKNAME_LEN + 1];
        if (!extract_nickname(&fields, nickname, sizeof(nickname))) {
            ESP_LOGW(TAG, "Namebadge found but no nickname");
            return 0;
        }

        /* Update or add to nearby friends list */
        if (xSemaphoreTake(friends_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            int idx = find_friend_by_addr(event->disc.addr.val);
            bool is_new = false;
            
            if (idx >= 0) {
                /* Update existing entry */
                nearby_friends[idx].rssi = event->disc.rssi;
                nearby_friends[idx].last_seen = esp_timer_get_time();
                
                /* Update nickname if changed */
                if (strcmp(nearby_friends[idx].nickname, nickname) != 0) {
                    strncpy(nearby_friends[idx].nickname, nickname, MAX_NICKNAME_LEN);
                    nearby_friends[idx].nickname[MAX_NICKNAME_LEN] = '\0';
                    ESP_LOGI(TAG, "Updated nickname: %s (RSSI: %d)", nickname, event->disc.rssi);
                }
            } else {
                /* Add new entry */
                idx = find_empty_slot();
                if (idx >= 0) {
                    nearby_friends[idx].is_active = true;
                    nearby_friends[idx].rssi = event->disc.rssi;
                    nearby_friends[idx].last_seen = esp_timer_get_time();
                    /* Check if already met */
                    nearby_friends[idx].is_met = met_tracker_is_met(nickname);
                    memcpy(nearby_friends[idx].addr, event->disc.addr.val, 6);
                    strncpy(nearby_friends[idx].nickname, nickname, MAX_NICKNAME_LEN);
                    nearby_friends[idx].nickname[MAX_NICKNAME_LEN] = '\0';
                    is_new = true;
                    
                    if (nearby_friends[idx].is_met) {
                        ESP_LOGI(TAG, "Found friend (already met): %s (RSSI: %d)", nickname, event->disc.rssi);
                    } else {
                        ESP_LOGI(TAG, "Found new friend: %s (RSSI: %d)", nickname, event->disc.rssi);
                    }
                } else {
                    ESP_LOGW(TAG, "Nearby friends list full, ignoring: %s", nickname);
                }
            }
            
            xSemaphoreGive(friends_mutex);
            
            /* Notify callback of update */
            if (update_callback != NULL && (is_new || idx >= 0)) {
                update_callback();
            }
        }
        
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "Scan complete, restarting...");
        /* Restart scanning */
        ble_scanning_start();
        return 0;

    default:
        return 0;
    }
}

/**
 * @brief Start the actual BLE scan
 */
static int start_scan(void)
{
    struct ble_gap_disc_params disc_params;
    
    /* Configure scan parameters */
    memset(&disc_params, 0, sizeof(disc_params));
    disc_params.filter_duplicates = 0;  /* Don't filter duplicates (we want RSSI updates) */
    disc_params.passive = 0;             /* Active scanning (send scan requests) */
    disc_params.itvl = BLE_SCAN_INTERVAL_MS * 1000 / 625;  /* Convert ms to 0.625ms units */
    disc_params.window = BLE_SCAN_WINDOW_MS * 1000 / 625;  /* Convert ms to 0.625ms units */
    disc_params.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;
    disc_params.limited = 0;             /* General discovery */
    
    /* Start scanning indefinitely */
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &disc_params,
                         ble_gap_event_cb, NULL);
    
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start scan, rc=%d", rc);
        return rc;
    }
    
    ESP_LOGI(TAG, "BLE scanning started");
    return 0;
}

esp_err_t ble_scanning_init(void)
{
    ESP_LOGI(TAG, "Initializing BLE scanning...");

    /* Create mutex for friends list */
    if (friends_mutex == NULL) {
        friends_mutex = xSemaphoreCreateMutex();
        if (friends_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_FAIL;
        }
    }

    /* Initialize nearby friends list */
    memset(nearby_friends, 0, sizeof(nearby_friends));
    for (int i = 0; i < MAX_NEARBY_FRIENDS; i++) {
        nearby_friends[i].is_active = false;
    }

    ESP_LOGI(TAG, "BLE scanning initialized successfully");
    return ESP_OK;
}

esp_err_t ble_scanning_start(void)
{
    if (is_scanning) {
        ESP_LOGW(TAG, "Already scanning");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting BLE scan...");

    /* Wait for BLE host to sync */
    if (!ble_hs_synced()) {
        ESP_LOGE(TAG, "BLE host not synced");
        return ESP_FAIL;
    }

    /* Start scanning */
    int rc = start_scan();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start scan, rc=%d", rc);
        return ESP_FAIL;
    }

    is_scanning = true;
    return ESP_OK;
}

esp_err_t ble_scanning_stop(void)
{
    if (!is_scanning) {
        ESP_LOGW(TAG, "Not currently scanning");
        return ESP_OK;
    }

    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Failed to stop scan, rc=%d", rc);
        return ESP_FAIL;
    }

    is_scanning = false;
    ESP_LOGI(TAG, "BLE scanning stopped");
    return ESP_OK;
}

esp_err_t ble_scanning_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing BLE scanning...");

    /* Stop scanning if active */
    if (is_scanning) {
        ble_scanning_stop();
    }

    /* Clear nearby friends list */
    if (xSemaphoreTake(friends_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memset(nearby_friends, 0, sizeof(nearby_friends));
        xSemaphoreGive(friends_mutex);
    }

    ESP_LOGI(TAG, "BLE scanning deinitialized");
    return ESP_OK;
}

bool ble_scanning_is_active(void)
{
    return is_scanning;
}

const nearby_friend_t* ble_scanning_get_nearby_friends(int *count)
{
    if (count == NULL) {
        return nearby_friends;
    }

    *count = 0;
    
    if (xSemaphoreTake(friends_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        /* Count active, non-met friends */
        for (int i = 0; i < MAX_NEARBY_FRIENDS; i++) {
            if (nearby_friends[i].is_active && !nearby_friends[i].is_met) {
                (*count)++;
            }
        }
        xSemaphoreGive(friends_mutex);
    }

    return nearby_friends;
}

esp_err_t ble_scanning_mark_as_met(const char *nickname)
{
    if (nickname == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_ERR_NOT_FOUND;

    if (xSemaphoreTake(friends_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < MAX_NEARBY_FRIENDS; i++) {
            if (nearby_friends[i].is_active && 
                strcmp(nearby_friends[i].nickname, nickname) == 0) {
                nearby_friends[i].is_met = true;
                ESP_LOGI(TAG, "Marked as met: %s", nickname);
                ret = ESP_OK;
                break;
            }
        }
        xSemaphoreGive(friends_mutex);
        
        if (ret == ESP_OK && update_callback != NULL) {
            update_callback();
        }
    }

    return ret;
}

bool ble_scanning_is_friend_nearby(const char *nickname)
{
    if (nickname == NULL) {
        return false;
    }

    bool found = false;

    if (xSemaphoreTake(friends_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < MAX_NEARBY_FRIENDS; i++) {
            if (nearby_friends[i].is_active && 
                strcmp(nearby_friends[i].nickname, nickname) == 0) {
                found = true;
                break;
            }
        }
        xSemaphoreGive(friends_mutex);
    }

    return found;
}

void ble_scanning_register_update_callback(ble_scan_update_cb_t callback)
{
    update_callback = callback;
    ESP_LOGI(TAG, "Update callback %s", callback ? "registered" : "unregistered");
}

void ble_scanning_clear_all(void)
{
    if (xSemaphoreTake(friends_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(nearby_friends, 0, sizeof(nearby_friends));
        for (int i = 0; i < MAX_NEARBY_FRIENDS; i++) {
            nearby_friends[i].is_active = false;
        }
        xSemaphoreGive(friends_mutex);
        
        ESP_LOGI(TAG, "Cleared all nearby friends");
        
        if (update_callback != NULL) {
            update_callback();
        }
    }
}

void ble_scanning_cleanup_stale_entries(void)
{
    int64_t now = esp_timer_get_time();
    int64_t timeout_us = NEARBY_FRIEND_TIMEOUT_MS * 1000LL;
    bool removed_any = false;

    if (xSemaphoreTake(friends_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < MAX_NEARBY_FRIENDS; i++) {
            if (nearby_friends[i].is_active) {
                if ((now - nearby_friends[i].last_seen) > timeout_us) {
                    ESP_LOGI(TAG, "Removing stale entry: %s", nearby_friends[i].nickname);
                    nearby_friends[i].is_active = false;
                    memset(&nearby_friends[i], 0, sizeof(nearby_friend_t));
                    removed_any = true;
                }
            }
        }
        xSemaphoreGive(friends_mutex);
        
        if (removed_any && update_callback != NULL) {
            update_callback();
        }
    }
}
