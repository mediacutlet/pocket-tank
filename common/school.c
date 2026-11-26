/* school.c - THE SCHOOL TANK, an experiment (2026-11-01). See school.h and
 * docs/school-tank.md.
 *
 * Each tick, each fish:
 *   1. finds its SCHOOL_K nearest neighbors (a grid of 2-BL cells, searched
 *      outward ring by ring until the K nearest are certain or 6 BL is
 *      reached - a dense school costs no more than a loose one);
 *   2. steers by them: keep off the very close ones (separation), match
 *      their velocity (alignment), drift to their middle (cohesion) - the
 *      ones ahead of it count more than the ones behind (a fish's eyes);
 *   3. adds its own goals: the glass ahead, a crumb it can see, a scout's
 *      spot, a held finger, its wander, the crowd (a blurred head count of
 *      the whole tank it climbs - so small groups find the big one);
 *   4. copies a fright: a neighbor dashing away makes it dash the same way a
 *      beat later, weaker by a hop, so a tap sends a wave through the school
 *      and splits it where the wave runs out of strength.
 * The new velocities are all computed from the last tick's state, then
 * committed together: no fish sees another's future.
 */
#include "school.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

int64_t (*school_clock_us)(void);
int64_t  school_prof_us[6];
#ifdef SCHOOL_STATS
long school_stat[4];                       /* counters for tuning: candidates, rings, fish pixels, fish rows */
#endif
#define PROF_T0(v) int64_t v = school_clock_us ? school_clock_us() : 0
#define PROF_ADD(k, v) do { if (school_clock_us) school_prof_us[k] += school_clock_us() - (v); } while (0)

#define TAU_F 6.2831853f
#define SCHOOL_CLOCK_LAP 62831.853f
#define S     SCHOOL_S
#define BL0   (10.0f * S)                  /* a body length on this glass, px */
#define DEPTH (2.5f * BL0)                 /* the depth the school uses: a shallow slab, so fish stack ~2 deep (more fish = a bigger school, not a denser blob) */
#define CELL  ((20 * SCHOOL_SHORT + 184) / 368)   /* the neighbor grid: 2 BL (integer: it sizes arrays) */
#define GW    (TANK_W / CELL + 1)
#define GH    (TANK_H / CELL + 1)
#define FCELL ((40 * SCHOOL_SHORT + 184) / 368)   /* the crowd field: 4 BL */
#define FW    (TANK_W / FCELL + 1)
#define FH    (TANK_H / FCELL + 1)
#define SURFACE_Y (TANK_H * 0.07f)         /* the water's top: fish keep under it */
#define SAND_Y    ((float)TANK_BOT - 18.0f * S)
#ifndef SCHOOL_STEER_EVERY
#define SCHOOL_STEER_EVERY 2               /* ticks between a fish's decisions (1 = every tick) */
#endif

/* the piece (driftwood with Christmas moss and a Windelov java fern, 2026-11-01): drawn by code once per glass, in
   the lower part of the glass; it stands at PIECE_Z in the school's depth - a nearer fish passes in front of it, a
   farther one behind it */
#define PIECE_H   ((185 * SCHOOL_SHORT + 184) / 368 + 8)   /* the rows it may use, up from the glass's bottom */
#define PIECE_Y0  (TANK_BOT - PIECE_H)
#define PIECE_Z   (0.5f * DEPTH)
#define PIECE_TH  (0.2f * DEPTH)            /* its thickness either side of PIECE_Z: the fish keep out of it */

/* ---- the scratch: grid, field, tables ------------------------------------ */
typedef struct {
    uint16_t cell_start[GW * GH + 1];
    uint16_t order[SCHOOL_MAX];            /* fish indexes sorted by cell */
    float    pos[SCHOOL_MAX][4];          /* last tick's x, y, z (and a pad), in cell order: one load base for the scan */
    float    vx[SCHOOL_MAX], vy[SCHOOL_MAX], vz[SCHOOL_MAX];
    float    burst[SCHOOL_MAX], fear[SCHOOL_MAX];
    float    nvx[SCHOOL_MAX], nvy[SCHOOL_MAX], nvz[SCHOOL_MAX];   /* this tick's answers, by fish index */
    float    field[FW * FH], tmp[FW * FH];
    float    raw[FW * FH];                 /* the 4-BL head count, unblurred */
    float    seen_x[FW * FH], seen_y[FW * FH];   /* the other fish seen 8..20 BL off: their pull, per cell */
    float    nearf[GW * GH], ntmp[GW * GH];
    float    near_gx[GW * GH], near_gy[GW * GH];      /* the school's-body pull per 2-BL cell (direction x strength) */
    float    crowd_gx[FW * FH], crowd_gy[FW * FH];    /* the crowd's pull per 4-BL cell (a direction) */
    float    small[FW * FH];                          /* 0 in the biggest crowd .. 1 alone */
    int8_t   ring_dx[128], ring_dy[128];              /* the "seen" ring's cell offsets (8..20 BL) */
    float    sinlut[256];
    int32_t  bodyq[65], bodykq[65];                   /* the minnow's half-height along its body (16.16), the shade's step over it (24.8) */
    float    ring_ux[128], ring_uy[128];
    int      ring_n;
    float    field_max;                    /* the biggest crowd's blurred head count this tick */  /* the 2-BL grid's head count, blurred: the school's own body */
    uint16_t zorder[SCHOOL_MAX];           /* far to near, kept between frames (nearly sorted: insertion sort) */
    int      zn;
    int16_t  gx0[TANK_H], gx1[TANK_H];     /* the glass's inside per row */
    uint16_t cell_of[SCHOOL_MAX], slot_of[SCHOOL_MAX];
    uint16_t uf[SCHOOL_MAX], ucnt[SCHOOL_MAX];   /* school_shape */
    float    nnd[SCHOOL_MAX];
    uint32_t prgba[TANK_W * PIECE_H];      /* the piece while it is drawn: r, g, b, coverage (8 bits each) */
    uint8_t  pa5[TANK_W * PIECE_H];        /* its coverage, 0..32: what the frame puts back over a fish behind it */
    int16_t  px0[PIECE_H], px1[PIECE_H];   /* its extent per row (px1 < px0: none) */
    int16_t  pd0[PIECE_H], pd1[PIECE_H];   /* this frame's fish drawn behind it, per row */
    uint8_t  solid[GW * GH];               /* how much of each 2-BL cell the piece fills, 0..255 (the fish keep out) */
    uint16_t water_row[TANK_H];            /* the water's color per row (565): a fish over the piece fades into water, not wood */
    bool     ready;
} school_scratch_t;

static school_scratch_t *g_sc;
static uint16_t *g_bg;
static int g_bg_key = -1;

size_t school_scratch_bytes(void) { return sizeof(school_scratch_t); }
void school_set_scratch(void *buf) { g_sc = (school_scratch_t *)buf; if (g_sc) g_sc->ready = false; }
void school_set_bg_cache(uint16_t *buf) { g_bg = buf; g_bg_key = -1; }

/* ---- the glass ----------------------------------------------------------- */
float school_glass_dist(float x, float y) {
#ifdef TANK_ROUND
    float dx = x - TANK_RAD, dy = y - TANK_RAD;
    return TANK_RAD - sqrtf(dx * dx + dy * dy);
#elif defined(TANK_CORNER_R)
    const float R = TANK_CORNER_R;
    float qx = fabsf(x - TANK_W * 0.5f) - (TANK_W * 0.5f - R), qy = fabsf(y - TANK_H * 0.5f) - (TANK_H * 0.5f - R);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, in = qx > qy ? qx : qy;
    return -(sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - R);
#else
    float d = x; if (TANK_W - x < d) d = TANK_W - x; if (y < d) d = y; if (TANK_H - y < d) d = TANK_H - y;
    return d;
#endif
}
/* where a fish may be: inside the glass, under the surface, over the sand */
static float swim_dist(float x, float y) {
    float d = school_glass_dist(x, y);
    if (y - SURFACE_Y < d) d = y - SURFACE_Y;
    if (SAND_Y - y < d) d = SAND_Y - y;
    return d;
}

/* the body's half-height along its length, 0 = the tail's root .. 1 = the snout, in body lengths */
static float body_half(float u) {
    /* a minnow: a slim spindle, deepest a little ahead of the middle, a narrow wrist before the tail */
    if (u <= 0) return 0.035f;
    if (u >= 1) return 0;
    float a = u < 0.58f ? u / 0.58f : (1 - u) / 0.42f;
    float h = u < 0.58f ? 0.035f + 0.075f * (1 - (1 - a) * (1 - a)) : 0.11f * sqrtf(a);
    return h;
}
static void piece_build(school_scratch_t *g);
static school_scratch_t *scratch(void) {
    if (!g_sc) g_sc = (school_scratch_t *)calloc(1, sizeof *g_sc);   /* the sim; the firmware hands one in from PSRAM */
    if (!g_sc->ready) {
        for (int y = 0; y < TANK_H; y++) {
            int x0 = TANK_W, x1 = -1;
            for (int x = 0; x < TANK_W; x++)
                if (school_glass_dist(x + 0.5f, y + 0.5f) >= 0) { if (x < x0) x0 = x; x1 = x; }
            g_sc->gx0[y] = (int16_t)x0; g_sc->gx1[y] = (int16_t)x1;
        }
        g_sc->zn = 0;
        g_sc->ring_n = 0;
        for (int dy = -5; dy <= 5; dy++)
            for (int dx = -5; dx <= 5; dx++) {
                int d2 = dx * dx + dy * dy;
                if (d2 < 4 || d2 > 25) continue;
                int k = g_sc->ring_n++;
                float m = 1 / sqrtf((float)d2);
                g_sc->ring_dx[k] = (int8_t)dx; g_sc->ring_dy[k] = (int8_t)dy; g_sc->ring_ux[k] = dx * m; g_sc->ring_uy[k] = dy * m;
            }
        for (int k = 0; k < 256; k++) g_sc->sinlut[k] = sinf(k * (TAU_F / 256));
        for (int k = 0; k <= 64; k++) {
            float h = body_half(k / 64.0f), bk = 3.5f / (h + 1e-4f) * 256.0f;
            g_sc->bodyq[k] = (int32_t)(h * 65536.0f);
            g_sc->bodykq[k] = (int32_t)(bk > 30000 ? 30000 : bk);   /* capped: no overflow at the snout */
        }
        piece_build(g_sc);
        g_sc->ready = true;
    }
    return g_sc;
}

/* ---- small helpers --------------------------------------------------------- */
static inline uint32_t rnd(school_t *s) { uint32_t x = s->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return s->rng = x; }
static inline float frand(school_t *s) { return (rnd(s) >> 8) * (1.0f / 16777216.0f); }
static inline float frange(school_t *s, float a, float b) { return a + (b - a) * frand(s); }
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float inv_sqrt(float x) {                     /* one Newton step: ~0.2% - plenty for steering */
    union { float f; uint32_t i; } u = { x };
    u.i = 0x5f3759dfu - (u.i >> 1);
    return u.f * (1.5f - 0.5f * x * u.f * u.f);
}
static inline float approach(float v, float target, float rate_dt) { return v + (target - v) * (rate_dt > 1 ? 1 : rate_dt); }

/* a random point of open water at least m px from every edge */
static void open_water(school_t *s, float m, float *x, float *y) {
    for (int k = 0; k < 40; k++) {
        *x = frange(s, 0, TANK_W); *y = frange(s, SURFACE_Y, SAND_Y);
        if (swim_dist(*x, *y) >= m) return;
    }
    *x = TANK_W * 0.5f; *y = (SURFACE_Y + SAND_Y) * 0.5f;
}

static void fish_spawn(school_t *s, sfish_t *f, float cx, float cy, float spread) {
    memset(f, 0, sizeof *f);
    float a = frange(s, 0, TAU_F), r = spread * sqrtf(frand(s));
    f->x = cx + cosf(a) * r; f->y = cy + sinf(a) * r * 0.6f;
    if (swim_dist(f->x, f->y) < 2 * BL0) { f->x = cx; f->y = cy; }
    f->z = frange(s, 0.2f, 0.8f) * DEPTH;
    f->size = BL0 * frange(s, 0.86f, 1.12f);
    f->cruise = BL0 * frange(s, 2.45f, 2.75f);
    float h = frange(s, -0.4f, 0.4f) + (frand(s) < 0.5f ? 0 : 3.14159f);
    f->vx = cosf(h) * f->cruise; f->vy = sinf(h) * f->cruise * 0.4f;
    f->phase = frand(s);
    f->appetite = frange(s, 0.6f, 1.0f);
    f->wander = frange(s, -1, 1);
    f->zbias = frange(s, 0.25f, 0.75f) * DEPTH;
    f->hue = (uint8_t)(rnd(s) & 7);
}

void school_init(school_t *s, uint32_t seed, int n) {
    memset(s, 0, sizeof *s);
    s->rng = seed ? seed : 1;
    s->light_on = true;
    s->body_bl = BL0;
    s->w_align = 1.0f; s->w_cohere = 1.0f; s->w_separate = 1.0f; s->w_gather = 1.0f; s->w_wall = 1.0f;
    s->scout_t = 6;
    scratch();
    school_set_count(s, n);
}

void school_set_count(school_t *s, int n) {
    if (n > SCHOOL_MAX) n = SCHOOL_MAX;
    if (n < 0) n = 0;
    /* newcomers arrive near the crowd's middle (or the middle of the water) */
    float cx = 0, cy = 0;
    for (int i = 0; i < s->n; i++) { cx += s->f[i].x; cy += s->f[i].y; }
    if (s->n) { cx /= s->n; cy /= s->n; } else open_water(s, 8 * BL0, &cx, &cy);
    for (int i = s->n; i < n; i++) fish_spawn(s, &s->f[i], cx, cy, BL0 * (3 + sqrtf((float)n) * 0.55f));
    s->n = n;
    scratch()->zn = 0;                                     /* the depth order is rebuilt */
}

/* ---- gestures ----------------------------------------------------------------- */
#define R_TAP   (5.0f * BL0)        /* a tap frightens the fish this near it outright */
#define R_HEARD (11.0f * BL0)       /* and puts these on edge */

static void startle(sfish_t *f, float dx, float dy, float strength) {
    float m = dx * dx + dy * dy;
    if (m < 1e-6f) { dx = 1; dy = 0; m = 1; }
    m = inv_sqrt(m);
    dx *= m; dy *= m;
    float ahead = 4.0f * BL0;
    if (swim_dist(f->x + dx * ahead, f->y + dy * ahead) < 1.5f * BL0) {      /* the glass that way: dash along it */
        float ldx = -dy, ldy = dx;                                           /* the two ways along */
        float l = swim_dist(f->x + ldx * ahead, f->y + ldy * ahead), r = swim_dist(f->x - ldx * ahead, f->y - ldy * ahead);
        if (r > l) { ldx = -ldx; ldy = -ldy; }
        dx = 0.25f * dx + ldx; dy = 0.25f * dy + ldy;
        m = inv_sqrt(dx * dx + dy * dy); dx *= m; dy *= m;
    }
    f->sx = dx; f->sy = dy;
    f->startle_in = 0;
    f->burst = strength;
    if (f->fear < strength) f->fear = strength;
    f->refractory = 1.1f;
    f->goal_t = 0;
    /* the dash itself: velocity snaps toward the escape line (a C-start is ~1/20 s) */
    float sp = f->cruise + strength * (11.0f * BL0 - f->cruise);
    f->vx = f->sx * sp; f->vy = f->sy * sp; f->vz *= 0.5f;
}

void school_tap(school_t *s, float x, float y) {
    for (int i = 0; i < SCHOOL_RIPPLES; i++)
        if (!s->r[i].live) { s->r[i] = (sripple_t){ x, y, 0, true }; break; }
    for (int i = 0; i < s->n; i++) {
        sfish_t *f = &s->f[i];
        float dx = f->x - x, dy = f->y - y, d = sqrtf(dx * dx + dy * dy);
        if (d < R_TAP && f->refractory <= 0) {
            /* away from the finger, with a little scatter of its own */
            float a = frange(s, -0.45f, 0.45f), c = cosf(a), sn = sinf(a);
            startle(f, dx * c - dy * sn, dx * sn + dy * c, 1.0f - 0.25f * d / R_TAP);
            s->startles++;
        } else if (d < R_HEARD) {
            float e = 0.45f * (1 - d / R_HEARD);
            if (f->fear < e) f->fear = e;
        }
    }
}

void school_feed(school_t *s, float x) {
    int k = 0, want = 10 + (int)(rnd(s) % 5);
    for (int i = 0; i < SCHOOL_PELLETS && k < want; i++) {
        if (s->p[i].live) continue;
        spellet_t *p = &s->p[i];
        p->live = true; p->rest = 0;
        p->x = clampf(x + frange(s, -3.5f, 3.5f) * BL0, 4 * BL0, TANK_W - 4 * BL0);
        p->y = SURFACE_Y + frange(s, -2, 6) * S;
        p->z = frange(s, 0.15f, 0.85f) * DEPTH;
        p->vy = 0;
        /* curved glass (the bowl, the rounded corners): the water's top at this x is lower - start there */
        while (school_glass_dist(p->x, p->y) < 1.5f * BL0 && p->y < SAND_Y * 0.5f) p->y += 2;
        if (school_glass_dist(p->x, p->y) < 1.5f * BL0) p->live = false;
        else k++;
    }
}

void school_hold(school_t *s, float x, float y, bool down) {
    if (down && !s->holding) {
        s->hold_t = 0;
        /* which way round: the way the school near the finger is already turning about it */
        float L = 0;
        for (int i = 0; i < s->n; i++) { const sfish_t *f = &s->f[i]; L += (f->x - x) * f->vy - (f->y - y) * f->vx; }
        s->hold_dir = L >= 0 ? -1.0f : 1.0f;
    }
    s->holding = down; s->hold_x = x; s->hold_y = y;
}

void school_drag(school_t *s, float x, float y, float vx, float vy, bool down) {
    s->dragging = down; s->drag_x = x; s->drag_y = y; s->drag_vx = vx; s->drag_vy = vy;
    if (down && s->holding) school_hold(s, x, y, false);              /* a held finger that starts to move is a drag */
}

/* raw touch: a quick tap in the top band feeds, elsewhere it frightens; a still hold
   draws the school in (it grows curious about the finger); a finger that moves more than its
   own width is DRAGGED through the water (2026-11-01, Strato: "as if the user is dragging their
   finger through the school") */
void school_touch(school_t *s, float x, float y, bool down, double now) {
    static bool was; static double t_down, t_last; static float x0, y0, lx, ly, svx, svy; static bool moved;
    if (down && !was) { t_down = t_last = now; x0 = lx = x; y0 = ly = y; svx = svy = 0; moved = false; }
    if (down) {
        if ((x - x0) * (x - x0) + (y - y0) * (y - y0) > 144 * S * S) moved = true;
        double dts = now - t_last;
        if (dts > 0.004) {                                            /* the finger's velocity, smoothed over ~60 ms */
            float k = (float)(dts / (0.06 + dts));
            svx += ((x - lx) / (float)dts - svx) * k; svy += ((y - ly) / (float)dts - svy) * k;
            lx = x; ly = y; t_last = now;
        }
        if (moved) school_drag(s, x, y, svx, svy, true);
        else if (now - t_down > 0.45) school_hold(s, x, y, true);
    }
    if (!down && was) {
        if (!s->holding && !moved && now - t_down < 0.45) {
            if (y0 < SURFACE_Y + TANK_H * 0.08f) school_feed(s, x0);
            else school_tap(s, x0, y0);
        }
        school_hold(s, x, y, false);
        school_drag(s, x, y, 0, 0, false);
    }
    was = down;
}

/* ---- the crowd field: a blurred head count, climbed by fish that want company ---- */
static void blur(float *f, float *tmp) {
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++) {
            float l = x > 0 ? f[y * FW + x - 1] : 0, r = x < FW - 1 ? f[y * FW + x + 1] : 0;   /* zero past the glass: */
            tmp[y * FW + x] = 0.25f * l + 0.5f * f[y * FW + x] + 0.25f * r;
        }
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++) {
            float u = y > 0 ? tmp[(y - 1) * FW + x] : 0, d = y < FH - 1 ? tmp[(y + 1) * FW + x] : 0;   /* a wall never looks crowded */
            f[y * FW + x] = 0.25f * u + 0.5f * tmp[y * FW + x] + 0.25f * d;
        }
}

/* ---- the tick ------------------------------------------------------------------- */
static void build_grid(school_t *s, school_scratch_t *g) {
    uint16_t *cell_of = g->cell_of;
    uint16_t count[GW * GH];
    memset(count, 0, sizeof count);
    for (int i = 0; i < s->n; i++) {
        int cx = (int)(s->f[i].x / CELL), cy = (int)(s->f[i].y / CELL);
        cx = cx < 0 ? 0 : cx >= GW ? GW - 1 : cx; cy = cy < 0 ? 0 : cy >= GH ? GH - 1 : cy;
        cell_of[i] = (uint16_t)(cy * GW + cx);
        count[cell_of[i]]++;
    }
    int acc = 0;
    for (int c = 0; c < GW * GH; c++) { g->cell_start[c] = (uint16_t)acc; acc += count[c]; }
    g->cell_start[GW * GH] = (uint16_t)acc;
    memset(count, 0, sizeof count);
    for (int i = 0; i < s->n; i++) {
        int c = cell_of[i], k = g->cell_start[c] + count[c]++;
        const sfish_t *f = &s->f[i];
        g->order[k] = (uint16_t)i;
        g->pos[k][0] = f->x; g->pos[k][1] = f->y; g->pos[k][2] = f->z;
        g->slot_of[i] = (uint16_t)k;
        g->vx[k] = f->vx; g->vy[k] = f->vy; g->vz[k] = f->vz;
        g->burst[k] = f->burst; g->fear[k] = f->fear;
    }
    /* the near crowd: the grid's own counts, blurred twice (~4 BL): pulls a string of fish into a body */
    for (int c = 0; c < GW * GH; c++) g->nearf[c] = (float)(g->cell_start[c + 1] - g->cell_start[c]);
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < GH; y++)
            for (int x = 0; x < GW; x++) {
                const float *f = g->nearf;
                g->ntmp[y * GW + x] = 0.5f * f[y * GW + x] + 0.25f * ((x > 0 ? f[y * GW + x - 1] : 0) + (x < GW - 1 ? f[y * GW + x + 1] : 0));
            }
        for (int y = 0; y < GH; y++)
            for (int x = 0; x < GW; x++) {
                const float *f = g->ntmp;
                g->nearf[y * GW + x] = 0.5f * f[y * GW + x] + 0.25f * ((y > 0 ? f[(y - 1) * GW + x] : 0) + (y < GH - 1 ? f[(y + 1) * GW + x] : 0));
            }
    }
    /* its pull per cell: the gradient's direction, saturating in its strength */
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            const float *f = g->nearf;
            float gx = 0.5f * ((x < GW - 1 ? f[y * GW + x + 1] : 0) - (x > 0 ? f[y * GW + x - 1] : 0));
            float gy = 0.5f * ((y < GH - 1 ? f[(y + 1) * GW + x] : 0) - (y > 0 ? f[(y - 1) * GW + x] : 0));
            float m2 = gx * gx + gy * gy, k = m2 > 1e-8f ? inv_sqrt(m2 + 1.0f) : 0;   /* |g| / sqrt(|g|^2 + 1) along g */
            g->near_gx[y * GW + x] = gx * k; g->near_gy[y * GW + x] = gy * k;
        }
    /* the crowd: counts per 4-BL cell, blurred twice (near: ~12 BL) and again (far: the whole tank) */
    memset(g->field, 0, sizeof g->field);
    for (int i = 0; i < s->n; i++) {
        int fx = (int)(s->f[i].x / FCELL), fy = (int)(s->f[i].y / FCELL);
        fx = fx < 0 ? 0 : fx >= FW ? FW - 1 : fx; fy = fy < 0 ? 0 : fy >= FH ? FH - 1 : fy;
        g->field[fy * FW + fx] += 1;
    }
    memcpy(g->raw, g->field, sizeof g->field);
    blur(g->field, g->tmp); blur(g->field, g->tmp);
    g->field_max = 1e-3f;
    for (int c = 0; c < FW * FH; c++) if (g->field[c] > g->field_max) g->field_max = g->field[c];
    /* the crowd's pull per cell: which way the head count climbs; and how small this crowd is */
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++) {
            const float *f = g->field;
            float gx = (x < FW - 1 ? f[y * FW + x + 1] : 0) - (x > 0 ? f[y * FW + x - 1] : 0);
            float gy = (y < FH - 1 ? f[(y + 1) * FW + x] : 0) - (y > 0 ? f[(y - 1) * FW + x] : 0);
            float m2 = gx * gx + gy * gy, k = m2 > 1e-8f ? inv_sqrt(m2) : 0;
            g->crowd_gx[y * FW + x] = gx * k; g->crowd_gy[y * FW + x] = gy * k;
            g->small[y * FW + x] = 1 - f[y * FW + x] / g->field_max;
        }
    /* what a fish sees across the water: every fish 8..20 BL off (a ring - its own crowd is inside the
       hole) pulls toward itself. Two schools of the same size see each other this way and meet; one school
       barely notices (its far edge pulls its near edge in). Scattered from the few cells that hold fish. */
    memset(g->seen_x, 0, sizeof g->seen_x); memset(g->seen_y, 0, sizeof g->seen_y);
    for (int sy = 0; sy < FH; sy++)
        for (int sx = 0; sx < FW; sx++) {
            float c = g->raw[sy * FW + sx];
            if (c == 0) continue;
            for (int k = 0; k < g->ring_n; k++) {
                int tx = sx - g->ring_dx[k], ty = sy - g->ring_dy[k];      /* the cell that sees this one at offset k */
                if ((unsigned)tx >= FW || (unsigned)ty >= FH) continue;
                g->seen_x[ty * FW + tx] += c * g->ring_ux[k]; g->seen_y[ty * FW + tx] += c * g->ring_uy[k];
            }
        }
    /* stored as a direction x how much is seen (20 fish = all of it) */
    for (int c = 0; c < FW * FH; c++) {
        float sx = g->seen_x[c], sy = g->seen_y[c], m2 = sx * sx + sy * sy;
        if (m2 < 1e-8f) { g->seen_x[c] = g->seen_y[c] = 0; continue; }
        float im = inv_sqrt(m2), k = im * clampf(m2 * im / 20, 0, 1);
        g->seen_x[c] = sx * k; g->seen_y[c] = sy * k;
    }
}

/* one set of bilinear weights for several per-cell arrays */
typedef struct { int a, b, c, d; float wa, wb, wc, wd; } bilin_t;
static inline bilin_t bilin(int w, int h, float cell, float x, float y) {
    float fx = x / cell - 0.5f, fy = y / cell - 0.5f;
    int ix = (int)floorf(fx), iy = (int)floorf(fy);
    float ax = fx - ix, ay = fy - iy;
    int x0 = ix < 0 ? 0 : ix >= w ? w - 1 : ix, x1 = ix + 1 < 0 ? 0 : ix + 1 >= w ? w - 1 : ix + 1;
    int y0 = iy < 0 ? 0 : iy >= h ? h - 1 : iy, y1 = iy + 1 < 0 ? 0 : iy + 1 >= h ? h - 1 : iy + 1;
    return (bilin_t){ y0 * w + x0, y0 * w + x1, y1 * w + x0, y1 * w + x1, (1 - ax) * (1 - ay), ax * (1 - ay), (1 - ax) * ay, ax * ay };
}
static inline float bl_at(const float *f, bilin_t b) { return f[b.a] * b.wa + f[b.b] * b.wb + f[b.c] * b.wc + f[b.d] * b.wd; }

typedef struct { float d2; int k; } nb_t;

/* the K nearest to (x, y, z): the block of cells round its own, one more ring at a time until the Kth is
   certain. In cell order a row of the block is one run of the arrays (one loop, not one per cell); the Kth's
   distance stays in a register, and most candidates are turned away on x and y alone. Its own function: in
   the big tick the loop ran out of registers */
static __attribute__((noinline)) int scan_knn(const school_scratch_t *g, float x, float y, float z, int cx, int cy, int self, nb_t *nb, float hint) {
    int nn = 0;
    /* hint: last tick's distance to its Kth, with room - candidates past it are turned away from the start;
       if it proves too tight the ring is searched again without it */
    for (int r = 1; r <= 3; r++) {
        float kth = hint; nn = 0;
        int y0 = cy - r < 0 ? 0 : cy - r, y1 = cy + r >= GH ? GH - 1 : cy + r;
        int x0 = cx - r < 0 ? 0 : cx - r, x1 = cx + r >= GW ? GW - 1 : cx + r;
        for (int gy = y0; gy <= y1; gy++) {
            int a = g->cell_start[gy * GW + x0], b = g->cell_start[gy * GW + x1 + 1];
#ifdef SCHOOL_STATS
            school_stat[0] += b - a;
#endif
            const float *p = g->pos[a];
            for (int k = a; k < b; k++, p += 4) {
                float dx = p[0] - x, dy = p[1] - y, d2 = dx * dx + dy * dy;
                if (d2 >= kth) continue;
                float dz = p[2] - z;
                d2 += dz * dz;
                if (d2 >= kth || k == self) continue;
                int j = nn < SCHOOL_K ? nn++ : SCHOOL_K - 1;          /* keep the K best, nearest first */
                while (j > 0 && nb[j - 1].d2 > d2) { nb[j] = nb[j - 1]; j--; }
                nb[j].d2 = d2; nb[j].k = k;
                if (nn == SCHOOL_K) kth = nb[SCHOOL_K - 1].d2;
            }
        }
#ifdef SCHOOL_STATS
        school_stat[1]++;
#endif
        if (nn < SCHOOL_K && hint < 1e30f) { hint = 1e30f; r--; continue; }    /* the hint was too tight */
        float reach = r * (float)CELL;                                /* nothing unsearched is nearer than this */
        if (nn == SCHOOL_K && kth <= reach * reach) break;
    }
    return nn;
}

static void steer(school_t *s, school_scratch_t *g, int slot, float dt, int live_pellets) {
    const int i = g->order[slot];
    sfish_t *f = &s->f[i];
    const float BL = BL0;
    float x = f->x, y = f->y, z = f->z, vx = f->vx, vy = f->vy, vz = f->vz;
    float sp2 = vx * vx + vy * vy + vz * vz, isp = sp2 > 1e-6f ? inv_sqrt(sp2) : 0;
    float hx = vx * isp, hy = vy * isp, hz = vz * isp;           /* heading */
    int cx = (int)(x / CELL), cy = (int)(y / CELL);
    cx = cx < 0 ? 0 : cx >= GW ? GW - 1 : cx; cy = cy < 0 ? 0 : cy >= GH ? GH - 1 : cy;

    /* 1. the K nearest (scan_knn) */
    nb_t nb[SCHOOL_K];
    float sepx = 0, sepy = 0, sepz = 0;
    const float RSEP = 1.7f * BL, RSEP2 = RSEP * RSEP, iRSEP = 1 / RSEP;
    float hint = f->kth2 > 0 ? f->kth2 * 2.25f : 1e30f;              /* (1.5 x the distance)^2 */
    int nn = scan_knn(g, x, y, z, cx, cy, slot, nb, hint);
    f->kth2 = nn == SCHOOL_K ? nb[SCHOOL_K - 1].d2 : 0;
    const float RSEE = 6.0f * BL, RSEE2 = RSEE * RSEE;
    while (nn > 0 && nb[nn - 1].d2 > RSEE2) nn--;                  /* only what it can make out */
    f->nn = (uint16_t)nn;

    /* 2. align, cohere, catch a fright */
    float ax = 0, ay = 0, az = 0;
    float avx = 0, avy = 0, avz = 0, mx = 0, my = 0, mz = 0, wsum = 0;
    int scared_k = -1; float scared = 0;
    for (int q = 0; q < nn; q++) {
        int k = nb[q].k;
        float dx = g->pos[k][0] - x, dy = g->pos[k][1] - y, dz = g->pos[k][2] - z;
        float id = inv_sqrt(nb[q].d2 + 1e-6f), d = nb[q].d2 * id;
        if (nb[q].d2 < RSEP2) {                                        /* too close: keep off (the K nearest are the close ones) */
            float w = 1 - d * iRSEP; w = w * w * id;
            sepx -= dx * w; sepy -= dy * w; sepz -= dz * w;
        }
        float ahead = (dx * hx + dy * hy + dz * hz) * id;             /* -1 behind .. 1 ahead */
        float w = ahead < -0.7f ? 0.45f : 0.8f + 0.2f * ahead;          /* a little blind behind (more and they follow in lines) */
        avx += g->vx[k] * w; avy += g->vy[k] * w; avz += g->vz[k] * w;
        mx += dx * w; my += dy * w; mz += dz * w;
        wsum += w;
        if (g->burst[k] > 0.5f * g->fear[k] && g->fear[k] > scared) { scared = g->fear[k]; scared_k = k; }
    }
    float fear = f->fear, burst = f->burst;
    float night = s->light_on ? 1.0f : 0.55f;
    float want_sp = f->cruise * night * (1 + 0.55f * fear);
    if (wsum > 0) {
        float iw = 1 / wsum;
        float ka = 1.6f * s->w_align * night;
        ax += ka * (avx * iw - vx); ay += ka * (avy * iw - vy); az += ka * (avz * iw - vz);
        float kc = 1.3f * s->w_cohere * night * (1 - 0.85f * burst) * (1 + 0.8f * f->regroup);
        ax += kc * mx * iw; ay += kc * my * iw; az += kc * mz * iw;
    }
    float ks = 9.0f * s->w_separate * (1 + fear) * f->cruise;
    ax += ks * sepx; ay += ks * sepy; az += ks * sepz;

    /* a neighbor dashing away: dash the same way a beat later, a hop weaker (the wave) */
    if (scared_k >= 0 && f->refractory <= 0 && f->startle_in <= 0 && burst < 0.2f) {
        float carry = scared * 0.86f;
        if (carry > 0.3f) {
            float m = inv_sqrt(g->vx[scared_k] * g->vx[scared_k] + g->vy[scared_k] * g->vy[scared_k] + 1e-6f);
            f->sx = g->vx[scared_k] * m; f->sy = g->vy[scared_k] * m;
            f->startle_in = frange(s, 0.05f, 0.13f);
            f->fear = carry > f->fear ? carry : f->fear;           /* how strong its own dash will be */
        } else if (fear < carry + 0.2f) f->fear = carry + 0.2f;      /* the wave's tail: on edge, no dash */
    }

    /* a held finger: how much it has this fish's attention (the crowd's pulls relax for it - else the school
       just parks beside the finger as one lump instead of wrapping round it) */
    float held = 0, hdx = 0, hdy = 0, hd = 0;
    if (s->holding && s->hold_t > 0.3f) {
        hdx = s->hold_x - x; hdy = s->hold_y - y;
        float d2 = hdx * hdx + hdy * hdy;
        if (d2 < 22.0f * BL * 22.0f * BL && d2 > 1e-4f) {
            hd = d2 * inv_sqrt(d2);
            held = clampf((s->hold_t - 0.3f) / 3.0f, 0, 1) * (fear < 0.4f ? 1 : 0.2f);
        }
    }

    /* 3. its own goals */
    /* the crowd: climb the head count - always a little, hard when alone or regrouping */
    float lonely = 1 - nn * (1.0f / SCHOOL_K);
    bilin_t fb = bilin(FW, FH, (float)FCELL, x, y);
    float small = bl_at(g->small, fb);                               /* 0 in the biggest crowd .. 1 alone */
    float calm = (1 - burst) * (1 - 0.9f * fear);                     /* a frightened fish keeps to its few, not the crowd */
    float kg = s->w_gather * BL * (0.9f + 1.6f * lonely + 2.2f * small + 1.4f * f->regroup) * calm * (1 - 0.85f * held);
    if (kg > 0) {
        ax += kg * bl_at(g->crowd_gx, fb); ay += kg * bl_at(g->crowd_gy, fb);
        /* the others, seen across the water */
        float k = s->w_gather * BL * (1.0f + 2.0f * small + 1.0f * f->regroup) * calm * (1 - 0.85f * held);
        ax += k * bl_at(g->seen_x, fb); ay += k * bl_at(g->seen_y, fb);
    }
    /* the school's body: climb the near head count (surface tension - it rounds a string into a school) */
    {
        bilin_t nb2 = bilin(GW, GH, (float)CELL, x, y);
        float kn = 1.3f * BL * s->w_cohere * night * (1 - burst) * (1 - 0.9f * held);
        ax += kn * bl_at(g->near_gx, nb2); ay += kn * bl_at(g->near_gy, nb2);
    }
    /* food it can see (and wants) */
    float excite = 0; bool feeding = false;
    if (live_pellets && f->appetite > 0.2f) {
        int best = -1; float bd2 = 256.0f * BL * BL;                 /* clear water: a crumb is seen 16 BL off */
        for (int p = 0; p < SCHOOL_PELLETS; p++) {
            if (!s->p[p].live) continue;
            float dx = s->p[p].x - x, dy = s->p[p].y - y, dz = (s->p[p].z - z) * 0.5f, d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < bd2) { bd2 = d2; best = p; }
        }
        if (best >= 0) {
            spellet_t *p = &s->p[best];
            float hd = 0.45f * f->size;                                /* it bites with its mouth, not its middle */
            float dx = p->x - (x + hx * hd), dy = p->y - (y + hy * hd), dz = (p->z - z) * 0.5f, d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < 0.7f * BL * 0.7f * BL) {                         /* a bite */
                p->live = false; s->eaten++;
                f->appetite -= 0.3f; if (f->appetite < 0) f->appetite = 0;
                if (f->regroup < 0.6f) f->regroup = 0.6f;              /* fed: back to the others */
            } else {
                float m = inv_sqrt(d2), sp = f->cruise * 1.9f;
                float kf = 3.0f * f->appetite;
                ax += kf * (dx * m * sp - vx); ay += kf * (dy * m * sp - vy); az += kf * (dz * m * sp - vz);
                excite = f->appetite;
                feeding = true;
                want_sp *= 1 + 0.5f * excite;
            }
        }
    }
    /* a scout's spot */
    if (f->goal_t > 0) {
        float dx = f->gx - x, dy = f->gy - y, m = dx * dx + dy * dy;
        if (m < 4 * BL * BL) f->goal_t = 0;
        else { m = inv_sqrt(m); float k = 1.6f * BL; ax += k * dx * m; ay += k * dy * m; }
    }
    /* the finger: curiosity that builds - swim round it (a MILL, the school's own whirlpool), the way they
       were already turning, on a ring wide enough for the whole school */
    if (held > 0) {
        float m = 1 / hd, ux = hdx * m, uy = hdy * m;                    /* toward the finger */
        /* the ring holds the school as a band ~3.5 BL thick, an empty eye in the middle */
        float r0 = BL * (3.0f + s->n / (TAU_F * 3.5f)), rmax = 0.36f * SCHOOL_SHORT;
        if (r0 > rmax) r0 = rmax;
        float in = clampf((hd - r0) / r0, -1, 1);                         /* + outside the ring */
        float tx = -uy * s->hold_dir, ty = ux * s->hold_dir;              /* round it */
        float sp = f->cruise * 1.1f;
        float wx = (tx + 0.8f * in * ux) * sp, wy = (ty + 0.8f * in * uy) * sp;
        float k = 2.2f * held;
        ax += k * (wx - vx); ay += k * (wy - vy);
    }
    /* a finger dragged through the water: the fish part round it - away from it, and out of its path to the
       side they are already on - and the water it drags carries the ones just behind along. Brushed by a
       fast finger they dash (and the dash spreads, as a tap's does); a slow one only parts them, and the
       school flows back together behind it */
    float shove = 0;
    if (s->dragging) {
        float dx = x - s->drag_x, dy = y - s->drag_y, d2 = dx * dx + dy * dy;
        float fvx = s->drag_vx, fvy = s->drag_vy, fs2 = fvx * fvx + fvy * fvy, ifs = fs2 > 1e-4f ? inv_sqrt(fs2) : 0, fs = fs2 * ifs;
        const float R = 4.0f * BL, WAKE = 7.0f * BL;
        if (d2 < WAKE * WAKE && d2 > 1e-4f) {
            float id = inv_sqrt(d2), d = d2 * id, ux = dx * id, uy = dy * id;
            float fhx = fvx * ifs, fhy = fvy * ifs, px = -fhy, py = fhx;      /* the finger's heading and its side */
            float along = dx * fhx + dy * fhy, lat = dx * px + dy * py;       /* px: ahead of it (+), beside it */
            float side = lat >= 0 ? 1.0f : -1.0f;
            if (d < R) {
                float w = 1 - d * (1 / R); w *= w;
                shove = w;
                /* away from the finger - but not straight ahead of it (that herds the school before it): aside */
                float rx = ux, ry = uy;
                if (along > 0) { float a = along * id * 0.85f; rx -= a * fhx; ry -= a * fhy; }
                float k = 45.0f * BL * w;
                ax += k * rx; ay += k * ry;
                /* brushed by a fast finger: a dash out of its way (the fire happens in the move, like a copied startle) */
                if (ifs > 0 && fs > 6.0f * BL && d < 2.2f * BL && f->refractory <= 0 && f->startle_in <= 0 && burst < 0.3f) {
                    float strength = clampf(fs * (1 / (16.0f * BL)), 0.45f, 1.0f);
                    float ex = side * px + 0.6f * ux, ey = side * py + 0.6f * uy, em = inv_sqrt(ex * ex + ey * ey + 1e-6f);
                    f->sx = ex * em; f->sy = ey * em; f->startle_in = 0.01f;
                    if (f->fear < strength) f->fear = strength;
                }
            }
            /* in its lane ahead: step aside early (it looks ~0.6 s ahead), sooner the nearer - the school opens
               before the finger and closes behind it */
            if (ifs > 0 && along > -1.0f * BL) {
                float reach = 3.0f * BL + 0.6f * fs, lane = 3.0f * BL, al = lat * side;
                if (along < reach && al < lane) {
                    float w = (1 - al * (1 / lane)) * (1 - clampf(along, 0, reach) / reach);
                    float kp = 32.0f * BL * w * (0.5f + 0.5f * clampf(fs * (1 / (8.0f * BL)), 0, 1));
                    ax += kp * side * px; ay += kp * side * py;
                    if (shove < 0.7f * w) shove = 0.7f * w;
                }
            }
            if (ifs > 0 && dx * fvx + dy * fvy < 0) {                     /* behind it: the water it drags carries them along */
                float w = 1 - d * (1 / WAKE);
                float kw = 1.6f * w * w;
                ax += kw * (0.45f * fvx - vx); ay += kw * (0.45f * fvy - vy);
            }
        }
    }

    /* its own wander: a slow sideways lean */
    ax += -hy * f->wander * 0.9f * BL; ay += hx * f->wander * 0.9f * BL;
    /* depth: drift toward its own level, softly off the front and back glass */
    az += 0.5f * (f->zbias - z);
    if (z < 0.6f * BL) az += 6 * (0.6f * BL - z);
    if (z > DEPTH - 0.6f * BL) az -= 6 * (z - (DEPTH - 0.6f * BL));
    /* the piece: at its depth, the wood ahead is in the way - pass in front of it or behind it, whichever side the
       fish is already on (its own depth breaks a tie) */
    {
        int qx = (int)((x + hx * 1.5f * BL) * (1.0f / CELL)), qy = (int)((y + hy * 1.5f * BL) * (1.0f / CELL));
        qx = qx < 0 ? 0 : qx >= GW ? GW - 1 : qx; qy = qy < 0 ? 0 : qy >= GH ? GH - 1 : qy;
        int sa = g->solid[qy * GW + qx], sb = g->solid[cy * GW + cx];
        float sol = (sa > sb ? sa : sb) * (1.0f / 255);
        float dz = z - PIECE_Z, band = PIECE_TH + 0.5f * BL;
        if (sol > 0.15f && dz > -band && dz < band) {
            float side = dz > 0.05f * BL ? 1.0f : dz < -0.05f * BL ? -1.0f : f->zbias > PIECE_Z ? 1.0f : -1.0f;
            az += side * 14.0f * BL * sol * (1 - fabsf(dz) * (1 / band));
        }
    }
    /* night: settle a little lower */
    if (!s->light_on) ay += 0.25f * BL;

    /* the glass ahead: look 0.9 s down the heading; turn along the wall's normal before getting there */
    float look = 0.9f * (sp2 > 0 ? sp2 * isp : f->cruise) + 2 * BL;
    float qx = x + hx * look, qy = y + hy * look, margin = (feeding ? 1.5f : 4.0f) * BL;   /* after a crumb it dares the sand */
    float dq = swim_dist(qx, qy), dp = swim_dist(x, y);
    if (dq < margin || dp < margin) {
        float e = 2.0f;
        float nx = swim_dist(qx + e, qy) - swim_dist(qx - e, qy), ny = swim_dist(qx, qy + e) - swim_dist(qx, qy - e);
        float m = nx * nx + ny * ny;
        if (m > 1e-8f) {
            m = inv_sqrt(m); nx *= m; ny *= m;
            float push = (margin - (dq < dp ? dq : dp)) * (1 / (4.0f * BL)) * (feeding ? 4.0f / 1.5f : 1.0f);
            float kw = 26.0f * BL * s->w_wall * push * push;
            ax += kw * nx; ay += kw * ny;
            /* and turn along it rather than stopping dead: the side the fish already leans to */
            float side = (-ny * hx + nx * hy) >= 0 ? 1.0f : -1.0f;
            ax += side * -ny * kw * 0.5f; ay += side * nx * kw * 0.5f;
        }
    }

    /* 4. pace: ease to the wanted speed along the heading; a dash carries it */
    float sp = sp2 * isp;
    float target = want_sp + burst * (11.0f * BL - want_sp);
    float kp = 1.6f + 6.0f * burst;
    ax += kp * (target - sp) * hx; ay += kp * (target - sp) * hy; az += kp * (target - sp) * hz;

    /* the limits of a fish */
    float amax = (7.0f + 80.0f * burst + 40.0f * shove) * BL, a2 = ax * ax + ay * ay + az * az;   /* (a finger close by: they get out of its way fast) */
    if (a2 > amax * amax) { float m = amax * inv_sqrt(a2); ax *= m; ay *= m; az *= m; }
    float nvx = vx + ax * dt, nvy = vy + ay * dt, nvz = vz + az * dt;
    /* mostly level: real schools swim flat; climbs and dives are shallow */
    float hor2 = nvx * nvx + nvz * nvz + 1e-6f, hor = hor2 * inv_sqrt(hor2);
    float pitch = (0.75f + 3.0f * held) * hor + 2 * BL * burst;          /* circling a finger, they climb and dive its sides */
    if (fabsf(nvy) > pitch) nvy = (nvy > 0 ? 1 : -1) * pitch;
    float zcap = 0.45f * fabsf(nvx) + 0.3f * BL;                        /* (more beside the piece: over it or under it) */
    if (g->solid[cy * GW + cx]) zcap += 1.2f * BL;
    if (fabsf(nvz) > zcap) nvz = (nvz > 0 ? 1 : -1) * zcap;
    float n2 = nvx * nvx + nvy * nvy + nvz * nvz, vmin = 0.45f * f->cruise * night, vmax = 12.0f * BL;
    if (n2 < vmin * vmin) { float m = vmin * inv_sqrt(n2 + 1e-6f); nvx *= m; nvy *= m; nvz *= m; }
    if (n2 > vmax * vmax) { float m = vmax * inv_sqrt(n2); nvx *= m; nvy *= m; nvz *= m; }
    g->nvx[i] = nvx; g->nvy[i] = nvy; g->nvz[i] = nvz;
}

/* a spot by the piece (in among the fern and over the moss), low enough to reach: false if there is none */
static bool piece_spot(school_t *s, float *x, float *y) {
    const school_scratch_t *g = scratch();
    for (int k = 0; k < 40; k++) {
        int c = (int)(rnd(s) % (GW * GH));
        if (g->solid[c] < 40) continue;
        float cx = (c % GW + 0.5f) * CELL, cy = (c / GW + 0.5f) * CELL;
        if (cy > SAND_Y - 4.5f * BL0) cy = SAND_Y - 4.5f * BL0;
        if (school_glass_dist(cx, cy) < 6 * BL0) continue;             /* clear of the side glass (the sand is the point) */
        *x = cx; *y = cy;
        return true;
    }
    return false;
}

static void scouts(school_t *s, float dt) {
    if ((s->scout_t -= dt) > 0 || s->n == 0) return;
    s->scout_t = frange(s, 7, 15);
    /* an idea: a spot of open water, taken up by a few fish somewhere in the crowd -
       nobody else knows; they just follow whoever turns */
    float gx, gy;
    if (frand(s) > 0.35f || !piece_spot(s, &gx, &gy)) open_water(s, 7 * BL0, &gx, &gy);   /* (a third of them: the plants) */
    int lead = (int)(rnd(s) % s->n), want = 2 + s->n / 16;
    float lx = s->f[lead].x, ly = s->f[lead].y;
    for (int i = 0, got = 0; i < s->n && got < want; i++) {
        sfish_t *f = &s->f[i];
        float dx = f->x - lx, dy = f->y - ly;
        if (dx * dx + dy * dy < 25 * BL0 * BL0 && f->fear < 0.3f && frand(s) < 0.7f) {
            f->goal_t = frange(s, 4, 9); f->gx = gx + frange(s, -2, 2) * BL0; f->gy = gy + frange(s, -2, 2) * BL0; got++;
        }
    }
}

void school_tick(school_t *s, float dt) {
    if (dt > 0.1f) dt = 0.1f;
    school_scratch_t *g = scratch();
    s->t += dt;
    if (s->t >= SCHOOL_CLOCK_LAP) s->t -= SCHOOL_CLOCK_LAP;   /* 20000 pi s: a whole number of the crumbs' 2.1 (a float left running stops at 12 days: audit FW2-1) */
    if (s->holding) s->hold_t += dt;
    PROF_T0(t0);
    build_grid(s, g);
    PROF_ADD(0, t0);
    scouts(s, dt);
    int live = 0;
    for (int p = 0; p < SCHOOL_PELLETS; p++) live += s->p[p].live;
    PROF_T0(t1);
    /* each fish decides every SCHOOL_STEER_EVERY ticks (80 ms at 25 fps: about a fish's reaction time), half
       the school one tick and half the next; positions still move every tick */
    const unsigned turn = s->ticks++ % SCHOOL_STEER_EVERY;
    for (int k = 0; k < s->n; k++)                                   /* in cell order: neighbors are near in memory too */
        if (g->order[k] % SCHOOL_STEER_EVERY == (int)turn) steer(s, g, k, dt * SCHOOL_STEER_EVERY, live);
    PROF_ADD(1, t1);
    PROF_T0(t2);
    const float BL = BL0;
    for (int i = 0; i < s->n; i++) {
        sfish_t *f = &s->f[i];
        float ovx = f->vx, ovy = f->vy;
        if (i % SCHOOL_STEER_EVERY == (int)turn) { f->vx = g->nvx[i]; f->vy = g->nvy[i]; f->vz = g->nvz[i]; }
        /* a copied startle fires */
        if (f->startle_in > 0 && (f->startle_in -= dt) <= 0) {
            float a = frange(s, -0.3f, 0.3f), c = cosf(a), sn = sinf(a);
            startle(f, f->sx * c - f->sy * sn, f->sx * sn + f->sy * c, f->fear);
            s->startles++;
        }
        f->x += f->vx * dt; f->y += f->vy * dt; f->z += f->vz * dt;
        /* never through the glass, whatever the forces said */
        float d = swim_dist(f->x, f->y);
        if (d < 0.5f * BL) {
            float e = 1.0f, nx = swim_dist(f->x + e, f->y) - swim_dist(f->x - e, f->y), ny = swim_dist(f->x, f->y + e) - swim_dist(f->x, f->y - e);
            float m = nx * nx + ny * ny;
            if (m > 1e-8f) {
                m = inv_sqrt(m); nx *= m; ny *= m;
                f->x += nx * (0.5f * BL - d); f->y += ny * (0.5f * BL - d);
                float vn = f->vx * nx + f->vy * ny;
                if (vn < 0) { f->vx -= vn * nx; f->vy -= vn * ny; }
            }
        }
        f->z = clampf(f->z, 0, DEPTH);
        /* the flank catches the light as it turns (and in a dash) */
        float cr = ovx * f->vy - ovy * f->vx, m2 = (ovx * ovx + ovy * ovy) * (f->vx * f->vx + f->vy * f->vy);
        float turn = m2 > 1e-6f ? fabsf(cr) * inv_sqrt(m2) / dt : 0;    /* rad/s */
        float gl = clampf(turn * 0.28f + f->burst * 0.9f, 0, 1);
        f->glint = gl > f->glint ? approach(f->glint, gl, dt * 18) : approach(f->glint, gl, dt * 3.5f);
        /* the tail beats with the pace */
        float sp = sqrtf(f->vx * f->vx + f->vy * f->vy + f->vz * f->vz);
        float hz = clampf(1.4f + 0.85f * sp / BL, 1.4f, 8.5f);
        f->phase += hz * dt; f->phase -= (int)f->phase;
        /* the fright fades; once it has, the crowd pulls hard for a while */
        bool was_scared = f->fear > 0.45f;
        f->burst = approach(f->burst, 0, dt * 2.2f);
        f->fear = approach(f->fear, 0, dt * 0.25f);
        if (was_scared && f->fear <= 0.45f) f->regroup = 1;
        f->regroup = approach(f->regroup, 0, dt / 14.0f);
        if (f->refractory > 0) f->refractory -= dt;
        if (f->goal_t > 0) f->goal_t -= dt;
        f->appetite = clampf(f->appetite + dt / 90.0f, 0, 1);
        /* the wander and the depth it likes drift slowly */
        f->wander = clampf(f->wander + frange(s, -1, 1) * dt * 0.9f - f->wander * dt * 0.25f, -1, 1);
        if ((rnd(s) & 1023) < 2) f->zbias = frange(s, 0.2f, 0.8f) * DEPTH;
    }
    /* food sinks, wobbling, then lies on the sand a while */
    for (int p = 0; p < SCHOOL_PELLETS; p++) {
        spellet_t *q = &s->p[p];
        if (!q->live) continue;
        if (q->y < SAND_Y - 1.5f * S) {
            q->vy = approach(q->vy, (0.6f + 0.07f * (p % 9)) * BL, dt * 1.5f);      /* crumbs sink at their own rates */
            q->y += q->vy * dt;
            q->x += sinf(s->t * 2.1f + p * 1.7f) * 0.25f * BL * dt;
        } else if ((q->rest += dt) > 45) q->live = false;
    }
    for (int i = 0; i < SCHOOL_RIPPLES; i++)
        if (s->r[i].live && (s->r[i].t += dt) > 0.9f) s->r[i].live = false;
    PROF_ADD(2, t2);
}

/* ---- the shape of the crowd -------------------------------------------------------- */
static int uf_find(uint16_t *p, int a) { while (p[a] != a) { p[a] = p[p[a]]; a = p[a]; } return a; }
school_shape_t school_shape(const school_t *s) {
    school_shape_t r = { 0, 0, 0, 0 };
    if (s->n == 0) return r;
    school_scratch_t *g = scratch();
    uint16_t *par = g->uf, *cnt = g->ucnt;
    float *nnd = g->nnd;
    const float L = 2.5f * BL0, L2 = L * L;
    float sx = 0, sy = 0;
    for (int i = 0; i < s->n; i++) { par[i] = (uint16_t)i; nnd[i] = 1e9f; }
    for (int i = 0; i < s->n; i++) {
        const sfish_t *a = &s->f[i];
        float m = sqrtf(a->vx * a->vx + a->vy * a->vy) + 1e-6f;
        sx += a->vx / m; sy += a->vy / m;
        for (int j = i + 1; j < s->n; j++) {
            const sfish_t *b = &s->f[j];
            float dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z, d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < nnd[i]) nnd[i] = d2;
            if (d2 < nnd[j]) nnd[j] = d2;
            if (d2 < L2) { int ra = uf_find(par, i), rb = uf_find(par, j); if (ra != rb) par[ra] = (uint16_t)rb; }
        }
    }
    memset(cnt, 0, sizeof(uint16_t) * SCHOOL_MAX);
    for (int i = 0; i < s->n; i++) cnt[uf_find(par, i)]++;
    for (int i = 0; i < s->n; i++) if (cnt[i]) { r.groups++; if (cnt[i] > r.biggest) r.biggest = cnt[i]; }
    r.polar = sqrtf(sx * sx + sy * sy) / s->n;
    /* the median nearest neighbor (a partial selection is plenty) */
    for (int i = 0; i < s->n; i++) nnd[i] = sqrtf(nnd[i]) / BL0;
    int half = s->n / 2;
    for (int i = 0; i <= half; i++) {
        int mi = i;
        for (int j = i + 1; j < s->n; j++) if (nnd[j] < nnd[mi]) mi = j;
        float t = nnd[i]; nnd[i] = nnd[mi]; nnd[mi] = t;
    }
    r.nnd_bl = nnd[half];
    return r;
}

/* ---- drawing ------------------------------------------------------------------------ */
typedef struct { uint8_t r, g, b; } col_t;
static inline col_t col(uint32_t rgb) { return (col_t){ (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb }; }
static inline col_t mixc(col_t a, col_t b, float t) {
    return (col_t){ (uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t), (uint8_t)(a.b + (b.b - a.b) * t) };
}
/* RGB565 three channels at once (the jelly spike's trick): spread as 00000GGGGGG00000RRRRR000000BBBBB
   so one multiply blends all three */
#define SPREAD_MASK 0x07E0F81FU
static inline uint32_t spread565(uint16_t c) { return (c | ((uint32_t)c << 16)) & SPREAD_MASK; }
static inline uint16_t pack565(uint32_t s) { return (uint16_t)((s | (s >> 16)) & 0xFFFF); }
static inline uint16_t to565(col_t c) { return (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }
static inline col_t from565(uint16_t p) { return (col_t){ (uint8_t)(((p >> 11) & 31) << 3), (uint8_t)(((p >> 5) & 63) << 2), (uint8_t)((p & 31) << 3) }; }
/* the frame's byte order (render.h's PT_FB_SWAP, 2026-10-23): on the device the frame and the baked water hold
   every pixel as the panel's wire wants it, the two bytes swapped; the colour math stays native. Every store goes
   through FB16, every load back (the sim builds it off) */
#ifndef PT_FB_SWAP
#define PT_FB_SWAP 0
#endif
#if PT_FB_SWAP
#define FB16(v) ((uint16_t)((((uint16_t)(v)) << 8) | (((uint16_t)(v)) >> 8)))
#else
#define FB16(v) ((uint16_t)(v))
#endif
static inline void blend_s(uint16_t *p, uint32_t fg, int a5) {
    uint32_t bg = spread565(FB16(*p));
    *p = FB16(pack565((bg + (((fg - bg) * (uint32_t)a5) >> 5)) & SPREAD_MASK));
}

/* ---- the piece: driftwood, Christmas moss, a Windelov java fern ---------------------------------------------
   Drawn once per glass (at the scratch's setup) into an RGBA layer over the lower part of the glass, from tapered
   strokes ("capsules"): the wood a few gnarled chains, the moss a fuzzy coat plus hundreds of tiny fir-shaped
   fronds, the fern a fan of long blades whose tips split into the Windelov's lace. The water's bake lays it in */
typedef struct { float x0, y0, r0, x1, y1, r1, s0, s1; } cap_t;      /* a tapered stroke; s: along its shape, px */
typedef struct { int k; float t, s, lat, latv, x, y; } cap_px_t;       /* lat: across, -1..1; latv: up -1 .. down +1 */
typedef col_t (*cap_paint_t)(const void *ctx, const cap_px_t *p);

static uint32_t g_prng;
static float prand(void) { uint32_t x = g_prng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; g_prng = x; return (x >> 8) * (1.0f / 16777216.0f); }
static float prange(float a, float b) { return a + (b - a) * prand(); }
static float hash2(int x, int y, int seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffff) * (1.0f / 65535);
}
static float fsin(float x) {                                          /* a cheap sine for textures (+-0.06) */
    x *= 0.15915494f; x -= (float)((int)(x + 1024.5f) - 1024);         /* turns, -0.5 .. 0.5 */
    return 8 * x * (1 - 2 * fabsf(x));
}
static float vnoise(float x, float y, int seed) {                     /* smooth value noise, 0..1 */
    int ix = (int)(x + 4096) - 4096, iy = (int)(y + 4096) - 4096;    /* floor without the library call */
    float tx = x - ix, ty = y - iy;
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
    float a = hash2(ix, iy, seed), b = hash2(ix + 1, iy, seed), c = hash2(ix, iy + 1, seed), d = hash2(ix + 1, iy + 1, seed);
    return a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty;
}

/* lay color c at coverage a over the layer ("over", premultiplied: no divide - the S3 has none in its FPU) */
static void pput(school_scratch_t *g, int x, int y, col_t c, float a) {
    if (a <= 0.004f || (unsigned)x >= TANK_W || y < PIECE_Y0 || y >= TANK_BOT || x < g->gx0[y] || x > g->gx1[y]) return;
    int ai = a >= 1 ? 256 : (int)(a * 256), ki = 256 - ai;
    uint32_t *p = &g->prgba[(y - PIECE_Y0) * TANK_W + x], v = *p;
    uint32_t r = (c.r * ai + (v & 255) * ki + 128) >> 8, gg = (c.g * ai + ((v >> 8) & 255) * ki + 128) >> 8;
    uint32_t b = (c.b * ai + ((v >> 16) & 255) * ki + 128) >> 8, A = (255 * ai + (v >> 24) * ki + 128) >> 8;
    *p = r | gg << 8 | b << 16 | A << 24;
}

/* one shape: its strokes' union, antialiased, painted per pixel by the stroke nearest inside. Built once, but on
   the S3 (no divide, no square root in the FPU: each is a library call) a naive pass ran ~0.5 G instructions -
   so every stroke gets its own box and reciprocals up front, and a pixel takes a square root only when it is
   inside the stroke's reach */
typedef struct { float ex, ey, il2, il, rr, x0, y0, x1, y1; } cap_pre_t;
static void shape(school_scratch_t *g, const cap_t *c, int n, cap_paint_t paint, const void *ctx, float alpha) {
    enum { CMAX = 64 };
    cap_pre_t pre[CMAX];
    if (n > CMAX) n = CMAX;
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
    for (int k = 0; k < n; k++) {
        cap_pre_t *q = &pre[k];
        float r = (c[k].r0 > c[k].r1 ? c[k].r0 : c[k].r1) + 0.5f;
        q->ex = c[k].x1 - c[k].x0; q->ey = c[k].y1 - c[k].y0;
        float l2 = q->ex * q->ex + q->ey * q->ey;
        q->il2 = l2 > 1e-6f ? 1 / l2 : 0; q->il = l2 > 1e-6f ? 1 / sqrtf(l2) : 0; q->rr = r * r;
        q->x0 = fminf(c[k].x0, c[k].x1) - r; q->x1 = fmaxf(c[k].x0, c[k].x1) + r;
        q->y0 = fminf(c[k].y0, c[k].y1) - r; q->y1 = fmaxf(c[k].y0, c[k].y1) + r;
        bx0 = fminf(bx0, q->x0); bx1 = fmaxf(bx1, q->x1); by0 = fminf(by0, q->y0); by1 = fmaxf(by1, q->y1);
    }
    int y0 = (int)floorf(by0), y1 = (int)ceilf(by1), x0 = (int)floorf(bx0), x1 = (int)ceilf(bx1);
    if (y0 < PIECE_Y0) y0 = PIECE_Y0;
    if (y1 > TANK_BOT - 1) y1 = TANK_BOT - 1;
    if (x0 < 0) x0 = 0;
    if (x1 > TANK_W - 1) x1 = TANK_W - 1;
    for (int y = y0; y <= y1; y++) {
        const float py = y + 0.5f;
        uint8_t live[CMAX]; int nl = 0;                                 /* the strokes this row crosses */
        for (int k = 0; k < n; k++) if (py >= pre[k].y0 && py <= pre[k].y1) live[nl++] = k;
        if (!nl) continue;
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f, best = 0, bd = 0;
            cap_px_t q = { -1, 0, 0, 0, 0, px, py };
            for (int j = 0; j < nl; j++) {
                const int k = live[j];
                const cap_pre_t *e = &pre[k];
                if (px < e->x0 || px > e->x1) continue;
                float t = ((px - c[k].x0) * e->ex + (py - c[k].y0) * e->ey) * e->il2;
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                float dx = px - (c[k].x0 + e->ex * t), dy = py - (c[k].y0 + e->ey * t), d2 = dx * dx + dy * dy;
                if (d2 >= e->rr) continue;                              /* out of reach: no square root */
                float d = sqrtf(d2), r = c[k].r0 + (c[k].r1 - c[k].r0) * t;
                float cv = r - d + 0.5f;
                cv = cv < 0 ? 0 : cv > 1 ? 1 : cv;
                if (cv > best || (cv == best && cv > 0 && d < bd)) {
                    best = cv; bd = d; q.k = k; q.t = t; q.s = c[k].s0 + (c[k].s1 - c[k].s0) * t;
                    float ir = r > 0.3f ? 1 / r : 1 / 0.3f;
                    q.lat = clampf((dx * -e->ey + dy * e->ex) * e->il * ir, -1, 1);   /* + to the stroke's right */
                    q.latv = clampf(dy * ir, -1, 1);
                }
            }
            if (best > 0) pput(g, x, y, paint(ctx, &q), best * alpha);
        }
    }
}

static col_t ramp3(uint32_t a, uint32_t b, uint32_t c, float t) {
    t = clampf(t, 0, 1);
    return t < 0.5f ? mixc(col(a), col(b), t * 2) : mixc(col(b), col(c), t * 2 - 1);
}
static float g_u;                                                     /* the piece's unit: 1 px on the 1.8 */

/* the wood: near-black driftwood, lit from above, its grain running along it */
static col_t paint_wood(const void *ctx, const cap_px_t *p) {
    (void)ctx;
    float lt = 0.5f - 0.5f * p->latv, ao = 1 - p->lat * p->lat;
    float gn = vnoise(p->s / (3.0f * g_u), p->lat * 2.5f, 11);
    float streak = fsin(p->s / (1.6f * g_u) + gn * 6 + p->lat * 2.2f);
    col_t c = ramp3(0x140e0a, 0x34251b, 0x67503e, lt * (0.55f + 0.45f * ao) + 0.15f * (gn - 0.5f));
    if (streak > 0.72f) c = mixc(c, col(0x0c0806), 0.55f);
    return c;
}
/* the moss's coat: a fine, fuzzy green, brighter on top */
static col_t paint_moss(const void *ctx, const cap_px_t *p) {
    (void)ctx;
    float n1 = vnoise(p->x / (1.3f * g_u), p->y / (1.3f * g_u), 21), n2 = vnoise(p->x / (3.4f * g_u), p->y / (3.4f * g_u), 22);
    float lt = 0.5f - 0.5f * p->latv;
    col_t c = ramp3(0x10240c, 0x2f5a1a, 0x6e9e34, 0.1f + 0.5f * n1 * n2 * 1.6f + 0.3f * lt);
    return mixc(c, col(0x0a1608), clampf(p->latv, 0, 1) * 0.45f);        /* shade underneath */
}
typedef struct { uint32_t base, tip; } frond_c_t;
static col_t paint_frond(const void *ctx, const cap_px_t *p) {
    const frond_c_t *fc = (const frond_c_t *)ctx;
    return mixc(col(fc->base), col(fc->tip), clampf(p->s, 0, 1));
}
/* the fern's blade: deep green, one half of the fold lighter, a pale midrib, faint veins */
typedef struct { float len; float shade; } leaf_c_t;
static col_t paint_leaf(const void *ctx, const cap_px_t *p) {
    const leaf_c_t *lc = (const leaf_c_t *)ctx;
    float u = p->s / lc->len, al = fabsf(p->lat);
    col_t c = p->lat > 0 ? mixc(col(0x2f5d22), col(0x4b8530), 0.5f + 0.5f * u) : mixc(col(0x24481a), col(0x3c7228), 0.4f + 0.6f * u);
    c = mixc(c, col(0x76ad4c), clampf(u - 0.75f, 0, 1) * 1.6f);       /* the lace at the tip: young and pale */
    if (al < 0.1f && u < 0.8f) c = mixc(c, col(0x7aa856), 0.38f);
    else if (fsin(p->s / (1.5f * g_u) - al * 3.2f) > 0.8f) c = mixc(c, col(0x1c3a14), 0.35f);
    if (al > 0.82f) c = mixc(c, col(0x1a3412), 0.3f);
    return mixc(c, col(0x000000), lc->shade);
}

/* a chain of tapered strokes through nodes (x, y, r) in units from the anchor */
static int chain(cap_t *c, const float (*nd)[3], int n, float ax, float ay, float u) {
    float s = 0;
    for (int k = 0; k + 1 < n; k++) {
        cap_t *q = &c[k];
        q->x0 = ax + nd[k][0] * u; q->y0 = ay + nd[k][1] * u; q->r0 = nd[k][2] * u;
        q->x1 = ax + nd[k + 1][0] * u; q->y1 = ay + nd[k + 1][1] * u; q->r1 = nd[k + 1][2] * u;
        q->s0 = s; s += sqrtf((q->x1 - q->x0) * (q->x1 - q->x0) + (q->y1 - q->y0) * (q->y1 - q->y0)); q->s1 = s;
    }
    return n - 1;
}

/* a sprig of Christmas moss: a stem with side shoots alternating, longest at the base - a tiny fir */
static void frond(school_scratch_t *g, float x, float y, float dx, float dy, float len, uint32_t base, uint32_t tip) {
    cap_t c[9];
    float w = 0.42f * (g_u > 1 ? g_u : 1), px = -dy, py = dx;
    c[0] = (cap_t){ x, y, w * 1.15f, x + dx * len, y + dy * len, w * 0.7f, 0, 1 };
    int n = 1, m = 4 + (int)(prand() * 3);
    for (int j = 0; j < m; j++) {
        float t = 0.18f + j * (0.75f / m), side = j & 1 ? 1.0f : -1.0f;
        float bl = (1 - t) * 0.6f * len + 0.6f * g_u, an = 0.95f + prange(-0.2f, 0.2f);
        float bx = x + dx * len * t, by = y + dy * len * t;
        float ex = dx * cosf(an) + side * px * sinf(an), ey = dy * cosf(an) + side * py * sinf(an);
        c[n++] = (cap_t){ bx, by, w * 0.85f, bx + ex * bl, by + ey * bl, w * 0.55f, t, 1 };
    }
    frond_c_t fc = { base, tip };
    shape(g, c, n, paint_frond, &fc, 0.95f);
}

/* one fern leaf: a curved blade (a quadratic through p0, p1, p2), its tip split into lobes that fork again */
static void leaf(school_scratch_t *g, float x0, float y0, float th, float len, float wm, float shade) {
    enum { SEG = 22 };
    cap_t c[SEG + 40];
    float dx = sinf(th), dy = -cosf(th);
    float th2 = th + (th >= 0 ? 1 : -1) * (0.3f + 0.45f * fabsf(th));
    float p1x = x0 + dx * 0.5f * len, p1y = y0 + dy * 0.5f * len;
    float p2x = p1x + sinf(th2) * 0.55f * len, p2y = p1y - cosf(th2) * 0.55f * len + 0.2f * len * fabsf(th);
    float px[SEG + 1], py[SEG + 1], pr[SEG + 1], ps[SEG + 1], s = 0;
    for (int k = 0; k <= SEG; k++) {
        float t = 0.86f * k / SEG, a = (1 - t) * (1 - t), b = 2 * (1 - t) * t, e = t * t;
        px[k] = a * x0 + b * p1x + e * p2x; py[k] = a * y0 + b * p1y + e * p2y;
        if (k) s += sqrtf((px[k] - px[k - 1]) * (px[k] - px[k - 1]) + (py[k] - py[k - 1]) * (py[k] - py[k - 1]));
        ps[k] = s;
        float tt = (t - 0.1f) / 0.9f, h;
        if (t < 0.1f) h = 0.55f * g_u;                                  /* the stalk */
        else h = wm * powf(tt < 0.33f ? sinf(1.5708f * tt / 0.33f) : cosf(1.5708f * (tt - 0.33f) / 0.67f), 0.6f) + 0.5f * g_u
                 + (t > 0.2f ? 0.55f * g_u * sinf(s / (2.8f * g_u)) : 0);       /* the blade's wavy edge */
        pr[k] = h;
    }
    int n = 0;
    for (int k = 0; k < SEG; k++) c[n++] = (cap_t){ px[k], py[k], pr[k], px[k + 1], py[k + 1], pr[k + 1], ps[k], ps[k + 1] };
    /* the Windelov's lace: three to five lobes fanning from the tip, each forking once or twice */
    float bx = px[SEG - 3], by = py[SEG - 3], tx = px[SEG] - px[SEG - 4], ty = py[SEG] - py[SEG - 4], tm = 1 / sqrtf(tx * tx + ty * ty + 1e-6f);
    tx *= tm; ty *= tm;
    int nl = 4 + (int)(prand() * 3);
    for (int j = 0; j < nl && n < SEG + 36; j++) {
        float an = (nl > 1 ? -0.85f + 1.7f * j / (nl - 1) : 0) + prange(-0.12f, 0.12f), ll = len * prange(0.09f, 0.15f);
        float lx = tx * cosf(an) - ty * sinf(an), ly = tx * sinf(an) + ty * cosf(an);
        float r0 = fmaxf(pr[SEG - 3] * 0.55f, 1.3f * g_u), mx = bx + lx * ll * 0.6f, my = by + ly * ll * 0.6f;
        c[n++] = (cap_t){ bx, by, r0, mx, my, r0 * 0.8f, ps[SEG - 3], ps[SEG - 3] + ll * 0.6f };
        for (int f2 = -1; f2 <= 1; f2 += 2) {
            float a2 = an + f2 * prange(0.3f, 0.55f), l2 = ll * prange(0.45f, 0.7f);
            float qx = tx * cosf(a2) - ty * sinf(a2), qy = tx * sinf(a2) + ty * cosf(a2);
            c[n++] = (cap_t){ mx, my, r0 * 0.7f, mx + qx * l2, my + qy * l2, 0.45f * g_u, ps[SEG - 3] + ll * 0.6f, ps[SEG - 3] + ll * 0.6f + l2 };
        }
    }
    leaf_c_t lc = { s + len * 0.15f, shade };
    shape(g, c, n, paint_leaf, &lc, 0.97f);
}

#ifdef SCHOOL_BAKE_PROF                                                  /* (s3bench: where the bake goes) */
#include <stdio.h>
#define BAKE_MARK(w) do { int64_t t_ = school_clock_us ? school_clock_us() : 0; printf("  bake %-7s %lld vus\n", w, (long long)(t_ - bake_t)); bake_t = t_; } while (0)
#else
#define BAKE_MARK(w) do { } while (0)
#endif
static void piece_build(school_scratch_t *g) {
#ifdef SCHOOL_BAKE_PROF
    int64_t bake_t = school_clock_us ? school_clock_us() : 0;
#endif
    memset(g->prgba, 0, sizeof g->prgba);
    g_prng = 0x2b7e1516u;
    /* the anchor: on the sand a little right of the glass's middle, scaled down where the glass is narrow */
    int yr = (int)(SAND_Y - 30 * S);
    float avail = (float)(g->gx1[yr] - g->gx0[yr]), u = S;
    if (0.8f * avail < 270 * S) u = 0.8f * avail / 270;
    g_u = u;
    float ax = (g->gx0[yr] + g->gx1[yr]) * 0.5f + 0.05f * avail, ay = SAND_Y + 7 * S;

    /* the wood: a gnarled log, a branch climbing at the left, a knob at the right, roots into the sand */
    static const float MAIN[][3] = { { -124, 2, 6 }, { -102, -4, 9 }, { -78, -12, 12 }, { -52, -20, 15 }, { -24, -28, 16 }, { 6, -34, 17 },
                                     { 34, -38, 16 }, { 60, -38, 15 }, { 84, -32, 13 }, { 102, -21, 11 }, { 114, -8, 8 }, { 120, 4, 6 } };
    static const float BRA[][3] = { { -76, -16, 9 }, { -90, -34, 7 }, { -99, -50, 5 }, { -102, -60, 3.2f } };
    static const float BRB[][3] = { { 44, -44, 9 }, { 56, -60, 6 }, { 61, -72, 4 }, { 58, -80, 2.4f } };
    static const float ROC[][3] = { { 96, -26, 6 }, { 118, -38, 4.5f }, { 133, -44, 2.6f } };
    static const float ROD[][3] = { { -102, -4, 6 }, { -122, -12, 4 }, { -134, -8, 2.4f } };
    cap_t w[24];
    int n = 0;
    n += chain(w + n, MAIN, 12, ax, ay, u);
    n += chain(w + n, BRA, 4, ax, ay, u);
    n += chain(w + n, BRB, 4, ax, ay, u);
    n += chain(w + n, ROC, 3, ax, ay, u);
    n += chain(w + n, ROD, 3, ax, ay, u);
    shape(g, w, n, paint_wood, NULL, 1);
    BAKE_MARK("wood");

    /* the moss's coat: over most of the log and up the branches, thick on top, thin (or none) underneath; the
       right end and its root stay bare wood */
    cap_t m[24];
    int nm = 0;
    static const float TOP[9] = { 3, 6, 9, 7, 10, 8, 5, 7, 3 }, BOT[9] = { -1, 1, -3, -5, -2, -6, -4, -3, -6 };   /* MAIN 0..8: lumps on top, wood showing beneath */
    for (int k = 0; k < 8; k++) {
        float tt0 = (TOP[k] + prange(-1, 1)) * u, tb0 = (BOT[k] + prange(-1, 1)) * u, tt1 = (TOP[k + 1] + prange(-1, 1)) * u, tb1 = (BOT[k + 1] + prange(-1, 1)) * u;
        const cap_t *q = &w[k];
        m[nm++] = (cap_t){ q->x0, q->y0 - (tt0 - tb0) * 0.5f, q->r0 + (tt0 + tb0) * 0.5f, q->x1, q->y1 - (tt1 - tb1) * 0.5f, q->r1 + (tt1 + tb1) * 0.5f, q->s0, q->s1 };
    }
    for (int k = 11; k < 13; k++) { cap_t q = w[k]; q.r0 += 3 * u; q.r1 += 2.5f * u; q.x0 -= 1.5f * u; q.x1 -= 1.5f * u; m[nm++] = q; }   /* BRA's foot */
    { cap_t q = w[14]; q.r0 += 2.5f * u; q.r1 += 1.5f * u; q.y0 -= 1.5f * u; q.y1 -= 1.5f * u; m[nm++] = q; }                              /* BRB's foot */
    shape(g, m, nm, paint_moss, NULL, 1);
    BAKE_MARK("moss");

    /* the sprigs: first a layer over the coat's face, then the fuzz all round its edge */
    static const uint32_t MOSS_BASE[4] = { 0x1c3e12, 0x264c16, 0x17340f, 0x2c561a }, MOSS_TIP[4] = { 0x6a9c34, 0x7cae3e, 0x527e2a, 0x8cbc4a };
    float tot = 0;
    for (int k = 0; k < nm; k++) tot += m[k].s1 - m[k].s0;
    int nf = (int)(tot / u * 1.7f);
    for (int pass = 0; pass < 2; pass++)
        for (int j = 0; j < nf; j++) {
            float pick = prand() * tot; int k = 0;
            while (k < nm - 1 && pick > m[k].s1 - m[k].s0) { pick -= m[k].s1 - m[k].s0; k++; }
            const cap_t *q = &m[k];
            float t = prand(), cx = q->x0 + (q->x1 - q->x0) * t, cy = q->y0 + (q->y1 - q->y0) * t, r = q->r0 + (q->r1 - q->r0) * t;
            float an = prand() < 0.75f ? prange(-1.9f, 1.9f) : prange(-3.14159f, 3.14159f);       /* from straight up */
            float ox = sinf(an), oy = -cosf(an), rf = pass ? prange(0.8f, 1.0f) : prange(0.1f, 0.85f);
            float x = cx + ox * r * rf, y = cy + oy * r * rf;
            float da = an + prange(-0.6f, 0.6f), dx = sinf(da), dy = -cosf(da) + (pass ? 0.35f : 0.6f);   /* they droop */
            float dm = 1 / sqrtf(dx * dx + dy * dy); dx *= dm; dy *= dm;
            int ci = (int)(prand() * 4) & 3;
            float len = (pass ? prange(4, 9) : prange(3, 6)) * u, dk = clampf(-oy * rf, -1, 1);   /* +1 on top .. -1 underneath */
            col_t tip = mixc(col(MOSS_TIP[pass ? ci : (ci + 2) & 3]), col(0x0c1a08), dk < 0 ? -dk * 0.6f : 0.15f - 0.15f * dk);
            frond(g, x, y, dx, dy, len, MOSS_BASE[ci], (uint32_t)tip.r << 16 | (uint32_t)tip.g << 8 | tip.b);
        }

    BAKE_MARK("fronds");
    /* the fern: a rhizome tied on top of the log by the branch, a fan of blades leaning out to the left */
    float rx = ax - 50 * u, ry = ay - 34 * u;
    { cap_t rz = { rx - 10 * u, ry + 2 * u, 2.6f * u, rx + 12 * u, ry - 1 * u, 2.2f * u, 0, 1 };
      frond_c_t rc = { 0x3a3220, 0x4e4a2a }; shape(g, &rz, 1, paint_frond, &rc, 1); }
    enum { NLEAF = 11 };
    static const int ORDER[NLEAF] = { 0, 10, 1, 9, 2, 8, 3, 7, 4, 6, 5 };   /* the outer blades first: the middle ones lie on top */
    for (int o = 0; o < NLEAF; o++) {
        int j = ORDER[o];
        float th = -1.45f + 2.05f * j / (NLEAF - 1) + prange(-0.1f, 0.1f);
        float len = 98 * u * (0.62f + 0.42f * sinf(3.14159f * (j + 0.5f) / NLEAF)) + prange(-8, 8) * u;
        float x0 = rx + prange(-10, 12) * u, y0 = ry + prange(-1, 2) * u;
        leaf(g, x0, y0, th, len, prange(5.0f, 6.4f) * u, (o < 4 ? 0.25f : o < 8 ? 0.12f : 0) + prange(0, 0.06f));
    }
    BAKE_MARK("leaves");
    /* a few sprigs of moss over the rhizome: tied in, not sitting on top */
    for (int j = 0; j < 26; j++) {
        float x = rx + prange(-14, 14) * u, y = ry + prange(-1, 5) * u, an = prange(-1.5f, 1.5f);
        frond(g, x, y, sinf(an), -cosf(an) * 0.6f + 0.4f, prange(4, 7.5f) * u, MOSS_BASE[j & 3], MOSS_TIP[j & 3]);
    }

    BAKE_MARK("sprigs");
    /* what the frame needs: coverage, extents, the cells it fills */
    for (int r = 0; r < PIECE_H; r++) {
        int a = TANK_W, b = -1;
        for (int x = 0; x < TANK_W; x++) {
            int k = (int)(((g->prgba[r * TANK_W + x] >> 24) * 32 + 127) / 255);
            g->pa5[r * TANK_W + x] = (uint8_t)k;
            if (k) { if (x < a) a = x; b = x; }
        }
        g->px0[r] = (int16_t)a; g->px1[r] = (int16_t)b;
        g->pd0[r] = INT16_MAX; g->pd1[r] = -1;
    }
    memset(g->solid, 0, sizeof g->solid);
    for (int cy = 0; cy < GH; cy++)
        for (int cx = 0; cx < GW; cx++) {
            int hit = 0, all = 0;
            for (int y = cy * CELL; y < (cy + 1) * CELL; y++)
                for (int x = cx * CELL; x < (cx + 1) * CELL; x++) {
                    all++;
                    if (y >= PIECE_Y0 && y < TANK_BOT && x < TANK_W && g->pa5[(y - PIECE_Y0) * TANK_W + x] >= 16) hit++;
                }
            g->solid[cy * GW + cx] = (uint8_t)(hit * 255 / all);
        }
    BAKE_MARK("tables");
}

/* the water */
static const uint32_t WATER_DAY[3]   = { 0x4f9eaa, 0x216a7e, 0x123c50 };   /* (darker 11-01: the minnows stand out) */
static const uint32_t WATER_NIGHT[3] = { 0x10283a, 0x0a1a2a, 0x06121e };
static const uint32_t SAND_DAY[2]    = { 0xa08c68, 0x766447 };

static void draw_background(const school_t *s, uint16_t *dst, int stride) {
    static const uint8_t BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    school_scratch_t *g = scratch();
    const uint32_t *w = s->light_on ? WATER_DAY : WATER_NIGHT;
    col_t top = col(w[0]), mid = col(w[1]), bot = col(w[2]);
    col_t s0 = col(SAND_DAY[0]), s1 = col(SAND_DAY[1]);
    float shade = s->light_on ? 1.0f : 0.22f;
    for (int y = 0; y < TANK_H; y++) {
        uint16_t *row = dst + y * stride;
        int x0 = g->gx0[y], x1 = g->gx1[y];
        for (int x = 0; x < TANK_W; x++) {
            if (x < x0 || x > x1) { row[x] = 0; continue; }
            float r, gg, b;
            if (y >= SAND_Y) {
                float t = clampf((y - SAND_Y) / (TANK_BOT - SAND_Y + 1), 0, 1);
                col_t c = mixc(s0, s1, t);
                float grain = ((x * 7 + y * 13) % 5) * 0.012f;          /* a little grain */
                r = c.r * (shade * (1 - grain)); gg = c.g * (shade * (1 - grain)); b = c.b * (shade * (1 - grain));
                if (y < SAND_Y + 2 * S) { r *= 0.85f; gg *= 0.85f; b *= 0.85f; }
            } else {
                float p = (float)y / SAND_Y;
                col_t c = p < 0.5f ? mixc(top, mid, p * 2) : mixc(mid, bot, (p - 0.5f) * 2);
                r = c.r; gg = c.g; b = c.b;
                if (s->light_on) {
                    /* soft light shafts slanting down from the surface, fading with depth */
                    float u = (x + y * 0.35f) / (70.0f * S);
                    float shaft = 0.5f + 0.5f * sinf(u * 1.3f) * sinf(u * 0.47f + 1.1f);
                    shaft = shaft * shaft * shaft * (1 - p) * 0.16f;
                    r += 255 * shaft * 0.55f; gg += 255 * shaft * 0.75f; b += 255 * shaft * 0.7f;
                    if (y < SURFACE_Y) { float k = 1 - y / SURFACE_Y; r += 40 * k; gg += 50 * k; b += 45 * k; }
                }
            }
            if (x == TANK_W / 2) g->water_row[y] = (uint16_t)((((int)clampf(r, 0, 255) >> 3) << 11) | (((int)clampf(gg, 0, 255) >> 2) << 5) | ((int)clampf(b, 0, 255) >> 3));
            if (y >= PIECE_Y0 && y < TANK_BOT) {                       /* the piece over the water (night dims it) */
                uint32_t v = g->prgba[(y - PIECE_Y0) * TANK_W + x];
                if (v >> 24) {                                         /* (premultiplied) */
                    float a = (v >> 24) * (1.0f / 255), k = s->light_on ? 1.0f : 0.26f;
                    r = r * (1 - a) + (v & 255) * k; gg = gg * (1 - a) + ((v >> 8) & 255) * k; b = b * (1 - a) + ((v >> 16) & 255) * k;
                }
            }
            float d = (BAYER[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.5f;
            int R = (int)(r + d * 1.0f), G = (int)(gg + d * 0.5f), B = (int)(b + d * 1.0f);
            R = R < 0 ? 0 : R > 255 ? 255 : R; G = G < 0 ? 0 : G > 255 ? 255 : G; B = B < 0 ? 0 : B > 255 ? 255 : B;
            row[x] = FB16(((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3));
        }
    }
}
static int bg_key(const school_t *s) { return s->light_on ? 1 : 0; }
const uint16_t *school_bg_buf(const school_t *s) { return g_bg && g_bg_key == bg_key(s) ? g_bg : NULL; }
static const uint16_t *g_primed;
void school_fb_primed(const uint16_t *fb) { g_primed = fb; }


/* the minnow's colors (silver): back, flank, the stripe that glints, belly */
static const uint32_t C_BACK = 0x55665a, C_FLANK = 0xaebcbc, C_STRIPE = 0xd8e8ec, C_GLINT = 0xffffff, C_BELLY = 0xdcdcd2, C_FIN = 0xc8d2d0, C_EYE = 0x101418;

/* one row of a fish, in 16.16 fixed point, in a function of its own: the S3's FPU holds no float constants in
   its instructions and both register files are small, so inside the whole fish's drawing the per-pixel loop
   reloaded its constants and spilled on every pixel */
typedef struct {
    int32_t dsq, dtq, bendq, Lq8, Lfq8, eye_r2q;
    int     fin_a;
    uint32_t fin_s, eye_s, pal[8];
    const int32_t *bodyq, *bodykq;         /* the body table (the scratch) */
} fish_q_t;
enum { U24 = 15729, UEYE = 58565, TEYE = 1311 };                          /* 0.24, the eye's 0.24 + 0.86 x 0.76, 0.02 */
static __attribute__((noinline)) void fish_row(uint16_t *row, int xa, int xb, int32_t sq, int32_t tq, const fish_q_t *q) {
    const int32_t dsq = q->dsq, dtq = q->dtq, bendq = q->bendq, Lq8 = q->Lq8;
    const int32_t *bodyq = q->bodyq, *bodykq = q->bodykq;
    for (int x = xa; x <= xb; x++, sq += dsq, tq += dtq) {
        int32_t u = sq + 32768;                                            /* 0 tail tip .. 65536 snout */
#ifdef SCHOOL_STATS
        school_stat[2]++;
#endif
        /* the bend: the tail swings, the head barely */
        int32_t bp = (65536 - u) >> 4, bp2 = (bp * bp) >> 8;              /* (1 - u)^2 */
        int32_t tc = tq - ((bendq * bp2) >> 16), at = tc < 0 ? -tc : tc;
        if (u > U24) {                                                     /* the body */
            int li = ((u - U24) * 84) >> 16;                               /* 64 steps over 0.76 */
            if (li > 64) li = 64;                                          /* (the snout's half-pixel margin runs past the table) */
            int32_t edge = ((bodyq[li] - at) * Lq8) >> 16;               /* 1/256 px inside the edge */
            if (edge <= -128) continue;
            int a5 = edge >= 128 ? 32 : (edge + 128) >> 3;
            if (li > 48) {                                                 /* the eye: only near the snout */
                int32_t ex = ((u - UEYE) * q->Lfq8) >> 16, ey = ((tc - TEYE) * Lq8) >> 16;
                if (ex * ex + ey * ey < q->eye_r2q) { blend_s(&row[x], q->eye_s, a5); continue; }
            }
            int k = (int)(((4 << 24) - tc * bodykq[li]) >> 24);
            k = k < 0 ? 0 : k > 7 ? 7 : k;
            blend_s(&row[x], q->pal[k], a5);
        } else {                                                           /* the tail fin: a fork */
            int32_t fu = ((U24 - u) * 4267) >> 10;                         /* 0 at the root .. 65536 at the tip */
            int32_t edge = ((2294 + ((fu * 7864) >> 16) - at) * Lq8) >> 16;   /* half-height 0.035 + 0.12 fu */
            if (edge <= -128) continue;
            if (fu > 36045 && at < (((fu - 36045) * 10486) >> 16)) continue;   /* the fork's notch */
            int a5 = edge >= 128 ? q->fin_a : ((edge + 128) * q->fin_a) >> 8;
            blend_s(&row[x], q->fin_s, a5);
        }
    }
}

/* narrow [k0, k1] to the steps k where lo <= v + dv * k <= hi (idv = 1 / dv, 0 when dv is ~0: no division
   per row - the S3's FPU has no divide, each one is a library call) */
static inline void clip_lin(float v, float dv, float idv, float lo, float hi, float *k0, float *k1) {
    if (idv == 0) { if (v < lo || v > hi) *k1 = -1; return; }
    float a = (lo - v) * idv, b = (hi - v) * idv;
    if (a > b) { float t = a; a = b; b = t; }
    if (a > *k0) *k0 = a;
    if (b < *k1) *k1 = b;
}

static void draw_fish(uint16_t *fb, int stride, school_scratch_t *g, const sfish_t *f, const uint16_t *bgsrc, float lit, bool behind) {
    const float zn = f->z / DEPTH;                                        /* 0 far .. 1 near */
    float L = f->size * (0.78f + 0.4f * zn);
    float vx = f->vx, vy = f->vy, vz = f->vz;
    float vxy2 = vx * vx + vy * vy, v2 = vxy2 + vz * vz;
    if (vxy2 < 1e-6f) { vx = 1; vy = 0; vxy2 = 1; v2 = 1; }
    float ixy = inv_sqrt(vxy2), iv = inv_sqrt(v2);
    float fore = vxy2 * ixy * iv, ifore = v2 * iv * ixy;                   /* swimming toward or away: shorter */
    if (fore < 0.42f) { fore = 0.42f; ifore = 1 / 0.42f; }
    float dx = vx * ixy, dy = vy * ixy;
    float px = dx >= 0 ? dy : -dy, py = dx >= 0 ? -dx : dx;                /* the back is toward the top of the glass */
    float Ls = L * fore;
    /* the tail: body bend with the beat */
    float beat = g->sinlut[(int)(f->phase * 256) & 255], amp = 0.06f + 0.03f * f->burst, bend = amp * beat;
    const float eye_r2 = 0.55f * S * S + 0.35f;

    /* the water behind it tints it by depth (far fish fade into the blue) */
    int cx = (int)f->x, cy = (int)f->y;
    cx = cx < 0 ? 0 : cx >= TANK_W ? TANK_W - 1 : cx;
    cy = cy < 0 ? 0 : cy >= TANK_H ? TANK_H - 1 : cy;
    bool over = cy >= PIECE_Y0 && cy < TANK_BOT && g->pa5[(cy - PIECE_Y0) * TANK_W + cx];
    col_t water = from565(over ? g->water_row[cy] : bgsrc ? FB16(bgsrc[cy * TANK_W + cx]) : 0x2c4f);
    float fade = 0.55f * (1 - zn);
    const col_t black = { 0, 0, 0 };
    col_t back = mixc(mixc(col(C_BACK), water, fade), black, 1 - lit);
    col_t flank = mixc(mixc(col(C_FLANK), water, fade), black, 1 - lit);
    col_t stripe = mixc(mixc(mixc(col(C_STRIPE), col(C_GLINT), f->glint), water, fade * (1 - 0.6f * f->glint)), black, 1 - lit);
    col_t belly = mixc(mixc(col(C_BELLY), water, fade), black, 1 - lit);
    col_t fin = mixc(mixc(col(C_FIN), water, fade + 0.15f), black, 1 - lit);
    col_t eye = mixc(col(C_EYE), water, fade * 0.6f);
    /* a small per-fish tint: no two exactly alike */
    int ht = (int)f->hue - 4;
    back.g = (uint8_t)clampf(back.g + ht * 2, 0, 255); flank.b = (uint8_t)clampf(flank.b + ht * 2, 0, 255);
    /* the palette across the body, back (+1) to belly (-1): 8 steps */
    uint32_t pal[8];
    for (int k = 0; k < 8; k++) {
        float q = 1 - k * (2.0f / 7);                                     /* +1 .. -1 */
        col_t c = q > 0.45f ? mixc(flank, back, (q - 0.45f) / 0.55f)
                : q > 0.0f ? mixc(stripe, flank, q / 0.45f)
                : q > -0.4f ? mixc(stripe, flank, -q / 0.4f)
                : mixc(flank, belly, (-q - 0.4f) / 0.6f);
        pal[k] = spread565(to565(c));
    }
    uint32_t fin_s = spread565(to565(fin)), eye_s = spread565(to565(eye));
    int fin_a = (int)(17 * (0.7f + 0.3f * zn));

    /* the box: head at +0.5 L (along), tail tip at -0.5 L; the body is 0.24 L tall at most */
    float hx = dx * Ls * 0.5f, hy = dy * Ls * 0.5f, wx = px * L * 0.27f, wy = py * L * 0.27f;
    float bx0 = f->x - fabsf(hx) - fabsf(wx) - 1, bx1 = f->x + fabsf(hx) + fabsf(wx) + 1;
    float by0 = f->y - fabsf(hy) - fabsf(wy) - 1, by1 = f->y + fabsf(hy) + fabsf(wy) + 1;
    int y0 = (int)(by0 + 1024) - 1024, y1 = (int)(by1 + 1025) - 1024, xa = (int)(bx0 + 1024) - 1024, xb = (int)(bx1 + 1025) - 1024;
    y0 = y0 < 0 ? 0 : y0;
    y1 = y1 > TANK_H - 1 ? TANK_H - 1 : y1;
    if (behind)                                                         /* the frame puts the piece back over these rows */
        for (int y = y0 < PIECE_Y0 ? PIECE_Y0 : y0; y <= y1 && y < TANK_BOT; y++) {
            int r = y - PIECE_Y0;
            if (g->px1[r] < g->px0[r] || xb < g->px0[r] || xa > g->px1[r]) continue;
            if (xa < g->pd0[r]) g->pd0[r] = (int16_t)xa;
            if (xb > g->pd1[r]) g->pd1[r] = (int16_t)xb;
        }
    const float iL = inv_sqrt(L * L), iLs = iL * ifore;                    /* no divide on this FPU: a library call */
    const float dsx = dx * iLs, dtx = px * iL;                              /* along and across, per pixel step in x */
    const float ids = dsx > -1e-6f && dsx < 1e-6f ? 0 : dsx * inv_sqrt(dsx * dsx) * inv_sqrt(dsx * dsx);
    const float idt = dtx > -1e-6f && dtx < 1e-6f ? 0 : dtx * inv_sqrt(dtx * dtx) * inv_sqrt(dtx * dtx);
    fish_q_t fq = { (int32_t)(dsx * 65536.0f), (int32_t)(dtx * 65536.0f), (int32_t)(bend * 65536.0f),
                    (int32_t)(L * 256.0f), (int32_t)(L * fore * (256.0f / 0.76f)), (int32_t)(eye_r2 * 65536.0f),
                    fin_a, fin_s, eye_s, { 0 }, g->bodyq, g->bodykq };
    memcpy(fq.pal, pal, sizeof pal);
    for (int y = y0; y <= y1; y++) {
        int rx0 = xa > g->gx0[y] ? xa : g->gx0[y], rx1 = xb < g->gx1[y] ? xb : g->gx1[y];
        float ry = y + 0.5f - f->y;
        /* along (s) and across (t), at x = rx0, stepping per pixel */
        float rx = rx0 + 0.5f - f->x;
        float s = (rx * dx + ry * dy) * iLs, t = (rx * px + ry * py) * iL;
        const float ds = dsx, dt = dtx;
        /* only the pixels of this row inside the fish's strip (|s| <= 0.5, |t| <= 0.25 and the edge's half pixel) */
        float k0 = 0, k1 = (float)(rx1 - rx0);
        clip_lin(s, ds, ids, -0.5f - 0.5f * iLs, 0.5f + 0.5f * iLs, &k0, &k1);
        clip_lin(t, dt, idt, -0.25f - 0.5f * iL, 0.25f + 0.5f * iL, &k0, &k1);
#ifdef SCHOOL_STATS
        school_stat[3]++;
#endif
        if (k0 > k1) continue;
        int ka = (int)k0; ka += k0 > ka; int kb = (int)k1;              /* ceil, floor: both >= 0 here (no library calls) */
        s += ds * ka; t += dt * ka;
        fish_row(fb + y * stride, rx0 + ka, rx0 + kb, (int32_t)(s * 65536.0f), (int32_t)(t * 65536.0f), &fq);
    }
}

static void draw_pellet(uint16_t *fb, int stride, const school_scratch_t *g, const spellet_t *p) {
    float r = 1.3f * S;
    int x0 = (int)(p->x - r - 1), x1 = (int)(p->x + r + 1), y0 = (int)(p->y - r - 1), y1 = (int)(p->y + r + 1);
    uint32_t c = spread565(to565(col(0x8a5a2a))), hi = spread565(to565(col(0xd09858)));
    for (int y = y0; y <= y1; y++) {
        if ((unsigned)y >= TANK_H) continue;
        for (int x = x0; x <= x1; x++) {
            if (x < g->gx0[y] || x > g->gx1[y]) continue;
            float dx = x + 0.5f - p->x, dy = y + 0.5f - p->y, d = sqrtf(dx * dx + dy * dy);
            float e = r - d + 0.5f;
            if (e <= 0) continue;
            int a5 = e >= 1 ? 32 : (int)(e * 32);
            blend_s(&fb[y * stride + x], dy < -0.2f ? hi : c, a5);
        }
    }
}

static void draw_ripple(uint16_t *fb, int stride, const school_scratch_t *g, const sripple_t *r) {
    float rad = (1 + r->t * 7) * BL0, a = 1 - r->t / 0.9f;
    if (a <= 0) return;
    uint32_t c = spread565(to565(col(0xe8f6ff)));
    int n = (int)(rad * 6.3f / 1.5f) + 8;
    for (int k = 0; k < n; k++) {
        float an = k * TAU_F / n;
        int x = (int)(r->x + cosf(an) * rad), y = (int)(r->y + sinf(an) * rad);
        if ((unsigned)y >= TANK_H || x < g->gx0[y] || x > g->gx1[y]) continue;
        blend_s(&fb[y * stride + x], c, (int)(a * 14));
    }
}

/* the piece back over the fish drawn behind it: only the rows and spans they touched */
static void piece_restore(school_scratch_t *g, uint16_t *fb, int stride, const uint16_t *bg) {
    for (int r = 0; r < PIECE_H; r++) {
        int a = g->pd0[r], b = g->pd1[r];
        if (b < a) continue;
        g->pd0[r] = INT16_MAX; g->pd1[r] = -1;
        if (a < g->px0[r]) a = g->px0[r];
        if (b > g->px1[r]) b = g->px1[r];
        const int y = PIECE_Y0 + r;
        const uint8_t *pa = &g->pa5[r * TANK_W];
        uint16_t *row = fb + y * stride;
        const uint16_t *brow = bg + y * TANK_W;
        for (int x = a; x <= b; x++) {
            int k = pa[x];
            if (!k) continue;
            if (k >= 32) row[x] = brow[x];
            else blend_s(&row[x], spread565(FB16(brow[x])), k);
        }
    }
}

void school_render(const school_t *s, uint16_t *fb, int stride) {
    school_scratch_t *g = scratch();
    PROF_T0(t0);
    const uint16_t *bgsrc = NULL;
    if (g_bg) {
        if (g_bg_key != bg_key(s)) { draw_background(s, g_bg, TANK_W); g_bg_key = bg_key(s); }
        bgsrc = g_bg;
        if (fb != g_bg && fb != g_primed) {
            if (stride == TANK_W) memcpy(fb, g_bg, sizeof(uint16_t) * TANK_W * TANK_H);
            else for (int y = 0; y < TANK_H; y++) memcpy(fb + y * stride, g_bg + y * TANK_W, sizeof(uint16_t) * TANK_W);
        }
    } else draw_background(s, fb, stride);
    g_primed = NULL;
    PROF_ADD(3, t0);
    PROF_T0(t1);
    /* far to near: the order changes little frame to frame, so an insertion sort is nearly free */
    if (g->zn != s->n) { for (int i = 0; i < s->n; i++) g->zorder[i] = (uint16_t)i; g->zn = s->n; }
    for (int i = 1; i < s->n; i++) {
        uint16_t v = g->zorder[i]; float z = s->f[v].z; int j = i;
        while (j > 0 && s->f[g->zorder[j - 1]].z > z) { g->zorder[j] = g->zorder[j - 1]; j--; }
        g->zorder[j] = v;
    }
    const float lit = s->light_on ? 1.0f : 0.32f;
    int i = 0;
#ifndef SCHOOL_NO_PIECE                                                  /* (s3bench's A/B: the cost of the layering) */
    if (bgsrc) {                                                        /* behind the piece, then the piece, then in front */
        for (; i < s->n && s->f[g->zorder[i]].z < PIECE_Z; i++) draw_fish(fb, stride, g, &s->f[g->zorder[i]], bgsrc, lit, true);
        piece_restore(g, fb, stride, bgsrc);
    }
#endif
    for (; i < s->n; i++) draw_fish(fb, stride, g, &s->f[g->zorder[i]], bgsrc, lit, false);
    PROF_ADD(4, t1);
    PROF_T0(t2);
    for (int p = 0; p < SCHOOL_PELLETS; p++) if (s->p[p].live) draw_pellet(fb, stride, g, &s->p[p]);
    for (int i = 0; i < SCHOOL_RIPPLES; i++) if (s->r[i].live) draw_ripple(fb, stride, g, &s->r[i]);
    PROF_ADD(5, t2);
}
