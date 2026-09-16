/* touch_port_gt911.c — GT911 capacitive touch on the Waveshare
 * ESP32-P4-WIFI6-Touch-LCD-4B (720x720) -> tank_touch_hold / tank_touch_tap,
 * with the same gesture timing as the sim's mouse: press+release < 350 ms
 * with < 24 px displacement = tap (fingertips roll and this panel is 322 ppi);
 * held > 300 ms = hold; a drag down from the top edge = feed at that x;
 * every touched frame streams to tank_touch_drag (a moving stroke wipes
 * algae; a horizontal slash through a canopy trims it). Fish taps hit-test
 * 38 px against the press-time fish snapshot AND the current position - fish
 * move during a tap. While the stats card is up, a tap anywhere on empty
 * glass dismisses it (hunting the same fish again to close it was the old,
 * cumbersome way) and does nothing else. Coordinates: the GT911 reports
 * 720x720 screen coords and the tank scene is the FULL 720x720 square fit
 * (the glass side, SCALED_W/SCALED_H/GLASS_Y_OFF, lives in display_port.h;
 * the tank-side division is local) - no portrait rotation (the panel is square
 * and the scene is already landscape). */
#include "touch_port.h"
#include "display_port.h"
#include "tank.h"
#include "render.h"
#include "setup.h"
#include "notice.h"
#include "audio_port.h"
#include "progression.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
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
static int  s_shop_act;                           /* an UNLOCK / MOVE tapped: the raw tap code, for main (one-shot) */
static int  s_set_what, s_set_val;                /* a segment tapped: SET_TAP_* + value, for main */
#define CONFIRM_TIMEOUT_US (20LL * 1000000)
static bool s_inverted;                           /* stored only: the 4B has no IMU (it stays false) */
/* Fingers land a little BELOW where the eye aims - the pad rolls onto the
 * glass under the fingertip (phones shift their hit targets down for the
 * same reason; Strato saw it on the swatch rows, 2026-09-13). Reported
 * points move UP by this many px in displayed space; director `touch bias
 * <px>` tunes it live. */
static int s_bias_y = 10;
void touch_port_set_bias(int px) { s_bias_y = px; }
int  touch_port_bias(void) { return s_bias_y; }

void touch_port_set_inverted(bool inverted) { s_inverted = inverted; }

/* 720x720 screen coord -> tank coord through the full-width scaled fit
 * (glass side: SCALED_W/SCALED_H/GLASS_Y_OFF from display_port.h), clamped
 * to the tank */
static int screen_x(uint16_t sx) {
    int tx = (int)sx * TANK_W / SCALED_W;   /* GLASS_X_OFF is 0 (full width) */
    if (tx > TANK_W - 1) tx = TANK_W - 1;
    return tx;
}
static int screen_y(uint16_t sy) {
    int ty = ((int)sy - GLASS_Y_OFF) * TANK_H / SCALED_H;
    if (ty < 0) ty = 0;
    if (ty > TANK_H - 1) ty = TANK_H - 1;
    return ty;
}

bool touch_port_init(void) {
    if (bsp_i2c_init() != ESP_OK) { ESP_LOGW(TAG, "no BSP I2C bus"); return false; }
    /* NULL = the BSP's default touch config (no swap / mirror) */
    if (bsp_touch_new(NULL, &s_tp) != ESP_OK) { ESP_LOGW(TAG, "no GT911"); return false; }
    ESP_LOGI(TAG, "GT911 ready (720x720 glass -> tank %dx%d full-width scale)", TANK_W, TANK_H);
    return true;
}

/* call every frame from the tank task */
void touch_port_poll(tank_t *t) {
    int64_t now = esp_timer_get_time();
    if (s_cf && now - s_cf_us > CONFIRM_TIMEOUT_US) touch_port_confirm_answer(-1);   /* nobody answered: keep the tank */
    if (!s_tp) return;
    uint16_t x[1], y[1], st[1]; uint8_t n = 0;
    esp_lcd_touch_read_data(s_tp);
    bool touched = esp_lcd_touch_get_coordinates(s_tp, x, y, st, &n, 1) && n > 0;
    /* screen (sx,sy) -> tank (tx,ty) through the full-width scaled fit; the
     * finger-landing bias still shifts ty up (clamped at the glass) */
    float tx = touched ? (float)screen_x(x[0]) : s_lx;
    float ty = touched ? (float)screen_y(y[0]) - s_bias_y : s_ly;
    if (touched && ty < 0) ty = 0;
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
                                                  : r == SET_TAP_LIGHT ? "lights out" : "idle seconds", v);
        if (r == SET_TAP_CLOSE) { s_set = false; s_ms = true; s_back = true; }   /* back to the milestones page (2026-09-16); the release is spent */
        else if (r == SET_TAP_BRIGHT || r == SET_TAP_VOLUME || r == SET_TAP_LIGHT || r == SET_TAP_IDLE) { s_set_what = r; s_set_val = v; }
    }
    if (su && !s_cf) {
        bool birth = setup_is_birth(); int who = setup_fish(), place = setup_item();
        setup_touch(t, tx, ty, touched);                     /* taps and the letter wheel, classified in setup.c */
        if (!setup_active()) {
            if (birth) ESP_LOGI(TAG, "birth flow done: %s named and saved", who >= 0 && who < t->n_fish ? t->fish[who].name : "?");
            else if (place >= 0) ESP_LOGI(TAG, "placed: %s at x %.0f, %s layer, saved", SD_ITEMS[place].name, tank_decor_x(t, place),
                                          tank_decor_z(t, place) == DECOR_Z_BACK ? "BEHIND" : tank_decor_z(t, place) == DECOR_Z_FRONT ? "IN FRONT" : "AMONG");
            else ESP_LOGI(TAG, "setup done: %s + %s", t->fish[0].name, t->fish[1].name);
        }
    }
    bool modal = s_ms || s_set || s_shop || s_cf || su;               /* a page or a prompt owns the glass */
    if (touched) { s_lx = tx; s_ly = ty; if (!modal) tank_touch_drag(t, tx, ty); }  /* stroke = wipe/slash */
    if (touched && !modal && now - s_press_us > 300000 && fabsf(ty - s_py) < 30) tank_touch_hold(t, tx, ty);
    if (!touched && s_down) {
        /* release: classify with the LAST touched position (the old code fell
           back to the PRESS position here, so dx/dy were always 0 - every
           quick swipe read as a tap and the drag-feed could never fire) */
        float dx = s_lx - s_px, dy = s_ly - s_py;
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
        if (now - s_press_us < 350000 && dx * dx + dy * dy < 24 * 24) {
            if (notice_current()) { notice_dismiss(); ESP_LOGI(TAG, "tap closed the announcement"); goto released; }
            if (s_set || s_back) { s_back = false; goto released; }   /* the settings page had the glass (render_settings_touch above) */
            if (s_shop) {                                           /* the shop: a row's modal, UNLOCK, HOW TO EARN, CLOSE */
                int r = render_shop_tap(t, s_px, s_py);
                ESP_LOGI(TAG, "shop tap at %.0f,%.0f -> %s", s_px, s_py, r == SHOP_TAP_CLOSE ? "CLOSE" : r >= SHOP_TAP_MOVE ? "MOVE" : r >= SHOP_TAP_BUY ? "UNLOCK" : r == SHOP_TAP_KEPT ? "modal" : "nothing");
                if (r == SHOP_TAP_CLOSE) { s_shop = false; render_shop_leave(); s_ms = true; }   /* back to the milestones page (2026-09-16) */
                else if (r >= SHOP_TAP_BUY) s_shop_act = r;   /* main.c buys (and plays the cue) or opens the placement page */
                goto released;
            }
            if (s_ms) {                                             /* the page: badges open a modal, the CLOSE
                                                                       button ends it, the brightness row cycles */
                int r = render_milestones_tap(t, s_px, s_py);     /* CLOSE / SETTINGS / the sand dollar, detail modal, nothing */
                ESP_LOGI(TAG, "page tap at %.0f,%.0f (release %.0f,%.0f) -> %s", s_px, s_py, s_lx, s_ly,
                         r == MS_TAP_CLOSE ? "CLOSE" : r == MS_TAP_SETTINGS ? "SETTINGS" : r == MS_TAP_SHOP ? "SHOP" : r == MS_TAP_KEPT ? "detail" : "nothing");
                if (r != MS_TAP_CLOSE && r != MS_TAP_SETTINGS && r != MS_TAP_SHOP) goto released;   /* only a button leaves the page */
                s_ms = false; s_sel = -1; s_set = r == MS_TAP_SETTINGS; s_shop = r == MS_TAP_SHOP;
                progression_ack_milestones(t); render_milestones_leave();   /* everything shown is now "seen" */
                goto released;
            }
            if (s_sel >= 0 && s_sel != RENDER_CARD_SNAIL && s_px >= RENDER_CARD_X && s_px < RENDER_CARD_X + RENDER_CARD_W &&
                s_py >= RENDER_CARD_Y && s_py < RENDER_CARD_Y + RENDER_CARD_H) {
                s_ms = true; goto released;                          /* a tap ON the card = milestones page */
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
            if (best >= 0) { s_sel = (best == s_sel) ? -1 : best; s_sel_us = now; }
            else if (tank_snail_hit(t, s_px, s_py)) {   /* the snail: its card (2026-09-16), the fish first */
                s_sel = s_sel == RENDER_CARD_SNAIL ? -1 : RENDER_CARD_SNAIL; s_sel_us = now;
                ESP_LOGI(TAG, "snail tapped: card %s (%d spots grazed)", s_sel >= 0 ? "up" : "down", (int)t->snail_grazed); }
            else if (s_sel >= 0) s_sel = -1;   /* card up: a tap on empty glass just
                                                  dismisses it - it is NOT a tank tap
                                                  (no feed, no light-toggle burst) */
            else tank_touch_tap(t, s_px, s_py);
        }
        else if (!s_ms && s_py < 60 && dy >= 40) tank_feed(t, s_lx, 3);  /* drag down from the top = feed */
    }
released:
    s_down = touched;
    if (s_sel >= t->n_fish && s_sel != RENDER_CARD_SNAIL) s_sel = -1;   /* fresh tank / save load */
    if (s_sel >= 0 && now - s_sel_us > 10 * 1000000) s_sel = -1; /* auto-dismiss */
}

int touch_port_selected(void) { return s_sel; }
bool touch_port_milestones(void) { return s_ms; }
void touch_port_show_milestones(bool on) { if (s_ms && !on) render_milestones_leave(); s_ms = on; }
void touch_port_dismiss(void) { s_sel = -1; if (s_ms) render_milestones_leave(); if (s_shop) render_shop_leave(); s_ms = false; s_set = false; s_shop = false; }

/* ---- reset confirm prompt ---- */
void touch_port_confirm_open(void) {
    s_cf = true; s_cf_us = esp_timer_get_time(); s_cf_ans = 0;
    s_sel = -1; s_ms = false; s_set = false; s_shop = false; render_shop_leave();   /* it replaces the card / the pages */
    ESP_LOGI(TAG, "reset prompt up (YES / NO on the glass; NO by itself in %d s)", (int)(CONFIRM_TIMEOUT_US / 1000000));
}
bool touch_port_confirm_answer(int ans) {
    if (!s_cf) return false;
    s_cf = false; s_cf_ans = ans > 0 ? 1 : -1;
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
void touch_port_show_settings(bool on) { s_set = on; if (on) { s_ms = false; s_sel = -1; } }
int  touch_port_take_setting(int *value) { int w = s_set_what; *value = s_set_val; s_set_what = 0; return w; }
bool touch_port_shop(void) { return s_shop; }
void touch_port_show_shop(bool on) { if (s_shop && !on) render_shop_leave(); s_shop = on; if (on) { s_ms = false; s_set = false; s_sel = -1; } }
int  touch_port_take_shop(void) { int r = s_shop_act; s_shop_act = 0; return r; }
