/* touch_port_ft3168.c — FT3168 capacitive touch (FT5x06 register family) ->
 * tank_touch_hold / tank_touch_tap, with the same gesture timing as the sim's
 * mouse: press+release < 350 ms with < 24 px displacement = tap (fingertips
 * roll and this panel is 322 ppi) - on a BUTTON any still press is its tap,
 * however long (0.4.1, touch_gate.h A FIRM PRESS); held > 300 ms on the
 * water = hold; a drag down from
 * the top edge = feed at that x; every touched frame streams to
 * tank_touch_drag (a moving stroke wipes algae; a horizontal slash through
 * a canopy trims it). Fish taps hit-test 38 px against the press-time fish
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
 * reset on a GPIO. Its own build is a portrait tank: panel px = tank px.
 * The P4 HOME TANK (10.1, 2026-10-12) has a GT911-class chip (I2C 0x5D or
 * 0x14, 16-bit registers), read here directly; its panel px go through the
 * display port's turn and 2x (display_port_panel_to_tank), then the same
 * gestures.
 * The SQUARE (2.16, 2026-10-12) has a CST9220 - the round board's CST9217
 * family, read the same way, on its own reset pin and a 480 px panel; its own
 * build is the panel too. Which way its report runs is `touch mirror`.
 * (this fork) The 320x240 boards: the CYD's FT6336 and the Touch-LCD-2's
 * CST816D (adampog, 2026-10-08) through esp_lcd_touch like the 1.8's, each
 * already landscape from the driver; map_touch turns the CYD's 180 degrees. */
#include "touch_port.h"
#include "link_dev.h"
#include "display_port.h"
#include "update.h"
#include "board_pins.h"
#include "tank.h"
#include "collection.h"
#include "gift.h"
#include "tank_link.h"
#include "render.h"
#include "setup.h"
#include "notice.h"
#include "advanced.h"
#include "audio_port.h"
#include "progression.h"
#include "tank_events.h"
#include "touch_gate.h"
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
#include "xt_notice.h"
/* the big screens' own touch chips (the 10.1's GT911, the Tab5's ST712x) and their maps: on the boards only - the
   simulator's --selftest-touch-port runs this file against its stand-in chip in every world (TOUCH_PORT_HOST) */
#if defined(TANK_P4) && !defined(TOUCH_PORT_HOST)
#define TOUCH_P4 1
#endif
#if defined(TANK_TAB5_HW) && !defined(TOUCH_PORT_HOST)
#define TOUCH_TAB5 1
#endif

static const char *TAG = "touch";
static esp_lcd_touch_handle_t s_tp;
static bool s_down; static int64_t s_press_us; static float s_px, s_py;
static open_wait_t s_pg_wait;    /* A PAGE A TAP BROUGHT (0.4.1 audit round 1, TCH-4): the keeper page, the shop, the collection - a
                                    tap that changes between them stamps it, and a tap pressed within OPEN_WAIT_US is that tap's
                                    double (spent; THE TOUCH RULE, never restarted): a doubled shop X or collection CLOSE shut the keeper
                                    page too, a doubled VISITORS opened and shut the collection, a doubled UPGRADES opened HOW TO EARN.
                                    Settings' CLOSE and the cog, UPDATES, LINK have their own (s_back, the page gate): parity */
static bool s_card_was; static open_wait_t s_card_gone;   /* THE GIFT CARD GOING (0.4.1 audit round 2, R2T-1): it had the glass on the last
                                    poll, and when it went. A card going is a page coming back: a tap pressed before it went, or
                                    within OPEN_WAIT_US after, is no tap on what was under it (the answer doubled sold the fish whose
                                    SELL was armed, bought the shop piece, DEPORTED, tapped CHECK FOR UPDATES, flipped a setting) */
static bool s_nvis_was; static open_wait_t s_notice_back;   /* an announcement on show last poll; when it came back from under a page */
static double s_press_clock;   /* the tank clock at the press: the render taps' waits read it (render_tap_press, touch_gate.h READ AT THE PRESS) */
static float s_lx, s_ly;                          /* LAST touched position (release classification) */
static float s_fx[N_FISH_MAX], s_fy[N_FISH_MAX];  /* fish positions at press time */
static int s_sel = -1; static int64_t s_sel_us;   /* tapped fish -> stats card */
static bool s_quiet = true, s_quiet_press;   /* LOADING (the boot show, 0.4 sweep): the glass takes no tap; a finger down then is not a tap at its lift */
static open_wait_t s_crit_back;   /* a critter's card put back as its name wheel closed (0.4.2): the double of DONE's / CANCEL's tap is spent */
static int s_card_for = -1; static int64_t s_card_us;   /* which card is up, and since when (0.4: a visitor's DEPORT spends its opening tap's double since 11-06, not GIFT_HOLD_S) */
static bool s_ms;                                 /* milestones page up (its CLOSE button ends it) */
static bool s_cf; static int64_t s_cf_us; static int s_cf_ans; static open_wait_t s_cf_wait;   /* reset confirm prompt (its open wait: 0.4.1) */
static bool s_set;                                /* settings page up (CLOSE returns to the milestones page) */
static bool s_shop;                               /* the shop page up (CLOSE returns to the milestones page) */
static bool s_coll;                               /* the VISITOR COLLECTION page up (CLOSE returns to the milestones page) */
static spent_lift_t s_back;                       /* the settings page's CLOSE just brought the milestones page back: that
                                                     release must not reach the page as a tap on ITS CLOSE (same spot) - the
                                                     next lift takes it, a slow one too (0.4.1, touch_gate.h) */
static page_gate_t s_page_gate;                   /* settings / UPDATES / link mode opened by a tap: the press after it is
                                                     hidden from them (0.4.1, the watch's UPDATES CLOSE over SCREEN) */
static bool s_press_btn;                          /* this press landed on a button on the bare tank: no hold, a tap at any length */
static bool s_press_spent;                        /* main.c took this press as a hold (MY TANKS's MOVE, REMOVE): its lift is no tap */
static int  s_shop_act;                           /* an UNLOCK / MOVE / SELL tapped: the raw tap code, for main (one-shot) */
static bool s_held_page;                          /* this press opened a piece's page by holding on it (2026-09-24) */
static bool s_page_gone;                          /* (0.4.3 audit r3, R3-L2) a page closed under this press: the rest of it strokes nothing */
static int  s_adv_act;                            /* the ADVANCED page's CELL / FIT, for main (one-shot) */
static int  s_set_what, s_set_val;                /* a segment tapped: SET_TAP_* + value, for main */
static bool s_pill;                               /* main.c drew the battery pill this frame: a tap on it is its page's */
static bool s_bat; static int64_t s_bat_us;       /* the battery page up, and since when (it closes itself) */
static bool s_swallow;                            /* the tank just lit from the dark: no gesture until the glass is clear */
#define BATTERY_PAGE_US (30LL * 1000000)
#define CONFIRM_TIMEOUT_US (20LL * 1000000)
static int  s_q;                                  /* the picture's quarter turns (tank_orient): 2 = flipped, 1 / 3 = on a side */
/* Fingers land a little BELOW where the eye aims - the pad rolls onto the
 * glass under the fingertip (phones shift their hit targets down for the
 * same reason; Strato saw it on the swatch rows, 2026-09-13). Reported
 * points move UP by this many px in displayed space; director `touch bias
 * <px>` tunes it live. */
#if CONFIG_POCKET_TANK_320X240
/* None on the CYD (2026-09-26): with the AMOLED's 10 px every missed button
 * in the first setup walk-through read ABOVE the button, never below it.
 * The Touch-LCD-2 is the same size of glass and starts from the same 0. */
static int s_bias_y = 0;
#else
static int s_bias_y = 10;
#endif
void touch_port_set_bias(int px) { s_bias_y = px; }
int  touch_port_bias(void) { return s_bias_y; }

void touch_port_set_inverted(bool inverted) { s_q = inverted ? 2 : 0; }
void touch_port_set_quarter(int q) { s_q = TANK_QUARTER(q); }
extern i2c_master_bus_handle_t board_i2c_bus(void);
extern bool board_is_v2(void);

/* ---- the round board's CST9217 (I2C 0x5A, reset on a GPIO) ----
 * One report: the read command D0 00, ten bytes back, then the read's ack
 * (D0 00 AB) as SensorLib sends it. Byte 6 = AB marks a real report, byte 5
 * the finger count, and the first finger is 06 in its low nibble while down:
 * x = b1:b3.hi, y = b2:b3.lo, 12 bits each, in panel px - mirrored on both
 * axes against the picture (Waveshare's BSP sets mirror_x and mirror_y). */
static i2c_master_dev_handle_t s_cst;
static gpio_num_t cst_rst(void) { return board_is_square() ? S_PIN_TP_RST : board_is_175() ? B_PIN_TP_RST : R_PIN_TP_RST; }
static int cst_panel(void) { return board_is_square() ? S_PANEL : R_PANEL; }
/* how the chip's report runs against the picture: bit 0 = x mirrored, bit 1 = y
 * mirroring, bit 2 = the axes swapped. The round board's is 3 (both mirrored,
 * Waveshare's BSP sets mirror_x and mirror_y); the square's is 5 (the axes
 * swapped, then x mirrored - fitted as 6 from Strato's taps on the first-run
 * NEXT with the picture at MADCTL 0x60, then turned with the picture to
 * 0xA0, USB-down, 2026-10-12). Director `touch mirror <n>` tries another live. */
#define CST_MIRROR_ROUND  3
#define CST_MIRROR_SQUARE 5
static int s_cst_mirror = CST_MIRROR_ROUND;
void touch_port_set_mirror(int m) { if (m >= 0) s_cst_mirror = m & 7; }
int  touch_port_mirror(void) { return s_cst_mirror; }
static bool cst9217_init(void) {
    s_cst_mirror = board_is_square() ? CST_MIRROR_SQUARE : CST_MIRROR_ROUND;
    gpio_config_t rst = { .pin_bit_mask = 1ULL << cst_rst(), .mode = GPIO_MODE_OUTPUT };
    gpio_config(&rst); gpio_sleep_sel_dis(cst_rst());
    gpio_set_level(cst_rst(), 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(cst_rst(), 1); vTaskDelay(pdMS_TO_TICKS(50));
    if (i2c_master_probe(board_i2c_bus(), I2C_ADDR_CST9217, 50) != ESP_OK) { ESP_LOGW(TAG, "no CST9217"); return false; }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = I2C_ADDR_CST9217, .scl_speed_hz = 400000,
                                .flags.disable_ack_check = 1 };   /* it NACKs a read now and then while it scans: not an error, and not a log line each */
    if (i2c_master_bus_add_device(board_i2c_bus(), &cfg, &s_cst) != ESP_OK) { s_cst = NULL; return false; }
    ESP_LOGI(TAG, "CST92%s ready", board_is_square() ? "20 (the square)" : "17");
    return true;
}
/* the night (2026-10-02): the chip's own sleep instead of its reset held low
 * all night - SensorLib's sequence: D1 1E twice opens command mode (0002
 * echoes 1E), D1 01 the debug mode (echoes 01), then D1 05 sleeps it. The
 * grace left it in reset, so it is let out and given its boot first. Only a
 * reset brings it back: the wake is a reboot, and cst9217_init pulses it. */
bool touch_port_deep_sleep(void) {
    if (!s_cst) return false;
    gpio_set_level(cst_rst(), 1); vTaskDelay(pdMS_TO_TICKS(150));
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
    if (!ok) gpio_set_level(cst_rst(), 0);
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
        int rx = (d[1] << 4) | (d[3] >> 4), ry = (d[2] << 4) | (d[3] & 0x0F), pn = cst_panel();
        if (s_cst_mirror & 4) { int tmp = rx; rx = ry; ry = tmp; }
        if (rx > pn - 1) rx = pn - 1;
        if (ry > pn - 1) ry = pn - 1;
        lx = (uint16_t)((s_cst_mirror & 1) ? pn - 1 - rx : rx); ly = (uint16_t)((s_cst_mirror & 2) ? pn - 1 - ry : ry);
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
 * YH YL, 12 bits each, in panel px, unmirrored.
 * THE REST (11-02, a night's soak + a DIAG build naming the chip): now and
 * then, idle for a while, a read stretches past its 20 ms and times out, and
 * the next 1-3 fail at once while the bus recovers - 16-24 ms each, a burst
 * holding one frame 40-90 ms every few seconds. After a failed read the chip
 * is left alone for FT_REST_US: silence, which a press bridges - the failed
 * read's 20 ms + the rest are more than CST_LIFT_US, so the first rest of a
 * silence does not count toward the lift (touch_gate.h, the evening sweep). */
#define FT_REST_US 50000
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
    static uint16_t lx, ly; static press_rest_t pr;
    uint8_t reg = 0x02, d[5] = { 0 };
    int64_t now = esp_timer_get_time();
    bool answered = false;
    if (press_rest_may_read(&pr, now)) {
        if (i2c_master_transmit_receive(s_ft, &reg, 1, d, sizeof d, 20) == ESP_OK) answered = (d[0] & 0x0F) <= 2 && d[0] != 0xFF;
        else press_rest_failed(&pr, esp_timer_get_time(), FT_REST_US);   /* THE REST: no second try inside the burst (a press bridges it) */
    }
    bool finger = answered && (d[0] & 0x0F) >= 1 && (d[1] >> 6) != 1, was = pr.down;
    if (finger) {
        int rx = ((d[1] & 0x0F) << 8) | d[2], ry = ((d[3] & 0x0F) << 8) | d[4];
        if (rx > W_PANEL_W - 1) rx = W_PANEL_W - 1;
        if (ry > W_PANEL_H - 1) ry = W_PANEL_H - 1;
        lx = (uint16_t)rx; ly = (uint16_t)ry;
        if (!was) s_cst_gaps = 0;
        s_seen_us = now;
    }
    if (press_rest_step(&pr, now, answered, finger, CST_LIFT_US) && was && !finger) s_cst_gaps++;   /* "no finger", said plainly, is a lift; silence is bridged */
    *x = lx; *y = ly;
    return pr.down;
}
#ifdef TOUCH_P4
/* ---- the home tank's GT911 (2026-10-12): status at 0x814E (bit 7 = a fresh
 * report, the low nibble = fingers), the first point at 0x8150 (x, y, 16-bit
 * little-endian, in panel px), the status written back to 0 to ask for the
 * next. A report with no finger is the lift; no report for GT_LIFT_US while
 * down (the chip stalled) is one too. ---- */
static i2c_master_dev_handle_t s_gt;
#define GT_LIFT_US 120000
static bool gt911_init(void) {
    uint8_t addr = 0;
    for (int t = 0; t < 12 && !addr; t++) {          /* it boots on the panel's reset line: give it a moment */
        if (i2c_master_probe(board_i2c_bus(), I2C_ADDR_GT911, 50) == ESP_OK) addr = I2C_ADDR_GT911;
        else if (i2c_master_probe(board_i2c_bus(), I2C_ADDR_GT911_ALT, 50) == ESP_OK) addr = I2C_ADDR_GT911_ALT;
        else vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!addr) { ESP_LOGW(TAG, "no GT911 at 0x%02x / 0x%02x", I2C_ADDR_GT911, I2C_ADDR_GT911_ALT); return false; }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(board_i2c_bus(), &cfg, &s_gt) != ESP_OK) { s_gt = NULL; return false; }
    uint8_t reg[2] = { 0x81, 0x40 }, id[4] = { 0 };
    i2c_master_transmit_receive(s_gt, reg, 2, id, 4, 50);
    uint8_t rr[2] = { 0x80, 0x48 }, res[4] = { 0 };   /* the chip's own configured resolution */
    i2c_master_transmit_receive(s_gt, rr, 2, res, 4, 50);
    ESP_LOGI(TAG, "GT911 ready at 0x%02x: id %.4s, %d x %d (the home tank: read directly)", addr, (const char *)id, res[0] | res[1] << 8, res[2] | res[3] << 8);
    return true;
}
/* director `touch gt`: ten raw reads a second apart - the status byte and the first point (bench) */
void touch_port_gt911_dump(void) {
    if (!s_gt) { ESP_LOGW(TAG, "no GT911 handle"); return; }
    for (int i = 0; i < 10; i++) {
        uint8_t reg[2] = { 0x81, 0x4E }, st = 0xEE, pr[2] = { 0x81, 0x50 }, d[8] = { 0 };
        esp_err_t e1 = i2c_master_transmit_receive(s_gt, reg, 2, &st, 1, 50);
        esp_err_t e2 = i2c_master_transmit_receive(s_gt, pr, 2, d, 8, 50);
        uint8_t cfg[2] = { 0x80, 0x47 }, c[1] = { 0 }; i2c_master_transmit_receive(s_gt, cfg, 2, c, 1, 50);   /* config version */
        uint8_t md[2] = { 0x80, 0x4D }, m[1] = { 0 }; i2c_master_transmit_receive(s_gt, md, 2, m, 1, 50);     /* module switch 1: INT trigger, X2Y, ... */
        ESP_LOGI(TAG, "gt911: status %02x (%s/%s) point %02x %02x %02x %02x %02x %02x %02x %02x | cfg ver %02x switch1 %02x", st,
                 esp_err_to_name(e1), esp_err_to_name(e2), d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], c[0], m[0]);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
static bool gt911_read(uint16_t *x, uint16_t *y) {
    static uint16_t lx, ly; static int64_t seen_us; static bool down;
    int64_t now = esp_timer_get_time();
    uint8_t reg[2] = { 0x81, 0x4E }, st = 0;
    if (i2c_master_transmit_receive(s_gt, reg, 2, &st, 1, 20) == ESP_OK && (st & 0x80)) {
        int n = st & 0x0F;
        if (n >= 1 && n <= 10) {
            uint8_t pr[2] = { 0x81, 0x50 }, d[6] = { 0 };
            if (i2c_master_transmit_receive(s_gt, pr, 2, d, 6, 20) == ESP_OK) {
                int rx = d[0] | d[1] << 8, ry = d[2] | d[3] << 8;
                if (rx > P4_PANEL_W - 1) rx = P4_PANEL_W - 1;
                if (ry > P4_PANEL_H - 1) ry = P4_PANEL_H - 1;
                lx = (uint16_t)rx; ly = (uint16_t)ry;
                if (!down) s_cst_gaps = 0;
                seen_us = s_seen_us = now; down = true;
            }
        } else down = false;                          /* a report with no finger: the lift, said plainly */
        uint8_t clr[3] = { 0x81, 0x4E, 0 };
        i2c_master_transmit(s_gt, clr, 3, 20);
    } else if (down && now - seen_us > GT_LIFT_US) down = false;
    *x = lx; *y = ly;
    return down;
}
#ifdef TOUCH_TAB5
/* ---- the Tab5's ST7121 / ST7123 (2026-10-19): the touch half of the panel's own chip at 0x55,
 * 16-bit register addresses (Espressif's esp_lcd_touch_st7123): 0x0010 = "advanced info" (bit 3:
 * a coordinate report is there), 0x0009 = how many report slots, 0x0014 = the reports, 7 bytes
 * each: valid (bit 7) + x high (6 bits), x low, y high, y low, area, intensity, -. Panel px,
 * 720 across x 1280 down. A report with no valid point is the lift; no report for ST_LIFT_US
 * while down is one too (as on the GT911). The first Tab5s' GT911 (0x14) is read by the code above. ---- */
static i2c_master_dev_handle_t s_st;
#define ST_LIFT_US 120000
static bool st_rd(uint16_t reg, uint8_t *d, int n) {
    uint8_t r[2] = { (uint8_t)(reg >> 8), (uint8_t)reg };
    return i2c_master_transmit_receive(s_st, r, 2, d, n, 20) == ESP_OK;
}
static bool st712x_init(void) {
    for (int t = 0; t < 12; t++) {
        if (i2c_master_probe(board_i2c_bus(), I2C_ADDR_ST712X, 50) == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = I2C_ADDR_ST712X, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(board_i2c_bus(), &cfg, &s_st) != ESP_OK) { s_st = NULL; return false; }
    uint8_t ver = 0, info[5] = { 0 };
    if (!st_rd(0x0000, &ver, 1) || !st_rd(0x0005, info, 5)) { ESP_LOGW(TAG, "ST712x touch at 0x%02x does not answer", I2C_ADDR_ST712X); i2c_master_bus_rm_device(s_st); s_st = NULL; return false; }
    ESP_LOGI(TAG, "ST712x touch ready (fw %u = %s): %d x %d, %d points (the Tab5: read directly)", ver, ver == 1 ? "ST7121" : ver == 3 ? "ST7123" : "?",
             info[0] << 8 | info[1], info[2] << 8 | info[3], info[4]);
    return true;
}
static bool st712x_read(uint16_t *x, uint16_t *y) {
    static uint16_t lx, ly; static int64_t seen_us; static bool down;
    int64_t now = esp_timer_get_time();
    uint8_t adv = 0;
    if (st_rd(0x0010, &adv, 1) && (adv & 0x08)) {
        uint8_t n = 0, d[7 * 10];
        if (st_rd(0x0009, &n, 1) && n >= 1) {
            if (n > 10) n = 10;
            bool got = false;
            if (st_rd(0x0014, d, 7 * n))
                for (int i = 0; i < n && !got; i++) {
                    const uint8_t *r = d + 7 * i;
                    if (!(r[0] & 0x80)) continue;
                    int rx = (r[0] & 0x3F) << 8 | r[1], ry = r[2] << 8 | r[3];
                    if (rx > P4_PANEL_W - 1) rx = P4_PANEL_W - 1;
                    if (ry > P4_PANEL_H - 1) ry = P4_PANEL_H - 1;
                    lx = (uint16_t)rx; ly = (uint16_t)ry; got = true;
                }
            if (got) { if (!down) s_cst_gaps = 0; seen_us = s_seen_us = now; down = true; }
            else down = false;                    /* a report with no finger: the lift, said plainly */
        }
    } else if (down && now - seen_us > ST_LIFT_US) down = false;
    *x = lx; *y = ly;
    return down;
}
#endif
#endif
/* one read of the glass, whichever chip: true while a finger is down, in panel px */
static bool panel_read(uint16_t *x, uint16_t *y) {
#ifdef TOUCH_P4
    if (s_gt) return gt911_read(x, y);
#endif
#ifdef TOUCH_TAB5
    if (s_st) return st712x_read(x, y);
#endif
    if (s_cst) return cst9217_read(x, y);
    if (s_ft) return ft3168_read(x, y);
    if (!s_tp) return false;
    uint16_t st[1]; uint8_t n = 0;
    esp_lcd_touch_read_data(s_tp);
    if (!esp_lcd_touch_get_coordinates(s_tp, x, y, st, &n, 1) || n == 0) return false;
    s_seen_us = esp_timer_get_time();
    return true;
}

#if CONFIG_POCKET_TANK_CYD_320X240
/* The CYD's FT6336G (the same FT5x06 register family) on the shared bus, with
 * its reset on GPIO18. The driver's swap_xy turns its portrait report
 * landscape; what is left for touch_port_poll is a 180-degree turn, found on
 * the bench (2026-09-26): a drag to the right moved the bubble column left,
 * and the welcome page's NEXT, at the foot, answered a tap at the top. The
 * display was turned 180 degrees the same day, and touch has to follow it. */
#define TOUCH_SWAP_XY  1
#define TOUCH_MIRROR_X 0
#define TOUCH_MIRROR_Y 1
#define TOUCH_TURN_180 1

static bool ft6336_init(void) {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_cfg.dev_addr = I2C_ADDR_FT6336; io_cfg.scl_speed_hz = 400000;
    if (esp_lcd_new_panel_io_i2c(board_i2c_bus(), &io_cfg, &io) != ESP_OK) { ESP_LOGW(TAG, "no touch io"); return false; }
    esp_lcd_touch_config_t tp_cfg = { .x_max = PANEL_W, .y_max = PANEL_H, .rst_gpio_num = PIN_TP_RST, .int_gpio_num = -1,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = TOUCH_SWAP_XY, .mirror_x = TOUCH_MIRROR_X, .mirror_y = TOUCH_MIRROR_Y } };
    if (esp_lcd_touch_new_i2c_ft5x06(io, &tp_cfg, &s_tp) != ESP_OK) { ESP_LOGW(TAG, "no FT6336"); return false; }
    ESP_LOGI(TAG, "FT6336 ready");
    return true;
}
#elif CONFIG_POCKET_TANK_WST_320X240
/* The Touch-LCD-2's CST816D (the CST816S driver's register set, as on the
 * AMOLED's V2 board) on the shared bus, reporting in the panel's portrait
 * frame. The driver mirrors first, then swaps: x = raw y, y = 239 - raw x,
 * which is Waveshare's factory app's own mapping for the landscape it scans
 * (board_pins.h LCD_*: MX | MV). x_max / y_max are the last pixel, so the
 * mirror lands on 239, not 240. Check against the director's `touch log on`
 * stroke line: a tap near the top-left corner reads near 0,0. */
#define TOUCH_SWAP_XY  1
#define TOUCH_MIRROR_X 1
#define TOUCH_MIRROR_Y 0
#define TOUCH_TURN_180 0

static bool cst816d_init(void) {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    io_cfg.dev_addr = I2C_ADDR_CST816D; io_cfg.scl_speed_hz = 400000;
    if (esp_lcd_new_panel_io_i2c(board_i2c_bus(), &io_cfg, &io) != ESP_OK) { ESP_LOGW(TAG, "no touch io"); return false; }
    esp_lcd_touch_config_t tp_cfg = { .x_max = PANEL_W - 1, .y_max = PANEL_H - 1, .rst_gpio_num = PIN_TP_RST, .int_gpio_num = -1,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = TOUCH_SWAP_XY, .mirror_x = TOUCH_MIRROR_X, .mirror_y = TOUCH_MIRROR_Y } };
    if (esp_lcd_touch_new_i2c_cst816s(io, &tp_cfg, &s_tp) != ESP_OK) { ESP_LOGW(TAG, "no CST816D"); return false; }
    ESP_LOGI(TAG, "CST816D ready");
    return true;
}
#endif
bool touch_port_init(void) {
#if CONFIG_POCKET_TANK_CYD_320X240   /* (this fork) the 320x240 boards' own controllers */
    return ft6336_init();
#elif CONFIG_POCKET_TANK_WST_320X240
    return cst816d_init();
#endif
#ifdef TOUCH_TAB5
    if (i2c_master_probe(board_i2c_bus(), I2C_ADDR_GT911_ALT, 50) != ESP_OK) return st712x_init();   /* no GT911 at 0x14: an ST712x panel */
#endif
#ifdef TOUCH_P4
    return gt911_init();
#endif
    if (board_is_round() || board_is_square()) return cst9217_init();
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
/* The SQUARE (2.16), 2026-10-12: two sittings of 18 presses, the box in a
 * palm with the THUMB (x = 0.998 t + 8.9, y = 1.011 t + 7.4) and in one hand
 * with the other's INDEX finger (x = 1.022 t + 3.2, y = 1.015 t + 7.6) - the
 * two grips within 4 px of each other anywhere on the glass, so ONE map, the
 * mean of the two fits (docs/touch/2026-10-12_square-*.log). The panel does
 * not stretch; the finger lands ~8 px right and, counting the bias, ~20 px
 * low - the same landing the bowl and the 1.8 showed, so CAL_LAND applies
 * here too and the y offset carries the small rest. The TURNED sitting the
 * same hour (18 presses, the box upside down) told the two apart: y on the
 * cross (mean -1.4: the landing turned with the picture, as modelled), but
 * x 14 px RIGHT after an upright map that took 8.5 off - so upright +8.5
 * and turned -8 in the raw frame: the panel has NO x offset and the finger
 * lands ~8 px toward the tapping hand, turning with the picture - the
 * watch's CAL_W_LAND_X, the same 8. */
/* ... and then the base turned to USB-DOWN (MADCTL 0xA0, Strato's normal
 * grip) and the map above, carried over by arithmetic, read 20 px left at
 * the left column: the panel's two ends are not alike, so the final frame
 * got its own two sittings (docs/touch/2026-10-12_square-B-thumb.log,
 * -B-index.log, 18 presses each, over the carried map): the THUMB reaches
 * the far column 14 px further than the INDEX finger (x 1.065 t - 19 vs
 * 1.019 t - 7; y 1.018 t - 13 vs 1.006 t - 5). One map, the mean of the two
 * (x 1.042 t - 13, y 1.012 t - 9), composed onto the carried one: each grip
 * is ~7 px off at the far-left column and inside a fingertip elsewhere. */
#define CAL_S_X_GAIN 1.052f
#define CAL_S_LAND_X 8.0f
#define CAL_S_X_OFF  13.1f
#define CAL_S_Y_GAIN 1.025f
#define CAL_S_Y_OFF  6.8f
/* the HOME TANK's GT9271 (2026-10-13, 18 presses, the board on its stand at 45
 * degrees, the picture upright: docs/touch/2026-10-13_p4-pass1.log; the check
 * sitting on this map, _p4-check.log: y mean off +0.8, x +5.0). x is true - press = 1.005 x - 0.1,
 * 6 px across 1280, inside the scatter: left alone. y, with the 10 px landing
 * bias taken off, read press = 1.013 y - 8.2: 7 px HIGH at the top row, 5 in the
 * middle, 2 low at the bottom (the bias is a little much up there and the panel
 * runs 1.3 % long). Fitted upright only: the offset folds the finger's landing
 * and the panel's own into one number, as the pendant's once did - a sitting
 * with SCREEN TURNED is owed before anyone mounts it that way. */
#define CAL_P4_Y_GAIN 1.013f
#define CAL_P4_Y_OFF  8.2f
#define CAL_T5_X_GAIN 0.9958f
#define CAL_T5_X_OFF  19.1f
#define CAL_T5_Y_GAIN 0.9667f
#define CAL_T5_Y_OFF  20.2f
static void cal_point(float rx, float ry, float *tx, float *ty);
/* an upright raw report -> the finger's point on the picture as shown: the board's own map, then the keeper's (0.3.9.2,
 * the ADVANCED page; none unless one was kept), both upright - the panel's own frame - then turned with the picture.
 * EVERY reading goes through this one path: a tap's (map_touch) and the panel's reach the stroke stretches to the glass
 * (edge_stretch). Until 0.4.2 the reach left the keeper's map out: a calibrated tank's scrub stopped short of the glass
 * where the keeper's map pulled the reading in (Machito on Discord 11-07: "impossible to clean ... along the right edge",
 * fine after RESET TOUCH). */
static void view_point(float rx, float ry, bool keeper, float *tx, float *ty) {
    cal_point(rx, ry, tx, ty);
    if (keeper) touch_fit_apply(adv_touch_fit_at(s_q), tx, ty, 0);   /* (the map for this way up: advanced.h, its sittings) */
    tank_quarter_from_upright(s_q, tx, ty);
}
/* the panel's reach on the picture as shown: its raw extremes through view_point (the bias is live) */
static void view_reach(bool keeper, float *x0, float *x1, float *y0, float *y1) {
    view_point(0, 0, keeper, x0, y0); view_point(TANK_W - 1, TANK_H - 1, keeper, x1, y1);
    if (*x0 > *x1) { float k = *x0; *x0 = *x1; *x1 = k; }   /* turned: the far corner is the near one (on a side the axes trade too) */
    if (*y0 > *y1) { float k = *y0; *y0 = *y1; *y1 = k; }
}
static void cal_point(float rx, float ry, float *tx, float *ty) {                 /* a raw tank-space report -> where the finger is */
    /* the viewer's "down" and "right" on the upright glass: turned over they are its up and left, on a side
       (0.3.9.3) its left / right and down / up (tank_quarter_from_upright) */
    static const int8_t DOWN[4][2] = { { 0, 1 }, { -1, 0 }, { 0, -1 }, { 1, 0 } }, RIGHT[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
    float land = (float)s_bias_y + (board_is_round() || board_is_v2() || board_is_square() ? CAL_LAND : 0);
    float side = board_is_watch() ? CAL_W_LAND_X : board_is_square() ? CAL_S_LAND_X : 0;
    rx -= land * DOWN[s_q][0] + side * RIGHT[s_q][0];   /* the finger's own low landing is the viewer's "down"; on a wrist, and the */
    ry -= land * DOWN[s_q][1] + side * RIGHT[s_q][1];   /* square in a hand, it lands toward the tapping hand too: the viewer's right */
    if (board_is_round()) ry = (ry + CAL_R_Y_OFF) / CAL_R_Y_GAIN;
    else if (board_is_watch()) { rx = (rx + CAL_W_X_OFF) / CAL_W_X_GAIN; ry = (ry + CAL_W_Y_OFF) / CAL_W_Y_GAIN; }
    else if (board_is_v2()) { rx = (rx + CAL_X_OFF) / CAL_X_GAIN; ry = (ry + CAL_Y_OFF) / CAL_Y_GAIN; }
    else if (board_is_square()) { rx = (rx + CAL_S_X_OFF) / CAL_S_X_GAIN; ry = (ry + CAL_S_Y_OFF) / CAL_S_Y_GAIN; }
#ifdef TOUCH_TAB5                                      /* the Tab5's ST7121 (2026-10-19, nine crosses twice, Strato's finger:
                                                         docs/touch/2026-10-19_tab5-pass1.log): press = 0.996 x + 19.1, 0.967 y + 20.2 -
                                                         the finger lands ~19 px right and ~20 px low; rms 10 / 7 px, a finger's scatter */
    rx = (rx - CAL_T5_X_OFF) / CAL_T5_X_GAIN;
    ry = (ry - CAL_T5_Y_OFF) / CAL_T5_Y_GAIN;
#elif defined(TOUCH_P4)
    ry = (ry + CAL_P4_Y_OFF) / CAL_P4_Y_GAIN;
#endif
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
     * 180-degree turn comes after it (view_point). Until 2026-10-02 the turn came
     * first, and a turned picture's taps were off by the fit's asymmetry: ~15 px
     * on the watch, ~20 at the top of the bowl, ~2 on the 1.8. */
#if CONFIG_POCKET_TANK_320X240 && TOUCH_TURN_180 /* the CYD: landscape from the driver (swap_xy), turned 180 degrees (ft6336_init) */
    int rx = TANK_W - 1 - fx, ry = TANK_H - 1 - fy;
#elif CONFIG_POCKET_TANK_320X240                 /* the Touch-LCD-2: landscape from the driver, the right way up (cst816d_init) */
    int rx = fx, ry = fy;
#elif defined(TANK_WATCH) || defined(TANK_SQUARE)     /* the watch's portrait tank, the square's: the panel's px are the tank's */
    int rx = fx, ry = fy;
#elif defined(TOUCH_P4)                                /* the home tank: the panel's px through the display port's turn */
    float p4x, p4y; display_port_panel_to_tank(px, py, &p4x, &p4y);
    int rx = (int)p4x, ry = (int)p4y;
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
    view_point((float)rx, (float)ry, true, tx, ty);   /* the board's map, the keeper's, then turned with the picture */
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
    float x0, y0, x1, y1;                         /* the panel's reach: its raw extremes through the same map a tap takes, the keeper's too */
    view_reach(true, &x0, &x1, &y0, &y1);
    *ox = edge_stretch1(x, x0, x1, TANK_W - 1, EDGE_BAND_X, EDGE_SNAP_X); *oy = edge_stretch1(y, y0, y1, TANK_H - 1, EDGE_BAND_Y, EDGE_SNAP_Y);
}
/* A TAP on MY TANKS / THE LIBRARY at the glass's foot (0.4.2, Strato's 1.8 turned over, 2026-11-11: BACK and the icon
 * foot took no tap, the library icon opened the tank above it). The panel's report stops short of the glass - turned
 * over, the 1.8's ends near y 317 - and those pages keep their buttons in the last 40 px: a finger on one read 308..317,
 * above every button and on the shelf's name plates. So a shelf tap within SHELF_EDGE_SNAP of the panel's last line is
 * a tap at the glass's edge, and the SHELF_EDGE_BAND before it eases in; above that a tap is where it read. (y only,
 * the foot only: the top of those pages holds a title. Narrower than a stroke's band: a tap on a name plate, 20 px
 * above the limit, stays on its plate.) */
#define SHELF_EDGE_BAND 22.0f
#define SHELF_EDGE_SNAP 12.0f
static float shelf_tap_y(float y) {
    float x0, y0, x1, y1; view_reach(true, &x0, &x1, &y0, &y1);
    (void)x0; (void)x1; (void)y0;
    return edge_stretch1(y, 0, y1, TANK_H - 1, SHELF_EDGE_BAND, SHELF_EDGE_SNAP);
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
static bool s_shelf;                                      /* MY TANKS (the tank switcher): BOOT twice opens it */
static void (*s_jelly)(float x, float y, bool down, double now);   /* THE JELLY TANK on the glass (docs/jelly-tank-0.6.md): the bare glass is
                                                                   its (tap, hold, stroke, the jet's fan, the double tap's light); NULL = fish */
void touch_port_set_jelly(void (*cb)(float x, float y, bool down, double now)) { s_jelly = cb; }
static bool s_set_from_shelf, s_shelf_return;      /* the settings page was opened by MY TANKS's cog; its CLOSE asks main.c for MY TANKS back */
static bool s_shelf_tap; static float s_shelf_tx, s_shelf_ty; static int64_t s_shelf_press_us;   /* its last tap (and its press), for main.c */
static open_wait_t s_shelf_wait;   /* MY TANKS's first OPEN_WAIT_US (0.4.1 audit round 1, TCH-5): a doubled MY TANKS on the keeper foot landed on
                                      its own page (CLOSE on the watch, THE LIBRARY on the bowl, a place on the 1.8). A press from before
                                      it opened, or within the window, is no MY TANKS tap (THE TOUCH RULE) */
static bool s_fdown; static float s_fingx, s_fingy;           /* the finger this poll (touch_port_finger) */
static finger_gate_t s_fgate;                                 /* ... hidden under the gift card / the prompt (touch_gate.h) */
static bool s_modal_last;                                     /* a page, prompt or card owned the glass at the last poll */
bool touch_port_finger(float *x, float *y) { *x = s_fingx; *y = s_fingy; return s_fdown; }
#define XT_GLASS_US 60000                                    /* (0.4.3) from the first poll that finds the thank-you up to it on the glass: its frame's flush */
static bool s_xt_jdown, s_xt_up_was; static int64_t s_xt_press_us, s_xt_up_us;     /* (0.4.3) the experimental tank has a press; when one on its thank-you began; when it came up */
static bool s_tlock; static int64_t s_tlock_us;           /* THE TOUCH LOCK, and the last touch on the locked glass (its note) */
#define TOUCH_LOCK_HINT_US 2000000
void touch_port_set_touch_lock(bool on) { s_tlock = on; s_tlock_us = 0; }
bool touch_port_touch_lock(void) { return s_tlock; }
bool touch_port_lock_hint(void) { return s_tlock && s_tlock_us && esp_timer_get_time() - s_tlock_us < TOUCH_LOCK_HINT_US; }
/* (this fork) the touch lock undone without BOOT on a 320x240 board: in the CYD's case BOOT cannot be reached, and
 * upstream's only way back on the glass is BOOT twice. A finger held still on the locked glass for 5 s (the BOOT
 * chord's hold, ADV_BOOT_HOLD_S) opens MY TANKS instead - nothing else - and its padlock undoes the lock there. 5 s
 * still is a deliberate act: a tap, a swipe or a palm moving over the glass does nothing, as before. It is needed
 * over the plain locked tank, and (0.4.3's sync) over an experimental tank, locked or not - its glass is its own, with
 * no way to MY TANKS but BOOT twice (the jelly tank's branch below): since 0.3.9.6 upstream lets a setup page (a new
 * tank's welcome, a fry's naming) take its taps through the lock, and the reset prompt answers NO by itself after
 * CONFIRM_TIMEOUT_US. */
#define TOUCH_LOCK_HOLD_US 5000000
#define TOUCH_LOCK_HOLD_SLOP 24                           /* px the finger may roll and still be "still" (a tap's own slop) */
static bool s_tlock_held, s_tlock_open;                   /* this press already asked; the ask, for main.c */
#if CONFIG_POCKET_TANK_320X240
static bool s_hold_moved;                                 /* (this fork) this press has left its slop once: no still finger again */
#define XT_BACK_PX 56                                     /* (this fork) an experimental tank's way back: a press that lands in the
                                                             glass's bottom-left square this wide (render_xt_notice names it) */
#endif
bool touch_port_take_lock_open(void) { bool o = s_tlock_open; s_tlock_open = false; return o; }
bool touch_port_read_raw(float *x, float *y) {
    uint16_t px[1], py[1];
    if (!panel_read(px, py)) return false;
    map_touch(px[0], py[0], x, y);
    return true;
}

/* A FIRM PRESS on the bare tank (0.4.1, touch_gate.h): does (x, y) land on one of the bare tank's buttons - the ones the
 * release below answers before the water: the tool chip, the battery pill, a fish card's toolbox (GIVE's tile too) / NEXT
 * FISH, a visitor's passport, a critter card's arrows, a card itself, the crab card's COLLECT. Such a press calls no fish
 * and is a tap at its lift however long it stayed. Same tests, same order as the release's. */
static bool bare_button_at(const tank_t *t, float x, float y) {
    const bool fish_card = s_sel >= 0 && s_sel < t->n_fish;
    if (t->tool == TOOL_BREED) return render_breed_button_at(t, x, y);   /* (SELECTIVE BREEDING: its card, the chip, a fish - a firm press too) */
    if (t->tool != TOOL_HAND) return render_tool_chip_hit(t, x, y);
    if (s_pill && RENDER_BAT_HIT(x, y)) return true;
    if (fish_card && (render_give_hit(t, s_sel, x, y) || render_tools_hit(t, s_sel, x, y) >= 0)) return true;
    if (!fish_card && render_tool_chip_hit(t, x, y)) return true;
    if (tank_is_visitor(s_sel) && t->vis_used[s_sel - N_FISH_MAX] && render_passport_hit(x, y) != PASSPORT_HIT_NONE) return true;
    if (fish_card && render_next_hit(x, y)) return true;
    if (render_critter_here(t, s_sel) && render_critter_nav_hit(s_sel, x, y)) return true;
    if (render_critter_name_hit(t, s_sel, x, y)) return true;   /* (a critter card's NAME / RENAME, 0.4.2) */
    if (render_critter_shelve_hit(t, s_sel, x, y)) return true; /* (... and its SHELVE, 0.4.2) */
    if (s_sel >= 0 && s_sel != RENDER_CARD_SNAIL && s_sel != RENDER_CARD_SHRIMP && s_sel != RENDER_CARD_URCHIN && s_sel != RENDER_CARD_SEAHORSE
        && s_sel != RENDER_CARD_CRAB && s_sel != RENDER_CARD_OCTOPUS && !tank_is_visitor(s_sel) && RENDER_CARD_HIT(x, y)) return true;
    if (s_sel == RENDER_CARD_OCTOPUS && render_octo_collect_hit(t, x, y)) return true;
    return s_sel == RENDER_CARD_CRAB && render_crab_collect_hit(t, x, y);
}
void touch_port_spend_press(void) { s_press_spent = true; }

/* call every frame from the tank task */
void touch_port_poll(tank_t *t) {
    int64_t now = esp_timer_get_time();
    if (s_cf && now - s_cf_us > CONFIRM_TIMEOUT_US && !render_reset_holding() && !render_reset_erase_armed()) touch_port_confirm_answer(-1);   /* nobody answered: keep the tank
                                                                  (each page has its own 20 s; never out from under a finger holding HOLD TO ERASE) */
    {   static uint8_t gen; static bool seen;                  /* a fish left, the slots shifted: the stat card follows its fish, and the
                                                                  card of the one that left closes (the 11-02 sweep; tank_t.removed_slot) */
        if (seen && t->roster_shift != gen && s_sel >= 0 && s_sel < N_FISH_MAX) {   /* (roster_shift: an arrival shifts nothing, the
                                                                  card stays - 0.4.1 audit round 1, R-seam F5) */
            int r = t->removed_slot;
            if ((uint8_t)(gen + 1) != t->roster_shift || r < 0 || s_sel == r) s_sel = -1; else if (s_sel > r) s_sel--;
        }
        gen = t->roster_shift; seen = true;
    }
#ifdef TOUCH_TAB5
    if (!s_gt && !s_st) return;
#elif defined(TOUCH_P4)
    if (!s_gt) return;
#else
    if (!s_tp && !s_cst && !s_ft) return;
#endif
    uint16_t x[1], y[1];
    bool touched = panel_read(x, y);
    if (s_swallow) {                            /* the finger from the dark (touch_port_swallow): no press, */
        if (!touched) s_swallow = false;        /* no tap, no hold - the next touch is a fresh one */
        s_down = false; s_held_page = false; s_page_gone = false;
        s_fdown = false;                        /* ... and none for the pages that follow the finger (touch_port_finger) */
        return;
    }
    /* portrait panel (px,py) -> landscape tank (tx,ty): tx = TANK_W-1-py, ty = px;
     * flipped screen: mirror both, so downstream gestures live in displayed space */
    float tx = s_lx, ty = s_ly;
    if (touched) map_touch(x[0], y[0], &tx, &ty);           /* calibrated (above); the release keeps the last position */
    s_fdown = finger_gate(&s_fgate, touched, s_cf || (!setup_active() && render_gift_up(t)));   /* (the finger as it is: a page that tracks it, MY TANKS's
                                                                  RENAME wheel and holds - none of a press that met the gift card or the prompt over
                                                                  the page: holding NO THANKS sold a shelved tank, the evening sweep) */
    s_fingx = tx; s_fingy = ty;
    if (touched && !s_down) {
        audio_port_prewarm();                   /* the release's cue plays warm */
        s_press_us = now; s_press_clock = t->clock; s_px = tx; s_py = ty;
        /* snapshot the school: the user aims at where a fish WAS - by release
           a darting fish has moved and the finger hid it the whole time */
        for (int i = 0; i < t->n_fish && i < N_FISH_MAX; i++) { s_fx[i] = t->fish[i].x; s_fy[i] = t->fish[i].y; }
    }
#if CONFIG_POCKET_TANK_320X240
    /* (this fork) the 5 s holds below count a finger that stayed still: a press that once leaves its slop is a drag or a
       stroke to its lift, even back at its landing point - a finger stirring the water came round to it and asked */
    if (touched && !s_down) s_hold_moved = false;
    else if (touched && (tx - s_px) * (tx - s_px) + (ty - s_py) * (ty - s_py) >= TOUCH_LOCK_HOLD_SLOP * TOUCH_LOCK_HOLD_SLOP) s_hold_moved = true;
#endif
    /* LOADING (0.4, the 11-02 sweep: a kid's taps on the boot show fed the fish, opened a card or the keeper page - which ended the
       show at once - or put the light out): over the bare tank the finger goes nowhere until the tank is up; a press that began then
       is let go at its lift. A page (a new tank's welcome, MY TANKS by BOOT twice) is not the show: it takes its taps */
    if (s_quiet && !s_ms && !s_set && !s_shop && !s_coll && !s_shelf && !s_bat && !s_upd && !s_cf && !setup_active() && !adv_up()) {
        if (touched) s_quiet_press = true;
        s_down = touched; s_held_page = false; s_page_gone = false;
        return;
    }
    if (s_quiet_press) { if (!touched) s_quiet_press = false; s_down = touched; s_held_page = false; s_page_gone = false; return; }
    /* THE TOUCH LOCK: the finger goes nowhere - the note says how to get back. Not over MY TANKS (its padlock is the way back), and
       not over a page the tank opened itself and only a tap closes - a new tank's welcome after SWAP IN, a fry's naming (0.3.9.6,
       Migima's #help thread 11-01 "Stuck in the Welcome Screen": the lock is the board's and stays across the swap's restart, the
       welcome covered the note, and BOOT twice will not open MY TANKS over a naming page - nothing could reach the padlock) */
    if (s_tlock && !s_shelf && !setup_active()) {
        if (touched) s_tlock_us = now;                       /* (held: the note stays up, then 2 s past the lift) */
        if (touched != s_down && s_log) ESP_LOGI(TAG, "touch locked: the %s ignored", touched ? "press" : "lift");
#if CONFIG_POCKET_TANK_320X240
        if (!touched) s_tlock_held = false;                  /* (this fork) a held finger opens MY TANKS (above) */
        else if (s_down && !s_tlock_held && !s_hold_moved && now - s_press_us >= TOUCH_LOCK_HOLD_US) {
            s_tlock_held = true; s_tlock_open = true;
            ESP_LOGI(TAG, "touch locked: a finger held still %d s - MY TANKS, where the padlock undoes it", (int)(TOUCH_LOCK_HOLD_US / 1000000));
        }
#endif
        s_down = touched; s_held_page = false; s_page_gone = false;
        return;
    }
    bool su = setup_active();                                /* before the touch: BEGIN's release is not a tank tap */
    /* a gift offered to us (0.4, the 11-02 sweep): its card is up OVER any page and has the glass first - no page under it
       sees the finger (not the LINK page's arrows or its quiet timer either); under the tank's own setup, the reset prompt
       and ADVANCED it waits (render_gift_up says) */
    gift_book_t *gcard = gift_of(t);
    const bool card = !su && !s_cf && render_gift_up(t);
    const bool pressed = touched && !s_down;                 /* the finger landed this poll */
    if (s_card_was && !card) {                               /* the card went (answered, its time, called off): the pages under it come back */
        open_wait_start(&s_card_gone, now); page_gate_open(&s_page_gate, now);
        s_page_gate.hidden = touched;                        /* (a press still on the glass is the card's, not the page's: hidden to its lift) */
    }
    s_card_was = card;
    {   const notice_t *nt = notice_current();                /* AN ANNOUNCEMENT BACK FROM UNDER A PAGE (the review of THE TOUCH RULE): the tap
                                                                that closed the page brought it back - its double's second half is no tap
                                                                on it (W). A new one comes by itself: G1 only */
        const bool nvis = nt && !s_ms && !s_set && !s_upd && !s_shop && !s_coll && !s_shelf && !adv_up();
        if (nvis && !s_nvis_was && (notice_came_back() || notice_after_page())) open_wait_start(&s_notice_back, now);   /* (0.4.3 audit r2, R2-4:
                                                                also one that queued under the page, or that the closing tap earned - its
                                                                double's second half took it unread) */
        s_nvis_was = nvis; }
    const bool xtn = xt_notice_up();                         /* (0.4.3) THE EXPERIMENTAL TANK'S THANK-YOU: the first poll that finds it up -
                                                                drawn, not yet on the glass (its frame's flush) - every poll, so it is never stale */
    if (xtn && !s_xt_up_was) s_xt_up_us = now;
    s_xt_up_was = xtn;
    if (s_jelly && !s_ms && !s_set && !s_shop && !s_coll && !s_shelf && !s_bat && !s_upd && !s_cf && !su && !adv_up() && !card) {
#if CONFIG_POCKET_TANK_320X240
        /* (this fork) AN EXPERIMENTAL TANK's glass is all its own, and upstream's one way from it to MY TANKS is BOOT twice -
           out of reach in the CYD's case. A finger held still 5 s in the glass's bottom-left corner (XT_BACK_PX) asks for
           MY TANKS here, as anywhere on the locked glass (above; main.c opens it): the tank lets go of that press at once,
           and the rest of it is MY TANKS's, whose taps are only presses begun after it opened. Only there - a finger held
           anywhere else is the tank's own gesture, for as long as the keeper likes (the minnows mill round it, the jellies
           turn to it as a light), and the corner is clear of each tank's controls (the jellies' nozzle at the upper right,
           the minnows' feeding band along the top, the piranhas' taps). The thank-you says where (render_xt_notice). On a
           320 x 240 board only the serial console makes such a tank (shelf.h SHELF_XT_LIBRARY) */
        if (!touched) s_tlock_held = false;
        else if (s_tlock_held) { s_down = touched; s_held_page = false; s_page_gone = false; return; }
        else if (s_down && !s_hold_moved && s_px < XT_BACK_PX && s_py >= TANK_H - XT_BACK_PX && now - s_press_us >= TOUCH_LOCK_HOLD_US) {
            s_tlock_held = true; s_tlock_open = true;
            if (s_xt_jdown) { s_jelly(tx, ty, false, now / 1e6); s_xt_jdown = false; }   /* (the tank's own press: it lets go) */
            ESP_LOGI(TAG, "an experimental tank: a finger held still %d s in the corner - MY TANKS", (int)(TOUCH_LOCK_HOLD_US / 1000000));
            s_down = touched; s_held_page = false; s_page_gone = false;
            return;
        }
#endif
        if (xtn && !(s_xt_jdown && s_down)) {                /* THE EXPERIMENTAL TANK'S THANK-YOU (0.4.3, xt_notice.h) has the glass: a tap on it
                                                                closes it, the tank sees nothing. A press the tank already had when it came
                                                                stays the tank's to its lift (THE TOUCH RULE, G1: no tap on the notice);
                                                                s_xt_jdown with no finger down: its lift went to a card or the lock */
            if (pressed) s_xt_press_us = now;
            else if (!touched && s_down) { const bool took = xt_notice_tap(s_xt_press_us < s_xt_up_us + XT_GLASS_US ? 1e9f : (float)(now - s_xt_press_us) / 1e6f);   /* (a press
                                                                from before it reached the glass: spent, G1) */
                                           s_xt_up_was = xt_notice_up();   /* (closed: the next one up is stamped afresh) */
                                           ESP_LOGI(TAG, "tap %s the experimental tank's thank-you", !took ? "missed" : xt_notice_up() ? "held by" : "closed"); }
            s_down = touched; s_held_page = false; s_page_gone = false;
            return;
        }
        s_xt_jdown = touched;
        s_jelly(tx, ty, touched, now / 1e6);                 /* the jellies have the bare glass (jelly_touch): nothing of the fish tank's below */
        if (touched) { s_lx = tx; s_ly = ty; }               /* (a release keeps the last place) */
        s_down = touched; s_held_page = false; s_page_gone = false;
        return;
    }
    if (s_set && !s_cf && !su && !card) {                             /* the settings page owns the glass: segments, LIGHTS OUT's arrows, CLOSE */
        int v = 0, r = render_settings_touch(t, tx, ty, page_gate(&s_page_gate, touched, pressed, now), &v);   /* (a press just
                                                                       as it opened is not its own: page_gate) */
        if (r && r != SET_TAP_BRIGHT_LIVE && r != SET_TAP_VOLUME_LIVE) ESP_LOGI(TAG, "settings: %s %d", r == SET_TAP_CLOSE ? "CLOSE" : r == SET_TAP_BRIGHT ? "brightness" : r == SET_TAP_VOLUME ? "volume"
                                                  : r == SET_TAP_LIGHT ? "lights out" : r == SET_TAP_SCREEN ? "screen (1 = turned)"
                                                  : r == SET_TAP_FEED ? "auto feed (1 = on)" : r == SET_TAP_ROTATE ? "rotation (1 = locked)"
                                                  : r == SET_TAP_UPDATES ? "UPDATES" : r == SET_TAP_LINK ? "LINK" : r == SET_TAP_GFX ? "graphics quality (1 low .. 3 high)"
                                                  : r == SET_TAP_VIG ? "vignette (1 = on)" : r == SET_TAP_GFX_PAGE ? "the GRAPHICS page (1 = opened, 0 = BACK)"
                                                  : r == SET_TAP_FLIP ? "screen" : r == SET_TAP_SLEEP ? "sleep" : "idle seconds", v);
        if (r == SET_TAP_CLOSE && s_set_from_shelf) { s_set = false; s_set_from_shelf = false; s_shelf_return = true; spent_lift_set(&s_back); }   /* opened by MY TANKS's cog (0.4.2, Strato 11-12): CLOSE goes back there (main.c takes it) */
        else if (r == SET_TAP_CLOSE) { s_set = false; s_ms = !s_jelly; spent_lift_set(&s_back); }   /* back to the milestones page (2026-09-16); the release is spent
                                                                       (a jelly tank has no keeper page: back to the jellies) */
#ifdef TANK_P4                                          /* THE BIG SCREENS' SETTINGS PAGE: its rail (render_big.c). CLOSE from a page MY TANKS' cog opened goes back
                                                           there, as the pocket page's; a jelly tank has no keeper pages: the rail's others close to the jellies */
        else if (r == SET_TAP_TO_TANK && s_set_from_shelf) { s_set = false; s_set_from_shelf = false; s_shelf_return = true; spent_lift_set(&s_back); }
        else if (r == SET_TAP_TO_TANK || (s_jelly && (r == SET_TAP_TO_KEEPER || r == SET_TAP_TO_SHOP || r == SET_TAP_TO_VISITORS))) { s_set = false; s_set_from_shelf = false; spent_lift_set(&s_back); }
        else if (r == SET_TAP_TO_KEEPER) { s_set = false; s_set_from_shelf = false; s_ms = true; spent_lift_set(&s_back); }
        else if (r == SET_TAP_TO_SHOP) { s_set = false; s_set_from_shelf = false; s_shop = true; open_wait_start(&s_pg_wait, now); spent_lift_set(&s_back); }
        else if (r == SET_TAP_TO_VISITORS) { s_set = false; s_set_from_shelf = false; s_coll = true; render_collection_leave(); open_wait_start(&s_pg_wait, now); spent_lift_set(&s_back); }
        else if (r == SET_TAP_TO_TANKS) { s_set = false; s_set_from_shelf = false; s_ms = !s_jelly; s_set_what = SET_TAP_TANKS; s_set_val = 0; spent_lift_set(&s_back); }   /* (as the keeper page's: main opens MY TANKS) */
#endif
        else if (r == SET_TAP_UPDATES) { s_set = false; s_upd = true; updates_page_open(); spent_lift_set(&s_back); page_gate_open(&s_page_gate, now); ESP_LOGI(TAG, "updates page up"); }
        else if (r == SET_TAP_LINK && s_jelly) ESP_LOGI(TAG, "settings: LINK - not in a jelly tank (its fish are on the shelf)");
        else if (r == SET_TAP_LINK) { s_set = false; s_set_from_shelf = false; spent_lift_set(&s_back); page_gate_open(&s_page_gate, now); link_dev_page_open(t); }   /* link mode over the live tank (0.4) */
        else if (r == SET_TAP_BRIGHT || r == SET_TAP_BRIGHT_LIVE || r == SET_TAP_VOLUME || r == SET_TAP_VOLUME_LIVE || r == SET_TAP_LIGHT || r == SET_TAP_IDLE || r == SET_TAP_FEED || r == SET_TAP_ROTATE || r == SET_TAP_GFX || r == SET_TAP_VIG) { s_set_what = r; s_set_val = v; }
#if CONFIG_POCKET_TANK_320X240
        else if (r == SET_TAP_FLIP || r == SET_TAP_SLEEP) { s_set_what = r; s_set_val = v; }   /* (this fork) the 320x240 boards' SCREEN and SLEEP rows */
#endif
    } else {                                                 /* not fed this poll (the card, the prompt, closed): its press forgotten (R2T-3) -
                                                                and a slider in hand saved where the glass shows it (the release would have) */
        int v = 0, w = render_settings_cut(&v);
        if (w) { s_set_what = w; s_set_val = v; ESP_LOGI(TAG, "settings: %s %d (the drag cut: kept as shown)", w == SET_TAP_BRIGHT ? "brightness" : "volume", v); }
    }
    if (adv_up() && !s_cf && !su) {                          /* the ADVANCED page (0.3.9.2): its taps, the crosses, the check (a gift card waits under it) */
        { float x0, x1, y0, y1; view_reach(false, &x0, &x1, &y0, &y1); adv_set_reach(x0, x1, y0, y1); }   /* a cross read at the panel's end
                                                                is no aim (0.4.2: turned over, the 1.8's bottom row lies past it) */
        int r = adv_touch(tx, ty, touched, s_q);
        if (r == ADV_ACT_BACK) { s_set = true; render_settings_home(); spent_lift_set(&s_back); page_gate_open(&s_page_gate, now); ESP_LOGI(TAG, "advanced: BACK -> settings"); }
        else if (r == ADV_ACT_TICK) tank_emit(TEV_WHEEL_TICK, -1);
        else if (r) s_adv_act = r;                           /* main saves it (and tells the charger) */
    }
    if (s_upd && !s_cf && !su && !card) {                    /* the UPDATES page: CHECK (main restarts), FORGET, CLOSE */
        int r = updates_page_touch(tx, ty, page_gate(&s_page_gate, touched, pressed, now), now);
        if (r == UPD_TAP_CLOSE) { s_upd = false; s_set = true; render_settings_home(); render_settings_from_updates(t); spent_lift_set(&s_back); page_gate_open(&s_page_gate, now);   /* (the watch: its CLOSE over
                                                                       settings' SCREEN - a double tap turned the picture; 0.4.1) */
                                  ESP_LOGI(TAG, "updates page: CLOSE -> settings"); }
        else if (r == UPD_TAP_CHECK) { s_upd_act = r; ESP_LOGI(TAG, "updates page: CHECK FOR UPDATES"); }
        else if (r == UPD_TAP_ADD) { s_upd_act = r; ESP_LOGI(TAG, "wi-fi page: ADD A NETWORK"); }               /* (0.4.2: main restarts into update mode at the list) */
        else if (r == UPD_TAP_NETS || r == UPD_TAP_NETS_BACK) ESP_LOGI(TAG, "%s", r == UPD_TAP_NETS ? "updates page: the WI-FI page" : "wi-fi page: BACK -> updates");   /* (the face turned in update.c) */
        else if (r >= UPD_TAP_FORGET) ESP_LOGI(TAG, "updates page: a network forgotten (row %d)", r - UPD_TAP_FORGET + 1);
    } else updates_page_cancel();                            /* not fed this poll: its press forgotten (R2T-3: CHECK fired as the card
                                                                went, or the next time UPDATES opened after BOOT twice) */
    if (touched && !s_down && !link_dev_page_up() && !s_cf && !su                                  /* the DRIFTED notice's DISCONNECT: only where it is */
        && render_drift_live(t, s_ms || s_set || s_upd || s_shop || s_coll || s_shelf || s_bat || adv_up()) && link_dev_drift_hit(tx, ty)) {   /* seen (11-02 sweep) */
        link_dev_disconnect(); spent_lift_set(&s_back);        /* the release is spent */
    }
    if (link_dev_page_up() && !s_cf && !su && !card && !s_shelf) {   /* link mode owns the glass: the arrows (held), DONE, DISCONNECT - not
                                                                       under MY TANKS (BOOT twice over it): its holds are MY TANKS's, the hidden
                                                                       page's arrows and doors never see them (0.4.1; the 11-02 sweep) */
        static int64_t s_link_us;
        float dt = s_link_us ? (now - s_link_us) / 1e6f : 0; if (dt > 0.1f) dt = 0.1f;
        s_link_us = now;
        if (link_dev_touch(tx, ty, page_gate(&s_page_gate, touched, pressed, now), dt) == LINK_UI_DONE) spent_lift_set(&s_back);   /* the release is spent */
    } else { render_link_leave(); link_dev_touch_cancel(); }   /* not fed this poll: the press and an arrow's hold forgotten (R2T-3), the radio note's too */
    if (su && !s_cf) {
        bool birth = setup_is_birth(), rename = setup_is_rename(); int who = setup_fish(), place = setup_item(), crit = setup_crit();
        setup_touch(t, tx, ty, touched);                     /* taps and the letter wheel, classified in setup.c */
        if (!setup_active() && touched) { spent_lift_set(&s_back); s_press_spent = true; s_held_page = true; s_page_gone = true; }   /* (0.4.3 audit r2, R2-L32: the page
                                                                closed under the finger - KEEP?'s YES held full - its lift is nothing, and the
                                                                rest of the press no hold: no fish called, no piece's page - as a held page's) */
        if (!setup_active()) {
            if (rename && crit >= 0) ESP_LOGI(TAG, "rename closed: the critter is %s", t->crit_name[crit][0] ? t->crit_name[crit] : "(no name)");
            else if (rename) ESP_LOGI(TAG, "rename closed: the fish is %s", who >= 0 && who < t->n_fish ? t->fish[who].name : "?");
            else if (birth) ESP_LOGI(TAG, "birth flow done: %s named and saved", who >= 0 && who < t->n_fish ? t->fish[who].name : "?");
            else if (place >= 0) ESP_LOGI(TAG, "placed: %s at x %.0f, %s layer, saved", SD_ITEMS[place].name, tank_decor_x(t, place),
                                          tank_decor_z(t, place) == DECOR_Z_BACK ? "BEHIND" : tank_decor_z(t, place) == DECOR_Z_FRONT ? "IN FRONT" : "AMONG");
            else ESP_LOGI(TAG, "setup done: %s + %s", t->fish[0].name, t->fish[1].name);
        }
    }
    {   static bool s_su_press;                              /* (0.4.3 audit r3, R3-L2) a page was up under this press */
        if (touched && s_down && s_su_press && !setup_active() && !s_held_page) { spent_lift_set(&s_back); s_press_spent = true; s_held_page = true; s_page_gone = true; }
                                                             /* the page went BETWEEN polls under the finger (its fish left: a trade
                                                                finishing, a gift landing - setup_fish_removed): as when it closes in
                                                                setup_touch above, the rest of the press and its lift are nothing */
        s_su_press = touched && ((s_down && s_su_press) || setup_active());
    }
    { int se, sn = setup_take_sdgift(&se);                   /* the sand dollar dial's SEND (0.4.2): back to link mode, the gift on its way */
      if (sn > 0) link_dev_sd_send(t, se, sn); }
    { int rf = setup_take_renamed();                         /* a rename closed (2026-10-01): back to the milestones page, the fish's card up */
      if (rf >= 0) { s_ms = true; s_set = false; s_shop = false; s_coll = false; s_sel = -1; render_milestones_show_fish(t, rf);
                     open_wait_start(&s_pg_wait, esp_timer_get_time()); } }   /* (the card back: DONE / BACK doubled is spent, not RENAME or MORE - 0.4.2 review) */
    bool modal = s_ms || s_set || s_shop || s_coll || s_cf || su || s_bat || s_upd || s_shelf || adv_up() || link_dev_page_up() || card
                 || render_card_over_tank(t);   /* a page, a prompt, the gift card or the GIVE modal owns the glass (its holds and
                                                   swipes opened a piece's page, called the fish, wiped: the evening sweep; the VISITORS
                                                   pages were missing - a hold opened the castle's page under them, 0.4 rc round two) */
    s_modal_last = modal;                                    /* (touch_port_page_open: the radio's update waits for the bare tank) */
    if (pressed) { s_press_btn = !modal && bare_button_at(t, tx, ty); s_press_spent = false; s_page_gone = false; }   /* a button on the bare tank: no hold (0.4.1) */
    if (touched) {                                           /* stroke = wipe/slash, reaching the glass (stroke_point above) */
        float dx, dy; stroke_point(!s_down, tx, ty, &dx, &dy);
        if (!s_down) { s_dx0 = s_dx1 = dx; s_frames = 0; s_raw_px = x[0]; s_raw_py = y[0]; }
        if (dx < s_dx0) s_dx0 = dx;
        if (dx > s_dx1) s_dx1 = dx;
        s_dlx = dx; s_dly = dy; s_frames++;
        s_lx = tx; s_ly = ty; if (!modal && !s_page_gone) tank_touch_drag(t, dx, dy);   /* (R3-L2: a tool in hand wipes / cuts nothing after its page went) */
    }
    if (s_cf) {                         /* ERASE EVERYTHING's last answer is a HOLD (0.4.2, render_reset_hold): the finger every poll, of a
                                           press that began on this page (G1 + W, as its taps) */
        const bool counts = touched && s_press_us > s_cf_us && !open_wait_spent_at(&s_cf_wait, s_press_us, now);
        static int64_t last;            /* (the hold is what the polls that saw the finger add up to, never the press's age: 0.4.2 review) */
        int h = render_reset_hold(counts, s_px, s_py, tx, ty, last ? (float)((now - last) / 1e6) : 0);
        last = now;
        if (h) { touch_port_confirm_answer(h); spent_lift_set(&s_back); }   /* (the finger is still down: its lift is nothing) */
    }
    /* tap-and-hold on a decoration (2026-09-24, Strato: "right now it's like 5
       taps to get back to the edit"): a still finger 700 ms on an owned
       piece opens its placement page (MOVE / DEPTH / SELL) right there */
    if (touched && !modal && !s_held_page && !s_press_btn && t->tool == TOOL_HAND && now - s_press_us > 700000 && fabsf(tx - s_px) < 24 && fabsf(ty - s_py) < 24) {
        int it = tank_decor_hit(t, s_px, s_py);
        if (it >= 0) { setup_begin_place(t, it); s_held_page = true; s_sel = -1;
                       ESP_LOGI(TAG, "held on the %s: placement page up", SD_ITEMS[it].name); }
    }
    if (touched && !modal && !s_held_page && !s_press_btn && now - s_press_us > 300000 && fabsf(ty - s_py) < 30) tank_touch_hold(t, tx, ty);
#ifndef TOUCH_PORT_HOST
    {   /* the director's `finger` (0.4.3, the frame-rate tests): a finger held on the glass with nobody's hand on it */
        extern int64_t director_finger_until_us; extern float director_finger_x, director_finger_y;
        if (now < director_finger_until_us) tank_touch_hold(t, director_finger_x, director_finger_y); }
#endif
    if (!touched && s_down) {
        if (!render_critter_shelve_hit(t, s_sel, s_px, s_py)) render_critter_shelve_disarm();   /* a critter card's SHELVE asks again after any other
                                                                   tap: a card closed, an arrow, NAME, COLLECT, the water (0.4.2) */
        /* release: classify with the LAST touched position (the old code fell
           back to the PRESS position here, so dx/dy were always 0 - every
           quick swipe read as a tap and the drag-feed could never fire) */
        float dx = s_lx - s_px, dy = s_ly - s_py;
        render_tap_press(s_press_clock);               /* (every wait this release meets is read at its press: 0.4.1 audit round 1, TCH-1) */
        int64_t held_us = s_seen_us - s_press_us;      /* the finger's own time on the glass: the lift's bridge is not the keeper's */
        const bool still = tap_still(dx, dy), quick = tap_quick(held_us);   /* A FIRM PRESS (touch_gate.h, 0.4.1): still = a tap on a button at
                                                                       any length; the water's tap must be quick too (longer is its hold) */
        const bool back = spent_lift_take(&s_back);   /* a page's own button took this press: whatever this lift is, it is spent, and the next one is not */
        if (s_log) ESP_LOGI(TAG, "stroke: raw %d,%d | press %.0f,%.0f -> lift %.0f,%.0f | lands %.0f,%.0f, drag x %.0f..%.0f, ends %.0f,%.0f | %d frames %d ms%s | scissors %s%s%s, travel %.0f",
                            s_raw_px, s_raw_py, s_px, s_py, s_lx, s_ly, s_sx, s_sy, s_dx0, s_dx1, s_dlx, s_dly, s_frames, (int)(held_us / 1000),
                            modal ? " (a page had the glass)" : "", t->slash_armed ? "ARMED" : "not armed", t->slash_engaged ? " ENGAGED" : "", t->slash_cut ? " CUT" : "", t->drag_dist);
        if (s_log && s_ft) ESP_LOGI(TAG, "stroke: %d reads inside the press showed no finger (bridged, lift after %d ms of them)", s_cst_gaps, CST_LIFT_US / 1000);
        if (s_log && s_cst) ESP_LOGI(TAG, "stroke: lift by %s (%d ms rule, said %s) | %d silent reads bridged, longest silence %d ms | %d \"no finger\" reports, a finger again after %d | pressed %d ms after the last lift | lift seen %d ms after the finger",
                                     s_cst_by_said ? "the chip's word" : "silence", s_lift_us / 1000, s_lift_said ? "on" : "off", s_cst_gaps, s_cst_maxgap_ms, s_cst_said, s_cst_back, s_cst_again_ms, (int)((now - s_seen_us) / 1000));
        if (s_cf) {                     /* the prompt owns the glass: a press AND release on the
                                           same button answers it, nothing else counts - not
                                           even the tap that opened it (it began before) */
            int h = s_press_us > s_cf_us && !open_wait_spent_at(&s_cf_wait, s_press_us, now) ? render_reset_tap(s_px, s_py, s_lx, s_ly) : 0;   /* (and not in its
                                           first OPEN_WAIT_US, its opening tap's double, never restarted: a kid's mash as the prompt came reset
                                           the tank - 0.4.1 audit P8; THE TOUCH RULE since 11-06) */
            if (h == RESET_EV_PAGE) { s_cf_us = now; open_wait_start(&s_cf_wait, now); }   /* (0.4.2) another of its pages: a page a tap brought -
                                           its own stamp (G1 + W) and its own 20 s */
            else if (h) touch_port_confirm_answer(h);   /* NO, or RESET THIS TANK's YES. ERASE EVERYTHING is never a tap: the hold, above */
            goto released;
        }
        if (su) {                       /* the setup had the glass (setup_touch above); just the log:
                                           where the finger landed vs what it hit, in case this panel
                                           reports fingers offset from where they feel */
            ESP_LOGI(TAG, "setup touch press %.0f,%.0f release %.0f,%.0f -> %s", s_px, s_py, s_lx, s_ly,
                     setup_hit_name(setup_active() ? setup_hit(s_px, s_py) : 0));
            s_sel = -1; goto released;
        }
        if (card) {                     /* the gift card has the glass (over any page): ACCEPT / NO THANKS - a press from before it is no answer (THE TOUCH RULE), counted
                                           from when it was first seen; a miss is nothing (11-02 sweep) */
            if (s_tlock) {             /* THE TOUCH LOCK (0.4.1 audit round 1, TCH-6): the card is not answered over MY TANKS either (the
                                           lock lets MY TANKS through for its padlock, and the card's branch came first); it waits
                                           out its time, as over the locked tank */
                ESP_LOGI(TAG, "gift card up: touch locked - no answer"); goto released;
            }
            if (still && !(gcard && gcard->ask.on)) {   /* FISH SWAP (0.4.3): its card in the gift card's place */
                static const char *const SW_TAP[] = { "off the card - nothing", "the card", "spent (a press from before its step, or its opening tap's double)",
                                                      "SWAP / PICK", "NO THANKS / BACK", "BACK", "CALL OFF", "OK", "a fish" };
                int r = render_swap_tap(t, s_px, s_py);
                ESP_LOGI(TAG, "swap card tap at %.0f,%.0f -> %s", s_px, s_py, SW_TAP[r]);
            } else if (still) {
                int gh = render_gift_hit(s_px, s_py);
                if ((gh == GIFT_HIT_ACCEPT || gh == GIFT_HIT_NO) && render_gift_back_window(t, s_press_clock)) {   /* back from under a page a tap
                                                                       closed: that tap's double (W, touch_gate.h) */
                    ESP_LOGI(TAG, "gift: a tap on %s - spent (the double of the tap that brought it back)", gh == GIFT_HIT_ACCEPT ? "ACCEPT" : "NO THANKS");
                } else if (gh == GIFT_HIT_ACCEPT || gh == GIFT_HIT_NO) {
                    if (gift_answer(gcard, s_press_clock, t->clock, gh == GIFT_HIT_ACCEPT)) ESP_LOGI(TAG, "gift: %s %s", gcard->ask.pp.name, gh == GIFT_HIT_ACCEPT ? "ACCEPTED" : "declined (NO THANKS)");
                    else ESP_LOGI(TAG, "gift: a tap on %s - spent (a press from before the card)", gh == GIFT_HIT_ACCEPT ? "ACCEPT" : "NO THANKS");
                } else ESP_LOGI(TAG, "gift card up: a tap at %.0f,%.0f off its buttons - nothing", s_px, s_py);
            }
            goto released;
        }
        if (s_card_gone.on && (s_press_us < s_card_gone.at || open_wait_spent_at(&s_card_gone, s_press_us, now))) {   /* the card's answer
                                                                       doubled, or a tap as it went (above): spent (THE TOUCH RULE: not restarted) - from its lift */
            ESP_LOGI(TAG, "tap at %.0f,%.0f - spent (the gift card just went: its answer doubled)", s_px, s_py);
            goto released;
        }
        if (s_bat) { s_bat = false; ESP_LOGI(TAG, "battery page closed"); goto released; }   /* any release closes it */
        if (s_shelf) {                                              /* MY TANKS: main.c knows what is up (the shelf, a card, the library) and acts */
            if (still && !s_press_spent && s_press_us >= s_shelf_wait.at && !open_wait_spent_at(&s_shelf_wait, s_press_us, now)) { s_shelf_tap = true; s_shelf_tx = s_px; s_shelf_ty = shelf_tap_y(s_py); s_shelf_press_us = s_press_us; }   /* (a press main.c took as a hold - MOVE's
                                                                       lift, REMOVE done - is no tap: touch_port_spend_press) */
            goto released;
        }
        if (still) {
            if (!s_page_gone && notice_current() && (quick || modal) && render_give_modal_step(t) == RENDER_GM_OFF) {   /* (R3-L2: the lift of a
                                                                       press its page closed under is no tap on a notice) */   /* (the lights-out notice lets its tap
                                                                       through: notice.h; over the bare tank a slow press was the fish's hold
                                                                       and leaves the notice up. Not under the GIVE modal, drawn over it: its
                                                                       taps are the modal's - BACK went to the notice, 0.4.1 audit TCH-8) */
                if (s_notice_back.on && open_wait_spent_at(&s_notice_back, s_press_us, now)) { ESP_LOGI(TAG, "tap at %.0f,%.0f - spent (the announcement just came back: the double of the tap that closed the page)", s_px, s_py); goto released; }
                int ef = render_notice_evolve_tap(t, s_px, s_py);   /* (0.4.3) ON the READY TO EVOLVE card: its reveal; beside it: closed */
                bool took = notice_dismiss_held((float)(now - s_press_us) / 1e6f); ESP_LOGI(TAG, "tap %s the announcement", notice_current() ? "held by" : "closed");
                if (took && !notice_current() && ef >= 0) { s_sel = -1; setup_begin_evolve_tank(t, ef); ESP_LOGI(TAG, "the READY TO EVOLVE card tapped: %s's reveal", t->fish[ef].name); }
                if (took) goto released; }
            if (s_set || s_upd || back || adv_up() || link_dev_page_up()) goto released;   /* the settings / updates page had the glass (their touch calls above) */
            if ((s_ms || s_shop || s_coll) && open_wait_spent_at(&s_pg_wait, s_press_us, now)) {   /* the tap that brought this page, doubled (above) */
                ESP_LOGI(TAG, "page tap at %.0f,%.0f - spent (the double of the tap that brought the page)", s_px, s_py);
                goto released;
            }
            if (s_coll) {                                           /* the visitor collection: a fish's passport, CLOSE */
                int pg = render_collection_page(), r = render_collection_tap(t, s_px, s_py);
                ESP_LOGI(TAG, "collection tap at %.0f,%.0f -> %s (page %d of %d)", s_px, s_py, r == COLL_TAP_CLOSE ? "CLOSE" :
                         render_collection_page() != pg ? "PAGE" : r == COLL_TAP_KEPT ? "passport" : "nothing", render_collection_page() + 1, collection_pages());
                if (r == COLL_TAP_CLOSE) { s_coll = false; render_collection_leave(); s_ms = true; open_wait_start(&s_pg_wait, now); }   /* back to the milestones page */
#ifdef TANK_P4                                                /* the big screens' rail (render_big.c) */
                else if (r == COLL_TAP_TO_TANK) { s_coll = false; render_collection_leave(); }
                else if (r == COLL_TAP_TO_SHOP) { s_coll = false; render_collection_leave(); s_shop = true; open_wait_start(&s_pg_wait, now); }
                else if (r == COLL_TAP_TO_SETTINGS) { s_coll = false; render_collection_leave(); s_set = true; s_set_from_shelf = false; render_settings_home(); page_gate_open(&s_page_gate, now); }
                else if (r == COLL_TAP_TO_TANKS) { s_coll = false; render_collection_leave(); s_ms = true; s_set_what = SET_TAP_TANKS; s_set_val = 0; }   /* (as the keeper page's: main opens MY TANKS) */
#endif
                goto released;
            }
            if (s_shop) {                                           /* the shop: a row's modal, UNLOCK, HOW TO EARN, CLOSE */
                int r = render_shop_tap(t, s_px, s_py);
                ESP_LOGI(TAG, "shop tap at %.0f,%.0f -> %s", s_px, s_py, r == SHOP_TAP_CLOSE ? "CLOSE" : r >= SHOP_TAP_BG_USE ? "USE A BACKGROUND" : r >= SHOP_TAP_BG_BUY ? "UNLOCK A BACKGROUND" : r >= SHOP_TAP_PLACE ? "PUT BACK" : r >= SHOP_TAP_SHELVE ? "SHELVE" : r >= SHOP_TAP_FRY ? "BUY A FRY" : r >= SHOP_TAP_SELL ? "SELL" : r >= SHOP_TAP_MOVE ? "MOVE" : r >= SHOP_TAP_BUY ? "UNLOCK" : r == SHOP_TAP_KEPT ? "modal" : "nothing");
                if (r == SHOP_TAP_CLOSE) { s_shop = false; s_coll = false; render_shop_leave(); s_ms = true; open_wait_start(&s_pg_wait, now); }   /* back to the milestones page (2026-09-16) */
#ifdef TANK_P4                                                  /* THE BIG SCREENS' UPGRADES PAGE: its rail's other pages (render_big.c) */
                else if (r == SHOP_TAP_TO_TANK) { s_shop = false; s_coll = false; render_shop_leave(); }
                else if (r == SHOP_TAP_TO_SETTINGS) { s_shop = false; s_coll = false; render_shop_leave(); s_set = true; s_set_from_shelf = false; render_settings_home(); page_gate_open(&s_page_gate, now); }
                else if (r == SHOP_TAP_TO_VISITORS) { s_shop = false; render_shop_leave(); s_coll = true; render_collection_leave(); open_wait_start(&s_pg_wait, now); }
                else if (r == SHOP_TAP_TO_TANKS) { s_shop = false; s_coll = false; render_shop_leave(); s_ms = true; s_set_what = SET_TAP_TANKS; s_set_val = 0; }   /* (as the keeper page's: main opens MY TANKS, closing that page) */
#endif
                else if (r >= SHOP_TAP_BUY) s_shop_act = r;   /* main.c buys (and plays the cue) or opens the placement page */
                goto released;
            }
            {   gift_book_t *gb = gift_of(t);                /* the GIVE modal (0.4): every tap while it is up, over the keeper page or the
                                                                       live tank (there a gift offered to us is on top: its card answers first) */
                int gs = render_give_modal_step(t);
                if (gs != RENDER_GM_OFF && !setup_active() && !s_cf && (s_ms || !(gb && gb->ask.on))) {
                    int r = render_give_modal_tap(t, s_px, s_py);
                    static const char *const GM_TAP[] = { "off the card - nothing", "the card", "spent (its opening tap's double, or a press from before its step)", "GIVE", "the LEFT tank", "the RIGHT tank", "BACK", "CALL OFF", "OK", "MOVE (the travel bowl)", "LET IN (the travel bowl)" };
                    ESP_LOGI(TAG, "give modal tap at %.0f,%.0f -> %s", s_px, s_py, GM_TAP[r]);
                    if (r == RENDER_GM_TAP_GIVE || r == RENDER_GM_TAP_LEFT || r == RENDER_GM_TAP_RIGHT) s_sel_us = now;   /* (the stat card stays while it asks) */
                    goto released;
                }
            }
            if (s_ms) {                                             /* the page: badges open a modal, the CLOSE
                                                                       button ends it, the brightness row cycles */
                int r = render_milestones_tap(t, s_px, s_py);     /* CLOSE / SETTINGS / the sand dollar, detail modal, nothing */
                ESP_LOGI(TAG, "page tap at %.0f,%.0f (release %.0f,%.0f) -> %s", s_px, s_py, s_lx, s_ly,
                         r >= MS_TAP_EVOLVE ? "EVOLVE (a fish's card)" : r >= MS_TAP_ABOUT ? "MORE (a fish's card)" : r >= MS_TAP_SELL ? "SELL (a fish's card)" : r >= MS_TAP_RENAME ? "RENAME (a fish's card)" :
                         r == MS_TAP_CLOSE ? "CLOSE" : r == MS_TAP_SETTINGS ? "SETTINGS" : r == MS_TAP_SHOP ? "SHOP" : r == MS_TAP_VISITORS ? "VISITORS" : r == MS_TAP_TANKS ? "MY TANKS" : r == MS_TAP_KEPT ? "detail" : "nothing");
                if (r >= MS_TAP_EVOLVE) {                           /* ... EVOLVE (0.4.3): the reveal page over the tank; setup_take_renamed
                                                                       brings the card back */
                    s_ms = false; s_sel = -1;
                    setup_begin_evolve(t, r - MS_TAP_EVOLVE);
                    goto released;
                }
                if (r >= MS_TAP_ABOUT) {                            /* ... MORE (0.4.2): its pages over the keeper page, halved (main draws
                                                                       them: render_keeper_about); setup_take_renamed brings the card back */
                    s_ms = false; s_sel = -1;
                    setup_begin_about(t, r - MS_TAP_ABOUT);
                    goto released;
                }
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
                if (r == MS_TAP_TANKS) {                            /* the foot's shelf (0.4): MY TANKS, as BOOT twice (main opens it, closing this page) */
                    progression_ack_milestones(t); s_set_what = SET_TAP_TANKS; s_set_val = 0;
                    goto released;
                }
                if (r != MS_TAP_CLOSE && r != MS_TAP_SETTINGS && r != MS_TAP_SHOP && r != MS_TAP_VISITORS) goto released;   /* only a button leaves the page */
                s_ms = false; s_sel = -1; s_set = r == MS_TAP_SETTINGS; s_set_from_shelf = false; s_shop = r == MS_TAP_SHOP;
                if (s_set) render_settings_home();           /* (the settings page itself, never its GRAPHICS face left up - 0.4.2) */
                if (s_set) page_gate_open(&s_page_gate, now);   /* (its double's second half is not a settings tap) */
                s_coll = r == MS_TAP_VISITORS; if (s_coll) render_collection_leave();
                if (s_shop || s_coll) open_wait_start(&s_pg_wait, now);   /* (the shop's foot, the collection's CLOSE under it: TCH-4) */
                progression_ack_milestones(t); render_milestones_leave();   /* everything shown is now "seen" */
                goto released;
            }
            if (!quick && !s_press_btn) goto released;      /* the bare tank: a slow press is a tap only where it landed on a button (bare_button_at) */
            if (open_wait_spent_at(&s_crit_back, s_press_us, now)) {   /* the double of the DONE / CANCEL that put a critter's card back (0.4.2): DONE and
                                                                   CANCEL stand over the card's arrows - a kid's double tap stepped it to another critter */
                ESP_LOGI(TAG, "tap at %.0f,%.0f - spent (the double of the tap that closed the name wheel)", s_px, s_py);
                goto released;
            }
            if (t->tool != TOOL_HAND) {                      /* a tool in hand owns the glass (2026-10-04): only its chip's DONE answers a tap */
                s_sel = -1;
                if (t->tool == TOOL_BREED) {                 /* SELECTIVE BREEDING (0.4.2): the card's buttons, CANCEL, a fish marked */
                    static const char *const BT_WORD[] = { "nothing", "held (a press before the card)", "first fish marked", "unmarked", "second fish: the PAIR card", "a visitor (THAT FISH IS VISITING)", "PAIRED", "refused (short of sand dollars)", "UNPAIRED (refunded)", "CANCEL - the tool back in the box", "DONE - the tool back in the box" };
                    int r = render_breed_tap(t, s_px, s_py);
                    ESP_LOGI(TAG, "breeding tool tap at %.0f,%.0f: %s (balance %d)", s_px, s_py, BT_WORD[r], (int)t->sd_balance);
                    goto released;
                }
                if (render_tool_chip_hit(t, s_px, s_py)) { tank_set_tool(t, TOOL_HAND); ESP_LOGI(TAG, "tool chip tapped at %.0f,%.0f: back to bare hands", s_px, s_py); }
                else ESP_LOGI(TAG, "tap at %.0f,%.0f ignored: a tool is in hand (DONE first)", s_px, s_py);
                goto released;
            }
            if (s_pill && RENDER_BAT_HIT(s_px, s_py)) {      /* the battery pill, while it shows: its page (2026-09-24) */
                s_bat = true; s_bat_us = now; s_sel = -1;
                ESP_LOGI(TAG, "battery pill tapped at %.0f,%.0f: battery page up", s_px, s_py);
                goto released;
            }
            if (s_sel >= 0 && s_sel < t->n_fish && render_give_hit(t, s_sel, s_px, s_py)) {   /* GIVE's tile in the toolbox while linked (0.4; a tile 0.4.2) */
                render_give_tap(t, s_sel); s_sel_us = now;
                ESP_LOGI(TAG, "give box tap at %.0f,%.0f (%s) -> the GIVE modal %s", s_px, s_py, t->fish[s_sel].name, render_give_modal_step(t) != RENDER_GM_OFF ? "up" : "not up");
                goto released;
            }
            if (s_sel >= 0 && s_sel < t->n_fish && render_tools_hit(t, s_sel, s_px, s_py) >= 0) {   /* the toolbox with the card (2026-10-01):
                                                                                     pick a tool up, or put the one in hand back */
                int tl = render_tools_hit(t, s_sel, s_px, s_py);
                tank_set_tool(t, t->tool == tl ? TOOL_HAND : tl); s_sel = -1;
                if (t->tool == TOOL_BREED) render_breed_tool_up(t);   /* SELECTIVE BREEDING: a pair already set shows its card at once */
                ESP_LOGI(TAG, "toolbox tap at %.0f,%.0f -> %s", s_px, s_py, t->tool == TOOL_SPONGE ? "SPONGE" : t->tool == TOOL_SCISSORS ? "SCISSORS" : t->tool == TOOL_BREED ? "BREEDING" : "bare hands");
                goto released;
            }
            if ((s_sel < 0 || s_sel >= t->n_fish) && render_tool_chip_hit(t, s_px, s_py)) {   /* the tool chip's DONE */
                tank_set_tool(t, TOOL_HAND);
                ESP_LOGI(TAG, "tool chip tapped at %.0f,%.0f: back to bare hands", s_px, s_py);
                goto released;
            }
            if (tank_is_visitor(s_sel) && t->vis_used[s_sel - N_FISH_MAX]) {   /* a visitor's passport is up (2026-10-06): its buttons */
                int ph = render_passport_hit(s_px, s_py);
                if (ph == PASSPORT_HIT_DEPORT && hold_spent_us_at(&s_card_us, s_press_us, now, TAP_W_US)) {   /* (the 11-02 sweep: one tap of a kid's
                                                                                        second tap on the visitor sent a friend's fish home: its opening
                                                                                        tap's double is spent - THE TOUCH RULE, 0.2 s, never restarted) */
                    ESP_LOGI(TAG, "passport: DEPORT spent (the double of the tap that opened it, or a press from before)");
                    s_sel_us = now; goto released;
                }
                if (ph == PASSPORT_HIT_DEPORT) {
                    ESP_LOGI(TAG, "passport: %s DEPORTED - home it swims, our door shut to it a while", t->fish[s_sel].name);
                    tank_link_deport(t, s_sel); s_sel = -1; goto released;
                }
                if (ph == PASSPORT_HIT_COLLECT) {
                    bool full = collection_full(), ok = !full && collection_add(t, s_sel - N_FISH_MAX);   /* (FULL: the button is dim, the tap does nothing) */
                    ESP_LOGI(TAG, "passport: %s %s (%d in the collection, %d pages)", t->fish[s_sel].name,
                             ok ? "COLLECTED" : full ? "not added: FULL" : "not added (in already)", collection_count(), collection_pages());
                    s_sel_us = now; goto released;
                }
                if (ph == PASSPORT_HIT_CARD) { s_sel_us = now; goto released; }   /* (a tap on the card itself keeps it) */
            }
            if (s_sel >= 0 && s_sel < t->n_fish && render_next_hit(s_px, s_py)) {   /* the NEXT FISH arrow beside the card (0.3.9): the card
                                                                                  steps on (wrapping round), its 10 s start again */
                int was = s_sel; s_sel = render_next_fish(t, s_sel); s_sel_us = now;
                if (s_sel != was) tank_emit(TEV_WHEEL_TICK, -1);
                ESP_LOGI(TAG, "next fish tap at %.0f,%.0f -> card %d (%s)", s_px, s_py, s_sel, t->fish[s_sel].name);
                goto released;
            }
            if (render_critter_here(t, s_sel) && render_critter_nav_hit(s_sel, s_px, s_py)) {   /* a critter card's arrows (0.3.9.3):
                                                                                      back or on through the critters, its 10 s again */
                int dir = render_critter_nav_hit(s_sel, s_px, s_py), was = s_sel;
                s_sel = render_critter_step(t, s_sel, dir); s_sel_us = now;
                if (s_sel != was) tank_emit(TEV_WHEEL_TICK, -1);
                ESP_LOGI(TAG, "critter arrow %s at %.0f,%.0f -> card %d", dir < 0 ? "left" : "right", s_px, s_py, s_sel);
                goto released;
            }
            if (render_critter_shelve_hit(t, s_sel, s_px, s_py)) {   /* a critter card's SHELVE (0.4.2): asked twice (SURE?), then into its jar
                                                                     on the shelf with a poof and the card closes; the shop's PUT BACK returns it */
                if (hold_spent_us_at(&s_card_us, s_press_us, now, TAP_W_US)) {   /* (the double of the tap that opened the card, or a press from before: spent) */
                    ESP_LOGI(TAG, "critter card: SHELVE spent (the double of the tap that opened it, or a press from before)");
                    s_sel_us = now; goto released;
                }
                int r = render_critter_shelve_tap(t, s_sel, s_px, s_py), item = render_crit_item_of_card(s_sel);
                if (r == CRIT_SHELVE_GO && progression_shelve(t, item)) { tank_emit(TEV_CONFIRM, -1); s_sel = -1;
                    ESP_LOGI(TAG, "critter card: %s on the shelf (the shop's PUT BACK returns it)", SD_ITEMS[item].name); }
                else { s_sel_us = now; ESP_LOGI(TAG, "critter card: SHELVE %s", r == CRIT_SHELVE_ARMED ? "armed (SURE?)" : "refused"); }
                goto released;
            }
            if (render_critter_name_hit(t, s_sel, s_px, s_py)) {   /* a critter card's NAME / RENAME (0.4.2): the letter wheel over the live tank,
                                                                   as a fish card's RENAME; its card back up when the wheel closes (below) */
                if (hold_spent_us_at(&s_card_us, s_press_us, now, TAP_W_US)) {   /* (the double of the tap that opened the card - a kid's second
                                                                   tap on a seahorse in front of the button - or a press from before: spent, THE TOUCH RULE) */
                    ESP_LOGI(TAG, "critter card: NAME spent (the double of the tap that opened it, or a press from before)");
                    s_sel_us = now; goto released;
                }
                int k = render_crit_name_of_card(s_sel);
                s_sel = -1; setup_begin_crit_rename(t, k);
                ESP_LOGI(TAG, "critter card: NAME at %.0f,%.0f -> %s", s_px, s_py, setup_active() ? "the wheel is up (CANCEL / DONE)" : "refused");
                goto released;
            }
            if (s_sel >= 0 && s_sel != RENDER_CARD_SNAIL && s_sel != RENDER_CARD_SHRIMP && s_sel != RENDER_CARD_URCHIN && s_sel != RENDER_CARD_SEAHORSE && s_sel != RENDER_CARD_CRAB && s_sel != RENDER_CARD_OCTOPUS && !tank_is_visitor(s_sel) && RENDER_CARD_HIT(s_px, s_py)) {   /* a tap ON the card (or the slop
                ESP_LOGI(TAG, "card tap at %.0f,%.0f -> milestones", s_px, s_py);         under its MORE button) = milestones page */
                if (s_sel < t->n_fish && tank_evo_ready(&t->fish[s_sel]) && !t->fish[s_sel].away) {   /* (0.4.3) its stat card says EVOLVE: the reveal */
                    if (hold_spent_us_at(&s_card_us, s_press_us, now, TAP_W_US)) {   /* (0.4.3 audit r1 TP-5) the double of the tap that opened the
                                                                   card - a READY fish under its corner, tapped twice - or a press from before: spent */
                        ESP_LOGI(TAG, "card tap: EVOLVE spent (the double of the tap that opened the card, or a press from before)");
                        s_sel_us = now; goto released;
                    }
                    int ef = s_sel; s_sel = -1; setup_begin_evolve_tank(t, ef);
                    ESP_LOGI(TAG, "card tap: %s is ready to evolve -> its reveal", t->fish[ef].name);
                    goto released;
                }
                s_ms = true; goto released;
            }
            if (s_sel == RENDER_CARD_CRAB && render_crab_collect_hit(t, s_px, s_py)) {   /* the crab card's COLLECT (0.3.9.4): the find taken, the card stays up */
                tank_crab_tap(t, t->cr_x); s_sel_us = now;
                ESP_LOGI(TAG, "crab card: COLLECT - the keeper took its sand dollar (%u so far)", (unsigned)t->cr_found);
                goto released;
            }
            if (s_sel == RENDER_CARD_OCTOPUS && render_octo_collect_hit(t, s_px, s_py)) {   /* the octopus card's COLLECT (0.4.3): the crab's, to the letter */
                tank_octopus_collect(t); s_sel_us = now;
                ESP_LOGI(TAG, "octopus card: COLLECT - the keeper took its sand dollar (%u so far)", (unsigned)t->oc_found);
                goto released;
            }
            if (!quick) goto released;                       /* (the water, a fish, a critter: a slow press was the hold) */
            if (tank_octopus_find_hit(t, s_px, s_py)) {      /* the octopus's sand dollar held up: first in line, as the crab's (0.4.3) */
                tank_octopus_tap(t, s_px, s_py);
                ESP_LOGI(TAG, "octopus tapped: the keeper took its sand dollar (%u so far), first in line", (unsigned)t->oc_found);
                goto released;
            }
            if (tank_crab_find_hit(t, s_px, s_py)) {   /* a sand dollar held up: before everything that swims or crawls in front (0.3.9.4) */
                tank_crab_tap(t, s_px);
                ESP_LOGI(TAG, "crab tapped: the keeper took its sand dollar (%u so far), first in line", (unsigned)t->cr_found);
                goto released;
            }
            /* fish first; only an empty tap reaches the water. 38 px radius
               (a fingertip on this 322 ppi panel covers ~60 px) against BOTH
               the press-time snapshot and the current position - whichever is
               closer - so a fish that moved mid-tap still registers. */
            int best = -1; float bd = 38 * 38;
            for (int i = 0; i < t->n_fish; i++) {
                float ax = s_fx[i] - s_px, ay = s_fy[i] - s_py;
                float bx = t->fish[i].x - s_px, by = t->fish[i].y - s_py;
                float d2a = ax * ax + ay * ay, d2b = bx * bx + by * by;
                float d2 = d2a < d2b ? d2a : d2b;
                if (d2 < bd) { bd = d2; best = i; }
            }
            for (int k = 0; k < N_VISIT_MAX; k++) {      /* visitors (the tank link): their passport */
                if (!t->vis_used[k]) continue;
                float bx = t->fish[N_FISH_MAX + k].x - s_px, by = t->fish[N_FISH_MAX + k].y - s_py;
                if (bx * bx + by * by < bd) { bd = bx * bx + by * by; best = N_FISH_MAX + k; }
            }
            if (best >= 0) { s_sel = (best == s_sel) ? -1 : best; s_sel_us = now;
                             if (best >= N_FISH_MAX) ESP_LOGI(TAG, "visitor tapped: %s's passport %s", t->fish[best].name, s_sel >= 0 ? "up" : "down"); }
            else if (tank_snail_hit(t, s_px, s_py)) {   /* the snail: its card (2026-09-16), the fish first */
                s_sel = s_sel == RENDER_CARD_SNAIL ? -1 : RENDER_CARD_SNAIL; s_sel_us = now;
                ESP_LOGI(TAG, "snail tapped: card %s (%d spots grazed)", s_sel >= 0 ? "up" : "down", (int)t->snail_grazed); }
            else if (tank_urchin_hit(t, s_px, s_py)) {  /* the urchin: its card (2026-10-02) */
                s_sel = s_sel == RENDER_CARD_URCHIN ? -1 : RENDER_CARD_URCHIN; s_sel_us = now;
                ESP_LOGI(TAG, "urchin tapped: card %s (%.0f cm of grass grazed)", s_sel >= 0 ? "up" : "down", t->urchin_grazed_px / PX_PER_CM); }
            else if (tank_crab_hit(t, s_px, s_py)) {  /* the strawberry crab (0.3.9): a tap its card and claws up, the third quick tap sends it running */
                int taps = tank_crab_tap(t, s_px);
                if (taps == 1) { s_sel = s_sel == RENDER_CARD_CRAB ? -1 : RENDER_CARD_CRAB; s_sel_us = now; }
                else if (taps >= 3 && s_sel == RENDER_CARD_CRAB) s_sel = -1;
                if (taps == 0) ESP_LOGI(TAG, "crab tapped: the keeper took its sand dollar (%u so far)", (unsigned)t->cr_found);
                else ESP_LOGI(TAG, "crab tapped (%d): card %s%s", taps, s_sel == RENDER_CARD_CRAB ? "up" : "down", taps >= 3 ? ", running for cover" : ""); }
            else if (tank_octopus_hit(t, s_px, s_py)) {   /* the octopus (0.4.3): its card */
                s_sel = s_sel == RENDER_CARD_OCTOPUS ? -1 : RENDER_CARD_OCTOPUS; s_sel_us = now;
                ESP_LOGI(TAG, "octopus tapped: card %s (%u found)", s_sel >= 0 ? "up" : "down", (unsigned)t->oc_found); }
            else if (tank_seahorse_hit(t, s_px, s_py)) {  /* the seahorse: its card (0.3.6) */
                s_sel = s_sel == RENDER_CARD_SEAHORSE ? -1 : RENDER_CARD_SEAHORSE; s_sel_us = now;
                ESP_LOGI(TAG, "seahorse tapped: card %s (%d hitches, %.1f days)", s_sel >= 0 ? "up" : "down", (int)t->sh_hitches, tank_seahorse_days(t)); }
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
        else if (s_ms && !back && fabsf(dx) >= 40 && fabsf(dx) > 2 * fabsf(dy)) {   /* sideways along the TANK row: its page (2026-09-30) */
            if (render_milestones_swipe(t, s_px, s_py, dx)) ESP_LOGI(TAG, "page swipe %.0f px on the tank row", dx);
        }
        else if (!modal && !back && t->tool == TOOL_HAND && s_py - tank_glass_top(s_px) < 60 && dy >= 40) tank_feed(t, s_lx, 3);  /* drag down from the top = feed (not with a tool in hand,
                                                                  never under a page: 0.3.9.1, a keeper fed the tank from the SETTINGS page - a tap at its top that slid
                                                                  40 px read as this drag, and only the keeper page held it off) */
    }
released:
    render_tap_press_end();
    if (!touched) { s_held_page = false; s_page_gone = false; }
    s_down = touched;
    { int rk = setup_take_crit_renamed();                   /* a critter's rename closed (0.4.2): its card back up - here, past the release's
                                                               setup branch (DONE's own lift clears the card there) */
      if (rk >= 0 && render_critter_here(t, render_card_of_crit_name(rk))) { s_sel = render_card_of_crit_name(rk); s_sel_us = now; open_wait_start(&s_crit_back, now); } }
    if (s_sel >= t->n_fish && s_sel != RENDER_CARD_SNAIL && s_sel != RENDER_CARD_SHRIMP && s_sel != RENDER_CARD_URCHIN && s_sel != RENDER_CARD_SEAHORSE && s_sel != RENDER_CARD_CRAB && s_sel != RENDER_CARD_OCTOPUS
        && !(tank_is_visitor(s_sel) && t->vis_used[s_sel - N_FISH_MAX])) s_sel = -1;   /* fresh tank / save load / a visitor gone home */
    if (s_sel >= 0 && now - s_sel_us > 10 * 1000000) s_sel = -1; /* auto-dismiss */
    if (s_sel != s_card_for) { s_card_for = s_sel; s_card_us = now; }   /* a card's first poll: its stamp (DEPORT's double window) */
    if (s_bat && now - s_bat_us > BATTERY_PAGE_US) s_bat = false; /* the battery page too, after a while */
}

int touch_port_selected(void) { return s_sel; }
void touch_port_select(int fish, int64_t now_us) { s_sel = fish; s_sel_us = now_us; }
bool touch_port_milestones(void) { return s_ms; }
void touch_port_show_milestones(bool on) {   /* (put up: TCH-4's page wait, 0.4.1 audit round 2 R2T-5 - MY TANKS's CLOSE doubled shut
                                                the keeper page shelf_close brought back; its callers: shelf_close, the director) */
    if (s_ms && !on) render_milestones_leave();
    if (on) open_wait_start(&s_pg_wait, esp_timer_get_time());
    s_ms = on;
}
void touch_port_set_quiet(bool on) { s_quiet = on; }
/* (and the GIVE modal, 0.4.1 audit round 1, TCH-7: BOOT twice left it up - set the padlock on MY TANKS, CLOSE, and it stood over
   the locked tank with every tap on its BACK dropped; the birth flow came up under it. Closing it is BACK: an asked gift goes on) */
void touch_port_dismiss(void) { adv_close(); render_give_modal_close(); s_sel = -1; if (s_ms) render_milestones_leave(); if (s_shop) render_shop_leave(); if (s_coll) render_collection_leave(); s_ms = false; s_set = false; s_shop = false; s_coll = false; s_bat = false; s_upd = false; s_shelf = false; }
bool touch_port_updates(void) { return s_upd; }
bool touch_port_page_open(void) { return s_modal_last; }
void touch_port_show_updates(bool on) { s_upd = on; if (on) { updates_page_open(); s_ms = false; s_set = false; s_shop = false; s_coll = false; s_sel = -1; s_bat = false; } }
bool touch_port_shelf(void) { return s_shelf; }
void touch_port_shelf_back(void) { open_wait_start(&s_shelf_wait, esp_timer_get_time()); }   /* MY TANKS's main page is back (R2T-5) */
void touch_port_show_shelf(bool on) { if (on && !s_shelf) open_wait_start(&s_shelf_wait, esp_timer_get_time()); s_shelf = on; s_shelf_tap = false; }
bool touch_port_take_shelf_tap(float *x, float *y, int64_t *press_us) { if (!s_shelf_tap) return false; s_shelf_tap = false; *x = s_shelf_tx; *y = s_shelf_ty; *press_us = s_shelf_press_us; return true; }
int  touch_port_take_update(void) { int r = s_upd_act; s_upd_act = 0; return r; }

/* ---- reset confirm prompt ---- */
void touch_port_confirm_open(void) { touch_port_confirm_open_page(RESET_PG_CHOOSE); }
void touch_port_confirm_open_page(int page) {   /* (a page by name: the director's staging, the selftests) */
    render_reset_open(page);
    s_cf = true; s_cf_us = esp_timer_get_time(); s_cf_ans = 0; open_wait_start(&s_cf_wait, s_cf_us);
    if (s_ms) render_milestones_leave();       /* (as touch_port_dismiss: a card's SELL armed under the prompt was still armed */
    if (s_coll) render_collection_leave();     /*  when the keeper page came back - one tap sold the fish) */
    s_sel = -1; s_ms = false; s_set = false; s_shop = false; s_coll = false; s_bat = false; s_upd = false; render_shop_leave();   /* it replaces the card / the pages */
    s_shelf = false; s_shelf_tap = false; render_give_modal_close();   /* MY TANKS and the GIVE modal too (0.4.1 audit round 1, TCH-3: kept under the
                                                   prompt, after YES the welcome drew over MY TANKS, which went on reading the finger - a 2 s
                                                   press on the welcome's NEXT over REMOVE's HOLD TO SELL removed a shelved tank; main.c closes
                                                   its own MY TANKS first, this is the port's half) */
    ESP_LOGI(TAG, "reset prompt up (page %d: RESET THIS TANK / ERASE EVERYTHING / BACK on the glass; NO by itself in %d s)", render_reset_page(), (int)(CONFIRM_TIMEOUT_US / 1000000));
}
bool touch_port_confirm_answer(int ans) {
    if (!s_cf) return false;
    s_cf = false; s_cf_ans = ans == RESET_ANS_ERASE || ans == RESET_ANS_ERASE_WIFI ? ans : ans > 0 ? RESET_ANS_TANK : RESET_ANS_NO;
    render_reset_close();
    return true;
}
bool  touch_port_confirm_up(void)   { return s_cf; }
float touch_port_confirm_frac(void) {
    if (!s_cf) return 0;
    float f = 1.0f - (esp_timer_get_time() - s_cf_us) / (float)CONFIRM_TIMEOUT_US;
    return f < 0 ? 0 : f;
}
int  touch_port_confirm_take(void)  { int a = s_cf_ans; s_cf_ans = 0; return a; }
bool touch_port_pressed_since(int64_t us) { return s_down && s_press_us > us; }
bool touch_port_settings(void) { return s_set; }
/* (the BOOT chord: it fires only on a press begun after BOOT was held 5 s, so a slider in hand here is the chord's own tap and its
   jump, never a drag - nothing saved: the slider is named, and main.c puts the saved level back. 0.4.1 audit round 2, DR2) */
int touch_port_open_advanced(void) { s_set = false; int v = 0, w = render_settings_cut(&v); (void)v;
    if (w && s_set_what == (w == SET_TAP_BRIGHT ? SET_TAP_BRIGHT_LIVE : SET_TAP_VOLUME_LIVE)) s_set_what = 0;   /* (its preview, not yet taken: dropped) */
    adv_open(); ESP_LOGI(TAG, "ADVANCED page up%s", w ? " (the chord's tap was on a slider: its level goes back)" : "");
    return w; }
int  touch_port_take_adv(void) { int r = s_adv_act; s_adv_act = 0; return r; }
void touch_port_settings_from_shelf(void) { s_set_from_shelf = s_set; }   /* right after touch_port_show_settings(true): its CLOSE returns to MY TANKS */
bool touch_port_take_shelf_return(void) { bool r = s_shelf_return; s_shelf_return = false; return r; }
void touch_port_show_settings(bool on) { s_set = on; s_set_from_shelf = false; s_shelf_return = false; if (on) page_gate_open(&s_page_gate, esp_timer_get_time()); if (on) { render_settings_home(); s_ms = false; s_sel = -1; s_bat = false; } }
/* A SETTING PUSHED OFF THE GLASS (the director's `gfx`, `vignette`) waits in its OWN place, one a kind, the latest value of each (the
   second review of the GRAPHICS page: pushed into the page's slot, `gfx low` and `vignette off` read in one director poll saved only
   the second, and a push could take the place of a slider's release from the touch poll just before it). main takes one setting a
   loop: the glass's first, then these - nothing is overwritten, nothing lost. Two bytes: 0 none, else the value + 1 */
static uint8_t s_push_gfx, s_push_vig;
int  touch_port_take_setting(int *value) {
    int w = s_set_what; *value = s_set_val; s_set_what = 0;
    if (!w && s_push_gfx) { w = SET_TAP_GFX; *value = s_push_gfx - 1; s_push_gfx = 0; }
    else if (!w && s_push_vig) { w = SET_TAP_VIG; *value = s_push_vig - 1; s_push_vig = 0; }
    return w;
}
void touch_port_push_setting(int what, int value) {
    if (what == SET_TAP_GFX) s_push_gfx = (uint8_t)(value + 1);
    else if (what == SET_TAP_VIG) s_push_vig = (uint8_t)((value != 0) + 1);
    else ESP_LOGW(TAG, "a pushed setting of kind %d is not one main saves from here: dropped", what);
}
bool touch_port_shop(void) { return s_shop; }
bool touch_port_collection(void) { return s_coll; }
void touch_port_show_collection(bool on) { if (s_coll && !on) render_collection_leave(); s_coll = on; if (on) { s_ms = false; s_set = false; s_shop = false; s_sel = -1; s_bat = false; s_upd = false; } }
void touch_port_show_shop(bool on) { if (s_shop && !on) render_shop_leave(); s_shop = on; if (on) { s_ms = false; s_set = false; s_sel = -1; s_bat = false; } }
int  touch_port_take_shop(void) { int r = s_shop_act; s_shop_act = 0; return r; }
void touch_port_set_pill(bool up) { s_pill = up; }
bool touch_port_battery(void) { return s_bat; }
void touch_port_show_battery(bool on) { s_bat = on; if (on) { s_bat_us = esp_timer_get_time(); s_sel = -1; } }

/* ---- the dark (touch_port.h; main.c's enter_dark) ----
 * The firmware's alone: the simulator's --selftest-touch-port builds this file on the host (TOUCH_PORT_HOST), where
 * nothing goes dark and its stand-in ESP-IDF has neither esp_lcd_touch_get_data nor the board's INT pin. */
#ifndef TOUCH_PORT_HOST
bool touch_port_finger_now(void) {
    if (!s_tp) return false;
#if CONFIG_POCKET_TANK_WST_320X240
    /* (this fork, 2026-10-08) The Touch-LCD-2's CST816D may stop answering
       I2C while it idles: the CST816 family has an auto-sleep a few seconds
       after the last finger, woken by a touch - unseen on this chip, since
       nobody here has the board. A read it does not answer is no finger,
       but the CST816S driver and each I2C layer under it log an error for
       it ("I2C read failed", "i2c transaction failed", the NACK): a handful
       of lines ten times a second, for as long as the dark lasts. A probe of
       its address first is silent on a NACK (only a stuck bus logs), so a
       dozing chip is passed over quietly, and the read goes ahead only when
       it answers. */
    if (i2c_master_probe(board_i2c_bus(), I2C_ADDR_CST816D, 20) != ESP_OK) return false;
#endif
    if (esp_lcd_touch_read_data(s_tp) != ESP_OK) return false;   /* no answer: no finger */
    esp_lcd_touch_point_data_t point[1];
    uint8_t n = 0;
    return esp_lcd_touch_get_data(s_tp, point, &n, 1) == ESP_OK && n > 0;
}
void touch_port_swallow(void) { s_swallow = true; }
#if CONFIG_POCKET_TANK_320X240
/* The touch controller's INT, PIN_TP_INT in board_pins.h, which the game
 * never uses: it polls. A floating or dead line costs nothing: the pull-up
 * keeps it high, main.c never arms a line that already reads low, and the
 * dark's own reads still find the finger.
 * The CYD: the FT6336's INT on GPIO17. Not yet seen on the bench, and the
 * driver never sets the chip's G_MODE, so whether it holds low through a
 * touch or pulses is the chip's default - a held line is a sure level wake,
 * a pulse a likely one.
 * (this fork, 2026-10-08) The Touch-LCD-2: the CST816D's INT on GPIO46,
 * which pulses low on a touch by the CST816 family's default - a likely
 * wake, as the CYD's pulse would be; unseen here, since nobody here has the
 * board. GPIO46 is one of the ESP32-S3's strapping pins (with GPIO0 it
 * picks the boot mode), and setting it up here is safe: a strap is sampled
 * only at a chip reset, when every pad is back to its reset state, so what
 * the running app does with the pad cannot reach the next one; and this
 * only reads it, with a pull-up towards the level the touch INT idles at -
 * nothing here drives it. Any GPIO can wake the S3 from light sleep, an
 * RTC pin or not (gpio_wakeup_enable), so GPIO46, which is not one, is
 * armed as the CYD's GPIO17 is. */
int touch_port_wake_gpio(void) {
    static bool configured;
    if (!s_tp) return -1;
    if (!configured) {
        gpio_config_t io = { .pin_bit_mask = 1ULL << PIN_TP_INT, .mode = GPIO_MODE_INPUT,
                             .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE };
        configured = gpio_config(&io) == ESP_OK;
        if (!configured) return -1;
    }
    return PIN_TP_INT;
}
#else
int touch_port_wake_gpio(void) { return -1; }    /* the AMOLED's INT is not in use, and display_port_sleep holds its touch in reset */
#endif
#endif  /* TOUCH_PORT_HOST */
