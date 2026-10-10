/* touch_port_ft3168.c — FT3168 capacitive touch (FT5x06 register family) ->
 * tank_touch_hold / tank_touch_tap, with the same gesture timing as the sim's
 * mouse: press+release < 350 ms with < 24 px displacement = tap (fingertips
 * roll and this panel is 322 ppi); held > 300 ms = hold; a drag down from
 * the top edge = feed at that x; every touched frame streams to
 * tank_touch_drag (a moving stroke wipes algae; a horizontal slash through
 * a canopy trims it). Fish taps hit-test each creature's own reach (tank_fish_hit_r) against the press-time fish
 * snapshot AND the current position - fish move during a tap. While the stats
 * card is up, a tap anywhere on empty glass dismisses it (hunting the same
 * fish again to close it was the old, cumbersome way) and does nothing else.
 * While the battery pill shows (main.c says so each frame), a tap on it -
 * RENDER_BAT_HIT, the pill plus a fingertip's slop - opens the battery page;
 * any release closes that page (2026-09-24).
 * Coordinates are mapped from the portrait panel to the landscape tank.
 * The ROUND board (1.75C, 2026-10-01) has a CST9217 instead, read here
 * directly over I2C; its panel px go through the display port's view
 * (display_port_panel_to_tank) and everything after that is the same.
 * The WATCH (2.06, 2026-10-02) has the FT3168 of the 1.8's V1 board, its
 * reset on a GPIO. Its own build is a portrait tank: panel px = tank px. */
#include "touch_port.h"
#include "display_port.h"
#include "update.h"
#include "board_pins.h"
#include "tank.h"
#include "render.h"
#include "setup.h"
#include "notice.h"
#include "audio_port.h"
#include "progression.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lcd_panel_io.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "touch";
static esp_lcd_touch_handle_t s_tp;
static bool s_down; static int64_t s_press_us; static float s_px, s_py;
static float s_lx, s_ly;                          /* LAST touched position (release classification) */
static float s_fx[N_FISH_MAX], s_fy[N_FISH_MAX];  /* fish positions at press time */
static int s_sel = -1; static int64_t s_sel_us;   /* tapped fish -> stats card */
static bool s_ms;                                 /* milestones page up (its CLOSE button ends it) */
static bool s_cf; static int64_t s_cf_us; static int s_cf_ans;   /* reset confirm prompt */
static bool s_set;                                /* settings page up (CLOSE returns to the milestones page) */
static bool s_shop;                               /* the shop page up (CLOSE returns to the milestones page) */
static bool s_back;                               /* the settings page's CLOSE just brought the milestones page back: that
                                                     release must not reach the page as a tap on ITS CLOSE (same spot) */
static int  s_shop_act;                           /* an UNLOCK / MOVE / SELL tapped: the raw tap code, for main (one-shot) */
static bool s_held_page;                          /* this press opened a piece's page by holding on it (2026-09-24) */
static int  s_set_what, s_set_val;                /* a segment tapped: SET_TAP_* + value, for main */
static bool s_pill;                               /* main.c drew the battery pill this frame: a tap on it is its page's */
static bool s_bat; static int64_t s_bat_us;       /* the battery page up, and since when (it closes itself) */
#define BATTERY_PAGE_US (30LL * 1000000)
#define CONFIRM_TIMEOUT_US (20LL * 1000000)
static bool s_inverted;                           /* screen 180-flipped: mirror into tank space */
/* Fingers land a little BELOW where the eye aims - the pad rolls onto the
 * glass under the fingertip (phones shift their hit targets down for the
 * same reason; Strato saw it on the swatch rows, 2026-09-13). Reported
 * points move UP by this many px in displayed space; director `touch bias
 * <px>` tunes it live. */
static int s_bias_y = 10;
void touch_port_set_bias(int px) { s_bias_y = px; }
int  touch_port_bias(void) { return s_bias_y; }

void touch_port_set_inverted(bool inverted) { s_inverted = inverted; }
extern i2c_master_bus_handle_t board_i2c_bus(void);
extern bool board_is_v2(void);

/* ---- the round board's CST9217 (I2C 0x5A, reset on a GPIO) ----
 * One report: the read command D0 00, ten bytes back, then the read's ack
 * (D0 00 AB) as SensorLib sends it. Byte 6 = AB marks a real report, byte 5
 * the finger count, and the first finger is 06 in its low nibble while down:
 * x = b1:b3.hi, y = b2:b3.lo, 12 bits each, in panel px - mirrored on both
 * axes against the picture (Waveshare's BSP sets mirror_x and mirror_y). */
static i2c_master_dev_handle_t s_cst;
static bool cst9217_init(void) {
    gpio_config_t rst = { .pin_bit_mask = 1ULL << R_PIN_TP_RST, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&rst); gpio_sleep_sel_dis(R_PIN_TP_RST);
    gpio_set_level(R_PIN_TP_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(R_PIN_TP_RST, 1); vTaskDelay(pdMS_TO_TICKS(50));
    if (i2c_master_probe(board_i2c_bus(), I2C_ADDR_CST9217, 50) != ESP_OK) { ESP_LOGW(TAG, "no CST9217"); return false; }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = I2C_ADDR_CST9217, .scl_speed_hz = 400000,
                                .flags.disable_ack_check = 1 };   /* it NACKs a read now and then while it scans: not an error, and not a log line each */
    if (i2c_master_bus_add_device(board_i2c_bus(), &cfg, &s_cst) != ESP_OK) { s_cst = NULL; return false; }
    ESP_LOGI(TAG, "CST9217 ready");
    return true;
}
/* the night (2026-10-02): the chip's own sleep instead of its reset held low
 * all night - SensorLib's sequence: D1 1E twice opens command mode (0002
 * echoes 1E), D1 01 the debug mode (echoes 01), then D1 05 sleeps it. The
 * grace left it in reset, so it is let out and given its boot first. Only a
 * reset brings it back: the wake is a reboot, and cst9217_init pulses it. */
bool touch_port_deep_sleep(void) {
    if (!s_cst) return false;
    gpio_set_level(R_PIN_TP_RST, 1); vTaskDelay(pdMS_TO_TICKS(150));
    static const uint8_t open[2] = { 0xD1, 0x1E }, dbg[2] = { 0xD1, 0x01 }, slp[2] = { 0xD1, 0x05 }, where[2] = { 0x00, 0x02 };
    uint8_t r[4] = { 0 };
    bool open_ok = false;
    for (int i = 0; i < 3 && !open_ok; i++) {
        i2c_master_transmit(s_cst, open, 2, 20); i2c_master_transmit(s_cst, open, 2, 20);
        open_ok = i2c_master_transmit_receive(s_cst, where, 2, r, 4, 20) == ESP_OK && r[1] == 0x1E;
        if (!open_ok) vTaskDelay(pdMS_TO_TICKS(50));
    }
    i2c_master_transmit(s_cst, dbg, 2, 20);
    bool dbg_ok = i2c_master_transmit_receive(s_cst, where, 2, r, 2, 20) == ESP_OK && r[1] == 0x01;
    vTaskDelay(pdMS_TO_TICKS(10));
    bool ok = i2c_master_transmit(s_cst, slp, 2, 20) == ESP_OK && open_ok && dbg_ok;
    ESP_LOGI(TAG, "CST9217 sleep: command mode %s, debug mode %s -> %s", open_ok ? "ok" : "NO ECHO", dbg_ok ? "ok" : "NO ECHO",
             ok ? "asleep, reset held high" : "not confirmed: back into reset for the night");
    if (!ok) gpio_set_level(R_PIN_TP_RST, 0);
    return ok;
}
/* A press is one press: the chip answers some reads mid-touch with nothing (no
 * new report ready, or a NACK while it scans), and taken at their word each
 * was a lift - one finger down read as two taps (Strato, 2026-10-01). So the
 * finger is DOWN until no report has shown it for CST_LIFT_US; between
 * reports the last position stands.
 * 2026-10-03, Strato: "double taps don't always turn the light off" on the
 * round board. That silence also swallowed the gap between two quick taps
 * (one long press), and at 17 fps it was 65-95 ms before a lift showed. So a
 * real report with no finger in it (byte 6 = AB) is a lift at once - only
 * silence (no report ready, a NACK) is bridged. Director `touch lift <ms>` /
 * `touch said on|off` tune both live; the stroke log tells them apart. */
#define CST_LIFT_US 60000
static int  s_lift_us = CST_LIFT_US;              /* silence this long inside a press = a lift */
static bool s_lift_said = true;                   /* a report that says "no finger" lifts at once */
static int s_cst_gaps;                            /* reads inside a press that showed no finger (the stroke log) */
static int s_cst_said, s_cst_back, s_cst_maxgap_ms, s_cst_again_ms = -1;   /* ... "no finger" reports, a finger after one, the longest silence, ms since the last lift at the press */
static bool s_cst_by_said;                        /* ... and how the press ended */
static int64_t s_seen_us;                         /* the last read that showed a finger, any chip: a press is timed to it, not to the lift's bridge */
void touch_port_set_lift(int ms, int said) { if (ms > 0) s_lift_us = ms * 1000; if (said >= 0) s_lift_said = said; }
int  touch_port_lift_ms(void) { return s_lift_us / 1000; }
bool touch_port_lift_said(void) { return s_lift_said; }
static bool cst9217_read(uint16_t *x, uint16_t *y) {
    static const uint8_t cmd[2] = { 0xD0, 0x00 }, ack[3] = { 0xD0, 0x00, 0xAB };
    static uint16_t lx, ly; static int64_t seen_us, lift_us; static bool down, said_in;
    uint8_t d[10] = { 0 };
    int64_t now = esp_timer_get_time();
    bool finger = false, said = false;
    if (i2c_master_transmit_receive(s_cst, cmd, 2, d, sizeof d, 20) == ESP_OK) {
        i2c_master_transmit(s_cst, ack, 3, 20);
        finger = d[6] == 0xAB && (d[5] & 0x7F) && (d[0] & 0x0F) == 0x06;
        said = d[6] == 0xAB && d[0] != 0xAB && !finger;      /* a report, and no finger in it */
    }
    if (finger) {
        int rx = (d[1] << 4) | (d[3] >> 4), ry = (d[2] << 4) | (d[3] & 0x0F);
        if (rx > R_PANEL - 1) rx = R_PANEL - 1;
        if (ry > R_PANEL - 1) ry = R_PANEL - 1;
        lx = (uint16_t)(R_PANEL - 1 - rx); ly = (uint16_t)(R_PANEL - 1 - ry);
        if (!down) { s_cst_gaps = s_cst_said = s_cst_back = s_cst_maxgap_ms = 0; said_in = false;
                     s_cst_again_ms = lift_us ? (int)((now - lift_us) / 1000) : -1; }
        else {
            if ((int)((now - seen_us) / 1000) > s_cst_maxgap_ms) s_cst_maxgap_ms = (int)((now - seen_us) / 1000);
            if (said_in) { s_cst_back++; said_in = false; }
        }
        seen_us = s_seen_us = now; down = true;
    } else if (down) {
        if (said) { s_cst_said++; said_in = true; }
        if ((said && s_lift_said) || now - seen_us > s_lift_us) { down = false; lift_us = now; s_cst_by_said = said && s_lift_said; }
        else s_cst_gaps++;
    }
    *x = lx; *y = ly;
    return down;
}
/* ---- the watch's FT3168 (I2C 0x38, reset on a GPIO), read directly ----
 * Idle, the chip drops into its monitor mode and NACKs a read every few
 * seconds (every ~3.7 s on the bench, five error lines each through the
 * esp_lcd driver). So: its own device with the ack check off - a read the
 * chip did not answer comes back all ones, which is no report - and the
 * CST9217's rule for silence inside a press. A report: TD_STATUS (02) = the
 * finger count, then the first point's XH (event in bits 7:6, 1 = lift) XL
 * YH YL, 12 bits each, in panel px, unmirrored. */
static i2c_master_dev_handle_t s_ft;
static bool ft3168_init(void) {
    gpio_config_t rst = { .pin_bit_mask = 1ULL << W_PIN_TP_RST, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&rst); gpio_sleep_sel_dis(W_PIN_TP_RST);
    gpio_set_level(W_PIN_TP_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(W_PIN_TP_RST, 1); vTaskDelay(pdMS_TO_TICKS(300));   /* its boot: it answers nothing sooner */
    if (i2c_master_probe(board_i2c_bus(), I2C_ADDR_FT3168, 50) != ESP_OK) { ESP_LOGW(TAG, "no FT3168"); return false; }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = I2C_ADDR_FT3168, .scl_speed_hz = 400000,
                                .flags.disable_ack_check = 1 };
    if (i2c_master_bus_add_device(board_i2c_bus(), &cfg, &s_ft) != ESP_OK) { s_ft = NULL; return false; }
    ESP_LOGI(TAG, "FT3168 ready (the watch: read directly)");
    return true;
}
static bool ft3168_read(uint16_t *x, uint16_t *y) {
    static uint16_t lx, ly; static int64_t seen_us; static bool down;
    uint8_t reg = 0x02, d[5] = { 0 };
    int64_t now = esp_timer_get_time();
    bool answered = i2c_master_transmit_receive(s_ft, &reg, 1, d, sizeof d, 20) == ESP_OK && (d[0] & 0x0F) <= 2 && d[0] != 0xFF;
    if (answered && (d[0] & 0x0F) >= 1 && (d[1] >> 6) != 1) {
        int rx = ((d[1] & 0x0F) << 8) | d[2], ry = ((d[3] & 0x0F) << 8) | d[4];
        if (rx > W_PANEL_W - 1) rx = W_PANEL_W - 1;
        if (ry > W_PANEL_H - 1) ry = W_PANEL_H - 1;
        lx = (uint16_t)rx; ly = (uint16_t)ry;
        if (!down) s_cst_gaps = 0;
        seen_us = s_seen_us = now; down = true;
    } else if (down) {
        if (answered || now - seen_us > CST_LIFT_US) down = false;   /* "no finger", said plainly, is a lift; silence is bridged */
        else s_cst_gaps++;
    }
    *x = lx; *y = ly;
    return down;
}
/* one read of the glass, whichever chip: true while a finger is down, in panel px */
static bool panel_read(uint16_t *x, uint16_t *y) {
    if (s_cst) return cst9217_read(x, y);
    if (s_ft) return ft3168_read(x, y);
    if (!s_tp) return false;
    uint16_t st[1]; uint8_t n = 0;
    esp_lcd_touch_read_data(s_tp);
    if (!esp_lcd_touch_get_coordinates(s_tp, x, y, st, &n, 1) || n == 0) return false;
    s_seen_us = esp_timer_get_time();
    return true;
}

bool touch_port_init(void) {
    if (board_is_round()) return cst9217_init();
    if (board_is_watch()) return ft3168_init();
    esp_lcd_panel_io_handle_t io;
    bool v2 = board_is_v2();
    esp_lcd_panel_io_i2c_config_t io_cfg = v2 ? (esp_lcd_panel_io_i2c_config_t)ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG()
                                              : (esp_lcd_panel_io_i2c_config_t)ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_cfg.dev_addr = v2 ? I2C_ADDR_CST816 : I2C_ADDR_FT3168; io_cfg.scl_speed_hz = 400000;
    if (esp_lcd_new_panel_io_i2c(board_i2c_bus(), &io_cfg, &io) != ESP_OK) { ESP_LOGW(TAG, "no touch io"); return false; }
    esp_lcd_touch_config_t tp_cfg = { .x_max = PANEL_W, .y_max = PANEL_H, .rst_gpio_num = -1, .int_gpio_num = -1,
        .levels = { .reset = 0, .interrupt = 0 }, .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 } };
    esp_err_t err = v2 ? esp_lcd_touch_new_i2c_cst816s(io, &tp_cfg, &s_tp)
                       : esp_lcd_touch_new_i2c_ft5x06(io, &tp_cfg, &s_tp);
    if (err != ESP_OK) { ESP_LOGW(TAG, "no %s", v2 ? "CST816" : "FT3168"); return false; }
    ESP_LOGI(TAG, "%s ready", v2 ? "CST816" : "FT3168");
    return true;
}

/* panel -> tank space, CALIBRATED (2026-09-30). Nine crosshairs on Strato's
 * V2 board (CST816), a press each, the panel's report against the target
 * (update mode's `ota calib`; the log fits the map):
 *
 *   target x   48  224  400      reported  22  218  424   (means over 3 rows)
 *   target y   40  184  328      reported  23  192  351   (means over 3 cols)
 *
 * so reported = 1.142 x - 33 and 1.138 y - 22 (with the old 10 px y bias
 * already inside the reported y): the panel stretches ~14% about the top
 * left. A tap at the bottom right read 25 px right and 25 px low - "aim
 * slightly left", "fingers land low near the bezel". Inverted: the same map
 * on the mirrored raw values (the panel's geometry does not turn with the
 * picture). The V1 board (FT3168) was never measured: the old bias only.
 *
 * The round board (CST9217), 2026-10-01: 63 presses on its 3 x 3
 * (tools/touch_calib.py, docs/touch/), the bias inside as above:
 *
 *   target y  110  233  356      reported  114  243  374   (sd 4..10 px a row)
 *   target x  110  233  356      reported  107  231  357   (sd 8..13 px a column)
 *
 * reported y = 1.059 y - 3.0: a press at the foot of the bowl read 18 px
 * low. In x the fit (1.015 x - 4.8, the gain +/- 0.013) cannot be told from
 * the taps' own scatter - a fingertip lands within a millimeter, 10 px on
 * this glass - so x is left as the panel says.
 *
 * The watch (FT3168), 2026-10-01: 45 presses on its 3 x 3, WORN on the
 * wrist and tapped with the other hand's finger (two sittings), the bias
 * inside:
 *
 *   target x   70  205  340      reported   82  213  342
 *   target y   70  251  432      reported   79  257  425
 *
 * reported = 0.963 x + 14.8 and 0.957 y + 13.5: this panel reads SMALL (4%),
 * where the other two read large, and worn the finger lands right of and
 * below the cross at the top left of the glass. A first sitting of 27, the
 * watch held in the hand and tapped with its thumb, sat 10 px left of these
 * (0.974 x + 5.7, 0.975 y + 14.7): the grip moves where a finger lands by
 * most of a millimeter, and a watch is used worn. */
#define CAL_X_GAIN 1.142f
#define CAL_X_OFF  33.0f
#define CAL_Y_GAIN 1.138f
#define CAL_Y_OFF  32.0f     /* (22 until 2026-10-04: the finger's landing is CAL_LAND deeper than the bias, below - upright the same map) */
#define CAL_R_Y_GAIN 1.059f
/* The bowl turned over (2026-10-04, 27 presses, held in the hand, the picture
 * turned): every press read 22 px LOW on the picture as shown (y = 0.977 t +
 * 27.6, rms 5; x within its scatter). Upright, the panel's own offset and the
 * finger's low landing were one number (+7 raw) and could not be told apart;
 * turned over the landing changes sign and they can: the panel reports 15 px
 * HIGH and a finger lands ~22 px low (the bias alone says 10). The same two
 * numbers also give the 10-01 turned-over sitting, taken when the turn came
 * before the map (+2 predicted, +0.1 measured). So on the bowl the landing is
 * the bias + CAL_LAND more and the offset carries the panel's 15: upright
 * the map is the one fitted on 10-01, (raw - 7) / 1.059, to the pixel.
 * The 1.8 (V2) turned over, the same evening (27 presses, a second board):
 * 19 px low over the two rows its panel reaches, which is the same finger -
 * a landing of 21 px, the panel 33 high - so CAL_LAND is the calibrated
 * boards' and the 1.8's offset carries the rest (upright unchanged). Checked
 * with a fresh turned sitting on each at a landing of 22: the bowl +1 px, the
 * 1.8 -5 on the rows its panel reaches - so 20 it is, between the two (the
 * bowl +3, the 1.8 -2 by the arithmetic; the taps scatter 5..10). Turned over,
 * the 1.8's panel runs out ~50 px above the picture's foot: a press aimed
 * lower reads there. */
#define CAL_LAND     10.0f
#define CAL_R_Y_OFF  13.0f
#define CAL_W_X_GAIN 0.963f
/* The watch TURNED (settings SCREEN, worn buttons toward the elbow), 2026-10-04,
 * 27 presses worn: y on the cross (+1), x 17 px RIGHT of it. The other hand's
 * finger comes at a worn watch from the same side whichever way round it is
 * strapped, and lands ~8 px toward that side: upright that was inside the
 * fit's 14.8, turned it doubles. So x has a landing of its own, turned with
 * the picture like y's, and the offset keeps the panel's 6.8 (upright: the
 * same map). y needed nothing: its landing is the bias's 10. */
#define CAL_W_LAND_X 8.0f
#define CAL_W_X_OFF  (-6.8f)
#define CAL_W_Y_GAIN 0.957f
#define CAL_W_Y_OFF  (-13.5f)
static void cal_point(float rx, float ry, float *tx, float *ty);
/* an upright raw report -> the finger's point on the picture as shown: calibrated, then turned with the picture */
static void cal_view(float rx, float ry, float *tx, float *ty) {
    cal_point(rx, ry, tx, ty);
    if (s_inverted) { *tx = TANK_W - 1 - *tx; *ty = TANK_H - 1 - *ty; }
}
static void cal_point(float rx, float ry, float *tx, float *ty) {                 /* a raw tank-space report -> where the finger is */
    float land = (float)s_bias_y + (board_is_round() || board_is_v2() ? CAL_LAND : 0);
    ry -= s_inverted ? -land : land;                  /* the finger's own low landing is the viewer's "down": turned, that is the panel's up */
    if (board_is_watch()) rx -= s_inverted ? -CAL_W_LAND_X : CAL_W_LAND_X;   /* ... and on a wrist it lands toward the tapping hand */
    if (board_is_round()) ry = (ry + CAL_R_Y_OFF) / CAL_R_Y_GAIN;
    else if (board_is_watch()) { rx = (rx + CAL_W_X_OFF) / CAL_W_X_GAIN; ry = (ry + CAL_W_Y_OFF) / CAL_W_Y_GAIN; }
    else if (board_is_v2()) { rx = (rx + CAL_X_OFF) / CAL_X_GAIN; ry = (ry + CAL_Y_OFF) / CAL_Y_GAIN; }
    if (rx < 0) rx = 0;
    if (rx > TANK_W - 1) rx = TANK_W - 1;
    if (ry < 0) ry = 0;
    if (ry > TANK_H - 1) ry = TANK_H - 1;
    *tx = rx; *ty = ry;
}
/* the panel's raw reach, as seen since boot (director `touch`): the stroke's
 * reach below takes it for 0..max on both axes - scrub the four walls, then ask */
static int s_raw_x0 = TANK_W, s_raw_x1 = -1, s_raw_y0 = TANK_H, s_raw_y1 = -1;
void touch_port_raw_seen(int *x0, int *x1, int *y0, int *y1) { *x0 = s_raw_x0; *x1 = s_raw_x1; *y0 = s_raw_y0; *y1 = s_raw_y1; }
static void map_touch(uint16_t px, uint16_t py, float *tx, float *ty) {
    int ox, oy; display_port_frame_origin(&ox, &oy);  /* a panel bigger than the frame (the rectangle image on the watch): the frame's corner */
    int fx = (int)px - ox, fy = (int)py - oy;
    /* the raw report in tank space AS IF UPRIGHT: the calibration below is the
     * panel's own stretch, fitted upright, so it is undone in that frame and the
     * 180-degree turn comes after it (cal_view). Until 2026-10-02 the turn came
     * first, and a turned picture's taps were off by the fit's asymmetry: ~15 px
     * on the watch, ~20 at the top of the bowl, ~2 on the 1.8. */
#ifdef TANK_WATCH                                     /* the watch's portrait tank: the panel's px are the tank's */
    int rx = fx, ry = fy;
#else
    int rx = TANK_W - 1 - fy, ry = fx;
#endif
    if (board_is_round()) {                           /* square panel, no turn: the display port's view of the frame, undone */
        float fx, fy; display_port_panel_to_tank(px, py, &fx, &fy);
        rx = (int)fx; ry = (int)fy;
    }
    if (rx < s_raw_x0) s_raw_x0 = rx;
    if (rx > s_raw_x1) s_raw_x1 = rx;
    if (ry < s_raw_y0) s_raw_y0 = ry;
    if (ry > s_raw_y1) s_raw_y1 = ry;
    cal_view((float)rx, (float)ry, tx, ty);
}
/* The STROKE's reach to the glass (2026-10-01). The panel's report runs out
 * before the glass does: its raw 0..max lands, through the map above, on x
 * 29..420 and y 11..333 - a finger on the outer 29 px (the bottom 35) reads
 * as the limit, never beyond. Taps want exactly that map (the crosshairs),
 * but a scrub could no longer reach the film in the edge cells and a slash
 * stopped short of a bed's outer frond and of the floor (Strato: "difficult
 * if not impossible to scrub algae which sits too close to the glass edge",
 * "the farthest left and most far right seagrass fronds cannot be clipped").
 * Before the calibration the stretched report overshot to the edge on its
 * own. So the stream to tank_touch_drag - and nothing else - is stretched
 * over the last EDGE_BAND_* px inside each limit so the limit lands on the
 * glass (EDGE_SNAP_* short of it already does: a pad at the bezel does not
 * drive the report to its very end). A stroke keeps the finger's own
 * travel for its first EDGE_RAMP0_PX and eases into the full stretch by
 * EDGE_RAMP1_PX: a tap's roll is not magnified into a wipe or a snip (tank.c
 * WIPE_ENGAGE_PX / SLASH_PX), a real stroke runs to the glass. It LANDS
 * fully stretched in y (a sweep along the floor is on the floor from its
 * first frame, and stays level) but only EDGE_LAND_X of the way in x: a
 * finger put down on a bed's outer frond, 24 px from the wall, reads as the
 * panel's limit - landed at the glass it would be a whole pad's width off
 * the spine and arm no scissors (tank.c SLASH_START_SIDE_PX); landed half
 * way it is beside the spine with the frond inside its reach.
 * One exception to the ramp, x only: travel TOWARD a side wall is stretched
 * from its first px. A swipe at a wall frond is a short thing in the report -
 * the pad runs into the bezel and its center stops 15..25 px short of the
 * limit - so it never travelled far enough to open the ramp, or to be a
 * slash at all (Strato's log, 2026-10-01: starts at x 43..71, lifts at
 * 39..53, 4..20 px apart). The price: within the band, a tap that rolls 8 px
 * toward the wall on a tall frond can snip it. */
#define EDGE_BAND_X   44.0f
#define EDGE_SNAP_X    8.0f
#define EDGE_BAND_Y   44.0f
#define EDGE_SNAP_Y    8.0f
#define EDGE_RAMP0_PX 18.0f
#define EDGE_RAMP1_PX 42.0f
#define EDGE_LAND_X    0.5f
static float s_sx, s_sy, s_reach;                 /* this stroke: its landing point, how far the stretch is in (0..1) */
static float s_ex0;                               /* ... and its landing x fully stretched */
static bool  s_log;                               /* director `touch log on`: one line per press, at its release */
static float s_dx0, s_dx1, s_dlx, s_dly; static int s_frames, s_raw_px, s_raw_py;   /* ... what it reports */
void touch_port_set_log(bool on) { s_log = on; }
static float edge_stretch1(float v, float lo, float hi, float max, float band, float snap) {
    if (lo > 0 && v < lo + band) {
        float e = v - lo - snap;
        return e <= 0 ? 0 : e * (lo + band) / (band - snap);
    }
    if (hi < max && v > hi - band) {
        float e = hi - v - snap;
        return e <= 0 ? max : max - e * (max - hi + band) / (band - snap);
    }
    return v;
}
static void edge_stretch(float x, float y, float *ox, float *oy) {
    float x0, y0, x1, y1;                         /* the panel's reach: its raw extremes through the same map (the bias is live) */
    cal_view(0, 0, &x0, &y0); cal_view(TANK_W - 1, TANK_H - 1, &x1, &y1);
    if (x0 > x1) { float k = x0; x0 = x1; x1 = k; }   /* turned: the far corner is the near one */
    if (y0 > y1) { float k = y0; y0 = y1; y1 = k; }
    *ox = edge_stretch1(x, x0, x1, TANK_W - 1, EDGE_BAND_X, EDGE_SNAP_X); *oy = edge_stretch1(y, y0, y1, TANK_H - 1, EDGE_BAND_Y, EDGE_SNAP_Y);
}
/* the calibrated point of a touched frame -> the stroke's point for tank_touch_drag */
static void stroke_point(bool landed, float tx, float ty, float *dx, float *dy) {
    float ex, ey; edge_stretch(tx, ty, &ex, &ey);
    if (landed) { s_sx = tx + (ex - tx) * EDGE_LAND_X; s_sy = ey; s_ex0 = ex; s_reach = 0; }
    float d = hypotf(tx - s_px, ty - s_py);
    float r = (d - EDGE_RAMP0_PX) / (EDGE_RAMP1_PX - EDGE_RAMP0_PX);
    if (r > 1) r = 1;
    if (r > s_reach) s_reach = r;                 /* latched: scrubbing back past the landing point stays stretched */
    float ux = s_sx + tx - s_px;                  /* the finger's own travel from its landing ... */
    if ((ex - tx) * (tx - s_px) > 0) ux = s_sx + ex - s_ex0;   /* ... but stretched at once on its way TO a wall (below) */
    *dx = ux * (1 - s_reach) + ex * s_reach;
    *dy = (s_sy + ty - s_py) * (1 - s_reach) + ey * s_reach;
    if (*dx < 0) *dx = 0;
    if (*dx > TANK_W - 1) *dx = TANK_W - 1;
    if (*dy < 0) *dy = 0;
    if (*dy > TANK_H - 1) *dy = TANK_H - 1;
}
static bool s_upd; static int s_upd_act;                 /* the UPDATES page (2026-09-30) */
bool touch_port_read_raw(float *x, float *y) {
    uint16_t px[1], py[1];
    if (!panel_read(px, py)) return false;
    map_touch(px[0], py[0], x, y);
    return true;
}

/* call every frame from the tank task */
void touch_port_poll(tank_t *t) {
    int64_t now = esp_timer_get_time();
    if (s_cf && now - s_cf_us > CONFIRM_TIMEOUT_US) touch_port_confirm_answer(-1);   /* nobody answered: keep the tank */
    if (!s_tp && !s_cst && !s_ft) return;
    uint16_t x[1], y[1];
    bool touched = panel_read(x, y);
    /* portrait panel (px,py) -> landscape tank (tx,ty): tx = TANK_W-1-py, ty = px;
     * flipped screen: mirror both, so downstream gestures live in displayed space */
    float tx = s_lx, ty = s_ly;
    if (touched) map_touch(x[0], y[0], &tx, &ty);           /* calibrated (above); the release keeps the last position */
    if (touched && !s_down) {
        audio_port_prewarm();                   /* the release's cue plays warm */
        s_press_us = now; s_px = tx; s_py = ty;
        /* snapshot the school: the user aims at where a fish WAS - by release
           a darting fish has moved and the finger hid it the whole time */
        for (int i = 0; i < t->n_fish && i < N_FISH_MAX; i++) { s_fx[i] = t->fish[i].x; s_fy[i] = t->fish[i].y; }
    }
    bool su = setup_active();                                /* before the touch: BEGIN's release is not a tank tap */
    if (s_set && !s_cf && !su) {                             /* the settings page owns the glass: segments, the seconds wheel, CLOSE */
        int v = 0, r = render_settings_touch(t, tx, ty, touched, &v);
        if (r) ESP_LOGI(TAG, "settings: %s %d", r == SET_TAP_CLOSE ? "CLOSE" : r == SET_TAP_BRIGHT ? "brightness" : r == SET_TAP_VOLUME ? "volume"
                                                  : r == SET_TAP_THEME ? "theme" : r == SET_TAP_LIGHT ? "lights out" : r == SET_TAP_SCREEN ? "screen (1 = turned)"
                                                  : r == SET_TAP_FEED ? "auto feed (1 = on)" : r == SET_TAP_ROTATE ? "rotation (1 = locked)" : r == SET_TAP_ABOUT ? "about (1 = up)" : r == SET_TAP_RESET ? "reset" : "idle seconds", v);
        if (r == SET_TAP_CLOSE) { s_set = false; s_ms = true; s_back = true; }   /* back to the milestones page (2026-09-16); the release is spent */
        else if (r == SET_TAP_UPDATES) { s_set = false; s_upd = true; s_back = true; ESP_LOGI(TAG, "updates page up"); }
        else if (r == SET_TAP_RESET) { s_set = false; s_back = true; touch_port_confirm_open(); ESP_LOGI(TAG, "settings: RESET - the confirm prompt is up (NO / YES, it times out)"); }
        else if (r == SET_TAP_BRIGHT || r == SET_TAP_VOLUME || r == SET_TAP_LIGHT || r == SET_TAP_IDLE || r == SET_TAP_FEED || r == SET_TAP_ROTATE || r == SET_TAP_ABOUT) { s_set_what = r; s_set_val = v; }
    }
    if (s_upd && !s_cf && !su) {                             /* the UPDATES page: CHECK (main restarts), FORGET, CLOSE */
        int r = updates_page_touch(tx, ty, touched);
        if (r == UPD_TAP_CLOSE) { s_upd = false; s_set = true; s_back = true; ESP_LOGI(TAG, "updates page: CLOSE -> settings"); }
        else if (r == UPD_TAP_CHECK) { s_upd_act = r; ESP_LOGI(TAG, "updates page: CHECK FOR UPDATES"); }
        else if (r == UPD_TAP_FORGET) ESP_LOGI(TAG, "updates page: network forgotten");
    }
    if (su && !s_cf) {
        bool birth = setup_is_birth(), rename = setup_is_rename(); int who = setup_fish(), place = setup_item();
        setup_touch(t, tx, ty, touched);                     /* taps and the letter wheel, classified in setup.c */
        if (!setup_active()) {
            if (rename) ESP_LOGI(TAG, "rename closed: the fish is %s", who >= 0 && who < t->n_fish ? t->fish[who].name : "?");
            else if (birth) ESP_LOGI(TAG, "birth flow done: %s named and saved", who >= 0 && who < t->n_fish ? t->fish[who].name : "?");
            else if (place >= 0) ESP_LOGI(TAG, "placed: %s at x %.0f, %s layer, saved", SD_ITEMS[place].name, tank_decor_x(t, place),
                                          tank_decor_z(t, place) == DECOR_Z_BACK ? "BEHIND" : tank_decor_z(t, place) == DECOR_Z_FRONT ? "IN FRONT" : "AMONG");
            else ESP_LOGI(TAG, "setup done: %s + %s", t->fish[0].name, t->fish[1].name);
        }
    }
    { int rf = setup_take_renamed();                         /* a rename closed (2026-10-01): back to the milestones page, the fish's card up */
      if (rf >= 0) { s_ms = true; s_set = false; s_shop = false; s_sel = -1; render_milestones_show_fish(t, rf); } }
    bool modal = s_ms || s_set || s_shop || s_cf || su || s_bat || s_upd;   /* a page or a prompt owns the glass */
    if (touched) {                                           /* stroke = wipe/slash, reaching the glass (stroke_point above) */
        float dx, dy; stroke_point(!s_down, tx, ty, &dx, &dy);
        if (!s_down) { s_dx0 = s_dx1 = dx; s_frames = 0; s_raw_px = x[0]; s_raw_py = y[0]; }
        if (dx < s_dx0) s_dx0 = dx;
        if (dx > s_dx1) s_dx1 = dx;
        s_dlx = dx; s_dly = dy; s_frames++;
        s_lx = tx; s_ly = ty; if (!modal) tank_touch_drag(t, dx, dy);
    }
    /* tap-and-hold on a decoration (2026-09-24, Strato: "right now it's like 5
       taps to get back to the edit"): a still finger 700 ms on an owned
       piece opens its placement page (MOVE / DEPTH / SELL) right there */
    if (touched && !modal && !s_held_page && t->tool == TOOL_HAND && now - s_press_us > 700000 && fabsf(tx - s_px) < 24 && fabsf(ty - s_py) < 24) {
        int it = tank_decor_hit(t, s_px, s_py);
        if (it >= 0) { setup_begin_place(t, it); s_held_page = true; s_sel = -1;
                       ESP_LOGI(TAG, "held on the %s: placement page up", SD_ITEMS[it].name); }
    }
    if (touched && !modal && !s_held_page && now - s_press_us > 300000 && fabsf(ty - s_py) < 30) tank_touch_hold(t, tx, ty);
    if (!touched && s_down) {
        /* release: classify with the LAST touched position (the old code fell
           back to the PRESS position here, so dx/dy were always 0 - every
           quick swipe read as a tap and the drag-feed could never fire) */
        float dx = s_lx - s_px, dy = s_ly - s_py;
        int64_t held_us = s_seen_us - s_press_us;      /* the finger's own time on the glass: the lift's bridge is not the keeper's */
        if (s_log) ESP_LOGI(TAG, "stroke: raw %d,%d | press %.0f,%.0f -> lift %.0f,%.0f | lands %.0f,%.0f, drag x %.0f..%.0f, ends %.0f,%.0f | %d frames %d ms%s | scissors %s%s%s, travel %.0f",
                            s_raw_px, s_raw_py, s_px, s_py, s_lx, s_ly, s_sx, s_sy, s_dx0, s_dx1, s_dlx, s_dly, s_frames, (int)(held_us / 1000),
                            modal ? " (a page had the glass)" : "", t->slash_armed ? "ARMED" : "not armed", t->slash_engaged ? " ENGAGED" : "", t->slash_cut ? " CUT" : "", t->drag_dist);
        if (s_log && s_ft) ESP_LOGI(TAG, "stroke: %d reads inside the press showed no finger (bridged, lift after %d ms of them)", s_cst_gaps, CST_LIFT_US / 1000);
        if (s_log && s_cst) ESP_LOGI(TAG, "stroke: lift by %s (%d ms rule, said %s) | %d silent reads bridged, longest silence %d ms | %d \"no finger\" reports, a finger again after %d | pressed %d ms after the last lift | lift seen %d ms after the finger",
                                     s_cst_by_said ? "the chip's word" : "silence", s_lift_us / 1000, s_lift_said ? "on" : "off", s_cst_gaps, s_cst_maxgap_ms, s_cst_said, s_cst_back, s_cst_again_ms, (int)((now - s_seen_us) / 1000));
        if (s_cf) {                     /* the prompt owns the glass: a press AND release on the
                                           same button answers it, nothing else counts - not
                                           even the tap that opened it (it began before) */
            int h = s_press_us > s_cf_us ? render_confirm_hit(s_px, s_py) : 0;
            if (h && h == render_confirm_hit(s_lx, s_ly)) touch_port_confirm_answer(h);
            goto released;
        }
        if (su) {                       /* the setup had the glass (setup_touch above); just the log:
                                           where the finger landed vs what it hit, in case this panel
                                           reports fingers offset from where they feel */
            ESP_LOGI(TAG, "setup touch press %.0f,%.0f release %.0f,%.0f -> %s", s_px, s_py, s_lx, s_ly,
                     setup_hit_name(setup_active() ? setup_hit(s_px, s_py) : 0));
            s_sel = -1; goto released;
        }
        if (s_bat) { s_bat = false; ESP_LOGI(TAG, "battery page closed"); goto released; }   /* any release closes it */
        if (held_us < 350000 && dx * dx + dy * dy < 24 * 24) {
            if (notice_current()) {                                 /* (the lights-out notice lets its tap through: notice.h) */
                bool took = notice_dismiss(); ESP_LOGI(TAG, "tap closed the announcement"); if (took) goto released; }
            if (s_set || s_upd || s_back) { s_back = false; goto released; }   /* the settings / updates page had the glass (their touch calls above) */
            if (s_shop) {                                           /* the shop: a row's modal, UNLOCK, HOW TO EARN, CLOSE */
                int r = render_shop_tap(t, s_px, s_py);
                ESP_LOGI(TAG, "shop tap at %.0f,%.0f -> %s", s_px, s_py, r == SHOP_TAP_CLOSE ? "CLOSE" : r >= SHOP_TAP_SELL ? "SELL" : r >= SHOP_TAP_MOVE ? "MOVE" : r >= SHOP_TAP_BUY ? "UNLOCK" : r == SHOP_TAP_KEPT ? "modal" : "nothing");
                if (r == SHOP_TAP_CLOSE) { s_shop = false; render_shop_leave(); s_ms = true; }   /* back to the milestones page (2026-09-16) */
                else if (r >= SHOP_TAP_BUY) s_shop_act = r;   /* main.c buys (and plays the cue) or opens the placement page */
                goto released;
            }
            if (s_ms) {                                             /* the page: badges open a modal, the CLOSE
                                                                       button ends it, the brightness row cycles */
                int r = render_milestones_tap(t, s_px, s_py);     /* CLOSE / SETTINGS / the sand dollar, detail modal, nothing */
                ESP_LOGI(TAG, "page tap at %.0f,%.0f (release %.0f,%.0f) -> %s", s_px, s_py, s_lx, s_ly,
                         r >= MS_TAP_SELL ? "SELL (a fish's card)" : r >= MS_TAP_RENAME ? "RENAME (a fish's card)" :
                         r == MS_TAP_CLOSE ? "CLOSE" : r == MS_TAP_SETTINGS ? "SETTINGS" : r == MS_TAP_SHOP ? "SHOP" : r == MS_TAP_KEPT ? "detail" : "nothing");
                if (r >= MS_TAP_SELL) {                             /* a fish's card: SELL, confirmed - the page stays, a row shorter */
                    int fi = r - MS_TAP_SELL, worth = progression_fish_value(t, fi);
                    char nm[FISH_NAME_MAX + 1]; snprintf(nm, sizeof nm, "%s", fi < t->n_fish ? t->fish[fi].name : "?");
                    if (progression_sell_fish(t, fi)) { s_sel = -1; ESP_LOGI(TAG, "%s sold for %d: %d fish left, balance %d", nm, worth, t->n_fish, (int)t->sd_balance); }
                    goto released;
                }
                if (r >= MS_TAP_RENAME) {                           /* ... RENAME: the letter wheel over the live tank */
                    s_ms = false; s_sel = -1; progression_ack_milestones(t); render_milestones_leave();
                    setup_begin_rename(t, r - MS_TAP_RENAME);
                    goto released;
                }
                if (r != MS_TAP_CLOSE && r != MS_TAP_SETTINGS && r != MS_TAP_SHOP) goto released;   /* only a button leaves the page */
                s_ms = false; s_sel = -1; s_set = r == MS_TAP_SETTINGS; s_shop = r == MS_TAP_SHOP;
                progression_ack_milestones(t); render_milestones_leave();   /* everything shown is now "seen" */
                goto released;
            }
            if (t->tool != TOOL_HAND) {                      /* a tool in hand owns the glass (2026-10-04): only its chip's DONE answers a tap */
                s_sel = -1;
                if (render_tool_chip_hit(t, s_px, s_py)) { tank_set_tool(t, TOOL_HAND); ESP_LOGI(TAG, "tool chip tapped at %.0f,%.0f: back to bare hands", s_px, s_py); }
                else ESP_LOGI(TAG, "tap at %.0f,%.0f ignored: a tool is in hand (DONE first)", s_px, s_py);
                goto released;
            }
            if (s_pill && RENDER_BAT_HIT(s_px, s_py)) {      /* the battery pill, while it shows: its page (2026-09-24) */
                s_bat = true; s_bat_us = now; s_sel = -1;
                ESP_LOGI(TAG, "battery pill tapped at %.0f,%.0f: battery page up", s_px, s_py);
                goto released;
            }
            if (s_sel >= 0 && s_sel < t->n_fish && render_tools_hit(s_px, s_py) >= 0) {   /* the toolbox under the card (2026-10-01):
                                                                                     pick a tool up, or put the one in hand back */
                int tl = render_tools_hit(s_px, s_py);
                tank_set_tool(t, t->tool == tl ? TOOL_HAND : tl); s_sel = -1;
                ESP_LOGI(TAG, "toolbox tap at %.0f,%.0f -> %s", s_px, s_py, t->tool == TOOL_SPONGE ? "SPONGE" : t->tool == TOOL_SCISSORS ? "SCISSORS" : "bare hands");
                goto released;
            }
            if ((s_sel < 0 || s_sel >= t->n_fish) && render_tool_chip_hit(t, s_px, s_py)) {   /* the tool chip's DONE */
                tank_set_tool(t, TOOL_HAND);
                ESP_LOGI(TAG, "tool chip tapped at %.0f,%.0f: back to bare hands", s_px, s_py);
                goto released;
            }
            if (s_sel >= 0 && s_sel != RENDER_CARD_SNAIL && s_sel != RENDER_CARD_SHRIMP && s_sel != RENDER_CARD_URCHIN && RENDER_CARD_HIT(s_px, s_py)) {   /* a tap ON the card (or the slop
                ESP_LOGI(TAG, "card tap at %.0f,%.0f -> milestones", s_px, s_py);         under its MORE button) = milestones page */
                s_ms = true; goto released;
            }
            /* fish first; only an empty tap reaches the water. Each creature's
               own reach (tank_fish_hit_r: its species' hit_r at its size, 38 px
               for an adult classic fish, never under TANK_HIT_MIN_R - a
               fingertip on this 322 ppi panel covers ~60 px; a hammerhead's is
               ~90) against BOTH the press-time snapshot and the current
               position - whichever is closer - so a fish that moved mid-tap
               still registers. The nearest by its own reach wins. */
            int best = -1; float bd = 1.0f;
            for (int i = 0; i < t->n_fish; i++) {
                float ax = s_fx[i] - s_px, ay = s_fy[i] - s_py;
                float bx = t->fish[i].x - s_px, by = t->fish[i].y - s_py;
                float d2a = ax * ax + ay * ay, d2b = bx * bx + by * by;
                float r = tank_fish_hit_r(&t->fish[i]);
                float d2 = (d2a < d2b ? d2a : d2b) / (r * r);
                if (d2 < bd) { bd = d2; best = i; }
            }
            if (best >= 0) {
                /* two quick taps on the same creature (2026-10-10): its trick - the puffer puffs, the squid
                   and the octopus ink - and the card stays down; any other creature: the card up, then down */
                static int s_poke_fish = -1; static int64_t s_poke_us;
                bool again = best == s_poke_fish && now - s_poke_us < 500000;
                s_poke_fish = best; s_poke_us = now;
                if (again && tank_poke(t, best)) { s_sel = -1; s_poke_fish = -1; ESP_LOGI(TAG, "%s double-tapped: its trick", t->fish[best].name); }
                else { s_sel = (best == s_sel) ? -1 : best; s_sel_us = now; }
            }
            else if (tank_snail_hit(t, s_px, s_py)) {   /* the snail: its card (2026-09-16), the fish first */
                s_sel = s_sel == RENDER_CARD_SNAIL ? -1 : RENDER_CARD_SNAIL; s_sel_us = now;
                ESP_LOGI(TAG, "snail tapped: card %s (%d spots grazed)", s_sel >= 0 ? "up" : "down", (int)t->snail_grazed); }
            else if (tank_urchin_hit(t, s_px, s_py)) {  /* the urchin: its card (2026-10-02) */
                s_sel = s_sel == RENDER_CARD_URCHIN ? -1 : RENDER_CARD_URCHIN; s_sel_us = now;
                ESP_LOGI(TAG, "urchin tapped: card %s (%.0f cm of grass grazed)", s_sel >= 0 ? "up" : "down", t->urchin_grazed_px / PX_PER_CM); }
            else if (tank_shrimp_hit(t, s_px, s_py)) {  /* the shrimp school (2026-09-29): a tap its card, the third quick tap scares them */
                int taps = tank_shrimp_tap(t, s_px, s_py);
                if (taps == 1) { s_sel = s_sel == RENDER_CARD_SHRIMP ? -1 : RENDER_CARD_SHRIMP; s_sel_us = now; }
                else if (taps >= 3) s_sel = -1;
                ESP_LOGI(TAG, "shrimp tapped (%d): card %s | %d shrimp, %d eaten, %d/%d to the next", taps, s_sel == RENDER_CARD_SHRIMP ? "up" : "down",
                         t->shrimp_n, (int)t->shrimp_eaten, t->shrimp_food, SHRIMP_PER_JOIN); }
            else if (s_sel >= 0) s_sel = -1;   /* card up: a tap on empty glass just
                                                  dismisses it - it is NOT a tank tap
                                                  (no feed, no light-toggle burst) */
            else tank_touch_tap(t, s_px, s_py);
        }
        else if (s_ms && !s_back && fabsf(dx) >= 40 && fabsf(dx) > 2 * fabsf(dy)) {   /* sideways along the TANK row: its page (2026-09-30) */
            if (render_milestones_swipe(t, s_px, s_py, dx)) ESP_LOGI(TAG, "page swipe %.0f px (the tank row or the fish rows)", dx);
        }
        else if (!s_ms && t->tool == TOOL_HAND && s_py - tank_glass_top(s_px) < 60 && dy >= 40) tank_feed(t, s_lx, 3);  /* drag down from the top = feed (not with a tool in hand) */
    }
released:
    if (!touched) s_held_page = false;
    s_down = touched;
    if (s_sel >= t->n_fish && s_sel != RENDER_CARD_SNAIL && s_sel != RENDER_CARD_SHRIMP && s_sel != RENDER_CARD_URCHIN) s_sel = -1;   /* fresh tank / save load */
    if (s_sel >= 0 && now - s_sel_us > 10 * 1000000) s_sel = -1; /* auto-dismiss */
    if (s_bat && now - s_bat_us > BATTERY_PAGE_US) s_bat = false; /* the battery page too, after a while */
}

int touch_port_selected(void) { return s_sel; }
bool touch_port_milestones(void) { return s_ms; }
void touch_port_show_milestones(bool on) { if (s_ms && !on) render_milestones_leave(); s_ms = on; }
void touch_port_dismiss(void) { s_sel = -1; if (s_ms) render_milestones_leave(); if (s_shop) render_shop_leave(); s_ms = false; s_set = false; s_shop = false; s_bat = false; s_upd = false; }
bool touch_port_updates(void) { return s_upd; }
void touch_port_show_updates(bool on) { s_upd = on; if (on) { s_ms = false; s_set = false; s_shop = false; s_sel = -1; s_bat = false; } }
int  touch_port_take_update(void) { int r = s_upd_act; s_upd_act = 0; return r; }

/* ---- reset confirm prompt ---- */
void touch_port_confirm_open(void) {
    s_cf = true; s_cf_us = esp_timer_get_time(); s_cf_ans = 0;
    s_sel = -1; s_ms = false; s_set = false; s_shop = false; s_bat = false; s_upd = false; render_shop_leave();   /* it replaces the card / the pages */
    ESP_LOGI(TAG, "reset prompt up (YES / NO on the glass; NO by itself in %d s)", (int)(CONFIRM_TIMEOUT_US / 1000000));
}
bool touch_port_confirm_answer(int ans) {
    if (!s_cf) return false;
    s_cf = false; s_cf_ans = ans > 0 ? 1 : -1;
    return true;
}
bool  touch_port_confirm_up(void)   { return s_cf; }
bool  touch_port_down(void)         { return s_down; }
float touch_port_confirm_frac(void) {
    if (!s_cf) return 0;
    float f = 1.0f - (esp_timer_get_time() - s_cf_us) / (float)CONFIRM_TIMEOUT_US;
    return f < 0 ? 0 : f;
}
int  touch_port_confirm_take(void)  { int a = s_cf_ans; s_cf_ans = 0; return a; }
bool touch_port_pressed_since(int64_t us) { return s_down && s_press_us > us; }
bool touch_port_settings(void) { return s_set; }
void touch_port_show_settings(bool on) { s_set = on; if (on) { s_ms = false; s_sel = -1; s_bat = false; } }
int  touch_port_take_setting(int *value) { int w = s_set_what; *value = s_set_val; s_set_what = 0; return w; }
bool touch_port_shop(void) { return s_shop; }
void touch_port_show_shop(bool on) { if (s_shop && !on) render_shop_leave(); s_shop = on; if (on) { s_ms = false; s_set = false; s_sel = -1; s_bat = false; } }
int  touch_port_take_shop(void) { int r = s_shop_act; s_shop_act = 0; return r; }
void touch_port_set_pill(bool up) { s_pill = up; }
bool touch_port_battery(void) { return s_bat; }
void touch_port_show_battery(bool on) { s_bat = on; if (on) { s_bat_us = esp_timer_get_time(); s_sel = -1; } }
