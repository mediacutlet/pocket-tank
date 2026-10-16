/* display_port_tab5.c — the M5Stack Tab5 (2026-10-16): an ESP32-P4 with a
 * 5-inch 720 x 1280 MIPI-DSI glass, held LANDSCAPE.
 *
 * Two halves to this port, and the line between them matters:
 *
 * 1. THE BOARD'S OWN CHIPS BELONG TO M5STACK'S/ESPRESSIF'S BSP
 *    (espressif/m5stack_tab5_noglib, esp-bsp bsp/m5stack_tab5). A Tab5 is not
 *    one board: it ships with three different glasses and their own touch
 *    chips, told apart on the I2C bus - an ILI9881C with a GT911, an ST7123
 *    with an ST7123 touch, an ST7121 with an ST712x-compatible one. The BSP
 *    runs that test (its own "Discovered board version N" log), powers the
 *    panel and the touch chip through the PI4IOE5V6408 expanders, brings the
 *    MIPI D-PHY's LDO up, and picks the glass's own DPI timing, lane rate and
 *    init sequence. This port would only get that wrong by hand - it did, on
 *    the first try: see docs/board-tab5.md (an ST7123/ST7121 unit does not
 *    answer the ILI9881C driver's DSI ID read, and the DSI HAL's RX wait has
 *    no timeout, so app_main stops with no log line at all).
 *    The noglib variant is the same BSP without LVGL: the tank draws its own
 *    pixels, it has no widget tree to want.
 *
 * 2. WHAT THE TANK NEEDS IS THIS FILE'S. The tank is a 720 x 405 landscape
 *    world (tank.h, TANK_TAB5) and the panel is a 720 x 1280 portrait one, so
 *    the picture needs a quarter turn; and 291k pixels are drawn rather than
 *    922k because a software renderer cannot fill this glass at 30 fps. Both
 *    jobs go to the P4's pixel-processing accelerator (PPA) in one pass:
 *
 *      the tank's frame (720 x 405 RGB565, PSRAM, the renderer's own buffer)
 *        -> PPA SRM: rotate a quarter turn, scale x16/9
 *        -> one of the DSI's two frame buffers (720 x 1280, PSRAM)
 *        -> esp_lcd_panel_draw_bitmap hands that buffer to the DPI engine,
 *           which switches to it at the next frame boundary: no tearing
 *
 *    The renderer, its double buffer, the scene cache and the game's geometry
 *    are untouched by any of it. display_port_panel_to_tank is the exact
 *    inverse, and it is the ONE place the turn is written down (TAB5_ROT). */
#include "display_port.h"
#include "board_pins.h"
#include "tank.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "driver/ppa.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/m5stack_tab5.h"        /* the board: expander, LCD/touch power, the D-PHY's LDO, the glass */
#include "bsp/display.h"

/* ---- the turn ----
 * 90 a quarter turn counter-clockwise, 270 clockwise. Pick the one that lands
 * the tank upright on the glass of THIS unit; the touch mapping below is the
 * same constant's inverse, so flipping it flips the taps with it. A device held
 * the other way up is display_port_set_inverted (a half turn on top).
 * 90 is what this unit needs (2026-10-16: at 270 the tank came up upside down -
 * the Tab5 in its stand had to be turned over - and 270 + 180 = 90). */
#define TAB5_ROT_DEG 90
#if TAB5_ROT_DEG == 90
#define TAB5_ROT PPA_SRM_ROTATION_ANGLE_90
#elif TAB5_ROT_DEG == 180
#define TAB5_ROT PPA_SRM_ROTATION_ANGLE_180
#elif TAB5_ROT_DEG == 270
#define TAB5_ROT PPA_SRM_ROTATION_ANGLE_270
#else
#define TAB5_ROT PPA_SRM_ROTATION_ANGLE_0
#endif

/* the tank's frame fills the panel: the tank turned a quarter is 405 x 720, and
 * 720 x 1280 is 16/9 of nothing else - scale = 1280/720 = 720/405 */
#define TAB5_SCALE   ((float)T_PANEL_H / (float)TANK_W)
#define TAB5_FB_BYTES ((size_t)T_PANEL_W * T_PANEL_H * 2)

static i2c_master_bus_handle_t s_bus;
static esp_lcd_panel_handle_t s_panel;
static ppa_client_handle_t s_ppa;
static SemaphoreHandle_t s_ppa_done;
static uint16_t *s_fb[2];                 /* the DSI's own frame buffers (the BSP makes CONFIG_BSP_LCD_DPI_BUFFER_NUMS of them) */
static uint16_t *s_ppa_out;               /* the buffer the running PPA writes: what the DSI is handed next frame */
static uint16_t *s_ppa_free;              /* the other one: what the next PPA writes */
static bool s_ppa_busy;                   /* a PPA transaction is in flight */
static bool s_inverted;
static uint8_t s_bright = 0xFF;           /* this port's contract: 0..255 */
static int64_t s_last_send_us;            /* this frame's PPA launch */
static int64_t s_last_wait_us;            /* ... and the wait for the previous frame's */

static bool ppa_done_cb(ppa_client_handle_t client, ppa_event_data_t *edata, void *user);

/* the I2C bus: the BSP creates it (its own pins, 400 kHz) and hands it out;
 * main.c's board_i2c_bus() and every port that talks to a chip go through it */
i2c_master_bus_handle_t board_i2c_bus(void)
{
    if (!s_bus) s_bus = bsp_i2c_get_handle();
    return s_bus;
}

/* the touch chip's power (the expander's TOUCH_EN). The BSP raises it inside
 * bsp_touch_new; the touch port calls this first so its probe finds the chip,
 * and so the boot log can say which unit this is before the tank exists. */
void tab5_touch_power(bool on)
{
    if (bsp_feature_enable(BSP_FEATURE_TOUCH, on) != ESP_OK)
        ESP_LOGW("display", "the touch chip's power (expander TOUCH_EN) did not switch");
    if (on) vTaskDelay(pdMS_TO_TICKS(500));      /* the BSP's own settle: the chip will not answer sooner */
}

bool display_port_init(void)
{
    ESP_LOGI("display", "Tab5 display: the BSP brings the glass up | tank %dx%d turned %d deg, scaled %.4f",
             TANK_W, TANK_H, TAB5_ROT_DEG, (double)TAB5_SCALE);
    if (!board_i2c_bus()) return false;

    const bsp_display_config_t cfg = {
        /* 1 Gbps for the ILI9881C and the ST7123; the ST7121 variant wants 965
         * (the BSP turns it down for that one, and it is the BSP that knows
         * which this is - if the picture never comes up on an ST7121 unit, this
         * is the number to change) */
        .dsi_bus = { .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
                     .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS },
    };
    bsp_lcd_handles_t h = { 0 };
    if (bsp_display_new_with_handles(&cfg, &h) != ESP_OK || !h.panel) {
        ESP_LOGE("display", "the BSP could not bring the glass up: no picture");
        return false;
    }
    s_panel = h.panel;
    ESP_LOGI("display", "panel up (%d x %d, %d frame buffers): the turn and the scale are the PPA's from here",
             T_PANEL_W, T_PANEL_H, (int)CONFIG_BSP_LCD_DPI_BUFFER_NUMS);

    if (esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, (void **)&s_fb[0], (void **)&s_fb[1]) != ESP_OK) {
        ESP_LOGE("display", "no DSI frame buffers (CONFIG_BSP_LCD_DPI_BUFFER_NUMS must be 2)");
        return false;
    }
    ESP_LOGI("display", "fb %p / %p (%u KB each)", s_fb[0], s_fb[1], (unsigned)(TAB5_FB_BYTES / 1024));

    s_ppa_done = xSemaphoreCreateBinary();
    const ppa_client_config_t pc = { .oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 2 };
    if (!s_ppa_done || ppa_register_client(&pc, &s_ppa) != ESP_OK) { ESP_LOGE("display", "no PPA client"); return false; }
    const ppa_event_callbacks_t cbs = { .on_trans_done = ppa_done_cb };
    ppa_client_register_event_callbacks(s_ppa, &cbs);
    s_ppa_out = s_fb[0]; s_ppa_free = s_fb[1]; s_ppa_busy = false;
    display_port_set_brightness(s_bright);
    return true;
}

/* ---- one frame: the PPA turns and scales the tank into the idle DSI buffer,
 * then the DSI is told to show it.
 *
 * THE PPA RUNS ONE FRAME BEHIND (2026-10-16): a blocking PPA call measured
 * 20 ms of the 40 ms frame, and the tank has nothing to do while it waits - so
 * the call is NON-blocking now. Frame N starts its turn-and-scale and returns;
 * frame N+1 first collects frame N's (the wait lands where the tank had nothing
 * anyway), hands that buffer to the DSI, and starts its own. The two buffers
 * make that safe: the PPA writes the one the DSI is NOT scanning, and the tank
 * draws into its own buffer, which the PPA last read two frames ago. */
static bool ppa_done_cb(ppa_client_handle_t client, ppa_event_data_t *edata, void *user)
{
    (void)client; (void)edata; (void)user;
    BaseType_t hp = pdFALSE;
    if (s_ppa_done) xSemaphoreGiveFromISR(s_ppa_done, &hp);
    return hp == pdTRUE;
}

void display_port_flush(const uint16_t *fb)
{
    if (!s_ppa || !s_fb[0] || !fb) return;
    const int64_t t0 = esp_timer_get_time();

    if (s_ppa_busy) {                       /* the previous frame's turn: collect it */
        xSemaphoreTake(s_ppa_done, portMAX_DELAY);
        s_ppa_busy = false;
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, T_PANEL_W, T_PANEL_H, s_ppa_out);
    }
    s_last_wait_us = esp_timer_get_time() - t0;

    const int64_t t1 = esp_timer_get_time();
    ppa_srm_oper_config_t op = {
        .in = { .buffer = fb, .pic_w = TANK_W, .pic_h = TANK_H,
                .block_w = TANK_W, .block_h = TANK_H, .block_offset_x = 0, .block_offset_y = 0,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .out = { .buffer = s_ppa_free, .buffer_size = TAB5_FB_BYTES,
                 .pic_w = T_PANEL_W, .pic_h = T_PANEL_H, .block_offset_x = 0, .block_offset_y = 0,
                 .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        /* the device held the other way up adds a half turn to the quarter:
         * 90 + 180 = 270 and 270 + 180 = 90, so one angle says both */
        .rotation_angle = s_inverted ? (TAB5_ROT == PPA_SRM_ROTATION_ANGLE_90
                                        ? PPA_SRM_ROTATION_ANGLE_270 : PPA_SRM_ROTATION_ANGLE_90)
                                     : TAB5_ROT,
        .scale_x = TAB5_SCALE, .scale_y = TAB5_SCALE,
        .mirror_x = false, .mirror_y = false,
        .rgb_swap = false, .byte_swap = false,
        .alpha_update_mode = PPA_ALPHA_NO_CHANGE,
        .mode = PPA_TRANS_MODE_NON_BLOCKING,
    };
    esp_err_t err = ppa_do_scale_rotate_mirror(s_ppa, &op);
    if (err != ESP_OK) { ESP_LOGW("display", "PPA refused the frame: %s", esp_err_to_name(err)); return; }
    s_ppa_out = s_ppa_free;                                        /* what the DSI gets next frame */
    s_ppa_free = (s_ppa_out == s_fb[0]) ? s_fb[1] : s_fb[0];       /* ... and what the PPA writes then */
    s_ppa_busy = true;
    s_last_send_us = esp_timer_get_time() - t1;
}

void display_port_flush_prof(int64_t *wait_us, int64_t *send_us)
{
    if (wait_us) *wait_us = s_last_wait_us;    /* waiting for the previous frame's turn-and-scale */
    if (send_us) *send_us = s_last_send_us;    /* launching this frame's */
}

void display_port_sleep(void)
{
    display_port_set_brightness(0);
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, false);   /* the BSP's panel is a real driver: it has this op */
}

void display_port_wake(void)
{
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, true);
    display_port_set_brightness(s_bright);
}

void display_port_set_inverted(bool inverted) { s_inverted = inverted; }

/* this port's contract is 0..255; the BSP's is a percentage (its LEDC is 10-bit) */
void display_port_set_brightness(uint8_t level)
{
    s_bright = level;
    bsp_display_brightness_set((int)level * 100 / 255);
}
uint8_t display_port_brightness(void) { return s_bright; }

/* ---- the touch's half of the turn: the panel px a finger is on -> the tank px
 * under it. The inverse of the PPA transform in display_port_flush, and nothing
 * else: the per-board calibration the other boards carry is not fitted for this
 * glass yet (the finger's own low landing is the shared s_bias_y). */
void display_port_panel_to_tank(int px, int py, float *tx, float *ty)
{
#if TAB5_ROT_DEG == 90
    /* 90 CCW: panel_x = tank_y * k, panel_y = (TANK_W - tank_x) * k */
    *ty = (float)px / TAB5_SCALE;
    *tx = (float)TANK_W - (float)py / TAB5_SCALE;
#elif TAB5_ROT_DEG == 270
    /* 270 CW: panel_x = (TANK_H - tank_y) * k, panel_y = tank_x * k */
    *ty = (float)TANK_H - (float)px / TAB5_SCALE;
    *tx = (float)py / TAB5_SCALE;
#else
    *tx = (float)px / TAB5_SCALE;
    *ty = (float)py / TAB5_SCALE;
#endif
    if (*tx < 0) *tx = 0;
    if (*tx > TANK_W - 1) *tx = TANK_W - 1;
    if (*ty < 0) *ty = 0;
    if (*ty > TANK_H - 1) *ty = TANK_H - 1;
}

/* ---- the rest of the port's interface: what this board is, and what it has
 * nothing of. This is none of the four Waveshare boards: no PMIC, no expander
 * in the 1.8's sense, no PWR-key sense line, no panel reset to hold. ---- */
bool board_is_round(void) { return false; }
bool board_is_watch(void) { return false; }
bool board_is_square(void) { return false; }
bool board_is_tab5(void) { return true; }
bool board_is_v2(void) { return false; }
bool board_has_expander(void) { return false; }
int  board_pwr_sense_pin(void) { return -1; }
void display_port_deep_sleep_pins(bool tp_awake_high) { (void)tp_awake_high; }
void display_port_deep_sleep_bus(void) {}
void display_port_deep_standby(void) {}
void display_port_set_madctl(uint8_t v) { (void)v; }
uint8_t display_port_madctl(void) { return 0; }
void display_port_frame_origin(int *px, int *py) { *px = 0; *py = 0; }
void display_port_set_view(int view) { (void)view; }
int  display_port_view(void) { return DISPLAY_VIEW_FULL; }
