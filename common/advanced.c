/* advanced.c - THE ADVANCED PAGE (0.3.9.2): see advanced.h. */
#include "advanced.h"
#include "render.h"
#include "tank.h"
#include <math.h>
#include <string.h>

/* ---- the battery ---- */
static const int SIZES[] = { 100, 150, 200, 250, 300, 400, 500, 600, 800, 1000, 1100, 1200, 1500, 2000 };
#define N_SIZES ((int)(sizeof SIZES / sizeof SIZES[0]))
#if defined(TANK_SQUARE)                 /* the board's own end-of-charge / precharge current (battery_port_axp2101.c board_trickle_ma) */
#define ADV_TRICKLE_STOCK 100
#elif defined(TANK_ROUND) || defined(TANK_WATCH)
#define ADV_TRICKLE_STOCK 50
#else
#define ADV_TRICKLE_STOCK 25
#endif
int  adv_cell_sizes(const int **sizes) { *sizes = SIZES; return N_SIZES; }
bool adv_cell_ok(int mah) { for (int i = 0; i < N_SIZES; i++) if (SIZES[i] == mah) return true; return false; }
int adv_charge_ma(int mah) {
    if (mah == ADV_CELL_STOCK) return ADV_CHARGE_STOCK;
    int c = mah * 3 / 4;                                   /* ~0.75 C, down to the charger's next step */
    c = c <= 200 ? c / 25 * 25 : c < 300 ? 200 : c / 100 * 100;
    if (c < 25) c = 25;
    if (mah < ADV_CELL_STOCK && c > ADV_CHARGE_STOCK) c = ADV_CHARGE_STOCK;   /* a smaller cell never faster than the board's own (the square's own is 0.5 C) */
    return c > ADV_CHARGE_MAX ? ADV_CHARGE_MAX : c;
}
int adv_trickle_ma(int mah) {
    if (mah == ADV_CELL_STOCK) return ADV_TRICKLE_STOCK;
    int t = mah / 10 / 25 * 25;
    return t < 25 ? 25 : t > 200 ? 200 : t;
}
bool adv_cell_fast(int mah) { return adv_charge_ma(mah) > ADV_CHARGE_STOCK; }

/* ---- the touch map ---- */
bool touch_fit_is_none(const touch_fit_t *f) { return f->ax == 1 && f->bx == 0 && f->ay == 1 && f->by == 0; }
bool touch_fit_ok(const touch_fit_t *f) {
    if (!(f->ax >= 0.85f && f->ax <= 1.15f && f->ay >= 0.85f && f->ay <= 1.15f)) return false;   /* (NaN fails too) */
    for (int e = 0; e < 2; e++) {                          /* the move it makes at each edge of the glass: a fingertip or two */
        float x = e ? TANK_W - 1 : 0, y = e ? TANK_H - 1 : 0;
        if (fabsf(f->ax * x + f->bx - x) > 80 || fabsf(f->ay * y + f->by - y) > 80) return false;
    }
    return true;
}
void touch_fit_apply(const touch_fit_t *f, float *x, float *y, int q) {
    if (touch_fit_is_none(f)) return;
    float ux = *x, uy = *y;
    tank_quarter_to_upright(q, &ux, &uy);
    ux = f->ax * ux + f->bx; uy = f->ay * uy + f->by;
    if (ux < 0) ux = 0;
    if (ux > TANK_W - 1) ux = TANK_W - 1;
    if (uy < 0) uy = 0;
    if (uy > TANK_H - 1) uy = TANK_H - 1;
    tank_quarter_from_upright(q, &ux, &uy);
    *x = ux; *y = uy;
}
/* one axis: target = a press + b, least squares; the rms of what is left */
static bool fit_axis(const float *t, const float *p, int n, float *a, float *b, float *rms) {
    float mp = 0, mt = 0;
    for (int i = 0; i < n; i++) { mp += p[i]; mt += t[i]; }
    mp /= n; mt /= n;
    float spp = 0, spt = 0;
    for (int i = 0; i < n; i++) { spp += (p[i] - mp) * (p[i] - mp); spt += (p[i] - mp) * (t[i] - mt); }
    if (spp < 1) return false;                             /* every press in one place: no slope to find */
    *a = spt / spp; *b = mt - *a * mp;
    float r = 0;
    for (int i = 0; i < n; i++) { float e = *a * p[i] + *b - t[i]; r += e * e; }
    *rms = sqrtf(r / n);
    return true;
}
#define ADV_FIT_RMS_MAX 20.0f                              /* presses scattered wider than this are not a sitting: try again */
#define ADV_FIT_MAX 16
bool touch_fit_solve(const float *tx, const float *ty, const float *px, const float *py, int n, int q, touch_fit_t *out) {
    if (n < 3 || n > ADV_FIT_MAX) return false;
    float utx[ADV_FIT_MAX], uty[ADV_FIT_MAX], upx[ADV_FIT_MAX], upy[ADV_FIT_MAX];   /* fitted on a turned picture: the same map, upright */
    for (int i = 0; i < n; i++) {
        utx[i] = tx[i]; uty[i] = ty[i]; tank_quarter_to_upright(q, &utx[i], &uty[i]);
        upx[i] = px[i]; upy[i] = py[i]; tank_quarter_to_upright(q, &upx[i], &upy[i]);
    }
    touch_fit_t f; float rx, ry;
    if (!fit_axis(utx, upx, n, &f.ax, &f.bx, &rx) || !fit_axis(uty, upy, n, &f.ay, &f.by, &ry)) return false;
    if (rx > ADV_FIT_RMS_MAX || ry > ADV_FIT_RMS_MAX) return false;
    if (!touch_fit_ok(&f)) return false;
    *out = f;
    return true;
}

/* ---- the crosses: the same places update mode's calibration uses (update.c) ---- */
#ifdef TANK_ROUND                                          /* the bowl: a 3 x 3 the circle holds */
static const int CX[3] = { 110, TANK_W / 2, TANK_W - 110 }, CY[3] = { 110, TANK_H / 2, TANK_H - 110 };
#elif defined(TANK_WATCH)                                  /* the watch: in from its round corners */
static const int CX[3] = { 70, TANK_W / 2, TANK_W - 70 }, CY[3] = { 70, TANK_H / 2, TANK_H - 70 };
#else
static const int CX[3] = { 48, TANK_W / 2, TANK_W - 48 }, CY[3] = { 40, TANK_H / 2, TANK_H - 40 };
#endif
void adv_cross_target(int i, float *x, float *y) { *x = CX[i % 3]; *y = CY[(i / 3) % 3]; }
void adv_check_target(float *x, float *y) { *x = TANK_W * 3 / 10; *y = TANK_H * 2 / 3; }   /* none of the nine */

/* ---- the page ---- */
#define ADV_WARN_HOLD_S 2.0f                               /* the warning ignores taps this long: a quick second tap on SET does not land on it */
static int s_pg, s_cell = ADV_CELL_STOCK, s_pick = ADV_CELL_STOCK;
static bool s_ticked;
static touch_fit_t s_saved = { 1, 0, 1, 0 }, s_live = { 1, 0, 1, 0 }, s_new;
static int s_i; static int s_q0;
static float s_tx[ADV_N_CROSS], s_ty[ADV_N_CROSS], s_px[ADV_N_CROSS], s_py[ADV_N_CROSS];
static float s_t;                                          /* the page's clock: the warning's hold, a cross's wait, the check's time left */
static const char *s_msg;
static bool s_down, s_skip; static float s_dx, s_dy;      /* this press: where it landed; s_skip = the finger that opened the page */

void adv_init(int cell_mah, const touch_fit_t *fit) {
    s_cell = s_pick = adv_cell_ok(cell_mah) ? cell_mah : ADV_CELL_STOCK;
    s_saved = s_live = fit && touch_fit_ok(fit) ? *fit : TOUCH_FIT_NONE;
    s_pg = ADV_PG_NONE;
}
int  adv_cell(void) { return s_cell; }
int  adv_pick(void) { return s_pick; }
const touch_fit_t *adv_touch_fit(void) { return &s_live; }
static void go(int pg) { s_pg = pg; s_t = 0; s_down = false; }
void adv_open(void) { s_pick = s_cell; s_msg = NULL; s_skip = true; go(ADV_PG_MAIN); }
bool adv_up(void) { return s_pg != ADV_PG_NONE; }
int  adv_page(void) { return s_pg; }
void adv_close(void) { s_live = s_saved; s_pg = ADV_PG_NONE; }   /* a sitting cut short keeps the old map */
static void touch_page(const char *msg) { s_live = s_saved; s_msg = msg; go(ADV_PG_TOUCH); }
void adv_tick(float dt) {
    if (s_pg == ADV_PG_NONE) return;
    s_t += dt;
    if (s_pg == ADV_PG_CROSS && s_t > ADV_CROSS_IDLE_S) touch_page("STOPPED - NOTHING CHANGED");
    if (s_pg == ADV_PG_CHECK && s_t > ADV_CHECK_S) touch_page("KEPT THE OLD TOUCH");
}
static int step_pick(int dir) {
    for (int i = 0; i < N_SIZES; i++)
        if (SIZES[i] == s_pick) {
            int j = i + dir;
            if (j < 0 || j >= N_SIZES) return ADV_ACT_NONE;
            s_pick = SIZES[j]; return ADV_ACT_TICK;
        }
    s_pick = ADV_CELL_STOCK; return ADV_ACT_TICK;
}
int adv_touch(float x, float y, bool down, int q) {
    if (s_pg == ADV_PG_NONE) return ADV_ACT_NONE;
    if (s_skip) { if (!down) s_skip = false; return ADV_ACT_NONE; }   /* the chord's finger: its lift is not a tap */
    if (down && !s_down) { s_down = true; s_dx = x; s_dy = y; return ADV_ACT_NONE; }
    if (down || !s_down) return ADV_ACT_NONE;
    s_down = false;                                        /* a release: what it pressed, if it stayed there */
    bool still = (x - s_dx) * (x - s_dx) + (y - s_dy) * (y - s_dy) < 24 * 24;
    switch (s_pg) {
    case ADV_PG_MAIN: {
        if (!still) return ADV_ACT_NONE;
        int h = render_advanced_tap(s_dx, s_dy);
        if (h == ADV_HIT_BACK) { s_pg = ADV_PG_NONE; return ADV_ACT_BACK; }
        if (h == ADV_HIT_CALIBRATE) { touch_page(NULL); return ADV_ACT_TICK; }
        if (h == ADV_HIT_PREV) return step_pick(-1);
        if (h == ADV_HIT_NEXT) return step_pick(+1);
        if (h == ADV_HIT_SET && s_pick != s_cell) {
            if (adv_cell_fast(s_pick)) { s_ticked = false; go(ADV_PG_WARN); return ADV_ACT_TICK; }
            s_cell = s_pick; return ADV_ACT_CELL;          /* the same rate or slower: no warning */
        }
        return ADV_ACT_NONE; }
    case ADV_PG_WARN: {
        if (!still || s_t < ADV_WARN_HOLD_S) return ADV_ACT_NONE;
        int h = render_battery_warning_tap(s_dx, s_dy);
        if (h == ADV_HIT_BOX) { s_ticked = !s_ticked; return ADV_ACT_TICK; }
        if (h == ADV_HIT_CANCEL) { s_pick = s_cell; go(ADV_PG_MAIN); return ADV_ACT_TICK; }
        if (h == ADV_HIT_CONTINUE && s_ticked) { s_cell = s_pick; go(ADV_PG_MAIN); return ADV_ACT_CELL; }
        return ADV_ACT_NONE; }
    case ADV_PG_TOUCH: {
        if (!still) return ADV_ACT_NONE;
        bool own = !touch_fit_is_none(&s_saved);
        int h = render_touch_intro_tap(s_dx, s_dy, own);
        if (h == ADV_HIT_BACK) { s_msg = NULL; go(ADV_PG_MAIN); return ADV_ACT_TICK; }
        if (h == ADV_HIT_START) { s_live = TOUCH_FIT_NONE; s_i = 0; s_q0 = q; s_msg = NULL; go(ADV_PG_CROSS); return ADV_ACT_TICK; }
        if (h == ADV_HIT_RESET && own) { s_saved = s_live = TOUCH_FIT_NONE; s_msg = "TOUCH BACK TO NORMAL"; return ADV_ACT_FIT; }
        return ADV_ACT_NONE; }
    case ADV_PG_CROSS: {                                   /* any press counts: where it LANDED (the finger's aim, before its roll) */
        if (q != s_q0) { s_i = 0; s_q0 = q; s_t = 0; return ADV_ACT_NONE; }   /* the picture turned mid-sitting: start over */
        adv_cross_target(s_i, &s_tx[s_i], &s_ty[s_i]);
        s_px[s_i] = s_dx; s_py[s_i] = s_dy; s_i++; s_t = 0;
        if (s_i < ADV_N_CROSS) return ADV_ACT_TICK;
        if (!touch_fit_solve(s_tx, s_ty, s_px, s_py, ADV_N_CROSS, s_q0, &s_new)) { touch_page("DID NOT WORK - TRY AGAIN"); return ADV_ACT_TICK; }
        s_live = s_new; go(ADV_PG_CHECK);                  /* the next press goes through the new map */
        return ADV_ACT_TICK; }
    case ADV_PG_CHECK: {
        float cx, cy; adv_check_target(&cx, &cy);
        if ((s_dx - cx) * (s_dx - cx) + (s_dy - cy) * (s_dy - cy) > ADV_CHECK_HIT_PX * ADV_CHECK_HIT_PX) return ADV_ACT_NONE;   /* a miss: the clock runs on */
        s_saved = s_new; touch_page("NEW TOUCH SAVED");
        return ADV_ACT_FIT; }
    }
    return ADV_ACT_NONE;
}
void adv_render(uint16_t *fb, int stride) {
    switch (s_pg) {
    case ADV_PG_MAIN: render_advanced(fb, stride, s_pick, adv_charge_ma(s_pick), s_cell, s_pick == ADV_CELL_STOCK, adv_cell_fast(s_pick)); break;
    case ADV_PG_WARN: render_battery_warning(fb, stride, s_pick, adv_charge_ma(s_pick), s_ticked); break;
    case ADV_PG_TOUCH: render_touch_intro(fb, stride, !touch_fit_is_none(&s_saved), s_msg); break;
    case ADV_PG_CROSS: { float x, y; adv_cross_target(s_i, &x, &y); render_touch_cross(fb, stride, s_i, ADV_N_CROSS, (int)x, (int)y); break; }
    case ADV_PG_CHECK: { float x, y; adv_check_target(&x, &y); render_touch_check(fb, stride, (int)x, (int)y, ADV_CHECK_S - s_t, ADV_CHECK_S); break; }
    default: break;
    }
}
