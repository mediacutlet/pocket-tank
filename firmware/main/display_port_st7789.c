/* display_port_st7789.c — Waveshare ESP32-S3-(Touch-)LCD-1.69: ST7789V2,
 * 240x280 IPS over plain 4-wire SPI, PWM backlight on GPIO15.
 *
 * The tank still renders its native landscape 448x368 frame (common/render.c
 * is untouched); this port scales it to the panel's 280x240 landscape view
 * (x 0.625, y 0.652 - a ~4% vertical squash, no letterbox) with a 2x2 box
 * filter, and rotates it 90 degrees into the panel's portrait RAM in DMA
 * stripes - the same shape as the SH8601 port. The panel is set up exactly
 * as Waveshare's own esp-idf example does it (mirror x+y, 20-row gap,
 * inverted colors), because in that orientation the CST816T's raw touch
 * coordinates equal the panel's logical coordinates; touch_port_ft3168.c
 * then undoes the same rotation + scale (board_pins.h LCD169_*). */
#include "display_port.h"
#include "board_pins.h"
#include "tank.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "st7789";
#define LCD_HOST     SPI2_HOST
#define LCD_PCLK_HZ  (80 * 1000 * 1000)       /* Waveshare ships 40 MHz; 80 halves the flush - drop back if the picture glitches */
#define STRIPE_ROWS  40                        /* panel rows per DMA transfer (280 / 40 = 7 stripes) */
#define BL_LEDC_TIMER   LEDC_TIMER_0
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static i2c_master_bus_handle_t s_i2c;
static uint8_t s_brightness = 0xFF;
static bool s_bl_ready, s_asleep;
static bool s_lit;                              /* the first frame is on the glass: the backlight may come on */
static uint16_t *s_stripe[2];
static SemaphoreHandle_t s_stripe_free;
static bool s_inverted;
/* view pixel -> the two tank columns / rows the box filter averages */
static uint16_t s_x0[LCD169_VIEW_W], s_x1[LCD169_VIEW_W];
static uint16_t s_y0[LCD169_VIEW_H], s_y1[LCD169_VIEW_H];

i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c; }
bool board_is_v2(void) { return true; }        /* touch_port: this board's touch is a CST816 */

void display_port_set_inverted(bool inverted) { s_inverted = inverted; }

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx) {
    (void)io; (void)ev; (void)ctx;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_stripe_free, &hp);
    return hp == pdTRUE;
}

static void bl_write(uint8_t level) {
    if (!s_bl_ready) return;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

static void bl_init(void) {
    ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                              .timer_num = BL_LEDC_TIMER, .freq_hz = 20000, .clk_cfg = LEDC_AUTO_CLK };
    ledc_channel_config_t c = { .gpio_num = PIN_LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = BL_LEDC_CHANNEL,
                                .timer_sel = BL_LEDC_TIMER, .duty = 0, .hpoint = 0 };
    s_bl_ready = ledc_timer_config(&t) == ESP_OK && ledc_channel_config(&c) == ESP_OK;
    if (!s_bl_ready) {                          /* no PWM: plain on/off */
        gpio_config_t io = { .pin_bit_mask = 1ULL << PIN_LCD_BL, .mode = GPIO_MODE_OUTPUT };
        gpio_config(&io);
        gpio_set_level(PIN_LCD_BL, 0);
        ESP_LOGW(TAG, "backlight PWM unavailable: on/off only");
    }
}

static void panel_setup(void) {
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_mirror(s_panel, true, true);
    esp_lcd_panel_set_gap(s_panel, 0, LCD169_Y_GAP);
    esp_lcd_panel_disp_on_off(s_panel, true);
}

/* CST816T: pulse its reset (TP_RST is a plain GPIO on this board, not an
 * expander bit), then keep it from auto-sleeping - asleep it NACKs every
 * poll until a finger wakes it, and the first tap is lost. */
static void touch_reset(void) {
    gpio_config_t io = { .pin_bit_mask = 1ULL << PIN_TP_RST, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    gpio_set_level(PIN_TP_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TP_RST, 1); vTaskDelay(pdMS_TO_TICKS(60));
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = I2C_ADDR_CST816, .scl_speed_hz = 400000 };
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(s_i2c, &cfg, &dev) == ESP_OK) {
        uint8_t no_autosleep[2] = { 0xFE, 0x01 };           /* DisAutoSleep */
        esp_err_t e = i2c_master_transmit(dev, no_autosleep, 2, 100);
        ESP_LOGI(TAG, "CST816T %s", e == ESP_OK ? "up, auto-sleep off" : "did not answer (no-touch board variant?)");
        i2c_master_bus_rm_device(dev);
    }
}

bool display_port_init(void) {
    /* the buzzer is a passive one on a transistor: left floating it can sit
     * half-on and cook the LDO (Waveshare's FAQ) - pin it low first thing */
    gpio_config_t bz = { .pin_bit_mask = 1ULL << PIN_BUZZER, .mode = GPIO_MODE_OUTPUT, .pull_down_en = GPIO_PULLDOWN_ENABLE };
    gpio_config(&bz); gpio_set_level(PIN_BUZZER, 0);

    i2c_master_bus_config_t bus = { .i2c_port = I2C_NUM_0, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_i2c));
    touch_reset();

    for (int x = 0; x < LCD169_VIEW_W; x++) {
        int sx = x * TANK_W / LCD169_VIEW_W;
        s_x0[x] = sx; s_x1[x] = sx + 1 < TANK_W ? sx + 1 : sx;
    }
    for (int y = 0; y < LCD169_VIEW_H; y++) {
        int sy = y * TANK_H / LCD169_VIEW_H;
        s_y0[y] = sy; s_y1[y] = sy + 1 < TANK_H ? sy + 1 : sy;
    }

    for (int i = 0; i < 2; i++) {
        s_stripe[i] = heap_caps_malloc(LCD169_PANEL_W * STRIPE_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_stripe[i]) { ESP_LOGE(TAG, "no DMA RAM for the stripes"); return false; }
    }
    s_stripe_free = xSemaphoreCreateCounting(2, 2);

    bl_init();                                  /* dark until the first frame is on the glass */
    spi_bus_config_t spi = { .sclk_io_num = PIN_LCD_SCLK, .mosi_io_num = PIN_LCD_MOSI, .miso_io_num = -1,
                             .quadwp_io_num = -1, .quadhd_io_num = -1,
                             .max_transfer_sz = LCD169_PANEL_W * STRIPE_ROWS * 2 };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &spi, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io_cfg = { .dc_gpio_num = PIN_LCD_DC, .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_PCLK_HZ, .lcd_cmd_bits = 8, .lcd_param_bits = 8, .spi_mode = 0,
        .trans_queue_depth = 4, .on_color_trans_done = on_trans_done };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io));
    esp_lcd_panel_dev_config_t pcfg = { .reset_gpio_num = PIN_LCD_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                        .bits_per_pixel = 16 };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io, &pcfg, &s_panel));
    panel_setup();
    ESP_LOGI(TAG, "panel up: %dx%d portrait, tank %dx%d scaled to %dx%d and rotated 90 deg",
             LCD169_PANEL_W, LCD169_PANEL_H, TANK_W, TANK_H, LCD169_VIEW_W, LCD169_VIEW_H);
    return true;
}

void display_port_sleep(void) {
    if (!s_panel) return;
    bl_write(0);
    if (!s_bl_ready) gpio_set_level(PIN_LCD_BL, 0);
    esp_lcd_panel_disp_on_off(s_panel, false);
    esp_lcd_panel_disp_sleep(s_panel, true);
    s_asleep = true;
}

void display_port_wake(void) {
    if (!s_panel) return;
    esp_lcd_panel_disp_sleep(s_panel, false);
    panel_setup();
    s_asleep = false;
    display_port_set_brightness(s_brightness);
}

void display_port_set_brightness(uint8_t level) {
    s_brightness = level;
    if (s_asleep || !s_lit) return;
    if (s_bl_ready) bl_write(level);
    else gpio_set_level(PIN_LCD_BL, level ? 1 : 0);
}
uint8_t display_port_brightness(void) { return s_brightness; }

/* 2x2 box average of four RGB565 pixels: spread each into R.G.B lanes of a
 * 32-bit word (G in the top half), add, round, fold back */
static inline uint32_t spread(uint16_t p) { return ((uint32_t)p | ((uint32_t)p << 16)) & 0x07E0F81Fu; }
static inline uint16_t avg4(uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
    uint32_t s = spread(a) + spread(b) + spread(c) + spread(d) + 0x00401002u;   /* +2 per lane: round */
    s = (s >> 2) & 0x07E0F81Fu;
    uint16_t v = (uint16_t)(s | (s >> 16));
    return (uint16_t)((v << 8) | (v >> 8));                                    /* big-endian over SPI */
}

/* panel (px, py) in portrait shows view (vx, vy):
 *   upright:  vx = VIEW_W-1-py, vy = px
 *   inverted: vx = py,          vy = VIEW_H-1-px
 * Outer loop over panel columns = tank rows, inner over the stripe's rows =
 * a forward/backward walk along ONE tank row: cache-line friendly in PSRAM. */
void display_port_flush(const uint16_t *fb) {
    if (!s_panel) return;
    int cur = 0;
    for (int py0 = 0; py0 < LCD169_PANEL_H; py0 += STRIPE_ROWS) {
        int rows = LCD169_PANEL_H - py0 < STRIPE_ROWS ? LCD169_PANEL_H - py0 : STRIPE_ROWS;
        xSemaphoreTake(s_stripe_free, portMAX_DELAY);
        uint16_t *stripe = s_stripe[cur];
        for (int px = 0; px < LCD169_PANEL_W; px++) {
            int vy = s_inverted ? LCD169_VIEW_H - 1 - px : px;
            const uint16_t *ra = fb + s_y0[vy] * TANK_W, *rb = fb + s_y1[vy] * TANK_W;
            uint16_t *dst = stripe + px;
            for (int r = 0; r < rows; r++) {
                int py = py0 + r;
                int vx = s_inverted ? py : LCD169_VIEW_W - 1 - py;
                int x0 = s_x0[vx], x1 = s_x1[vx];
                dst[r * LCD169_PANEL_W] = avg4(ra[x0], ra[x1], rb[x0], rb[x1]);
            }
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, py0, LCD169_PANEL_W, py0 + rows, stripe);
        if (err != ESP_OK) {
            xSemaphoreGive(s_stripe_free);
            static int logged;
            if (logged++ < 3) ESP_LOGE(TAG, "draw_bitmap py0=%d: %s", py0, esp_err_to_name(err));
        }
        cur ^= 1;
    }
    if (!s_lit) {                               /* light the backlight only once there is a picture */
        s_lit = true;
        xSemaphoreTake(s_stripe_free, portMAX_DELAY); xSemaphoreTake(s_stripe_free, portMAX_DELAY);
        xSemaphoreGive(s_stripe_free); xSemaphoreGive(s_stripe_free);
        display_port_set_brightness(s_brightness);
    }
}
