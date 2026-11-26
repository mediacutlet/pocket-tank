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
    if (!isfinite(f->ax) || !isfinite(f->bx) || !isfinite(f->ay) || !isfinite(f->by)) return false;   /* (a NaN offset passed the
                                                              edge test below - fabsf(NaN) > 80 is false - and every tap went NaN) */
    if (!(f->ax >= 0.85f && f->ax <= 1.15f && f->ay >= 0.85f && f->ay <= 1.15f)) return false;
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
/* one axis: target = a press + b, least squares over the presses `use` keeps; the rms of what is left */
static bool fit_axis(const float *t, const float *p, const bool *use, int n, float *a, float *b, float *rms) {
    float mp = 0, mt = 0; int m = 0;
    for (int i = 0; i < n; i++) if (use[i]) { mp += p[i]; mt += t[i]; m++; }
    if (m < 3) return false;                               /* (a whole row left out still leaves six) */
    mp /= m; mt /= m;
    float spp = 0, spt = 0;
    for (int i = 0; i < n; i++) if (use[i]) { spp += (p[i] - mp) * (p[i] - mp); spt += (p[i] - mp) * (t[i] - mt); }
    if (spp < 1) return false;                             /* every press in one place: no slope to find */
    *a = spt / spp; *b = mt - *a * mp;
    float r = 0;
    for (int i = 0; i < n; i++) if (use[i]) { float e = *a * p[i] + *b - t[i]; r += e * e; }
    *rms = sqrtf(r / m);
    return true;
}
#define ADV_FIT_RMS_MAX 20.0f                              /* presses scattered wider than this are not a sitting: try again */
#define ADV_FIT_MAX 16
bool touch_fit_solve_masked(const float *tx, const float *ty, const float *px, const float *py, const uint8_t *drop, int n, int q, touch_fit_t *out) {
    if (n < 3 || n > ADV_FIT_MAX) return false;
    float utx[ADV_FIT_MAX], uty[ADV_FIT_MAX], upx[ADV_FIT_MAX], upy[ADV_FIT_MAX];   /* fitted on a turned picture: the same map, upright */
    bool ux[ADV_FIT_MAX], uy[ADV_FIT_MAX];
    const bool side = TANK_QUARTER(q) & 1;                 /* on a side the picture's x is the glass's y */
    for (int i = 0; i < n; i++) {
        utx[i] = tx[i]; uty[i] = ty[i]; tank_quarter_to_upright(q, &utx[i], &uty[i]);
        upx[i] = px[i]; upy[i] = py[i]; tank_quarter_to_upright(q, &upx[i], &upy[i]);
        int d = drop ? drop[i] : 0;
        ux[i] = !(d & (side ? 2 : 1)); uy[i] = !(d & (side ? 1 : 2));
    }
    touch_fit_t f; float rx, ry;
    if (!fit_axis(utx, upx, ux, n, &f.ax, &f.bx, &rx) || !fit_axis(uty, upy, uy, n, &f.ay, &f.by, &ry)) return false;
    if (rx > ADV_FIT_RMS_MAX || ry > ADV_FIT_RMS_MAX) return false;
    if (!touch_fit_ok(&f)) return false;
    *out = f;
    return true;
}
bool touch_fit_solve(const float *tx, const float *ty, const float *px, const float *py, int n, int q, touch_fit_t *out) {
    return touch_fit_solve_masked(tx, ty, px, py, NULL, n, q, out);
}

/* ---- the keeper's calibration: its sittings (advanced.h) ---- */
/* the viewer's right and down on the upright glass, the picture turned q (touch_port_ft3168.c cal_point's) */
static const int8_t Q_RIGHT[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } }, Q_DOWN[4][2] = { { 0, 1 }, { -1, 0 }, { 0, -1 }, { 1, 0 } };
#define TOUCH_CAL_LAND_MAX 40.0f                           /* a finger's landing the two sittings may split out: past it, two grips */
/* ... and how far apart their GAINS may be. A gain is the panel's alone, so two sittings of one grip on one glass agree on it
   to within their scatter: Strato's sittings differed by 0.011 / 0.018 (the watch, two sittings), 0.024 / 0.004 (the square,
   thumb vs index, 10-12), +-0.013 (the bowl's x, inside one sitting); only two GRIPS went past it (the square B, thumb vs
   index: 0.046 - the grip moves where a finger lands by most of a millimetre, docs/touch). Past this the two are not one
   panel's map: the mean would carry the worse one's stretch into the good one's way up (the 11-07 review: a sloppy 1.12
   sitting turned over, then a true 1.00 one upright, left the upright map at 1.06 - 27 px at the far corner of the 1.8, and
   the check cross, nearer the middle, saw only 17). At 0.04 the mean is at most 0.02 off either: 9 px at the 1.8's far
   corner, 10 on the square. */
#define TOUCH_CAL_GAIN_SPLIT 0.04f
static bool cal_q_ok(int q) { return q >= 0 && q < 4 && TANK_QUARTER(q) == q; }
/* two sittings at different quarters -> the panel's map (gains, offset) and the finger's landing (right, down) */
static bool cal_split(const touch_cal_t *c, touch_fit_t *panel, float *lr, float *ld) {
    if (c->n != 2 || !cal_q_ok(c->q[0]) || !cal_q_ok(c->q[1]) || c->q[0] == c->q[1]) return false;
    if (!(fabsf(c->s[0].ax - c->s[1].ax) <= TOUCH_CAL_GAIN_SPLIT && fabsf(c->s[0].ay - c->s[1].ay) <= TOUCH_CAL_GAIN_SPLIT)) return false;   /* (NaN too) */
    const int a = c->q[0], b = c->q[1];
    /* sitting i's offset = the panel's - R(q_i) L: so B_a - B_b = (R(q_b) - R(q_a)) L */
    float m00 = Q_RIGHT[b][0] - Q_RIGHT[a][0], m01 = Q_DOWN[b][0] - Q_DOWN[a][0];
    float m10 = Q_RIGHT[b][1] - Q_RIGHT[a][1], m11 = Q_DOWN[b][1] - Q_DOWN[a][1];
    float det = m00 * m11 - m01 * m10;
    if (fabsf(det) < 0.5f) return false;
    float dx = c->s[0].bx - c->s[1].bx, dy = c->s[0].by - c->s[1].by;
    *lr = (m11 * dx - m01 * dy) / det; *ld = (m00 * dy - m10 * dx) / det;
    if (!(fabsf(*lr) <= TOUCH_CAL_LAND_MAX && fabsf(*ld) <= TOUCH_CAL_LAND_MAX)) return false;   /* (NaN fails too) */
    panel->ax = (c->s[0].ax + c->s[1].ax) / 2; panel->ay = (c->s[0].ay + c->s[1].ay) / 2;
    panel->bx = c->s[0].bx + *lr * Q_RIGHT[a][0] + *ld * Q_DOWN[a][0];
    panel->by = c->s[0].by + *lr * Q_RIGHT[a][1] + *ld * Q_DOWN[a][1];
    return true;
}
touch_fit_t touch_cal_at(const touch_cal_t *c, int q) {
    q = TANK_QUARTER(q);
    if (!c || c->n == 0) return TOUCH_FIT_NONE;
    touch_fit_t p; float lr, ld;
    if (cal_split(c, &p, &lr, &ld)) {
        p.bx -= lr * Q_RIGHT[q][0] + ld * Q_DOWN[q][0];
        p.by -= lr * Q_RIGHT[q][1] + ld * Q_DOWN[q][1];
        return p;
    }
    return c->n == 2 ? c->s[1] : c->s[0];                  /* one sitting (or two that would not split: the newer) */
}
static bool cal_ok(const touch_cal_t *c) {                 /* every way up the picture can be, a map a panel could need */
    for (int q = 0; q < 4; q++) if (cal_q_ok(q)) { touch_fit_t f = touch_cal_at(c, q); if (!touch_fit_is_none(&f) && !touch_fit_ok(&f)) return false; }
    if (c->n == 2) { touch_fit_t p; float lr, ld; if (!cal_split(c, &p, &lr, &ld)) return false; }
    return true;
}
void touch_cal_merge(const touch_cal_t *old, const touch_fit_t *f, int q, touch_cal_t *out) {
    q = TANK_QUARTER(q);
    touch_cal_t c = TOUCH_CAL_NONE, one = TOUCH_CAL_NONE;
    one.n = 1; one.q[0] = (int8_t)q; one.s[0] = *f;
    if (old && old->n >= 1 && old->q[0] >= 0) {            /* (a 0.4.1 map, quarter unknown: replaced) */
        int keep = -1;                                     /* the old sitting kept beside the new: the newest one taken another way up */
        for (int i = old->n - 1; i >= 0 && keep < 0; i--) if (old->q[i] >= 0 && old->q[i] != q) keep = i;
        if (keep >= 0) { c.n = 2; c.q[0] = old->q[keep]; c.s[0] = old->s[keep]; c.q[1] = (int8_t)q; c.s[1] = *f; }
    }
    *out = c.n == 2 && cal_ok(&c) ? c : one;               /* two that do not make one map (two grips, a wild sitting): the new one alone */
}
bool touch_cal_load(const void *blob, size_t len, touch_cal_t *out) {
    *out = TOUCH_CAL_NONE;
    if (!blob) return false;
    if (len == sizeof(touch_fit_t)) {                      /* 0.4.1's: one map, the quarter it was taken at not kept */
        touch_fit_t f; memcpy(&f, blob, sizeof f);
        if (touch_fit_is_none(&f) || !touch_fit_ok(&f)) return false;
        out->n = 1; out->q[0] = -1; out->s[0] = f;
        return true;
    }
    if (len != sizeof(touch_cal_t)) return false;
    touch_cal_t c; memcpy(&c, blob, sizeof c);
    if (c.ver != TOUCH_CAL_VER || c.n < 1 || c.n > 2) return false;
    for (int i = 0; i < c.n; i++) if (!touch_fit_ok(&c.s[i]) || c.q[i] < -1 || c.q[i] > 3 || (c.q[i] >= 0 && !cal_q_ok(c.q[i]))) return false;
    if (c.n == 2 && (c.q[0] < 0 || c.q[1] < 0 || !cal_ok(&c))) return false;
    if (c.n == 1) { c.q[1] = -1; c.s[1] = TOUCH_FIT_NONE; }
    *out = c;
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
#define ADV_WARN_HOLD_S 0.2f   /* the double of SET only: a press begun this soon after the warning opened is no tap (THE TOUCH RULE, touch_gate.h; 2 s at the lift before 11-06) */                               /* the warning ignores taps this long: a quick second tap on SET does not land on it */
static int s_pg, s_cell = ADV_CELL_STOCK, s_pick = ADV_CELL_STOCK;
static bool s_ticked;
static touch_cal_t s_saved = TOUCH_CAL_NONE_INIT, s_live = TOUCH_CAL_NONE_INIT, s_new;
static touch_fit_t s_eff[4] = { { 1, 0, 1, 0 }, { 1, 0, 1, 0 }, { 1, 0, 1, 0 }, { 1, 0, 1, 0 } };   /* s_live, every way up */
static void set_live(const touch_cal_t *c) { s_live = *c; for (int q = 0; q < 4; q++) s_eff[q] = touch_cal_at(&s_live, q); }
static float s_reach[4] = { -1e9f, 1e9f, -1e9f, 1e9f };   /* adv_set_reach: x0, x1, y0, y1 on the picture as shown */
void adv_set_reach(float x0, float x1, float y0, float y1) { s_reach[0] = x0; s_reach[1] = x1; s_reach[2] = y0; s_reach[3] = y1; }
static int s_i; static int s_q0;
static bool s_two;                                         /* the crosses / check up are the TURNED sitting's (the first is saved) */
static bool s_done_turned;                                 /* DONE came from the turned sitting: it stays the way the tank is held */
static touch_fit_t s_first;                               /* the first sitting as saved: the turned one must join IT */
static int s_q1 = 0, s_imu = -1, s_imu1 = -1;              /* the first sitting's way up; the IMU now (adv_quarter; -1 none) and at it */
static float s_tx[ADV_N_CROSS], s_ty[ADV_N_CROSS], s_px[ADV_N_CROSS], s_py[ADV_N_CROSS];
static uint8_t s_drop[ADV_N_CROSS];                       /* a cross's press read at the panel's reach: bit 0 its x, bit 1 its y */
#define ADV_REACH_PX 2.0f
static float s_t;
static float s_press_t;                                    /* s_t at the press: the WARNING's window is read there (THE TOUCH RULE) */                                          /* the page's clock: the warning's hold, a cross's wait, the check's time left */
static const char *s_msg;
static bool s_down, s_skip; static float s_dx, s_dy;      /* this press: where it landed; s_skip = the finger that opened the page */

void adv_init_cal(int cell_mah, const touch_cal_t *cal) {
    s_cell = s_pick = adv_cell_ok(cell_mah) ? cell_mah : ADV_CELL_STOCK;
    touch_cal_t c = TOUCH_CAL_NONE;
    if (cal && !touch_cal_load(cal, sizeof *cal, &c)) c = TOUCH_CAL_NONE;   /* (the same checks as a blob off the flash) */
    s_saved = c; set_live(&c);
    s_pg = ADV_PG_NONE;
}
void adv_init(int cell_mah, const touch_fit_t *fit) {
    touch_cal_t c = TOUCH_CAL_NONE;
    if (fit) touch_cal_load(fit, sizeof *fit, &c);
    adv_init_cal(cell_mah, &c);
}
int  adv_cell(void) { return s_cell; }
int  adv_pick(void) { return s_pick; }
const touch_fit_t *adv_touch_fit_at(int q) { return &s_eff[TANK_QUARTER(q)]; }
const touch_fit_t *adv_touch_fit(void) { return &s_eff[0]; }
const touch_cal_t *adv_touch_cal(void) { return &s_saved; }
const char *adv_touch_msg(void) { return s_msg; }
static void go(int pg) { s_pg = pg; s_t = 0; s_down = false; }
void adv_open(void) { s_pick = s_cell; s_msg = NULL; s_skip = true; s_two = false; s_done_turned = false; go(ADV_PG_MAIN); }
bool adv_up(void) { return s_pg != ADV_PG_NONE; }
int  adv_page(void) { return s_pg; }
void adv_close(void) { set_live(&s_saved); s_two = false; s_done_turned = false; s_pg = ADV_PG_NONE; }   /* a sitting cut short keeps the old map */
static void touch_page(const char *msg) { set_live(&s_saved); s_two = false; s_msg = msg; go(ADV_PG_TOUCH); }
static const char *saved_msg(void) { return s_saved.n == 2 ? ADV_MSG_SAVED_BOTH : ADV_MSG_SAVED_ONE; }
static const char *fail_msg(void) { return s_saved.n == 2 ? ADV_MSG_SAVED_BOTH : ADV_MSG_TURN_FAIL; }   /* (the first joined one from before) */
static touch_fit_t f_of(const touch_cal_t *c, int q) { for (int i = c->n - 1; i >= 0; i--) if (c->q[i] == q) return c->s[i]; return TOUCH_FIT_NONE; }
/* DONE (Strato, 11-09: "all it's missing is feedback to let the user know they are done and the new calibration is saved"):
   the flow's end with something saved - the turned sitting kept, SKIP, or TURN IT OVER left alone - a page of its own (a
   check mark, DONE, what was saved, OK), not a line on the TOUCH page; OK goes back to the ADVANCED page */
static void done_page(bool turned) { bool held = s_down; set_live(&s_saved); s_two = false; s_done_turned = turned; s_msg = saved_msg(); go(ADV_PG_DONE); s_skip = held; }
/* THE TURN (advanced.h): the first sitting saved, the turned one asked for */
/* (s_skip: a finger on the glass as the page changes under it - a thumb holding the tank through the turn - is no press; it
   lifts first) */
static void turn_page(void) { bool held = s_down; set_live(&s_saved); s_two = false; s_msg = NULL; go(ADV_PG_TURN); s_skip = held; }
static void start_two(int q) { bool held = s_down; { touch_cal_t none = TOUCH_CAL_NONE; set_live(&none); } s_i = 0; s_q0 = TANK_QUARTER(q); s_two = true; go(ADV_PG_CROSS); s_skip = held; }
static int turn_target(void) { return TANK_QUARTER(s_q1 + 2); }   /* upside down from the first sitting */
static bool turned_part(void) { return s_pg == ADV_PG_TURN || (s_two && (s_pg == ADV_PG_CROSS || s_pg == ADV_PG_CHECK)) || (s_pg == ADV_PG_DONE && s_done_turned); }
bool adv_turned_sitting(void) { return s_two && (s_pg == ADV_PG_CROSS || s_pg == ADV_PG_CHECK); }
int adv_quarter(int shown, int live) {
    s_imu = live < 0 ? -1 : TANK_QUARTER(live);
    if (!turned_part()) return shown;
    if (s_imu < 0) return turn_target();                  /* the watch: the flow turns the picture itself (GO starts the crosses) */
    if (s_pg == ADV_PG_TURN) { if (s_imu == turn_target() && s_imu1 == s_q1) start_two(s_imu); }   /* (only when the first sitting's
                                                              picture stood as the tank did: ROTATION locked another way, the IMU's
                                                              "over" is no turn of the picture - TURN IT OVER waits for SKIP) */
    else if (s_pg != ADV_PG_DONE && s_imu != s_q0) turn_page();   /* turned back (or on to a side) mid-sitting: ask again, the first kept */
    return s_imu;
}
void adv_tick(float dt) {
    if (s_pg == ADV_PG_NONE) return;
    s_t += dt;
    if (s_pg == ADV_PG_CROSS && s_t > ADV_CROSS_IDLE_S) touch_page(s_two ? fail_msg() : "STOPPED - NOTHING CHANGED");
    if (s_pg == ADV_PG_CHECK && s_t > ADV_CHECK_S) touch_page(s_two ? fail_msg() : "KEPT THE OLD TOUCH");
    if (s_pg == ADV_PG_TURN && s_t > ADV_TURN_IDLE_S) done_page(false);
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
    if (down && !s_down) { s_down = true; s_dx = x; s_dy = y; s_press_t = s_t; return ADV_ACT_NONE; }
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
        if (!still || s_press_t < ADV_WARN_HOLD_S) return ADV_ACT_NONE;   /* (pressed inside SET's double window: spent) */
        int h = render_battery_warning_tap(s_dx, s_dy);
        if (h == ADV_HIT_BOX) { s_ticked = !s_ticked; return ADV_ACT_TICK; }
        if (h == ADV_HIT_CANCEL) { s_pick = s_cell; go(ADV_PG_MAIN); return ADV_ACT_TICK; }
        if (h == ADV_HIT_CONTINUE && s_ticked) { s_cell = s_pick; go(ADV_PG_MAIN); return ADV_ACT_CELL; }
        return ADV_ACT_NONE; }
    case ADV_PG_TOUCH: {
        if (!still) return ADV_ACT_NONE;
        bool own = s_saved.n > 0;
        int h = render_touch_intro_tap(s_dx, s_dy, own);
        if (h == ADV_HIT_BACK) { s_msg = NULL; go(ADV_PG_MAIN); return ADV_ACT_TICK; }
        if (h == ADV_HIT_START) { { touch_cal_t none = TOUCH_CAL_NONE; set_live(&none); } s_i = 0; s_q0 = q; s_imu1 = s_imu; s_two = false; s_msg = NULL; go(ADV_PG_CROSS); return ADV_ACT_TICK; }
        if (h == ADV_HIT_RESET && own) { s_saved = TOUCH_CAL_NONE; set_live(&s_saved); s_msg = "TOUCH BACK TO NORMAL"; return ADV_ACT_FIT; }
        return ADV_ACT_NONE; }
    case ADV_PG_CROSS: {                                   /* any press counts: where it LANDED (the finger's aim, before its roll) */
        if (q != s_q0) {                                   /* the picture turned mid-sitting: start over (the turned one: ask again) */
            if (s_two) { turn_page(); return ADV_ACT_NONE; }
            s_i = 0; s_q0 = q; s_imu1 = s_imu; s_t = 0; return ADV_ACT_NONE; }
        adv_cross_target(s_i, &s_tx[s_i], &s_ty[s_i]);
        s_px[s_i] = s_dx; s_py[s_i] = s_dy;
        s_drop[s_i] = (uint8_t)((s_dx <= s_reach[0] + ADV_REACH_PX || s_dx >= s_reach[1] - ADV_REACH_PX ? 1 : 0)
                              | (s_dy <= s_reach[2] + ADV_REACH_PX || s_dy >= s_reach[3] - ADV_REACH_PX ? 2 : 0));
        s_i++; s_t = 0;
        if (s_i < ADV_N_CROSS) return ADV_ACT_TICK;
        touch_fit_t f;
        if (!touch_fit_solve_masked(s_tx, s_ty, s_px, s_py, s_drop, ADV_N_CROSS, s_q0, &f)) { touch_page(s_two ? fail_msg() : "DID NOT WORK - TRY AGAIN"); return ADV_ACT_TICK; }
        touch_cal_merge(&s_saved, &f, s_q0, &s_new);       /* kept with a sitting taken another way up (advanced.h) */
        if (s_two && (s_new.n != 2 || s_new.q[0] != s_q1 || memcmp(&s_new.s[0], &s_first, sizeof s_first)))
            { touch_page(fail_msg()); return ADV_ACT_TICK; }   /* the turned one would stand alone (two grips) or join another: the
                                                              first, the keeper's own way up, stays */
        set_live(&s_new); go(ADV_PG_CHECK);                /* the next press goes through the new map */
        return ADV_ACT_TICK; }
    case ADV_PG_CHECK: {
        float cx, cy; adv_check_target(&cx, &cy);
        if ((s_dx - cx) * (s_dx - cx) + (s_dy - cy) * (s_dy - cy) > ADV_CHECK_HIT_PX * ADV_CHECK_HIT_PX) return ADV_ACT_NONE;   /* a miss: the clock runs on */
        s_saved = s_new;                                   /* saved now: whatever the turn brings, this one stays */
        if (s_two) done_page(true);
        else { s_q1 = s_q0; s_first = f_of(&s_saved, s_q0); turn_page(); }
        return ADV_ACT_FIT; }
    case ADV_PG_TURN: {                                    /* TURN IT OVER: the IMU starts the crosses (adv_quarter); the watch's GO */
        if (!still) return ADV_ACT_NONE;
        int h = render_touch_turn_tap(s_dx, s_dy);
        if (h == ADV_HIT_SKIP) { done_page(false); return ADV_ACT_TICK; }
        if (h == ADV_HIT_GO && TANK_WORN) { start_two(q); return ADV_ACT_TICK; }
        return ADV_ACT_NONE; }
    case ADV_PG_DONE: {
        if (!still || s_press_t < ADV_DONE_HOLD_S || render_touch_done_tap(s_dx, s_dy) != ADV_HIT_OK) return ADV_ACT_NONE;   /* (pressed in its
                                                              first moment: a double tap's second half - spent) */
        s_msg = NULL; s_done_turned = false; go(ADV_PG_MAIN); return ADV_ACT_TICK; }
    }
    return ADV_ACT_NONE;
}
void adv_render(uint16_t *fb, int stride) {
    switch (s_pg) {
    case ADV_PG_MAIN: render_advanced(fb, stride, s_pick, adv_charge_ma(s_pick), s_cell, s_pick == ADV_CELL_STOCK, adv_cell_fast(s_pick)); break;
    case ADV_PG_WARN: render_battery_warning(fb, stride, s_pick, adv_charge_ma(s_pick), s_ticked); break;
    case ADV_PG_TOUCH: render_touch_intro(fb, stride, s_saved.n > 0, s_msg); break;
    case ADV_PG_CROSS: { float x, y; adv_cross_target(s_i, &x, &y); render_touch_cross(fb, stride, s_i, ADV_N_CROSS, (int)x, (int)y); break; }
    case ADV_PG_CHECK: { float x, y; adv_check_target(&x, &y); render_touch_check(fb, stride, (int)x, (int)y, ADV_CHECK_S - s_t, ADV_CHECK_S); break; }
    case ADV_PG_TURN: render_touch_turn(fb, stride); break;
    case ADV_PG_DONE: render_touch_done(fb, stride); break;
    default: break;
    }
}
