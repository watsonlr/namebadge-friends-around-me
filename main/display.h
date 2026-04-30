/**
 * @file display.h
 * @brief ILI9341 Display Driver for Friends Around Me
 * 
 * Handles low-level display operations for the ILI9341 TFT LCD (240x320).
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include "esp_err.h"
#include <stdint.h>

/* Display dimensions (landscape mode) */
#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 240

/* RGB565 color definitions */
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_GRAY    0x8410
#define COLOR_ORANGE  0xFD20
#define COLOR_PURPLE  0x8010

/**
 * @brief Initialize the ILI9341 display
 * 
 * Configures SPI bus and initializes the display controller.
 * Sets landscape orientation (320x240).
 * 
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t display_init(void);

/**
 * @brief Fill entire display with a color
 * 
 * @param color RGB565 color value
 */
void display_fill(uint16_t color);

/**
 * @brief Draw a filled rectangle
 * 
 * @param x X coordinate (0-319)
 * @param y Y coordinate (0-239)
 * @param w Width in pixels
 * @param h Height in pixels
 * @param color RGB565 color value
 */
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/**
 * @brief Draw a rectangle outline
 * 
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width
 * @param h Height
 * @param color RGB565 color
 */
void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/**
 * @brief Draw a horizontal line
 * 
 * @param x Starting X coordinate
 * @param y Y coordinate
 * @param w Width (length)
 * @param color RGB565 color
 */
void display_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color);

/**
 * @brief Draw a vertical line
 * 
 * @param x X coordinate
 * @param y Starting Y coordinate
 * @param h Height (length)
 * @param color RGB565 color
 */
void display_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color);

/**
 * @brief Draw a single pixel
 * 
 * @param x X coordinate
 * @param y Y coordinate
 * @param color RGB565 color
 */
void display_draw_pixel(int16_t x, int16_t y, uint16_t color);

/**
 * @brief Draw a character at specified position
 * 
 * Uses built-in 8x8 font.
 * 
 * @param x X coordinate
 * @param y Y coordinate
 * @param c Character to draw
 * @param color Foreground color
 * @param bg Background color
 * @param size Font scale (1 = 8x8, 2 = 16x16, etc.)
 */
void display_draw_char(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);

/**
 * @brief Draw a string at specified position
 * 
 * @param x X coordinate
 * @param y Y coordinate
 * @param str String to draw
 * @param color Foreground color
 * @param bg Background color
 * @param size Font scale (1-4)
 */
void display_draw_string(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t size);

/**
 * @brief Set display backlight (if controllable)
 * 
 * @param on true to turn on, false to turn off
 */
void display_set_backlight(bool on);

#endif /* DISPLAY_H */
