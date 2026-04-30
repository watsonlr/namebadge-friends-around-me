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

/**
 * @brief Load met list from NVS
 */
static esp_err_t load_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err;
    
    err = nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for reading: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Read count */
    err = nvs_get_u16(handle, NVS_KEY_COUNT, &met_count);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* First run, no data yet */
        met_count = 0;
        nvs_close(handle);
        ESP_LOGI(TAG, "No existing met list found (first run)");
        return ESP_OK;
    } else if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to read met count: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Validate count */
    if (met_count > MAX_MET_PEOPLE) {
        ESP_LOGW(TAG, "Met count %u exceeds max, capping to %d", met_count, MAX_MET_PEOPLE);
        met_count = MAX_MET_PEOPLE;
    }
    
    /* Read list blob */
    if (met_count > 0) {
        size_t blob_size = met_count * (MET_TRACKER_MAX_NICKNAME_LEN + 1);
        err = nvs_get_blob(handle, NVS_KEY_LIST, met_list, &blob_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read met list: %s", esp_err_to_name(err));
            met_count = 0;
            nvs_close(handle);
            return err;
        }
    }
    
    nvs_close(handle);
    ESP_LOGI(TAG, "Loaded %u people from met list", met_count);
    
    /* Log the names */
    for (uint16_t i = 0; i < met_count; i++) {
        ESP_LOGI(TAG, "  - %s", met_list[i]);
    }
    
    return ESP_OK;
}

/**
 * @brief Save met list to NVS
 */
static esp_err_t save_to_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err;
    
    err = nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Write count */
    err = nvs_set_u16(handle, NVS_KEY_COUNT, met_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write met count: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }
    
    /* Write list blob */
    if (met_count > 0) {
        size_t blob_size = met_count * (MET_TRACKER_MAX_NICKNAME_LEN + 1);
        err = nvs_set_blob(handle, NVS_KEY_LIST, met_list, blob_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write met list: %s", esp_err_to_name(err));
            nvs_close(handle);
            return err;
        }
    } else {
        /* Erase list if count is 0 */
        nvs_erase_key(handle, NVS_KEY_LIST);
    }
    
    /* Commit changes */
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }
    
    nvs_close(handle);
    ESP_LOGI(TAG, "Saved %u people to met list", met_count);
    return ESP_OK;
}

esp_err_t met_tracker_init(void)
{
    ESP_LOGI(TAG, "Initializing met tracker...");
    
    /* Create mutex */
    if (met_mutex == NULL) {
        met_mutex = xSemaphoreCreateMutex();
        if (met_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_FAIL;
        }
    }
    
    /* Initialize list */
    memset(met_list, 0, sizeof(met_list));
    met_count = 0;
    
    /* Load from NVS */
    esp_err_t err = load_from_nvs();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load from NVS, starting with empty list");
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
    
    /* Save to NVS */
    esp_err_t err = save_to_nvs();
    
    xSemaphoreGive(met_mutex);
    return err;
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
    
    /* Save to NVS */
    esp_err_t err = save_to_nvs();
    
    xSemaphoreGive(met_mutex);
    return err;
}

esp_err_t met_tracker_clear_all(void)
{
    if (xSemaphoreTake(met_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_FAIL;
    }
    
    memset(met_list, 0, sizeof(met_list));
    met_count = 0;
    
    ESP_LOGI(TAG, "Cleared all met people");
    
    /* Save to NVS */
    esp_err_t err = save_to_nvs();
    
    xSemaphoreGive(met_mutex);
    return err;
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
