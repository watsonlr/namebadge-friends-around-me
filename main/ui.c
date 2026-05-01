/**
 * @file ui.c
 * @brief User Interface Implementation
 */

#include "ui.h"
#include "display.h"
#include "ble_scanning.h"
#include "ble_advertising.h"
#include "met_tracker.h"
#include "leds.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "UI";

/* UI Layout constants */
#define HEADER_HEIGHT       60   /* two stacked title + nickname lines */
#define FOOTER_HEIGHT       25
#define LIST_START_Y        (HEADER_HEIGHT + 5)
#define LIST_ITEM_HEIGHT    25
#define LIST_MAX_VISIBLE    6   /* fewer rows now that the header is taller */

/* Which list we're showing in the body. */
typedef enum {
    VIEW_TO_MEET = 0,   /* nearby badges I haven't met yet */
    VIEW_MET,           /* nearby badges I've already met */
} view_mode_t;

/* Flash state for a single friend row. */
typedef enum {
    ROW_FLASH_NONE = 0,
    ROW_FLASH_RED,     /* I'm meeting them (sender, outgoing) */
    ROW_FLASH_YELLOW,  /* They want to meet me */
    ROW_FLASH_GREEN,   /* They're trying to find me */
} row_flash_t;

/* UI State */
static char user_nickname[33] = "Badge";
static view_mode_t current_view = VIEW_TO_MEET;
static int selected_index = 0;
static int scroll_offset = 0;
static bool need_redraw = true;
static uint32_t blink_tick = 0;

/* "Now friends with X" overlay state. */
#define ANNOUNCEMENT_DURATION_US (3LL * 1000 * 1000)
static char announcement_text[MAX_NICKNAME_LEN + 1] = {0};
static bool announcement_i_initiated = false;
static int64_t announcement_until_us = 0;

/* Draw text horizontally centred at the given y, white-on-black. */
static void draw_centered(int16_t y, const char *text, uint8_t scale)
{
    int width_px = (int)strlen(text) * 8 * scale;
    int x = (DISPLAY_WIDTH - width_px) / 2;
    if (x < 0) x = 0;
    display_draw_string((int16_t)x, y, text, COLOR_WHITE, COLOR_BLACK, scale);
}

/* Full-screen "now friends with <name>" overlay. The opening line varies
 * by who initiated the meet — "I am now…" if we did, "You are now…" if
 * the other side asked first and we accepted. */
static void draw_announcement(const char *nickname, bool i_initiated)
{
    display_fill(COLOR_BLACK);
    draw_centered(40, i_initiated ? "I am now" : "You are now", 3);
    draw_centered(85, "friends with", 3);

    /* Pick the largest scale the nickname will fit in (320 px wide). */
    int len = (int)strlen(nickname);
    uint8_t scale = 4;
    while (scale > 1 && len * 8 * scale > DISPLAY_WIDTH) scale--;
    int y = (scale >= 3) ? 150 : 160;
    draw_centered((int16_t)y, nickname, scale);
}

/**
 * @brief Draw the two-line header (title + "I am: <nick>") on a blue bg.
 */
static void draw_header(void)
{
    display_fill_rect(0, 0, DISPLAY_WIDTH, HEADER_HEIGHT, COLOR_BLUE);

    /* Title line — "Friends to Meet" / "Friends I've Met". Same scale-2
     * font as the nickname line; "Badge Friends to Meet" was 21 chars and
     * would overrun 320 px so the wording is shortened to fit. */
    const char *title = (current_view == VIEW_MET)
        ? " Friends I've Met" : " Friends to Meet";
    display_draw_string(5, 5, title, COLOR_WHITE, COLOR_BLUE, 2);

    /* "I am: <nick>" line below the title. */
    char header_text[50];
    snprintf(header_text, sizeof(header_text), " I am: %s", user_nickname);
    display_draw_string(5, 32, header_text, COLOR_WHITE, COLOR_BLUE, 2);

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
    snprintf(footer_text, sizeof(footer_text), " Seen: %d   Met:%2d", nearby_count, met_count);
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
static void draw_list_item(int16_t y, const char *nickname, int8_t rssi,
                           bool is_selected, row_flash_t flash, bool flash_on)
{
    uint16_t bg_color = COLOR_BLACK;
    uint16_t fg_color = COLOR_WHITE;

    if (flash != ROW_FLASH_NONE && flash_on) {
        switch (flash) {
            case ROW_FLASH_RED:    bg_color = COLOR_RED;    break;
            case ROW_FLASH_YELLOW: bg_color = COLOR_YELLOW; break;
            case ROW_FLASH_GREEN:  bg_color = COLOR_GREEN;  break;
            default: break;
        }
        fg_color = COLOR_BLACK;
    } else if (is_selected) {
        bg_color = COLOR_ORANGE;
        fg_color = COLOR_BLACK;
    }

    /* Draw background */
    display_fill_rect(0, y, DISPLAY_WIDTH, LIST_ITEM_HEIGHT, bg_color);

    /* Draw selection indicator */
    if (is_selected) {
        display_draw_string(5, y + 5, ">", fg_color, bg_color, 2);
    }

    /* Draw nickname */
    display_draw_string(25, y + 5, nickname, fg_color, bg_color, 2);

    /* Draw signal strength indicator */
    draw_signal_indicator(DISPLAY_WIDTH - 25, y + 7, rssi);
}

/* Decide the row's flash colour based on incoming/outgoing requests.
 * - they want to meet me  → yellow
 * - they want to find me  → green
 * - I'm meet-targeting them → red (find is one-way, no row flash) */
static row_flash_t row_flash_for(const nearby_friend_t *f,
                                 uint8_t my_kind, const uint8_t my_target[2])
{
    if (f->they_request_me) return ROW_FLASH_YELLOW;
    if (f->they_find_me)    return ROW_FLASH_GREEN;
    if (my_kind == BLE_TARGET_MEET &&
        (my_target[0] || my_target[1]) &&
        my_target[0] == f->addr[1] && my_target[1] == f->addr[0]) {
        return ROW_FLASH_RED;
    }
    return ROW_FLASH_NONE;
}

/* Build the visible list for the current view. Returns the count and fills
 * the caller's array with pointers into the friends table. */
static int build_visible_list(const nearby_friend_t *friends,
                              const nearby_friend_t *out[MAX_NEARBY_FRIENDS])
{
    int n = 0;
    for (int i = 0; i < MAX_NEARBY_FRIENDS; i++) {
        if (!friends[i].is_active) continue;
        bool met = friends[i].is_met;
        if ((current_view == VIEW_TO_MEET && !met) ||
            (current_view == VIEW_MET     &&  met)) {
            out[n++] = &friends[i];
        }
    }
    return n;
}

/**
 * @brief Draw the friends list
 */
static void draw_friends_list(void)
{
    int count = 0;
    const nearby_friend_t *friends = ble_scanning_get_nearby_friends(&count);

    uint8_t my_kind;
    uint8_t my_target[2];
    ble_advertising_get_target(&my_kind, my_target);
    bool flash_on = (blink_tick & 1) == 0;

    int list_height = DISPLAY_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT - 10;
    display_fill_rect(0, LIST_START_Y, DISPLAY_WIDTH, list_height, COLOR_BLACK);

    const nearby_friend_t *active_friends[MAX_NEARBY_FRIENDS];
    int active_count = build_visible_list(friends, active_friends);

    if (active_count == 0) {
        const char *msg1 = (current_view == VIEW_MET)
            ? "No met friends" : "No friends nearby";
        const char *msg2 = (current_view == VIEW_MET)
            ? "in range" : "yet...";
        display_draw_string(50, 110, msg1, COLOR_GRAY, COLOR_BLACK, 2);
        display_draw_string(90, 140, msg2, COLOR_GRAY, COLOR_BLACK, 2);
        return;
    }

    if (selected_index >= active_count) {
        selected_index = active_count > 0 ? active_count - 1 : 0;
    }
    if (selected_index < scroll_offset) {
        scroll_offset = selected_index;
    } else if (selected_index >= scroll_offset + LIST_MAX_VISIBLE) {
        scroll_offset = selected_index - LIST_MAX_VISIBLE + 1;
    }

    int y = LIST_START_Y;
    for (int i = 0; i < LIST_MAX_VISIBLE && (scroll_offset + i) < active_count; i++) {
        int idx = scroll_offset + i;
        bool is_selected = (idx == selected_index);
        row_flash_t flash = row_flash_for(active_friends[idx], my_kind, my_target);
        draw_list_item(y, active_friends[idx]->nickname, active_friends[idx]->rssi,
                       is_selected, flash, flash_on);
        y += LIST_ITEM_HEIGHT;
    }

    if (active_count > LIST_MAX_VISIBLE) {
        int indicator_height = (LIST_MAX_VISIBLE * list_height) / active_count;
        if (indicator_height < 10) indicator_height = 10;
        int indicator_y = LIST_START_Y + ((scroll_offset * list_height) / active_count);
        display_fill_rect(DISPLAY_WIDTH - 5, indicator_y, 3, indicator_height, COLOR_CYAN);
    }
}

/* Per-tick summary of flashing rows so the UI loop and LED loop can both
 * make decisions without re-walking the friends list. */
typedef struct {
    bool any_flashing;       /* true → keep redrawing for animation         */
    bool incoming_meet;      /* at least one row flashing yellow            */
    bool incoming_find;      /* at least one row flashing green             */
} flash_summary_t;

static flash_summary_t scan_flash_summary(void)
{
    flash_summary_t s = { false, false, false };
    int count = 0;
    const nearby_friend_t *friends = ble_scanning_get_nearby_friends(&count);
    if (count == 0) return s;

    uint8_t my_kind;
    uint8_t my_target[2];
    ble_advertising_get_target(&my_kind, my_target);
    bool i_have_meet_target = (my_kind == BLE_TARGET_MEET &&
                               (my_target[0] || my_target[1]));

    /* Walk every active slot — both the to-meet and the met rows can flash. */
    for (int i = 0; i < MAX_NEARBY_FRIENDS; i++) {
        if (!friends[i].is_active) continue;
        if (friends[i].they_request_me) {
            s.any_flashing = true;
            s.incoming_meet = true;
        }
        if (friends[i].they_find_me) {
            s.any_flashing = true;
            s.incoming_find = true;
        }
        if (i_have_meet_target &&
            my_target[0] == friends[i].addr[1] &&
            my_target[1] == friends[i].addr[0]) {
            s.any_flashing = true;
        }
    }
    return s;
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
    /* Active "now friends" announcement takes over the screen. */
    int64_t now_us = esp_timer_get_time();
    bool announce_active = (announcement_until_us > 0 &&
                            now_us < announcement_until_us);
    bool announce_just_ended = (announcement_until_us > 0 &&
                                now_us >= announcement_until_us);

    if (announce_active) {
        leds_clear();
        leds_show();
        if (need_redraw) {
            draw_announcement(announcement_text, announcement_i_initiated);
            need_redraw = false;
        }
        return;
    }
    if (announce_just_ended) {
        announcement_until_us = 0;
        announcement_text[0] = '\0';
        need_redraw = true;   /* repaint the list now that we're back */
    }

    /* FIND requests auto-expire — let the advertiser tick that. */
    ble_advertising_check_target_timeout();

    flash_summary_t fs = scan_flash_summary();
    if (!need_redraw && !fs.any_flashing) {
        leds_clear();
        leds_show();
        return;
    }

    if (fs.any_flashing) {
        blink_tick++;
    }
    bool blink_on = (blink_tick & 1) == 0;

    /* LED bar: green when someone is finding me, yellow when someone is
     * trying to meet me, off otherwise. Green wins if both are present. */
    if (blink_on && fs.incoming_find) {
        leds_fill(0, 8, 0);   /* low-intensity green */
    } else if (blink_on && fs.incoming_meet) {
        leds_fill(8, 6, 0);   /* low-intensity yellow */
    } else {
        leds_clear();
    }
    leds_show();

    int nearby_count = 0;
    ble_scanning_get_nearby_friends(&nearby_count);

    int met_count = met_tracker_get_count();

    draw_header();
    draw_friends_list();
    draw_footer(nearby_count, met_count);

    need_redraw = false;
}

/* Helper: number of rows visible in the current view. */
static int visible_count(void)
{
    int count = 0;
    const nearby_friend_t *friends = ble_scanning_get_nearby_friends(&count);
    const nearby_friend_t *tmp[MAX_NEARBY_FRIENDS];
    return build_visible_list(friends, tmp);
}

void ui_select_up(void)
{
    int n = visible_count();
    if (n == 0) { selected_index = 0; return; }

    if (selected_index > 0) {
        selected_index--;
    } else {
        selected_index = n - 1;
        scroll_offset = selected_index - LIST_MAX_VISIBLE + 1;
        if (scroll_offset < 0) scroll_offset = 0;
    }
    need_redraw = true;
}

void ui_select_down(void)
{
    int n = visible_count();
    if (n == 0) { selected_index = 0; return; }

    if (selected_index < n - 1) {
        selected_index++;
    } else {
        selected_index = 0;
        scroll_offset = 0;
    }
    need_redraw = true;
}

esp_err_t ui_get_selected_nickname(char *nickname, size_t max_len)
{
    if (nickname == NULL || max_len == 0) return ESP_ERR_INVALID_ARG;

    int count = 0;
    const nearby_friend_t *friends = ble_scanning_get_nearby_friends(&count);
    const nearby_friend_t *active_friends[MAX_NEARBY_FRIENDS];
    int active_count = build_visible_list(friends, active_friends);
    if (selected_index >= active_count) return ESP_ERR_NOT_FOUND;

    strncpy(nickname, active_friends[selected_index]->nickname, max_len - 1);
    nickname[max_len - 1] = '\0';
    return ESP_OK;
}

esp_err_t ui_get_selected_addr(uint8_t out[6])
{
    int count = 0;
    const nearby_friend_t *friends = ble_scanning_get_nearby_friends(&count);

    const nearby_friend_t *active_friends[MAX_NEARBY_FRIENDS];
    int active_count = build_visible_list(friends, active_friends);
    if (active_count == 0 || selected_index >= active_count) {
        return ESP_ERR_NOT_FOUND;
    }

    memcpy(out, active_friends[selected_index]->addr, 6);
    return ESP_OK;
}

bool ui_in_met_view(void)
{
    return current_view == VIEW_MET;
}

void ui_toggle_view(void)
{
    current_view = (current_view == VIEW_TO_MEET) ? VIEW_MET : VIEW_TO_MEET;
    selected_index = 0;
    scroll_offset  = 0;
    need_redraw = true;
    ESP_LOGI(TAG, "View toggled to %s",
             current_view == VIEW_MET ? "MET" : "TO_MEET");
}

int ui_get_selected_index(void)
{
    int count = 0;
    ble_scanning_get_nearby_friends(&count);
    
    return (count > 0 && selected_index < count) ? selected_index : -1;
}

void ui_force_redraw(void)
{
    /* Just flag for redraw — the UI task picks it up on its next tick.
     * Doing SPI display work here would blow the stack of whichever task
     * called us (commonly the BLE host task via the scan callback). */
    need_redraw = true;
}

void ui_show_friend_announcement(const char *nickname, bool i_initiated)
{
    if (nickname == NULL) return;
    strncpy(announcement_text, nickname, sizeof(announcement_text) - 1);
    announcement_text[sizeof(announcement_text) - 1] = '\0';
    announcement_i_initiated = i_initiated;
    announcement_until_us = esp_timer_get_time() + ANNOUNCEMENT_DURATION_US;
    need_redraw = true;
}

bool ui_selected_is_requesting_me(void)
{
    int count = 0;
    const nearby_friend_t *friends = ble_scanning_get_nearby_friends(&count);

    const nearby_friend_t *active_friends[MAX_NEARBY_FRIENDS];
    int active_count = build_visible_list(friends, active_friends);
    if (selected_index < 0 || selected_index >= active_count) return false;
    return active_friends[selected_index]->they_request_me;
}
