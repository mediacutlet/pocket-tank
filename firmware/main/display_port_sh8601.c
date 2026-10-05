/* display_port_sh8601.c — Waveshare 1.8" AMOLED over QSPI via esp_lcd.
 * The tank renders landscape 448x368 (common/render.c); the panel is portrait
 * 368x448, so frames are rotated 90 degrees in software into DMA stripes.
 * Works for V1 (SH8601) and V2 (CO5300) - same init sequence, V2 adds an x gap.
 *
 * And the ROUND 1.75C (2026-10-01; CO5300 again, 466x466, the same driver):
 * recognized at boot by what answers on the I2C bus, its resets on GPIOs
 * instead of the expander, its clock on another pin. The same 448x368 frame
 * is presented in the circle unrotated - scaled 4/5 so all of it shows (FIT),
 * or px for px with the corners cropped (FULL); see display_port.h.
 *
 * And the WATCH (the 2.06, 2026-10-02; CO5300, 410x502 portrait), with GPIO
 * resets and its own init. Its own build (TANK_WATCH) is a PORTRAIT tank,
 * 410x502: the frame is the panel, sent row for row, nothing turned. The
 * rectangle image on a watch takes the 1.8's path - its 448x368 frame turned
 * 90 degrees, centred in the glass (a black border, drawn once).
 *
 * And the 2.16 (BOARD_SQ216; CO5300, 480x480 SQUARE): the 1.75C's family with
 * its own resets and init, run down the round board's path - its own build's
 * square tank (TANK_SQUARE) IS the panel, px for px (FULL, at 0, 0). */
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
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char *TAG = "sh8601";
#define LCD_HOST SPI2_HOST
#define STRIPE_ROWS 32                         /* panel rows per DMA transfer */
#define STRIPE_BYTES (PANEL_W * STRIPE_ROWS * 2)   /* one stripe buffer; the round panel and the watch fit their rows into the same bytes */
#define FRAME_PW TANK_H                         /* the frame on a portrait panel: TANK_H across, TANK_W down */
#define FRAME_PH TANK_W
#define FRAME_ROWS ((STRIPE_BYTES / (FRAME_PW * 2)) & ~1)   /* its rows per stripe: 32 on the 1.8, 28 of the watch's 410 px */

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;          /* kept for DCS writes after init (brightness) */
static uint8_t s_brightness = 0xFF;             /* what init_cmds' 0x51 sets */
static uint16_t *s_stripe[2];                   /* PANEL_W x STRIPE_ROWS, DMA-capable; ping-pong */
static SemaphoreHandle_t s_stripe_free;         /* counts stripe buffers not in DMA flight */
/* where a flush's time goes (2026-10-03): waiting for a stripe buffer to come
 * back from the wire, and inside the driver's send; the rest is the CPU
 * filling stripes from the frame */
static int64_t s_prof_wait, s_prof_send;
static inline void stripe_take(void) {
    int64_t t = esp_timer_get_time();
    xSemaphoreTake(s_stripe_free, portMAX_DELAY);
    s_prof_wait += esp_timer_get_time() - t;
}
void display_port_flush_prof(int64_t *wait_us, int64_t *send_us) { *wait_us = s_prof_wait; *send_us = s_prof_send; s_prof_wait = s_prof_send = 0; }
static i2c_master_bus_handle_t s_i2c;
static bool s_v2;
static bool s_round;                            /* the 1.75C: no expander, a round 466 px CO5300 */
static bool s_watch;                            /* the 2.06 watch: no expander, a 410 x 502 CO5300, an RTC chip */
static bool s_sq;                               /* the 2.16: the round board's path (s_round too) on a 480 px square */
static int  s_rp = R_PANEL;                     /* the round path's square panel: 466 (the 1.75C) or 480 (the 2.16) */
static int  s_pw = PANEL_W, s_ph = PANEL_H;     /* the portrait panel this board has */
static int  s_fx, s_fy;                         /* ... and where the frame's corner sits on it (even, as the CO5300 wants) */
static bool s_border_clear;                     /* a frame smaller than its panel: the glass around it is owed its black */
static bool s_expander;                         /* the 1.8's TCA9554 answered */
static bool s_inverted;                         /* 180-degree flip, done in the transpose */
/* the round panel's view: where the tank's picture sits on the 466 px square */
#define FIT_W 360                               /* 448 x 368 at 4/5: 90 x 74 groups of 5 px -> 4 */
#define FIT_H 296
static int  s_view = DISPLAY_VIEW_FIT;
static int  s_ox, s_oy, s_ow, s_oh;             /* the picture's window on the panel (even, as the CO5300 wants) */
static bool s_round_clear;                      /* the glass around the window is owed its black (init, wake, a view change) */
static uint8_t s_fw[4][4][4];                   /* FIT: [row in group][px in group] -> weights /32 of a[k] a[k+1] b[k] b[k+1] */

void display_port_set_inverted(bool inverted) { s_inverted = inverted; }
/* the 2.16's square turns all four ways (2026-10-04), in the panel itself:
 * MADCTL per quarter turn clockwise, set between frames. 60 is upright (the
 * init's), A0 its half turn; 00 / C0 the sideways pair (SQ_MAD_CW below). */
#define SQ_MAD_CW  0x00                         /* the quarter turn clockwise (bench, 2026-10-04); C0 the other */
static const uint8_t SQ_MAD[4] = { 0x60, SQ_MAD_CW, 0xA0, SQ_MAD_CW ^ 0xC0 };
static int s_rot_want, s_rot_set;               /* asked / in the panel (-1: the init's own, re-sent after a wake) */
void display_port_set_rotation(int quarter) { s_rot_want = quarter & 3; }
bool board_is_round(void) { return s_round; }
bool board_is_watch(void) { return s_watch; }
bool board_is_sq216(void) { return s_sq; }
int  board_round_panel(void) { return s_rp; }
int  board_pwr_sense_pin(void) { return s_sq ? S_PIN_PWR_SENSE : s_round ? R_PIN_PWR_SENSE : s_watch ? W_PIN_PWR_SENSE : -1; }
void display_port_frame_origin(int *px, int *py) { *px = s_fx; *py = s_fy; }
/* the boards whose resets are GPIOs (the 1.75C, the watch): the pads held through a deep sleep */
typedef struct { gpio_num_t pin; int level; } held_t;
static const held_t ROUND_HELD[] = { { R_PIN_LCD_RST, 1 }, { R_PIN_TP_RST, 0 }, { PIN_LCD_CS, 1 } };
static const held_t WATCH_HELD[] = { { W_PIN_LCD_RST, 1 }, { W_PIN_TP_RST, 0 }, { PIN_LCD_CS, 1 } };
static const held_t SQ_HELD[]    = { { S_PIN_LCD_RST, 1 }, { S_PIN_TP_RST, 0 }, { PIN_LCD_CS, 1 } };
#define HELD_N 3
static const held_t *held_pins(void) { return s_sq ? SQ_HELD : s_round ? ROUND_HELD : s_watch ? WATCH_HELD : NULL; }
static gpio_num_t pin_lcd_rst(void) { return s_sq ? S_PIN_LCD_RST : s_round ? R_PIN_LCD_RST : s_watch ? W_PIN_LCD_RST : GPIO_NUM_NC; }
static gpio_num_t pin_tp_rst(void)  { return s_sq ? S_PIN_TP_RST : s_round ? R_PIN_TP_RST : W_PIN_TP_RST; }
static void dcs(uint8_t cmd, const uint8_t *param, size_t n);
void display_port_deep_sleep_pins(bool tp_awake_high) {
    const held_t *h = held_pins();
    if (!h) return;
    for (size_t i = 0; i < HELD_N; i++) {
        int level = h[i].pin == pin_tp_rst() && tp_awake_high ? 1 : h[i].level;
        gpio_reset_pin(h[i].pin);                     /* off the SPI / panel routing, a GPIO again */
        gpio_set_direction(h[i].pin, GPIO_MODE_OUTPUT);
        gpio_set_level(h[i].pin, level);
        gpio_hold_en(h[i].pin);
    }
}
/* the round board's QSPI lines through the night (2026-10-03). The 1.8 cuts
 * the panel's power at sleep; here the panel stays powered on VCC3V3, and
 * the deep-sleep hold leaves every un-held pad isolated - the clock and the
 * four data lines float into a powered chip. The night drew ~8 mA with
 * everything else asleep (1.63 %/h of a ~500 mAh cell). Driven low, held. */
static const gpio_num_t ROUND_BUS[] = { R_PIN_LCD_PCLK, PIN_LCD_DATA0, PIN_LCD_DATA1, PIN_LCD_DATA2, PIN_LCD_DATA3 };
void display_port_deep_sleep_bus(void) {
    if (!s_round) return;
    for (size_t i = 0; i < sizeof ROUND_BUS / sizeof ROUND_BUS[0]; i++) {
        gpio_reset_pin(ROUND_BUS[i]);
        gpio_set_direction(ROUND_BUS[i], GPIO_MODE_OUTPUT);
        gpio_set_level(ROUND_BUS[i], 0);
        gpio_hold_en(ROUND_BUS[i]);
    }
}
/* the night's deep sleep (2026-10-02): DSTBON 4Fh 01 from the sleep-in the
 * grace left it in (the datasheet allows it there). The panel is then deaf
 * until RESX goes low > 3 ms - the boot's reset - so its reset is held high
 * all night (display_port_deep_sleep_pins). */
void display_port_deep_standby(void) {
    if (!held_pins() || !s_io) return;
    static const uint8_t on = 0x01;
    dcs(0x4F, &on, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
}
bool board_has_expander(void) { return s_expander; }
int  display_port_view(void) { return s_view; }
void display_port_set_view(int view) {
    s_view = view == DISPLAY_VIEW_FULL ? DISPLAY_VIEW_FULL : DISPLAY_VIEW_FIT;
#ifdef TANK_ROUND                               /* the round build's frame IS the panel: one view, px for px */
    s_view = DISPLAY_VIEW_FULL;
#endif
    if (s_sq) s_view = DISPLAY_VIEW_FULL;           /* the 2.16's square holds the whole frame px for px: nothing to fit */
    s_ow = s_view == DISPLAY_VIEW_FULL ? TANK_W : FIT_W; s_oh = s_view == DISPLAY_VIEW_FULL ? TANK_H : FIT_H;
    s_ox = ((s_rp - s_ow) / 2) & ~1; s_oy = ((s_rp - s_oh) / 2) & ~1;
    s_round_clear = true;
}
void display_port_panel_to_tank(int px, int py, float *tx, float *ty) {
    float i = (float)(px - s_ox), j = (float)(py - s_oy);   /* upright: the touch port turns it after its calibration */
    if (s_view == DISPLAY_VIEW_FIT) { i = i * 1.25f + 0.5f; j = j * 1.25f + 0.5f; }
    *tx = i; *ty = j;
}

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

/* the 1.75C's init, from Waveshare's BSP: the 1.8's sequence behind two
 * page-0x20 writes, the window 466 wide from the panel's 6 px gap */
static const sh8601_lcd_init_cmd_t round_init_cmds[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 0},
    {0x11, NULL, 0, 120},
    {0x29, NULL, 0, 0},
};

/* the watch's init, from Waveshare's BSP (esp32_s3_touch_amoled_2_06): sleep
 * out first, the tear line at row 465, the window 410 wide from the panel's
 * 22 px gap, 502 tall; lit at brightness 0 and raised once the picture is on */
static const sh8601_lcd_init_cmd_t watch_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x63, (uint8_t[]){0xFF}, 1, 10},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x16, 0x01, 0xAF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xF5}, 4, 0},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};
/* the 2.16's init, from Waveshare's BSP (esp32_s3_touch_amoled_2_16): the
 * 1.75C's sequence, sleep out first, the window the whole 480 x 480 (no gap).
 * MADCTL 60 (MX | MV), the BSP's A0 (MY | MV) turned 180 degrees: the BSP's
 * upright has the keys at the foot; held in a hand they belong on top */
static const sh8601_lcd_init_cmd_t sq_init_cmds[] = {
    {0x11, NULL, 0, 120},
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x36, (uint8_t[]){0x60}, 1, 0},
    {0x29, NULL, 0, 20},
};

/* The watch's panel power: DSI_PWR_EN is pulled up to the PMIC's ALDO2, and
 * the ES7210 lives whole on ALDO1 (off, it clamps this very bus - the 1.75C's
 * lesson). The PMIC keeps its rail switches across a reset while a cell is
 * connected, so whatever ran before may have left them off: both on, here,
 * before the panel is touched (REG 90 bits 0 / 1; battery_port pins them
 * against the boot trim later). */
static void watch_rails_on(void) {
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x34, .scl_speed_hz = 400000 };
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(s_i2c, &cfg, &dev) != ESP_OK) return;
    uint8_t reg = 0x90, v = 0;
    if (i2c_master_transmit_receive(dev, &reg, 1, &v, 1, 100) == ESP_OK && (v & 0x03) != 0x03) {
        uint8_t wr[2] = { 0x90, (uint8_t)(v | 0x03) };
        i2c_master_transmit(dev, wr, 2, 100);
        ESP_LOGW(TAG, "the PMIC had ALDO1/ALDO2 off (REG 90 was %02x): switched on - the panel's power enable hangs on ALDO2", v);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    i2c_master_bus_rm_device(dev);
}

/* a DCS command over the QSPI link, framed as the sh8601 driver frames its own:
 * <write opcode 0x02><cmd><00> in a 32-bit word */
static void dcs(uint8_t cmd, const uint8_t *param, size_t n) {
    if (s_io) esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | (cmd << 8), param, n);
}

static int panel_x_gap(void) { return s_sq ? 0 : s_round ? R_PANEL_X_GAP : s_watch ? W_PANEL_X_GAP : s_v2 ? V2_PANEL_X_GAP : 0; }
static bool probe(uint8_t addr) {                /* three tries: a board is not chosen on one glitch */
    for (int i = 0; i < 3; i++) if (i2c_master_probe(s_i2c, addr, 50) == ESP_OK) return true;
    return false;
}
bool display_port_init(void) {
    i2c_master_bus_config_t bus = { .i2c_port = I2C_NUM_0, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_i2c));
    /* which board: the 1.8 has the TCA9554 expander; the 1.75C has none, and
       an ES7210 the 1.8 lacks. Neither, with the PMIC answering = the 1.8's
       path, as before. Nothing at all = a dead bus (the 1.75C with A3V3 off
       is exactly that - battery_port.h): no panel is driven on a guess, the
       pins differ between the boards. */
    s_expander = probe(I2C_ADDR_EXPANDER);
    bool mic = !s_expander && probe(I2C_ADDR_ES7210);
    s_watch = mic && probe(I2C_ADDR_RTC);            /* the watch has the 1.75C's ES7210 AND an RTC chip (on the always-on RTC rail) */
    s_round = mic && !s_watch;
#ifdef BOARD_SQ216                                    /* the 2.16 has the watch's tell (ES7210 + RTC): this build says which it is */
    s_sq = s_round = mic; s_watch = false;
    if (!s_sq) {
        ESP_LOGE(TAG, "this is the 2.16 build (480 x 480) and the board has no ES7210 without an expander: it has no picture here - flash this board's own image");
        return false;
    }
#endif
    if (!s_expander && !mic && !probe(0x34)) {
        ESP_LOGE(TAG, "nothing answers on the I2C bus (no expander, no ES7210, no PMIC): the board cannot be told, the panel stays dark - "
                      "power the board off and on (unplug USB; with a battery, hold PWR until it cuts, then press it)");
        return false;
    }
#ifdef TANK_ROUND
    if (!s_round) {
        ESP_LOGE(TAG, "this is the ROUND build (a 466 px bowl) and the board is not the 1.75C: it has no picture here - flash the 1.8's image");
        return false;
    }
#endif
#ifdef TANK_WATCH
    if (!s_watch) {
        ESP_LOGE(TAG, "this is the WATCH build (a 502 x 410 tank) and the board is not the 2.06 watch: it has no picture here - flash this board's own image");
        return false;
    }
#endif
    if (s_watch) {
        ESP_LOGI(TAG, "board: the WATCH, 2.06 (CO5300 410x502 / FT3168) - no expander, an ES7210 at 0x%02x, an RTC at 0x%02x", I2C_ADDR_ES7210, I2C_ADDR_RTC);
        for (size_t i = 0; i < HELD_N; i++) gpio_hold_dis(WATCH_HELD[i].pin);   /* a deep sleep's holds end here */
        watch_rails_on();
        s_pw = W_PANEL_W; s_ph = W_PANEL_H;
    } else if (s_round) {
        if (s_sq) ESP_LOGI(TAG, "board: the 2.16 (CO5300 480x480 / CST9220) - no expander, an ES7210 at 0x%02x", I2C_ADDR_ES7210);
        else ESP_LOGI(TAG, "board: the ROUND 1.75C (CO5300 466x466 / CST9217) - no expander, an ES7210 at 0x%02x", I2C_ADDR_ES7210);
        s_rp = s_sq ? S_PANEL : R_PANEL;
        for (size_t i = 0; i < HELD_N; i++) gpio_hold_dis(held_pins()[i].pin);   /* a deep sleep's holds end here */
        for (size_t i = 0; i < sizeof ROUND_BUS / sizeof ROUND_BUS[0]; i++) gpio_hold_dis(ROUND_BUS[i]);   /* ... and the QSPI lines' (sleepcfg 8) */
        for (int ky = 0; ky < 4; ky++) for (int kx = 0; kx < 4; kx++) {       /* FIT's area weights: 25ths, as 32nds that sum to 32 */
            int w[4] = { (4 - ky) * (4 - kx), (4 - ky) * (1 + kx), (1 + ky) * (4 - kx), (1 + ky) * (1 + kx) }, sum = 0, big = 0;
            for (int i = 0; i < 4; i++) { w[i] = (w[i] * 32 + 12) / 25; sum += w[i]; if (w[i] > w[big]) big = i; }
            w[big] += 32 - sum;
            for (int i = 0; i < 4; i++) s_fw[ky][kx][i] = (uint8_t)w[i];
        }
        display_port_set_view(s_view);
    } else {
        expander_power_up();
        s_v2 = i2c_master_probe(s_i2c, I2C_ADDR_CST816, 50) == ESP_OK;
        ESP_LOGI(TAG, "board revision: %s", s_v2 ? "V2 (CO5300/CST816)" : "V1 (SH8601/FT3168)");
    }
#ifndef TANK_WATCH                                    /* (the watch's own build: the frame is the panel, origin 0, 0) */
    if (!s_round) {                                   /* a portrait panel: the turned frame centred on it (the whole of it, on the 1.8) */
        s_fx = ((s_pw - FRAME_PW) / 2) & ~1; s_fy = ((s_ph - FRAME_PH) / 2) & ~1;
        s_border_clear = s_fx > 0 || s_fy > 0;
    }
#endif

    for (int i = 0; i < 2; i++) {
        s_stripe[i] = heap_caps_malloc(STRIPE_BYTES, MALLOC_CAP_DMA);
        if (!s_stripe[i]) return false;
    }
    s_stripe_free = xSemaphoreCreateCounting(2, 2);
    const spi_bus_config_t spi = SH8601_PANEL_BUS_QSPI_CONFIG(s_round ? R_PIN_LCD_PCLK : PIN_LCD_PCLK, PIN_LCD_DATA0, PIN_LCD_DATA1,
                                                              PIN_LCD_DATA2, PIN_LCD_DATA3, STRIPE_BYTES);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &spi, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, on_trans_done, NULL);
    io_cfg.pclk_hz = 80 * 1000 * 1000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));
    s_io = io;
    const sh8601_vendor_config_t vendor = { .init_cmds = s_sq ? sq_init_cmds : s_round ? round_init_cmds : s_watch ? watch_init_cmds : init_cmds,
                                            .init_cmds_size = s_sq ? sizeof sq_init_cmds / sizeof sq_init_cmds[0]
                                                            : s_round ? sizeof round_init_cmds / sizeof round_init_cmds[0]
                                                            : s_watch ? sizeof watch_init_cmds / sizeof watch_init_cmds[0] : sizeof init_cmds / sizeof init_cmds[0],
                                            .flags.use_qspi_interface = 1 };
    const esp_lcd_panel_dev_config_t pcfg = { .reset_gpio_num = pin_lcd_rst(), .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                              .bits_per_pixel = 16, .vendor_config = (void *)&vendor };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io, &pcfg, &s_panel));
    if (held_pins()) gpio_sleep_sel_dis(pin_lcd_rst());   /* the reset line keeps its level through the sleep grace's light sleep */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, panel_x_gap(), 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
#ifdef TANK_ROUND
    if (s_round) ESP_LOGI(TAG, "panel up: %d px round, the bowl px for px", R_PANEL);
#else
    if (s_sq) ESP_LOGI(TAG, "panel up: %d px square, the %dx%d tank frame px for px at (%d, %d)", s_rp, TANK_W, TANK_H, s_ox, s_oy);
    else if (s_round) ESP_LOGI(TAG, "panel up: %d px round, the %dx%d tank frame %s", R_PANEL, TANK_W, TANK_H,
                          s_view == DISPLAY_VIEW_FULL ? "px for px (corners cropped)" : "at 4/5 (all of it inside the circle)");
#endif
#ifdef TANK_WATCH
    else ESP_LOGI(TAG, "panel up: %dx%d portrait, the portrait tank px for px", s_pw, s_ph);
#else
    else if (s_fx || s_fy) ESP_LOGI(TAG, "panel up: %dx%d portrait, the %dx%d tank frame rotated 90 deg and centred (%d, %d)", s_pw, s_ph, TANK_W, TANK_H, s_fx, s_fy);
    else ESP_LOGI(TAG, "panel up: %dx%d portrait, tank frame rotated 90 deg", s_pw, s_ph);
#endif
    return true;
}

/* sleep-mode power-down: display off, then the expander cuts the panel rail
 * (resets low, SD_CS kept high). display_port_init re-sequences it on wake.
 * The round board has no switch on the panel's rail: display off + sleep-in
 * (the wake resets and re-inits it all the same). */
void display_port_sleep(void) {
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, false);
    if (held_pins()) { dcs(0x10, NULL, 0); gpio_set_level(pin_tp_rst(), 0); return; }   /* ... and the touch chip held in reset, as the 1.8's expander holds its own (the watch: the same) */
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
    if (!held_pins()) expander_power_up();
    else gpio_set_level(pin_tp_rst(), 1);           /* the touch chip is reporting again by the time the panel is up */
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_set_gap(s_panel, panel_x_gap(), 0);
    display_port_set_brightness(s_brightness);      /* init_cmds put it back at 255 */
    esp_lcd_panel_disp_on_off(s_panel, true);
    s_round_clear = true;                           /* a reset panel's memory is not black */
    s_rot_set = 0;                                  /* the init put MADCTL back upright */
    s_border_clear = s_fx > 0 || s_fy > 0;
}

/* DCS 0x51 WRDISBV */
void display_port_set_brightness(uint8_t level) {
    s_brightness = level;
    dcs(0x51, &level, 1);
}
uint8_t display_port_brightness(void) { return s_brightness; }

/* ---- the round panel: the 448x368 frame into the circle, no rotation ----
 * A stripe is whole rows of the picture's window, as many as fit the stripe
 * buffer (26 of FULL's 448 px, 32 of FIT's 360); same ping-pong as below.
 * FIT is an area filter: each 5x5 block of the tank becomes 4x4 on the glass,
 * every panel px the 32nds-weighted mix of the four tank px it straddles
 * (s_fw) - the RGB565 channels spread into one 32-bit word (0x07E0F81F) so
 * a px costs four multiplies. The frame's 448 and 368 are 2 px short of 90
 * and 74 whole groups: the last column and row repeat. Flipped: the same
 * picture turned 180 degrees in its window. */
#define RGB_SPREAD(c) ((((uint32_t)(c)) | ((uint32_t)(c) << 16)) & 0x07E0F81Fu)
typedef uint32_t __attribute__((may_alias)) u32a_t;       /* a pair of frame px read as one word */
static inline uint16_t be565(uint32_t c) { return (uint16_t)(((c & 0xFF) << 8) | ((c >> 8) & 0xFF)); }
#ifndef TANK_ROUND
static void round_row_full(const uint16_t *fb, int j, uint16_t *out) {
    if (!s_inverted) {
        const uint16_t *s = fb + j * TANK_W;
        for (int x = 0; x < TANK_W; x++) out[x] = be565(s[x]);
    } else {
        const uint16_t *s = fb + (TANK_H - 1 - j) * TANK_W + TANK_W - 1;
        for (int x = 0; x < TANK_W; x++) out[x] = be565(s[-x]);
    }
}
static void round_row_fit(const uint16_t *fb, int j, uint16_t *out) {
    if (s_inverted) j = FIT_H - 1 - j;
    int ra = 5 * (j >> 2) + (j & 3), rb = ra + 1;
    if (ra > TANK_H - 1) ra = TANK_H - 1;
    if (rb > TANK_H - 1) rb = TANK_H - 1;
    const uint16_t *a = fb + ra * TANK_W, *b = fb + rb * TANK_W;
    const uint8_t (*w)[4] = s_fw[j & 3];
    int step = s_inverted ? -1 : 1;
    uint16_t *o = s_inverted ? out + FIT_W - 1 : out;
    for (int g = 0; g < FIT_W / 4; g++, a += 5, b += 5) {
        uint32_t ea[5], eb[5];
        if (g < FIT_W / 4 - 1) for (int i = 0; i < 5; i++) { ea[i] = RGB_SPREAD(a[i]); eb[i] = RGB_SPREAD(b[i]); }
        else for (int i = 0; i < 5; i++) { int xi = i < 3 ? i : 2; ea[i] = RGB_SPREAD(a[xi]); eb[i] = RGB_SPREAD(b[xi]); }   /* x 445..447, the last repeated */
        for (int k = 0; k < 4; k++, o += step) {
            uint32_t v = ((ea[k] * w[k][0] + ea[k + 1] * w[k][1] + eb[k] * w[k][2] + eb[k + 1] * w[k][3]) >> 5) & 0x07E0F81Fu;
            *o = be565(v | (v >> 16));
        }
    }
}
#endif
static void round_send(int x0, int y0, int w, int n, const uint16_t *stripe) {
    int64_t t = esp_timer_get_time();
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x0, y0, x0 + w, y0 + n, stripe);
    s_prof_send += esp_timer_get_time() - t;
    if (err != ESP_OK) {
        static int logged;
        if (logged++ < 3) ESP_LOGE(TAG, "draw_bitmap y0=%d: %s", y0, esp_err_to_name(err));
    }
}
#ifdef TANK_ROUND
/* The round build: the frame is the panel's own 466 x 466, and only the
 * circle exists on the glass - each stripe sends the columns its widest row
 * spans (even, as the CO5300 wants), a fifth fewer bytes than the square. */
static void round_flush(const uint16_t *fb) {
    int cur = 0;
    const int rows = 24;
    for (int y0 = 0; y0 < R_PANEL; y0 += rows) {
        int n = R_PANEL - y0 < rows ? R_PANEL - y0 : rows;
        float c = R_PANEL * 0.5f, a = y0 + 0.5f - c, b = y0 + n - 0.5f - c;
        float d = (a < 0 && b > 0) ? 0 : (a < 0 ? -b : a);          /* the stripe's row nearest the centre line */
        float half = sqrtf(c * c - d * d);
        int x0 = (int)(c - half) & ~1, x1 = ((int)(c + half) + 2) & ~1;
        if (x0 < 0) x0 = 0;
        if (x1 > R_PANEL) x1 = R_PANEL;
        int w = x1 - x0;
        stripe_take();
        uint16_t *stripe = s_stripe[cur];
        for (int r = 0; r < n; r++) {
            uint16_t *o = stripe + r * w;
            /* two px a step (2026-10-03: the fill was 20 of the flush's 22 ms, the wire never the wait):
               x0 and w are even and the frame 4-aligned, so each pair is one aligned 32-bit load, a
               byte swap inside both halves, one store */
            uint32_t *o32 = (uint32_t *)o;
            if (!s_inverted) {
                const u32a_t *s = (const u32a_t *)(fb + (y0 + r) * TANK_W + x0);
                for (int x = 0; x < w / 2; x++) { uint32_t v = s[x]; o32[x] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu); }
            } else {
                const u32a_t *s = (const u32a_t *)(fb + (TANK_H - 1 - y0 - r) * TANK_W + (TANK_W - 2 - x0));   /* the pair ending at the mirrored px */
                for (int x = 0; x < w / 2; x++) { uint32_t v = s[-x]; v = (v >> 16) | (v << 16); o32[x] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu); }
            }
        }
        round_send(x0, y0, w, n, stripe);
        cur ^= 1;
    }
}
#else
static void round_flush(const uint16_t *fb) {
    int cur = 0;
    if (s_round_clear) {                               /* the whole square black, once: the picture's window covers its part again below */
        s_round_clear = false;
        int rows = (STRIPE_BYTES / (s_rp * 2)) & ~1;
        for (int y0 = 0; y0 < s_rp; y0 += rows) {
            int n = s_rp - y0 < rows ? s_rp - y0 : rows;
            stripe_take();
            memset(s_stripe[cur], 0, (size_t)s_rp * n * 2);
            round_send(0, y0, s_rp, n, s_stripe[cur]);
            cur ^= 1;
        }
    }
    int rows = (STRIPE_BYTES / (s_ow * 2)) & ~1;
    for (int y0 = 0; y0 < s_oh; y0 += rows) {
        int n = s_oh - y0 < rows ? s_oh - y0 : rows;
        stripe_take();
        uint16_t *stripe = s_stripe[cur];
        if (s_view == DISPLAY_VIEW_FULL) for (int r = 0; r < n; r++) round_row_full(fb, y0 + r, stripe + r * s_ow);
        else for (int r = 0; r < n; r++) round_row_fit(fb, y0 + r, stripe + r * s_ow);
        round_send(s_ox, s_oy + y0, s_ow, n, stripe);
        cur ^= 1;
    }
}
#endif

/* landscape fb[y][x] (TANK_W x TANK_H) -> portrait panel: px = y, py = TANK_W-1-x.
 * Colors are byte-swapped for the panel (big-endian RGB565 over SPI).
 * The transpose walks the PSRAM framebuffer row-sequentially (16 contiguous
 * pixels per row segment = one cache line) and scatters into the internal
 * stripe buffer; two stripe buffers ping-pong so the transpose of stripe N+1
 * overlaps the DMA of stripe N. The frame is FRAME_PW x FRAME_PH on the panel,
 * FRAME_ROWS of it a stripe (the last may be short), at (s_fx, s_fy): the
 * whole panel on the 1.8 and on the watch's own build. */
#ifdef TANK_WATCH
/* The watch's own build: the frame is the panel's 410 x 502, row for row -
 * no turn. As many whole rows as fit a stripe buffer (28); flipped, the same
 * picture turned 180 degrees. */
static void native_flush(const uint16_t *fb) {
    int cur = 0;
    const int rows = (STRIPE_BYTES / (TANK_W * 2)) & ~1;
    for (int y0 = 0; y0 < TANK_H; y0 += rows) {
        const int n = TANK_H - y0 < rows ? TANK_H - y0 : rows;
        stripe_take();
        uint16_t *stripe = s_stripe[cur];
        for (int r = 0; r < n; r++) {
            uint32_t *o = (uint32_t *)(stripe + r * TANK_W);        /* two px a store (TANK_W is even, the stripe 4-aligned) */
            if (!s_inverted) {                                      /* ... and two px a load (2026-10-03, as the bowl's fill) */
                const u32a_t *s = (const u32a_t *)(fb + (y0 + r) * TANK_W);
                for (int x = 0; x < TANK_W / 2; x++) { uint32_t v = s[x]; o[x] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu); }
            } else {
                const u32a_t *s = (const u32a_t *)(fb + (TANK_H - 1 - y0 - r) * TANK_W + TANK_W - 2);
                for (int x = 0; x < TANK_W / 2; x++) { uint32_t v = s[-x]; v = (v >> 16) | (v << 16); o[x] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu); }
            }
        }
        round_send(0, y0, TANK_W, n, stripe);
        cur ^= 1;
    }
}
#endif
void display_port_flush(const uint16_t *fb) {
    if (!s_panel) return;
    if (s_sq && s_rot_want != s_rot_set) {             /* both stripes home (no DMA in flight), then the turn */
        stripe_take(); stripe_take();
        dcs(0x36, &SQ_MAD[s_rot_want], 1);
        xSemaphoreGive(s_stripe_free); xSemaphoreGive(s_stripe_free);
        s_rot_set = s_rot_want;
    }
    if (s_round) { round_flush(fb); return; }
#ifdef TANK_WATCH
    native_flush(fb); return;
#else
    int cur = 0;
    if (s_border_clear) {                              /* the whole panel black, once: the frame covers its part again below */
        s_border_clear = false;
        int rows = (STRIPE_BYTES / (s_pw * 2)) & ~1;
        for (int y0 = 0; y0 < s_ph; y0 += rows) {
            int n = s_ph - y0 < rows ? s_ph - y0 : rows;
            stripe_take();
            memset(s_stripe[cur], 0, (size_t)s_pw * n * 2);
            round_send(0, y0, s_pw, n, s_stripe[cur]);
            cur ^= 1;
        }
    }
    for (int py0 = 0; py0 < FRAME_PH; py0 += FRAME_ROWS) {
        const int n = FRAME_PH - py0 < FRAME_ROWS ? FRAME_PH - py0 : FRAME_ROWS;
        stripe_take();
        uint16_t *stripe = s_stripe[cur];
        int x1 = TANK_W - 1 - py0;                    /* source columns x1-(n-1) .. x1 */
        if (!s_inverted) for (int px = 0; px < FRAME_PW; px += 2) {  /* pair adjacent panel columns: one 32-bit store */
            const uint16_t *s0 = fb + px * TANK_W + x1 - (n - 1);
            const uint16_t *s1 = s0 + TANK_W;
            uint32_t *dst = (uint32_t *)(stripe + px);
            for (int r = 0; r < n; r++) {
                uint16_t a = s0[n - 1 - r], b = s1[n - 1 - r];   /* x = x1 - r */
                a = (uint16_t)((a << 8) | (a >> 8)); b = (uint16_t)((b << 8) | (b >> 8));
                dst[r * (FRAME_PW / 2)] = (uint32_t)a | ((uint32_t)b << 16);
            }
        }
        /* 180-degree flip: panel (px,py) = fb[TANK_H-1-px][py] (TANK_W == FRAME_PH).
         * Row segments are read forward instead of backward — same cache pattern. */
        else for (int px = 0; px < FRAME_PW; px += 2) {
            const uint16_t *s0 = fb + (TANK_H - 1 - px) * TANK_W + py0;
            const uint16_t *s1 = s0 - TANK_W;         /* panel column px+1 = the fb row above */
            uint32_t *dst = (uint32_t *)(stripe + px);
            for (int r = 0; r < n; r++) {
                uint16_t a = s0[r], b = s1[r];
                a = (uint16_t)((a << 8) | (a >> 8)); b = (uint16_t)((b << 8) | (b >> 8));
                dst[r * (FRAME_PW / 2)] = (uint32_t)a | ((uint32_t)b << 16);
            }
        }
        round_send(s_fx, s_fy + py0, FRAME_PW, n, stripe);
        cur ^= 1;
    }
#endif
}
