/**
 * @file met_tracker.h
 * @brief Met People Tracking with Persistent Storage
 * 
 * Tracks people who have been met and stores the list in NVS
 * (user_data partition) so it survives app updates and reboots.
 */

#ifndef MET_TRACKER_H
#define MET_TRACKER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* Maximum number of people we can track as met */
#define MAX_MET_PEOPLE 256

/* Maximum nickname length */
#define MET_TRACKER_MAX_NICKNAME_LEN 32

/**
 * @brief Initialize the met people tracking system
 * 
 * Opens NVS storage and loads the met people list.
 * 
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t met_tracker_init(void);

/**
 * @brief Add a person to the met list
 * 
 * Adds the nickname to the met list and persists to NVS.
 * If the person is already in the list, this is a no-op.
 * 
 * @param nickname Nickname to add (max 32 chars)
 * @return ESP_OK on success, ESP_ERR_NO_MEM if list is full, ESP_FAIL on NVS error
 */
esp_err_t met_tracker_add(const char *nickname);

/**
 * @brief Check if a person has been met
 * 
 * @param nickname Nickname to check
 * @return true if in met list, false otherwise
 */
bool met_tracker_is_met(const char *nickname);

/**
 * @brief Get the total count of people met
 * 
 * @return Number of people in the met list
 */
uint16_t met_tracker_get_count(void);

/**
 * @brief Get a nickname from the met list by index
 * 
 * @param index Index in met list (0-based)
 * @param nickname Output buffer for nickname
 * @param max_len Buffer size
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if index out of range
 */
esp_err_t met_tracker_get_nickname(uint16_t index, char *nickname, size_t max_len);

/**
 * @brief Remove a person from the met list
 * 
 * Removes the nickname from the met list and updates NVS.
 * 
 * @param nickname Nickname to remove
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not in list
 */
esp_err_t met_tracker_remove(const char *nickname);

/**
 * @brief Clear all people from the met list
 * 
 * Erases the entire met list from NVS.
 * 
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t met_tracker_clear_all(void);

/**
 * @brief Export the met list to a callback function
 * 
 * Useful for debugging or displaying the full list.
 * 
 * @param callback Function to call for each nickname
 */
typedef void (*met_tracker_export_cb_t)(const char *nickname);
void met_tracker_export(met_tracker_export_cb_t callback);

#endif /* MET_TRACKER_H */
