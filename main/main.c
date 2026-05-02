/**
 * @file main.c
 * @brief Friends Around Me - BLE Proximity Application for BYUI eBadge
 *
 * This OTA application enables namebadges to discover nearby friends via BLE
 * and track who has been met at events.
 *
 * Architecture:
 * - Reads user nickname from bootloader's user_data NVS partition
 * - Broadcasts nickname via BLE advertisements
 * - Scans for other namebadges and displays nearby friends
 * - Allows users to "check off" met people (stored in NVS)
 * - Met people are filtered from future displays
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_partition.h"
#include "ble_advertising.h"
#include "ble_scanning.h"
#include "ui.h"
#include "buttons.h"
#include "met_tracker.h"
#include "leds.h"

static const char *TAG = "FRIENDS_APP";

/* NVS Configuration (from bootloader) */
#define WIFI_CONFIG_NVS_PARTITION  "user_data"
#define WIFI_CONFIG_NVS_NAMESPACE  "badge_cfg"
#define WIFI_CONFIG_NVS_KEY_NICK   "nick"

/**
 * @brief Get the configured badge nickname
 * @param out Output buffer
 * @param outlen Buffer size
 * @return ESP_OK on success
 */
static esp_err_t get_badge_nickname(char *out, size_t outlen)
{
    if (!out || outlen == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    out[0] = '\0';
    
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(WIFI_CONFIG_NVS_PARTITION,
                                           WIFI_CONFIG_NVS_NAMESPACE,
                                           NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    
    size_t len = outlen;
    err = nvs_get_str(h, WIFI_CONFIG_NVS_KEY_NICK, out, &len);
    nvs_close(h);
    
    return err;
}

/**
 * @brief Initialize NVS partitions
 * @return ESP_OK on success
 */
static esp_err_t init_nvs(void)
{
    // Initialize default NVS partition (required by ESP-IDF)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || 
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize user_data partition (created by bootloader)
    // This partition is NEVER erased by OTA updates
    ret = nvs_flash_init_partition(WIFI_CONFIG_NVS_PARTITION);
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || 
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "user_data partition needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase_partition(WIFI_CONFIG_NVS_PARTITION));
        ret = nvs_flash_init_partition(WIFI_CONFIG_NVS_PARTITION);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init user_data partition: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "NVS initialized successfully");
    return ESP_OK;
}

/**
 * @brief Initialize met people tracking
 * Creates NVS namespace if it doesn't exist
 * @return ESP_OK on success
 */
static esp_err_t init_met_people(void)
{
    /* Met tracker initialization now handled by met_tracker component */
    return met_tracker_init();
}

/**
 * @brief Cleanup task - removes stale entries from nearby friends list
 */
static void cleanup_task(void *param)
{
    ESP_LOGI(TAG, "Cleanup task started");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));  /* Run every 2 seconds */
        ble_scanning_cleanup_stale_entries();
    }
}

/**
 * @brief UI refresh task - updates the display periodically
 */
static void ui_task(void *param)
{
    ESP_LOGI(TAG, "UI task started");
    
    while (1) {
        ui_refresh();
        vTaskDelay(pdMS_TO_TICKS(500));  /* Update display at 2 Hz */
    }
}

/* Set when we press Right while the selected friend is NOT yet requesting
 * us — i.e., we're the side initiating the meet, not accepting one. */
static bool g_i_initiated_meet = false;

/**
 * @brief Callback for nearby friends list updates
 */
static void on_friends_update(void)
{
    int count = 0;
    const nearby_friend_t *friends = ble_scanning_get_nearby_friends(&count);

    ESP_LOGI(TAG, "Nearby friends updated: %d active", count);

    for (int i = 0; i < MAX_NEARBY_FRIENDS; i++) {
        if (friends[i].is_active && !friends[i].is_met) {
            ESP_LOGI(TAG, "  - %s (RSSI: %d dBm)", friends[i].nickname, friends[i].rssi);
        }
    }

    ui_force_redraw();
}

/**
 * @brief Callback fired when a meet handshake completes both ways.
 * Just queues the celebration overlay — UI task does the real drawing.
 */
static void on_friend_met(const char *nickname)
{
    ESP_LOGI(TAG, "Now friends with %s (initiated=%d)", nickname, g_i_initiated_meet);
    ui_show_friend_announcement(nickname, g_i_initiated_meet);
    g_i_initiated_meet = false;
}

/**
 * @brief Callback fired when an already-met friend pings us via FIND.
 * Display "Hello from <them>" while their LEDs/row keep flashing green.
 */
static void on_friend_find(const char *nickname)
{
    ESP_LOGI(TAG, "Find request from %s", nickname);
    ui_show_hello(nickname);
}

/**
 * @brief Button event callback
 */
static void on_button_event(button_event_t event)
{
    /* Only process click events for now */
    if (event.event != BUTTON_EVENT_CLICK) {
        return;
    }
    
    switch (event.button) {
    case BUTTON_UP:
        ESP_LOGI(TAG, "Button UP clicked - move selection up");
        ui_select_up();
        break;
        
    case BUTTON_DOWN:
        ESP_LOGI(TAG, "Button DOWN clicked - move selection down");
        ui_select_down();
        break;
        
    case BUTTON_RIGHT:
        /* In TO_MEET view: meet request (handshake → marks both met).
         * In MET view: one-way "find me" → flashes their LEDs green. */
        {
            uint8_t addr[6];
            if (ui_get_selected_addr(addr) != ESP_OK) {
                ESP_LOGI(TAG, "Button RIGHT - no friend selected");
                break;
            }
            uint8_t b0 = addr[1], b1 = addr[0];
            if (ui_in_met_view()) {
                ble_advertising_set_target(BLE_TARGET_FIND, b0, b1);
                ESP_LOGI(TAG, "Button RIGHT - find request → %02X:%02X", b0, b1);
            } else {
                g_i_initiated_meet = !ui_selected_is_requesting_me();
                ble_advertising_set_target(BLE_TARGET_MEET, b0, b1);
                ESP_LOGI(TAG, "Button RIGHT - meet request → %02X:%02X (initiated=%d)",
                         b0, b1, g_i_initiated_meet);
            }
            ui_force_redraw();
        }
        break;

    case BUTTON_LEFT:
        ESP_LOGI(TAG, "Button LEFT clicked - toggle view");
        ui_toggle_view();
        break;
        
    case BUTTON_A:
        /* Future: reset met list */
        ESP_LOGI(TAG, "Button A clicked (not implemented)");
        break;
        
    case BUTTON_B:
        /* Future: toggle modes */
        ESP_LOGI(TAG, "Button B clicked (not implemented)");
        break;
        
    default:
        break;
    }
}

/* Set true once the user clicks BUTTON_A on the splash. Until then we
 * intentionally don't init/start BLE so the badge isn't broadcasting. */
static volatile bool g_app_started = false;

/* Splash-only button handler — only BUTTON_A's click matters here. */
static void splash_button_event(button_event_t event)
{
    if (event.event == BUTTON_EVENT_CLICK && event.button == BUTTON_A) {
        g_app_started = true;
    }
}

/**
 * @brief Application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  Friends Around Me - BYUI eBadge App");
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "");

    // Initialize NVS
    ESP_ERROR_CHECK(init_nvs());

    // Get nickname (or fall back to a default if badge isn't configured)
    char nickname[33] = {0};
    esp_err_t err = get_badge_nickname(nickname, sizeof(nickname));
    if (err != ESP_OK || nickname[0] == '\0') {
        ESP_LOGW(TAG, "Badge not configured (%s) - using MAC-based nickname",
                 err == ESP_OK ? "empty nick" : esp_err_to_name(err));
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_BT);
        snprintf(nickname, sizeof(nickname), "badge-%02X%02X", mac[4], mac[5]);
    }
    ESP_LOGI(TAG, "Badge nickname: %s", nickname);

    if (!leds_init()) {
        ESP_LOGE(TAG, "leds_init failed - continuing without LEDs");
    } else {
        leds_clear();
        leds_show();
    }

    ESP_ERROR_CHECK(init_met_people());

    // Initialize display so we can paint the splash.
    ESP_ERROR_CHECK(ui_init());
    ui_set_nickname(nickname);
    ui_show_splash();

    // Buttons online so we can detect the BUTTON_A start press.
    ESP_ERROR_CHECK(buttons_init());
    buttons_register_callback(splash_button_event);

    ESP_LOGI(TAG, "Splash up — waiting for BUTTON_A...");
    while (!g_app_started) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGI(TAG, "BUTTON_A pressed — starting BLE.");

    // Hand button events back to the regular handler before BLE goes live.
    buttons_register_callback(on_button_event);

    // Initialize BLE advertising
    ESP_LOGI(TAG, "Initializing BLE advertising...");
    err = ble_advertising_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE advertising: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "Continuing without BLE...");
    } else {
        err = ble_advertising_start(nickname);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start BLE advertising: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "BLE advertising started successfully");
        }
    }

    // Initialize BLE scanning
    ESP_LOGI(TAG, "Initializing BLE scanning...");
    err = ble_scanning_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE scanning: %s", esp_err_to_name(err));
    } else {
        ble_scanning_register_update_callback(on_friends_update);
        ble_scanning_register_meet_callback(on_friend_met);
        ble_scanning_register_find_callback(on_friend_find);

        vTaskDelay(pdMS_TO_TICKS(500));  /* Small delay after advertising */
        err = ble_scanning_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start BLE scanning: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "BLE scanning started successfully");
        }
    }

    // Background tasks
    xTaskCreate(cleanup_task, "cleanup", 4096, NULL, 5, NULL);
    ui_force_redraw();   /* paint the friends UI over the splash */
    xTaskCreate(ui_task, "ui", 8192, NULL, 5, NULL);
    
    // Application initialized
    ESP_LOGI(TAG, "Application initialized successfully");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "===========================================");
    
    // Main loop
    ESP_LOGI(TAG, "Entering main loop...");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  /* Log status every 10 seconds */
        
        int nearby_count = 0;
        ble_scanning_get_nearby_friends(&nearby_count);
        int met_count = met_tracker_get_count();
        
        ESP_LOGI(TAG, "Status: ADV=%s SCAN=%s Nearby=%d Met=%d",
                 ble_advertising_is_active() ? "ON" : "OFF",
                 ble_scanning_is_active() ? "ON" : "OFF",
                 nearby_count,
                 met_count);
    }
}
