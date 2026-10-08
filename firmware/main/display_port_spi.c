/* display_port_spi.c — a 240x320 portrait SPI LCD scanned as landscape 320x240,
 * via esp_lcd over 4-wire SPI:
 *   - the 2.8" ESP32-S3 CYD (ES3C28P): ILI9341V (CONFIG_POCKET_TANK_DISPLAY_ILI9341)
 *   - the Waveshare ESP32-S3-Touch-LCD-2: ST7789T3 (CONFIG_POCKET_TANK_DISPLAY_ST7789)
 * The pins and the bench facts of how each glass is mounted (LCD_SWAP_XY,
 * LCD_MIRROR_*, LCD_BGR, LCD_INVERT, LCD_PCLK_HZ) live in board_pins.h.
 *
 * Unlike the AMOLED port there is no software transpose: the controller turns
 * its own scan through MADCTL (esp_lcd's swap_xy / mirror), so a landscape
 * frame goes out row by row, only byte-swapped (RGB565 is big-endian on the
 * wire). The 180-degree flip is the same register with both mirrors toggled.
 * Brightness is the backlight's PWM duty (LEDC); neither panel has a
 * brightness command worth using.
 *
 * This port also owns the board's I2C bus (touch, codec, IMU, the I2C socket),
 * as the AMOLED port does - board_i2c_bus() is how everything else reaches it. */
#include "display_port.h"
#include "board_pins.h"
#include "tank.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#if CONFIG_POCKET_TANK_DISPLAY_ST7789
#include "esp_lcd_panel_vendor.h"               /* esp_lcd_new_panel_st7789, part of esp_lcd itself */
#define new_panel esp_lcd_new_panel_st7789
static const char *TAG = "st7789";
#else
#include "esp_lcd_ili9341.h"
#define new_panel esp_lcd_new_panel_ili9341
static const char *TAG = "ili9341";
#endif
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define LCD_HOST    SPI2_HOST
#define STRIPE_ROWS 20                          /* tank rows per DMA transfer: 320 x 20 x 2 = 12.8 KB */

#define BL_TIMER   LEDC_TIMER_0
#define BL_CHANNEL LEDC_CHANNEL_0

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_stripe[2];                   /* TANK_W x STRIPE_ROWS, DMA-capable; ping-pong */
static SemaphoreHandle_t s_stripe_free;         /* counts stripe buffers not in DMA flight */
static i2c_master_bus_handle_t s_i2c;
static uint8_t s_brightness = 0xFF;
static bool s_inverted, s_applied_inverted;     /* the flip wanted, and the one MADCTL has */

i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c; }
bool board_is_v2(void) { return false; }        /* the AMOLED's revision question; never asked here */

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx) {
    (void)io; (void)ev; (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_stripe_free, &woken);
    return woken == pdTRUE;
}

static void backlight_init(void) {
    ledc_timer_config_t timer = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                                  .timer_num = BL_TIMER, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t channel = { .gpio_num = PIN_LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
                                      .channel = BL_CHANNEL, .timer_sel = BL_TIMER, .duty = 0, .hpoint = 0 };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

static void backlight_set(uint8_t level) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}

/* MADCTL for the current flip: upright uses the bench mirrors, flipped
 * inverts both, which is a 180-degree turn of the scan. */
static void apply_orientation(void) {
    esp_lcd_panel_swap_xy(s_panel, LCD_SWAP_XY);
    esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X ^ s_inverted, LCD_MIRROR_Y ^ s_inverted);
    s_applied_inverted = s_inverted;
}

bool display_port_init(void) {
    i2c_master_bus_config_t bus = { .i2c_port = I2C_NUM_0, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_i2c));
    /* Who answers on the shared bus: the bring-up question for the codec. */
    for (int address = 0x08; address < 0x78; address++)
        if (i2c_master_probe(s_i2c, address, 20) == ESP_OK) ESP_LOGI(TAG, "i2c: device at 0x%02X", address);

    backlight_init();                               /* dark until the first frame is in */
#if CONFIG_POCKET_TANK_WST_320X240
    /* the TF card shares the LCD's SPI lines: hold its select high so the
       card never answers a panel transfer. (this fork, 2026-10-08) Through
       gpio_config, as every other output in the tree: gpio_set_direction
       alone routes the GPIO matrix's output to the pin but leaves the
       IO_MUX function as it was, and GPIO41's function 0 is the JTAG MTDI,
       not the GPIO - gpio_config selects the GPIO function as well. */
    const gpio_config_t sd_select = { .pin_bit_mask = 1ULL << PIN_SD_CS, .mode = GPIO_MODE_OUTPUT,
                                      .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                      .intr_type = GPIO_INTR_DISABLE };
    ESP_ERROR_CHECK(gpio_config(&sd_select));
    gpio_set_level(PIN_SD_CS, 1);
#endif

    for (int i = 0; i < 2; i++) {
        s_stripe[i] = heap_caps_malloc(TANK_W * STRIPE_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_stripe[i]) return false;
    }
    s_stripe_free = xSemaphoreCreateCounting(2, 2);

    const spi_bus_config_t spi = { .sclk_io_num = PIN_LCD_SCLK, .mosi_io_num = PIN_LCD_MOSI, .miso_io_num = PIN_LCD_MISO,
                                   .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = TANK_W * STRIPE_ROWS * 2 };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &spi, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io;
    const esp_lcd_panel_io_spi_config_t io_cfg = { .cs_gpio_num = PIN_LCD_CS, .dc_gpio_num = PIN_LCD_DC,
        .spi_mode = 0, .pclk_hz = LCD_PCLK_HZ, .trans_queue_depth = 4, .on_color_trans_done = on_trans_done,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));
    const esp_lcd_panel_dev_config_t pcfg = { .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_BGR ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16 };
    ESP_ERROR_CHECK(new_panel(io, &pcfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));  /* no reset pin (-1): the driver sends the software reset */
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    esp_lcd_panel_invert_color(s_panel, LCD_INVERT);
    apply_orientation();
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    backlight_set(s_brightness);
    ESP_LOGI(TAG, "panel up: %dx%d landscape at %d MHz", TANK_W, TANK_H, LCD_PCLK_HZ / 1000000);
    return true;
}

void display_port_sleep(void) {
    backlight_set(0);
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, false);
}

void display_port_wake(void) {
    if (!s_panel) return;
    esp_lcd_panel_disp_on_off(s_panel, true);
    backlight_set(s_brightness);
}

void display_port_set_inverted(bool inverted) { s_inverted = inverted; }

void display_port_set_brightness(uint8_t level) {
    s_brightness = level;
    backlight_set(level);
}
uint8_t display_port_brightness(void) { return s_brightness; }

/* Landscape fb[y][x], TANK_W x TANK_H, straight to the landscape scan: each
 * stripe is STRIPE_ROWS whole rows, byte-swapped into an internal DMA buffer.
 * Two buffers ping-pong, so filling stripe N+1 overlaps sending stripe N. */
void display_port_flush(const uint16_t *fb) {
    /* A flip lands between frames, never mid-scan - but only once every
     * stripe of the frame before is out, because MADCTL is a command on the
     * same bus. */
    if (s_inverted != s_applied_inverted) {
        xSemaphoreTake(s_stripe_free, portMAX_DELAY); xSemaphoreTake(s_stripe_free, portMAX_DELAY);
        apply_orientation();
        xSemaphoreGive(s_stripe_free); xSemaphoreGive(s_stripe_free);
    }
    int current = 0;
    for (int y0 = 0; y0 < TANK_H; y0 += STRIPE_ROWS) {
        int rows = TANK_H - y0 < STRIPE_ROWS ? TANK_H - y0 : STRIPE_ROWS;
        xSemaphoreTake(s_stripe_free, portMAX_DELAY);
        uint16_t *stripe = s_stripe[current];
        const uint32_t *source = (const uint32_t *)(fb + y0 * TANK_W);
        uint32_t *destination = (uint32_t *)stripe;
        /* Two pixels per 32-bit word: swap the bytes inside each half. */
        for (int i = 0; i < rows * TANK_W / 2; i++) {
            uint32_t pair = source[i];
            destination[i] = ((pair & 0x00FF00FFu) << 8) | ((pair & 0xFF00FF00u) >> 8);
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y0, TANK_W, y0 + rows, stripe);
        if (err != ESP_OK) {
            xSemaphoreGive(s_stripe_free);          /* no transfer, so no completion will return it */
            static int logged;
            if (logged++ < 3) ESP_LOGE(TAG, "draw_bitmap y0=%d: %s", y0, esp_err_to_name(err));
        }
        current ^= 1;
    }
}

/* ---- the board questions upstream's ports ask (display_port.h, v0.3.0) ----
 * Neither SPI board is one of upstream's AMOLED boards: no round glass, no
 * watch, no IO expander, no PWR key sense line, no panel deep standby or
 * QSPI bus to hold, and the frame IS the panel - so each answer is the plain
 * one, on the CYD and the Touch-LCD-2 alike. */
bool board_is_round(void) { return false; }
bool board_is_watch(void) { return false; }
bool board_has_expander(void) { return false; }
int  board_pwr_sense_pin(void) { return -1; }
void display_port_deep_sleep_pins(bool tp_awake_high) { (void)tp_awake_high; }
void display_port_deep_sleep_bus(void) { }
void display_port_deep_standby(void) { }
void display_port_frame_origin(int *px, int *py) { *px = 0; *py = 0; }
void display_port_set_view(int view) { (void)view; }
int  display_port_view(void) { return DISPLAY_VIEW_FIT; }
void display_port_panel_to_tank(int px, int py, float *tx, float *ty) { *tx = (float)px; *ty = (float)py; }
void display_port_flush_prof(int64_t *wait_us, int64_t *send_us) { *wait_us = 0; *send_us = 0; }
