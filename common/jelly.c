/* jelly.c - THE JELLY TANK, habitat #2: the render spike (2026-10-18).
 *
 * docs/jelly-tank-0.6.md is the design. This file is its step 0: is a tank
 * of soft, translucent, glowing jellies cheap enough to hold the frame rate?
 * Everything here draws straight into an RGB565 frame and keeps no state
 * outside jelly_tank_t and the caller's two buffers (the background cache and
 * the scratch), so the firmware can put both in PSRAM: the board's internal
 * RAM has no room for new tables (firmware-internal-ram-margin).
 *
 * The pieces, back to front in a frame:
 *   background  the water's gradient for the light's color, the LED bar, the
 *               glass's dark edge - baked once per light change, copied
 *   motes       the jet's micro-bubbles, brine shrimp, a tap's ripple ring
 *   per jelly   (far to near) its glow halo (an ADD, not a blend: light on
 *               light), tentacles, oral arms, the bell, the bell's specks
 *   the finger  a resting finger is a soft light
 *
 * Tentacles are drawn along a TRAIL: each jelly remembers which way its
 * tail pointed over the last couple of seconds (where it was heading, how
 * it moved through the water, a little weight), and a tentacle's segment i
 * follows the direction from i steps ago. A turn sweeps down the tentacles
 * as a lazy curve, a pulse straightens them, a current bends them - with no
 * rope simulation and no state per tentacle.
 */
#include "jelly.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define PI_F  3.14159265f
#define TAU_F 6.28318531f

int64_t (*jelly_clock_us)(void) = 0;
int64_t jelly_prof_us[6];

const char *const JELLY_SPECIES_NAMES[JSP_N] = { "MOON JELLY", "SEA NETTLE", "BLUBBER" };
const char *const JELLY_LIGHT_NAMES[JLIGHT_N] = { "VIOLET", "BLUE", "TEAL", "MAGENTA", "WHITE" };
const char *const JELLY_NEON_NAMES[JELLY_NEON_N] = {
    "MOON WHITE", "ICE BLUE", "VIOLET", "MAGENTA", "HOT PINK", "NEON GREEN", "AMBER", "TEAL" };
const uint32_t JELLY_NEON_RGB[JELLY_NEON_N] = {
    0xe4ecff, 0x6cd6ff, 0xa883ff, 0xff58dc, 0xff6a9e, 0x9cff62, 0xffb04a, 0x48ffd6 };

/* the neons by index: 0 MOON WHITE 1 ICE BLUE 2 VIOLET 3 MAGENTA 4 HOT PINK 5 NEON GREEN 6 AMBER 7 TEAL */
const jelly_theme_t JELLY_THEMES[JELLY_THEME_N] = {
    /* the spike's first tank (`jelly on 6`): pale moons with warm accents in violet water - the pendant */
    { "MOONLIGHT", JLIGHT_VIOLET,  7, { { JSP_MOON, 0 }, { JSP_NETTLE, 6 }, { JSP_MOON, 1 }, { JSP_BLUBBER, 4 }, { JSP_MOON, 2 }, { JSP_NETTLE, 1 } } },
    /* warm long-stringed nettles in magenta water - the square */
    { "SUNSET",    JLIGHT_MAGENTA, 11, { { JSP_NETTLE, 6 }, { JSP_NETTLE, 4 }, { JSP_BLUBBER, 6 }, { JSP_NETTLE, 0 }, { JSP_BLUBBER, 4 }, { JSP_NETTLE, 6 } } },
    /* cool round bells, no nettles, in blue water - the watch */
    { "DEEP",      JLIGHT_BLUE,    23, { { JSP_BLUBBER, 5 }, { JSP_MOON, 7 }, { JSP_BLUBBER, 1 }, { JSP_MOON, 5 }, { JSP_BLUBBER, 7 }, { JSP_MOON, 0 } } },
};

void jelly_theme_start(jelly_tank_t *t, int theme) {
    if (theme < 0 || theme >= JELLY_THEME_N) theme = 0;
    const jelly_theme_t *th = &JELLY_THEMES[theme];
    jelly_init(t, th->seed);
    for (int i = 0; i < JELLY_THEME_JELLIES; i++) jelly_add(t, th->mix[i][0], th->mix[i][1]);
    t->light = th->light;
}

/* the water under each LED color: surface, middle, floor */
static const uint32_t WATER[JLIGHT_N][3] = {
    { 0x2e1c72, 0x170d46, 0x070418 },   /* VIOLET */
    { 0x143e8e, 0x0b2256, 0x03091c },   /* BLUE */
    { 0x0d4e62, 0x072c3a, 0x020d12 },   /* TEAL */
    { 0x541763, 0x2c0a3c, 0x0c0314 },   /* MAGENTA */
    { 0x323c5c, 0x1a2134, 0x070912 },   /* WHITE */
};
static const uint32_t WATER_OFF[3] = { 0x0b0819, 0x04030d, 0x000000 };   /* light off: the dark the glow is for */
static const uint32_t LED_RGB[JLIGHT_N] = { 0xb592ff, 0x7cb8ff, 0x72f2ff, 0xff7eea, 0xf2f6ff };

/* ---- species ------------------------------------------------------------ */
typedef struct {
    float a;             /* adult bell half-width, a fraction of the glass's shorter side */
    float h;             /* dome height over a */
    float period;        /* seconds a beat */
    float thrust;        /* px/s a full beat adds along the heading, at JELLY_S 1 */
    int   n_tent;        /* 1 px tentacles off the margin */
    float tent_len;      /* their length over the bell's width (2a) */
    int   n_arm;         /* oral arms: frilled ribbons under the bell */
    float arm_len;       /* over 2a */
    float arm_w;         /* width over a */
    int   body_a;        /* bell fill alpha 0..255 (translucent moon .. solid blubber) */
    int   rim_a;
    float halo;          /* glow radius over a */
    int   n_rib;
    float lappet;        /* depth of the margin's scallops, px at JELLY_S 1 */
    int   n_lap;
} jsp_t;

static const jsp_t SP[JSP_N] = {
    /* MOON: wide, flat, see-through; a short fine fringe, four long arms, the four rings */
    { 0.064f, 0.62f, 2.4f, 34.0f, 28, 0.30f, 4, 0.95f, 0.15f,  64, 215, 2.0f, 16, 1.6f, 8 },
    /* NETTLE: a taller bell with stripes, long trailing tentacles, a frilly skirt of arms */
    { 0.058f, 0.80f, 1.8f, 36.0f, 16, 2.40f, 4, 1.35f, 0.22f, 120, 225, 1.7f, 16, 1.2f, 16 },
    /* BLUBBER: a thick dome with painted spots, cauliflower arms, no tentacles */
    { 0.058f, 0.86f, 1.5f, 34.0f,  0, 0.00f, 8, 0.62f, 0.26f, 165, 235, 1.9f,  8, 1.0f, 8 },
};

static int rope_points(int species) { return species == JSP_NETTLE ? JELLY_ROPE_N : species == JSP_MOON ? 4 : 0; }

/* ---- scratch (PSRAM on the device) --------------------------------------- */
#define HALO_R 32
#define HALO_N (2 * HALO_R + 1)
typedef struct {
    uint8_t halo[HALO_N * HALO_N];     /* a soft disc, 0..255 */
    int16_t gx0[TANK_H], gx1[TANK_H];  /* the glass's inside per row (gx1 < gx0: none) */
    float   sinlut[256];
    bool    ready;
} jelly_scratch_t;

static jelly_scratch_t *g_sc;
static uint16_t *g_bg;
static int g_bg_key = -1;

size_t jelly_scratch_bytes(void) { return sizeof(jelly_scratch_t); }
void jelly_set_scratch(void *buf) { g_sc = (jelly_scratch_t *)buf; if (g_sc) g_sc->ready = false; }
static jelly_scratch_t *scratch(void) {
    if (!g_sc) g_sc = (jelly_scratch_t *)calloc(1, sizeof *g_sc);   /* the sim; the firmware hands one in from PSRAM */
    if (!g_sc->ready) {
        for (int i = 0; i < 256; i++) g_sc->sinlut[i] = sinf(i * (TAU_F / 256));
        const float k = 4.5f, e = expf(-k);
        for (int y = 0; y < HALO_N; y++)
            for (int x = 0; x < HALO_N; x++) {
                float dx = (x - HALO_R) / (float)HALO_R, dy = (y - HALO_R) / (float)HALO_R;
                float d2 = dx * dx + dy * dy;
                float v = d2 >= 1 ? 0 : (expf(-k * d2) - e) / (1 - e);
                g_sc->halo[y * HALO_N + x] = (uint8_t)(v * 255 + 0.5f);
            }
        for (int y = 0; y < TANK_H; y++) {
            int x0 = TANK_W, x1 = -1;                   /* the first and the last pixel inside, found from each end (0.4.2: every
                                                           pixel was asked - a whole frame of distances each visit to THE LIBRARY) */
            for (int x = 0; x < TANK_W; x++) if (jelly_glass_dist(x + 0.5f, y + 0.5f) >= 0) { x0 = x; break; }
            for (int x = TANK_W - 1; x >= x0; x--) if (jelly_glass_dist(x + 0.5f, y + 0.5f) >= 0) { x1 = x; break; }
            g_sc->gx0[y] = (int16_t)x0; g_sc->gx1[y] = (int16_t)x1;
        }
        g_sc->ready = true;
    }
    return g_sc;
}
void jelly_set_bg_cache(uint16_t *buf) { g_bg = buf; g_bg_key = -1; }
static const uint16_t *g_primed;
static int bg_key(const jelly_tank_t *t) { return t->light | (t->light_on ? 16 : 0); }
const uint16_t *jelly_bg_buf(const jelly_tank_t *t) { return g_bg && g_bg_key == bg_key(t) ? g_bg : NULL; }
void jelly_fb_primed(const uint16_t *fb) { g_primed = fb; }

static inline float fsin(float x) { return g_sc->sinlut[(int)(x * (256.0f / TAU_F)) & 255]; }
static inline float fcos(float x) { return fsin(x + PI_F * 0.5f); }

/* ---- small math --------------------------------------------------------- */
static uint32_t rnd(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x ? x : 0x9e3779b9u; }
static float frand(uint32_t *s) { return (rnd(s) >> 8) * (1.0f / 16777216.0f); }
static uint32_t hash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
static float hashf(uint32_t x) { return (hash(x) >> 8) * (1.0f / 16777216.0f); }
/* 1/sqrt(x) to ~0.2% (one Newton step): the strings' lengths, many a frame */
static inline float rsqrt_fast(float x) {
    union { float f; uint32_t i; } u = { x };
    u.i = 0x5f3759df - (u.i >> 1);
    return u.f * (1.5f - 0.5f * x * u.f * u.f);
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float wrap_pi(float a) { while (a > PI_F) a -= TAU_F; while (a < -PI_F) a += TAU_F; return a; }

typedef struct { int r, g, b; } col_t;
static col_t col(uint32_t rgb) { col_t c = { (int)(rgb >> 16 & 255), (int)(rgb >> 8 & 255), (int)(rgb & 255) }; return c; }
static col_t cmix(col_t a, col_t b, float p) {
    col_t c = { a.r + (int)((b.r - a.r) * p), a.g + (int)((b.g - a.g) * p), a.b + (int)((b.b - a.b) * p) }; return c; }
static col_t cscale(col_t a, float k) { col_t c = { (int)(a.r * k), (int)(a.g * k), (int)(a.b * k) }; return c; }

/* ---- the glass ------------------------------------------------------------ */
float jelly_glass_dist(float x, float y) {
#if defined(TANK_ROUND)
    return TANK_RAD - hypotf(x - TANK_RAD, y - TANK_RAD);
#else
#ifdef TANK_CORNER_R
    const float r = TANK_CORNER_R;
#else
    const float r = 0;
#endif
    float qx = fabsf(x - TANK_W * 0.5f) - (TANK_W * 0.5f - r);
    float qy = fabsf(y - TANK_H * 0.5f) - (TANK_H * 0.5f - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float out = sqrtf(ox * ox + oy * oy) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - r;
    return -out;
#endif
}
static void glass_normal(float x, float y, float *nx, float *ny) {   /* pointing inward */
    float gx = jelly_glass_dist(x + 1, y) - jelly_glass_dist(x - 1, y);
    float gy = jelly_glass_dist(x, y + 1) - jelly_glass_dist(x, y - 1);
    float m = sqrtf(gx * gx + gy * gy);
    if (m < 1e-4f) { *nx = 0; *ny = -1; return; }
    *nx = gx / m; *ny = gy / m;
}

/* ---- the water ---------------------------------------------------------- */
float jelly_jet_lap_s(int jet) { return jet == JET_STEADY ? 45.0f : 75.0f; }

#if defined(TANK_ROUND)
#define FLOW_CX TANK_RAD
#define FLOW_CY TANK_RAD
#else
#define FLOW_CX (TANK_W * 0.5f)
#define FLOW_CY (TANK_H * 0.5f)
#endif
/* THE JET (2026-10-18: aimable, Strato: "a directional control for the jet ... some predefined
 * directions instead of an entire 360 degrees"). The nozzle sits on the glass at the upper right, padded
 * in from it (the pendant's curve took the first one to the very edge). It points one of five ways,
 * measured from straight at the tank's center: along the glass either way, the two diagonals, straight
 * in. Along the glass it turns the whole tank (a kreisel: the water circles the center at one angular
 * speed past 3/4 of the way out, so a lap takes the same time everywhere and on every glass, easing to
 * calm at the middle); straight in it throws a plume across; between, some of each. */
#define JET_PAD_S     22.0f                 /* the nozzle's distance from the glass, px at JELLY_S 1 */
#define JET_FAN_R_S   34.0f                 /* the arrows' distance from the nozzle */
#define JET_CHIP_R_S  10.0f                 /* an arrow's radius */
#define JET_STEADY_LEVEL 1.6667f            /* STEADY laps in 45 s, GENTLE in 75 */
#define JET_RAMP      0.5f                  /* level a second: ~2 s from still to GENTLE (the pace, not the water) */
#define WATER_TAU_NEAR 1.0f                 /* s: the water the plume reaches follows the jet this fast */
#define WATER_TAU_FAR  5.5f                 /* s: the rest of the tank coasts on its own momentum this long */
static const float JET_AIM_DEG[JET_AIMS] = { -72, -36, 0, 36, 72 };   /* from straight at the center */

/* the nozzle and the five directions never move on a glass: found once (a few dozen bytes) */
static bool  s_nz_ready;
static float s_nz_x, s_nz_y, s_aim_dx[JET_AIMS], s_aim_dy[JET_AIMS];
static void nozzle_find(void) {
    if (s_nz_ready) return;
    /* on the right-hand glass a third of the way down (the top-right corner left two walls for the fan
     * to hit on the 1.8), JET_PAD in from it - and further in until every arrow, at its biggest, clears
     * the glass by 4 px on this board */
    float a = -0.5f;
    float dx = cosf(a) * TANK_W * 0.5f, dy = sinf(a) * TANK_H * 0.5f, m = sqrtf(dx * dx + dy * dy);
    dx /= m; dy /= m;
    float px = FLOW_CX, py = FLOW_CY;
    for (int i = 0; i < 2000 && jelly_glass_dist(px + dx, py + dy) >= JET_PAD_S * JELLY_S; i++) { px += dx; py += dy; }
    for (int tries = 0; tries < 200; tries++) {
        float cx = FLOW_CX - px, cy = FLOW_CY - py, cm = sqrtf(cx * cx + cy * cy);
        cx /= cm; cy /= cm;
        float worst = 1e9f;
        for (int k = 0; k < JET_AIMS; k++) {
            float an = JET_AIM_DEG[k] * (PI_F / 180), c = cosf(an), sn = sinf(an);
            s_aim_dx[k] = cx * c - cy * sn; s_aim_dy[k] = cx * sn + cy * c;   /* + turns clockwise on the glass */
            float ax = px + s_aim_dx[k] * JET_FAN_R_S * JELLY_S, ay = py + s_aim_dy[k] * JET_FAN_R_S * JELLY_S;
            float clear = jelly_glass_dist(ax, ay) - JET_CHIP_R_S * JELLY_S * 1.38f;
            if (clear < worst) worst = clear;
        }
        if (worst >= 4) break;
        px -= dx; py -= dy;                             /* a step further in */
    }
    s_nz_x = px; s_nz_y = py;
    s_nz_ready = true;
}
static void nozzle_pos(float *x, float *y) { nozzle_find(); *x = s_nz_x; *y = s_nz_y; }
/* aim k's direction, unit */
static void aim_dir(int k, float nx, float ny, float *dx, float *dy) {
    (void)nx; (void)ny; nozzle_find();
    *dx = s_aim_dx[k]; *dy = s_aim_dy[k];
}
void jelly_jet_geom(const jelly_tank_t *t, float *nx, float *ny, float ax[JET_AIMS], float ay[JET_AIMS], float *chip_r) {
    (void)t;
    nozzle_pos(nx, ny);
    for (int k = 0; k < JET_AIMS; k++) {
        float dx, dy; aim_dir(k, *nx, *ny, &dx, &dy);
        ax[k] = *nx + dx * JET_FAN_R_S * JELLY_S; ay[k] = *ny + dy * JET_FAN_R_S * JELLY_S;
    }
    *chip_r = JET_CHIP_R_S * JELLY_S;
}
void jelly_jet_set(jelly_tank_t *t, int pace, int aim) {
    t->jet = pace < 0 ? JET_OFF : pace > JET_STEADY ? JET_STEADY : pace;
    t->jet_aim = aim < 0 ? 0 : aim >= JET_AIMS ? JET_AIMS - 1 : aim;
}
/* the water the jet asks for at (x, y), at its live aim and level */
static void jet_flow(const jelly_tank_t *t, float x, float y, float *u, float *v, float *wgt) {
    *u = *v = 0; *wgt = 0;
    float nzx, nzy; nozzle_pos(&nzx, &nzy);
    float dx, dy; aim_dir(t->jet_aim_live, nzx, nzy, &dx, &dy);
    /* the turn: how much of the aim runs along the glass, and which way round */
    float rx = nzx - FLOW_CX, ry = nzy - FLOW_CY, rm = sqrtf(rx * rx + ry * ry);
    float cross = (rx * dy - ry * dx) / rm;              /* + clockwise on the glass */
    const float ax = TANK_W * 0.5f, ay = TANK_H * 0.5f;
    float nx = (x - FLOW_CX) / ax, ny = (y - FLOW_CY) / ay;
    float r = sqrtf(nx * nx + ny * ny);
    if (r > 1e-3f && fabsf(cross) > 0.05f) {
        float shape = clampf(r / 0.75f, 0, 1);
        shape = shape * shape * (3 - 2 * shape);
        float w = TAU_F / 75.0f * cross * shape;        /* radians a second, at full push */
        *u += -ny * ax * w;
        *v +=  nx * ay * w;
    }
    /* the plume: out of the nozzle along the aim, widening and fading across the tank */
    float px = x - nzx, py = y - nzy, along = px * dx + py * dy, lat = -px * dy + py * dx;
    const float S = JELLY_S;
    if (along > -10 * S) {
        float wdt = 30 * S + 0.3f * fmaxf(along, 0), g = lat / wdt;
        float k = expf(-g * g) * expf(-fmaxf(along, 0) / (0.9f * JELLY_SHORT)) * clampf((along + 10 * S) / (20 * S), 0, 1);
        float sp = 24 * S * k;
        *u += dx * sp; *v += dy * sp;
        *wgt = k;
    }
}
/* the jet's level and aim, a tick: a new aim first eases the push to nothing (and the water, with its own
 * lag, keeps going a while), then the nozzle turns and the push builds again in the new direction */
static void tick_jet(jelly_tank_t *t, float dt) {
    float want = t->jet == JET_OFF ? 0 : t->jet == JET_STEADY ? JET_STEADY_LEVEL : 1.0f;
    t->jet_aim_live = t->jet_aim;                       /* the jet turns at once; the water takes its time (below) */
    float step = JET_RAMP * dt;
    float e = want - t->jet_level;
    t->jet_level += e > step ? step : e < -step ? -step : e;
    /* the nozzle's drawn angle follows the live aim, a gentle swing */
    float nx, ny, dx, dy; nozzle_pos(&nx, &ny); aim_dir(t->jet_aim_live, nx, ny, &dx, &dy);
    float target = atan2f(dy, dx), d = wrap_pi(target - t->jet_stub), sw = 5.0f * dt;   /* a quick swing, ~0.5 s */
    t->jet_stub = wrap_pi(t->jet_stub + (d > sw ? sw : d < -sw ? -sw : d));
}

#define CELL_W ((float)TANK_W / JELLY_FIELD_X)
#define CELL_H ((float)TANK_H / JELLY_FIELD_Y)
void jelly_field_at(const jelly_tank_t *t, float x, float y, float *u, float *v) {
    float fx = clampf(x / CELL_W - 0.5f, 0, JELLY_FIELD_X - 1.001f);
    float fy = clampf(y / CELL_H - 0.5f, 0, JELLY_FIELD_Y - 1.001f);
    int ix = (int)fx, iy = (int)fy;
    float sx = fx - ix, sy = fy - iy;
    float u0 = t->fu[iy][ix] + (t->fu[iy][ix + 1] - t->fu[iy][ix]) * sx;
    float u1 = t->fu[iy + 1][ix] + (t->fu[iy + 1][ix + 1] - t->fu[iy + 1][ix]) * sx;
    float v0 = t->fv[iy][ix] + (t->fv[iy][ix + 1] - t->fv[iy][ix]) * sx;
    float v1 = t->fv[iy + 1][ix] + (t->fv[iy + 1][ix + 1] - t->fv[iy + 1][ix]) * sx;
    *u = u0 + (u1 - u0) * sy;
    *v = v0 + (v1 - v0) * sy;
}

/* ---- jellies -------------------------------------------------------------- */
float jelly_bell_a(const jelly_t *j) {
    return SP[j->species].a * JELLY_SHORT * (0.35f + 0.65f * j->grow) * (0.78f + 0.22f * j->z);
}
static float bell_h(const jelly_t *j) { return SP[j->species].h * jelly_bell_a(j); }

/* how closed the bell is through one beat: a quick squeeze, a slow release */
static float contraction(float p) {
    const float SQ = 0.22f;
    if (p < SQ) { float s = p / SQ; return s * s * (3 - 2 * s); }
    float s = (p - SQ) / (1 - SQ);
    return (1 - s) * (1 - s) * (1 + 2 * s) * (1 - 0.15f * s);
}

void jelly_init(jelly_tank_t *t, uint32_t seed) {
    memset(t, 0, sizeof *t);
    t->rng = seed ? seed : 1;
    t->jet_aim = t->jet_aim_live = JET_AIMS - 1;        /* along the glass, clockwise */
    {   /* which end of the fan turns the tank clockwise depends on the glass: pick that one */
        float nx, ny, dx, dy; nozzle_pos(&nx, &ny); aim_dir(JET_AIMS - 1, nx, ny, &dx, &dy);
        if ((nx - FLOW_CX) * dy - (ny - FLOW_CY) * dx < 0) t->jet_aim = t->jet_aim_live = 0;
        aim_dir(t->jet_aim, nx, ny, &dx, &dy); t->jet_stub = atan2f(dy, dx);
    }
    t->ui_hot = -1;
    t->ju_aim = -1;
    t->tc_tap_t = -1e9;
    t->light = JLIGHT_VIOLET;
    t->light_on = true;
    scratch();
}

int jelly_add(jelly_tank_t *t, int species, int neon) {
    if (t->n >= JELLY_MAX) return -1;
    jelly_t *j = &t->j[t->n];
    memset(j, 0, sizeof *j);
    j->species = (uint8_t)(species % JSP_N);
    j->neon = (uint8_t)(neon % JELLY_NEON_N);
    j->seed = rnd(&t->rng);
    j->z = frand(&t->rng);
    j->grow = 1.0f;
    float a = jelly_bell_a(j);
    for (int tries = 0; tries < 64; tries++) {
        j->x = TANK_W * (0.15f + 0.7f * frand(&t->rng));
        j->y = TANK_H * (0.2f + 0.55f * frand(&t->rng));
        if (jelly_glass_dist(j->x, j->y) > a * 3) break;
    }
    j->heading = (frand(&t->rng) - 0.5f) * 1.2f;
    j->target = j->heading;
    j->wander_t = 1 + 5 * frand(&t->rng);
    j->depth = 0.28f + 0.5f * frand(&t->rng);
    j->period = SP[j->species].period * (0.88f + 0.24f * frand(&t->rng));
    j->phase = frand(&t->rng);
    return t->n++;
}
void jelly_remove_last(jelly_tank_t *t) { if (t->n > 0) t->n--; }

static void flash_near(jelly_tank_t *t, float x, float y, float rad, float amount) {
    for (int i = 0; i < t->n; i++) {
        jelly_t *j = &t->j[i];
        float d = hypotf(j->x - x, j->y - y);
        if (d < rad) j->flash = fminf(1.6f, j->flash + amount * (1 - d / rad));
    }
}

void jelly_feed(jelly_tank_t *t, float x) {
    int made = 0;
    for (int i = 0; i < JELLY_SPECKS && made < 14; i++) {
        jelly_mote_t *m = &t->speck[i];
        if (m->live) continue;
        m->live = true;
        m->x = x + (frand(&t->rng) - 0.5f) * 30 * JELLY_S;
        m->y = 10 * JELLY_S + frand(&t->rng) * 14 * JELLY_S;
        m->vx = (frand(&t->rng) - 0.5f) * 16 * JELLY_S;
        m->vy = (4 + 10 * frand(&t->rng)) * JELLY_S;
        m->life = 45 + 20 * frand(&t->rng);
        made++;
    }
}

void jelly_tap(jelly_tank_t *t, float x, float y) {
    if (y < TANK_H * 0.14f) { jelly_feed(t, x); return; }
    jelly_ripple_t *r = &t->ripple[0];
    for (int i = 0; i < JELLY_RIPPLES; i++) if (!t->ripple[i].live) { r = &t->ripple[i]; break; }
    r->live = true; r->x = x; r->y = y; r->t = 0;
    const float rad = 0.36f * JELLY_SHORT, push = 0.16f * JELLY_SHORT;
    for (int i = 0; i < t->n; i++) {
        jelly_t *j = &t->j[i];
        float dx = j->x - x, dy = j->y - y, d = sqrtf(dx * dx + dy * dy);
        if (d >= rad || d < 1e-3f) continue;
        float w = 1 - d / rad;
        j->vx += dx / d * push * w; j->vy += dy / d * push * w;
    }
    for (int cy = 0; cy < JELLY_FIELD_Y; cy++)
        for (int cx = 0; cx < JELLY_FIELD_X; cx++) {
            float px = (cx + 0.5f) * CELL_W - x, py = (cy + 0.5f) * CELL_H - y, d = sqrtf(px * px + py * py);
            if (d >= rad || d < 1e-3f) continue;
            float w = 1 - d / rad;
            t->fu[cy][cx] += px / d * push * 0.4f * w; t->fv[cy][cx] += py / d * push * 0.4f * w;
        }
    flash_near(t, x, y, rad, 1.0f);
}

void jelly_hold(jelly_tank_t *t, float x, float y, bool down) {
    if (down && !t->holding) t->hold_t = 0;
    t->holding = down;
    t->hold_x = x; t->hold_y = y;
}

void jelly_swipe(jelly_tank_t *t, float x0, float y0, float x1, float y1, float dt) {
    if (dt <= 0) return;
    float vx = (x1 - x0) / dt, vy = (y1 - y0) / dt, m = sqrtf(vx * vx + vy * vy);
    const float vmax = 0.5f * JELLY_SHORT;
    if (m > vmax) { vx *= vmax / m; vy *= vmax / m; }
    const float rad = 0.18f * JELLY_SHORT, fmax = 0.3f * JELLY_SHORT;
    for (int cy = 0; cy < JELLY_FIELD_Y; cy++)
        for (int cx = 0; cx < JELLY_FIELD_X; cx++) {
            float px = (cx + 0.5f) * CELL_W - x1, py = (cy + 0.5f) * CELL_H - y1, d = sqrtf(px * px + py * py);
            if (d >= rad) continue;
            float w = (1 - d / rad) * fminf(1, dt * 8);
            float *fu = &t->fu[cy][cx], *fv = &t->fv[cy][cx];
            *fu += (vx - *fu) * 0.6f * w; *fv += (vy - *fv) * 0.6f * w;
            float fm = sqrtf(*fu * *fu + *fv * *fv);
            if (fm > fmax) { *fu *= fmax / fm; *fv *= fmax / fm; }
        }
    flash_near(t, x1, y1, 0.1f * JELLY_SHORT, fminf(1, dt * 6));
}

/* ---- touch ----------------------------------------------------------------- */
#define TC_HOLD_S   0.35                /* a finger resting this long is a hold */
#define TC_MOVE_S   10.0f               /* a finger moving this far (px at JELLY_S 1) is a stroke */
#define TC_NOZZLE_S 30.0f               /* a hold this close to the nozzle opens the fan */
#define TC_DOUBLE_S 0.5                 /* a second tap this soon after a tap ... */
#define TC_DOUBLE_R 0.25f               /* ... and this near it (a fraction of the short side) is the light */
/* what on the open fan is under (x, y): an arrow, the hub, or nothing; sliding out from the hub, the
 * arrow whose direction the finger is in */
static int fan_target(const jelly_tank_t *t, float x, float y) {
    float nx, ny, ax[JET_AIMS], ay[JET_AIMS], cr;
    jelly_jet_geom(t, &nx, &ny, ax, ay, &cr);
    int best = -1; float bd = 1e9f;
    for (int k = 0; k < JET_AIMS; k++) {
        float d = hypotf(x - ax[k], y - ay[k]);
        if (d < cr * 1.9f && d < bd) { bd = d; best = k; }
    }
    if (best >= 0) return best;
    float dx = x - nx, dy = y - ny, dd = sqrtf(dx * dx + dy * dy), rf = JET_FAN_R_S * JELLY_S;
    if (dd < cr * 1.6f) return JET_AIMS;
    if (dd > 0.5f * rf && dd < 1.9f * rf) {
        float bdot = 0.80f;                             /* within ~37 degrees of an arrow */
        for (int k = 0; k < JET_AIMS; k++) {
            float ex, ey; aim_dir(k, nx, ny, &ex, &ey);
            float dot = (dx * ex + dy * ey) / dd;
            if (dot > bdot) { bdot = dot; best = k; }
        }
    }
    return best;
}
void jelly_touch(jelly_tank_t *t, float x, float y, bool down, double now) {
    const float S = JELLY_S;
    if (down && !t->tc_down) {                          /* a press */
        t->tc_down = true; t->tc_opened = false; t->tc_mode = 0;
        t->tc_x0 = t->tc_lx = x; t->tc_y0 = t->tc_ly = y; t->tc_t0 = t->tc_tl = now;
        if (t->ui_open) { t->tc_mode = 3; t->ui_hot = fan_target(t, x, y); t->ui_idle = 0; }
        return;
    }
    if (down) {                                         /* still down */
        float dt = (float)(now - t->tc_tl); t->tc_tl = now;
        if (t->tc_mode == 0) {
            if (hypotf(x - t->tc_x0, y - t->tc_y0) > TC_MOVE_S * S) t->tc_mode = 2;
            else if (now - t->tc_t0 > TC_HOLD_S) {
                float nx, ny; nozzle_pos(&nx, &ny);
                if (hypotf(t->tc_x0 - nx, t->tc_y0 - ny) < TC_NOZZLE_S * S) {   /* the nozzle: the fan */
                    t->ui_open = true; t->ui_t = 0; t->ui_idle = 0; t->tc_mode = 3; t->tc_opened = true; t->ui_hot = JET_AIMS;
                } else t->tc_mode = 1;                  /* anywhere else: a light they turn toward */
            }
        }
        if (t->tc_mode == 1) jelly_hold(t, x, y, true);
        else if (t->tc_mode == 2 && dt > 0) jelly_swipe(t, t->tc_lx, t->tc_ly, x, y, dt);
        else if (t->tc_mode == 3) { t->ui_hot = fan_target(t, x, y); t->ui_idle = 0; }
        t->tc_lx = x; t->tc_ly = y;
        return;
    }
    if (!t->tc_down) return;
    t->tc_down = false;                                 /* a release */
    if (t->tc_mode == 0) {                              /* a tap */
        if (t->ui_open) { t->ui_open = false; t->ui_hot = -1; }
        else if (now - t->tc_tap_t < TC_DOUBLE_S &&
                 hypotf(t->tc_x0 - t->tc_tap_x, t->tc_y0 - t->tc_tap_y) < TC_DOUBLE_R * JELLY_SHORT) {
            /* the second of a double tap: the light, as in the fish tank (2026-10-19). The first tap already
             * rippled at once - a ripple is harmless, so nothing waits to see whether a second tap comes */
            t->light_on = !t->light_on;
            t->tc_lights++;
            t->tc_tap_t = -1e9;
        } else {
            jelly_tap(t, t->tc_x0, t->tc_y0);
            t->tc_ripples++;
            t->tc_tap_t = now; t->tc_tap_x = t->tc_x0; t->tc_tap_y = t->tc_y0;
        }
    } else if (t->tc_mode == 1) jelly_hold(t, t->tc_lx, t->tc_ly, false);
    else if (t->tc_mode == 3) {
        int k = t->ui_hot;
        if (k >= 0 && k < JET_AIMS) {                   /* an arrow: that way (a still jet starts gentle) */
            jelly_jet_set(t, t->jet == JET_OFF ? JET_GENTLE : t->jet, k);
            t->ui_open = false;
        } else if (k == JET_AIMS && !t->tc_opened) {    /* a tap on the hub: the pace, OFF / GENTLE / STEADY */
            t->jet = (t->jet + 1) % 3; t->ui_idle = 0;
        } else if (k < 0 && !t->tc_opened) t->ui_open = false;   /* a tap away closes it */
        t->ui_hot = -1;
    }
    t->tc_mode = 0;
}

static void tick_jelly(jelly_tank_t *t, jelly_t *j, float dt) {
    const jsp_t *sp = &SP[j->species];
    const float S = JELLY_S;
    float a = jelly_bell_a(j), h = bell_h(j);

    float before = j->contract;
    j->phase += dt / j->period;
    if (j->phase >= 1) j->phase -= 1;
    j->contract = contraction(j->phase);
    float dc = j->contract - before;

    float Ux = sinf(j->heading), Uy = -cosf(j->heading);
    float wu, wv;
    jelly_field_at(t, j->x, j->y - h * 0.5f, &wu, &wv);
    float kd = 1 - expf(-dt * 1.1f);                    /* the water's drag */
    j->vx += (wu - j->vx) * kd;
    j->vy += (wv - j->vy) * kd;
    j->vy += 6.0f * S * dt;                             /* heavier than water: why they pulse */
    /* the heading: a slow wander, away from the glass, toward a finger's light */
    j->wander_t -= dt;
    if (j->wander_t <= 0) {
        float r = frand(&t->rng);
        /* a heading to travel on: mostly a clear lean to one side, so the beats carry it across the
         * tank and the turns between legs are long arcs; now and then nearly upright */
        float side = frand(&t->rng) < 0.5f ? -1.0f : 1.0f;
        j->target = r < 0.15f ? (frand(&t->rng) - 0.5f) * 0.4f : side * (0.5f + 0.55f * frand(&t->rng));
        j->wander_t = 7 + 9 * frand(&t->rng);
        if (frand(&t->rng) < 0.35f) j->depth = 0.22f + 0.6f * frand(&t->rng);
    }
    /* the depth it likes: above it, it lies over and pulses less (and sinks);
     * below it, it rights itself and pulses up */
    float dd = j->y / TANK_H - j->depth;
    float tgt = j->target;
    if (dd < -0.06f) {
        float side = tgt >= 0 ? 1.0f : -1.0f, w = clampf((-dd - 0.06f) * 5, 0, 1);
        tgt = tgt + (side * 1.3f - tgt) * w;
    } else if (dd > 0.06f) tgt *= 1 - clampf((dd - 0.06f) * 4, 0, 0.7f);
    float drive = clampf(0.6f + 2.2f * dd, 0.25f, 1.3f);
    float cx = j->x + Ux * h * 0.5f, cy = j->y + Uy * h * 0.5f;     /* the bell's middle */
    float d = jelly_glass_dist(cx, cy), m = 3.4f * a;
    if (d < m) {
        float nx, ny; glass_normal(cx, cy, &nx, &ny);
        float away = atan2f(nx, -ny);
        float w = clampf((m - d) / m * 1.6f, 0, 1);
        tgt = tgt + wrap_pi(away - tgt) * w;
        j->vx += nx * (m - d) / m * 30 * S * dt;
        j->vy += ny * (m - d) / m * 30 * S * dt;
    }
    if (t->holding) {
        float hx = t->hold_x - cx, hy = t->hold_y - cy, hd = sqrtf(hx * hx + hy * hy);
        if (hd < 0.6f * JELLY_SHORT && hd > a) {
            float toward = atan2f(hx, -hy);
            float w = 0.85f * (1 - hd / (0.6f * JELLY_SHORT)) + 0.15f;
            tgt = tgt + wrap_pi(toward - tgt) * w;
        }
    }
    if (dc > 0) { j->vx += Ux * sp->thrust * S * dc * drive; j->vy += Uy * sp->thrust * S * dc * drive; }
    /* the turn (Strato, 2026-10-18: "it needs to be moving in a lateral direction and gracefully pivot
     * as it moves"). A jelly steers with a lopsided beat against the water it is moving through, so it
     * can only turn as fast as that motion allows: |spin| <= speed through the water / a turning radius
     * of ~3 bell half-widths - still, it cannot turn at all; pulsing, it curves. The spin eases in and
     * out (momentum), and the bell turns about its middle, not about its rim. */
    {
        float rvx = j->vx - wu, rvy = j->vy - wv;
        float through = sqrtf(rvx * rvx + rvy * rvy);
        j->turn_max = through / (3.0f * a);
        float diff = wrap_pi(tgt - j->heading);
        float want = clampf(diff * 0.8f, -0.7f, 0.7f);
        j->spin += (want - j->spin) * (1 - expf(-dt * 2.0f));
        j->spin = clampf(j->spin, -j->turn_max, j->turn_max);
        float mid = h * 0.45f;
        float mx = j->x + sinf(j->heading) * mid, my = j->y - cosf(j->heading) * mid;
        j->heading = wrap_pi(j->heading + j->spin * dt);
        j->x = mx - sinf(j->heading) * mid; j->y = my + cosf(j->heading) * mid;
    }

    j->x += j->vx * dt;
    j->y += j->vy * dt;

    /* never through the glass: the bell's middle keeps its radius inside */
    Ux = sinf(j->heading); Uy = -cosf(j->heading);
    float rr = (a > h ? a : h) * 0.95f;
    for (int pass = 0; pass < 2; pass++) {
        cx = j->x + Ux * h * 0.5f; cy = j->y + Uy * h * 0.5f;
        d = jelly_glass_dist(cx, cy);
        if (d >= rr) break;
        float nx, ny; glass_normal(cx, cy, &nx, &ny);
        j->x += nx * (rr - d); j->y += ny * (rr - d);
        float vn = j->vx * nx + j->vy * ny;
        if (vn < 0) { j->vx -= vn * nx; j->vy -= vn * ny; }
    }

    j->flash *= expf(-dt / 1.4f);
    j->fed = fmaxf(0, j->fed - dt / 3600.0f);

}

int jelly_tentacles(const jelly_t *j) { int n = SP[j->species].n_tent; return n > JELLY_TENT_MAX ? JELLY_TENT_MAX : n; }
int jelly_rope_points(const jelly_t *j) { return SP[j->species].n_tent > 0 ? rope_points(j->species) : 0; }
float jelly_rope_len(const jelly_t *j, int k) {
    return SP[j->species].tent_len * 2 * jelly_bell_a(j) * (0.8f + 0.4f * hashf(j->seed + k * 77));
}
/* where the tentacles hang from: on the margin, which curves down at the sides and closes with the
 * beat. The bell's frame is worked out once a jelly (sinf / cosf are software on the S3: four per
 * tentacle were ~2 ms a frame for six jellies on the 1.8) */
typedef struct { float x, y, a, h, Ux, Uy, Rx, Ry; int n; } bellframe_t;
static bellframe_t bell_frame(const jelly_t *j) {
    bellframe_t b;
    b.x = j->x; b.y = j->y; b.n = SP[j->species].n_tent;
    b.a = jelly_bell_a(j) * (1 - 0.14f * j->contract); b.h = bell_h(j) * (1 + 0.16f * j->contract);
    float sn = sinf(j->heading), cs = cosf(j->heading);
    b.Ux = sn; b.Uy = -cs; b.Rx = cs; b.Ry = sn;
    return b;
}
static void tent_root(const bellframe_t *b, int k, float *x, float *y) {
    float uk = b->n == 1 ? 0 : -0.92f + 1.84f * k / (b->n - 1);
    float mv = 0.2f * b->h * (1 - uk * uk);
    *x = b->x + b->Rx * uk * b->a + b->Ux * mv; *y = b->y + b->Ry * uk * b->a + b->Uy * mv;
}
/* the strings (second pass, 2026-10-18 - Strato: "sometimes they get a bit unruly and spirally ...
 * if it sits in an eddy, the tentacles curl up on themselves"). Measured on the bowl, the nettles' long
 * tentacles were curled past half a turn 21% of the time in still water, 53% in the jet, 80% stirred.
 * What holds a real tentacle in shape when the jelly is not going anywhere is drag and a little
 * weight, and the strings had almost neither. Now:
 *  - drag: a point keeps little of its own motion a tick and takes up the water's (ROPE_DRAG);
 *  - weight: enough to hang when the bell is still (ROPE_SAG); drag streams them back when it travels;
 *  - the ripple is drawn (draw_tentacles), not pushed: a force there could wind a coil;
 *  - each joint bends at most ROPE_BEND from the one above (the first from straight down the bell,
 *    ROPE_BEND_ROOT): a string can curve, never spiral. Lengths are still held follow-the-leader. */
#ifndef ROPE_DRAG
#define ROPE_DRAG      4.0f        /* 1/s */
#endif
#ifndef ROPE_SAG
#define ROPE_SAG       12.0f       /* px/s^2 at JELLY_S 1: nearly as light as the water (10-19: 40 hung them straight
                                    * down off a tilted bell - Strato's footage shows them trailing the bell's axis) */
#endif
#ifndef ROPE_BEND
#define ROPE_BEND      0.45f       /* radians a joint */
#endif
#ifndef ROPE_BEND_ROOT
#define ROPE_BEND_ROOT 0.70f       /* ~40 degrees off the bell's axis at the rim, at most: the bend comes down the length */
#endif
/* the wake (10-19): every beat pushes water out of the bell's underside along its axis - what keeps a real
 * jelly's tentacles trailing behind it whichever way it leans. Strongest in the squeeze, fading down the
 * strings so their far ends drift free */
#ifndef WAKE_BASE
#define WAKE_BASE      10.0f       /* px/s at JELLY_S 1 */
#endif
#ifndef WAKE_PULSE
#define WAKE_PULSE     24.0f
#endif
#ifndef WAKE_FADE
#define WAKE_FADE      1.6f        /* e-folds from the root to the tip */
#endif
/* the oral arms: heavier, wider, a little less floppy than a tentacle - but hanging and trailing
 * with everything else (Strato: tentacles dangled under gravity while the arms, still swept along
 * the bell's axis, stood out "nearly perpendicular") */
#ifndef ARM_DRAG
#define ARM_DRAG       3.5f
#endif
#ifndef ARM_SAG
#define ARM_SAG        18.0f
#endif
#ifndef ARM_BEND
#define ARM_BEND       0.32f
#endif
#ifndef ARM_BEND_ROOT
#define ARM_BEND_ROOT  0.60f
#endif
int jelly_arms(const jelly_t *j) { int n = SP[j->species].n_arm; return n > JELLY_ARM_MAX ? JELLY_ARM_MAX : n; }
float jelly_arm_len(const jelly_t *j, int m) {
    return SP[j->species].arm_len * 2 * jelly_bell_a(j) * (0.85f + 0.3f * hashf(j->seed + m * 131));
}
static void arm_root(const bellframe_t *b, int m, int n, float *x, float *y) {
    float um = n == 1 ? 0 : (-1 + 2.0f * m / (n - 1)) * 0.32f;
    float mv = 0.2f * b->h - 1;                         /* under the middle of the hollow */
    *x = b->x + b->Rx * um * b->a + b->Ux * mv; *y = b->y + b->Ry * um * b->a + b->Uy * mv;
}

/* one string, one tick: verlet (keep, then the water's share and the weight), then each point back
 * to its length from the one above, its joint bent no more than (c, s) from that one's direction
 * (the first joint from straight down the bell, (cr, sr)) */
static void rope_step(float *X, float *Y, float *PX, float *PY, int N, float rx, float ry, float seg,
                      float keep, float wdx, float wdy, float sag, float downx, float downy,
                      float c, float s, float cr, float sr, float wkx, float wky, float wfade) {
    PX[0] = X[0]; PY[0] = Y[0]; X[0] = rx; Y[0] = ry;   /* the root rides the bell */
    float wf = 1, wstep = wfade;                        /* the wake's share, fading down the string */
    for (int i = 1; i < N; i++) {
        wf *= wstep;
        float vx = (X[i] - PX[i]) * keep + wdx + wkx * wf, vy = (Y[i] - PY[i]) * keep + wdy + wky * wf;
        PX[i] = X[i]; PY[i] = Y[i];
        X[i] += vx; Y[i] += vy + sag;
    }
    float px = downx, py = downy;
    for (int i = 1; i < N; i++) {
        float dx = X[i] - X[i - 1], dy = Y[i] - Y[i - 1], m2 = dx * dx + dy * dy;
        if (m2 < 1e-8f) { dx = px; dy = py; m2 = 1; }
        float r = rsqrt_fast(m2);
        dx *= r; dy *= r;
        float cl = i == 1 ? cr : c, sl = i == 1 ? sr : s;
        if (px * dx + py * dy < cl) {                   /* bent past the limit: turn it back to the limit */
            float side = px * dy - py * dx >= 0 ? 1.0f : -1.0f;
            dx = px * cl - py * sl * side; dy = px * sl * side + py * cl;
        }
        X[i] = X[i - 1] + dx * seg; Y[i] = Y[i - 1] + dy * seg;
        px = dx; py = dy;
    }
}

static void tick_ropes(const jelly_tank_t *t, jelly_t *j, float dt) {
    const float S = JELLY_S;
    const bellframe_t bf = bell_frame(j);
    const float Ux = bf.Ux, Uy = bf.Uy, ba = jelly_bell_a(j);
    float wu, wv;
    jelly_field_at(t, j->x - Ux * ba, j->y - Uy * ba, &wu, &wv);

    const float wake = (WAKE_BASE + WAKE_PULSE * j->contract) * S;   /* px/s out of the bell, along -U */
    int n = jelly_tentacles(j), N = jelly_rope_points(j);
    if (n > 0 && N >= 2) {
        if (!j->rope_ready) {                           /* hanging straight down from the margin */
            for (int k = 0; k < n; k++) {
                float rx, ry; tent_root(&bf, k, &rx, &ry);
                float seg = jelly_rope_len(j, k) / (N - 1);
                for (int i = 0; i < N; i++) {
                    j->rope_x[k][i] = j->rope_px[k][i] = rx - Ux * seg * i;
                    j->rope_y[k][i] = j->rope_py[k][i] = ry - Uy * seg * i;
                }
            }
            j->rope_ready = true;
        }
        const float keep = expf(-dt * ROPE_DRAG), sag = ROPE_SAG * S * dt * dt;
        const float cb = cosf(ROPE_BEND), sb = sinf(ROPE_BEND), cr = cosf(ROPE_BEND_ROOT), sr = sinf(ROPE_BEND_ROOT);
        for (int k = 0; k < n; k++) {
            float rx, ry; tent_root(&bf, k, &rx, &ry);
            float seg = SP[j->species].tent_len * 2 * ba * (0.8f + 0.4f * hashf(j->seed + k * 77)) / (N - 1);
            /* no two alike: each its own weight and drag (+-30%), so a bundle spreads and crosses */
            float hk = hashf(j->seed + k * 191), hd = hashf(j->seed + k * 313);
            float lose = (1 - keep) * (0.7f + 0.6f * hd);
            rope_step(j->rope_x[k], j->rope_y[k], j->rope_px[k], j->rope_py[k], N, rx, ry, seg,
                      1 - lose, wu * dt * lose, wv * dt * lose, sag * (0.7f + 0.6f * hk), -Ux, -Uy, cb, sb, cr, sr,
                      -Ux * wake * dt * lose, -Uy * wake * dt * lose, expf(-WAKE_FADE / (N - 1)));
        }
    }

    int na = jelly_arms(j);
    if (na > 0) {
        const int NA = JELLY_ARM_N;
        if (!j->arm_ready) {
            for (int m = 0; m < na; m++) {
                float rx, ry; arm_root(&bf, m, na, &rx, &ry);
                float seg = jelly_arm_len(j, m) / (NA - 1);
                for (int i = 0; i < NA; i++) {
                    j->arm_x[m][i] = j->arm_px[m][i] = rx - Ux * seg * i;
                    j->arm_y[m][i] = j->arm_py[m][i] = ry - Uy * seg * i;
                }
            }
            j->arm_ready = true;
        }
        const float keep = expf(-dt * ARM_DRAG), sag = ARM_SAG * S * dt * dt;
        const float cb = cosf(ARM_BEND), sb = sinf(ARM_BEND), cr = cosf(ARM_BEND_ROOT), sr = sinf(ARM_BEND_ROOT);
        for (int m = 0; m < na; m++) {
            float rx, ry; arm_root(&bf, m, na, &rx, &ry);
            float seg = SP[j->species].arm_len * 2 * ba * (0.85f + 0.3f * hashf(j->seed + m * 131)) / (NA - 1);
            float hk = hashf(j->seed + m * 211), hd = hashf(j->seed + m * 337);
            float lose = (1 - keep) * (0.75f + 0.5f * hd);
            rope_step(j->arm_x[m], j->arm_y[m], j->arm_px[m], j->arm_py[m], NA, rx, ry, seg,
                      1 - lose, wu * dt * lose, wv * dt * lose, sag * (0.75f + 0.5f * hk), -Ux, -Uy, cb, sb, cr, sr,
                      -Ux * wake * dt * lose, -Uy * wake * dt * lose, expf(-WAKE_FADE / (NA - 1)));
        }
    }
}

void jelly_tick(jelly_tank_t *t, float dt) {
    if (dt <= 0) return;
    int64_t tk0 = jelly_clock_us ? jelly_clock_us() : 0;
    if (dt > 0.1f) dt = 0.1f;
    scratch();
    t->clock += dt;
    if (t->clock >= JELLY_CLOCK_LAP) { t->clock -= JELLY_CLOCK_LAP; t->laps++; }
    const float S = JELLY_S;

    tick_jet(t, dt);
    if (t->ui_open) {                                   /* the fan closes itself when left alone */
        t->ui_t += dt;
        if (!t->tc_down && (t->ui_idle += dt) > 6) { t->ui_open = false; t->ui_hot = -1; }
    }
    /* the water relaxes to the jet's flow (or to still); the flow at full push is worked out once an aim
     * (two expf a cell in software were ~0.4 ms a tick on the 1.8) and scaled by the push */
    if (t->ju_aim != t->jet_aim_live) {
        for (int cy = 0; cy < JELLY_FIELD_Y; cy++)
            for (int cx = 0; cx < JELLY_FIELD_X; cx++)
                jet_flow(t, (cx + 0.5f) * CELL_W, (cy + 0.5f) * CELL_H, &t->ju[cy][cx], &t->jv[cy][cx], &t->jw[cy][cx]);
        t->ju_aim = t->jet_aim_live;
    }
    /* the water's own inertia: where the plume reaches, it follows the jet within a second; everywhere else it
     * coasts on and turns over slowly - a circling tank slows, stops and comes round, starting at the nozzle */
    float kn = 1 - expf(-dt / WATER_TAU_NEAR), kfar = 1 - expf(-dt / WATER_TAU_FAR), lv = t->jet_level;
    float reach = lv > 1 ? 1 : lv;
    for (int cy = 0; cy < JELLY_FIELD_Y; cy++)
        for (int cx = 0; cx < JELLY_FIELD_X; cx++) {
            float w = t->jw[cy][cx] * reach, kf = kfar + (kn - kfar) * (w > 1 ? 1 : w);
            t->fu[cy][cx] += (t->ju[cy][cx] * lv - t->fu[cy][cx]) * kf;
            t->fv[cy][cx] += (t->jv[cy][cx] * lv - t->fv[cy][cx]) * kf;
        }

    for (int i = 0; i < t->n; i++) { tick_jelly(t, &t->j[i], dt); tick_ropes(t, &t->j[i], dt); }

    /* a little room between bells (overlap is allowed, sitting inside one is not) */
    for (int i = 0; i < t->n; i++)
        for (int k = i + 1; k < t->n; k++) {
            jelly_t *p = &t->j[i], *q = &t->j[k];
            float dx = q->x - p->x, dy = q->y - p->y, d = sqrtf(dx * dx + dy * dy);
            float mind = (jelly_bell_a(p) + jelly_bell_a(q)) * 2.6f;   /* room: they spread through the water */
            if (d >= mind || d < 1e-3f) continue;
            float w = (mind - d) / mind;
            float f = w * w * 40 * S * dt;
            p->vx -= dx / d * f; p->vy -= dy / d * f;
            q->vx += dx / d * f; q->vy += dy / d * f;
        }

    /* brine shrimp: drift and sink; a bell or its arms passing through collects them */
    for (int i = 0; i < JELLY_SPECKS; i++) {
        jelly_mote_t *m = &t->speck[i];
        if (!m->live) continue;
        float u, v; jelly_field_at(t, m->x, m->y, &u, &v);
        float k = 1 - expf(-dt * 0.9f);
        m->vx += (u - m->vx) * k; m->vy += (v + 7 * S - m->vy) * k;
        m->x += m->vx * dt; m->y += m->vy * dt;
        m->life -= dt;
        if (m->life <= 0 || jelly_glass_dist(m->x, m->y) < 1.5f) { m->live = false; continue; }
        for (int n = 0; n < t->n; n++) {
            jelly_t *j = &t->j[n];
            float a = jelly_bell_a(j);
            bool caught = hypotf(m->x - j->x, m->y - j->y) < a * 1.2f;
            for (int q = 0; !caught && j->arm_ready && q < jelly_arms(j); q++)     /* an arm's middle sweeps them up */
                caught = hypotf(m->x - j->arm_x[q][JELLY_ARM_N / 2], m->y - j->arm_y[q][JELLY_ARM_N / 2]) < a * 0.6f;
            if (caught) {
                m->live = false;
                j->fed = fminf(1, j->fed + 0.07f);
                j->flash = fminf(1.6f, j->flash + 0.12f);
                break;
            }
        }
    }

    /* the jet's micro-bubbles ride the flow: you can see the current */
    if (t->jet_level > 0.02f) {
        t->bubble_acc += dt * 5.5f * t->jet_level;
        float nx, ny; nozzle_pos(&nx, &ny);
        float dx = cosf(t->jet_stub), dy = sinf(t->jet_stub);
        while (t->bubble_acc >= 1) {
            t->bubble_acc -= 1;
            for (int i = 0; i < JELLY_BUBBLES; i++)
                if (!t->bubble[i].live) {
                    jelly_mote_t *b = &t->bubble[i];
                    b->live = true; b->x = nx + dx * 7 * S; b->y = ny + dy * 7 * S;
                    b->vx = dx * 26 * S * t->jet_level; b->vy = dy * 26 * S * t->jet_level;   /* out of the nozzle */
                    b->life = 8 + 8 * frand(&t->rng);
                    break;
                }
        }
    }
    for (int i = 0; i < JELLY_BUBBLES; i++) {
        jelly_mote_t *b = &t->bubble[i];
        if (!b->live) continue;
        float u, v; jelly_field_at(t, b->x, b->y, &u, &v);
        float k = 1 - expf(-dt * 1.2f);
        b->vx += (u - b->vx) * k; b->vy += (v - 5 * S - b->vy) * k;
        b->x += b->vx * dt + (frand(&t->rng) - 0.5f) * 0.6f; b->y += b->vy * dt;
        b->life -= dt;
        if (b->life <= 0 || jelly_glass_dist(b->x, b->y) < 1) b->live = false;
    }

    for (int i = 0; i < JELLY_RIPPLES; i++)
        if (t->ripple[i].live && (t->ripple[i].t += dt) > 1.3f) t->ripple[i].live = false;
    if (t->holding) {
        t->hold_t += dt;
        flash_near(t, t->hold_x, t->hold_y, 0.12f * JELLY_SHORT, dt * 0.8f);
    }
    if (jelly_clock_us) jelly_prof_us[5] += jelly_clock_us() - tk0;
}

/* ---- drawing -------------------------------------------------------------- */
static uint16_t *g_fb;
static int g_stride;

static inline bool inside(int x, int y) {
    return (unsigned)y < TANK_H && x >= g_sc->gx0[y] && x <= g_sc->gx1[y];
}
/* RGB565 three channels at once (2026-10-18, the performance pass): a pixel spread across a 32-bit
 * word as 00000GGGGGG00000RRRRR000000BBBBB keeps a gap above every field, so one multiply blends all
 * three and one add adds them, the overflow landing in the gaps. The blend's alpha is 5 bits (a step
 * of 8/255: within one color step of the per-channel math, 2 at most); the add is exact. */
#define SPREAD_MASK 0x07E0F81FU
/* the frame's byte order (render.h's PT_FB_SWAP, main since 2026-10-23): on the device every pixel in the frame and the
 * background cache is stored byte-swapped, as the panel's wire wants it. Every load from the frame and every store into
 * it goes through JFB; the color math stays native. The sim builds with it off. */
#ifndef PT_FB_SWAP
#define PT_FB_SWAP 0
#endif
#if PT_FB_SWAP
#define JFB(v) ((uint16_t)((((uint16_t)(v)) << 8) | (((uint16_t)(v)) >> 8)))
#else
#define JFB(v) ((uint16_t)(v))
#endif
static inline uint32_t spread565(uint16_t c) { return (c | ((uint32_t)c << 16)) & SPREAD_MASK; }
static inline uint16_t pack565(uint32_t s) { return (uint16_t)((s | (s >> 16)) & 0xFFFF); }
static inline uint32_t col_spread(col_t c) { return spread565((uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3))); }
static inline void blend_s(uint16_t *p, uint32_t fg, int a5) {
    uint32_t bg = spread565(JFB(*p));
    *p = JFB(pack565((bg + (((fg - bg) * (uint32_t)a5) >> 5)) & SPREAD_MASK));
}
static inline void add_s(uint16_t *p, uint32_t s) {
    uint32_t sum = spread565(JFB(*p)) + s, ov = sum & 0x08010020U;
    if (ov) sum |= (ov & 0x20 ? 0x1FU : 0) | (ov & 0x10000 ? 0xF800U : 0) | (ov & 0x8000000 ? 0x07E00000U : 0);
    *p = JFB(pack565(sum & SPREAD_MASK));
}
/* what light c at strength a (0..255) adds, spread: the per-channel add truncated to 565 exactly */
static inline uint32_t add_term(col_t c, int a) {
    return spread565((uint16_t)((((c.r * a >> 8) >> 3) << 11) | (((c.g * a >> 8) >> 2) << 5) | ((c.b * a >> 8) >> 3)));
}
static inline void pblend_s(int x, int y, uint32_t fg, int a) {
    if (!inside(x, y) || a < 4) return;
    blend_s(&g_fb[y * g_stride + x], fg, a > 255 ? 32 : (a + 4) >> 3);
}
static inline void pblend(int x, int y, col_t c, int a) { pblend_s(x, y, col_spread(c), a); }
/* light on light: a saturating add (the glow; a blend only ever lerps) */
static inline void padd_raw(uint16_t *p, col_t c, int a) { add_s(p, add_term(c, a)); }
static inline void padd(int x, int y, col_t c, int a) {
    if (!inside(x, y) || a <= 0) return;
    padd_raw(&g_fb[y * g_stride + x], c, a > 255 ? 255 : a);
}

/* the water, the LED bar, the glass's dark edge - into dst (stride TANK_W
 * for the cache, the frame's otherwise), ordered-dithered to RGB565.
 * 0.4.2 (THE LIBRARY's pictures: three of these with no cache were most of its 2-3 s): the same pixels, sooner. A row's
 * LED bar is worked out once (it was an expf a pixel), and only the pixels within `edge` of the glass ask for their
 * distance: the glass is convex, so the rest of a row is one run between them, where the water is the row's own color
 * (k = 1) - four dithered values, repeated. */
typedef struct { col_t c, led; float bar; } bg_row_t;
static inline uint16_t bg_px(const bg_row_t *R, int x, int y, float k) {
    static const uint8_t BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    const col_t c = R->c, led = R->led; const float bar = R->bar;
    int r = (int)(c.r * k), g = (int)(c.g * k), b = (int)(c.b * k);
    if (bar > 0.01f) { r += (int)((255 - r) * bar * 0.85f * (led.r / 255.0f)); g += (int)((255 - g) * bar * 0.85f * (led.g / 255.0f)); b += (int)((255 - b) * bar * 0.85f * (led.b / 255.0f)); }
    int dth = BAYER[(y & 3) * 4 + (x & 3)];
    r += (dth - 8) >> 1; b += (dth - 8) >> 1; g += (dth - 8) >> 2;
    r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
    return JFB((uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
}
static void draw_background(const jelly_tank_t *t, uint16_t *dst, int stride) {
    const uint32_t *w = t->light_on ? WATER[t->light] : WATER_OFF;
    col_t top = col(w[0]), mid = col(w[1]), bot = col(w[2]);
    col_t led = col(LED_RGB[t->light]);
    const float S = JELLY_S, edge = 30 * S;
    for (int y = 0; y < TANK_H; y++) {
        float p = (float)y / (TANK_H - 1);
        col_t c = p < 0.42f ? cmix(top, mid, p / 0.42f) : cmix(mid, bot, (p - 0.42f) / 0.58f);
        if (t->light_on) {                              /* the LED's spill down the water */
            float g = 0.30f * expf(-y / (55 * S));
            c = cmix(c, led, g);
        }
        bg_row_t R = { c, led, t->light_on ? expf(-y / (2.2f * S)) : 0 };   /* (bar: the bar itself, at the top of the glass) */
        uint16_t *row = dst + y * stride;
        int xa = 0, xb = TANK_W - 1;                    /* the glass's edge from the left, then from the right, a pixel at a time */
        for (; xa < TANK_W; xa++) {
            float d = jelly_glass_dist(xa + 0.5f, y + 0.5f);
            if (d >= edge) break;
            row[xa] = d < 0 ? 0 : bg_px(&R, xa, y, 0.5f + 0.5f * (d / edge) * (2 - d / edge));
        }
        for (; xb > xa; xb--) {
            float d = jelly_glass_dist(xb + 0.5f, y + 0.5f);
            if (d >= edge) break;
            row[xb] = d < 0 ? 0 : bg_px(&R, xb, y, 0.5f + 0.5f * (d / edge) * (2 - d / edge));
        }
        if (xa <= xb && xa < TANK_W) {                  /* the open water between: the row's color */
            uint16_t v[4]; for (int i = 0; i < 4; i++) v[i] = bg_px(&R, i, y, 1);
            for (int x = xa; x <= xb; x++) row[x] = v[x & 3];
        }
    }
}

/* a soft disc of light, radius R, intensity 0..1 */
static void halo(float cx, float cy, float R, col_t c, float inten) {
    if (inten <= 0.01f || R < 2) return;
    int ai = (int)(inten * 256);
    if (ai > 256) ai = 256;
    /* how far out the disc still adds anything (a > 2): the faint rim of the box is skipped, each row
     * spans only its chord of that circle (the disc is exp(-4.5 d^2), renormalized to 0 at d = 1) */
    float thr = (512.0f / ai + 1) / 255.0f;
    if (thr >= 1) return;
    const float kk = 4.5f, e0 = expf(-kk);
    float d2 = -logf(thr * (1 - e0) + e0) / kk;
    float Re = R * (d2 < 1 ? sqrtf(d2) : 1.0f) + 1;
    int y0 = (int)(cy - Re), y1 = (int)(cy + Re) + 1;
    float k = HALO_R / R;
    if (y0 < 0) y0 = 0;
    if (y1 > TANK_H - 1) y1 = TANK_H - 1;
    uint32_t tab[256];                                  /* the add term for each level of the disc */
    for (int v = 0; v < 256; v++) { int a = v * ai >> 8; tab[v] = a > 2 ? add_term(c, a) : 0; }
    for (int y = y0; y <= y1; y++) {
        int ly = (int)((y + 0.5f - cy) * k + HALO_R + 0.5f);
        if (ly < 0 || ly >= HALO_N) continue;
        const uint8_t *lr = &g_sc->halo[ly * HALO_N];
        float ddy = y + 0.5f - cy, ch2 = Re * Re - ddy * ddy;
        if (ch2 <= 0) continue;
        float ch = sqrtf(ch2);
        int x0 = (int)(cx - ch), x1 = (int)(cx + ch) + 1;
        if (x0 < g_sc->gx0[y]) x0 = g_sc->gx0[y];
        if (x1 > g_sc->gx1[y]) x1 = g_sc->gx1[y];
        uint16_t *p = &g_fb[y * g_stride + x0];
        int fx = (int)(((x0 + 0.5f - cx) * k + HALO_R + 0.5f) * 65536), fk = (int)(k * 65536);
        for (int x = x0; x <= x1; x++, p++, fx += fk) {
            int lx = fx >> 16;
            if ((unsigned)lx >= HALO_N) continue;
            uint32_t sterm = tab[lr[lx]];
            if (sterm) add_s(p, sterm);
        }
    }
}

typedef struct {                   /* one jelly's look this frame */
    float a, h, Ux, Uy, Rx, Ry, c, glow;
    col_t neon, body, rim, tent, arm;
    int body_a, rim_a, tent_a, arm_a;
    bool night;
} look_t;

/* the tentacles: a Catmull-Rom curve through each string's points (three points a segment, straight
 * one-pixel steps between them), fading to the tip */
static void draw_tentacles(const jelly_tank_t *t, const jelly_t *j, const look_t *L) {
    int n = SP[j->species].n_tent, N = jelly_rope_points(j);
    if (n <= 0 || N < 2 || !j->rope_ready) return;
    if (n > JELLY_TENT_MAX) n = JELLY_TENT_MAX;
    static const float B[3][4] = {                       /* the basis at u = 1/3, 2/3, 1 */
        { -0.0740741f, 0.7777778f, 0.3333333f, -0.0370370f },
        { -0.0370370f, 0.3333333f, 0.7777778f, -0.0740741f },
        { 0, 0, 1, 0 } };
    float rip_x[JELLY_ROPE_N], rip_y[JELLY_ROPE_N];
    const uint32_t tent_s = col_spread(L->tent);
    const float amp = 1.6f * JELLY_S;                    /* the ripple: drawn, a sideways offset growing to the tip */
    for (int k = 0; k < n; k++) {
        const float *RX0 = j->rope_x[k], *RY0 = j->rope_y[k];
        float ph = t->clock * (1.0f + 0.6f * hashf(j->seed + k * 191)) + k * 1.37f + (j->seed & 255) * 0.1f;
        if (t->laps)                                     /* this tentacle's own rate is no whole number of turns a lap: the turns the laps left over */
            ph += fmodf((float)t->laps * fmodf(JELLY_CLOCK_LAP * (1.0f + 0.6f * hashf(j->seed + k * 191)), TAU_F), TAU_F);
        for (int i = 0; i < N; i++) {
            int a0 = i > 0 ? i - 1 : 0, a1 = i + 1 < N ? i + 1 : N - 1;
            float tx = RX0[a1] - RX0[a0], ty = RY0[a1] - RY0[a0], m2 = tx * tx + ty * ty;
            float r = m2 > 1e-8f ? rsqrt_fast(m2) : 0, f = (float)i / (N - 1);
            float o = amp * f * fsin(ph - i * 0.7f);
            rip_x[i] = RX0[i] - ty * r * o; rip_y[i] = RY0[i] + tx * r * o;
        }
        const float *X = rip_x, *Y = rip_y;
        float lx = X[0], ly = Y[0];
        int lpx = (int)lx, lpy = (int)ly;
        for (int i = 0; i < N - 1; i++) {
            int i0 = i > 0 ? i - 1 : 0, i3 = i + 2 < N ? i + 2 : N - 1;
            for (int q = 0; q < 3; q++) {
                float x = X[i0] * B[q][0] + X[i] * B[q][1] + X[i + 1] * B[q][2] + X[i3] * B[q][3];
                float y = Y[i0] * B[q][0] + Y[i] * B[q][1] + Y[i + 1] * B[q][2] + Y[i3] * B[q][3];
                float ddx = x - lx, ddy = y - ly;
                int steps = (int)(fabsf(ddx) > fabsf(ddy) ? fabsf(ddx) : fabsf(ddy)) + 1;
                float a0 = L->tent_a * (1 - 0.8f * (i + q / 3.0f) / (N - 1)), a1 = L->tent_a * (1 - 0.8f * (i + (q + 1) / 3.0f) / (N - 1));
                for (int s2 = 1; s2 <= steps; s2++) {
                    float w = (float)s2 / steps;
                    int px = (int)(lx + ddx * w), py = (int)(ly + ddy * w);
                    if (px == lpx && py == lpy) continue;
                    pblend_s(px, py, tent_s, (int)(a0 + (a1 - a0) * w));
                    lpx = px; lpy = py;
                }
                lx = x; ly = y;
            }
        }
    }
}

/* the oral arms: frilled ribbons along their strings (a Catmull-Rom curve, three points a segment),
 * a pixel step at a time, each step a short span across the ribbon that narrows and frills */
static void draw_arms(const jelly_tank_t *t, const jelly_t *j, const look_t *L) {
    const jsp_t *sp = &SP[j->species];
    int n = jelly_arms(j);
    if (n <= 0 || !j->arm_ready) return;
    const int N = JELLY_ARM_N;
    static const float B[3][4] = {
        { -0.0740741f, 0.7777778f, 0.3333333f, -0.0370370f },
        { -0.0370370f, 0.3333333f, 0.7777778f, -0.0740741f },
        { 0, 0, 1, 0 } };
    const float wmax = sp->arm_w * L->a;
    const uint32_t arm_s = col_spread(L->arm);
    for (int m = 0; m < n; m++) {
        const float *X = j->arm_x[m], *Y = j->arm_y[m];
        float lx = X[0], ly = Y[0], s_px = 0, carry = 0;   /* one slice a pixel of length, the remainder carried */
        for (int i = 0; i < N - 1; i++) {
            int i0 = i > 0 ? i - 1 : 0, i3 = i + 2 < N ? i + 2 : N - 1;
            for (int q = 0; q < 3; q++) {
                float x = X[i0] * B[q][0] + X[i] * B[q][1] + X[i + 1] * B[q][2] + X[i3] * B[q][3];
                float y = Y[i0] * B[q][0] + Y[i] * B[q][1] + Y[i + 1] * B[q][2] + Y[i3] * B[q][3];
                float ddx = x - lx, ddy = y - ly, m2 = ddx * ddx + ddy * ddy;
                if (m2 < 1e-6f) continue;
                float r = rsqrt_fast(m2), len = m2 * r;
                float dx = ddx * r, dy = ddy * r, nx = -dy, ny = dx;   /* along; across the ribbon */
                float dd = carry;
                for (; dd < len; dd += 1, s_px += 1) {
                    float w = dd / len, along = (i + (q + w) / 3.0f) / (N - 1);
                    float cx = lx + ddx * w, cy = ly + ddy * w;
                    float width = wmax * (1 - 0.55f * along) * (0.7f + 0.3f * fsin(s_px * 0.55f + t->clock * 2.0f + m));
                    float frill = width * 0.45f * fsin(s_px * 0.38f + t->clock * 1.7f + m * 2.0f);
                    int a = (int)(L->arm_a * (1 - 0.6f * along));
                    int nw = (int)(width + 0.5f);
                    for (int k = -nw / 2; k <= nw / 2; k++) {
                        float o = k + frill;
                        int ea = (k == -nw / 2 || k == nw / 2) ? a * 3 / 2 : a;   /* the frilled edge reads brighter */
                        pblend_s((int)(cx + nx * o), (int)(cy + ny * o), arm_s, ea);
                    }
                }
                carry = dd - len;
                lx = x; ly = y;
            }
        }
    }
}

/* integer mixes for the per-pixel work: w 0..256 */
static inline col_t imix(col_t a, col_t b, int w) {
    col_t c = { a.r + ((b.r - a.r) * w >> 8), a.g + ((b.g - a.g) * w >> 8), a.b + ((b.b - a.b) * w >> 8) }; return c; }

/* The bell, drawn row by row over only the pixels it covers (2026-10-18: the bells were the largest
 * stage on the glass, 10 ms of six on the 1.8, 16 on the pendant - a box of pixels each transformed,
 * tested, divided and square-rooted, many thrown away). Each row's span is solved from the dome's
 * ellipse and the line under the lappets; along a row the bell's own coordinates step by addition;
 * a fast inverse square root stands in for the root and the division; colors mix in integers. */
static void draw_bell(const jelly_tank_t *t, const jelly_t *j, const look_t *L) {
    const jsp_t *sp = &SP[j->species];
    const float a = L->a, h = L->h, ia = 1 / a, ih = 1 / h;
    const float lap = sp->lappet * JELLY_S * (1 + 0.6f * L->c);
    const float rib_n = (float)sp->n_rib;
    const float cav = 0.2f * h, edge_px = (a < h ? a : h) * 1.1f;
    const float Rx = L->Rx, Ry = L->Ry, Ux = L->Ux, Uy = L->Uy;
    const float lap_w = PI_F * sp->n_lap;
    /* the span: inside the ellipse (a + 2, h + 2) round the margin center, and above the lappets' foot */
    const float A = a + 2, H = h + 2, vmin = -lap - 1.5f;
    const float al_ = Rx * Rx / (A * A) + Ux * Ux / (H * H);
    const float be_ = Rx * Ry / (A * A) + Ux * Uy / (H * H);
    const float ga_ = Ry * Ry / (A * A) + Uy * Uy / (H * H);
    const float ext = (A > H ? A : H) + 1;
    int y0 = (int)(j->y - ext), y1 = (int)(j->y + ext) + 1;
    if (y0 < 0) y0 = 0;
    if (y1 > TANK_H - 1) y1 = TANK_H - 1;
    const col_t white = { 255, 255, 255 };
    const col_t dark = cscale(L->body, 0.45f), spot = cmix(L->neon, white, 0.55f);
    const int nettle = j->species == JSP_NETTLE, moon = j->species == JSP_MOON;
    float spx[6], spy[6]; int nsp = j->species == JSP_BLUBBER ? 6 : 0;   /* blubber spots, hashed per jelly */
    for (int k = 0; k < nsp; k++) {
        spx[k] = (hashf(j->seed + k * 31) - 0.5f) * 1.3f;
        spy[k] = 0.2f + 0.6f * hashf(j->seed + k * 57);
    }
    static const float RX[4] = { -0.50f, -0.17f, 0.17f, 0.50f }, RY[4] = { 0.36f, 0.50f, 0.50f, 0.36f };
    const int lap_a = L->rim_a * 3 / 4;
    for (int y = y0; y <= y1; y++) {
        float dy = y + 0.5f - j->y;
        /* the ellipse: al dx^2 + 2 be dy dx + (ga dy^2 - 1) <= 0 */
        float B = be_ * dy, C = ga_ * dy * dy - 1, disc = B * B - al_ * C;
        if (disc <= 0) continue;
        float r = sqrtf(disc), lo = (-B - r) / al_, hi = (-B + r) / al_;
        /* above the lappets' foot: dx Ux + dy Uy >= vmin */
        if (Ux > 1e-4f) { float m = (vmin - dy * Uy) / Ux; if (m > lo) lo = m; }
        else if (Ux < -1e-4f) { float m = (vmin - dy * Uy) / Ux; if (m < hi) hi = m; }
        else if (dy * Uy < vmin) continue;
        int xa = (int)floorf(j->x + lo), xb = (int)ceilf(j->x + hi);
        if (xa < g_sc->gx0[y]) xa = g_sc->gx0[y];
        if (xb > g_sc->gx1[y]) xb = g_sc->gx1[y];
        if (xa > xb) continue;
        float dx = xa + 0.5f - j->x;
        float u = dx * Rx + dy * Ry, v = dx * Ux + dy * Uy;
        uint16_t *p = &g_fb[y * g_stride + xa];
        for (int x = xa; x <= xb; x++, p++, u += Rx, v += Ux) {
            float un = u * ia;
            if (un < -1 || un > 1) continue;
            col_t c; int al;
            float un2 = un * un, mv = cav * (1 - un2);                        /* the underside is hollow */
            if (v < mv) {                                                     /* under the margin: the scalloped lappets */
                float depth = lap * (0.5f + 0.5f * fcos(un * lap_w));
                if (mv - v > depth) continue;
                c = L->rim; al = lap_a;
            } else {
                float vn = v * ih, q = un2 + vn * vn;
                if (q > 1) continue;
                float rs = q > 1e-6f ? rsqrt_fast(q) : 1000.0f, sq = q * rs;
                float e = q * q; e *= e;                                      /* the outline glows: q^4 */
                int we = (int)(e * 256);
                c = imix(L->body, L->rim, we);
                al = L->body_a + ((L->rim_a - L->body_a) * we >> 8);
                if (q > 0.05f && q < 0.92f) {                                 /* ribs (moon) or stripes (nettle) */
                    float rf = (un * rs + 1) * 0.5f * rib_n, dd = rf - (float)(int)(rf + 0.5f);
                    if (dd < 0) dd = -dd;
                    if (nettle) { if (dd < 0.17f) { c = imix(c, dark, 192); al += 50; } }
                    else if (dd < 0.07f) { c = imix(c, L->rim, 115); al += 26; }
                }
                if (moon && vn > 0.14f && vn < 0.72f)                         /* the four rings live in this band */
                    for (int k = 0; k < 4; k++) {
                        float ex = (un - RX[k]) * 6.25f, ey = (vn - RY[k]) * 4.7619f, e2 = ex * ex + ey * ey;
                        if (e2 < 1 && e2 > 0.3f) { c = imix(c, L->neon, 218); if (al < 190) al = 190; }
                    }
                for (int k = 0; k < nsp; k++) {
                    float ex = un - spx[k], ey = vn - spy[k];
                    if (ex * ex + ey * ey < 0.028f) { c = spot; al = 235; }
                }
                if (v - mv < 1.4f) { c = L->rim; al = L->rim_a; }            /* the margin's bright line */
                float cov = (1 - sq) * edge_px + 0.35f;                       /* the dome's edge, anti-aliased */
                if (cov < 1) al = (int)(al * (cov < 0 ? 0 : cov));
            }
            if (al < 4) continue;
            blend_s(p, col_spread(c), al > 255 ? 32 : (al + 4) >> 3);
        }
    }
    /* specks: little lights in the bell that come and go */
    col_t sc = cmix(L->neon, white, 0.6f);
    for (int k = 0; k < 8; k++) {
        float un = (hashf(j->seed + k * 13 + 1) - 0.5f) * 1.4f, vn = 0.12f + 0.7f * hashf(j->seed + k * 17 + 2);
        if (un * un + vn * vn > 0.8f) continue;
        float tw = 0.5f + 0.5f * fsin(t->clock * (1.6f + 0.3f * k) + k * 2.1f);
        int x = (int)(j->x + L->Rx * un * a + L->Ux * vn * h), y = (int)(j->y + L->Ry * un * a + L->Uy * vn * h);
        padd(x, y, sc, (int)(170 * tw * (L->night ? 1.3f : 1.0f)));
    }
}

static void draw_jelly(const jelly_tank_t *t, const jelly_t *j) {
    const jsp_t *sp = &SP[j->species];
    look_t L;
    L.night = !t->light_on;
    L.c = j->contract;
    L.a = jelly_bell_a(j) * (1 - 0.14f * L.c);
    L.h = bell_h(j) * (1 + 0.16f * L.c);
    L.Ux = sinf(j->heading); L.Uy = -cosf(j->heading);
    L.Rx = cosf(j->heading); L.Ry = sinf(j->heading);
    L.glow = clampf(0.30f + 0.30f * j->fed + 0.22f * L.c + j->flash, 0, 1.8f);
    float depth = 0.62f + 0.38f * j->z;                 /* far ones are fainter */
    col_t white = { 255, 255, 255 };
    col_t tint = t->light_on ? col(LED_RGB[t->light]) : white;
    L.neon = col(JELLY_NEON_RGB[j->neon]);
    switch (j->species) {
    case JSP_NETTLE:
        L.body = cmix(L.neon, white, 0.12f);
        L.rim = cmix(L.neon, white, 0.55f);
        L.tent = cmix(L.neon, white, 0.35f);
        L.arm = cmix(L.neon, white, 0.45f);
        break;
    case JSP_BLUBBER:
        L.body = cmix(L.neon, white, 0.18f);
        L.rim = cmix(L.neon, white, 0.6f);
        L.tent = L.rim;
        L.arm = cmix(L.neon, white, 0.35f);
        break;
    default:                                            /* the moon: see-through, the light's tint, a neon glow */
        L.body = cmix(cmix(L.neon, white, 0.55f), tint, 0.25f);
        L.rim = cmix(L.neon, white, 0.78f);
        L.tent = cmix(L.neon, white, 0.65f);
        L.arm = cmix(L.neon, white, 0.5f);
        break;
    }
    float lit = L.night ? 0.72f : 1.0f;
    L.body_a = (int)(sp->body_a * depth * lit);
    L.rim_a = (int)((sp->rim_a + 25 * L.c) * depth);
    L.tent_a = (int)((L.night ? 150 : 175) * depth);
    L.arm_a = (int)((j->species == JSP_MOON ? 120 : 150) * depth * lit);

    int64_t t0 = jelly_clock_us ? jelly_clock_us() : 0;
    float hx = j->x + L.Ux * L.h * 0.45f, hy = j->y + L.Uy * L.h * 0.45f;
    float hr = sp->halo * L.a * (0.9f + 0.25f * L.glow);
    float hi = (L.night ? 0.30f + 0.36f * L.glow : 0.13f + 0.17f * L.glow) * depth;
    halo(hx, hy, hr, L.neon, hi);
    int64_t t1 = jelly_clock_us ? jelly_clock_us() : 0;
    draw_tentacles(t, j, &L);
    draw_arms(t, j, &L);
    int64_t t2 = jelly_clock_us ? jelly_clock_us() : 0;
    draw_bell(t, j, &L);
    int64_t t3 = jelly_clock_us ? jelly_clock_us() : 0;
    jelly_prof_us[1] += t1 - t0; jelly_prof_us[2] += t2 - t1; jelly_prof_us[3] += t3 - t2;
}

/* ---- the jet's nozzle and fan -------------------------------------------------- */
/* (hypotf is slow in software on the S3: a pixel's distance here is d2 * rsqrt(d2), and only near an edge) */
static void disc(float cx, float cy, float r, uint32_t fg, int a) {     /* filled, anti-aliased edge */
    float in2 = r > 0.5f ? (r - 0.5f) * (r - 0.5f) : 0, out2 = (r + 0.5f) * (r + 0.5f);
    for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++)
        for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d2 = dx * dx + dy * dy;
            if (d2 >= out2) continue;
            if (d2 <= in2) { pblend_s(x, y, fg, a); continue; }
            float cov = r + 0.5f - d2 * rsqrt_fast(d2);
            if (cov > 0) pblend_s(x, y, fg, (int)(a * (cov > 1 ? 1 : cov)));
        }
}
static void ring(float cx, float cy, float r, float w, uint32_t fg, int a) {
    float ro = r + w * 0.5f + 0.5f, ri = r - w * 0.5f - 0.5f, ro2 = ro * ro, ri2 = ri > 0 ? ri * ri : 0;
    for (int y = (int)(cy - ro); y <= (int)(cy + ro); y++)
        for (int x = (int)(cx - ro); x <= (int)(cx + ro); x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d2 = dx * dx + dy * dy;
            if (d2 >= ro2 || d2 <= ri2) continue;
            float cov = w * 0.5f + 0.5f - fabsf(d2 * rsqrt_fast(d2) - r);
            if (cov > 0) pblend_s(x, y, fg, (int)(a * (cov > 1 ? 1 : cov)));
        }
}
static void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t fg, int a) {
    int bx0 = (int)fminf(x0, fminf(x1, x2)), bx1 = (int)fmaxf(x0, fmaxf(x1, x2)) + 1;
    int by0 = (int)fminf(y0, fminf(y1, y2)), by1 = (int)fmaxf(y0, fmaxf(y1, y2)) + 1;
    float ar = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (fabsf(ar) < 1e-3f) return;
    for (int y = by0; y <= by1; y++)
        for (int x = bx0; x <= bx1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / ar;
            float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / ar;
            if (w0 >= 0 && w1 >= 0 && w0 + w1 <= 1) pblend_s(x, y, fg, a);
        }
}
/* the nozzle: a dark hub on the glass with a lit lip and a short spout pointing where the jet pushes */
static void draw_nozzle(const jelly_tank_t *t, col_t led) {
    const float S = JELLY_S;
    const col_t white = { 255, 255, 255 };
    float nx, ny; nozzle_pos(&nx, &ny);
    float on = t->jet == JET_OFF && t->jet_level < 0.05f ? 0.55f : 1.0f;   /* a still jet is dimmer */
    uint32_t body = col_spread((col_t){ 40, 44, 58 }), lip = col_spread(cmix(led, white, 0.45f));
    float dx = cosf(t->jet_stub), dy = sinf(t->jet_stub);
    for (int i = 0; i <= 4; i++) disc(nx + dx * i * 2 * S, ny + dy * i * 2 * S, 2.2f * S, body, (int)(235 * on));
    disc(nx + dx * 8.5f * S, ny + dy * 8.5f * S, 1.6f * S, lip, (int)(200 * on));
    disc(nx, ny, 5.0f * S, body, (int)(240 * on));
    ring(nx, ny, 4.6f * S, 1.2f * S, lip, (int)(170 * on));
}
/* the fan: five arrows round the nozzle, the chosen one bigger, filled brighter and ringed thicker (never
 * told by hue alone); the hub shows the pace as dots - none OFF, one GENTLE, two STEADY */
static void draw_fan(const jelly_tank_t *t, col_t led) {
    const float S = JELLY_S;
    const col_t white = { 255, 255, 255 };
    float f = clampf(t->ui_t / 0.2f, 0, 1);
    float nx, ny, ax[JET_AIMS], ay[JET_AIMS], cr;
    jelly_jet_geom(t, &nx, &ny, ax, ay, &cr);
    uint32_t dark = col_spread((col_t){ 14, 18, 34 }), lite = col_spread(cmix(led, white, 0.75f)), wht = col_spread(white);
    for (int k = 0; k < JET_AIMS; k++) {
        bool sel = k == t->jet_aim, hot = k == t->ui_hot;
        float r = cr * (sel ? 1.15f : 1.0f) * (hot ? 1.2f : 1.0f);
        disc(ax[k], ay[k], r, dark, (int)(200 * f));
        float pulse = 1.0f;
        ring(ax[k], ay[k], r, (sel ? 2.4f : 1.3f) * S, sel || hot ? wht : lite, (int)((sel || hot ? 240 : 150) * f * pulse));
        /* an arrow, not a wedge: a slim head on a shaft (a wide triangle read as pointing sideways at this size) */
        float dx, dy; aim_dir(k, nx, ny, &dx, &dy);
        float px = -dy, py = dx, tip = r * 0.66f, neck = r * 0.02f, hw = r * 0.40f;
        uint32_t ink = sel ? wht : lite; int ia = (int)((sel ? 250 : 150) * f);
        tri(ax[k] + dx * tip, ay[k] + dy * tip, ax[k] + dx * neck + px * hw, ay[k] + dy * neck + py * hw,
            ax[k] + dx * neck - px * hw, ay[k] + dy * neck - py * hw, ink, ia);
        for (float sh = -r * 0.60f; sh <= neck; sh += 0.7f)
            disc(ax[k] + dx * sh, ay[k] + dy * sh, r * 0.10f + 0.35f, ink, ia);
    }
    bool hub = t->ui_hot == JET_AIMS;
    ring(nx, ny, cr * (hub ? 1.25f : 1.1f), 1.3f * S, hub ? wht : lite, (int)((hub ? 230 : 140) * f));
    float cx = FLOW_CX - nx, cy = FLOW_CY - ny, cm = sqrtf(cx * cx + cy * cy); cx /= cm; cy /= cm;
    for (int d = 0; d < t->jet; d++) {                  /* the pace: dots toward the tank's middle, inside the fan */
        float o = (t->jet == 2 ? (d == 0 ? -1 : 1) : 0) * 3.5f * S;
        disc(nx + cx * cr * 1.75f - cy * o, ny + cy * cr * 1.75f + cx * o, 2.0f * S, wht, (int)(230 * f));
    }
}

void jelly_render(jelly_tank_t *t, uint16_t *fb, int stride) {
    scratch();
    g_fb = fb; g_stride = stride;
    int64_t t0 = jelly_clock_us ? jelly_clock_us() : 0;
    int key = bg_key(t);
    bool primed = g_primed == fb && g_bg && key == g_bg_key && stride == TANK_W;   /* the DMA already copied it */
    g_primed = NULL;
    if (!primed && g_bg) {
        if (key != g_bg_key) { draw_background(t, g_bg, TANK_W); g_bg_key = key; }
        if (stride == TANK_W) memcpy(fb, g_bg, sizeof(uint16_t) * TANK_W * TANK_H);
        else for (int y = 0; y < TANK_H; y++) memcpy(fb + y * stride, g_bg + y * TANK_W, sizeof(uint16_t) * TANK_W);
    } else if (!primed) draw_background(t, fb, stride);
    int64_t t1 = jelly_clock_us ? jelly_clock_us() : 0;
    jelly_prof_us[0] += t1 - t0;

    const float S = JELLY_S;
    col_t white = { 255, 255, 255 };
    col_t led = t->light_on ? col(LED_RGB[t->light]) : (col_t){ 150, 170, 255 };

    /* motes behind everything */
    draw_nozzle(t, led);
    for (int i = 0; i < JELLY_BUBBLES; i++) {
        const jelly_mote_t *b = &t->bubble[i];
        if (!b->live) continue;
        int a = (int)(110 * fminf(1, b->life / 2));
        padd((int)b->x, (int)b->y, cmix(led, white, 0.6f), a);
    }
    col_t brine = t->light_on ? (col_t){ 255, 190, 140 } : (col_t){ 120, 90, 70 };
    for (int i = 0; i < JELLY_SPECKS; i++) {
        const jelly_mote_t *m = &t->speck[i];
        if (!m->live) continue;
        pblend((int)m->x, (int)m->y, brine, 220);
        pblend((int)m->x + 1, (int)m->y, brine, 120);
    }
    int64_t t2 = jelly_clock_us ? jelly_clock_us() : 0;

    /* the jellies, far to near */
    int order[JELLY_MAX];
    for (int i = 0; i < t->n; i++) order[i] = i;
    for (int i = 1; i < t->n; i++)
        for (int k = i; k > 0 && t->j[order[k]].z < t->j[order[k - 1]].z; k--) { int s = order[k]; order[k] = order[k - 1]; order[k - 1] = s; }
    for (int i = 0; i < t->n; i++) draw_jelly(t, &t->j[order[i]]);

    int64_t t3 = jelly_clock_us ? jelly_clock_us() : 0;
    /* a tap's ripple, a finger's light */
    for (int i = 0; i < JELLY_RIPPLES; i++) {
        const jelly_ripple_t *r = &t->ripple[i];
        if (!r->live) continue;
        float rad = 0.36f * JELLY_SHORT * (r->t / 1.3f) * (2 - r->t / 1.3f);
        int a = (int)(90 * (1 - r->t / 1.3f));
        int n = (int)(TAU_F * rad) + 8;
        for (int k = 0; k < n; k++) {
            float ang = TAU_F * k / n;
            padd((int)(r->x + fcos(ang) * rad), (int)(r->y + fsin(ang) * rad), cmix(led, white, 0.5f), a);
        }
    }
    if (t->holding) halo(t->hold_x, t->hold_y, 22 * S, cmix(led, white, 0.55f), 0.35f * fminf(1, t->hold_t * 3));
    if (t->ui_open) draw_fan(t, led);
    int64_t t4 = jelly_clock_us ? jelly_clock_us() : 0;
    jelly_prof_us[4] += (t2 - t1) + (t4 - t3);
}
