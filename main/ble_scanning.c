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
#include "esp_mac.h"
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
static ble_scan_meet_cb_t   meet_callback   = NULL;
static ble_scan_find_cb_t   find_callback   = NULL;

/* Scan parameters */
#define BLE_SCAN_INTERVAL_MS 100  /* How often to scan (100 ms) */
#define BLE_SCAN_WINDOW_MS   50   /* How long each scan lasts (50 ms) */

/* Magic prefix that follows the company ID inside our mfg data. */
static const char namebadge_magic[BLE_NAMEBADGE_MAGIC_LEN] = BLE_NAMEBADGE_MAGIC;

/* Last two bytes of our own BD address (esp_read_mac order: mac[4], mac[5]).
 * Cached at scanning init so we can detect adverts that target us. */
static uint8_t my_mac_tail[2] = {0, 0};

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
 * @brief Check if mfg data is one of our beacons (company ID + magic prefix).
 */
static bool is_namebadge_mfg(const struct ble_hs_adv_fields *fields)
{
    if (fields->mfg_data == NULL ||
        fields->mfg_data_len < BLE_NAMEBADGE_MFG_HDR_LEN) {
        return false;
    }
    uint16_t company_id = fields->mfg_data[0] | (fields->mfg_data[1] << 8);
    if (company_id != BLE_NAMEBADGE_MANUFACTURER_ID) {
        return false;
    }
    return memcmp(&fields->mfg_data[2], namebadge_magic, BLE_NAMEBADGE_MAGIC_LEN) == 0;
}

/**
 * @brief Extract nickname from our mfg data (bytes after company ID + magic).
 */
static bool extract_nickname(const struct ble_hs_adv_fields *fields, char *nickname, size_t max_len)
{
    size_t nick_off = BLE_NAMEBADGE_MFG_HDR_LEN;
    if (fields->mfg_data_len <= nick_off) {
        return false;
    }
    size_t len = fields->mfg_data_len - nick_off;
    if (len > max_len - 1) {
        len = max_len - 1;
    }
    memcpy(nickname, &fields->mfg_data[nick_off], len);
    nickname[len] = '\0';
    return true;
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
        if (!is_namebadge_mfg(&fields)) {
            return 0;
        }

        /* Extract nickname */
        char nickname[MAX_NICKNAME_LEN + 1];
        if (!extract_nickname(&fields, nickname, sizeof(nickname))) {
            ESP_LOGW(TAG, "Namebadge found but no nickname");
            return 0;
        }

        /* Decode the kind+target they're advertising. Layout in mfg_data:
         *   [company:2][magic:4][kind:1][target:2][nickname...] */
        uint8_t their_kind     = fields.mfg_data[2 + BLE_NAMEBADGE_MAGIC_LEN];
        const uint8_t *their_target = &fields.mfg_data[2 + BLE_NAMEBADGE_MAGIC_LEN + 1];
        bool target_is_me = (their_target[0] == my_mac_tail[0] &&
                             their_target[1] == my_mac_tail[1] &&
                             (my_mac_tail[0] || my_mac_tail[1]));
        bool they_request_me = target_is_me && (their_kind == BLE_TARGET_MEET);
        bool they_find_me    = target_is_me && (their_kind == BLE_TARGET_FIND);

        /* Are we currently MEET-targeting this advertiser?
         * NimBLE stores BD addresses LSB-first in addr.val[], so the bytes
         * that match mac[4] and mac[5] from esp_read_mac are val[1] and val[0]. */
        uint8_t my_kind;
        uint8_t my_outgoing[2];
        ble_advertising_get_target(&my_kind, my_outgoing);
        bool i_meet_target_them = (my_kind == BLE_TARGET_MEET &&
                                   my_outgoing[0] == event->disc.addr.val[1] &&
                                   my_outgoing[1] == event->disc.addr.val[0] &&
                                   (my_outgoing[0] || my_outgoing[1]));

        /* Update or add to nearby friends list */
        if (xSemaphoreTake(friends_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            int idx = find_friend_by_addr(event->disc.addr.val);
            bool list_changed = false;
            bool just_finded = false;

            if (idx >= 0) {
                /* Update existing entry — RSSI/last_seen don't trigger redraw. */
                nearby_friends[idx].rssi = event->disc.rssi;
                nearby_friends[idx].last_seen = esp_timer_get_time();

                if (strcmp(nearby_friends[idx].nickname, nickname) != 0) {
                    strncpy(nearby_friends[idx].nickname, nickname, MAX_NICKNAME_LEN);
                    nearby_friends[idx].nickname[MAX_NICKNAME_LEN] = '\0';
                    ESP_LOGI(TAG, "Updated nickname: %s (RSSI: %d)", nickname, event->disc.rssi);
                    list_changed = true;
                }

                /* Once a friend is met, suppress the "they're requesting me"
                 * flash — the peer's MEET target stays advertised for a
                 * couple of seconds after the handshake (post-bilateral
                 * grace) and we don't want the row pulsing yellow then. */
                bool effective_request_me = they_request_me &&
                                            !nearby_friends[idx].is_met;
                if (nearby_friends[idx].they_request_me != effective_request_me) {
                    nearby_friends[idx].they_request_me = effective_request_me;
                    list_changed = true;
                }
                if (nearby_friends[idx].they_find_me != they_find_me) {
                    if (they_find_me) just_finded = true;  /* rising edge */
                    nearby_friends[idx].they_find_me = they_find_me;
                    list_changed = true;
                }
            } else {
                idx = find_empty_slot();
                if (idx >= 0) {
                    nearby_friends[idx].is_active = true;
                    nearby_friends[idx].rssi = event->disc.rssi;
                    nearby_friends[idx].last_seen = esp_timer_get_time();
                    nearby_friends[idx].is_met = met_tracker_is_met(nickname);
                    nearby_friends[idx].they_request_me = they_request_me;
                    nearby_friends[idx].they_find_me = they_find_me;
                    if (they_find_me) just_finded = true;
                    memcpy(nearby_friends[idx].addr, event->disc.addr.val, 6);
                    strncpy(nearby_friends[idx].nickname, nickname, MAX_NICKNAME_LEN);
                    nearby_friends[idx].nickname[MAX_NICKNAME_LEN] = '\0';
                    list_changed = true;

                    if (nearby_friends[idx].is_met) {
                        ESP_LOGI(TAG, "Found friend (already met): %s (RSSI: %d)", nickname, event->disc.rssi);
                    } else {
                        ESP_LOGI(TAG, "Found new friend: %s (RSSI: %d)", nickname, event->disc.rssi);
                    }
                } else {
                    ESP_LOGW(TAG, "Nearby friends list full, ignoring: %s", nickname);
                }
            }

            /* Bilateral handshake → both have agreed to meet. Mark met
             * locally, then schedule (rather than do) the target clear:
             * the other side may not have seen our MEET yet, so we keep
             * advertising it for a short grace period so they bilateral too. */
            bool just_met = false;
            if (idx >= 0 && i_meet_target_them && they_request_me &&
                !nearby_friends[idx].is_met) {
                ESP_LOGI(TAG, "Mutual meet with %s — marked as met", nickname);
                met_tracker_add(nickname);
                nearby_friends[idx].is_met = true;
                nearby_friends[idx].they_request_me = false;
                ble_advertising_clear_target_after(2LL * 1000 * 1000);
                list_changed = true;
                just_met = true;
            }

            xSemaphoreGive(friends_mutex);

            /* Only notify when the displayed list actually changed. */
            if (list_changed && update_callback != NULL) {
                update_callback();
            }
            if (just_met && meet_callback != NULL) {
                meet_callback(nickname);
            }
            if (just_finded && find_callback != NULL) {
                find_callback(nickname);
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

    /* Cache the last two bytes of our BD address so per-event compares
     * against the meet-target field can be done without re-querying. */
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    my_mac_tail[0] = mac[4];
    my_mac_tail[1] = mac[5];
    ESP_LOGI(TAG, "My MAC tail: %02X:%02X", my_mac_tail[0], my_mac_tail[1]);

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

void ble_scanning_register_meet_callback(ble_scan_meet_cb_t callback)
{
    meet_callback = callback;
    ESP_LOGI(TAG, "Meet callback %s", callback ? "registered" : "unregistered");
}

void ble_scanning_register_find_callback(ble_scan_find_cb_t callback)
{
    find_callback = callback;
    ESP_LOGI(TAG, "Find callback %s", callback ? "registered" : "unregistered");
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
