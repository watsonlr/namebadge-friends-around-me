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
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"

static const char *TAG = "FRIENDS_APP";

/* NVS Configuration (from bootloader) */
#define WIFI_CONFIG_NVS_PARTITION  "user_data"
#define WIFI_CONFIG_NVS_NAMESPACE  "badge_cfg"
#define WIFI_CONFIG_NVS_KEY_NICK   "nick"

/* NVS Configuration (this app) */
#define FRIENDS_NVS_NAMESPACE  "friends_app"
#define FRIENDS_NVS_KEY_MET    "met_list"
#define FRIENDS_NVS_KEY_COUNT  "met_count"

/* Maximum people we can track as met */
#define MAX_MET_PEOPLE 256

/**
 * @brief Check if badge has been configured with a nickname via bootloader
 * @return true if configured, false otherwise
 */
static bool is_badge_configured(void)
{
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
 * @brief Display error message and hang
 * Used when badge is not configured
 */
static void display_config_error(void)
{
    ESP_LOGE(TAG, "===========================================");
    ESP_LOGE(TAG, "        BADGE NOT CONFIGURED!");
    ESP_LOGE(TAG, "===========================================");
    ESP_LOGE(TAG, "");
    ESP_LOGE(TAG, "This app requires configuration via the");
    ESP_LOGE(TAG, "BYUI eBadge bootloader.");
    ESP_LOGE(TAG, "");
    ESP_LOGE(TAG, "To configure:");
    ESP_LOGE(TAG, "  1. Press RESET button");
    ESP_LOGE(TAG, "  2. Within 500ms, press and hold BOOT");
    ESP_LOGE(TAG, "  3. Follow bootloader setup wizard");
    ESP_LOGE(TAG, "  4. Enter your nickname");
    ESP_LOGE(TAG, "");
    ESP_LOGE(TAG, "===========================================");
    
    // TODO: Display on screen when display component is ready
    
    // Hang here - user must enter bootloader
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
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
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(WIFI_CONFIG_NVS_PARTITION,
                                           FRIENDS_NVS_NAMESPACE,
                                           NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open friends_app namespace: %s", esp_err_to_name(err));
        return err;
    }
    
    // Check if met_count exists, create if not
    uint16_t count = 0;
    err = nvs_get_u16(h, FRIENDS_NVS_KEY_COUNT, &count);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // First run, initialize to 0
        ESP_LOGI(TAG, "First run, initializing met people list");
        nvs_set_u16(h, FRIENDS_NVS_KEY_COUNT, 0);
        nvs_commit(h);
        count = 0;
    }
    
    nvs_close(h);
    
    ESP_LOGI(TAG, "Met people tracking initialized, count: %u", count);
    return ESP_OK;
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
    
    // Check if badge is configured
    if (!is_badge_configured()) {
        display_config_error();
        // Never returns
    }
    
    // Get nickname
    char nickname[33] = {0};
    esp_err_t err = get_badge_nickname(nickname, sizeof(nickname));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read nickname: %s", esp_err_to_name(err));
        display_config_error();
    }
    
    ESP_LOGI(TAG, "Badge nickname: %s", nickname);
    
    // Initialize met people tracking
    ESP_ERROR_CHECK(init_met_people());
    
    // TODO: Initialize BLE component
    ESP_LOGI(TAG, "TODO: Initialize BLE broadcasting and scanning");
    
    // TODO: Initialize display component
    ESP_LOGI(TAG, "TODO: Initialize display driver");
    
    // TODO: Initialize button component
    ESP_LOGI(TAG, "TODO: Initialize button handlers");
    
    // TODO: Start main application loop
    ESP_LOGI(TAG, "Application initialized successfully");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "===========================================");
    
    // Main loop (placeholder)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "App running... (components not yet implemented)");
    }
}
