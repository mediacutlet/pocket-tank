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
 * And the SQUARE (the 2.16, 2026-10-12; CO5300, 480x480), GPIO resets like
 * the 1.75C's, the clock on its pin, the panel on VCC3V3 like its. Its own
 * build (TANK_SQUARE) is a tank that IS the panel, sent row for row like the
 * watch's; the rectangle image on it takes the 1.8's path, centred. Which way
 * is up is the panel's MADCTL (S_MADCTL; director `madctl` to try another). */
#include "display_port.h"
#include "board_pins.h"
#include "tank.h"
#include "render.h"
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
#include "freertos/task.h"
#include "esp_async_memcpy.h"
#include "esp32s3/rom/cache.h"
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
/* ---- the ASYNCHRONOUS flush (2026-10-23; Strato: "15 fps is too sluggish",
 * and the same for every board). The flush was the CPU: every frame it read
 * the whole framebuffer out of PSRAM into the DMA stripes - 16-21 ms on every
 * board, 30-40% of a frame - and the next frame waited for it. Now a FLUSH
 * TASK owns the frame once it is drawn: the GDMA (esp_async_memcpy, its own
 * channel) copies each stripe's rows out of PSRAM into the stripe buffer, the
 * task swaps the bytes in place in internal RAM (a few ms a frame, in small
 * pieces), and the SPI DMA takes it to the wire - while the tank task renders
 * the next frame into the other buffer. The tank task hands a frame over
 * (display_port_flush_async) and, before it writes a buffer again, asks for
 * its flush to be over (display_port_flush_wait); a callback tells it a
 * buffer has left the wire, so the scene prefetch into it can start at once.
 * What the DMA cannot gather - the 1.8's 90-degree turn, a 180-degree flip,
 * the bowl's FIT filter - is still a CPU fill, on the flush task: it overlaps
 * the wire and the waits, not the render. */
static TaskHandle_t s_fl_task;
static async_memcpy_handle_t s_fl_amc;          /* the stripe copies' GDMA channel (NULL: CPU fills, as before) */
static SemaphoreHandle_t s_fl_req, s_fl_done, s_fl_copied;
static const uint16_t *volatile s_fl_fb;        /* the frame on its way out (NULL: none) */
static void (*s_fl_cb)(const uint16_t *fb, void *arg); static void *s_fl_arg;
static int64_t s_prof_copy;                     /* waiting on the GDMA copies */
#define FL_DMA_ROWS 16                          /* rows a DMA stripe: 16 x (W x 2) bytes is a multiple of 64 on every glass (W even) */
/* MEASURED on the square 2026-10-23 (4 elders, grown grass): the GDMA copies
 * take 24 ms a frame (PSRAM to internal at ~19 MB/s) and fight the scene
 * prefetch's DMA and the render for the PSRAM bus - the frame went from 70
 * to 80 ms. The bus, not the CPU, is the wall on these boards (docs: "an
 * overlay pixel is bound by PSRAM traffic"). So the copies stay OFF until the
 * whole-frame prefetch is gone (a dirty restore instead); the flush task
 * with CPU fills is a wash (it preempts the render) but costs nothing. */
#define FL_DMA_COPIES 0
#define FL_PSRAM_DIRECT 0                        /* the stripes sent straight from the PSRAM framebuffer (2026-10-23, with the frame in the
                                                 * panel's byte order so the colours were right): the esp_lcd SPI path from PSRAM is not a free
                                                 * DMA - on the flush task above the tank task it took 28 ms a frame and slowed the tick and
                                                 * the render by as much (67 ms a frame, as before); below the tank task it starved (79 ms).
                                                 * The CPU fill, 19 ms, is the cheapest way to feed the wire from PSRAM on this chip. */
#define FL_PRELOAD 0                             /* EXPERIMENT 2026-10-23: the S3's data cache preload engine pulling the NEXT stripe's rows
                                                 * out of PSRAM while the CPU swaps this one: the fill went from 19 to 26 ms. No. */
static bool fl_copy_cb(async_memcpy_handle_t h, async_memcpy_event_t *e, void *arg);
static bool fl_copy(void *dst, const void *src, size_t n) {   /* a stripe's rows out of PSRAM; n rounded up to the cache line (the frame's
                                                               * allocation is, so the last stripe's spill reads inside it) */
    n = (n + 63) & ~(size_t)63;
    int64_t t = esp_timer_get_time();
    if (esp_async_memcpy(s_fl_amc, dst, (void *)src, n, fl_copy_cb, NULL) != ESP_OK) return false;
    xSemaphoreTake(s_fl_copied, portMAX_DELAY);
    s_prof_copy += esp_timer_get_time() - t;
    return true;
}
static bool fl_copy_cb(async_memcpy_handle_t h, async_memcpy_event_t *e, void *arg) {
    (void)h; (void)e; (void)arg;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_fl_copied, &hp);
    return hp == pdTRUE;
}
static inline void fl_swap(uint32_t *o, int words) {        /* two px a word, in internal RAM */
    for (int x = 0; x < words; x++) { uint32_t v = o[x]; o[x] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu); }
}
static i2c_master_bus_handle_t s_i2c;
static bool s_v2;
static bool s_round;                            /* the 1.75C: no expander, a round 466 px CO5300 (and the 1.75: s_175) */
static bool s_175;                              /* the 1.75 / 1.75-B: the round panel, an expander + an ES7210, resets on 39 / 40, an RTC */
static bool s_watch;                            /* the 2.06 watch: no expander, a 410 x 502 CO5300, an RTC chip */
static bool s_square;                           /* the 2.16 square: the watch's I2C signature, a CST92xx touch chip, a 480 x 480 CO5300 */
#define S_MADCTL 0xA0                           /* the panel's orientation: the USB at the FOOT, the picture upright - Waveshare's own
                                                 * "rotation 0" (2026-10-12: an afternoon at 0x60, which is upright USB-UP - the way
                                                 * Strato first held it; his normal grip is USB-down, and update mode, which never
                                                 * reads the IMU, showed its crosses upside down there) */
static uint8_t s_madctl = S_MADCTL;
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
/* ---- on a side (0.3.9.3): the square and the pendant stood on any edge ----
 * A quarter turn is the panel's own MADCTL - the row/column exchange - so it
 * costs the flush nothing; turned over stays the flush's 180 above, and the
 * other side is the two at once. Sent by the flush task between frames (the
 * DCS write waits out the stripes still on the wire). The side's MADCTL and
 * gap are per board, tried on the glass; director `turn` tries others live. */
#define R_MADCTL      0x00                      /* the pendant's way up: the panel's reset value (its init never sends 0x36) */
#define R_MADCTL_SIDE 0x60                      /* a quarter clockwise: rows and columns exchanged */
#define S_MADCTL_SIDE 0x00
static volatile int s_side;                     /* wanted: the picture a quarter turn clockwise on the glass */
static int s_side_sent = -1;                    /* what the panel has (-1: send it, a fresh init) */
static int s_madctl_side = -1;                  /* -1: the board's own (R_ / S_MADCTL_SIDE) */
static int s_side_gx = -1, s_side_gy = -1;      /* the side's gap: -1 = the board's own (below) */
static int panel_x_gap(void);
static void dcs(uint8_t cmd, const uint8_t *param, size_t n);
bool display_port_quarters(void) {
#if defined(TANK_ROUND)
    return s_round;
#elif defined(TANK_SQUARE)
    return s_square;
#else
    return false;                               /* a picture that is not square has no side to stand on */
#endif
}
void display_port_set_quarter(int q) {
    s_inverted = (q & 2) != 0;
    s_side = display_port_quarters() && (q & 1);
}
static uint8_t madctl_side(void) { return s_madctl_side >= 0 ? (uint8_t)s_madctl_side : s_square ? S_MADCTL_SIDE : R_MADCTL_SIDE; }
static void side_gap(int *gx, int *gy) {        /* the square's panel has no gap (the pendant turns in its flush, side_apply) */
    *gx = s_side_gx >= 0 ? s_side_gx : 0;
    *gy = s_side_gy >= 0 ? s_side_gy : panel_x_gap();
}
static void side_apply(void) {
    if (!display_port_quarters() || s_side == s_side_sent) return;
#ifdef TANK_ROUND
    /* the pendant turns its picture in the flush instead (round_flush): on its side the panel takes no row
     * address past the picture's own 466, so its 6 px offset - the round glass's edge - could never be drawn
     * (Strato's photos, 10-30: a strip never drawn along the top with the USB on the left, whatever the gap) */
    s_side_sent = s_side;
    return;
#endif
    uint8_t m = s_side ? madctl_side() : s_square ? s_madctl : R_MADCTL;
    int gx = panel_x_gap(), gy = 0;
    if (s_side) side_gap(&gx, &gy);
    dcs(0x36, &m, 1);
    esp_lcd_panel_set_gap(s_panel, gx, gy);
    s_side_sent = s_side;
}
void display_port_side_tune(int madctl, int gx, int gy) {
    if (madctl >= 0) s_madctl_side = madctl & 0xFF;
    if (gx >= 0) s_side_gx = gx;
    if (gy >= 0) s_side_gy = gy;
    s_side_sent = -1;                           /* the next frame sends it */
}
void display_port_side_info(int *madctl, int *gx, int *gy, bool *on) {
    *madctl = madctl_side(); side_gap(gx, gy); *on = s_side;
}
bool board_is_round(void) { return s_round; }
bool board_is_175(void) { return s_175; }
bool board_is_watch(void) { return s_watch; }
bool board_is_square(void) { return s_square; }
/* the board this IS, by the chips that answered (the probe below), as a release
 * board id (version.h PT_BOARD): NULL when the probe could not tell - the 1.8's
 * path taken on the PMIC alone, or a dead bus. The 1.75 / 1.75-B runs the bowl's
 * image, so it reads as the bowl. */
static volatile bool s_probed;                  /* the chips were asked (display_port_init) */
bool board_port_probed(void) { return s_probed; }
const char *board_port_hw(void) {
    return s_square ? "square216" : s_watch ? "watch206" : s_round ? "round175c" : s_expander ? "amoled18" : NULL;
}
int  board_pwr_sense_pin(void) { return s_175 ? -1 : s_round ? R_PIN_PWR_SENSE : s_watch ? W_PIN_PWR_SENSE : s_square ? S_PIN_PWR_SENSE : -1; }
void display_port_frame_origin(int *px, int *py) { *px = s_fx; *py = s_fy; }
/* the boards whose resets are GPIOs (the 1.75C, the watch): the pads held through a deep sleep */
typedef struct { gpio_num_t pin; int level; } held_t;
static const held_t ROUND_HELD[] = { { R_PIN_LCD_RST, 1 }, { R_PIN_TP_RST, 0 }, { PIN_LCD_CS, 1 } };
static const held_t WATCH_HELD[] = { { W_PIN_LCD_RST, 1 }, { W_PIN_TP_RST, 0 }, { PIN_LCD_CS, 1 } };
static const held_t SQUARE_HELD[] = { { S_PIN_LCD_RST, 1 }, { S_PIN_TP_RST, 0 }, { PIN_LCD_CS, 1 } };
static const held_t B175_HELD[] = { { B_PIN_LCD_RST, 1 }, { B_PIN_TP_RST, 0 }, { PIN_LCD_CS, 1 } };
#define HELD_N 3
static const held_t *held_pins(void) { return s_175 ? B175_HELD : s_round ? ROUND_HELD : s_watch ? WATCH_HELD : s_square ? SQUARE_HELD : NULL; }
static gpio_num_t pin_lcd_rst(void) { return s_175 ? B_PIN_LCD_RST : s_round ? R_PIN_LCD_RST : s_watch ? W_PIN_LCD_RST : s_square ? S_PIN_LCD_RST : GPIO_NUM_NC; }
static gpio_num_t pin_tp_rst(void)  { return s_175 ? B_PIN_TP_RST : s_round ? R_PIN_TP_RST : s_square ? S_PIN_TP_RST : W_PIN_TP_RST; }
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
 * everything else asleep (1.63 %/h of a ~500 mAh cell). Driven low, held.
 * The square's panel sits on VCC3V3 the same way, its clock on the same pin. */
static const gpio_num_t ROUND_BUS[] = { R_PIN_LCD_PCLK, PIN_LCD_DATA0, PIN_LCD_DATA1, PIN_LCD_DATA2, PIN_LCD_DATA3 };
void display_port_deep_sleep_bus(void) {
    if (!s_round && !s_square) return;
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
bool board_has_expander(void) { return s_expander && !s_175; }
int  display_port_view(void) { return s_view; }
void display_port_set_view(int view) {
    s_view = view == DISPLAY_VIEW_FULL ? DISPLAY_VIEW_FULL : DISPLAY_VIEW_FIT;
#ifdef TANK_ROUND                               /* the round build's frame IS the panel: one view, px for px */
    s_view = DISPLAY_VIEW_FULL;
#endif
    s_ow = s_view == DISPLAY_VIEW_FULL ? TANK_W : FIT_W; s_oh = s_view == DISPLAY_VIEW_FULL ? TANK_H : FIT_H;
    s_ox = ((R_PANEL - s_ow) / 2) & ~1; s_oy = ((R_PANEL - s_oh) / 2) & ~1;
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

/* the 1.8's TF card select is the expander's EXIO7 (bit 7): low = selected.
 * The resets stay high. (sd_spike.c holds it low for a whole card visit.) */
void board_sd_select(bool on) {
    if (!s_expander || s_175) return;
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = I2C_ADDR_EXPANDER, .scl_speed_hz = 400000 };
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(s_i2c, &cfg, &dev) != ESP_OK) return;
    uint8_t out[2] = { 0x01, (uint8_t)(on ? 0x07 : 0x87) };
    i2c_master_transmit(dev, out, 2, 100);
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
/* the square's init, from Waveshare's BSP (esp32_s3_touch_amoled_2_16): the
 * 1.75C's sequence (sleep out first, as the watch), the window the whole 480,
 * no gap, and the orientation (0x36) before the display comes on */
static const sh8601_lcd_init_cmd_t square_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
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
    {0x36, (uint8_t[]){S_MADCTL}, 1, 0},
    {0x29, (uint8_t[]){0x00}, 0, 20},
};
/* the square's way up, tried live from the director (`madctl <hex>`): the
 * panel's 0x36, a reset-and-redraw later it is what the next init sends too */
void display_port_set_madctl(uint8_t v) {
    s_madctl = v;
    if (s_io) dcs(0x36, &v, 1);
    s_border_clear = s_fx > 0 || s_fy > 0;
}
uint8_t display_port_madctl(void) { return s_madctl; }

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

static int panel_x_gap(void) { return s_round ? R_PANEL_X_GAP : s_watch ? W_PANEL_X_GAP : s_square ? S_PANEL_X_GAP : s_v2 ? V2_PANEL_X_GAP : 0; }
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
    s_175 = s_expander && probe(I2C_ADDR_ES7210);    /* the 1.75 / 1.75-B: an expander like the 1.8's AND the 1.75C's ES7210 (the 1.8 has none) */
    bool mic = !s_expander && probe(I2C_ADDR_ES7210);
    s_watch = mic && probe(I2C_ADDR_RTC);            /* the watch has the 1.75C's ES7210 AND an RTC chip (on the always-on RTC rail) */
    s_round = s_175 || (mic && !s_watch);
    if (s_watch) {                                   /* ... and so has the 2.16 square: told apart by the touch chip. Its reset let go
                                                        (GPIO 40 - the watch's I2S data line, an output anyway: harmless there) and a
                                                        CST92xx asked for at 0x5A; the watch's FT3168 sits at 0x38 */
        gpio_reset_pin(S_PIN_TP_RST); gpio_set_direction(S_PIN_TP_RST, GPIO_MODE_OUTPUT);
        gpio_set_level(S_PIN_TP_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(S_PIN_TP_RST, 1); vTaskDelay(pdMS_TO_TICKS(60));
        s_square = probe(I2C_ADDR_CST9217);
        if (s_square) s_watch = false;
    }
    s_probed = true;                                 /* board_port_hw() means it from here (the installer's question waits for it) */
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
#ifdef TANK_SQUARE
    if (!s_square) {
        ESP_LOGE(TAG, "this is the SQUARE build (a 480 x 480 tank) and the board is not the 2.16: it has no picture here - flash this board's own image");
        return false;
    }
#endif
    if (s_square) {
        ESP_LOGI(TAG, "board: the SQUARE, 2.16 (CO5300 480x480 / CST92xx at 0x%02x) - no expander, an ES7210 at 0x%02x, an RTC at 0x%02x", I2C_ADDR_CST9217, I2C_ADDR_ES7210, I2C_ADDR_RTC);
        for (size_t i = 0; i < HELD_N; i++) gpio_hold_dis(SQUARE_HELD[i].pin);   /* a deep sleep's holds end here */
        for (size_t i = 0; i < sizeof ROUND_BUS / sizeof ROUND_BUS[0]; i++) gpio_hold_dis(ROUND_BUS[i]);   /* ... and the QSPI lines' */
        s_pw = S_PANEL; s_ph = S_PANEL;
    } else if (s_watch) {
        ESP_LOGI(TAG, "board: the WATCH, 2.06 (CO5300 410x502 / FT3168) - no expander, an ES7210 at 0x%02x, an RTC at 0x%02x", I2C_ADDR_ES7210, I2C_ADDR_RTC);
        for (size_t i = 0; i < HELD_N; i++) gpio_hold_dis(WATCH_HELD[i].pin);   /* a deep sleep's holds end here */
        watch_rails_on();
        s_pw = W_PANEL_W; s_ph = W_PANEL_H;
    } else if (s_round) {
        if (s_175) ESP_LOGI(TAG, "board: the ROUND 1.75 / 1.75-B (CO5300 466x466 / CST9217) - an expander at 0x%02x AND an ES7210 at 0x%02x; resets on %d / %d (BETA)",
                            I2C_ADDR_EXPANDER, I2C_ADDR_ES7210, B_PIN_LCD_RST, B_PIN_TP_RST);
        else ESP_LOGI(TAG, "board: the ROUND 1.75C (CO5300 466x466 / CST9217) - no expander, an ES7210 at 0x%02x", I2C_ADDR_ES7210);
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
#if !defined(TANK_WATCH) && !defined(TANK_SQUARE)     /* (the watch's and the square's own builds: the frame is the panel, origin 0, 0) */
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
    const spi_bus_config_t spi = SH8601_PANEL_BUS_QSPI_CONFIG(s_round ? R_PIN_LCD_PCLK : s_square ? S_PIN_LCD_PCLK : PIN_LCD_PCLK, PIN_LCD_DATA0, PIN_LCD_DATA1,
                                                              PIN_LCD_DATA2, PIN_LCD_DATA3, STRIPE_BYTES);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &spi, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, on_trans_done, NULL);
    io_cfg.pclk_hz = 80 * 1000 * 1000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));
    s_io = io;
    const sh8601_vendor_config_t vendor = { .init_cmds = s_round ? round_init_cmds : s_watch ? watch_init_cmds : s_square ? square_init_cmds : init_cmds,
                                            .init_cmds_size = s_round ? sizeof round_init_cmds / sizeof round_init_cmds[0]
                                                            : s_watch ? sizeof watch_init_cmds / sizeof watch_init_cmds[0]
                                                            : s_square ? sizeof square_init_cmds / sizeof square_init_cmds[0] : sizeof init_cmds / sizeof init_cmds[0],
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
    if (s_round) ESP_LOGI(TAG, "panel up: %d px round, the %dx%d tank frame %s", R_PANEL, TANK_W, TANK_H,
                          s_view == DISPLAY_VIEW_FULL ? "px for px (corners cropped)" : "at 4/5 (all of it inside the circle)");
#endif
#ifdef TANK_WATCH
    else ESP_LOGI(TAG, "panel up: %dx%d portrait, the portrait tank px for px", s_pw, s_ph);
#elif defined(TANK_SQUARE)
    else ESP_LOGI(TAG, "panel up: %dx%d square, the tank px for px (MADCTL %02x)", s_pw, s_ph, s_madctl);
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
    display_port_flush_wait(NULL);                  /* a frame on its way out first */
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
    display_port_flush_wait(NULL);
    if (!s_panel) return;
    if (!held_pins()) expander_power_up();
    else gpio_set_level(pin_tp_rst(), 1);           /* the touch chip is reporting again by the time the panel is up */
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_set_gap(s_panel, panel_x_gap(), 0);
    if (s_square && s_madctl != S_MADCTL) dcs(0x36, &s_madctl, 1);   /* a way up tried from the director outlives the re-init */
    s_side_sent = -1;                               /* ... and a side: the next frame sends it again */
    display_port_set_brightness(s_brightness);      /* init_cmds put it back at 255 */
    esp_lcd_panel_disp_on_off(s_panel, true);
    s_round_clear = true;                           /* a reset panel's memory is not black */
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
#define RGB_SPREAD(c) ((((uint32_t)FB_SWAP16(c)) | ((uint32_t)FB_SWAP16(c) << 16)) & 0x07E0F81Fu)   /* (a frame pixel, native: unswapped under PT_FB_SWAP) */
typedef uint32_t __attribute__((may_alias)) u32a_t;       /* a pair of frame px read as one word */
static inline uint16_t be565(uint32_t c) { return PT_FB_SWAP ? (uint16_t)c : (uint16_t)(((c & 0xFF) << 8) | ((c >> 8) & 0xFF)); }   /* (with PT_FB_SWAP the frame is in the panel's order; FIT's filter unswaps as it mixes) */
#ifndef TANK_ROUND
static void round_row_full(const uint16_t *fb, int j, uint16_t *out) {
    if (!s_inverted) {
        const uint16_t *s = fb + j * TANK_W;
        for (int x = 0; x < TANK_W; x++) out[x] = PT_FB_SWAP ? s[x] : be565(s[x]);
    } else {
        const uint16_t *s = fb + (TANK_H - 1 - j) * TANK_W + TANK_W - 1;
        for (int x = 0; x < TANK_W; x++) out[x] = PT_FB_SWAP ? s[-x] : be565(s[-x]);
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
            { uint32_t m = v | (v >> 16); *o = (uint16_t)(((m & 0xFF) << 8) | ((m >> 8) & 0xFF)); }   /* mixed native -> the panel's order */
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
        if (s_side) {                                  /* on a side (0.3.9.3): turned here, not by the panel (side_apply) - a
                                                          column of the stripe is a run of one frame row, read in order */
            const int step = s_inverted ? -1 : 1;
            for (int px = x0; px < x0 + w; px++) {
                const uint16_t *s = s_inverted ? fb + px * TANK_W + (TANK_W - 1 - y0)      /* the other side: the turn and the flip */
                                               : fb + (TANK_H - 1 - px) * TANK_W + y0;      /* a quarter clockwise */
                uint16_t *o = stripe + (px - x0);
                for (int r = 0; r < n; r++, s += step, o += w) { uint16_t v = *s; *o = PT_FB_SWAP ? v : (uint16_t)((v << 8) | (v >> 8)); }
            }
        } else
        for (int r = 0; r < n; r++) {
            uint16_t *o = stripe + r * w;
            /* two px a step (2026-10-03: the fill was 20 of the flush's 22 ms, the wire never the wait):
               x0 and w are even and the frame 4-aligned, so each pair is one aligned 32-bit load, a
               byte swap inside both halves, one store */
            uint32_t *o32 = (uint32_t *)o;
            if (!s_inverted) {
                const u32a_t *s = (const u32a_t *)(fb + (y0 + r) * TANK_W + x0);
                for (int x = 0; x < w / 2; x++) { uint32_t v = s[x]; o32[x] = PT_FB_SWAP ? v : ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu); }
            } else {
                const u32a_t *s = (const u32a_t *)(fb + (TANK_H - 1 - y0 - r) * TANK_W + (TANK_W - 2 - x0));   /* the pair ending at the mirrored px */
                for (int x = 0; x < w / 2; x++) { uint32_t v = s[-x]; v = (v >> 16) | (v << 16); o32[x] = PT_FB_SWAP ? v : ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu); }
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
        int rows = (STRIPE_BYTES / (R_PANEL * 2)) & ~1;
        for (int y0 = 0; y0 < R_PANEL; y0 += rows) {
            int n = R_PANEL - y0 < rows ? R_PANEL - y0 : rows;
            stripe_take();
            memset(s_stripe[cur], 0, (size_t)R_PANEL * n * 2);
            round_send(0, y0, R_PANEL, n, s_stripe[cur]);
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
#if defined(TANK_WATCH) || defined(TANK_SQUARE)
static void native_flush(const uint16_t *fb);
/* The watch's own build: the frame is the panel's 410 x 502, row for row -
 * no turn. As many whole rows as fit a stripe buffer (28; 24 of the square's
 * 480); flipped, the same picture turned 180 degrees. */
static void native_flush_dma(const uint16_t *fb) {               /* the rows out of PSRAM by DMA, swapped in place, sent */
    int cur = 0;
#if FL_PSRAM_DIRECT
    for (int s0 = 0; s0 < TANK_H; s0 += FL_DMA_ROWS) {
        const int n = TANK_H - s0 < FL_DMA_ROWS ? TANK_H - s0 : FL_DMA_ROWS;
        round_send(0, s0, TANK_W, n, fb + s0 * TANK_W);
    }
    return;
#endif
    for (int s0 = 0; s0 < TANK_H; s0 += FL_DMA_ROWS) {           /* source blocks, each at a cache-line offset */
        const int n = TANK_H - s0 < FL_DMA_ROWS ? TANK_H - s0 : FL_DMA_ROWS;
        stripe_take();
        uint16_t *stripe = s_stripe[cur];
        if (!fl_copy(stripe, fb + s0 * TANK_W, (size_t)n * TANK_W * 2)) { native_flush(fb); return; }   /* (the CPU way, whole) */
        int y0 = s0;
        if (!s_inverted) { if (!PT_FB_SWAP) fl_swap((uint32_t *)stripe, n * TANK_W / 2); }
        else {                                                   /* the 180-degree flip: the block's pixels in reverse order IS the block
                                                                  * turned (rows and pixels at once), swapped on the way; it lands where the
                                                                  * turned frame has it */
            uint16_t *a = stripe, *b = stripe + n * TANK_W - 1;
            for (; a < b; a++, b--) { uint16_t va = *a, vb = *b; *a = PT_FB_SWAP ? vb : (uint16_t)((vb << 8) | (vb >> 8)); *b = PT_FB_SWAP ? va : (uint16_t)((va << 8) | (va >> 8)); }
            y0 = TANK_H - s0 - n;
        }
        round_send(0, y0, TANK_W, n, stripe);
        cur ^= 1;
    }
}
static void native_flush(const uint16_t *fb) {
    int cur = 0;
    const int rows = (STRIPE_BYTES / (TANK_W * 2)) & ~1;
    uint32_t autoload = 0;
    if (FL_PRELOAD) { autoload = Cache_Start_DCache_Preload((uint32_t)fb, (uint32_t)(rows * TANK_W * 2), 0); while (!Cache_DCache_Preload_Done()) { } }
    for (int y0 = 0; y0 < TANK_H; y0 += rows) {
        const int n = TANK_H - y0 < rows ? TANK_H - y0 : rows;
        if (FL_PRELOAD && y0 + rows < TANK_H) {        /* the next stripe's rows into the cache while this one is swapped */
            int n2 = TANK_H - (y0 + rows) < rows ? TANK_H - (y0 + rows) : rows;
            Cache_Start_DCache_Preload((uint32_t)(fb + (y0 + rows) * TANK_W), (uint32_t)(n2 * TANK_W * 2), 0);
        }
        stripe_take();
        uint16_t *stripe = s_stripe[cur];
        for (int r = 0; r < n; r++) {
            uint32_t *o = (uint32_t *)(stripe + r * TANK_W);        /* two px a store (TANK_W is even, the stripe 4-aligned) */
            if (!s_inverted) {                                      /* ... and two px a load (2026-10-03, as the bowl's fill) */
                const u32a_t *s = (const u32a_t *)(fb + (y0 + r) * TANK_W);
                for (int x = 0; x < TANK_W / 2; x++) { uint32_t v = s[x]; o[x] = PT_FB_SWAP ? v : ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu); }
            } else {
                const u32a_t *s = (const u32a_t *)(fb + (TANK_H - 1 - y0 - r) * TANK_W + TANK_W - 2);
                for (int x = 0; x < TANK_W / 2; x++) { uint32_t v = s[-x]; v = (v >> 16) | (v << 16); o[x] = PT_FB_SWAP ? v : ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu); }
            }
        }
        round_send(0, y0, TANK_W, n, stripe);
        cur ^= 1;
    }
    if (FL_PRELOAD) Cache_End_DCache_Preload(autoload);
}
#endif
/* director `turn probe` (0.3.9.3, the pendant's strip on a side): the panel's whole memory white once, then
 * every frame two px bands of eight colours along the picture's four edges, the outermost first - red,
 * yellow, green, cyan, blue, magenta, white, orange - so a photo shows what the glass cuts off and what it
 * shows of memory never drawn (white) */
static volatile bool s_probe, s_probe_white;
void display_port_probe(bool on) { s_probe = on; s_probe_white = on; s_side_sent = -1; }
static void probe_rect(int x, int y, int w, int h, uint16_t rgb565) {
    uint16_t v = (uint16_t)((rgb565 >> 8) | (rgb565 << 8));   /* the panel takes it big-endian */
    static int cur;
    int rows = (STRIPE_BYTES / (w * 2)); if (rows > h) rows = h;
    for (int y0 = 0; y0 < h; y0 += rows) {
        int n = h - y0 < rows ? h - y0 : rows;
        stripe_take();                            /* (the ping-pong: a buffer back from the wire) */
        for (int i = 0; i < w * n; i++) s_stripe[cur][i] = v;
        round_send(x, y + y0, w, n, s_stripe[cur]);
        cur ^= 1;
    }
}
static void probe_draw(void) {
    if (s_probe_white) {
        s_probe_white = false;
        esp_lcd_panel_set_gap(s_panel, 0, 0);
        probe_rect(0, 0, 480, 480, 0xFFFF);
        s_side_sent = -1; side_apply();
    }
    static const uint16_t BANDS[8] = { 0xF800, 0xFFE0, 0x07E0, 0x07FF, 0x001F, 0xF81F, 0xFFFF, 0xFC00 };
    for (int b = 0; b < 8; b++) {
        int e = b * 2;
        probe_rect(0, e, TANK_W, 2, BANDS[b]); probe_rect(0, TANK_H - 2 - e, TANK_W, 2, BANDS[b]);
        probe_rect(e, 0, 2, TANK_H, BANDS[b]); probe_rect(TANK_W - 2 - e, 0, 2, TANK_H, BANDS[b]);
    }
}
static void flush_now(const uint16_t *fb) {
    if (!s_panel) return;
    side_apply();
    if (s_probe) { probe_draw(); return; }
    if (s_round) { round_flush(fb); return; }
#if defined(TANK_WATCH) || defined(TANK_SQUARE)
    if (s_fl_amc || FL_PSRAM_DIRECT) native_flush_dma(fb); else native_flush(fb);
    return;
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
                if (!PT_FB_SWAP) { a = (uint16_t)((a << 8) | (a >> 8)); b = (uint16_t)((b << 8) | (b >> 8)); }   /* (the frame is in the panel's order already) */
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
                if (!PT_FB_SWAP) { a = (uint16_t)((a << 8) | (a >> 8)); b = (uint16_t)((b << 8) | (b >> 8)); }   /* (the frame is in the panel's order already) */
                dst[r * (FRAME_PW / 2)] = (uint32_t)a | ((uint32_t)b << 16);
            }
        }
        round_send(s_fx, s_fy + py0, FRAME_PW, n, stripe);
        cur ^= 1;
    }
#endif
}

static int64_t s_fl_us; static int s_fl_frames;
/* the flush task: one frame at a time, as handed over */
static void fl_task(void *arg) {
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_fl_req, portMAX_DELAY);
        const uint16_t *fb = s_fl_fb;
        int64_t t0 = esp_timer_get_time();
        flush_now(fb);
        s_fl_us += esp_timer_get_time() - t0; s_fl_frames++;
        s_fl_fb = NULL;
        if (s_fl_cb) s_fl_cb(fb, s_fl_arg);
        xSemaphoreGive(s_fl_done);
    }
}
void display_port_flush_wait(const uint16_t *fb) {           /* until no flush of fb (NULL: of any frame) is in flight */
    if (!s_fl_task) return;
    if (fb && s_fl_fb != fb && s_fl_fb != NULL) return;      /* another frame is out: fb is free */
    if (s_fl_fb == NULL) return;
    xSemaphoreTake(s_fl_done, portMAX_DELAY);               /* the task gives it once per frame; taken here, it is ours */
    xSemaphoreGive(s_fl_done);
}
bool display_port_flush_async(const uint16_t *fb, void (*done)(const uint16_t *fb, void *arg), void *arg) {
    if (!s_panel) return false;
    if (!s_fl_task) {                                       /* the first call sets the task up (after the panel and the SPI have their DMA) */
        s_fl_req = xSemaphoreCreateBinary(); s_fl_done = xSemaphoreCreateBinary(); s_fl_copied = xSemaphoreCreateBinary();
        async_memcpy_config_t cfg = ASYNC_MEMCPY_DEFAULT_CONFIG();
        cfg.backlog = 2; cfg.sram_trans_align = 4; cfg.psram_trans_align = 64;
        if (!FL_DMA_COPIES || esp_async_memcpy_install(&cfg, &s_fl_amc) != ESP_OK) { s_fl_amc = NULL; ESP_LOGW(TAG, "flush: no GDMA channel for the stripe copies - CPU fills on the flush task"); }
        if (xTaskCreatePinnedToCore(fl_task, "flush", 4096, NULL, 5, &s_fl_task, 0) != pdPASS) { s_fl_task = NULL; return false; }
        xSemaphoreGive(s_fl_done);
        ESP_LOGI(TAG, "flush: asynchronous, %s", s_fl_amc ? "the stripes copied by GDMA" : "CPU fills");
    }
    if (s_border_clear || s_round_clear) return false;      /* a clear owed: the plain flush does it */
    display_port_flush_wait(NULL);                          /* one frame on its way at a time */
    xSemaphoreTake(s_fl_done, 0);                           /* (it was given: ours now, until the task gives it back) */
    s_fl_fb = fb; s_fl_cb = done; s_fl_arg = arg;
    xSemaphoreGive(s_fl_req);
    return true;
}
void display_port_flush(const uint16_t *fb) {               /* the plain flush, whole, here and now */
    display_port_flush_wait(NULL);
    flush_now(fb);
}
void display_port_flush_async_prof(int64_t *task_us, int *frames, int64_t *copy_us) {
    *task_us = s_fl_us; *frames = s_fl_frames; *copy_us = s_prof_copy; s_fl_us = s_prof_copy = 0; s_fl_frames = 0;
    if (*frames) ESP_LOGI(TAG, "flush: task %s, gdma %s, inverted %d, dma rows %d", s_fl_task ? "up" : "none", s_fl_amc ? "yes" : "NO", (int)s_inverted, FL_DMA_ROWS);
}
