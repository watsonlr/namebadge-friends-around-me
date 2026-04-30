/**
 * @file buttons.c
 * @brief Button Input Handler Implementation
 */

#include "buttons.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "BUTTONS";

/* Button GPIO pins (from HARDWARE.md) */
static const uint8_t button_pins[BUTTON_COUNT] = {
    17,  /* UP */
    16,  /* DOWN */
    14,  /* LEFT */
    15,  /* RIGHT */
    38,  /* A */
    18   /* B */
};

static const char *button_names[BUTTON_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "A", "B"
};

/* Button state tracking */
typedef struct {
    bool is_pressed;
    uint32_t press_time_ms;
    bool long_press_sent;
} button_state_t;

static button_state_t button_states[BUTTON_COUNT];
static QueueHandle_t button_event_queue = NULL;
static button_callback_t event_callback = NULL;

/* Debounce settings */
#define DEBOUNCE_TIME_MS    50
#define LONG_PRESS_TIME_MS  1000

/**
 * @brief GPIO ISR handler for button presses
 */
static void IRAM_ATTR button_isr_handler(void *arg)
{
    button_id_t button = (button_id_t)(int)arg;
    
    /* Send button ID to queue for processing in task */
    xQueueSendFromISR(button_event_queue, &button, NULL);
}

/**
 * @brief Button event processing task
 */
static void button_task(void *param)
{
    button_id_t button;
    button_event_t event;
    
    ESP_LOGI(TAG, "Button task started");
    
    while (1) {
        if (xQueueReceive(button_event_queue, &button, pdMS_TO_TICKS(100))) {
            /* Debounce delay */
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME_MS));
            
            /* Read current button state */
            int level = gpio_get_level(button_pins[button]);
            bool is_pressed = (level == 0);  /* Active LOW */
            
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            
            if (is_pressed && !button_states[button].is_pressed) {
                /* Button pressed */
                button_states[button].is_pressed = true;
                button_states[button].press_time_ms = now_ms;
                button_states[button].long_press_sent = false;
                
                event.button = button;
                event.event = BUTTON_EVENT_PRESSED;
                event.timestamp_ms = now_ms;
                
                if (event_callback) {
                    event_callback(event);
                }
                
                ESP_LOGI(TAG, "Button %s PRESSED", button_names[button]);
                
            } else if (!is_pressed && button_states[button].is_pressed) {
                /* Button released */
                uint32_t press_duration = now_ms - button_states[button].press_time_ms;
                button_states[button].is_pressed = false;
                
                event.button = button;
                event.event = BUTTON_EVENT_RELEASED;
                event.timestamp_ms = now_ms;
                
                if (event_callback) {
                    event_callback(event);
                }
                
                /* Send click event if it was a short press */
                if (press_duration < LONG_PRESS_TIME_MS && !button_states[button].long_press_sent) {
                    event.event = BUTTON_EVENT_CLICK;
                    if (event_callback) {
                        event_callback(event);
                    }
                    ESP_LOGI(TAG, "Button %s CLICK", button_names[button]);
                } else {
                    ESP_LOGI(TAG, "Button %s RELEASED", button_names[button]);
                }
            }
        }
        
        /* Check for long presses */
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        for (int i = 0; i < BUTTON_COUNT; i++) {
            if (button_states[i].is_pressed && !button_states[i].long_press_sent) {
                uint32_t press_duration = now_ms - button_states[i].press_time_ms;
                if (press_duration >= LONG_PRESS_TIME_MS) {
                    button_states[i].long_press_sent = true;
                    
                    event.button = i;
                    event.event = BUTTON_EVENT_LONG_PRESS;
                    event.timestamp_ms = now_ms;
                    
                    if (event_callback) {
                        event_callback(event);
                    }
                    
                    ESP_LOGI(TAG, "Button %s LONG_PRESS", button_names[i]);
                }
            }
        }
    }
}

esp_err_t buttons_init(void)
{
    ESP_LOGI(TAG, "Initializing buttons...");
    
    /* Initialize button states */
    for (int i = 0; i < BUTTON_COUNT; i++) {
        button_states[i].is_pressed = false;
        button_states[i].press_time_ms = 0;
        button_states[i].long_press_sent = false;
    }
    
    /* Create event queue */
    button_event_queue = xQueueCreate(10, sizeof(button_id_t));
    if (button_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create button event queue");
        return ESP_FAIL;
    }
    
    /* Configure GPIO pins */
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE  /* Trigger on both edges */
    };
    
    for (int i = 0; i < BUTTON_COUNT; i++) {
        io_conf.pin_bit_mask = (1ULL << button_pins[i]);
        gpio_config(&io_conf);
        
        /* Attach ISR handler */
        gpio_isr_handler_add(button_pins[i], button_isr_handler, (void*)i);
    }
    
    /* Install GPIO ISR service */
    gpio_install_isr_service(0);
    
    /* Create button processing task */
    xTaskCreate(button_task, "buttons", 3072, NULL, 10, NULL);
    
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
    if (button < BUTTON_COUNT) {
        return button_names[button];
    }
    return "UNKNOWN";
}

bool buttons_is_pressed(button_id_t button)
{
    if (button < BUTTON_COUNT) {
        return button_states[button].is_pressed;
    }
    return false;
}
