/**
 * @file display.c
 * @brief ILI9341 Display Driver Implementation
 */

#include "display.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "DISPLAY";

/* Pin definitions (from HARDWARE.md) */
#define PIN_MISO     10
#define PIN_MOSI     11
#define PIN_CLK      12
#define PIN_CS       9
#define PIN_DC       13
#define PIN_RST      48

/* SPI configuration */
#define SPI_CLOCK_SPEED_HZ  (40 * 1000 * 1000)  /* 40 MHz */
#define SPI_HOST            SPI2_HOST

/* ILI9341 commands */
#define ILI9341_NOP         0x00
#define ILI9341_SWRESET     0x01
#define ILI9341_SLPOUT      0x11
#define ILI9341_INVON       0x21
#define ILI9341_DISPOFF     0x28
#define ILI9341_DISPON      0x29
#define ILI9341_CASET       0x2A
#define ILI9341_PASET       0x2B
#define ILI9341_RAMWR       0x2C
#define ILI9341_MADCTL      0x36
#define ILI9341_COLMOD      0x3A
#define ILI9341_FRMCTR1     0xB1
#define ILI9341_DFUNCTR     0xB6
#define ILI9341_PWCTR1      0xC0
#define ILI9341_PWCTR2      0xC1
#define ILI9341_VMCTR1      0xC5
#define ILI9341_VMCTR2      0xC7
#define ILI9341_GMCTRP1     0xE0
#define ILI9341_GMCTRN1     0xE1

static spi_device_handle_t spi_handle;

/* Built-in 8x8 font (basic ASCII 32-127) */
static const uint8_t font8x8[96][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // (space)
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // !
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // "
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // #
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, // $
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, // %
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, // &
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // '
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, // (
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, // )
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // *
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, // +
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // ,
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, // -
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // .
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // /
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, // 0
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, // 1
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, // 2
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, // 3
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, // 4
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, // 5
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, // 6
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, // 7
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // 8
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, // 9
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // :
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // ;
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, // <
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, // =
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, // >
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, // ?
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, // @
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, // A
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, // B
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00}, // C
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, // D
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00}, // E
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, // F
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00}, // G
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, // H
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // I
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, // J
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, // K
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, // L
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00}, // M
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, // N
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, // O
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, // P
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, // Q
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, // R
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, // S
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // T
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00}, // U
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // V
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // W
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, // X
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, // Y
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, // Z
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, // [
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, // (backslash)
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, // ]
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, // ^
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // _
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, // `
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, // a
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, // b
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, // c
    {0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00}, // d
    {0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00}, // e
    {0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00}, // f
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // g
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, // h
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // i
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, // j
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, // k
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // l
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, // m
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, // n
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, // o
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, // p
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, // q
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, // r
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, // s
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, // t
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, // u
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // v
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, // w
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, // x
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // y
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, // z
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, // {
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // |
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, // }
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ~
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}  // DEL
};

/* Pre-transfer callback: sets DC pin from transaction user field (0=cmd, 1=data) */
static void IRAM_ATTR spi_pre_cb(spi_transaction_t *t)
{
    gpio_set_level(PIN_DC, (int)(intptr_t)t->user);
}

static void send_command(uint8_t cmd)
{
    spi_transaction_t t = {
        .length  = 8,
        .tx_data = {cmd},
        .flags   = SPI_TRANS_USE_TXDATA,
        .user    = (void *)0,   /* DC LOW = command */
    };
    spi_device_polling_transmit(spi_handle, &t);
}

static void send_data(uint8_t data)
{
    spi_transaction_t t = {
        .length  = 8,
        .tx_data = {data},
        .flags   = SPI_TRANS_USE_TXDATA,
        .user    = (void *)1,   /* DC HIGH = data */
    };
    spi_device_polling_transmit(spi_handle, &t);
}

static void send_data_buf(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = data,
        .user      = (void *)1,   /* DC HIGH = data */
    };
    spi_device_polling_transmit(spi_handle, &t);
}

/* With MADCTL=0x40 (MX=1, MV=0) the panel is portrait 240×320 with columns mirrored.
 * Logical landscape coords (x=0..319, y=0..239) map to:
 *   CASET (col) = (239 - y1) .. (239 - y0)   [y axis → portrait columns]
 *   PASET (row) = (319 - x1) .. (319 - x0)   [x axis → portrait rows]
 * This matches the working BYUI-Namebadge4-OTA bootloader driver. */
static void set_address_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint16_t col0 = (DISPLAY_HEIGHT - 1) - y1;
    uint16_t col1 = (DISPLAY_HEIGHT - 1) - y0;
    uint16_t row0 = (DISPLAY_WIDTH  - 1) - x1;
    uint16_t row1 = (DISPLAY_WIDTH  - 1) - x0;

    uint8_t d[4];

    send_command(ILI9341_CASET);
    d[0] = col0 >> 8; d[1] = col0 & 0xFF; d[2] = col1 >> 8; d[3] = col1 & 0xFF;
    send_data_buf(d, 4);

    send_command(ILI9341_PASET);
    d[0] = row0 >> 8; d[1] = row0 & 0xFF; d[2] = row1 >> 8; d[3] = row1 & 0xFF;
    send_data_buf(d, 4);

    send_command(ILI9341_RAMWR);
}

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing ILI9341 display...");

    /* Configure GPIOs: DC, RST, and SD card CS (GPIO3 must be deselected to avoid bus contention) */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RST) | (1ULL << 3),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(3, 1);   /* SD card CS high = deselected */

    /* Configure SPI bus */
    spi_bus_config_t bus_cfg = {
        .miso_io_num = PIN_MISO,
        .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * 2 + 8
    };
    
    esp_err_t ret = spi_bus_initialize(SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure SPI device */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPI_CLOCK_SPEED_HZ,
        .mode           = 0,
        .spics_io_num   = PIN_CS,
        .queue_size     = 7,
        .pre_cb         = spi_pre_cb,   /* Sets DC pin before each transaction */
    };
    
    ret = spi_bus_add_device(SPI_HOST, &dev_cfg, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Hardware reset — hold low for 20ms, then wait 120ms for panel to stabilise */
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Software reset, wait 150ms */
    send_command(ILI9341_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    /* Power control A */
    send_command(0xCB);
    send_data(0x39); send_data(0x2C); send_data(0x00);
    send_data(0x34); send_data(0x02);

    /* Power control B */
    send_command(0xCF);
    send_data(0x00); send_data(0xC1); send_data(0x30);

    /* Driver timing control A */
    send_command(0xE8);
    send_data(0x85); send_data(0x00); send_data(0x78);

    /* Driver timing control B */
    send_command(0xEA);
    send_data(0x00); send_data(0x00);

    /* Power on sequence */
    send_command(0xED);
    send_data(0x64); send_data(0x03); send_data(0x12); send_data(0x81);

    /* Pump ratio */
    send_command(0xF7);
    send_data(0x20);

    /* Power control 1 */
    send_command(ILI9341_PWCTR1);
    send_data(0x23);

    /* Power control 2 */
    send_command(ILI9341_PWCTR2);
    send_data(0x10);

    /* VCOM control 1 */
    send_command(ILI9341_VMCTR1);
    send_data(0x3E); send_data(0x28);

    /* VCOM control 2 */
    send_command(ILI9341_VMCTR2);
    send_data(0x86);

    /* Memory access control: landscape, FPC on left, BGR */
    send_command(ILI9341_MADCTL);
    send_data(0x40);

    /* Pixel format: 16-bit RGB565 */
    send_command(ILI9341_COLMOD);
    send_data(0x55);

    /* Frame rate: 79 Hz */
    send_command(ILI9341_FRMCTR1);
    send_data(0x00); send_data(0x18);

    /* Display function control */
    send_command(ILI9341_DFUNCTR);
    send_data(0x08); send_data(0x82); send_data(0x27);

    /* 3-gamma disable */
    send_command(0xF2);
    send_data(0x00);

    /* Gamma curve 1 */
    send_command(0x26);
    send_data(0x01);

    /* Positive gamma correction */
    send_command(ILI9341_GMCTRP1);
    send_data(0x0F); send_data(0x31); send_data(0x2B); send_data(0x0C);
    send_data(0x0E); send_data(0x08); send_data(0x4E); send_data(0xF1);
    send_data(0x37); send_data(0x07); send_data(0x10); send_data(0x03);
    send_data(0x0E); send_data(0x09); send_data(0x00);

    /* Negative gamma correction */
    send_command(ILI9341_GMCTRN1);
    send_data(0x00); send_data(0x0E); send_data(0x14); send_data(0x03);
    send_data(0x11); send_data(0x07); send_data(0x31); send_data(0xC1);
    send_data(0x48); send_data(0x08); send_data(0x0F); send_data(0x0C);
    send_data(0x31); send_data(0x36); send_data(0x0F);

    /* Sleep out — mandatory 120ms wait */
    send_command(ILI9341_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Invert display (required for this panel variant) */
    send_command(ILI9341_INVON);

    /* Display on */
    send_command(ILI9341_DISPON);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Display initialized successfully");
    return ESP_OK;
}

void display_fill(uint16_t color)
{
    display_fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, color);
}

void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) return;
    if (x + w > DISPLAY_WIDTH) w = DISPLAY_WIDTH - x;
    if (y + h > DISPLAY_HEIGHT) h = DISPLAY_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    set_address_window(x, y, x + w - 1, y + h - 1);

    /* Convert color to big-endian */
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;

    /* Fill with color */
    gpio_set_level(PIN_DC, 1);  /* Data mode */
    
    uint32_t pixels = w * h;
    const uint32_t chunk = 128;  /* Send in chunks */
    static uint8_t buffer[128 * 2];
    
    /* Prepare buffer with color */
    for (uint32_t i = 0; i < chunk * 2; i += 2) {
        buffer[i] = hi;
        buffer[i + 1] = lo;
    }
    
    while (pixels > 0) {
        uint32_t count = pixels > chunk ? chunk : pixels;
        send_data_buf(buffer, count * 2);
        pixels -= count;
    }
}

void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    display_draw_hline(x, y, w, color);
    display_draw_hline(x, y + h - 1, w, color);
    display_draw_vline(x, y, h, color);
    display_draw_vline(x + w - 1, y, h, color);
}

void display_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color)
{
    display_fill_rect(x, y, w, 1, color);
}

void display_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color)
{
    display_fill_rect(x, y, 1, h, color);
}

void display_draw_pixel(int16_t x, int16_t y, uint16_t color)
{
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) return;
    
    set_address_window(x, y, x, y);
    send_data(color >> 8);
    send_data(color & 0xFF);
}

void display_draw_char(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size)
{
    if (c < 32 || c > 127) c = ' ';  /* Only printable ASCII */
    const uint8_t *glyph = font8x8[c - 32];
    
    for (uint8_t row = 0; row < 8; row++) {
        uint8_t line = glyph[row];
        for (uint8_t col = 0; col < 8; col++) {
            if (line & 0x01) {
                /* Draw foreground pixel(s) */
                if (size == 1) {
                    display_draw_pixel(x + col, y + row, color);
                } else {
                    display_fill_rect(x + (col * size), y + (row * size), size, size, color);
                }
            } else {
                /* Draw background pixel(s) */
                if (size == 1) {
                    display_draw_pixel(x + col, y + row, bg);
                } else {
                    display_fill_rect(x + (col * size), y + (row * size), size, size, bg);
                }
            }
            line >>= 1;
        }
    }
}

void display_draw_string(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t size)
{
    int16_t cursor_x = x;
    int16_t cursor_y = y;
    uint8_t char_width = 8 * size;
    uint8_t char_height = 8 * size;
    
    while (*str) {
        if (*str == '\n') {
            cursor_y += char_height;
            cursor_x = x;
        } else if (*str == '\r') {
            /* Ignore */
        } else {
            if (cursor_x + char_width > DISPLAY_WIDTH) {
                cursor_x = x;
                cursor_y += char_height;
            }
            if (cursor_y + char_height > DISPLAY_HEIGHT) {
                break;  /* Out of screen space */
            }
            display_draw_char(cursor_x, cursor_y, *str, color, bg, size);
            cursor_x += char_width;
        }
        str++;
    }
}

void display_set_backlight(bool on)
{
    /* Backlight control not implemented (always on via hardware) */
    (void)on;
}
