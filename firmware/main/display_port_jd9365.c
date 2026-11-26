/* display_port_jd9365.c - the HOME TANK: Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1
 * (2026-10-12). A 10.1" IPS panel, 800 x 1280 native portrait, a JD9365
 * driver over two MIPI-DSI lanes; the P4's DSI host scans one of its own
 * frame buffers out continuously (DPI mode: the panel is a monitor, not a
 * smart panel with its own RAM like the AMOLEDs).
 *
 * The tank is the glass, 1280 x 800 (TANK_P4, common/tank.h), px for px:
 * display_port_flush hands the frame to the P4's PPA (the pixel-processing
 * accelerator) which turns it 90 degrees straight into the
 * DSI frame buffer that is NOT on the glass, then the DPI driver switches to
 * that buffer at the next frame boundary. Two DSI buffers (2 MB each, PSRAM),
 * no tearing, no CPU copy. The way up (90 or 270: which long edge is the
 * foot) is the keeper's ROTATION setting through display_port_set_inverted,
 * as on the 1.8; P4_ROT_DEFAULT is the board on its stand.
 *
 * Sources: Waveshare's BSP component waveshare/esp32_p4_wifi6_touch_lcd_x
 * 2.0.3 (the panel's init table below, the DSI numbers, the pins) and the
 * schematic in resources/ESP32-P4-Touch-LCD-10.1/. No PMIC, no IMU, no RTC
 * chip on this board: every board_* probe here answers "no".
 *
 * THE TAB5 (TANK_TAB5, 2026-10-19) is the same display path on M5Stack's 5"
 * 720 x 1280 panel: everything below the panel's bring-up is shared (the PPA's
 * turn, the tiles, the two DSI buffers). Its bring-up is tab5_panel_up():
 * the two I/O expanders, the panel's reset through one of them (M5's way: the
 * line is driven low as an output and RELEASED as an input with its pull-up -
 * the panel has no pull-up of its own to the expander's 3.3 V), which of the
 * three panels this is (the touch chip tells), and that panel's init table
 * sent over the DSI's command channel before the DPI panel starts. */
#include "display_port.h"
#include "board_pins.h"
#include "tank.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/ppa.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_jd9365.h"
#include "esp_cache.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "jd9365";
#define P4_FB_BYTES   (P4_PANEL_W * P4_PANEL_H * 2)     /* one DSI frame buffer, RGB565 */
#define P4_ROT_DEFAULT PPA_SRM_ROTATION_ANGLE_90        /* the picture's way up on the stand: 270 is the other long edge down */
#define BL_TIMER      LEDC_TIMER_1
#define BL_CHANNEL    LEDC_CHANNEL_1
#define BL_BITS       LEDC_TIMER_10_BIT

static i2c_master_bus_handle_t s_i2c;
static esp_lcd_dsi_bus_handle_t s_dsi;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static esp_ldo_channel_handle_t s_ldo;
static ppa_client_handle_t s_ppa, s_ppa128;         /* the same operation at the PPA's short (16 B) and full (128 B) bursts */
static ppa_client_handle_t s_ppa_rs;               /* THE RESTORE BESIDE THE TICK's (below) */
static void restore_start(void);
static uint8_t *s_fb[2];                      /* the DSI driver's two frame buffers (PSRAM) */
static int s_cur;                             /* the one on the glass */
static SemaphoreHandle_t s_switched;           /* the DSI DMA has moved on to the buffer last handed over: the other is free */
static bool s_inverted, s_lit, s_on;
static uint8_t s_brightness = 0xFF;
static int64_t s_wait_us, s_send_us;
static bool s_gt911, s_codec;                 /* what answered on the I2C bus at boot (a log line, nothing more) */
/* THE WHITE FLASHES (Strato, from the first picture: one or two a few seconds apart) and
 * the bench's bisect that found them, director `p4 <mask>`: 1 = no flush at all (a frozen
 * frame: clean), 2 = the PPA writes the buffer ON the glass, no switching (still flashed),
 * 4 = no PPA - a steady gray the CPU fills, then the switch (clean): the PPA's ONE 2 MB
 * transfer starves the DSI's frame DMA, though the bridge never reports an underrun.
 * What holds: the PPA works the frame in P4_PPA_STRIPS strips, back to back - the gaps
 * between the operations are the DSI's (the default; 128 = the one whole transfer, for
 * the record; 64 = at the PPA's short 16-byte bursts (the default is the full 128: with the
 * strips it is clean too, and 6 ms a frame quicker), 32 = a tick's breather between the
 * strips, 256 = the strips as the input's row bands (column bands by default); 8 = the scene
 * prefetch DMA off, in main.c). */
static int s_dbg;
#define P4_PPA_STRIPS 8
static bool turn_start(void);                 /* THE PIPELINE, below */
/* ONLY WHAT CHANGED (2026-10-12, the 1x world): a whole 2 MB frame through the PPA's turn
 * is 50 ms, so the port turns the frame's dynamic tiles only - this frame's rectangles
 * (render_changed_rects) and overlay tiles (render_overlay), handed over by main.c,
 * plus the tiles the target buffer still shows from the frame it holds. Which tiles, for
 * which buffer, is common/p4_tiles.c - included here, and by the simulator's
 * --selftest-tiles, which runs the same bookkeeping against a full render; this file is
 * the PPA doing the copies (p4_ops, below). Everything goes when main.c says so (a page
 * that covers the glass, the scene baked again), on the first frames, and once every
 * P4_FULL_EVERY frames for safety. 512 = the bisect's "always everything"; 1024 = main.c's
 * old rule, "everything" under any overlay (the bench: before / after on one build). */
#include "../../common/p4_tiles.c"
void display_port_p4_algae(const uint8_t *algae) { p4_tiles_algae(algae); }
void display_port_p4_changed(const short *xy, int n) { p4_tiles_changed(xy, n); }
void display_port_p4_changed_page(const short *xy, int n, int src) { p4_tiles_changed_page(xy, n, src); }   /* A KEPT PAGE's frame (common/p4_tiles.c) */
void display_port_p4_tiles(const render_overlay_t *o) { p4_tiles_overlay(o); }
void display_port_p4_tile_stats(unsigned *turned, unsigned *spared) { *turned = s_turned; *spared = s_spared; s_turned = s_spared = 0; }
static unsigned s_ops_turn, s_ops_put;            /* PPA operations (the bench) */
void display_port_p4_restore_stats(unsigned *restored, unsigned *film_only, unsigned *rects) { *restored = s_restored; *film_only = s_restored_film; *rects = s_rect_tiles; s_restored = s_restored_film = s_rect_tiles = 0; }
void display_port_p4_ops(unsigned *turn, unsigned *put) { *turn = s_ops_turn; *put = s_ops_put; s_ops_turn = s_ops_put = 0; }
int  display_port_p4_debug_mask(void) { return s_dbg; }
void display_port_p4_debug(int mask) { s_dbg = mask; ESP_LOGI(TAG, "debug mask %d: %s%s%s%s%s%s%s%s", mask, mask & 1 ? "NO FLUSH " : "", mask & 2 ? "NO SWITCH " : "", mask & 4 ? "NO PPA (solid fill) " : "", mask & 128 ? "ONE TRANSFER " : "strips ", mask & 64 ? "burst 16 " : "burst 128 ", mask & 32 ? "breather " : "", mask & 2048 ? "NOT PIPELINED " : "", mask & 4096 ? "RESTORE BESIDE THE TICK FLIPPED" : ""); }

i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c; }
bool board_is_round(void) { return false; }
bool board_is_watch(void) { return false; }
bool board_is_square(void) { return false; }
bool board_is_175(void) { return false; }      /* (the 1.75 / 1.75-B, 0.3.6: an S3) */
void display_port_set_madctl(uint8_t v) { (void)v; }   /* (the square's panel register: none here) */
uint8_t display_port_madctl(void) { return 0; }
bool board_has_expander(void) { return false; }
bool board_is_v2(void) { return false; }
int  board_pwr_sense_pin(void) { return -1; }
void display_port_frame_origin(int *px, int *py) { *px = 0; *py = 0; }
void display_port_set_view(int view) { (void)view; }
int  display_port_view(void) { return DISPLAY_VIEW_FULL; }
void display_port_deep_sleep_pins(bool tp_awake_high) { (void)tp_awake_high; }
void display_port_deep_sleep_bus(void) { }
void display_port_deep_standby(void) { }
void display_port_set_inverted(bool inverted) { s_inverted = inverted; }
void display_port_flush_prof(int64_t *wait_us, int64_t *send_us) { *wait_us = s_wait_us; *send_us = s_send_us; s_wait_us = s_send_us = 0; }

/* a touch on the panel (its native px, 800 across, 1280 down) -> the tank px under it
 * as if the picture were upright (the touch port turns it over for ROTATION itself):
 * the PPA's turn undone. The PPA turns counter-clockwise: at 90 the
 * source's right edge is the panel's top, at 270 its left edge is. */
void display_port_panel_to_tank(int px, int py, float *tx, float *ty) {
    if (P4_ROT_DEFAULT == PPA_SRM_ROTATION_ANGLE_90) { *tx = (float)(P4_PANEL_H - 1 - py); *ty = (float)px; }
    else                                              { *tx = (float)py; *ty = (float)(P4_PANEL_W - 1 - px); }
}

#if !defined(TANK_TAB5_HW) && !defined(TANK_P4_BETA)
/* the panel's init table, Waveshare's for the 10.1 (their BSP, Apache-2.0) */
static const jd9365_lcd_init_cmd_t p4_init_cmds[] = {
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0xE1, (uint8_t[]){0x93}, 1, 0},
    {0xE2, (uint8_t[]){0x65}, 1, 0},
    {0xE3, (uint8_t[]){0xF8}, 1, 0},
    {0x80, (uint8_t[]){0x01}, 1, 0},
    {0xE0, (uint8_t[]){0x01}, 1, 0},
    {0x00, (uint8_t[]){0x00}, 1, 0},
    {0x01, (uint8_t[]){0x38}, 1, 0},
    {0x03, (uint8_t[]){0x10}, 1, 0},
    {0x04, (uint8_t[]){0x38}, 1, 0},
    {0x0C, (uint8_t[]){0x74}, 1, 0},
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x18, (uint8_t[]){0xAF}, 1, 0},
    {0x19, (uint8_t[]){0x00}, 1, 0},
    {0x1A, (uint8_t[]){0x00}, 1, 0},
    {0x1B, (uint8_t[]){0xAF}, 1, 0},
    {0x1C, (uint8_t[]){0x00}, 1, 0},
    {0x35, (uint8_t[]){0x26}, 1, 0},
    {0x37, (uint8_t[]){0x09}, 1, 0},
    {0x38, (uint8_t[]){0x04}, 1, 0},
    {0x39, (uint8_t[]){0x00}, 1, 0},
    {0x3A, (uint8_t[]){0x01}, 1, 0},
    {0x3C, (uint8_t[]){0x78}, 1, 0},
    {0x3D, (uint8_t[]){0xFF}, 1, 0},
    {0x3E, (uint8_t[]){0xFF}, 1, 0},
    {0x3F, (uint8_t[]){0x7F}, 1, 0},
    {0x40, (uint8_t[]){0x06}, 1, 0},
    {0x41, (uint8_t[]){0xA0}, 1, 0},
    {0x42, (uint8_t[]){0x81}, 1, 0},
    {0x43, (uint8_t[]){0x1E}, 1, 0},
    {0x44, (uint8_t[]){0x0D}, 1, 0},
    {0x45, (uint8_t[]){0x28}, 1, 0},
    //{0x4A, (uint8_t[]){0x35}, 1, 0},//bist
    {0x55, (uint8_t[]){0x02}, 1, 0},
    {0x57, (uint8_t[]){0x69}, 1, 0},
    {0x59, (uint8_t[]){0x0A}, 1, 0},
    {0x5A, (uint8_t[]){0x2A}, 1, 0},
    {0x5B, (uint8_t[]){0x17}, 1, 0},
    {0x5D, (uint8_t[]){0x7F}, 1, 0},
    {0x5E, (uint8_t[]){0x6A}, 1, 0},
    {0x5F, (uint8_t[]){0x5B}, 1, 0},
    {0x60, (uint8_t[]){0x4F}, 1, 0},
    {0x61, (uint8_t[]){0x4A}, 1, 0},
    {0x62, (uint8_t[]){0x3D}, 1, 0},
    {0x63, (uint8_t[]){0x41}, 1, 0},
    {0x64, (uint8_t[]){0x2A}, 1, 0},
    {0x65, (uint8_t[]){0x44}, 1, 0},
    {0x66, (uint8_t[]){0x43}, 1, 0},
    {0x67, (uint8_t[]){0x44}, 1, 0},
    {0x68, (uint8_t[]){0x62}, 1, 0},
    {0x69, (uint8_t[]){0x52}, 1, 0},
    {0x6A, (uint8_t[]){0x59}, 1, 0},
    {0x6B, (uint8_t[]){0x4C}, 1, 0},
    {0x6C, (uint8_t[]){0x48}, 1, 0},
    {0x6D, (uint8_t[]){0x3A}, 1, 0},
    {0x6E, (uint8_t[]){0x26}, 1, 0},
    {0x6F, (uint8_t[]){0x00}, 1, 0},
    {0x70, (uint8_t[]){0x7F}, 1, 0},
    {0x71, (uint8_t[]){0x6A}, 1, 0},
    {0x72, (uint8_t[]){0x5B}, 1, 0},
    {0x73, (uint8_t[]){0x4F}, 1, 0},
    {0x74, (uint8_t[]){0x4A}, 1, 0},
    {0x75, (uint8_t[]){0x3D}, 1, 0},
    {0x76, (uint8_t[]){0x41}, 1, 0},
    {0x77, (uint8_t[]){0x2A}, 1, 0},
    {0x78, (uint8_t[]){0x44}, 1, 0},
    {0x79, (uint8_t[]){0x43}, 1, 0},
    {0x7A, (uint8_t[]){0x44}, 1, 0},
    {0x7B, (uint8_t[]){0x62}, 1, 0},
    {0x7C, (uint8_t[]){0x52}, 1, 0},
    {0x7D, (uint8_t[]){0x59}, 1, 0},
    {0x7E, (uint8_t[]){0x4C}, 1, 0},
    {0x7F, (uint8_t[]){0x48}, 1, 0},
    {0x80, (uint8_t[]){0x3A}, 1, 0},
    {0x81, (uint8_t[]){0x26}, 1, 0},
    {0x82, (uint8_t[]){0x00}, 1, 0},
    {0xE0, (uint8_t[]){0x02}, 1, 0},
    {0x00, (uint8_t[]){0x42}, 1, 0},
    {0x01, (uint8_t[]){0x42}, 1, 0},
    {0x02, (uint8_t[]){0x40}, 1, 0},
    {0x03, (uint8_t[]){0x40}, 1, 0},
    {0x04, (uint8_t[]){0x5E}, 1, 0},
    {0x05, (uint8_t[]){0x5E}, 1, 0},
    {0x06, (uint8_t[]){0x5F}, 1, 0},
    {0x07, (uint8_t[]){0x5F}, 1, 0},
    {0x08, (uint8_t[]){0x5F}, 1, 0},
    {0x09, (uint8_t[]){0x57}, 1, 0},
    {0x0A, (uint8_t[]){0x57}, 1, 0},
    {0x0B, (uint8_t[]){0x77}, 1, 0},
    {0x0C, (uint8_t[]){0x77}, 1, 0},
    {0x0D, (uint8_t[]){0x47}, 1, 0},
    {0x0E, (uint8_t[]){0x47}, 1, 0},
    {0x0F, (uint8_t[]){0x45}, 1, 0},
    {0x10, (uint8_t[]){0x45}, 1, 0},
    {0x11, (uint8_t[]){0x4B}, 1, 0},
    {0x12, (uint8_t[]){0x4B}, 1, 0},
    {0x13, (uint8_t[]){0x49}, 1, 0},
    {0x14, (uint8_t[]){0x49}, 1, 0},
    {0x15, (uint8_t[]){0x5F}, 1, 0},
    {0x16, (uint8_t[]){0x41}, 1, 0},
    {0x17, (uint8_t[]){0x41}, 1, 0},
    {0x18, (uint8_t[]){0x40}, 1, 0},
    {0x19, (uint8_t[]){0x40}, 1, 0},
    {0x1A, (uint8_t[]){0x5E}, 1, 0},
    {0x1B, (uint8_t[]){0x5E}, 1, 0},
    {0x1C, (uint8_t[]){0x5F}, 1, 0},
    {0x1D, (uint8_t[]){0x5F}, 1, 0},
    {0x1E, (uint8_t[]){0x5F}, 1, 0},
    {0x1F, (uint8_t[]){0x57}, 1, 0},
    {0x20, (uint8_t[]){0x57}, 1, 0},
    {0x21, (uint8_t[]){0x77}, 1, 0},
    {0x22, (uint8_t[]){0x77}, 1, 0},
    {0x23, (uint8_t[]){0x46}, 1, 0},
    {0x24, (uint8_t[]){0x46}, 1, 0},
    {0x25, (uint8_t[]){0x44}, 1, 0},
    {0x26, (uint8_t[]){0x44}, 1, 0},
    {0x27, (uint8_t[]){0x4A}, 1, 0},
    {0x28, (uint8_t[]){0x4A}, 1, 0},
    {0x29, (uint8_t[]){0x48}, 1, 0},
    {0x2A, (uint8_t[]){0x48}, 1, 0},
    {0x2B, (uint8_t[]){0x5F}, 1, 0},
    {0x2C, (uint8_t[]){0x01}, 1, 0},
    {0x2D, (uint8_t[]){0x01}, 1, 0},
    {0x2E, (uint8_t[]){0x00}, 1, 0},
    {0x2F, (uint8_t[]){0x00}, 1, 0},
    {0x30, (uint8_t[]){0x1F}, 1, 0},
    {0x31, (uint8_t[]){0x1F}, 1, 0},
    {0x32, (uint8_t[]){0x1E}, 1, 0},
    {0x33, (uint8_t[]){0x1E}, 1, 0},
    {0x34, (uint8_t[]){0x1F}, 1, 0},
    {0x35, (uint8_t[]){0x17}, 1, 0},
    {0x36, (uint8_t[]){0x17}, 1, 0},
    {0x37, (uint8_t[]){0x37}, 1, 0},
    {0x38, (uint8_t[]){0x37}, 1, 0},
    {0x39, (uint8_t[]){0x08}, 1, 0},
    {0x3A, (uint8_t[]){0x08}, 1, 0},
    {0x3B, (uint8_t[]){0x0A}, 1, 0},
    {0x3C, (uint8_t[]){0x0A}, 1, 0},
    {0x3D, (uint8_t[]){0x04}, 1, 0},
    {0x3E, (uint8_t[]){0x04}, 1, 0},
    {0x3F, (uint8_t[]){0x06}, 1, 0},
    {0x40, (uint8_t[]){0x06}, 1, 0},
    {0x41, (uint8_t[]){0x1F}, 1, 0},
    {0x42, (uint8_t[]){0x02}, 1, 0},
    {0x43, (uint8_t[]){0x02}, 1, 0},
    {0x44, (uint8_t[]){0x00}, 1, 0},
    {0x45, (uint8_t[]){0x00}, 1, 0},
    {0x46, (uint8_t[]){0x1F}, 1, 0},
    {0x47, (uint8_t[]){0x1F}, 1, 0},
    {0x48, (uint8_t[]){0x1E}, 1, 0},
    {0x49, (uint8_t[]){0x1E}, 1, 0},
    {0x4A, (uint8_t[]){0x1F}, 1, 0},
    {0x4B, (uint8_t[]){0x17}, 1, 0},
    {0x4C, (uint8_t[]){0x17}, 1, 0},
    {0x4D, (uint8_t[]){0x37}, 1, 0},
    {0x4E, (uint8_t[]){0x37}, 1, 0},
    {0x4F, (uint8_t[]){0x09}, 1, 0},
    {0x50, (uint8_t[]){0x09}, 1, 0},
    {0x51, (uint8_t[]){0x0B}, 1, 0},
    {0x52, (uint8_t[]){0x0B}, 1, 0},
    {0x53, (uint8_t[]){0x05}, 1, 0},
    {0x54, (uint8_t[]){0x05}, 1, 0},
    {0x55, (uint8_t[]){0x07}, 1, 0},
    {0x56, (uint8_t[]){0x07}, 1, 0},
    {0x57, (uint8_t[]){0x1F}, 1, 0},
    {0x58, (uint8_t[]){0x40}, 1, 0},
    {0x5B, (uint8_t[]){0x30}, 1, 0},
    {0x5C, (uint8_t[]){0x00}, 1, 0},
    {0x5D, (uint8_t[]){0x34}, 1, 0},
    {0x5E, (uint8_t[]){0x05}, 1, 0},
    {0x5F, (uint8_t[]){0x02}, 1, 0},
    {0x63, (uint8_t[]){0x00}, 1, 0},
    {0x64, (uint8_t[]){0x6A}, 1, 0},
    {0x67, (uint8_t[]){0x73}, 1, 0},
    {0x68, (uint8_t[]){0x07}, 1, 0},
    {0x69, (uint8_t[]){0x08}, 1, 0},
    {0x6A, (uint8_t[]){0x6A}, 1, 0},
    {0x6B, (uint8_t[]){0x08}, 1, 0},
    {0x6C, (uint8_t[]){0x00}, 1, 0},
    {0x6D, (uint8_t[]){0x00}, 1, 0},
    {0x6E, (uint8_t[]){0x00}, 1, 0},
    {0x6F, (uint8_t[]){0x88}, 1, 0},
    {0x75, (uint8_t[]){0xFF}, 1, 0},
    {0x77, (uint8_t[]){0xDD}, 1, 0},
    {0x78, (uint8_t[]){0x2C}, 1, 0},
    {0x79, (uint8_t[]){0x15}, 1, 0},
    {0x7A, (uint8_t[]){0x17}, 1, 0},
    {0x7D, (uint8_t[]){0x14}, 1, 0},
    {0x7E, (uint8_t[]){0x82}, 1, 0},
    {0xE0, (uint8_t[]){0x04}, 1, 0},
    {0x00, (uint8_t[]){0x0E}, 1, 0},
    {0x02, (uint8_t[]){0xB3}, 1, 0},
    {0x09, (uint8_t[]){0x61}, 1, 0},
    {0x0E, (uint8_t[]){0x48}, 1, 0},
    {0x37, (uint8_t[]){0x58}, 1, 0}, // 全志
    {0x2B, (uint8_t[]){0x0F}, 1, 0}, // 全志
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0xE6, (uint8_t[]){0x02}, 1, 0},
    {0xE7, (uint8_t[]){0x0C}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 20},
};
#endif

#ifdef TANK_TAB5_HW
/* ---- the Tab5's two PI4IOE5V6408 expanders (M5's BSP, m5stack_tab5.c bsp_io_expander_pi4ioe_init) ---- */
#define IOX_RESET 0x01
#define IOX_DIR   0x03                       /* 1 = output */
#define IOX_OUT   0x05
#define IOX_HIZ   0x07                       /* 1 = output high-impedance */
#define IOX_INDEF 0x09
#define IOX_PULLEN 0x0B
#define IOX_PULLUP 0x0D                      /* 1 = up, 0 = down */
#define IOX_IN    0x0F
#define IOX_IMASK 0x11
static i2c_master_dev_handle_t s_iox1, s_iox2, s_tp;
static void iox_w(i2c_master_dev_handle_t d, uint8_t reg, uint8_t v) { uint8_t b[2] = { reg, v }; i2c_master_transmit(d, b, 2, 50); }
static uint8_t iox_r(i2c_master_dev_handle_t d, uint8_t reg) { uint8_t v = 0; i2c_master_transmit_receive(d, &reg, 1, &v, 1, 50); return v; }
static void iox_bit(i2c_master_dev_handle_t d, uint8_t reg, int bit, bool on) { uint8_t v = iox_r(d, reg); iox_w(d, reg, on ? (v | (1u << bit)) : (v & ~(1u << bit))); }
static i2c_master_dev_handle_t iox_add(uint8_t addr) {
    i2c_device_config_t c = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000 };
    i2c_master_dev_handle_t d = NULL;
    if (i2c_master_bus_add_device(s_i2c, &c, &d) != ESP_OK) return NULL;
    return d;
}
static void tab5_expanders(void) {
    s_iox1 = iox_add(I2C_ADDR_IOX1); s_iox2 = iox_add(I2C_ADDR_IOX2);
    if (!s_iox1 || !s_iox2) { ESP_LOGE(TAG, "tab5: an expander is missing"); return; }
    iox_w(s_iox1, IOX_RESET, 0xFF); (void)iox_r(s_iox1, IOX_RESET);
    iox_w(s_iox1, IOX_PULLUP, 0x7F); iox_w(s_iox1, IOX_PULLEN, 0x7F);     /* P0..P6 pulled up: a released reset is high */
    iox_w(s_iox1, IOX_OUT, 0x66);            /* SPK_EN, EXT5V_EN, TP_RST, CAM_RST high; LCD_RST latched LOW (it is only ever driven low) */
    iox_w(s_iox1, IOX_HIZ, 0x00);
    iox_w(s_iox1, IOX_DIR, 0x7F);            /* LCD_RST out: the panel held in reset ... */
    vTaskDelay(pdMS_TO_TICKS(10));
    iox_w(s_iox1, IOX_DIR, 0x6F);            /* ... and released: an input, its pull-up lifts it */
    vTaskDelay(pdMS_TO_TICKS(50));
    iox_w(s_iox2, IOX_RESET, 0xFF); (void)iox_r(s_iox2, IOX_RESET);
    iox_w(s_iox2, IOX_DIR, 0xB9);            /* P0 P3 P4 P5 P7 out; P6 (charge status) in */
    iox_w(s_iox2, IOX_HIZ, 0x06);
    iox_w(s_iox2, IOX_PULLUP, 0xB9); iox_w(s_iox2, IOX_PULLEN, 0xF9);
    iox_w(s_iox2, IOX_INDEF, 0x40); iox_w(s_iox2, IOX_IMASK, 0xBF);
    iox_w(s_iox2, IOX_OUT, 0x09);            /* the C6's power and the USB-A 5 V on; quick charge allowed (P5 low) */
    vTaskDelay(pdMS_TO_TICKS(50));
    iox_bit(s_iox2, IOX_OUT, 7, true);       /* CHG_EN: the battery charges only while the board says so (and only while it is on) */
}
/* the panel and the touch half of it, out of reset together (M5's bsp_reset_tp): LCD_RST and TP_RST low, then TP_RST high
 * and LCD_RST released to its pull-up */
static void tab5_reset_panel(void) {
    if (!s_iox1) return;
    uint8_t out = iox_r(s_iox1, IOX_OUT) & ~((1u << 4) | (1u << 5));
    iox_w(s_iox1, IOX_OUT, out);
    iox_bit(s_iox1, IOX_DIR, 4, true);
    vTaskDelay(pdMS_TO_TICKS(100));
    iox_w(s_iox1, IOX_OUT, (out & ~(1u << 4)) | (1u << 5));
    iox_bit(s_iox1, IOX_DIR, 4, false);
    vTaskDelay(pdMS_TO_TICKS(100));
}
void display_port_tab5_speaker(bool on) { if (s_iox1) iox_bit(s_iox1, IOX_OUT, 1, on); }
bool display_port_tab5_charging(void) { return s_iox2 && !(iox_r(s_iox2, IOX_IN) & (1u << 6)); }

typedef struct { uint8_t cmd; const uint8_t *data; uint8_t n; uint16_t ms; } tab5_cmd_t;
/* the ST7121's init table (M5's esp_lcd_st7121, Apache-2.0) */
static const tab5_cmd_t st7121_cmds[] = {
    {0x60, (uint8_t[]){0x71, 0x21, 0xA2}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x21, 0xA3}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x21, 0xA4}, 3, 0},
    {0x78, (uint8_t[]){0x21}, 1, 0},
    {0x79, (uint8_t[]){0xEF}, 1, 0},
    {0xA4, (uint8_t[]){0x31}, 1, 0},
    {0xB7, (uint8_t[]){0x00, 0x00, 0x5F, 0x5F, 0x44, 0x1A}, 6, 0},
    {0xB0, (uint8_t[]){0x22, 0x6B, 0x11, 0x89, 0x25, 0x43, 0x43}, 7, 0},
    {0xBF, (uint8_t[]){0xA7, 0xA7}, 2, 0},
    {0xA5, (uint8_t[]){0xF0, 0x03}, 2, 0},
    {0xD7, (uint8_t[]){0x10, 0x2C, 0x14, 0x2A, 0x80, 0x80}, 6, 0},
    {0x90, (uint8_t[]){0x71, 0x23, 0x5A, 0x20, 0x24, 0x11, 0x21}, 7, 0},
    {0xA3, (uint8_t[]){0x80, 0x01, 0x8C, 0xFF, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00,
                       0x1E, 0x5C, 0x1E, 0x80, 0x10, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
                       0x00, 0x00, 0x1E, 0x5C, 0x1E, 0x80, 0x10, 0xEF, 0x58, 0x00, 0x00, 0x00, 0xFF}, 39, 0},
    {0xA6, (uint8_t[]){0x0A, 0x00, 0x24, 0x71, 0x36, 0x00, 0x00, 0x00, 0x68, 0x68, 0x91, 0xFF, 0x00, 0x24,
                       0x71, 0x37, 0x00, 0x00, 0x00, 0x68, 0x68, 0x91, 0xFF, 0x00, 0x24, 0x71, 0x00, 0x00,
                       0x00, 0x00, 0x68, 0x68, 0x91, 0xFF, 0x00, 0x2C, 0x71, 0x00, 0x01, 0x00, 0x00, 0x68,
                       0x68, 0xFF, 0xFF, 0x00, 0x08, 0x80, 0x08, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00}, 55, 0},
    {0xA7, (uint8_t[]){0x1A, 0x1A, 0xC0, 0x64, 0x40, 0x04, 0x15, 0x40, 0x00, 0x40, 0x00, 0x68, 0x68, 0x91, 0xFF,
                       0x08, 0x80, 0x64, 0x40, 0x26, 0x37, 0x40, 0x00, 0x00, 0x00, 0x68, 0x68, 0x91, 0xFF, 0x08,
                       0x80, 0x64, 0x40, 0x8C, 0x9D, 0x40, 0x00, 0x00, 0x00, 0x68, 0x68, 0x91, 0xFF, 0x08, 0x80,
                       0x64, 0x40, 0xAE, 0xBF, 0x00, 0x00, 0x20, 0x00, 0x68, 0x68, 0x91, 0xFF, 0x08, 0x80, 0x79}, 60, 0},
    {0xAC, (uint8_t[]){0x1D, 0x18, 0x19, 0x1D, 0x18, 0x19, 0x04, 0x1C, 0x1D, 0x08, 0x0A, 0x10, 0x12, 0x0C, 0x0E,
                       0x14, 0x16, 0x00, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x18, 0x19, 0x1D, 0x18, 0x19, 0x06, 0x1C,
                       0x1D, 0x09, 0x0B, 0x11, 0x13, 0x0D, 0x0F, 0x15, 0x17, 0x02, 0x1D, 0x1D, 0x1D, 0x1D}, 44, 0},
    {0xAD, (uint8_t[]){0x0C, 0x40, 0x46, 0x00, 0x07, 0x4B, 0x4B, 0xFF, 0xFF, 0xF0, 0x40, 0x0E, 0x01,
                       0x07, 0x42, 0x42, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF}, 25, 0},
    {0xAE, (uint8_t[]){0xF0, 0xFF, 0x03, 0xF0, 0xFF, 0x03, 0x00}, 7, 0},
    {0xB2, (uint8_t[]){0x15, 0x19, 0x05, 0x23, 0x49, 0x2D, 0x03, 0x2E, 0x5C, 0xD2, 0xFF, 0x10, 0x60, 0xFD, 0x20, 0xC0, 0x00}, 17, 0},
    {0xE8, (uint8_t[]){0x20, 0x60, 0x04, 0x8E, 0x8E, 0x3E, 0x04, 0xDC, 0xDC, 0x3E, 0x06, 0xFA, 0x26, 0x3E}, 14, 0},
    {0x75, (uint8_t[]){0x03, 0x04}, 2, 0},
    {0xE7, (uint8_t[]){0x4B, 0x00, 0x00, 0xBE, 0x4B, 0x8C, 0x20, 0x1A, 0xF0, 0x7D, 0x14, 0x7D, 0x14, 0x7D,
                       0x14, 0x7D, 0x14, 0xFF, 0x00, 0x32, 0x30, 0x73, 0x00, 0x00, 0xC8, 0x6A, 0xFF, 0x5A,
                       0x64, 0x38, 0x88, 0x15, 0xB1, 0x01, 0x01, 0x64, 0x01, 0x01, 0x7C, 0xFF, 0x1A, 0x51}, 42, 0},
    {0xE1, (uint8_t[]){0x0C, 0x0C}, 2, 0},
    {0xEA, (uint8_t[]){0x15, 0x00, 0x01}, 3, 0},
    {0xC8, (uint8_t[]){0x00, 0x00, 0x04, 0x08, 0x10, 0x00, 0x1F, 0x01, 0x39, 0x3E, 0x00, 0x78, 0x06,
                       0xE2, 0x02, 0x11, 0x33, 0x01, 0x7A, 0x0D, 0x21, 0xC4, 0x0B, 0x19, 0x08, 0x32,
                       0xA0, 0x08, 0x1A, 0x0A, 0xF3, 0x7F, 0x0E, 0xC5, 0xE8, 0x03, 0xFF}, 37, 0},
    {0xC9, (uint8_t[]){0x00, 0x00, 0x04, 0x08, 0x10, 0x00, 0x1F, 0x01, 0x39, 0x3E, 0x00, 0x78, 0x06,
                       0xE2, 0x02, 0x11, 0x33, 0x01, 0x7A, 0x0D, 0x21, 0xC4, 0x0B, 0x19, 0x08, 0x32,
                       0xA0, 0x08, 0x1A, 0x0A, 0xF3, 0x7F, 0x0E, 0xC5, 0xE8, 0x03, 0xFF}, 37, 0},
    {0x60, (uint8_t[]){0x71, 0x21, 0x00}, 3, 0},
    {0x11, NULL, 0, 80},
    {0x29, NULL, 0, 800},
    {0x35, (uint8_t[]){0x00}, 1, 0},
};
/* the ST7123's (M5's m5stack_tab5.c st7123_vendor_specific_init_default, Apache-2.0) */
static const tab5_cmd_t st7123_cmds[] = {
    {0x60, (uint8_t[]){0x71, 0x23, 0xa2}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x23, 0xa3}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x23, 0xa4}, 3, 0},
    {0xA4, (uint8_t[]){0x31}, 1, 0},
    {0xD7, (uint8_t[]){0x10, 0x0A, 0x10, 0x2A, 0x80, 0x80}, 6, 0},
    {0x90, (uint8_t[]){0x71, 0x23, 0x5A, 0x20, 0x24, 0x09, 0x09}, 7, 0},
    {0xA3, (uint8_t[]){0x80, 0x01, 0x88, 0x30, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00,
                       0x1E, 0x5C, 0x1E, 0x80, 0x00, 0x4F, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
                       0x00, 0x00, 0x1E, 0x5C, 0x1E, 0x80, 0x00, 0x6F, 0x58, 0x00, 0x00, 0x00, 0xFF}, 40, 0},
    {0xA6, (uint8_t[]){0x03, 0x00, 0x24, 0x55, 0x36, 0x00, 0x39, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0x24,
                       0x55, 0x38, 0x00, 0x37, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0x24, 0x11, 0x00, 0x00,
                       0x00, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0xEC, 0x11, 0x00, 0x03, 0x00, 0x03, 0x6E,
                       0x6E, 0xFF, 0xFF, 0x00, 0x08, 0x80, 0x08, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00}, 55, 0},
    {0xA7, (uint8_t[]){0x19, 0x19, 0x80, 0x64, 0x40, 0x07, 0x16, 0x40, 0x00, 0x44, 0x03, 0x6E, 0x6E, 0x91, 0xFF,
                       0x08, 0x80, 0x64, 0x40, 0x25, 0x34, 0x40, 0x00, 0x02, 0x01, 0x6E, 0x6E, 0x91, 0xFF, 0x08,
                       0x80, 0x64, 0x40, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x08, 0x80,
                       0x64, 0x40, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x6E, 0x6E, 0x84, 0xFF, 0x08, 0x80, 0x44}, 60, 0},
    {0xAC, (uint8_t[]){0x03, 0x19, 0x19, 0x18, 0x18, 0x06, 0x13, 0x13, 0x11, 0x11, 0x08, 0x08, 0x0A, 0x0A, 0x1C,
                       0x1C, 0x07, 0x07, 0x00, 0x00, 0x02, 0x02, 0x01, 0x19, 0x19, 0x18, 0x18, 0x06, 0x12, 0x12,
                       0x10, 0x10, 0x09, 0x09, 0x0B, 0x0B, 0x1C, 0x1C, 0x07, 0x07, 0x03, 0x03, 0x01, 0x01}, 44, 0},
    {0xAD, (uint8_t[]){0xF0, 0x00, 0x46, 0x00, 0x03, 0x50, 0x50, 0xFF, 0xFF, 0xF0, 0x40, 0x06, 0x01,
                       0x07, 0x42, 0x42, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF}, 25, 0},
    {0xAE, (uint8_t[]){0xFE, 0x3F, 0x3F, 0xFE, 0x3F, 0x3F, 0x00}, 7, 0},
    {0xB2, (uint8_t[]){0x15, 0x19, 0x05, 0x23, 0x49, 0xAF, 0x03, 0x2E, 0x5C, 0xD2, 0xFF, 0x10, 0x20, 0xFD, 0x20, 0xC0, 0x00}, 17, 0},
    {0xE8, (uint8_t[]){0x20, 0x6F, 0x04, 0x97, 0x97, 0x3E, 0x04, 0xDC, 0xDC, 0x3E, 0x06, 0xFA, 0x26, 0x3E}, 15, 0},
    {0x75, (uint8_t[]){0x03, 0x04}, 2, 0},
    {0xE7, (uint8_t[]){0x3B, 0x00, 0x00, 0x7C, 0xA1, 0x8C, 0x20, 0x1A, 0xF0, 0xB1, 0x50, 0x00,
                       0x50, 0xB1, 0x50, 0xB1, 0x50, 0xD8, 0x00, 0x55, 0x00, 0xB1, 0x00, 0x45,
                       0xC9, 0x6A, 0xFF, 0x5A, 0xD8, 0x18, 0x88, 0x15, 0xB1, 0x01, 0x01, 0x77}, 36, 0},
    {0xEA, (uint8_t[]){0x13, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x2C}, 8, 0},
    {0xB0, (uint8_t[]){0x22, 0x43, 0x11, 0x61, 0x25, 0x43, 0x43}, 7, 0},
    {0xb7, (uint8_t[]){0x00, 0x00, 0x73, 0x73}, 4, 0},
    {0xBF, (uint8_t[]){0xA6, 0xAA}, 2, 0},
    {0xA9, (uint8_t[]){0x00, 0x00, 0x73, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03}, 10, 0},
    {0xC8, (uint8_t[]){0x00, 0x00, 0x10, 0x1F, 0x36, 0x00, 0x5D, 0x04, 0x9D, 0x05, 0x10, 0xF2, 0x06,
                       0x60, 0x03, 0x11, 0xAD, 0x00, 0xEF, 0x01, 0x22, 0x2E, 0x0E, 0x74, 0x08, 0x32,
                       0xDC, 0x09, 0x33, 0x0F, 0xF3, 0x77, 0x0D, 0xB0, 0xDC, 0x03, 0xFF}, 37, 0},
    {0xC9, (uint8_t[]){0x00, 0x00, 0x10, 0x1F, 0x36, 0x00, 0x5D, 0x04, 0x9D, 0x05, 0x10, 0xF2, 0x06,
                       0x60, 0x03, 0x11, 0xAD, 0x00, 0xEF, 0x01, 0x22, 0x2E, 0x0E, 0x74, 0x08, 0x32,
                       0xDC, 0x09, 0x33, 0x0F, 0xF3, 0x77, 0x0D, 0xB0, 0xDC, 0x03, 0xFF}, 37, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 100},
    {0x29, (uint8_t[]){0x00}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 100},
};
typedef enum { TAB5_PANEL_NONE, TAB5_PANEL_ILI9881C, TAB5_PANEL_ST7123, TAB5_PANEL_ST7121 } tab5_panel_t;
static tab5_panel_t s_tab5_panel;
static const char *tab5_panel_name(tab5_panel_t p) { return p == TAB5_PANEL_ST7121 ? "ST7121" : p == TAB5_PANEL_ST7123 ? "ST7123" : p == TAB5_PANEL_ILI9881C ? "ILI9881C + GT911" : "none"; }
/* which panel (M5's bsp_detect_display_type): a GT911 at 0x14 is the first one; the ST712x answer at 0x55, and their
 * register 0 (16-bit address) says which: 1 = ST7121, 3 = ST7123 */
static tab5_panel_t tab5_detect(void) {
    if (i2c_master_probe(s_i2c, I2C_ADDR_GT911_ALT, 50) == ESP_OK) return TAB5_PANEL_ILI9881C;
    for (int t = 0; t < 10; t++) {
        if (i2c_master_probe(s_i2c, I2C_ADDR_ST712X, 50) == ESP_OK) {
            i2c_device_config_t c = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = I2C_ADDR_ST712X, .scl_speed_hz = 100000 };
            if (!s_tp && i2c_master_bus_add_device(s_i2c, &c, &s_tp) != ESP_OK) s_tp = NULL;
            uint8_t reg[2] = { 0, 0 }, v = 0;
            if (s_tp && i2c_master_transmit_receive(s_tp, reg, 2, &v, 1, 50) == ESP_OK) {
                if (v == 1) return TAB5_PANEL_ST7121;
                if (v == 3) return TAB5_PANEL_ST7123;
            }
            return TAB5_PANEL_ST7123;          /* M5's fallback */
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    return TAB5_PANEL_NONE;
}
/* the panel up: DSI bus, the command channel, the init table, then the DPI panel and its two frame buffers */
static bool tab5_panel_up(void) {
    tab5_expanders();
    tab5_reset_panel();
    s_tab5_panel = tab5_detect();
    if (s_tab5_panel == TAB5_PANEL_NONE || s_tab5_panel == TAB5_PANEL_ILI9881C) {
        ESP_LOGE(TAG, "tab5: panel %s - %s", tab5_panel_name(s_tab5_panel),
                 s_tab5_panel == TAB5_PANEL_NONE ? "nothing answered at 0x14 or 0x55" : "the first Tab5s' panel is not supported in this build yet");
        return false;
    }
    bool st7121 = s_tab5_panel == TAB5_PANEL_ST7121;
    esp_ldo_channel_config_t ldo = { .chan_id = P4_DSI_LDO_CHAN, .voltage_mv = P4_DSI_LDO_MV };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo, &s_ldo));
    esp_lcd_dsi_bus_config_t dsi = { .bus_id = 0, .num_data_lanes = P4_DSI_LANES, .phy_clk_src = 0, .lane_bit_rate_mbps = P4_DSI_MBPS };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&dsi, &s_dsi));
    esp_lcd_dbi_io_config_t dbi = { .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(s_dsi, &dbi, &s_io));
    esp_lcd_dpi_panel_config_t dpi = {
        .virtual_channel = 0, .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT, .dpi_clock_freq_mhz = 70,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565, .num_fbs = 2,
        .video_timing = { .h_size = P4_PANEL_W, .v_size = P4_PANEL_H,
                          .hsync_pulse_width = 2, .hsync_back_porch = 40, .hsync_front_porch = 40,
                          .vsync_pulse_width = st7121 ? 20 : 2, .vsync_back_porch = st7121 ? 24 : 8, .vsync_front_porch = st7121 ? 200 : 220 },
    };
    dpi.flags.use_dma2d = false;              /* the PPA writes the buffers, the DMA scans them (as on the 10.1) */
    dpi.flags.disable_lp = false;             /* the link drops to low power between lines, as M5 runs it: with it held in high speed
                                                 (the 10.1's setting) the ST7121 lost its place mid-frame - the lower half of each scan
                                                 slid along its lines, a shear on the glass (Strato's photo, 2026-10-19) */
    ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(s_dsi, &dpi, &s_panel));
    if (st7121) { esp_lcd_panel_io_tx_param(s_io, 0x01, NULL, 0); vTaskDelay(pdMS_TO_TICKS(120)); }   /* the ST7121's software reset (its driver's) */
    else vTaskDelay(pdMS_TO_TICKS(20));
    const tab5_cmd_t *cmds = st7121 ? st7121_cmds : st7123_cmds;
    int n = st7121 ? (int)(sizeof st7121_cmds / sizeof st7121_cmds[0]) : (int)(sizeof st7123_cmds / sizeof st7123_cmds[0]);
    for (int i = 0; i < n; i++) {
        esp_err_t e = esp_lcd_panel_io_tx_param(s_io, cmds[i].cmd, cmds[i].data, cmds[i].n);
        if (e != ESP_OK) { ESP_LOGE(TAG, "tab5: init command %02x: %s", cmds[i].cmd, esp_err_to_name(e)); return false; }
        vTaskDelay(pdMS_TO_TICKS(cmds[i].ms + (st7121 ? 5 : 0)));
    }
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));      /* the DPI side: the frame DMA starts scanning */
    return true;
}
const char *display_port_tab5_panel(void) { return tab5_panel_name(s_tab5_panel); }
#endif

#ifdef TANK_P4_BETA                            /* BETA (2026-11-22): the Waveshare 7 and 8 inch panels' bring-up */
#include "display_p4_beta.c"
#endif

static void backlight(uint8_t level) {
#ifdef TANK_P4_LCD7
    lcd7_mcu_backlight(level);                  /* the 7 inch: its panel controller's PWM (and the PWM pin below, as Waveshare drives both) */
#endif
    uint32_t duty = (uint32_t)level * level * ((1u << BL_BITS) - 1) / (255u * 255u);   /* a backlight is linear in light: square the step so 30% looks like 30% */
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}
static bool IRAM_ATTR on_frame_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *ed, void *ctx) {
    (void)panel; (void)ed; (void)ctx;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_switched, &hp);
    return hp == pdTRUE;
}
static bool probe(uint8_t addr) { return i2c_master_probe(s_i2c, addr, 50) == ESP_OK; }

bool display_port_init(void) {
#ifdef TANK_TAB5_HW
    i2c_master_bus_config_t bus = { .i2c_port = I2C_NUM_0, .sda_io_num = TAB5_PIN_I2C_SDA, .scl_io_num = TAB5_PIN_I2C_SCL,
#else
    i2c_master_bus_config_t bus = { .i2c_port = I2C_NUM_0, .sda_io_num = P4_PIN_I2C_SDA, .scl_io_num = P4_PIN_I2C_SCL,
#endif
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_i2c));
    /* the backlight first, dark: the panel shows its RAM until the first frame lands */
    const ledc_timer_config_t bt = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = BL_BITS, .timer_num = BL_TIMER,
                                     .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
#ifdef TANK_TAB5_HW
    const ledc_channel_config_t bc = { .gpio_num = P4_PIN_LCD_BL_TAB5, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = BL_CHANNEL,
                                       .intr_type = LEDC_INTR_DISABLE, .timer_sel = BL_TIMER, .duty = 0, .hpoint = 0 };
#else
    const ledc_channel_config_t bc = { .gpio_num = P4_PIN_LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = BL_CHANNEL,
                                       .intr_type = LEDC_INTR_DISABLE, .timer_sel = BL_TIMER, .duty = 0, .hpoint = 0 };
#endif
    ESP_ERROR_CHECK(ledc_timer_config(&bt));
    ESP_ERROR_CHECK(ledc_channel_config(&bc));
#ifdef TANK_TAB5_HW
    if (!tab5_panel_up()) return false;
#elif defined(TANK_P4_BETA)
    if (!beta_panel_up()) {                     /* no picture: the board runs on, the console and the sound with it (display_p4_beta.c) */
        s_gt911 = probe(I2C_ADDR_GT911) || probe(I2C_ADDR_GT911_ALT);
        s_codec = probe(0x18);
        beta_board_line();
        return false;
    }
#else
    /* the DSI PHY's supply is the chip's own LDO channel 3 at 2.5 V */
    esp_ldo_channel_config_t ldo = { .chan_id = P4_DSI_LDO_CHAN, .voltage_mv = P4_DSI_LDO_MV };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo, &s_ldo));
    esp_lcd_dsi_bus_config_t dsi = { .bus_id = 0, .num_data_lanes = P4_DSI_LANES, .phy_clk_src = 0, .lane_bit_rate_mbps = P4_DSI_MBPS };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&dsi, &s_dsi));
    esp_lcd_dbi_io_config_t dbi = { .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(s_dsi, &dbi, &s_io));
    esp_lcd_dpi_panel_config_t dpi = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    dpi.num_fbs = 2;
    dpi.flags.use_dma2d = false;              /* nothing is copied into the frame buffers: the PPA writes them, the DMA scans them */
    dpi.flags.disable_lp = true;              /* no low-power idle on the link between frames */
    jd9365_vendor_config_t vendor = { .init_cmds = p4_init_cmds, .init_cmds_size = sizeof p4_init_cmds / sizeof p4_init_cmds[0],
                                      .mipi_config = { .dsi_bus = s_dsi, .dpi_config = &dpi, .lane_num = P4_DSI_LANES } };
    esp_lcd_panel_dev_config_t dev = { .bits_per_pixel = 16, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                       .reset_gpio_num = P4_PIN_LCD_RST, .vendor_config = &vendor };
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9365(s_io, &dev, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));      /* the touch chip hangs on the same reset line: it boots with the panel */
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
#endif
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, (void **)&s_fb[0], (void **)&s_fb[1]));
    for (int i = 0; i < 2; i++) {               /* black, both: the first frame lands in one, the other shows meanwhile */
        memset(s_fb[i], 0, P4_FB_BYTES);
        esp_cache_msync(s_fb[i], P4_FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }
    s_switched = xSemaphoreCreateBinary();
    xSemaphoreGive(s_switched);
    esp_lcd_dpi_panel_event_callbacks_t cbs = { .on_frame_buf_complete = on_frame_done };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, NULL));
#if !defined(TANK_TAB5_HW) && !defined(TANK_P4_BETA)   /* (the ST712x tables and the beta tables end with the display on) */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
#endif
    s_on = true;
    ppa_client_config_t pc = { .oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1,
                               .data_burst_length = PPA_DATA_BURST_LENGTH_16 };   /* the short-burst client (bench knob 64); the strips are what keeps the DSI fed, not the burst */
    ESP_ERROR_CHECK(ppa_register_client(&pc, &s_ppa));
    pc.data_burst_length = PPA_DATA_BURST_LENGTH_128;
    ESP_ERROR_CHECK(ppa_register_client(&pc, &s_ppa128));
    if (ppa_register_client(&pc, &s_ppa_rs) != ESP_OK) s_ppa_rs = NULL;   /* THE RESTORE BESIDE THE TICK's own client (none: the restore as before) */
    if (!turn_start()) { ESP_LOGE(TAG, "the turn task did not start: no picture"); return false; }   /* (THE PIPELINE) */
    restore_start();
#ifdef TANK_TAB5_HW
    s_gt911 = s_tp != NULL;
    s_codec = probe(0x10);
    ESP_LOGI(TAG, "board: the SMALL HOME TANK, Tab5 (%s %dx%d over %d DSI lanes at %d Mbps) - touch %s, codec ES8388 %s, IMU %s, RTC %s, INA226 %s, charger %s",
             tab5_panel_name(s_tab5_panel), P4_PANEL_W, P4_PANEL_H, P4_DSI_LANES, P4_DSI_MBPS, s_gt911 ? "at 0x55" : "SILENT",
             s_codec ? "answered" : "silent", probe(0x68) ? "BMI270" : "silent", probe(0x32) ? "RX8130CE" : "silent", probe(0x41) ? "answered" : "silent",
             display_port_tab5_charging() ? "CHARGING" : "idle");
#else
    s_gt911 = probe(I2C_ADDR_GT911) || probe(I2C_ADDR_GT911_ALT);
    s_codec = probe(0x18);
#endif
#ifdef TANK_P4_BETA
    beta_board_line();
#elif !defined(TANK_TAB5_HW)
    ESP_LOGI(TAG, "board: the HOME TANK, P4 10.1 (JD9365 %dx%d over %d DSI lanes at %d Mbps) - touch %s, codec %s, no PMIC / IMU / RTC chip",
             P4_PANEL_W, P4_PANEL_H, P4_DSI_LANES, P4_DSI_MBPS, s_gt911 ? "answered" : "SILENT", s_codec ? "answered" : "silent");
#endif
    ESP_LOGI(TAG, "panel up: the %dx%d tank px for px, turned %d, by the PPA into two %u KB DSI buffers", TANK_W, TANK_H,
             P4_ROT_DEFAULT == PPA_SRM_ROTATION_ANGLE_90 ? 90 : 270, (unsigned)P4_FB_BYTES / 1024);
    return true;
}

/* the PPA's side of common/p4_tiles.c: the turn of a block of the frame into a DSI buffer (collected, then done
 * by the turn task with the flush's operation), and the scene's block into a render buffer (the PPA as a plain copier) */
static esp_err_t s_op_e;                      /* the restore's PPA result (p4_put) */
/* THE PIPELINE (2026-10-20): the turn of frame N into a DSI buffer reads render buffer A while the
 * tank draws frame N+1 into render buffer B - two buffers, nothing shared - so the PPA no longer turns
 * with the CPU waiting on it: p4_tiles_flush's turns are COLLECTED as runs here, and the turn task
 * (core 0, above the tank task; it only waits on the PPA) does them while the next frame is drawn.
 * turn_wait() is where they meet: the restore of B (the turn reads it) and the next flush wait there
 * for the turn to be done and hand its DSI buffer to the panel; the flush then waits for the panel to
 * take it before the other is written. The glass shows each frame one frame later. On Strato's grown
 * 10.1 tank a frame was draw ~28 ms + turn ~15 + restore ~13, one after the other. Director `p4 2048`
 * = the old way (the turn waited for at once), for the bench. */
typedef struct { int16_t x0, y0, w, h; } p4_run_t;
#define P4_RUNS_MAX 1024                          /* runs a frame (1000 tiles at worst); past it, the whole frame */
static p4_run_t *s_runs; static int s_nruns; static bool s_runs_all;
static ppa_srm_oper_config_t s_job_op; static ppa_client_handle_t s_job_cl; static ppa_srm_rotation_angle_t s_job_rot;
static int s_job_dsi; static bool s_job_pending, s_job_breathe, s_job_one;
static volatile bool s_turn_busy; static volatile esp_err_t s_turn_err;
static SemaphoreHandle_t s_turn_go, s_turn_done;
/* THE RESTORE BESIDE THE TICK (p4-perf 0.4.3): the scene put back into the next frame's buffer was a run of blocking PPA
 * copies on the tank's core - 12 - 13 ms of the 10.1's frame in a heavy tank, the core waiting on the PPA. Now its runs are
 * collected as the turn's are (p4_tiles_restore decides them, as ever) and a task of their own does the copies through a
 * client of their own, while the tank's core polls and ticks the tank; the frame waits for them just before it draws
 * (display_port_p4_restore_wait). Nothing reads or writes that buffer in between. Director `p4 4096`: the old way. */
static p4_run_t *s_rruns; static int s_nrruns; static bool s_rs_all, s_rs_collect;
static uint16_t *s_rs_fb; static const uint16_t *s_rs_scene;
static SemaphoreHandle_t s_rs_go, s_rs_done; static volatile bool s_rs_busy; static volatile esp_err_t s_rs_err; static bool s_rs_up;
#ifdef TANK_TAB5_HW                                /* (the Tab5's v1.3 PPA: hardware, not the 720-tall world - the 7 inch beta, a v3 chip, takes
                                                      the 10.1's ON, unmeasured there: `p4 4096` flips it for an A/B) */
#define RS_ASYNC_DEFAULT 0                         /* (the Tab5: OFF - its PPA serves the turn and the restore one operation at a time at ~1 ms each, and beside the
                                                      tick the frame waited 23 ms for it: 12.9 -> 10.0 fps in the worst tank, 11-22. `p4 4096` turns it ON there) */
#else
#define RS_ASYNC_DEFAULT 1                         /* (the 10.1: ON; `p4 4096` turns it off) */
#endif
static void p4_turn(int dsi, int x0, int y0, int w, int h) {       /* p4_tiles_flush's turn: one more run for the turn task */
    (void)dsi;
    if (s_nruns == P4_RUNS_MAX) { s_runs_all = true; return; }
    s_runs[s_nruns++] = (p4_run_t){ (int16_t)x0, (int16_t)y0, (int16_t)w, (int16_t)h };
}
static void p4_turn_all(int dsi) { (void)dsi; s_runs_all = true; }
static esp_err_t turn_run(int x0, int y0, int w, int h) {         /* the turn task: a block of the frame into the job's DSI buffer */
    ppa_srm_oper_config_t *op = &s_job_op;
    op->in.block_offset_x = x0; op->in.block_offset_y = y0; op->in.block_w = w; op->in.block_h = h;
    /* the turn is counter-clockwise: at 90 the input's (x, y) lands at (y, W - x - w) on the panel */
    if (s_job_rot == PPA_SRM_ROTATION_ANGLE_90) { op->out.block_offset_x = y0; op->out.block_offset_y = TANK_W - (x0 + w); }
    else                                        { op->out.block_offset_x = TANK_H - (y0 + h); op->out.block_offset_y = x0; }
    s_ops_turn++;
    return ppa_do_scale_rotate_mirror(s_job_cl, op);
}
static void turn_task(void *arg) {
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_turn_go, portMAX_DELAY);
        esp_err_t e = ESP_OK;
        if (s_runs_all) {
            if (s_job_one) e = turn_run(0, 0, TANK_W, TANK_H);                /* the one whole transfer: flashes (bench 128) */
            else for (int i = 0; i < P4_PPA_STRIPS && e == ESP_OK; i++) {     /* column bands of the input -> whole output rows */
                const int cols = TANK_W / P4_PPA_STRIPS;
                e = turn_run(i * cols, 0, cols, TANK_H);
                if (s_job_breathe) vTaskDelay(1);
            }
        } else for (int i = 0; i < s_nruns && e == ESP_OK; i++) e = turn_run(s_runs[i].x0, s_runs[i].y0, s_runs[i].w, s_runs[i].h);
        s_turn_err = e;
        xSemaphoreGive(s_turn_done);
    }
}
static bool turn_start(void) {
    s_runs = heap_caps_malloc(P4_RUNS_MAX * sizeof *s_runs, MALLOC_CAP_SPIRAM);
    s_turn_go = xSemaphoreCreateBinary(); s_turn_done = xSemaphoreCreateBinary();
    return s_runs && s_turn_go && s_turn_done && xTaskCreatePinnedToCore(turn_task, "turn", 3072, NULL, 5, NULL, 0) == pdPASS;
}
/* the turn in flight done, and its buffer handed to the panel (the restore of the buffer it read; the next flush) */
static void turn_wait(void) {
    if (!s_job_pending) return;
    if (s_turn_busy) {
        if (xSemaphoreTake(s_turn_done, pdMS_TO_TICKS(500)) != pdTRUE) { static int said; if (said++ < 3) ESP_LOGE(TAG, "turn: not done in 500 ms"); return; }
        s_turn_busy = false;
    }
    s_job_pending = false;
    if (s_turn_err != ESP_OK) { static int said; if (said++ < 3) ESP_LOGE(TAG, "ppa: %s", esp_err_to_name(s_turn_err)); return; }
    /* our own buffer: the DMA moves to it at the next frame. ONE row named, not the
       picture: the driver only uses the rows for a cache writeback of them, and the
       PPA wrote the buffer past the cache - a 2 MB writeback a frame is a long
       stretch with the interrupts off, in which the DMA's re-arm at the frame's end
       runs late and the panel shows white (2026-10-12) */
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, P4_PANEL_W, 1, s_fb[s_job_dsi]);
    xSemaphoreTake(s_switched, 0);              /* (a refresh that ended before the hand-over says nothing about it: the next one to end does) */
    s_cur = s_job_dsi;
    if (!s_lit && s_on) { s_lit = true; backlight(s_brightness); }   /* the first real frame: light the glass */
}
static void p4_put(uint16_t *fb, const uint16_t *scene, int x0, int y0, int w, int h) {
    if (s_rs_collect) {                                                   /* THE RESTORE BESIDE THE TICK: one more run for the restore task */
        if (s_nrruns == P4_RUNS_MAX) s_rs_all = true;
        else s_rruns[s_nrruns++] = (p4_run_t){ (int16_t)x0, (int16_t)y0, (int16_t)w, (int16_t)h };
        return;
    }
    if (s_op_e != ESP_OK) return;
    { extern int g_p4_x;                                                  /* (the bench, `p4x 4`: the CPU as the copier; 8: tried on the Tab5 11-16, 17 -> 11 ms a restore of single-row runs) */
      if (g_p4_x & 4) { for (int y = y0; y < y0 + h; y++) memcpy(fb + (size_t)y * TANK_W + x0, scene + (size_t)y * TANK_W + x0, (size_t)w * 2); s_ops_put++; return; } }
    ppa_srm_oper_config_t op = {
        .in  = { .buffer = scene, .pic_w = TANK_W, .pic_h = TANK_H, .block_w = w, .block_h = h, .block_offset_x = x0, .block_offset_y = y0, .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .out = { .buffer = fb, .buffer_size = (uint32_t)TANK_W * TANK_H * 2, .pic_w = TANK_W, .pic_h = TANK_H, .block_offset_x = x0, .block_offset_y = y0, .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0, .scale_x = 1.0f, .scale_y = 1.0f, .mode = PPA_TRANS_MODE_BLOCKING,
    };
    s_op_e = ppa_do_scale_rotate_mirror(s_ppa128, &op); s_ops_put++;
}
static void p4_put_all(uint16_t *fb, const uint16_t *scene) {
    if (s_rs_collect) { s_rs_all = true; return; }
    const int rows = TANK_H / P4_PPA_STRIPS;                               /* row bands: contiguous both sides */
    for (int i = 0; i < P4_PPA_STRIPS; i++) p4_put(fb, scene, 0, i * rows, TANK_W, rows);
}
static const p4_tiles_ops_t p4_ops = { p4_turn, p4_turn_all, p4_put, p4_put_all };

static esp_err_t rs_copy(int x0, int y0, int w, int h) {
    ppa_srm_oper_config_t op = {
        .in  = { .buffer = s_rs_scene, .pic_w = TANK_W, .pic_h = TANK_H, .block_w = w, .block_h = h, .block_offset_x = x0, .block_offset_y = y0, .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .out = { .buffer = s_rs_fb, .buffer_size = (uint32_t)TANK_W * TANK_H * 2, .pic_w = TANK_W, .pic_h = TANK_H, .block_offset_x = x0, .block_offset_y = y0, .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0, .scale_x = 1.0f, .scale_y = 1.0f, .mode = PPA_TRANS_MODE_BLOCKING,
    };
    s_ops_put++;
    return ppa_do_scale_rotate_mirror(s_ppa_rs, &op);
}
static void restore_task(void *arg) {
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_rs_go, portMAX_DELAY);
        esp_err_t e = ESP_OK;
        if (s_rs_all) { const int rows = TANK_H / P4_PPA_STRIPS; for (int i = 0; i < P4_PPA_STRIPS && e == ESP_OK; i++) e = rs_copy(0, i * rows, TANK_W, rows); }
        else for (int i = 0; i < s_nrruns && e == ESP_OK; i++) e = rs_copy(s_rruns[i].x0, s_rruns[i].y0, s_rruns[i].w, s_rruns[i].h);
        s_rs_err = e;
        xSemaphoreGive(s_rs_done);
    }
}
static void restore_start(void) {
    s_rruns = heap_caps_malloc(P4_RUNS_MAX * sizeof *s_rruns, MALLOC_CAP_SPIRAM);
    s_rs_go = xSemaphoreCreateBinary(); s_rs_done = xSemaphoreCreateBinary();
    s_rs_up = s_ppa_rs && s_rruns && s_rs_go && s_rs_done && xTaskCreatePinnedToCore(restore_task, "restore", 3072, NULL, 5, NULL, 0) == pdPASS;
    if (!s_rs_up) ESP_LOGW(TAG, "the restore task did not start: the restore stays on the tank's core");
}
/* the restore in flight done: before anything writes the buffer it puts the scene into (the frame's draw) */
void display_port_p4_restore_wait(void) {
    if (!s_rs_busy) return;
    if (xSemaphoreTake(s_rs_done, pdMS_TO_TICKS(500)) != pdTRUE) {   /* (late: said, and still waited for - its runs and its buffer are the job's until it is done) */
        static int said; if (said++ < 3) ESP_LOGE(TAG, "restore: not done in 500 ms - waiting on");
        xSemaphoreTake(s_rs_done, portMAX_DELAY);
    }
    s_rs_busy = false;
    if (s_rs_err != ESP_OK) { static int said; if (said++ < 3) ESP_LOGE(TAG, "ppa restore: %s", esp_err_to_name(s_rs_err)); }
}
void display_port_flush(const uint16_t *fb) {
    if (!s_panel || !s_ppa) return;
    if (s_dbg & 1) return;
    int64_t t0 = esp_timer_get_time();
    turn_wait();                                        /* the last frame's turn done and handed over (THE PIPELINE) */
    xSemaphoreTake(s_switched, pdMS_TO_TICKS(60));     /* the last frame's buffer is on the glass: its predecessor is ours again */
    int64_t t1 = esp_timer_get_time();
    int idle = (s_dbg & 2) ? s_cur : s_cur ^ 1;
    if (s_dbg & 4) {                                    /* no PPA: a solid color the CPU writes, then the usual hand-over */
        const uint16_t tone = 0x2104;                   /* a steady dark gray: a white frame shows against it */
        uint16_t *d = (uint16_t *)s_fb[idle]; for (int i = 0; i < P4_PANEL_W * P4_PANEL_H; i++) d[i] = tone;
        esp_cache_msync(s_fb[idle], P4_FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, P4_PANEL_W, 1, s_fb[idle]);
        s_cur = idle; return;
    }
    ppa_srm_rotation_angle_t rot = P4_ROT_DEFAULT;
    if (s_inverted) rot = rot == PPA_SRM_ROTATION_ANGLE_90 ? PPA_SRM_ROTATION_ANGLE_270 : PPA_SRM_ROTATION_ANGLE_90;
    ppa_srm_oper_config_t op = {
        .in  = { .buffer = fb, .pic_w = TANK_W, .pic_h = TANK_H, .block_w = TANK_W, .block_h = TANK_H,
                 .block_offset_x = 0, .block_offset_y = 0, .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .out = { .buffer = s_fb[idle], .buffer_size = P4_FB_BYTES, .pic_w = P4_PANEL_W, .pic_h = P4_PANEL_H,
                 .block_offset_x = 0, .block_offset_y = 0, .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .rotation_angle = rot, .scale_x = 1.0f, .scale_y = 1.0f,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    s_nruns = 0; s_runs_all = false;
    p4_tiles_flush(idle, s_dbg & 512, &p4_ops);       /* which tiles: common/p4_tiles.c (collected by p4_turn) */
    s_job_op = op; s_job_cl = (s_dbg & 64) ? s_ppa : s_ppa128;   /* the full burst: clean on the glass in strips (Strato, 2026-10-12 night); 64 = the short bursts */
    s_job_rot = rot; s_job_dsi = idle; s_job_breathe = s_dbg & 32; s_job_one = s_dbg & 128;
    s_job_pending = true; s_turn_err = ESP_OK;
    if (s_nruns || s_runs_all) { s_turn_busy = true; xSemaphoreGive(s_turn_go); }
    if (s_dbg & 2048) turn_wait();                      /* the bench: not pipelined (the turn waited for here, as before) */
    int64_t t2 = esp_timer_get_time();
    s_wait_us += t1 - t0; s_send_us += t2 - t1;
}

void display_port_sleep(void) {
    turn_wait();                               /* (a turn in flight finishes first) */
    backlight(0); s_lit = false;
#ifdef TANK_P4_BETA                            /* (bare DPI panels: no driver's on/off - and only onto a link that takes it) */
    if (s_panel) beta_tx(0x28, NULL, 0);
#elif defined(TANK_TAB5_HW)
    if (s_io) esp_lcd_panel_io_tx_param(s_io, 0x28, NULL, 0);    /* DISPOFF: the ST7123's driver leaves its on/off empty, so say it plainly */
#else
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, false);
#endif
    s_on = false;
    ESP_LOGI(TAG, "panel off, backlight off");
}
void display_port_wake(void) {
#ifdef TANK_P4_BETA
    if (s_panel) beta_tx(0x29, NULL, 0);
#elif defined(TANK_TAB5_HW)
    if (s_io) esp_lcd_panel_io_tx_param(s_io, 0x29, NULL, 0);    /* DISPON */
#else
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, true);
#endif
    s_on = true;                               /* the backlight returns with the first frame (display_port_flush) */
    ESP_LOGI(TAG, "panel on");
}
void display_port_set_brightness(uint8_t level) {
    s_brightness = level;
    if (s_lit) backlight(level);
}
uint8_t display_port_brightness(void) { return s_brightness; }

/* THE SCENE RESTORE for the render buffer the next frame draws into (2026-10-12): it holds
 * the frame before this one, so only that frame's dynamic tiles need the scene put back -
 * the PPA as a plain block copier (no turn, no scale), one operation a run of tiles. A
 * whole 2 MB scene copy by the AHB DMA took 23 ms a frame; this is a few ms. `full`: the
 * scene was baked again, or that buffer held a page / an overlay (the port saw "everything"). */
void display_port_p4_restore(uint16_t *fb, const uint16_t *scene, bool full) {
    if (!s_ppa128 || !fb || !scene) return;
    display_port_p4_restore_wait();                /* (the last one's copies first - one job at a time) */
    turn_wait();                                   /* the turn in flight reads this buffer (THE PIPELINE) */
    s_op_e = ESP_OK;
    { extern int g_p4_x;
      if (s_rs_up && RS_ASYNC_DEFAULT != !!(s_dbg & 4096) && !(g_p4_x & 4)) {   /* THE RESTORE BESIDE THE TICK: the runs decided here, copied by the restore task */
          s_nrruns = 0; s_rs_all = false; s_rs_fb = fb; s_rs_scene = scene;
          s_rs_collect = true; p4_tiles_restore(fb, scene, full, &p4_ops); s_rs_collect = false;
          if (s_nrruns || s_rs_all) { s_rs_busy = true; xSemaphoreGive(s_rs_go); }
          return;
      } }
    p4_tiles_restore(fb, scene, full, &p4_ops);
    esp_err_t e = s_op_e;
    if (e != ESP_OK) { static int said; if (said++ < 3) ESP_LOGE(TAG, "ppa restore: %s", esp_err_to_name(e)); }
}

/* ---- what the pocket tanks' ports grew since (0.3.9 - 0.4.2) and this panel has no use for: the flush task (this port's
 * turn already runs while the next frame is drawn, THE PIPELINE), the panel rail's watch (no PMIC here), the quarter
 * turns and the side tune (a board on a stand), the edge probe ---- */
bool display_port_flush_async(const uint16_t *fb, void (*done)(const uint16_t *, void *), void *arg) { (void)fb; (void)done; (void)arg; return false; }
void display_port_flush_wait(const uint16_t *fb) { (void)fb; }
void display_port_flush_async_prof(int64_t *task_us, int *frames, int64_t *copy_us) { *task_us = 0; *frames = 0; *copy_us = 0; }
bool display_port_panel_power_check(void) { return false; }
uint32_t display_port_panel_heals(void) { return 0; }
/* the watch-dark work (0.4.3, display_port.h): no PMIC REG 90 here, and no board-not-found restart */
int  display_port_boot_reg90(void) { return -1; }
bool display_port_panel_up(void) { return s_panel != NULL; }
bool display_port_board_back(void) { return false; }
void display_port_panel_rail_test_off(void) {}
void display_port_set_quarter(int q) { (void)q; }
bool display_port_quarters(void) { return false; }
void display_port_side_tune(int madctl, int gx, int gy) { (void)madctl; (void)gx; (void)gy; }
void display_port_side_info(int *madctl, int *gx, int *gy, bool *on) { *madctl = 0; *gx = *gy = 0; *on = false; }
void display_port_probe(bool on) { (void)on; }
/* the boot's chip probe (0.3.9.3, the wrong-board question): this image is its own target - a pocket tank's image cannot
   start on a P4 nor this one on an S3 - so there is nothing to ask. The mic ADC (ES7210) is at 0x40 on both big screens */
bool board_port_probed(void) { return true; }
const char *board_port_hw(void) { return NULL; }
uint8_t board_es7210_addr(void) { return 0x40; }
