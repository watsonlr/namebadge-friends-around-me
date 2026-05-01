/**
 * @file ble_advertising.c
 * @brief BLE Advertising Implementation
 * 
 * Implements BLE advertising using the NimBLE stack.
 */

#include "ble_advertising.h"
#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_ADV";

/* Advertisement state */
static bool is_advertising = false;
static char adv_nickname[BLE_ADV_MAX_NICKNAME_LEN + 1] = {0};

/* NimBLE host task handle */
static void ble_host_task(void *param);

/**
 * @brief NimBLE host reset callback
 * 
 * Called when the NimBLE host resets. This can happen during initialization
 * or if the controller encounters an error.
 */
static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset, reason=%d", reason);
    is_advertising = false;
}

/**
 * @brief NimBLE host sync callback
 * 
 * Called when the NimBLE host and controller are synchronized and ready.
 * This is where we can start advertising.
 */
static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "BLE host synced");
    
    /* Make sure we have a valid address */
    int rc;
    uint8_t addr_type;
    rc = ble_hs_id_infer_auto(0, &addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to determine address type, rc=%d", rc);
        return;
    }
    
    /* Print the device address */
    uint8_t addr_val[6];
    rc = ble_hs_id_copy_addr(addr_type, addr_val, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "BLE device address: %02x:%02x:%02x:%02x:%02x:%02x",
                 addr_val[5], addr_val[4], addr_val[3],
                 addr_val[2], addr_val[1], addr_val[0]);
    }
}

/**
 * @brief Configure and start advertising
 * 
 * Sets up the advertisement data and scan response, then starts advertising.
 * 
 * @return 0 on success, BLE error code on failure
 */
static int ble_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    /* Set advertisement fields */
    memset(&fields, 0, sizeof(fields));

    /* Advertise flags: general discoverable + BLE only */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* Include TX power level */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    /* Set the device name */
    fields.name = (uint8_t *)adv_nickname;
    fields.name_len = strlen(adv_nickname);
    fields.name_is_complete = 1;

    /* Set the service UUID (128-bit) */
    uint8_t uuid128[16] = {BLE_NAMEBADGE_SERVICE_UUID_128};
    fields.uuids128 = (ble_uuid128_t[]){
        BLE_UUID128_INIT(
            uuid128[0], uuid128[1], uuid128[2], uuid128[3],
            uuid128[4], uuid128[5], uuid128[6], uuid128[7],
            uuid128[8], uuid128[9], uuid128[10], uuid128[11],
            uuid128[12], uuid128[13], uuid128[14], uuid128[15]
        )
    };
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    /* Set advertisement data */
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertisement data, rc=%d", rc);
        return rc;
    }

    /* Set up manufacturer data in scan response (more space for nickname) */
    memset(&fields, 0, sizeof(fields));
    
    /* Create manufacturer data: [Company ID (2 bytes)] [Nickname (up to 29 bytes)] */
    uint8_t mfg_data[31];  /* Max manufacturer data size */
    mfg_data[0] = BLE_NAMEBADGE_MANUFACTURER_ID & 0xFF;         /* Company ID low byte */
    mfg_data[1] = (BLE_NAMEBADGE_MANUFACTURER_ID >> 8) & 0xFF;  /* Company ID high byte */
    
    /* Copy nickname after company ID */
    size_t nickname_len = strlen(adv_nickname);
    if (nickname_len > 29) {
        nickname_len = 29;  /* Leave room for company ID */
    }
    memcpy(&mfg_data[2], adv_nickname, nickname_len);
    
    fields.mfg_data = mfg_data;
    fields.mfg_data_len = 2 + nickname_len;

    /* Set scan response data */
    rc = ble_gap_adv_rsp_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set scan response data, rc=%d", rc);
        return rc;
    }

    /* Configure advertising parameters */
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;  /* Non-connectable */
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  /* General discoverable */
    adv_params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;  /* 30 ms */
    adv_params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;  /* 60 ms */

    /* Start advertising */
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                          &adv_params, NULL, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising, rc=%d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "Advertising started with nickname: %s", adv_nickname);
    is_advertising = true;
    
    return 0;
}

/**
 * @brief NimBLE host task
 * 
 * This task runs the NimBLE host stack.
 */
static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();  /* This function will block until nimble_port_stop() */
    nimble_port_freertos_deinit();
}

esp_err_t ble_advertising_init(void)
{
    ESP_LOGI(TAG, "Initializing BLE advertising...");

    /* Initialize NimBLE controller and host (handles HCI init internally in ESP-IDF v5.x) */
    int nimble_rc = nimble_port_init();
    if (nimble_rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", nimble_rc);
        return ESP_FAIL;
    }

    /* Configure the host */
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Set default device name */
    nimble_rc = ble_svc_gap_device_name_set("BYUI Badge");
    if (nimble_rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name, rc=%d", nimble_rc);
        return ESP_FAIL;
    }

    /* Initialize GATT services */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* Start the NimBLE host task */
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE advertising initialized successfully");
    return ESP_OK;
}

esp_err_t ble_advertising_start(const char *nickname)
{
    if (nickname == NULL || nickname[0] == '\0') {
        ESP_LOGE(TAG, "Invalid nickname");
        return ESP_ERR_INVALID_ARG;
    }

    /* Copy nickname (truncate if too long) */
    strncpy(adv_nickname, nickname, BLE_ADV_MAX_NICKNAME_LEN);
    adv_nickname[BLE_ADV_MAX_NICKNAME_LEN] = '\0';

    ESP_LOGI(TAG, "Starting advertising with nickname: %s", adv_nickname);

    /* Update device name in GAP service */
    int rc = ble_svc_gap_device_name_set(adv_nickname);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name, rc=%d", rc);
        return ESP_FAIL;
    }

    /* Start advertising (will be called from sync callback if not synced yet) */
    if (ble_hs_synced()) {
        rc = ble_advertise();
        if (rc != 0) {
            ESP_LOGE(TAG, "Failed to advertise, rc=%d", rc);
            return ESP_FAIL;
        }
    } else {
        ESP_LOGI(TAG, "BLE not synced yet, advertising will start after sync");
        /* Store nickname so we can advertise after sync */
    }

    return ESP_OK;
}

esp_err_t ble_advertising_stop(void)
{
    if (!is_advertising) {
        ESP_LOGW(TAG, "Advertising is not active");
        return ESP_OK;
    }

    int rc = ble_gap_adv_stop();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to stop advertising, rc=%d", rc);
        return ESP_FAIL;
    }

    is_advertising = false;
    ESP_LOGI(TAG, "Advertising stopped");
    return ESP_OK;
}

esp_err_t ble_advertising_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing BLE advertising...");

    /* Stop advertising if active */
    if (is_advertising) {
        ble_advertising_stop();
    }

    /* Stop the NimBLE host */
    int rc = nimble_port_stop();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to stop NimBLE port, rc=%d", rc);
        return ESP_FAIL;
    }

    /* Deinit NimBLE host and controller */
    nimble_port_deinit();

    is_advertising = false;
    ESP_LOGI(TAG, "BLE advertising deinitialized");
    return ESP_OK;
}

bool ble_advertising_is_active(void)
{
    return is_advertising;
}
