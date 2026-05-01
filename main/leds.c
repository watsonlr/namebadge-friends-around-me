/**
 * @file leds.c
 * @brief WS2813B addressable LED driver using ESP-IDF RMT TX.
 *
 * Direct port from the BYUI-Namebadge4-OTA bootloader. Pixel order GRB.
 * RMT clock 10 MHz → 100 ns/tick; bit timing per WS2813B datasheet.
 */

#include "leds.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "leds"

#define RMT_RESOLUTION_HZ   (10 * 1000 * 1000)

static uint8_t s_grb[LEDS_COUNT * 3];
static rmt_channel_handle_t s_tx_chan = NULL;
static rmt_encoder_handle_t s_encoder = NULL;
static bool s_ready = false;

bool leds_init(void)
{
    if (s_ready) return true;

    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num          = LEDS_GPIO,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 1024,
        .trans_queue_depth = 4,
        .flags.invert_out  = false,
        .flags.with_dma    = true,
    };
    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &s_tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
        return false;
    }

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = { .level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9 },
        .bit1 = { .level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 6 },
        .flags.msb_first = true,
    };
    err = rmt_new_bytes_encoder(&enc_cfg, &s_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_bytes_encoder failed: %s", esp_err_to_name(err));
        rmt_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return false;
    }

    err = rmt_enable(s_tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
        rmt_del_encoder(s_encoder);
        rmt_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        s_encoder = NULL;
        return false;
    }

    memset(s_grb, 0, sizeof(s_grb));
    s_ready = true;
    ESP_LOGI(TAG, "Initialised: %d LEDs on GPIO %d", LEDS_COUNT, LEDS_GPIO);
    return true;
}

void leds_set(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= LEDS_COUNT) return;
    s_grb[index * 3 + 0] = g;
    s_grb[index * 3 + 1] = r;
    s_grb[index * 3 + 2] = b;
}

void leds_fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < LEDS_COUNT; i++) {
        s_grb[i * 3 + 0] = g;
        s_grb[i * 3 + 1] = r;
        s_grb[i * 3 + 2] = b;
    }
}

void leds_clear(void)
{
    memset(s_grb, 0, sizeof(s_grb));
}

void leds_show(void)
{
    if (!s_ready) return;

    rmt_transmit_config_t tx_config = { .loop_count = 0 };
    esp_err_t err = rmt_transmit(s_tx_chan, s_encoder,
                                 s_grb, sizeof(s_grb), &tx_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
        return;
    }
    rmt_tx_wait_all_done(s_tx_chan, pdMS_TO_TICKS(10));
    vTaskDelay(pdMS_TO_TICKS(1));   /* >300 µs reset gap */
}
