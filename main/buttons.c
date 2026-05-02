/**
 * @file buttons.c
 * @brief Button input — direct port of the V4 bootloader state machine.
 *
 * The buttons sit behind ~10 µF debounce caps + the SoC pull-up; the line
 * takes ~hundreds of ms to fall to LOW. A naive "any sample change resets
 * debounce" loop never accumulates a stable LOW window, so the first press
 * is dropped. This implementation copies the V4 BYUI bootloader approach:
 * a per-button {IDLE, DEBOUNCING, FIRED} state machine that tolerates HIGH
 * glitches up to GLITCH_MS during the slow-fall press window.
 */

#include "buttons.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "BUTTONS";

/* Button GPIO pins — BYUI eBadge V4.0 */
static const uint8_t button_pins[BUTTON_COUNT] = {
    11,  /* UP */
    47,  /* DOWN */
    21,  /* LEFT */
    10,  /* RIGHT */
    34,  /* A */
    33   /* B */
};

static const char *button_names[BUTTON_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "A", "B"
};

#define POLL_MS       10   /* task wakes every 10 ms                       */
#define DEBOUNCE_MS   30   /* button must read LOW this long to register   */
#define GLITCH_MS      8   /* brief HIGH during the press is forgiven      */

typedef enum { STATE_IDLE, STATE_DEBOUNCING, STATE_FIRED } btn_state_t;

static btn_state_t button_state[BUTTON_COUNT];
static int64_t     press_since[BUTTON_COUNT];
static int64_t     release_since[BUTTON_COUNT];
static bool        button_held[BUTTON_COUNT];
static button_callback_t event_callback = NULL;

static void emit_click(button_id_t b)
{
    if (!event_callback) return;
    button_event_t event = {
        .button       = b,
        .event        = BUTTON_EVENT_CLICK,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
    };
    event_callback(event);
}

static void button_task(void *param)
{
    ESP_LOGI(TAG, "Button task started (poll %d ms, debounce %d ms)",
             POLL_MS, DEBOUNCE_MS);

    /* Held-at-boot guard: if a button is already LOW when the task starts
     * (user pressing during reset, or a debounce cap not yet charged),
     * start in FIRED so we don't synthesise a spurious CLICK on boot —
     * the state machine will only re-arm once it's HIGH for DEBOUNCE_MS. */
    int64_t now0 = esp_timer_get_time();
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (gpio_get_level(button_pins[i]) == 0) {
            button_state[i]  = STATE_FIRED;
            release_since[i] = now0;
            ESP_LOGI(TAG, "Button %s held at boot — ignoring until release",
                     button_names[i]);
        }
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        int64_t now = esp_timer_get_time();

        for (int i = 0; i < BUTTON_COUNT; i++) {
            bool low = (gpio_get_level(button_pins[i]) == 0);

            switch (button_state[i]) {
            case STATE_IDLE:
                if (low) {
                    button_state[i]   = STATE_DEBOUNCING;
                    press_since[i]    = now;
                    release_since[i]  = 0;
                }
                break;

            case STATE_DEBOUNCING:
                if (!low) {
                    /* Forgive short HIGH glitches; abort only if the line
                     * stays HIGH for longer than GLITCH_MS. */
                    if (release_since[i] == 0) {
                        release_since[i] = now;
                    } else if ((now - release_since[i]) >=
                               (int64_t)GLITCH_MS * 1000) {
                        button_state[i]  = STATE_IDLE;
                        release_since[i] = 0;
                    }
                } else {
                    release_since[i] = 0;
                }
                if (low &&
                    (now - press_since[i]) >= (int64_t)DEBOUNCE_MS * 1000) {
                    button_held[i] = true;
                    ESP_LOGI(TAG, "Button %s CLICK", button_names[i]);
                    emit_click((button_id_t)i);
                    button_state[i] = STATE_FIRED;
                }
                break;

            case STATE_FIRED:
                /* Require a stable HIGH for DEBOUNCE_MS before re-arming so
                 * release-bounce can't fire a second event. */
                if (low) {
                    release_since[i] = 0;
                } else {
                    if (release_since[i] == 0) {
                        release_since[i] = now;
                    } else if ((now - release_since[i]) >=
                               (int64_t)DEBOUNCE_MS * 1000) {
                        button_state[i]   = STATE_IDLE;
                        release_since[i]  = 0;
                        button_held[i]    = false;
                    }
                }
                break;
            }
        }
    }
}

esp_err_t buttons_init(void)
{
    ESP_LOGI(TAG, "Initializing buttons (V4 state machine)...");

    /* Release any GPIOs that may have been held through deep sleep. */
    gpio_force_unhold_all();

    gpio_config_t cfg = {
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    for (int i = 0; i < BUTTON_COUNT; i++) {
        cfg.pin_bit_mask = (1ULL << button_pins[i]);
        gpio_config(&cfg);
        button_state[i]   = STATE_IDLE;
        press_since[i]    = 0;
        release_since[i]  = 0;
        button_held[i]    = false;
    }

    /* Let the pull-ups charge the debounce caps before the first read. */
    vTaskDelay(pdMS_TO_TICKS(50));

    xTaskCreate(button_task, "buttons", 4096, NULL, 10, NULL);
    ESP_LOGI(TAG, "Buttons initialized successfully");
    return ESP_OK;
}

void buttons_register_callback(button_callback_t callback)
{
    event_callback = callback;
    ESP_LOGI(TAG, "Button callback %s", callback ? "registered" : "unregistered");
}

const char* button_get_name(button_id_t button)
{
    if (button < BUTTON_COUNT) return button_names[button];
    return "UNKNOWN";
}

bool buttons_is_pressed(button_id_t button)
{
    if (button < BUTTON_COUNT) return button_held[button];
    return false;
}
