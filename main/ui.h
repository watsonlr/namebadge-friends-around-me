/**
 * @file ui.h
 * @brief User Interface for Friends Around Me
 * 
 * Handles high-level UI rendering and state management for the
 * Friends Around Me application.
 */

#ifndef UI_H
#define UI_H

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Initialize the UI system
 * 
 * Initializes the display and sets up the UI.
 * 
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t ui_init(void);

/**
 * @brief Set the user's nickname to display in header
 * 
 * @param nickname User's nickname (max 32 chars)
 */
void ui_set_nickname(const char *nickname);

/**
 * @brief Update the UI display
 * 
 * Refreshes the screen with current nearby friends list and selection.
 * Call this periodically or when the friends list changes.
 */
void ui_refresh(void);

/**
 * @brief Move selection up in the friends list
 */
void ui_select_up(void);

/**
 * @brief Move selection down in the friends list
 */
void ui_select_down(void);

/**
 * @brief Get the currently selected friend's nickname
 * 
 * @param nickname Output buffer for nickname
 * @param max_len Buffer size
 * @return ESP_OK if a friend is selected, ESP_ERR_NOT_FOUND if list is empty
 */
esp_err_t ui_get_selected_nickname(char *nickname, size_t max_len);

/**
 * @brief Get the BD address of the currently selected friend.
 *
 * @param out 6-byte buffer for the address (NimBLE order: out[0]=LSB).
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no selection.
 */
esp_err_t ui_get_selected_addr(uint8_t out[6]);

/**
 * @brief Get the current selection index
 * 
 * @return Selected index (0-based), or -1 if no selection
 */
int ui_get_selected_index(void);

/**
 * @brief Force a full screen redraw
 * 
 * Clears the screen and redraws all elements.
 */
void ui_force_redraw(void);

#endif /* UI_H */
