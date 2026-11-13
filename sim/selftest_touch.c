#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_touch.c - --selftest-touch-port (0.4.1): the FIRMWARE's touch port (firmware/main/touch_port_ft3168.c),
 * built on the host against sim/touchhost/host_esp.h, driven by a finger the test puts on the chip - the board's own
 * chip in each world (the 1.8's CST816 through esp_lcd, the bowl's CST9217 and the square's CST9220 read directly, the
 * watch's FT3168), its report going through the board's own calibration (map_touch) like every tap on the glass. The
 * pages, cards and hit tests under it are the sim's common/ code. Polled at 25 fps.
 *
 * What it holds (docs/release-0.4.md, the touch items deferred to 0.4.1):
 *  1. A FIRM PRESS: a still press of 0.12 / 0.8 / 2 s on the keeper page's foot (UPGRADES, the cog, MY TANKS, VISITORS,
 *     CLOSE), the collection's CLOSE, MY TANKS's places, a fish card on the bare tank (the card, NEXT FISH), the battery
 *     pill and a tool chip's DONE: every one acts at its lift, and a long one on the bare tank's buttons calls no fish.
 *     The water keeps its rule: a slow press there is the hold (no tap at its lift). A press main.c took as a hold
 *     (MY TANKS's MOVE, REMOVE done: touch_port_spend_press) is no tap.
 *  2. A SLOW CLOSE: settings' and UPDATES' CLOSE held 0.8 s, then a tap on the page that came back: the tap acts.
 *  3. A DOUBLE TAP ON A PAGE'S WAY IN OR OUT: UPDATES' CLOSE (on the watch over settings' SCREEN), the keeper page's
 *     cog, settings' UPDATES and LINK, double tapped (90 / 100 / 90 ms) and mashed at 5 Hz: the second half reaches
 *     nothing on the page under it; after a 0.7 s pause a tap there acts.
 *  4. MY TANKS OVER LINK MODE: holds anywhere on MY TANKS never reach the hidden link page; MY TANKS sees the finger
 *     and its taps; with MY TANKS closed the link page has the finger again.
 *  5. THE AUDIT'S HOLDS (0.4.1 P8): the reset prompt mashed from its opening, the gift card's ACCEPT / NO THANKS, the
 *     GIVE modal's GIVE and a visitor's DEPORT mashed at 5 Hz past their 2 s holds: nothing acts (a spent tap restarts
 *     the wait, touch_gate.h ONE RULE); after a pause as long as the wait one tap does.
 * PT_TOUCH_LOG=1 prints the port's own log lines. */
#include "host_esp.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef TOUCH_PORT_SRC
#define TOUCH_PORT_SRC "../firmware/main/touch_port_ft3168.c"
#endif
#include TOUCH_PORT_SRC

/* ---- the host's ESP-IDF ---- */
static int64_t g_now = 5000000;
static bool g_log;
int64_t esp_timer_get_time(void) { return g_now; }
void host_log(const char *tag, const char *fmt, ...) {
    if (!g_log) return;
    va_list a; va_start(a, fmt); printf("    [%s] ", tag); vprintf(fmt, a); printf("\n"); va_end(a);
}
struct host_i2c_bus { int unused; };
struct host_i2c_dev { uint16_t addr; };
struct host_tp { int unused; };
struct host_io { int unused; };
static struct host_i2c_bus g_bus; static struct host_i2c_dev g_dev[4]; static int g_ndev;
static struct host_tp g_tp; static struct host_io g_io;
static bool g_down; static int g_rx, g_ry;                 /* the finger, in the chip's own report */
i2c_master_bus_handle_t board_i2c_bus(void) { return &g_bus; }
esp_err_t i2c_master_probe(i2c_master_bus_handle_t b, uint16_t a, int t) { (void)b; (void)a; (void)t; return ESP_OK; }
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t b, const i2c_device_config_t *c, i2c_master_dev_handle_t *d) {
    (void)b; if (g_ndev >= 4) return ESP_FAIL;
    g_dev[g_ndev].addr = c->device_address; *d = &g_dev[g_ndev++]; return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d, const uint8_t *w, size_t n, int t) { (void)d; (void)w; (void)n; (void)t; return ESP_OK; }
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t d, const uint8_t *w, size_t wn, uint8_t *r, size_t rn, int t) {
    (void)w; (void)wn; (void)t;
    memset(r, 0, rn);
    if (d->addr == I2C_ADDR_CST9217 && rn >= 10) {          /* a report: AB in byte 6; a finger = 06 in byte 0's low nibble, a count in 5 */
        r[6] = 0xAB;
        if (g_down) { r[0] = 0x06; r[5] = 1; r[1] = (uint8_t)(g_rx >> 4); r[2] = (uint8_t)(g_ry >> 4); r[3] = (uint8_t)(((g_rx & 15) << 4) | (g_ry & 15)); }
        return ESP_OK;
    }
    if (d->addr == I2C_ADDR_FT3168 && rn >= 5) {            /* TD_STATUS then the first point (event 0 = down) */
        if (g_down) { r[0] = 1; r[1] = (uint8_t)((g_rx >> 8) & 15); r[2] = (uint8_t)g_rx; r[3] = (uint8_t)((g_ry >> 8) & 15); r[4] = (uint8_t)g_ry; }
        return ESP_OK;
    }
    return ESP_FAIL;
}
esp_err_t gpio_config(const gpio_config_t *c) { (void)c; return ESP_OK; }
esp_err_t gpio_sleep_sel_dis(gpio_num_t p) { (void)p; return ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t p, uint32_t l) { (void)p; (void)l; return ESP_OK; }
void vTaskDelay(int ticks) { (void)ticks; }
esp_err_t esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t b, const esp_lcd_panel_io_i2c_config_t *c, esp_lcd_panel_io_handle_t *io) { (void)b; (void)c; *io = &g_io; return ESP_OK; }
esp_err_t esp_lcd_touch_new_i2c_cst816s(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *c, esp_lcd_touch_handle_t *tp) { (void)io; (void)c; *tp = &g_tp; return ESP_OK; }
esp_err_t esp_lcd_touch_new_i2c_ft5x06(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *c, esp_lcd_touch_handle_t *tp) { (void)io; (void)c; *tp = &g_tp; return ESP_OK; }
esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp) { (void)tp; return ESP_OK; }
bool esp_lcd_touch_get_coordinates(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *s, uint8_t *n, uint8_t max) {
    (void)tp; (void)s; (void)max;
    if (!g_down) { *n = 0; return false; }
    x[0] = (uint16_t)g_rx; y[0] = (uint16_t)g_ry; *n = 1; return true;
}
/* ---- the board: this world's ---- */
#if defined(TANK_ROUND)
#define HB_ROUND 1
#elif defined(TANK_WATCH)
#define HB_WATCH 1
#elif defined(TANK_SQUARE)
#define HB_SQUARE 1
#else
#define HB_V2 1                                             /* the 1.8 (V2: CST816), Strato's */
#endif
#ifndef HB_ROUND
#define HB_ROUND 0
#endif
#ifndef HB_WATCH
#define HB_WATCH 0
#endif
#ifndef HB_SQUARE
#define HB_SQUARE 0
#endif
#ifndef HB_V2
#define HB_V2 0
#endif
bool board_is_round(void) { return HB_ROUND; }
bool board_is_watch(void) { return HB_WATCH; }
bool board_is_square(void) { return HB_SQUARE; }
static bool g_v1;                                           /* the 1.8's other panel (V1: FT3168, the bias only): the calibration tests run it too */
bool board_is_v2(void) { return HB_V2 && !g_v1; }
bool board_is_175(void) { return false; }
void display_port_frame_origin(int *px, int *py) { *px = 0; *py = 0; }
void display_port_panel_to_tank(int px, int py, float *tx, float *ty) { *tx = (float)px; *ty = (float)py; }
void audio_port_prewarm(void) { }
/* ---- link mode: the page and who saw the finger ---- */
static bool g_link_page; static int g_link_down, g_link_calls;
bool link_dev_page_up(void) { return g_link_page; }
void link_dev_page_open(tank_t *t) { (void)t; g_link_page = true; }
static int g_link_ui;                                       /* what the real link page (render_link_touch) made of it - read by R2T-3's gate */
int  link_dev_touch(float x, float y, bool down, float dt) {
    g_link_calls++; if (down) g_link_down++;
    static link_view_t v; int r = render_link_touch(&v, x, y, down, dt); if (r) g_link_ui = r;   /* (doors shut: DONE and the arrows) */
    return LINK_UI_NONE;                                    /* (the port still gets nothing: the other cases count calls) */
}
void link_dev_disconnect(void) { }
static int g_sd_sent, g_sd_edge = -1;                     /* the sand dollar dial's SEND, as the port hands it on (0.4.2) */
void link_dev_sd_send(tank_t *t, int edge, int amount) { (void)t; g_sd_sent += amount; g_sd_edge = edge; }
bool link_dev_drift_hit(float x, float y) { (void)x; (void)y; return false; }

/* ---- the finger ---- */
static tank_t T;
static int64_t g_shelf_press;                              /* touch_port_take_shelf_tap's press time (main.c's waits read it) */
#define FRAME_MS 40
static int panel_w(void) { return HB_ROUND ? R_PANEL : HB_SQUARE ? S_PANEL : HB_WATCH ? W_PANEL_W : PANEL_W; }
static int panel_h(void) { return HB_ROUND ? R_PANEL : HB_SQUARE ? S_PANEL : HB_WATCH ? W_PANEL_H : PANEL_H; }
static void chip_from_panel(int px, int py, int *rx, int *ry) {   /* the CST92xx's mirror bits undone (cst9217_read) */
    if (HB_ROUND || HB_SQUARE) {
        int m = s_cst_mirror, pn = cst_panel();
        int a = (m & 1) ? pn - 1 - px : px, b = (m & 2) ? pn - 1 - py : py;
        if (m & 4) { *rx = b; *ry = a; } else { *rx = a; *ry = b; }
    } else { *rx = px; *ry = py; }
}
/* where to put the finger so the board reads (x, y) on the glass - through its calibration and the bias */
static float g_ax, g_ay;                                    /* where it reads, exactly */
static int g_pbx, g_pby;                                    /* ... and the panel px that read it (before the chip's mirror) */
static bool aim(float x, float y) {
    int pw = panel_w(), ph = panel_h(), bx = -1, by = -1; float bd = 1e18f;
    for (int py = 0; py < ph; py += 3) for (int px = 0; px < pw; px += 3) {
        float mx, my; map_touch((uint16_t)px, (uint16_t)py, &mx, &my);
        float d = (mx - x) * (mx - x) + (my - y) * (my - y);
        if (d < bd) { bd = d; bx = px; by = py; }
    }
    for (int py = by - 3; py <= by + 3; py++) for (int px = bx - 3; px <= bx + 3; px++) {
        if (px < 0 || py < 0 || px >= pw || py >= ph) continue;
        float mx, my; map_touch((uint16_t)px, (uint16_t)py, &mx, &my);
        float d = (mx - x) * (mx - x) + (my - y) * (my - y);
        if (d < bd) { bd = d; bx = px; by = py; }
    }
    if (bd > 4 * 4) return false;                           /* the panel cannot read there (past its reach) */
    map_touch((uint16_t)bx, (uint16_t)by, &g_ax, &g_ay);
    chip_from_panel(bx, by, &g_rx, &g_ry); g_pbx = bx; g_pby = by;
    return true;
}
static bool g_draw;                                         /* frames drawn as main.c draws them (the GIVE modal's stamps live in its draw) */
static uint16_t g_fb[TANK_W * TANK_H];
static void frame(void) {
    adv_tick(FRAME_MS / 1000.0f);                           /* (main.c ticks the ADVANCED page every frame: its clocks - DONE's first moment) */
    if (!g_draw) { g_now += FRAME_MS * 1000; T.clock += FRAME_MS / 1000.0; touch_port_poll(&T); return; }
    for (int k = 0; k < 3; k++) {                           /* main.c's tank task: poll, render_frame_begin + poll + the frame's draw, poll */
        g_now += FRAME_MS * 1000 / 3; T.clock += FRAME_MS / 3000.0;
        if (k == 1) render_frame_begin();
        touch_port_poll(&T);
        if (k == 1) { if (touch_port_milestones()) render_milestones(&T, g_fb, TANK_W); else if (!setup_active()) render_give_modal(&T, g_fb, TANK_W); }   /* (main.c: not under setup) */
    }
}
static void hold_for(int ms) { for (int k = 0; k < (ms + FRAME_MS - 1) / FRAME_MS; k++) frame(); }
static void lift(int ms) { g_down = false; hold_for(ms < FRAME_MS ? FRAME_MS : ms); }
static bool press(float x, float y, int ms) { if (!aim(x, y)) return false; g_down = true; hold_for(ms); return true; }
static bool tap(float x, float y, int down_ms, int up_ms) { if (!press(x, y, down_ms)) return false; lift(up_ms); return true; }
static int fails;
#define TFAIL(...) do { printf("FAIL: touch-port: "); printf(__VA_ARGS__); printf("\n"); fails++; } while (0)

/* a page's hit area: the hit point nearest its middle */
typedef int (*hit_fn)(float x, float y, int arg);
static bool find_hit(hit_fn f, int arg, float *ox, float *oy) {
    double sx = 0, sy = 0; int n = 0;
    for (int y = 0; y < TANK_H; y += 2) for (int x = 0; x < TANK_W; x += 2) if (f((float)x, (float)y, arg)) { sx += x; sy += y; n++; }
    if (!n) return false;
    sx /= n; sy /= n; float bd = 1e18f;
    for (int y = 0; y < TANK_H; y += 2) for (int x = 0; x < TANK_W; x += 2) if (f((float)x, (float)y, arg)) {
        float d = (float)((x - sx) * (x - sx) + (y - sy) * (y - sy));
        if (d < bd) { bd = d; *ox = (float)x; *oy = (float)y; }
    }
    return true;
}
/* ... the point of it the panel can read nearest its middle (turned over, the 1.8 V2's reach cuts a foot corner: TOUCH's BACK
   and TURN IT OVER's SKIP stand there alike) */
static bool find_hit_read(hit_fn f, int arg, float *ox, float *oy) {
    float cx, cy; if (!find_hit(f, arg, &cx, &cy)) return false;
    if (aim(cx, cy)) { *ox = cx; *oy = cy; return true; }
    float bd = 1e18f; bool got = false;
    for (int y = 0; y < TANK_H; y += 4) for (int x = 0; x < TANK_W; x += 4) if (f((float)x, (float)y, arg)) {
        float d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
        if (d < bd && aim((float)x, (float)y)) { bd = d; *ox = (float)x; *oy = (float)y; got = true; }
    }
    return got;
}
static int hit_settings(float x, float y, int want) { int v; return render_settings_tap(x, y, &v) == want; }
static int hit_updates(float x, float y, int want) { return updates_page_tap(x, y) == want; }
static int hit_next(float x, float y, int u) { (void)u; return render_next_hit(x, y); }
static int hit_chip(float x, float y, int u) { (void)u; return render_tool_chip_hit(&T, x, y); }
static void foot(int which, float *x, float *y) {
    int bx, by; render_ms_foot_box(which, &bx, &by);
    *x = PAGE_X + bx + MSP_FOOT_SQ / 2.0f; *y = PAGE_Y + by + MSP_FOOT_SQ / 2.0f;
}
static void bare(void) {                                    /* the bare tank, the finger up, every page and card down */
    g_down = false; touch_port_dismiss(); g_link_page = false; touch_port_set_pill(false); tank_set_tool(&T, TOOL_HAND);
    hold_for(800); { int v; touch_port_take_setting(&v); } touch_port_take_update(); touch_port_take_shop();
    float sx, sy; touch_port_take_shelf_tap(&sx, &sy, &g_shelf_press);
    T.hold_active = false;
}
static void card_up(int fish) { s_sel = fish; s_sel_us = g_now; }   /* (the port's own state: a fish's card, as a tap on it brings) */
typedef struct { bool scr, rot, feed; int light, idle, gfx; bool gfxp, vig; } pagestate_t;   /* (gfxp: the GRAPHICS page is the settings page's face - its tile is a door, 0.4.2) */
static pagestate_t pagestate(void) { pagestate_t s = { T.screen_turned, T.orient_lock, T.autofeed_off, tank_light_choice(&T), T.light_idle_s, render_gfx_level(), render_gfx_page(), render_vignette() }; return s; }
static bool same(pagestate_t a, pagestate_t b) { return a.scr == b.scr && a.rot == b.rot && a.feed == b.feed && a.light == b.light && a.idle == b.idle && a.gfx == b.gfx && a.gfxp == b.gfxp && a.vig == b.vig; }
/* AUTO FEED set AGAINST what a tap at (x, y) would pick: a tap that gets through shows (the tiles - SCREEN, ROTATION,
   GRAPHICS - step from whatever they show: any tap that gets through shows) */
static void prime(float x, float y) {
    int v = 0, h = render_settings_tap(x, y, &v);
    if (h == SET_TAP_FEED) T.autofeed_off = v != 0;
}

static const int HOLDS[3] = { 120, 800, 2000 };

/* 0. THE SETTINGS FOOT AND TILES (0.4.2) through the board's own port: each tapped at the middle of where the page draws it
   (render_settings_box) acts at once - UPDATES opens its page, LINK link mode, CLOSE the keeper page, the tile turns its
   setting, GRAPHICS opens its page (the settings page's other face: the port's settings page stays up) - and there QUALITY and
   VIGNETTE are handed to main to save (SET_TAP_GFX, SET_TAP_VIG), BACK is the settings page again, and the settings page opened
   afresh is never the GRAPHICS page left up; the second half of the tap that opened settings
   acts on nothing there (page_gate, W). The jelly tank (its NOW card's SETTINGS, J7) has the same page: CLOSE goes back to the
   jellies, LINK is not for it, the rest the same. */
static int g_jelly_seen;
static int drain_setting(void);
static void jelly_cb(float x, float y, bool down, double now) { (void)x; (void)y; (void)down; (void)now; g_jelly_seen++; }
static void settings_nav(void) {
    static const char *const NAME[SET_BOX_N] = { "UPDATES", "LINK", "CLOSE", TANK_WORN ? "SCREEN" : "ROTATION", "GRAPHICS" };
    int n = 0;
    for (int jelly = 0; jelly < 2; jelly++)
        for (int b = 0; b < SET_BOX_N; b++) {
            bare(); touch_port_set_jelly(jelly ? jelly_cb : NULL); render_set_gfx_level(RENDER_GFX_HIGH);
            touch_port_show_settings(true); hold_for(800);
            int bx, by, bw, bh; render_settings_box(b, &bx, &by, &bw, &bh);
            const float x = PAGE_X + bx + bw / 2.0f, y = PAGE_Y + by + bh / 2.0f;
            pagestate_t s0 = pagestate();
            if (!tap(x, y, 90, FRAME_MS)) { TFAIL("settings' %s at %.0f,%.0f is past the panel's reach", NAME[b], x, y); continue; }
            int v = 0, w = touch_port_take_setting(&v);
            const char *jt = jelly ? " (the jelly tank)" : "";
            if (b == SET_BOX_UPDATES && (!touch_port_updates() || touch_port_settings())) TFAIL("settings' UPDATES did not open the UPDATES page%s", jt);
            if (b == SET_BOX_LINK && (jelly ? (g_link_page || !touch_port_settings()) : (!g_link_page || touch_port_settings())))
                TFAIL("settings' LINK%s: link page %d, settings %d", jt, g_link_page, touch_port_settings());
            if (b == SET_BOX_CLOSE && (touch_port_settings() || touch_port_milestones() == (bool)jelly)) TFAIL("settings' CLOSE%s: settings %d, keeper page %d", jt, touch_port_settings(), touch_port_milestones());
            if (b == SET_BOX_TILE && (!touch_port_settings() || (TANK_WORN ? T.screen_turned == s0.scr : (T.orient_lock == s0.rot || w != SET_TAP_ROTATE))))
                TFAIL("settings' %s%s did not turn (setting %d)", NAME[b], jt, w);
            if (b == SET_BOX_GFX) {                                          /* THE DOOR, and the page behind it through the port */
                if (!touch_port_settings() || !render_gfx_page() || w || render_gfx_level() != RENDER_GFX_HIGH || !render_vignette())
                    TFAIL("settings' GRAPHICS%s: settings %d, its page %d, setting %d/%d, level %d (want the GRAPHICS page, nothing changed)", jt, touch_port_settings(), render_gfx_page(), w, v, render_gfx_level());
                static const struct { int box, what, val; const char *n; } GP[4] = { { GFXP_BOX_Q_LOW, SET_TAP_GFX, RENDER_GFX_LOW, "QUALITY LOW" }, { GFXP_BOX_V_OFF, SET_TAP_VIG, 0, "VIGNETTE OFF" },
                    { GFXP_BOX_Q_MEDIUM, SET_TAP_GFX, RENDER_GFX_MEDIUM, "QUALITY MEDIUM" }, { GFXP_BOX_V_ON, SET_TAP_VIG, 1, "VIGNETTE ON" } };
                for (int k = 0; k < 4; k++) {
                    int gx, gy, gw, gh; render_gfx_page_box(GP[k].box, &gx, &gy, &gw, &gh);
                    hold_for(800);
                    if (!tap(PAGE_X + gx + gw / 2.0f, PAGE_Y + gy + gh / 2.0f, 90, FRAME_MS)) { TFAIL("the GRAPHICS page's %s is past the panel's reach", GP[k].n); continue; }
                    w = touch_port_take_setting(&v);
                    if (!touch_port_settings() || !render_gfx_page() || w != GP[k].what || v != GP[k].val
                        || (GP[k].what == SET_TAP_GFX ? render_gfx_level() != GP[k].val : render_vignette() != (GP[k].val != 0)))
                        TFAIL("the GRAPHICS page's %s%s: setting %d/%d (want %d/%d, handed to main), level %d, vignette %d", GP[k].n, jt, w, v, GP[k].what, GP[k].val, render_gfx_level(), render_vignette());
                }
                {   int gx, gy, gw, gh; render_gfx_page_box(GFXP_BOX_BACK, &gx, &gy, &gw, &gh);
                    hold_for(800);
                    const bool hit = tap(PAGE_X + gx + gw / 2.0f, PAGE_Y + gy + gh / 2.0f, 90, FRAME_MS);
                    w = touch_port_take_setting(&v);
                    if (!hit || !touch_port_settings() || render_gfx_page() || touch_port_milestones() || touch_port_updates() || g_link_page || w)
                        TFAIL("the GRAPHICS page's BACK%s: hit %d, settings %d, its page still up %d, keeper page %d, link %d, setting %d (want the settings page)", jt, hit,
                              touch_port_settings(), render_gfx_page(), touch_port_milestones(), g_link_page, w);
                    /* ... and BACK's double (0.15 s on: LINK stands under it on the settings page) answers nothing */
                    hold_for(60); tap(PAGE_X + gx + gw / 2.0f, PAGE_Y + gy + gh / 2.0f, 60, FRAME_MS); w = touch_port_take_setting(&v);
                    if (!touch_port_settings() || render_gfx_page() || g_link_page || touch_port_updates() || w) TFAIL("the GRAPHICS page's BACK%s: its double answered on the settings page (link %d, setting %d)", jt, g_link_page, w);
                }
                /* left up and the settings page opened again: the settings page itself */
                render_gfx_page_show(true); bare(); touch_port_set_jelly(jelly ? jelly_cb : NULL); touch_port_show_settings(true);
                if (render_gfx_page()) TFAIL("the settings page opened%s on the GRAPHICS page left up", jt);
                render_set_vignette(true);
            }
            if (b == SET_BOX_TILE) { T.screen_turned = s0.scr; tank_orient_lock(&T, s0.rot); }
            /* the second half of the tap that opened settings (THE TOUCH RULE's W: a press within 0.2 s of the page appearing)
               answers nothing on it - as the jelly's NOW card SETTINGS or the cog opens it at a lift */
            bare(); touch_port_set_jelly(jelly ? jelly_cb : NULL); render_set_gfx_level(RENDER_GFX_HIGH);
            touch_port_show_settings(true); s0 = pagestate();
            if (!tap(x, y, 90, FRAME_MS)) continue;
            w = touch_port_take_setting(&v);
            if (!touch_port_settings() || touch_port_updates() || g_link_page || w || !same(s0, pagestate()))
                TFAIL("settings' %s%s answered the opening tap's second half (setting %d)", NAME[b], jt, w);
            T.screen_turned = s0.scr; tank_orient_lock(&T, s0.rot);
            n++;
        }
    touch_port_set_jelly(NULL); render_set_gfx_level(RENDER_GFX_HIGH); render_set_vignette(true); render_settings_home(); render_debug_gfx_idle(); bare();
    {   /* a setting changed off the glass (the director's `vignette off`, `gfx low`) reaches main as the page's own tap does: one save */
        int v = 7, w;
        touch_port_push_setting(SET_TAP_VIG, 0); w = touch_port_take_setting(&v);
        if (w != SET_TAP_VIG || v != 0 || touch_port_take_setting(&v)) TFAIL("a pushed VIGNETTE OFF reached main as %d/%d (want %d/0, once)", w, v, SET_TAP_VIG);
        touch_port_push_setting(SET_TAP_GFX, RENDER_GFX_LOW); w = touch_port_take_setting(&v);
        if (w != SET_TAP_GFX || v != RENDER_GFX_LOW) TFAIL("a pushed QUALITY LOW reached main as %d/%d", w, v);
        /* TWO IN ONE POLL (the director reads `gfx low` and `vignette off` in one call; main takes one setting a loop): both reach main */
        touch_port_push_setting(SET_TAP_GFX, RENDER_GFX_LOW); touch_port_push_setting(SET_TAP_VIG, 0);
        int v2 = 7, w2; w = touch_port_take_setting(&v); w2 = touch_port_take_setting(&v2);
        if (w != SET_TAP_GFX || v != RENDER_GFX_LOW || w2 != SET_TAP_VIG || v2 != 0 || touch_port_take_setting(&v))
            TFAIL("two settings pushed in one poll reached main as %d/%d then %d/%d (want %d/1 then %d/0, then nothing)", w, v, w2, v2, SET_TAP_GFX, SET_TAP_VIG);
        touch_port_push_setting(SET_TAP_VIG, 0); touch_port_push_setting(SET_TAP_VIG, 1); w = touch_port_take_setting(&v);
        if (w != SET_TAP_VIG || v != 1 || touch_port_take_setting(&v)) TFAIL("a setting pushed twice: %d/%d (want its latest value, once)", w, v);
        /* ... and a push never takes the place of what the glass just answered (a tap's or a slider's release from the touch poll before it) */
        float fx = -1, fy = -1;
        for (int y = 0; y < TANK_H && fx < 0; y += 4) for (int x = 0; x < TANK_W && fx < 0; x += 4) { int u; if (render_settings_tap((float)x, (float)y, &u) == SET_TAP_FEED && u == 0) { fx = (float)x + 6; fy = (float)y + 6; } }
        bare(); touch_port_show_settings(true); hold_for(800); touch_port_take_setting(&v); T.autofeed_off = false;
        if (fx < 0 || !tap(fx, fy, 90, FRAME_MS)) TFAIL("AUTO FEED OFF is nowhere the panel reads");
        else {
            touch_port_push_setting(SET_TAP_VIG, 0);
            w = touch_port_take_setting(&v); w2 = touch_port_take_setting(&v2);
            if (w != SET_TAP_FEED || w2 != SET_TAP_VIG || v2 != 0) TFAIL("a setting pushed after the glass answered: main got %d/%d then %d/%d (want AUTO FEED's %d first, then %d/0)", w, v, w2, v2, SET_TAP_FEED, SET_TAP_VIG);
        }
        T.autofeed_off = false; bare();
    }
    /* EACH FOOT BUTTON'S WHOLE REACH against the page it opens (the review of 0.4.2: UPDATES answered 6 px above its top,
       under the UPDATES page's FORGET NETWORK - tap, tap, tap there opened the page, armed SURE? and forgot the Wi-Fi):
       every point where a foot button answers, on the page that button brings up, reaches nothing that is hard to undo -
       UPDATES' page: FORGET, CHECK FOR UPDATES (a restart); link mode: DISCONNECT, a door's CLOSE, an ARM; the keeper
       page under CLOSE: a fish's SELL. (A Wi-Fi saved for it, in a file of its own) */
    {   char wf[640]; const char *sv = getenv("POCKET_TANK_SAVE"), *was = getenv("POCKET_TANK_WIFI"); char keep[640] = "";
        if (was) snprintf(keep, sizeof keep, "%s", was);
        snprintf(wf, sizeof wf, "%s.navtest.wifi", sv && sv[0] ? sv : "/tmp/pt-touch-nav");
        setenv("POCKET_TANK_WIFI", wf, 1); net_port_creds_set("TANKNET", "fishfood");
        link_view_t lv; memset(&lv, 0, sizeof lv); lv.edge[LINK_LEFT].state = LV_OPEN;   /* (a door open: DISCONNECT is up) */
        int swept[3] = { 0, 0, 0 }, bad = 0;
        for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) {
            int v, h = render_settings_tap((float)x, (float)y, &v), under = 0;
            const char *what = NULL;
            if (h == SET_TAP_UPDATES) { swept[0]++; under = updates_page_tap((float)x, (float)y);
                if (under >= UPD_TAP_FORGET) what = "the UPDATES page's FORGET (a saved network's)"; else if (under == UPD_TAP_CHECK) what = "the UPDATES page's CHECK FOR UPDATES"; }
            else if (h == SET_TAP_LINK) { swept[1]++; render_link_leave();
                under = render_link_touch(&lv, (float)x, (float)y, true, 0.03f); if (!under) under = render_link_touch(&lv, (float)x, (float)y, false, 0.03f);
                render_link_leave(); if (under && under != LINK_UI_DONE) what = "link mode's DISCONNECT / a door"; }
            else if (h == SET_TAP_CLOSE) { swept[2]++; under = render_milestones_tap(&T, (float)x, (float)y); render_milestones_leave();
                if (under >= MS_TAP_SELL) what = "the keeper page's SELL"; }
            if (what && bad++ < 6) TFAIL("settings' foot at frame %d,%d (hit %d) lies over %s (%d)", x, y, h, what, under);
        }
        net_port_creds_forget();
        if (keep[0]) setenv("POCKET_TANK_WIFI", keep, 1); else unsetenv("POCKET_TANK_WIFI");
        if (!swept[0] || !swept[1] || !swept[2]) TFAIL("the foot's reach: a button answers nowhere (%d %d %d px)", swept[0], swept[1], swept[2]);
        printf("selftest-touch-port: the foot's whole reach - UPDATES %d px, LINK %d, CLOSE %d: none over FORGET, CHECK, DISCONNECT, a door or SELL%s\n",
               swept[0], swept[1], swept[2], bad ? " (FAILED)" : "");
    }
#if TANK_WORN
    /* THE PAGE TURNED UNDER THE FINGER, on the watch's own port: SCREEN tapped, the picture turns (main.c feeds the port the
       quarter each frame); the double's second half on the same spot of the glass - now VOLUME or LIGHTS OUT's end of the
       page - acts on nothing; after a pause the same tap is the page's */
    {   int bx, by, bw, bh, pts = 0, ok = 0, later = 0; render_settings_box(SET_BOX_TILE, &bx, &by, &bw, &bh);
        for (int py = by + 6; py < by + bh - 4; py += 14) for (int px = bx + 8; px < bx + bw - 4; px += 30) {
            const float x = PAGE_X + px, y = PAGE_Y + py, mx = TANK_W - 1 - x, my = TANK_H - 1 - y;
            int u; if (render_settings_tap(mx, my, &u) == SET_TAP_NONE) continue;
            bare(); T.screen_turned = false; touch_port_set_quarter(0); tank_light_choice_set(&T, 3); touch_port_show_settings(true); hold_for(800);   /* (LIGHTS OUT mid-list) */
            if (!tap(x, y, 90, 1)) continue;
            touch_port_set_quarter(T.screen_turned ? 2 : 0);              /* (main.c, the next frame) */
            pts++;
            pagestate_t s0 = pagestate(); drain_setting();
            lift(100); tap(mx, my, 90, FRAME_MS);                        /* the same spot of the glass: the frame's other end now */
            int w = drain_setting();
            if (!T.screen_turned) TFAIL("the watch: SCREEN tapped at %d,%d did not turn the picture", px, py);
            else if (w || !same(s0, pagestate())) TFAIL("the watch: SCREEN at %d,%d double tapped - the second half, on the turned page, answered (setting %d)", px, py, w);
            else ok++;
            lift(700); s0 = pagestate(); tap(mx, my, 90, FRAME_MS); w = drain_setting();
            if (w || !same(s0, pagestate())) later++;
            T.screen_turned = false; touch_port_set_quarter(0); tank_light_choice_set(&T, 0);
        }
        if (!pts || ok != pts || later != pts) TFAIL("the watch's turned page: %d spots, %d doubles spent, %d taps answered after a pause", pts, ok, later);
        printf("selftest-touch-port: the watch's SCREEN double tapped at %d spots: the second half on the turned page answered nothing; after 0.7 s it did\n", pts);
        bare();
    }
#endif
    printf("selftest-touch-port: the settings foot and tiles - %d (UPDATES, LINK, CLOSE, %s, GRAPHICS; the fish tank and the jelly tank's): "
           "each acted at its tap where it is drawn, none for the opening tap's second half\n", n, TANK_WORN ? "SCREEN" : "ROTATION");
}

/* 1. A FIRM PRESS */
static void firm_press(void) {
    int checked = 0;
    static const struct { int box; const char *name; } FOOT[] = { { MSF_UPG, "UPGRADES" }, { MSF_SET, "the cog" }, { MSF_TANKS, "MY TANKS" }, { MSF_VIS, "VISITORS" }, { MSF_CLOSE, "CLOSE" } };
    for (int f = 0; f < 5; f++)
        for (int h = 0; h < 3; h++) {
            bare(); touch_port_show_milestones(true); hold_for(800);
            float x, y; foot(FOOT[f].box, &x, &y);
            if (!press(x, y, HOLDS[h])) { TFAIL("the keeper page's %s at %.0f,%.0f is past the panel's reach", FOOT[f].name, x, y); continue; }
            lift(FRAME_MS);
            int v, w = touch_port_take_setting(&v);
            bool ok = FOOT[f].box == MSF_UPG ? touch_port_shop() : FOOT[f].box == MSF_SET ? touch_port_settings() : FOOT[f].box == MSF_TANKS ? w == SET_TAP_TANKS
                    : FOOT[f].box == MSF_VIS ? touch_port_collection() : !touch_port_milestones();
            if (!ok) TFAIL("the keeper page's %s pressed %d ms did nothing", FOOT[f].name, HOLDS[h]);
            checked++;
        }
    for (int h = 0; h < 3; h++) {                            /* the collection's CLOSE */
        bare(); touch_port_show_collection(true); hold_for(800);
        float x = PAGE_X + MSP_CLOSE_X + MSP_CLOSE_W / 2.0f, y = PAGE_Y + MSP_CLOSE_Y + MSP_CLOSE_H / 2.0f;
        if (!press(x, y, HOLDS[h])) { TFAIL("the collection's CLOSE is past the panel's reach"); continue; }
        lift(FRAME_MS);
        if (touch_port_collection() || !touch_port_milestones()) TFAIL("the collection's CLOSE pressed %d ms did nothing", HOLDS[h]);
        checked++;
    }
    /* MY TANKS: its places (main.c reads the tap) - and a press main.c took as a hold is none */
    for (int h = 0; h < 3; h++) {
        bare(); touch_port_show_shelf(true); hold_for(800);
        const float x = TANK_W / 2.0f, y = TANK_H / 2.0f; float sx, sy;
        if (!press(x, y, HOLDS[h])) { TFAIL("MY TANKS: the middle is past the panel's reach"); continue; }
        lift(FRAME_MS);
        if (!touch_port_take_shelf_tap(&sx, &sy, &g_shelf_press) || fabsf(sx - g_ax) > 1 || fabsf(sy - g_ay) > 1) TFAIL("MY TANKS: a press of %d ms was no tap", HOLDS[h]);
        if (!press(x, y, HOLDS[h] / 2)) continue;
        touch_port_spend_press(); hold_for(HOLDS[h] / 2); lift(FRAME_MS);
        if (touch_port_take_shelf_tap(&sx, &sy, &g_shelf_press)) TFAIL("MY TANKS: a press main.c took as a hold (MOVE, REMOVE) still tapped at its lift (%d ms)", HOLDS[h]);
        press(x, y, 100); lift(FRAME_MS);
        if (!touch_port_take_shelf_tap(&sx, &sy, &g_shelf_press)) TFAIL("MY TANKS: the press after a spent one was no tap");
        checked += 2;
    }
    /* the bare tank's buttons: a fish card itself (-> the keeper page), NEXT FISH, the pill, a tool chip's DONE */
    while (T.n_fish < 3) if (tank_add_fish(&T, 0, 1) < 0) break;
    for (int i = 0; i < T.n_fish; i++) { T.fish[i].x = TANK_W * 0.85f; T.fish[i].y = TANK_H * 0.8f; }
    float nx = 0, ny = 0, cx = 0, cy = 0;
    bool next_ok = (bare(), card_up(0), find_hit(hit_next, 0, &nx, &ny));
    tank_set_tool(&T, TOOL_SPONGE); bool chip_ok = find_hit(hit_chip, 0, &cx, &cy); tank_set_tool(&T, TOOL_HAND);
    if (!next_ok) TFAIL("no NEXT FISH arrow beside a card");
    if (!chip_ok) TFAIL("no tool chip with the sponge in hand");
    for (int h = 0; h < 3; h++) {
        bare(); card_up(0);
        const float kx = RENDER_CARD_X + RENDER_CARD_W / 2.0f, ky = RENDER_CARD_Y + RENDER_CARD_H / 3.0f;
        if (!press(kx, ky, HOLDS[h])) { TFAIL("the card is past the panel's reach"); continue; }
        bool called = T.hold_active; lift(FRAME_MS);
        if (!touch_port_milestones()) TFAIL("a fish card pressed %d ms did not open the keeper page", HOLDS[h]);
        if (called) TFAIL("a press of %d ms on a fish card called the fish (a button's press is no hold)", HOLDS[h]);
        checked++;
        if (next_ok) {
            bare(); card_up(0);
            if (press(nx, ny, HOLDS[h])) { bool c2 = T.hold_active; lift(FRAME_MS);
                if (touch_port_selected() == 0) TFAIL("NEXT FISH pressed %d ms did not step the card", HOLDS[h]);
                if (c2) TFAIL("NEXT FISH pressed %d ms called the fish", HOLDS[h]);
                checked++; }
            else TFAIL("NEXT FISH is past the panel's reach");
        }
        bare(); touch_port_set_pill(true);
        if (press(RENDER_BAT_X + RENDER_BAT_W / 2.0f, RENDER_BAT_Y + RENDER_BAT_H / 2.0f, HOLDS[h])) { lift(FRAME_MS);
            if (!touch_port_battery()) TFAIL("the battery pill pressed %d ms did not open its page", HOLDS[h]);
            checked++; }
        else TFAIL("the battery pill is past the panel's reach");
        if (chip_ok) {
            bare(); tank_set_tool(&T, TOOL_SPONGE);
            if (press(cx, cy, HOLDS[h])) { lift(FRAME_MS);
                if (T.tool != TOOL_HAND) TFAIL("the tool chip's DONE pressed %d ms kept the sponge", HOLDS[h]);
                checked++; }
            else TFAIL("the tool chip is past the panel's reach");
        }
    }
    /* the water keeps the quick rule: a quick tap is a tap; a slow press is the hold - no tap at its lift (a card stays) */
    const float wx = TANK_W * 0.55f, wy = TANK_H * 0.45f;
    bare(); T.tap_count = 0;
    if (!tap(wx, wy, 120, FRAME_MS)) TFAIL("the water at %.0f,%.0f is past the panel's reach", wx, wy);
    else if (T.tap_count != 1) TFAIL("a quick tap on the water was no tap (%d) - the test's spot is not open water", T.tap_count);
    for (int h = 1; h < 3; h++) {
        bare(); T.tap_count = 0;
        press(wx, wy, HOLDS[h]); bool held = T.hold_active; lift(FRAME_MS);
        if (!held) TFAIL("a still press of %d ms on the water called no fish (the hold)", HOLDS[h]);
        if (T.tap_count) TFAIL("a still press of %d ms on the water tapped the glass at its lift", HOLDS[h]);
        bare(); card_up(1);
        press(wx, wy, HOLDS[h]); lift(FRAME_MS);
        if (touch_port_selected() != 1) TFAIL("a still press of %d ms on the water closed a fish card (a hold, not a tap)", HOLDS[h]);
        checked += 2;
    }
    printf("selftest-touch-port: a firm press - %d presses (0.12 / 0.8 / 2 s: the keeper page's foot, the collection's CLOSE, MY TANKS, a card, NEXT FISH, the pill, "
           "a tool's DONE) acted at their lift; on the water a slow press stays the hold\n", checked);
}

/* 2. A SLOW CLOSE, then a tap on the page that came back */
static void slow_close(void) {
    float cx, cy, ux, uy, sx, sy;
    if (!find_hit(hit_settings, SET_TAP_CLOSE, &cx, &cy) || !find_hit(hit_updates, UPD_TAP_CLOSE, &ux, &uy)) { TFAIL("no CLOSE on settings / UPDATES"); return; }
    foot(MSF_SET, &sx, &sy);
    int n = 0;
    for (int h = 0; h < 3; h++) {
        bare(); touch_port_show_settings(true); hold_for(800);
        press(cx, cy, HOLDS[h]); lift(FRAME_MS);
        if (!touch_port_milestones() || touch_port_settings()) { TFAIL("settings' CLOSE pressed %d ms did not bring the keeper page back", HOLDS[h]); continue; }
        lift(700);
        tap(sx, sy, 120, FRAME_MS);
        if (!touch_port_settings()) TFAIL("settings' CLOSE pressed %d ms, then a tap on the keeper page's cog: the tap was eaten", HOLDS[h]);
        bare(); touch_port_show_updates(true); hold_for(800);
        press(ux, uy, HOLDS[h]); lift(FRAME_MS);
        if (!touch_port_settings() || touch_port_updates()) { TFAIL("UPDATES' CLOSE pressed %d ms did not bring settings back", HOLDS[h]); continue; }
        lift(700);
        tap(cx, cy, 120, FRAME_MS);
        if (touch_port_settings() || !touch_port_milestones()) TFAIL("UPDATES' CLOSE pressed %d ms, then a tap on settings' CLOSE: the tap was eaten", HOLDS[h]);
        n += 2;
    }
    printf("selftest-touch-port: a slow close - settings' and UPDATES' CLOSE held 0.12 / 0.8 / 2 s, the next tap acted (%d)\n", n);
}

/* 3. A DOUBLE TAP (and a mash) on a page's way in or out: the second half reaches nothing on the page it opened */
static int drain_setting(void) { int v; return touch_port_take_setting(&v); }
static void double_taps(void) {
    float ux, uy, cx, cy;
    if (!find_hit(hit_updates, UPD_TAP_CLOSE, &ux, &uy) || !find_hit(hit_settings, SET_TAP_CLOSE, &cx, &cy)) { TFAIL("no CLOSE on UPDATES / settings"); return; }
    int pts = 0, over = 0, screen = 0;
    static const int MASH[1] = { 2 };   /* (doubles only: under THE TOUCH RULE, touch_gate.h, a mash's later taps are deliberate) */
    for (int y = 0; y < TANK_H; y += 8) for (int x = 0; x < TANK_W; x += 8) {
        if (updates_page_tap((float)x, (float)y) != UPD_TAP_CLOSE) continue;
        if (!aim((float)x, (float)y) || updates_page_tap(g_ax, g_ay) != UPD_TAP_CLOSE) continue;   /* (where the panel reads it) */
        int v, under = render_settings_tap(g_ax, g_ay, &v);
        if (under != SET_TAP_NONE) over++;
        if (under == SET_TAP_SCREEN) screen++;
        for (int m = 0; m < 1; m++) {
            bare(); touch_port_show_updates(true); hold_for(800);
            prime(g_ax, g_ay); pagestate_t s0 = pagestate();
            for (int k = 0; k < MASH[m]; k++) { tap((float)x, (float)y, 90, k == MASH[m] - 1 ? FRAME_MS : 100); }
            int w = drain_setting();
            if (!touch_port_settings() || !same(s0, pagestate()) || w) {
                TFAIL("UPDATES' CLOSE at %d,%d tapped %d times at 5 Hz: the taps after the first reached settings (%s, setting %d, under it: %d)",
                      x, y, MASH[m], touch_port_settings() ? "still up" : "closed", w, under); break; }
            pts++;
        }
        /* a 0.7 s pause, then the tap is settings' own */
        lift(700); prime(g_ax, g_ay); pagestate_t s1 = pagestate();
        tap((float)x, (float)y, 90, FRAME_MS);
        int w = drain_setting();
        bool acted = !same(s1, pagestate()) || w || !touch_port_settings() || touch_port_updates() || g_link_page;
        if (under != SET_TAP_NONE && !acted) TFAIL("settings at %d,%d (hit %d) did not answer a tap 0.7 s after UPDATES' CLOSE", x, y, under);
    }
    if (!pts) TFAIL("no point of UPDATES' CLOSE could be tapped");
    /* (0.4.1's bug: on the watch UPDATES' CLOSE stood over settings' SCREEN, and its double turned the picture. Since
       0.4.2 SCREEN is a tile up the page and nothing there stands under UPDATES' CLOSE; the double must still land on
       some settings control on every board - the foot's - or this no longer tests anything) */
    if (!over) TFAIL("no point of UPDATES' CLOSE over a settings control - the double tap test no longer covers anything");
    /* the keeper page's cog, settings' UPDATES and LINK: double tapped, the page they open answers nothing */
    float gx, gy, upx, upy, lx, ly; foot(MSF_SET, &gx, &gy);
    bool have_up = find_hit(hit_settings, SET_TAP_UPDATES, &upx, &upy), have_link = find_hit(hit_settings, SET_TAP_LINK, &lx, &ly);
    int ways = 0;
    for (int m = 0; m < 1; m++) {
        bare(); touch_port_show_milestones(true); hold_for(800);
        prime(gx, gy); pagestate_t s0 = pagestate();
        for (int k = 0; k < MASH[m]; k++) tap(gx, gy, 90, k == MASH[m] - 1 ? FRAME_MS : 100);
        if (!touch_port_settings() || !same(s0, pagestate()) || drain_setting()) TFAIL("the keeper page's cog tapped %d times: settings answered the taps after the first", MASH[m]);
        lift(700); tap(cx, cy, 90, FRAME_MS);
        if (touch_port_settings()) TFAIL("the keeper page's cog tapped %d times, a pause, settings' CLOSE: not answered", MASH[m]);
        ways++;
        if (have_up) {
            bare(); touch_port_show_settings(true); hold_for(800);
            for (int k = 0; k < MASH[m]; k++) tap(upx, upy, 90, k == MASH[m] - 1 ? FRAME_MS : 100);
            if (!touch_port_updates() || touch_port_take_update()) TFAIL("settings' UPDATES tapped %d times: the UPDATES page answered the taps after the first (or closed)", MASH[m]);
            lift(700); tap(ux, uy, 90, FRAME_MS);
            if (touch_port_updates()) TFAIL("settings' UPDATES tapped %d times, a pause, UPDATES' CLOSE: not answered", MASH[m]);
            ways++;
        }
        if (have_link) {
            bare(); touch_port_show_settings(true); hold_for(800);
            tap(lx, ly, 90, 100); g_link_down = 0;
            for (int k = 1; k < MASH[m]; k++) tap(lx, ly, 90, k == MASH[m] - 1 ? FRAME_MS : 100);
            if (!g_link_page) TFAIL("settings' LINK did not open link mode");
            else if (g_link_down) TFAIL("settings' LINK tapped %d times: link mode saw the finger of the taps after the first", MASH[m]);
            lift(700); press(lx, ly, 200);
            if (!g_link_down) TFAIL("settings' LINK tapped %d times, a pause: link mode never saw the next press", MASH[m]);
            lift(FRAME_MS);
            ways++;
        }
    }
    if (!have_up || !have_link) TFAIL("settings has no UPDATES / LINK button");
    printf("selftest-touch-port: double taps and mashes - UPDATES' CLOSE at %d points x 3 (%d over a settings button, %d over SCREEN), the cog, UPDATES, LINK (%d): "
           "nothing reached the page under them, each answered after a 0.7 s pause\n", pts / 3, over, screen, ways);
}

/* 4. MY TANKS OVER LINK MODE */
static void link_holds(void) {
    int n = 0;
    for (int y = TANK_H / 8; y < TANK_H; y += TANK_H / 4) for (int x = TANK_W / 8; x < TANK_W; x += TANK_W / 4) {
        bare(); g_link_page = true; touch_port_show_shelf(true); hold_for(800);
        g_link_down = 0;
        if (!press((float)x, (float)y, 2000)) continue;
        float fx, fy; bool seen = touch_port_finger(&fx, &fy);
        lift(FRAME_MS);
        if (g_link_down) TFAIL("MY TANKS over link mode: a 2 s hold at %d,%d reached the hidden link page (%d polls)", x, y, g_link_down);
        if (!seen || fabsf(fx - g_ax) > 1 || fabsf(fy - g_ay) > 1) TFAIL("MY TANKS over link mode: the hold at %d,%d was not MY TANKS's finger", x, y);
        float sx, sy;
        tap((float)x, (float)y, 120, FRAME_MS);
        if (!touch_port_take_shelf_tap(&sx, &sy, &g_shelf_press)) TFAIL("MY TANKS over link mode: a tap at %d,%d did not reach MY TANKS", x, y);
        if (g_link_down) TFAIL("MY TANKS over link mode: a tap at %d,%d reached the hidden link page", x, y);
        n++;
    }
    bare(); g_link_page = true; hold_for(800); g_link_down = 0;   /* MY TANKS closed: link mode has the finger again */
    press(TANK_W / 2.0f, TANK_H / 2.0f, 400); lift(FRAME_MS);
    if (!g_link_down) TFAIL("link mode with MY TANKS closed: the page never saw the finger");
    if (!n) TFAIL("MY TANKS over link mode: no point could be held");
    printf("selftest-touch-port: MY TANKS over link mode - %d spots held 2 s and tapped: every one MY TANKS's, the hidden link page saw none\n", n);
}

/* 5. THE AUDIT'S TAP HOLES (0.4.1 P8, docs/audit-0.4.1/T1.md F5a-c, F5e): a kid's mash at 5 Hz (80 ms down, 120 up),
   longer than the hold it meets, never acts; a pause as long as the hold, then one tap, does */
static int hit_confirm(float x, float y, int want) { return render_confirm_hit(x, y) == want; }
static int hit_gift(float x, float y, int want) { return render_gift_hit(x, y) == want; }
static int hit_passport(float x, float y, int want) { return render_passport_hit(x, y) == want; }
#define MASH_N 14                                            /* 14 taps at 5 Hz: 2.8 s, past every 2 s hold */
static void mash(float x, float y, int n) { for (int k = 0; k < n; k++) tap(x, y, 80, 120); }
static void mashed_holds(void) {   /* (P8's mashes, under THE TOUCH RULE: touch_gate.h, 11-06) */
    float x, y; int n = 0;
    /* the reset prompt: a press from before it opened, and its double's second half, are no answer; a tap 0.3 s on answers */
    if (!find_hit(hit_confirm, 1, &x, &y)) TFAIL("the reset prompt has no YES");
    else {
        bare(); touch_port_confirm_open_page(RESET_PG_TANK); hold_for(60);   /* (the chord's tap lifted as it opened) */
        tap(x, y, 80, FRAME_MS);
        int a = touch_port_confirm_take();
        if (a || !touch_port_confirm_up()) TFAIL("the reset prompt: YES 0.1 s after it opened answered (%d) - the tank would reset", a);
        lift(300); tap(x, y, 80, FRAME_MS);
        if ((a = touch_port_confirm_take()) != 1) TFAIL("the reset prompt: YES 0.3 s on did not answer (%d)", a);
        touch_port_confirm_answer(-1); touch_port_confirm_take(); n++;
    }
    static gift_book_t G; memset(&G, 0, sizeof G); gift_attach(&T, &G);
    while (T.n_fish < 3) if (tank_add_fish(&T, 0, 1) < 0) break;
    for (int i = 0; i < T.n_fish; i++) T.fish[i].stage = STAGE_ADULT;
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    /* the gift card: it comes by itself - no hold: ACCEPT / NO THANKS tapped at once answer (a press from before it: card_prepress) */
    for (int b = 0; b < 2; b++) {
        if (!find_hit(hit_gift, b ? GIFT_HIT_NO : GIFT_HIT_ACCEPT, &x, &y)) { TFAIL("the gift card has no %s", b ? "NO THANKS" : "ACCEPT"); continue; }
        bare(); G.ask.on = true; G.ask.uid = 0x77u; G.ask.answer = 0; G.ask.shown = true; G.ask.seen = T.clock; snprintf(G.ask.pp.name, sizeof G.ask.pp.name, "MOSS");
        tap(x, y, 80, FRAME_MS);
        if (G.ask.answer != (b ? -1 : 1)) TFAIL("the gift card: %s tapped as it came did not answer (%d) - no hold since THE TOUCH RULE", b ? "NO THANKS" : "ACCEPT", G.ask.answer);
        G.ask.on = false; G.ask.answer = 0; n++;
    }
    /* the GIVE modal: GIVE as the double's second half of the tap that opened it - nobody asked; 0.3 s on, GIVE asks */
    T.door_open[LINK_LEFT] = true; G.door_peer[LINK_LEFT] = 0x49439648u; G.door_badge[LINK_LEFT] = 1;
    int f0 = -1; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 < 0) TFAIL("no fish to give");
    else {
        bare(); render_give_tap(&T, f0);
        int gx = 0, gy = 0;
        if (render_give_modal_step(&T) != RENDER_GM_CONFIRM || !render_give_modal_button(&T, RENDER_GM_TAP_GIVE, &gx, &gy)) TFAIL("the GIVE modal did not come up on CONFIRM (step %d)", render_give_modal_step(&T));
        else {
            hold_for(100); tap((float)gx, (float)gy, 80, FRAME_MS);
            if (G.want_uid) TFAIL("the GIVE modal: GIVE 0.1 s after it opened (the opening tap's double) asked the other tank");
            lift(300); tap((float)gx, (float)gy, 80, FRAME_MS);
            if (G.want_uid != T.fish[f0].uid) TFAIL("the GIVE modal: GIVE 0.3 s on did not ask (step %d)", render_give_modal_step(&T));
            n++;
        }
        render_give_modal_close(); G.want_uid = 0; G.want_door = 0; G.want_peer = 0;
    }
    T.door_open[LINK_LEFT] = false; memset(&G, 0, sizeof G);
    /* a visitor's passport: DEPORT as the double of the tap that opened it - kept; 0.3 s on, home it goes */
    const int v = N_FISH_MAX;
    T.fish[v] = T.fish[0]; T.fish[v].uid = 0x5151u; T.fish[v].homeward = T.fish[v].deported = false; T.vis_used[0] = true;
    snprintf(T.fish[v].name, sizeof T.fish[v].name, "DRIFT");
    if (!find_hit(hit_passport, PASSPORT_HIT_DEPORT, &x, &y)) TFAIL("a visitor's passport has no DEPORT");
    else {
        bare(); card_up(v); frame();
        tap(x, y, 80, FRAME_MS);
        if (T.fish[v].deported) TFAIL("a visitor's passport: DEPORT 0.1 s after it came (the opening tap's double) sent it home");
        if (s_sel != v) TFAIL("a visitor's passport: the card went down under the double");
        lift(300); tap(x, y, 80, FRAME_MS);
        if (!T.fish[v].deported) TFAIL("a visitor's passport: DEPORT 0.3 s on did not send it home");
        n++;
    }
    T.vis_used[0] = false; memset(&T.fish[v], 0, sizeof T.fish[v]); T.deport_uid = 0; s_sel = -1;
    gift_attach(&T, NULL);
    printf("selftest-touch-port: the old holds under THE TOUCH RULE - %d (the reset prompt, GIVE, DEPORT: the opening tap's double spent, a tap 0.3 s "
           "on acts; the gift card: no hold, a tap as it came answers)\n", n);
}

/* ---- 6. A FIRM SECOND PRESS (0.4.1 audit round 1, TCH-1 / TCH-9: every wait is read at the PRESS) ----
 * The second half of a double tap pressed slowly: it lands inside the wait and lifts after it. Read at the lift it acted -
 * the keeper card's SELL sold the fish, the shop's row put its UNLOCK under the finger and bought, a shelved piece's SELL
 * sold it, MY TANKS's NEW TANK (300 SD) and SWAP IN; the 2 s holds (the gift card, the GIVE modal, DEPORT) answered a press
 * begun inside them and held past. Each here: never acts, at any length; and a tap after the pause still does. */
#define FIRM_GAP 100                                         /* the second press, 100 ms after the first tap lifted */
static const int FIRM_HOLDS[5] = { 100, 300, 500, 700, 900 };
static void firm_tap_then_press(float x, float y, int hold) { tap(x, y, 90, FIRM_GAP); press(x, y, hold); lift(FRAME_MS); }
static int shop_row_under_button(float *ox, float *oy) {     /* a shop row whose band stands under the one-button modal's UNLOCK */
    const int bx = SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2, by = SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H;
    const float x = PAGE_X + bx + MSP_HOW_W / 2.0f;
    for (int j = SHP_PER_PAGE - 1; j >= 0; j--) { const int top = SHP_ROW_Y0 + j * SHP_ROW_DY;
        for (int yy = top - 8; yy < top + SHP_ROW_DY - 8; yy++) {
            if (!(yy >= by - MSP_HOW_SLOP_UP && yy < by + MSP_HOW_H + MSP_HOW_SLOP_DN)) continue;
            bool on_foot = false;
            for (int b = 0; b < SHF_N && !on_foot; b++) { int fx, fy; render_shop_foot_box(b, &fx, &fy);
                on_foot = !(x - PAGE_X < fx - 6 || x - PAGE_X >= fx + MSP_FOOT_SQ + 6 || yy < fy - 8 || yy >= fy + MSP_FOOT_SQ + 12); }
            if (!on_foot && aim(x, (float)(PAGE_Y + yy + 4))) { *ox = x; *oy = (float)(PAGE_Y + yy + 4); return j; }
        } }
    return -1;
}
static void firm_seconds(void) {
    int n = 0;
    while (T.n_fish < 4) if (tank_add_fish(&T, 0, 1) < 0) break;
    for (int i = 0; i < T.n_fish; i++) T.fish[i].stage = STAGE_ADULT;
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    /* the keeper card's SELL: tap (SELL arms: +N OK?), then the firm second press - the fish stays; 0.7 s on, a tap sells */
    for (int h = 0; h < 5; h++) {
        bare(); touch_port_show_milestones(true); hold_for(800); render_milestones_show_fish(&T, 2);
        int fish, sx, by; if (!render_milestones_card(&T, &fish, NULL, &sx, &by)) { TFAIL("firm second press: no fish card on the keeper page"); break; }
        const int n0 = T.n_fish;
        firm_tap_then_press((float)sx, (float)by, FIRM_HOLDS[h]);
        if (T.n_fish != n0) { TFAIL("firm second press: the keeper card's SELL tapped, then pressed again %d ms on for %d ms - the fish was SOLD", FIRM_GAP, FIRM_HOLDS[h]); break; }
        if (h == 4) { lift(700); tap((float)sx, (float)by, 90, FRAME_MS);
                      if (T.n_fish != n0 - 1) TFAIL("firm second press: the keeper card's SELL 0.7 s after the firm press did not sell"); }
        n++;
    }
    while (T.n_fish < 4) if (tank_add_fish(&T, 0, 1) < 0) break;
    for (int i = 0; i < T.n_fish; i++) T.fish[i].stage = STAGE_ADULT;
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    /* the shop: a row tapped opens its modal, UNLOCK stands where the row was - the firm second press buys nothing */
    float rx, ry; int row;
    bare(); touch_port_show_shop(true); hold_for(200); render_shop_leave();
    if ((row = shop_row_under_button(&rx, &ry)) < 0) printf("selftest-touch-port: (no shop row under the modal's UNLOCK on this board)\n");
    else for (int h = 0; h < 5; h++) {
        bare(); T.sd_balance = 100000; T.sd_unlocks = 0; T.sd_shelved = 0; touch_port_show_shop(true); hold_for(800);
        firm_tap_then_press(rx, ry, FIRM_HOLDS[h]);
        int r = touch_port_take_shop();
        if (r >= SHOP_TAP_BUY) { TFAIL("firm second press: shop row %d at %.0f,%.0f tapped, then pressed again %d ms on for %d ms - act %d (UNLOCK)", row, rx, ry, FIRM_GAP, FIRM_HOLDS[h], r); break; }
        if (h == 4) { lift(700); tap(rx, ry, 90, FRAME_MS); r = touch_port_take_shop();
                      if (!(r >= SHOP_TAP_BUY && r < SHOP_TAP_MOVE)) TFAIL("firm second press: UNLOCK 0.7 s after the firm press did not buy (%d)", r); }
        n++;
    }
    /* a shelved piece's SELL (the two-button modal): SELL tapped (+N OK?), then the firm second press - not sold */
    int pi = -1; for (int i = 0; i < SD_ITEM_COUNT && pi < 0; i++) if (tank_decor_placeable(i)) pi = i;
    if (pi < 0) TFAIL("firm second press: no placeable piece");
    else for (int h = 0; h < 5; h++) {
        bare(); T.sd_unlocks = SD_ITEMS[pi].bit; T.sd_shelved = SD_ITEMS[pi].bit; touch_port_show_shop(true); hold_for(200);
        int a, pg, r0; render_shop_leave();
        if (!render_shop_find(&T, pi, &a, &pg, &r0)) { TFAIL("firm second press: the shelved %s is not in the shop", SD_ITEMS[pi].name); break; }
        { int fx, fy; render_shop_foot_box(SHF_DECOR + a, &fx, &fy); render_shop_tap(&T, PAGE_X + fx + MSP_FOOT_SQ / 2.0f, PAGE_Y + fy + MSP_FOOT_SQ / 2.0f); }
        for (int k = 0; k < pg; k++) render_shop_tap(&T, PAGE_X + SHP_ARROW_X1 + SHP_ARROW_W / 2.0f, PAGE_Y + SHP_ROW_Y0 - 20.0f);
        const float x0 = PAGE_X + 100.0f, y0 = PAGE_Y + SHP_ROW_Y0 + r0 * SHP_ROW_DY + 20.0f;   /* the row's name: its modal */
        const float sx = PAGE_X + SHP_TWO_X1 + MSP_HOW_W / 2.0f, sy = PAGE_Y + SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H / 2.0f;
        tap(x0, y0, 90, 700);
        firm_tap_then_press(sx, sy, FIRM_HOLDS[h]);
        int r = touch_port_take_shop();
        if (r == SHOP_TAP_SELL + pi) { TFAIL("firm second press: the shelved %s's SELL tapped, then pressed again %d ms on for %d ms - SOLD", SD_ITEMS[pi].name, FIRM_GAP, FIRM_HOLDS[h]); break; }
        if (h == 4) { lift(700); tap(sx, sy, 90, FRAME_MS); r = touch_port_take_shop();
                      if (r != SHOP_TAP_SELL + pi) TFAIL("firm second press: the shelved piece's SELL 0.7 s after the firm press did not sell (%d)", r); }
        n++;
    }
    render_shop_leave(); T.sd_unlocks = 0; T.sd_shelved = 0;
    /* MY TANKS (main.c's waits: THE LIBRARY's NEW TANK, a card's SWAP IN): the port names when the tap's press BEGAN, and the
       wait read there spends a firm second press that main.c, reading its own clock at the take, let through */
    {   float px = -1, py = -1;
        for (int y = 0; y < TANK_H && px < 0; y += 2) for (int x = 0; x < TANK_W && px < 0; x += 2) if (render_shelf_tap((float)x, (float)y) >= SHELF_TAP_SLOT0 && aim((float)x, (float)y)) { px = (float)x; py = (float)y; }
        if (px < 0) TFAIL("firm second press: MY TANKS has no place");
        else for (int h = 0; h < 5; h++) {
            bare(); touch_port_show_shelf(true); hold_for(800);  /* (past MY TANKS's own open wait, TCH-5) */
            float sx, sy; tap(px, py, 90, FIRM_GAP);
            if (!touch_port_take_shelf_tap(&sx, &sy, &g_shelf_press)) { TFAIL("firm second press: MY TANKS took no tap"); break; }
            open_wait_t lib; open_wait_start(&lib, g_now);    /* main.c: THE LIBRARY (or a card) opens on this tap */
            const int64_t at_press = g_now + FRAME_MS * 1000;
            press(px, py, FIRM_HOLDS[h]); lift(FRAME_MS);
            if (!touch_port_take_shelf_tap(&sx, &sy, &g_shelf_press)) { TFAIL("firm second press: MY TANKS took no second tap (%d ms)", FIRM_HOLDS[h]); break; }
            if (g_shelf_press > at_press || g_shelf_press < at_press - FRAME_MS * 1000) TFAIL("firm second press: MY TANKS's tap says it was pressed %lld ms before its take, not when it began (%d ms held)", (long long)(g_now - g_shelf_press) / 1000, FIRM_HOLDS[h]);
            if (!open_wait_spent_at(&lib, g_shelf_press, g_now)) TFAIL("firm second press: THE LIBRARY's wait read at the press let a %d ms press %d ms on through (NEW TANK)", FIRM_HOLDS[h], FIRM_GAP);
            n++;
        }
    }
    /* the 2 s holds (TCH-9): a press begun inside one and held past it is spent - the gift card, the GIVE modal, DEPORT */
    static gift_book_t G; memset(&G, 0, sizeof G); gift_attach(&T, &G);
    float x, y;
    for (int b = 0; b < 2; b++) {
        if (!find_hit(hit_gift, b ? GIFT_HIT_NO : GIFT_HIT_ACCEPT, &x, &y)) { TFAIL("the gift card has no %s", b ? "NO THANKS" : "ACCEPT"); continue; }
        bare(); G.ask.on = true; G.ask.uid = 0x77u; G.ask.answer = 0; G.ask.shown = true; G.ask.seen = T.clock; snprintf(G.ask.pp.name, sizeof G.ask.pp.name, "MOSS");
        hold_for(100); press(x, y, (int)(GIFT_HOLD_S * 1000) + 100); lift(FRAME_MS);
        if (G.ask.answer != (b ? -1 : 1)) TFAIL("firm press: the gift card's %s pressed 0.1 s after it came (no hold since THE TOUCH RULE) did not answer (%d)", b ? "NO THANKS" : "ACCEPT", G.ask.answer);
        G.ask.on = false; G.ask.answer = 0; n++;
    }
    T.door_open[LINK_LEFT] = true; G.door_peer[LINK_LEFT] = 0x49439648u; G.door_badge[LINK_LEFT] = 1;
    int f0 = -1; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 < 0) TFAIL("no fish to give");
    else {
        bare(); render_give_tap(&T, f0);
        int gx = 0, gy = 0;
        if (render_give_modal_step(&T) != RENDER_GM_CONFIRM || !render_give_modal_button(&T, RENDER_GM_TAP_GIVE, &gx, &gy)) TFAIL("the GIVE modal did not come up on CONFIRM");
        else {
            hold_for(100); press((float)gx, (float)gy, (int)(GIFT_HOLD_S * 1000) + 100); lift(FRAME_MS);
            if (G.want_uid) TFAIL("firm press: the GIVE modal's GIVE pressed 0.1 s after it came and held %.1f s asked the other tank", GIFT_HOLD_S + 0.1f);
            lift((int)(GIFT_HOLD_S * 1000) + 100); tap((float)gx, (float)gy, 90, FRAME_MS);
            if (G.want_uid != T.fish[f0].uid) TFAIL("firm press: GIVE after its pause did not ask (step %d)", render_give_modal_step(&T));
            n++;
        }
        render_give_modal_close(); G.want_uid = 0; G.want_door = 0; G.want_peer = 0;
    }
    T.door_open[LINK_LEFT] = false; memset(&G, 0, sizeof G); gift_attach(&T, NULL);
    const int v = N_FISH_MAX;
    T.fish[v] = T.fish[0]; T.fish[v].uid = 0x5151u; T.fish[v].homeward = T.fish[v].deported = false; T.vis_used[0] = true;
    snprintf(T.fish[v].name, sizeof T.fish[v].name, "DRIFT");
    if (!find_hit(hit_passport, PASSPORT_HIT_DEPORT, &x, &y)) TFAIL("a visitor's passport has no DEPORT");
    else {
        bare(); card_up(v); frame(); hold_for(100);
        press(x, y, 2000); lift(FRAME_MS);
        if (T.fish[v].deported) TFAIL("firm press: DEPORT pressed 0.1 s after the passport came (its opening tap's double) and held 2 s sent the visitor home");
        lift((int)(GIFT_HOLD_S * 1000) + 100); tap(x, y, 90, FRAME_MS);
        if (!T.fish[v].deported) TFAIL("firm press: DEPORT after its pause did not send it home");
        n++;
    }
    T.vis_used[0] = false; memset(&T.fish[v], 0, sizeof T.fish[v]); T.deport_uid = 0; s_sel = -1;
    /* the reset prompt: YES pressed 0.1 s after it opened (the chord tap's double), held 2 s - no answer */
    if (find_hit(hit_confirm, 1, &x, &y)) {
        bare(); touch_port_confirm_open_page(RESET_PG_TANK); hold_for(100); press(x, y, 2000); lift(FRAME_MS);
        int a = touch_port_confirm_take();
        if (a) TFAIL("firm press: the reset prompt's YES pressed 0.1 s after it opened and held 2 s answered (%d)", a);
        lift(700); tap(x, y, 90, FRAME_MS);
        if ((a = touch_port_confirm_take()) != 1) TFAIL("firm press: YES after its pause did not answer (%d)", a);
        touch_port_confirm_answer(-1); touch_port_confirm_take(); n++;
    }
    printf("selftest-touch-port: a firm second press (TCH-1/TCH-9) - %d cases (the keeper card's SELL, the shop's UNLOCK and a shelved "
           "piece's SELL, a tap then a press %d ms on held 100-900 ms; MY TANKS's tap names its press; the gift card, the GIVE modal, "
           "DEPORT and the reset prompt pressed inside their wait and held past it): none acted, each acted after its pause\n", n, FIRM_GAP);
}

/* ---- 7. A PRESS FROM BEFORE THE GIFT CARD (0.4.1 audit round 1, TCH-2) ----
 * A finger resting on the water (the fish come round it) where ACCEPT or NO THANKS will stand; a friend's GIVE pops the card
 * under it; held 2.4 s more, then lifted: it answered for the keeper. Over MY TANKS the same. Only a press begun after the card
 * came answers it: this one is spent, and a tap after the hold's pause still answers. */
static void card_prepress(void) {
    static gift_book_t G; memset(&G, 0, sizeof G); gift_attach(&T, &G);
    int n = 0; float x, y;
    for (int over = 0; over < 2; over++) for (int b = 0; b < 2; b++) {
        const char *bn = b ? "NO THANKS" : "ACCEPT", *on = over ? "over MY TANKS" : "over the tank";
        if (!find_hit(hit_gift, b ? GIFT_HIT_NO : GIFT_HIT_ACCEPT, &x, &y)) { TFAIL("the gift card has no %s", bn); continue; }
        bare(); G.ask.on = false; G.ask.answer = 0;
        if (over) { touch_port_show_shelf(true); hold_for(200); }
        if (!press(x, y, 1000)) { TFAIL("a press before the card: %s cannot be reached", bn); continue; }
        G.ask.on = true; G.ask.uid = 0x77u; G.ask.answer = 0; G.ask.shown = true; G.ask.seen = T.clock; snprintf(G.ask.pp.name, sizeof G.ask.pp.name, "MOSS");
        hold_for(2400); lift(FRAME_MS);
        if (G.ask.answer) TFAIL("a press before the card (%s): a finger resting on %s 1 s before the gift card came, held 2.4 s more, answered it (%d)", on, bn, G.ask.answer);
        { float sx, sy; if (touch_port_take_shelf_tap(&sx, &sy, &g_shelf_press)) TFAIL("a press before the card (%s): its lift tapped MY TANKS under the card", on); }
        lift((int)(GIFT_HOLD_S * 1000) + 100); tap(x, y, 90, FRAME_MS);
        if (G.ask.answer != (b ? -1 : 1)) TFAIL("a press before the card (%s): %s after the hold's pause did not answer (%d)", on, bn, G.ask.answer);
        G.ask.on = false; G.ask.answer = 0; n++;
    }
    memset(&G, 0, sizeof G); gift_attach(&T, NULL);
    printf("selftest-touch-port: a press from before the gift card (TCH-2) - %d (ACCEPT / NO THANKS, over the tank and over MY TANKS, "
           "held 1 s before it came and 2.4 s after): none answered, each answered by a tap after the pause\n", n);
}

/* ---- 8. A PAGE'S WAY IN AND OUT, DOUBLED (0.4.1 audit round 1, TCH-4) ----
 * Settings' CLOSE spends its double's second half; the shop's X and the collection's CLOSE did not - the second tap shut the
 * keeper page under them. A doubled VISITORS opened and shut the collection; a doubled UPGRADES opened the shop's HOW TO EARN.
 * Each doubled (90 ms down, 100 up, 90 down) and mashed: the page it brought stays; 0.7 s on, a tap there acts. */
static void dbl(float x, float y) { tap(x, y, 90, 100); tap(x, y, 90, FRAME_MS); }
static void page_doubles(void) {
    int n = 0, bx, by; float x, y, kx, ky;
    foot(MSF_CLOSE, &kx, &ky);
    for (int mashed = 0; mashed < 1; mashed++) {   /* (doubles only since THE TOUCH RULE) */
        const char *how = mashed ? "mashed (4 taps)" : "double tapped";
        /* the shop's X -> the keeper page */
        render_shop_foot_box(SHF_CLOSE, &bx, &by); x = PAGE_X + bx + MSP_FOOT_SQ / 2.0f; y = PAGE_Y + by + MSP_FOOT_SQ / 2.0f;
        bare(); touch_port_show_shop(true); hold_for(800);
        if (mashed) mash(x, y, 4); else dbl(x, y);
        if (touch_port_shop() || !touch_port_milestones()) TFAIL("a page doubled: the shop's X %s - shop %d, keeper page %d (the keeper page shut too)", how, touch_port_shop(), touch_port_milestones());
        lift(700); tap(kx, ky, 90, FRAME_MS);
        if (touch_port_milestones()) TFAIL("a page doubled: the keeper page's CLOSE 0.7 s after the shop's X %s did not close it", how);
        n++;
        /* the collection's CLOSE -> the keeper page */
        x = PAGE_X + MSP_CLOSE_X + MSP_CLOSE_W / 2.0f; y = PAGE_Y + MSP_CLOSE_Y + MSP_CLOSE_H / 2.0f;
        bare(); touch_port_show_collection(true); hold_for(800);
        if (mashed) mash(x, y, 4); else dbl(x, y);
        if (touch_port_collection() || !touch_port_milestones()) TFAIL("a page doubled: the collection's CLOSE %s - collection %d, keeper page %d", how, touch_port_collection(), touch_port_milestones());
        n++;
        /* VISITORS -> the collection (its middle, its right side) */
        for (int part = 0; part < 2; part++) {
            render_ms_foot_box(MSF_VIS, &bx, &by); x = PAGE_X + bx + (part ? MSP_FOOT_SQ - 6 : MSP_FOOT_SQ / 2.0f); y = PAGE_Y + by + MSP_FOOT_SQ / 2.0f;
            bare(); touch_port_show_milestones(true); hold_for(800);
            if (mashed) mash(x, y, 4); else dbl(x, y);
            if (!touch_port_collection()) TFAIL("a page doubled: VISITORS (%s) %s - the collection opened and shut again (keeper page %d)", part ? "right side" : "middle", how, touch_port_milestones());
            n++;
        }
        /* UPGRADES -> the shop (its HOW TO EARN, "?", under it) */
        foot(MSF_UPG, &x, &y);
        bare(); touch_port_show_milestones(true); hold_for(800);
        if (mashed) mash(x, y, 4); else dbl(x, y);
        if (!touch_port_shop() || render_shop_modal_up()) TFAIL("a page doubled: UPGRADES %s - shop %d, a modal (HOW TO EARN) %d", how, touch_port_shop(), render_shop_modal_up());
        render_shop_leave(); n++;
    }
    printf("selftest-touch-port: a page's way in and out doubled and mashed (TCH-4) - %d (the shop's X, the collection's CLOSE, VISITORS "
           "at its middle and right side, UPGRADES): the page each brought stayed, its next tap after 0.7 s acted\n", n);
}

/* ---- 9. THE RESET PROMPT OVER MY TANKS (0.4.1 audit round 1, TCH-3) ----
 * BOOT held 5 s + a tap on MY TANKS: the prompt came up over it and MY TANKS stayed open under it; after YES the welcome drew
 * over MY TANKS, which went on reading the finger (REMOVE's HOLD TO SELL under the welcome's NEXT). The prompt now takes the
 * glass alone: MY TANKS and the GIVE modal close as it opens (main.c closes its own MY TANKS first: not host-built). */
static void prompt_over_shelf(void) {
    int n = 0;
    bare(); touch_port_show_shelf(true); hold_for(200);
    touch_port_confirm_open(); frame();
    if (touch_port_shelf()) TFAIL("the reset prompt over MY TANKS: MY TANKS stayed open under it");
    touch_port_confirm_answer(1); touch_port_confirm_take();
    setup_begin(&T);                                         /* main.c reset_tank: the welcome */
    press(TANK_W / 2.0f, TANK_H * 0.75f, 2200); lift(FRAME_MS);
    { float sx, sy; if (touch_port_shelf() || touch_port_take_shelf_tap(&sx, &sy, &g_shelf_press)) TFAIL("the reset prompt over MY TANKS: after YES, MY TANKS (%d) took a 2.2 s press under the welcome", touch_port_shelf()); }
    setup_cancel(&T); touch_port_dismiss(); n++;
    /* the GIVE modal over the tank: the prompt closes it */
    static gift_book_t G; memset(&G, 0, sizeof G); gift_attach(&T, &G);
    while (T.n_fish < 3) if (tank_add_fish(&T, 0, 1) < 0) break;
    for (int i = 0; i < T.n_fish; i++) T.fish[i].stage = STAGE_ADULT;
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    T.door_open[LINK_LEFT] = true; G.door_peer[LINK_LEFT] = 0x49439648u; G.door_badge[LINK_LEFT] = 1;
    int f0 = -1; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 >= 0) {
        bare(); render_give_tap(&T, f0); frame();
        if (render_give_modal_step(&T) == RENDER_GM_OFF) TFAIL("the reset prompt: no GIVE modal to put it over");
        touch_port_confirm_open(); frame();
        if (render_give_modal_step(&T) != RENDER_GM_OFF) TFAIL("the reset prompt: the GIVE modal stayed up under it (step %d)", render_give_modal_step(&T));
        touch_port_confirm_answer(-1); touch_port_confirm_take(); n++;
    }
    render_give_modal_close(); T.door_open[LINK_LEFT] = false; memset(&G, 0, sizeof G); gift_attach(&T, NULL);
    printf("selftest-touch-port: the reset prompt over MY TANKS and the GIVE modal (TCH-3) - %d: both closed as it opened; after YES "
           "nothing under the welcome took a press\n", n);
}

/* ---- 10. A DOUBLED MY TANKS (0.4.1 audit round 1, TCH-5) ----
 * The keeper foot's MY TANKS doubled: its second tap landed on MY TANKS's own page (CLOSE on the watch, THE LIBRARY on the
 * bowl, a place on the 1.8) - the page had no open wait. main.c's half as the board runs it: the foot's tap is SET_TAP_TANKS,
 * shelf_toggle dismisses the pages and MY TANKS shows on the next frame. Its double, or a mash, is no MY TANKS tap; 0.7 s on one is. */
static void shelf_double(void) {
    int n = 0; float x, y; foot(MSF_TANKS, &x, &y);
    for (int taps = 1; taps <= 1; taps += 3) {   /* (the double only since THE TOUCH RULE) */
        bare(); touch_port_show_milestones(true); hold_for(800);
        tap(x, y, 90, FRAME_MS);
        int v = 0, w = touch_port_take_setting(&v);
        if (w != SET_TAP_TANKS) { TFAIL("a doubled MY TANKS: the keeper foot's MY TANKS gave %d, not SET_TAP_TANKS", w); return; }
        touch_port_dismiss(); hold_for(FRAME_MS); touch_port_show_shelf(true);   /* main.c: shelf_toggle, then the next tank frame */
        hold_for(60);
        for (int k = 0; k < taps; k++) tap(x, y, 90, k + 1 < taps ? 120 : FRAME_MS);
        float sx, sy;
        if (touch_port_take_shelf_tap(&sx, &sy, &g_shelf_press)) TFAIL("a doubled MY TANKS: %d more tap%s after the keeper foot's MY TANKS reached MY TANKS's own page at %.0f,%.0f", taps, taps > 1 ? "s" : "", sx, sy);
        lift(700); tap(x, y, 90, FRAME_MS);
        if (!touch_port_take_shelf_tap(&sx, &sy, &g_shelf_press)) TFAIL("a doubled MY TANKS: a tap 0.7 s on was no MY TANKS tap");
        n++;
    }
    touch_port_show_shelf(false);
    printf("selftest-touch-port: a doubled MY TANKS (TCH-5) - %d (doubled, mashed): MY TANKS's own page took neither; a tap 0.7 s on it took\n", n);
}

/* ---- 11. THE TOUCH LOCK AND THE GIFT CARD OVER MY TANKS (0.4.1 audit round 1, TCH-6) ----
 * The lock lets MY TANKS through (its padlock is the way back), and the gift card's branch came first: the card was answered
 * over MY TANKS under the lock, and not over the tank. Now it waits out its time under the lock there too; unlocked, it answers. */
static void lock_card(void) {
    static gift_book_t G; memset(&G, 0, sizeof G); gift_attach(&T, &G);
    float x, y; int n = 0;
    if (!find_hit(hit_gift, GIFT_HIT_ACCEPT, &x, &y)) TFAIL("the gift card has no ACCEPT");
    else for (int over = 0; over < 2; over++) {
        bare(); touch_port_set_touch_lock(true);
        if (over) { touch_port_show_shelf(true); hold_for(800); }
        G.ask.on = true; G.ask.uid = 0x77u; G.ask.answer = 0; G.ask.shown = true; G.ask.seen = T.clock; snprintf(G.ask.pp.name, sizeof G.ask.pp.name, "MOSS");
        hold_for((int)(GIFT_HOLD_S * 1000) + 200); tap(x, y, 90, FRAME_MS);
        if (G.ask.answer) TFAIL("the touch lock: the gift card's ACCEPT was answered %s (%d)", over ? "over MY TANKS" : "over the tank", G.ask.answer);
        touch_port_set_touch_lock(false); hold_for((int)(GIFT_HOLD_S * 1000) + 200); tap(x, y, 90, FRAME_MS);
        if (G.ask.answer != 1) TFAIL("the touch lock: unlocked, ACCEPT %s did not answer (%d)", over ? "over MY TANKS" : "over the tank", G.ask.answer);
        G.ask.on = false; G.ask.answer = 0; n++;
    }
    touch_port_set_touch_lock(false); memset(&G, 0, sizeof G); gift_attach(&T, NULL);
    printf("selftest-touch-port: the touch lock and the gift card (TCH-6) - %d (over the tank, over MY TANKS): no answer while locked, "
           "an answer once unlocked\n", n);
}

/* ---- 12. THE GIVE MODAL AND BOOT TWICE (0.4.1 audit round 1, TCH-7) ----
 * The GIVE modal over the tank, then BOOT twice (MY TANKS: touch_port_dismiss), the padlock, CLOSE: the modal was back over the
 * locked tank and the lock dropped every tap on its BACK. BOOT twice closes it with the other pages now. */
static void give_dismiss(void) {
    static gift_book_t G; memset(&G, 0, sizeof G); gift_attach(&T, &G);
    while (T.n_fish < 3) if (tank_add_fish(&T, 0, 1) < 0) break;
    for (int i = 0; i < T.n_fish; i++) T.fish[i].stage = STAGE_ADULT;
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    T.door_open[LINK_LEFT] = true; G.door_peer[LINK_LEFT] = 0x49439648u; G.door_badge[LINK_LEFT] = 1;
    int f0 = -1; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 < 0) TFAIL("no fish to give");
    else {
        bare(); render_give_tap(&T, f0); hold_for(200);
        if (render_give_modal_step(&T) == RENDER_GM_OFF) TFAIL("the GIVE modal did not come up");
        touch_port_dismiss();                                 /* BOOT twice: shelf_toggle */
        touch_port_show_shelf(true); hold_for(800); touch_port_set_touch_lock(true); touch_port_show_shelf(false); hold_for(200);
        if (render_give_modal_step(&T) != RENDER_GM_OFF) TFAIL("the GIVE modal: BOOT twice, the padlock, CLOSE - it stood over the locked tank (step %d)", render_give_modal_step(&T));
        touch_port_set_touch_lock(false);
    }
    render_give_modal_close(); T.door_open[LINK_LEFT] = false; memset(&G, 0, sizeof G); gift_attach(&T, NULL);
    printf("selftest-touch-port: the GIVE modal and BOOT twice (TCH-7): closed with the other pages, never left over the locked tank\n");
}

/* ---- 13. A NOTICE UNDER THE GIVE MODAL (0.4.1 audit round 1, TCH-8) ----
 * The modal is drawn over an announcement, but the notice was tested first in the release: BACK closed the notice and the modal
 * stayed. The modal's taps are its own now; the notice is still there for the tap after. */
static void notice_under_give(void) {
    static gift_book_t G; memset(&G, 0, sizeof G); gift_attach(&T, &G);
    T.door_open[LINK_LEFT] = true; G.door_peer[LINK_LEFT] = 0x49439648u; G.door_badge[LINK_LEFT] = 1;
    int f0 = -1; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 < 0) TFAIL("no fish to give");
    else {
        bare(); render_give_tap(&T, f0); hold_for(2200);
        notice_sync(&T); notice_low_battery(); for (int k = 0; k < 40 && !notice_current(); k++) notice_tick(&T, 0.1f, false);
        for (int k = 0; k < 30; k++) notice_tick(&T, 0.1f, false);   /* (past its own 2 s hold) */
        int gx, gy; render_give_modal_button(&T, RENDER_GM_TAP_BACK, &gx, &gy);
        if (!notice_current()) TFAIL("a notice under the GIVE modal: no notice came up");
        else {
            tap((float)gx, (float)gy, 90, FRAME_MS);
            if (render_give_modal_step(&T) != RENDER_GM_OFF) TFAIL("a notice under the GIVE modal took the tap on the modal's BACK (the modal stayed, the notice %s)", notice_current() ? "up" : "gone");
            if (!notice_current()) TFAIL("a notice under the GIVE modal: BACK closed the notice too");
        }
        render_give_modal_close(); notice_sync(&T);
    }
    T.door_open[LINK_LEFT] = false; memset(&G, 0, sizeof G); gift_attach(&T, NULL);
    printf("selftest-touch-port: a notice under the GIVE modal (TCH-8): the modal's BACK closed the modal, the notice stayed for the next tap\n");
}

/* 7. an arrival shifts no slot: an open stat card stays (0.4.1 audit round 1, R-seam-link-progression F5 - P7 ticks the
   roster on every arrival, and the port closed the card at each one); a fish that leaves moves the card down with its
   fish, and the card of the fish that left closes (the 11-02 sweep) */
static void roster_card(void) {
    bare(); while (progression_newborn() >= 0) progression_newborn_done(&T);
    while (T.n_fish < 3) { progression_force_arrival(&T); progression_newborn_done(&T); }
    card_up(1); frame();
    progression_force_arrival(&T); frame();
    if (touch_port_selected() != 1) TFAIL("a birth closed fish 1's open card (%d)", touch_port_selected());
    progression_newborn_done(&T);
    card_up(2); frame();
    T.fish[0].trust = 9; if (!progression_sell_fish(&T, 0)) TFAIL("roster card: the sale"); frame();
    if (touch_port_selected() != 1) TFAIL("fish 0 sold: fish 2's card did not follow it to slot 1 (%d)", touch_port_selected());
    if (!progression_sell_fish(&T, 1)) TFAIL("roster card: the second sale"); frame();
    if (touch_port_selected() != -1) TFAIL("the open card's own fish sold: the card stayed (%d)", touch_port_selected());
    bare();
    printf("selftest-touch-port: 7. a birth keeps an open stat card; a sale moves it with its fish, and closes the sold fish's\n");
}


/* ===== AUDIT PROBES (0.4.1 phase 2, reviewer "touch"): not a gate - prints AUDIT lines ===== */
static int g_audit;
#define AUD(...) do { printf("AUDIT: "); printf(__VA_ARGS__); printf("\n"); g_audit++; } while (0)
static unsigned char g_reach[TANK_H][TANK_W];
static void reach_build(void) {
    memset(g_reach, 0, sizeof g_reach);
    int pw = panel_w(), ph = panel_h();
    for (int py = 0; py < ph; py++) for (int px = 0; px < pw; px++) {
        float mx, my; map_touch((uint16_t)px, (uint16_t)py, &mx, &my);
        int ix = (int)lroundf(mx), iy = (int)lroundf(my);
        for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
            int x = ix + dx, y = iy + dy;
            if (x >= 0 && y >= 0 && x < TANK_W && y < TANK_H) g_reach[y][x] = 1;
        }
    }
    int y0 = TANK_H, y1 = -1, x0 = TANK_W, x1 = -1;
    for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) if (g_reach[y][x]) { if (y < y0) y0 = y; if (y > y1) y1 = y; if (x < x0) x0 = x; if (x > x1) x1 = x; }
    printf("audit: the panel reads x %d..%d, y %d..%d of %dx%d\n", x0, x1, y0, y1, TANK_W, TANK_H);
}
/* a button's hit area: how much of it the panel can read; and the area's bounding box */
static void reach_check(const char *name, hit_fn f, int arg) {
    int n = 0, r = 0, x0 = TANK_W, x1 = -1, y0 = TANK_H, y1 = -1;
    for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) if (f((float)x, (float)y, arg)) {
        n++; r += g_reach[y][x];
        if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
    }
    if (!n) { printf("audit:   %-28s (none here)\n", name); return; }
    int cx = (x0 + x1) / 2, run = 0;
    for (int y = y0; y <= y1; y++) if (f((float)cx, (float)y, arg) && g_reach[y][cx]) run++;
    printf("audit:   %-28s x %3d..%3d y %3d..%3d  %3d%% readable, %d px tall at its middle\n", name, x0, x1, y0, y1, 100 * r / n, run);
    if (r * 2 < n || run < 14) AUD("%s: only %d%% of its hit area is readable by the panel (%d px tall at x %d) - hit area y %d..%d", name, 100 * r / n, run, cx, y0, y1);
}
static int hp_ms(float x, float y, int want) { int r = render_milestones_tap(&T, x, y); render_milestones_leave(); return r == want; }
static int g_tcol;
static int hp_trow(float x, float y, int col) {     /* the TANK row: the modal a tap opens names badge k? compare via ms state is private: use the column math */
    (void)col; float px = x - PAGE_X, py = y - PAGE_Y;
    if (!(py >= MSP_TANK_Y - 6 && py < MSP_FOOT_HIT_Y)) return 0;
    if (px >= MSP_TPG_HIT_X) return g_tcol == 7;
    const int bx0 = MSP_TBADGE_X0;
    if (!(px >= bx0 - 4 && px < bx0 + 7 * MSP_BADGE_DX)) return 0;
    int k = (int)((px - bx0 + 4) / MSP_BADGE_DX); if (k > 6) k = 6;
    return k == g_tcol;
}
static int hp_set(float x, float y, int want) { int v; return render_settings_tap(x, y, &v) == want; }
static int hp_upd(float x, float y, int want) { return updates_page_tap(x, y) == want; }
static int hp_nets(float x, float y, int want) { updates_page_show_nets(true); const int r = updates_page_tap(x, y) == want; updates_page_show_nets(false); return r; }   /* (0.4.2: the WI-FI face) */
static int hp_adv(float x, float y, int want) { return render_advanced_tap(x, y) == want; }
static int hp_warn(float x, float y, int want) { return render_battery_warning_tap(x, y) == want; }
static int hp_intro(float x, float y, int want) { return render_touch_intro_tap(x, y, true) == want; }
static int hp_turn(float x, float y, int want) { return render_touch_turn_tap(x, y) == want; }
static int hp_done(float x, float y, int want) { return render_touch_done_tap(x, y) == want; }
static int hp_cf(float x, float y, int want) { return render_confirm_hit(x, y) == want; }
static int hp_gift(float x, float y, int want) { return render_gift_hit(x, y) == want; }
static int hp_pp(float x, float y, int want) { return render_passport_hit(x, y) == want; }
static int hp_shelf(float x, float y, int want) { return render_shelf_tap(x, y) == want; }
static int hp_shc(float x, float y, int want) { return render_shelf_card_tap(x, y) == want; }
static int hp_lib(float x, float y, int want) { return render_library_tap(x, y, 1) == want; }
static int hp_rm(float x, float y, int want) { return render_shelf_remove_hit(x, y) == want; }
static int hp_key(float x, float y, int want) { return render_shelf_rename_hit(x, y) == want; }
static int hp_sfoot(float x, float y, int b) {
    int bx, by; render_shop_foot_box(b, &bx, &by); x -= PAGE_X; y -= PAGE_Y;
    return !(x < bx - 6 || x >= bx + MSP_FOOT_SQ + 6 || y < by - 8 || y >= by + MSP_FOOT_SQ + 12);
}
static int hp_srow(float x, float y, int j) { x -= PAGE_X; y -= PAGE_Y; int top = SHP_ROW_Y0 + j * SHP_ROW_DY;
    for (int b = 0; b < SHF_N; b++) if (hp_sfoot(x + PAGE_X, y + PAGE_Y, b)) return 0;
    return x >= 20 && y >= top - 8 && y < top + SHP_ROW_DY - 8; }
static int hp_smod(float x, float y, int which) {   /* 0 = the one button, 1 = left of two, 2 = right of two, 3..5 = a fry box */
    x -= PAGE_X; y -= PAGE_Y;
    const int bx = SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2, by = SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H;
    bool row = y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN;
    if (which == 0) return row && x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X;
    if (which == 1) return row && x >= SHP_TWO_X0 - MSP_HOW_SLOP_X && x < SHP_TWO_X0 + MSP_HOW_W + SHP_TWO_GAP / 2;
    if (which == 2) return row && x >= SHP_TWO_X1 - SHP_TWO_GAP / 2 && x < SHP_TWO_X1 + MSP_HOW_W + MSP_HOW_SLOP_X;
    int k = which - 3;
    return y >= SHP_FRY_BOX_Y - 8 && y < SHP_FRY_BOX_Y + SHP_FRY_BOX_H + 6 && x >= SHP_FRY_BOX_X(k) - SHP_FRY_BOX_GAP / 2 && x < SHP_FRY_BOX_X(k) + SHP_FRY_BOX_W + SHP_FRY_BOX_GAP / 2;
}
static int hp_gm(float x, float y, int want) {
    /* render_give_modal_tap has effects (BACK closes, GIVE asks): read the hit by geometry through render_give_modal_button's frame */
    (void)x; (void)y; (void)want; return 0;
}
static void reach_all(void) {
    reach_build();
    static const struct { int b; const char *n; } F[] = { { MSF_UPG, "keeper foot UPGRADES" }, { MSF_SET, "keeper foot cog" }, { MSF_TANKS, "keeper foot MY TANKS" }, { MSF_VIS, "keeper foot VISITORS" }, { MSF_CLOSE, "keeper foot CLOSE" } };
    static const int FT[] = { MS_TAP_SHOP, MS_TAP_SETTINGS, MS_TAP_TANKS, MS_TAP_VISITORS, MS_TAP_CLOSE };
    for (int i = 0; i < 5; i++) { if (HB_ROUND && i == 1) { reach_check("keeper cog (the cap)", hp_ms, MS_TAP_SETTINGS); continue; } reach_check(F[i].n, hp_ms, FT[i]); }
    char nm[48];
    for (int k = 0; k < 8; k++) { g_tcol = k; snprintf(nm, sizeof nm, k < 7 ? "TANK row badge %d" : "TANK row page arrow", k); reach_check(nm, hp_trow, k); }
    static const int SETV[] = { SET_TAP_CLOSE, SET_TAP_BRIGHT_LIVE, SET_TAP_VOLUME_LIVE, SET_TAP_LIGHT, SET_TAP_IDLE, SET_TAP_UPDATES, SET_TAP_SCREEN, SET_TAP_FEED, SET_TAP_ROTATE, SET_TAP_LINK, SET_TAP_BRIGHT, SET_TAP_VOLUME, SET_TAP_GFX };
    for (unsigned i = 0; i < sizeof SETV / sizeof SETV[0]; i++) { snprintf(nm, sizeof nm, "settings hit %d", SETV[i]); reach_check(nm, hp_set, SETV[i]); }
    reach_check("UPDATES CLOSE", hp_upd, UPD_TAP_CLOSE); reach_check("UPDATES CHECK", hp_upd, UPD_TAP_CHECK); reach_check("WI-FI FORGET", hp_nets, UPD_TAP_FORGET); reach_check("WI-FI ADD A NETWORK", hp_nets, UPD_TAP_ADD); reach_check("UPDATES WI-FI door", hp_upd, UPD_TAP_NETS);
    reach_check("ADVANCED BACK", hp_adv, ADV_HIT_BACK); reach_check("ADVANCED CALIBRATE", hp_adv, ADV_HIT_CALIBRATE); reach_check("ADVANCED PREV", hp_adv, ADV_HIT_PREV);
    reach_check("ADVANCED NEXT", hp_adv, ADV_HIT_NEXT); reach_check("ADVANCED SET", hp_adv, ADV_HIT_SET);
    reach_check("WARNING CANCEL", hp_warn, ADV_HIT_CANCEL); reach_check("WARNING CONTINUE", hp_warn, ADV_HIT_CONTINUE); reach_check("WARNING box", hp_warn, ADV_HIT_BOX);
    reach_check("TOUCH BACK", hp_intro, ADV_HIT_BACK); reach_check("TOUCH START", hp_intro, ADV_HIT_START); reach_check("TOUCH RESET TOUCH", hp_intro, ADV_HIT_RESET);
    reach_check("TURN IT OVER SKIP", hp_turn, ADV_HIT_SKIP); reach_check("TURN IT OVER GO", hp_turn, ADV_HIT_GO); reach_check("DONE OK", hp_done, ADV_HIT_OK);
    reach_check("reset prompt YES", hp_cf, 1); reach_check("reset prompt NO", hp_cf, -1);
    reach_check("gift ACCEPT", hp_gift, GIFT_HIT_ACCEPT); reach_check("gift NO THANKS", hp_gift, GIFT_HIT_NO);
    reach_check("passport DEPORT", hp_pp, PASSPORT_HIT_DEPORT); reach_check("passport COLLECT", hp_pp, PASSPORT_HIT_COLLECT);
    reach_check("MY TANKS CLOSE", hp_shelf, SHELF_TAP_CLOSE); reach_check("MY TANKS LIBRARY", hp_shelf, SHELF_TAP_LIBRARY); reach_check("MY TANKS padlock", hp_shelf, SHELF_TAP_TOUCH_LOCK);
    reach_check("MY TANKS PREV", hp_shelf, SHELF_TAP_PREV); reach_check("MY TANKS NEXT", hp_shelf, SHELF_TAP_NEXT);
    for (int i = 0; i < 8; i++) { snprintf(nm, sizeof nm, "MY TANKS place %d", i); reach_check(nm, hp_shelf, SHELF_TAP_SLOT0 + i); }
    reach_check("card DORMANT", hp_shc, SHC_TAP_DORMANT); reach_check("card PAUSED", hp_shc, SHC_TAP_PAUSED); reach_check("card SWAP IN", hp_shc, SHC_TAP_SWAP);
    reach_check("card RENAME", hp_shc, SHC_TAP_RENAME); reach_check("card MORE", hp_shc, SHC_TAP_MORE);
    reach_check("LIBRARY BACK", hp_lib, LIB_TAP_BACK); reach_check("LIBRARY NEW TANK", hp_lib, LIB_TAP_ROW0);
    reach_check("REMOVE HOLD TO SELL", hp_rm, SHM_TAP_HOLD); reach_check("REMOVE BACK", hp_rm, SHM_TAP_BACK);
    for (int k = SHK_CHAR0; k <= SHK_CLEAR; k++) { if (k >= SHK_CHAR0 + 14 && k < SHK_MORE) continue; snprintf(nm, sizeof nm, "rename key %d", k); reach_check(nm, hp_key, k); }
    static const char *const SF[] = { "shop foot ?", "shop foot DECOR", "shop foot CRITTERS", "shop foot BACKGROUNDS", "shop foot X" };
    for (int b = 0; b < SHF_N; b++) reach_check(SF[b], hp_sfoot, b);
    for (int j = 0; j < SHP_PER_PAGE; j++) { snprintf(nm, sizeof nm, "shop row %d", j); reach_check(nm, hp_srow, j); }
    static const char *const SM[] = { "shop modal button", "shop modal left", "shop modal right", "fry box 0", "fry box 1", "fry box 2" };
    for (int w = 0; w < 6; w++) reach_check(SM[w], hp_smod, w);
    (void)hp_gm;
}

/* what the second half of a double tap meets: the page a button opens, at the button's own spot */
static void doubles_map(void) {
    int n = 0, hit[16] = { 0 };
    int bx, by; render_ms_foot_box(MSF_TANKS, &bx, &by);
    for (int y = by - 4; y < by + MSP_FOOT_SQ + 4; y += 2) for (int x = bx - 4; x < bx + MSP_FOOT_SQ + 4; x += 2) {
        int r = render_shelf_tap((float)(x + PAGE_X), (float)(y + PAGE_Y)); n++;
        hit[r >= SHELF_TAP_SLOT0 ? 10 : r]++;
    }
    printf("audit: the keeper foot's MY TANKS square, doubled -> MY TANKS's page there: none %d, CLOSE %d, LIBRARY %d, PREV %d, NEXT %d, padlock %d, a place %d (of %d)\n",
           hit[0], hit[SHELF_TAP_CLOSE], hit[SHELF_TAP_LIBRARY], hit[SHELF_TAP_PREV], hit[SHELF_TAP_NEXT], hit[SHELF_TAP_TOUCH_LOCK], hit[10], n);
    if (hit[SHELF_TAP_CLOSE] || hit[SHELF_TAP_TOUCH_LOCK]) AUD("a doubled MY TANKS on the keeper foot can land on MY TANKS's %s (no open wait on its main page)", hit[SHELF_TAP_TOUCH_LOCK] ? "padlock" : "CLOSE");
    /* the card's RENAME -> the rename keyboard under it */
    int kn[32] = { 0 }, m = 0;
    for (int y = 0; y < TANK_H; y += 2) for (int x = 0; x < TANK_W; x += 2) if (render_shelf_card_tap((float)x, (float)y) == SHC_TAP_RENAME) { int k = render_shelf_rename_hit((float)x, (float)y); kn[k < 32 ? k : 0]++; m++; }
    printf("audit: a tank card's RENAME, doubled -> the keyboard there: nothing %d, a letter %d, DEL %d, SPACE %d, MORE %d, LINE %d, BACK %d, DONE %d, plate %d, CLEAR %d (of %d)\n",
           kn[0], m - kn[0] - kn[SHK_DEL] - kn[SHK_SPACE] - kn[SHK_MORE] - kn[SHK_LINE] - kn[SHK_BACK] - kn[SHK_DONE] - kn[SHK_LINE1] - kn[SHK_LINE2] - kn[SHK_CLEAR],
           kn[SHK_DEL], kn[SHK_SPACE], kn[SHK_MORE], kn[SHK_LINE], kn[SHK_BACK], kn[SHK_DONE], kn[SHK_LINE1] + kn[SHK_LINE2], kn[SHK_CLEAR], m);
    if (kn[SHK_CLEAR] || kn[SHK_DONE] || kn[SHK_DEL]) AUD("a doubled RENAME on a tank's card lands on the keyboard's %s (the keyboard has no open wait)", kn[SHK_CLEAR] ? "CLEAR" : kn[SHK_DONE] ? "DONE" : "DEL");
    /* settings' UPDATES -> UPDATES's FORGET under it? (the page gate covers this: count only) */
    int f = 0, u = 0;
    for (int y = 0; y < TANK_H; y += 2) for (int x = 0; x < TANK_W; x += 2) { int v; if (render_settings_tap((float)x, (float)y, &v) == SET_TAP_UPDATES) { u++; f += updates_page_tap((float)x, (float)y) == UPD_TAP_FORGET; } }
    printf("audit: settings' UPDATES over UPDATES's FORGET: %d of %d points (the page gate spends the double)\n", f, u);
}


/* the keeper page's way in and out, doubled: where the second half lands on the page that comes */
static void doubles_foot(void) {
    static const char *const KN[] = { "UPGRADES", "cog", "MY TANKS", "VISITORS", "CLOSE" };
    static const char *const SN[] = { "?", "DECOR", "CRITTERS", "BACKGROUNDS", "X" };
    for (int b = 0; b < MSF_N; b++) {
        if (HB_ROUND && b == MSF_SET) continue;
        int bx, by; render_ms_foot_box(b, &bx, &by);
        int n = 0, shop[SHF_N] = { 0 }, coll_close = 0;
        for (int y = by; y < by + MSP_FOOT_SQ; y += 2) for (int x = bx; x < bx + MSP_FOOT_SQ; x += 2) {
            float fx = (float)(x + PAGE_X), fy = (float)(y + PAGE_Y); n++;
            for (int s2 = 0; s2 < SHF_N; s2++) if (hp_sfoot(fx, fy, s2)) shop[s2]++;
            if (render_collection_tap(&T, fx, fy) == COLL_TAP_CLOSE) coll_close++;
        }
        if (b == MSF_UPG) { printf("audit: the keeper foot's UPGRADES square doubled -> the shop there: ? %d, DECOR %d, CRITTERS %d, BG %d, X %d (of %d)\n", shop[0], shop[1], shop[2], shop[3], shop[4], n);
                            for (int s2 = 0; s2 < SHF_N; s2++) if (shop[s2] * 2 > n) AUD("a doubled UPGRADES lands on the shop's %s (%d of %d points of the square): the shop has no open wait on its foot", SN[s2], shop[s2], n); }
        if (b == MSF_VIS) { printf("audit: the keeper foot's VISITORS square doubled -> the collection's CLOSE: %d of %d\n", coll_close, n);
                            if (coll_close) AUD("a doubled VISITORS can land on the collection's CLOSE (%d of %d points): it opens and closes", coll_close, n); }
        (void)KN;
    }
    /* the shop's X and the collection's CLOSE, doubled -> the keeper page's foot */
    for (int which = 0; which < 2; which++) {
        int n = 0, ms_close = 0, other = 0;
        for (int y = 0; y < TANK_H; y += 2) for (int x = 0; x < TANK_W; x += 2) {
            bool on = which ? render_collection_tap(&T, (float)x, (float)y) == COLL_TAP_CLOSE : hp_sfoot((float)x, (float)y, SHF_CLOSE);
            if (!on) continue;
            n++; int r = render_milestones_tap(&T, (float)x, (float)y); render_milestones_leave();
            if (r == MS_TAP_CLOSE) ms_close++; else if (r != MS_TAP_NONE) other++;
        }
        printf("audit: %s doubled -> the keeper page there: its CLOSE %d, another button %d (of %d)\n", which ? "the collection's CLOSE" : "the shop's X", ms_close, other, n);
        if (ms_close * 2 > n) AUD("%s doubled closes the keeper page too (%d of %d points): no spent lift as settings' CLOSE has", which ? "the collection's CLOSE" : "the shop's X", ms_close, n);
    }
}


static void doubles_e2e(void) {
    int bx, by; float x, y;
    /* the shop's X, double tapped (90 down / 100 up / 90 down) */
    render_shop_foot_box(SHF_CLOSE, &bx, &by); x = PAGE_X + bx + MSP_FOOT_SQ / 2.0f; y = PAGE_Y + by + MSP_FOOT_SQ / 2.0f;
    bare(); touch_port_show_shop(true); hold_for(800); tap(x, y, 90, 100); tap(x, y, 90, FRAME_MS);
    printf("audit: e2e - the shop's X double tapped: shop %d, keeper page %d\n", touch_port_shop(), touch_port_milestones());
    if (!touch_port_shop() && !touch_port_milestones()) AUD("e2e: a double tap on the shop's X closed the shop AND the keeper page (the tank is back)");
    /* the collection's CLOSE */
    x = PAGE_X + MSP_CLOSE_X + MSP_CLOSE_W / 2.0f; y = PAGE_Y + MSP_CLOSE_Y + MSP_CLOSE_H / 2.0f;
    bare(); touch_port_show_collection(true); hold_for(800); tap(x, y, 90, 100); tap(x, y, 90, FRAME_MS);
    printf("audit: e2e - the collection's CLOSE double tapped: collection %d, keeper page %d\n", touch_port_collection(), touch_port_milestones());
    if (!touch_port_collection() && !touch_port_milestones()) AUD("e2e: a double tap on the collection's CLOSE closed it AND the keeper page");
    /* the keeper page's VISITORS, double tapped at its square's middle and its right part */
    for (int part = 0; part < 2; part++) {
        render_ms_foot_box(MSF_VIS, &bx, &by); x = PAGE_X + bx + (part ? MSP_FOOT_SQ - 6 : MSP_FOOT_SQ / 2.0f); y = PAGE_Y + by + MSP_FOOT_SQ / 2.0f;
        bare(); touch_port_show_milestones(true); hold_for(800); tap(x, y, 90, 100); tap(x, y, 90, FRAME_MS);
        printf("audit: e2e - the keeper page's VISITORS (%s) double tapped: collection %d, keeper page %d\n", part ? "right side" : "middle", touch_port_collection(), touch_port_milestones());
        if (!touch_port_collection()) AUD("e2e: a double tap on VISITORS (%s of its square) opened the collection and closed it again", part ? "the right side" : "the middle");
    }
    /* the keeper page's CLOSE mashed 3 times: taps 2 and 3 on the bare tank - the light */
    render_ms_foot_box(MSF_CLOSE, &bx, &by); x = PAGE_X + bx + MSP_FOOT_SQ / 2.0f; y = PAGE_Y + by + MSP_FOOT_SQ / 2.0f;
    bare(); T.light_auto = false; bool was = T.light_manual_off; touch_port_show_milestones(true); hold_for(800); T.tap_count = 0;
    tap(x, y, 90, 100); tap(x, y, 90, 100); tap(x, y, 90, 100);
    int tc = T.tap_count; hold_for(1500);
    printf("audit: e2e - the keeper page's CLOSE tapped 3 times at 5 Hz: tank taps %d, the light %s\n", tc, T.light_manual_off != was ? "TOGGLED" : "as it was");
    if (T.light_manual_off != was) AUD("e2e: three quick taps on the keeper page's CLOSE put the tank's light out (taps 2-3 reach the bare tank as its double tap)");
    T.light_manual_off = was;
}


/* A FIRM SECOND PRESS: a double tap whose second half is pressed slowly - it lands inside the wait, lifts after it */
static void firm_second(void) {
    /* the keeper card's SELL: tap (arms), then 100 ms later a press held 700 ms */
    while (T.n_fish < 4) if (tank_add_fish(&T, 0, 1) < 0) break;
    for (int i = 0; i < T.n_fish; i++) T.fish[i].stage = STAGE_ADULT;
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    for (int hold = 100; hold <= 900; hold += 200) {
        bare(); touch_port_show_milestones(true); hold_for(800);
        render_milestones_show_fish(&T, 2);
        int fish, sx, by; if (!render_milestones_card(&T, &fish, NULL, &sx, &by)) { AUD("no fish card on the keeper page"); return; }
        int n0 = T.n_fish;
        tap((float)sx, (float)by, 90, 100);              /* arms: TAP AGAIN TO SELL */
        press((float)sx, (float)by, hold); lift(FRAME_MS);
        printf("audit: firm second press - keeper card SELL tapped, then pressed again 100 ms later for %d ms: fish %d -> %d\n", hold, n0, T.n_fish);
        if (T.n_fish < n0) { AUD("the keeper card's SELL: a tap then a %d ms press 100 ms later SOLD the fish (the 0.6 s wait is read at the lift)", hold); break; }
    }
    /* the shop: a row tapped (its modal), the second half pressed slowly where the modal's button stands */
    for (int hold = 100; hold <= 900; hold += 200) {
        bare(); T.sd_balance = 100000; T.sd_unlocks = 0; touch_port_show_shop(true); hold_for(800);
        const int bx = SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2, by = SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H;
        float x = PAGE_X + bx + MSP_HOW_W / 2.0f, y = -1;
        for (int j = SHP_PER_PAGE - 1; j >= 0 && y < 0; j--) { int top = SHP_ROW_Y0 + j * SHP_ROW_DY;
            for (int yy = top - 8; yy < top + SHP_ROW_DY - 8; yy++) if (yy >= by - MSP_HOW_SLOP_UP && yy < by + MSP_HOW_H + MSP_HOW_SLOP_DN && !hp_sfoot(x, (float)(yy + PAGE_Y), SHF_EARN) && !hp_sfoot(x, (float)(yy + PAGE_Y), SHF_CRIT) && !hp_sfoot(x, (float)(yy + PAGE_Y), SHF_DECOR)) { y = (float)(PAGE_Y + yy + 4); break; } }
        if (y < 0) { printf("audit: firm second press - no shop row under the modal's button\n"); break; }
        tap(x, y, 90, 100);
        press(x, y, hold); lift(FRAME_MS);
        int r = touch_port_take_shop();
        printf("audit: firm second press - a shop row at %.0f,%.0f tapped, then pressed again 100 ms later for %d ms: shop act %d%s\n", x, y, hold, r, r >= SHOP_TAP_BUY && r < SHOP_TAP_MOVE ? " (UNLOCK)" : "");
        if (r >= SHOP_TAP_BUY && r < SHOP_TAP_MOVE) { AUD("the shop: a row tapped, then a %d ms press 100 ms later on the same spot is UNLOCK (the modal's 0.6 s wait is read at the lift)", hold); break; }
    }
    render_shop_leave(); T.sd_unlocks = 0;
    /* the gift card: shown, a press 100 ms on held 2.1 s */
    static gift_book_t G; memset(&G, 0, sizeof G); gift_attach(&T, &G);
    float gx, gy;
    if (find_hit(hit_gift, GIFT_HIT_ACCEPT, &gx, &gy)) {
        bare(); G.ask.on = true; G.ask.uid = 0x77u; G.ask.shown = true; G.ask.seen = T.clock; hold_for(100);
        press(gx, gy, 2100); lift(FRAME_MS);
        printf("audit: firm second press - the gift card's ACCEPT pressed 0.1 s after it came, held 2.1 s: answer %d\n", G.ask.answer);
    }
    G.ask.on = false; gift_attach(&T, NULL);
}


/* a notice up under the GIVE modal over the live tank: whose is the tap on BACK? */
static void notice_under_modal(void) {
    static gift_book_t G; memset(&G, 0, sizeof G); gift_attach(&T, &G);
    T.door_open[LINK_LEFT] = true; G.door_peer[LINK_LEFT] = 0x49439648u; G.door_badge[LINK_LEFT] = 1;
    int f0 = -1; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 >= 0) {
        bare(); render_give_tap(&T, f0); hold_for(2200);
        notice_sync(&T); notice_low_battery(); for (int k = 0; k < 40 && !notice_current(); k++) notice_tick(&T, 0.1f, false);
        for (int k = 0; k < 30; k++) notice_tick(&T, 0.1f, false);   /* (past its own 2 s hold) */
        int gx, gy; render_give_modal_button(&T, RENDER_GM_TAP_BACK, &gx, &gy);
        bool nup = notice_current() != NULL;
        tap((float)gx, (float)gy, 90, FRAME_MS);
        int st = render_give_modal_step(&T);
        printf("audit: a notice up (%d) under the GIVE modal over the tank, BACK tapped: the modal %s, the notice %s\n", nup, st != RENDER_GM_OFF ? "STILL UP" : "closed", notice_current() ? "up" : "gone");
        if (nup && st != RENDER_GM_OFF) AUD("a notice under the GIVE modal (drawn under it) takes the tap meant for the modal's BACK");
        render_give_modal_close(); notice_sync(&T);
    }
    T.door_open[LINK_LEFT] = false; gift_attach(&T, NULL);
}


/* the reset's welcome over MY TANKS (the chord on MY TANKS, YES): does MY TANKS still see the finger? */
static void welcome_over_shelf(void) {
    bare(); touch_port_show_shelf(true); hold_for(200);
    touch_port_confirm_open(); frame();
    bool shelf_kept = touch_port_shelf();
    touch_port_confirm_answer(1); touch_port_confirm_take();
    setup_begin(&T);                                         /* main.c reset_tank: progression_reset, then setup_begin */
    press(TANK_W / 2.0f, TANK_H * 0.75f, 2200);
    float fx, fy; bool seen = touch_port_finger(&fx, &fy);
    lift(FRAME_MS);
    printf("audit: the reset prompt over MY TANKS: MY TANKS %s under it; after YES the welcome is up (%d) and MY TANKS still up (%d) sees a 2.2 s press under the welcome: %d\n",
           shelf_kept ? "kept" : "closed", setup_active(), touch_port_shelf(), seen);
    if (shelf_kept && touch_port_shelf() && seen) AUD("the reset's welcome comes up over MY TANKS, which stays open and sees the finger (its REMOVE hold, RENAME keys, MOVE hold read touch_port_finger)");
    setup_cancel(&T); touch_port_dismiss();
}

/* the gift card answered by a press that began BEFORE it came */
static void gift_prepress(void) {
    static gift_book_t G; memset(&G, 0, sizeof G); gift_attach(&T, &G);
    float x, y;
    for (int b = 0; b < 2; b++) {
        if (!find_hit(hit_gift, b ? GIFT_HIT_NO : GIFT_HIT_ACCEPT, &x, &y)) { AUD("no gift button"); continue; }
        /* on the bare tank: a finger resting on the water (the fish come round it) where ACCEPT will stand */
        bare(); G.ask.on = false; G.ask.answer = 0;
        if (!aim(x, y)) continue;
        g_down = true; hold_for(1000);
        bool held = T.hold_active;
        G.ask.on = true; G.ask.uid = 0x77u; G.ask.answer = 0; G.ask.shown = true; G.ask.seen = T.clock; snprintf(G.ask.pp.name, sizeof G.ask.pp.name, "MOSS");
        hold_for(2400); lift(FRAME_MS);
        printf("audit: gift card - a finger resting on the water (%s) 1 s before the card came, held 2.4 s more, lifted: answer %d\n", held ? "the fish's hold" : "no hold", G.ask.answer);
        if (G.ask.answer) AUD("the gift card took %s from a press that began before it came (finger held on the water; touch_port_ft3168.c card branch has no press-after-shown check, gift.c:162)", b ? "NO THANKS" : "ACCEPT");
        /* the same over MY TANKS */
        bare(); G.ask.on = false; G.ask.answer = 0; touch_port_show_shelf(true); hold_for(200);
        g_down = true; hold_for(1000);
        G.ask.on = true; G.ask.answer = 0; G.ask.shown = true; G.ask.seen = T.clock;
        hold_for(2400); lift(FRAME_MS);
        { float sx, sy; bool st = touch_port_take_shelf_tap(&sx, &sy, &g_shelf_press);
          printf("audit: gift card over MY TANKS - a press begun before it, held 2.4 s past it: answer %d, MY TANKS tap %d\n", G.ask.answer, st);
          if (G.ask.answer) AUD("over MY TANKS too: a press that began before the gift card answered it (%s)", b ? "NO THANKS" : "ACCEPT"); }
        G.ask.on = false; G.ask.answer = 0;
    }
    /* L8: the touch lock with MY TANKS open: the card answers */
    bool ok = find_hit(hit_gift, GIFT_HIT_ACCEPT, &x, &y);
    if (ok) {
        bare(); touch_port_set_touch_lock(true);
        G.ask.on = true; G.ask.answer = 0; G.ask.shown = true; G.ask.seen = T.clock;
        hold_for(2200); tap(x, y, 100, 100);
        int over_tank = G.ask.answer;
        G.ask.answer = 0; touch_port_show_shelf(true); G.ask.seen = T.clock;
        hold_for(2200); tap(x, y, 100, 100);
        printf("audit: the touch lock - the gift card's ACCEPT over the bare tank: %d; over MY TANKS: %d\n", over_tank, G.ask.answer);
        if (G.ask.answer && !over_tank) AUD("the touch lock: the gift card is answered while MY TANKS is open (T1 L8) - and not over the tank");
        touch_port_set_touch_lock(false); G.ask.on = false; G.ask.answer = 0;
    }
    /* the GIVE modal left up under the lock: BOOT twice (dismiss), the padlock, CLOSE - the modal over the tank, no tap reaches it */
    while (T.n_fish < 3) if (tank_add_fish(&T, 0, 1) < 0) break;
    for (int i = 0; i < T.n_fish; i++) T.fish[i].stage = STAGE_ADULT;
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    T.door_open[LINK_LEFT] = true; G.door_peer[LINK_LEFT] = 0x49439648u; G.door_badge[LINK_LEFT] = 1;
    int f0 = -1; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 >= 0) {
        bare(); render_give_tap(&T, f0); hold_for(2200);
        touch_port_dismiss();                                 /* BOOT twice: shelf_toggle -> touch_port_dismiss */
        touch_port_show_shelf(true); hold_for(200); touch_port_set_touch_lock(true); touch_port_show_shelf(false); hold_for(200);
        int gx, gy; bool bb = render_give_modal_button(&T, RENDER_GM_TAP_BACK, &gx, &gy);
        if (bb) { tap((float)gx, (float)gy, 100, 100); hold_for(2200); tap((float)gx, (float)gy, 100, 100); }
        int st = render_give_modal_step(&T);
        printf("audit: the GIVE modal, then BOOT twice, the padlock, CLOSE: the modal %s over the locked tank (step %d), BACK tapped twice\n", st != RENDER_GM_OFF ? "STILL UP" : "gone", st);
        if (st != RENDER_GM_OFF) AUD("the GIVE modal stays up over the locked glass: touch_port_dismiss (BOOT twice) never closes it, and the lock drops every tap on its BACK");
        touch_port_set_touch_lock(false); render_give_modal_close();
    }
    T.door_open[LINK_LEFT] = false; memset(&G, 0, sizeof G); gift_attach(&T, NULL);
}

/* a confirm step pressed slowly: the reset prompt's YES held 2 s from a press that began 0.3 s after it opened */
static void slow_confirm(void) {
    float x, y;
    if (!find_hit(hit_confirm, 1, &x, &y)) return;
    bare(); touch_port_confirm_open_page(RESET_PG_TANK); hold_for(300);
    press(x, y, 2000); lift(FRAME_MS);
    int a = touch_port_confirm_take();
    printf("audit: the reset prompt - YES pressed 0.3 s after it opened and held 2 s: answer %d (the open wait is checked at the press)\n", a);
    if (a == 1) AUD("the reset prompt's YES acts on a slow press that began inside its open wait");
    touch_port_confirm_answer(-1); touch_port_confirm_take();
    /* DEPORT: a press that began inside its 2 s hold, held past it */
    const int v = N_FISH_MAX;
    T.fish[v] = T.fish[0]; T.fish[v].uid = 0x5151u; T.fish[v].homeward = T.fish[v].deported = false; T.vis_used[0] = true;
    if (find_hit(hit_passport, PASSPORT_HIT_DEPORT, &x, &y)) {
        bare(); card_up(v); frame(); hold_for(500);
        press(x, y, 2000); lift(FRAME_MS);
        printf("audit: DEPORT - a press 0.5 s after the passport came, held 2 s: %s\n", T.fish[v].deported ? "DEPORTED" : "kept");
        if (T.fish[v].deported) AUD("DEPORT acts on a slow press that BEGAN inside its 2 s hold (the hold is measured at the lift, the reset prompt's at the press)");
    }
    T.vis_used[0] = false; memset(&T.fish[v], 0, sizeof T.fish[v]); T.deport_uid = 0; s_sel = -1;
}
static void audit_probes(void) {
    printf("audit: ---- the touch reviewer's probes ----\n");
    reach_all();
    doubles_map();
    doubles_foot();
    doubles_e2e();
    firm_second();
    notice_under_modal();
    welcome_over_shelf();
    gift_prepress();
    slow_confirm();
    printf("audit: %d AUDIT lines\n", g_audit);
}


/* ===== ROUND 2 AUDIT PROBES (0.4.1 round 2, reviewer "touch"): not a gate - prints R2AUDIT lines (PT_TOUCH_AUDIT2=1) ===== */
static int g_r2;
#define R2(...) do { printf("R2AUDIT: "); printf(__VA_ARGS__); printf("\n"); g_r2++; } while (0)
static int hit_setup(float x, float y, int want) { return setup_hit(x, y) == want; }
static int hit_shelf(float x, float y, int want) { return render_shelf_tap(x, y) == want; }
static int hit_adv(float x, float y, int want) { return render_advanced_tap(x, y) == want; }
static gift_book_t G2;
static void r2_frames(int ms) { for (int k = 0; k < (ms + FRAME_MS - 1) / FRAME_MS; k++) { frame(); render_gift_allow(!setup_active() && !touch_port_confirm_up() && !adv_up()); } }
static void r2_tap(float x, float y, int dn, int up) { if (!aim(x, y)) return; g_down = true; r2_frames(dn); g_down = false; r2_frames(up < FRAME_MS ? FRAME_MS : up); }
/* the hit point nearest its middle that the panel can read */
static bool find_hit_aim(hit_fn f, int arg, float *ox, float *oy) {
    double sx = 0, sy = 0; int n = 0;
    for (int y = 0; y < TANK_H; y += 2) for (int x = 0; x < TANK_W; x += 2) if (f((float)x, (float)y, arg)) { sx += x; sy += y; n++; }
    if (!n) return false;
    sx /= n; sy /= n; float bd = 1e18f; bool got = false;
    for (int y = 0; y < TANK_H; y += 2) for (int x = 0; x < TANK_W; x += 2) if (f((float)x, (float)y, arg)) {
        float d = (float)((x - sx) * (x - sx) + (y - sy) * (y - sy));
        if (d < bd && aim((float)x, (float)y) && fabsf(g_ax - x) < 2 && fabsf(g_ay - y) < 2 && f(g_ax, g_ay, arg)) { bd = d; *ox = (float)x; *oy = (float)y; got = true; }
    }
    return got;
}
static void r2_card_on(double seen) {   /* a gift offered to us, first seen at `seen` */
    G2.ask.on = true; G2.ask.uid = 0x77u; G2.ask.answer = 0; G2.ask.shown = true; G2.ask.seen = seen;
    snprintf(G2.ask.pp.name, sizeof G2.ask.pp.name, "MOSS");
}
static void r2_fish(int n) {
    while (T.n_fish < n) if (tank_add_fish(&T, 0, 1) < 0) break;
    for (int i = 0; i < T.n_fish; i++) T.fish[i].stage = STAGE_ADULT;
    while (progression_newborn() >= 0) progression_newborn_done(&T);
}
/* R2-1 recheck: the placement page's SELL, firm second press */
static void r2_place_sell(void) {
    const int item = 2;                                       /* the castle */
    for (int h = 0; h < 2; h++) {
        bare(); T.sd_unlocks |= SD_ITEMS[item].bit; T.sd_shelved &= ~SD_ITEMS[item].bit; T.sd_locked &= ~SD_ITEMS[item].bit;
        tank_decor_set(&T, item, 200, DECOR_Z_BACK);
        setup_begin_place(&T, item);
        float x, y; if (!find_hit(hit_setup, SETUP_HIT_SELL, &x, &y)) { printf("r2: no SELL on the placement page\n"); setup_cancel(&T); return; }
        const int hold = h ? 600 : 100;
        tap(x, y, 90, 100); press(x, y, hold); lift(FRAME_MS);
        bool sold = !(T.sd_unlocks & SD_ITEMS[item].bit);
        printf("r2: R2-1 placement SELL tapped, pressed again 100 ms later for %d ms: %s\n", hold, sold ? "SOLD" : "kept");
        if (sold) R2("R2-1 confirmed: placement page SELL sold by a firm second press (%d ms), the wait read at the lift (setup.c:490)", hold);
        if (setup_active()) setup_cancel(&T);
        T.sd_unlocks |= SD_ITEMS[item].bit;
    }
}
/* a 0.7 s hold on a piece opens its placement page under the SAME finger: setup_begin_place zeroes setup.c's s_down, so the
   finger reads as a fresh press on the page, and its lift taps whatever button it rests on */
static void r2_held_page(void) {
    static const char *const HN[] = { "", "DONE", "?", "?", "?", "SELL", "BCK", "FWD", "SHELVE", "LOCK" };
    for (int item = 0; item < SD_ITEM_COUNT; item++) {
        if (!tank_decor_placeable(item)) continue;
        bare();
        T.sd_unlocks |= SD_ITEMS[item].bit; T.sd_shelved &= ~SD_ITEMS[item].bit; T.sd_locked &= ~SD_ITEMS[item].bit;
        int cnt[256] = { 0 }, n = 0; float ex[256], ey[256]; bool have[256] = { false };
        static unsigned char hitmap[TANK_H][TANK_W];
        for (int y = 0; y < TANK_H; y += 2) for (int x = 0; x < TANK_W; x += 2) hitmap[y][x] = tank_decor_hit(&T, (float)x, (float)y) == item;
        setup_begin_place(&T, item);
        for (int y = 0; y < TANK_H; y += 2) for (int x = 0; x < TANK_W; x += 2) if (hitmap[y][x]) {
            n++; int hh = setup_hit((float)x, (float)y) & 255; cnt[hh]++;
            if (hh && !have[hh] && aim((float)x, (float)y)) { have[hh] = true; ex[hh] = (float)x; ey[hh] = (float)y; }
        }
        setup_cancel(&T);
        printf("r2: held %-10s (x %.0f): %d hit points; under the page's buttons:", SD_ITEMS[item].name, tank_decor_x(&T, item), n);
        for (int k = 1; k < 256; k++) if (cnt[k]) printf(" %s(%d)=%d", k < 10 ? HN[k] : "id", k, cnt[k]);
        printf("\n");
        for (int k = 1; k < 256; k++) if (have[k]) {
            bare(); T.sd_unlocks |= SD_ITEMS[item].bit; T.sd_shelved &= ~SD_ITEMS[item].bit; T.sd_locked &= ~SD_ITEMS[item].bit;
            uint32_t un = T.sd_unlocks, sh = T.sd_shelved, lk = T.sd_locked; float x0 = tank_decor_x(&T, item); int z0 = tank_decor_z(&T, item);
            press(ex[k], ey[k], 1000); bool up = setup_active(); lift(FRAME_MS); bool still_up = setup_active();
            printf("r2:   e2e held on %s at %.0f,%.0f (under %s): page %s after 1 s, after the lift %s | owned %d shelved %d locked %d z %d->%d x %.0f->%.0f\n",
                   SD_ITEMS[item].name, ex[k], ey[k], k < 10 ? HN[k] : "a swatch/depth", up ? "up" : "NOT up", still_up ? "up" : "CLOSED",
                   !!(T.sd_unlocks & SD_ITEMS[item].bit), !!(T.sd_shelved & SD_ITEMS[item].bit), !!(T.sd_locked & SD_ITEMS[item].bit), z0, tank_decor_z(&T, item), x0, tank_decor_x(&T, item));
            if (up && (T.sd_unlocks != un || T.sd_shelved != sh || T.sd_locked != lk || !still_up || tank_decor_z(&T, item) != z0))
                R2("the hold that opened %s's placement page tapped its %s at the lift (the page's own press began before it opened)", SD_ITEMS[item].name, k < 10 ? HN[k] : "row");
            if (setup_active()) setup_cancel(&T);
            T.sd_unlocks |= SD_ITEMS[item].bit; T.sd_shelved &= ~SD_ITEMS[item].bit; T.sd_locked &= ~SD_ITEMS[item].bit;
        }
    }
}
/* the gift card comes back from under a page (the birth flow, the reset prompt, ADVANCED) with its hold long spent: a doubled
   closing tap answers it. The GIVE modal stamps a fresh hold when it comes back (gm_draw); the card does not. */
static void r2_gift_back(void) {
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(3);
    float x, y; int ov[3] = { 0 }, tot = 0;
    /* 1. the birth flow's last page (DONE) over a card that was up */
    bare(); r2_card_on(T.clock - 30); render_gift_allow(false);
    setup_begin_birth(&T, T.n_fish - 1);
    for (int k = 0; k < SETUP_BIRTH_PAGES - 1 && setup_active(); k++) setup_activate(&T, SETUP_HIT_NEXT);
    if (find_hit(hit_setup, SETUP_HIT_NEXT, &x, &y)) {
        for (int yy = 0; yy < TANK_H; yy += 2) for (int xx = 0; xx < TANK_W; xx += 2) if (setup_hit((float)xx, (float)yy) == SETUP_HIT_NEXT) {
            tot++; int g = render_gift_hit((float)xx, (float)yy); if (g == GIFT_HIT_ACCEPT) ov[1]++; else if (g == GIFT_HIT_NO) ov[2]++; }
        printf("r2: the birth flow's last page DONE (%d points): over the gift card's ACCEPT %d, NO THANKS %d\n", tot, ov[1], ov[2]);
        float px = x, py = y;
        /* aim at a DONE point over a card button, if any */
        double mx = 0, my = 0; int mn = 0; float bd = 1e18f;
        for (int yy = 0; yy < TANK_H; yy += 2) for (int xx = 0; xx < TANK_W; xx += 2)
            if (setup_hit((float)xx, (float)yy) == SETUP_HIT_NEXT && (render_gift_hit((float)xx, (float)yy) == GIFT_HIT_ACCEPT || render_gift_hit((float)xx, (float)yy) == GIFT_HIT_NO)) { mx += xx; my += yy; mn++; }
        if (mn) { mx /= mn; my /= mn;
            for (int yy = 0; yy < TANK_H; yy += 2) for (int xx = 0; xx < TANK_W; xx += 2) {
                float d = (float)((xx - mx) * (xx - mx) + (yy - my) * (yy - my));
                if (d < bd && setup_hit((float)xx, (float)yy) == SETUP_HIT_NEXT && aim((float)xx, (float)yy) && setup_hit(g_ax, g_ay) == SETUP_HIT_NEXT
                    && (render_gift_hit(g_ax, g_ay) == GIFT_HIT_ACCEPT || render_gift_hit(g_ax, g_ay) == GIFT_HIT_NO)) { bd = d; px = (float)xx; py = (float)yy; } } }
        int gh = render_gift_hit(px, py);
        printf("r2: birth flow page %d (NATURE %d) before DONE, hit there %d, aimed %d -> %.1f,%.1f hit %d\n", setup_page(), SETUP_PG_NATURE, setup_hit(px, py), aim(px, py), g_ax, g_ay, setup_hit(g_ax, g_ay));
        r2_frames(200); printf("r2: after 200 ms of frames: active %d page %d hit %d\n", setup_active(), setup_page(), setup_hit(px, py));
        bool lg = g_log; g_log = true; r2_tap(px, py, 90, 100); g_log = lg; bool closed = !setup_active();   /* DONE: the flow ends at its lift */
        r2_tap(px, py, 90, FRAME_MS);                          /* the double's second half */
        printf("r2: birth DONE doubled at %.0f,%.0f (card %s there): flow closed %d, the gift card's answer %d\n", px, py,
               gh == GIFT_HIT_ACCEPT ? "ACCEPT" : gh == GIFT_HIT_NO ? "NO THANKS" : "no button", closed, G2.ask.answer);
        if (G2.ask.answer) R2("the gift card back from under the birth flow answered (%d) by the second half of a doubled DONE - its hold counted from when it was first seen, 30 s before", G2.ask.answer);
    }
    if (setup_active()) setup_cancel(&T);
    /* 2. the reset prompt's NO */
    bare(); r2_card_on(T.clock - 30); G2.ask.answer = 0;
    touch_port_confirm_open_page(RESET_PG_TANK); render_gift_allow(false); hold_for(800);
    if (find_hit(hit_confirm, -1, &x, &y)) {
        int gh = render_gift_hit(x, y), a;
        r2_tap(x, y, 90, 100); a = touch_port_confirm_take(); r2_tap(x, y, 90, FRAME_MS);
        printf("r2: the reset prompt's NO doubled at %.0f,%.0f (card %s there): prompt answer %d, the gift card's answer %d\n", x, y,
               gh == GIFT_HIT_ACCEPT ? "ACCEPT" : gh == GIFT_HIT_NO ? "NO THANKS" : "no button", a, G2.ask.answer);
        if (G2.ask.answer) R2("the gift card back from under the reset prompt answered (%d) by a doubled NO", G2.ask.answer);
    }
    touch_port_confirm_answer(-1); touch_port_confirm_take();
    /* 3. ADVANCED's BACK (to settings, the card over it) */
    bare(); r2_card_on(T.clock - 30); G2.ask.answer = 0;
    touch_port_show_settings(true); hold_for(800); touch_port_open_advanced(); render_gift_allow(false); hold_for(800);
    if (find_hit(hit_adv, ADV_HIT_BACK, &x, &y)) {
        int gh = render_gift_hit(x, y);
        r2_tap(x, y, 90, 100); bool back = !adv_up(); r2_tap(x, y, 90, FRAME_MS);
        printf("r2: ADVANCED's BACK doubled at %.0f,%.0f (card %s there): back to settings %d, the gift card's answer %d\n", x, y,
               gh == GIFT_HIT_ACCEPT ? "ACCEPT" : gh == GIFT_HIT_NO ? "NO THANKS" : "no button", back, G2.ask.answer);
        if (G2.ask.answer) R2("the gift card back from under ADVANCED answered (%d) by a doubled BACK", G2.ask.answer);
    }
    adv_close(); render_gift_allow(true);
    G2.ask.on = false; memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
}
/* a page that reads the finger itself, starved of the lift: UPDATES (its own s_down), settings */
static void r2_stale_lift(void) {
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2);
    float x, y;
    if (!find_hit(hit_updates, UPD_TAP_CHECK, &x, &y)) { printf("r2: no CHECK\n"); return; }
    /* 1. the gift card comes while CHECK is pressed; the keeper lifts on the card; it times out */
    bare(); touch_port_show_updates(true); hold_for(800);
    press(x, y, 200); r2_card_on(T.clock); hold_for(300); lift(400);
    int a1 = touch_port_take_update();
    G2.ask.on = false; hold_for(200);                         /* the card goes (its time, or another keeper's word) */
    int a2 = touch_port_take_update();
    printf("r2: UPDATES - CHECK pressed, a gift card came mid-press, the finger lifted on the card: CHECK %d at the lift, %d once the card went\n", a1, a2);
    if (a2 == UPD_TAP_CHECK) R2("UPDATES acted on CHECK FOR UPDATES once the gift card went - a press the card cut, its lift never seen (update.c static s_down), the board would restart into update mode");
    /* 2. BOOT twice (touch_port_dismiss) while CHECK is pressed; UPDATES opened again later */
    bare(); touch_port_show_updates(true); hold_for(800);
    press(x, y, 200); touch_port_dismiss(); hold_for(200); lift(400);
    touch_port_take_update();
    hold_for(5000); touch_port_show_updates(true); hold_for(800);
    int a3 = touch_port_take_update();
    printf("r2: UPDATES - CHECK pressed, BOOT twice closed it mid-press; UPDATES opened again 5 s later: CHECK %d\n", a3);
    if (a3 == UPD_TAP_CHECK) R2("UPDATES opened again acted on CHECK FOR UPDATES at once - the press BOOT twice cut, read as a tap the next time the page opened");
    /* 3. settings: a two-way row pressed, a gift card mid-press, the card times out */
    bare(); touch_port_show_settings(true); hold_for(800);
    int rows[2] = { SET_TAP_FEED, SET_TAP_SCREEN };
    for (int r = 0; r < 2; r++) {
        if (!find_hit(hit_settings, rows[r], &x, &y)) continue;
        prime(x, y); pagestate_t s0 = pagestate();
        press(x, y, 200); r2_card_on(T.clock); hold_for(300); lift(400);
        pagestate_t s1 = pagestate(); G2.ask.on = false; hold_for(200); pagestate_t s2 = pagestate();
        printf("r2: settings - %s pressed, a gift card came mid-press, lifted, the card timed out: changed at the lift %d, after the card %d\n",
               r ? "SCREEN" : "AUTO FEED", !same(s0, s1), !same(s1, s2));
        if (!same(s1, s2)) R2("settings changed %s by itself as the gift card went (a press the card cut, its lift read later)", r ? "SCREEN" : "AUTO FEED");
        drain_setting();
    }
    G2.ask.on = false; memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
}
/* R2-6 and its siblings: a press older than the GIVE modal's step stamp */
static void r2_give_step(void) {
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(3);
    T.door_open[LINK_LEFT] = true; G2.door_peer[LINK_LEFT] = 0x49439648u; G2.door_badge[LINK_LEFT] = 1;
    int f0 = -1; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 >= 0) {
        bare(); render_give_tap(&T, f0); hold_for(2200);
        int gx, gy; render_give_modal_button(&T, RENDER_GM_TAP_GIVE, &gx, &gy);
        tap((float)gx, (float)gy, 90, FRAME_MS); hold_for(2300);
        int st = render_give_modal_step(&T);
        int cx, cy; bool cb = render_give_modal_button(&T, RENDER_GM_TAP_CALLOFF, &cx, &cy);
        printf("r2: GIVE tapped after its hold: step %d (ASKING %d), CALL OFF at %d,%d (%d)\n", st, RENDER_GM_ASKING, cx, cy, cb);
        if (cb) {
            press((float)cx, (float)cy, 400);                 /* a finger on CALL OFF ... */
            uint32_t uid = T.fish[f0].uid; G2.want_uid = 0; G2.asking_uid = 0; G2.wait_uid = 0;
            G2.said_uid = uid; G2.said_reason = 0; G2.said_until = T.clock + GIFT_SAID_S;   /* ... as the other keeper says NO THANKS */
            hold_for(400); int st2 = render_give_modal_step(&T); lift(FRAME_MS);
            printf("r2: R2-6 - CALL OFF pressed, THEY SAID NO THANKS mid-press (step %d), lifted 0.4 s later: the modal %s\n", st2,
                   render_give_modal_step(&T) == RENDER_GM_OFF ? "CLOSED (the SAID line's OK, unread)" : "up");
            if (render_give_modal_step(&T) == RENDER_GM_OFF) R2("R2-6 confirmed: a press begun before the step stamp closed the SAID line with no hold");
        }
    }
    render_give_modal_close(); T.door_open[LINK_LEFT] = false; memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
}
/* TCH-4's siblings: MY TANKS's CLOSE back to the keeper page, THE LIBRARY's BACK, the card's outside tap */
static void r2_shelf_exits(void) {
    float x, y, sx, sy; int64_t sp;
    if (find_hit_aim(hit_shelf, SHELF_TAP_CLOSE, &x, &y)) {
        bare(); touch_port_show_milestones(true); hold_for(800);
        touch_port_show_shelf(true); hold_for(800);           /* (main: the keeper foot's MY TANKS, s_shelf_from_ms) */
        tap(x, y, 90, FRAME_MS);
        bool t1 = touch_port_take_shelf_tap(&sx, &sy, &sp);
        if (t1 && render_shelf_tap(sx, sy) == SHELF_TAP_CLOSE) { touch_port_show_shelf(false); touch_port_show_milestones(true); }   /* main.c shelf_close */
        hold_for(60); { bool lg = g_log; g_log = true; tap(x, y, 90, FRAME_MS); g_log = lg; }
        int bx, by; render_ms_foot_box(MSF_CLOSE, &bx, &by);
        printf("r2: (after it: shop %d, collection %d, settings %d)\n", touch_port_shop(), touch_port_collection(), touch_port_settings());
        printf("r2: MY TANKS CLOSE (from the keeper page) doubled at %.0f,%.0f: first tap %d, keeper page after the second %d (its CLOSE box x %d..%d y %d..)\n",
               x, y, t1, touch_port_milestones(), PAGE_X + bx, PAGE_X + bx + MSP_FOOT_SQ, PAGE_Y + MSP_FOOT_HIT_Y);
        if (t1 && !touch_port_milestones()) R2("a doubled MY TANKS CLOSE (opened from the keeper foot) also shut the keeper page it went back to - no page wait when main brings it back (TCH-4 sibling)");
        /* THE LIBRARY's BACK stands where CLOSE does (render_library_tap): its double reaches MY TANKS's CLOSE */
        bare(); touch_port_show_shelf(true); hold_for(800);
        tap(x, y, 90, FRAME_MS); bool l1 = touch_port_take_shelf_tap(&sx, &sy, &sp); int lr = render_library_tap(sx, sy, 1);
        if (l1 && lr == LIB_TAP_BACK) touch_port_shelf_back();   /* (main.c since R2T-5: the main page is back) */
        hold_for(60); tap(x, y, 90, FRAME_MS); bool l2 = touch_port_take_shelf_tap(&sx, &sy, &sp); int sr = render_shelf_tap(sx, sy);
        printf("r2: THE LIBRARY's BACK doubled: first tap %d (library says %s), second tap reaches MY TANKS %d (shelf says %s)\n", l1, lr == LIB_TAP_BACK ? "BACK" : "?",
               l2, sr == SHELF_TAP_CLOSE ? "CLOSE" : "?");
        if (l1 && lr == LIB_TAP_BACK && l2 && sr == SHELF_TAP_CLOSE) R2("a doubled THE LIBRARY BACK closes MY TANKS (its second half is a live MY TANKS tap on CLOSE: main.c sets s_sh_lib false with no wait)");
    }
    if (find_hit(hit_shelf, SHELF_TAP_TOUCH_LOCK, &x, &y)) {
        int n = 0, out = 0; bool locks = false;
        for (int yy = 0; yy < TANK_H; yy += 2) for (int xx = 0; xx < TANK_W; xx += 2) if (render_shelf_tap((float)xx, (float)yy) == SHELF_TAP_TOUCH_LOCK) { n++; out += render_shelf_card_tap((float)xx, (float)yy) == SHC_TAP_OUTSIDE; }
        printf("r2: the padlock (%d points): outside a tank's card %d - a doubled tap that closes the card there toggles the touch lock\n", n, out);
        if (find_hit_aim(hit_shelf, SHELF_TAP_TOUCH_LOCK, &x, &y)) {
            bare(); touch_port_show_shelf(true); hold_for(800);
            tap(x, y, 90, 100); bool p1 = touch_port_take_shelf_tap(&sx, &sy, &sp); int c1 = render_shelf_card_tap(sx, sy);
            if (p1 && c1 == SHC_TAP_OUTSIDE) touch_port_shelf_back();   /* (main.c since R2T-5: the card closed, the main page is back) */
            tap(x, y, 90, FRAME_MS); bool p2 = touch_port_take_shelf_tap(&sx, &sy, &sp); int c2 = render_shelf_tap(sx, sy);
            locks = p1 && p2 && c2 == SHELF_TAP_TOUCH_LOCK;
            printf("r2: a card's outside tap doubled on the padlock at %.0f,%.0f: first reaches main.c %d (the card says %s), second %d (the main page says %s)\n", x, y,
                   p1, c1 == SHC_TAP_OUTSIDE ? "OUTSIDE" : "?", p2, c2 == SHELF_TAP_TOUCH_LOCK ? "THE PADLOCK" : "?");
        }
        if (out && locks) R2("a tap outside a MY TANKS card that closes it, doubled on the padlock (%d of %d points), locks the touch: the main page has no wait after a card closes", out, n);   /* (0.4.1 round 2: printed on the e2e, not the geometry alone) */
    }
    bare();
}
/* the keeper page a tap on the stat card brings, and the rename's DONE back to the fish's card: no page wait */
static void r2_keeper_in(void) {
    bare(); r2_fish(3);
    float cx = RENDER_CARD_X + RENDER_CARD_W / 2.0f, cy = RENDER_CARD_Y + 30;
    card_up(0); frame();
    bool g = g_log; g_log = true;
    printf("r2: ---- a doubled tap on the stat card (log follows) ----\n");
    tap(cx, cy, 90, 100); tap(cx, cy, 90, FRAME_MS);
    g_log = g;
    printf("r2: the stat card tapped twice at %.0f,%.0f: keeper page %d (a 'page tap ... -> detail' above = the second half opened a modal)\n", cx, cy, touch_port_milestones());
    bare();
    setup_begin_rename(&T, 0); hold_for(200);
    float x, y;
    if (find_hit(hit_setup, SETUP_HIT_NEXT, &x, &y)) {
        g_log = true; printf("r2: ---- RENAME's DONE doubled (log follows) ----\n");
        tap(x, y, 90, 100); bool closed = !setup_active(); bool card = render_milestones_card(&T, NULL, NULL, NULL, NULL);
        { int ax, ay, fi = -1; render_milestones_card(&T, &fi, NULL, NULL, NULL); if (render_milestones_arrow(&T, true, &ax, &ay)) printf("r2: (the card back is fish %d; its right arrow at %d,%d)\n", fi, ax, ay); }
        tap(x, y, 90, FRAME_MS); g_log = g;
        { int fi = -1; render_milestones_card(&T, &fi, NULL, NULL, NULL); printf("r2: (after the second half the card is fish %d)\n", fi); }
        printf("r2: RENAME DONE doubled at %.0f,%.0f: closed %d, card back %d; after the second half: card %d, rename again %d, GIVE modal %d\n", x, y, closed, card,
               render_milestones_card(&T, NULL, NULL, NULL, NULL), setup_active(), render_give_modal_step(&T) != RENDER_GM_OFF);
        if (closed && card && !render_milestones_card(&T, NULL, NULL, NULL, NULL)) R2("RENAME's DONE doubled: the second half closed the fish's card it came back to");
        if (setup_active()) setup_cancel(&T);
    }
    bare();
}
/* the birth flow opening under a resting finger */
static void r2_birth_under_finger(void) {
    bare(); if (T.n_fish > 3) T.n_fish = 3; r2_fish(3);
    setup_begin_birth(&T, T.n_fish - 1);
    float x, y; bool ok = find_hit(hit_setup, SETUP_HIT_NEXT, &x, &y); setup_cancel(&T);
    if (!ok) return;
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    setup_poll_birth(&T);                                     /* (nothing owed: the last offer forgotten) */
    uint32_t keep = T.sd_unlocks; T.sd_unlocks = 0;           /* (no piece under the finger: its hold would open a placement page) */
    bare(); press(x, y, 1000);                                /* a finger resting on the water: the fish come round it */
    progression_force_arrival(&T); int owed = progression_newborn(); int nb = setup_poll_birth(&T); touch_port_dismiss();
    printf("r2: (%d fish, owed %d, offered %d)\n", T.n_fish, owed, nb);   /* (main.c: a fry owed its welcome) */
    int p0 = setup_page(); hold_for(1000); lift(FRAME_MS);
    printf("r2: the birth flow opened (%d) under a finger resting on its NEXT 1 s, lifted 1 s on: page %d -> %d (BORN %d)\n", nb, p0, setup_active() ? setup_page() : -1, SETUP_PG_BORN);
    if (nb >= 0 && setup_active() && setup_page() != p0) R2("the birth flow's first page was turned by a finger that was on the glass before it opened (setup_begin_birth zeroes s_down: the old press reads as new)");
    if (setup_active()) setup_cancel(&T);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    T.sd_unlocks = keep; bare();
}
/* THE GIFT CARD'S ANSWER, DOUBLED: the first tap answers it and tank_link closes it the same frame; the second half lands on
   the page under it, whose own waits were spent long ago (no spent lift, no page wait after the card) */
static void r2_frames_link(int ms) { for (int k = 0; k < (ms + FRAME_MS - 1) / FRAME_MS; k++) { frame(); if (G2.ask.on && G2.ask.answer) G2.ask.on = false; } }   /* (tank_link.c:238) */
static void r2_card_double(float x, float y) { aim(x, y); g_down = true; r2_frames_link(90); g_down = false; r2_frames_link(100); aim(x, y); g_down = true; r2_frames_link(90); g_down = false; r2_frames_link(FRAME_MS * 2); }
static bool r2_near_gift(float cx, float cy, int rx, int ry, int (*ok)(float, float), float *ox, float *oy) {   /* a point by (cx, cy) on a card button and on the target */
    float bd = 1e18f; bool got = false;
    for (int y = (int)cy - ry; y <= (int)cy + ry; y += 2) for (int x = (int)cx - rx; x <= (int)cx + rx; x += 2) {
        int g = render_gift_hit((float)x, (float)y); if (g != GIFT_HIT_ACCEPT && g != GIFT_HIT_NO) continue;
        if (ok && !ok((float)x, (float)y)) continue;
        float d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
        if (d < bd && aim((float)x, (float)y) && render_gift_hit(g_ax, g_ay) == g && (!ok || ok(g_ax, g_ay))) { bd = d; *ox = (float)x; *oy = (float)y; got = true; }
    }
    return got;
}
static int ok_lib_row(float x, float y) { return render_library_tap(x, y, 1) == LIB_TAP_ROW0; }
static int ok_shelf_slot(float x, float y) { return render_shelf_tap(x, y) >= SHELF_TAP_SLOT0; }
static int ok_deport(float x, float y) { return render_passport_hit(x, y) == PASSPORT_HIT_DEPORT; }
static int ok_check(float x, float y) { return updates_page_tap(x, y) == UPD_TAP_CHECK; }
static int g_set_want; static int ok_set(float x, float y) { int v; return render_settings_tap(x, y, &v) == g_set_want; }
static void r2_card_doubled(void) {
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(4);
    float x, y; const char *gn;
    /* 1. the keeper page, a fish's card, SELL armed */
    bare(); touch_port_show_milestones(true); hold_for(800); render_milestones_show_fish(&T, 2);
    int fish, sx, by;
    if (render_milestones_card(&T, &fish, NULL, &sx, &by)) {
        tap((float)sx, (float)by, 90, 1000);                  /* SELL: armed (TAP AGAIN TO SELL), then the keeper waits */
        bool near = r2_near_gift((float)sx, (float)by, 24, 10, NULL, &x, &y);
        gn = near ? (render_gift_hit(x, y) == GIFT_HIT_ACCEPT ? "ACCEPT" : "NO THANKS") : "-";
        printf("r2: keeper card SELL at %d,%d; a gift card button over it: %s%s\n", sx, by, near ? gn : "none", "");
        if (near) {
            int n0 = T.n_fish; r2_card_on(T.clock - 5); r2_card_double(x, y);
            printf("r2: the gift card's %s doubled at %.0f,%.0f over the keeper card's armed SELL: answer %d, fish %d -> %d\n", gn, x, y, G2.ask.answer, n0, T.n_fish);
            if (T.n_fish < n0) R2("a doubled %s on the gift card SOLD the fish whose card's SELL was armed under it", gn);
        }
    }
    G2.ask.on = false; G2.ask.answer = 0;
    /* 1b. the same, SELL not armed: does the second half arm it / open RENAME / GIVE? */
    bare(); touch_port_show_milestones(true); hold_for(800); render_milestones_show_fish(&T, 2);
    {   int rx; if (render_milestones_card(&T, &fish, &rx, &sx, &by)) {
            int pts[3] = { rx, (rx + sx) / 2, sx };
            for (int k = 0; k < 3; k++) if (r2_near_gift((float)pts[k], (float)by, 24, 10, NULL, &x, &y))
                printf("r2: keeper card button %d (x %d) under the gift card's %s at %.0f,%.0f\n", k, pts[k], render_gift_hit(x, y) == GIFT_HIT_ACCEPT ? "ACCEPT" : "NO THANKS", x, y);
        } }
    /* 2. the shop: a row's modal open (UNLOCK), the card over it */
    float rx, ry;
    bare(); T.sd_balance = 100000; T.sd_unlocks = 0; T.sd_shelved = 0; touch_port_show_shop(true); hold_for(200); render_shop_leave();
    if (shop_row_under_button(&rx, &ry) >= 0) {
        bare(); T.sd_balance = 100000; T.sd_unlocks = 0; T.sd_shelved = 0; touch_port_show_shop(true); hold_for(800);
        tap(rx, ry, 90, 1000);                                /* the row: its modal, UNLOCK where the row was */
        const float ux = PAGE_X + SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2 + MSP_HOW_W / 2.0f, uy = PAGE_Y + SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H / 2.0f;
        bool near = r2_near_gift(ux, uy, MSP_HOW_W / 2, MSP_HOW_H / 2, NULL, &x, &y);
        printf("r2: the shop modal's UNLOCK at %.0f,%.0f; a gift card button over it: %s\n", ux, uy, near ? (render_gift_hit(x, y) == GIFT_HIT_ACCEPT ? "ACCEPT" : "NO THANKS") : "none");
        if (near) {
            touch_port_take_shop(); r2_card_on(T.clock - 5); r2_card_double(x, y); int r = touch_port_take_shop();
            printf("r2: the gift card's answer doubled over the shop modal's UNLOCK: answer %d, shop act %d (UNLOCK >= %d)\n", G2.ask.answer, r, SHOP_TAP_BUY);
            if (r >= SHOP_TAP_BUY && r < SHOP_TAP_MOVE) R2("a doubled answer on the gift card BOUGHT the shop piece whose modal stood under it");
        }
    }
    G2.ask.on = false; G2.ask.answer = 0; render_shop_leave(); T.sd_unlocks = 0;
    /* 3. MY TANKS: THE LIBRARY's NEW TANK, a place, under the card */
    {   int nl = 0, ns = 0, tot = 0;
        for (int yy = 0; yy < TANK_H; yy += 2) for (int xx = 0; xx < TANK_W; xx += 2) { int g = render_gift_hit((float)xx, (float)yy);
            if (g != GIFT_HIT_ACCEPT && g != GIFT_HIT_NO) continue; tot++; nl += ok_lib_row((float)xx, (float)yy); ns += ok_shelf_slot((float)xx, (float)yy); }
        printf("r2: the gift card's buttons (%d points) over THE LIBRARY's NEW TANK %d, over MY TANKS's places %d\n", tot, nl, ns);
        if (nl && r2_near_gift(TANK_W / 2.0f, TANK_H / 2.0f, TANK_W / 2, TANK_H / 2, ok_lib_row, &x, &y)) {
            bare(); touch_port_show_shelf(true); hold_for(800); float qx, qy; int64_t qp;
            r2_card_on(T.clock - 5); r2_card_double(x, y); bool got = touch_port_take_shelf_tap(&qx, &qy, &qp);
            printf("r2: the gift card's %s doubled at %.0f,%.0f over THE LIBRARY (open > 0.6 s): answer %d, MY TANKS tap %d (the library says %s)\n",
                   render_gift_hit(x, y) == GIFT_HIT_ACCEPT ? "ACCEPT" : "NO THANKS", x, y, G2.ask.answer, got, got && ok_lib_row(qx, qy) ? "NEW TANK" : "-");
            if (got && ok_lib_row(qx, qy)) R2("a doubled answer on the gift card over THE LIBRARY reaches main.c as a tap on NEW TANK (300 SD; its wait long spent)");
            G2.ask.on = false;
        }
    }
    /* 4. UPDATES's CHECK and settings' rows */
    if (r2_near_gift(TANK_W / 2.0f, TANK_H / 2.0f, TANK_W / 2, TANK_H / 2, ok_check, &x, &y)) {
        bare(); touch_port_show_updates(true); hold_for(800); touch_port_take_update();
        r2_card_on(T.clock - 5); r2_card_double(x, y); int a = touch_port_take_update();
        printf("r2: the gift card's answer doubled over UPDATES's CHECK at %.0f,%.0f: answer %d, CHECK %d\n", x, y, G2.ask.answer, a == UPD_TAP_CHECK);
        if (a == UPD_TAP_CHECK) R2("a doubled answer on the gift card over UPDATES tapped CHECK FOR UPDATES (the board restarts into update mode)");
        G2.ask.on = false;
    } else printf("r2: no gift card button over UPDATES's CHECK\n");
    int rows[3] = { SET_TAP_FEED, SET_TAP_SCREEN, SET_TAP_ROTATE };
    for (int r = 0; r < 3; r++) { g_set_want = rows[r];
        if (!r2_near_gift(TANK_W / 2.0f, TANK_H / 2.0f, TANK_W / 2, TANK_H / 2, ok_set, &x, &y)) continue;
        bare(); touch_port_show_settings(true); hold_for(800); prime(x, y); pagestate_t s0 = pagestate();
        r2_card_on(T.clock - 5); r2_card_double(x, y); pagestate_t s1 = pagestate(); drain_setting();
        printf("r2: the gift card's answer doubled over settings' %s at %.0f,%.0f: answer %d, the row changed %d\n", r == 0 ? "AUTO FEED" : r == 1 ? "SCREEN" : "ROTATION", x, y, G2.ask.answer, !same(s0, s1));
        if (!same(s0, s1)) R2("a doubled answer on the gift card changed settings' %s under it", r == 0 ? "AUTO FEED" : r == 1 ? "SCREEN" : "ROTATION");
        G2.ask.on = false;
    }
    /* 5. a visitor's passport on the bare tank under the card: DEPORT */
    {   const int v = N_FISH_MAX; int nd = 0, tot = 0;
        for (int yy = 0; yy < TANK_H; yy += 2) for (int xx = 0; xx < TANK_W; xx += 2) { int g = render_gift_hit((float)xx, (float)yy);
            if (g != GIFT_HIT_ACCEPT && g != GIFT_HIT_NO) continue; tot++; nd += render_passport_hit((float)xx, (float)yy) == PASSPORT_HIT_DEPORT; }
        printf("r2: the gift card's buttons (%d points) over a passport's DEPORT: %d\n", tot, nd);
        T.fish[v] = T.fish[0]; T.fish[v].uid = 0x5151u; T.fish[v].homeward = T.fish[v].deported = false; T.vis_used[0] = true;
        if (nd && r2_near_gift(TANK_W / 2.0f, TANK_H / 2.0f, TANK_W / 2, TANK_H / 2, ok_deport, &x, &y)) {
            bare(); T.vis_used[0] = true; card_up(v); frame(); hold_for(3000);   /* the passport up 3 s (its DEPORT hold long spent) */
            r2_card_on(T.clock - 2.5); r2_card_double(x, y);
            printf("r2: the gift card's %s doubled at %.0f,%.0f over a visitor's passport: answer %d, the visitor %s\n", render_gift_hit(x, y) == GIFT_HIT_ACCEPT ? "ACCEPT" : "NO THANKS",
                   x, y, G2.ask.answer, T.fish[v].deported ? "DEPORTED" : "kept");
            if (T.fish[v].deported) R2("a doubled answer on the gift card DEPORTED the visitor whose passport stood under it");
        }
        T.vis_used[0] = false; memset(&T.fish[v], 0, sizeof T.fish[v]); T.deport_uid = 0; s_sel = -1; G2.ask.on = false;
    }
    G2.ask.on = false; memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
}
/* the GIVE modal's way out doubled over the keeper card it came from (SELL armed before GIVE was tapped) */
static void r2_give_exit_doubled(void) {
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(4);
    T.door_open[LINK_LEFT] = true; G2.door_peer[LINK_LEFT] = 0x49439648u; G2.door_badge[LINK_LEFT] = 1;
    for (int pass = 0; pass < 2; pass++) {
        bare(); touch_port_show_milestones(true); hold_for(800); render_milestones_show_fish(&T, 2);
        int fish, sx, by, gx, gy; if (!render_milestones_card(&T, &fish, NULL, &sx, &by) || !render_milestones_give(&T, &gx, &gy)) { printf("r2: no keeper card with GIVE\n"); break; }
        if (pass) tap((float)sx, (float)by, 90, 800);         /* SELL armed first (pass 1) */
        tap((float)gx, (float)gy, 90, 2300);                   /* GIVE: the modal over the card; past its hold */
        int bx, byy; if (!render_give_modal_button(&T, RENDER_GM_TAP_BACK, &bx, &byy)) { printf("r2: the GIVE modal has no BACK\n"); break; }
        int n0 = T.n_fish; bool lg = g_log; g_log = true;
        tap((float)bx, (float)byy, 90, 100); tap((float)bx, (float)byy, 90, FRAME_MS); g_log = lg;
        printf("r2: the GIVE modal's BACK (%d,%d) doubled over the keeper card (SELL at %d,%d, %s): modal %d, fish %d -> %d\n", bx, byy, sx, by, pass ? "armed" : "not armed",
               render_give_modal_step(&T) != RENDER_GM_OFF, n0, T.n_fish);
        if (T.n_fish < n0) R2("the GIVE modal's BACK doubled SOLD the fish whose SELL was armed before GIVE");
        r2_fish(4);
    }
    render_give_modal_close(); T.door_open[LINK_LEFT] = false; memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
}
static void r2_probes(void) {
    printf("r2: ---- the round-2 touch reviewer's probes ----\n");
    r2_place_sell();
    r2_held_page();
    r2_gift_back();
    r2_stale_lift();
    r2_give_step();
    r2_shelf_exits();
    r2_keeper_in();
    r2_birth_under_finger();
    r2_card_doubled();
    r2_give_exit_doubled();
    printf("r2: %d R2AUDIT lines\n", g_r2);
}

/* ===== ROUND 2 GATES (0.4.1 audit round 2, FIX-touch.md): red before each fix, green after ===== */
/* R2T-1. THE GIFT CARD'S ANSWER, DOUBLED: the first tap answers it and tank_link closes it the same frame (r2_frames_link); the
   second half met the page under it, whose own waits were spent long ago. Every page the reviewer found under the card's buttons,
   on each board where its geometry puts one there; and the card going by itself (its time) under a tap. 0.7 s on, a tap acts. */
static void r2g_card_doubled(void) {
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(4);
    float x, y; int n = 0;
    /* the keeper page, a fish's card, SELL armed */
    for (int how = 0; how < 2; how++) {                       /* 0: the answer doubled; 1: the card goes by itself as a tap lands */
        bare(); touch_port_show_milestones(true); hold_for(800); render_milestones_show_fish(&T, 2);
        int fish, sx, by;
        if (!render_milestones_card(&T, &fish, NULL, &sx, &by)) { TFAIL("R2T-1: no fish card on the keeper page"); break; }
        tap((float)sx, (float)by, 90, 1000);                  /* SELL armed */
        const int n0 = T.n_fish;
        if (how == 0) {
            if (!r2_near_gift((float)sx, (float)by, 24, 10, NULL, &x, &y)) { printf("selftest-touch-port: (R2T-1: no gift card button over the keeper card's SELL here)\n"); continue; }
            r2_card_on(T.clock - 5); r2_card_double(x, y);
            if (G2.ask.answer == 0) TFAIL("R2T-1: the gift card's doubled answer over the keeper card did not answer it");
        } else {
            r2_card_on(T.clock - 5); r2_frames_link(400); G2.ask.on = false; r2_frames_link(FRAME_MS);   /* its time runs out */
            tap((float)sx, (float)by, 90, FRAME_MS);           /* a tap meant for the card, just as it went */
            x = (float)sx; y = (float)by;
        }
        if (T.n_fish != n0) TFAIL("R2T-1: %s - the fish whose SELL was armed under it was SOLD (%d -> %d)", how ? "the gift card went by itself under a tap" : "the gift card's answer doubled", n0, T.n_fish);
        n++;
        G2.ask.on = false; G2.ask.answer = 0; r2_fish(4);
    }
    /* the shop: a row's modal open, UNLOCK under the card */
    float rx, ry;
    bare(); T.sd_balance = 100000; T.sd_unlocks = 0; T.sd_shelved = 0; touch_port_show_shop(true); hold_for(200); render_shop_leave();
    if (shop_row_under_button(&rx, &ry) >= 0) {
        bare(); T.sd_balance = 100000; T.sd_unlocks = 0; T.sd_shelved = 0; touch_port_show_shop(true); hold_for(800);
        tap(rx, ry, 90, 1000);
        const float ux = PAGE_X + SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2 + MSP_HOW_W / 2.0f, uy = PAGE_Y + SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H / 2.0f;
        if (r2_near_gift(ux, uy, MSP_HOW_W / 2, MSP_HOW_H / 2, NULL, &x, &y)) {
            touch_port_take_shop(); r2_card_on(T.clock - 5); r2_card_double(x, y); int r = touch_port_take_shop();
            if (r >= SHOP_TAP_BUY && r < SHOP_TAP_MOVE) TFAIL("R2T-1: the gift card's answer doubled BOUGHT the shop piece whose modal stood under it");
            lift(700); tap(ux, uy, 90, FRAME_MS); r = touch_port_take_shop();
            if (!(r >= SHOP_TAP_BUY && r < SHOP_TAP_MOVE)) TFAIL("R2T-1: UNLOCK 0.7 s after the card went did not buy (%d)", r);
            n++;
        }
    }
    G2.ask.on = false; G2.ask.answer = 0; render_shop_leave(); T.sd_unlocks = 0;
    /* UPDATES's CHECK */
    if (r2_near_gift(TANK_W / 2.0f, TANK_H / 2.0f, TANK_W / 2, TANK_H / 2, ok_check, &x, &y)) {
        bare(); touch_port_show_updates(true); hold_for(800); touch_port_take_update();
        r2_card_on(T.clock - 5); r2_card_double(x, y); int a = touch_port_take_update();
        if (a == UPD_TAP_CHECK) TFAIL("R2T-1: the gift card's answer doubled tapped UPDATES's CHECK FOR UPDATES under it");
        lift(700); tap(x, y, 90, FRAME_MS); a = touch_port_take_update();
        if (a != UPD_TAP_CHECK) TFAIL("R2T-1: CHECK 0.7 s after the card went was not tapped (%d)", a);
        G2.ask.on = false; n++;
    }
    /* settings' two-way rows */
    static const int ROWS[4] = { SET_TAP_FEED, SET_TAP_SCREEN, SET_TAP_ROTATE, SET_TAP_GFX };
    static const char *const ROWN[4] = { "AUTO FEED", "SCREEN", "ROTATION", "GRAPHICS" };
    for (int r = 0; r < 4; r++) { g_set_want = ROWS[r];
        if (!r2_near_gift(TANK_W / 2.0f, TANK_H / 2.0f, TANK_W / 2, TANK_H / 2, ok_set, &x, &y)) continue;
        bare(); touch_port_show_settings(true); hold_for(800); prime(x, y); pagestate_t s0 = pagestate();
        r2_card_on(T.clock - 5); r2_card_double(x, y); pagestate_t s1 = pagestate(); drain_setting();
        if (!same(s0, s1)) TFAIL("R2T-1: the gift card's answer doubled changed settings' %s under it", ROWN[r]);
        G2.ask.on = false; n++;
    }
    /* a visitor's passport: DEPORT */
    {   const int v = N_FISH_MAX;
        T.fish[v] = T.fish[0]; T.fish[v].uid = 0x5151u; T.fish[v].homeward = T.fish[v].deported = false; T.vis_used[0] = true;
        if (r2_near_gift(TANK_W / 2.0f, TANK_H / 2.0f, TANK_W / 2, TANK_H / 2, ok_deport, &x, &y)) {
            bare(); T.vis_used[0] = true; card_up(v); frame(); hold_for(3000);
            r2_card_on(T.clock - 2.5); r2_card_double(x, y);
            if (T.fish[v].deported) TFAIL("R2T-1: the gift card's answer doubled DEPORTED the visitor whose passport stood under it");
            n++;
        }
        T.vis_used[0] = false; memset(&T.fish[v], 0, sizeof T.fish[v]); T.deport_uid = 0; s_sel = -1; G2.ask.on = false;
    }
    memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
    printf("selftest-touch-port: R2T-1 the gift card's answer doubled - %d (an armed keeper SELL, and the card going by itself under a tap; "
           "the shop's UNLOCK; UPDATES's CHECK; settings' rows; DEPORT - each where a card button stands over it): none acted, a tap 0.7 s on did\n", n);
}

/* R2T-5. MY TANKS'S WAYS OUT (TCH-4 / TCH-5's siblings): main.c's half emulated as it reads - shelf_close brings the keeper page
   back (touch_port_show_milestones), and THE LIBRARY's BACK, a card's outside tap and MOVE's end bring MY TANKS's main page back
   (touch_port_shelf_back). Each doubled: the second half reaches nothing; 0.7 s on, a tap does. */
static void r2g_shelf_exits(void) {
    float x, y, sx, sy; int64_t sp; int n = 0;
    /* MY TANKS's CLOSE, opened from the keeper foot: the keeper page comes back */
    if (find_hit_aim(hit_shelf, SHELF_TAP_CLOSE, &x, &y)) {
        bare(); touch_port_show_milestones(true); hold_for(800);
        touch_port_dismiss(); touch_port_show_shelf(true); hold_for(800);   /* (main: the foot's MY TANKS, s_shelf_from_ms) */
        tap(x, y, 90, FRAME_MS);
        bool t1 = touch_port_take_shelf_tap(&sx, &sy, &sp);
        if (!t1 || render_shelf_tap(sx, sy) != SHELF_TAP_CLOSE) TFAIL("R2T-5: MY TANKS's CLOSE was no tap on CLOSE");
        else {
            touch_port_show_shelf(false); touch_port_show_milestones(true);   /* main.c shelf_close: back to the keeper page */
            hold_for(60); tap(x, y, 90, FRAME_MS);
            if (!touch_port_milestones()) TFAIL("R2T-5: a doubled MY TANKS CLOSE also shut the keeper page it went back to (shop %d, collection %d, settings %d)", touch_port_shop(), touch_port_collection(), touch_port_settings());
            else { float kx, ky; foot(MSF_CLOSE, &kx, &ky); lift(700); tap(kx, ky, 90, FRAME_MS);
                   if (touch_port_milestones()) TFAIL("R2T-5: the keeper page's CLOSE 0.7 s after MY TANKS closed did not close it"); }
            n++;
        }
    }
    /* THE LIBRARY's BACK (it stands where CLOSE does), a card's outside tap on the padlock, MOVE ended on a place: main.c puts
       MY TANKS's main page back with touch_port_shelf_back, and the second half is no MY TANKS tap */
    static const char *const WAY[3] = { "THE LIBRARY's BACK", "a card's outside tap (on the padlock)", "MOVE's end (on a place)" };
    for (int w = 0; w < 3; w++) {
        bool ok = w == 0 ? find_hit_aim(hit_shelf, SHELF_TAP_CLOSE, &x, &y) : w == 1 ? find_hit_aim(hit_shelf, SHELF_TAP_TOUCH_LOCK, &x, &y)
                : find_hit_aim(hit_shelf, SHELF_TAP_SLOT0 + 1, &x, &y);
        if (!ok) { TFAIL("R2T-5: no spot for %s", WAY[w]); continue; }
        bare(); touch_port_show_shelf(true); hold_for(800);
        tap(x, y, 90, FRAME_MS);
        if (!touch_port_take_shelf_tap(&sx, &sy, &sp)) { TFAIL("R2T-5: %s was no MY TANKS tap", WAY[w]); continue; }
        touch_port_shelf_back();                              /* main.c: s_sh_lib / s_sh_card / s_sh_moving off - the main page is back */
        hold_for(60); tap(x, y, 90, FRAME_MS);
        if (touch_port_take_shelf_tap(&sx, &sy, &sp)) TFAIL("R2T-5: %s doubled - the second half reached MY TANKS's main page (%s)", WAY[w],
                                                            render_shelf_tap(sx, sy) == SHELF_TAP_TOUCH_LOCK ? "the padlock: TOUCH LOCKED" : render_shelf_tap(sx, sy) == SHELF_TAP_CLOSE ? "CLOSE" : "a place");
        lift(700); tap(x, y, 90, FRAME_MS);
        if (!touch_port_take_shelf_tap(&sx, &sy, &sp)) TFAIL("R2T-5: a tap 0.7 s after %s was no MY TANKS tap", WAY[w]);
        n++;
    }
    touch_port_show_shelf(false); bare();
    printf("selftest-touch-port: R2T-5 MY TANKS's ways out doubled - %d (CLOSE back to the keeper page, THE LIBRARY's BACK, a card's outside "
           "tap on the padlock, MOVE's end): the second half reached nothing, a tap 0.7 s on did\n", n);
}

/* R2-1. THE PLACEMENT PAGE'S SELL (TCH-1 missed this page): SELL tapped (+N OK?), then a press 100 ms on held 100-900 ms, or a
   4-tap mash - the piece stays; 0.7 s after, a tap sells it. Every placeable piece, the first one in the tank. */
static int hit_setup_g(float x, float y, int want) { return setup_hit(x, y) == want; }
static void r2g_place_sell(void) {
    int n = 0, item = -1;
    for (int i = 0; i < SD_ITEM_COUNT && item < 0; i++) if (tank_decor_placeable(i)) item = i;
    if (item < 0) { TFAIL("R2-1: no placeable piece"); return; }
    for (int h = 0; h < 5; h++) {   /* (the mash case went with THE TOUCH RULE: its later taps are deliberate) */
        bare(); T.sd_unlocks |= SD_ITEMS[item].bit; T.sd_shelved &= ~SD_ITEMS[item].bit; T.sd_locked &= ~SD_ITEMS[item].bit;
        tank_decor_set(&T, item, 200, DECOR_Z_BACK);
        setup_begin_place(&T, item); hold_for(200);
        float x, y; if (!find_hit(hit_setup_g, SETUP_HIT_SELL, &x, &y)) { TFAIL("R2-1: no SELL on the placement page"); setup_cancel(&T); return; }
        if (h < 5) firm_tap_then_press(x, y, FIRM_HOLDS[h]);
        else { tap(x, y, 90, FIRM_GAP); mash(x, y, 4); }
        const bool sold = !(T.sd_unlocks & SD_ITEMS[item].bit);
        if (sold) TFAIL("R2-1: the placement page's SELL tapped, then %s %d ms - the %s was SOLD", h < 5 ? "pressed again 100 ms on and held" : "mashed 4 taps over", h < 5 ? FIRM_HOLDS[h] : 800, SD_ITEMS[item].name);
        else if (h == 4) { lift(700); tap(x, y, 90, FRAME_MS);
            if (T.sd_unlocks & SD_ITEMS[item].bit) TFAIL("R2-1: SELL 0.7 s after %s did not sell the %s", h == 4 ? "the firm press" : "the mash", SD_ITEMS[item].name); }
        if (setup_active()) setup_cancel(&T);
        T.sd_unlocks |= SD_ITEMS[item].bit; n++;
    }
    bare();
    printf("selftest-touch-port: R2-1 the placement page's SELL - %d (tapped, then a press 100 ms on held 100-900 ms, or a 4-tap mash): "
           "the piece stayed, SELL 0.7 s on sold it\n", n);
}

/* R2-6. THE GIVE MODAL'S STEP STAMPED BY AN EVENT: the modal is ASKING and a finger is on CALL OFF; the other keeper's NO THANKS
   comes mid-press (SAID, its step stamped then). The lift closed the SAID line unread - a press older than the step. Now it is
   spent; OK after its 2 s hold closes it. */
static void r2g_give_step(void) {
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(3);
    T.door_open[LINK_LEFT] = true; G2.door_peer[LINK_LEFT] = 0x49439648u; G2.door_badge[LINK_LEFT] = 1;
    int f0 = -1, n = 0; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 < 0) TFAIL("R2-6: no fish to give");
    else for (int hold = 400; hold <= 2400; hold += 2000) {
        bare(); render_give_tap(&T, f0); hold_for(2200);
        int gx, gy; render_give_modal_button(&T, RENDER_GM_TAP_GIVE, &gx, &gy);
        tap((float)gx, (float)gy, 90, FRAME_MS); hold_for(2300);
        int cx, cy;
        if (render_give_modal_step(&T) != RENDER_GM_ASKING || !render_give_modal_button(&T, RENDER_GM_TAP_CALLOFF, &cx, &cy)) { TFAIL("R2-6: GIVE did not ask (step %d)", render_give_modal_step(&T)); break; }
        press((float)cx, (float)cy, 400);                     /* a finger on CALL OFF ... */
        const uint32_t uid = T.fish[f0].uid; G2.want_uid = 0; G2.asking_uid = 0; G2.wait_uid = 0;
        G2.said_uid = uid; G2.said_reason = 0; G2.said_until = T.clock + GIFT_SAID_S;   /* ... as the other keeper says NO THANKS */
        hold_for(hold); lift(FRAME_MS);
        if (render_give_modal_step(&T) == RENDER_GM_OFF) TFAIL("R2-6: CALL OFF pressed, THEY SAID NO THANKS came mid-press, lifted %d ms on - the SAID line closed unread", hold);
        else { int ox, oy; if (render_give_modal_button(&T, RENDER_GM_TAP_OK, &ox, &oy)) { lift((int)(GIFT_HOLD_S * 1000) + 100); tap((float)ox, (float)oy, 90, FRAME_MS);
                   if (render_give_modal_step(&T) != RENDER_GM_OFF) TFAIL("R2-6: OK after the SAID line's hold did not close it (step %d)", render_give_modal_step(&T)); } }
        n++;
        render_give_modal_close(); G2.said_uid = 0;
    }
    render_give_modal_close(); T.door_open[LINK_LEFT] = false; memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
    printf("selftest-touch-port: R2-6 a press older than the GIVE modal's step - %d (CALL OFF held as THEY SAID NO THANKS came, lifted 0.4 / 2.4 s on): "
           "the SAID line stayed, OK after its hold closed it\n", n);
}

/* R2T-2. THE GIFT CARD BACK FROM UNDER A PAGE (the birth flow, the reset prompt, ADVANCED: render_gift_allow false): its 2 s hold
   was counted from when it was first seen, long before, so a doubled closing tap answered it. Now it comes back with a fresh hold,
   as the GIVE modal does. Every board: hidden 30 s, back, ACCEPT / NO THANKS tapped at once is spent, 2.1 s on it answers; and the
   birth flow's DONE doubled where DONE stands over a card button. */
static void r2g_gift_back(void) {
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(3);
    float x, y; int n = 0;
    for (int b = 0; b < 2; b++) {
        if (!find_hit(hit_gift, b ? GIFT_HIT_NO : GIFT_HIT_ACCEPT, &x, &y)) { TFAIL("R2T-2: the gift card has no %s", b ? "NO THANKS" : "ACCEPT"); continue; }
        bare(); r2_card_on(T.clock - 30); render_gift_allow(false); hold_for(400);   /* under a page, long after it was first seen */
        render_gift_allow(true); hold_for(FRAME_MS);                                 /* back */
        tap(x, y, 90, FRAME_MS);
        if (G2.ask.answer) TFAIL("R2T-2: the gift card back from under a page answered a tap at once (%s: %d) - its hold was spent long ago", b ? "NO THANKS" : "ACCEPT", G2.ask.answer);
        lift((int)(GIFT_HOLD_S * 1000) + 100); tap(x, y, 90, FRAME_MS);
        if (G2.ask.answer != (b ? -1 : 1)) TFAIL("R2T-2: %s 2.1 s after the card came back did not answer (%d)", b ? "NO THANKS" : "ACCEPT", G2.ask.answer);
        G2.ask.on = false; G2.ask.answer = 0; n++;
    }
    /* the birth flow's last page: DONE doubled over a card that waited under it */
    bare(); r2_card_on(T.clock - 30); render_gift_allow(false);
    setup_begin_birth(&T, T.n_fish - 1);
    for (int k = 0; k < SETUP_BIRTH_PAGES - 1 && setup_active(); k++) setup_activate(&T, SETUP_HIT_NEXT);
    float px = -1, py = -1; double mx = 0, my = 0; int mn = 0; float bd = 1e18f;
    for (int yy = 0; yy < TANK_H; yy += 2) for (int xx = 0; xx < TANK_W; xx += 2)
        if (setup_hit((float)xx, (float)yy) == SETUP_HIT_NEXT && (render_gift_hit((float)xx, (float)yy) == GIFT_HIT_ACCEPT || render_gift_hit((float)xx, (float)yy) == GIFT_HIT_NO)) { mx += xx; my += yy; mn++; }
    if (mn) { mx /= mn; my /= mn;
        for (int yy = 0; yy < TANK_H; yy += 2) for (int xx = 0; xx < TANK_W; xx += 2) {
            float d = (float)((xx - mx) * (xx - mx) + (yy - my) * (yy - my));
            if (d < bd && setup_hit((float)xx, (float)yy) == SETUP_HIT_NEXT && aim((float)xx, (float)yy) && setup_hit(g_ax, g_ay) == SETUP_HIT_NEXT
                && (render_gift_hit(g_ax, g_ay) == GIFT_HIT_ACCEPT || render_gift_hit(g_ax, g_ay) == GIFT_HIT_NO)) { bd = d; px = (float)xx; py = (float)yy; } } }
    if (px < 0) printf("selftest-touch-port: (R2T-2: the birth DONE stands over no card button here)\n");
    else {
        r2_frames(200);
        r2_tap(px, py, 90, 100); const bool closed = !setup_active();   /* DONE: the flow ends at its lift */
        r2_tap(px, py, 90, FRAME_MS);                                     /* the double's second half */
        if (!closed) TFAIL("R2T-2: the birth flow's DONE did not close it");
        if (G2.ask.answer) TFAIL("R2T-2: the birth flow's DONE doubled answered the gift card that waited under it (%d)", G2.ask.answer);
        n++;
    }
    if (setup_active()) setup_cancel(&T);
    render_gift_allow(true); G2.ask.on = false; memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
    printf("selftest-touch-port: R2T-2 the gift card back from under a page - %d (ACCEPT / NO THANKS at once after 30 s hidden; the birth DONE "
           "doubled over it): none answered, 2.1 s on each did\n", n);
}

/* R2T-3. A PRESS THE GIFT CARD OR BOOT TWICE CUT, READ LATER: settings, UPDATES and link mode read the finger themselves and keep
   their own press. Cut (the card came, the page closed), the press stayed in hand, and the next feed's "up" was its release: CHECK
   FOR UPDATES once the card went (or the next time UPDATES opened), a settings row flipped by itself, link mode's DONE. Now a page
   not fed forgets its press. 0.7 s on, a fresh tap acts. */
static int hit_link_done(float x, float y, int u) { (void)u; static link_view_t v; render_link_touch(&v, x, y, true, 0); return render_link_touch(&v, x, y, false, 0) == LINK_UI_DONE; }
static void r2g_stale_lift(void) {
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2);
    float x, y; int n = 0;
    if (!find_hit(hit_updates, UPD_TAP_CHECK, &x, &y)) TFAIL("R2T-3: no CHECK");
    else {
        /* the card comes mid-press on CHECK; the finger lifts on the card; the card goes */
        bare(); touch_port_show_updates(true); hold_for(800); touch_port_take_update();
        press(x, y, 200); r2_card_on(T.clock); hold_for(300); lift(400);
        int a1 = touch_port_take_update();
        G2.ask.on = false; hold_for(200);
        int a2 = touch_port_take_update();
        if (a1 == UPD_TAP_CHECK || a2 == UPD_TAP_CHECK) TFAIL("R2T-3: CHECK pressed, a gift card came mid-press, lifted on the card - CHECK FOR UPDATES %s", a1 ? "at the lift" : "once the card went");
        lift(700); tap(x, y, 90, FRAME_MS);
        if (touch_port_take_update() != UPD_TAP_CHECK) TFAIL("R2T-3: CHECK after the card went was not tapped");
        n++;
        /* BOOT twice mid-press on CHECK; UPDATES opened again 5 s later */
        bare(); touch_port_show_updates(true); hold_for(800);
        press(x, y, 200); touch_port_dismiss(); hold_for(200); lift(400); touch_port_take_update();
        hold_for(5000); touch_port_show_updates(true); hold_for(800);
        if (touch_port_take_update() == UPD_TAP_CHECK) TFAIL("R2T-3: CHECK pressed, BOOT twice closed UPDATES mid-press - opened again 5 s later, CHECK FOR UPDATES at once");
        tap(x, y, 90, FRAME_MS);
        if (touch_port_take_update() != UPD_TAP_CHECK) TFAIL("R2T-3: CHECK on UPDATES opened again was not tapped");
        n++;
    }
    /* THE UPDATES PAGE'S TWO FACES through the port (0.4.2, update.h): the door, ADD A NETWORK handed to main once, the door's
       double spent, WI-FI's BACK keeps the UPDATES page up (and its double does not close it), BACK there is settings */
    {   float dx, dy, ax, ay, bx, by;
        bare(); touch_port_show_updates(true); hold_for(800); touch_port_take_update();
        const bool got = find_hit(hit_updates, UPD_TAP_NETS, &dx, &dy) && find_hit(hit_updates, UPD_TAP_CLOSE, &bx, &by);
        updates_page_show_nets(true); const bool got2 = find_hit(hit_updates, UPD_TAP_ADD, &ax, &ay); updates_page_show_nets(false);
        if (!got || !got2) TFAIL("the UPDATES page: no WI-FI door, BACK or ADD A NETWORK to aim at");
        else {
            tap(dx, dy, 90, FRAME_MS);
            if (!updates_page_nets() || !touch_port_updates() || touch_port_take_update()) TFAIL("the WI-FI door: tapped, the WI-FI page is not up (or main was handed something)");
            hold_for(60); tap(dx, dy, 60, 400);                    /* the door's double: ADD A NETWORK is under part of it */
            if (touch_port_take_update() || !updates_page_nets()) TFAIL("the WI-FI door's double answered on the WI-FI page");
            tap(ax, ay, 90, 200);
            if (touch_port_take_update() != UPD_TAP_ADD) TFAIL("ADD A NETWORK was not handed to main");
            if (touch_port_take_update()) TFAIL("ADD A NETWORK was handed to main twice");
            tap(bx, by, 90, FRAME_MS);
            if (updates_page_nets() || !touch_port_updates() || touch_port_settings()) TFAIL("WI-FI's BACK: the UPDATES face is not up (WI-FI %d, updates %d, settings %d)", updates_page_nets(), touch_port_updates(), touch_port_settings());
            hold_for(60); tap(bx, by, 60, 400);
            if (!touch_port_updates() || touch_port_settings()) TFAIL("WI-FI's BACK doubled closed the UPDATES page too");
            tap(bx, by, 90, 400);
            if (touch_port_updates() || !touch_port_settings()) TFAIL("the UPDATES page's BACK did not bring settings back");
            n++;
        }
    }
    /* settings: a two-way row pressed, the card mid-press, it times out */
    static const int ROWS[2] = { SET_TAP_FEED, SET_TAP_SCREEN };
    for (int r = 0; r < 2; r++) {
        if (!find_hit(hit_settings, ROWS[r], &x, &y)) continue;
        bare(); touch_port_show_settings(true); hold_for(800); prime(x, y); pagestate_t s0 = pagestate();
        press(x, y, 200); r2_card_on(T.clock); hold_for(300); lift(400);
        G2.ask.on = false; hold_for(200); pagestate_t s2 = pagestate(); drain_setting();
        if (!same(s0, s2)) TFAIL("R2T-3: settings' %s pressed, a gift card came mid-press - the row flipped by itself as the card went", r ? "SCREEN" : "AUTO FEED");
        n++;
    }
    /* link mode: DONE pressed, the card mid-press, lifted on the card, the card goes */
    {   float dx, dy;
        if (!find_hit_aim(hit_link_done, 0, &dx, &dy)) printf("selftest-touch-port: (R2T-3: link mode's DONE cannot be reached here)\n");
        else {
            bare(); g_link_page = true; hold_for(800); g_link_ui = 0;
            press(dx, dy, 200); r2_card_on(T.clock); hold_for(300); lift(400);
            G2.ask.on = false; hold_for(200);
            if (g_link_ui == LINK_UI_DONE) TFAIL("R2T-3: link mode's DONE pressed, a gift card came mid-press, lifted on the card - DONE once the card went");
            g_link_ui = 0; lift(700); tap(dx, dy, 90, FRAME_MS);
            if (g_link_ui != LINK_UI_DONE) TFAIL("R2T-3: link mode's DONE after the card went was not tapped (%d)", g_link_ui);
            g_link_page = false; n++;
        }
    }
    G2.ask.on = false; memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
    printf("selftest-touch-port: R2T-3 a press the gift card or BOOT twice cut - %d (UPDATES's CHECK cut by the card and by BOOT twice, settings' "
           "rows, link mode's DONE): none read later, a fresh tap acted\n", n);
}

/* R2T-3's regression (the diff review's Finding 1): A SLIDER CUT MID-DRAG. BRIGHTNESS and VOLUME only preview while the finger
   moves (SET_TAP_*_LIVE: RAM); the release's SET_TAP_BRIGHT / SET_TAP_VOLUME is the save (NVS). Cut - the gift card, BOOT twice,
   a birth flow - the press was forgotten and the level on the glass never saved. Each slider through each cut, and with none:
   exactly one save, of the step the glass last showed; BOOT twice, then settings opened again later, saves nothing more. main.c
   is played as it reads: every frame it takes the setting (touch_port_take_setting). (The BOOT chord into ADVANCED is no drag:
   r2g_chord_tap.) */
static int g_sl_saves, g_sl_saved, g_sl_shown;
static void sl_frames(int ms) {
    for (int k = 0; k < (ms + FRAME_MS - 1) / FRAME_MS; k++) {
        frame(); int v = 0, w = touch_port_take_setting(&v);
        if (w == SET_TAP_BRIGHT_LIVE || w == SET_TAP_VOLUME_LIVE) g_sl_shown = v;
        else if (w == SET_TAP_BRIGHT || w == SET_TAP_VOLUME) { g_sl_saves++; g_sl_saved = v; }
    }
}
static void r2g_slider_cut(void) {
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(3);
    static const char *const CUT[4] = { "no cut (lifted on the page)", "the gift card", "BOOT twice (settings opened again 2 s on)", "a birth flow" };
    int n = 0;
    for (int sl = 0; sl < 2; sl++) {
        const int want = sl ? SET_TAP_VOLUME : SET_TAP_BRIGHT;
        float x, y; if (!find_hit(hit_settings, want, &x, &y)) { TFAIL("slider cut: settings has no %s", sl ? "VOLUME" : "BRIGHTNESS"); continue; }
        float x2 = -1; for (int dx = 80; dx >= 30 && x2 < 0; dx -= 10) for (int sg = -1; sg <= 1 && x2 < 0; sg += 2) {
            int v0, v1; float xx = x + sg * dx; if (render_settings_tap(xx, y, &v1) == want && aim(xx, y) && render_settings_tap(x, y, &v0) == want && v0 != v1) x2 = xx; }
        if (x2 < 0) { TFAIL("slider cut: no second step along %s", sl ? "VOLUME" : "BRIGHTNESS"); continue; }
        for (int c = 0; c < 4; c++) {
            bare(); touch_port_show_settings(true); hold_for(800); { int v; touch_port_take_setting(&v); }
            g_sl_saves = 0; g_sl_saved = g_sl_shown = -1;
            if (!aim(x, y)) continue;
            g_down = true; sl_frames(200);                    /* the press: the knob jumps to the finger */
            aim(x2, y); sl_frames(240);                        /* the drag: the knob follows */
            const int shown = g_sl_shown;
            if (c == 1) { r2_card_on(T.clock); sl_frames(300); }
            else if (c == 2) { touch_port_dismiss(); sl_frames(300); }
            else if (c == 3) { setup_begin_birth(&T, T.n_fish - 1); sl_frames(300); }
            g_down = false; sl_frames(200);                    /* the lift (on the page, or on what cut it) */
            if (c == 1) { G2.ask.on = false; sl_frames(300); }
            if (c == 2) { sl_frames(2000); touch_port_show_settings(true); sl_frames(800); }
            if (c == 3) { if (setup_active()) setup_cancel(&T); sl_frames(300); }
            if (g_sl_saves != 1 || g_sl_saved != shown)
                TFAIL("slider cut: %s dragged to %d, cut by %s - saved %d time%s (last %d)", sl ? "VOLUME" : "BRIGHTNESS", shown, CUT[c], g_sl_saves, g_sl_saves == 1 ? "" : "s", g_sl_saved);
            n++;
        }
    }
    touch_port_show_settings(false); while (progression_newborn() >= 0) progression_newborn_done(&T);
    memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
    printf("selftest-touch-port: a slider cut mid-drag (R2T-3's regression) - %d (BRIGHTNESS, VOLUME x no cut, the gift card, BOOT twice, a birth "
           "flow): each saved once, the step the glass showed\n", n);
}
/* THE BOOT CHORD'S OWN TAP ON A SLIDER (the second review, DR2-touch.md): the chord fires only on a press begun after BOOT was held
   5 s, so the slider "in hand" as ADVANCED opens is that tap's jump, never a drag - saving it saved a level nobody chose (the watch:
   VOLUME's left, mute; and the save's chime). The port saves nothing and names the slider; main.c puts the saved level back
   (brightness_restore / audio_port_restore_volume: no NVS, no sound) - played here as main.c reads, with its saved and shown levels. */
static void r2g_chord_tap(void) {
    int n = 0;
    for (int sl = 0; sl < 2; sl++) {
        const int want = sl ? SET_TAP_VOLUME : SET_TAP_BRIGHT;
        float x, y; if (!find_hit(hit_settings, want, &x, &y)) { TFAIL("chord tap: settings has no %s", sl ? "VOLUME" : "BRIGHTNESS"); continue; }
        for (int side = 0; side < 4; side++) {               /* the row's left end (VOLUME: mute) and its right; each with the knob's
                                                                jump taken by main.c before the chord (side 0, 1), and still in the
                                                                port's slot as the chord fires (side 2, 3: the press in the same
                                                                frame's later poll - the firmware polls up to three times a take) */
            const bool untaken = side >= 2;
            float tx = -1; int tv = -1;
            const bool right = side & 1;
            for (int dx = 0; dx < 200 && tx < 0; dx += 4) { float xx = right ? x + dx : x - dx; int v;
                if (render_settings_tap(xx, y, &v) == want && aim(xx, y) && (right ? v >= 80 : v <= (sl ? 0 : 20))) { tx = xx; tv = v; } }
            if (tx < 0) { printf("selftest-touch-port: (chord tap: no %s end of %s here)\n", right ? "right" : "left", sl ? "VOLUME" : "BRIGHTNESS"); continue; }
            bare(); touch_port_show_settings(true); hold_for(800); { int v; touch_port_take_setting(&v); }
            const int saved = 50; int shown = saved;          /* main.c: the level saved (NVS), the level on the glass */
            g_sl_saves = 0; g_sl_saved = g_sl_shown = -1;
            aim(tx, y); g_down = true;                         /* the chord's tap: settings' press, the knob jumps (previewed) */
            if (untaken) frame(); else sl_frames(120);         /* (untaken: the jump's preview still in the port's slot) */
            if (g_sl_shown >= 0) shown = g_sl_shown;
            const int w = touch_port_open_advanced();         /* main.c sleep_button_poll: BOOT held 5 s + this tap */
            if (w == SET_TAP_BRIGHT || w == SET_TAP_VOLUME) shown = saved;   /* main.c: brightness_restore / audio_port_restore_volume */
            g_sl_shown = -1; sl_frames(200); if (g_sl_shown >= 0) shown = g_sl_shown;
            g_down = false; sl_frames(200); if (g_sl_shown >= 0) shown = g_sl_shown;
            adv_close(); sl_frames(300); if (g_sl_shown >= 0) shown = g_sl_shown;
            if (g_sl_saves) TFAIL("chord tap: BOOT held + a tap at %s's %s end (%d%s) - saved %d (a level nobody chose%s)", sl ? "VOLUME" : "BRIGHTNESS", right ? "right" : "left", tv,
                                  untaken ? ", its preview untaken" : "", g_sl_saved, sl ? ", and its chime" : "");
            if (w != want) TFAIL("chord tap: touch_port_open_advanced named %d, not the slider the tap was on (%d) - main.c cannot put the level back", w, want);
            if (shown != saved) TFAIL("chord tap: %s shows %d after ADVANCED opened%s, the saved level is %d", sl ? "VOLUME" : "BRIGHTNESS", shown,
                                      untaken ? " (its preview was still untaken: main.c took it after the restore)" : "", saved);
            n++;
        }
    }
    bare();
    printf("selftest-touch-port: the BOOT chord's own tap on a slider - %d (BRIGHTNESS, VOLUME x its ends x the jump's preview taken or still "
           "in the port's slot): nothing saved, the slider named, the saved level back on the knob\n", n);
}
/* NO SAVE WITH NO SLIDER IN HAND (the second review's test gap: render_settings_cut without its s_set_down guard saved every ordinary
   release again when settings next stopped being fed): a drag, lifted on the page (one save), then settings closed - its CLOSE,
   BOOT twice, the gift card over it - and opened again: still one save. */
static void r2g_slider_no_resave(void) {
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2);
    float cx, cy; const bool has_close = find_hit(hit_settings, SET_TAP_CLOSE, &cx, &cy);
    int n = 0;
    for (int sl = 0; sl < 2; sl++) {
        const int want = sl ? SET_TAP_VOLUME : SET_TAP_BRIGHT;
        float x, y; if (!find_hit(hit_settings, want, &x, &y)) continue;
        float x2 = -1; for (int dx = 80; dx >= 30 && x2 < 0; dx -= 10) for (int sg = -1; sg <= 1 && x2 < 0; sg += 2) {
            int v0, v1; float xx = x + sg * dx; if (render_settings_tap(xx, y, &v1) == want && aim(xx, y) && render_settings_tap(x, y, &v0) == want && v0 != v1) x2 = xx; }
        if (x2 < 0) continue;
        for (int c = 0; c < 3; c++) {
            if (c == 0 && !has_close) continue;
            bare(); touch_port_show_settings(true); hold_for(800); { int v; touch_port_take_setting(&v); }
            g_sl_saves = 0; g_sl_saved = g_sl_shown = -1;
            aim(x, y); g_down = true; sl_frames(200); aim(x2, y); sl_frames(240); g_down = false; sl_frames(200);   /* drag, lift: the save */
            if (c == 0) { lift(700); aim(cx, cy); g_down = true; sl_frames(90); g_down = false; sl_frames(200); }
            else if (c == 1) { touch_port_dismiss(); sl_frames(300); }
            else { r2_card_on(T.clock); sl_frames(300); G2.ask.on = false; sl_frames(300); }
            touch_port_show_settings(true); sl_frames(800); touch_port_show_settings(false); sl_frames(300);
            if (g_sl_saves != 1) TFAIL("no resave: %s dragged and lifted, then settings %s and opened again - saved %d times", sl ? "VOLUME" : "BRIGHTNESS",
                                       c == 0 ? "CLOSEd" : c == 1 ? "shut by BOOT twice" : "under the gift card", g_sl_saves);
            n++;
        }
    }
    memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
    printf("selftest-touch-port: no save with no slider in hand - %d (a drag lifted on the page, then CLOSE / BOOT twice / the gift card, settings "
           "opened again): one save each\n", n);
}

/* THE DIFF REVIEW'S COVERAGE NOTES (DR-touch.md), gated: R2T-1's card-gone check also stands before the battery page, MY TANKS,
   the notice, the GIVE modal and the bare water - each meets the card's answer doubled (where a card button stands over it) and
   keeps; a tap 0.7 s on acts. (Moved below `if (s_shelf)` the MY TANKS case goes red; below `if (s_bat)` the battery page's.) */
static int ok_water(float x, float y) { return !RENDER_BAT_HIT(x, y) && !render_tool_chip_hit(&T, x, y); }
static void r2g_card_gone_more(void) {
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(3);
    float x, y, sx, sy; int64_t sp; int n = 0;
    /* the battery page (any release closes it) */
    if (r2_near_gift(TANK_W / 2.0f, TANK_H / 2.0f, TANK_W / 2, TANK_H / 2, NULL, &x, &y)) {
        bare(); touch_port_show_battery(true); hold_for(200);
        r2_card_on(T.clock - 5); r2_card_double(x, y);
        if (!touch_port_battery()) TFAIL("R2T-1+: the gift card's answer doubled closed the battery page under it");
        lift(700); tap(x, y, 90, FRAME_MS);
        if (touch_port_battery()) TFAIL("R2T-1+: a tap 0.7 s after the card went did not close the battery page");
        G2.ask.on = false; n++;
    }
    /* MY TANKS: a place under the card */
    if (r2_near_gift(TANK_W / 2.0f, TANK_H / 2.0f, TANK_W / 2, TANK_H / 2, ok_shelf_slot, &x, &y)) {
        bare(); touch_port_show_shelf(true); hold_for(800);
        r2_card_on(T.clock - 5); r2_card_double(x, y);
        if (touch_port_take_shelf_tap(&sx, &sy, &sp)) TFAIL("R2T-1+: the gift card's answer doubled reached MY TANKS under it (a place at %.0f,%.0f)", sx, sy);
        lift(700); tap(x, y, 90, FRAME_MS);
        if (!touch_port_take_shelf_tap(&sx, &sy, &sp)) TFAIL("R2T-1+: a tap 0.7 s after the card went was no MY TANKS tap");
        touch_port_show_shelf(false); G2.ask.on = false; n++;
    }
    /* an announcement over the bare tank (it waits for a tap) */
    if (r2_near_gift(TANK_W / 2.0f, TANK_H / 2.0f, TANK_W / 2, TANK_H / 2, ok_water, &x, &y)) {
        bare(); notice_sync(&T); notice_low_battery(); for (int k = 0; k < 40 && !notice_current(); k++) notice_tick(&T, 0.1f, false);
        for (int k = 0; k < 30; k++) notice_tick(&T, 0.1f, false);   /* (past its own 2 s hold) */
        if (!notice_current()) TFAIL("R2T-1+: no notice came up");
        else {
            r2_card_on(T.clock - 5); r2_card_double(x, y);
            if (!notice_current()) TFAIL("R2T-1+: the gift card's answer doubled closed the announcement under it, unread");
            lift(700); tap(x, y, 90, FRAME_MS);
            if (notice_current()) TFAIL("R2T-1+: a tap 0.7 s after the card went did not close the announcement");
            n++;
        }
        notice_sync(&T); G2.ask.on = false;
    }
    /* the bare water: the light's double tap counts taps there */
    if (r2_near_gift(TANK_W / 2.0f, TANK_H / 2.0f, TANK_W / 2, TANK_H / 2, ok_water, &x, &y)) {
        bare(); T.tap_count = 0;
        r2_card_on(T.clock - 5); r2_card_double(x, y);
        if (T.tap_count) TFAIL("R2T-1+: the gift card's answer doubled reached the bare water under it (%d tank taps)", T.tap_count);
        G2.ask.on = false; T.tap_count = 0; n++;
    }
    /* the GIVE modal over the tank: its BACK or GIVE under the card */
    T.door_open[LINK_LEFT] = true; G2.door_peer[LINK_LEFT] = 0x49439648u; G2.door_badge[LINK_LEFT] = 1;
    int f0 = -1; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 >= 0) {
        bare(); render_give_tap(&T, f0); hold_for(2200);
        int bx, by, gx, gy; bool hb = render_give_modal_button(&T, RENDER_GM_TAP_BACK, &bx, &by), hg = render_give_modal_button(&T, RENDER_GM_TAP_GIVE, &gx, &gy);
        bool on_back = hb && r2_near_gift((float)bx, (float)by, 30, 10, NULL, &x, &y), on_give = !on_back && hg && r2_near_gift((float)gx, (float)gy, 30, 10, NULL, &x, &y);
        if (on_back || on_give) {
            r2_card_on(T.clock - 5); r2_card_double(x, y);
            if (render_give_modal_step(&T) == RENDER_GM_OFF || G2.want_uid) TFAIL("R2T-1+: the gift card's answer doubled reached the GIVE modal's %s under it (step %d, asked %d)",
                                                                              on_back ? "BACK" : "GIVE", render_give_modal_step(&T), G2.want_uid != 0);
            n++;
        } else printf("selftest-touch-port: (R2T-1+: no gift card button over the GIVE modal's BACK or GIVE here)\n");
        render_give_modal_close(); G2.want_uid = 0; G2.ask.on = false;
    }
    T.door_open[LINK_LEFT] = false;
    memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
    printf("selftest-touch-port: R2T-1's other pages - %d (the battery page, MY TANKS, an announcement, the bare water, the GIVE modal; each where "
           "a card button stands over it): the card's answer doubled reached none, a tap 0.7 s on did\n", n);
}
/* R2-6's other buttons: GIVE (CONFIRM), a tank's row (PICK) and OK (SAID) - a press older than the step it meets is no answer: the
   modal opening under a finger already on GIVE or a row, held past the hold; a door shutting mid-press (CANNOT BE GIVEN RIGHT NOW),
   the press lifting on its OK. None acts; after its hold, the button does. */
static void gm_doors(int both) {
    T.door_open[LINK_LEFT] = true; G2.door_peer[LINK_LEFT] = 0x49439648u; G2.door_badge[LINK_LEFT] = 1;
    T.door_open[LINK_RIGHT] = both; G2.door_peer[LINK_RIGHT] = 0x51515151u; G2.door_badge[LINK_RIGHT] = 2;
}
static void r2g_give_steps_more(void) {
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(3);
    int f0 = -1, n = 0; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 < 0) { TFAIL("R2-6+: no fish to give"); return; }
    /* the spots, from a dry run of each step */
    int gx = -1, gy = -1, lx = -1, ly = -1, ox = -1, oy = -1;
    bare(); gm_doors(0); render_give_tap(&T, f0); frame(); render_give_modal_button(&T, RENDER_GM_TAP_GIVE, &gx, &gy);
    T.door_open[LINK_LEFT] = false; frame(); if (render_give_modal_step(&T) == RENDER_GM_SAID) render_give_modal_button(&T, RENDER_GM_TAP_OK, &ox, &oy);
    render_give_modal_close();
    bare(); gm_doors(1); render_give_tap(&T, f0); frame(); if (render_give_modal_step(&T) == RENDER_GM_PICK) render_give_modal_button(&T, RENDER_GM_TAP_LEFT, &lx, &ly);
    render_give_modal_close();
    for (int c = 0; c < 3; c++) {
        static const char *const CN[3] = { "GIVE (the modal opened under a finger on it)", "a tank's row (PICK opened under a finger on it)", "OK (a door shut mid-press: CANNOT BE GIVEN)" };
        const int px = c == 0 ? gx : c == 1 ? lx : ox, py = c == 0 ? gy : c == 1 ? ly : oy;
        if (px < 0) { TFAIL("R2-6+: no spot for %s", CN[c]); continue; }
        bare(); G2.want_uid = 0; gm_doors(c == 1);
        if (c < 2) {                                          /* the finger first, then the modal under it, held past its hold */
            if (!aim((float)px, (float)py)) { TFAIL("R2-6+: %s cannot be reached", CN[c]); continue; }
            g_down = true; hold_for(200); render_give_tap(&T, f0); hold_for((int)(GIFT_HOLD_S * 1000) + 300); g_down = false; hold_for(FRAME_MS);
            if (G2.want_uid) TFAIL("R2-6+: %s - the press asked the other tank", CN[c]);
            lift((int)(GIFT_HOLD_S * 1000) + 100); tap((float)px, (float)py, 90, FRAME_MS);
            if (G2.want_uid != T.fish[f0].uid) TFAIL("R2-6+: %s after its hold did not ask (step %d)", c ? "the row" : "GIVE", render_give_modal_step(&T));
        } else {                                              /* CONFIRM past its hold; a press on OK's spot; the door shuts mid-press */
            render_give_tap(&T, f0); hold_for(2300);
            press((float)px, (float)py, 300); T.door_open[LINK_LEFT] = false; hold_for(300);
            const int st = render_give_modal_step(&T); g_down = false; hold_for(FRAME_MS);
            if (st != RENDER_GM_SAID) TFAIL("R2-6+: the door shut, the modal is not on SAID (%d)", st);
            else if (render_give_modal_step(&T) == RENDER_GM_OFF || G2.want_uid) TFAIL("R2-6+: %s - the press %s", CN[c], G2.want_uid ? "asked the other tank" : "closed the line unread");
            else { lift((int)(GIFT_HOLD_S * 1000) + 100); tap((float)px, (float)py, 90, FRAME_MS);
                   if (render_give_modal_step(&T) != RENDER_GM_OFF) TFAIL("R2-6+: OK after its hold did not close the line"); }
        }
        n++;
        render_give_modal_close(); G2.want_uid = 0; G2.want_door = 0; G2.want_peer = 0;
    }
    T.door_open[LINK_LEFT] = T.door_open[LINK_RIGHT] = false; memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
    printf("selftest-touch-port: R2-6's other buttons - %d (GIVE and a PICK row with the modal opened under the finger, OK as a door shut "
           "mid-press): none answered by the older press, each after its hold\n", n);
}
/* R2T-3, link mode's arrow hold: held part way, MY TANKS opened over link mode (BOOT twice) and closed again with the finger
   still down and moved onto the water - the hold must not resume and ask to join. */
static int hit_link_left(float x, float y, int u) { (void)u; static link_view_t v; int r = render_link_touch(&v, x, y, true, LINK_ARM_HOLD_S + 0.1f); render_link_touch(&v, x, y, false, 0); return r == LINK_UI_ARM_LEFT; }
static void r2g_link_hold(void) {
    float ax, ay, wx = TANK_W / 2.0f, wy = TANK_H * 0.7f;
    if (!find_hit_aim(hit_link_left, 0, &ax, &ay)) { printf("selftest-touch-port: (link mode's left arrow cannot be reached here)\n"); return; }
    if (!aim(wx, wy)) { TFAIL("link hold: the water cannot be reached"); return; }
    bare(); g_link_page = true; hold_for(800); g_link_ui = 0;
    const int part = (int)(LINK_ARM_HOLD_S * 1000 * 0.6f);
    aim(ax, ay); g_down = true; hold_for(part);               /* the arrow, held part way */
    touch_port_show_shelf(true); hold_for(300);               /* BOOT twice: MY TANKS over link mode, the finger still down */
    aim(wx, wy); hold_for(80); touch_port_show_shelf(false);  /* moved onto the water; BOOT twice again: link mode back */
    hold_for(part); g_down = false; hold_for(FRAME_MS);
    { float sx, sy; touch_port_take_shelf_tap(&sx, &sy, &g_shelf_press); }
    if (g_link_ui == LINK_UI_ARM_LEFT) TFAIL("link hold: an arrow held part way, cut by MY TANKS, then a finger on the water - the hold resumed and asked to join");
    g_link_ui = 0; lift(700); tap(ax, ay, (int)(LINK_ARM_HOLD_S * 1000) + 200, FRAME_MS);
    if (g_link_ui != LINK_UI_ARM_LEFT) TFAIL("link hold: the arrow held its whole time after did not ask (%d)", g_link_ui);
    g_link_page = false; bare();
    printf("selftest-touch-port: R2T-3, link mode's arrow hold cut by MY TANKS: it did not resume on the water; held whole after, it asked\n");
}

/* SAND DOLLAR GIFTS (0.4.2): the dial over link mode, through the port as the glass feeds it - a drag turns the ones digit,
   NEXT, a doubled NEXT sends nothing, SEND hands the amount on to the link (once); link mode never sees those fingers */
static void sdg_dial_port(void) {
    float nx, ny, sx, sy;
    bare(); g_link_page = true; T.sd_balance = 200; hold_for(800);
    setup_begin_sdgift(&T, LINK_RIGHT, 2); hold_for(200);
    if (!find_hit_aim(hit_setup, SETUP_HIT_NEXT, &nx, &ny)) { TFAIL("sd dial: NEXT cannot be reached"); setup_cancel(&T); g_link_page = false; bare(); return; }
    int down0 = g_link_down; g_sd_sent = 0; g_sd_edge = -1;
    float dx = PAGE_X + (PAGE_W - (2 * SETUP_SLOT_PX + SETUP_SLOT_W)) / 2 + 2 * SETUP_SLOT_PX + SETUP_SLOT_W / 2.0f, dy = PAGE_Y + SETUP_SLOT_Y + SETUP_SLOT_H / 2.0f;
    if (!aim(dx, dy)) { TFAIL("sd dial: the ones digit cannot be reached"); setup_cancel(&T); g_link_page = false; bare(); return; }
    g_down = true; hold_for(80);
    for (int k = 1; k <= 4; k++) { aim(dx, dy - k * (SETUP_SPIN_PX + 2)); hold_for(FRAME_MS); }
    lift(200);
    if (setup_sdgift_amount() != 4) TFAIL("sd dial: a drag up four steps gave %d", setup_sdgift_amount());
    tap(nx, ny, 90, 60); tap(nx, ny, 90, 200);                /* NEXT, doubled */
    if (!setup_sdgift_confirming() || g_sd_sent) TFAIL("sd dial: a doubled NEXT - confirming %d, sent %d", setup_sdgift_confirming(), g_sd_sent);
    if (!find_hit_aim(hit_setup, SETUP_HIT_NEXT, &sx, &sy)) TFAIL("sd dial: SEND cannot be reached");
    else tap(sx, sy, 90, 200);
    if (setup_active() || g_sd_sent != 4 || g_sd_edge != LINK_RIGHT) TFAIL("sd dial: SEND - open %d, sent %d at %d", setup_active(), g_sd_sent, g_sd_edge);
    hold_for(300);
    if (g_sd_sent != 4) TFAIL("sd dial: SEND handed on twice (%d)", g_sd_sent);
    if (g_link_down != down0) TFAIL("sd dial: link mode saw the dial's finger down %d polls", g_link_down - down0);
    g_link_page = false; bare();
    printf("selftest-touch-port: sand dollar dial over link mode: a drag, a doubled NEXT, SEND handed on once (%d)\n", g_sd_sent);
}

/* GIVE DOES NOTHING (Strato on the 1.8, 2ca0ac5; FIX-give.md): P8's restart made every tap inside the 2 s hold start it again, so
   a keeper tapping GIVE about once a second never got through. Drawn as the board draws (g_draw), through every way in - the
   live tank's GIVE box, the keeper card's GIVE, back from under a birth flow: one tap 2.5 s on asks; taps every 1.0 s ask within
   the first four; a 5 Hz mash from the opening still never asks, and a tap 0.7 s after it does. */
static void r2g_give_ways(void) {
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(3);
    T.door_open[LINK_LEFT] = true; G2.door_peer[LINK_LEFT] = 0x49439648u; G2.door_badge[LINK_LEFT] = 1;
    int f0 = -1, n = 0; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 < 0) { TFAIL("GIVE: no fish to give"); return; }
    static const char *const WAY[3] = { "the live tank's GIVE box", "the keeper card's GIVE", "back from under a birth flow" };
    g_draw = true;
    for (int way = 0; way < 3; way++) for (int how = 0; how < 3; how++) {
        bare(); G2.want_uid = 0;
        if (way == 0) {
            card_up(f0); frame();
            float gx = -1, gy = -1; for (int y = 0; y < TANK_H && gx < 0; y += 2) for (int x = 0; x < TANK_W && gx < 0; x += 2) if (render_give_hit(&T, f0, (float)x, (float)y) && aim((float)x, (float)y) && render_give_hit(&T, f0, g_ax, g_ay)) { gx = (float)x; gy = (float)y; }   /* (where the panel reads it as the box too) */
            if (gx < 0) { if (how == 0) printf("selftest-touch-port: (GIVE: no GIVE box the panel can read on the live tank here)\n"); continue; }
            tap(gx, gy, 90, FRAME_MS);
        } else if (way == 1) {
            touch_port_show_milestones(true); hold_for(800); render_milestones_show_fish(&T, f0); hold_for(200);
            int kx, ky; if (!render_milestones_give(&T, &kx, &ky)) { TFAIL("GIVE: no GIVE on the keeper card"); continue; }
            tap((float)kx, (float)ky, 90, FRAME_MS);
        } else {
            render_give_tap(&T, f0); hold_for(2500);             /* up, its hold run out; then a birth flow over it (not dismissed) */
            setup_begin_birth(&T, T.n_fish - 1); hold_for(1500); setup_cancel(&T); frame();
        }
        if (render_give_modal_step(&T) != RENDER_GM_CONFIRM) { TFAIL("GIVE: %s - the modal is not on CONFIRM (%d)", WAY[way], render_give_modal_step(&T)); continue; }
        int bx, by; render_give_modal_button(&T, RENDER_GM_TAP_GIVE, &bx, &by);
        int taps = 0; const uint32_t uid = T.fish[f0].uid;
        if (how == 0) { hold_for(2500); tap((float)bx, (float)by, 90, FRAME_MS); taps = 1;
                        if (G2.want_uid != uid) TFAIL("GIVE: %s - one tap 2.5 s on did not ask", WAY[way]); }
        else if (how == 1) { for (; taps < 4 && G2.want_uid != uid; taps++) { hold_for(1000 - 90 - FRAME_MS); tap((float)bx, (float)by, 90, FRAME_MS); }
                             if (G2.want_uid != uid) TFAIL("GIVE: %s - tapped once a second, 4 taps, it never asked (step %d)", WAY[way], render_give_modal_step(&T)); }
        else { tap((float)bx, (float)by, 80, FRAME_MS);       /* the double's second half, 0.1 s after the step came up */
               if (G2.want_uid) TFAIL("GIVE: %s - GIVE 0.1 s after the modal came up (the opening tap's double) asked", WAY[way]);
               lift(300); tap((float)bx, (float)by, 80, FRAME_MS);
               if (G2.want_uid != uid) TFAIL("GIVE: %s - a tap 0.3 s after it came up did not ask", WAY[way]); }
        n++;
        render_give_modal_close(); G2.want_uid = 0; G2.want_door = 0; G2.want_peer = 0;
    }
    g_draw = false; T.door_open[LINK_LEFT] = false; memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
    printf("selftest-touch-port: GIVE through every way in, drawn as the board draws - %d (the live tank, the keeper card, back from under a birth "
           "flow x one tap 2.5 s on, a tap a second, the opening's double then a tap 0.3 s on): asks, asks at once, spent then asks\n", n);
}

/* THE TOUCH RULE (touch_gate.h, Strato 11-06), gated: "its just quick double taps we should be guarding against without
   disrupting folks that are comfortable with the UI and move quickly".
   ROWS: a normal tap 0.3 s after each page or step comes up acts. BURST: a tap 0.1 s in (the double's second half) is spent,
   and does not push the window on - a tap 0.25 s in still acts. FAST KEEPER: page to page, act, close, a tap every 0.35 s,
   every tap counts. */
#define RULE_UP 280                                          /* a tap's lift to the next press: about 0.3 s */
static bool rule_tap(float x, float y, int after_ms) { lift(after_ms); return tap(x, y, 90, FRAME_MS); }
static void r2g_rule_rows(void) {
    int n = 0; float x, y, kx, ky, sx, sy; int64_t sp;
    foot(MSF_CLOSE, &kx, &ky);
    /* the shop's X, the collection's CLOSE, settings' CLOSE, UPDATES' CLOSE, MY TANKS's CLOSE: each 0.3 s after its page opened */
    int bx, by; render_shop_foot_box(SHF_CLOSE, &bx, &by);
    bare(); touch_port_show_milestones(true); hold_for(400); foot(MSF_UPG, &x, &y); tap(x, y, 90, FRAME_MS);
    if (!touch_port_shop()) TFAIL("rule rows: UPGRADES did not open the shop");
    else { rule_tap(PAGE_X + bx + MSP_FOOT_SQ / 2.0f, PAGE_Y + by + MSP_FOOT_SQ / 2.0f, RULE_UP);
           if (touch_port_shop() || !touch_port_milestones()) TFAIL("rule rows: the shop's X 0.3 s after the shop opened did not close it"); else n++; }
    bare(); touch_port_show_milestones(true); hold_for(400); foot(MSF_VIS, &x, &y); tap(x, y, 90, FRAME_MS);
    if (!touch_port_collection()) TFAIL("rule rows: VISITORS did not open the collection");
    else { rule_tap(PAGE_X + MSP_CLOSE_X + MSP_CLOSE_W / 2.0f, PAGE_Y + MSP_CLOSE_Y + MSP_CLOSE_H / 2.0f, RULE_UP);
           if (touch_port_collection() || !touch_port_milestones()) TFAIL("rule rows: the collection's CLOSE 0.3 s after it opened did not close it"); else n++; }
    float cx, cy, ux, uy;
    if (find_hit(hit_settings, SET_TAP_CLOSE, &cx, &cy) && find_hit(hit_updates, UPD_TAP_CLOSE, &ux, &uy)) {
        bare(); touch_port_show_milestones(true); hold_for(400); foot(MSF_SET, &x, &y); tap(x, y, 90, FRAME_MS);
        if (!touch_port_settings()) TFAIL("rule rows: the cog did not open settings");
        else { rule_tap(cx, cy, RULE_UP); if (touch_port_settings() || !touch_port_milestones()) TFAIL("rule rows: settings' CLOSE 0.3 s after it opened did not close it"); else n++; }
        float upx, upy;
        if (find_hit(hit_settings, SET_TAP_UPDATES, &upx, &upy)) {
            bare(); touch_port_show_settings(true); hold_for(400); tap(upx, upy, 90, FRAME_MS);
            if (!touch_port_updates()) TFAIL("rule rows: settings' UPDATES did not open UPDATES");
            else { rule_tap(ux, uy, RULE_UP); if (touch_port_updates()) TFAIL("rule rows: UPDATES' CLOSE 0.3 s after it opened did not close it"); else n++; }
        }
    }
    if (find_hit_aim(hit_shelf, SHELF_TAP_CLOSE, &x, &y)) {
        bare(); touch_port_show_shelf(true); rule_tap(x, y, RULE_UP);
        if (!touch_port_take_shelf_tap(&sx, &sy, &sp)) TFAIL("rule rows: MY TANKS's CLOSE 0.3 s after it opened was no tap"); else n++;
        touch_port_show_shelf(false);
    }
    /* SELL: the keeper card's, armed then +N OK? 0.3 s on */
    r2_fish(4);
    {   bare(); touch_port_show_milestones(true); hold_for(400); render_milestones_show_fish(&T, 2);
        int fish, sxx, byy; const int n0 = T.n_fish;
        if (render_milestones_card(&T, &fish, NULL, &sxx, &byy)) {
            tap((float)sxx, (float)byy, 90, FRAME_MS); rule_tap((float)sxx, (float)byy, RULE_UP);
            if (T.n_fish != n0 - 1) TFAIL("rule rows: the keeper card's SELL, its +N OK? 0.3 s after the arm, did not sell"); else n++;
        }
        r2_fish(4);
    }
    /* the shop: a row's modal, UNLOCK 0.3 s on */
    float rx, ry;
    bare(); T.sd_balance = 100000; T.sd_unlocks = 0; T.sd_shelved = 0; touch_port_show_shop(true); hold_for(200); render_shop_leave();
    if (shop_row_under_button(&rx, &ry) >= 0) {
        bare(); T.sd_balance = 100000; T.sd_unlocks = 0; touch_port_show_shop(true); hold_for(400);
        const float ux2 = PAGE_X + SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2 + MSP_HOW_W / 2.0f, uy2 = PAGE_Y + SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H / 2.0f;
        tap(rx, ry, 90, FRAME_MS); touch_port_take_shop(); rule_tap(ux2, uy2, RULE_UP);
        int r = touch_port_take_shop();
        if (!(r >= SHOP_TAP_BUY && r < SHOP_TAP_MOVE)) TFAIL("rule rows: UNLOCK 0.3 s after the row opened its modal did not buy (%d)", r); else n++;
    }
    render_shop_leave(); T.sd_unlocks = 0;
    /* the placement page's SELL, +N OK? 0.3 s after the arm */
    {   int item = -1; for (int i = 0; i < SD_ITEM_COUNT && item < 0; i++) if (tank_decor_placeable(i)) item = i;
        bare(); T.sd_unlocks |= SD_ITEMS[item].bit; T.sd_shelved &= ~SD_ITEMS[item].bit; T.sd_locked &= ~SD_ITEMS[item].bit;
        tank_decor_set(&T, item, 200, DECOR_Z_BACK); setup_begin_place(&T, item); hold_for(200);
        if (find_hit(hit_setup_g, SETUP_HIT_SELL, &x, &y)) {
            tap(x, y, 90, FRAME_MS); rule_tap(x, y, RULE_UP);
            if (T.sd_unlocks & SD_ITEMS[item].bit) TFAIL("rule rows: the placement page's SELL, +N OK? 0.3 s after the arm, did not sell"); else n++;
        }
        if (setup_active()) setup_cancel(&T);
    }
    /* the GIVE modal: PICK's row 0.3 s after it opened, CALL OFF 0.3 s after ASKING */
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(3);
    int f0 = -1; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 >= 0) {
        bare(); gm_doors(1); render_give_tap(&T, f0); frame();
        int lx, ly, ox, oy;
        if (render_give_modal_step(&T) != RENDER_GM_PICK || !render_give_modal_button(&T, RENDER_GM_TAP_LEFT, &lx, &ly)) TFAIL("rule rows: the GIVE modal is not on PICK");
        else {
            rule_tap((float)lx, (float)ly, RULE_UP);
            if (G2.want_uid != T.fish[f0].uid) TFAIL("rule rows: a PICK row 0.3 s after the modal opened did not ask (step %d)", render_give_modal_step(&T));
            else if (!render_give_modal_button(&T, RENDER_GM_TAP_CALLOFF, &ox, &oy)) TFAIL("rule rows: ASKING has no CALL OFF");
            else { rule_tap((float)ox, (float)oy, RULE_UP);
                   if (render_give_modal_step(&T) != RENDER_GM_OFF) TFAIL("rule rows: CALL OFF 0.3 s after ASKING came up did not close it (step %d)", render_give_modal_step(&T)); else n += 2; }
        }
        render_give_modal_close(); G2.want_uid = 0; G2.want_door = 0; G2.want_peer = 0;
    }
    T.door_open[LINK_LEFT] = T.door_open[LINK_RIGHT] = false; memset(&G2, 0, sizeof G2); gift_attach(&T, NULL);
    /* an announcement: it comes by itself - a tap at once closes it (only a press from before it is spent) */
    bare(); notice_sync(&T); notice_low_battery(); for (int k = 0; k < 40 && !notice_current(); k++) notice_tick(&T, 0.1f, false);
    if (!notice_current()) TFAIL("rule rows: no notice came up");
    else { notice_tick(&T, 0.3f, false);                    /* (the board ticks it every frame: 0.3 s up) */
           tap(TANK_W / 2.0f, TANK_H * 0.6f, 90, FRAME_MS);
           if (notice_current()) TFAIL("rule rows: an announcement did not close on a tap as it came (age %.1f s)", notice_current()->age); else n++; }
    notice_sync(&T); bare();
    printf("selftest-touch-port: THE TOUCH RULE's rows - %d (the shop's X, the collection's CLOSE, settings' and UPDATES' CLOSE, MY TANKS's CLOSE, "
           "the keeper card's and the placement page's SELL confirm, the shop's UNLOCK, a PICK row, CALL OFF, an announcement): a tap 0.3 s on acts\n", n);
}
/* BURST: the opening tap's double (0.1 s in) is spent and pushes nothing on - a tap 0.25 s after the opening still acts */
static void r2g_rule_burst(void) {
    int n = 0; float x, y; int bx, by;
    render_shop_foot_box(SHF_CLOSE, &bx, &by);
    const float xx = PAGE_X + bx + MSP_FOOT_SQ / 2.0f, xy = PAGE_Y + by + MSP_FOOT_SQ / 2.0f;
    bare(); touch_port_show_milestones(true); hold_for(400); foot(MSF_UPG, &x, &y);
    tap(x, y, 90, FRAME_MS);                                 /* the shop opens at this lift */
    tap(xx, xy, 40, 160);                                    /* about 0.05 s on: spent (the X, as a double's stray half) */
    if (!touch_port_shop()) TFAIL("rule burst: the shop's X 0.05 s after the shop opened closed it");
    tap(xx, xy, 90, FRAME_MS);                               /* pressed about 0.28 s after the opening (0.16 s after the spent tap's lift): acts */
    if (touch_port_shop()) TFAIL("rule burst: the shop's X 0.28 s after the shop opened, after a spent tap, did not close it (the window was pushed on)"); else n++;
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(3);
    int f0 = -1; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 >= 0) {
        bare(); gm_doors(0); render_give_tap(&T, f0); frame();
        int gx, gy; render_give_modal_button(&T, RENDER_GM_TAP_GIVE, &gx, &gy);
        tap((float)gx, (float)gy, 40, 160);
        if (G2.want_uid) TFAIL("rule burst: GIVE 0.05 s after the modal opened asked");
        tap((float)gx, (float)gy, 90, FRAME_MS);
        if (G2.want_uid != T.fish[f0].uid) TFAIL("rule burst: GIVE 0.28 s after the modal opened, after a spent tap, did not ask (the window was pushed on)"); else n++;
        render_give_modal_close(); G2.want_uid = 0; G2.want_door = 0; G2.want_peer = 0;
    }
    T.door_open[LINK_LEFT] = false; memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
    printf("selftest-touch-port: THE TOUCH RULE's burst - %d (the shop's X, GIVE): a tap inside the window spent, the next after it acts\n", n);
}
/* FAST KEEPER: a tap every ~0.35 s (90 ms down) through the pages - the shop and out, settings (a row) and out, the collection
   and out, MY TANKS (main.c played) and out, the keeper page closed: every tap counts */
static void r2g_fast_keeper(void) {
    int n = 0, bad = 0; float x, y, sx, sy; int64_t sp; int bx, by, v;
    const int UP = 240;
    float cx, cy, fx, fy, mx, my;
    bool has_set = find_hit(hit_settings, SET_TAP_CLOSE, &cx, &cy) && find_hit(hit_settings, SET_TAP_FEED, &fx, &fy);
    bool has_shelf = find_hit_aim(hit_shelf, SHELF_TAP_CLOSE, &mx, &my);
    bare(); touch_port_show_milestones(true); hold_for(400);
    #define FK(cond, what) do { n++; if (!(cond)) { TFAIL("fast keeper: %s (tap %d, every 0.35 s)", what, n); bad++; } } while (0)
    foot(MSF_UPG, &x, &y); tap(x, y, 90, UP); FK(touch_port_shop(), "UPGRADES did not open the shop");
    render_shop_foot_box(SHF_CLOSE, &bx, &by); tap(PAGE_X + bx + MSP_FOOT_SQ / 2.0f, PAGE_Y + by + MSP_FOOT_SQ / 2.0f, 90, UP);
    FK(!touch_port_shop() && touch_port_milestones(), "the shop's X did not close the shop");
    if (has_set) {
        foot(MSF_SET, &x, &y); tap(x, y, 90, UP); FK(touch_port_settings(), "the cog did not open settings");
        drain_setting(); tap(fx, fy, 90, UP); FK(touch_port_take_setting(&v) == SET_TAP_FEED, "AUTO FEED did not answer");
        tap(cx, cy, 90, UP); FK(!touch_port_settings() && touch_port_milestones(), "settings' CLOSE did not close settings");
    }
    foot(MSF_VIS, &x, &y); tap(x, y, 90, UP); FK(touch_port_collection(), "VISITORS did not open the collection");
    tap(PAGE_X + MSP_CLOSE_X + MSP_CLOSE_W / 2.0f, PAGE_Y + MSP_CLOSE_Y + MSP_CLOSE_H / 2.0f, 90, UP);
    FK(!touch_port_collection() && touch_port_milestones(), "the collection's CLOSE did not close it");
    if (has_shelf) {
        foot(MSF_TANKS, &x, &y); tap(x, y, 90, FRAME_MS);
        FK(touch_port_take_setting(&v) == SET_TAP_TANKS, "the foot's MY TANKS did not ask for MY TANKS");
        touch_port_dismiss(); touch_port_show_shelf(true); lift(UP - FRAME_MS);   /* main.c: shelf_toggle, the page on the next frame */
        tap(mx, my, 90, FRAME_MS); FK(touch_port_take_shelf_tap(&sx, &sy, &sp) && render_shelf_tap(sx, sy) == SHELF_TAP_CLOSE, "MY TANKS's CLOSE was no tap");
        touch_port_show_shelf(false); touch_port_show_milestones(true); lift(UP - FRAME_MS);   /* main.c: shelf_close at the take, back to the keeper page */
    }
    foot(MSF_CLOSE, &x, &y); tap(x, y, 90, UP); FK(!touch_port_milestones(), "the keeper page's CLOSE did not close it");
    #undef FK
    drain_setting(); bare();
    printf("selftest-touch-port: the fast keeper - %d taps 0.35 s apart through the shop, settings, the collection, MY TANKS and out: %d missed\n", n, bad);
}

/* THE TOUCH RULE's review (11-06): the press from before (G1) on DEPORT and an announcement; CANNOT BE GIVEN at the open and an
   announcement back from under a page spend the opening tap's double (W). */
static void r2g_rule_review(void) {
    int n = 0; float x, y;
    /* a press begun on the gift card over the keeper card's armed SELL; the card goes mid-press (its time); lifted 0.4 s on, on
       SELL: no tap on the page (the press began before the card went - G1 of the card-gone stamp) */
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(4);
    {   bare(); touch_port_show_milestones(true); hold_for(400); render_milestones_show_fish(&T, 2);
        int fish, sxx, byy;
        if (render_milestones_card(&T, &fish, NULL, &sxx, &byy) && aim((float)sxx, (float)byy)) {
            tap((float)sxx, (float)byy, 90, 400);            /* SELL armed */
            const int n0 = T.n_fish;
            r2_card_on(T.clock - 5); frame();                /* a gift card over it */
            aim((float)sxx, (float)byy); g_down = true; hold_for(200);
            G2.ask.on = false; hold_for(400);                /* its time runs out under the finger */
            g_down = false; hold_for(FRAME_MS);
            if (T.n_fish != n0) TFAIL("rule review: a press begun on the gift card, lifted after the card went, sold the fish whose SELL was armed under it (G1)"); else n++;
        }
        G2.ask.on = false; r2_fish(4);
    }
    memset(&G2, 0, sizeof G2); gift_attach(&T, NULL);
    /* the gift card: a press in the poll its first draw stamps (the same clock: main.c polls before it draws) is no answer */
    {   static gift_book_t GB; memset(&GB, 0, sizeof GB); gift_attach(&T, &GB); float gx, gy;
        if (find_hit(hit_gift, GIFT_HIT_ACCEPT, &gx, &gy) && aim(gx, gy)) {
            bare(); GB.ask.on = true; GB.ask.uid = 0x77u; GB.ask.answer = 0; GB.ask.shown = true; GB.ask.seen = T.clock + FRAME_MS / 1000.0;   /* (drawn in the press's poll) */
            g_down = true; frame(); hold_for(60); g_down = false; hold_for(FRAME_MS);
            if (GB.ask.answer) TFAIL("rule review: a press in the very poll the gift card was first drawn answered it (%d)", GB.ask.answer); else n++;
        }
        memset(&GB, 0, sizeof GB); gift_attach(&T, NULL);
    }
    /* DEPORT: the passport comes up under a finger already down on DEPORT's spot - held 1 s, lifted: kept */
    const int v = N_FISH_MAX;
    T.fish[v] = T.fish[0]; T.fish[v].uid = 0x5151u; T.fish[v].homeward = T.fish[v].deported = false; T.vis_used[0] = true;
    if (find_hit(hit_passport, PASSPORT_HIT_DEPORT, &x, &y) && aim(x, y)) {
        bare(); g_down = true; hold_for(80); card_up(v); hold_for(120); g_down = false; hold_for(FRAME_MS);   /* (a quick press: under TAP_QUICK_US) */
        if (T.fish[v].deported) TFAIL("rule review: the passport came up under a quick press already on DEPORT - its lift sent the visitor home (G1)"); else n++;
    }
    T.vis_used[0] = false; memset(&T.fish[v], 0, sizeof T.fish[v]); T.deport_uid = 0; s_sel = -1;
    /* an announcement comes under a finger resting on the water - lifted 0.5 s on: it stays; a tap then closes it */
    {   const float wx = TANK_W / 2.0f, wy = TANK_H * 0.6f;
        bare(); notice_sync(&T);
        if (aim(wx, wy)) {
            g_down = true; hold_for(120); notice_low_battery();    /* a quick press (under TAP_QUICK_US) that began before it came */
            for (int k = 0; k < 40 && !notice_current(); k++) notice_tick(&T, 0.01f, false);
            frame(); notice_tick(&T, 0.08f, false); frame();
            g_down = false; hold_for(FRAME_MS);
            if (!notice_current()) TFAIL("rule review: an announcement that came under a press already down was closed by its lift (G1)");
            else { notice_tick(&T, 0.3f, false); tap(wx, wy, 90, FRAME_MS);
                   if (notice_current()) TFAIL("rule review: a tap after did not close the announcement"); else n++; }
        }
        notice_sync(&T);
    }
    /* an announcement back from under the keeper page its CLOSE shut: that tap's double is spent, a tap 0.3 s on closes it */
    {   float kx, ky; foot(MSF_CLOSE, &kx, &ky);
        bare(); notice_sync(&T); notice_low_battery(); for (int k = 0; k < 40 && !notice_current(); k++) notice_tick(&T, 0.1f, false);
        notice_tick(&T, 0.5f, false);
        touch_port_show_milestones(true); notice_tick(&T, 0.1f, true); hold_for(400);   /* (main.c: covered, it steps aside) */
        tap(kx, ky, 90, FRAME_MS);                           /* CLOSE: the keeper page goes ... */
        notice_tick(&T, 0.04f, false); frame();              /* ... and the announcement comes back (main.c's next tick) */
        notice_tick(&T, 0.15f, false);                       /* (it ages as the board ticks it: older than the double's press, so G1 alone lets it through) */
        if (touch_port_milestones()) TFAIL("rule review: the keeper page's CLOSE did not close it");
        else if (!notice_current()) TFAIL("rule review: no announcement under the keeper page");
        else { tap(kx, ky, 80, FRAME_MS);                    /* the double's second half */
               if (!notice_current()) TFAIL("rule review: CLOSE's double closed the announcement that came back under it");
               else { notice_tick(&T, 0.3f, false); rule_tap(kx, ky, RULE_UP);
                      if (notice_current()) TFAIL("rule review: a tap 0.3 s on did not close the announcement"); else n++; } }
        notice_sync(&T);
    }
    /* CANNOT BE GIVEN at the open (a door with no tank behind it): OK as the opening tap's double is spent, 0.3 s on it closes */
    memset(&G2, 0, sizeof G2); gift_attach(&T, &G2); r2_fish(3);
    int f0 = -1; for (int i = 0; i < T.n_fish && f0 < 0; i++) if (progression_fish_givable(&T, i)) f0 = i;
    if (f0 >= 0) {
        bare(); T.door_open[LINK_LEFT] = true; G2.door_peer[LINK_LEFT] = 0; G2.door_badge[LINK_LEFT] = 1;
        render_give_tap(&T, f0); frame();
        int ox, oy;
        if (render_give_modal_step(&T) != RENDER_GM_SAID || !render_give_modal_button(&T, RENDER_GM_TAP_OK, &ox, &oy)) printf("selftest-touch-port: (rule review: the GIVE modal did not open on CANNOT BE GIVEN: step %d)\n", render_give_modal_step(&T));
        else {
            tap((float)ox, (float)oy, 80, FRAME_MS);
            if (render_give_modal_step(&T) == RENDER_GM_OFF) TFAIL("rule review: CANNOT BE GIVEN's OK 0.1 s after the modal opened (its double) closed it unread");
            else { rule_tap((float)ox, (float)oy, RULE_UP);
                   if (render_give_modal_step(&T) != RENDER_GM_OFF) TFAIL("rule review: CANNOT BE GIVEN's OK 0.3 s on did not close it"); else n++; }
        }
        render_give_modal_close();
    }
    T.door_open[LINK_LEFT] = false; memset(&G2, 0, sizeof G2); gift_attach(&T, NULL); bare();
    printf("selftest-touch-port: THE TOUCH RULE's review - %d (DEPORT and an announcement under a finger already down: kept; an announcement back "
           "from under the keeper page and CANNOT BE GIVEN at the open: the double spent, a tap 0.3 s on acts)\n", n);
}

/* ---- THE CRITTERS' NAMES (0.4.2): a critter card's NAME / RENAME through the board's port ----
 * Each nameable critter's card up: the button's own press opens the letter wheel for that critter (a press of 0.09 s and a
 * firm one of 0.8 s alike); the double of the tap that opened the card is spent (a kid's second tap on a seahorse in front
 * of it); DONE keeps the name the wheel turned and puts that critter's card back up; CANCEL puts the old name back and the
 * card back up. The shrimp's card has no button. */
static int hit_crit_name(float x, float y, int card) { return render_critter_name_hit(&T, card, x, y); }
/* MORE's pages (0.4.2, the blind review): BACK / NEXT sit under the card's icon row. A doubled MORE must not turn ITS STORY
   (its second half on NEXT), a doubled NEXT must not skip a page, a doubled DONE must not reopen the pages from the card it
   brings back (on MORE), a doubled BACK must not open the wheel (on RENAME); 0.4 s on, each tap acts */
static void more_pages_doubled(void) {
    bare(); r2_fish(3);
    float nx, ny, bx, by; int mx, my, rx, sx, cy;
    touch_port_show_milestones(true); hold_for(800); render_milestones_show_fish(&T, 2); frame();
    if (!render_milestones_more(&T, &mx, &my) || !render_milestones_card(&T, NULL, &rx, &sx, &cy)) { TFAIL("MORE doubled: no fish card with MORE"); bare(); return; }
    tap((float)mx, (float)my, 90, 100);
    if (!setup_is_about() || setup_page() != SETUP_PG_ABOUT) { TFAIL("MORE did not open ITS STORY"); setup_cancel(&T); bare(); return; }
    if (!find_hit(hit_setup, SETUP_HIT_NEXT, &nx, &ny) || !find_hit(hit_setup, SETUP_HIT_BACK, &bx, &by)) { TFAIL("MORE doubled: no BACK / NEXT"); setup_cancel(&T); bare(); return; }
    tap(nx, ny, 90, 400);
    if (setup_page() != SETUP_PG_ABOUT) TFAIL("MORE doubled onto NEXT turned ITS STORY over");
    tap(nx, ny, 90, 100);
    if (setup_page() != SETUP_PG_FAMILY) TFAIL("NEXT 0.4 s on did not turn to FAMILY");
    tap(nx, ny, 90, 400);
    if (setup_page() != SETUP_PG_FAMILY) TFAIL("NEXT doubled skipped FAMILY");
    tap(nx, ny, 90, 400);                                        /* to NATURE */
    tap(nx, ny, 90, 100);                                        /* DONE ... */
    if (setup_active() || !render_milestones_card(&T, NULL, NULL, NULL, NULL)) TFAIL("DONE did not bring the card back");
    tap((float)mx, (float)my, 90, 400);                          /* ... doubled, onto MORE */
    if (setup_active()) TFAIL("DONE doubled onto MORE opened the pages again");
    if (!render_milestones_card(&T, NULL, NULL, NULL, NULL)) TFAIL("DONE doubled closed the card it came back to");
    tap((float)mx, (float)my, 90, 400);                          /* MORE, 0.4 s on: acts */
    if (!setup_is_about()) TFAIL("MORE 0.4 s after DONE did not open the pages");
    tap(bx, by, 90, 100);                                        /* BACK on ITS STORY (its press 0.4 s on): the card */
    if (setup_active()) TFAIL("BACK on ITS STORY did not bring the card back");
    tap((float)rx, (float)cy, 90, 400);                          /* ... doubled, onto RENAME */
    if (setup_active()) TFAIL("BACK doubled onto RENAME opened the letter wheel");
    if (setup_active()) setup_cancel(&T);
    (void)setup_take_renamed();
    printf("touch-port: MORE's pages: a doubled MORE, NEXT, DONE and BACK each spent; 0.4 s on each acts\n");
    bare();
}
static void crit_names_port(void) {
    T.sd_unlocks |= SD_ITEM_SNAIL | SD_ITEM_SHRIMP | SD_ITEM_URCHIN | SD_ITEM_SEAHORSE | SD_ITEM_CRAB;
    tank_snail_place(&T); tank_shrimp_place(&T, 4); tank_urchin_place(&T); tank_seahorse_place(&T); tank_crab_place(&T);
    float dx, dy, ux, uy, cx, cy; int n = 0;
    for (int k = 0; k < CRIT_NAME_N; k++) {
        const int card = render_card_of_crit_name(k);
        float x, y;
        bare(); memset(T.crit_name, 0, sizeof T.crit_name);
        if (!find_hit(hit_crit_name, card, &x, &y)) { TFAIL("critter %d: its card has no NAME button the hit test answers", k); continue; }
        card_up(card); tap(x, y, 90, 100);                                   /* the opening tap's double: spent */
        if (setup_active()) { TFAIL("critter %d: NAME answered the double of the tap that opened the card", k); setup_cancel(&T); continue; }
        card_up(card); hold_for(400); tap(x, y, 90, 200);
        if (setup_crit() != k) { TFAIL("critter %d: a tap on NAME did not open its wheel (crit %d, active %d)", k, setup_crit(), setup_active()); setup_cancel(&T); continue; }
        if (s_sel != -1) TFAIL("critter %d: its card stayed up under the wheel (%d)", k, s_sel);
        if (!find_hit(hit_setup, SETUP_HIT_NEXT, &dx, &dy) || !find_hit(hit_setup, SETUP_HIT_UP, &ux, &uy) || !find_hit(hit_setup, SETUP_HIT_BACK, &cx, &cy))
            { TFAIL("critter %d: the wheel's DONE / up / CANCEL not found", k); setup_cancel(&T); continue; }
        tap(ux, uy, 90, 200); tap(ux, uy, 90, 200);                           /* two letters up: B */
        tap(dx, dy, 90, 200);                                                 /* DONE */
        if (setup_active() || strcmp(T.crit_name[k], "b")) TFAIL("critter %d: DONE left '%s' (want 'b'), wheel %s", k, T.crit_name[k], setup_active() ? "up" : "down");
        if (s_sel != card) TFAIL("critter %d: DONE did not put its card back (card %d)", k, s_sel);
        card_up(-1); hold_for(400); card_up(card); hold_for(400);
        tap(x, y, 90, 200); tap(dx, dy, 90, 100); tap(dx, dy, 90, FRAME_MS);   /* the wheel again, DONE double tapped (over the card's arrows) */
        if (setup_active() || s_sel != card) TFAIL("critter %d: DONE double tapped left the wheel %s and card %d (want its own, %d)", k, setup_active() ? "up" : "down", s_sel, card);
        hold_for(400); press(x, y, 800); lift(200);                           /* a FIRM press on RENAME: the wheel again */
        if (setup_crit() != k) { TFAIL("critter %d: a firm press on RENAME did not open the wheel", k); setup_cancel(&T); continue; }
        tap(ux, uy, 90, 200); tap(cx, cy, 90, 200);                           /* a letter on, then CANCEL */
        if (setup_active() || strcmp(T.crit_name[k], "b")) TFAIL("critter %d: CANCEL left '%s' (want the old 'b')", k, T.crit_name[k]);
        if (s_sel != card) TFAIL("critter %d: CANCEL did not put its card back (card %d)", k, s_sel);
        n++;
    }
    { float x, y; if (find_hit(hit_crit_name, RENDER_CARD_SHRIMP, &x, &y)) TFAIL("the shrimp school's card answers a NAME button"); }
    bare(); memset(T.crit_name, 0, sizeof T.crit_name);
    T.sd_unlocks &= ~(uint32_t)(SD_ITEM_SNAIL | SD_ITEM_SHRIMP | SD_ITEM_URCHIN | SD_ITEM_SEAHORSE | SD_ITEM_CRAB);
    T.snail_x = T.urchin_x = T.sh_x = T.cr_x = -1; T.shrimp_n = 0;
    printf("selftest-touch-port: the critters' names - %d of %d cards: NAME's opening double spent, a tap and a firm press open the wheel, "
           "DONE keeps the name and CANCEL the old one, each back on its card; the shrimp has none\n", n, CRIT_NAME_N);
}

/* THE CRITTER SHELF on the card, through the port (0.4.2, docs/critter-shelf.md): SHELVE asks twice - the opening tap's
   double is spent, the first tap arms (the critter stays), the doubled second is spent, a second tap a moment later
   shelves it and closes the card. Any other tap lets the ask go: armed, then a tap off it (the card closed) and the card
   back, or an arrow away and back - SHELVE asks again, never shelves at one tap (the review's finding: the arm outlived
   its card) */
static int hit_crit_shelve(float x, float y, int card) { return render_critter_shelve_hit(&T, card, x, y); }
static void crit_shelf_port(void) {
    static const int CARDS[5] = { RENDER_CARD_SNAIL, RENDER_CARD_SHRIMP, RENDER_CARD_URCHIN, RENDER_CARD_SEAHORSE, RENDER_CARD_CRAB };
    T.sd_unlocks |= SD_CRIT_BITS; T.sd_shelved &= ~(uint32_t)SD_CRIT_BITS;
    tank_snail_place(&T); tank_shrimp_place(&T, 4); tank_urchin_place(&T); tank_seahorse_place(&T); tank_crab_place(&T);
    int n = 0;
    for (int k = 0; k < 5; k++) {
        const int card = CARDS[k], item = render_crit_item_of_card(card); const uint32_t bit = SD_ITEMS[item].bit;
        float x, y;
        bare();
        if (!find_hit(hit_crit_shelve, card, &x, &y)) { TFAIL("critter card %d: no SHELVE the hit test answers", card); continue; }
        int X0, Y0, W0, H0; render_crit_shelve_box(card, &X0, &Y0, &W0, &H0);
        card_up(card); tap(x, y, 90, 100);                                       /* the opening tap's double: spent */
        if (!tank_crit_in(&T, bit)) { TFAIL("critter card %d: the double of the opening tap shelved it", card); continue; }
        card_up(card); hold_for(400); tap(x, y, 90, 100);                        /* arms */
        tap(x, y, 90, 400);                                                      /* doubled: spent */
        if (!tank_crit_in(&T, bit) || s_sel != card) { TFAIL("critter card %d: one tap (or a doubled one) shelved it", card); continue; }
        tap((float)(X0 + W0 / 2), (float)(Y0 - 60), 90, 400);                    /* a tap off it, on the card */
        card_up(-1); hold_for(400); card_up(card); hold_for(400);
        tap(x, y, 90, 400);                                                      /* the card back: asks again */
        if (!tank_crit_in(&T, bit)) { TFAIL("critter card %d: armed before a tap off it, one tap shelved it", card); continue; }
        {   int nx = 0; float rx = 0, ry = 0;
            for (int py = 0; py < TANK_H && !nx; py += 2) for (int px = TANK_W / 2; px < TANK_W && !nx; px += 2)
                if (render_critter_nav_hit(card, (float)px, (float)py) > 0) { nx = 1; rx = (float)px; ry = (float)py; }
            if (nx) {                                                            /* an arrow away and back: asks again */
                tap(rx, ry, 90, 400);
                int other = s_sel;
                if (other != card && render_critter_here(&T, other)) {
                    float lx = 0, ly = 0; int got = 0;
                    for (int py = 0; py < TANK_H && !got; py += 2) for (int px = 0; px < TANK_W / 2 && !got; px += 2)
                        if (render_critter_nav_hit(other, (float)px, (float)py) < 0) { got = 1; lx = (float)px; ly = (float)py; }
                    if (got) tap(lx, ly, 90, 400);
                }
                if (s_sel == card) {
                    tap(x, y, 90, 400);
                    if (!tank_crit_in(&T, bit)) { TFAIL("critter card %d: armed before an arrow away and back, one tap shelved it", card); continue; }
                }
            }
        }
        if (s_sel != card) card_up(card);
        hold_for(400); tap(x, y, 90, 400); hold_for(400); tap(x, y, 90, 400);    /* asked and answered */
        if (tank_crit_in(&T, bit) || !(T.sd_unlocks & bit)) { TFAIL("critter card %d: two taps a moment apart did not shelve it (in %d)", card, tank_crit_in(&T, bit)); continue; }
        if (s_sel == card) TFAIL("critter card %d: its card stayed up with the critter on the shelf", card);
        progression_unshelve(&T, item);
        n++;
    }
    bare();
    T.sd_unlocks &= ~(uint32_t)SD_CRIT_BITS; T.sd_shelved &= ~(uint32_t)SD_CRIT_BITS;
    T.snail_x = T.urchin_x = T.sh_x = T.cr_x = -1; T.shrimp_n = 0;
    printf("selftest-touch-port: the critter shelf - %d of 5 cards: SHELVE's opening double spent, armed by one tap, a doubled "
           "second spent, a tap off it or an arrow away lets the ask go, two taps a moment apart shelve and close the card\n", n);
}

/* ===== THE KEEPER'S CALIBRATION ON THE GLASS (0.4.2; Machito on Discord 11-07, his 1.8: "impossible to clean [the algae]
 * along the right edge" once calibrated, fine after RESET TOUCH; "the calibration is fine until I rotate the screen").
 * Every world, every way up the picture can be (the 1.8 also as its V1 panel, the bias only):
 *  A. A keeper's ADVANCED map that shrinks the reading (0.90 x + 12, 0.93 y + 6, upright: what a sitting on a panel that reads
 *     large keeps): scrubs over the whole panel with the sponge leave no film cell on the glass, and a stroke at each bed's
 *     outermost frond, heading for its wall, cuts it - as they do with no keeper's map (the control).
 *  B. The ADVANCED sitting through the port's own map: a panel that reads off (gain + offset, the panel's frame) and a finger
 *     that lands off (the viewer's frame): one sitting of a panel-only error holds in every orientation; a sitting in each
 *     of two orientations of a panel AND a finger holds in every orientation (the second sitting no longer throws the first
 *     away: the two tell the panel from the finger). */
static const int8_t CQ_RIGHT[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } }, CQ_DOWN[4][2] = { { 0, 1 }, { -1, 0 }, { 0, -1 }, { 1, 0 } };
static int nquarters(int *qs) { int n = 0; for (int q = 0; q < 4; q++) if (TANK_QUARTER(q) == q) qs[n++] = q; return n; }
static void finger_panel(int px, int py) {
    int pw = panel_w(), ph = panel_h();
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px > pw - 1) px = pw - 1;
    if (py > ph - 1) py = ph - 1;
    chip_from_panel(px, py, &g_rx, &g_ry); g_down = true; frame();
}
static void panel_stroke(float x0, float y0, float x1, float y1, float step) {   /* panel px, a frame every `step` px, held at its end, lifted */
    float d = hypotf(x1 - x0, y1 - y0); int n = (int)ceilf(d / step); if (n < 1) n = 1;
    for (int k = 0; k <= n; k++) { float u = (float)k / n; finger_panel((int)lroundf(x0 + (x1 - x0) * u), (int)lroundf(y0 + (y1 - y0) * u)); }
    frame(); frame();
    lift(200);
    T.drag_active = T.drag_has_prev = false; T.drag_dist = 0;   /* (tank_tick's end of a lifted stroke: the harness runs no tick) */
    T.slash_armed = T.slash_engaged = T.slash_cut = false; T.slash_h = T.slash_v = 0;
}
static bool cell_off_glass(int cx, int cy) {   /* tank.c algae_ok's cells off the glass (the bowl's circle, the round corners) */
    float px = cx * ALGAE_CELL + ALGAE_CELL * 0.5f, py = cy * ALGAE_CELL + ALGAE_CELL * 0.5f;
#ifdef TANK_ROUND
    float dx = px - TANK_RAD, dy = py - TANK_RAD; return dx * dx + dy * dy > (TANK_RAD - 6) * (TANK_RAD - 6);
#elif defined(TANK_CORNER_R)
    if (px > TANK_W - 1) px = TANK_W - 1;
    if (py > TANK_H - 1) py = TANK_H - 1;
    return (px < TANK_CORNER_R || px > TANK_W - 1 - TANK_CORNER_R) && (py < TANK_CORNER_R || py > TANK_H - 1 - TANK_CORNER_R);
#else
    (void)px; (void)py; return false;
#endif
}
/* scrub the whole panel with the sponge, rows and columns both ways: the film cells left on the glass (the first one's centre) */
static int scrub_all(int *fx, int *fy) {
    for (int i = 0; i < ALGAE_CELLS; i++) T.algae[i] = 200;
    bare(); tank_set_tool(&T, TOOL_SPONGE);
    int pw = panel_w(), ph = panel_h();
    for (int y = 0; ; y += 14) { if (y > ph - 1) y = ph - 1; panel_stroke(0, y, pw - 1, y, 6); panel_stroke(pw - 1, y, 0, y, 6); if (y == ph - 1) break; }
    for (int x = 0; ; x += 14) { if (x > pw - 1) x = pw - 1; panel_stroke(x, 0, x, ph - 1, 6); panel_stroke(x, ph - 1, x, 0, 6); if (x == pw - 1) break; }
    int left = 0; *fx = *fy = -1;
    for (int cy = 0; cy < ALGAE_ROWS; cy++) for (int cx = 0; cx < ALGAE_COLS; cx++)
        if (T.algae[cy * ALGAE_COLS + cx] && !cell_off_glass(cx, cy)) { if (!left) { *fx = cx * ALGAE_CELL + ALGAE_CELL / 2; *fy = cy * ALGAE_CELL + ALGAE_CELL / 2; } left++; }
    memset(T.algae, 0, sizeof T.algae);
    bare();
    return left;
}
/* every WALL frond (a bed's outermost, within 48 px of a side wall: tank.c SLASH_WALL_PX), stroked from 80 px inside toward
   its wall to the panel's end, the scissors in hand: the stroke must pass over its spine and cut it. How many failed (the
   first one's spine and how far the stroke got), of how many tried */
static int fronds_walls(int *tried, float *first, float *got) {
    int bad = 0; *tried = 0; *first = *got = -1;
    for (int b = 0; b < tank_veg_beds(&T); b++) {
        int n; float x0, x1, top; tank_veg_bed(&T, b, &x0, &x1, &top, &n);
        if (n < 1) continue;
        for (int e = 0; e < 2; e++) {
            int i = e ? n - 1 : 0; float sx; tank_veg_frond(&T, b, i, &sx);
            int dir = e ? 1 : -1;
            if (dir < 0 ? sx > TANK_FX0 + 48 : sx < TANK_FX1 - 48) continue;   /* not a wall frond */
            float y = TANK_BOT - 60;
            for (int bb = 0; bb < tank_veg_beds(&T); bb++) tank_veg_set(&T, bb, 1.0f);
            bare(); tank_set_tool(&T, TOOL_SCISSORS);
            float h0 = T.veg_h[b][i];
            if (!aim(sx - dir * 80, y)) continue;
            float p0x = g_pbx, p0y = g_pby;
            if (!aim(sx - dir * 50, y)) continue;
            float vx = g_pbx - p0x, vy = g_pby - p0y, vl = hypotf(vx, vy); if (vl < 1) continue;
            vx /= vl; vy /= vl;
            float ex = p0x, ey = p0y;
            while (ex >= 0 && ey >= 0 && ex <= panel_w() - 1 && ey <= panel_h() - 1) { ex += vx; ey += vy; }
            (*tried)++;
            panel_stroke(p0x, p0y, ex, ey, 5);
            float reach = dir < 0 ? s_dx0 : s_dx1;
            bool over = dir < 0 ? reach <= sx : reach >= sx, cut = T.veg_h[b][i] < h0 - 0.05f;
            if (!over || !cut) { if (!bad) { *first = sx; *got = reach; } bad++; }
        }
    }
    for (int bb = 0; bb < tank_veg_beds(&T); bb++) tank_veg_set(&T, bb, 1.0f);
    bare();
    return bad;
}
static void calib_reach(void) {
    static const touch_fit_t KEEP = { 0.90f, 12.0f, 0.93f, 6.0f };
    int qs[4], nq = nquarters(qs), runs = 0, ntried = 0;
    for (int v = 0; v < (HB_V2 ? 2 : 1); v++) {
        g_v1 = v == 1;
        for (int k = 0; k < nq; k++) for (int m = 0; m < 2; m++) {
            adv_init(ADV_CELL_STOCK, m ? &KEEP : NULL);
            touch_port_set_quarter(qs[k]);
            int fx, fy, left = scrub_all(&fx, &fy), tried; float fs, fg; int stood = fronds_walls(&tried, &fs, &fg);
            const char *who = HB_V2 ? (g_v1 ? "the 1.8 V1" : "the 1.8 V2") : "the board";
            if (left) TFAIL("calibrated reach: %s, quarter %d, %s: %d film cells left on the glass after scrubbing the whole panel (the first at %d,%d)",
                            who, qs[k], m ? "the keeper's map" : "the board's own map", left, fx, fy);
            if (stood) TFAIL("calibrated reach: %s, quarter %d, %s: %d of %d wall fronds missed by a stroke to their wall (the first: spine x %.0f, the stroke got to %.0f)",
                             who, qs[k], m ? "the keeper's map" : "the board's own map", stood, tried, fs, fg);
            ntried += tried;
            runs++;
        }
    }
    g_v1 = false; adv_init(ADV_CELL_STOCK, NULL); touch_port_set_quarter(0); bare();
    printf("selftest-touch-port: the keeper's map and the glass's edges - %d runs (%s, every way up, the board's map and a keeper's 0.90 x + 12 / "
           "0.93 y + 6): every film cell scrubbed; %d strokes at a wall frond, each over its spine and cutting it\n", runs, HB_V2 ? "the V2 and V1 panels" : "this board", ntried);
}

/* B: a finger on the cross at (tx, ty) on the picture turned q: where the port reads it. The keeper's panel and finger are off
   the board's own map by an upright (panel) affine A u + B and a landing L in the viewer's frame (right, down) */
typedef struct { float ax, bx, ay, by, lr, ld; } keeper_t;
static void cal_inverse(float ux, float uy, float *rx, float *ry) {   /* cal_point at the port's quarter: x and y apart */
    float dx = 1e9f, dy = 1e9f;
    for (float r = 0; r <= TANK_W - 1; r += 0.25f) { float x, y; cal_point(r, TANK_H / 2.0f, &x, &y); if (fabsf(x - ux) < dx) { dx = fabsf(x - ux); *rx = r; } }
    for (float r = 0; r <= TANK_H - 1; r += 0.25f) { float x, y; cal_point(TANK_W / 2.0f, r, &x, &y); if (fabsf(y - uy) < dy) { dy = fabsf(y - uy); *ry = r; } }
}
static void keeper_panel(const keeper_t *k, int q, float tx, float ty, int *px, int *py) {   /* the panel px the finger presses */
    touch_port_set_quarter(q);
    float ux = tx, uy = ty; tank_quarter_to_upright(q, &ux, &uy);
    float Ux = k->ax * ux + k->bx + k->lr * CQ_RIGHT[q][0] + k->ld * CQ_DOWN[q][0];
    float Uy = k->ay * uy + k->by + k->lr * CQ_RIGHT[q][1] + k->ld * CQ_DOWN[q][1];
    float rx = 0, ry = 0; cal_inverse(Ux, Uy, &rx, &ry);
    int irx = (int)lroundf(rx), iry = (int)lroundf(ry);
#if defined(TANK_WATCH) || defined(TANK_SQUARE) || defined(TANK_ROUND)
    *px = irx; *py = iry;
#else
    *px = iry; *py = TANK_W - 1 - irx;                      /* map_touch's rx = TANK_W - 1 - fy, ry = fx, undone */
#endif
}
static void keeper_reads(const keeper_t *k, int q, float tx, float ty, float *x, float *y) {
    int px, py; keeper_panel(k, q, tx, ty, &px, &py);
    map_touch((uint16_t)px, (uint16_t)py, x, y);
}
static int intro_now(float x, float y, int want) { return render_touch_intro_tap(x, y, !touch_fit_is_none(adv_touch_fit())) == want; }
static void keeper_tap(const keeper_t *k, int q, float tx, float ty) {   /* a press on the glass, through the port's poll */
    int px, py; keeper_panel(k, q, tx, ty, &px, &py);
    finger_panel(px, py); frame(); frame(); lift(300);
}
static bool sitting(const keeper_t *k, int q) {           /* through the port: CALIBRATE, START, the crosses, the check - kept? */
    float x, y;
    touch_port_set_quarter(q); bare(); s_adv_act = 0;
    adv_open(); hold_for(200);
    if (!find_hit(hp_adv, ADV_HIT_CALIBRATE, &x, &y) || !tap(x, y, 90, 300)) { adv_close(); return false; }
    if (!find_hit(intro_now, ADV_HIT_START, &x, &y) || !tap(x, y, 90, 300)) { adv_close(); return false; }
    for (int i = 0; i < ADV_N_CROSS; i++) { float tx, ty; adv_cross_target(i, &tx, &ty); keeper_tap(k, q, tx, ty); }
    if (adv_page() != ADV_PG_CHECK) { adv_close(); bare(); return false; }
    float cx, cy; adv_check_target(&cx, &cy); keeper_tap(k, q, cx, cy);
    bool kept = s_adv_act == ADV_ACT_FIT; s_adv_act = 0;
    if (kept && (adv_page() != ADV_PG_TURN || !find_hit_read(hp_turn, ADV_HIT_SKIP, &x, &y) || !tap(x, y, 90, 300) || adv_page() != ADV_PG_DONE
                 || !find_hit_read(hp_done, ADV_HIT_OK, &x, &y) || !tap(x, y, 90, 300) || adv_page() != ADV_PG_MAIN)) kept = false;   /* one at a
                                                              time: SKIP at TURN IT OVER (calib_guided runs the turn) */
    adv_close(); bare();
    return kept;
}
static float worst_tap(const keeper_t *k, int q, float *wx, float *wy) {   /* a 5 x 5 of targets on the picture turned q */
    float worst = 0;
    for (int j = 0; j < 5; j++) for (int i = 0; i < 5; i++) {
        float tx = 70 + i * (TANK_W - 140) / 4.0f, ty = 60 + j * (TANK_H - 120) / 4.0f, x, y;
        keeper_reads(k, q, tx, ty, &x, &y);
        float e = hypotf(x - tx, y - ty); if (e > worst) { worst = e; *wx = tx; *wy = ty; }
    }
    return worst;
}
static void calib_turns(void) {
    static const keeper_t PANEL = { 1.04f, -9.0f, 0.96f, 7.0f, 0, 0 }, BOTH = { 1.04f, -9.0f, 0.96f, 7.0f, 4.0f, 9.0f };
    int qs[4], nq = nquarters(qs), n = 0;
    for (int v = 0; v < (HB_V2 ? 2 : 1); v++) {
        g_v1 = v == 1;
        const char *who = HB_V2 ? (g_v1 ? "the 1.8 V1" : "the 1.8 V2") : "the board";
        for (int s = 0; s < nq; s++) {                      /* one sitting of a panel-only error, taken at each quarter */
            adv_init(ADV_CELL_STOCK, NULL);
            if (!sitting(&PANEL, qs[s])) { TFAIL("calibrated turns: %s, a panel-only sitting at quarter %d was not kept", who, qs[s]); continue; }
            for (int k = 0; k < nq; k++) { float wx = 0, wy = 0, w = worst_tap(&PANEL, qs[k], &wx, &wy);
                if (w > 4) TFAIL("calibrated turns: %s, one sitting at quarter %d (the panel's error only): a tap at quarter %d lands %.1f px off (at %.0f,%.0f)", who, qs[s], qs[k], w, wx, wy); n++; }
        }
        for (int s = 1; s < nq; s++) {                      /* a panel AND a finger: a sitting upright, then one at another quarter */
            adv_init(ADV_CELL_STOCK, NULL);
            if (!sitting(&BOTH, 0)) { TFAIL("calibrated turns: %s, the upright sitting was not kept", who); continue; }
            float w0x = 0, w0y = 0, w0 = worst_tap(&BOTH, 0, &w0x, &w0y);
            if (w0 > 4) TFAIL("calibrated turns: %s, upright right after its own sitting: %.1f px off", who, w0);
            if (!sitting(&BOTH, qs[s])) { TFAIL("calibrated turns: %s, the second sitting (quarter %d) was not kept", who, qs[s]); continue; }
            for (int k = 0; k < nq; k++) { float wx = 0, wy = 0, w = worst_tap(&BOTH, qs[k], &wx, &wy);
                if (w > 4) TFAIL("calibrated turns: %s, sittings at quarters 0 and %d (a panel and a finger): a tap at quarter %d lands %.1f px off (at %.0f,%.0f)", who, qs[s], qs[k], w, wx, wy); n++; }
        }
        { adv_init(ADV_CELL_STOCK, NULL); sitting(&BOTH, 0); float wx, wy;   /* (one sitting can't tell them apart: what it leaves turned, for the record) */
          printf("selftest-touch-port: (%s, one upright sitting of a panel and a finger landing 4 right / 9 low: upright %.1f px, turned over %.1f px)\n",
                 who, worst_tap(&BOTH, 0, &wx, &wy), worst_tap(&BOTH, qs[nq - 1] == 3 ? 2 : qs[nq - 1], &wx, &wy)); }
    }
    g_v1 = false; adv_init(ADV_CELL_STOCK, NULL); touch_port_set_quarter(0); bare();
    printf("selftest-touch-port: the keeper's map turned - %d checks: a panel's own error from one sitting holds every way up; a panel and a finger "
           "from two sittings (upright + another way up) hold every way up (taps within 4 px)\n", n);
}

/* THE TURN, GUIDED (0.4.2): the whole flow through the port, as main.c runs it - the way up handed through adv_quarter each
   frame: the first sitting upright, TURN IT OVER, the IMU (the watch: GO on the page drawn turned), the second sitting upside
   down - and a panel AND a finger's landing then hold every way up. Each tap is the keeper's finger on the glass, read by
   the board's own chip and map */
static int g_live = 0;                                      /* the pretend IMU (-1: the watch has none to go by) */
static void way_up(int shown) { touch_port_set_quarter(adv_quarter(shown, g_live)); }
static void calib_guided(void) {
    static const keeper_t BOTH = { 1.04f, -9.0f, 0.96f, 7.0f, 4.0f, 9.0f };
    int qs[4], nq = nquarters(qs), n = 0;
    for (int v = 0; v < (HB_V2 ? 2 : 1); v++) {
        g_v1 = v == 1;
        const char *who = HB_V2 ? (g_v1 ? "the 1.8 V1" : "the 1.8 V2") : "the board";
        float x, y;
        adv_init(ADV_CELL_STOCK, NULL); g_live = TANK_WORN ? -1 : 0; way_up(0); bare(); s_adv_act = 0;
        adv_open(); hold_for(200);
        if (!find_hit(hp_adv, ADV_HIT_CALIBRATE, &x, &y) || !tap(x, y, 90, 300) || !find_hit(intro_now, ADV_HIT_START, &x, &y) || !tap(x, y, 90, 300))
            { TFAIL("guided: %s, CALIBRATE / START not reached", who); adv_close(); continue; }
        for (int i = 0; i < ADV_N_CROSS; i++) { float tx, ty; adv_cross_target(i, &tx, &ty); way_up(0); keeper_tap(&BOTH, 0, tx, ty); }
        float cx, cy; adv_check_target(&cx, &cy); way_up(0); keeper_tap(&BOTH, 0, cx, cy);
        if (s_adv_act != ADV_ACT_FIT || adv_page() != ADV_PG_TURN) { TFAIL("guided: %s, the first sitting: saved %d, page %d (want saved, TURN IT OVER)", who, s_adv_act == ADV_ACT_FIT, adv_page()); adv_close(); continue; }
        s_adv_act = 0; way_up(0); hold_for(500);
        if (adv_page() != ADV_PG_TURN) TFAIL("guided: %s, TURN IT OVER did not wait upright", who);
        int tq;
        if (TANK_WORN) {                                    /* the watch: the page drawn turned, GO on it */
            tq = adv_quarter(0, -1); way_up(0);
            if (tq != 2 || !find_hit(hp_turn, ADV_HIT_GO, &x, &y) || !tap(x, y, 90, 300)) TFAIL("guided: %s, GO (the page at quarter %d)", who, tq);
        } else { g_live = 2; way_up(0); tq = 2; }           /* turned over: the IMU says so */
        if (adv_page() != ADV_PG_CROSS || !adv_turned_sitting()) { TFAIL("guided: %s, the turned crosses did not come (page %d)", who, adv_page()); adv_close(); continue; }
        for (int i = 0; i < ADV_N_CROSS; i++) { float tx, ty; adv_cross_target(i, &tx, &ty); way_up(0); keeper_tap(&BOTH, tq, tx, ty); }
        way_up(0); keeper_tap(&BOTH, tq, cx, cy);
        if (s_adv_act != ADV_ACT_FIT || adv_page() != ADV_PG_DONE || adv_touch_cal()->n != 2)
            { TFAIL("guided: %s, the turned sitting: saved %d, page %d, %d sittings", who, s_adv_act == ADV_ACT_FIT, adv_page(), adv_touch_cal()->n); adv_close(); continue; }
        s_adv_act = 0;
        way_up(0);                                          /* DONE: still the way the tank is held, its OK there */
        if (!find_hit_read(hp_done, ADV_HIT_OK, &x, &y) || !tap(x, y, 90, 300) || adv_page() != ADV_PG_MAIN) TFAIL("guided: %s, DONE's OK (page %d)", who, adv_page());
        adv_close();
        for (int k = 0; k < nq; k++) { float wx = 0, wy = 0, w = worst_tap(&BOTH, qs[k], &wx, &wy);
            if (w > 4) TFAIL("guided: %s, the two sittings of the flow: a tap at quarter %d lands %.1f px off (at %.0f,%.0f)", who, qs[k], w, wx, wy); n++; }
        g_live = 0; touch_port_set_quarter(0); bare();
    }
    g_v1 = false; g_live = 0; adv_init(ADV_CELL_STOCK, NULL); touch_port_set_quarter(0); bare();
    printf("selftest-touch-port: the guided turn - %d checks: the first sitting upright, TURN IT OVER (%s), the second upside down, through "
           "the port: a panel and a finger landing 4 right / 9 low hold every way up (taps within 4 px)\n", n, TANK_WORN ? "GO on the page drawn turned" : "the IMU's turn");
}

/* ---- THE WHEEL'S FEEL (0.4.2, setup.c): the letter wheel through the board's port ----
 * A keeper: "tapping the next letter when trying to adjust the current one, or changing the current one when trying to go
 * to the next". Strato: a drag anywhere on the dial scrolls, wherever it starts; a letter just turned keeps the row for a
 * moment. Held here, on a fish's RENAME:
 *  A. DRAG ANYWHERE: a drag that starts on the up chevron, on the down chevron, or just under the letter turns the focused
 *     letter a step per SETUP_SPIN_PX, and its lift taps nothing; a drag past the slop but short of a step does nothing (it
 *     is no tap on the chevron); a tap with a small roll still steps once. Above a letter past the chevrons is its column.
 *  B. THE CURRENT LETTER AT ONCE: right after a change the chevron's tap steps at its lift, a drag on the letter turns it
 *     on the frame it passes a step - never a wait.
 *  C. THE LETTER HOLD: a tap on the next letter inside the hold keeps the focus (a drag from there turns the focused one);
 *     a press it kept never restarts the hold; past the hold a tap picks the next letter at once.
 *  D. reach < the row: a tap squarely on the next letter picks it at once, one at the edge between them stays; hold 0 = none. */
static int hit_setup_w(float x, float y, int want) { return setup_hit(x, y) == want; }
static float slot_cx(int i) { return PAGE_X + SETUP_SLOT_X + i * SETUP_SLOT_PX + SETUP_SLOT_W * 0.5f; }
static float slot_cy(void) { return PAGE_Y + SETUP_SLOT_Y + SETUP_SLOT_H * 0.5f; }
static bool wdrag(float x, float y0, float y1, int frames, int up_ms) {   /* a finger down at (x, y0), drawn to y1 over `frames`, lifted */
    if (!aim(x, y0)) return false;
    g_down = true; frame();
    for (int k = 1; k <= frames; k++) { if (!aim(x, y0 + (y1 - y0) * k / frames)) return false; frame(); }
    lift(up_ms); return true;
}
static bool wtap(float x, float y, int down_ms, int up_ms) {   /* a tap that must land (a point the panel cannot read = a FAIL, never a silent pass) */
    if (tap(x, y, down_ms, up_ms)) return true;
    TFAIL("wheel: the panel cannot read %.0f,%.0f", x, y); return false;
}
static void wheel_feel_port(void) {
    int hold0, reach0, slop0; setup_wheel_tuning(&hold0, &reach0, &slop0);
    bare(); tank_set_name(&T, 0, "aaa");
    setup_wheel_tune(500, 999, 10);
    setup_begin_rename(&T, 0); hold_for(800);
    float ux, uy, dx, dy; int n = 0;
    if (!setup_is_rename() || setup_slot() != 0 || !find_hit(hit_setup_w, SETUP_HIT_UP, &ux, &uy) || !find_hit(hit_setup_w, SETUP_HIT_DOWN, &dx, &dy)) {
        TFAIL("wheel: the rename wheel or its chevrons not found"); if (setup_active()) setup_cancel(&T); return;
    }
    const float sp = SETUP_SPIN_PX, x0 = slot_cx(0), cy = slot_cy();
    #define NAME_IS(want, ...) do { if (strcmp(T.fish[0].name, (want))) { TFAIL(__VA_ARGS__); printf("      (the name is '%s', want '%s')\n", T.fish[0].name, (want)); } else n++; } while (0)
    /* A. from the up chevron, three steps up: aaa -> daa (the lift no fourth) */
    if (!wdrag(x0, uy, uy - 3 * sp - 4, 12, 700)) TFAIL("wheel: the up chevron's drag off the glass");
    NAME_IS("daa", "wheel A: a drag begun on the UP chevron did not turn the letter three steps");
    if (!wdrag(x0, dy, dy + 2 * sp + 4, 10, 700)) TFAIL("wheel: the down chevron's drag off the glass");   /* from the down chevron, two down: baa */
    NAME_IS("baa", "wheel A: a drag begun on the DOWN chevron did not turn the letter two steps");
    {   /* just under the letter: 8 px below the row's band (the down chevron's band, a tap there would step DOWN) */
        const float yu = PAGE_Y + SETUP_SLOT_Y + SETUP_SLOT_H + 20 + 8;
        if (setup_hit(x0, yu) != SETUP_HIT_DOWN) TFAIL("wheel: 8 px under the row's band is not the down chevron's (%d)", setup_hit(x0, yu));
        if (!wdrag(x0, yu, yu - sp - 4, 6, 700)) TFAIL("wheel: the under-letter drag off the glass");
    }
    NAME_IS("caa", "wheel A: a drag begun just under the letter did not turn it a step up");
    if (!wdrag(x0, uy, uy + 16, 4, 700)) TFAIL("wheel: the short drag off the glass");                    /* past the slop, short of a step */
    NAME_IS("caa", "wheel A: a 16 px drag on the UP chevron acted (a tap or a step)");
    if (!wdrag(x0, uy, uy - 6, 2, 700)) TFAIL("wheel: the rolled tap off the glass");                     /* a tap that rolled 6 px */
    NAME_IS("daa", "wheel A: a tap on UP that rolled 6 px did not step once");
    if (setup_slot() != 0) TFAIL("wheel A: the focus moved to %d", setup_slot());
    /* B + C: a change, then at once the next letter tapped: kept on slot 0; the focused letter's chevron and drag at once */
    wtap(ux, uy, 90, 120);                                                                       /* d -> e: a change */
    NAME_IS("eaa", "wheel B: UP tapped did not step");
    wtap(slot_cx(1), cy, 90, 120);                                                                /* ~0.12 s after the change */
    if (setup_slot() != 0) TFAIL("wheel C: a tap on the next letter 0.2 s after a change moved the focus to %d", setup_slot()); else n++;
    wtap(ux, uy, 90, 0);                                                                          /* the focused letter: at once */
    NAME_IS("faa", "wheel B: the chevron's tap right after a kept press did not step at its lift");
    {   /* a drag from the next letter's middle, inside the hold: the focused letter turns, on the frame it passes a step */
        if (!aim(slot_cx(1), cy)) TFAIL("wheel: slot 1 off the glass");
        g_down = true; frame();
        int at = -1;
        for (int k = 1; k <= 8 && at < 0; k++) { if (!aim(slot_cx(1), cy - k * 12.0f)) TFAIL("wheel: the drag's frame %d off the glass", k); frame(); if (T.fish[0].name[0] == 'g') at = k; }
        lift(120);
        if (at != 3) TFAIL("wheel B/C: a drag from the next letter inside the hold turned the focused letter at frame %d (want 3: 36 px, the step's own frame)", at); else n++;
        NAME_IS("gaa", "wheel C: the drag from the next letter inside the hold did not turn the focused one (or turned the next)");
        if (setup_slot() != 0) TFAIL("wheel C: the drag from the next letter moved the focus to %d", setup_slot());
    }
    /* C: kept presses never restart the hold - change at t0, kept presses at ~0.08 / ~0.28 s, the next at ~0.6 s picks */
    wtap(ux, uy, 90, 60);                                                                         /* g -> h, t0 */
    wtap(slot_cx(1), cy, 90, 60);
    wtap(slot_cx(1), cy, 90, 60);
    if (setup_slot() != 0) TFAIL("wheel C: taps on the next letter inside the hold moved the focus to %d", setup_slot()); else n++;
    hold_for(120); wtap(slot_cx(1), cy, 90, 60);                                                  /* pressed ~0.6 s after the change (a restart at
                                                                                                    the last kept press would hold it to ~0.78 s) */
    if (setup_slot() != 1) TFAIL("wheel C: past the hold (the kept taps restarted it?) a tap on the next letter left the focus on %d", setup_slot()); else n++;
    wtap(slot_cx(0), cy, 90, 120);                                                                /* no change since: back at once */
    if (setup_slot() != 0) TFAIL("wheel C: with no change a tap on another letter did not pick it at once (%d)", setup_slot()); else n++;
    /* D: reach 12 - squarely on the next letter picks it at once; 6 px past the edge between them stays */
    setup_wheel_tune(-1, 12, -1);
    wtap(ux, uy, 90, 120);                                                                        /* h -> i */
    wtap(x0 + SETUP_SLOT_PX * 0.5f + 6, cy, 90, 120);
    if (setup_slot() != 0) TFAIL("wheel D: reach 12, a tap 6 px past the edge moved the focus to %d", setup_slot()); else n++;
    wtap(slot_cx(1), cy, 90, 120);
    if (setup_slot() != 1) TFAIL("wheel D: reach 12, a tap squarely on the next letter inside the hold left the focus on %d", setup_slot()); else n++;
    setup_wheel_tune(0, 999, -1);                                                                /* hold 0: none */
    wtap(ux, uy, 90, 120);                                                                        /* slot 1: a -> b */
    wtap(slot_cx(0), cy, 90, 120);
    if (setup_slot() != 0) TFAIL("wheel D: hold 0, a tap on another letter right after a change did not pick it (%d)", setup_slot()); else n++;
    NAME_IS("iba", "wheel D: the name after the reach / hold cases");
    /* A again, far along the row: a drag begun above a letter past the chevrons' reach picks that letter and turns it */
    setup_wheel_tune(500, 999, -1); hold_for(700);
    if (!wdrag(slot_cx(3), uy, uy - sp - 4, 6, 700)) TFAIL("wheel: the far column's drag off the glass");
    if (setup_slot() != 3) TFAIL("wheel A: a drag above the fourth letter (past the chevrons) left the focus on %d", setup_slot()); else n++;
    NAME_IS("ibaa", "wheel A: a drag above the fourth letter did not turn it");
    #undef NAME_IS
    setup_cancel(&T); bare();
    setup_wheel_tune(hold0, reach0, slop0);
    printf("selftest-touch-port: the wheel's feel - %d checks (a drag from either chevron or under the letter turns it, a short drag "
           "taps nothing, a rolled tap steps; the turned letter answers at once; the next letter waits out the hold, which no kept "
           "press restarts; reach and hold 0)\n", n);
}

/* THE RESET PROMPT'S PAGES (0.4.2, render.h RESET_PG_*) under the port's own taps: the chord opens the CHOICE; a tap never
   erases; ERASE EVERYTHING is only the full hold on its last page, of a press that began on that page; each page has its
   own 20 s; a mash on any button never erases */
static int hit_reset(float x, float y, int btn) { return render_reset_hit(x, y) == btn; }
static bool rp_at(int btn, float *x, float *y) { return find_hit_read(hit_reset, btn, x, y); }
static bool rp_tap(int btn, int after_ms) { float x, y; if (!rp_at(btn, &x, &y)) return false; lift(after_ms); return tap(x, y, 90, FRAME_MS); }
static bool rp_to_hold(bool wifi) {                         /* the chord, ERASE EVERYTHING, the box, NEXT: the last page up */
    bare(); touch_port_confirm_open(); hold_for(60);
    if (!rp_tap(RESET_BTN_ERASE, 300) || render_reset_page() != RESET_PG_ERASE) return false;
    if (wifi && (!rp_tap(RESET_BTN_WIFI, 300) || !render_reset_wifi())) return false;
    return rp_tap(RESET_BTN_NEXT, 300) && render_reset_page() == RESET_PG_HOLD && touch_port_confirm_up();
}
static void reset_pages_port(void) {
    float x, y, hx, hy, nx, ny; int a, n = 0;
    /* the chord opens the choice; its opening tap's double is no tap; then ERASE EVERYTHING brings its page */
    bare(); touch_port_confirm_open(); hold_for(60);
    if (render_reset_page() != RESET_PG_CHOOSE) TFAIL("reset pages: the chord opened page %d (want the choice)", render_reset_page());
    if (!rp_at(RESET_BTN_ERASE, &x, &y)) { TFAIL("reset pages: the choice has no ERASE EVERYTHING the panel can read"); return; }
    tap(x, y, 80, FRAME_MS);
    if (render_reset_page() != RESET_PG_CHOOSE || touch_port_confirm_take()) TFAIL("reset pages: ERASE EVERYTHING 0.1 s after the prompt opened was taken (page %d)", render_reset_page());
    lift(300); tap(x, y, 80, FRAME_MS);
    if (render_reset_page() != RESET_PG_ERASE || render_reset_wifi()) TFAIL("reset pages: ERASE EVERYTHING did not bring its page with the box unticked (page %d)", render_reset_page());
    /* the double of that tap, wherever it lands on the new page: nothing */
    tap(x, y, 80, FRAME_MS);
    if (render_reset_page() != RESET_PG_ERASE || render_reset_wifi() || touch_port_confirm_take()) TFAIL("reset pages: the double of the tap that brought ERASE EVERYTHING? did something");
    /* the box, NEXT, the last page */
    if (!rp_tap(RESET_BTN_WIFI, 300) || !render_reset_wifi()) TFAIL("reset pages: the Wi-Fi box did not tick");
    if (!rp_tap(RESET_BTN_WIFI, 300) || render_reset_wifi()) TFAIL("reset pages: the Wi-Fi box did not untick");
    if (!rp_tap(RESET_BTN_NEXT, 300) || render_reset_page() != RESET_PG_HOLD) TFAIL("reset pages: NEXT did not bring the last page (page %d)", render_reset_page());
    if (!rp_at(RESET_BTN_HOLD, &hx, &hy) || !rp_at(RESET_BTN_BACK, &nx, &ny)) { TFAIL("reset pages: the last page has no HOLD TO ERASE or no NO the panel can read"); return; }
    n++;
    /* a tap, a mash, a short hold: nothing erased, the prompt still up, the bar empty */
    lift(300); tap(hx, hy, 90, FRAME_MS);
    if (touch_port_confirm_take() || !touch_port_confirm_up() || render_reset_hold_frac() != 0) TFAIL("reset pages: a tap on HOLD TO ERASE answered, or left the bar part full");
    mash(hx, hy, MASH_N);
    if (touch_port_confirm_take() || !touch_port_confirm_up()) TFAIL("reset pages: a mash on HOLD TO ERASE answered");
    lift(300); press(hx, hy, 2500);
    if (touch_port_confirm_take() || !render_reset_holding() || render_reset_hold_frac() < 0.7f) TFAIL("reset pages: 2.5 s into the hold: answered, or the bar shows %.2f", render_reset_hold_frac());
    lift(FRAME_MS);
    if (touch_port_confirm_take() || !touch_port_confirm_up() || render_reset_hold_frac() != 0 || render_reset_page() != RESET_PG_HOLD) TFAIL("reset pages: let go at 2.5 s: answered, or the bar stayed");
    /* slid off at 1 s, held on, slid back: that press is done */
    lift(300); press(hx, hy, 1000); aim(nx, ny); hold_for(400); aim(hx, hy); hold_for(4000);
    if (touch_port_confirm_take() || !touch_port_confirm_up()) TFAIL("reset pages: slid off HOLD TO ERASE and back, held 4 s more: answered");
    lift(FRAME_MS);
    if (!touch_port_confirm_up() || render_reset_page() != RESET_PG_HOLD) TFAIL("reset pages: the slide's lift closed the prompt");
    n++;
    /* the full hold: ERASE (the box unticked); the lift after it is nothing */
    lift(300); press(hx, hy, 2900);
    if (touch_port_confirm_take()) TFAIL("reset pages: the hold answered before 3 s");
    hold_for(300);
    if ((a = touch_port_confirm_take()) != RESET_ANS_ERASE || touch_port_confirm_up()) TFAIL("reset pages: the full hold answered %d (want %d), prompt %s", a, RESET_ANS_ERASE, touch_port_confirm_up() ? "still up" : "down");
    lift(FRAME_MS); n++;
    /* ... and with the box ticked */
    if (!rp_to_hold(true)) TFAIL("reset pages: could not walk to the last page with the box ticked");
    else { lift(300); press(hx, hy, 3200);
           if ((a = touch_port_confirm_take()) != RESET_ANS_ERASE_WIFI) TFAIL("reset pages: the full hold, the box ticked, answered %d (want %d)", a, RESET_ANS_ERASE_WIFI);
           lift(FRAME_MS); n++; }
    /* a press from before the page (the finger already there as it came), and one in its first 0.2 s: held 5 s, nothing */
    bare(); press(hx, hy, 200); touch_port_confirm_open_page(RESET_PG_HOLD); hold_for(5000);
    if (touch_port_confirm_take() > 0) TFAIL("reset pages: a finger down before the last page came, held 5 s, erased");
    lift(FRAME_MS); touch_port_confirm_answer(-1); touch_port_confirm_take();
    bare(); touch_port_confirm_open_page(RESET_PG_HOLD); hold_for(80); press(hx, hy, 5000);
    if (touch_port_confirm_take() > 0) TFAIL("reset pages: a press 0.1 s after the last page came, held 5 s, erased");
    lift(FRAME_MS); touch_port_confirm_answer(-1); touch_port_confirm_take(); n++;
    /* A STALL: 0.5 s into the hold the tank task stops for 5 s, then reads the finger still down - a step, not a hold */
    if (rp_to_hold(false)) { lift(300); press(hx, hy, 500); g_now += 5000000; frame(); frame();
        if (touch_port_confirm_take() > 0) TFAIL("reset pages: 0.5 s held, a 5 s stall of the task, one read of the finger: ERASED");
        if (render_reset_hold_frac() > 0.4f) TFAIL("reset pages: after the stall the bar shows %.2f (the stall was counted as held)", render_reset_hold_frac());
        lift(FRAME_MS); touch_port_confirm_answer(-1); touch_port_confirm_take(); n++; }
    /* COULD NOT ERASE (the board refused the first write): OK is one tap, a NO; a hold on it is nothing */
    bare(); touch_port_confirm_open_page(RESET_PG_REFUSED); hold_for(300);
    if (rp_at(RESET_BTN_BACK, &x, &y)) { press(x, y, 4000);
        if (touch_port_confirm_take() > 0) TFAIL("reset pages: COULD NOT ERASE's OK held 4 s answered an erase");
        lift(FRAME_MS); if (touch_port_confirm_up()) { lift(300); tap(x, y, 90, FRAME_MS); }
        if ((a = touch_port_confirm_take()) > 0 || touch_port_confirm_up()) TFAIL("reset pages: COULD NOT ERASE's OK did not close it as a NO (%d)", a); n++; }
    else TFAIL("reset pages: COULD NOT ERASE has no OK the panel can read");
    /* ERASING, ARMED (the hold's answer behind it): nothing on it answers and it never gives up - only the erase ends it */
    bare(); touch_port_confirm_open_page(RESET_PG_ERASING); render_reset_erasing(RESET_ANS_ERASE); hold_for(300);
    for (int gy = 1; gy < 6; gy++) for (int gx = 1; gx < 6; gx++) if (aim(TANK_W * gx / 6.0f, TANK_H * gy / 6.0f)) { tap(TANK_W * gx / 6.0f, TANK_H * gy / 6.0f, 90, 260);
        if (touch_port_confirm_take() || !touch_port_confirm_up() || render_reset_page() != RESET_PG_ERASING || !render_reset_erase_armed()) { TFAIL("reset pages: a tap at %d,%d on ERASING (armed) did something", TANK_W * gx / 6, TANK_H * gy / 6); break; } }
    press(TANK_W / 2.0f, TANK_H / 2.0f, 5000); lift(FRAME_MS); hold_for(60000);
    if (touch_port_confirm_take() || !touch_port_confirm_up() || render_reset_page() != RESET_PG_ERASING || !render_reset_erase_armed()) TFAIL("reset pages: ERASING (armed) answered a hold, or gave up after a minute");
    touch_port_confirm_answer(-1); touch_port_confirm_take();
    if (render_reset_erase_armed()) TFAIL("reset pages: the prompt answered NO under an armed erase: still armed");
    /* ... STAGED by name (the director's `reset erasing`, no erase behind it): a tap takes it down, and it gives up by itself */
    bare(); touch_port_confirm_open_page(RESET_PG_ERASING); hold_for(300);
    if (aim(TANK_W / 2.0f, TANK_H / 2.0f)) { tap(TANK_W / 2.0f, TANK_H / 2.0f, 90, FRAME_MS);
        if ((a = touch_port_confirm_take()) != RESET_ANS_NO || touch_port_confirm_up()) TFAIL("reset pages: a tap on the staged ERASING page did not take it down (%d)", a); }
    bare(); touch_port_confirm_open_page(RESET_PG_ERASING); hold_for(19000);
    if (!touch_port_confirm_up()) TFAIL("reset pages: the staged ERASING page gave up before 20 s");
    hold_for(1500);
    if (touch_port_confirm_up() || touch_port_confirm_take() != RESET_ANS_NO) TFAIL("reset pages: the staged ERASING page never gives up (it holds the glass, the chord and BOOT twice)");
    n++;
    /* NO on the last page: one tap, the tank kept */
    if (rp_to_hold(false)) { rp_tap(RESET_BTN_BACK, 300);
        if ((a = touch_port_confirm_take()) != RESET_ANS_NO || touch_port_confirm_up()) TFAIL("reset pages: NO on the last page answered %d", a); n++; }
    /* BACK on ERASE EVERYTHING?: the choice again; BACK there: out */
    bare(); touch_port_confirm_open(); hold_for(60); rp_tap(RESET_BTN_ERASE, 300); rp_tap(RESET_BTN_BACK, 300);
    if (render_reset_page() != RESET_PG_CHOOSE || !touch_port_confirm_up() || touch_port_confirm_take()) TFAIL("reset pages: BACK on ERASE EVERYTHING? did not bring the choice back");
    rp_tap(RESET_BTN_BACK, 300);
    if ((a = touch_port_confirm_take()) != RESET_ANS_NO || touch_port_confirm_up()) TFAIL("reset pages: BACK on the choice answered %d (want NO)", a);
    /* RESET THIS TANK: its page, YES = 1 (the reset as it always was); NO = -1 */
    bare(); touch_port_confirm_open(); hold_for(60); rp_tap(RESET_BTN_TANK, 300);
    if (render_reset_page() != RESET_PG_TANK) TFAIL("reset pages: RESET THIS TANK did not bring its page (page %d)", render_reset_page());
    rp_tap(RESET_BTN_YES, 300);
    if ((a = touch_port_confirm_take()) != RESET_ANS_TANK) TFAIL("reset pages: RESET THIS TANK's YES answered %d (want %d)", a, RESET_ANS_TANK);
    bare(); touch_port_confirm_open(); hold_for(60); rp_tap(RESET_BTN_TANK, 300); rp_tap(RESET_BTN_BACK, 300);
    if ((a = touch_port_confirm_take()) != RESET_ANS_NO) TFAIL("reset pages: RESET THIS TANK's NO answered %d", a);
    n++;
    /* a mash at every button's place, from the choice on, 60 taps each: never ERASE EVERYTHING */
    for (int b = RESET_BTN_BACK; b <= RESET_BTN_HOLD; b++)
        for (int pg = RESET_PG_CHOOSE; pg <= RESET_PG_REFUSED; pg++) {
            bare(); render_reset_open(pg); bool have = rp_at(b, &x, &y); render_reset_close();
            if (!have) continue;
            touch_port_confirm_open(); hold_for(60);
            for (int k = 0; k < 60; k++) { tap(x, y, 80, 120); a = touch_port_confirm_take();
                if (a >= RESET_ANS_ERASE) { TFAIL("reset pages: a mash where button %d is on page %d ERASED EVERYTHING (tap %d)", b, pg, k); break; }
                if (!touch_port_confirm_up()) { touch_port_confirm_open(); hold_for(60); } }
            touch_port_confirm_answer(-1); touch_port_confirm_take();
        }
    n++;
    /* the give-up: 20 s on a page nobody answers; a page a tap brought has its own 20 s; never out from under a hold */
    bare(); touch_port_confirm_open(); hold_for(19000);
    if (!touch_port_confirm_up()) TFAIL("reset pages: the choice gave up before 20 s");
    hold_for(1500);
    if ((a = touch_port_confirm_take()) != RESET_ANS_NO || touch_port_confirm_up()) TFAIL("reset pages: the choice, nobody answering, did not give up as NO (%d)", a);
    bare(); touch_port_confirm_open(); hold_for(15000); rp_tap(RESET_BTN_ERASE, FRAME_MS); hold_for(15000);
    if (!touch_port_confirm_up() || render_reset_page() != RESET_PG_ERASE) TFAIL("reset pages: ERASE EVERYTHING?, 15 s old, went with the choice's 20 s");
    hold_for(6000);
    if ((a = touch_port_confirm_take()) != RESET_ANS_NO) TFAIL("reset pages: ERASE EVERYTHING? did not give up as NO (%d)", a);
    if (rp_to_hold(false)) { hold_for(18500); press(hx, hy, 3300);
        if ((a = touch_port_confirm_take()) != RESET_ANS_ERASE) TFAIL("reset pages: a hold begun 1.5 s before the give-up answered %d (want the erase: the timeout waits for a finger holding)", a);
        lift(FRAME_MS); }
    if (rp_to_hold(false)) { hold_for(18500); press(hx, hy, 2000); lift(FRAME_MS); hold_for(400);
        if ((a = touch_port_confirm_take()) != RESET_ANS_NO) TFAIL("reset pages: a short hold across the give-up, let go: answer %d (want NO, its time was up)", a); }
    n++;
    bare();
    printf("selftest-touch-port: the reset prompt's pages - %d cases (the choice, the opening tap's double, the box, taps / a mash / a short hold / a slide never erase, "
           "the full hold erases by the box, a press from before the page never, NO and BACK, each page's 20 s)\n", n);
}
/* --selftest-full-reset's port half: the port brought up as selftest_touch_port does, then the pages alone */
int touch_port_reset_prompt(void) {
    g_log = getenv("PT_TOUCH_LOG") != NULL;
    char sav[96]; snprintf(sav, sizeof sav, "/tmp/pocket-tank-selftest-resetpages-%s.sav", HB_ROUND ? "bowl" : HB_WATCH ? "watch" : HB_SQUARE ? "square" : "rect");
    setenv("POCKET_TANK_SAVE", sav, 1); remove(sav);
    tank_init(&T, 9191); progression_boot(&T); progression_setup_done(&T);
    T.trickle_off = true;
    if (!touch_port_init()) { printf("FAIL: touch-port: the port found no chip\n"); return 1; }
    touch_port_set_quiet(false);
    fails = 0;
    reset_pages_port();
    remove(sav);
    return fails;
}

/* --selftest-back-overlap (0.4.2): BACK OVER THE SETTINGS FOOT. BACK on the GRAPHICS page and on the UPDATES page (and its
 * WI-FI face) is one box in the middle of the settings foot's band; back on the settings page the same spot is the foot:
 * LINK under BACK's middle, UPDATES under its left end, CLOSE under its right. Strato's decision: BACK and CLOSE stay
 * where they are, and ONE guard is built - back from UPDATES, a new press where BACK was is spent for SET_UPD_BACK_S
 * (render.h). No other page gets a wait.
 *   A  the footprints, counted (told, not judged): every point of each BACK against the settings page's hit test
 *   B  UPDATES -> settings: a second tap 0.44 s after BACK's lift at BACK's middle, its left end and its right end does
 *      nothing (no link mode, no UPDATES again, settings not closed), and the window is not restarted by it: the same
 *      tap after the window acts (LINK / UPDATES / CLOSE). CONTROL: PT_UPDBACK_OLD=1 skips the port's call - red
 *   C  ... while everything else answers: 0.3 s after BACK's lift AUTO FEED, a slider, the GRAPHICS tile and CLOSE outside
 *      the footprint each act (0.3 s: past the page gate's 0.2 s, THE TOUCH RULE's double, which is all that ever waited)
 *   D  WI-FI -> UPDATES -> settings by three taps at one point, 0.3 s apart: ends on the settings page, link mode NOT open
 *      (WI-FI's BACK under UPDATES' BACK is the same action, back again: not guarded)
 *   E  GRAPHICS -> settings, UNCHANGED FROM MAIN (known, documented, not a failure): its BACK has no such guard - the page
 *      takes no press for 0.4 s after it (SET_TURN_WAIT_S, as on main), and a second tap at BACK's middle 0.6 s on
 *      OPENS LINK MODE; 0.44 s after an UPDATES BACK elsewhere in the run changes nothing of that */
int selftest_back_overlap(void) {
    char sav[96]; snprintf(sav, sizeof sav, "/tmp/pocket-tank-selftest-backoverlap-%s.sav", HB_ROUND ? "bowl" : HB_WATCH ? "watch" : HB_SQUARE ? "square" : "rect");
    setenv("POCKET_TANK_SAVE", sav, 1); remove(sav);
    tank_init(&T, 9191); progression_boot(&T); progression_setup_done(&T); T.trickle_off = true;
    if (!touch_port_init()) { printf("FAIL: back-overlap: the port found no chip\n"); return 1; }
    touch_port_set_quiet(false);
    fails = 0;
    #define BF(...) do { printf("FAIL: back-overlap: "); printf(__VA_ARGS__); printf("\n"); fails++; } while (0)
    /* ---- A ---- */
    for (int page = 0; page < 2; page++) {                   /* 0 the GRAPHICS page's BACK, 1 the UPDATES page's */
        int n = 0, on[4] = { 0, 0, 0, 0 }, diff = 0;
        if (page) updates_page_open();
        for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) {
            int v; const bool g = render_gfx_page_tap((float)x, (float)y, &v) == SET_TAP_GFX_PAGE, u = updates_page_tap((float)x, (float)y) == UPD_TAP_CLOSE;
            if (page && g != u) diff++;
            if (page ? !u : !g) continue;
            const int h = render_settings_tap((float)x, (float)y, &v);
            n++; on[h == SET_TAP_UPDATES ? 0 : h == SET_TAP_LINK ? 1 : h == SET_TAP_CLOSE ? 2 : 3]++;
        }
        printf("back-overlap: the %s page's BACK: of its %d px of reach the settings page reads UPDATES at %d, LINK at %d, CLOSE at %d, nothing at %d\n", page ? "UPDATES" : "GRAPHICS", n, on[0], on[1], on[2], on[3]);
        if (diff) BF("A: the UPDATES page's BACK and the GRAPHICS page's differ at %d px (the guard reads its footprint from the GRAPHICS page's)", diff);
    }
    int gx, gy, gw, gh; render_gfx_page_box(GFXP_BOX_BACK, &gx, &gy, &gw, &gh);
    const float my = PAGE_Y + gy + gh / 2.0f, mx = PAGE_X + gx + gw / 2.0f;
    const float PX[3] = { mx, PAGE_X + gx - 9.0f, PAGE_X + gx + gw + 8.0f };   /* BACK's middle, its reach's left end, its right end */
    static const char *const PN[3] = { "middle (LINK)", "left end (UPDATES)", "right end (CLOSE)" };
    static const int PW[3] = { SET_TAP_LINK, SET_TAP_UPDATES, SET_TAP_CLOSE };
    for (int k = 0; k < 3; k++) { int v; if (render_settings_tap(PX[k], my, &v) != PW[k] || render_gfx_page_tap(PX[k], my, &v) != SET_TAP_GFX_PAGE) BF("(BACK's %s is not over that button on this board)", PN[k]); }
    #define ACTED(k) ((k) == 0 ? g_link_page : (k) == 1 ? touch_port_updates() : !touch_port_settings())
    #define FROM_UPDATES() do { bare(); touch_port_show_updates(true); hold_for(800); tap(mx, my, 90, FRAME_MS); \
                                if (!touch_port_settings() || touch_port_updates()) BF("(the UPDATES page's BACK did not bring settings back)"); \
                                if (getenv("PT_UPDBACK_OLD")) render_settings_from_updates_off(); } while (0)
    /* ---- B ---- */
    for (int k = 0; k < 3; k++) {
        FROM_UPDATES(); hold_for(400);                       /* (0.44 s after the lift) */
        tap(PX[k], my, 90, FRAME_MS);
        if (ACTED(k) || !touch_port_settings() || g_link_page || touch_port_updates()) BF("B: back from UPDATES, a tap 0.44 s on at BACK's %s acted (settings %d, updates %d, link %d)", PN[k], touch_port_settings(), touch_port_updates(), g_link_page);
        hold_for(80); tap(PX[k], my, 90, FRAME_MS);          /* 0.65 s after BACK: past the window (the spent tap did not restart it) */
        if (!ACTED(k)) BF("B: back from UPDATES, a tap 0.65 s on at BACK's %s did nothing (the window is %.1f s, never restarted)", PN[k], SET_UPD_BACK_S);
    }
    /* ---- C ---- */
    {   int bx, by, bw, bh, v, w; float x, y;
        FROM_UPDATES(); hold_for(260); T.autofeed_off = false;
        if (!find_hit(hit_settings, SET_TAP_FEED, &x, &y)) BF("C: (no AUTO FEED)");
        else { render_settings_box(SET_BOX_GFX, &bx, &by, &bw, &bh);   /* AUTO FEED's OFF: the second segment */
               x = PAGE_X + SET_SEG_X + SET_SEG_DX + SET_SEG_W / 2.0f; y = PAGE_Y + SET_ROW4_Y + 4.0f;
               tap(x, y, 90, FRAME_MS); w = touch_port_take_setting(&v);
               if (w != SET_TAP_FEED || v != 0) BF("C: AUTO FEED OFF 0.3 s after UPDATES' BACK -> %d/%d", w, v); }
        FROM_UPDATES(); hold_for(260); touch_port_take_setting(&v);
        tap(PAGE_X + SET_BRT_X(40), PAGE_Y + SET_ROW1_Y + 4.0f, 90, FRAME_MS); w = touch_port_take_setting(&v);
        if (w != SET_TAP_BRIGHT) BF("C: the BRIGHTNESS slider 0.3 s after UPDATES' BACK -> %d/%d", w, v);
        FROM_UPDATES(); hold_for(260);
        render_settings_box(SET_BOX_GFX, &bx, &by, &bw, &bh);
        tap(PAGE_X + bx + bw / 2.0f, PAGE_Y + by + bh / 2.0f, 90, FRAME_MS);
        if (!render_gfx_page()) BF("C: the GRAPHICS tile 0.3 s after UPDATES' BACK did not open its page");
        render_gfx_page_show(false);
        FROM_UPDATES(); hold_for(260);
        render_settings_box(SET_BOX_CLOSE, &bx, &by, &bw, &bh);
        x = PAGE_X + bx + bw - 6.0f; y = PAGE_Y + by + bh / 2.0f;
        if (render_gfx_page_tap(x, y, &v) == SET_TAP_GFX_PAGE) BF("C: (CLOSE's right side is inside BACK's footprint)");
        tap(x, y, 90, FRAME_MS);
        if (touch_port_settings()) BF("C: CLOSE, outside BACK's footprint, 0.3 s after UPDATES' BACK did not close settings");
    }
    /* ---- D ---- */
    bare(); touch_port_show_updates(true); updates_page_show_nets(true); hold_for(800);
    tap(mx, my, 90, 300);
    if (!touch_port_updates() || updates_page_nets()) BF("D: WI-FI's BACK: not the UPDATES face");
    tap(mx, my, 90, 300);
    if (!touch_port_settings() || touch_port_updates()) BF("D: the second tap, 0.3 s on: not the settings page (WI-FI's BACK then UPDATES' BACK must walk out)");
    tap(mx, my, 90, 300);
    if (!touch_port_settings() || touch_port_updates() || g_link_page) BF("D: three taps at BACK's middle from WI-FI: settings %d, updates %d, LINK MODE %d (want the settings page, no link mode)", touch_port_settings(), touch_port_updates(), g_link_page);
    /* ---- E: the GRAPHICS page, as on main ---- */
    FROM_UPDATES(); hold_for(800);                           /* (an UPDATES BACK earlier in the run: its guard is long over) */
    render_gfx_page_show(true); hold_for(800);
    tap(mx, my, 90, FRAME_MS);
    if (!touch_port_settings() || render_gfx_page() || g_link_page) BF("E: (the GRAPHICS page's BACK did not bring settings back)");
    hold_for(160); tap(mx, my, 90, FRAME_MS);                /* 0.2 s on: inside the page's own 0.4 s (SET_TURN_WAIT_S, main's) */
    if (g_link_page) BF("E: the GRAPHICS page's BACK: a tap 0.2 s on opened link mode (main spends it: the page just changed its face)");
    hold_for(400); tap(mx, my, 90, FRAME_MS);
    const bool g_link = g_link_page;
    if (!g_link) BF("E: the GRAPHICS page's BACK, a second tap past 0.4 s did NOT open link mode - the GRAPHICS page is to stay as it is on main (no guard there: Strato)");
    printf("back-overlap: KNOWN, as on main: after the GRAPHICS page's BACK a second tap at the same spot past 0.4 s opens link mode (no guard there, by Strato's word)\n");
    bare(); remove(sav);
    if (fails) { printf("selftest-back-overlap: FAIL (%d)\n", fails); return 1; }
    printf("selftest-back-overlap: PASS - back from UPDATES a press where BACK was is spent for %.1f s (middle, left end, right end), then acts; AUTO FEED, a slider, the GRAPHICS tile and "
           "CLOSE beside it act at once; WI-FI -> UPDATES -> settings by three taps, no link mode; the GRAPHICS page's BACK as on main\n", SET_UPD_BACK_S);
    return 0;
}
int selftest_touch_port(void) {
    g_log = getenv("PT_TOUCH_LOG") != NULL;
    char sav[96]; snprintf(sav, sizeof sav, "/tmp/pocket-tank-selftest-touchport-%s.sav", HB_ROUND ? "bowl" : HB_WATCH ? "watch" : HB_SQUARE ? "square" : "rect");
    setenv("POCKET_TANK_SAVE", sav, 1); remove(sav);   /* (one per world: four worlds' checks at once never share it) */
    tank_init(&T, 9191); progression_boot(&T); progression_setup_done(&T);
    T.trickle_off = true;
    if (!touch_port_init()) { printf("FAIL: touch-port: the port found no chip\n"); return 1; }
    touch_port_set_quiet(false);                             /* (LOADING is over) */
    printf("selftest-touch-port: the %s's port (%s), its calibration and bias on every tap\n",
           HB_ROUND ? "bowl" : HB_WATCH ? "watch" : HB_SQUARE ? "square" : "1.8", s_cst ? "CST92xx" : s_ft ? "FT3168" : "CST816 via esp_lcd");
    settings_nav();
    firm_press();
    slow_close();
    double_taps();
    link_holds();
    mashed_holds();
    firm_seconds();
    card_prepress();
    page_doubles();
    prompt_over_shelf();
    shelf_double();
    lock_card();
    give_dismiss();
    notice_under_give();
    roster_card();
    r2g_card_doubled();
    r2g_shelf_exits();
    r2g_place_sell();
    r2g_give_step();
    r2g_gift_back();
    r2g_stale_lift();
    r2g_slider_cut();
    r2g_chord_tap();
    r2g_slider_no_resave();
    r2g_give_ways();
    r2g_rule_rows();
    r2g_rule_burst();
    r2g_fast_keeper();
    r2g_rule_review();
    r2g_card_gone_more();
    r2g_give_steps_more();
    r2g_link_hold();
    more_pages_doubled();
    sdg_dial_port();
    crit_names_port();
    crit_shelf_port();
    calib_reach();
    calib_turns();
    calib_guided();
    wheel_feel_port();
    reset_pages_port();
    if (getenv("PT_TOUCH_AUDIT")) audit_probes();
    if (getenv("PT_TOUCH_AUDIT2")) r2_probes();
    if (fails) { printf("selftest-touch-port: %d FAILED\n", fails); return 1; }
    printf("selftest-touch-port: PASS\n");
    return 0;
}
