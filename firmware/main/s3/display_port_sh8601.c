/* display_port_sh8601.c — Waveshare 1.8" AMOLED over QSPI via esp_lcd.
 * The tank renders landscape 448x368 (common/render.c); the panel is portrait
 * 368x448, so frames are rotated 90 degrees in software into DMA stripes.
 * Works for V1 (SH8601) and V2 (CO5300) - same init sequence, V2 adds an x gap. */
#include "display_port.h"
#include "board_pins.h"
#include "tank.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "sh8601";
#define LCD_HOST SPI2_HOST
#define STRIPE_ROWS 32                         /* panel rows per DMA transfer */

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;          /* kept for DCS writes after init (brightness) */
static uint8_t s_brightness = 0xFF;             /* what init_cmds' 0x51 sets */
static uint16_t *s_stripe[2];                   /* PANEL_W x STRIPE_ROWS, DMA-capable; ping-pong */
static SemaphoreHandle_t s_stripe_free;         /* counts stripe buffers not in DMA flight */
static i2c_master_bus_handle_t s_i2c;
static bool s_v2;
static bool s_inverted;                         /* 180-degree flip, done in the transpose */

void display_port_set_inverted(bool inverted) { s_inverted = inverted; }

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx) {
    (void)io; (void)ev; (void)ctx;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_stripe_free, &hp);
    return hp == pdTRUE;
}

i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c; }
bool board_is_v2(void) { return s_v2; }

static const sh8601_lcd_init_cmd_t init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x11, NULL, 0, 100},
    {0x29, NULL, 0, 0},
};

/* TCA9554: drive LCD_RST, DSI_PWR_EN, TOUCH_RST high (SD_CS high = deselected) */
static void expander_power_up(void) {
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = I2C_ADDR_EXPANDER, .scl_speed_hz = 400000 };
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(s_i2c, &cfg, &dev) != ESP_OK) { ESP_LOGW(TAG, "no IO expander"); return; }
    uint8_t config[2] = { 0x03, (uint8_t)~0x87 };   /* bits 0,1,2,7 as outputs */
    uint8_t out_lo[2]  = { 0x01, 0x80 };            /* resets low, SD_CS high */
    uint8_t out_hi[2]  = { 0x01, 0x87 };
    i2c_master_transmit(dev, config, 2, 100);
    i2c_master_transmit(dev, out_lo, 2, 100); vTaskDelay(pdMS_TO_TICKS(20));
    i2c_master_transmit(dev, out_hi, 2, 100); vTaskDelay(pdMS_TO_TICKS(120));
    i2c_master_bus_rm_device(dev);
}

bool display_port_init(void) {
    i2c_master_bus_config_t bus = { .i2c_port = I2C_NUM_0, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_i2c));
    expander_power_up();
    s_v2 = i2c_master_probe(s_i2c, I2C_ADDR_CST816, 50) == ESP_OK;
    ESP_LOGI(TAG, "board revision: %s", s_v2 ? "V2 (CO5300/CST816)" : "V1 (SH8601/FT3168)");

    for (int i = 0; i < 2; i++) {
        s_stripe[i] = heap_caps_malloc(PANEL_W * STRIPE_ROWS * 2, MALLOC_CAP_DMA);
        if (!s_stripe[i]) return false;
    }
    s_stripe_free = xSemaphoreCreateCounting(2, 2);
    const spi_bus_config_t spi = SH8601_PANEL_BUS_QSPI_CONFIG(PIN_LCD_PCLK, PIN_LCD_DATA0, PIN_LCD_DATA1,
                                                              PIN_LCD_DATA2, PIN_LCD_DATA3, PANEL_W * STRIPE_ROWS * 2);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &spi, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, on_trans_done, NULL);
    io_cfg.pclk_hz = 80 * 1000 * 1000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));
    s_io = io;
    const sh8601_vendor_config_t vendor = { .init_cmds = init_cmds, .init_cmds_size = sizeof init_cmds / sizeof init_cmds[0],
                                            .flags.use_qspi_interface = 1 };
    const esp_lcd_panel_dev_config_t pcfg = { .reset_gpio_num = -1, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                              .bits_per_pixel = 16, .vendor_config = (void *)&vendor };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io, &pcfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, s_v2 ? V2_PANEL_X_GAP : 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    ESP_LOGI(TAG, "panel up: %dx%d portrait, tank frame rotated 90 deg", PANEL_W, PANEL_H);
    return true;
}

/* sleep-mode power-down: display off, then the expander cuts the panel rail
 * (resets low, SD_CS kept high). display_port_init re-sequences it on wake. */
void display_port_sleep(void) {
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, false);
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = I2C_ADDR_EXPANDER, .scl_speed_hz = 400000 };
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(s_i2c, &cfg, &dev) == ESP_OK) {
        uint8_t out_off[2] = { 0x01, 0x80 };
        i2c_master_transmit(dev, out_off, 2, 100);
        i2c_master_bus_rm_device(dev);
    }
}

/* drowse wake: rail back up (expander re-sequenced), then the full panel init
 * over the still-open QSPI/I2C buses - no reboot needed. init_cmds is file-
 * static, so the driver's retained pointer stays valid for this re-init. */
void display_port_wake(void) {
    if (!s_panel) return;
    expander_power_up();
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_set_gap(s_panel, s_v2 ? V2_PANEL_X_GAP : 0, 0);
    display_port_set_brightness(s_brightness);      /* init_cmds put it back at 255 */
    esp_lcd_panel_disp_on_off(s_panel, true);
}

/* DCS 0x51 WRDISBV over the QSPI link: the sh8601 driver frames a command as
 * <write opcode 0x02><cmd><00> in a 32-bit word, so we do the same here */
void display_port_set_brightness(uint8_t level) {
    s_brightness = level;
    if (!s_io) return;
    int cmd = (0x02 << 24) | (0x51 << 8);
    esp_lcd_panel_io_tx_param(s_io, cmd, &level, 1);
}
uint8_t display_port_brightness(void) { return s_brightness; }

/* landscape fb[y][x] (TANK_W x TANK_H) -> portrait panel: px = y, py = TANK_W-1-x.
 * Colors are byte-swapped for the panel (big-endian RGB565 over SPI).
 * The transpose walks the PSRAM framebuffer row-sequentially (16 contiguous
 * pixels per row segment = one cache line) and scatters into the internal
 * stripe buffer; two stripe buffers ping-pong so the transpose of stripe N+1
 * overlaps the DMA of stripe N. */
void display_port_flush(const uint16_t *fb) {
    int cur = 0;
    for (int py0 = 0; py0 < PANEL_H; py0 += STRIPE_ROWS) {
        xSemaphoreTake(s_stripe_free, portMAX_DELAY);
        uint16_t *stripe = s_stripe[cur];
        int x1 = TANK_W - 1 - py0;                    /* source columns x1-(STRIPE_ROWS-1) .. x1 */
        if (!s_inverted) for (int px = 0; px < PANEL_W; px += 2) {  /* pair adjacent panel columns: one 32-bit store */
            const uint16_t *s0 = fb + px * TANK_W + x1 - (STRIPE_ROWS - 1);
            const uint16_t *s1 = s0 + TANK_W;
            uint32_t *dst = (uint32_t *)(stripe + px);
            for (int r = 0; r < STRIPE_ROWS; r++) {
                uint16_t a = s0[STRIPE_ROWS - 1 - r], b = s1[STRIPE_ROWS - 1 - r];   /* x = x1 - r */
                a = (uint16_t)((a << 8) | (a >> 8)); b = (uint16_t)((b << 8) | (b >> 8));
                dst[r * (PANEL_W / 2)] = (uint32_t)a | ((uint32_t)b << 16);
            }
        }
        /* 180-degree flip: panel (px,py) = fb[TANK_H-1-px][py] (TANK_W == PANEL_H).
         * Row segments are read forward instead of backward — same cache pattern. */
        else for (int px = 0; px < PANEL_W; px += 2) {
            const uint16_t *s0 = fb + (TANK_H - 1 - px) * TANK_W + py0;
            const uint16_t *s1 = s0 - TANK_W;         /* panel column px+1 = the fb row above */
            uint32_t *dst = (uint32_t *)(stripe + px);
            for (int r = 0; r < STRIPE_ROWS; r++) {
                uint16_t a = s0[r], b = s1[r];
                a = (uint16_t)((a << 8) | (a >> 8)); b = (uint16_t)((b << 8) | (b >> 8));
                dst[r * (PANEL_W / 2)] = (uint32_t)a | ((uint32_t)b << 16);
            }
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, py0, PANEL_W, py0 + STRIPE_ROWS, stripe);
        if (err != ESP_OK) {
            static int logged;
            if (logged++ < 3) ESP_LOGE(TAG, "draw_bitmap py0=%d: %s", py0, esp_err_to_name(err));
        }
        cur ^= 1;
    }
}
