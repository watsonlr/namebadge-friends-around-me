/**
 * @file leds.h
 * @brief WS2813B addressable LED driver — 24 LEDs on GPIO 7 (BYUI eBadge V4).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define LEDS_GPIO   7
#define LEDS_COUNT  24

bool leds_init(void);
void leds_set(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void leds_fill(uint8_t r, uint8_t g, uint8_t b);
void leds_clear(void);
void leds_show(void);
