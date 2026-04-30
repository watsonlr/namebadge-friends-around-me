/**
 * @file buttons.h
 * @brief Button Input Handler for Friends Around Me
 * 
 * Handles button press detection with debouncing and event generation.
 */

#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdbool.h>
#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Button identifiers
 */
typedef enum {
    BUTTON_UP = 0,
    BUTTON_DOWN,
    BUTTON_LEFT,
    BUTTON_RIGHT,
    BUTTON_A,
    BUTTON_B,
    BUTTON_COUNT
} button_id_t;

/**
 * @brief Button event types
 */
typedef enum {
    BUTTON_EVENT_PRESSED = 0,
    BUTTON_EVENT_RELEASED,
    BUTTON_EVENT_CLICK,       /* Short press and release */
    BUTTON_EVENT_LONG_PRESS   /* Held for >1 second */
} button_event_type_t;

/**
 * @brief Button event structure
 */
typedef struct {
    button_id_t button;
    button_event_type_t event;
    uint32_t timestamp_ms;
} button_event_t;

/**
 * @brief Button event callback function type
 * 
 * @param event Button event
 */
typedef void (*button_callback_t)(button_event_t event);

/**
 * @brief Initialize button input system
 * 
 * Configures GPIO pins for all buttons with internal pull-ups
 * and enables interrupt-based detection.
 * 
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t buttons_init(void);

/**
 * @brief Register a callback for button events
 * 
 * The callback will be invoked from a task context (not ISR).
 * 
 * @param callback Callback function (NULL to unregister)
 */
void buttons_register_callback(button_callback_t callback);

/**
 * @brief Get the name of a button
 * 
 * @param button Button ID
 * @return Button name string
 */
const char* button_get_name(button_id_t button);

/**
 * @brief Check if a button is currently pressed
 * 
 * @param button Button ID
 * @return true if pressed, false otherwise
 */
bool buttons_is_pressed(button_id_t button);

#endif /* BUTTONS_H */
