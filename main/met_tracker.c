/**
 * @file met_tracker.c
 * @brief Met People Tracking Implementation
 */

#include "met_tracker.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "MET_TRACKER";

/* NVS Configuration */
#define NVS_PARTITION   "user_data"      /* Survives OTA updates */
#define NVS_NAMESPACE   "friends_app"
#define NVS_KEY_COUNT   "met_count"
#define NVS_KEY_LIST    "met_list"

/* Met list storage */
static char met_list[MAX_MET_PEOPLE][MET_TRACKER_MAX_NICKNAME_LEN + 1];
static uint16_t met_count = 0;
static SemaphoreHandle_t met_mutex = NULL;

esp_err_t met_tracker_init(void)
{
    ESP_LOGI(TAG, "Initializing met tracker (in-memory only)...");

    if (met_mutex == NULL) {
        met_mutex = xSemaphoreCreateMutex();
        if (met_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_FAIL;
        }
    }

    memset(met_list, 0, sizeof(met_list));
    met_count = 0;

    /* Wipe any met list that older builds may have left in NVS so the badge
     * really does start empty on every reset. Best effort — failure is fine. */
    nvs_handle_t handle;
    if (nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE,
                                NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, NVS_KEY_COUNT);
        nvs_erase_key(handle, NVS_KEY_LIST);
        nvs_commit(handle);
        nvs_close(handle);
    }

    ESP_LOGI(TAG, "Met tracker initialized with %u people", met_count);
    return ESP_OK;
}

esp_err_t met_tracker_add(const char *nickname)
{
    if (nickname == NULL || nickname[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(met_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex");
        return ESP_FAIL;
    }
    
    /* Check if already in list */
    for (uint16_t i = 0; i < met_count; i++) {
        if (strcmp(met_list[i], nickname) == 0) {
            xSemaphoreGive(met_mutex);
            ESP_LOGI(TAG, "%s already in met list", nickname);
            return ESP_OK;
        }
    }
    
    /* Check if list is full */
    if (met_count >= MAX_MET_PEOPLE) {
        xSemaphoreGive(met_mutex);
        ESP_LOGE(TAG, "Met list is full (%d people)", MAX_MET_PEOPLE);
        return ESP_ERR_NO_MEM;
    }
    
    /* Add to list */
    strncpy(met_list[met_count], nickname, MET_TRACKER_MAX_NICKNAME_LEN);
    met_list[met_count][MET_TRACKER_MAX_NICKNAME_LEN] = '\0';
    met_count++;
    
    ESP_LOGI(TAG, "Added %s to met list (total: %u)", nickname, met_count);

    xSemaphoreGive(met_mutex);
    return ESP_OK;
}

bool met_tracker_is_met(const char *nickname)
{
    if (nickname == NULL) {
        return false;
    }
    
    bool found = false;
    
    if (xSemaphoreTake(met_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (uint16_t i = 0; i < met_count; i++) {
            if (strcmp(met_list[i], nickname) == 0) {
                found = true;
                break;
            }
        }
        xSemaphoreGive(met_mutex);
    }
    
    return found;
}

uint16_t met_tracker_get_count(void)
{
    uint16_t count = 0;
    
    if (xSemaphoreTake(met_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        count = met_count;
        xSemaphoreGive(met_mutex);
    }
    
    return count;
}

esp_err_t met_tracker_get_nickname(uint16_t index, char *nickname, size_t max_len)
{
    if (nickname == NULL || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_ERR_INVALID_ARG;
    
    if (xSemaphoreTake(met_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (index < met_count) {
            strncpy(nickname, met_list[index], max_len - 1);
            nickname[max_len - 1] = '\0';
            ret = ESP_OK;
        }
        xSemaphoreGive(met_mutex);
    }
    
    return ret;
}

esp_err_t met_tracker_remove(const char *nickname)
{
    if (nickname == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(met_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_FAIL;
    }
    
    /* Find in list */
    int found_idx = -1;
    for (uint16_t i = 0; i < met_count; i++) {
        if (strcmp(met_list[i], nickname) == 0) {
            found_idx = i;
            break;
        }
    }
    
    if (found_idx < 0) {
        xSemaphoreGive(met_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    /* Shift remaining entries */
    for (uint16_t i = found_idx; i < met_count - 1; i++) {
        strcpy(met_list[i], met_list[i + 1]);
    }
    met_count--;
    
    ESP_LOGI(TAG, "Removed %s from met list (total: %u)", nickname, met_count);

    xSemaphoreGive(met_mutex);
    return ESP_OK;
}

esp_err_t met_tracker_clear_all(void)
{
    if (xSemaphoreTake(met_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_FAIL;
    }
    
    memset(met_list, 0, sizeof(met_list));
    met_count = 0;
    
    ESP_LOGI(TAG, "Cleared all met people");

    xSemaphoreGive(met_mutex);
    return ESP_OK;
}

void met_tracker_export(met_tracker_export_cb_t callback)
{
    if (callback == NULL) {
        return;
    }
    
    if (xSemaphoreTake(met_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (uint16_t i = 0; i < met_count; i++) {
            callback(met_list[i]);
        }
        xSemaphoreGive(met_mutex);
    }
}
