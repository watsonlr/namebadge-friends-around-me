/**
 * @file ui.c
 * @brief User Interface Implementation
 */

#include "ui.h"
#include "display.h"
#include "ble_scanning.h"
#include "met_tracker.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "UI";

/* UI Layout constants */
#define HEADER_HEIGHT       30
#define FOOTER_HEIGHT       25
#define LIST_START_Y        (HEADER_HEIGHT + 5)
#define LIST_ITEM_HEIGHT    25
#define LIST_MAX_VISIBLE    7  /* Max items visible on screen */

/* UI State */
static char user_nickname[33] = "Badge";
static int selected_index = 0;
static int scroll_offset = 0;
static bool need_redraw = true;

/**
 * @brief Draw the header with user's nickname
 */
static void draw_header(void)
{
    /* Draw header background */
    display_fill_rect(0, 0, DISPLAY_WIDTH, HEADER_HEIGHT, COLOR_BLUE);
    
    /* Draw user's nickname */
    char header_text[50];
    snprintf(header_text, sizeof(header_text), " Your Name: %s", user_nickname);
    display_draw_string(5, 8, header_text, COLOR_WHITE, COLOR_BLUE, 2);
    
    /* Draw separator line */
    display_draw_hline(0, HEADER_HEIGHT, DISPLAY_WIDTH, COLOR_WHITE);
}

/**
 * @brief Draw the footer with statistics
 */
static void draw_footer(int nearby_count, int met_count)
{
    int footer_y = DISPLAY_HEIGHT - FOOTER_HEIGHT;
    
    /* Draw separator line */
    display_draw_hline(0, footer_y, DISPLAY_WIDTH, COLOR_WHITE);
    
    /* Draw footer background */
    display_fill_rect(0, footer_y + 1, DISPLAY_WIDTH, FOOTER_HEIGHT - 1, COLOR_BLUE);
    
    /* Draw statistics */
    char footer_text[50];
    snprintf(footer_text, sizeof(footer_text), " Met: %d  |  Nearby: %d", met_count, nearby_count);
    display_draw_string(10, footer_y + 6, footer_text, COLOR_WHITE, COLOR_BLUE, 2);
}

/**
 * @brief Draw RSSI signal strength indicator
 */
static void draw_signal_indicator(int16_t x, int16_t y, int8_t rssi)
{
    /* RSSI to bars mapping:
     * -30 to -50 dBm: 4 bars (very strong)
     * -50 to -65 dBm: 3 bars (strong)
     * -65 to -75 dBm: 2 bars (medium)
     * -75 to -85 dBm: 1 bar (weak)
     * < -85 dBm: 0 bars (very weak)
     */
    int bars = 0;
    if (rssi >= -50) bars = 4;
    else if (rssi >= -65) bars = 3;
    else if (rssi >= -75) bars = 2;
    else if (rssi >= -85) bars = 1;
    
    /* Draw 4 bars */
    for (int i = 0; i < 4; i++) {
        uint16_t color = (i < bars) ? COLOR_GREEN : COLOR_GRAY;
        display_fill_rect(x + (i * 4), y - (i * 2), 3, 10 + (i * 2), color);
    }
}

/**
 * @brief Draw a single friend list item
 */
static void draw_list_item(int16_t y, const char *nickname, int8_t rssi, bool is_selected)
{
    uint16_t bg_color = is_selected ? COLOR_ORANGE : COLOR_BLACK;
    uint16_t fg_color = is_selected ? COLOR_BLACK : COLOR_WHITE;
    
    /* Draw background */
    display_fill_rect(0, y, DISPLAY_WIDTH, LIST_ITEM_HEIGHT, bg_color);
    
    /* Draw selection indicator */
    if (is_selected) {
        display_draw_string(5, y + 5, ">", COLOR_BLACK, bg_color, 2);
    }
    
    /* Draw nickname */
    display_draw_string(25, y + 5, nickname, fg_color, bg_color, 2);
    
    /* Draw signal strength indicator */
    draw_signal_indicator(DISPLAY_WIDTH - 25, y + 7, rssi);
}

/**
 * @brief Draw the friends list
 */
static void draw_friends_list(void)
{
    int count = 0;
    const nearby_friend_t *friends = ble_scanning_get_nearby_friends(&count);
    
    /* Clear list area */
    int list_height = DISPLAY_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT - 10;
    display_fill_rect(0, LIST_START_Y, DISPLAY_WIDTH, list_height, COLOR_BLACK);
    
    if (count == 0) {
        /* No friends nearby */
        const char *msg1 = "No friends nearby";
        const char *msg2 = "yet...";
        display_draw_string(50, 100, msg1, COLOR_GRAY, COLOR_BLACK, 2);
        display_draw_string(90, 125, msg2, COLOR_GRAY, COLOR_BLACK, 2);
        return;
    }
    
    /* Build list of active, non-met friends */
    const nearby_friend_t *active_friends[MAX_NEARBY_FRIENDS];
    int active_count = 0;
    
    for (int i = 0; i < MAX_NEARBY_FRIENDS; i++) {
        if (friends[i].is_active && !friends[i].is_met) {
            active_friends[active_count++] = &friends[i];
        }
    }
    
    /* Update selection bounds */
    if (selected_index >= active_count) {
        selected_index = active_count > 0 ? active_count - 1 : 0;
    }
    
    /* Update scroll offset */
    if (selected_index < scroll_offset) {
        scroll_offset = selected_index;
    } else if (selected_index >= scroll_offset + LIST_MAX_VISIBLE) {
        scroll_offset = selected_index - LIST_MAX_VISIBLE + 1;
    }
    
    /* Draw visible items */
    int y = LIST_START_Y;
    for (int i = 0; i < LIST_MAX_VISIBLE && (scroll_offset + i) < active_count; i++) {
        int idx = scroll_offset + i;
        bool is_selected = (idx == selected_index);
        draw_list_item(y, active_friends[idx]->nickname, active_friends[idx]->rssi, is_selected);
        y += LIST_ITEM_HEIGHT;
    }
    
    /* Draw scroll indicator if needed */
    if (active_count > LIST_MAX_VISIBLE) {
        int indicator_height = (LIST_MAX_VISIBLE * list_height) / active_count;
        if (indicator_height < 10) indicator_height = 10;
        
        int indicator_y = LIST_START_Y + ((scroll_offset * list_height) / active_count);
        display_fill_rect(DISPLAY_WIDTH - 5, indicator_y, 3, indicator_height, COLOR_CYAN);
    }
}

esp_err_t ui_init(void)
{
    ESP_LOGI(TAG, "Initializing UI...");
    
    /* Initialize display */
    esp_err_t ret = display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return ret;
    }
    
    /* Clear screen */
    display_fill(COLOR_BLACK);
    
    ESP_LOGI(TAG, "UI initialized successfully");
    return ESP_OK;
}

void ui_set_nickname(const char *nickname)
{
    if (nickname != NULL) {
        strncpy(user_nickname, nickname, sizeof(user_nickname) - 1);
        user_nickname[sizeof(user_nickname) - 1] = '\0';
        need_redraw = true;
    }
}

void ui_refresh(void)
{
    if (!need_redraw) {
        return;
    }
    
    /* Get nearby friends count */
    int nearby_count = 0;
    ble_scanning_get_nearby_friends(&nearby_count);
    
    /* Get met count from tracker */
    int met_count = met_tracker_get_count();
    
    /* Draw UI components */
    draw_header();
    draw_friends_list();
    draw_footer(nearby_count, met_count);
    
    need_redraw = false;
}

void ui_select_up(void)
{
    int count = 0;
    ble_scanning_get_nearby_friends(&count);
    
    if (count == 0) {
        selected_index = 0;
        return;
    }
    
    if (selected_index > 0) {
        selected_index--;
    } else {
        /* Wrap to bottom */
        selected_index = count - 1;
        scroll_offset = selected_index - LIST_MAX_VISIBLE + 1;
        if (scroll_offset < 0) scroll_offset = 0;
    }
    
    need_redraw = true;
}

void ui_select_down(void)
{
    int count = 0;
    ble_scanning_get_nearby_friends(&count);
    
    if (count == 0) {
        selected_index = 0;
        return;
    }
    
    if (selected_index < count - 1) {
        selected_index++;
    } else {
        /* Wrap to top */
        selected_index = 0;
        scroll_offset = 0;
    }
    
    need_redraw = true;
}

esp_err_t ui_get_selected_nickname(char *nickname, size_t max_len)
{
    if (nickname == NULL || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int count = 0;
    const nearby_friend_t *friends = ble_scanning_get_nearby_friends(&count);
    
    if (count == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    /* Build list of active, non-met friends */
    const nearby_friend_t *active_friends[MAX_NEARBY_FRIENDS];
    int active_count = 0;
    
    for (int i = 0; i < MAX_NEARBY_FRIENDS; i++) {
        if (friends[i].is_active && !friends[i].is_met) {
            active_friends[active_count++] = &friends[i];
        }
    }
    
    if (selected_index >= active_count) {
        return ESP_ERR_NOT_FOUND;
    }
    
    strncpy(nickname, active_friends[selected_index]->nickname, max_len - 1);
    nickname[max_len - 1] = '\0';
    
    return ESP_OK;
}

int ui_get_selected_index(void)
{
    int count = 0;
    ble_scanning_get_nearby_friends(&count);
    
    return (count > 0 && selected_index < count) ? selected_index : -1;
}

void ui_force_redraw(void)
{
    need_redraw = true;
    ui_refresh();
}
