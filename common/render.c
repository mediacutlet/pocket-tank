/* render.c — porthole look: deep gradient water, AMOLED-black floor, procedural
 * fish (body polygon + animated tail + earned markings), bubbles, seagrass
 * the keeper trims. Everything is drawn
 * into a bare RGB565 buffer; night dims the palette. */
#include "render.h"
#include "ui.h"
#include "advanced.h"
#include "icons.h"
#include "progression.h"
#include "tank_events.h"
#include "setup.h"
#include "version.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define TAU 6.2831853f

/* a draw target: fb + stride + night dim, plus the window it covers in tank
 * coordinates (ox,oy,w,h) - the frame is (0,0,TANK_W,TANK_H); the stats card
 * cache is a small sprite that still gets drawn in tank coordinates */
typedef struct { uint16_t *fb; int stride; float dim; int ox, oy, w, h; } ctx_t;
static ctx_t ctx_full(uint16_t *fb, int stride, float dim) {
    ctx_t c = { fb, stride, dim, 0, 0, TANK_W, TANK_H }; return c;
}
/* PAGE space (render.h): the full-screen pages, the prompts and the exported
 * drawing helpers put their 448 x 368 layout in the middle of the frame - a
 * ctx whose origin is the page's corner. In the rectangle that IS the frame. */
static bool g_dirty_hold;                          /* below, with the dirty mask */
static ctx_t ctx_page(uint16_t *fb, int stride) {
    ctx_t c = { fb, stride, 1.0f, -PAGE_X, -PAGE_Y, TANK_W, TANK_H };
    g_dirty_hold = true;                           /* a page's coordinates must never reach the mask */
    return c;
}
#define CTX_IN(c, x, y) ((unsigned)((x) - (c)->ox) < (unsigned)(c)->w && (unsigned)((y) - (c)->oy) < (unsigned)(c)->h)
#define CTX_PX(c, x, y) ((c)->fb[((y) - (c)->oy) * (c)->stride + ((x) - (c)->ox)])

/* Dirty mask (2026-09-01): one bit per pixel, set by everything drawn over
 * the baked scene (px, span, blends), cleared every frame. The porthole
 * vignette re-apply walks the mask instead of comparing each pixel with the
 * scene cache (32 pixels per word, no scene reads), and a pixel that already
 * had its vignette applied inline (frond spans, span_final) is simply not
 * marked, so no later rect darkens it twice. (A first version tagged the
 * green LSB instead and cleared it across the scene - that halved the color
 * steps of the dark vignette falloff into visible contour rings. Colors are
 * untouched now.) */
#define DIRTY_WORDS_PER_ROW ((TANK_W + 31) / 32)   /* 14 (15 on the 466 px bowl) */
static uint32_t *g_dirty = NULL;
void render_set_dirty_mask(uint32_t *buf) { g_dirty = buf; }
/* A PAGE draws in its own coordinates (ctx_page: on the bowl its origin is
 * not the frame's, and its y runs negative above the page) - marked as they
 * are, those wrote in front of the mask (2026-10-01: the settings page's
 * fill took the round board down). Nothing reads the mask after render_tank's
 * sweep, so from the first page ctx of a frame until the next render_tank
 * nothing is marked. */
static bool g_dirty_hold;
static inline void dirty_px(int x, int y) {
    if (g_dirty && !g_dirty_hold) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] |= 1u << (x & 31);
}
static inline void dirty_span(int x0, int x1, int y) {
    if (!g_dirty || g_dirty_hold) return;
    uint32_t *row = g_dirty + y * DIRTY_WORDS_PER_ROW;
    int w0 = x0 >> 5, w1 = x1 >> 5;
    if (w0 == w1) { row[w0] |= (0xFFFFFFFFu >> (31 - (x1 & 31))) & (0xFFFFFFFFu << (x0 & 31)); return; }
    row[w0] |= 0xFFFFFFFFu << (x0 & 31);
    for (int w = w0 + 1; w < w1; w++) row[w] = 0xFFFFFFFFu;
    row[w1] |= 0xFFFFFFFFu >> (31 - (x1 & 31));
}

static inline void dirty_clear_span(int x0, int x1, int y) {   /* the span's marks off, a word at a time */
    if (!g_dirty || g_dirty_hold) return;
    uint32_t *row = g_dirty + y * DIRTY_WORDS_PER_ROW;
    int w0 = x0 >> 5, w1 = x1 >> 5;
    if (w0 == w1) { row[w0] &= ~((0xFFFFFFFFu >> (31 - (x1 & 31))) & (0xFFFFFFFFu << (x0 & 31))); return; }
    row[w0] &= ~(0xFFFFFFFFu << (x0 & 31));
    for (int w = w0 + 1; w < w1; w++) row[w] = 0;
    row[w1] &= ~(0xFFFFFFFFu >> (31 - (x1 & 31)));
}

static uint16_t rgb565(uint32_t rgb, float dim) {
    uint32_t r = (uint32_t)(((rgb >> 16) & 255) * dim);
    uint32_t g = (uint32_t)(((rgb >> 8) & 255) * dim);
    uint32_t b = (uint32_t)((rgb & 255) * dim);
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* bounding box of everything drawn while g_bb_on (render_tank wraps each
 * fish in it, so its vignette rect is exactly the pixels it touched instead
 * of a fixed 88 px box - fewer PSRAM reads in the re-apply sweep) */
static bool g_bb_on; static int g_bb_x0, g_bb_y0, g_bb_x1, g_bb_y1;
static inline void bb_add(int x0, int x1, int y) {
    if (!g_bb_on) return;
    if (x0 < g_bb_x0) g_bb_x0 = x0;
    if (x1 > g_bb_x1) g_bb_x1 = x1;
    if (y < g_bb_y0) g_bb_y0 = y;
    if (y > g_bb_y1) g_bb_y1 = y;
}
/* The TOUCHED mask (2026-10-23, the frame rate on every board): one bit per
 * pixel the frame WROTE over the baked scene - every writer marks it, in the
 * FRAME's coordinates (a page's ctx has its own origin), never held, final
 * stores included. It is what puts the scene back under the frame once it
 * has left the wire (render_restore_touched: the touched words copied from
 * the scene), in place of the whole-frame prefetch that moved 920 KB over
 * the PSRAM bus every frame on the square - the bus, not the CPU, is the
 * wall on these boards. The firmware keeps one per framebuffer and hands
 * the frame's over before render_tank (render_set_touched_mask); a frame
 * that is a whole page says so (render_touch_all). The sim's
 * --selftest-restore draws through this path against a whole re-prime and
 * fails on any ghost. */
static uint32_t *g_touched;
void render_set_touched_mask(uint32_t *buf) { g_touched = buf; }
static inline void touch_span_f(int x0, int x1, int y) {          /* frame coordinates */
    if (!g_touched || (unsigned)y >= (unsigned)TANK_H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > TANK_W - 1) x1 = TANK_W - 1;
    if (x0 > x1) return;
    uint32_t *row = g_touched + y * DIRTY_WORDS_PER_ROW;
    int w0 = x0 >> 5, w1 = x1 >> 5;
    if (w0 == w1) { row[w0] |= (0xFFFFFFFFu >> (31 - (x1 & 31))) & (0xFFFFFFFFu << (x0 & 31)); return; }
    row[w0] |= 0xFFFFFFFFu << (x0 & 31);
    for (int w = w0 + 1; w < w1; w++) row[w] = 0xFFFFFFFFu;
    row[w1] |= 0xFFFFFFFFu >> (31 - (x1 & 31));
}
static inline void touch_px_f(int x, int y) {
    if (g_touched && (unsigned)x < (unsigned)TANK_W && (unsigned)y < (unsigned)TANK_H) g_touched[y * DIRTY_WORDS_PER_ROW + (x >> 5)] |= 1u << (x & 31);
}
#define TOUCH_SPAN(c, x0, x1, y) touch_span_f((x0) - (c)->ox, (x1) - (c)->ox, (y) - (c)->oy)
#define TOUCH_PX(c, x, y)        touch_px_f((x) - (c)->ox, (y) - (c)->oy)
void render_touch_all(void) { if (g_touched) memset(g_touched, 0xff, (size_t)RENDER_DIRTY_WORDS * 4); }
/* THE GRASS CACHE (2026-10-24, Strato: "keep the back grass in a cached scene
 * that updates a few times a second"). The grass was a third of a full frame
 * (27 ms on the square), and its sway is slow - a frond's tip moves under 4 px
 * a second. So the BACK half of it (the beds behind the fish and the even
 * fronds of the woven beds) is painted into a copy of the scene, g_sceneg,
 * at most every g_sg_period seconds; frames are primed from that copy and
 * draw only the front fronds live. The pieces behind the grass that animate
 * every frame (a crown's tentacles, the candle, the lava and the volcano's
 * bubbles) must stay UNDER it: the cache records which pixels the back grass
 * covers (g_bgmask) and those overlays skip them (g_under). A scene rebake,
 * a placement page or a page in between: the frame draws its grass as before
 * and the cache is rebuilt at its end. Off unless the platform hands over the
 * buffers and turns it on (director `grasscache`, the sim's PT_GRASS_CACHE). */
static uint16_t *g_sceneg;                     /* the scene + the back grass */
static uint32_t *g_bgmask;                     /* the back grass's pixels in it (RENDER_DIRTY_WORDS) */
static bool      g_sg_on;
static float     g_sg_period = 0.16f;
static bool      g_sg_ok;
static unsigned  g_sg_scene_ep, g_sg_ep = 1;
static float     g_sg_clock;
static int       g_sg_top = 0;                 /* the highest row the cached grass touched */
static const uint32_t *g_under;                /* while the overlays behind are drawn: pixels to leave alone */
static bool      g_bg_rec;                     /* while the cache's grass is painted: record into g_bgmask */
static int64_t   g_sg_us; static int g_sg_n;   /* the refreshes' cost (the director's line) */
void render_set_grass_cache(uint16_t *buf, uint32_t *mask) { g_sceneg = buf; g_bgmask = mask; g_sg_ok = false; }
void render_set_grass_cache_on(bool on, int period_ms) { bool was = g_sg_on; g_sg_on = on && g_sceneg && g_bgmask; if (period_ms > 0) g_sg_period = period_ms / 1000.0f; if (!was || !g_sg_on) g_sg_ok = false; }   /* (a new period alone keeps the cache) */
bool render_grass_cache_on(void) { return g_sg_on; }
int  render_grass_cache_ms(void) { return (int)(g_sg_period * 1000 + 0.5f); }
void render_grass_cache_prof(int64_t *us, int *n) { *us = g_sg_us; *n = g_sg_n; g_sg_us = 0; g_sg_n = 0; }
static inline bool under_bg(int x, int y) {
    return g_under && (unsigned)x < (unsigned)TANK_W && (unsigned)y < (unsigned)TANK_H && ((g_under[y * DIRTY_WORDS_PER_ROW + (x >> 5)] >> (x & 31)) & 1);
}
static inline void bg_mark(int x0, int x1, int y) {
    if (!g_bg_rec || (unsigned)y >= (unsigned)TANK_H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > TANK_W - 1) x1 = TANK_W - 1;
    if (x0 > x1) return;
    uint32_t *row = g_bgmask + y * DIRTY_WORDS_PER_ROW;
    int w0 = x0 >> 5, w1 = x1 >> 5;
    if (w0 == w1) row[w0] |= (0xFFFFFFFFu >> (31 - (x1 & 31))) & (0xFFFFFFFFu << (x0 & 31));
    else { row[w0] |= 0xFFFFFFFFu << (x0 & 31); for (int w = w0 + 1; w < w1; w++) row[w] = 0xFFFFFFFFu; row[w1] |= 0xFFFFFFFFu >> (31 - (x1 & 31)); }
    if (y < g_sg_top) g_sg_top = y;
}
static void px(ctx_t *c, int x, int y, uint16_t col) {
    if (CTX_IN(c, x, y)) {
        CTX_PX(c, x, y) = FB_SWAP16(col);
        bb_add(x, x, y);
        dirty_px(x, y); TOUCH_PX(c, x, y);
    }
}

/* A source color dimmed ONCE per shape (night palette), never per pixel:
 * the float multiply + conversions were the bulk of every blended pixel's
 * cost (vegetation, algae film, light shafts, the stats card backdrop). */
typedef struct { int r, g, b; uint16_t v; } src_t;
static inline src_t src_color(uint32_t rgb, float dim) {
    src_t s;
    s.r = (int)(((rgb >> 16) & 255) * dim);
    s.g = (int)(((rgb >> 8) & 255) * dim);
    s.b = (int)((rgb & 255) * dim);
    s.v = (uint16_t)(((s.r >> 3) << 11) | ((s.g >> 2) << 5) | (s.b >> 3));
    return s;
}
/* alpha 0..255 blend of a pre-dimmed source onto one framebuffer pixel */
static inline void blend565(uint16_t *p, const src_t *s, int a) {
    uint16_t cur = FB_SWAP16(*p);
    int dr = (cur >> 11) << 3, dg = ((cur >> 5) & 63) << 2, db = (cur & 31) << 3;
    int r = dr + ((s->r - dr) * a >> 8), g = dg + ((s->g - dg) * a >> 8), b = db + ((s->b - db) * a >> 8);
    *p = FB_SWAP16((uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
}
static inline void px_blend_s(ctx_t *c, int x, int y, const src_t *s, int a) {
    if (CTX_IN(c, x, y) && !under_bg(x, y)) { blend565(&CTX_PX(c, x, y), s, a); dirty_px(x, y); TOUCH_PX(c, x, y); }
}
/* alpha 0..255 blend onto existing pixel (one-off pixels; shapes hoist) */
static void px_blend(ctx_t *c, int x, int y, uint32_t rgb, int a) {
    src_t s = src_color(rgb, c->dim);
    px_blend_s(c, x, y, &s, a);
}
/* horizontal span [x0,x1] on row y, clipped to the frame, pre-dimmed source */
static inline void span(ctx_t *c, int x0, int x1, int y, const src_t *s, int alpha) {
    if ((unsigned)(y - c->oy) >= (unsigned)c->h) return;
    if (x0 < c->ox) x0 = c->ox;
    if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
    if (x0 > x1) return;
    if (g_under) {                                   /* under the cached grass: pixel by pixel, only what shows (marking the rest would vignette the grass twice) */
        uint16_t *q = &CTX_PX(c, x0, y); uint16_t sv = FB_SWAP16(s->v);
        for (int x = x0; x <= x1; x++, q++) { if (under_bg(x, y)) continue; if (alpha >= 255) *q = sv; else blend565(q, s, alpha); dirty_px(x, y); TOUCH_PX(c, x, y); }
        bb_add(x0, x1, y);
        return;
    }
    bb_add(x0, x1, y);
    dirty_span(x0, x1, y); TOUCH_SPAN(c, x0, x1, y);
    uint16_t *p = &CTX_PX(c, x0, y);
    if (alpha >= 255) { uint16_t sv = FB_SWAP16(s->v); for (int x = x0; x <= x1; x++) *p++ = sv; }
    else              for (int x = x0; x <= x1; x++) blend565(p++, s, alpha);
}

/* row half-width of a unit circle at |t| = k/64: sqrt(1 - t^2), tabled -
 * the ESP32-S3 computes sqrtf in software and a fouled glass alone asks for
 * ~7000 of them per frame (algae blobs); the quantisation is sub-pixel */
static float ell_half(float t) {
    static float lut[66]; static bool filled;
    if (!filled) { for (int k = 0; k <= 65; k++) { float u = k / 64.0f; lut[k] = u < 1 ? sqrtf(1 - u * u) : 0; } filled = true; }
    if (t < 0) t = -t;
    return t >= 1 ? 0 : lut[(int)(t * 64 + 0.5f)];
}
static void fill_ellipse(ctx_t *c, float cx, float cy, float rx, float ry,
                         uint32_t rgb, int alpha) {
    src_t s = src_color(rgb, c->dim);
    int y0 = (int)(cy - ry), y1 = (int)(cy + ry);
    for (int y = y0; y <= y1; y++) {
        float w = ell_half((y - cy) / ry);
        if (w <= 0) continue;
        float half = rx * w;
        span(c, (int)(cx - half), (int)(cx + half), y, &s, alpha);
    }
}

/* sine from a 256-entry table: the frond sway only has to look continuous,
 * and the ESP32-S3 computes sinf in software (~1000 per frame at full
 * canopy). Phases here are non-negative; & 255 keeps any sign honest. */
static float fast_sin(float x) {
    static float lut[256]; static bool filled;
    if (!filled) { for (int i = 0; i < 256; i++) lut[i] = sinf(i * (TAU / 256)); filled = true; }
    return lut[(int)(x * (256.0f / TAU)) & 255];
}

/* filled convex polygon, points in world space */
static void fill_poly(ctx_t *c, const float *xs, const float *ys, int n, uint32_t rgb) {
    float miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < n; i++) { if (ys[i] < miny) miny = ys[i]; if (ys[i] > maxy) maxy = ys[i]; }
    src_t s = src_color(rgb, c->dim);
    for (int y = (int)miny; y <= (int)maxy; y++) {
        float x0 = 1e9f, x1 = -1e9f;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            float ay = ys[i], by = ys[j];
            if ((ay <= y && by > y) || (by <= y && ay > y)) {
                float x = xs[i] + (xs[j] - xs[i]) * (y - ay) / (by - ay);
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
            }
        }
        span(c, (int)x0, (int)x1, y, &s, 255);
    }
}

static uint32_t mix(uint32_t a, uint32_t b, float p) {
    int ar = a >> 16 & 255, ag = a >> 8 & 255, ab = a & 255;
    int br = b >> 16 & 255, bg = b >> 8 & 255, bb = b & 255;
    return ((uint32_t)(ar + (br - ar) * p) << 16) |
           ((uint32_t)(ag + (bg - ag) * p) << 8) |
            (uint32_t)(ab + (bb - ab) * p);
}


/* the turn (2026-09-29, was a roll that flattened the fish to a line): the
 * fish's yaw (tank.c) FORESHORTENS it along its length - side-on at +-1,
 * head-on at 0 - and the sign mirrors it, so its back stays up. The head's
 * yaw leads and the tail's follows, blended along the body, so mid-turn it
 * bends. Eased here so the swing starts and lands softly. */
static float yaw_ease(float y) { return y * (1.5f - 0.5f * y * y); }
#define YAW_MIN 0.06f                    /* the outline never folds to a line (the body's
                                         * thickness is drawn on its own: FISH_THICK) */
#define FISH_THICK 4.2f                  /* half the body's thickness, local px: turned toward
                                         * the glass a fish shows a rounded oval, not a card */
/* the yaw at body point lx: the tail's at the tail tip (-26), the head's at the nose (16) */
static float yaw_at(float yh, float yt, float lx) {
    float u = (lx + 26) * (1.0f / 42);
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    float s = yt + (yh - yt) * u;
    if (s > -YAW_MIN && s < YAW_MIN) s = s < 0 ? -YAW_MIN : YAW_MIN;
    return s;
}

/* The fish's body is a ledger of its life: stage sets size (tank.c) and fin
 * elaboration, hunger drains saturation, and earned markings stay:
 *   elder -> a longer tail and a dorsal crest. All procedural, zero flash. */
/* `mark` scales the accent stripes: 1 in the tank (every stage wears the
 * same 2 x 5 px marks), the preview's size so they read on a big fish */
/* `floor_y`: the sand line under a fish in the tank (FISH_FLOOR_Y; the
 * previews pass none) - nose up, a fish close to it lifts off level */
static void draw_fish_core(ctx_t *c, const fish_t *f, float clock, bool asleep, float mark, float floor_y) {
    float tail = sinf(clock * (8 + f->speed * 0.055f) + f->wander) *
                 (0.32f + f->speed * 0.007f);
    float stress = f->stress / 10.0f;
    float pale = f->hunger > 7 ? (f->hunger - 7) / 3.0f * 0.35f : 0;   /* hungry = washed out */
    /* pitch: the heading folded to face right (the climb or dive), then the
     * yaw scales x: X = s(lx) * (lx cos p - ly sin p), Y = lx sin p + ly cos p.
     * The BODY tilts less than the path: a fish rises and sinks on its fins
     * with its back near level, so the travel angle is eased toward a cap of
     * ~35 deg (~55 at a dash) - never nose-straight-up (Strato 09-29: "too
     * much time in a vertical position") */
    float pmax = 0.61f + 0.35f * fminf(fmaxf((f->speed - 35) * (1 / 40.0f), 0), 1);
    float p = pmax * tanhf(0.8f * atan2f(sinf(f->heading), fabsf(cosf(f->heading))) / pmax);
    /* off the sand (2026-10-24, Strato: a fish leaving the pellets on the
     * floor swung its tail through the sand and a castle's footing): nose
     * up, the tail swings DOWN - ~30 local px under the centre at a dash, an
     * elder's tip even further. Close to the floor the climb starts LEVEL
     * and tilts as the fish rises: level with the tail's flap just clear of
     * the sand (16 local px of water under the centre), the full tilt 20 px
     * higher. A fish rises off the bottom on its fins, back level. The
     * painter's window stops at the sand line too (draw_fish), so whatever
     * still reaches it brushes the floor instead of showing through it. */
    if (p < 0 && floor_y < 1e8f) {
        float k = ((floor_y - f->y) / f->size - 16) * (1 / 20.0f);
        p *= k < 0 ? 0 : k > 1 ? 1 : k;
    }
    float cp = cosf(p), sp = sinf(p);
    float yh = yaw_ease(f->yaw), yt = yaw_ease(f->yaw_tail);
    bool elder = f->stage == STAGE_ELDER, grown = f->stage >= STAGE_ADULT;
    float tail_len = elder ? 1.22f : 1.0f;
    uint32_t body = mix(mix(f->color, 0xf25b65, stress * 0.22f), 0x7a8a8e, pale);
    uint32_t fin  = mix(mix(f->fin, 0xf25b65, stress * 0.18f), 0x5a6a6e, pale);
#define TX(lx, ly) (f->x + yaw_at(yh, yt, lx) * ((lx) * cp - (ly) * sp) * f->size)
#define TY(lx, ly) (f->y + ((lx) * sp + (ly) * cp) * f->size)

    /* tail fin (triangle, flaps with tail phase) */
    {
        float lx[3] = {-11, -26 * tail_len, -26 * tail_len}, ly[3] = {0, -10 - tail * 8, 10 + tail * 8};
        float xs[3], ys[3];
        for (int i = 0; i < 3; i++) { xs[i] = TX(lx[i], ly[i]); ys[i] = TY(lx[i], ly[i]); }
        fill_poly(c, xs, ys, 3, fin);
    }
    /* dorsal crest: adults a small fin, elders a taller one */
    if (grown) {
        float h = elder ? -17 : -13;
        float lx[3] = {6, -2, -9}, ly[3] = {-8, h, -8};
        float xs[3], ys[3];
        for (int i = 0; i < 3; i++) { xs[i] = TX(lx[i], ly[i]); ys[i] = TY(lx[i], ly[i]); }
        fill_poly(c, xs, ys, 3, fin);
    }
    /* body: sampled outline of the prototype's quadratic silhouette */
    {
        static const float blx[10] = { 16, 10, 2, -8, -15, -18, -15, -8, 2, 10 };
        static const float bly[10] = { 0, -7, -10, -9, -6, 0, 6, 9, 10, 7 };
        float xs[10], ys[10];
        for (int i = 0; i < 10; i++) { xs[i] = TX(blx[i], bly[i]); ys[i] = TY(blx[i], bly[i]); }
        fill_poly(c, xs, ys, 10, body);
        /* its thickness: the cross-section at the deepest point, widening as
         * the fish turns toward the glass (sin of the yaw angle) */
        float sm = yaw_at(yh, yt, 2), rx = FISH_THICK * f->size * sqrtf(fmaxf(0, 1 - sm * sm));
        if (rx >= 1) fill_ellipse(c, TX(2, 0), TY(2, 0), rx, 9.5f * f->size, body, 255);
    }
    /* accent stripes: on the flank, so they narrow with the turn and fade as
     * the fish comes head-on (seen edge-on, a flank shows nothing).
     * A FRY has none yet: its markings come in with its first growth spurt
     * (the setup's colour page keeps the accent a "?" for that reason) */
    if (f->stage >= STAGE_JUV) for (int i = -1; i <= 1; i++) {
        float lx = -3 + i * 6, s = fabsf(yaw_at(yh, yt, lx));
        if (s > 0.25f)
            fill_ellipse(c, TX(lx, 0), TY(lx, 0), 2 * mark * s, 5 * mark,
                         f->accent, (int)(150 * (s - 0.25f) / 0.75f));
    }
    /* eye — closed to a lid line when asleep (resting at night) */
    if (asleep) {
        fill_ellipse(c, TX(9, -3), TY(9, -3), 2.2f, 0.7f, 0x9fb4b8, 200);
    } else {
        fill_ellipse(c, TX(9, -3), TY(9, -3), 2.2f, 2.2f, 0xffffff, 255);
        fill_ellipse(c, TX(9.7f, -3), TY(9.7f, -3), 1.1f, 1.1f, 0x031015, 255);
    }
#undef TX
#undef TY
}
/* the tank's fish (the previews below draw a fish of their own) */
static void draw_fish(ctx_t *c, const tank_t *t, const fish_t *f) {
    ctx_t cf = *c;                                     /* its window ends at the sand line (FISH_FLOOR_Y): a fin that reaches
                                                          the floor touches it, never shows through it - nothing of a fish is
                                                          ever painted over the sand or a piece's footing (2026-10-24) */
    int h = FISH_FLOOR_Y - cf.oy; if (h < cf.h) cf.h = h;
    bool asleep = t->night && f->goal.id == GOAL_REST;
    /* and its BODY rests on the sand, never in it: the physics lets a centre
       down to 15 x size off the bottom (tank.c's hard clamp; a pellet lies
       2 px into the sand), where the biggest fish's belly would be 8 px under
       the line and the window would cut it flat. Drawn that much higher, it
       lies on the sand with its mouth at the pellet - smoothly, the lift
       growing as it sinks - and only a fin ever reaches the window's edge. */
    float sink = f->y + 10 * f->size - FISH_FLOOR_Y;
    if (sink > 0) { fish_t g = *f; g.y -= sink; draw_fish_core(&cf, &g, t->clock, asleep, 1.0f, FISH_FLOOR_Y); return; }
    draw_fish_core(&cf, f, t->clock, asleep, 1.0f, FISH_FLOOR_Y);
}

/* ---- the shrimp (2026-09-29): cherry shrimp from 18 x 8 pixel grids,
 * facing right, drawn pixel by pixel through the fish's turn - the yaw
 * foreshortens x - so they stay procedural (Strato: "all aquatic marine life
 * sprites should be procedurally drawn. raster icons only for menus").
 * What makes six pixels a shrimp: the hunched back, the body thinning to a
 * fanned tail, one eye up front, antennae longer than the body; the tail,
 * legs and antennae are see-through. Character is in the motion: paddling
 * legs, a peck at the floor, the tail-flick escape. (working-assets/shrimp/
 * has the approved drafts.) Tones: o belly edge, r cherry, h the back's
 * light, s a glint, e the eye, a antenna, l legs, t tail fan. */
#define SHRIMP_TONES 8
static const char SHRIMP_KEYS[SHRIMP_TONES + 1] = "orhsealt";
static const uint32_t SHRIMP_RGB[SHRIMP_TONES]  = { 0x6e0f1c, 0xd0262e, 0xff5a4a, 0xffb8a0, 0x100408, 0xff7a70, 0xc0303a, 0xff7060 };
static const uint8_t  SHRIMP_ALPHA[SHRIMP_TONES] = { 255, 255, 255, 255, 255, 140, 190, 205 };
static const char *const SHRIMP_IDLE_A[8] = {
    "..................", "......ohhs......aa", "....ohrrrhhhs..a..", "..ohrrrrrrrrrea...",
    ".trrrrrrrrrrrrrr..", "tto..oorrrrrroo...", "t......l.l.l.l.aa.", "................a." };
static const char *const SHRIMP_IDLE_B[8] = {
    "................a.", "......ohhs......a.", "....ohrrrhhhs..a..", "..ohrrrrrrrrrea...",
    ".trrrrrrrrrrrrrr..", "tto..oorrrrrroo...", "t.....l.l.l.l..aa.", ".................." };
static const char *const SHRIMP_PECK[8] = {      /* nose to the sand */
    "..................", "......ohhs........", "....ohrrrhhhs.....", "..ohrrrrrrrrrr.aa.",
    ".trrrrrrrrrrrrea..", "tto..oorrrrrrrrr..", "t......l.l.l.lo.a.", "................a." };
static const char *const SHRIMP_DART[8] = {      /* the escape: tail snapped under, antennae streaming back */
    "..................", "aaaa..ohhs........", "....ohrrrhhhs.....", "...orrrrrrrrrea...",
    "...ttrrrrrrrrrr...", "...tt.oorrrroo....", "..................", ".................." };
static void draw_shrimp(ctx_t *c, const shrimp_t *q, const src_t *pal) {
    float sp2 = q->vx * q->vx + q->vy * q->vy;
    const char *const *fr = q->dart > 0 ? SHRIMP_DART
                          : q->pecking ? (fmodf(q->ph * 3, 1) < 0.5f ? SHRIMP_PECK : SHRIMP_IDLE_A)
                          : (fmodf(q->ph * (sp2 > 9 ? 8 : 2.5f), 1) < 0.5f ? SHRIMP_IDLE_A : SHRIMP_IDLE_B);
    float s = q->yaw;                                   /* head-on it is a sliver, never nothing */
    if (s > -0.3f && s < 0.3f) s = s < 0 ? -0.3f : 0.3f;
    int oy = (int)floorf(q->y + 0.5f) - 4;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 18; x++) {
            char ch = fr[y][x];
            if (ch == '.') continue;
            const char *k = strchr(SHRIMP_KEYS, ch);
            if (!k) continue;
            int ti = (int)(k - SHRIMP_KEYS);
            px_blend_s(c, (int)floorf(q->x + (x - 9) * s + 0.5f), oy + y, &pal[ti], SHRIMP_ALPHA[ti]);
        }
}

/* a bed of swaying seaweed fronds; seed varies phase/heights between beds.
 * n and max_seg come from tank_veg_bed (growth-driven: the beds keep growing
 * up and out until the keeper trims them; at VEG_NUB they are green stubble).
 * layer: 0 = the fronds drawn BEHIND the fish, 1 = the fronds drawn in FRONT
 * (alternating), so a fish that dips into a canopy swims woven through it
 * instead of floating on top. */

/* algae film on the glass, drawn over everything: dappled blobs per covered
 * grid cell, thicker film = bigger and greener. The keeper wipes it off. */
static void draw_algae(ctx_t *c, const tank_t *t) {
    for (int cy = 0; cy < ALGAE_ROWS; cy++)
        for (int cx = 0; cx < ALGAE_COLS; cx++) {
            int cov = t->algae[cy * ALGAE_COLS + cx];
            if (!cov) continue;
            uint32_t h = (uint32_t)((cx * 73856093u) ^ (cy * 19349663u));
            float bx = cx * ALGAE_CELL, by = cy * ALGAE_CELL;
            for (int b = 0; b < 3; b++) {
                uint32_t hb = h ^ (b * 2654435761u);
                float ox = (float)(hb % ALGAE_CELL);
                float oy = (float)((hb >> 5) % ALGAE_CELL);
                float r = (1.5f + (float)((hb >> 10) % 3)) * (0.55f + 0.45f * cov / 255.0f)
                        + 2.2f * cov / 255.0f;
                fill_ellipse(c, bx + ox, by + oy, r, r * 0.85f,
                             (hb & 4) ? 0x3f7a45 : 0x35663d, 34 + cov * 96 / 255);
            }
        }
}

/* the snail: drawn procedurally (2026-09-16), defined after the castle
 * block below - it shares the castle's position hash */
static void draw_snail(ctx_t *c, const tank_t *t, bool upright_pass);

/* optional per-stage frame profiling (render.h) */
int64_t (*render_clock_us)(void) = NULL;
int64_t render_prof_us[14];
#define PROF_MARK() (render_clock_us ? render_clock_us() : 0)
#define PROF_ADD(i, t0) do { if (render_clock_us) { int64_t _n = render_clock_us(); render_prof_us[i] += _n - (t0); (t0) = _n; } } while (0)

/* ---- static scene: gradient, pebbles, backdrop decor (cacheable) ---- */
static uint16_t *g_scene = NULL;
static float     g_scene_dim = -1;
static unsigned  g_scene_epoch = 0;
static const uint16_t *g_primed_fb = NULL;
static unsigned  g_primed_epoch = 0;
static uint8_t  *g_vig = NULL;                  /* per-pixel vignette alpha (static) */
static bool      g_vig_filled = false;
void render_set_scene_cache(uint16_t *buf) { g_scene = buf; g_scene_dim = -1; }
void render_set_vignette_cache(uint8_t *buf) { g_vig = buf; g_vig_filled = false; }

const uint16_t *render_scene_buf(unsigned *epoch) {
    if (g_sg_on && g_sg_ok && g_sg_scene_ep == g_scene_epoch && g_scene_epoch) { if (epoch) *epoch = 0x80000000u | g_sg_ep; return g_sceneg; }   /* the grass cache, when it is current */
    if (epoch) *epoch = g_scene_epoch;
    return g_scene_epoch ? g_scene : NULL;
}
void render_fb_primed(const uint16_t *fb, unsigned epoch) { g_primed_fb = fb; g_primed_epoch = epoch; }
/* the scene back under what a frame drew (its touched mask): runs of touched
 * 32 px words copied from the scene. Returns the scene epoch it restored
 * against: the buffer is primed for that epoch (a bake in between shows as a
 * mismatch at the next render, which copies the whole scene then). */
unsigned render_restore_touched(uint16_t *fb, const uint32_t *touched) {
    unsigned ep = g_scene_epoch;
    if (!g_scene || !touched) return ep;
    for (int y = 0; y < TANK_H; y++) {
        const uint32_t *row = touched + y * DIRTY_WORDS_PER_ROW;
        uint16_t *d = fb + y * TANK_W; const uint16_t *s = g_scene + y * TANK_W;
        for (int w = 0; w < DIRTY_WORDS_PER_ROW; w++) {
            if (!row[w]) continue;
            int w1 = w;
            while (w1 + 1 < DIRTY_WORDS_PER_ROW && row[w1 + 1]) w1++;
            int x0 = w * 32, x1 = (w1 + 1) * 32;
            if (x1 > TANK_W) x1 = TANK_W;
            memcpy(d + x0, s + x0, (size_t)(x1 - x0) * 2);
            w = w1;
        }
    }
    return ep;
}
unsigned render_scene_epoch(void) { return g_scene_epoch; }

/* vignette alpha at (x,y); matches the classic per-pixel loop */
static inline int vig_alpha(int x, int y) {
    float dx = (x - TANK_W * 0.5f) * (2.0f / TANK_W);   /* (multiplies: a frond span calls this - 2026-10-23) */
    float dy = (y - TANK_H * 0.5f) * (2.0f / TANK_H);
    float d2 = dx * dx + dy * dy;
    if (d2 <= 0.72f) return 0;
    int a = (int)((d2 - 0.72f) * 220);
    return a > 255 ? 255 : a;
}

/* pure darkening (blend toward black), independent of ctx dim */
static inline void px_darken(uint16_t *p, int a) {
    int inv = 256 - a; uint16_t cur = FB_SWAP16(*p);
    *p = FB_SWAP16((uint16_t)(((((cur >> 11) * inv) >> 8) << 11) |
                              (((((cur >> 5) & 63) * inv) >> 8) << 5) |
                              (((cur & 31) * inv) >> 8)));
}

/* ---- the pumpkin lantern's night light (0.3.6; Strato 2026-10-17: "its
 * glow should gently illuminate the other decorations and critters in the
 * tank") ----
 * With the tank light off the lantern's candle lights everything near it,
 * not only the water behind: the light at a pixel is one table lookup by its
 * squared distance (g_pk_lut: warm at the heart, going gold-green with
 * distance - the water drinks the red first), added before the vignette.
 * Where it lands:
 *   - the baked scene: the water and the floor once, dithered (pk_halo),
 *     and every piece painted into the bake - the final-paint paths (the
 *     castle, the corals, the mounds) call PK_LIGHT_PX before their vignette;
 *   - every frame, what is painted live: each frond span (one light per
 *     span, as its vignette) and - in one pass over the dirty mask before
 *     the vignette sweep (pk_light_dirty) - the fish, the shrimp, the snail,
 *     the urchin, the seahorse, the food, the bubbles, the crowns.
 * g_pk_lx < 0 = no light (the light on, no pumpkin, a page drawing):
 * render_tank sets it for its own frame and clears it on the way out. */
#define PK_HALO_R   176                       /* the reach along the floor, px (a castle's half width and more) */
#define PK_HALO_RY  150                       /* and up: it spreads along the floor a little more */
#define PK_LUT_K    (16777216 / (PK_HALO_R * PK_HALO_R))   /* d2 -> table index (256 = the edge) */
#define PK_DY_K     (256 * PK_HALO_R * PK_HALO_R / (PK_HALO_RY * PK_HALO_RY))   /* dy^2 -> its share of d2, x256 (no divide per pixel) */
static int g_pk_lx = -1, g_pk_ly;
/* the light by squared distance, 128 steps: its RGB in 8 bits (the bake's
 * dithered water) and packed as 565 increments in g_pk_lut - 256 bytes of
 * internal RAM, looked up thousands of times a frame (a PSRAM table cost
 * cache misses across the whole render) */
static uint16_t g_pk_lut[128], g_pk_lut_shade[128], g_pk_lut_half[128];   /* ... the spill where something stands in its way, and a half step at a shadow's soft edge */
#define PK_HALF 0.55f
static bool g_pk_lut_ok;
/* the castle in the way (2026-10-17, Strato: "the castle should block the
 * pumpkin's glow"): with the lantern BEHIND a castle IN FRONT, the castle's
 * silhouette is a shadow - its stone takes PK_SPILL of the light (the water
 * scatters a little round it) and so does anything drawn inside it - but the
 * arch's opening and the windows let the light through, so the gateway glows
 * and a fish passing it is lit. pk_shaded says which, in the frame's
 * coordinates (defined with the castle, below). */
#define PK_SPILL 0.22f
static bool g_pk_under_castle;                /* the castle stands over the lantern: its silhouette's rules */
/* THE PIECES (2026-10-17): the castle, the coral, the cluster and the pumpkin
 * are one list, back to front (tank_decor_order: depth, then rank); render.c
 * paints that list and nothing else decides who covers whom. Declared here,
 * defined with the pieces below. */
static int  g_npc;                            /* this frame's pieces */
static int  g_pk_k = -1;                      /* the lantern's index in the list, -1 = none */
static int  g_clip_k = -1;                    /* an overlay (a crown, the candle) of piece g_clip_k is being drawn: the pieces above it cover it */
static bool g_cst_clip;                       /* the castle is being painted again inside a box (render_tank's live pass, 0.3.9): castle_put skips the rest */
static int  g_cst_clip_x0, g_cst_clip_y0, g_cst_clip_x1, g_cst_clip_y1;
static int  g_castle_cx = -1;                 /* the castle's centre whatever its depth (-1 = none): its shadow's geometry */
static bool pc_covered_above(int k, int x, int y);
/* and everything else in front of a lantern BEHIND (Strato 2026-10-17: "add
 * the same shadow drawing to other things that fall in front of the pumpkin
 * like blades of sea grass and the sword plant and corals"): a blade, a
 * leaf, a coral or cluster IN FRONT is backlit - its own pixels take the
 * spill (g_pk_shade_all while they are drawn) - and casts a shadow on the
 * sand under its footprint (g_pk_cast, read by pk_shade): dark under a
 * solid piece, the spill under a grass bed (light leaks between the
 * blades), and never on the lantern's own patch of sand */
static bool g_pk_shadows;                     /* the lantern is BEHIND and lit: pk_shade has a say */
static bool g_pk_shade_all;                   /* what is being drawn stands in front of the lantern */
#define PK_CAST_MAX (VEG_BEDS_MAX * 4 + 2)
static struct { short x0, x1; signed char level; } g_pk_cast[PK_CAST_MAX];
static int g_pk_cast_n;
#define PK_FLOOR_Y (TANK_BOT - 18)            /* below this the sand begins (its top varies by a few px) */
static int  pk_shade(int x, int y);           /* 0 = lit, 1 = half, 2 = the spill, 3 = dark */
static void pk_rgb8(int i, int *r, int *g, int *b) {
    float d = sqrtf((i + 0.5f) / 128.0f), a = 0.88f * powf(1 - d, 1.7f);   /* a long soft tail: the neighbours catch it */
    *r = (int)(a * (196 - 84 * d)); *g = (int)(a * (116 - 4 * d)); *b = (int)(a * (34 + 22 * d));
}
static void pk_lut_fill(void) {
    if (g_pk_lut_ok) return;
    for (int i = 0; i < 128; i++) {
        int r, g, b; pk_rgb8(i, &r, &g, &b);
        g_pk_lut[i] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
        int hr = (int)(r * PK_HALF), hg = (int)(g * PK_HALF), hb = (int)(b * PK_HALF);
        g_pk_lut_half[i] = (uint16_t)((hr >> 3) << 11 | (hg >> 2) << 5 | (hb >> 3));
        r = (int)(r * PK_SPILL); g = (int)(g * PK_SPILL); b = (int)(b * PK_SPILL);
        g_pk_lut_shade[i] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
    }
    g_pk_lut_ok = true;
}
static inline int pk_light_i(int x, int y) {      /* the table step at (x, y), -1 = outside the light */
    int dx = x - g_pk_lx, dy = y - g_pk_ly;
    if (dx <= -PK_HALO_R || dx >= PK_HALO_R || dy <= -PK_HALO_RY || dy >= PK_HALO_RY) return -1;
    int i = (dx * dx + (dy * dy * PK_DY_K >> 8)) * PK_LUT_K >> 17;
    return i < 128 ? i : -1;
}
static inline uint32_t pk_light_at(int x, int y) {
    int i = pk_light_i(x, y);
    if (i < 0) return 0;
    int sh = g_pk_shadows ? pk_shade(x, y) : 0;
    if (g_pk_shade_all && sh < 2) sh = 2;
    return sh == 0 ? g_pk_lut[i] : sh == 1 ? g_pk_lut_half[i] : sh == 2 ? g_pk_lut_shade[i] : 0;
}
static inline uint16_t px_lit(uint16_t v, uint32_t l) {   /* a 565 colour plus the light's 565 increments, saturated */
    int r = (v >> 11) + (int)(l >> 11), g = ((v >> 5) & 63) + (int)((l >> 5) & 63), b = (v & 31) + (int)(l & 31);
    return (uint16_t)((r > 31 ? 31 : r) << 11 | (g > 63 ? 63 : g) << 5 | (b > 31 ? 31 : b));
}
#define PK_LIGHT_PX(p, x, y) do { if (g_pk_lx >= 0) { uint32_t _l = pk_light_at((x), (y)); if (_l) *(p) = FB_SWAP16(px_lit(FB_SWAP16(*(p)), _l)); } } while (0)

/* a span that is FINAL: blended, then vignetted right here, and NOT marked
 * dirty, so the re-apply sweep never touches it (scene cache mode only) */
int g_veg_bench_dry;                              /* director `vegbench` (2026-10-23): 1 = span_final does everything but the stores; 2 = and no vignette alpha; 3 = veg_put returns at once */
/* the vignette's CLEAR ZONE per row (2026-10-24): vig_alpha is 0 within this
 * many px of the frame's centre column on row y (255: nowhere on the row).
 * Built once; a span whose middle is inside it skips the alpha's arithmetic -
 * most of the grass is (vegbench: 0.25 us of a 1.7 us span on the square) */
static uint8_t g_vig_half[TANK_H];
static bool    g_vig_half_ok;
static void vig_half_fill(void) {
    for (int y = 0; y < TANK_H; y++) {
        if (vig_alpha(TANK_W / 2, y)) { g_vig_half[y] = 255; continue; }
        int d = 0;
        while (TANK_W / 2 + d + 1 < TANK_W && d < 254 && !vig_alpha(TANK_W / 2 + d + 1, y)) d++;
        g_vig_half[y] = (uint8_t)d;
    }
    g_vig_half_ok = true;
}
static inline int vig_alpha_span(int xc, int y) {
    if (!g_vig_half_ok) vig_half_fill();
    if ((unsigned)y < (unsigned)TANK_H && g_vig_half[y] != 255) {
        int dd = xc - TANK_W / 2; if (dd < 0) dd = -dd;
        if (dd <= g_vig_half[y]) return 0;
    }
    return vig_alpha(xc, y);
}
static inline void span_final(ctx_t *c, int x0, int x1, int y, const src_t *s, int alpha) {
    if ((unsigned)(y - c->oy) >= (unsigned)c->h) return;
    if (x0 < c->ox) x0 = c->ox;
    if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
    if (x0 > x1) return;
    TOUCH_SPAN(c, x0, x1, y);
    uint16_t *p = &CTX_PX(c, x0, y);
    /* ONE vignette alpha per span, computed (a 5 px frond span changes it by
       < 4%, invisible) rather than read: the PSRAM LUT costs a cache line
       per span and fronds are vertical, which was most of the frond cost */
    int a = g_veg_bench_dry >= 2 ? 0 : vig_alpha_span((x0 + x1) >> 1, y);
    uint32_t l = g_pk_lx >= 0 ? pk_light_at((x0 + x1) >> 1, y) : 0;   /* the lantern's light, one per span likewise */
    if (l || a) dirty_clear_span(x0, x1, y);    /* lit here: a fish under it must not be lit again by the dirty pass; in the vignette's ring: a crab or a
                                                   fish under it must not have the sweep dim this span a second time (2026-10-24, the watch: a front
                                                   bed's fronds a deeper green over the crab backed into the castle; the matrix) */
    if (alpha >= 255) {                         /* opaque: a store, no PSRAM read */
        uint16_t v = FB_SWAP16(l ? px_lit(s->v, l) : s->v);
        if (g_veg_bench_dry) return;
        for (int x = x0; x <= x1; x++, p++) { *p = v; if (a) px_darken(p, a); }
        return;
    }
    for (int x = x0; x <= x1; x++, p++) {
        blend565(p, s, alpha);
        if (l) *p = FB_SWAP16(px_lit(FB_SWAP16(*p), l));
        if (a) px_darken(p, a);
    }
}

/* water gradient colour of row y (shared by the scene bake and the frond
 * pre-tint below) */
static inline uint32_t water_rgb(int y) {
    float p = (float)y / TANK_BOT;                 /* the gradient runs surface to floor; under a bowl's sand line it holds */
    if (p > 1) p = 1;
    return p < 0.45f ? mix(0x0a3c46, 0x08272f, p / 0.45f)
                     : mix(0x08272f, 0x031015, (p - 0.45f) / 0.55f);
}

/* Fronds are drawn OPAQUE from a per-row pre-tinted palette (2026-09-04):
 * the 220/255 blend against the water that gave them their depth tint is
 * folded in here, once per row per colour, when the day/night dim changes -
 * so each frond pixel is a store instead of a PSRAM read-modify-write
 * (a ceiling-high jungle was ~16 of 19.5 ms on the device). Only what a
 * frond covers OTHER than water changes: a fish behind a front-layer frond
 * no longer shows through at 14%. */
#define VEG_ALPHA 220
static uint16_t g_veg_row[4][TANK_H];   /* grass pair, then the sword plant's yellow-green pair */
static float    g_veg_row_dim = -1;
static void veg_tint_fill(float dim) {
    static const uint32_t frond[4] = { 0x3f8b55, 0x2e7d4f, 0x8dbb48, 0x6c9d38 };
    if (g_veg_row_dim == dim) return;
    for (int y = 0; y < TANK_H; y++) {
        uint32_t w = water_rgb(y);
        for (int k = 0; k < 4; k++)
            g_veg_row[k][y] = rgb565(mix(w, frond[k], VEG_ALPHA / 255.0f), dim);
    }
    g_veg_row_dim = dim;
}

#define VEG_SEG_DY   VEG_SEG_PX           /* segment pitch, px of height (tank.h: 3.2, the watch 4.3) */
#define VEG_SEG_RY   2.2f                 /* the old segment ellipse's half-height */
#define VEG_MAX_SEGS (VEG_SEGS_FULL + 5)  /* tank_veg_bed tops out at VEG_SEGS_FULL */
/* final: the scene cache is live, so each frond span applies its own vignette
 * (see span_final) and needs no re-apply rect - a full canopy used to hand
 * the sweep three bed-sized boxes, the largest PSRAM traffic in the frame. */
/* a bed's depth (2026-09-16): the grass beds are woven with the fish
 * (alternate fronds behind and in front); the plant is wherever the keeper
 * put it - all behind, woven, or all in front (tank_decor_z) */
static int bed_z(const tank_t *t, int b) { return b == 3 ? tank_decor_z(t, 0) : DECOR_Z_MIDDLE; }
/* a frond span - minus the castle's silhouette while the castle stands IN
 * FRONT of the grass (the pieces outside it are drawn; the mask and
 * g_veg_mask_cx live with the castle, below). Off the castle it is one span. */
/* DEPTH (0.3.9, tank.h DECOR_FY): a piece stands on the floor line of its
 * depth - BEHIND at the back of the sand, IN FRONT 8 px lower at its front
 * edge - so every piece's FY below is its own line plus this frame's shift
 * for it (pieces_build sets them from the tank's depths; 0 with no piece).
 * The castle's native line is the back line, the cell sprites' (the coral,
 * the cluster, the lantern) 6 px under it (sunk into the pebbles). */
static int g_dz_castle, g_dz_coral, g_dz_cl, g_dz_pk, g_dz_vo;
static inline int decor_dz(int z, int native_fy) { return DECOR_FY(z) - native_fy; }
/* and its HAZE with the water: BEHIND a little more, IN FRONT a little less (the
 * same shift says which: the back line's is 0 or -6, the front line's +8 or +2) */
static inline float decor_haze(float alpha, int dz) { float a = dz > 0 ? alpha + 0.05f : alpha - 0.08f; return a > 0.97f ? 0.97f : a; }
#define CASTLE_FY   (TANK_BOT - 16 + g_dz_castle)
#define CASTLE_ROWS 164
#define CASTLE_ARCH_R 26                     /* the opening's half-width (and the vault's radius) */
#define CASTLE_ARCH_S 24                     /* the spring line: straight jambs below, the vault above */
#define CASTLE_TRIM   5                      /* the brick trim's width */
static uint32_t g_castle_mask[CASTLE_ROWS + 12][224 / 32];        /* its silhouette, centre-relative (shape only: valid at any x) */
/* ... and its FRONT ROW alone (what stands over the fish when it is IN FRONT): castle_front() in the PSRAM decor scratch (internal RAM is spoken for) */
static uint32_t (*castle_front(void))[224 / 32];
static int      g_veg_mask_cx;
/* the arch passes (2026-10-17): with the castle IN FRONT its arch shows the
 * back of the tank, so what the frame draws AFTER the fish (the odd fronds,
 * a bed IN FRONT) must go BEFORE them inside the arch - a fish in the
 * gateway is in front of what it frames. -1: only the arch's pixels (the
 * early pass); +1: everything but them (the late pass); 0: no rule. */
static int      g_veg_arch_pass;
static bool     castle_arch(int x, int y);
/* the pieces a bed's spans leave alone (2026-10-17): a bit per piece in the
 * list - every piece IN FRONT of the plant layer for a grass bed, and for
 * the sword plant also the pieces at its own depth ranked above it
 * (render_tank sets it per bed: veg_skip_for) */
static uint32_t g_veg_skip;
static bool pc_covers(int k, int x, int y);
static bool pc_in_box(int k, int x0, int x1, int y);
static uint32_t pc_row_bits(int k, int x0, int n, int y);   /* piece k's mask over n (<= 32) pixels from x0 on row y */
static uint8_t g_veg_row_near[TANK_H];
/* the grass in ROW PAIRS (2026-10-24, Strato: "explore making the grass cheaper
 * to draw ... open to an altered look"): with g_veg_step 2 each frond is walked
 * every second row and each step paints two rows with one span's setup - one
 * clip, one vignette alpha, one light - so a frond costs about half the spans.
 * Its edges step in 2 px instead of 1 (the corals' cells are 2 px already).
 * Director `vegstep 1|2`, the sim's PT_VEG_STEP. */
int g_veg_step = 1;
int g_veg_thin;                                   /* director `vegthin 1` (2026-10-24, a COST PROBE only): every third frond not drawn - what a sparser bed would save */
void render_set_veg_step(int s) { g_veg_step = s == 2 ? 2 : s == 0 ? 0 : 1; }   /* (0: the grass not drawn - the director's A/B of its whole cost) */
int  render_veg_step(void) { return g_veg_step; }
static inline void span_final2(ctx_t *c, int x0, int x1, int y, uint16_t v0, uint16_t v1) {   /* rows y and y-1, opaque, final */
    if ((unsigned)(y - 1 - c->oy) >= (unsigned)(c->h - 1)) {                /* (a pair at the window's edge: row by row) */
        src_t s0 = { 0, 0, 0, v0 }, s1 = { 0, 0, 0, v1 };
        span_final(c, x0, x1, y, &s0, 255); span_final(c, x0, x1, y - 1, &s1, 255);
        return;
    }
    if (x0 < c->ox) x0 = c->ox;
    if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
    if (x0 > x1) return;
    TOUCH_SPAN(c, x0, x1, y); TOUCH_SPAN(c, x0, x1, y - 1);
    int xc = (x0 + x1) >> 1, a = vig_alpha_span(xc, y);
    uint32_t l = g_pk_lx >= 0 ? pk_light_at(xc, y) : 0;
    if (l || a) { dirty_clear_span(x0, x1, y); dirty_clear_span(x0, x1, y - 1); }   /* (as span_final: lit or in the ring, nothing dirty stays under a final pair) */
    uint16_t w0 = FB_SWAP16(l ? px_lit(v0, l) : v0), w1 = FB_SWAP16(l ? px_lit(v1, l) : v1);
    uint16_t *p = &CTX_PX(c, x0, y), *q = &CTX_PX(c, x0, y - 1);
    if (a) for (int x = x0; x <= x1; x++, p++, q++) { *p = w0; px_darken(p, a); *q = w1; px_darken(q, a); }
    else   for (int x = x0; x <= x1; x++, p++, q++) { *p = w0; *q = w1; }
}
/* the back grass in the open arch of a castle IN FRONT (the back beds, the
 * even fronds of the woven beds) is FINAL - vignetted on the spot, never in
 * the dirty mask - so since the arch's shade went into the bake and the
 * per-frame pass re-shades only the dirty pixels (2026-10-23), those fronds
 * came back at full brightness (Strato's 1.8, the same evening: "every other
 * frond in the arch"). A final span that crosses the arch takes the tunnel's
 * shade itself, by the same alpha; the grass cache's bake goes through here
 * too, so a frame primed from it matches. Off while the lantern behind the
 * castle lights the tunnel, as arch_shade is (render_tank sets both). */
static bool g_veg_arch_shade;                     /* this frame: a castle IN FRONT whose tunnel is shaded */
static bool g_veg_arch_night;
static int  arch_shade_alpha(int y, bool night);
static void veg_arch_shade(ctx_t *c, int x0, int x1, int y) {
    if (!CTX_IN(c, x0, y) && !CTX_IN(c, x1, y)) return;
    int a = arch_shade_alpha(y, g_veg_arch_night);
    for (int x = x0; x <= x1; x++)
        if (castle_arch(x, y) && CTX_IN(c, x, y)) px_darken(&CTX_PX(c, x, y), a);
}
static inline void veg_put(ctx_t *c, int x0, int x1, int y, const src_t *s, bool final, bool arch) {
    if (g_veg_bench_dry >= 3) return;
    bg_mark(x0, x1, y);
    if (final && g_veg_arch_pass >= 0) {
        span_final(c, x0, x1, y, s, 255);
        if (arch) {
            if (g_veg_arch_shade && g_veg_arch_pass == 0) veg_arch_shade(c, x0, x1, y);   /* (the late pass never enters the arch) */
            else dirty_clear_span(x0, x1, y);         /* a bed IN FRONT across the opening: final, so the arch's dirty pass must not shade it over a crab or a fish */
        }
    } else span(c, x0, x1, y, s, 255);               /* the early arch pass: marked, so the arch's shade finds it (2026-10-23) and the sweep vignettes it */
}
static inline void veg_span(ctx_t *c, int x0, int x1, int y, const src_t *s, bool final) {
    if ((unsigned)y < (unsigned)TANK_H && !g_veg_row_near[y]) {   /* this row meets no piece and no arch (draw_veg's table): the span, or nothing in the early arch pass */
        if (g_veg_arch_pass >= 0) veg_put(c, x0, x1, y, s, final, false);
        return;
    }
    uint32_t near = 0;
    for (int k = 0; k < g_npc; k++) if (((g_veg_skip >> k) & 1) && pc_in_box(k, x0, x1, y)) near |= 1u << k;
    bool arch_box = g_veg_mask_cx >= 0 && y <= CASTLE_FY && y >= CASTLE_FY - CASTLE_ARCH_S - CASTLE_ARCH_R
                    && x1 >= g_veg_mask_cx - CASTLE_ARCH_R && x0 <= g_veg_mask_cx + CASTLE_ARCH_R;
    if (g_veg_arch_pass < 0 && !arch_box) return;      /* the early arch pass: nothing outside the arch */
    if (!near && !(g_veg_arch_pass && arch_box)) {      /* the usual frond: one span */
        veg_put(c, x0, x1, y, s, final, arch_box);
        return;
    }
    /* near a piece: the pieces' bits over the span, one look per piece
       (a frond is a few px wide: one or two mask words), then the runs they
       leave. A span over 32 px (none today) goes pixel by pixel. */
    int n = x1 - x0 + 1;
    uint32_t m = 0, full = n < 32 ? (1u << n) - 1 : 0xffffffffu;
    if (n <= 32) {
        for (int k = 0; k < g_npc; k++) if ((near >> k) & 1) m |= pc_row_bits(k, x0, n, y);
        if (g_veg_arch_pass && arch_box)                 /* the arch: the early pass draws only it, the late one skips it */
            for (int i = 0; i < n; i++) { bool a = castle_arch(x0 + i, y); if (g_veg_arch_pass > 0 ? a : !a) m |= 1u << i; }
        if (!m) { veg_put(c, x0, x1, y, s, final, arch_box); return; }
        if (m == full) return;
    }
    int run = -1;
    for (int i = 0; i <= n; i++) {
        bool in = i == n;
        if (!in) {
            if (n <= 32) in = (m >> i) & 1;
            else {
                for (int k = 0; k < g_npc && !in; k++) if (((near >> k) & 1) && pc_covers(k, x0 + i, y)) in = true;
                if (!in && g_veg_arch_pass && arch_box) { bool a = castle_arch(x0 + i, y); if (g_veg_arch_pass > 0 ? a : !a) in = true; }
            }
        }
        if (!in) { if (run < 0) run = x0 + i; }
        else if (run >= 0) {
            veg_put(c, run, x0 + i - 1, y, s, final, arch_box);
            run = -1;
        }
    }
}
/* layer: 0 = the even fronds, 1 = the odd ones, -1 = every frond */
static const int VEG_SEED[VEG_BEDS_MAX] = { 0, 7, 3, 5 };   /* each bed's sway phase (the seahorse's blade reads it too) */
/* g_veg_row_near (declared with veg_span above): per row of the bed being drawn: the pieces whose boxes its spans can meet (bits by
                                                              * piece), and bit 7 = the row crosses a castle's open arch (draw_veg fills it once a
                                                              * bed; veg_span reads it: most rows meet nothing and take the fast path - 2026-10-23) */
static void draw_veg(ctx_t *c, const tank_t *t, int b, int seed, int layer, bool final) {
    if (!g_veg_step) return;                         /* director `vegstep 0`: the A/B of the grass's whole cost */
    veg_tint_fill(c->dim);
    int n; float bx0, bx1; tank_veg_bed(t, b, &bx0, &bx1, NULL, &n);
    /* the sword plant (bed 3, the shop): broad lanceolate leaves - a narrow
       stem, 7 px half-width at 40% of the way up, a point at the tip - a
       lazier sway, its own yellow-green pair */
    bool sword = tank_veg_kind(t, b) == VEG_KIND_SWORD;
    float root = 2.4f, tip = 0.4f, amp = sword ? 2.5f : 4.0f;
    /* once a bed, not a span (2026-10-23: the tests below ran for every one of a frame's
       ~6000 spans): per row, the pieces whose boxes the bed's spans can meet (+-8: the
       sway and the half-width), and whether the row crosses a castle's open arch */
    { int y_lo = (int)(TANK_BOT - 16 - (VEG_MAX_SEGS - 1) * VEG_SEG_DY - VEG_SEG_RY), y_hi = (int)(TANK_BOT - 16 + VEG_SEG_RY);
      if (y_lo < 0) y_lo = 0;
      if (y_hi > TANK_H - 1) y_hi = TANK_H - 1;
      int xa = (int)bx0 - 8, xb = (int)bx1 + 8;
      for (int y = y_lo; y <= y_hi; y++) {
          uint8_t m = 0;
          for (int k = 0; k < g_npc; k++) if (((g_veg_skip >> k) & 1) && pc_in_box(k, xa, xb, y)) m |= (uint8_t)(1u << k);
          if (g_veg_mask_cx >= 0 && y <= CASTLE_FY && y >= CASTLE_FY - CASTLE_ARCH_S - CASTLE_ARCH_R
              && xb >= g_veg_mask_cx - CASTLE_ARCH_R && xa <= g_veg_mask_cx + CASTLE_ARCH_R) m |= 0x80;
          g_veg_row_near[y] = m;
      } }
    for (int i = 0; i < n; i++) {
        if (layer >= 0 && (i & 1) != layer) continue;
        if (g_veg_thin && b != 3 && i % 3 == 2) continue;   /* (the cost probe: a sparser bed) */
        float bx;
        /* each frond's own height (tank_t.veg_h): what the keeper cut is
           exactly what shows - no render-side variation on top */
        int segs = tank_veg_frond(t, b, i, &bx);
        if (g_veg_arch_pass < 0 && (bx + 14 < g_veg_mask_cx - CASTLE_ARCH_R || bx - 14 > g_veg_mask_cx + CASTLE_ARCH_R)) continue;   /* the early arch pass: only the fronds that reach the arch */
        if (segs < 1) segs = 1;                    /* nubs: always a bit of green */
        if (segs > VEG_MAX_SEGS) segs = VEG_MAX_SEGS;
        float sway = fast_sin(t->clock * (sword ? 0.6f : 0.9f) + (i + seed) * 1.7f) * amp;
        /* the swaying chain of segment centres (the frond's spine) */
        float xs[VEG_MAX_SEGS + 1];
        for (int seg = 0; seg <= segs; seg++)
            xs[seg] = bx + sway * seg / (float)segs * fast_sin(seg * 0.4f + t->clock * 0.6f + i + seed);
        /* one span per pixel row, centred on the spine, half-width tapering
         * toward the tip: the same silhouette the old chain of overlapping
         * ellipses drew (their union was a 5 px ribbon), at ~a quarter fewer
         * pixels and without a sqrt per row. Full canopy = ~1000 segments.
         * The rows are WALKED (2026-10-23: the interpolation chain per row -
         * the segment, its fraction, the taper - was 0.86 us of a 1.86 us
         * span on the square): the spine's x and the half-width advance by a
         * constant a row within a segment, the taper by a constant a row
         * (the sword's in two legs), so a row costs a few adds. */
        const uint16_t *pal = g_veg_row[(sword ? 2 : 0) + ((i + seed) & 1)];
        int y_bot = (int)(TANK_BOT - 16 + VEG_SEG_RY);
        int y_top = (int)(TANK_BOT - 16 - (segs - 1) * VEG_SEG_DY - VEG_SEG_RY);
        const float dsp = 1.0f / VEG_SEG_DY, segf = (float)(segs - 1), inv = segs > 1 ? 1.0f / (segs - 1) : 1.0f;
        /* the taper: grass thins root -> tip in a line; the sword widens to u = 0.4 and thins to the point */
        const float dh_grass = -(root - tip) * inv * dsp;
        const float sw_peak = 0.4f * segf;                      /* the sword's widest segment */
        const float dh_sw_up = 6.0f * 2.5f * inv * dsp, dh_sw_dn = -6.0f * (1.0f / 0.6f) * inv * dsp;
        const int st = g_veg_step;                              /* rows a step: 1, or 2 (row pairs, above) */
        float sp = (TANK_BOT - 16 - y_bot) * dsp;              /* the first row sits under the root: sp < 0 is clamped */
        int k = 0; float cx = xs[0], dcx = (xs[1] - xs[0]) * dsp, half, dhalf;
        if (sword) { half = 1.2f; dhalf = dh_sw_up; } else { half = root; dhalf = dh_grass; }
        for (int y = y_bot; y >= y_top; y -= st, sp += dsp * st) {
            if (sp >= 0) {
                if (sp >= segf) {                               /* the tip rows: held at the last segment */
                    if (dcx != 0 || dhalf != 0) { half = sword ? 1.2f : tip; dcx = 0; dhalf = 0; }
                    cx = xs[segs - 1];
                } else {
                    int kk = (int)sp;
                    if (kk != k) {                              /* into the next segment: its own slope, re-anchored (no drift) */
                        k = kk; dcx = (xs[k + 1] - xs[k]) * dsp; cx = xs[k] + (xs[k + 1] - xs[k]) * (sp - k);
                        if (sword) { dhalf = sp < sw_peak ? dh_sw_up : dh_sw_dn; half = sp < sw_peak ? 1.2f + 6.0f * (sp * inv) * 2.5f : 1.2f + 6.0f * (1 - sp * inv) * (1.0f / 0.6f); }
                        else half = root - (root - tip) * sp * inv;
                    } else { cx += dcx * st; half += dhalf * st; }
                    if (sword && dhalf > 0 && sp >= sw_peak) { dhalf = dh_sw_dn; half = 1.2f + 6.0f * (1 - sp * inv) * (1.0f / 0.6f); }
                }
            }
            float h = half < tip ? tip : half;
            int x0 = (int)(cx - h), x1 = (int)(cx + h);
            if (st == 2 && y >= 1) {                            /* a row pair: one span's work when the pair meets nothing */
                if (final && g_veg_arch_pass >= 0 && !g_veg_row_near[y] && !g_veg_row_near[y - 1] && !g_veg_bench_dry) { span_final2(c, x0, x1, y, pal[y], pal[y - 1]); bg_mark(x0, x1, y); bg_mark(x0, x1, y - 1); }
                else { src_t s0; s0.v = pal[y]; veg_span(c, x0, x1, y, &s0, final); src_t s1; s1.v = pal[y - 1]; veg_span(c, x0, x1, y - 1, &s1, final); }
                continue;
            }
            src_t s; s.v = pal[y];                              /* opaque: only .v is read */
            veg_span(c, x0, x1, y, &s, final);
        }
    }
}


/* director `vegbench` (2026-10-23): one bed's fronds drawn 20 times into fb, with and without the stores */
void render_veg_bench(const tank_t *t, uint16_t *fb, int bed, int64_t (*clock_us)(void), int64_t *full_us, int64_t dry_us[3], int *spans) {
    ctx_t c = ctx_full(fb, TANK_W, 1.0f);
    g_veg_skip = 0; g_veg_mask_cx = -1; g_veg_arch_pass = 0;
    int n; tank_veg_bed(t, bed, NULL, NULL, NULL, &n);
    int rows = 0; for (int i = 0; i < n; i++) { float bx; int s = tank_veg_frond(t, bed, i, &bx); if (s < 1) s = 1; rows += (int)(s * VEG_SEG_DY + 2 * VEG_SEG_RY); }
    *spans = rows;
    int64_t t0 = clock_us(); for (int k = 0; k < 20; k++) draw_veg(&c, t, bed, VEG_SEED[bed], -1, true); *full_us = (clock_us() - t0) / 20;
    for (int m = 1; m <= 3; m++) {
        g_veg_bench_dry = m;
        t0 = clock_us(); for (int k = 0; k < 20; k++) draw_veg(&c, t, bed, VEG_SEED[bed], -1, true); dry_us[m - 1] = (clock_us() - t0) / 20;
    }
    g_veg_bench_dry = 0;
}
/* ---- the castle (2026-09-16, from Strato's castle-v2 mockup) ----
 * A swim-through decoration drawn the way the fish and the fronds are -
 * spans and fills from a little geometry, no bitmap - so it sits in the same
 * water as everything else instead of a pixel-art sprite clashing with it.
 * Two layers: the KEEP (towers, battlements, the base, the dark courtyard
 * behind the arch) is static and bakes into the scene cache with the floor;
 * the GATE WALL (the curtain wall round the arch, its brick trim and jambs)
 * draws after the fish every frame, so a fish crossing the arch is tucked
 * behind the jambs and seen through the opening - it swims THROUGH. Stone
 * is a brick-course pattern from a position hash (the pebbled floor's
 * trick), golden-lit from the upper left like the mockup, mossed toward the
 * base, and pre-tinted with the water of its row like the fronds - the gate
 * a touch less than the keep, so the keep reads farther back. Geometry is
 * in px about the centre x on the floor line; ~176 x 150 px, the arch
 * opening 52 x 50 (an adult fish is ~37 x 22). */
/* CASTLE_FY (the floor line the keep stands on) and CASTLE_ROWS (rows above
 * it the palette covers) are defined with veg_span above */
enum { CT_LIGHT, CT_MID, CT_MIDDK, CT_DARK, CT_MORTAR, CT_MOSS_A, CT_MOSS_B,
       CT_BRICK_L, CT_BRICK, CT_BRICK_D, CT_ROOF_L, CT_ROOF, CT_ROOF_D, CT_INSIDE, CT_N };
static const uint32_t CASTLE_RGB[CT_N] = {
    0xd9c58c, 0xa99b7b, 0x87795f, 0x5d5446, 0x6a6151, 0x7aa33c, 0x527f2e,
    0xe08a58, 0xc25f38, 0x8b3d24, 0xe4cd8a, 0xc4a95e, 0x8f7a42, 0x061219 };
/* [keep | gate][tone][row], row 0 = CASTLE_FY - CASTLE_ROWS */
static uint16_t g_castle_row[2][CT_N][CASTLE_ROWS + 12];
static float    g_castle_dim = -1;
static int      g_castle_dzk = 99;                                   /* the shift (= the depth) the rows were hazed for */
/* the castle's silhouette, one bit per pixel in local coords (x - cx + 112,
 * 0..223; row 0 = CASTLE_FY - CASTLE_ROWS): every pixel castle_put writes
 * sets its bit, so the first full draw fills it. While the castle stands IN
 * FRONT of the grass, frond spans skip the pixels inside it (veg_span). */
#define CASTLE_MASK_W 224
static void castle_tint_fill(float dim) {
    if (g_castle_dim == dim && g_castle_dzk == g_dz_castle) return;
    float keep = decor_haze(0.70f, g_dz_castle), gate = decor_haze(0.82f, g_dz_castle);   /* by its depth (0.3.9) */
    for (int r = 0; r < CASTLE_ROWS + 12; r++) {
        int y = CASTLE_FY - CASTLE_ROWS + r;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        for (int k = 0; k < CT_N; k++) {
            g_castle_row[0][k][r] = rgb565(mix(w, CASTLE_RGB[k], k == CT_INSIDE ? 0.6f : keep), dim);
            g_castle_row[1][k][r] = rgb565(mix(w, CASTLE_RGB[k], k == CT_INSIDE ? 0.6f : gate), dim);
        }
    }
    g_castle_dim = dim; g_castle_dzk = g_dz_castle;
}
static inline uint32_t chash(int a, int b) {
    uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
/* moss creeps up from the base: 4 px cells, a chance that fades with height,
 * feathered per pixel so the patches have ragged edges */
static inline int castle_moss(int lx, int y, int extra_pct) {
    int d = CASTLE_FY - y;
    int pct = (d < 36 ? 26 - d * 26 / 36 : 0) + extra_pct;
    if (pct <= 0) return -1;
    uint32_t h = chash((lx + 4096) >> 2, y >> 2);
    if ((int)(h % 100) >= pct) return -1;
    if ((chash(lx, y) & 7) == 0) return -1;
    return (h >> 8) & 1 ? CT_MOSS_A : CT_MOSS_B;
}
/* a pixel's tone. mode 0 = WALL (5 px brick courses, 9 px bricks), 1 = ROOF
 * (4 px tile courses), 2 = RUBBLE (the base: no courses, mossy), 3 = the
 * brick JAMBS (courses), 4 = INSIDE. u = 0..1 across the element: the left
 * fifth catches the light, the right fifth falls into shadow. */
static int castle_tone(int mode, int lx, int y, float u) {
    if (mode == 4) return CT_INSIDE;
    int d = CASTLE_FY - y;
    int lit = u < 0.22f ? -1 : u > 0.78f ? 1 : 0;
    if (mode == 2) {
        int m = castle_moss(lx, y, 18); if (m >= 0) return m;
        static const int8_t pick[8] = { CT_MIDDK, CT_DARK, CT_DARK, CT_MIDDK, CT_MID, CT_DARK, CT_MIDDK, CT_DARK };
        int t = pick[chash(lx >> 1, y) & 7] + (lit < 0 ? -1 : 0);
        return t < CT_MID ? CT_MID : t;
    }
    int pitch = mode == 1 ? 4 : 5, len = mode == 1 ? 6 : 9;
    int course = d / pitch;
    if (d % pitch == 0) return mode == 3 ? CT_BRICK_D : mode == 1 ? CT_ROOF_D : CT_MORTAR;
    int bx = lx + 4096 + ((course & 1) ? len / 2 : 0);
    if (bx % len == 0) return mode == 3 ? CT_BRICK_D : mode == 1 ? CT_ROOF_D : CT_MORTAR;
    if (mode == 3) {
        static const int8_t pick[4] = { CT_BRICK, CT_BRICK, CT_BRICK_L, CT_BRICK_D };
        int t = pick[chash(course, bx / len) & 3] + lit;
        return t < CT_BRICK_L ? CT_BRICK_L : t > CT_BRICK_D ? CT_BRICK_D : t;
    }
    if (mode == 1) {
        static const int8_t pick[4] = { CT_ROOF, CT_ROOF, CT_ROOF_L, CT_ROOF_D };
        int t = pick[chash(course, bx / len) & 3] + lit;
        return t < CT_ROOF_L ? CT_ROOF_L : t > CT_ROOF_D ? CT_ROOF_D : t;
    }
    int m = castle_moss(lx, y, 0); if (m >= 0) return m;
    static const int8_t pick[8] = { CT_MID, CT_MID, CT_MID, CT_MIDDK, CT_MID, CT_MIDDK, CT_LIGHT, CT_MIDDK };
    int t = pick[chash(course, bx / len) & 7] + lit;
    return t < CT_LIGHT ? CT_LIGHT : t > CT_DARK ? CT_DARK : t;
}
typedef struct { ctx_t *c; int cx; int layer; bool final; } cst_t;
static inline void castle_put(const cst_t *k, int x, int y, uint16_t v) {
    ctx_t *c = k->c;
    if (!CTX_IN(c, x, y)) return;
    if (g_cst_clip && (x < g_cst_clip_x0 || x > g_cst_clip_x1 || y < g_cst_clip_y0 || y > g_cst_clip_y1)) return;   /* a repaint inside a box (0.3.9) */
    { int lx = x - k->cx + CASTLE_MASK_W / 2, r = y - (CASTLE_FY - CASTLE_ROWS);
      if ((unsigned)lx < CASTLE_MASK_W && (unsigned)r < CASTLE_ROWS + 12) {
          g_castle_mask[r][lx >> 5] |= 1u << (lx & 31);
          if (k->layer == 1) castle_front()[r][lx >> 5] |= 1u << (lx & 31);
      } }
    uint16_t *p = &CTX_PX(c, x, y);
    *p = FB_SWAP16(v); TOUCH_PX(c, x, y);
    if (k->final) {                          /* vignetted here, and untagged: the sweep must not darken it again */
        PK_LIGHT_PX(p, x, y);
        int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
        if (a) px_darken(p, a);
        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
    } else dirty_px(x, y);
}
/* one row of an element: local x lx0..lx1 on tank row y; ux0..ux1 = the
 * element's full width on that row, for the lighting */
static void castle_span(const cst_t *k, int lx0, int lx1, int y, int mode, float ux0, float ux1) {
    int r = y - (CASTLE_FY - CASTLE_ROWS);
    if (r < 0 || r >= CASTLE_ROWS + 12) return;
    const ctx_t *c = k->c;                       /* clip to the window first: tones cost */
    if ((unsigned)(y - c->oy) >= (unsigned)c->h) return;
    if (k->cx + lx0 < c->ox) lx0 = c->ox - k->cx;
    if (k->cx + lx1 > c->ox + c->w - 1) lx1 = c->ox + c->w - 1 - k->cx;
    float uw = ux1 > ux0 ? ux1 - ux0 : 1;
    for (int lx = lx0; lx <= lx1; lx++)
        castle_put(k, k->cx + lx, y, g_castle_row[k->layer][castle_tone(mode, lx, y, (lx - ux0) / uw)][r]);
}
static void castle_rect(const cst_t *k, int lx0, int lx1, int ytop, int ybot, int mode) {
    for (int y = ytop; y <= ybot; y++) castle_span(k, lx0, lx1, y, mode, (float)lx0, (float)lx1);
}
/* a conical roof: half_base wide at ybase, a point at yapex, plus an eave
 * a pixel wider than the tower */
static void castle_cone(const cst_t *k, int lxc, int half_base, int ybase, int yapex) {
    for (int y = yapex; y <= ybase; y++) {
        float t = (y - yapex) / (float)(ybase - yapex);
        int half = (int)(half_base * t + 0.5f);
        castle_span(k, lxc - half, lxc + half, y, 1, (float)(lxc - half), (float)(lxc + half));
    }
}
/* merlons along a top edge: 7 wide, 7 tall, on a 12 px pitch */
static void castle_merlons(const cst_t *k, int lx0, int lx1, int ytop) {
    for (int x = lx0; x + 6 <= lx1; x += 12) castle_rect(k, x, x + 6, ytop - 7, ytop - 1, 0);
}
/* an arched window: w wide, from ybot up to ytop, the vault a half-disc */
static void castle_window(const cst_t *k, int lxc, int w, int ytop, int ybot) {
    float r = w / 2.0f;
    for (int y = ytop; y <= ybot; y++) {
        float dy = (ytop + r) - y;
        float half = dy > 0 ? r * ell_half(dy / r) : r;
        castle_span(k, (int)(lxc - half + 0.5f), (int)(lxc + half - 0.5f), y, 4, 0, 1);
    }
}
/* the gate: the arch's brick trim (a ring of voussoirs over the vault, jambs
 * down the sides) and the opening. A static tone map for the ring: the
 * voussoir index needs an angle, and the gate redraws every frame. */
static int8_t g_ring[CASTLE_ARCH_R + CASTLE_TRIM + 1][2 * (CASTLE_ARCH_R + CASTLE_TRIM) + 1];
static bool   g_ring_filled = false;
static void castle_ring_fill(void) {
    if (g_ring_filled) return;
    int R = CASTLE_ARCH_R + CASTLE_TRIM;
    for (int j = 0; j <= R; j++)
        for (int i = 0; i <= 2 * R; i++) {
            float dx = i - R, dy = (float)j;                 /* dy up from the spring line */
            float d = sqrtf(dx * dx + dy * dy);
            int8_t t = -1;
            if (d >= CASTLE_ARCH_R && d < R + 0.5f) {
                float ang = atan2f(dy, dx) / 3.14159265f * 9;   /* 9 voussoirs across the half-turn */
                int v = (int)ang; float fr = ang - v;
                t = fr < 0.14f ? CT_BRICK_D : (chash(v, 3) & 3) == 0 ? CT_BRICK_L : (chash(v, 3) & 3) == 1 ? CT_BRICK_D : CT_BRICK;
                if (v < 3 && t == CT_BRICK) t = CT_BRICK_L;    /* the lit side */
                if (v > 5 && t == CT_BRICK) t = CT_BRICK_D;
            }
            g_ring[j][i] = t;
        }
    g_ring_filled = true;
}
static void castle_gate_trim(const cst_t *k) {
    int R = CASTLE_ARCH_R + CASTLE_TRIM, ys = CASTLE_FY - CASTLE_ARCH_S;
    castle_ring_fill();
    for (int j = 0; j <= R; j++) {
        int y = ys - j, r = y - (CASTLE_FY - CASTLE_ROWS);
        if (r < 0) continue;
        for (int i = 0; i <= 2 * R; i++)
            if (g_ring[j][i] >= 0) castle_put(k, k->cx + i - R, y, g_castle_row[k->layer][g_ring[j][i]][r]);
    }
    castle_rect(k, -R, -CASTLE_ARCH_R - 1, ys + 1, CASTLE_FY, 3);
    castle_rect(k,  CASTLE_ARCH_R + 1, R,  ys + 1, CASTLE_FY, 3);
}
/* the gate wall: the curtain between the left tower and the right, minus the
 * opening. Drawn in the keep pass (baked) and again after the fish. */
#define CASTLE_GATE_X0 (-34)
#define CASTLE_GATE_X1  55
#define CASTLE_WALL_TOP (CASTLE_FY - 58)
/* the keep's curtain wall behind the gate, minus the opening (2026-10-17,
 * Strato: "let visibility pass straight through the castle arch so we see to
 * the back of the tank"): the arch paints NOTHING, so the water, the floor
 * and the pieces baked behind the castle show in it, the grass and the plant
 * draw through it (veg_span: the opening is not in the castle's mask), and a
 * fish in the gateway stays in front of them (render_tank's arch passes).
 * castle_opening's dark courtyard is gone with it. */
static void castle_back_wall(const cst_t *k) {
    int ys = CASTLE_FY - CASTLE_ARCH_S, R = CASTLE_ARCH_R;
    for (int y = CASTLE_WALL_TOP; y <= CASTLE_FY; y++) {
        float half = y > ys ? R : (ys - y) < R ? R * ell_half((ys - y) / (float)R) : 0;
        int h = (int)(half - 0.5f);
        if (half < 1) castle_span(k, -44, 44, y, 0, -44, 44);
        else { castle_span(k, -44, -h - 1, y, 0, -44, 44); castle_span(k, h + 1, 44, y, 0, -44, 44); }
    }
}
static void castle_gate_wall(const cst_t *k) {
    int ys = CASTLE_FY - CASTLE_ARCH_S, R = CASTLE_ARCH_R + CASTLE_TRIM;
    for (int y = CASTLE_WALL_TOP; y <= CASTLE_FY; y++) {
        float half = y > ys ? R : (ys - y) < R ? R * ell_half((ys - y) / (float)R) : 0;
        int h = (int)half;
        if (h < 1) castle_span(k, CASTLE_GATE_X0, CASTLE_GATE_X1, y, 0, -44, 44);
        else {
            castle_span(k, CASTLE_GATE_X0, -h - 1, y, 0, -44, 44);
            castle_span(k, h + 1, CASTLE_GATE_X1, y, 0, -44, 44);
        }
    }
    castle_merlons(k, -44, 44, CASTLE_WALL_TOP);
    castle_gate_trim(k);
}
/* the FRONT ROW: the gate wall and the three towers flush with it. Drawn in
 * the keep pass (baked) and again over the fish, clipped to their rects. */
static void castle_front_row(const cst_t *k) {
    int FY = CASTLE_FY;
    castle_gate_wall(k);
    /* the left tower, pointed */
    castle_rect(k, -62, -34, FY - 78, FY, 0);
    castle_cone(k, -48, 18, FY - 78, FY - 108);
    castle_window(k, -48, 7, FY - 62, FY - 48);
    /* the short far-left tower */
    castle_rect(k, -86, -60, FY - 46, FY, 0);
    castle_cone(k, -73, 17, FY - 46, FY - 72);
    castle_window(k, -73, 6, FY - 30, FY - 19);
    /* the open far-right tower, crenellated */
    castle_rect(k, 56, 86, FY - 70, FY, 0);
    castle_merlons(k, 56, 86, FY - 70);
    castle_window(k, 71, 7, FY - 44, FY - 30);
}
/* the whole castle (front = 0: the keep, then the front row over it - the
 * scene bake), or the front row alone (front = 1: over the fish) */
static void draw_castle(ctx_t *c, int cx, int front, bool final) {
    castle_tint_fill(c->dim);
    cst_t k = { c, cx, 1, final };
    if (front) { castle_front_row(&k); return; }
    k.layer = 0;
    int FY = CASTLE_FY;
    /* the base: a low rubble mound the towers stand on */
    for (int y = FY - 9; y <= FY + 6; y++) {
        float w = ell_half((y - (FY - 1)) / 8.0f);
        if (w <= 0) continue;
        int half = (int)(92 * w);
        castle_span(&k, -half, half, y, 2, (float)-half, (float)half);
    }
    /* the rear tower, tallest, right of centre: behind the wall */
    castle_rect(&k, 24, 54, FY - 108, FY, 0);
    castle_cone(&k, 39, 19, FY - 108, FY - 142);
    castle_window(&k, 39, 7, FY - 92, FY - 78);
    /* the curtain wall behind the gate's opening, then the courtyard's dark */
    castle_back_wall(&k);                    /* the arch is OPEN (2026-10-17): the tank behind shows through it */
    k.layer = 1;
    castle_front_row(&k);
}

/* ---- the coral, procedural (2026-09-23; Strato's coral-single.png: a
 * branching orange fan in chunky pixel art - rounded branches, a dark red
 * rim, sunlit yellow tips). Built ONCE as a tone sprite on a 2 px cell grid
 * (CORAL_CW x CORAL_CH cells): a skeleton of capsules (the trunk and its
 * branches), each cell classed by its signed distance to the nearest one -
 * the rim, a lit edge up-left, the tips, and a hashed speckle inside. The
 * colour is the keeper's (tank_coral_rgb): five tones derived from it and
 * hazed with the water per row, like the castle's stone. ~60 x 92 px. */
#define CORAL_CELL 2
#define CORAL_CW   32
#define CORAL_CH   46
#define CORAL_W    (CORAL_CW * CORAL_CELL)
#define CORAL_H    (CORAL_CH * CORAL_CELL)
#define CORAL_FY   (TANK_BOT - 14 + DECOR_SINK + g_dz_coral)   /* the base, sunk into the pebbles (a mound covers the joint); on its depth's line (0.3.9) */
enum { CO_NONE = 0, CO_RIM, CO_BODY, CO_SHADE, CO_LIT, CO_TIP, CO_N };
static int      g_coral_q = -1;               /* the growth step the sprite was built for (CORAL_Q steps) */
#define CORAL_Q 128                           /* ~5.6 h per step over the 30 days: a rebuild each */
static int      g_coral_cells;                /* filled cells in the sprite (the selftest) */
static uint32_t g_coral_rgb = 1;              /* what the rows hold (1 = never) */
static float    g_coral_dim = -1;
static int      g_coral_dzk = 99;
/* the decor scratch (2026-09-24): every table the coral and the reef cluster
 * build - sprites, tinted rows, the distance grids - lives in ONE block the
 * platform hands over (render_set_decor_scratch: the firmware gives PSRAM,
 * since internal RAM is down to ~23 KB free and the display driver needs
 * 57 KB of DMA memory at boot - the first cluster build's statics cost it
 * that and the tank booted black). Without one it is calloc'd (the sim). */
#define CL_CW   72
#define CL_CH   60
#define CL_H    (CL_CH * 2)
#define CL_TONES (1 + 5 * 5)
#define PK_CELL 2                                 /* the pumpkin lantern (below): a 2 px cell sprite like the coral's */
#define PK_CW   44
#define PK_CH   41
#define PK_N    16
#define VO_CELL 2                                 /* the bubble volcano (0.3.9, below): a 2 px cell sprite like the lantern's */
#define VO_CW   92
#define VO_CH   104
#define VO_W    (VO_CW * VO_CELL)
#define VO_H    (VO_CH * VO_CELL)
#define VO_GLOW_MAX 640
enum { VO_NONE = 0, VO_ROCK_L, VO_ROCK, VO_ROCK_D, VO_ROCK_DD, VO_SCORCH, VO_MOSS_A, VO_MOSS_B, VO_CAVE, VO_CAVE_MID,
       VO_CAVE_LIT, VO_LAVA_DIM, VO_LAVA, VO_LAVA_HOT, VO_LAVA_CORE, VO_N };          /* VO_CAVE_LIT.. glow: the lava's */
#define CR_CACHE_N 24                              /* the crab's grids kept (draw_crab): a walk is 16 poses, a climb ~26 */
typedef struct {
    uint8_t  co_sprite[CORAL_CH][CORAL_CW];
    uint16_t co_row[CO_N][CORAL_H];           /* [tone][row], row 0 = CORAL_FY - CORAL_H */
    int8_t   co_sd[CORAL_CH][CORAL_CW];
    uint8_t  co_tp[CORAL_CH][(CORAL_CW + 7) / 8];
    uint8_t  cl_sprite[CL_CH][CL_CW];
    uint16_t cl_row[CL_TONES][CL_H];
    int8_t   cl_sd[CL_CH][CL_CW];
    uint8_t  cl_dp[CL_CH][(CL_CW + 7) / 8];
    uint32_t pm[5][VO_H + 4][(CASTLE_MASK_W + 31) / 32];           /* the pieces' masks (2026-10-17): what piece k of the list (PIECES_MAX) covers
                                                                    * in the baked scene (sprite + mound); row 0 = its box's top, bit 0 its left
                                                                    * (the volcano, 0.3.9, is the tallest box) */
    uint32_t castle_front[CASTLE_ROWS + 12][(CASTLE_MASK_W + 31) / 32];   /* the castle's front row, centre-relative (castle_put) */
    /* the seahorse's frame scratch (0.3.6): its sprite (a byte a pixel), the
     * tail's distance map and its path - in PSRAM, never the stack or .bss */
    uint8_t  sh_spr[39 * 23], sh_d[39 * 23], sh_fr[39 * 23], sh_fl[39 * 23];
    uint8_t  sh_spr_card[39 * 23];                          /* the card's portrait: its own, so the tank's cached sprite
                                                             * is not overwritten under it (the tail wiggled, 10-15) */
    float    sh_rowx[39], sh_yoff;                          /* the upright draw's snapped row shifts and row offset (hysteresis) */
    float    sh_pts[140][3];                                /* x, y, arc length */
    uint8_t  sh_behind[140];
    uint8_t  sn_glass[35 * 35];                             /* the snail on the glass: what its heading alone decides (draw_snail) */
    uint8_t  pk_sprite[PK_CH][PK_CW];                       /* the pumpkin lantern's tones, built once */
    uint16_t pk_row[PK_N][PK_CH * PK_CELL];                 /* its rind tones per row, hazed and dimmed */
    uint8_t  pk_body[PK_CH][PK_CW], pk_hole[PK_CH][PK_CW];  /* the build's grids (internal RAM is spoken for) */
    uint8_t  vo_sprite[VO_CH][VO_CW];                       /* the bubble volcano's tones (0.3.9), built once */
    uint16_t vo_row[VO_N][VO_CH * VO_CELL];                 /* its rock tones per row, hazed and dimmed */
    uint16_t vo_glow[VO_GLOW_MAX];                          /* its lava cells (j * VO_CW + i): redrawn every frame */
    uint8_t  ur_tones[33 * 37];                              /* the urchin's tone grid, rebuilt a few times a second (2026-10-23) */
    uint8_t  cr_tones[CR_CACHE_N][50 * 25], cr_parts[50 * 25];   /* the strawberry crab's (0.3.9): its tone grids, one per pose seen lately (CR_GW x CR_GH), and the
                                                                   build's parts (scratch) */
    uint32_t cr_keys[CR_CACHE_N], cr_used[CR_CACHE_N];      /* ... each grid's pose (cr_hash; 0 = empty) and when it was last drawn (the least recent goes) */
    uint8_t  cr_card_tones[50 * 25], cr_card_parts[50 * 25]; /* its card's portrait, the roost (the tank's grid may hold a profile) */
    uint8_t  sn_tones[19 * 37];                              /* the upright snail's, likewise */
    uint16_t shelf_thumb[3][32 * 32];                       /* the shelf's little tanks (0.3.9): the plant, the coral, the cluster
                                                             * as they are, 32 x 32 (the shop icon's size), box-filtered from a
                                                             * full-size paint (shelf_build); the modal doubles it */
} decor_scratch_t;
static decor_scratch_t *g_ds;
static const uint8_t *g_cr_tn;                    /* the crab's grid as drawn this frame (draw_crab): crab_covers reads it */
static bool g_cr_nocache;                         /* the selftest's switch: a build every pose change, as before the cache */
static int  g_cr_builds;                          /* builds so far (the selftest counts them) */
void render_crab_cache(bool on) { g_cr_nocache = !on; }
int  render_crab_builds(void) { return g_cr_builds; }
static bool  g_pk_built;                          /* the pumpkin's sprite is in the scratch */
static float g_pk_dim = -1;                       /* the dim its rind rows were tinted for */
static int   g_pk_dzk = 99;                       /* and the shift (its depth) */
size_t render_decor_scratch_size(void) { return sizeof(decor_scratch_t); }
static float g_sng_heading;
void   render_set_decor_scratch(void *buf) { g_ds = (decor_scratch_t *)buf; if (g_ds) memset(g_ds, 0, sizeof *g_ds); g_cr_tn = NULL; g_coral_q = -1; g_coral_rgb = 1; g_sng_heading = 1e9f; g_pk_built = false; g_pk_dim = -1; }
static inline decor_scratch_t *ds(void) { if (!g_ds) g_ds = (decor_scratch_t *)calloc(1, sizeof(decor_scratch_t)); return g_ds; }
static uint32_t (*castle_front(void))[224 / 32] { return ds()->castle_front; }
/* the bake of a piece IN FRONT records what it covers (bake_scene sets these) */
static uint32_t *g_rec; static int g_rec_x0, g_rec_y0, g_rec_w, g_rec_rows, g_rec_wpr;
static inline void rec_px(int x, int y) {
    if (!g_rec) return;
    int lx = x - g_rec_x0, r = y - g_rec_y0;
    if ((unsigned)lx < (unsigned)g_rec_w && (unsigned)r < (unsigned)g_rec_rows) g_rec[r * g_rec_wpr + (lx >> 5)] |= 1u << (lx & 31);
}
typedef struct { float x0, y0, x1, y1, r; uint8_t tip; float g0, g1; } coral_seg_t;
/* cell coordinates, x from the left edge, y UP from the base row. g0..g1 is
 * the segment's GROWTH window (tank_coral_growth, 2026-09-23): absent below
 * g0, reaching out along its line until g1 - a child branch's window opens
 * where its parent's closes, so the fan comes in trunk first, low branches,
 * then the crown over the month. */
static const coral_seg_t CORAL_SEGS[] = {
    { 16, 0, 17, 20, 3.0f, 0, 0.00f, 0.30f },   /* the trunk: thick, like the art's */
    { 17, 20, 18, 33, 2.7f, 0, 0.30f, 0.60f },
    { 18, 33, 18, 41, 2.3f, 1, 0.60f, 0.85f },  /* its tip */
    { 16, 7, 6, 15, 2.5f, 0, 0.12f, 0.40f },    /* left, low */
    { 6, 15, 3, 22, 2.2f, 1, 0.40f, 0.62f },
    { 17, 12, 26, 18, 2.4f, 0, 0.20f, 0.48f },  /* right, low */
    { 26, 18, 29, 25, 2.1f, 1, 0.48f, 0.70f },
    { 17, 20, 9, 28, 2.4f, 0, 0.36f, 0.62f },   /* left, mid */
    { 9, 28, 7, 35, 2.1f, 1, 0.62f, 0.82f },
    { 18, 26, 25, 33, 2.3f, 0, 0.45f, 0.72f },  /* right, mid */
    { 25, 33, 27, 40, 2.0f, 1, 0.72f, 0.92f },
    { 18, 31, 12, 39, 2.2f, 1, 0.62f, 1.00f },  /* left, high: the last to finish */
    { 17, 3, 22, 7, 1.9f, 1, 0.05f, 0.20f },    /* a nub */
};
#define CORAL_NSEG ((int)(sizeof CORAL_SEGS / sizeof CORAL_SEGS[0]))
/* signed distance to the skeleton AT growth g, in cells; *tip = within a
 * lit cap (a finished tip's, or the reaching end of a branch still growing) */
#define CORAL_Y_SCALE 0.88f                                  /* the fan tops out ~76 px up (Strato: a modest height, never the grass's) */
#define CORAL_TOP_X   18.0f                                  /* the crown's root: the trunk tip's end, in cells */
#define CORAL_TOP_Y   (41.0f * CORAL_Y_SCALE)
static float coral_sd(float px, float py, float g, bool *tip) {
    float best = 1e9f; *tip = false;
    if (g > 1) g = 1;
    float thin = 0.72f + 0.28f * g;                          /* a young coral is slimmer all over */
    for (int i = 0; i < CORAL_NSEG; i++) {
        const coral_seg_t *s = &CORAL_SEGS[i];
        float f = (g - s->g0) / (s->g1 - s->g0);             /* how far along its line it has reached */
        if (f <= 0) continue;
        if (f > 1) f = 1;
        float sy0 = s->y0 * CORAL_Y_SCALE, sy1 = s->y1 * CORAL_Y_SCALE;
        float x1 = s->x0 + (s->x1 - s->x0) * f, y1 = sy0 + (sy1 - sy0) * f;
        float r = s->r * thin * (0.75f + 0.25f * f);
        float dx = x1 - s->x0, dy = y1 - sy0, len2 = dx * dx + dy * dy;
        float u = len2 > 1e-6f ? ((px - s->x0) * dx + (py - sy0) * dy) / len2 : 0;
        if (u < 0) u = 0;
        if (u > 1) u = 1;
        float ex = px - (s->x0 + dx * u), ey = py - (sy0 + dy * u);
        float d = sqrtf(ex * ex + ey * ey) - r;
        if (d < best) best = d;
        if (s->tip || f < 1) {                               /* the lit cap: the last r + 1 cells of a tip */
            float tx = px - x1, ty = py - y1;
            if (tx * tx + ty * ty <= (r + 0.6f) * (r + 0.6f)) *tip = true;
        }
    }
    return best;
}
/* the distance grids are int8 eighths of a cell (the sign and the few
 * thresholds are all the classing needs): internal RAM is tight on the
 * board - the float grids of 2026-09-24 morning cost the display driver its
 * semaphore and the tank booted black */
static inline int8_t sd_q(float d) { d *= 8; return (int8_t)(d < -127 ? -127 : d > 127 ? 127 : d); }
static void coral_build(float g) {
    int8_t  (*sd)[CORAL_CW] = ds()->co_sd;
    uint8_t (*tp)[(CORAL_CW + 7) / 8] = ds()->co_tp;
    g_coral_cells = 0;
    for (int j = 0; j < CORAL_CH; j++)
        for (int i = 0; i < CORAL_CW; i++) {
            bool tip; sd[j][i] = sd_q(coral_sd(i + 0.5f, (CORAL_CH - 1 - j) + 0.5f, g, &tip));
            if (tip) tp[j][i >> 3] |= (uint8_t)(1 << (i & 7)); else tp[j][i >> 3] &= (uint8_t)~(1 << (i & 7));
        }
    for (int j = 0; j < CORAL_CH; j++)
        for (int i = 0; i < CORAL_CW; i++) {
            int d = sd[j][i];
            bool tip = (tp[j][i >> 3] >> (i & 7)) & 1;
            uint8_t t = CO_NONE;
            if (d < 0) {
                /* the light comes from the upper left (the art's): an edge cell
                   with open water above or to its left is LIT, one with open
                   water below or to its right is the dark rim - the outline
                   that keeps the branches apart */
                bool out_l = i == 0 || sd[j][i - 1] >= 0, out_u = j == 0 || sd[j - 1][i] >= 0;
                bool out_r = i == CORAL_CW - 1 || sd[j][i + 1] >= 0, out_d = j == CORAL_CH - 1 || sd[j + 1][i] >= 0;
                bool edge = out_l || out_u || out_r || out_d;
                if (tip) t = (out_r || out_d) && !(out_l || out_u) ? CO_RIM : CO_TIP;
                else if (edge) t = (out_r || out_d) ? CO_RIM : CO_LIT;
                else {
                    uint32_t h = chash(i, j) % 100;
                    t = h < 22 ? CO_SHADE : h < 28 ? CO_LIT : CO_BODY;
                }
            }
            ds()->co_sprite[j][i] = t;
            g_coral_cells += t != CO_NONE;
        }
}
int render_coral_cells(float growth) {                       /* the selftest: how much coral there is at a growth */
    coral_build(growth); g_coral_q = -1;
    return g_coral_cells;
}
static void coral_tint_fill(uint32_t rgb, float dim) {
    if (g_coral_rgb == rgb && g_coral_dim == dim && g_coral_dzk == g_dz_coral) return;
    float haze = decor_haze(0.90f, g_dz_coral);
    uint32_t tone[CO_N];
    tone[CO_NONE]  = 0;
    tone[CO_RIM]   = mix(rgb, 0x30060e, 0.58f);
    tone[CO_BODY]  = rgb;
    tone[CO_SHADE] = mix(rgb, 0x000000, 0.24f);
    tone[CO_LIT]   = mix(rgb, 0xfff0b0, 0.36f);
    tone[CO_TIP]   = mix(rgb, 0xffe8a0, 0.58f);
    for (int r = 0; r < CORAL_H; r++) {
        int y = CORAL_FY - CORAL_H + r;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        for (int k = 1; k < CO_N; k++) ds()->co_row[k][r] = rgb565(mix(w, tone[k], k == CO_TIP ? haze + 0.04f : haze), dim);   /* a light haze: the art's colour stays saturated */
    }
    g_coral_rgb = rgb; g_coral_dim = dim; g_coral_dzk = g_dz_coral;
}
/* the crown (2026-09-23, Strato: "after it's fully grown it should sprout a
 * ring of delicate tentacles out of the top"): CORAL_TENT one-pixel
 * filaments fanning up and out from the trunk's tip once growth passes 1,
 * reaching CORAL_TENT_L px at CORAL_FULL, each swaying on its own phase with
 * the tank clock and ending in a bright bead. Drawn every frame over the
 * body (dynamic: the caller's rect covers the vignette), so the crown moves
 * while the fan stands still. Returns its bounding box through the pointers. */
#define CORAL_TENT   9
#define CORAL_TENT_L 15.0f
/* a fan of n one-pixel tentacles from (ox, oy), angles a0..a1 (screen
 * radians: -pi/2 is straight up), each `len` px, swaying on the tank clock
 * with its own phase (seed), a bright bead at the end; the bounding box
 * through the pointers (x1 < x0 = nothing drawn). The coral's crown and the
 * cluster's bloom both use it (2026-09-24). */
static void draw_crown(ctx_t *c, float ox, float oy, int n_t, float a0, float a1, float len, uint32_t pale,
                       int seed, float clock, int *bx0, int *by0, int *bx1, int *by1) {
    *bx0 = *by0 = 1 << 20; *bx1 = *by1 = -1;
    if (n_t < 1) return;
    uint32_t bead = mix(pale, 0xffffff, 0.55f);
    for (int k = 0; k < n_t; k++) {
        float a = n_t == 1 ? (a0 + a1) * 0.5f : a0 + (a1 - a0) * k / (n_t - 1);
        float sway = 0.22f * fast_sin(clock * 1.3f + (k + seed) * 0.9f);
        float lx = ox, ly = oy;
        int n = (int)(len + 0.5f);
        for (int i = 1; i <= n; i++) {
            float s = (float)i / n;
            float ang = a + sway * s + (k - (n_t - 1) * 0.5f) * 0.05f * s;   /* the outer ones curl outward */
            float x = ox + cosf(ang) * len * s, y = oy + sinf(ang) * len * s;
            int ix = (int)(x + 0.5f), iy = (int)(y + 0.5f);
            if (ix == (int)(lx + 0.5f) && iy == (int)(ly + 0.5f)) continue;
            if (g_clip_k >= 0 && pc_covered_above(g_clip_k, ix, iy)) continue;   /* under a piece above its own */
            px_blend(c, ix, iy, i == n ? bead : pale, i == n ? 255 : 190);
            if (ix < *bx0) *bx0 = ix;
            if (ix > *bx1) *bx1 = ix;
            if (iy < *by0) *by0 = iy;
            if (iy > *by1) *by1 = iy;
            lx = x; ly = y;
        }
    }
    if (*bx1 >= *bx0) { *bx0 -= 1; *by0 -= 1; *bx1 += 1; *by1 += 1; }
}
static void draw_coral_crown(ctx_t *c, int cx, uint32_t rgb, float growth, float clock,
                             int *bx0, int *by0, int *bx1, int *by1) {
    float phase = (growth - 1.0f) / (CORAL_FULL - 1.0f);
    *bx0 = *by0 = 1 << 20; *bx1 = *by1 = -1;
    if (phase <= 0) return;
    if (phase > 1) phase = 1;
    float ox = cx - CORAL_W / 2 + CORAL_TOP_X * CORAL_CELL + 1, oy = CORAL_FY - CORAL_TOP_Y * CORAL_CELL - 1;
    float len = CORAL_TENT_L * (0.35f + 0.65f * phase);
    /* a fan from ~29 deg left of up to 29 deg right, past the horizontal */
    draw_crown(c, ox, oy, CORAL_TENT, -3.14159f * 0.16f, -3.14159f * 0.84f, len, mix(rgb, 0xffffff, 0.55f), 0, clock, bx0, by0, bx1, by1);
}
/* a low mound of floor stones round a piece's base (2026-09-24): the same
 * pebble tones and position hash as the floor, an ellipse half_w wide whose
 * crest sits 6 px above the base line and whose skirt runs 3 px below it,
 * drawn AFTER the piece so its bottom rows are buried - the coral's trunk
 * and the cluster's rock grow out of the floor instead of standing on it.
 * final: vignetted and untagged here (the scene-cache path). */
static void draw_floor_mound(ctx_t *c, int cx, int half_w, int rise, int dz, bool final) {
    const int fl = TANK_BOT - 14 + dz;                                     /* the floor line: the pebbles' highest top, on the piece's depth (0.3.9) */
    for (int y = fl - rise; y <= fl + 4; y++) {
        if (y < 0 || y >= TANK_H) continue;
        float w = ell_half((y - (fl + 2)) / (float)(rise + 2));           /* an ellipse whose crest is `rise` px above the floor line */
        if (w <= 0) continue;
        int half = (int)(half_w * w);
        for (int x = cx - half; x <= cx + half; x++) {
            if (!CTX_IN(c, x, y)) continue;
            uint32_t h2 = (uint32_t)((x * 73856093u) ^ (y * 19349663u));
            uint32_t tone = (h2 >> 4) % 16;
            uint32_t col = tone < 2 ? 0x2e3b2c : tone < 5 ? 0x22301f : tone < 8 ? 0x1a2418 : 0x101a12;
            float up = (float)(fl - y) / rise;                           /* 0 at the floor line, 1 at the crest */
            if (up > 0) col = mix(col, 0x56684f, 0.25f + 0.40f * up);   /* the hump rises into the light; its skirt is the floor */
            if (up > 0.15f && (chash(x, y) & 15) == 0) col = 0x6e7f66;   /* a few lit grains */
            uint16_t *p = &CTX_PX(c, x, y);
            *p = FB_SWAP16(rgb565(col, c->dim)); TOUCH_PX(c, x, y);
            rec_px(x, y);
            if (final) {
                PK_LIGHT_PX(p, x, y);
                int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
                if (a) px_darken(p, a);
                if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
            }
        }
    }
}
/* the coral at centre x (its base on the floor), in the keeper's colour.
 * final: vignetted here and untagged (the scene-cache path); otherwise the
 * frame's own sweep does it */
static void draw_coral(ctx_t *c, int cx, uint32_t rgb, float growth, bool final) {
    if (growth > 1) growth = 1;
    int q = (int)(growth * (CORAL_Q - 0.01f));
    if (q != g_coral_q) { coral_build(growth); g_coral_q = q; }
    coral_tint_fill(rgb, c->dim);
    int x0 = cx - CORAL_W / 2, y0 = CORAL_FY - CORAL_H;
    for (int j = 0; j < CORAL_CH; j++)
        for (int i = 0; i < CORAL_CW; i++) {
            uint8_t t = ds()->co_sprite[j][i];
            if (!t) continue;
            for (int dy = 0; dy < CORAL_CELL; dy++) {
                int y = y0 + j * CORAL_CELL + dy;
                uint16_t v = ds()->co_row[t][j * CORAL_CELL + dy];
                for (int dx = 0; dx < CORAL_CELL; dx++) {
                    int x = x0 + i * CORAL_CELL + dx;
                    if (!CTX_IN(c, x, y)) continue;
                    uint16_t *p = &CTX_PX(c, x, y);
                    *p = FB_SWAP16(v); TOUCH_PX(c, x, y);
                    rec_px(x, y);
                    if (final) {
                        PK_LIGHT_PX(p, x, y);
                        int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
                        if (a) px_darken(p, a);
                        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
                    }
                }
            }
        }
    draw_floor_mound(c, cx, CORAL_HALF_W - 4, 9, g_dz_coral, final);              /* anchored: the trunk grows out of a hump of stones */
}

/* ---- the pumpkin lantern, procedural (0.3.6, SD_ITEM_PUMPKIN: the first
 * seasonal piece, in the shop in October; Strato's working-assets/
 * pumpkin-lantern, prototyped 2026-10-04) ----
 * A ribbed orange pumpkin with a crooked olive stem and a carved face - two
 * triangle eyes, a nose, a wide shouting mouth with two teeth - built ONCE
 * as a tone sprite on the coral's 2 px cell grid (PK_CW x PK_CH): five
 * overlapping lobes make the ribbed outline and the grooves between them,
 * the light comes from the upper left (the tank's, not the art's upper
 * right), and a mound of floor stones buries its base.
 * The candle is the point of it. Lights ON: the carved face shows a warm
 * lit inside and a little flame. Lights OFF: the face burns at full
 * brightness through the night's dim, the rind is a touch lit from within,
 * and its light falls on everything near it (the night light, above).
 * Per frame: BEHIND, the body is baked into the scene and only the carved
 * face and the flame are redrawn (~700 px of opaque stores) with one
 * flicker level for the frame - the candle; IN FRONT, the whole sprite
 * (~3.6k px) is drawn live over the grass and the fish. The light itself
 * stays still: a flickering light would be ~20k px of read-modify-write a
 * frame against the 25 fps / 4 s decisions budget, so the face does the
 * flickering. */
enum { PK_NONE = 0, PK_RIM, PK_SHADE, PK_BODY, PK_LIT, PK_GLEAM, PK_GROOVE,
       PK_STEM_D, PK_STEM, PK_STEM_L,
       PK_CUT, PK_CUT_HI, PK_IN0, PK_IN1, PK_IN2, PK_IN3 };      /* PK_CUT.. glow: candle-lit */
#define PK_EMIT  PK_CUT
#define PK_W     (PK_CW * PK_CELL)
#define PK_H     (PK_CH * PK_CELL)
#define PK_FY    (TANK_BOT - 14 + DECOR_SINK + g_dz_pk)         /* the base, sunk into the pebbles; on its depth's line (0.3.9) */
#define PK_S     1.2f                                           /* the drawing's units (below) per cell: the art is laid
                                                                 * out on a 36 x 34 grid and scaled up to PK_CW x PK_CH */
#define PK_MID   18.0f                                          /* the body's centre column, in drawing units */
#define PK_FLAME_X 18.0f                                        /* the candle on the floor inside, in drawing units */
#define PK_FLAME_Y 3.6f
static const uint32_t PK_RGB[PK_N] = {
    0, 0x8a2406, 0xc2420e, 0xe8641a, 0xf88a2c, 0xffd8a0, 0xa83410,
    0x3c4214, 0x66702a, 0x9aa24a,
    0xf89a36, 0xffc04a, 0x4a1206, 0x8a2a0a, 0xd8601a, 0xffa83a };
static const uint32_t PK_RGB_NIGHT[PK_N] = {                    /* the glow tones with the light off: the candle has the tank to itself */
    [PK_CUT] = 0xffa83a, [PK_CUT_HI] = 0xffd470,
    [PK_IN0] = 0xa8380c, [PK_IN1] = 0xe86818, [PK_IN2] = 0xffa630, [PK_IN3] = 0xffe6a0 };
static int g_pk_cells, g_pk_glow_cells;                         /* the sprite's filled / glowing cells (the sim's numbers) */
/* the five lobes: centre x offset, radii, in cells (y up from the base) */
static const float PK_LOX[5] = { -10.5f, -5.4f, 0.0f, 5.4f, 10.5f };
static const float PK_LRX[5] = { 6.8f, 7.0f, 7.0f, 7.0f, 6.8f };
static const float PK_LRY[5] = { 9.6f, 11.2f, 11.8f, 11.2f, 9.6f };
static bool pk_in_tri(float px, float py, float ax, float ay, float bx, float by, float cx, float cy) {
    float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
    float d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
    float d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);
    bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}
/* a carved hole at cell point (px, py)? */
static bool pk_hole(float px, float py) {
    if (pk_in_tri(px, py, 8.4f, 14.4f, 15.6f, 14.4f, 13.6f, 20.4f)) return true;    /* the eyes, slanted in */
    if (pk_in_tri(px, py, 20.4f, 14.4f, 27.6f, 14.4f, 22.4f, 20.4f)) return true;
    if (pk_in_tri(px, py, 16.2f, 11.8f, 19.8f, 11.8f, 18.0f, 14.4f)) return true;    /* the nose */
    float ex = (px - PK_MID) / 10.6f, ey = (py - 7.0f) / 4.6f;                       /* the mouth, wide open */
    if (ex * ex + ey * ey > 1 || py > 11.0f) return false;
    if (pk_in_tri(px, py, 11.6f, 11.4f, 13.6f, 11.4f, 12.6f, 8.9f)) return false;    /* its two teeth */
    if (pk_in_tri(px, py, 22.4f, 11.4f, 24.4f, 11.4f, 23.4f, 8.9f)) return false;
    return true;
}
/* the stem: a crooked capsule chain from the crown, leaning left at the top */
static float pk_stem_d(float px, float py, float *side) {
    static const float SX[4] = { 18.6f, 19.2f, 17.8f, 15.0f }, SY[4] = { 21.0f, 26.0f, 30.0f, 32.2f }, SR[3] = { 2.5f, 2.1f, 1.7f };
    float best = 1e9f; *side = 0;
    for (int i = 0; i < 3; i++) {
        float dx = SX[i + 1] - SX[i], dy = SY[i + 1] - SY[i], len2 = dx * dx + dy * dy;
        float u = ((px - SX[i]) * dx + (py - SY[i]) * dy) / len2;
        if (u < 0) u = 0;
        if (u > 1) u = 1;
        float ex = px - (SX[i] + dx * u), ey = py - (SY[i] + dy * u);
        float d = sqrtf(ex * ex + ey * ey) - SR[i];
        if (d < best) { best = d; float l = sqrtf(len2); *side = (ex * dy - ey * dx) / l / SR[i]; }   /* -1 left .. +1 right of the line */
    }
    return best;
}
static void pk_build(void) {
    uint8_t (*sp)[PK_CW] = ds()->pk_sprite;
    uint8_t (*body)[PK_CW] = ds()->pk_body, (*hole)[PK_CW] = ds()->pk_hole;
    for (int j = 0; j < PK_CH; j++)
        for (int i = 0; i < PK_CW; i++) {
            float px = (i + 0.5f) / PK_S - 0.33f, py = ((PK_CH - 1 - j) + 0.5f) / PK_S;
            float e1 = 9, e2 = 9; int k1 = -1;
            for (int k = 0; k < 5; k++) {
                float ex = (px - PK_MID - PK_LOX[k]) / PK_LRX[k], ey = (py - PK_LRY[k] - 0.2f) / PK_LRY[k];
                float e = ex * ex + ey * ey;
                if (e < e1) { e2 = e1; e1 = e; k1 = k; } else if (e < e2) e2 = e;
            }
            body[j][i] = e1 <= 1.0f ? (uint8_t)(k1 + 1) : 0;
            hole[j][i] = body[j][i] && pk_hole(px, py);
            uint8_t t = PK_NONE;
            if (body[j][i]) {
                int k = k1;
                float nx = (px - PK_MID - PK_LOX[k]) / PK_LRX[k], ny = (py - PK_LRY[k] - 0.2f) / PK_LRY[k];
                float lit = -nx * 0.55f + ny * 0.83f - 0.25f * (px - PK_MID) / 18.0f;   /* the right of the whole pumpkin falls off */
                t = lit > 0.50f ? PK_LIT : lit < -0.40f ? PK_SHADE : PK_BODY;
                bool face = py < 21.4f && py > 1.4f && fabsf(px - PK_MID) < 11.2f;   /* the carving: no rib lines across it */
                if (e2 <= 1.0f && e2 - e1 < 0.15f && !face) t = PK_GROOVE;                /* where two ribs meet */
                if (k == 1 && fabsf(nx + 0.52f) < 0.13f && ny > 0.38f && ny < 0.78f) t = PK_GLEAM;   /* a wet streak on the lit rib */
                if (t != PK_GLEAM && !face && (chash(i + 90, j + 90) & 31) == 0) t = t == PK_LIT ? PK_BODY : t == PK_BODY ? PK_SHADE : t;   /* a mottle */
            }
            float side, sd = pk_stem_d(px, py, &side);
            if (sd < 0 && (!body[j][i] || py > 20.0f))
                t = side > 0.35f ? PK_STEM_D : side < -0.30f || (chash(i, j + 7) & 7) == 0 ? PK_STEM_L : PK_STEM;
            sp[j][i] = t;
        }
    /* the outline, the cuts and the inside, from the neighbours */
    for (int j = 0; j < PK_CH; j++)
        for (int i = 0; i < PK_CW; i++) {
            uint8_t t = sp[j][i];
            if (!body[j][i] || (t >= PK_STEM_D && t <= PK_STEM_L)) continue;
            float px = (i + 0.5f) / PK_S - 0.33f, py = ((PK_CH - 1 - j) + 0.5f) / PK_S;
            if (hole[j][i]) {                                     /* the inside: lit by the candle on its floor */
                float dx = fabsf(px - PK_FLAME_X), dy = (py - PK_FLAME_Y) * 0.62f, d = sqrtf(dx * dx + dy * dy);
                if (py < 11.4f)                                   /* the mouth: a lit floor across it, the back wall darker up high */
                    sp[j][i] = py < 4.8f ? (dx < 4.0f ? PK_IN3 : PK_IN2) : py < 7.2f ? (dx < 6.5f ? PK_IN2 : PK_IN1) : py < 9.4f ? PK_IN1 : PK_IN0;
                else sp[j][i] = d < 10.5f ? PK_IN1 : PK_IN0;      /* the eyes and the nose: the lit dome inside */
                continue;
            }
            bool hu = j > 0 && hole[j - 1][i], hd = j < PK_CH - 1 && hole[j + 1][i];
            bool hl = i > 0 && hole[j][i - 1], hr = i < PK_CW - 1 && hole[j][i + 1];
            if (hu || hd || hl || hr) { sp[j][i] = hu ? PK_CUT_HI : PK_CUT; continue; }   /* the carved lip: the one under a hole catches the flame */
            bool ol = i == 0 || !body[j][i - 1], ou = j == 0 || (!body[j - 1][i] && sp[j - 1][i] == PK_NONE);
            bool orr = i == PK_CW - 1 || !body[j][i + 1], od = j == PK_CH - 1 || !body[j + 1][i];
            if (orr || od) sp[j][i] = PK_RIM;
            else if (ol || ou) sp[j][i] = PK_LIT;
        }
    g_pk_cells = g_pk_glow_cells = 0;
    for (int j = 0; j < PK_CH; j++)
        for (int i = 0; i < PK_CW; i++) { g_pk_cells += sp[j][i] != PK_NONE; g_pk_glow_cells += sp[j][i] >= PK_EMIT; }
    g_pk_built = true;
}
int render_pumpkin_px(int *glow) {                               /* its pixels, and how many of them are redrawn a frame (+ the flame's ~22) */
    if (!g_pk_built) pk_build();
    if (glow) *glow = g_pk_glow_cells * PK_CELL * PK_CELL + 22;
    return g_pk_cells * PK_CELL * PK_CELL;
}
/* the rind's rows: hazed with the water like the coral's, dimmed - and at
 * night dimmed less (the shell is lit a little from within) and warmed */
static void pk_tint_fill(float dim) {
    if (g_pk_dim == dim && g_pk_dzk == g_dz_pk) return;
    bool night = dim < 1.0f;
    float haze = decor_haze(0.90f, g_dz_pk);
    float rd = night ? 0.62f : 1.0f;
    for (int r = 0; r < PK_H; r++) {
        int y = PK_FY - PK_H + r;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        for (int k = 1; k < PK_EMIT; k++) {
            uint32_t col = PK_RGB[k];
            if (night && k < PK_STEM_D) col = mix(col, 0xff6a10, 0.18f);
            ds()->pk_row[k][r] = rgb565(mix(w, col, haze), k >= PK_STEM_D ? dim : rd);
        }
    }
    g_pk_dim = dim; g_pk_dzk = g_dz_pk;
}
/* the candle this frame: a level 0.78..1.0 from a few incommensurate beats,
 * with now and then a deeper gutter */
static float pk_flicker(float clock) {
    float f = 0.90f + 0.045f * fast_sin(clock * 11.3f + 8 * TAU) + 0.035f * fast_sin(clock * 17.9f + 1.3f + 8 * TAU)
                    + 0.025f * fast_sin(clock * 4.7f + 0.4f + 8 * TAU);
    float g = fast_sin(clock * 0.37f + 8 * TAU);
    if (g > 0.93f) f -= (g - 0.93f) * 1.6f;
    return f;
}
/* the pumpkin BEHIND under a castle IN FRONT (2026-10-17, Strato's 1.8: a
 * fish in the arch showed as a patch of castle over the pumpkin): the castle
 * is over it - the bake paints the castle after it, and every frame the face
 * and the flame leave the castle's pixels alone (the castle's silhouette,
 * g_castle_mask, relative to its centre) */
static inline bool castle_covers(int x, int y) {
    if (g_castle_cx < 0) return false;
    int r = y - (CASTLE_FY - CASTLE_ROWS), lx = x - g_castle_cx + CASTLE_MASK_W / 2;
    return (unsigned)r < CASTLE_ROWS + 12 && (unsigned)lx < CASTLE_MASK_W && ((g_castle_mask[r][lx >> 5] >> (lx & 31)) & 1);
}
/* the castle's openings, in the frame's coordinates: the arch (castle_opening's
 * shape) and the four windows (castle_window's: a half-disc vault over a
 * slot), where the light behind it comes through */
static bool castle_arch_at(int cx, int x, int y) {   /* inside the arch's opening of a castle centred at cx */
    int lx = x - cx, ys = CASTLE_FY - CASTLE_ARCH_S;
    if (y > CASTLE_FY || y < ys - CASTLE_ARCH_R) return false;
    float half = y > ys ? CASTLE_ARCH_R : CASTLE_ARCH_R * ell_half((ys - y) / (float)CASTLE_ARCH_R);
    return half >= 1 && lx >= (int)(-half + 0.5f) && lx <= (int)(half - 0.5f);
}
static bool castle_arch(int x, int y) { return castle_arch_at(g_veg_mask_cx, x, y); }   /* (the castle IN FRONT: the grass's arch passes) */
/* the arch's SHADE (2026-10-17, Strato: "anything seen through the arch
 * should appear a bit darker, shadowed"): the gateway is a tunnel, so what
 * shows in it - the water and the floor behind, the grass, a fish passing,
 * the rubble - is darkened, more under the vault than at the foot. A castle
 * BEHIND: in the bake, right after it (what is painted over it later is not
 * in the tunnel); IN FRONT: every frame after the fish, skipping the pieces
 * stacked over the castle. Lighter at night, and none while the lantern
 * behind the castle lights the tunnel - that glow is the point of it. */
static int arch_shade_alpha(int y, bool night) {
    int top = CASTLE_FY - CASTLE_ARCH_S - CASTLE_ARCH_R;
    float t = (float)(y - top) / (float)(CASTLE_ARCH_S + CASTLE_ARCH_R);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return night ? (int)(48 - 22 * t) : (int)(105 - 50 * t);
}
static bool pc_covered_above(int k, int x, int y);
static void arch_shade(ctx_t *c, int kc, int cx, bool night, bool over_check) {
    if (night && g_pk_under_castle && g_pk_lx >= 0) return;
    for (int y = CASTLE_FY - CASTLE_ARCH_S - CASTLE_ARCH_R; y <= CASTLE_FY; y++) {
        int a = arch_shade_alpha(y, night);
        for (int x = cx - CASTLE_ARCH_R; x <= cx + CASTLE_ARCH_R; x++) {
            if (!CTX_IN(c, x, y) || !castle_arch_at(cx, x, y)) continue;
            if (over_check && pc_covered_above(kc, x, y)) continue;
            px_darken(&CTX_PX(c, x, y), a); TOUCH_PX(c, x, y);
        }
    }
}
/* a castle IN FRONT that is in the scene: the bake shaded the tunnel (pc_draw),
 * so each frame only what was DRAWN in it - a fish, a frond of the early
 * arch pass, a bubble - takes the shade (2026-10-23: the whole opening was
 * re-shaded every frame, 2.9 ms of a square frame) */
static void arch_shade_dirty(ctx_t *c, int kc, int cx, bool night) {
    if (night && g_pk_under_castle && g_pk_lx >= 0) return;
    if (!g_dirty) { arch_shade(c, kc, cx, night, true); return; }
    int x0 = cx - CASTLE_ARCH_R, x1 = cx + CASTLE_ARCH_R;
    int wa = (x0 < 0 ? 0 : x0) >> 5, wb = (x1 > TANK_W - 1 ? TANK_W - 1 : x1) >> 5;
    for (int y = CASTLE_FY - CASTLE_ARCH_S - CASTLE_ARCH_R; y <= CASTLE_FY; y++) {
        if ((unsigned)y >= (unsigned)TANK_H) continue;
        int a = arch_shade_alpha(y, night);
        const uint32_t *drow = g_dirty + y * DIRTY_WORDS_PER_ROW;
        for (int w = wa; w <= wb; w++) {
            uint32_t bits = drow[w];
            while (bits) {
                int x = (w << 5) + __builtin_ctz(bits); bits &= bits - 1;
                if (x < x0 || x > x1 || !CTX_IN(c, x, y) || !castle_arch_at(cx, x, y)) continue;
                if (pc_covered_above(kc, x, y)) continue;
                px_darken(&CTX_PX(c, x, y), a);
            }
        }
    }
}
static bool castle_open(int x, int y) {
    int lx = x - g_castle_cx, ys = CASTLE_FY - CASTLE_ARCH_S;
    if (y <= CASTLE_FY && y >= ys - CASTLE_ARCH_R) {                        /* the arch, at the castle's own centre */
        float half = y > ys ? CASTLE_ARCH_R : CASTLE_ARCH_R * ell_half((ys - y) / (float)CASTLE_ARCH_R);
        if (half >= 1 && lx >= (int)(-half + 0.5f) && lx <= (int)(half - 0.5f)) return true;
    }
    static const struct { int lxc, w, top, bot; } W[4] = {                 /* castle_front_row's and the keep's windows (rows up from CASTLE_FY) */
        { 39, 7, 92, 78 }, { -48, 7, 62, 48 }, { -73, 6, 30, 19 }, { 71, 7, 44, 30 } };
    for (int k = 0; k < 4; k++) {
        int top = CASTLE_FY - W[k].top, bot = CASTLE_FY - W[k].bot;
        if (y < top || y > bot) continue;
        float r = W[k].w / 2.0f, dy = (top + r) - y, half = dy > 0 ? r * ell_half(dy / r) : r;
        if (lx >= (int)(W[k].lxc - half + 0.5f) && lx <= (int)(W[k].lxc + half - 0.5f)) return true;
    }
    return false;
}
/* the castle's shadow: its stone takes the spill, its openings the whole
 * light - and below its base line the SAND in front of it is dark (Strato
 * 2026-10-17: "light would never reach the sand below / in front of the
 * castle"), its footprint plus a 3 px step of spill at either edge, but for
 * the pool the gate throws on it: the arch's width, fanning out with the
 * distance and leaning away from the lantern's side (the light comes through
 * the gate at a slant), never more than the arch's own width off centre */
static int pk_shade(int x, int y) {
    if (g_pk_under_castle) {
        int lx = x - g_castle_cx;
        if (y > CASTLE_FY) {
            int alx = lx < 0 ? -lx : lx;
            if (alx <= CASTLE_HALF_W + 3) {
                float d = (float)(y - CASTLE_FY), pool = CASTLE_ARCH_R + d * 0.6f;
                float c = -(float)(g_pk_lx - g_castle_cx) * d / 80.0f;
                if (c > CASTLE_ARCH_R) c = CASTLE_ARCH_R;
                if (c < -CASTLE_ARCH_R) c = -CASTLE_ARCH_R;
                if (fabsf(lx - c) <= pool) return 0;              /* the gate's pool: the castle's word */
                return alx > CASTLE_HALF_W ? 2 : 3;
            }
        } else if (castle_covers(x, y)) return castle_open(x, y) ? 0 : 2;
    }
    if (y <= PK_FLOOR_Y) return 0;
    int lvl = 0;                                                  /* the sand: under what stands in front of the lantern */
    for (int k = 0; k < g_pk_cast_n && lvl < 3; k++) {
        if (x < g_pk_cast[k].x0 - 3 || x > g_pk_cast[k].x1 + 3) continue;
        int l = g_pk_cast[k].level;
        if (l == 3 && (x < g_pk_cast[k].x0 || x > g_pk_cast[k].x1)) l = 2;   /* a solid piece's edge: a step of spill */
        if (l > lvl) lvl = l;
    }
    return lvl;
}
/* the floor casts for this frame: a (clamped) span of sand each, in front of
 * the lantern BEHIND. A bed's is the spill, minus the lantern's own patch. */
static void pk_cast_add(int x0, int x1, int level) {
    if (x1 < x0 || g_pk_cast_n >= PK_CAST_MAX) return;
    g_pk_cast[g_pk_cast_n].x0 = (short)x0; g_pk_cast[g_pk_cast_n].x1 = (short)x1; g_pk_cast[g_pk_cast_n].level = (signed char)level; g_pk_cast_n++;
}
static void pk_casts_pieces(const tank_t *t);          /* (with the pieces, below) */
static void pk_casts(const tank_t *t, int pkx) {
    g_pk_cast_n = 0;
    int px0 = pkx - PUMPKIN_HALF_W, px1 = pkx + PUMPKIN_HALF_W;      /* the lantern's own patch of sand: lit, a 5 px half step either side */
    bool grass_above = g_pk_k >= 0 && tank_decor_z(t, 8) == DECOR_Z_BACK;   /* the grass beds stand in front of a lantern BEHIND */
    for (int b = 0; b < tank_veg_beds(t); b++) {
        if (b == 3 ? !tank_decor_above(t, 0, 8) : !grass_above) continue;    /* (the sword plant: by its own rank) */
        float x0, x1; tank_veg_bed(t, b, &x0, &x1, NULL, NULL);
        int a = (int)x0, z = (int)x1;
        if (z < px0 - 5 || a > px1 + 5) { pk_cast_add(a, z, 2); continue; }
        if (a < px0 - 5) pk_cast_add(a, px0 - 6, 2);
        pk_cast_add(px0 - 5 > a ? px0 - 5 : a, px0 - 1, 1);
        pk_cast_add(px1 + 1, px1 + 5 < z ? px1 + 5 : z, 1);
        if (z > px1 + 5) pk_cast_add(px1 + 6, z, 2);
    }
    pk_casts_pieces(t);
}
static const tank_t *g_guard;                             /* the critters in the FRONT SLOT (before a piece IN FRONT; the crab in a front volcano's cave, on its
                                                             flank): the per-frame overlays - the volcano's lava and embers, the pumpkin's face and flame - keep off
                                                             them (docs/render-stack.md; the matrix found the pumpkin's face painted over a crab walking past; 0.3.9.1,
                                                             Strato on the 1.8: the lava in the cave over the urchin walking past it - the snail and the urchin too) */
enum { GUARD_CRAB = 1, GUARD_SNAIL = 2, GUARD_URCHIN = 4 };
static unsigned g_guard_who;                              /* ... which of them */
static bool slot_covers(int x, int y);
static inline void pk_store(ctx_t *c, int x, int y, uint16_t v, bool final) {
    if (!CTX_IN(c, x, y) || under_bg(x, y)) return;
    if (g_pk_k >= 0 && pc_covered_above(g_pk_k, x, y)) return;   /* under a piece above it (the castle's stone, a coral IN FRONT, ...) */
    if (g_guard && slot_covers(x, y)) return;                    /* a critter in the front slot before the lantern: its face stays under it */
    uint16_t *p = &CTX_PX(c, x, y);
    rec_px(x, y);
    *p = FB_SWAP16(v); TOUCH_PX(c, x, y);
    if (final) {
        static int ky = -1, kx = -1, ka;       /* one vignette alpha per 8 px of a row, computed (the PSRAM LUT costs a cache line a read) */
        if (y != ky || (x >> 3) != kx) { ky = y; kx = x >> 3; ka = vig_alpha((x & ~7) + 4, y); }
        if (ka) px_darken(p, ka);
        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
    } else dirty_px(x, y);
}
/* the glowing part: the carved face's cells and the flame. Every frame,
 * over the baked body (and in the bake, at the steady level). night: the
 * candle's own tones, which ignore the night's dim */
static void draw_pumpkin_glow(ctx_t *c, int cx, float clock, bool night, bool steady, bool final) {
    if (!g_pk_built) pk_build();
    float f = steady ? 0.92f : pk_flicker(clock);
    uint16_t v[PK_N];
    for (int k = PK_EMIT; k < PK_N; k++) {
        uint32_t col = night ? PK_RGB_NIGHT[k] : PK_RGB[k];
        float lv = f * (k == PK_IN3 ? 1.04f : 1.0f);
        if (!night && k == PK_CUT) lv = 1.0f;                     /* by day the lip is mostly daylight */
        v[k] = rgb565(col, lv > 1 ? 1 : lv);
    }
    int x0 = cx - PK_W / 2, y0 = PK_FY - PK_H;
    for (int j = PK_CH - (int)(22 * PK_S); j < PK_CH; j++)       /* the face's rows only: the stem and the crown carry none */
        for (int i = (int)(6 * PK_S); i < PK_CW - (int)(6 * PK_S); i++) {
            uint8_t t = ds()->pk_sprite[j][i];
            if (t < PK_EMIT) continue;
            for (int dy = 0; dy < PK_CELL; dy++)
                for (int dx = 0; dx < PK_CELL; dx++) pk_store(c, x0 + i * PK_CELL + dx, y0 + j * PK_CELL + dy, v[t], final);
        }
    /* the flame: a small teardrop on a stub of candle, leaning with the flicker */
    int fx = x0 + (int)((PK_FLAME_X + 0.33f) * PK_S * PK_CELL), fy = PK_FY - (int)(PK_FLAME_Y * PK_S * PK_CELL);
    uint16_t wax = rgb565(night ? 0xf8e2b0 : 0xe8d4a8, f), core = rgb565(0xfffbe8, 1.0f),
             mid = rgb565(0xffd860, f > 0.95f ? 1.0f : f + 0.05f), tip = rgb565(0xff9a28, f);
    for (int y = fy - 1; y <= fy + 2; y++) for (int x = fx - 2; x <= fx + 1; x++) pk_store(c, x, y, wax, final);
    int h = steady ? 6 : 5 + (f > 0.93f) + (f > 0.86f);
    int lean = steady ? 0 : (int)floorf(1.4f * fast_sin(clock * 6.1f + 8 * TAU) + 0.5f);
    for (int k = 1; k <= h; k++) {
        int y = fy - 1 - k, x = fx - 1 + (k > h / 2 ? lean : 0);
        uint16_t a = k <= 2 ? core : k <= h - 2 ? mid : tip;
        pk_store(c, x, y, a, final);
        if (k <= h - 2) pk_store(c, x + 1, y, k <= 2 ? mid : tip, final);
    }
}
/* the light over the water and the floor, in the bake: the pixels already
 * carry the porthole vignette, so the light is darkened by it too, and it
 * is dithered so its falloff never bands. (The decor painted after it lights
 * itself - PK_LIGHT_PX in its final paint.) */
static void pk_halo(uint16_t *sc) {
    static const uint8_t bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
    uint8_t rgb[128][3];                           /* the light in 8 bits, for the dither */
    for (int i = 0; i < 128; i++) { int r, g, b; pk_rgb8(i, &r, &g, &b); rgb[i][0] = (uint8_t)r; rgb[i][1] = (uint8_t)g; rgb[i][2] = (uint8_t)b; }
    for (int y = g_pk_ly - PK_HALO_RY + 1; y < g_pk_ly + PK_HALO_RY; y++)
        for (int x = g_pk_lx - PK_HALO_R + 1; x < g_pk_lx + PK_HALO_R; x++) {
            if ((unsigned)x >= (unsigned)TANK_W || (unsigned)y >= (unsigned)TANK_H) continue;
            int i = pk_light_i(x, y);
            if (i < 0) continue;
            int inv = 256 - (g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y));
            if (g_pk_shadows) { int sh = pk_shade(x, y); if (sh == 3) continue; if (sh == 2) inv = (int)(inv * PK_SPILL); else if (sh == 1) inv = (int)(inv * PK_HALF); }   /* in a shadow */
            int lr = rgb[i][0] * inv >> 8, lg = rgb[i][1] * inv >> 8, lb = rgb[i][2] * inv >> 8;
            uint16_t *p = &sc[y * TANK_W + x];
            int d = bayer[y & 3][x & 3];
            uint16_t cur = FB_SWAP16(*p);
            int rr = (((cur >> 11) & 31) << 3) + 4 + lr, gg = (((cur >> 5) & 63) << 2) + 2 + lg, bb = ((cur & 31) << 3) + 4 + lb;
            int r5 = (rr + (d >> 1) - 4) >> 3, g6 = (gg + (d >> 2) - 2) >> 2, b5 = (bb + (d >> 1) - 4) >> 3;
            if (r5 > 31) r5 = 31;
            if (g6 > 63) g6 = 63;
            if (b5 > 31) b5 = 31;
            *p = FB_SWAP16((uint16_t)((r5 << 11) | (g6 << 5) | b5));
        }
}
/* every frame, before the vignette sweep: what was painted live in the
 * light's box and is still marked (fish, critters, food, bubbles, crowns)
 * takes the light; the sweep then vignettes it as before */
static void pk_light_dirty(ctx_t *c) {
    int y0 = g_pk_ly - PK_HALO_RY + 1, y1 = g_pk_ly + PK_HALO_RY - 1, x0 = g_pk_lx - PK_HALO_R + 1, x1 = g_pk_lx + PK_HALO_R - 1;
    if (y0 < 0) y0 = 0;
    if (y1 > TANK_H - 1) y1 = TANK_H - 1;
    if (x0 < 0) x0 = 0;
    if (x1 > TANK_W - 1) x1 = TANK_W - 1;
    for (int y = y0; y <= y1; y++) {
        const uint32_t *drow = g_dirty + y * DIRTY_WORDS_PER_ROW;
        for (int w = x0 >> 5; w <= x1 >> 5; w++) {
            uint32_t bits = drow[w];
            while (bits) {
                int x = (w << 5) + __builtin_ctz(bits); bits &= bits - 1;
                if (x < x0 || x > x1) continue;
                uint32_t l = pk_light_at(x, y);
                if (l) { uint16_t *p = &c->fb[y * c->stride + x]; *p = FB_SWAP16(px_lit(FB_SWAP16(*p), l)); }
            }
        }
    }
}
/* the lantern at centre x: the rind, the face (steady in the bake, the
 * candle's flicker live), and with mound the floor stones round its base
 * (lit by the light while it is on) */
static void draw_pumpkin(ctx_t *c, int cx, bool night, float clock, bool steady, bool mound, bool final) {
    if (!g_pk_built) pk_build();
    pk_tint_fill(c->dim);
    int x0 = cx - PK_W / 2, y0 = PK_FY - PK_H;
    for (int j = 0; j < PK_CH; j++)
        for (int i = 0; i < PK_CW; i++) {
            uint8_t t = ds()->pk_sprite[j][i];
            if (!t || t >= PK_EMIT) continue;
            for (int dy = 0; dy < PK_CELL; dy++) {
                uint16_t v = ds()->pk_row[t][j * PK_CELL + dy];
                for (int dx = 0; dx < PK_CELL; dx++) pk_store(c, x0 + i * PK_CELL + dx, y0 + j * PK_CELL + dy, v, final);
            }
        }
    draw_pumpkin_glow(c, cx, clock, night, steady, final);
    if (mound) draw_floor_mound(c, cx, PK_W / 2 - 4, 5, g_dz_pk, final);
}

/* ---- the bubble volcano, procedural (0.3.9; DonB's #ideas thread of
 * 2026-10-22, four keepers' 🦐: "a bubble volcano (think Finding Nemo) that
 * could maybe have glowing red bubbles visible when tank lights are low";
 * the reference is working-assets/volcano/Volcano Sprite.png: a cone of
 * stacked purple-gray boulders, a lava pool in the crater, orange cracks
 * down the flanks, a cave mouth at the foot lit from inside, moss on the
 * lower rocks, lit bubbles climbing from the crater). Built ONCE as a tone
 * sprite on a 2 px cell grid like the lantern's: the cone's silhouette from
 * a profile with a jagged edge, the boulders from the nearest of a jittered
 * grid of points (a crevice where two are as near), lit from the upper left
 * like every piece, the cracks as capsule chains down from the pool, the
 * moss by the castle's rule. The ROCK is baked, hazed and dimmed with the
 * water per row; the LAVA (the pool, the cracks, the cave's floor) is
 * emissive - its own tones, no dim - and is redrawn every frame with a slow
 * breathing flicker; the BUBBLES rise live from the pool, lit orbs that cool
 * from yellow to red and fade out over ~100 px - by night, the tank's embers.
 * About the castle's width (184 px) and taller (~200 px over the floor). */
#define VO_FY   (TANK_BOT - 16 + g_dz_vo)               /* the sprite's bottom row: the back line (the castle's), shifted to its depth's (0.3.9) */
#define VO_MID  46.0f                                   /* the cone's axis, in cells */
#define VO_BASE 4.0f                                    /* the cone's foot, cell rows up from the sprite's bottom */
#define VO_TOP  100.0f                                  /* the far rim's top */
#define VO_POOL_Y 93.0f                                 /* the lava pool's centre row */
#define VO_POOL_RX 9.5f
#define VO_POOL_RY 3.2f
#define VO_EMIT VO_CAVE_LIT
#define VO_NB   7                                       /* bubbles in the air at once */
#define VO_RISE 104                                     /* how far a bubble climbs before it fades, px */
static const uint32_t VO_RGB[VO_N] = {
    0, 0x74667a, 0x52475a, 0x362e3d, 0x1c1620, 0x5a2e28, 0x7aa33c, 0x527f2e, 0x1e0c0c, 0x48180e,
    0x9a3012, 0xc04012, 0xf26a1c, 0xffa826, 0xffe070 };
static const uint32_t VO_RGB_NIGHT[VO_N] = {                    /* the lava with the light off: brighter, it has the tank to itself */
    [VO_CAVE_LIT] = 0xb8380e, [VO_LAVA_DIM] = 0xd84810, [VO_LAVA] = 0xff7a1e, [VO_LAVA_HOT] = 0xffbc38, [VO_LAVA_CORE] = 0xfff2a8 };
static bool  g_vo_built;
static float g_vo_dim = -1;
static int   g_vo_dzk = 99;
static int   g_vo_cells, g_vo_glow_n;
static int   g_vo_style;                                        /* 0 stacked boulders (Strato's pick 10-23), 1 smooth rock (the sim's PT_VOLCANO_SMOOTH, kept for a look) */
void render_set_volcano_style(int style) { if (style != g_vo_style) { g_vo_style = style; g_vo_built = false; } }
/* smooth value noise on an 8-cell grid, -1..1 (the smooth rock's shading) */
static float vo_noise(float px, float py, int seed) {
    float gx = px / 8.0f, gy = py / 8.0f;
    int i0 = (int)floorf(gx), j0 = (int)floorf(gy);
    float fx = gx - i0, fy = gy - j0;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
#define VO_NV(i, j) (((chash((i) + (seed), (j) - (seed)) & 1023) / 511.5f) - 1.0f)
    return (VO_NV(i0, j0) * (1 - fx) + VO_NV(i0 + 1, j0) * fx) * (1 - fy) + (VO_NV(i0, j0 + 1) * (1 - fx) + VO_NV(i0 + 1, j0 + 1) * fx) * fy;
#undef VO_NV
}
/* the cone's half width at cell row y, per side (s = -1 left, +1 right): a
 * cone that leans a little, a shoulder on the left, lumps and a rough edge */
static float vo_half(float y, int s) {
    float u = (y - VO_BASE) / (VO_TOP - VO_BASE);
    if (u < 0) u = 0;
    if (u > 1) u = 1;
    float h = 11.0f + 35.0f * powf(1 - u, 0.85f);
    float g = s < 0 ? (u - 0.32f) / 0.16f : (u - 0.58f) / 0.12f;
    h += (s < 0 ? 5.0f : 2.5f) * expf(-g * g);
    h += 1.2f * fast_sin(y * 0.47f + (s < 0 ? 0.3f : 2.1f) + 8 * TAU) + 0.8f * fast_sin(y * 0.19f + (s < 0 ? 1.7f : 0.2f) + 8 * TAU);
    h += (float)((int)(chash((int)y >> 2, s + 5) % 3) - 1) * 0.6f;
    return h;
}
/* the boulders: the nearest two of a jittered grid of points (Worley), the
 * nearest's centre and hash for its lighting and its own shade */
#define VO_GX 7.5f
#define VO_GY 6.0f
static void vo_boulder(float px, float py, float *d1, float *d2, float *fx, float *fy, uint32_t *fh) {
    int gi = (int)floorf(px / VO_GX), gj = (int)floorf(py / VO_GY);
    *d1 = *d2 = 1e9f; *fx = *fy = 0; *fh = 0;
    for (int dj = -1; dj <= 1; dj++)
        for (int di = -1; di <= 1; di++) {
            int ci = gi + di, cj = gj + dj;
            uint32_t h = chash(ci + 31, cj + 17);
            float cx = (ci + 0.5f + (((h >> 3) & 255) / 255.0f - 0.5f) * 0.8f) * VO_GX;
            float cy = (cj + 0.5f + (((h >> 11) & 255) / 255.0f - 0.5f) * 0.8f) * VO_GY;
            float ex = px - cx, ey = (py - cy) * 1.2f, d = sqrtf(ex * ex + ey * ey);
            if (d < *d1) { *d2 = *d1; *d1 = d; *fx = cx; *fy = cy; *fh = h; }
            else if (d < *d2) *d2 = d;
        }
}
/* signed distance to a chain of capsules (dx from the axis, y pairs), its
 * width tapering w0 -> w1 along the chain */
static float vo_chain_sd(float px, float py, const int8_t *p, int n, float w0, float w1) {
    float best = 1e9f;
    for (int i = 0; i + 1 < n; i++) {
        float ax = VO_MID + p[2 * i], ay = p[2 * i + 1], bx = VO_MID + p[2 * i + 2], by = p[2 * i + 3];
        float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
        float u = ((px - ax) * dx + (py - ay) * dy) / len2;
        if (u < 0) u = 0;
        if (u > 1) u = 1;
        float ex = px - (ax + dx * u), ey = py - (ay + dy * u);
        float w = w0 + (w1 - w0) * ((i + u) / (n - 1));
        float d = sqrtf(ex * ex + ey * ey) - w;
        if (d < best) best = d;
    }
    return best;
}
static void vo_build(void) {
    uint8_t (*sp)[VO_CW] = ds()->vo_sprite;
    static const int8_t CA[] = { -5, 90, -8, 80, -6, 70, -10, 58, -8, 47, -11, 36 };    /* the cracks, from the pool's lip down */
    static const int8_t CB[] = { 5, 90, 8, 80, 6, 70, 10, 58, 9, 46, 13, 34 };
    static const int8_t CC[] = { -1, 89, 1, 78, -2, 67, 0, 59 };
    static const int8_t CD[] = { 9, 88, 14, 76, 12, 64, 16, 52 };
    static const struct { int8_t x, y, rx, ry; } BO[6] = {                           /* the boulders at the foot */
        { -40, 6, 9, 5 }, { -29, 4, 7, 4 }, { 38, 6, 10, 5 }, { 27, 3, 6, 4 }, { -47, 3, 6, 3 }, { 47, 3, 6, 3 } };
    for (int j = 0; j < VO_CH; j++)
        for (int i = 0; i < VO_CW; i++) {
            float px = i + 0.5f, py = (VO_CH - 1 - j) + 0.5f;
            uint8_t t = VO_NONE;
            bool rock = false; float nx = 0;
            if (py >= VO_BASE) {                                                   /* the cone */
                int s = px < VO_MID ? -1 : 1;
                float half = vo_half(py, s);
                if (py > 94) half -= (py - 94) * 0.9f;                             /* the far rim narrows behind the pool */
                if (fabsf(px - VO_MID) <= half && py <= VO_TOP + 0.5f) { rock = true; nx = (px - VO_MID) / half; }
            }
            if (!rock && py < 8) {                                                 /* the rubble skirt */
                float w = ell_half((py - 3.0f) / 4.8f);
                if (w > 0 && fabsf(px - VO_MID) <= 49 * w) { rock = true; nx = (px - VO_MID) / 49.0f; }
            }
            if (rock) {
                float u = (py - VO_BASE) / (VO_TOP - VO_BASE);
                if (u < 0) u = 0;
                if (g_vo_style == 0) {                                             /* stacked boulders */
                    float d1, d2, fx, fy; uint32_t fh;
                    vo_boulder(px, py, &d1, &d2, &fx, &fy, &fh);
                    float lx = (px - fx) / 4.0f, ly = (py - fy) / 4.0f;
                    float lit = -0.62f * nx + 0.5f * (-lx * 0.45f + ly * 0.85f) + (((fh >> 19) & 15) / 15.0f - 0.5f) * 0.4f;
                    t = lit > 0.42f ? VO_ROCK_L : lit > -0.18f ? VO_ROCK : VO_ROCK_D;
                    if (d2 - d1 < 0.9f) t = VO_ROCK_DD;                            /* the crevice between two boulders */
                    else if ((chash(i + 300, j + 300) & 15) == 0) t = t == VO_ROCK_L ? VO_ROCK : t == VO_ROCK ? VO_ROCK_D : VO_ROCK_DD;   /* a mottle */
                } else {                                                           /* smooth rock: the cone's shading, soft lumps, faint ledges */
                    float nz = 0.6f * vo_noise(px, py, 3) + 0.3f * vo_noise(px * 2, py * 2, 7);
                    float lit = -0.62f * nx + 0.12f + 0.35f * nz + ((chash(i + 900, j + 900) & 7) / 7.0f - 0.5f) * 0.16f;   /* a soft edge between the tones */
                    t = lit > 0.40f ? VO_ROCK_L : lit > -0.12f ? VO_ROCK : lit > -0.55f ? VO_ROCK_D : VO_ROCK_DD;
                    float st = py * 0.75f + 2.0f * vo_noise(px * 0.7f, py * 0.7f, 11);
                    if (st - floorf(st / 7.0f) * 7.0f < 0.55f) t = t == VO_ROCK_L ? VO_ROCK : t == VO_ROCK ? VO_ROCK_D : VO_ROCK_DD;   /* a ledge's underside */
                }
                if (u < 0.5f && t != VO_ROCK_DD) {                                 /* moss creeps up from the foot */
                    int p = (int)(40 * (1 - u / 0.5f));
                    if ((int)(chash(i >> 1, j >> 1) % 100) < p && (chash(i + 7, j + 3) & 3) != 0)
                        t = (chash(i >> 1, (j >> 1) + 99) & 1) ? VO_MOSS_A : VO_MOSS_B;
                }
            }
            for (int b = 0; b < 6; b++) {                                          /* the boulders over the foot */
                float ex = (px - (VO_MID + BO[b].x)) / BO[b].rx, ey = (py - BO[b].y) / BO[b].ry, e = ex * ex + ey * ey;
                if (e > 1) continue;
                float lit = -ex * 0.5f + ey * 0.8f;
                t = e > 0.72f && (ex > 0.2f || ey < -0.2f) ? VO_ROCK_DD : lit > 0.35f ? VO_ROCK_L : lit > -0.2f ? VO_ROCK : VO_ROCK_D;
                if (ey > 0.3f && (chash(i + 50, j + 50) % 5) == 0) t = VO_MOSS_A;
                rock = true;
            }
            if (rock) {                                                            /* the cave mouth: a vault at the foot, lit from its floor */
                float spring = VO_BASE + 10, R = 13.0f;
                float ch = py <= spring ? R : R * ell_half((py - spring) / R);
                float jag = (chash((int)py, 9) & 1) ? 0.6f : 0;
                if (py >= VO_BASE + 2 && ch >= 0.5f && fabsf(px - VO_MID) <= ch + jag) {   /* its floor at the sand line, not under it */
                    float dy = py - VO_BASE - 2, in = ch - fabsf(px - VO_MID);   /* the floor glows, feathered up and toward the jambs */
                    int g = (int)(chash(i + 11, j + 5) % 100);
                    t = dy < 1.5f && in > 1.5f ? VO_CAVE_LIT : dy < 3.5f && in > 2.5f && g < 55 - (int)(dy * 10) ? VO_CAVE_LIT
                      : dy < 6.5f && g < 80 - (int)(dy * 9) ? VO_CAVE_MID : VO_CAVE;
                }
            }
            if (rock && t < VO_CAVE) {                                             /* the pool and the cracks, over the rock */
                float sd = vo_chain_sd(px, py, CA, 6, 0.8f, 0.45f), s2;
                if ((s2 = vo_chain_sd(px, py, CB, 6, 0.8f, 0.45f)) < sd) sd = s2;
                if ((s2 = vo_chain_sd(px, py, CC, 4, 0.75f, 0.45f)) < sd) sd = s2;
                if ((s2 = vo_chain_sd(px, py, CD, 4, 0.75f, 0.45f)) < sd) sd = s2;
                float ex = (px - VO_MID) / VO_POOL_RX, ey = (py - VO_POOL_Y) / VO_POOL_RY, e = ex * ex + ey * ey;
                if (e <= 1) t = ey > 0.3f ? VO_LAVA : e < 0.28f ? VO_LAVA_CORE : e < 0.7f || ey < -0.45f ? VO_LAVA_HOT : VO_LAVA;   /* the far side darker, the near lip bright */
                else if (sd < 0) t = py > 82 ? VO_LAVA_HOT : py > 58 ? VO_LAVA : VO_LAVA_DIM;
                else if (sd < 1.0f || e < 1.45f) t = VO_SCORCH;                    /* the rock at a crack's edge, scorched */
            }
            sp[j][i] = t;
        }
    /* the lip over the cave catches the light; the counts and the lava's cell list */
    g_vo_cells = g_vo_glow_n = 0;
    for (int j = 0; j < VO_CH; j++)
        for (int i = 0; i < VO_CW; i++) {
            uint8_t t = sp[j][i];
            if (t >= VO_ROCK_L && t <= VO_ROCK_D && j + 1 < VO_CH && sp[j + 1][i] >= VO_CAVE && sp[j + 1][i] <= VO_CAVE_LIT) sp[j][i] = t = VO_ROCK_L;
            g_vo_cells += t != VO_NONE;
            if (t >= VO_EMIT && g_vo_glow_n < VO_GLOW_MAX) ds()->vo_glow[g_vo_glow_n++] = (uint16_t)(j * VO_CW + i);
        }
    g_vo_built = true;
}
int render_volcano_px(int *glow) {                               /* its pixels, and how many are redrawn a frame (the lava; the bubbles apart) */
    if (!g_vo_built) vo_build();
    if (glow) *glow = g_vo_glow_n * VO_CELL * VO_CELL;
    return g_vo_cells * VO_CELL * VO_CELL;
}
/* the rock's rows: hazed with the water like the castle's stone and dimmed;
 * by night the rows near the crater take a warm cast from the lava */
static void vo_tint_fill(float dim) {
    if (g_vo_dim == dim && g_vo_dzk == g_dz_vo) return;
    bool night = dim < 1.0f;
    float haze = decor_haze(0.88f, g_dz_vo);
    for (int r = 0; r < VO_H; r++) {
        int y = VO_FY - VO_H + r;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        float warm = night ? 0.22f * (1 - r / 40.0f) : 0;
        for (int k = 1; k < VO_EMIT; k++) {
            uint32_t col = VO_RGB[k];
            if (warm > 0) col = mix(col, 0xff5a10, warm);
            ds()->vo_row[k][r] = rgb565(mix(w, col, haze), dim);
        }
    }
    g_vo_dim = dim; g_vo_dzk = g_dz_vo;
}
static inline void vo_store(ctx_t *c, int x, int y, uint16_t v, bool final) {
    if (!CTX_IN(c, x, y) || under_bg(x, y)) return;
    if (g_clip_k >= 0 && pc_covered_above(g_clip_k, x, y)) return;   /* under a piece above it */
    if (g_guard && slot_covers(x, y)) return;                        /* the crab in its cave, a critter passing it: the embers stay under them (Strato, the pendant; the 1.8) */
    rec_px(x, y);
    uint16_t *p = &CTX_PX(c, x, y);
    *p = FB_SWAP16(v); TOUCH_PX(c, x, y);
    if (final) {
        PK_LIGHT_PX(p, x, y);
        int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
        if (a) px_darken(p, a);
        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
    } else dirty_px(x, y);
}
/* the lava breathes: a level 0.84..1.0 from two slow beats and a faster shimmer */
static float vo_flicker(float clock) {
    return 0.93f + 0.04f * fast_sin(clock * 2.7f + 8 * TAU) + 0.025f * fast_sin(clock * 0.9f + 1.1f + 8 * TAU) + 0.015f * fast_sin(clock * 9.3f + 0.4f + 8 * TAU);
}
/* the lava this frame: the pool, the cracks and the cave's floor, over the
 * baked rock (and in the bake, at the steady level) */
static void draw_volcano_glow(ctx_t *c, int cx, float clock, bool night, bool steady, bool final) {
    if (!g_vo_built) vo_build();
    float f = steady ? 0.95f : vo_flicker(clock);
    uint16_t v[VO_N];
    for (int k = VO_EMIT; k < VO_N; k++) {
        float lv = f * (k == VO_LAVA_CORE ? 1.04f : k == VO_CAVE_LIT ? 0.92f : 1.0f);
        v[k] = rgb565(night ? VO_RGB_NIGHT[k] : VO_RGB[k], lv > 1 ? 1 : lv);
    }
    int x0 = cx - VO_W / 2, y0 = VO_FY - VO_H;
    const uint16_t *g = ds()->vo_glow;
    for (int n = 0; n < g_vo_glow_n; n++) {
        int j = g[n] / VO_CW, i = g[n] % VO_CW;
        uint8_t t = ds()->vo_sprite[j][i];
        for (int dy = 0; dy < VO_CELL; dy++)
            for (int dx = 0; dx < VO_CELL; dx++) vo_store(c, x0 + i * VO_CELL + dx, y0 + j * VO_CELL + dy, v[t], final);
    }
}
/* the volcano at centre x: the rock, then the lava at its steady level */
static void draw_volcano(ctx_t *c, int cx, bool night, bool final) {
    if (!g_vo_built) vo_build();
    vo_tint_fill(c->dim);
    int x0 = cx - VO_W / 2, y0 = VO_FY - VO_H;
    for (int j = 0; j < VO_CH; j++)
        for (int i = 0; i < VO_CW; i++) {
            uint8_t t = ds()->vo_sprite[j][i];
            if (!t || t >= VO_EMIT) continue;
            for (int dy = 0; dy < VO_CELL; dy++) {
                uint16_t v = ds()->vo_row[t][j * VO_CELL + dy];
                for (int dx = 0; dx < VO_CELL; dx++) vo_store(c, x0 + i * VO_CELL + dx, y0 + j * VO_CELL + dy, v, final);
            }
        }
    draw_volcano_glow(c, cx, 0, night, true, final);
}
/* the bubbles: VO_NB in the air, each on its own period and phase, rising
 * from the pool, wobbling, swelling as they climb, cooling from yellow to a
 * dull red and shrinking away at the top. By night their own light (no dim)
 * with a soft ring. Returns their box for the vignette sweep. */
static void draw_volcano_bubbles(ctx_t *c, int cx, float clock, bool night, int *bx0, int *by0, int *bx1, int *by1) {
    ctx_t ce = *c;
    if (night) ce.dim = 1.0f;
    float py = VO_FY - VO_H + (VO_CH - VO_POOL_Y) * VO_CELL;
    for (int k = 0; k < VO_NB; k++) {
        float P = 2.9f + 0.41f * k, ph = clock / P + k * 0.618f;
        ph -= floorf(ph);
        float y = py - ph * VO_RISE;
        float x = cx + (k % 3 - 1) * 5.0f * ph + 3.5f * ph * fast_sin(clock * 1.3f + k * 2.1f + 8 * TAU);
        float r = 1.7f + 1.5f * ph;
        if (ph > 0.84f) r *= (1 - ph) / 0.16f;
        if (r < 0.6f) continue;
        uint32_t col = ph < 0.35f ? mix(0xffd060, 0xf8862a, ph / 0.35f) : ph < 0.7f ? mix(0xf8862a, 0xe04818, (ph - 0.35f) / 0.35f) : mix(0xe04818, 0xa02810, (ph - 0.7f) / 0.3f);
        if (night) fill_ellipse(&ce, x, y, r + 1.2f, r + 1.2f, col, 50);
        fill_ellipse(&ce, x, y, r, r, col, night ? 255 : 230);
        px_blend(&ce, (int)(x - r * 0.4f), (int)(y - r * 0.4f), 0xfff2c0, 150);
    }
    *bx0 = cx - 18; *bx1 = cx + 18; *by0 = (int)py - VO_RISE - 6; *by1 = (int)py + 6;
}
/* the light it throws with the tank's light off, in the bake: the crater's
 * on the water above and round it, the cave mouth's on the sand before it,
 * dithered like the lantern's halo (the fish passing through it are not lit:
 * the preview) */
static void vo_halo_src(uint16_t *sc, float lx, float ly, float rx, float ry_up, float ry_dn, int lr, int lg, int lb) {
    static const uint8_t bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
    int y0 = (int)(ly - ry_up), y1 = (int)(ly + ry_dn), x0 = (int)(lx - rx), x1 = (int)(lx + rx);
    for (int y = y0; y <= y1; y++) {
        if ((unsigned)y >= (unsigned)TANK_H) continue;
        float dy = (y - ly) / (y < ly ? ry_up : ry_dn);
        for (int x = x0; x <= x1; x++) {
            if ((unsigned)x >= (unsigned)TANK_W) continue;
            float dx = (x - lx) / rx, d = sqrtf(dx * dx + dy * dy);
            if (d >= 1) continue;
            float f = (1 - d) * (1 - d) * sqrtf(1 - d);
            int inv = 256 - (g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y));
            int ar = (int)(lr * f) * inv >> 8, ag = (int)(lg * f) * inv >> 8, ab = (int)(lb * f) * inv >> 8;
            uint16_t *p = &sc[y * TANK_W + x];
            int dd = bayer[y & 3][x & 3];
            uint16_t cur = FB_SWAP16(*p);
            int rr = (((cur >> 11) & 31) << 3) + 4 + ar, gg = (((cur >> 5) & 63) << 2) + 2 + ag, bb = ((cur & 31) << 3) + 4 + ab;
            int r5 = (rr + (dd >> 1) - 4) >> 3, g6 = (gg + (dd >> 2) - 2) >> 2, b5 = (bb + (dd >> 1) - 4) >> 3;
            if (r5 > 31) r5 = 31;
            if (g6 > 63) g6 = 63;
            if (b5 > 31) b5 = 31;
            *p = FB_SWAP16((uint16_t)((r5 << 11) | (g6 << 5) | b5));
        }
    }
}
static void vo_halo(uint16_t *sc, int cx) {
    float py = VO_FY - VO_H + (VO_CH - VO_POOL_Y) * VO_CELL, cy = VO_FY - VO_H + (VO_CH - VO_BASE - 3) * VO_CELL;
    vo_halo_src(sc, cx, py, 130, 120, 70, 92, 34, 6);
    vo_halo_src(sc, cx, cy, 70, 30, 24, 76, 22, 4);
}

/* ---- the snail, procedural (2026-09-16; Strato: "explore doing the same
 * for the snail so the aesthetic matches") ----
 * Drawn the castle's way in place of the two sprites: a little geometry with
 * a per-pixel tone rule, the castle's position hash for the foot's mottle,
 * golden light from the upper left (the lower right of the shell falls into
 * the dark tones, the rim shades, a wet gleam sits high on the left), mixed
 * with the water of its row like the fronds - a touch stronger on the pane
 * than on the floor, so the glass snail reads closer - and no outline. And
 * it moves (Strato: "the fish animations are so nice, maybe you can add a
 * subtle animation"): PEDAL WAVES run along the foot, head-ward on the glass
 * and back along the belly upright, the eye stalks sway against each other,
 * and the shell heaves a pixel with the wave.
 * UPRIGHT faces +x, the origin at the sole under the shell (snail_y +
 * SNAIL_SOLE_DY), mirrored to the heading; ~34 x 17 px. ON THE GLASS the
 * head points +y from the foot's centre, turned to the heading like the
 * sprite was; ~22 x 25 px. The tone rule runs over ~750 px per pose. */
enum { SN_S0, SN_S1, SN_S2, SN_S3, SN_S4, SN_F0, SN_F1, SN_F2, SN_F3, SN_TIP, SN_EYE, SN_NONE };
static const uint32_t SNAIL_RGB[SN_NONE] = {
    0xf8c868, 0xe6953c, 0xc4641e, 0x8c3d16, 0x4a2010,      /* the shell: lit .. the groove */
    0xf2ebe4, 0xd8cec6, 0xb3a7a6, 0x847a80,                /* the foot: lit .. its edge */
    0x1a1c34, 0x0a0c18 };                                  /* stalk tips; the eye and the mouth */
#define SNAIL_SOLE_DY   6.0f                               /* the sole below snail_y (the sprite's centre) */
#define SNAIL_TINT_FLOOR 0.84f
#define SNAIL_TINT_GLASS 0.90f
static inline int sn_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
/* within w of the segment (x0,y0)-(x1,y1); *u = 0..1 along it */
static inline bool sn_seg(float lx, float ly, float x0, float y0, float x1, float y1, float w, float *u) {
    float vx = x1 - x0, vy = y1 - y0, l2 = vx * vx + vy * vy;
    float t = ((lx - x0) * vx + (ly - y0) * vy) / l2;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float ex = lx - (x0 + t * vx), ey = ly - (y0 + t * vy);
    *u = t;
    return ex * ex + ey * ey < w * w;
}
/* the shell's tone from its lighting terms: band = the whorl's lit / mid /
 * groove, lit = -1..1 (upper left .. lower right), rim = on the edge */
static inline int sn_shell_tone(int band, float lit, bool rim) {
    int tone = band;
    if (lit > 0.35f) tone--; else if (lit < -0.15f) tone++;
    if (lit < -0.55f) tone++;
    if (rim) tone = lit < 0.25f ? (tone > 3 ? tone : 3) : (tone < 1 ? tone : 1);
    return tone;
}
/* UPRIGHT: the tone at local (lx, ly), or SN_NONE. top = the foot's ridge at
 * this column (computed once per column by the caller) */
typedef struct { float heave, sw1, sw2; } sn_sway_t;       /* this frame's heave and stalk sways: once, not per pixel (2026-10-03) */
static int snail_upright_tone(int lx, int ly, float top, float clock, const sn_sway_t *sw) {
    float heave = sw->heave;
    float sw1 = sw->sw1, sw2 = sw->sw2;
    float u;
    /* the eye stalks, swaying against each other */
    if (sn_seg(lx, ly, 12.5f, -7.5f, 17.5f + sw1, -13.5f, 0.7f, &u)) return u > 0.78f ? SN_TIP : SN_F1;
    if (sn_seg(lx, ly, 13.5f, -6.5f, 18.5f + sw2, -9.5f, 0.7f, &u)) return u > 0.78f ? SN_TIP : SN_F1;
    /* the shell: a disc, the whorl a spiral about an apex left of centre */
    const float cx = -3.5f, cy = -9.5f + heave, R = 8.6f;
    float dx = lx - cx, dy = ly - cy;
    float r = sqrtf(dx * dx + dy * dy * 1.0816f);
    if (r <= R) {
        float ax = lx - (cx - 1.5f), ay = ly - (cy + 0.5f);
        float ar = sqrtf(ax * ax + ay * ay), th = atan2f(ay, ax);
        const float pitch = 2.25f;                                 /* ~4 turns: Strato asked for one more groove */
        float w = fmodf(ar - th / TAU * pitch, pitch); if (w < 0) w += pitch; w /= pitch;
        int tone = sn_shell_tone(w < 0.5f ? 1 : w < 0.74f ? 2 : 3, (-dx * 0.55f - dy * 0.83f) / R, r > R - 1.3f);
        if (ar < 2.2f) tone = 3;                                   /* the apex whorl is tight and dark */
        { float gx = (dx + 3.4f) * 0.8f, gy = dy + 4.6f; if (gx * gx + gy * gy < 4.0f) tone = 0; }   /* a wet gleam */
        return SN_S0 + sn_clamp(tone, 0, 4);
    }
    /* the foot: a low lens under the shell, the head rising at the front */
    if (lx >= -15 && lx <= 13 && ly >= top && ly <= 0) {
        int tone = 1;
        if (ly < top + 1.4f) tone = 0;                             /* the lit ridge */
        if (ly > -0.9f) tone = 2;                                  /* the belly line */
        if (r < R + 1.8f && dy > 0 && tone < 3) tone++;            /* the shell's shadow */
        if (ly > -2.2f && tone <= 1 && fast_sin(lx * 1.1f + clock * 3.0f + TAU) > 0.7f) tone = 0;   /* a pedal wave runs back */
        if ((chash(lx, ly) & 7) == 0) tone = sn_clamp(tone + 1, 0, 3);
        return SN_F0 + tone;
    }
    /* the head */
    { float hx = (lx - 11.5f) * 1.05f, hy = ly + 5.2f;
      if (hx * hx + hy * hy <= 9.0f) {
          float ex = lx - 12.6f, ey = ly + 5.6f;
          if (ex * ex + ey * ey < 0.64f) return SN_EYE;
          return (-(lx - 11.5f) * 0.5f - hy) > 0.9f ? SN_F0 : SN_F1;
      } }
    return SN_NONE;
}
/* ON THE GLASS: local (lx, ly) is turned to the heading; (sx, sy) is the
 * screen offset, which the light and the shadow side follow (the light
 * stays upper-left however it crawls) */
/* (the body under the stalks: the foot, whose pedal waves move with the
 * clock - *foot says so - or the shell, which depends on the place alone) */
static int snail_glass_body(float lx, float ly, float sx, float sy, float clock, bool *foot) {
    *foot = false;
    float lit = (-sx * 0.55f - sy * 0.83f) / 11.0f;
    /* the foot: an egg, broad under the shell, narrowing to the head */
    float ey = ly / 11.0f;
    if (ey >= -1 && ey <= 1) {
        float rx = 6.6f * sqrtf(1 - ey * ey) * (1 - 0.18f * ey);
        float alx = lx < 0 ? -lx : lx;
        if (rx > 0.3f && alx <= rx) {
            *foot = true;
            int tone = 1;
            if (fast_sin(ly * 0.75f - clock * 2.6f + 64 * TAU) > 0.6f) tone = 0;   /* pedal waves crawl head-ward */
            float edge = alx / rx;
            if (edge > 0.8f) tone = 2;                             /* the foot's edge */
            else if (edge > 0.62f && lit < 0 && tone < 1) tone = 1; /* the shadow side falls off */
            if (alx < 1.2f && ly > 2 && tone < 2) tone = 2;        /* the pedal groove */
            if (ly < -5 && edge < 0.5f && tone > 1) tone = 1;
            if ((chash((int)floorf(lx), (int)floorf(ly)) & 7) == 0) tone = sn_clamp(tone + 1, 0, 3);
            if (alx < 1.5f && ly >= 7.4f && ly <= 8.8f) return SN_EYE;   /* the mouth */
            return SN_F0 + tone;
        }
    }
    /* the shell behind: a crescent of whorl lobes round the top and sides */
    float qx = lx / 11.0f, qy = (ly + 2.5f) / 10.6f;
    float sr = sqrtf(qx * qx + qy * qy);
    if (sr <= 1 && ly < 6.5f) {
        float lobe = fast_sin(atan2f(ly + 2.5f, lx) * 5.0f - 0.3f + 8 * TAU);
        int tone = sn_shell_tone(lobe > 0.15f ? 1 : 2, lit, sr > 0.88f);
        if (sr < 0.78f) tone++;                                    /* shadow where the foot meets it */
        { float gx = sx + 5.0f, gy = sy + 7.8f; if (gx * gx + gy * gy < 2.9f) tone = 0; }   /* gleam */
        return SN_S0 + sn_clamp(tone, 0, 4);
    }
    return SN_NONE;
}
/* stalk_y[0] / [1]: the left and right stalk's tip height this frame */
static int snail_glass_tone(float lx, float ly, float sx, float sy, float clock, const float *stalk_y) {
    float u;
    for (int sgn = -1; sgn <= 1; sgn += 2)                        /* the stalks: stubs by the head */
        if (sn_seg(lx, ly, sgn * 2.2f, 10.0f, sgn * 3.6f, stalk_y[sgn > 0], 0.6f, &u))
            return u > 0.7f ? SN_TIP : SN_F2;
    bool foot;
    return snail_glass_body(lx, ly, sx, sy, clock, &foot);
}
/* a row of snail pixels: the water of the row and each tone's colour over it
 * are worked out once per row and tone, not per pixel */
typedef struct { int y; uint32_t w; uint16_t have; src_t s[SN_NONE]; } sn_row_t;
static inline void snail_put(ctx_t *c, int x, int y, int tone, float tint, sn_row_t *r) {
    if (tone == SN_NONE || !CTX_IN(c, x, y)) return;
    if (r->y != y) { r->y = y; r->have = 0; r->w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y); }
    if (!(r->have >> tone & 1)) { r->s[tone] = src_color(mix(r->w, SNAIL_RGB[tone], tint), c->dim); r->have |= (uint16_t)(1u << tone); }
    px_blend_s(c, x, y, &r->s[tone], 255);
}
/* ON THE GLASS the shell and the empty corners depend only on the heading:
 * kept per pixel of the 35 x 35 box until the snail turns (the decor
 * scratch, PSRAM). SNG_DYN = the foot, or within reach of a stalk: asked
 * every frame. 2026-10-03: on the glass it cost the 1.8 ~4.5 ms a frame - a
 * sqrtf and an atan2f for every shell pixel, two divides and two sines for
 * every pixel of the box. */
#define SNG_N    35
#define SNG_NONE 0xff
#define SNG_DYN  0xfe
static float g_sng_heading = 1e9f;                     /* the heading sn_glass was worked out for */
/* UPRIGHT on the floor - a side view, mirrored to face the way it walks,
 * drawn in the scene with the fish (vignetted, behind the front fronds) -
 * or flat ON THE GLASS, its underside to the viewer, drawn after the algae
 * like the film itself (it is on the pane). */
static void draw_snail(ctx_t *c, const tank_t *t, bool upright_pass) {
    if (!(t->sd_unlocks & SD_ITEM_SNAIL) || t->snail_x < 0) return;
    bool upright = tank_snail_upright(t);
    if (upright != upright_pass) return;
    int ox = (int)t->snail_x, oy = (int)(t->snail_y + SNAIL_SOLE_DY);
    sn_row_t row = { .y = -(1 << 20) };
    if (upright) {
        bool flip = cosf(t->snail_heading) < 0;
        sn_sway_t sw = { 0.6f * fast_sin(t->clock * 2.4f), fast_sin(t->clock * 1.7f), fast_sin(t->clock * 1.7f + 2.2f) };
        float top[37];                                             /* the foot's ridge per column, then row by row (one water colour a row) */
        for (int lx = -16; lx <= 20; lx++) {
            float xf = (lx + 15) / 28.5f;
            top[lx + 16] = -(2.4f + 3.2f * powf(sinf(xf < 0 ? 0 : xf > 1 ? 1 : xf * 3.14159f), 0.6f));
            if (lx > 8) { float h = -(4.8f + (lx - 8) * 0.55f); if (h < top[lx + 16]) top[lx + 16] = h; }
        }
        static float s_t = -1e9f;                                  /* its tones a few times a second, not a frame (2026-10-23) */
        uint8_t *tn = ds()->sn_tones;
        if (t->clock - s_t >= 0.08f || t->clock < s_t) {
            for (int ly = -18; ly <= 0; ly++)
                for (int lx = -16; lx <= 20; lx++) tn[(ly + 18) * 37 + lx + 16] = (uint8_t)snail_upright_tone(lx, ly, top[lx + 16], t->clock, &sw);
            s_t = t->clock;
        }
        for (int ly = -18; ly <= 0; ly++)
            for (int lx = -16; lx <= 20; lx++)
                snail_put(c, flip ? ox - lx : ox + lx, oy + ly, tn[(ly + 18) * 37 + lx + 16], SNAIL_TINT_FLOOR, &row);
    } else {
        float a = t->snail_heading - 1.5708f;                      /* the head points +y locally */
        float ca = cosf(a), sa = sinf(a);
        int cx = (int)t->snail_x, cy = (int)t->snail_y;
        float stalk_y[2] = { 11.6f + 0.4f * fast_sin(t->clock * 2 + -1 + TAU), 11.6f + 0.4f * fast_sin(t->clock * 2 + 1 + TAU) };
        uint8_t *cls = ds()->sn_glass;
        if (g_sng_heading != t->snail_heading) {                   /* it turned: what the heading alone decides, again */
            g_sng_heading = t->snail_heading;
            for (int dy = -17; dy <= 17; dy++)
                for (int dx = -17; dx <= 17; dx++) {
                    float lx = ca * dx + sa * dy, ly = -sa * dx + ca * dy;
                    float alx = lx < 0 ? -lx : lx;
                    bool foot = false; int tone = SNG_DYN;
                    if (!(alx >= 1.5f && alx <= 4.3f && ly >= 9.3f && ly <= 12.7f))   /* out of both stalks' reach, wherever they sway */
                        tone = snail_glass_body(lx, ly, (float)dx, (float)dy, 0, &foot);
                    cls[(dy + 17) * SNG_N + dx + 17] = foot ? SNG_DYN : tone == SN_NONE ? SNG_NONE : (uint8_t)tone;
                }
        }
        for (int dy = -17; dy <= 17; dy++)
            for (int dx = -17; dx <= 17; dx++) {
                int k = cls[(dy + 17) * SNG_N + dx + 17];
                if (k == SNG_NONE) continue;
                if (k == SNG_DYN) {
                    float lx = ca * dx + sa * dy, ly = -sa * dx + ca * dy;   /* inverse rotation */
                    k = snail_glass_tone(lx, ly, (float)dx, (float)dy, t->clock, stalk_y);
                }
                snail_put(c, cx + dx, cy + dy, k, SNAIL_TINT_GLASS, &row);
            }
    }
}
/* does the snail on the floor (upright), as draw_snail painted it this frame (ds()->sn_tones), cover (x, y)? */
static bool snail_covers(const tank_t *t, int x, int y) {
    if (!(t->sd_unlocks & SD_ITEM_SNAIL) || t->snail_x < 0 || !tank_snail_upright(t)) return false;
    const int ox = (int)t->snail_x, oy = (int)(t->snail_y + SNAIL_SOLE_DY);
    const int lx = cosf(t->snail_heading) < 0 ? ox - x : x - ox, ly = y - oy;
    if (lx < -16 || lx > 20 || ly < -18 || ly > 0) return false;
    return ds()->sn_tones[(ly + 18) * 37 + lx + 16] != SN_NONE;
}
/* ---- the urchin, procedural (2026-10-02, SD_ITEM_URCHIN) ----
 * After Strato's sea-urchin-v1 (working-assets): a domed purple urchin seen
 * a little from above, a crown of short pointed spines with pale tips, warm
 * light from the upper left, no outline. Drawn the snail's way - a tone rule
 * per pixel over a ~37 x 24 box, mixed with the water of its row - in his six
 * tones. The spines are its life: they wave slowly, each on its own beat,
 * and work faster while it chews. The dome's centre is (urchin_x,
 * URCHIN_FLOOR_Y); its flat base sits on the root line, UR_BASE below. */
enum { UR_T0, UR_T1, UR_T2, UR_T3, UR_T4, UR_T5, UR_NONE };
static const uint32_t URCHIN_RGB[UR_NONE] = { 0x320746, 0x66107d, 0x9732b1, 0xbe53bc, 0xf88ab8, 0xfff0d3 };
#define UR_SPINES 11
#define UR_FACE    5
#define UR_RX     10.0f
#define UR_RY     8.0f
#define UR_BASE   7.0f
#define UR_TINT   0.93f
typedef struct { float dx[UR_SPINES + UR_FACE], dy[UR_SPINES + UR_FACE], len[UR_SPINES + UR_FACE];
                 float rim[UR_SPINES], end[UR_SPINES], den[UR_SPINES]; } ur_pose_t;   /* per crown spine: where it leaves the dome, its tip, the wedge's taper -
                                                                                        once a frame here, not per pixel (2026-10-03: a sqrtf and two
                                                                                        divides per spine per pixel cost the 1.8 milliseconds a frame) */
/* the spines this frame: a fan from just under the left side, over the top,
 * to just under the right (the floor hides the rest), long and short in
 * turn; then a few short ones on the dome's face, pointing at the viewer
 * and down - the pale streaks of the art */
static void urchin_pose(ur_pose_t *p, float clock, bool chewing) {
    float amp = chewing ? 0.13f : 0.07f, sp = chewing ? 2.6f : 0.8f;
    for (int k = 0; k < UR_SPINES; k++) {
        float a = -3.14159f - 0.32f + k * (3.14159f + 0.64f) / (UR_SPINES - 1);
        a += amp * fast_sin(clock * sp + k * 1.7f + 8 * TAU);
        p->dx[k] = cosf(a); p->dy[k] = sinf(a);
        p->len[k] = (k & 1) ? 4.0f : 6.0f;
        p->rim[k] = 1.0f / sqrtf(p->dx[k] * p->dx[k] / (UR_RX * UR_RX) + p->dy[k] * p->dy[k] / (UR_RY * UR_RY));
        p->end[k] = p->rim[k] + p->len[k];
        p->den[k] = p->end[k] - p->rim[k] + 2;
    }
    static const float FACE_A[UR_FACE] = { -2.4f, -1.9f, 2.2f, 1.2f, 0.5f };
    for (int k = 0; k < UR_FACE; k++) {
        float a = FACE_A[k] + 0.6f * amp * fast_sin(clock * sp + k * 2.3f + 8 * TAU);
        p->dx[UR_SPINES + k] = cosf(a); p->dy[UR_SPINES + k] = sinf(a);
        p->len[UR_SPINES + k] = 4.5f;
    }
}
/* the tone at local (lx, ly) from the dome's centre, or UR_NONE */
static int urchin_tone(float lx, float ly, const ur_pose_t *p) {
    if (ly > UR_BASE) return UR_NONE;
    float lit = (-lx * 0.55f - ly * 0.83f) / UR_RX;                /* -1..1: lower right .. upper left */
    float ex = lx / UR_RX, ey = ly / UR_RY, er = ex * ex + ey * ey;
    /* the face spines first: they stand over the dome */
    for (int k = UR_SPINES; k < UR_SPINES + UR_FACE; k++) {
        static const float FX[UR_FACE] = { -4.5f, 0.5f, -5.0f, 2.0f, 5.5f }, FY[UR_FACE] = { -2.5f, -4.5f, 3.0f, 2.5f, 0.0f };
        float vx = lx - FX[k - UR_SPINES], vy = ly - FY[k - UR_SPINES];
        float u = vx * p->dx[k] + vy * p->dy[k], w = vx * p->dy[k] - vy * p->dx[k];
        if (u < 0 || u > p->len[k]) continue;
        float half = 1.6f * (1 - u / p->len[k]) + 0.35f;
        if (w * w > half * half) continue;
        return u > p->len[k] - 1.6f ? UR_T5 : lit > 0 ? UR_T4 : UR_T3;
    }
    if (er <= 1.0f) {                                               /* the dome */
        int tone = 2;
        if (lit > 0.15f) tone = 3;
        if (lit > 0.55f && er > 0.20f) tone = 4;
        if (lit < -0.30f) tone = 1;
        if (lit < -0.75f || ly > UR_BASE - 1.2f) tone = 0;
        int ix = (int)floorf(lx), iy = (int)floorf(ly);
        uint32_t h = chash(ix + 40, iy + 40);
        if ((h & 7) == 0) tone = tone > 0 ? tone - 1 : 0;           /* the mottle */
        else if ((h & 15) == 1 && tone < 4) tone++;
        return UR_T0 + tone;
    }
    /* the crown: a spine is a wedge from inside the dome out to a fine point */
    for (int k = 0; k < UR_SPINES; k++) {
        float u = lx * p->dx[k] + ly * p->dy[k], w = lx * p->dy[k] - ly * p->dx[k];
        float rim = p->rim[k], end = p->end[k];
        if (u < rim - 2 || u > end) continue;
        float half = 2.6f * (end - u) / p->den[k] + 0.3f;
        if (w * w > half * half) continue;
        float slit = -p->dx[k] * 0.55f - p->dy[k] * 0.83f;          /* the spine's own side of the light */
        if (u > end - 1.8f) return slit > -0.5f ? UR_T5 : UR_T4;    /* pale tips */
        if (u > end - 3.8f) return slit > -0.3f ? UR_T4 : UR_T3;
        return slit > 0.2f ? UR_T3 : slit > -0.5f ? UR_T2 : UR_T1;
    }
    return UR_NONE;
}
static void draw_urchin(ctx_t *c, const tank_t *t) {
    if (!(t->sd_unlocks & SD_ITEM_URCHIN) || t->urchin_x < 0) return;
    /* its tones a few times a second, not a frame (2026-10-23: the spines' pose per pixel
       was 2-3 ms of a square frame); the row's palette once a row, not a pixel */
    static float s_t = -1e9f; static bool s_chew;
    bool chew = tank_urchin_chewing(t);
    uint8_t *tn = ds()->ur_tones;
    if (t->clock - s_t >= 0.08f || t->clock < s_t || chew != s_chew) {
        ur_pose_t p; urchin_pose(&p, t->clock, chew);
        for (int ly = -16; ly <= (int)UR_BASE; ly++)
            for (int lx = -18; lx <= 18; lx++) tn[(ly + 16) * 37 + lx + 18] = (uint8_t)urchin_tone((float)lx + 0.5f, (float)ly + 0.5f, &p);
        s_t = t->clock; s_chew = chew;
    }
    int cx = (int)floorf(t->urchin_x + 0.5f), cy = (int)(URCHIN_FLOOR_Y + t->urchin_dy);   /* (urchin_dy: on a front piece's base line in its front lane) */
    for (int ly = -16; ly <= (int)UR_BASE; ly++) {
        int y = cy + ly;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        src_t pal[UR_NONE];
        for (int k = 0; k < UR_NONE; k++) pal[k] = src_color(mix(w, URCHIN_RGB[k], UR_TINT), c->dim);
        const uint8_t *row = tn + (ly + 16) * 37;
        for (int lx = -18; lx <= 18; lx++) {
            int tone = row[lx + 18];
            if (tone == UR_NONE) continue;
            px_blend_s(c, cx + lx, y, &pal[tone], 255);
        }
    }
}
/* does the urchin, as draw_urchin painted it this frame (ds()->ur_tones), cover (x, y)? */
static bool urchin_covers(const tank_t *t, int x, int y) {
    if (!(t->sd_unlocks & SD_ITEM_URCHIN) || t->urchin_x < 0) return false;
    const int lx = x - (int)floorf(t->urchin_x + 0.5f), ly = y - (int)(URCHIN_FLOOR_Y + t->urchin_dy);
    if (lx < -18 || lx > 18 || ly < -16 || ly > (int)UR_BASE) return false;
    return ds()->ur_tones[(ly + 16) * 37 + lx + 18] != UR_NONE;
}

/* ---- the strawberry crab, procedural (0.3.9, SD_ITEM_CRAB) ----
 * After Strato's strawberry-crab sheets (working-assets/crab/ref7..ref10 and
 * the pixel crabs he sent 10-23). The prototypes are working-assets/crab/
 * strawberry_side.py (the profile) and strawberry_proc.py (the roost),
 * whose coordinates these are: crab units = px, x across its middle, y down
 * from above it to its feet at CR_FLOOR_SIDE / CR_FLOOR_FRONT. Every cell is
 * COMPUTED from a few shapes at its centre (no smoothing, so it stays crisp
 * pixel art): a dome with three tones and single cream seeds, round eyes with
 * a glint, a tan face, claws (an oval palm, two pale jaws), legs as cubic
 * curves ROOTED UNDER the shell (it is drawn over their roots) and curled in
 * at the toe; then one pass darkens every cell on a boundary between two
 * parts or against the water by its own tone's step (no drawn line: the other
 * creatures are tone-stepped too; never into the sand). Two views:
 *  - the PROFILE (walk, rest, dig, eat, claws up), facing cr_facing;
 *  - the ROOST (CR_ROOST, Strato: "facing forward, straight toward the
 *    glass, tucked in on itself"): the front view, breathing, its legs
 *    settling in over its first second, claws open while poked.
 * Drawn the urchin's way: the tones go into a grid rebuilt only when the
 * pose changes (a key of mode, gait step, dig beat, breath, poke), and the
 * grid is blitted every frame with the row's palette mixed with the water.
 * On the volcano's flank the grid is sampled through a rotation (cr_tilt)
 * about its footing. */
#define CR_GX0          (-25)
#define CR_GW             50
#define CR_GH             25          /* rows: crab y from floor - 25 to floor (the bottom row stands on the sand) */
#define CR_TINT        0.90f
#define CR_FLOOR_SIDE  21.0f
#define CR_FLOOR_FRONT 22.0f
#define CR_LX (-0.50f)
#define CR_LY (-0.70f)
#define CR_LZ  0.51f
enum { CRT_NONE, CRT_O, CRT_R, CRT_RD, CRT_RH, CRT_D, CRT_L, CRT_LD, CRT_LK, CRT_P, CRT_Q, CRT_F, CRT_G, CRT_M, CRT_E, CRT_W, CRT_T, CRT_X, CRT_SD, CRT_SDD, CRT_N };   /* SD, SDD: the sand dollar it digs up, gold (the shop's coin) */
static const uint32_t CR_RGB[CRT_N] = { 0, 0x5e0a0e, 0xe8281a, 0xb8180f, 0xff5a40, 0xf8dccc, 0xf24a30, 0xc0301e, 0xff8a66,
                                        0xf6c8a0, 0xd09070, 0xe8b48a, 0xc4885e, 0xd0201a, 0x1a0c08, 0xffffff, 0xe89a50, 0x2a0a08, 0xf4c838, 0xb07c16 };
/* part ids (the outline goes between them): 1 shell, 2 face, 3 the near eye, 4 the far eye,
 * 10/11 claws, 12 the far claw, 20+ near legs (2 per leg: root half, toe half), 40+ far legs */
#define CR_LEG_PTS 11
typedef struct { float p[CR_LEG_PTS][2]; float x0, y0, x1, y1, w0, w1; bool far; } cr_leg_t;
typedef struct { float ax0, ay0, ax1, ay1, cx, cy, rx, ry, j1[4], j2[4], plx, ply; int bite; } cr_sclaw_t;   /* bite: a tone held in the jaws (CRT_NONE = empty) */
typedef struct {
    bool       front;
    float      dy;                           /* the body's drop (rest, a step's bob) or rise (the roost's breath) */
    cr_leg_t   leg[8]; int nleg;
    cr_sclaw_t claw, far_claw;               /* the profile */
    float      eye[2][3]; bool eye_far[2];   /* the profile: x, y, r */
    float      fclaw[2][5];                  /* the roost: cx, cy, rx, ry, side */
    float      open;                         /* the roost's jaws, 0 shut .. 1 open */
    int        salt;
    int        find;                         /* a SAND DOLLAR held up in its claw: 0 none, 1 / 2 the glint's two beats */
    float      fx, fy;                       /* ... where (crab units) */
} cr_pose_t;
static inline int cr_tone3(float in, int lit, int base, int dark, float hi, float lo) { return in > hi ? lit : in < lo ? dark : base; }
static bool cr_seed(float px, float py, float pitch, int salt) {   /* one seed per cell of a jittered grid (the prototypes' seed_at) */
    float gx = floorf((px + 40) / pitch), gy = floorf((py + 40) / pitch);
    uint32_t h = chash((int)gx + salt, (int)gy + 3 * salt);
    float sx = gx * pitch - 40 + 0.6f + (float)(h & 7) / 7.0f * (pitch - 1.6f);
    float sy = gy * pitch - 40 + 0.6f + (float)((h >> 3) & 7) / 7.0f * (pitch - 1.6f);
    return floorf(px) == floorf(sx) && floorf(py) == floorf(sy);
}
static void cr_leg(cr_leg_t *lg, const float q[4][2], float w0, float w1, bool far) {
    lg->x0 = lg->y0 = 1e9f; lg->x1 = lg->y1 = -1e9f;
    for (int i = 0; i < CR_LEG_PTS; i++) {
        float s = (float)i / (CR_LEG_PTS - 1), a = (1 - s) * (1 - s) * (1 - s), b = 3 * (1 - s) * (1 - s) * s, c = 3 * (1 - s) * s * s, d = s * s * s;
        float x = a * q[0][0] + b * q[1][0] + c * q[2][0] + d * q[3][0], y = a * q[0][1] + b * q[1][1] + c * q[2][1] + d * q[3][1];
        lg->p[i][0] = x; lg->p[i][1] = y;
        if (x < lg->x0) lg->x0 = x;
        if (x > lg->x1) lg->x1 = x;
        if (y < lg->y0) lg->y0 = y;
        if (y > lg->y1) lg->y1 = y;
    }
    lg->x0 -= w0; lg->y0 -= w0; lg->x1 += w0; lg->y1 += w0; lg->w0 = w0; lg->w1 = w1; lg->far = far;
}
/* the nearest point of a leg: distance, how far along (0 root .. 1 toe), signed side (+ right of the way it runs); false = outside its box */
static bool cr_leg_near(const cr_leg_t *lg, float px, float py, float *d, float *u, float *side) {
    if (px < lg->x0 || px > lg->x1 || py < lg->y0 || py > lg->y1) return false;
    float best = 1e9f;
    for (int i = 0; i < CR_LEG_PTS - 1; i++) {
        float x0 = lg->p[i][0], y0 = lg->p[i][1], vx = lg->p[i + 1][0] - x0, vy = lg->p[i + 1][1] - y0, l2 = vx * vx + vy * vy;
        if (l2 < 1e-9f) l2 = 1e-9f;
        float t = ((px - x0) * vx + (py - y0) * vy) / l2; t = t < 0 ? 0 : t > 1 ? 1 : t;
        float ex = px - x0 - t * vx, ey = py - y0 - t * vy, dd = ex * ex + ey * ey;
        if (dd < best) { best = dd; *u = (i + t) / (CR_LEG_PTS - 1); *side = ((px - x0) * vy - (py - y0) * vx) / sqrtf(l2); }
    }
    *d = sqrtf(best);
    return true;
}
static float cr_segd(float px, float py, const float *s, float *t) {
    float vx = s[2] - s[0], vy = s[3] - s[1], l2 = vx * vx + vy * vy;
    if (l2 < 1e-9f) l2 = 1e-9f;
    float tt = ((px - s[0]) * vx + (py - s[1]) * vy) / l2; tt = tt < 0 ? 0 : tt > 1 ? 1 : tt;
    *t = tt;
    float ex = px - s[0] - tt * vx, ey = py - s[1] - tt * vy;
    return sqrtf(ex * ex + ey * ey);
}
static void cr_set_sclaw(cr_sclaw_t *c, float ax0, float ay0, float ax1, float ay1, float cx, float cy, float rx, float ry,
                         float a0, float a1, float a2, float a3, float b0, float b1, float b2, float b3) {
    c->ax0 = ax0; c->ay0 = ay0; c->ax1 = ax1; c->ay1 = ay1; c->cx = cx; c->cy = cy; c->rx = rx; c->ry = ry;
    c->j1[0] = a0; c->j1[1] = a1; c->j1[2] = a2; c->j1[3] = a3; c->j2[0] = b0; c->j2[1] = b1; c->j2[2] = b2; c->j2[3] = b3; c->bite = CRT_NONE;
}
/* the roost (the front view): settle 1 -> 0 over its first moments, a stretch's claws, a poke, a breath (the shell up a pixel) */
static void cr_pose_roost(cr_pose_t *p, float settle, float stretch, bool poke, bool breath) {
    float flex = poke ? -0.8f : 0.6f - 1.6f * settle - 0.6f * stretch;
    p->front = true;
    p->dy = (breath && !poke) ? -1.0f : 0.0f;
    p->open = poke ? 1.0f : 0.7f * stretch;
    p->nleg = 0;
    for (int s = -1; s <= 1; s += 2) {
        float f = flex;
        const float outer[4][2] = { { s * 11.5f, 8.0f }, { s * (17.8f - f), 9.5f }, { s * (17.8f - f), 18.5f }, { s * (12.0f - f * 0.5f), 21.6f } };
        const float inner[4][2] = { { s * 9.5f, 11.5f }, { s * (13.8f - f), 13.0f }, { s * (13.4f - f), 18.5f }, { s * (10.6f - f * 0.4f), 21.2f } };
        cr_leg(&p->leg[p->nleg++], outer, 2.5f, 1.5f, false);
        cr_leg(&p->leg[p->nleg++], inner, 2.0f, 1.2f, false);
    }
    const float fc[2][5] = { { -7.4f, 16.8f, 6.6f, 4.0f, -1 }, { 7.4f, 16.8f, 6.6f, 4.0f, 1 } };
    memcpy(p->fclaw, fc, sizeof fc);
}
static void cr_pose(cr_pose_t *p, const tank_t *t) {
    memset(p, 0, sizeof *p);
    const int mode = t->cr_mode;
    const bool poke = t->cr_poke > 0 || t->cr_scurry || t->cr_find > 0;   /* a tap's claws up; running scared, claws up too; a find held up */
    if (t->cr_find > 0) p->find = 1 + (((int)(t->clock * 2)) & 1);     /* its glint beats twice a second (the grid rebuilds on the beat) */
    p->salt = 7 + t->cr_variant;
    p->front = mode == CR_ROOST || t->cr_find > 0;                   /* a find: it turns to the glass and holds the coin up in both claws (Strato 2026-10-24) */
    if (p->front) {                                                   /* the roost: strawberry_proc.py pose() */
        float settle = t->cr_mode_s < 1.2f ? floorf((1 - t->cr_mode_s / 1.2f) * 6) / 6 : 0;   /* it settles in: legs spread, then drawn under */
        float stretch = 0;
        if (t->cr_fidget_kind == CR_FID_STRETCH) stretch = floorf(fast_sin((1 - t->cr_fidget / CR_STRETCH_S) * 3.14159f + 4 * TAU) * 6 + 0.5f) / 6;   /* a slow open and close of its claws */
        if (stretch < 0) stretch = 0;
        cr_pose_roost(p, t->cr_find > 0 ? 0 : settle, stretch, poke, fast_sin(t->clock * 0.9f + 4 * TAU) > 0.35f);
        if (t->cr_find > 0) {                                         /* the claws raised over its head, the coin between them (cr_draw_disc leaves the claws' px over its lower edge) */
            const float fc[2][5] = { { -8.6f, 3.2f, 5.4f, 3.4f, -1 }, { 8.6f, 3.2f, 5.4f, 3.4f, 1 } };
            memcpy(p->fclaw, fc, sizeof fc); p->open = 0; p->dy = 0;
        }
        return;
    }
    /* the profile: strawberry_side.py pose() */
    static const float NEAR[4][9] = { { 5.0f, 12.0f, 10.0f, 13.0f, 13.0f, 17.5f, 13.5f, CR_FLOOR_SIDE, 0.0f },
                                      { 0.5f, 12.5f, 3.5f, 14.0f, 6.0f, 18.0f, 6.0f, CR_FLOOR_SIDE, 0.5f },
                                      { -4.0f, 12.5f, -8.0f, 13.5f, -10.0f, 18.0f, -9.5f, CR_FLOOR_SIDE, 0.0f },
                                      { -8.5f, 12.0f, -15.0f, 12.5f, -18.5f, 17.0f, -18.5f, CR_FLOOR_SIDE, 0.5f } };
    static const float FAR[4][9] = { { 7.0f, 11.5f, 12.5f, 12.5f, 16.0f, 16.5f, 16.5f, CR_FLOOR_SIDE - 0.5f, 0.5f },
                                     { -1.5f, 12.0f, 0.0f, 13.5f, 1.5f, 17.5f, 1.0f, CR_FLOOR_SIDE - 0.5f, 0.0f },
                                     { -6.0f, 12.0f, -11.5f, 13.0f, -14.0f, 17.0f, -14.0f, CR_FLOOR_SIDE - 0.5f, 0.5f },
                                     { -10.0f, 11.5f, -17.5f, 11.5f, -21.0f, 15.5f, -21.5f, CR_FLOOR_SIDE - 0.5f, 0.0f } };
    const bool nibble = mode == CR_CLIMB && t->cr_nibble > 0;      /* a stop on the flank, nibbling the rock (0.3.9.1) */
    const bool walking = tank_crab_walking(t) && !nibble && !(mode == CR_CLIMB && t->cr_climb >= 1 && t->cr_climb < 1 + 25.0f / 75.0f);
    const float gait = floorf(fmodf(t->cr_gait, TAU) / TAU * 16) / 16 * TAU;   /* 16 steps a stride: the grid rebuilds on a step, not a frame */
    const bool resting = (mode == CR_REST || mode == CR_DIG) && !poke && !walking;
    float dy = walking ? 0.45f * fast_sin(gait * 2 + 4 * TAU) : resting ? 1.2f : 0.0f;
    /* the big claw lies between two keys - down at the sand ahead (L 0) and up at its face (L 1) - its jaws parted by
       `open`. A dig and a meal are one pick of CR_PICK_S: down with the jaws open, a pinch, a bite carried up to its
       face, and down again (Strato 10-23: the claw "didn't look quite right"); a groom lifts it empty */
    float L = walking ? 0.35f : 0.3f, open = 0; int bite = CRT_NONE;
    if ((mode == CR_DIG || mode == CR_EAT || nibble) && !poke) {
        float u = floorf(fmodf((nibble ? CR_NIBBLE_S - t->cr_nibble : t->cr_mode_s) / CR_PICK_S, 1.0f) * 12) / 12;
        if (u < 0.3f) { L = 1 - u / 0.3f; open = u >= 0.1f ? 1 : 0; }
        else if (u < 0.42f) { L = 0; open = u < 0.36f ? 0.5f : 0; }
        else if (u < 0.75f) { L = (u - 0.42f) / 0.33f; bite = mode == CR_EAT ? CRT_X : CRT_G; }
        else { L = 1; bite = mode == CR_EAT ? CRT_X : CRT_G; }
        dy += 0.8f * (1 - L);                                         /* it leans in to the pick */
    } else if (t->cr_fidget_kind == CR_FID_GROOM) {
        float g = floorf((1 - t->cr_fidget / CR_GROOM_S) * 8) / 8;
        L = 0.3f + 0.7f * fast_sin(g * 3.14159f + 4 * TAU);
    } else if (t->cr_fidget_kind == CR_FID_SETTLE) dy += 1.0f;        /* it sits a little lower for a moment */
    p->dy = dy;
    const float k = resting ? 0.35f : 0.0f;                           /* at rest the toes draw in under it */
    p->nleg = 0;
    for (int pass = 0; pass < 2; pass++) {
        const float (*L)[9] = pass == 0 ? FAR : NEAR;
        for (int i = 0; i < 4; i++) {
            float fx = L[i][6], fy = L[i][7];
            if (walking) {                                            /* a stance sweeps the foot back 3.2 px, a swing lifts it forward */
                float f = fmodf(gait / TAU + L[i][8], 1.0f);
                if (f < 0.6f) fx += 1.6f - 3.2f * (f / 0.6f);
                else { float g = (f - 0.6f) / 0.4f; fx += -1.6f + 3.2f * g; fy -= 2.2f * fast_sin(g * 3.14159f + 4 * TAU); }
            }
            const float q[4][2] = { { L[i][0], L[i][1] + dy },
                                    { L[i][2] - (L[i][2] - L[i][0]) * k * 0.4f, L[i][3] + dy },
                                    { L[i][4] + (fx - L[i][6]) * 0.6f - (L[i][4] - L[i][0]) * k, L[i][5] + dy * 0.5f },
                                    { fx - (fx - L[i][0]) * k, fy } };
            if (pass == 0) cr_leg(&p->leg[p->nleg++], q, 2.1f, 1.2f, true);
            else cr_leg(&p->leg[p->nleg++], q, 2.5f, 1.2f, false);
        }
    }
    if (poke)                                                         /* claws up: both raised, jaws open */
        cr_set_sclaw(&p->claw, 8.5f, 12.0f + dy, 12.0f, 9.0f + dy, 14.5f, 6.5f + dy, 4.6f, 3.2f,
                     17.5f, 4.5f + dy, 20.5f, 2.5f + dy, 18.5f, 7.2f + dy, 21.5f, 6.4f + dy);
    else {
        static const float SAND[16]  = { 8.5f, 13.0f, 13.5f, 16.5f,  16.8f, 18.0f, 4.4f, 2.6f,  20.6f, 17.0f, 23.6f, 19.4f,  20.2f, 19.6f, 22.6f, 20.6f };
        static const float MOUTH[16] = { 8.5f, 13.0f, 11.5f, 13.2f,  13.8f, 11.6f, 3.6f, 2.7f,  13.2f,  9.6f, 11.6f,  7.8f,  15.4f,  9.4f, 14.6f,  7.0f };
        float k8[16];
        for (int i = 0; i < 16; i++) k8[i] = SAND[i] + (MOUTH[i] - SAND[i]) * L;
        k8[1] += dy; k8[3] += dy; k8[5] += dy; k8[9] += dy * L; k8[11] += dy * L; k8[13] += dy * L; k8[15] += dy * L;   /* the body's lean; at the sand the jaws stay on it */
        k8[10] += -1.0f * L * open; k8[11] += -1.6f * (1 - L) * open;  /* the jaws part: up and down at the sand, apart at the face */
        k8[14] += 1.0f * L * open;  k8[15] += 1.2f * (1 - L) * open;
        cr_set_sclaw(&p->claw, k8[0], k8[1], k8[2], k8[3], k8[4], k8[5], k8[6], k8[7], k8[8], k8[9], k8[10], k8[11], k8[12], k8[13], k8[14], k8[15]);
        if (bite != CRT_NONE) { p->claw.bite = bite; p->claw.plx = (k8[10] + k8[14]) * 0.5f; p->claw.ply = (k8[11] + k8[15]) * 0.5f; }
    }
    if (poke)
        cr_set_sclaw(&p->far_claw, 9.5f, 11.0f + dy, 12.0f, 8.0f + dy, 13.5f, 5.2f + dy, 3.2f, 2.4f,
                     15.8f, 3.8f + dy, 18.0f, 2.4f + dy, 16.4f, 6.0f + dy, 18.6f, 5.4f + dy);
    else
        cr_set_sclaw(&p->far_claw, 9.5f, 12.0f + dy, 12.5f, 12.6f + dy, 14.6f, 13.2f + dy, 3.0f, 2.2f,
                     17.2f, 12.4f + dy, 19.4f, 12.8f + dy, 17.2f, 14.2f + dy, 19.0f, 14.2f + dy);
    if (p->find) { p->fx = 20.2f; p->fy = 1.2f + dy; }               /* held up in the raised claw's jaws (the poke pose), over its head */
    p->eye[0][0] = 6.5f; p->eye[0][1] = 6.5f + dy; p->eye[0][2] = 2.7f; p->eye_far[0] = false;
    p->eye[1][0] = 10.0f; p->eye[1][1] = 4.4f + dy; p->eye[1][2] = 1.9f; p->eye_far[1] = true;
}
/* the profile's claw (strawberry_side.py claw_px): an arm, an oval palm, two jaws, maybe a pellet */
static int cr_sclaw_tone(float px, float py, const cr_sclaw_t *c, bool far) {
    int hit = CRT_NONE;
    float t;
    const float arm[4] = { c->ax0, c->ay0, c->ax1, c->ay1 };
    float d = cr_segd(px, py, arm, &t);
    if (d <= 1.6f - 0.2f * t) hit = d < 0.9f ? CRT_L : CRT_LD;
    float ex = (px - c->cx) / c->rx, ey = (py - c->cy) / c->ry, v = ex * ex + ey * ey;
    if (v <= 1.0f) {
        hit = cr_tone3(-ex * 0.5f - ey * 0.85f, CRT_LK, CRT_L, CRT_LD, 0.45f, -0.45f);
        if (v < 0.6f && cr_seed(px, py, 3.6f, 13)) hit = CRT_D;
    }
    for (int j = 0; j < 2; j++) {
        float dj = cr_segd(px, py, j == 0 ? c->j1 : c->j2, &t);
        if (dj <= 1.25f - 0.6f * t && (hit == CRT_NONE || t > 0.15f)) hit = (j == 0 || t > 0.5f) ? CRT_P : CRT_Q;
    }
    if (c->bite != CRT_NONE && (px - c->plx) * (px - c->plx) + (py - c->ply) * (py - c->ply) <= 1.69f) hit = c->bite;
    if (hit != CRT_NONE && far) hit = hit == CRT_LK ? CRT_L : hit == CRT_L ? CRT_LD : hit == CRT_LD ? CRT_RD : hit == CRT_P ? CRT_Q : hit == CRT_D ? CRT_LD : hit;
    return hit;
}
/* the tone and the part at crab-unit (px, py); painter's order, the last hit wins */
static int cr_cell(float px, float py, const cr_pose_t *p, int *part) {
    int tone = CRT_NONE; *part = 0;
    float d, u, side;
    if (p->front) {                                                   /* strawberry_proc.py crab_px */
        if (py >= 10.5f && py <= 15.5f && fabsf(px) <= 11.5f - (py - 11.5f) * 1.6f) {   /* the face under the lip, the fangs */
            tone = (fabsf(px) > 9.5f - (py - 11.5f) * 1.6f || py > 13.5f) ? CRT_G : CRT_F;
            if (fabsf(px) >= 2.0f && fabsf(px) <= 3.0f && py < 13.0f) tone = CRT_M;
            *part = 2;
        }
        for (int i = 0; i < p->nleg; i++) {
            const cr_leg_t *lg = &p->leg[i];
            if (!cr_leg_near(lg, px, py, &d, &u, &side)) continue;
            float w = lg->w0 + (lg->w1 - lg->w0) * u;
            if (d > w) continue;
            float out = side * (lg->p[0][0] < 0 ? 1.0f : -1.0f);
            tone = out < -0.15f * w ? CRT_LK : out > 0.75f * w ? CRT_LD : CRT_L;
            if (u > 0.9f) tone = CRT_P;
            *part = 20 + i * 2 + (u < 0.5f ? 0 : 1);
        }
        float y = py - p->dy;
        if (y > -0.3f && y < 11.6f) {                                 /* the shell: a dome 14.3 wide at the lip */
            float tt = (11.6f - y) / 11.9f, hw = 14.3f * powf(fmaxf(0.0f, 1 - tt * tt), 0.45f);
            if (fabsf(px) <= hw) {
                float nx = px / 14.3f, ny = tt, n2 = nx * nx + ny * ny, nz = sqrtf(n2 < 1 ? 1 - n2 : 0);
                tone = cr_tone3(nx * CR_LX - ny * CR_LY + nz * CR_LZ, CRT_RH, CRT_R, CRT_RD, 0.80f, 0.08f);
                bool clear = true;
                for (int k = -1; k <= 1; k += 2) if ((px - k * 5.5f) * (px - k * 5.5f) + (y - 8.5f) * (y - 8.5f) <= 3.9f * 3.9f) clear = false;
                if (clear && fabsf(px) < hw - 1.2f && y > 0.9f && y < 10.6f && cr_seed(px, y, 2.9f, p->salt)) tone = CRT_D;
                *part = 1;
                for (int k = -1; k <= 1; k += 2) {                    /* the eyes on the lip */
                    float ex = k * 5.5f, ey = 8.5f, ddx = px - ex, ddy = y - ey, e = sqrtf(ddx * ddx + ddy * ddy);
                    if (e <= 2.95f) {
                        bool g = ddx >= -2.0f && ddx < 0 && ddy >= -2.0f && ddy < 0 && e < 2.3f;
                        tone = g ? CRT_W : (ddy > 1.6f && fabsf(ddx) > 0.6f) ? CRT_T : CRT_E; *part = 3;
                    } else if (ddy > 2.95f && ddy <= 3.9f && fabsf(ddx) <= 1.5f) { tone = CRT_T; *part = 3; }
                }
            }
        }
        for (int j = 0; j < 2; j++) {                                 /* the claws folded in front */
            const float *c = p->fclaw[j];
            float ex = (px - c[0]) / c[2], ey = (py - c[1]) / c[3], v = ex * ex + ey * ey;
            if (v > 1.0f) continue;
            tone = cr_tone3(-ex * 0.6f - ey * 0.8f, CRT_LK, CRT_L, CRT_LD, 0.45f, -0.45f);
            if (v < 0.6f && cr_seed(px, py, 4.4f, 11 + j)) tone = CRT_D;
            float inner = -c[4] * (px - c[0]);
            if (inner > c[2] * 0.45f && py > c[1] - 1.6f - 1.2f * p->open) {   /* the pincers: pale, the jaws parting as it opens */
                tone = py > c[1] - 0.6f ? CRT_P : CRT_Q;
                float gap = 1.0f + 2.6f * p->open;
                if (py >= c[1] + 0.9f && py < c[1] + 0.9f + gap) tone = CRT_O;
            }
            *part = 10 + j;
        }
        return tone;
    }
    /* the profile (strawberry_side.py crab_px): far legs, far claw, near legs, face, shell, eyes, the big claw */
    for (int i = 0; i < p->nleg; i++) {
        const cr_leg_t *lg = &p->leg[i];
        if (!lg->far || !cr_leg_near(lg, px, py, &d, &u, &side)) continue;
        if (d <= 2.1f - 0.9f * u) { tone = u > 0.88f ? CRT_Q : side > 0.2f ? CRT_LD : CRT_L; *part = 40 + i * 2 + (u < 0.5f ? 0 : 1); }
    }
    int ct = cr_sclaw_tone(px, py, &p->far_claw, true);
    if (ct != CRT_NONE) { tone = ct; *part = 12; }
    for (int i = 0; i < p->nleg; i++) {
        const cr_leg_t *lg = &p->leg[i];
        if (lg->far || !cr_leg_near(lg, px, py, &d, &u, &side)) continue;
        float w = 2.5f - 1.3f * u;
        if (d > w) continue;
        bool back = lg->p[0][0] > lg->p[CR_LEG_PTS - 1][0];           /* a leg reaching back: its outer side is the other one */
        float sd = back ? -side : side;
        tone = sd < -0.25f * w ? CRT_LK : sd > 0.7f * w ? CRT_LD : CRT_L;
        if (u > 0.88f) tone = CRT_P;
        *part = 20 + i * 2 + (u < 0.5f ? 0 : 1);
    }
    {   float fx = (px - 8.5f) / 5.0f, fy = (py - (12.3f + p->dy)) / 2.8f;   /* the tan face under the front */
        if (fx * fx + fy * fy <= 1.0f) { tone = (fy > 0.7f || fx < -0.7f) ? CRT_G : CRT_F; *part = 2; } }
    {   float y = py - p->dy;                                         /* the shell: a long dome, higher at the front */
        const float x0 = -1.0f, rx = px > x0 ? 12.5f : 14.0f, uu = (px - x0) / rx;
        if (fabsf(uu) < 1.0f) {
            float top = 12.5f - 13.0f * sqrtf(1 - uu * uu) * (1.0f + 0.12f * uu), bot = 12.5f - 2.6f * uu * uu * uu * uu;
            if (top < bot && y >= top && y <= bot) {
                float ny = (y - (top + bot) * 0.5f) / fmaxf(1.0f, (bot - top) * 0.5f);
                ny = ny < -1 ? -1 : ny > 1 ? 1 : ny;
                float n2 = 0.6f * uu * uu + 0.6f * ny * ny, nz = sqrtf(n2 < 1 ? 1 - n2 : 0);
                tone = cr_tone3(uu * CR_LX + ny * CR_LY + nz * CR_LZ, CRT_RH, CRT_R, CRT_RD, 0.78f, 0.12f);
                bool clear = true;
                for (int k = 0; k < 2; k++) { float ddx = px - p->eye[k][0], ddy = py - p->eye[k][1], r = p->eye[k][2] + 1.4f; if (ddx * ddx + ddy * ddy < r * r) clear = false; }
                if (clear && y > top + 1.0f && y < bot - 1.0f && cr_seed(px, y, 2.9f, p->salt)) tone = CRT_D;
                *part = 1;
            }
        } }
    for (int k = 0; k < 2; k++) {                                     /* the big eye on the flank, the far one over the front edge */
        float ddx = px - p->eye[k][0], ddy = py - p->eye[k][1], R = p->eye[k][2], e = sqrtf(ddx * ddx + ddy * ddy);
        if (e > R) continue;
        bool far = p->eye_far[k];
        bool g = !far ? (ddx >= -2.0f && ddx < 0 && ddy >= -2.0f && ddy < 0 && e < R - 0.3f) : (ddx >= -1.0f && ddx < 0 && ddy >= -1.0f && ddy < 0);
        tone = g ? CRT_W : CRT_E;
        if (!far && ddy > 1.2f && fabsf(ddx) > 0.6f) tone = CRT_T;
        *part = far ? 4 : 3;
    }
    ct = cr_sclaw_tone(px, py, &p->claw, false);
    if (ct != CRT_NONE) { tone = ct; *part = 10; }
    return tone;
}
/* the grid: every cell's tone, then its EDGES - a cell that borders the water
   or another part takes its own tone's darker step (the shell's dark red, the
   limbs' dark, the pincers' deeper pale, the face's tan), never a drawn line:
   the shrimp, the urchin, the snail and the seahorse separate their parts by
   tone steps alone, and a maroon line round the crab made it the one creature
   in another illustration style (Strato 2026-10-24: "parity"; the prototypes
   in working-assets/crab still draw the line). The eyes keep their rims. */
static int cr_dark_of(int tone) {
    switch (tone) {
    case CRT_R: case CRT_RH: case CRT_D: case CRT_RD: return CRT_RD;
    case CRT_L: case CRT_LK: case CRT_LD: return CRT_LD;
    case CRT_P: case CRT_Q: return CRT_Q;
    case CRT_F: case CRT_G: case CRT_M: return CRT_G;
    case CRT_SD: return CRT_SDD;
    default: return tone;
    }
}
static void cr_build(uint8_t *tn, uint8_t *pt, const cr_pose_t *p) {
    const float floor_u = p->front ? CR_FLOOR_FRONT : CR_FLOOR_SIDE;
    for (int gy = 0; gy < CR_GH; gy++)
        for (int gx = 0; gx < CR_GW; gx++) {
            int part, tone = cr_cell(CR_GX0 + gx + 0.5f, floor_u - CR_GH + gy + 0.5f, p, &part);
            tn[gy * CR_GW + gx] = (uint8_t)tone; pt[gy * CR_GW + gx] = (uint8_t)(tone == CRT_NONE ? 0 : part);
        }
    for (int gy = 0; gy < CR_GH; gy++)                                /* the edge pass reads the parts, never the tones it just wrote */
        for (int gx = 0; gx < CR_GW; gx++) {
            const int t0 = pt[gy * CR_GW + gx];
            if (!t0 || t0 == 3 || t0 == 4) continue;
            bool edge = false;
            const int N[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
            for (int k = 0; k < 4 && !edge; k++) {
                int nx = gx + N[k][0], ny = gy + N[k][1];
                if (ny >= CR_GH) continue;                            /* the sand under it */
                int n = (nx < 0 || nx >= CR_GW || ny < 0) ? 0 : pt[ny * CR_GW + nx];
                if (n == t0 || n == 3) continue;
                if (p->front && t0 == 2 && n == 1) continue;          /* the lip's step is on the shell's side */
                edge = true;
            }
            if (edge) tn[gy * CR_GW + gx] = (uint8_t)cr_dark_of(tn[gy * CR_GW + gx]);
        }
}
static uint32_t cr_hash(const cr_pose_t *p) {                        /* FNV-1a over the pose (memset to 0 first: no stray padding) */
    const uint8_t *b = (const uint8_t *)p; uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof *p; i++) { h ^= b[i]; h *= 16777619u; }
    return h ? h : 1;
}
/* the SAND DOLLAR it holds up (2026-10-24 evening, Strato at the glass after the first staged find: "it was so small
 * ... nearly as big as the crab itself holding it above its head like the icon"): a gold disc the badge icon's size,
 * two fifths of the crab's width, over its head between the raised claws and overlapping the top of its shell - drawn
 * OUTSIDE the tone grid (25 rows end at its head), with its five-petal mark and the glint that beats twice a second.
 * (0.3.9 as released held a 7 px disc in the right claw's jaws, inside the grid.) */
#define CR_DISC_R     9.5f
#define CR_DISC_UP    3.0f                       /* the disc's middle this far over the grid's top row: its lower edge between the raised claws */
static inline bool cr_claw_tone(int tone) { return tone == CRT_L || tone == CRT_LD || tone == CRT_LK || tone == CRT_P || tone == CRT_Q || tone == CRT_O || tone == CRT_D; }
/* tn: the crab's tone grid as blitted 1:1 at (cx_px, feet), or NULL - a claw's px in it stays over the coin: it HOLDS it */
static void cr_draw_disc(ctx_t *c, const tank_t *t, float cx, float cy, float sc, const uint8_t *tn, int cx_px, int feet) {
    const float r = CR_DISC_R * sc;
    const int phase = ((int)(t->clock * 2)) & 1;
    for (int y = (int)floorf(cy - r); y <= (int)ceilf(cy + r); y++) {
        if (y < 0 || y >= TANK_H) continue;
        const uint32_t w = water_rgb(y);
        src_t gold = src_color(mix(w, CR_RGB[CRT_SD], CR_TINT), c->dim), dark = src_color(mix(w, CR_RGB[CRT_SDD], CR_TINT), c->dim), glint = src_color(CR_RGB[CRT_W], c->dim);
        for (int x = (int)floorf(cx - r); x <= (int)ceilf(cx + r); x++) {
            const float ddx = (x + 0.5f - cx) / sc, ddy = (y + 0.5f - cy) / sc, e2 = ddx * ddx + ddy * ddy;
            if (e2 > CR_DISC_R * CR_DISC_R) continue;
            const src_t *s = &gold;
            if (e2 > (CR_DISC_R - 1.0f) * (CR_DISC_R - 1.0f)) s = &dark;                                              /* the rim */
            else if (e2 < 1.3f) s = &dark;                                                                            /* the mark: a dot ... */
            else if (e2 > 4.0f && e2 < 42.0f) {                                                                       /* ... and five petals */
                const float a = atan2f(ddy, ddx), k = fmodf(a + 3.1415926f, 2 * 3.1415926f / 5);
                if (fabsf(k - 3.1415926f / 5) < 0.14f) s = &dark;
            }
            if (phase && ddx > 2.2f && ddx < 5.0f && ddy < -2.2f && ddy > -5.0f && e2 < 30.0f) s = &glint;          /* the glint's beat */
            if (tn) { const int gx = x - cx_px - CR_GX0, gy = y - feet + CR_GH - 1;
                      if (gx >= 0 && gx < CR_GW && gy >= 0 && gy < CR_GH && cr_claw_tone(tn[gy * CR_GW + gx])) continue; }   /* the claw's px: over the coin */
            px_blend_s(c, x, y, s, 255);
        }
    }
}
static void draw_crab(ctx_t *c, const tank_t *t) {
    if (!(t->sd_unlocks & SD_ITEM_CRAB) || t->cr_x < 0) return;
    /* the grid is built only for a pose not drawn lately (0.3.9.1, C1704 on Discord: "lag when the crab walks" - the walk
       rebuilt it on 60% of frames, a run on every one, a few ms each on the 1.8): the last CR_CACHE_N poses keep their
       grids, and a walk cycles through 16 */
    static uint32_t s_clock;
    const int mode = t->cr_mode;
    cr_pose_t p; cr_pose(&p, t);                                      /* cheap: no cell is computed here */
    const uint32_t key = cr_hash(&p);                                 /* (cr_pose quantizes: the same picture, the same key) */
    decor_scratch_t *d = ds();
    int slot = -1, old = 0;
    for (int i = 0; i < CR_CACHE_N; i++) {
        if (d->cr_keys[i] == key && !g_cr_nocache) { slot = i; break; }
        if (d->cr_used[i] < d->cr_used[old]) old = i;
    }
    if (slot < 0) { slot = g_cr_nocache ? 0 : old; cr_build(d->cr_tones[slot], d->cr_parts, &p); d->cr_keys[slot] = key; g_cr_builds++; }
    d->cr_used[slot] = ++s_clock;
    const uint8_t *tn = d->cr_tones[slot];
    g_cr_tn = tn;
    const int f = (t->cr_facing < 0 && mode != CR_ROOST) ? -1 : 1;
    const int feet = (int)floorf(CR_FLOOR_Y - t->cr_y);              /* the bottom row's y */
    /* backed into its shelter's dark (cr_back, tank.c: a motion in and out, never a switch): with it the crab draws
       SMALLER, a few px HIGHER (the cave's floor receding) and DARKER - everything but its eyes' glints and a held
       sand dollar sinks toward black (half dark at the back: a crab in shadow, its glints bright) */
    const float back = t->cr_back;
    float dark = 0.55f * back;
    if (t->cr_tilt == 0 && back <= 0.001f) {
        const int cx = (int)floorf(t->cr_x + 0.5f);
        for (int gy = 0; gy < CR_GH; gy++) {
            const int y = feet - (CR_GH - 1) + gy;
            if (y < 0 || y >= TANK_H) continue;
            const uint8_t *row = tn + gy * CR_GW;
            bool any = false;
            for (int gx = 0; gx < CR_GW; gx++) if (row[gx]) { any = true; break; }
            if (!any) continue;
            const uint32_t w = water_rgb(y);
            src_t pal[CRT_N];
            for (int k = 1; k < CRT_N; k++) {
                uint32_t rgb = mix(w, CR_RGB[k], CR_TINT);
                if (dark > 0 && k != CRT_W && k != CRT_E && k != CRT_SD && k != CRT_SDD) rgb = mix(rgb, 0x050304, dark);
                pal[k] = src_color(rgb, c->dim);
            }
            for (int gx = 0; gx < CR_GW; gx++) {
                int tone = row[gx];
                if (tone == CRT_NONE) continue;
                int x = f > 0 ? cx + CR_GX0 + gx : cx - 1 - (CR_GX0 + gx);
                px_blend_s(c, x, y, &pal[tone], 255);
            }
        }
        if (t->cr_find > 0) cr_draw_disc(c, t, cx, (float)(feet - CR_GH) - CR_DISC_UP, 1.0f, tn, cx, feet);   /* the sand dollar held up in both claws */
        return;
    }
    /* on the flank: (cr_x, its feet) is its FOOTING on the rock - the grid's (0, the bottom) - and the grid is
       sampled through a rotation about it by cr_tilt (tank.c: its underside against the slope). Backed into the
       dark the same sampler draws it through a SCALE about its feet (down to 0.82) with the feet lifted (up to 4 px) */
    const float sc = 1.0f - 0.18f * back, lift = 4.0f * back;
    const float ox = t->cr_x, oy = (float)(feet + 1) - lift, ca = cosf(t->cr_tilt), sa = sinf(t->cr_tilt);
    const int R = 34;
    for (int y = (int)oy - R; y <= (int)oy + R; y++) {
        if (y < 0 || y >= TANK_H) continue;
        src_t pal[CRT_N]; bool have = false;
        for (int x = (int)ox - R; x <= (int)ox + R; x++) {
            float dx = x + 0.5f - ox, dy = y + 0.5f - oy;
            float lx = (ca * dx + sa * dy) * f / sc, ly = (-sa * dx + ca * dy) / sc;   /* back into the grid: x from its middle, y up from its feet */
            int gx = (int)floorf(lx) - CR_GX0, gy = (int)floorf(ly) + CR_GH;
            if (gx < 0 || gx >= CR_GW || gy < 0 || gy >= CR_GH) continue;
            int tone = tn[gy * CR_GW + gx];
            if (tone == CRT_NONE) continue;
            if (!have) {
                uint32_t w = water_rgb(y);
                for (int k = 1; k < CRT_N; k++) {
                    uint32_t rgb = mix(w, CR_RGB[k], CR_TINT);
                    if (dark > 0 && k != CRT_W && k != CRT_E && k != CRT_SD && k != CRT_SDD) rgb = mix(rgb, 0x050304, dark);
                    pal[k] = src_color(rgb, c->dim);
                }
                have = true;
            }
            px_blend_s(c, x, y, &pal[tone], 255);
        }
    }
    if (t->cr_find > 0) cr_draw_disc(c, t, ox, oy - (CR_GH + CR_DISC_UP) * sc, sc, NULL, 0, 0);   /* the sand dollar over its head, scaled with it (never tilted: a find is a dig's) */
}

/* does the crab, as draw_crab painted it this frame (g_cr_tn: the grid it drew), cover (x, y)?
   The painter's two mappings run backwards: the 1:1 blit, or the sampler's rotation / scale about its feet. */
static bool crab_covers(const tank_t *t, int x, int y) {
    if (!(t->sd_unlocks & SD_ITEM_CRAB) || t->cr_x < 0) return false;
    const int feet = (int)floorf(CR_FLOOR_Y - t->cr_y);
    if (t->cr_find > 0) {                                             /* the sand dollar over its head */
        const float ddx = x + 0.5f - t->cr_x, ddy = y + 0.5f - ((float)(feet - CR_GH) - CR_DISC_UP);
        if (ddx * ddx + ddy * ddy <= CR_DISC_R * CR_DISC_R) return true;
    }
    if (x < (int)t->cr_x - 36 || x > (int)t->cr_x + 36 || y < feet - 36 || y > feet + 36) return false;
    const int f = (t->cr_facing < 0 && t->cr_mode != CR_ROOST) ? -1 : 1;
    const float back = t->cr_back;
    int gx, gy;
    if (t->cr_tilt == 0 && back <= 0.001f) {
        const int cx = (int)floorf(t->cr_x + 0.5f);
        gx = f > 0 ? x - cx - CR_GX0 : cx - 1 - x - CR_GX0;
        gy = y - feet + CR_GH - 1;
    } else {
        const float sc = 1.0f - 0.18f * back, lift = 4.0f * back;
        const float ox = t->cr_x, oy = (float)(feet + 1) - lift, ca = cosf(t->cr_tilt), sa = sinf(t->cr_tilt);
        float dx = x + 0.5f - ox, dy = y + 0.5f - oy;
        float lx = (ca * dx + sa * dy) * f / sc, ly = (-sa * dx + ca * dy) / sc;
        gx = (int)floorf(lx) - CR_GX0; gy = (int)floorf(ly) + CR_GH;
    }
    if (gx < 0 || gx >= CR_GW || gy < 0 || gy >= CR_GH) return false;
    return g_cr_tn && g_cr_tn[gy * CR_GW + gx] != CRT_NONE;
}
/* is (x, y) one of the front slot's critters' (g_guard_who), as painted this frame? */
static bool slot_covers(int x, int y) {
    return ((g_guard_who & GUARD_CRAB) && crab_covers(g_guard, x, y)) || ((g_guard_who & GUARD_SNAIL) && snail_covers(g_guard, x, y))
        || ((g_guard_who & GUARD_URCHIN) && urchin_covers(g_guard, x, y));
}

/* ---- the seahorse (0.3.6; the prototype is working-assets/seahorse/
 * seahorse_proto.py, built from Strato's sprite - the local frame IS that
 * sprite's 23 x 39 grid, facing right, 1 cell = 1 px; the same geometry +
 * tone rule as the snail and the castle, lit from the upper left, grain
 * from a position hash, tinted with the row's water). The tail is a sampled
 * spine - a lead-in and a spiral - stamped into a distance map, so its rings
 * and edges come from the nearest sample instead of a search per pixel. Round
 * a blade the coil is centred on the blade's spine and the upper half of
 * every loop is BEHIND it: those pixels are skipped where they land on the
 * ribbon, so the blade shows through the coil, and a contact shadow sits on
 * the ribbon under each front crossing. That is the wrap (Strato: "it's
 * important we get the tail-wrapping and anchoring right"). */
#define SH_W 23
#define SH_H 39
#define SH_PTS_MAX 140
enum { SH_FREE = 0, SH_ONBLADE = 1, SH_GRIP = 2 };   /* the tail's pose: the open curl, round a blade, the tight grip */
enum { SHT_0, SHT_1, SHT_2, SHT_3, SHT_4, SHT_5, SHT_EYE, SHT_GLINT, SHT_FINM, SHT_FINR, SHT_N };
static const uint32_t SH_RGB[SHT_N]   = { 0xfff1b8, 0xfcd055, 0xfa9b19, 0xf3640e, 0xcb2f15, 0x8e1e0b, 0x1b3a63, 0xe4ecff, 0xffd98a, 0xf3640e };
static const uint8_t  SH_ALPHA[SHT_N] = { 255, 255, 255, 255, 255, 255, 255, 255, 150, 190 };
#define SH_TINT 0.82f
#define SHB_ON     0x80        /* a sprite byte: bits 0-3 the tone, bit 4 behind the blade, bit 5 the tail, bit 7 a pixel at all */
#define SHB_BEHIND 0x10
#define SHB_TAIL   0x20
static inline float sh_seg(float px, float py, float x0, float y0, float x1, float y1, float *u) {
    float vx = x1 - x0, vy = y1 - y0, L2 = vx * vx + vy * vy; if (L2 < 1e-6f) L2 = 1e-6f;
    float uu = ((px - x0) * vx + (py - y0) * vy) / L2; uu = uu < 0 ? 0 : uu > 1 ? 1 : uu;
    float dx = px - (x0 + uu * vx), dy = py - (y0 + uu * vy);
    if (u) *u = uu;
    return sqrtf(dx * dx + dy * dy);
}
static inline float sh_tail_hw(float frac) { return 2.4f - 1.8f * powf(frac, 0.8f); }
static inline int sh_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
/* the tail's spine: a bezier lead-in from the body's base and a spiral; on a
 * blade the spiral is centred on the blade (local x SX, half-width SW) and
 * the upper half of each loop is flagged behind it */
/* the three poses' spirals (the coil's centre, radii, turns, squash, the
 * lead-in's control point); a hold is reached by LERPING the open curl's
 * numbers into the hold's over u = 0..1 (tank_t.sh_land / SH_LAND_S), so
 * the tail closes round a blade or a finger and opens again instead of
 * snapping between two drawings (Strato 10-15) */
typedef struct { float cx, cy, r0, r1, turns, ky, c0x, c0y; } sh_spiral_t;
static void sh_spiral(sh_spiral_t *o, int pose, float SX, float SW) {
    if (pose == SH_ONBLADE)   { o->cx = SX; o->cy = 33.4f; o->r0 = SW + 3.2f; o->r1 = SW + 0.5f; o->turns = 2.05f; o->ky = 0.86f; o->c0x = SX - o->r0 - 0.4f; o->c0y = 29.2f; }
    else if (pose == SH_GRIP) { o->cx = 10.5f; o->cy = 32.6f; o->r0 = 4.6f; o->r1 = 0.9f; o->turns = 2.1f; o->ky = 0.9f; o->c0x = 5.0f; o->c0y = 29.4f; }
    else                      { o->cx = 11.0f; o->cy = 33.2f; o->r0 = 6.2f; o->r1 = 1.3f; o->turns = 1.55f; o->ky = 0.78f; o->c0x = 4.8f; o->c0y = 29.6f; }
}
static inline float sh_lerp(float a, float b, float u) { return a + (b - a) * u; }
static int sh_tail_path(decor_scratch_t *sc, int pose, float u, float t, float SX, float SW) {
    sh_spiral_t a, b; sh_spiral(&a, SH_FREE, SX, SW); sh_spiral(&b, pose, SX, SW);
    float cx = sh_lerp(a.cx, b.cx, u), cy = sh_lerp(a.cy, b.cy, u), r0 = sh_lerp(a.r0, b.r0, u), r1 = sh_lerp(a.r1, b.r1, u);
    float turns = sh_lerp(a.turns, b.turns, u), ky = sh_lerp(a.ky, b.ky, u), c0x = sh_lerp(a.c0x, b.c0x, u), c0y = sh_lerp(a.c0y, b.c0y, u);
    bool wrap = pose == SH_ONBLADE && u > 0.5f;                          /* past half way the blade is inside the coil */
    float breathe = 0.18f * sinf(t * 1.1f);
    const float P0x = 7.0f, P0y = 26.0f, P2x = cx - r0, P2y = cy;
    int n = 0; float s = 0, lx = 0, ly = 0;
    for (int i = 0; i < 14; i++) {
        float u = i / 13.0f, a = (1 - u) * (1 - u), b = 2 * (1 - u) * u, c = u * u;
        float x = a * P0x + b * c0x + c * P2x, y = a * P0y + b * c0y + c * P2y;
        if (i) s += sqrtf((x - lx) * (x - lx) + (y - ly) * (y - ly));
        sc->sh_pts[n][0] = x; sc->sh_pts[n][1] = y; sc->sh_pts[n][2] = s; sc->sh_behind[n] = 0; n++; lx = x; ly = y;
    }
    float th_max = turns * 2 * 3.14159265f; int m = (int)(th_max / 0.17f);
    for (int i = 1; i <= m && n < SH_PTS_MAX; i++) {
        float th = th_max * i / m, f = th / th_max;
        float r = r0 + (r1 - r0) * powf(f, 0.9f) + breathe * f;
        float sn = sinf(th), x = cx - r * cosf(th), y = cy + ky * r * sn;
        s += sqrtf((x - lx) * (x - lx) + (y - ly) * (y - ly));
        sc->sh_pts[n][0] = x; sc->sh_pts[n][1] = y; sc->sh_pts[n][2] = s; sc->sh_behind[n] = wrap && sn < 0; n++; lx = x; ly = y;
    }
    return n;
}
/* the body, row-wise: the spine's x and the half-width of the bean at a row */
static bool sh_body(float ly, float *sx, float *w, float *tb) {
    if (ly < 13.2f || ly > 26.4f) return false;
    float tt = (26.0f - ly) / 12.8f; tt = tt < 0 ? 0 : tt > 1 ? 1 : tt;
    float sp = sinf(3.14159265f * tt);
    *sx = 7.0f + 1.0f * tt + 1.6f * sp; *w = 3.0f + 3.0f * powf(sp, 0.75f); *tb = tt;
    return true;
}
/* one local pixel's byte (0 = water). nod and the row's body numbers come
 * from the caller (once a frame / once a row, not once a pixel: the S3 has
 * no hardware sine or power) */
typedef struct { bool body; float sx, w, tb; } sh_row_t;
static uint8_t sh_tone(float lx, float ly, float t, float flutter, const decor_scratch_t *sc, int k, float nod, const sh_row_t *row, float jut) {
    const float ex = 13.2f + jut, ey = 9.8f + nod;                        /* the snatch: the head and the snout jut forward, the neck stretches */
    const bool head_rows = ly < 15.5f;                                     /* the head, its crown, the snout and the eye live above row 15:
                                                                            * the body and tail rows skip their seven distance tests */
    float de = head_rows ? sqrtf((lx - ex) * (lx - ex) + (ly - ey) * (ly - ey)) : 99;
    if (de <= 1.75f) {                                                     /* the eye, its glint upper-left */
        float gx = lx - (ex - 0.7f), gy = ly - (ey - 0.7f);
        return SHB_ON | (gx * gx + gy * gy < 0.49f ? SHT_GLINT : SHT_EYE);
    }
    float u = 0, d = head_rows && lx > 13 ? sh_seg(lx, ly, 15.0f + jut, 11.0f + nod, 21.6f + jut, 12.5f + nod, &u) : 99;   /* the snout */
    float hw = 1.55f - 0.5f * u;
    if (d <= hw) {
        int tone = SHT_2;
        if (ly > 11.0f + nod + 0.6f + u * 1.2f) tone = SHT_3;
        if (d > hw - 0.7f && ly > 11.0f + nod) tone = SHT_4;
        if (u > 0.9f) tone = SHT_4;
        if ((chash((int)lx, (int)ly) & 7) == 0) tone = sh_clamp(tone + 1, 1, 5);
        return SHB_ON | tone;
    }
    /* the coronet: short blunt nubs, the middle one tallest (Strato 10-15: the
     * first pass's sharp points read as spikes); one short cheek point */
    static const float SPIKES[5][5] = { { 8.9f, 4.0f, 8.9f, 1.7f, 1.25f }, { 10.3f, 4.0f, 10.5f, 2.0f, 1.15f }, { 6.8f, 4.6f, 6.2f, 3.3f, 0.95f },
                                        { 12.6f, 4.6f, 13.3f, 3.5f, 0.85f }, { 5.2f, 7.6f, 3.6f, 7.4f, 0.9f } };
    if (ly < 10.5f) for (int i = 0; i < 5; i++) {
        const float *q = SPIKES[i];
        d = sh_seg(lx, ly, q[0] + jut, q[1] + nod, q[2] + jut, q[3] + nod, &u);
        if (d <= q[4] * (1 - 0.15f * u)) return SHB_ON | ((u > 0.75f || d > q[4] * 0.55f) ? SHT_3 : SHT_2);
    }
    float dx = (lx - 10.4f - jut) / 5.5f, dy = (ly - 8.5f - nod) / 5.4f, r = head_rows ? sqrtf(dx * dx + dy * dy) : 99;   /* the head: a round ellipse */
    if (r <= 1.0f) {
        float lit = -dx * 0.6f - dy * 0.8f;
        int tone = SHT_2;
        if (lit > 0.45f) tone = SHT_1;
        if (lit < -0.45f) tone = SHT_3;
        if (r > 0.82f && lit < 0.1f) tone = SHT_4;
        if (de < 2.75f && (lx < ex || ly < ey)) tone = SHT_1;            /* the pale ring over the eye */
        if ((chash((int)lx, (int)ly) & 7) == 0) tone = sh_clamp(tone + 1, 0, 5);
        return SHB_ON | tone;
    }
    d = ly > 10 && ly < 17.5f && lx > 4 && lx < 13 + jut ? sh_seg(lx, ly, 8.6f + jut, 11.8f + nod, 8.2f, 14.2f, &u) : 99;   /* the throat joins head and body (its top goes with the head) */
    bool throat = d <= 3.1f;
    float sx = row->sx, w = row->w;
    bool body = row->body;
    if (body || throat) {
        float un, ridge; bool inside;
        if (body) {
            un = (lx - sx) / w; inside = fabsf(un) <= 1.0f;
            ridge = fmodf(ly - 14.0f + un * 1.2f + 40.0f, 4.0f);
            if (!inside && un < 0 && fabsf(un) <= 1.0f + 1.3f / w && ridge < 1.4f) return SHB_ON | SHT_4;   /* a dorsal bump */
        } else { sx = 8.4f; w = 3.1f; un = (lx - 8.4f) / 3.1f; inside = true; ridge = 9; }
        if (inside) {
            int tone = SHT_2;
            if (un > 0.28f) {                                              /* the belly plates */
                tone = SHT_1;
                if (fmodf(ly - 15.0f + 30.0f, 3.0f) < 0.9f) tone = SHT_2;
                if (un > 0.86f) tone = SHT_3;
            } else {
                if (ridge < 1.0f) tone = SHT_3;                            /* the ring ridges */
                if (un < -0.78f) tone = SHT_4; else if (un < -0.45f) tone = SHT_3;
                if (un > -0.45f && un < 0.1f && ridge > 1.6f && ly < 20) tone = SHT_2;
            }
            if ((chash((int)lx, (int)ly + 7) & 7) == 0) tone = sh_clamp(tone + 1, 0, 5);
            return SHB_ON | tone;
        }
    }
    float vx = lx - 5.2f, vy = ly - 20.3f, rr = lx < 11.5f && ly > 13.5f && ly < 27.5f ? sqrtf(vx * vx + vy * vy) : 0;   /* the dorsal fin: a fan of rays, fluttering (its corner only) */
    if (rr > 0.6f) {
        float ang = atan2f(vy, vx) * (180.0f / 3.14159265f); if (ang < 0) ang += 360;
        if (ang >= 118 && ang <= 242) {
            float a = (ang - 180) / 62.0f;
            float L = 5.6f * (1 - 0.45f * a * a) + 0.5f * sinf(t * 9.0f * flutter + a * 2.5f);
            if (rr <= L) {
                float ray = fmodf((ang - 180) / 14.0f + 0.25f * sinf(t * 9.0f * flutter + rr) + 8.0f, 1.0f);
                return SHB_ON | ((ray < 0.3f || rr > L - 0.8f) ? SHT_FINR : SHT_FINM);
            }
        }
    }
    if (sc->sh_d[k] != 255) {                                              /* the tail, from the distance map */
        float dd = sc->sh_d[k] / 32.0f, frac = sc->sh_fr[k] / 255.0f;
        hw = sh_tail_hw(frac);
        if (dd <= hw) {
            int tone = SHT_2;
            if (fmodf(frac * 60.0f, 2.6f) < 0.8f) tone = SHT_3;             /* the rings down the tail */
            if (dd > hw - 0.75f) tone = (sc->sh_fl[k] & 2) ? SHT_4 : SHT_3;  /* the shaded edge */
            if (frac > 0.93f) tone = SHT_4;
            if ((chash((int)lx, (int)ly + 3) & 7) == 0) tone = sh_clamp(tone + 1, 0, 5);
            return SHB_ON | SHB_TAIL | ((sc->sh_fl[k] & 1) ? SHB_BEHIND : 0) | tone;
        }
    }
    return 0;
}
/* the sprite for this frame: the tail stamped into the distance map first,
 * then a byte per pixel */
static void sh_sprite(decor_scratch_t *sc, uint8_t *out, float t, int pose, float u, float SX, float SW, float flutter, float jut) {
    int n = sh_tail_path(sc, pose, u, t, SX, SW);
    float L = sc->sh_pts[n - 1][2];
    memset(sc->sh_d, 255, SH_W * SH_H);
    float hwb = sh_tail_hw(0);
    for (int i = 0; i + 1 < n; i++) {
        const float *a = sc->sh_pts[i], *b = sc->sh_pts[i + 1];
        float hwa = hwb; hwb = sh_tail_hw(b[2] / L);
        float hw = hwa + 0.6f;
        int x0 = sh_clamp((int)floorf(fminf(a[0], b[0]) - hw), 0, SH_W - 1), x1 = sh_clamp((int)ceilf(fmaxf(a[0], b[0]) + hw), 0, SH_W - 1);
        int y0 = sh_clamp((int)floorf(fminf(a[1], b[1]) - hw), 0, SH_H - 1), y1 = sh_clamp((int)ceilf(fmaxf(a[1], b[1]) + hw), 0, SH_H - 1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float px = x + 0.5f, py = y + 0.5f, u;
                float d = sh_seg(px, py, a[0], a[1], b[0], b[1], &u);
                float frac = (a[2] + (b[2] - a[2]) * u) / L;
                if (d > hwa + (hwb - hwa) * u + 0.6f) continue;
                int di = (int)(d * 32); if (di > 254) di = 254;
                int k = y * SH_W + x;
                if (di < sc->sh_d[k]) {
                    float cross = (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
                    sc->sh_d[k] = (uint8_t)di; sc->sh_fr[k] = (uint8_t)(frac * 255);
                    sc->sh_fl[k] = (uint8_t)((sc->sh_behind[i] ? 1 : 0) | (cross > 0 ? 2 : 0));
                }
            }
    }
    float nod = 0.35f * sinf(t * 1.3f);
    for (int y = 0; y < SH_H; y++) {
        sh_row_t row; row.body = sh_body(y + 0.5f, &row.sx, &row.w, &row.tb);
        for (int x = 0; x < SH_W; x++) out[y * SH_W + x] = sh_tone(x + 0.5f, y + 0.5f, t, flutter, sc, y * SH_W + x, nod, &row, jut);
    }
}
/* the sprite is rebuilt at ~10 Hz, not every frame (its animation - the nod,
 * the fin, the tail's breathing - is slow; a landing's morph is 0.7 s), and
 * at once when the pose or the morph moves: the build is the dear part on
 * the S3 (a sine, a power or a root for most pixels) */
static float g_sh_built_t = -1, g_sh_built_u = -1, g_sh_built_sw = -1, g_sh_built_jut = 0; static int g_sh_built_pose = -1;
static void sh_sprite_cached(decor_scratch_t *sc, float t, int pose, float u, float SX, float SW, float flutter, float jut) {
    bool stale = g_sh_built_pose != pose || fabsf(g_sh_built_u - u) > 0.04f || fabsf(g_sh_built_sw - SW) > 0.1f
              || fabsf(g_sh_built_jut - jut) > 0.4f || t - g_sh_built_t >= 0.1f || t < g_sh_built_t;
    if (!stale) return;
    sh_sprite(sc, sc->sh_spr, t, pose, u, SX, SW, flutter, jut);
    g_sh_built_t = t; g_sh_built_u = u; g_sh_built_sw = SW; g_sh_built_pose = pose; g_sh_built_jut = jut;
}
/* a blade's ribbon at a tank row, exactly as draw_veg lays it (its sway and
 * taper): the centre x and the half-width; false off the blade */
static bool veg_ribbon_at(const tank_t *t, int b, int i, int y, float clock, float *cx, float *half) {
    float bx; int segs = tank_veg_frond(t, b, i, &bx);
    if (segs < 1) segs = 1;
    if (segs > VEG_MAX_SEGS) segs = VEG_MAX_SEGS;
    bool sword = tank_veg_kind(t, b) == VEG_KIND_SWORD;
    int seed = VEG_SEED[b];
    float sway = fast_sin(clock * (sword ? 0.6f : 0.9f) + (i + seed) * 1.7f) * (sword ? 2.5f : 4.0f);
    if (g_veg_step == 2) { int yb = (int)(TANK_BOT - 16 + VEG_SEG_RY); y = yb - ((yb - y) & ~1); }   /* row pairs: the pair's lower row, as draw_veg paints it */
    float sp = (TANK_BOT - 16 - y) / VEG_SEG_DY;
    if (sp < -0.7f || sp > segs - 1 + 0.7f) return false;
    if (sp < 0) sp = 0;
    if (sp > segs - 1) sp = (float)(segs - 1);
    int k = (int)sp, k1 = k + 1 > segs ? segs : k + 1; float fr = sp - k;
    float xk  = bx + sway * k  / (float)segs * fast_sin(k  * 0.4f + clock * 0.6f + i + seed);
    float xk1 = bx + sway * k1 / (float)segs * fast_sin(k1 * 0.4f + clock * 0.6f + i + seed);
    *cx = xk + (xk1 - xk) * fr;
    float u = sp / (segs > 1 ? segs - 1 : 1);
    *half = sword ? 1.2f + 6.0f * (u < 0.4f ? u / 0.4f : (1 - u) / 0.6f) : fmaxf(0.4f, 2.4f - 2.0f * u);
    return true;
}
/* its pitch this frame, radians (the renderer's rule, below): forward with its speed */
static float seahorse_pitch(const tank_t *t) {
    if (t->sh_mode == SH_PERCH && t->sh_akind != SH_ANCHOR_NONE) return 0;
    float speed = sqrtf(t->sh_vx * t->sh_vx + t->sh_vy * t->sh_vy);
    float f = fabsf(t->sh_vx) / 42.0f + speed / 120.0f; if (f > 1) f = 1;
    return 30.0f * f * 3.14159265f / 180.0f;
}
/* its box this frame (the draw's loop and the dynamic rect): the sprite's
 * rectangle through its pitch about the anchor, plus the lean, the bob and a
 * margin - upright (most of the time) that is the 23 x 39 cells and little
 * more, not a box sized for the full pitch */
static void seahorse_box(const tank_t *t, int *x0, int *y0, int *x1, int *y1) {
    float th = seahorse_pitch(t), cs = cosf(th), sn = fabsf(sinf(th));
    float hw = (SH_W * 0.5f + 1) * cs + (SH_H * 0.5f) * sn + 6;        /* the rotated rect's half extents, about its middle */
    float hh = (SH_H * 0.5f + 1) * cs + (SH_W * 0.5f) * sn + 4;
    float cx = t->sh_x, cy = t->sh_y - 33.0f + SH_H * 0.5f;             /* its middle: the anchor row is ~33 cells down */
    *x0 = (int)floorf(cx - hw); *x1 = (int)ceilf(cx + hw);
    *y0 = (int)floorf(cy - hh); *y1 = (int)ceilf(cy + hh);
}
/* the pitch (Strato 10-15, from real seahorses: "sometimes the head will
 * pitch forward when it wants speed; slower movements are more vertical and
 * erect"): the body tips forward with its speed through the water, up to
 * SH_PITCH_MAX degrees at a full swim, about its middle, and stands up
 * again as it eases into a spot. On a blade and at the finger it is erect. */
#define SH_PITCH_MAX 30.0f
#define SH_PIVOT_X 9.0f
#define SH_PIVOT_Y 19.0f
static void draw_seahorse(ctx_t *c, const tank_t *t) {
    if (!(t->sd_unlocks & SD_ITEM_SEAHORSE) || t->sh_x < 0 || t->sh_hidden) return;
    decor_scratch_t *sc = ds();
    bool held = t->sh_mode == SH_PERCH && t->sh_akind != SH_ANCHOR_NONE;
    bool onblade = held && t->sh_akind == SH_ANCHOR_BLADE;             /* the one anchor that sways */
    bool onpost = held && t->sh_akind == SH_ANCHOR_POST;
    bool onsand = held && t->sh_akind == SH_ANCHOR_SAND;
    /* the hold it is closing on (or opening from): the pose and how far along.
     * On the sand the open curl IS the pose: the tail curls under it. */
    int pose = t->sh_pose == SH_POSE_GRIP ? SH_GRIP : onsand ? SH_FREE : SH_ONBLADE;
    float u = t->sh_land / SH_LAND_S; u = u < 0 ? 0 : u > 1 ? 1 : u;
    u = u * u * (3 - 2 * u);                                             /* eased */
    float SX = 5.0f, SW = 1.5f, ax = t->sh_x, ay = t->sh_y, lean = 1.2f * fast_sin(t->clock * 0.7f), pitch = 0;
    float speed = sqrtf(t->sh_vx * t->sh_vx + t->sh_vy * t->sh_vy);
    float flutter = t->sh_mode == SH_HITCH && !t->sh_hitched ? 1.6f : speed > 2 ? 1.0f + speed / 40.0f : 1.0f;
    float free_u = held ? 1 - u : 1;                                     /* how much of the open water is still in it */
    if (onblade) {                                                       /* the blade's sway and lean come in with the hold */
        float cx0 = ax, h0 = SW, cx1 = ax, h1;
        if (veg_ribbon_at(t, t->sh_bed, t->sh_frond, (int)ay, t->clock, &cx0, &h0)) { ax += (cx0 - ax) * u; SW = fmaxf(1.0f, fminf(3.5f, h0)); }
        veg_ribbon_at(t, t->sh_bed, t->sh_frond, (int)ay, t->clock + 0.05f, &cx1, &h1);
        float l = (cx1 - cx0) / 0.05f * 0.9f; l = l < -3 ? -3 : l > 3 ? 3 : l;
        lean = lean * (1 - u) + l * u;
    } else if (onpost) { SW = fmaxf(1.0f, fminf(3.5f, t->sh_ahw)); lean *= 1 - u; }   /* a post stands still: the coil hugs its middle */
    else if (onsand) lean *= 1 - u * 0.6f;
    if (free_u > 0) {                                                    /* off a blade it bobs in the water and pitches with speed */
        ay += free_u * 1.6f * fast_sin(t->clock * 1.7f); ax += free_u * 0.8f * fast_sin(t->clock * 0.9f + 1.0f);
        float f = fabsf(t->sh_vx) / 42.0f + speed / 120.0f; if (f > 1) f = 1;
        pitch = SH_PITCH_MAX * f * 3.14159265f / 180.0f;                /* forward = the way it faces */
    }
    sh_spiral_t pa, pb; sh_spiral(&pa, SH_FREE, SX, SW); sh_spiral(&pb, pose, SX, SW);
    float coilx = sh_lerp(pa.cx, pb.cx, u), coily = sh_lerp(pa.cy, pb.cy, u);
    sh_sprite_cached(sc, t->clock, pose, u, SX, SW, flutter, tank_seahorse_jut(t));
    int fac = t->sh_facing >= 0 ? 1 : -1;
    /* local (mirrored) -> tank: p = (lxm - coilx_m, ly - coily) sheared by the lean, rotated
     * by the pitch about the body's middle, then the anchor. Pitched, it is drawn by the
     * INVERSE: every tank pixel of the box maps back to a local cell, so a rotated sprite
     * has no holes. UPRIGHT (held: most of the time) each row is a whole-pixel shift of the
     * cells, and that shift has HYSTERESIS: a row keeps its snapped place until the true
     * one has pulled 0.75 px away (Strato 10-15: with the slow sway a row hovered at a
     * half-pixel and "fought between two pixels" while its neighbours stayed put). */
    float coilx_m = fac > 0 ? coilx : SH_W - coilx, pivx_m = (fac > 0 ? SH_PIVOT_X : SH_W - SH_PIVOT_X) - coilx_m, pivy = SH_PIVOT_Y - coily;
    float th = fac > 0 ? pitch : -pitch, cs = cosf(th), sn = sinf(th);
    int bx0, by0, bx1, by1; seahorse_box(t, &bx0, &by0, &bx1, &by1);
    src_t pal[SHT_N]; int pal_y = -1;
    bool upright = pitch == 0;
    if (upright) {                                                       /* the row offset, snapped with hysteresis */
        float want = ay - coily;
        if (fabsf(want - sc->sh_yoff) > 0.75f) sc->sh_yoff = roundf(want);
        if (sc->sh_yoff == 0 && want != 0) sc->sh_yoff = roundf(want);  /* the first frame */
    }
    for (int py = by0; py <= by1; py++) {
        if (!CTX_IN(c, bx0 + 1, py) && !CTX_IN(c, bx1 - 1, py)) continue;
        float rcx = 0, rhalf = 0; bool ribbon = onblade ? veg_ribbon_at(t, t->sh_bed, t->sh_frond, py, t->clock, &rcx, &rhalf)
                                              : onpost && fabsf(py - t->sh_ay) <= 14 && (rcx = t->sh_ax, rhalf = t->sh_ahw, true);
        int row_ly = -1, row_xs = 0;
        if (upright) {
            row_ly = py - (int)sc->sh_yoff;                               /* this row's cell row */
            if (row_ly < 0 || row_ly >= SH_H) continue;
            float want = ax - coilx_m + lean * (33.0f - (row_ly + 0.5f)) / 33.0f;   /* where the row's cell 0 lands, exactly */
            if (fabsf(want - sc->sh_rowx[row_ly]) > 0.75f) sc->sh_rowx[row_ly] = roundf(want);
            row_xs = (int)sc->sh_rowx[row_ly];
        }
        for (int px = bx0; px <= bx1; px++) {
            if (!CTX_IN(c, px, py)) continue;
            int ilx, ily;
            if (upright) {
                int lxm = px - row_xs; ily = row_ly;
                if (lxm < 0 || lxm >= SH_W) continue;
                ilx = fac > 0 ? lxm : SH_W - 1 - lxm;
            } else {
                float qx = px + 0.5f - ax - pivx_m, qy = py + 0.5f - ay - pivy;          /* about the pivot */
                float rx = qx * cs + qy * sn + pivx_m, ry = -qx * sn + qy * cs + pivy;   /* un-rotated */
                float ly = ry + coily;
                float lxm = rx - lean * (33.0f - ly) / 33.0f + coilx_m;                  /* un-sheared */
                ilx = (int)floorf(fac > 0 ? lxm : SH_W - lxm); ily = (int)floorf(ly);
                if (ilx < 0 || ilx >= SH_W || ily < 0 || ily >= SH_H) continue;
            }
            uint8_t b = sc->sh_spr[ily * SH_W + ilx];
            if (!(b & SHB_ON)) continue;
            bool on_ribbon = ribbon && fabsf(px + 0.5f - rcx) <= rhalf + 0.3f;
            if ((b & SHB_BEHIND) && on_ribbon) continue;                   /* the blade shows through the coil */
            int tone = b & 15;
            if (pal_y != py) {                                             /* this row's tones, tinted with its water */
                uint32_t w = water_rgb(py < 0 ? 0 : py >= TANK_H ? TANK_H - 1 : py);
                for (int k = 0; k < SHT_N; k++) pal[k] = src_color(mix(w, SH_RGB[k], SH_TINT), c->dim);
                pal_y = py;
            }
            px_blend_s(c, px, py, &pal[tone], SH_ALPHA[tone]);
            /* a contact shadow on the ribbon under a front crossing of the tail */
            if ((b & SHB_TAIL) && on_ribbon && ily > 27 && ily + 1 < SH_H && !(sc->sh_spr[(ily + 1) * SH_W + ilx] & SHB_TAIL)) {
                float r2cx = rcx, r2half = rhalf;
                if ((onpost || veg_ribbon_at(t, t->sh_bed, t->sh_frond, py + 1, t->clock, &r2cx, &r2half)) && fabsf(px + 0.5f - r2cx) <= r2half + 0.3f)
                    px_blend(c, px, py + 1, 0x000000, 115);
            }
        }
    }
}

/* where the castle stands this frame: its centre x (-1 = not bought), its
 * depth, and whether the keeper is dragging it on the placement page (then
 * it is drawn live over a scene baked WITHOUT it, instead of a rebake per
 * frame) */
/* the coral likewise (2026-09-23): BEHIND it is baked into the scene; AMONG
 * and IN FRONT it is drawn every frame (a 60 x 92 sprite - cheap) */

/* ---- the reef cluster, procedural (2026-09-24; Strato's coral-cluster.png:
 * a branching coral, three purple tube sponges, a cyan brain coral and green
 * weed on a pile of grey stones). The same idiom as the coral, bigger: a
 * CL_CW x CL_CH grid of 2 px cells, five ELEMENTS drawn in painter's order
 * (rock, weed, the coral, the tubes, the brain, weed in front), each classed
 * by its own signed distance - rim below / right, lit above / left, a deep
 * variant per element (moss on the rock, the coral's tips, the tube mouths,
 * the brain's grooves, the weed's tips) - into one tone sprite. The size is
 * the growth's first phase: CLUSTER_SIZE_MIN of full on the day it is bought
 * (it must look mature at once), full at 1. The look is a scheme (three
 * presets: the coral / the tubes / the brain each), tinted per row. */
#define CL_CELL 2
#define CL_W    (CL_CW * CL_CELL)
#define CL_FY   (TANK_BOT - 14 + DECOR_SINK + g_dz_cl)   /* on its depth's line (0.3.9) */
#define CL_Q    64                                           /* sprite rebuilds over the size phase */
enum { CLE_ROCK, CLE_WEED, CLE_CORAL, CLE_TUBE, CLE_BRAIN, CLE_N };
enum { CLV_BODY, CLV_LIT, CLV_RIM, CLV_SHADE, CLV_DEEP, CLV_N };
#define CL_TONE(e, v) (1 + (e) * CLV_N + (v))
static int      g_cl_q = -1, g_cl_cells;
static int      g_cl_scheme = -1; static float g_cl_dim = -1; static int g_cl_dzk = 99;
/* geometry in unscaled cells: x from the left, y UP from the base row; the
 * size scales it about (36, 0) */
#define CL_CX 36.0f
typedef struct { float x0, y0, x1, y1, r; } cl_cap_t;
static const cl_cap_t CL_ROCKS[] = {                         /* stones, as capsules (a circle = a zero-length one) */
    { 8, 3, 8, 3, 6 }, { 20, 4, 20, 4, 7 }, { 33, 3, 33, 3, 6 }, { 46, 4, 46, 4, 7 }, { 58, 3, 58, 3, 6 }, { 66, 4, 66, 4, 5 },
    { 14, 7, 14, 7, 4 }, { 40, 7, 40, 7, 4 }, { 52, 8, 52, 8, 4 }, { 4, 6, 70, 6, 2.5f },
};
static const cl_cap_t CL_TUBES[] = {                         /* back to front */
    { 45, 5, 44, 22, 4.5f }, { 61, 4, 63, 27, 5.0f }, { 52, 4, 53, 34, 5.5f }, { 58, 3, 59, 13, 3.5f },
};
static const cl_cap_t CL_BRAIN[] = {
    { 35, 9, 35, 9, 7 }, { 28, 7, 28, 7, 5.5f }, { 42, 7, 42, 7, 5.5f }, { 35, 14, 35, 14, 5 }, { 31, 12, 31, 12, 4 }, { 39, 12, 39, 12, 4 },
};
static const cl_cap_t CL_WEED_BACK[] = {
    { 13, 3, 11, 11, 1.1f }, { 14, 3, 15, 12, 1.1f }, { 12, 3, 9, 9, 1.0f },
    { 30, 3, 29, 10, 1.1f }, { 31, 3, 33, 11, 1.0f },
    { 47, 3, 48, 12, 1.1f }, { 46, 3, 44, 10, 1.0f },
    { 66, 3, 68, 11, 1.1f }, { 65, 3, 63, 9, 1.0f }, { 67, 3, 67, 12, 1.0f },
};
static const cl_cap_t CL_WEED_FRONT[] = {
    { 21, 2, 19, 9, 1.1f }, { 22, 2, 24, 8, 1.0f }, { 41, 2, 43, 8, 1.0f }, { 40, 2, 39, 7, 1.0f }, { 56, 2, 57, 8, 1.0f },
};
#define CL_CORAL_X  24.0f                                    /* the branching coral's base, and its scale */
#define CL_CORAL_Y  5.0f
#define CL_CORAL_SX 0.95f
#define CL_CORAL_SY 0.85f
static inline float cl_cap_sd(float px, float py, const cl_cap_t *k, float rs) {
    float dx = k->x1 - k->x0, dy = k->y1 - k->y0, len2 = dx * dx + dy * dy;
    float u = len2 > 1e-6f ? ((px - k->x0) * dx + (py - k->y0) * dy) / len2 : 0;
    if (u < 0) u = 0;
    if (u > 1) u = 1;
    float ex = px - (k->x0 + dx * u), ey = py - (k->y0 + dy * u);
    return sqrtf(ex * ex + ey * ey) - k->r * rs;
}
/* an element's signed distance at an unscaled point; *deep = the element's
 * deep variant here (a tube's mouth, a brain groove, the coral's tip cap) */
static float cl_sd(int e, int pass, float px, float py, bool *deep) {
    float best = 1e9f; *deep = false;
    if (e == CLE_ROCK) {
        for (int i = 0; i < (int)(sizeof CL_ROCKS / sizeof CL_ROCKS[0]); i++) { float d = cl_cap_sd(px, py, &CL_ROCKS[i], 1); if (d < best) best = d; }
        *deep = best < -1.5f && py > 4 && (chash((int)(px * 0.7f), (int)(py * 0.7f)) % 100) < 22;   /* moss between the stones */
    } else if (e == CLE_WEED) {
        const cl_cap_t *w = pass ? CL_WEED_FRONT : CL_WEED_BACK; int n = pass ? (int)(sizeof CL_WEED_FRONT / sizeof w[0]) : (int)(sizeof CL_WEED_BACK / sizeof w[0]);
        for (int i = 0; i < n; i++) { float d = cl_cap_sd(px, py, &w[i], 1); if (d < best) best = d;
            float tx = px - w[i].x1, ty = py - w[i].y1; if (tx * tx + ty * ty < 2.2f) *deep = true; }
    } else if (e == CLE_CORAL) {
        float qx = (px - CL_CORAL_X) / CL_CORAL_SX + 16.0f, qy = (py - CL_CORAL_Y) / CL_CORAL_SY;   /* into the coral's own cells */
        bool tip; best = coral_sd(qx, qy, 1.0f, &tip) * CL_CORAL_SY; *deep = tip;
    } else if (e == CLE_TUBE) {
        const cl_cap_t *k = &CL_TUBES[pass];
        best = cl_cap_sd(px, py, k, 1);
        float mx = (px - k->x1) / (k->r - 1.3f), my = (py - k->y1) / 1.7f;
        *deep = mx * mx + my * my < 1;                           /* the mouth */
    } else if (e == CLE_BRAIN) {
        float second = 1e9f;                                     /* the lobes: the seam where two meet is a groove */
        for (int i = 0; i < (int)(sizeof CL_BRAIN / sizeof CL_BRAIN[0]); i++) {
            float d = cl_cap_sd(px, py, &CL_BRAIN[i], 1);
            if (d < best) { second = best; best = d; } else if (d < second) second = d;
        }
        float v = fast_sin(px * 1.3f + py * 0.9f) + 0.6f * fast_sin(py * 2.1f - px * 0.5f);
        *deep = best < -0.8f && ((second < 0.5f && second > -1.2f) || v > 0.95f);   /* the seams, and a few winding grooves */
    }
    return best;
}
/* one element pass over the sprite: classed by its own distance field and
 * painted over what is there */
static void cl_paint(int e, int pass, float s) {
    int8_t  (*sd)[CL_CW] = ds()->cl_sd;                         /* eighths of a cell; the deep flags as bits (see sd_q) */
    uint8_t (*dp)[(CL_CW + 7) / 8] = ds()->cl_dp;
    for (int j = 0; j < CL_CH; j++)
        for (int i = 0; i < CL_CW; i++) {
            float px = CL_CX + (i + 0.5f - CL_CX) / s, py = (CL_CH - 1 - j + 0.5f) / s;   /* unscale the query */
            bool deep; sd[j][i] = sd_q(cl_sd(e, pass, px, py, &deep) * s);
            if (deep) dp[j][i >> 3] |= (uint8_t)(1 << (i & 7)); else dp[j][i >> 3] &= (uint8_t)~(1 << (i & 7));
        }
    for (int j = 0; j < CL_CH; j++)
        for (int i = 0; i < CL_CW; i++) {
            int d = sd[j][i];
            if (d >= 0) continue;
            bool deep = (dp[j][i >> 3] >> (i & 7)) & 1;
            bool out_l = i == 0 || sd[j][i - 1] >= 0, out_u = j == 0 || sd[j - 1][i] >= 0;
            bool out_r = i == CL_CW - 1 || sd[j][i + 1] >= 0, out_d = j == CL_CH - 1 || sd[j + 1][i] >= 0;
            int v;
            if (deep && e != CLE_ROCK) v = CLV_DEEP;
            else if (out_r || out_d) v = CLV_RIM;
            else if (out_l || out_u) v = CLV_LIT;
            else if (deep) v = CLV_DEEP;                        /* the rock's moss: never on its edge */
            else { uint32_t h = chash(i + 97 * e, j) % 100; v = h < 20 ? CLV_SHADE : h < 26 ? CLV_LIT : CLV_BODY; }
            if (e == CLE_TUBE && deep && (out_u || out_l)) v = CLV_RIM;   /* the mouth's far lip */
            ds()->cl_sprite[j][i] = (uint8_t)CL_TONE(e, v);
        }
}
static void cl_build(float g) {
    if (g > 1) g = 1;
    if (g < 0) g = 0;
    float s = CLUSTER_SIZE_MIN + (1.0f - CLUSTER_SIZE_MIN) * g;
    memset(ds()->cl_sprite, 0, sizeof ds()->cl_sprite);
    cl_paint(CLE_ROCK, 0, s);
    cl_paint(CLE_WEED, 0, s);
    cl_paint(CLE_CORAL, 0, s);
    for (int k = 0; k < 4; k++) cl_paint(CLE_TUBE, k, s);
    cl_paint(CLE_BRAIN, 0, s);
    cl_paint(CLE_WEED, 1, s);
    g_cl_cells = 0;
    for (int j = 0; j < CL_CH; j++) for (int i = 0; i < CL_CW; i++) g_cl_cells += ds()->cl_sprite[j][i] != 0;
}
int render_cluster_cells(float growth) { cl_build(growth); g_cl_q = -1; return g_cl_cells; }
static void cl_tint_fill(int scheme, float dim) {
    if (g_cl_scheme == scheme && g_cl_dim == dim && g_cl_dzk == g_dz_cl) return;
    float haze = decor_haze(0.90f, g_dz_cl);
    const cluster_scheme_t *sc = &CLUSTER_SCHEMES[scheme];
    uint32_t base[CLE_N] = { 0x9a9284, 0x8fc63a, sc->coral, sc->tube, sc->brain };
    uint32_t tone[CL_TONES]; tone[0] = 0;
    for (int e = 0; e < CLE_N; e++) {
        uint32_t c = base[e];
        tone[CL_TONE(e, CLV_BODY)]  = c;
        tone[CL_TONE(e, CLV_LIT)]   = mix(c, 0xfff0b0, e == CLE_ROCK ? 0.22f : 0.36f);
        tone[CL_TONE(e, CLV_RIM)]   = mix(c, e == CLE_ROCK ? 0x2a2620 : 0x20060e, 0.55f);
        tone[CL_TONE(e, CLV_SHADE)] = mix(c, 0x000000, 0.24f);
        tone[CL_TONE(e, CLV_DEEP)]  = e == CLE_ROCK ? 0x6f9a3a : e == CLE_CORAL ? mix(c, 0xffe8a0, 0.58f)
                                    : e == CLE_TUBE ? mix(c, 0x100418, 0.65f) : e == CLE_BRAIN ? mix(c, 0x0a3040, 0.45f) : mix(c, 0xe8ff80, 0.40f);
    }
    for (int r = 0; r < CL_H; r++) {
        int y = CL_FY - CL_H + r;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        for (int k = 1; k < CL_TONES; k++) ds()->cl_row[k][r] = rgb565(mix(w, tone[k], haze), dim);
    }
    g_cl_scheme = scheme; g_cl_dim = dim; g_cl_dzk = g_dz_cl;
}
static void draw_cluster(ctx_t *c, int cx, int scheme, float growth, bool final) {
    if (growth > 1) growth = 1;
    int q = (int)(growth * (CL_Q - 0.01f));
    if (q != g_cl_q) { cl_build(growth); g_cl_q = q; }
    cl_tint_fill(scheme, c->dim);
    int x0 = cx - CL_W / 2, y0 = CL_FY - CL_H;
    for (int j = 0; j < CL_CH; j++)
        for (int i = 0; i < CL_CW; i++) {
            uint8_t t = ds()->cl_sprite[j][i];
            if (!t) continue;
            for (int dy = 0; dy < CL_CELL; dy++) {
                int y = y0 + j * CL_CELL + dy;
                uint16_t v = ds()->cl_row[t][j * CL_CELL + dy];
                for (int dx = 0; dx < CL_CELL; dx++) {
                    int x = x0 + i * CL_CELL + dx;
                    if (!CTX_IN(c, x, y)) continue;
                    uint16_t *p = &CTX_PX(c, x, y);
                    *p = FB_SWAP16(v); TOUCH_PX(c, x, y);
                    rec_px(x, y);
                    if (final) {
                        PK_LIGHT_PX(p, x, y);
                        int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
                        if (a) px_darken(p, a);
                        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
                    }
                }
            }
        }
    draw_floor_mound(c, cx, CLUSTER_HALF_W - 2, 6, g_dz_cl, final);            /* anchored: the rock sits in a low hump of the floor's stones */
}
/* the bloom (growth 1 -> CLUSTER_FULL): more and more tentacles, dealt one
 * at a time round the hosts - the four tube mouths, the coral's seven tips,
 * the brain's top - up to CL_TENT_MAX, each host's fan widening as it fills.
 * Per frame, swaying (draw_crown); the bounding box for the caller's rect. */
#define CL_TENT_MAX 48
#define CL_HOSTS    12
static void draw_cluster_crown(ctx_t *c, int cx, int scheme, float growth, float clock, int *bx0, int *by0, int *bx1, int *by1) {
    *bx0 = *by0 = 1 << 20; *bx1 = *by1 = -1;
    float phase = (growth - 1.0f) / (CLUSTER_FULL - 1.0f);
    if (phase <= 0) return;
    if (phase > 1) phase = 1;
    int total = (int)(phase * CL_TENT_MAX + 0.5f);
    if (total < 1) return;
    const cluster_scheme_t *sc = &CLUSTER_SCHEMES[scheme];
    float s = 1.0f;                                          /* full size by the time it blooms */
    float x0 = cx - CL_W / 2;
    for (int k = 0; k < CL_HOSTS; k++) {
        int n = total / CL_HOSTS + (k < total % CL_HOSTS ? 1 : 0);
        if (!n) continue;
        float hx, hy, len; uint32_t rgb;
        if (k < 4) { const cl_cap_t *t = &CL_TUBES[k]; hx = t->x1; hy = t->y1 + 0.5f; len = 11 + t->r; rgb = sc->tube; }
        else if (k < 11) {                                   /* the coral's tips, in CORAL_SEGS order */
            int ti = 0; const coral_seg_t *sg = NULL;
            for (int i = 0; i < CORAL_NSEG && !sg; i++) if (CORAL_SEGS[i].tip && ti++ == k - 4) sg = &CORAL_SEGS[i];
            if (!sg) continue;
            hx = CL_CORAL_X + (sg->x1 - 16.0f) * CL_CORAL_SX; hy = CL_CORAL_Y + sg->y1 * CORAL_Y_SCALE * CL_CORAL_SY; len = 9; rgb = sc->coral;
        } else { hx = 35; hy = 20; len = 8; rgb = sc->brain; }
        float ox = x0 + (CL_CX + (hx - CL_CX) * s) * CL_CELL + 1, oy = CL_FY - hy * s * CL_CELL - 1;
        float spread = 0.30f + 0.08f * n;                    /* radians each side of up, widening as the fan fills */
        int qx0, qy0, qx1, qy1;
        draw_crown(c, ox, oy, n, -1.5708f - spread, -1.5708f + spread, len, mix(rgb, 0xffffff, 0.55f), k * 7, clock, &qx0, &qy0, &qx1, &qy1);
        if (qx1 >= qx0) { if (qx0 < *bx0) *bx0 = qx0; if (qy0 < *by0) *by0 = qy0; if (qx1 > *bx1) *bx1 = qx1; if (qy1 > *by1) *by1 = qy1; }
    }
}

/* ---- THE PIECES (2026-10-17, Strato: a stack of the pumpkin, the grass,
 * the sword plant and a coral broke - "we need some global layering
 * mechanic") ----
 * The castle, the coral, the reef cluster and the pumpkin are ONE list, back
 * to front: tank_decor_order (depth BEHIND / IN FRONT of the plant layer,
 * then the keeper's rank; the sword plant is grass - bed 3 - and takes part
 * by its rank where it matters). Everything about who covers whom follows
 * from the list:
 *   - the bake paints the pieces in list order (the ones BEHIND, then - with
 *     the scene cache and no placement page up - the ones IN FRONT), each
 *     recording the pixels it covers in its own MASK (ds()->pm[k]);
 *   - a grass bed's spans skip the masks of the pieces above the bed
 *     (veg_skip_for): every piece IN FRONT for the grass, plus the pieces at
 *     its own depth ranked above it for the sword plant;
 *   - after the fish, every piece IN FRONT comes back over what was drawn on
 *     it this frame from the scene (pc_restore: the dirty mask says where, the
 *     scene holds the pixel, already lit and vignetted) - the castle's front
 *     row included (its keep stays behind the fish: the swim-through), so its
 *     old per-rect repaint is gone;
 *   - an overlay drawn every frame for a piece - a crown's tentacles, the
 *     candle's flicker - is clipped by the masks of the pieces above it
 *     (pc_covered_above, through g_clip_k / the lantern's g_pk_k);
 *   - the lantern's light: the pieces above it shade (the castle by its own
 *     rules) and cast on the sand; those below are lit.
 * A piece being dragged on its page is not in the scene: it is drawn live
 * (BEHIND: right over the scene; IN FRONT: after the grass), unmasked. The
 * castle's masks are static and centre-relative (castle_put), so it covers
 * even while dragged. */
#define PIECES_MAX 5
typedef struct {
    int  item;                            /* 2 the castle, 3 the coral, 4 the cluster, 8 the pumpkin, 9 the volcano */
    int  cx, z;                           /* its centre on the floor, its depth */
    bool placing;                         /* dragged on its page this frame */
    bool baked;                           /* in the scene this frame (its mask is this frame's) */
    int  x0, y0, w, rows;                 /* its box: the mask's frame */
} piece_t;
static piece_t g_pc[PIECES_MAX];
static bool g_scene_front;                /* the baked scene holds the pieces IN FRONT */
/* the scene's signature: what the bake depends on (render_tank rebakes on a change) */
typedef struct { int n, item[PIECES_MAX], cx[PIECES_MAX], z[PIECES_MAX], q[PIECES_MAX]; uint32_t col[PIECES_MAX]; float dim; bool front; int lx; } scene_sig_t;
static scene_sig_t g_sig; static bool g_sig_ok;
static bool g_pk_hidden;                  /* director `pumpkin off`: the frame-rate A/B */
void render_set_pumpkin_hidden(bool hidden) { g_pk_hidden = hidden; }
bool render_pumpkin_hidden(void) { return g_pk_hidden; }
static float pc_half_w(int item) { return item == 2 ? CASTLE_HALF_W : item == 3 ? CORAL_HALF_W : item == 4 ? CLUSTER_HALF_W : item == 9 ? VOLCANO_HALF_W : PUMPKIN_HALF_W; }
static int   pc_index(int item) { for (int k = 0; k < g_npc; k++) if (g_pc[k].item == item) return k; return -1; }
/* the solid pieces above the lantern cast on the sand (pk_casts; the castle has its own rules) */
static void pk_casts_pieces(const tank_t *t) {
    (void)t;
    for (int k = g_pk_k + 1; k < g_npc; k++) {
        if (g_pc[k].item == 2 || g_pc[k].placing) continue;
        int hw = (int)pc_half_w(g_pc[k].item) - 3;
        pk_cast_add(g_pc[k].cx - hw, g_pc[k].cx + hw, 3);
    }
}
/* this frame's list, from the tank's order; front: the pieces IN FRONT ride in the scene */
static void pieces_build(const tank_t *t, bool cached, bool front) {
    int items[DECOR_SLOTS], n = tank_decor_order(t, items);
    g_npc = 0; g_pk_k = -1; g_castle_cx = -1;
    g_dz_castle = g_dz_coral = g_dz_cl = g_dz_pk = g_dz_vo = 0;
    for (int i = 0; i < n && g_npc < PIECES_MAX; i++) {
        int item = items[i];
        if (item == 0 || (item == 8 && g_pk_hidden)) continue;
        piece_t *p = &g_pc[g_npc];
        p->item = item; p->cx = (int)tank_decor_x(t, item); p->z = tank_decor_z(t, item);
        p->placing = setup_is_place() && setup_item() == item;
        p->baked = cached && !p->placing && (p->z != DECOR_Z_FRONT || front);
        switch (item) {                       /* its floor line by its depth first (0.3.9), then its box */
            case 2:  g_dz_castle = decor_dz(p->z, TANK_BOT - 16);
                     p->x0 = p->cx - CASTLE_MASK_W / 2; p->y0 = CASTLE_FY - CASTLE_ROWS; p->w = CASTLE_MASK_W; p->rows = CASTLE_ROWS + 12; g_castle_cx = p->cx; break;
            case 3:  g_dz_coral = decor_dz(p->z, TANK_BOT - 14 + DECOR_SINK);
                     p->x0 = p->cx - CORAL_W / 2; p->y0 = CORAL_FY - CORAL_H; p->w = CORAL_W; p->rows = CORAL_H + 4; break;
            case 4:  g_dz_cl = decor_dz(p->z, TANK_BOT - 14 + DECOR_SINK);
                     p->x0 = p->cx - CL_W / 2; p->y0 = CL_FY - CL_H; p->w = CL_W; p->rows = CL_H + 4; break;
            case 9:  g_dz_vo = decor_dz(p->z, TANK_BOT - 16);
                     p->x0 = p->cx - VO_W / 2; p->y0 = VO_FY - VO_H; p->w = VO_W; p->rows = VO_H + 4; break;
            default: g_dz_pk = decor_dz(p->z, TANK_BOT - 14 + DECOR_SINK);
                     p->x0 = p->cx - PK_W / 2; p->y0 = PK_FY - PK_H; p->w = PK_W; p->rows = PK_H + 4; break;
        }
        g_npc++;
    }
    /* depth first: every piece BEHIND, then every piece IN FRONT, each in its rank order */
    for (int i = 1; i < g_npc; i++) {
        piece_t v = g_pc[i]; int j = i;
        while (j > 0 && g_pc[j - 1].z > v.z) { g_pc[j] = g_pc[j - 1]; j--; }
        g_pc[j] = v;
    }
    g_pk_k = pc_index(8);
}
/* piece k's silhouette at (x, y): its recorded mask (this frame's scene), or the castle's static one */
static bool pc_covers(int k, int x, int y) {
    const piece_t *p = &g_pc[k];
    int lx = x - p->x0, r = y - p->y0;
    if ((unsigned)lx >= (unsigned)p->w || (unsigned)r >= (unsigned)p->rows) return false;
    if (p->item == 2) return (g_castle_mask[r][lx >> 5] >> (lx & 31)) & 1;
    return p->baked && ((ds()->pm[k][r][lx >> 5] >> (lx & 31)) & 1);
}
/* ... and the part of it that stands over the fish (a piece IN FRONT whole; the castle's front row) */
static bool pc_front_covers(int k, int x, int y) {
    const piece_t *p = &g_pc[k];
    if (p->z != DECOR_Z_FRONT) return false;
    if (p->item == 2) {
        int lx = x - p->x0, r = y - p->y0;
        return (unsigned)lx < (unsigned)p->w && (unsigned)r < (unsigned)p->rows && ((castle_front()[r][lx >> 5] >> (lx & 31)) & 1);
    }
    return pc_covers(k, x, y);
}
static bool pc_in_box(int k, int x0, int x1, int y) {
    const piece_t *p = &g_pc[k];
    return y >= p->y0 && y < p->y0 + p->rows && x1 >= p->x0 && x0 < p->x0 + p->w;
}
/* n (<= 32) bits of a bit row from bit l on; bits outside 0..nw*32-1 read 0 */
static inline uint32_t row_bits(const uint32_t *row, int nw, int l, int n) {
    int sh = 0;
    if (l < 0) { sh = -l; if (sh >= n) return 0; n -= sh; l = 0; }
    int w = l >> 5;
    if (w >= nw) return 0;
    uint64_t v = row[w];
    if (w + 1 < nw) v |= (uint64_t)row[w + 1] << 32;
    uint32_t out = (uint32_t)(v >> (l & 31));
    if (n < 32) out &= (1u << n) - 1;
    return out << sh;
}
static uint32_t pc_row_bits(int k, int x0, int n, int y) {
    const piece_t *p = &g_pc[k]; int r = y - p->y0;
    if ((unsigned)r >= (unsigned)p->rows) return 0;
    const uint32_t *row = p->item == 2 ? g_castle_mask[r] : p->baked ? ds()->pm[k][r] : NULL;
    return row ? row_bits(row, (p->w + 31) / 32, x0 - p->x0, n) : 0;
}
static bool pc_covered_above(int k, int x, int y) {
    for (int j = k + 1; j < g_npc; j++) if ((g_pc[j].baked || g_pc[j].item == 2) && pc_covers(j, x, y)) return true;
    return false;
}
bool render_decor_covers(int item, int x, int y) { int k = pc_index(item); return k >= 0 && pc_covers(k, x, y); }   /* (the tests) */
/* the CONTACT SHADOW (0.3.9, Strato: "we're losing depth ... everything flat
 * against the glass"): right after a piece is baked, its silhouette shifted
 * down and to the right (the light comes from the upper left) darkens what
 * stands behind it in the scene - the water, the sand, a piece behind it -
 * a hard step close in and a softer one further out, never on the piece
 * itself. The scene at that moment holds only what is behind it, so the
 * order does the rest. Bake only: nothing per frame. */
#define PC_SHADOW_NEAR 3
#define PC_SHADOW_FAR  6
static void pc_contact_shadow(ctx_t *c, int k) {
    const piece_t *p = &g_pc[k];
    int xa = p->x0, xb = p->x0 + p->w - 1 + PC_SHADOW_FAR, ya = p->y0, yb = p->y0 + p->rows - 1 + PC_SHADOW_FAR;
    if (xa < 0) xa = 0;
    if (xb > TANK_W - 1) xb = TANK_W - 1;
    if (ya < 0) ya = 0;
    if (yb > TANK_H - 1) yb = TANK_H - 1;
    for (int y = ya; y <= yb; y++)
        for (int x = xa; x <= xb; x++) {
            if (pc_covers(k, x, y)) continue;
            int a = pc_covers(k, x - PC_SHADOW_NEAR, y - PC_SHADOW_NEAR) ? 72 : pc_covers(k, x - PC_SHADOW_FAR, y - PC_SHADOW_FAR) ? 34 : 0;
            if (a) px_darken(&CTX_PX(c, x, y), a);
        }
}
/* the pieces a bed's spans leave alone (veg_span) */
static uint32_t veg_skip_for(const tank_t *t, int b) {
    int bz = bed_z(t, b); uint32_t m = 0;
    for (int k = 0; k < g_npc; k++) {
        if (!g_pc[k].baked && g_pc[k].item != 2) continue;              /* drawn live this frame: over the grass anyway, or dragged */
        if (g_pc[k].z == DECOR_Z_FRONT && bz != DECOR_Z_FRONT) m |= 1u << k;             /* in front of the plant layer */
        else if (b == 3 && bz != DECOR_Z_MIDDLE && g_pc[k].z == bz && tank_decor_above(t, g_pc[k].item, 0)) m |= 1u << k;   /* at the plant's depth, ranked above it */
    }
    return m;
}
/* a piece IN FRONT back over what was drawn on it this frame (the scene has it) */
static void pc_restore(uint16_t *fb, int k) {
    const piece_t *p = &g_pc[k];
    int xa = p->x0 < 0 ? 0 : p->x0, xb = p->x0 + p->w - 1 > TANK_W - 1 ? TANK_W - 1 : p->x0 + p->w - 1;
    int ya = p->y0 < 0 ? 0 : p->y0, yb = p->y0 + p->rows - 1 > TANK_H - 1 ? TANK_H - 1 : p->y0 + p->rows - 1;
    for (int y = ya; y <= yb; y++) {
        uint32_t *drow = g_dirty + y * DIRTY_WORDS_PER_ROW;
        for (int w = xa >> 5; w <= xb >> 5; w++) {
            uint32_t bits = drow[w];
            while (bits) {
                int x = (w << 5) + __builtin_ctz(bits); bits &= bits - 1;
                if (x < xa || x > xb || !pc_front_covers(k, x, y)) continue;
                fb[y * TANK_W + x] = g_scene[y * TANK_W + x];
                drow[w] &= ~(1u << (x & 31));
            }
        }
    }
}
/* one piece, whole: in the bake (final, recording its mask) or live */
static void pc_draw(ctx_t *c, const tank_t *t, int k, bool final, bool record) {
    piece_t *p = &g_pc[k];
    if (record) {
        memset(ds()->pm[k], 0, sizeof ds()->pm[k]);
        g_rec = &ds()->pm[k][0][0]; g_rec_x0 = p->x0; g_rec_y0 = p->y0; g_rec_w = p->w; g_rec_rows = p->rows; g_rec_wpr = (CASTLE_MASK_W + 31) / 32;
    }
    g_pk_shade_all = g_pk_lx >= 0 && g_pk_k >= 0 && k > g_pk_k && p->item != 2;   /* above the lantern: backlit (the castle has its own rules: the light through its arch and windows) */
    switch (p->item) {
        case 2:  draw_castle(c, p->cx, 0, final); if (p->z == DECOR_Z_BACK || record) arch_shade(c, k, p->cx, t->night, false); break;   /* IN FRONT: the bake carries the tunnel's shade too (2026-10-23); live, arch_shade_dirty does the rest */
        case 3:  draw_coral(c, p->cx, tank_coral_rgb(t), tank_coral_growth(t), final); break;
        case 4:  draw_cluster(c, p->cx, tank_cluster_scheme(t), tank_cluster_growth(t), final); break;
        case 9:  draw_volcano(c, p->cx, t->night, final); break;
        default: draw_pumpkin(c, p->cx, t->night, t->clock, final, true, final); break;
    }
    g_pk_shade_all = false;
    g_rec = NULL;
    if (record) pc_contact_shadow(c, k);
}
/* a piece's overlay, drawn every frame over the baked body: a crown's
 * tentacles - clipped by the pieces above it (g_clip_k); its rect joins the
 * vignette sweep */
#define PC_CROWN(k) do { int qx0 = 0, qy0 = 0, qx1 = -1, qy1 = 0; \
        if (g_pc[k].item == 3) draw_coral_crown(&c, g_pc[k].cx, tank_coral_rgb(t), tank_coral_growth(t), t->clock, &qx0, &qy0, &qx1, &qy1); \
        else if (g_pc[k].item == 4) draw_cluster_crown(&c, g_pc[k].cx, tank_cluster_scheme(t), tank_cluster_growth(t), t->clock, &qx0, &qy0, &qx1, &qy1); \
        if (qx1 >= qx0) DYN_RECT(qx0, qy0, qx1, qy1); } while (0)
/* the volcano's: its lava breathes over the baked rock, its bubbles rise (their rect joins the sweep) */
#define PC_VOLCANO(k) do { if (g_pc[k].baked) draw_volcano_glow(&c, g_pc[k].cx, t->clock, t->night, false, true); \
        int qx0, qy0, qx1, qy1; draw_volcano_bubbles(&c, g_pc[k].cx, t->clock, t->night, &qx0, &qy0, &qx1, &qy1); DYN_RECT(qx0, qy0, qx1, qy1); } while (0)
#define PK_LIGHT_Y (PK_FY - (int)(8.0f * PK_S * PK_CELL))   /* the face's middle: where the light leaves */

/* the floor's colour at (x,y), from a position hash so it is stable every
 * frame. The rectangle: a 14 px strip of speckled stones in four tones. The
 * bowl: the same speckle in 2 px grains over a bed that runs ~70 px down to
 * the glass, pebbles scattered through it, darkening with depth. */
static inline uint32_t floor_rgb(int x, int y) {
#ifdef TANK_ROUND
    uint32_t g = (uint32_t)(x >> 1) * 0x9E3779B1u ^ (uint32_t)(y >> 1) * 0x85EBCA77u;
    g ^= g >> 15; g *= 0x2C1B3C6Du; g ^= g >> 12;
    uint32_t tone = g % 16;
    uint32_t col = tone < 2 ? 0x2e3b2c : tone < 5 ? 0x22301f : tone < 8 ? 0x1a2418 : 0x101a12;
    int top = TANK_BOT - 14, cx = x / 13, cy = (y - top) / 9;            /* one pebble, or none, per 13 x 9 cell */
    uint32_t h = (uint32_t)(cx + 7) * 0x9E3779B1u ^ (uint32_t)(cy + 3) * 0xC2B2AE35u;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15;
    if (y > top + 4 && h % 8 < 3) {
        float px = cx * 13 + 4 + (h >> 4) % 6, py = top + cy * 9 + 4 + (h >> 8) % 3;
        float rx = 2.6f + (h >> 12) % 3, ry = 1.6f + ((h >> 16) % 3) * 0.5f;
        float dx = (x - px) / rx, dy = (y - py) / ry;
        if (dx * dx + dy * dy <= 1.0f) col = dy < -0.25f ? 0x44544a : (h >> 20) % 2 ? 0x334238 : 0x2a372c;   /* lit from above */
    }
    float depth = (float)(y - top) / (TANK_H - top);
    return mix(col, 0x02080a, depth < 0 ? 0 : depth * 0.55f);
#else
    uint32_t h2 = (uint32_t)((x * 73856093u) ^ (y * 19349663u));
    uint32_t tone = (h2 >> 4) % 16;
    return tone < 2 ? 0x2e3b2c : tone < 5 ? 0x22301f : tone < 8 ? 0x1a2418 : 0x101a12;
#endif
}
static void draw_scene(const tank_t *t, uint16_t *fb, int stride, float dim) {
    ctx_t c = ctx_full(fb, stride, dim);
    /* water gradient #0a3c46 → #08272f → #031015 */
    for (int y = 0; y < TANK_H; y++) {
        uint16_t v = FB_SWAP16(rgb565(water_rgb(y), dim));
        for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = v;
    }
    /* pebbled bottom: irregular top edge, speckled stones in three tones.
       All variation comes from a position hash so it is stable every frame. */
    for (int x = 0; x < TANK_W; x++) {
        uint32_t h = (uint32_t)(x * 2654435761u);
        int top = TANK_BOT - 14 - (int)((h >> 8) % 5);
        for (int y = top; y < TANK_H; y++) {
            uint32_t col = floor_rgb(x, y);
            fb[y * stride + x] = FB_SWAP16(rgb565(col, dim));
        }
    }
    /* (the dark oval under the left bed - the "reef rock" - is gone, 2026-10-02:
       it stood for nothing; the reef is the cluster the keeper buys) */
    for (int k = 0; k < g_npc; k++)                 /* uncached: every piece, in order, every frame - a dragged piece too when it is BEHIND, and the
                                                       castle whole (its keep goes under the fish), as the cached path draws them (0.3.9: before,
                                                       a dragged castle had only its front row here) */
        if (!g_pc[k].placing || g_pc[k].z == DECOR_Z_BACK || g_pc[k].item == 2) pc_draw(&c, t, k, false, false);
}


/* The baked scene (scene cache mode). Water gradient x porthole vignette
 * computed in 8-bit and ORDERED-DITHERED to RGB565 (4x4 Bayer: at 322 ppi
 * the pattern is invisible, the 5/6-bit banding of a dark gradient and of
 * the vignette falloff is not - Strato saw it at the edges), then the floor
 * and the backdrop decor on top, vignetted per pixel. Fills the vignette LUT on the
 * way. Bake-only: fidelity here costs nothing per frame. The light shafts
 * are gone (2026-09-01, Strato: they never looked good on this screen). */
static void bake_scene(const tank_t *t, uint16_t *sc, float dim) {
    static const uint8_t bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
    for (int y = 0; y < TANK_H; y++) {
        uint32_t col = water_rgb(y);
        int r = (int)(((col >> 16) & 255) * dim), g = (int)(((col >> 8) & 255) * dim), b = (int)((col & 255) * dim);
        for (int x = 0; x < TANK_W; x++) {
            int a = vig_alpha(x, y);
            if (g_vig) g_vig[y * TANK_W + x] = (uint8_t)a;
            int inv = 256 - a, d = bayer[y & 3][x & 3];
            int rr = (r * inv) >> 8, gg = (g * inv) >> 8, bb = (b * inv) >> 8;
            int r5 = (rr + (d >> 1)) >> 3, g6 = (gg + (d >> 2)) >> 2, b5 = (bb + (d >> 1)) >> 3;
            if (r5 > 31) r5 = 31;
            if (g6 > 63) g6 = 63;
            if (b5 > 31) b5 = 31;
            sc[y * TANK_W + x] = FB_SWAP16((uint16_t)((r5 << 11) | (g6 << 5) | b5));
        }
    }
    /* pebbled bottom: irregular top edge, speckled stones in three tones,
       stable position hash; vignetted in 8-bit before quantising */
    for (int x = 0; x < TANK_W; x++) {
        uint32_t h = (uint32_t)(x * 2654435761u);
        int top = TANK_BOT - 14 - (int)((h >> 8) % 5);
        for (int y = top; y < TANK_H; y++) {
            uint32_t col = floor_rgb(x, y);
            int inv = 256 - (g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y));
            int r = (int)(((col >> 16) & 255) * dim) * inv >> 8;
            int g = (int)(((col >> 8) & 255) * dim) * inv >> 8;
            int b = (int)((col & 255) * dim) * inv >> 8;
            sc[y * TANK_W + x] = FB_SWAP16((uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
        }
    }
    if (g_pk_lx >= 0) pk_halo(sc);                 /* the lantern's light on the water and the floor (the decor lights itself) */
    ctx_t c = ctx_full(sc, TANK_W, dim);
    if (t->night) for (int k = 0; k < g_npc; k++) if (g_pc[k].item == 9 && g_pc[k].baked) vo_halo(sc, g_pc[k].cx);   /* the volcano's embers on the water and the sand (0.3.9) */
    for (int k = 0; k < g_npc; k++)                 /* the pieces, back to front, each recording its mask */
        if (g_pc[k].baked) pc_draw(&c, t, k, true, true);
}

void render_tank(const tank_t *t, uint16_t *fb, int stride) {
    float dim = t->night ? 0.45f : 1.0f;
    ctx_t c = ctx_full(fb, stride, dim);
    int64_t p0 = PROF_MARK();
    bool cached = g_scene && g_dirty && stride == TANK_W;
    g_dirty_hold = false;                          /* the tank draws in the frame's coordinates: its marks count */
    if (cached) memset(g_dirty, 0, TANK_H * DIRTY_WORDS_PER_ROW * sizeof(uint32_t));
    if (g_touched) memset(g_touched, 0, TANK_H * DIRTY_WORDS_PER_ROW * sizeof(uint32_t));   /* this frame's marks begin here */
    struct { short x0, y0, x1, y1; } rects[6 + MAX_FOOD + MAX_BUBBLE + N_FISH_MAX + VEG_BEDS_MAX + SHRIMP_MAX];
    int nr = 0;
#define DYN_RECT(cx0, cy0, cx1, cy1) do { if (cached && nr < (int)(sizeof rects / sizeof rects[0])) { \
        rects[nr].x0 = (short)(cx0); rects[nr].y0 = (short)(cy0); \
        rects[nr].x1 = (short)(cx1); rects[nr].y1 = (short)(cy1); nr++; } } while (0)

    bool front = cached && !setup_is_place();      /* the pieces IN FRONT ride in the scene, but for a placement page's drag */
    bool sgv = false;                              /* the frame is primed from the grass cache (below) */
    pieces_build(t, cached, front);
    int pkx = g_pk_k >= 0 ? g_pc[g_pk_k].cx : -1, pkz = g_pk_k >= 0 ? g_pc[g_pk_k].z : DECOR_Z_BACK;
    bool pk_in_scene = g_pk_k >= 0 && !g_pc[g_pk_k].placing;
    g_pk_lx = t->night && pk_in_scene ? pkx : -1; g_pk_ly = PK_LIGHT_Y;     /* the lantern's light, this frame */
    if (g_pk_lx >= 0) pk_lut_fill();
    { int kc = pc_index(2); g_veg_mask_cx = kc >= 0 && g_pc[kc].z == DECOR_Z_FRONT ? g_pc[kc].cx : -1;   /* the castle IN FRONT: its arch's passes */
      g_pk_under_castle = kc >= 0 && g_pk_k >= 0 && kc > g_pk_k;                                          /* the castle over the lantern: its shadow's rules */
      g_veg_arch_night = t->night;                                                                        /* the back grass in the arch shades itself (veg_put), by arch_shade's rule */
      g_veg_arch_shade = g_veg_mask_cx >= 0 && g_pc[kc].baked && !(t->night && g_pk_under_castle && g_pk_lx >= 0); }   /* (a castle not in the scene - dragged, no cache - has its whole arch shaded at the frame's end instead) */
    g_pk_shadows = g_pk_lx >= 0 && (pkz == DECOR_Z_BACK || g_pk_k < g_npc - 1);                           /* something stands in front of the lantern */
    if (g_pk_shadows) pk_casts(t, pkx); else g_pk_cast_n = 0;
    if (cached) {
        /* the scene's signature: what the bake depends on; a change rebakes */
        scene_sig_t sig; memset(&sig, 0, sizeof sig);
        for (int k = 0; k < g_npc; k++) {
            if (!g_pc[k].baked) continue;
            int n = sig.n++;
            sig.item[n] = g_pc[k].item; sig.cx[n] = g_pc[k].cx; sig.z[n] = g_pc[k].z;
            if (g_pc[k].item == 3) { sig.q[n] = (int)(tank_coral_growth(t) * (CORAL_Q - 0.01f)); sig.col[n] = tank_coral_rgb(t); }
            if (g_pc[k].item == 4) { float lg = tank_cluster_growth(t); sig.q[n] = (int)((lg > 1 ? 1 : lg) * (CL_Q - 0.01f)); sig.col[n] = (uint32_t)tank_cluster_scheme(t); }
        }
        sig.dim = dim; sig.front = front; sig.lx = g_pk_lx;
        if (!g_sig_ok || memcmp(&sig, &g_sig, sizeof sig) || g_scene_dim != dim) {
            g_sig = sig; g_sig_ok = true; g_scene_front = front;
            /* rebuild the static scene with the vignette baked in (the
               per-frame pass then only re-darkens dynamic patches) */
            bake_scene(t, g_scene, dim);
            if (g_vig) g_vig_filled = true;
            g_scene_dim = dim; g_scene_epoch++;
        }
        sgv = front && g_sg_on && g_sg_ok && g_sg_scene_ep == g_scene_epoch;   /* primed with the back grass in it */
        { unsigned sep = sgv ? (0x80000000u | g_sg_ep) : g_scene_epoch;
          if (!(g_primed_fb == fb && g_primed_epoch == sep))
              memcpy(fb, sgv ? g_sceneg : g_scene, TANK_W * TANK_H * sizeof(uint16_t)); }
        g_primed_fb = NULL;
        /* a piece BEHIND that is not in the scene (dragged): live, right over it;
           the castle too, whole, when it is not baked (its front row again after
           the fish, below) */
        for (int k = 0; k < g_npc; k++)
            if (!g_pc[k].baked && (g_pc[k].z == DECOR_Z_BACK || g_pc[k].item == 2)) pc_draw(&c, t, k, true, false);
    } else draw_scene(t, fb, stride, dim);
    /* the overlays of the pieces BEHIND: a crown over its baked fan, the
       candle's face over the lantern's body - under the grass */
    if (sgv) g_under = g_bgmask;                     /* the grass is in the frame already: these stay under it */
    for (int k = 0; k < g_npc; k++) {
        if (g_pc[k].z != DECOR_Z_BACK) continue;
        g_clip_k = k;
        if (g_pc[k].item == 3 || g_pc[k].item == 4) PC_CROWN(k);
        else if (g_pc[k].item == 8 && g_pc[k].baked) draw_pumpkin_glow(&c, g_pc[k].cx, t->clock, t->night, false, true);
        else if (g_pc[k].item == 9) PC_VOLCANO(k);
        g_clip_k = -1;
    }
    g_under = NULL;
    PROF_ADD(0, p0);

    PROF_ADD(1, p0);   /* stage 1 (light shafts) retired 2026-09-01 */
    /* vegetation, BACK layer: the reef bed (left - milestone lushness widens
       its base, never withers) and two decor beds; all three keep growing up
       and out with tank_t.veg_growth until the keeper trims them (slash the
       canopy). Geometry comes from tank_veg_bed so physics and pixels agree.
       The alternating FRONT fronds draw after the fish, below. Each bed's
       spans skip the pieces above it (veg_skip_for), and a bed in front of
       the lantern is backlit (g_pk_shade_all). */
    const int *veg_seed = VEG_SEED;
#define VEG_PASS(b, layer) do { g_veg_skip = veg_skip_for(t, (b)); \
        g_pk_shade_all = g_pk_lx >= 0 && ((b) == 3 ? tank_decor_above(t, 0, 8) : pkz == DECOR_Z_BACK); \
        draw_veg(&c, t, (b), veg_seed[(b)], (layer), cached); g_pk_shade_all = false; } while (0)
    if (!sgv) {                                      /* (the grass cache holds them: the frame was primed with them) */
        for (int b = 0; b < tank_veg_beds(t); b++)      /* a BACK-layer piece first: behind the grass too */
            if (bed_z(t, b) == DECOR_Z_BACK) VEG_PASS(b, -1);
        for (int b = 0; b < tank_veg_beds(t); b++)
            if (bed_z(t, b) == DECOR_Z_MIDDLE) VEG_PASS(b, 0);
    }
    /* THE CRAB'S PLACES in the frame (2026-10-24, Strato on the 1.8 and the square: "if the crab is meant to be
       inside the cave or castle, nothing visible should really be behind it"):
       - cr_deep: in or on its way into a shelter (ROOST, SHELTER) it is the DEEPEST thing on the sand - drawn here,
         before the shrimp and the fish, under the snail and the urchin, under the front fronds. A castle at either
         depth works (its arch is open: the front row restores over the wall, the crab shows in the opening) and a
         volcano BEHIND (baked in the scene);
       - cr_slot: a volcano IN FRONT (its cave is painted rock: the restore would cover it) and the CLIMB on one, and
         its front LANE on the sand (tank.h THE LANES; a dig is in front whatever the lane) - the front slot after
         the pieces IN FRONT are restored, FIRST in it so the snail and the urchin in their front lane pass in front
         of it (Strato, the 1.8), under the front fronds (which keep off the volcano's silhouette anyway); the
         volcano's per-frame lava and embers keep off the crab (g_guard in vo_store - Strato, the pendant);
       - cr_top: a sand dollar held up - over the fronds, so the keeper sees it wherever it dug;
       - else the critter pass with the snail and the urchin (its back lane on the sand). */
    const bool cr_in = (t->sd_unlocks & SD_ITEM_CRAB) && t->cr_x >= 0;
    const bool cr_vol_front = cr_in && tank_decor_in(t, 9) && tank_decor_z(t, 9) == DECOR_Z_FRONT
                              && (t->cr_mode == CR_CLIMB || (t->cr_cave == 9 && (t->cr_mode == CR_ROOST || t->cr_mode == CR_SHELTER)));
    const bool cr_deep = cr_in && t->cr_find <= 0 && !cr_vol_front && (t->cr_mode == CR_ROOST || t->cr_mode == CR_SHELTER);
    /* ... and the deep crab follows its SHELTER'S DEPTH against the fronds (Strato 2026-10-24, the watch: the castle IN
       FRONT, the crab in its arch painted behind the fronds seen through it - "the castle is different in that you can
       see all the way to the back fronds through the arch"): a shelter BEHIND stands behind the fronds, so the crab in
       it is under every frond (drawn before the arch pass, the back fronds painted over it again); a castle IN FRONT
       stands before them, so the crab in its arch is over the fronds in the opening - drawn after the arch pass, still
       before the shrimp and the fish, and the castle's front row restores over it where it meets the wall */
    const bool cr_deep_front = cr_deep && t->cr_cave >= 0 && tank_decor_in(t, t->cr_cave) && tank_decor_z(t, t->cr_cave) == DECOR_Z_FRONT;
    const bool cr_deep_back = cr_deep && !cr_deep_front;
    bool cr_top = false, cr_slot = false;                        /* (decided with the lanes, below: with a page up or on the bare path the
                                                                    front volcano's crab goes to the top lane, after the live piece) */
    if (cr_deep_back) {
        draw_crab(&c, t);                                        /* (its rect is marked in the critter pass) */
        /* THE WEAVE kept (Strato 2026-10-24, the square: one frond still drawn behind the crab in the cave): the
           back fronds - a MIDDLE bed's even ones, a BACK bed's all - again over its rect, after it, not final (the
           sweep vignettes them with the rect), so no frond of any bed sits behind a crab in its cave. Not with the
           grass cache on: there the crab is drawn under the cached grass to begin with. */
        if (!sgv) {
            const int feet = (int)floorf(CR_FLOOR_Y - t->cr_y);
            const int wx0 = (int)t->cr_x - 28, wy0 = feet - CR_GH - 8, wx1 = (int)t->cr_x + 28, wy1 = feet + 1;
            ctx_t cw = c; cw.fb = &CTX_PX(&c, wx0, wy0); cw.ox = wx0; cw.oy = wy0; cw.w = wx1 - wx0 + 1; cw.h = wy1 - wy0 + 1;
            for (int b = 0; b < tank_veg_beds(t); b++) {
                float bx0, bx1; tank_veg_bed(t, b, &bx0, &bx1, NULL, NULL);
                int z = bed_z(t, b);
                if (z == DECOR_Z_FRONT || bx1 + 14 < wx0 || bx0 - 14 > wx1) continue;
                g_veg_skip = veg_skip_for(t, b);
                g_pk_shade_all = g_pk_lx >= 0 && (b == 3 ? tank_decor_above(t, 0, 8) : pkz == DECOR_Z_BACK);
                draw_veg(&cw, t, b, veg_seed[b], z == DECOR_Z_BACK ? -1 : 0, false);
                g_pk_shade_all = false;
            }
        }
    }
        /* no DYN_RECT: with the scene cache each frond span vignettes itself */
    if (g_veg_mask_cx >= 0) {                        /* a castle IN FRONT: inside its open arch the late fronds go before the fish */
        DYN_RECT(g_veg_mask_cx - CASTLE_ARCH_R, CASTLE_FY - CASTLE_ARCH_S - CASTLE_ARCH_R, g_veg_mask_cx + CASTLE_ARCH_R, CASTLE_FY);   /* (those are marked, not final: the sweep vignettes them) */
        g_veg_arch_pass = -1;
        for (int b = 0; b < tank_veg_beds(t); b++)
            if (bed_z(t, b) == DECOR_Z_MIDDLE) VEG_PASS(b, 1);     /* (a bed placed IN FRONT stands BEFORE the castle: no arch pass, no tunnel shade - the late pass draws it whole) */
        g_veg_arch_pass = 0;
    }
    PROF_ADD(2, p0);
    /* the airstone the column rises from, on the floor where the keeper put
       it (setup): three stones and a glint - dynamic, since it can move */
    {
        float ax = t->bubble_x, ay = TANK_BOT - 17;
        fill_ellipse(&c, ax - 6, ay + 1, 7, 4, 0x2a3634, 255);
        fill_ellipse(&c, ax + 5, ay + 2, 6, 3.5f, 0x22302c, 255);
        fill_ellipse(&c, ax, ay - 2, 6, 3.5f, 0x3a4a48, 255);
        fill_ellipse(&c, ax - 1, ay - 3, 2, 1.2f, 0x5a6a68, 200);
        DYN_RECT((int)ax - 14, (int)ay - 7, (int)ax + 12, (int)ay + 7);
    }
    /* food pellets */
    for (int i = 0; i < MAX_FOOD; i++)
        if (t->food[i].alive) {
            fill_ellipse(&c, t->food[i].x, t->food[i].y, 2.6f, 2.6f, 0xffbd59, 255);
            fill_ellipse(&c, t->food[i].x - 0.8f, t->food[i].y - 0.8f, 1.0f, 1.0f, 0xffe9bd, 255);
            DYN_RECT((int)t->food[i].x - 5, (int)t->food[i].y - 5, (int)t->food[i].x + 5, (int)t->food[i].y + 5);
        }
    /* bubbles */
    for (int i = 0; i < MAX_BUBBLE; i++) {
        const bubble_t *b = &t->bubble[i];
        float r = b->column ? 2.6f : 1.8f;
        fill_ellipse(&c, b->x, b->y, r, r, 0x9fd8e2, 60);
        px_blend(&c, (int)(b->x - r * 0.4f), (int)(b->y - r * 0.4f), 0xffffff, 120);
        DYN_RECT((int)b->x - 5, (int)b->y - 5, (int)b->x + 5, (int)b->y + 5);
    }
    if (cr_deep_front) draw_crab(&c, t);                         /* in a castle IN FRONT's arch: over the fronds seen through it, under the shrimp and the fish (its rect is marked in the critter pass) */
    /* the shrimp school: on the sand and in the grass, under the fish and
       the front fronds (the grass is their cover) */
    if (t->sd_unlocks & SD_ITEM_SHRIMP) {
        src_t pal[SHRIMP_TONES];
        for (int k = 0; k < SHRIMP_TONES; k++) pal[k] = src_color(SHRIMP_RGB[k], c.dim);
        for (int i = 0; i < t->shrimp_n; i++) {
            draw_shrimp(&c, &t->shrimp[i], pal);
            DYN_RECT((int)t->shrimp[i].x - 10, (int)t->shrimp[i].y - 5, (int)t->shrimp[i].x + 10, (int)t->shrimp[i].y + 5);
        }
    }
    /* the seahorse's BACK lane, before the fish (2026-10-17, Strato: a fish
       passed between the seahorse and the castle it held): it holds something
       behind them - an even frond, a piece BEHIND - so the fish pass in front
       of it, as they pass its hold. Its FRONT lane (an odd frond, a piece IN
       FRONT) is drawn after the pieces in front, below. Its rect either way. */
    if ((t->sd_unlocks & SD_ITEM_SEAHORSE) && t->sh_x >= 0) {
        if (!t->sh_front) draw_seahorse(&c, t);
        int bx0, by0, bx1, by1; seahorse_box(t, &bx0, &by0, &bx1, &by1);
        DYN_RECT(bx0, by0, bx1, by1);
    }
    PROF_ADD(3, p0);
    /* fish */
    for (int i = 0; i < t->n_fish; i++) {
        g_bb_on = true; g_bb_x0 = g_bb_y0 = 1 << 20; g_bb_x1 = g_bb_y1 = -1;
        draw_fish(&c, t, &t->fish[i]);
        g_bb_on = false;
        if (g_bb_x1 >= g_bb_x0) DYN_RECT(g_bb_x0, g_bb_y0, g_bb_x1, g_bb_y1);
    }
    PROF_ADD(7, p0);                                 /* the split of the stage below (2026-10-23): the fish */
    /* the snail on the floor (upright): in the scene, under the front fronds -
       unless it walks its front lane (tank_t.snail_front), drawn below, over
       the pieces placed IN FRONT */
    /* THE LANES' front slot holds only while every piece IN FRONT is in the scene. With a placement page up (a
       piece dragged live, over everything) - or on the bare path, which bakes nothing - a critter in its front
       lane goes to the TOP lane instead, after the live pieces, so it stays before them as it was before the
       baked ones (Strato 2026-10-24, the 1.8: the crab "dropped to the layer below behind the volcano when
       opening the move menu"); over the fronds for as long as the page is up */
    bool lanes = true;
    for (int k = 0; k < g_npc; k++) if (g_pc[k].z == DECOR_Z_FRONT && !g_pc[k].baked) lanes = false;
    const bool snail_lane = lanes && t->snail_front, urchin_lane = lanes && t->urchin_front;
    const bool snail_top = !lanes && t->snail_front, urchin_top = !lanes && t->urchin_front;
    /* THE STACK of the sand's critters (Strato 2026-10-24, the square: a crab backed into the volcano's cave drew over
       the urchin passing it): the CRAB lowest, then the snail, then the urchin - whatever walks past a crab in its
       cave, or a crab resting on the sand, is in front of it. The same order in the front slot below. */
    /* the strawberry crab on the floor (0.3.9): its places are decided above the fish (THE CRAB'S PLACES); here, with
       the snail and the urchin, only its back lane on the sand */
    const bool cr_lane_case = (t->cr_lane_front || t->cr_mode == CR_DIG) && t->cr_mode != CR_CLIMB && t->cr_mode != CR_ROOST && t->cr_mode != CR_SHELTER;
    cr_top = cr_in && (t->cr_find > 0 || (!lanes && (cr_vol_front || cr_lane_case)));   /* a page up / the bare path: the live pieces are drawn over everything, so a crab before one goes after them */
    cr_slot = cr_in && !cr_top && lanes && (cr_vol_front || cr_lane_case);
    if (cr_in) {
        if (!cr_top && !cr_deep && !cr_slot) draw_crab(&c, t);
        const int feet = (int)floorf(CR_FLOOR_Y - t->cr_y);
        if (t->cr_tilt == 0) DYN_RECT((int)t->cr_x - 27, feet - CR_GH - (t->cr_find > 0 ? (int)(CR_DISC_UP + CR_DISC_R) + 1 : 0), (int)t->cr_x + 27, feet + 1);   /* (+ the sand dollar over its head) */
        else DYN_RECT((int)t->cr_x - 36, feet - 35, (int)t->cr_x + 36, feet + 36);
    }
    if (tank_snail_upright(t)) {
        if (!snail_lane && !snail_top) draw_snail(&c, t, true);                                    /* (its front lane: the slot below, or the top lane with a page up) */
        DYN_RECT((int)t->snail_x - 21, (int)t->snail_y - 13, (int)t->snail_x + 21, (int)t->snail_y + 7);
    }
    /* the urchin on the floor: with the fish, under the front fronds - at a
       bed's foot the grass weaves over it */
    if ((t->sd_unlocks & SD_ITEM_URCHIN) && t->urchin_x >= 0) {
        if (!urchin_lane && !urchin_top) draw_urchin(&c, t);                                        /* (its front lane: the slot below, or the top lane with a page up) */
        DYN_RECT((int)t->urchin_x - 19, (int)URCHIN_FLOOR_Y - 17, (int)t->urchin_x + 19, (int)(URCHIN_FLOOR_Y + UR_BASE + t->urchin_dy) + 1);
    }
    PROF_ADD(8, p0);                                 /* the snail, the urchin and the crab */
    /* the pieces IN FRONT back over the fish (and the snail, the food, the
       bubbles), only where they were drawn: the scene holds them. The castle's
       front row comes back the same way - a fish in the arch swims THROUGH,
       its keep stays behind the fish. A castle IN FRONT that is not in the
       scene (dragged, or a page up): its front row again, live. */
    for (int k = 0; k < g_npc; k++) {
        if (g_pc[k].z != DECOR_Z_FRONT) continue;
        if (g_pc[k].baked) pc_restore(fb, k);
        else if (g_pc[k].item == 2) draw_castle(&c, g_pc[k].cx, 1, cached);
    }
    PROF_ADD(9, p0);                                 /* the pieces IN FRONT back over them */
    /* THE FRONT SLOT (2026-10-24, a keeper's ask): the sand's critters in their front lane - over the pieces IN
       FRONT just restored, under the front fronds that follow (woven through the grass like a fish). Their rects
       are marked above. The snail's lane lived above the fronds until today. */
    if (cr_slot) draw_crab(&c, t);                                                                 /* the crab lowest (the stack above) */
    if (tank_snail_upright(t) && snail_lane) draw_snail(&c, t, true);
    if ((t->sd_unlocks & SD_ITEM_URCHIN) && t->urchin_x >= 0 && urchin_lane) draw_urchin(&c, t);
    /* vegetation, FRONT layer: the alternating fronds drawn over the fish,
       so a fish inside a canopy is woven through it (each span applies its
       own vignette and untags itself, so the fish rects below skip it) */
    if (g_veg_mask_cx >= 0) g_veg_arch_pass = 1;     /* (the arch's pixels were drawn before the fish) */
    for (int b = 0; b < tank_veg_beds(t); b++)
        if (bed_z(t, b) == DECOR_Z_MIDDLE) VEG_PASS(b, 1);
    g_veg_arch_pass = 0;                             /* a bed placed IN FRONT last, WHOLE: over the grass, the fish, the critters and every piece it is
                                                        ranked above - the castle's arch included, and its fronds there take no tunnel shade (2026-10-24,
                                                        Strato on the 1.8: the sword plant IN FRONT drawn behind the crab in the castle, and tinted) */
    { const bool shade = g_veg_arch_shade; g_veg_arch_shade = false;
      for (int b = 0; b < tank_veg_beds(t); b++)
          if (bed_z(t, b) == DECOR_Z_FRONT) VEG_PASS(b, -1);
      g_veg_arch_shade = shade; }
#undef VEG_PASS
    PROF_ADD(10, p0);                                /* the front grass */
    /* the pieces IN FRONT that are not in the scene (a placement page is up,
       or no scene cache): live, in order, over everything; then every piece
       IN FRONT's overlay - its crown, the candle - clipped by the pieces
       above it */
    for (int k = 0; k < g_npc; k++)
        if (g_pc[k].z == DECOR_Z_FRONT && !g_pc[k].baked && g_pc[k].item != 2) pc_draw(&c, t, k, cached, false);
    /* a castle IN FRONT ranked above other pieces IN FRONT, none of them in the
       scene (a placement page is up, or no cache): the pass above drew those
       over its keep and its front row. The castle again, whole, inside each
       one's box - the keep over them, as the scene has it. The fish there were
       under them already, and the arch's opening is not painted, so nothing
       else moves (0.3.9, Strato: "when I tap and hold the castle, it falls
       behind the volcano visually even when it shows IN FRONT OF THE VOLCANO;
       once I press DONE it snaps back") */
    { int kc = pc_index(2);
      if (kc >= 0 && g_pc[kc].z == DECOR_Z_FRONT && !g_pc[kc].baked)
          for (int j = 0; j < kc; j++) {
              const piece_t *a = &g_pc[j], *b = &g_pc[kc];
              if (a->z != DECOR_Z_FRONT || a->baked) continue;
              int x0 = a->x0 > b->x0 ? a->x0 : b->x0, x1 = (a->x0 + a->w < b->x0 + b->w ? a->x0 + a->w : b->x0 + b->w) - 1;
              int y0 = a->y0 > b->y0 ? a->y0 : b->y0, y1 = (a->y0 + a->rows < b->y0 + b->rows ? a->y0 + a->rows : b->y0 + b->rows) - 1;
              if (x0 > x1 || y0 > y1) continue;
              g_cst_clip = true; g_cst_clip_x0 = x0; g_cst_clip_y0 = y0; g_cst_clip_x1 = x1; g_cst_clip_y1 = y1;
              draw_castle(&c, b->cx, 0, cached);
              g_cst_clip = false;
          }
    }
    g_guard_who = (cr_slot ? GUARD_CRAB : 0u) | (tank_snail_upright(t) && snail_lane ? GUARD_SNAIL : 0u)
                | ((t->sd_unlocks & SD_ITEM_URCHIN) && t->urchin_x >= 0 && urchin_lane ? GUARD_URCHIN : 0u);
    g_guard = g_guard_who ? t : NULL;                        /* the front slot's critters - the crab in the front volcano's cave, on its flank, or digging / resting /
                                                                  walking before it in its front lane; the snail and the urchin in theirs: the lava's cells keep off them
                                                                  (Strato, the 1.8: a dig at the cave mouth; 0.3.9.1, the urchin walking past the cave) */
    for (int k = 0; k < g_npc; k++) {
        if (g_pc[k].z != DECOR_Z_FRONT) continue;
        g_clip_k = k;
        if (g_pc[k].item == 3 || g_pc[k].item == 4) PC_CROWN(k);
        else if (g_pc[k].item == 8 && g_pc[k].baked) draw_pumpkin_glow(&c, g_pc[k].cx, t->clock, t->night, false, true);
        else if (g_pc[k].item == 9) PC_VOLCANO(k);
        g_clip_k = -1;
    }
    g_guard = NULL; g_guard_who = 0;
    PROF_ADD(11, p0);                                /* the front pieces' overlays (crowns, the candle, the lava) */
    if ((t->sd_unlocks & SD_ITEM_SEAHORSE) && t->sh_x >= 0 && t->sh_front) draw_seahorse(&c, t);   /* the seahorse's front lane (its rect is marked above) */
    if (cr_top) draw_crab(&c, t);                                                                      /* the crab holding a find up; a page up: its front lane (its rect is marked above) */
    if (tank_snail_upright(t) && snail_top) draw_snail(&c, t, true);                                   /* a page up: the snail's and the urchin's front lane, after the live pieces */
    if ((t->sd_unlocks & SD_ITEM_URCHIN) && t->urchin_x >= 0 && urchin_top) draw_urchin(&c, t);
    PROF_ADD(12, p0);                                /* the seahorse's front lane, the crab's top lane */
    { int kc = pc_index(2); if (kc >= 0 && g_pc[kc].z == DECOR_Z_FRONT) { if (g_pc[kc].baked && cached) arch_shade_dirty(&c, kc, g_pc[kc].cx, t->night); else arch_shade(&c, kc, g_pc[kc].cx, t->night, true); } }   /* the tunnel through a castle IN FRONT: what passed it this frame (the scene carries the rest) */
    PROF_ADD(13, p0);                                /* the arch's shade */
    PROF_ADD(4, p0);
    /* porthole vignette: darken corners toward AMOLED black. With a scene
       cache the full-frame pass is baked into the scene and only the dynamic
       patches are re-darkened; without one, the classic per-pixel pass runs. */
    if (cached && g_pk_lx >= 0) pk_light_dirty(&c);   /* the lantern lights what moved near it (before its vignette) */
    if (cached) {
        /* one row sweep over the union of the dynamic rects: pixels marked
           in the dirty mask were drawn this frame (and not already vignetted
           inline) and get the vignette re-applied exactly once. */
        for (int y = 0; y < TANK_H; y++) {
            float dyf = (y - TANK_H * 0.5f) / (TANK_H * 0.5f);
            float rem = 0.72f - dyf * dyf;
            int x_in = rem > 0 ? (int)(TANK_W * 0.5f * (1 - sqrtf(rem))) : TANK_W / 2;
            if (x_in <= 0) continue;
            short iv[sizeof rects / sizeof rects[0]][2]; int ni = 0;
            for (int i = 0; i < nr; i++)
                if (y >= rects[i].y0 && y <= rects[i].y1) {
                    int a0 = rects[i].x0 < 0 ? 0 : rects[i].x0;
                    int a1 = rects[i].x1 >= TANK_W ? TANK_W - 1 : rects[i].x1;
                    if (a0 > a1) continue;
                    int j = ni++;                       /* insertion sort by x0 */
                    while (j > 0 && iv[j - 1][0] > a0) { iv[j][0] = iv[j - 1][0]; iv[j][1] = iv[j - 1][1]; j--; }
                    iv[j][0] = (short)a0; iv[j][1] = (short)a1;
                }
            int end = -1;                               /* merged sweep */
            for (int i = 0; i < ni; i++) {
                int a0 = iv[i][0] > end + 1 ? iv[i][0] : end + 1;
                int a1 = iv[i][1];
                if (a1 > end) end = a1;
                for (int s = 0; s < 2; s++) {           /* clip to the two ring spans */
                    int r0 = s ? (TANK_W - x_in > a0 ? TANK_W - x_in : a0) : a0;
                    int r1 = s ? a1 : (x_in - 1 < a1 ? x_in - 1 : a1);
                    const uint32_t *drow = g_dirty + y * DIRTY_WORDS_PER_ROW;
                    for (int x = r0; x <= r1; x++) {
                        uint32_t w = drow[x >> 5] >> (x & 31);
                        if (!w) { x |= 31; continue; }         /* nothing else in this word */
                        if (!(w & 1)) continue;
                        uint16_t *p = &c.fb[y * c.stride + x];
                        int a = (g_vig && g_vig_filled) ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
                        if (a) px_darken(p, a);
                    }
                }
            }
        }
    } else {
        for (int y = 0; y < TANK_H; y++) {
            float dy = (y - TANK_H * 0.5f) / (TANK_H * 0.5f);
            float rem = 0.72f - dy * dy;
            int x_in = rem > 0 ? (int)(TANK_W * 0.5f * (1 - sqrtf(rem))) : TANK_W / 2;
            for (int side = 0; side < 2; side++)
                for (int k = 0; k < x_in; k++) {
                    int x = side ? TANK_W - 1 - k : k;
                    float dx = (x - TANK_W * 0.5f) / (TANK_W * 0.5f);
                    float d2 = dx * dx + dy * dy;
                    if (d2 > 0.72f) {
                        int a = (int)((d2 - 0.72f) * 220);
                        if (a > 0) px_blend(&c, x, y, 0x000000, a > 255 ? 255 : a);
                    }
                }
        }
    }
    /* algae film sits ON the glass - over the water, the fish, even the
     * vignette (which is why it draws after the re-darken pass: nothing
     * behind it needs repair, and next frame's scene restore erases wiped
     * cells for free) */
    PROF_ADD(5, p0);
    draw_algae(&c, t);
    draw_snail(&c, t, false);                    /* on the glass, over the film */
    /* the grass cache: rebuilt when it is stale (a rebake, the first frame) or
       its period is up - the rows the old grass reached copied back from the
       scene, the back grass painted in, its pixels recorded. The prefetch of
       the next frame starts after this, so it copies the new cache. */
    if (front && g_sg_on && (!sgv || t->clock - g_sg_clock >= g_sg_period || t->clock < g_sg_clock)) {
        int64_t q0 = render_clock_us ? render_clock_us() : 0;
        bool whole = !g_sg_ok || g_sg_scene_ep != g_scene_epoch;
        int top = whole ? 0 : (g_sg_top < 0 ? 0 : g_sg_top);
        if (whole) memcpy(g_sceneg, g_scene, (size_t)TANK_W * TANK_H * sizeof(uint16_t));
        else render_restore_touched(g_sceneg, g_bgmask);   /* only the words the old grass covered, back from the scene (a whole copy of its rows
                                                              cost half of a rebuild's 24 ms on the square) */
        memset(g_bgmask + top * DIRTY_WORDS_PER_ROW, 0, (size_t)(TANK_H - top) * DIRTY_WORDS_PER_ROW * 4);
        uint32_t *keep_t = g_touched; g_touched = NULL;
        ctx_t cg = ctx_full(g_sceneg, TANK_W, dim);
        g_sg_top = TANK_H; g_bg_rec = true; g_veg_arch_pass = 0;
#define SG_PASS(b, layer) do { g_veg_skip = veg_skip_for(t, (b)); \
        g_pk_shade_all = g_pk_lx >= 0 && ((b) == 3 ? tank_decor_above(t, 0, 8) : pkz == DECOR_Z_BACK); \
        draw_veg(&cg, t, (b), VEG_SEED[(b)], (layer), true); g_pk_shade_all = false; } while (0)
        for (int b = 0; b < tank_veg_beds(t); b++) if (bed_z(t, b) == DECOR_Z_BACK) SG_PASS(b, -1);
        for (int b = 0; b < tank_veg_beds(t); b++) if (bed_z(t, b) == DECOR_Z_MIDDLE) SG_PASS(b, 0);
#undef SG_PASS
        g_bg_rec = false; g_touched = keep_t;
        if (g_sg_top == TANK_H) g_sg_top = TANK_H - 1;
        g_sg_ok = true; g_sg_scene_ep = g_scene_epoch; g_sg_clock = t->clock; g_sg_ep = (g_sg_ep + 1) & 0x7fffffffu;
        if (render_clock_us) { g_sg_us += render_clock_us() - q0; g_sg_n++; }
    }
    g_pk_lx = -1; g_pk_under_castle = false; g_pk_shadows = false;   /* the light is this frame's: no page is ever lit by it */
    PROF_ADD(6, p0);
#undef DYN_RECT
}

/* ---- the battery (2026-09-24 redraw): Strato is color blind and could not
 * tell the charging pill (a teal fill) from the full one (green). Now the
 * cable shows as a SHAPE - a lightning bolt left of the pill - and flowing
 * charge as MOTION - a bright band sweeping the fill; the hue only repeats
 * it. On battery the fill wears the level colors; on the cable it is the
 * calm green whatever the level (a red sliver on the charger is no alarm).
 * The info page draws the same battery large. ---- */

/* the bolt, a polygon in a 10 x 16 box: the flat top, the stroke down to
   the left, the jog across, the tail to the point */
static const float BOLT_X[7] = { 6.0f, 0.5f, 4.0f, 2.5f, 10.0f, 6.5f, 10.0f };
static const float BOLT_Y[7] = { 0.0f, 9.5f, 9.5f, 16.0f, 5.5f, 5.5f, 0.0f };
/* filled with 4 sub-rows of exact span coverage per pixel row, so it stays a
   bolt at the pill's 15 px (no stair-stepped mush) and the page's 38 */
static void bolt_fill(ctx_t *c, float ox, float oy, float s, uint32_t rgb, int alpha) {
    src_t col = src_color(rgb, 1.0f);
    int x0 = (int)floorf(ox), w = (int)ceilf(10.0f * s) + 2;
    float cov[64];
    if (w > 64) w = 64;
    for (int y = (int)floorf(oy); y <= (int)ceilf(oy + 16.0f * s); y++) {
        memset(cov, 0, sizeof cov);
        for (int sub = 0; sub < 4; sub++) {
            float yy = (y + (sub + 0.5f) * 0.25f - oy) / s, xs[8]; int n = 0;
            for (int i = 0, j = 6; i < 7; j = i++)
                if ((BOLT_Y[i] > yy) != (BOLT_Y[j] > yy))
                    xs[n++] = ox + s * (BOLT_X[j] + (yy - BOLT_Y[j]) * (BOLT_X[i] - BOLT_X[j]) / (BOLT_Y[i] - BOLT_Y[j]));
            for (int i = 1; i < n; i++) for (int k = i; k > 0 && xs[k] < xs[k - 1]; k--) { float t = xs[k]; xs[k] = xs[k - 1]; xs[k - 1] = t; }
            for (int k = 0; k + 1 < n; k += 2)                   /* each inside interval, spread over the pixels it crosses */
                for (int px_ = (int)floorf(xs[k]); px_ <= (int)floorf(xs[k + 1]); px_++) {
                    float a = fmaxf(xs[k], (float)px_), b = fminf(xs[k + 1], px_ + 1.0f);
                    if (b > a && px_ - x0 >= 0 && px_ - x0 < w) cov[px_ - x0] += (b - a) * 0.25f;
                }
        }
        for (int i = 0; i < w; i++)
            if (cov[i] > 0.02f) px_blend_s(c, x0 + i, y, &col, (int)(fminf(cov[i], 1.0f) * alpha));
    }
}
/* the bolt h px tall at (x, y), with a dark rim so it reads over bright water */
static void draw_bolt(ctx_t *c, float x, float y, float h, uint32_t rgb) {
    float s = h / 16.0f;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy) bolt_fill(c, x + dx, y + dy, s, 0x04141a, 200);
    bolt_fill(c, x, y, s, rgb, 255);
}
static float bolt_w(float h) { return 10.0f * h / 16.0f; }
static int bolt_room(int H) { return (int)bolt_w(H * 1.25f) + H / 4 + 2; }   /* the bolt beside a battery H tall, and its gap */

/* a battery at (X, Y), body W x H, `line` px of outline, the nub on the right;
   on the cable the bolt stands left of it (the caller leaves the room) */
static void draw_battery(ctx_t *c, int X, int Y, int W, int H, int line, float frac, int state, float clock) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    bool pw = BAT_ON_POWER(state);
    uint32_t col = pw ? 0x78d67d : frac < 0.2f ? 0xf25b65 : frac < 0.45f ? 0xffbd59 : 0x78d67d;
    uint32_t edge = pw ? 0xdfeef0 : 0x9fb4b8;                  /* on the cable the rim brightens too */
    src_t bg = src_color(0x04141a, 1.0f), e = src_color(edge, 1.0f), f = src_color(col, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 215);
    for (int k = 0; k < line; k++) {
        span(c, X, X + W - 1, Y + k, &e, 255); span(c, X, X + W - 1, Y + H - 1 - k, &e, 255);
        for (int y = Y; y < Y + H; y++) { px(c, X + k, y, e.v); px(c, X + W - 1 - k, y, e.v); }
    }
    int nh = H * 2 / 5, nw = line + 2;                         /* the nub */
    for (int y = Y + (H - nh) / 2; y < Y + (H + nh) / 2; y++) span(c, X + W, X + W + nw - 1, y, &e, 255);
    int in = line + 1, iw = W - 2 * in, fw = (int)(iw * frac + 0.5f);
    if (pw && fw < 2) fw = 2;
    for (int y = Y + in; y < Y + H - in; y++) if (fw > 0) span(c, X + in, X + in + fw - 1, y, &f, 255);
    if (state == BAT_CHARGING && fw > 0) {                     /* the sweep: charge flowing in, left to right */
        float ph = fmodf(clock, 1.8f) / 1.3f;
        if (ph <= 1.0f) {
            float bw = fmaxf(4.0f, iw / 5.0f), cx = X + in - bw + ph * (fw + 2 * bw);
            src_t wh = src_color(0xffffff, 1.0f);
            for (int x = X + in; x < X + in + fw; x++) {
                float d = fabsf(x + 0.5f - cx) / bw;
                if (d < 1) for (int y = Y + in; y < Y + H - in; y++) px_blend_s(c, x, y, &wh, (int)(170 * (1 - d)));
            }
        }
    }
    if (pw) draw_bolt(c, X - bolt_room(H), Y - H / 8.0f, H * 1.25f, state == BAT_PLUGGED ? 0x9fb4b8 : 0xffffff);   /* a touch taller than the battery */
}

/* the pill, top right (render.h) */
void render_battery(uint16_t *fb, int stride, float frac, int state, float clock) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    draw_battery(&c, RENDER_BAT_X, RENDER_BAT_Y, RENDER_BAT_W, RENDER_BAT_H, 1, frac, state, clock);
}

/* ---- stats overlay (selection ring + visual card) ---- */

static void ring(ctx_t *c, float cx, float cy, float r, uint32_t rgb) {
    for (int i = 0; i < 64; i++) {
        float a = i * (TAU / 64);
        px_blend(c, (int)(cx + cosf(a) * r), (int)(cy + sinf(a) * r), rgb, 180);
    }
}

/* blend one native RGB565 pixel (icon art is pre-colored; no dim - the card
 * ignores night, matching px_blend's use with c->dim = 1) */
static void px565_blend(ctx_t *c, int x, int y, uint16_t v, int a) {
    if (!CTX_IN(c, x, y)) return;
    uint16_t *p = &CTX_PX(c, x, y), cur = FB_SWAP16(*p);
    int r = (cur >> 11)       + (((v >> 11)       - (cur >> 11))       * a >> 8);
    int g = ((cur >> 5) & 63) + ((((v >> 5) & 63) - ((cur >> 5) & 63)) * a >> 8);
    int b = (cur & 31)        + (((v & 31)        - (cur & 31))        * a >> 8);
    *p = FB_SWAP16((uint16_t)((r << 11) | (g << 5) | b)); TOUCH_PX(c, x, y);
}

/* draw a baked icon (icons.h, generated from Strato's pixel art) at x,y.
 * alpha scales the icon's own alpha plane: 255 = as drawn, lower = dimmed
 * (unrevealed traits, torn thought bubbles). */
static void blit_icon(ctx_t *c, int x, int y, const icon_t *ic, int alpha) {
    for (int j = 0; j < ic->h; j++)
        for (int i = 0; i < ic->w; i++) {
            int a = ic->a[j * ic->w + i];
            if (!a) continue;
            px565_blend(c, x + i, y + j, ic->rgb[j * ic->w + i], a * alpha >> 8);
        }
}

/* a segmented meter: value 0..1 over `segs` segments of `sw` x 10 px. The
 * last partial segment fills proportionally, so it still reads analog up
 * close but "3 of 5" at a glance. */
static void meter(ctx_t *c, int x, int y, int segs, int sw, float frac, uint32_t rgb) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    float per = 1.0f / segs;
    src_t on = src_color(rgb, c->dim), off = src_color(0x2a3f45, c->dim);
    for (int s = 0; s < segs; s++) {
        int sx = x + s * (sw + 2);
        float have = (frac - s * per) / per;                   /* 0..1 of this segment */
        int fw = have >= 1 ? sw : have <= 0 ? 0 : (int)(have * sw + 0.5f);
        for (int yy = 0; yy < 10; yy++) {
            if (fw > 0)  span(c, sx, sx + fw - 1, y + yy, &on, 235);
            if (fw < sw) span(c, sx + fw, sx + sw - 1, y + yy, &off, 150);
        }
    }
}

/* a trait spectrum: pole icons at both ends, the fish sits at `frac` between
 * them. Unrevealed = dimmed poles, dashed line, a "?" instead of the dot -
 * you learn who a fish is by watching it, not by reading it. */
static void slider(ctx_t *c, int x, int y, int w, float frac, uint32_t rgb,
                   const icon_t *lo, const icon_t *hi, bool revealed) {
    blit_icon(c, x, y, lo, revealed ? 255 : 70);
    blit_icon(c, x + w - 16, y, hi, revealed ? 255 : 70);
    int lx = x + 20, lw = w - 40, ly = y + 8;                  /* the line between poles */
    for (int xx = 0; xx < lw; xx++)
        if (revealed || (xx & 4))
            for (int yy = -1; yy <= 0; yy++) px_blend(c, lx + xx, ly + yy, 0x2a3f45, revealed ? 200 : 110);
    if (revealed) {
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        fill_ellipse(c, lx + lw * frac, ly - 0.5f, 3.4f, 3.4f, rgb, 255);
        fill_ellipse(c, lx + lw * frac - 1, ly - 1.5f, 1.0f, 1.0f, 0xffffff, 200);
    } else
        blit_icon(c, lx + lw / 2 - 8, y, &icon_unknown_16, 220);
}

static void button(ctx_t *c, int x, int y, int W, int H, uint32_t fill, uint32_t edge, const char *label, int scale);   /* below */
static void draw_text(ctx_t *c, int x, int y, int scale, uint32_t rgb, const char *s);                                  /* the pixel font, below */
#define CARD_MORE_H 22
static void card_draw(ctx_t c, const tank_t *t, int fish_idx) {
    const fish_t *f = &t->fish[fish_idx];
    /* card: top-left, bordered in the fish's own color (that's its "name").
     * Two zones: NEEDS (things you can act on now - icon + segmented meter)
     * above the divider, WHO THEY ARE (slow traits - pole-to-pole spectrum
     * sliders) below it. Iconified 2026-08-30 with Strato's pixel art. */
    const int X = RENDER_CARD_X, Y = RENDER_CARD_Y, W = RENDER_CARD_W, H = RENDER_CARD_H; /* x clear of the curved bezel */
    /* backdrop: 28k blended pixels - hoisted (this alone was most of the
     * card's ~12 ms/frame on the device through per-pixel px_blend) */
    src_t bg = src_color(0x04141a, 1.0f);
    for (int y = Y; y < Y + H; y++) span(&c, X, X + W - 1, y, &bg, 215);
    for (int x = X; x < X + W; x++) { px(&c, x, Y, rgb565(f->color, 1)); px(&c, x, Y + H - 1, rgb565(f->color, 1)); }
    for (int y = Y; y < Y + H; y++) { px(&c, X, y, rgb565(f->color, 1)); px(&c, X + W - 1, y, rgb565(f->color, 1)); }

    /* identity row: the fish's NAME and the certainty dot - bright = the
     * model was sure of its last decision, dim = torn. (2026-09-29, Strato:
     * the name at the top; the stage pips and growth bar are gone - "a bit
     * ambiguous", the keeper sees the fish's size, and the milestones page a
     * tap deeper names the stage. The colour swatch went with them: the
     * border is the fish's colour, and a 7-letter name needs the row.) */
    draw_text(&c, X + 8, Y + 8, 2, 0xffffff, f->name);
    fill_ellipse(&c, X + W - 14, Y + 14, 3.2f, 3.2f, 0xffffff, (int)(40 + 200 * f->goal.confidence));

    /* needs: 5 segments each. Hunger is shown as FULLNESS - a full belly is
     * a full meter, and it drains as the fish gets hungry (a food icon next
     * to a growing bar read backwards) */
    struct { const icon_t *ic; float v; uint32_t rgb; } needs[4] = {
        { &icon_hunger, 10.0f - f->hunger, 0xffbd59 },
        { &icon_energy, f->energy,         0x78d67d },
        { &icon_stress, f->stress,         0xf25b65 },
        { &icon_trust,  f->trust,          0xffd166 },
    };
    for (int i = 0; i < 4; i++) {
        int ry = Y + 30 + i * 27;
        blit_icon(&c, X + 8, ry, needs[i].ic, 255);
        meter(&c, X + 38, ry + 7, 5, 14, needs[i].v / 10.0f, needs[i].rgb);
    }

    /* divider between the zones: across the one-column card, down the
       middle of the CYD's two-column one */
#ifdef CONFIG_POCKET_TANK_320X240
    for (int y = Y + 30; y < Y + H - 8; y++) px_blend(&c, X + 124, y, 0x2a3f45, 200);
    const int TX = X + 130, TW = W - 138, TY = Y + 34;    /* the traits' column */
#else
    for (int x = X + 8; x < X + W - 8; x++) px_blend(&c, x, Y + 142, 0x2a3f45, 200);
    const int TX = X + 8, TW = W - 16, TY = Y + 152;
#endif

    /* who they are: spectrum sliders, revealed by behaviour you have seen
       this fish do (docs/progression.md habits) */
    bool saw_bold = f->ms_bits & MS_FIRST_DART;
    bool saw_social = f->ms_bits & MS_FIRST_FOLLOW;
    bool saw_curious = f->ms_bits & (MS_INSPECTED | MS_FIRST_BUBBLES);
    slider(&c, TX, TY,      TW, f->bold,             0xffffff, &icon_shy,      &icon_bold,    saw_bold);
    slider(&c, TX, TY + 26, TW, f->sociable,         0x38dcc7, &icon_solo,     &icon_social,  saw_social);
    slider(&c, TX, TY + 52, TW, f->curiosity / 10.0f, 0x6db9ff, &icon_cautious, &icon_curious, saw_curious);

    /* the way onward (2026-09-16): a MORE button in the pages' dress at the
       foot of the card. The whole card was already the tap that opens the
       milestones page (and from there SETTINGS / UPGRADES), but nothing said
       so - a keeper asked Strato how to get there. The button is the sign;
       the hit box is still the card (touch ports, RENDER_CARD_H). */
    button(&c, TX, Y + H - 8 - CARD_MORE_H, TW, CARD_MORE_H, 0x1c2f36, 0x9fd8e2, "MORE", UI_TEXT(2));
}

/* ---- stats card cache (2026-09-01) ----
 * The card cost ~7 ms of every frame it was up (28k blended backdrop pixels
 * + icons + meters) - fps sagged whenever the keeper inspected a fish. With a
 * cache it is redrawn at most 4x a second over the STATIC water under it
 * (from the scene cache, so the translucent backdrop looks as before; only
 * things swimming behind the card stop showing through at 16%) and copied
 * into the frame otherwise. The selection ring follows the fish per frame. */
static uint16_t *g_card = NULL;
static int g_card_fish = -1; static float g_card_t = -1; static unsigned g_card_epoch;
void render_set_card_cache(uint16_t *buf) { g_card = buf; g_card_fish = -1; }

/* the snail's card (2026-09-16, Strato: "tapping the snail show a simple
   card with the upright snail image and how much algae has been grazed so
   far"): a ring on the snail, a centred box in the modal's dress - the
   upright sprite at 2x, SNAIL, the tally. Drawn every frame (no cache: a
   few hundred blended pixels, nothing like the fish card's meters). */
static int  text_w(const char *s, int scale);                                  /* the pixel font, below */
static void draw_text(ctx_t *c, int x, int y, int scale, uint32_t rgb, const char *s);
static void rect_edge(ctx_t *c, int x, int y, int w, int h, uint32_t rgb);
static void blit_icon_scaled(ctx_t *c, int x, int y, const icon_t *ic, int s, bool lit);
static void snail_card_draw(ctx_t *c, const tank_t *t) {
    ring(c, t->snail_x, t->snail_y, 20, 0x9fd8e2);
    const int W = SNAIL_CARD_W, H = SNAIL_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    src_t bg = src_color(0x04141a, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 235);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    const icon_t *ic = &icon_snail_upright;
    blit_icon_scaled(c, X + (W - ic->w * UI_TEXT(2)) / 2, Y + UI(10), ic, UI_TEXT(2), true);
    draw_text(c, X + (W - text_w("SNAIL", UI_TEXT(3))) / 2, Y + UI(82), UI_TEXT(3), 0xffffff, "SNAIL");
    const char *cap = "ALGAE GRAZED SO FAR";
    draw_text(c, X + (W - text_w(cap, UI_TEXT(2))) / 2, Y + UI(112), UI_TEXT(2), 0x9fd8e2, cap);
    char n[24];
    if (t->snail_grazed <= 0) snprintf(n, sizeof n, "NOTHING YET");
    else snprintf(n, sizeof n, "%d SPOT%s", (int)t->snail_grazed, t->snail_grazed == 1 ? "" : "S");
    draw_text(c, X + (W - text_w(n, UI_TEXT(3))) / 2, Y + UI(134), UI_TEXT(3), 0xffffff, n);
}

/* the urchin's card (2026-10-02), the snail's: a ring on it, the urchin
   at 3x with its spines waving, SEA URCHIN, the grass it has eaten so far
   (the trim's centimeters) and what it is up to - in words, never colour. */
/* the crab's card (0.3.9; Strato 10-23: "we need to add a modal for when the user taps it"), the urchin's way:
   a ring on it, a centred card - the crab facing the glass at 2x (its own grid: the tank's may be a profile),
   STRAWBERRY CRAB, the pellets it has found, what it is doing */
static void crab_card_draw(ctx_t *c, const tank_t *t) {
    ring(c, t->cr_x, CR_FLOOR_Y - t->cr_y - 10, 24, 0x9fd8e2);
    const int W = CRAB_CARD_W, H = CRAB_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    src_t bg = src_color(0x04141a, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 235);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    static uint32_t s_key = 0;
    cr_pose_t p; memset(&p, 0, sizeof p); p.salt = 7 + t->cr_variant;   /* the portrait: its roost, breathing; open-clawed while poked */
    cr_pose_roost(&p, 0, 0, t->cr_poke > 0, fast_sin(t->clock * 0.9f + 4 * TAU) > 0.35f);
    const uint32_t key = cr_hash(&p);
    if (key != s_key) { cr_build(ds()->cr_card_tones, ds()->cr_card_parts, &p); s_key = key; }
    const int S = UI_TEXT(2), ox = X + (W - CR_GW * S) / 2, oy = Y + UI(10);   /* (this fork) 2x on the 1.8, 1x on the CYD */
    for (int gy = 0; gy < CR_GH; gy++)
        for (int gx = 0; gx < CR_GW; gx++) {
            int tone = ds()->cr_card_tones[gy * CR_GW + gx];
            if (tone == CRT_NONE) continue;
            src_t sc = src_color(CR_RGB[tone], 1.0f);
            for (int yy = 0; yy < S; yy++) span(c, ox + gx * S, ox + gx * S + S - 1, oy + gy * S + yy, &sc, 255);
        }
    draw_text(c, X + (W - text_w("STRAWBERRY CRAB", UI_TEXT(3))) / 2, Y + UI(70), UI_TEXT(3), 0xffffff, "STRAWBERRY CRAB");
    {   /* two counts side by side: the pellets it found, the sand dollars it dug up (0.3.9) - what they paid, not how many finds (0.3.9.1) */
        const int L = X + W / 4, R = X + 3 * W / 4;
        char n[16];
        draw_text(c, L - text_w("PELLETS", UI_TEXT(2)) / 2, Y + UI(104), UI_TEXT(2), 0x9fd8e2, "PELLETS");
        if (t->cr_eaten == 0) snprintf(n, sizeof n, "NONE YET"); else snprintf(n, sizeof n, "%u", (unsigned)t->cr_eaten);
        draw_text(c, L - text_w(n, UI_TEXT(3)) / 2, Y + UI(126), UI_TEXT(3), 0xffffff, n);
        draw_text(c, R - text_w("SAND DOLLARS", UI_TEXT(2)) / 2, Y + UI(104), UI_TEXT(2), 0x9fd8e2, "SAND DOLLARS");
        if (t->cr_found == 0) snprintf(n, sizeof n, "NONE YET"); else snprintf(n, sizeof n, "%u", SD_CRAB_FINDS_VALUE(t->cr_found));
        draw_text(c, R - text_w(n, UI_TEXT(3)) / 2, Y + UI(126), UI_TEXT(3), 0xffffff, n);
    }
    const char *now = t->cr_find > 0 ? "FOUND A SAND DOLLAR!"
                    : t->cr_scurry ? "RUNNING FOR COVER"
                    : (t->cr_mode == CR_ROOST && t->cr_hide > 0) ? (t->cr_cave == 2 ? "HIDING IN THE CASTLE" : "HIDING IN THE CAVE")
                    : t->cr_mode == CR_ROOST ? (t->cr_cave == 2 ? "ROOSTING IN THE CASTLE" : t->cr_cave == 9 ? "ROOSTING IN THE CAVE" : "ROOSTING")
                    : t->cr_mode == CR_SHELTER ? (t->cr_cave == 2 ? "OFF TO THE CASTLE" : "OFF TO THE CAVE")
                    : t->cr_mode == CR_DIG ? "PICKING AT THE SAND"
                    : t->cr_mode == CR_FEED ? "GOING FOR FOOD"
                    : t->cr_mode == CR_EAT ? "EATING"
                    : t->cr_mode == CR_CLIMB ? "CLIMBING THE VOLCANO"
                    : t->cr_mode == CR_WALK ? "OUT FOR A WALK" : "RESTING";
    draw_text(c, X + (W - text_w(now, UI_TEXT(2))) / 2, Y + UI(162), UI_TEXT(2), 0x9fd8e2, now);
}
static void urchin_card_draw(ctx_t *c, const tank_t *t) {
    ring(c, t->urchin_x, URCHIN_FLOOR_Y + t->urchin_dy - 3, 22, 0x9fd8e2);
    const int W = URCHIN_CARD_W, H = URCHIN_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    src_t bg = src_color(0x04141a, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 235);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    ur_pose_t p; urchin_pose(&p, t->clock, tank_urchin_chewing(t));
    const int S = UI_TEXT(3), ox = X + W / 2, oy = Y + UI(10) + 16 * S;   /* the dome's centre at 3x */
    for (int ly = -16; ly <= (int)UR_BASE; ly++)
        for (int lx = -18; lx <= 18; lx++) {
            int tone = urchin_tone((float)lx + 0.5f, (float)ly + 0.5f, &p);
            if (tone == UR_NONE) continue;
            src_t sc = src_color(URCHIN_RGB[tone], 1.0f);
            for (int yy = 0; yy < S; yy++) span(c, ox + lx * S, ox + lx * S + S - 1, oy + ly * S + yy, &sc, 255);
        }
    draw_text(c, X + (W - text_w("SEA URCHIN", UI_TEXT(3))) / 2, Y + UI(90), UI_TEXT(3), 0xffffff, "SEA URCHIN");
    const char *cap = "GRASS GRAZED SO FAR";
    draw_text(c, X + (W - text_w(cap, UI_TEXT(2))) / 2, Y + UI(118), UI_TEXT(2), 0x9fd8e2, cap);
    char n[24]; int cm = (int)(t->urchin_grazed_px / PX_PER_CM);
    /* CM under a meter, then M with one decimal, KM past a kilometer (0.3.9,
       Strato: "1.4K CM is odd"); the longest line, "999.9 M", fits the card */
    if (cm < 1) snprintf(n, sizeof n, "NOTHING YET");
    else if (cm < 100) snprintf(n, sizeof n, "%d CM", cm);
    else if (cm < 100000) snprintf(n, sizeof n, "%d.%d M", cm / 100, cm % 100 / 10);
    else snprintf(n, sizeof n, "%d.%d KM", cm / 100000, cm % 100000 / 10000);
    draw_text(c, X + (W - text_w(n, UI_TEXT(3))) / 2, Y + UI(140), UI_TEXT(3), 0xffffff, n);
    const char *now = tank_urchin_chewing(t) ? "CHEWING"
                    : t->urchin_frond >= 0 ? "OFF TO THE TALL GRASS"
                    : "RESTING";
    draw_text(c, X + (W - text_w(now, UI_TEXT(2))) / 2, Y + UI(170), UI_TEXT(2), 0x9fd8e2, now);
}

/* the shrimp school's card (2026-09-29, Strato: "one tap should bring up a
   card to show how much food they've eaten and progress to the next shrimp").
   The pips fill with pellets - filled vs hollow, never colour alone. */
static void shrimp_card_draw(ctx_t *c, const tank_t *t) {
    int n = t->shrimp_n;
    float cx = 0, cy = 0, r = 0;
    for (int i = 0; i < n; i++) { cx += t->shrimp[i].x; cy += t->shrimp[i].y; }
    cx /= n; cy /= n;
    for (int i = 0; i < n; i++) { float d = tank_dist(cx, cy, t->shrimp[i].x, t->shrimp[i].y); if (d > r) r = d; }
    ring(c, cx, cy, fminf(fmaxf(r + 14, 22), 90), 0x9fd8e2);
    const int W = SHRIMP_CARD_W, H = SHRIMP_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    src_t bg = src_color(0x04141a, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 235);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    /* a shrimp at 4x, its legs paddling */
    src_t pal[SHRIMP_TONES];
    for (int k = 0; k < SHRIMP_TONES; k++) pal[k] = src_color(SHRIMP_RGB[k], 1.0f);
    const char *const *fr = fmodf(t->clock * 2.5f, 1) < 0.5f ? SHRIMP_IDLE_A : SHRIMP_IDLE_B;
    const int S = UI_TEXT(4), sx = X + (W - 18 * S) / 2, sy = Y + UI(8);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 18; x++) {
            const char *k = fr[y][x] == '.' ? NULL : strchr(SHRIMP_KEYS, fr[y][x]);
            if (!k) continue;
            int ti = (int)(k - SHRIMP_KEYS);
            for (int yy = 0; yy < S; yy++) span(c, sx + x * S, sx + x * S + S - 1, sy + y * S + yy, &pal[ti], SHRIMP_ALPHA[ti]);
        }
    char line[32];
    snprintf(line, sizeof line, "%d SHRIMP", n);
    draw_text(c, X + (W - text_w(line, UI_TEXT(3))) / 2, Y + UI(46), UI_TEXT(3), 0xffffff, line);
    snprintf(line, sizeof line, "%d PELLET%s EATEN", (int)t->shrimp_eaten, t->shrimp_eaten == 1 ? "" : "S");
    draw_text(c, X + (W - text_w(line, UI_TEXT(2))) / 2, Y + UI(76), UI_TEXT(2), 0x9fd8e2, line);
    const char *cap = "NEXT SHRIMP";
    draw_text(c, X + (W - text_w(cap, UI_TEXT(2))) / 2, Y + UI(104), UI_TEXT(2), 0x9fd8e2, cap);
    bool full = n >= SHRIMP_MAX;
    int have = full ? SHRIMP_PER_JOIN : t->shrimp_food;
    const int pitch = UI(22), px0 = X + (W - (SHRIMP_PER_JOIN - 1) * pitch) / 2, py = Y + UI(132);
    for (int i = 0; i < SHRIMP_PER_JOIN; i++) {
        float x = px0 + i * pitch;
        if (i < have) {
            fill_ellipse(c, x, py, 6.5f * UI_F, 6.5f * UI_F, 0xffbd59, 255);
            fill_ellipse(c, x - UI(2), py - UI(2), 2.2f * UI_F, 2.2f * UI_F, 0xffe9bd, 255);
        } else ring(c, x, py, UI(6), 0x5a6a6e);
    }
    if (full) snprintf(line, sizeof line, "THE SCHOOL IS FULL");
    else if (tank_shrimp_refusing(t)) snprintf(line, sizeof line, "TOO MUCH ALGAE TO EAT");
    else if (t->shrimp_food >= SHRIMP_PER_JOIN && t->shrimp_cool > 0) {
        int m = (int)ceilf(t->shrimp_cool / 60.0f);
        snprintf(line, sizeof line, "ARRIVES IN %d MIN", m < 1 ? 1 : m);
    } else {
        int more = SHRIMP_PER_JOIN - t->shrimp_food;
        snprintf(line, sizeof line, "%d MORE PELLET%s", more, more == 1 ? "" : "S");
    }
    draw_text(c, X + (W - text_w(line, UI_TEXT(2))) / 2, Y + UI(158), UI_TEXT(2), 0xffffff, line);
}

/* the seahorse's card (0.3.6): a ring on it and a centred card - the
 * seahorse at 2x in its open curl, its days with the keeper, its hitches,
 * the trust it passed on and what it is doing */
static void seahorse_card_draw(ctx_t *c, const tank_t *t) {
    ring(c, t->sh_x + 4 * t->sh_facing, t->sh_y - SH_BODY_UP, 24, 0x9fd8e2);
    const int W = SEAHORSE_CARD_W, H = SEAHORSE_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    src_t bg = src_color(0x04141a, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 235);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    decor_scratch_t *sc = ds();
    sh_sprite(sc, sc->sh_spr_card, t->clock, SH_FREE, 0, 11.0f, 1.5f, 1.0f, 0);
    const int S = UI_TEXT(2), ox = X + UI(14), oy = Y + (H - SH_H * S) / 2;   /* (this fork) 1x on the CYD's scaled card, as the snail's */
    for (int ly = 0; ly < SH_H; ly++)
        for (int lx = 0; lx < SH_W; lx++) {
            uint8_t b = sc->sh_spr_card[ly * SH_W + lx];
            if (!(b & SHB_ON)) continue;
            src_t s = src_color(SH_RGB[b & 15], 1.0f);
            for (int yy = 0; yy < S; yy++) span(c, ox + lx * S, ox + lx * S + S - 1, oy + ly * S + yy, &s, SH_ALPHA[b & 15]);
        }
    const int TX = X + UI(14) + SH_W * S + UI(12);   /* "+N.N TRUST PASSED ON" is 21 chars at scale 2: 252 px, inside the card from here */
    draw_text(c, TX, Y + UI(18), UI_TEXT(3), 0xffffff, "SEAHORSE");
    char line[32];
    float days = tank_seahorse_days(t);
    if (days < 1) snprintf(line, sizeof line, "WITH YOU SINCE TODAY");
    else snprintf(line, sizeof line, "%d DAY%s WITH YOU", (int)days, (int)days == 1 ? "" : "S");
    draw_text(c, TX, Y + UI(52), UI_TEXT(2), 0x9fd8e2, line);
    snprintf(line, sizeof line, "%d HITCH%s", (int)t->sh_hitches, t->sh_hitches == 1 ? "" : "ES");
    draw_text(c, TX, Y + UI(76), UI_TEXT(2), 0xffffff, line);
    snprintf(line, sizeof line, "+%d.%d TRUST PASSED ON", (int)t->sh_trust_passed, (int)(t->sh_trust_passed * 10) % 10);
    draw_text(c, TX, Y + UI(100), UI_TEXT(2), 0xffffff, line);
    snprintf(line, sizeof line, "%d PELLET%s SNAPPED", (int)t->sh_eaten, t->sh_eaten == 1 ? "" : "S");
    draw_text(c, TX, Y + UI(124), UI_TEXT(2), 0xffffff, line);
    char now[32];
    if (t->sh_hitched) snprintf(now, sizeof now, "ON YOUR FINGER");
    else if (t->sh_mode == SH_HITCH) snprintf(now, sizeof now, "SWIMMING TO YOU");
    else if (t->night && t->sh_mode == SH_PERCH) snprintf(now, sizeof now, "ASLEEP");   /* only once it holds on (0.3.7: it said ASLEEP on the finger) */
    else if (t->sh_mode == SH_PERCH && t->sh_akind == SH_ANCHOR_SAND) snprintf(now, sizeof now, "RESTING ON THE SAND");
    else if (t->sh_mode == SH_PERCH) snprintf(now, sizeof now, "HOLDING %s", tank_seahorse_holding(t));
    else snprintf(now, sizeof now, "ADRIFT");
    draw_text(c, TX, Y + UI(156), UI_TEXT(2), 0x9fd8e2, now);
}

static void tools_draw(ctx_t *c, const tank_t *t);   /* the toolbox, below */
static void next_draw(ctx_t *c, const tank_t *t);    /* the NEXT FISH arrow, below */
void render_stats_card(const tank_t *t, int fish_idx, uint16_t *fb, int stride) {
    if (fish_idx == RENDER_CARD_SHRIMP) {
        if (!(t->sd_unlocks & SD_ITEM_SHRIMP) || t->shrimp_n <= 0) return;
        ctx_t sc = ctx_full(fb, stride, 1.0f);
        shrimp_card_draw(&sc, t);
        return;
    }
    if (fish_idx == RENDER_CARD_URCHIN) {
        if (!(t->sd_unlocks & SD_ITEM_URCHIN) || t->urchin_x < 0) return;
        ctx_t sc = ctx_full(fb, stride, 1.0f);
        urchin_card_draw(&sc, t);
        return;
    }
    if (fish_idx == RENDER_CARD_CRAB) {
        if (!(t->sd_unlocks & SD_ITEM_CRAB) || t->cr_x < 0) return;
        ctx_t sc = ctx_full(fb, stride, 1.0f);
        crab_card_draw(&sc, t);
        return;
    }
    if (fish_idx == RENDER_CARD_SEAHORSE) {
        if (!(t->sd_unlocks & SD_ITEM_SEAHORSE) || t->sh_x < 0) return;
        ctx_t sc = ctx_full(fb, stride, 1.0f);
        seahorse_card_draw(&sc, t);
        return;
    }
    if (fish_idx == RENDER_CARD_SNAIL) {
        if (!(t->sd_unlocks & SD_ITEM_SNAIL) || t->snail_x < 0) return;
        ctx_t sc = ctx_full(fb, stride, 1.0f);
        snail_card_draw(&sc, t);
        return;
    }
    if (fish_idx < 0 || fish_idx >= t->n_fish) return;
    ctx_t c = ctx_full(fb, stride, 1.0f);            /* card ignores night dimming */
    const fish_t *f = &t->fish[fish_idx];
    ring(&c, f->x, f->y, 17 * f->size, f->color);
    unsigned ep; const uint16_t *scene = render_scene_buf(&ep);
    if (g_card && scene) {
        if (fish_idx != g_card_fish || ep != g_card_epoch ||
            t->clock - g_card_t > 0.25f || t->clock < g_card_t) {
            for (int y = 0; y < RENDER_CARD_H; y++)
                memcpy(g_card + y * RENDER_CARD_W,
                       scene + (RENDER_CARD_Y + y) * TANK_W + RENDER_CARD_X, RENDER_CARD_W * 2);
            ctx_t cc = { g_card, RENDER_CARD_W, 1.0f, RENDER_CARD_X, RENDER_CARD_Y, RENDER_CARD_W, RENDER_CARD_H };
            card_draw(cc, t, fish_idx);
            g_card_fish = fish_idx; g_card_epoch = ep; g_card_t = t->clock;
        }
        for (int y = 0; y < RENDER_CARD_H; y++) {
            memcpy(fb + (RENDER_CARD_Y + y) * stride + RENDER_CARD_X,
                   g_card + y * RENDER_CARD_W, RENDER_CARD_W * 2);
            touch_span_f(RENDER_CARD_X, RENDER_CARD_X + RENDER_CARD_W - 1, RENDER_CARD_Y + y);
        }
    } else card_draw(c, t, fish_idx);
    tools_draw(&c, t);
    next_draw(&c, t);
}

/* ---- the toolbox (2026-10-01) ----
 * Drawn every frame, uncached - so the backdrop is the card's look by bit
 * shifts, not blends: an eighth of the water through, the rest the card's
 * dark (~8.7k pixels for the price of a copy; blended, the card's 28k cost
 * ~7 ms on the device). */
static void shade_box(ctx_t *c, int x, int y, int w, int h) {
    const uint16_t base = rgb565(0x04141a, 0.875f);
    for (int yy = y; yy < y + h; yy++) {
        if ((unsigned)(yy - c->oy) >= (unsigned)c->h) continue;
        int x0 = x < c->ox ? c->ox : x, x1 = x + w - 1 > c->ox + c->w - 1 ? c->ox + c->w - 1 : x + w - 1;
        uint16_t *p = &CTX_PX(c, x0, yy);
        for (int xx = x0; xx <= x1; xx++, p++) { uint16_t n = FB_SWAP16(*p); *p = FB_SWAP16((uint16_t)(((n >> 3) & 0x18E3) + base)); }
        TOUCH_SPAN(c, x0, x1, yy);
    }
}
static void rect_fill(ctx_t *c, int x, int y, int w, int h, uint32_t rgb);   /* below */
#define TOOL_BTN_W ((RENDER_TOOLS_W - 16 - 6) / 2)
#define TOOL_BTN_H (RENDER_TOOLS_H - 16)
static void tools_draw(ctx_t *c, const tank_t *t) {
    const int X = RENDER_TOOLS_X, Y = RENDER_TOOLS_Y, W = RENDER_TOOLS_W, H = RENDER_TOOLS_H;
    shade_box(c, X, Y, W, H);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    static const icon_t *const ICON[2] = { &icon_tool_sponge, &icon_tool_scissors };
    for (int k = 0; k < 2; k++) {
        int bx = X + 8 + k * (TOOL_BTN_W + 6), by = Y + 8;
        bool held = t->tool == (k ? TOOL_SCISSORS : TOOL_SPONGE);
        rect_fill(c, bx, by, TOOL_BTN_W, TOOL_BTN_H, held ? 0x356670 : 0x14252b);
        uint32_t edge = held ? 0xffffff : 0x4d6a72;
        rect_edge(c, bx, by, TOOL_BTN_W, TOOL_BTN_H, edge);
        if (held) rect_edge(c, bx + 1, by + 1, TOOL_BTN_W - 2, TOOL_BTN_H - 2, edge);
        const icon_t *ic = ICON[k];
        blit_icon_scaled(c, bx + (TOOL_BTN_W - ic->w * 2) / 2, by + (TOOL_BTN_H - ic->h * 2) / 2, ic, 2, true);
    }
    /* the LOCK beside it (0.3.5) */
    const int LX = RENDER_LOCK_X, LY = RENDER_LOCK_Y, LW = RENDER_LOCK_W, LH = RENDER_LOCK_H;
    if (t->tools_locked) rect_fill(c, LX, LY, LW, LH, 0x356670); else shade_box(c, LX, LY, LW, LH);
    rect_edge(c, LX, LY, LW, LH, t->tools_locked ? 0xffffff : 0x9fd8e2);
    rect_edge(c, LX + 1, LY + 1, LW - 2, LH - 2, t->tools_locked ? 0xffffff : 0x1c2f36);
    const icon_t *lk = t->tools_locked ? &icon_tool_lock_closed : &icon_tool_lock_open;
    blit_icon(c, LX + (LW - lk->w) / 2, LY + (LH - lk->h) / 2, lk, 255);
}
/* the NEXT FISH arrow (0.3.9) beside the card's top right: drawn every
   frame like the lock (outside the card's cache) */
static void next_draw(ctx_t *c, const tank_t *t) {
    const int X = RENDER_NEXT_X, Y = RENDER_NEXT_Y, W = RENDER_NEXT_W, H = RENDER_NEXT_H;
    const uint32_t rgb = t->n_fish > 1 ? 0x9fd8e2 : 0x2c4a52;
    shade_box(c, X, Y, W, H);
    rect_edge(c, X, Y, W, H, rgb); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    int cx = X + W / 2, cy = Y + H / 2;
    for (int i = 0; i < 9; i++) {                           /* a solid triangle, 9 wide, 17 tall, pointing on */
        int hh = 8 - i;
        rect_fill(c, cx - 4 + i, cy - hh, 1, 2 * hh + 1, rgb);
    }
}
bool render_next_hit(float x, float y) {
    return x >= RENDER_NEXT_X - RENDER_SIDE_GAP / 2 && x < RENDER_NEXT_X + RENDER_NEXT_W + 16 &&
           y >= RENDER_NEXT_Y - 8 && y < RENDER_NEXT_Y + RENDER_NEXT_H + 12;
}
int render_next_fish(const tank_t *t, int fish_idx) {
    if (fish_idx < 0 || fish_idx >= t->n_fish) return fish_idx;
    return (fish_idx + 1) % t->n_fish;
}
bool render_lock_hit(float x, float y) {
    return y >= RENDER_TOOLS_Y && x >= RENDER_LOCK_X - RENDER_SIDE_GAP / 2 && x < RENDER_LOCK_X + RENDER_LOCK_W + 16;
}
int render_tools_hit(float x, float y) {
    const int X = RENDER_TOOLS_X, W = RENDER_TOOLS_W;
    if (y < RENDER_TOOLS_Y || x < X - RENDER_CARD_HIT_SIDE || x >= X + W + RENDER_SIDE_GAP / 2) return -1;   /* (the right: half the gap, the lock has the rest) */
    return x < X + W / 2 ? TOOL_SPONGE : TOOL_SCISSORS;
}

/* the chip: the tool at 1x and DONE, top left where the card would be */
#ifdef TANK_ROUND                                    /* the bowl: up in the left shoulder, inside the glass */
#define TOOL_CHIP_X 84
#define TOOL_CHIP_Y 72
#else
#define TOOL_CHIP_X RENDER_CARD_X
#define TOOL_CHIP_Y RENDER_CARD_Y
#endif
#define TOOL_CHIP_H 40
static int tool_chip_w(void) { return 8 + 24 + 8 + text_w("DONE", 2) + 10; }
void render_tool_chip(const tank_t *t, int fish_idx, uint16_t *fb, int stride) {
    if (t->tool == TOOL_HAND || (fish_idx >= 0 && fish_idx < t->n_fish)) return;
    /* a stroke on the glass: the chip steps aside until the finger lifts, so
     * the film it covers shows as the sponge takes it (0.3.5, Strato: "the
     * sponge should be able to clean algae behind the DONE button") */
    if (tank_stroking(t)) return;                    /* (not a press: the tap on DONE itself keeps it in view) */
    ctx_t c = ctx_full(fb, stride, 1.0f);
    const int X = TOOL_CHIP_X, Y = TOOL_CHIP_Y, W = tool_chip_w(), H = TOOL_CHIP_H;
    shade_box(&c, X, Y, W, H);
    rect_edge(&c, X, Y, W, H, 0xffffff); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x9fd8e2);
    const icon_t *ic = t->tool == TOOL_SCISSORS ? &icon_tool_scissors : &icon_tool_sponge;
    blit_icon(&c, X + 8, Y + (H - ic->h) / 2, ic, 255);
    draw_text(&c, X + 8 + ic->w + 8, Y + (H - 14) / 2, 2, 0xffffff, "DONE");
}
bool render_tool_chip_hit(const tank_t *t, float x, float y) {
    return t->tool != TOOL_HAND && x < TOOL_CHIP_X + tool_chip_w() + 16 && y < TOOL_CHIP_Y + TOOL_CHIP_H + 24;
}


/* ---- a small pixel font (2026-09-11) ----
 * 5x7 glyphs - upper case, digits, a little punctuation - drawn as scale x
 * scale blocks, so one table reads at 2x for a caption and 3x for a
 * button. The renderer's first text (the milestones page can borrow it for
 * labels). Lower case maps to upper; anything else advances a cell. */
static const uint8_t FONT5X7[][7] = {
    { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* A */
    { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e }, /* B */
    { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e }, /* C */
    { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e }, /* D */
    { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f }, /* E */
    { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 }, /* F */
    { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f }, /* G */
    { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* H */
    { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* I */
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c }, /* J */
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* K */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f }, /* L */
    { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
    { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 }, /* N */
    { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* O */
    { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 }, /* P */
    { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d }, /* Q */
    { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 }, /* R */
    { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e }, /* S */
    { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* T */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* U */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 }, /* V */
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a }, /* W */
    { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 }, /* X */
    { 0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04 }, /* Y */
    { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f }, /* Z */
    { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e }, /* 0 */
    { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* 1 */
    { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f }, /* 2 */
    { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e }, /* 3 */
    { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 }, /* 4 */
    { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e }, /* 5 */
    { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e }, /* 6 */
    { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* 7 */
    { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e }, /* 8 */
    { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c }, /* 9 */
    { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 }, /* ? */
    { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 }, /* ! */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c }, /* . */
    { 0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08 }, /* , */
    { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 }, /* - */
    { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00 }, /* : */
    { 0x0c, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 }, /* ' */
    { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 }, /* / */
    { 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00 }, /* + */
    { 0x19, 0x1a, 0x02, 0x04, 0x08, 0x0b, 0x13 }, /* % */
    { 0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00 }, /* ~ (the battery page's "about") */
};
static const char FONT_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789?!.,-:'/+%~";
static const uint8_t *glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    const char *p = ch ? strchr(FONT_CHARS, ch) : NULL;
    return p ? FONT5X7[p - FONT_CHARS] : NULL;
}
static int text_w(const char *s, int scale) { int n = (int)strlen(s); return n ? n * 6 * scale - scale : 0; }
static void draw_text(ctx_t *c, int x, int y, int scale, uint32_t rgb, const char *s) {
    src_t col = src_color(rgb, c->dim);
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *g = glyph(*s);
        if (!g) continue;
        for (int r = 0; r < 7; r++)
            for (int k = 0; k < 5; k++)
                if (g[r] & (0x10 >> k))
                    for (int yy = 0; yy < scale; yy++)
                        span(c, x + k * scale, x + k * scale + scale - 1, y + r * scale + yy, &col, 255);
    }
}

/* 8 px text (2026-09-16, the settings page's version line): the 5x7 font at
 * scale 1 with its second row drawn twice. Row 1 is a vertical-stroke row in
 * every glyph the line can hold (letters, digits, '-', '.'), so the doubling
 * only lengthens strokes and never thickens a bar - Strato wanted the line
 * smaller than the page's text (14 px) but not the font's 7 px. */
static void draw_text_8px(ctx_t *c, int x, int y, uint32_t rgb, const char *s) {
    src_t col = src_color(rgb, c->dim);
    for (; *s; s++, x += 6) {
        const uint8_t *g = glyph(*s);
        if (!g) continue;
        for (int r = 0, yy = y; r < 7; r++, yy++) {
            for (int rep = r == 1 ? 2 : 1; rep; rep--, yy += rep ? 1 : 0)
                for (int k = 0; k < 5; k++)
                    if (g[r] & (0x10 >> k)) span(c, x + k, x + k, yy, &col, 255);
        }
    }
}

/* ---- reset confirm (2026-09-11) ----
 * The keeper's "start over" (device: hold BOOT 5 s, then tap the glass - 0.3.9.2; sim: X, or the 0 key held 5 s + a click). A
 * modal panel over the live tank - the fish keep swimming behind it - that
 * asks before the save is wiped. Opaque stores only, so a frame with it up
 * costs about what the milestones page does. */
static void rect_fill(ctx_t *c, int x, int y, int w, int h, uint32_t rgb) {
    src_t f = src_color(rgb, 1.0f);
    for (int yy = y; yy < y + h; yy++) span(c, x, x + w - 1, yy, &f, 255);
}
static void rect_edge(ctx_t *c, int x, int y, int w, int h, uint32_t rgb) {
    src_t e = src_color(rgb, 1.0f);
    span(c, x, x + w - 1, y, &e, 255); span(c, x, x + w - 1, y + h - 1, &e, 255);
    for (int yy = y + 1; yy < y + h - 1; yy++) { px(c, x, yy, e.v); px(c, x + w - 1, yy, e.v); }
}
static void button(ctx_t *c, int x, int y, int W, int H, uint32_t fill, uint32_t edge, const char *label, int scale) {
    rect_fill(c, x, y, W, H, fill);
    rect_edge(c, x, y, W, H, edge); rect_edge(c, x + 1, y + 1, W - 2, H - 2, edge);
    draw_text(c, x + (W - text_w(label, scale)) / 2, y + (H - 7 * scale) / 2, scale, 0xffffff, label);
}
/* the same primitives for panels built elsewhere (render.h) */
int  render_text_w(const char *s, int scale) { return text_w(s, scale); }
void render_text(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const char *s) {
    ctx_t c = ctx_page(fb, stride); draw_text(&c, x, y, scale, rgb, s);
}
void render_rect(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb) {
    ctx_t c = ctx_page(fb, stride); rect_fill(&c, x, y, w, h, rgb);
}
void render_rect_edge(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb) {
    ctx_t c = ctx_page(fb, stride); rect_edge(&c, x, y, w, h, rgb);
}
void render_rect_blend(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb, int alpha) {
    ctx_t c = ctx_page(fb, stride); src_t s = src_color(rgb, 1.0f);
    for (int yy = y; yy < y + h; yy++) span(&c, x, x + w - 1, yy, &s, alpha);
}
void render_ring(uint16_t *fb, int stride, float cx, float cy, float r, uint32_t rgb) {
    ctx_t c = ctx_page(fb, stride); ring(&c, cx, cy, r, rgb);
}
void render_button(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t fill, uint32_t edge, const char *label, int scale) {
    ctx_t c = ctx_page(fb, stride); button(&c, x, y, w, h, fill, edge, label, scale);
}
void render_glyph(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const uint8_t *rows) {
    ctx_t c = ctx_page(fb, stride); src_t col = src_color(rgb, 1.0f);
    for (int r = 0; r < 7; r++)
        for (int k = 0; k < 5; k++)
            if (rows[r] & (0x10 >> k))
                for (int yy = 0; yy < scale; yy++)
                    span(&c, x + k * scale, x + k * scale + scale - 1, y + r * scale + yy, &col, 255);
}
void render_fish_preview(uint16_t *fb, int stride, float x, float y, float size,
                         uint32_t body, uint32_t fin, uint32_t accent, float clock) {
    ctx_t c = ctx_page(fb, stride);
    fish_t f; memset(&f, 0, sizeof f);
    f.x = x; f.y = y; f.heading = 0; f.size = size; f.speed = 40;
    f.stage = STAGE_ADULT;                       /* grown: the crest shows the fin colour */
    f.hunger = 4; f.stress = 0; f.goal.id = GOAL_EXPLORE;
    f.color = body; f.fin = fin; f.accent = accent;
    tank_fish_face(&f);                          /* side-on, facing right */
    draw_fish_core(&c, &f, clock, false, size, 1e9f);
}
void render_fish_portrait(uint16_t *fb, int stride, float x, float y, float size, const fish_t *who, float clock) {
    ctx_t c = ctx_page(fb, stride);
    fish_t f; memset(&f, 0, sizeof f);
    f.x = x; f.y = y; f.heading = 0; f.size = size; f.speed = 40;
    f.stage = who->stage;
    f.hunger = 4; f.stress = 0; f.goal.id = GOAL_EXPLORE;
    f.color = who->color; f.fin = who->fin; f.accent = who->accent;
    tank_fish_face(&f);
    draw_fish_core(&c, &f, clock, false, size, 1e9f);
}
void render_confirm_reset(uint16_t *fb, int stride, float frac) {
    ctx_t c = ctx_page(fb, stride);              /* ignores night dimming, like the card */
    const int X = RENDER_CONFIRM_X, Y = RENDER_CONFIRM_Y, W = RENDER_CONFIRM_W, H = RENDER_CONFIRM_H;
    rect_fill(&c, X, Y, W, H, 0x04141a);
    rect_edge(&c, X, Y, W, H, 0x9fd8e2); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    const char *title = "RESET TANK?";
    draw_text(&c, X + (W - text_w(title, UI_TEXT(3))) / 2, Y + UI(20), UI_TEXT(3), 0xffffff, title);
    const char *l1 = "START OVER WITH TWO FRY", *l2 = "EVERYTHING ELSE IS LOST";
    draw_text(&c, X + (W - text_w(l1, UI_TEXT(2))) / 2, Y + UI(58), UI_TEXT(2), 0x9fd8e2, l1);
    draw_text(&c, X + (W - text_w(l2, UI_TEXT(2))) / 2, Y + UI(78), UI_TEXT(2), 0x9fd8e2, l2);
    /* NO is the calm one; YES wears the stress red */
    button(&c, RENDER_CONFIRM_NO_X,  RENDER_CONFIRM_BTN_Y, RENDER_CONFIRM_BTN_W, RENDER_CONFIRM_BTN_H, 0x1c2f36, 0x9fd8e2, "NO", UI_TEXT(3));
    button(&c, RENDER_CONFIRM_YES_X, RENDER_CONFIRM_BTN_Y, RENDER_CONFIRM_BTN_W, RENDER_CONFIRM_BTN_H, 0x7a2028, 0xf25b65, "YES", UI_TEXT(3));
    /* the prompt lets itself go: a bar draining toward the timeout */
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    const int bw = W - UI(48), bx = X + UI(24), by = Y + H - UI(16);
    rect_fill(&c, bx, by, bw, 3, 0x2a3f45);
    int fw = (int)(bw * frac + 0.5f);
    if (fw > 0) rect_fill(&c, bx, by, fw, 3, 0x9fd8e2);
}
int render_confirm_hit(float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;                    /* the page's own coordinates */
    const int m = UI(10);                               /* a fingertip's slop around each button */
    if (y < RENDER_CONFIRM_BTN_Y - m || y >= RENDER_CONFIRM_BTN_Y + RENDER_CONFIRM_BTN_H + m) return 0;
    if (x >= RENDER_CONFIRM_NO_X - m  && x < RENDER_CONFIRM_NO_X  + RENDER_CONFIRM_BTN_W + m) return -1;
    if (x >= RENDER_CONFIRM_YES_X - m && x < RENDER_CONFIRM_YES_X + RENDER_CONFIRM_BTN_W + m) return 1;
    return 0;
}

/* ---- the battery page (2026-09-24): a tap on the pill. The snail card's
 * dress, centered over the live tank: the battery large with its percent,
 * the state in words (the one thing no color has to carry), the estimate,
 * then two or three rows - since the cable moved, the screen-on time on
 * this charge, what a full charge lasts - and the cell voltage, dim, for
 * the curious. Rows the clock cannot answer are left out. ---- */
void render_battery_info(uint16_t *fb, int stride, const bat_info_t *bi, float clock) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    bool pw = BAT_ON_POWER(bi->state);
    char lab[3][16], val[3][32], d[16]; int nr = 0;
    if (bi->since_min >= 0) {
        snprintf(lab[nr], sizeof lab[nr], pw ? "PLUGGED IN" : "UNPLUGGED");
        if (bi->since_min < 1) snprintf(val[nr], sizeof val[nr], "JUST NOW");
        else { battery_fmt_dur(d, sizeof d, bi->since_min); snprintf(val[nr], sizeof val[nr], "%s AGO", d); }
        nr++;
    }
    if (!pw && bi->awake_min >= 0) {
        snprintf(lab[nr], sizeof lab[nr], "SCREEN ON");
        battery_fmt_dur(val[nr], sizeof val[nr], bi->awake_min); nr++;
    }
    if (bi->life_min > 0) {
        snprintf(lab[nr], sizeof lab[nr], "BATTERY LIFE");
        battery_fmt_dur(d, sizeof d, bi->life_min); snprintf(val[nr], sizeof val[nr], "~%s", d); nr++;
    }
    const int W = 336, H = (bi->mv > 0 ? 170 : 154) + nr * 26, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    src_t bg = src_color(0x04141a, 1.0f);
    for (int y = Y; y < Y + H; y++) span(&c, X, X + W - 1, y, &bg, 240);
    rect_edge(&c, X, Y, W, H, 0x9fd8e2); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    /* the battery, its percent beside it */
    char pct[16]; snprintf(pct, sizeof pct, "%d%%", bi->pct < 0 ? 0 : bi->pct > 100 ? 100 : bi->pct);
    const int BW = 84, BH = 36, TS = 5;
    int bolt = pw ? bolt_room(BH) : 0;
    int gw = bolt + BW + 6 + 20 + text_w(pct, TS), gx = X + (W - gw) / 2, gy = Y + 22;
    draw_battery(&c, gx + bolt, gy, BW, BH, 2, bi->pct / 100.0f, bi->state, clock);
    draw_text(&c, gx + bolt + BW + 6 + 20, gy + (BH - 7 * TS) / 2, TS, 0xffffff, pct);
    /* the state, then what it means in time */
    const char *st = bi->state == BAT_CHARGING ? "CHARGING" : bi->state == BAT_FULL ? "FULLY CHARGED"
                   : bi->state == BAT_PLUGGED ? "PLUGGED IN" : "ON BATTERY";
    draw_text(&c, X + (W - text_w(st, 3)) / 2, Y + 78, 3, 0xffffff, st);
    char est[48];
    if (bi->state == BAT_CHARGING) { battery_fmt_dur(d, sizeof d, bi->left_min); snprintf(est, sizeof est, "FULL IN ABOUT %s", d); }
    else if (bi->state == BAT_FULL) snprintf(est, sizeof est, "READY TO UNPLUG");
    else if (bi->state == BAT_PLUGGED) snprintf(est, sizeof est, "NOT CHARGING RIGHT NOW");
    else if (bi->left_min < 5) snprintf(est, sizeof est, "ALMOST EMPTY");
    else { battery_fmt_dur(d, sizeof d, bi->left_min); snprintf(est, sizeof est, "ABOUT %s LEFT", d); }
    draw_text(&c, X + (W - text_w(est, 2)) / 2, Y + 110, 2, 0x9fd8e2, est);
    rect_fill(&c, X + 20, Y + 138, W - 40, 1, 0x1c2f36);
    for (int i = 0; i < nr; i++) {
        int ry = Y + 152 + i * 26;
        draw_text(&c, X + 20, ry, 2, 0x9fd8e2, lab[i]);
        draw_text(&c, X + W - 20 - text_w(val[i], 2), ry, 2, 0xffffff, val[i]);
    }
    if (bi->mv > 0) {
        char v[32]; snprintf(v, sizeof v, "%d.%02d V", bi->mv / 1000, bi->mv % 1000 / 10);
        draw_text_8px(&c, X + (W - (int)strlen(v) * 6 + 1) / 2, Y + H - 20, 0x5f7f86, v);
    }
}

/* ---- milestones page (2026-09-13 redesign: an achievement wall) ----
 * Rows of 40 px: the fish (render_fish_preview at its real size, so growth
 * shows), its name, a growth strip, then six 32 px badges at a 40 px pitch.
 * The tank row repeats the shape with the population strip. A locked badge
 * is the art as a flat grey silhouette (blit_icon_locked); a badge earned
 * since the keeper last closed the page wears a two-tone ring. Tapping a
 * badge / a name / a strip opens a small detail modal in the reset prompt's
 * dress (the art at 2x, a title, the words); the next tap anywhere closes
 * it back to the page. Render-local state, cleared by render_milestones_leave. */
/* The BOWL (TANK_ROUND; Strato on the glass, 2026-10-01: the top fish's
 * portrait and its last badge were cut by the circle - "nudge this down",
 * SETTINGS "to the top, dead center as it would fit in that opening",
 * UPGRADES and CLOSE "closer together centered"): the rows start where the
 * circle is wide enough for them and stand 34 px apart, SETTINGS sits alone
 * in the cap above them, the other two side by side at the foot. */
/* (the page's numbers, MSP_*: render.h) */
#define MSP_INK       0x031015
#define MSP_DIM       0x2a3f45
#define MSP_QUIET     0x7f9aa1      /* quiet TEXT on the panels: a price out of reach, YOU HAVE, the settings' notes (0.3.9.1, Machito on Discord: MSP_DIM text
                                         "nearly impossible" to read, worse with the brightness down). ~6:1 on the panel; white and teal stay brighter.
                                         MSP_DIM stays for shapes: dividers, the dots, the silhouettes */
#define MSP_TEAL      0x9fd8e2

typedef struct { uint32_t bit; const icon_t *icon; } badge_t;
static const badge_t FISH_BADGES[6] = {
    { MS_FIRST_MEAL_FROM_YOU, &icon_ms_first_meal },  { MS_FIRST_HOLD_APPROACH, &icon_ms_first_hold_approach },
    { MS_FIRST_GRASS, &icon_ms_first_seagrass },      { MS_FIRST_BUBBLES, &icon_ms_first_bubbles },   /* (the third was "first reef" until 0.3.0) */
    { MS_FIRST_FOLLOW, &icon_ms_first_follow },       { MS_FIRST_DART, &icon_ms_first_dart },
};
#define TANK_BADGE_N 11
static const badge_t TANK_BADGES[TANK_BADGE_N] = {
    { TMS_FIRST_FEEDING, &icon_ms_first_feeding },    { TMS_FIRST_TRIM, &icon_ms_first_trimming },
    { TMS_FIRST_CLEANING, &icon_ms_first_glass_cleaning }, { TMS_FIRST_FULL_NIGHT, &icon_ms_first_quiet_night },
    { TMS_FIRST_PLAY_SESSION, &icon_ms_first_play_session }, { TMS_CHANGED_SOMEONE, &icon_ms_tank_changed_someone },
    { TMS_FULL_SCHOOL, &icon_ms_full_school },        /* 2026-09-30: only once the shrimp are bought (tank_badge_shown) */
    { TMS_FIRST_REEF, &icon_ms_first_reef },          /* 2026-10-02: only once the reef cluster is bought */
    { TMS_FIRST_SALE, &icon_ms_first_sale },          /* 0.3.5: a fish sold - the shop's fry */
    { TMS_SEAHORSE_FED, &icon_ms_seahorse_fed },      /* 0.3.6: only once the seahorse is bought (Strato: "visible and gray once purchased") */
    { TMS_CRAB_FIND, &icon_ms_crab_find },            /* 0.3.9: only once the crab is bought - its first sand dollar, taken */
};
/* the tank row's pages (2026-09-30, the seventh badge): six badges a page;
   with more, a small arrow at the row's right end (or a sideways swipe along
   the row) flips to the next page, pointing back from the last. */
static int g_ms_tpage;                  /* the tank row's page (kept while a modal opens and closes; 0 on leaving the page) */
/* which tank badges show: a bought thing's badge only once the thing is
   bought (the shrimp school's, the reef cluster's) - and, once earned, for
   good, whatever was sold since; the first sale's only once earned */
static bool tank_badge_shown(const tank_t *t, int i) {
    uint32_t bit = TANK_BADGES[i].bit;
    if (bit == TMS_FULL_SCHOOL) return (t->sd_unlocks & SD_ITEM_SHRIMP)  || (t->tank_ms_bits & bit);
    if (bit == TMS_FIRST_REEF)  return (t->sd_unlocks & SD_ITEM_CLUSTER) || (t->tank_ms_bits & bit);
    if (bit == TMS_FIRST_SALE)  return (t->tank_ms_bits & bit) != 0;   /* 0.3.5: once earned (its notice says what it opened) */
    if (bit == TMS_SEAHORSE_FED) return (t->sd_unlocks & SD_ITEM_SEAHORSE) || (t->tank_ms_bits & bit);
    if (bit == TMS_CRAB_FIND)   return (t->sd_unlocks & SD_ITEM_CRAB)     || (t->tank_ms_bits & bit);
    return true;
}
static int tank_badge_n(const tank_t *t) {
    int n = 0;
    for (int i = 0; i < TANK_BADGE_N; i++) n += tank_badge_shown(t, i);
    return n;
}
/* the k-th badge that shows (k < tank_badge_n) */
static const badge_t *tank_badge(const tank_t *t, int k) {
    for (int i = 0; i < TANK_BADGE_N; i++)
        if (tank_badge_shown(t, i) && k-- == 0) return &TANK_BADGES[i];
    return &TANK_BADGES[0];
}
static int tank_pages(const tank_t *t) { return (tank_badge_n(t) + MSP_PER_ROW - 1) / MSP_PER_ROW; }
static const char *const STAGE_WORDS[4] = { "FRY", "JUVENILE", "ADULT", "ELDER" };
/* the detail modal (a tap on a badge / name / strip): what to show until
 * the next tap. caption[0] == 0 means no modal. */
static char g_ms_caption[72], g_ms_title[16];
static char g_ms_caption2[32];       /* the caption's second line (the fry checklist's sentences), or empty */
static char g_ms_sub[32];            /* a line under the caption (the fry checklist's progress), or empty */
static bool g_ms_lit;
static const icon_t *g_ms_icon;      /* the badge's art, or NULL */
static int  g_ms_fish = -1;          /* a fish's own sprite instead, or -1 */
static bool g_ms_fry;                /* the fry-to-be (a silhouette) instead */
static int  g_ms_kind = -1;          /* a gate's modal: its kind (the HOW? button shows), or -1 */
static bool g_ms_tip;                /* the gate's tip page is up instead of its modal */
/* where the modal came from (2026-09-16, Strato: arrows at the top corners
   to cycle without dropping back to the page): the row (a fish's index, or
   the TANK / NEW FRY row) and the column (-1 = the name / strip, else the
   badge or gate). ms_step moves along the group the modal belongs to: a
   fish's six badges, the tank's six, the fry's gates, or - from a fish's
   name - the fish themselves. A group of one (TANK's tally, NEW FRY's)
   shows no arrows. */
static int  g_ms_row = -1, g_ms_k = -1;
static bool g_ms_tankrow, g_ms_fryrow;
/* a fish's CARD (the modal behind its name) carries two buttons at its foot
   (2026-10-01, Strato): RENAME - the letter wheel over the live tank - and
   SELL. SELL is two taps on the placement page's idiom: the first arms it
   (the label turns to the price and OK?, the line above says TAP AGAIN), the
   second sells - but not within MSP_SELL_WAIT_S of the first, so a doubled
   tap cannot sell a fish. A fish the tank will not part with (the last pair,
   or a fry still owed its welcome) shows SELL dim, and the line above stays
   empty until the dim button is tapped - then it gives the reason (Strato,
   2026-10-01: the reason "only if the gray sell button is tapped"). The card stands where a gate's modal does (the taller one), inside
   the box every board's glass shows whole: no per-board layout. */
#define MSP_CARD_BTN_W   UI(116)
#define MSP_CARD_BTN_GAP UI(24)
#define MSP_SELL_WAIT_S  0.6f
static bool  g_ms_sell_armed;
static bool  g_ms_sell_asked;        /* the dim SELL was tapped: the line says why it stays */
static float g_ms_sell_clock;        /* the tank clock when SELL was armed */

/* the TANK tally's picture (2026-10-02, Strato: the modal "feels bare"): the
 * school itself across the top of the box - every fish in its own colours,
 * at its own size and stage, in the order they arrived - then a dim
 * silhouette for each place still open. The count is in the words under it;
 * here it is seen (lit against dim, never by hue alone). */
#define MSP_SCHOOL_PITCH UI(52)
#define MSP_SCHOOL_SCALE (0.74f * UI_F)
#define MSP_SCHOOL_MAX   (1.02f * UI_F)   /* the size that still fits its place (a big elder) */
static void ms_school(const tank_t *t, uint16_t *fb, int stride, int X, int Y, int W) {
    for (int i = 0; i < POP_CAP; i++) {
        float cx = X + (W - POP_CAP * MSP_SCHOOL_PITCH) / 2 + MSP_SCHOOL_PITCH / 2 + UI(4) + i * MSP_SCHOOL_PITCH;   /* (+4: a fish's centre sits ahead of its middle) */
        if (i < t->n_fish) {
            float sz = t->fish[i].size * MSP_SCHOOL_SCALE;
            render_fish_portrait(fb, stride, cx, Y + UI(50), sz > MSP_SCHOOL_MAX ? MSP_SCHOOL_MAX : sz, &t->fish[i], t->clock + i * 0.9f);
        } else render_fish_preview(fb, stride, cx, Y + UI(50), 0.9f * MSP_SCHOOL_SCALE, MSP_DIM, MSP_DIM, MSP_DIM, t->clock + i * 0.9f);
    }
}

/* a locked badge: the same art as a flat grey silhouette - luminance keeps
 * the shapes readable, the low alpha keeps it quiet on the ink */
static uint32_t locked_rgb(uint16_t v) {
    int r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3;
    int l = (r * 77 + g * 151 + b * 28) >> 8;                /* 0..255 luminance */
    int k = 140 + l * 115 / 255;                              /* 0.55 .. 1.0, in 1/255 */
    return (uint32_t)(20 + (0x2a * k >> 8)) << 16 | (uint32_t)(20 + (0x3f * k >> 8)) << 8
         | (uint32_t)(20 + (0x45 * k >> 8));
}
static void blit_icon_locked(ctx_t *c, int x, int y, const icon_t *ic) {
    for (int j = 0; j < ic->h; j++)
        for (int i = 0; i < ic->w; i++) {
            int a = ic->a[j * ic->w + i];
            if (a) px_blend(c, x + i, y + j, locked_rgb(ic->rgb[j * ic->w + i]), a * 180 >> 8);
        }
}
/* the same art at an integer scale (the detail modal), lit or as the silhouette */
static void blit_icon_scaled(ctx_t *c, int x, int y, const icon_t *ic, int s, bool lit) {
    for (int j = 0; j < ic->h; j++)
        for (int i = 0; i < ic->w; i++) {
            int a = ic->a[j * ic->w + i];
            if (!a) continue;
            uint16_t v = ic->rgb[j * ic->w + i];
            for (int yy = 0; yy < s; yy++)
                for (int xx = 0; xx < s; xx++) {
                    if (lit) px565_blend(c, x + i * s + xx, y + j * s + yy, v, a);
                    else     px_blend(c, x + i * s + xx, y + j * s + yy, locked_rgb(v), a * 180 >> 8);
                }
        }
}
/* a tiny fish glyph facing right (the growth and population strips) */
static void fish_glyph(ctx_t *c, float cx, float cy, float r, uint32_t rgb) {
    float xs[3] = { cx - r * 1.2f, cx - r * 2.3f, cx - r * 2.3f }, ys[3] = { cy, cy - r * 0.9f, cy + r * 0.9f };
    fill_poly(c, xs, ys, 3, rgb);
    fill_ellipse(c, cx, cy, r * 1.6f, r, rgb, 255);
}
/* the page's art for a badge: the 24 px copy on the CYD (see MSP_ICON), the
   art itself otherwise. The detail modal keeps the full size. Every ms_*
   badge needs its pair here: one missing draws 32 px art in a 24 px slot,
   over its neighbours, with no error. */
static const icon_t *badge_art(const icon_t *ic) {
#ifdef CONFIG_POCKET_TANK_320X240
    static const struct { const icon_t *full, *small; } SMALL[] = {
        { &icon_ms_first_bubbles, &icon_ms_first_bubbles_24 },     { &icon_ms_first_dart, &icon_ms_first_dart_24 },
        { &icon_ms_first_feeding, &icon_ms_first_feeding_24 },     { &icon_ms_first_follow, &icon_ms_first_follow_24 },
        { &icon_ms_first_glass_cleaning, &icon_ms_first_glass_cleaning_24 },
        { &icon_ms_first_hold_approach, &icon_ms_first_hold_approach_24 },
        { &icon_ms_first_meal, &icon_ms_first_meal_24 },           { &icon_ms_first_play_session, &icon_ms_first_play_session_24 },
        { &icon_ms_first_quiet_night, &icon_ms_first_quiet_night_24 }, { &icon_ms_first_reef, &icon_ms_first_reef_24 },
        { &icon_ms_first_sale, &icon_ms_first_sale_24 },           /* (v0.3.6's sync) the tank row's FIRST FISH SOLD (0.3.5) */
        { &icon_ms_first_seagrass, &icon_ms_first_seagrass_24 },   { &icon_ms_full_school, &icon_ms_full_school_24 },
        { &icon_ms_first_trimming, &icon_ms_first_trimming_24 },   { &icon_ms_sand_dollar, &icon_ms_sand_dollar_24 },
        { &icon_ms_seahorse_fed, &icon_ms_seahorse_fed_24 },       /* (v0.3.6's sync) ... and FIRST SEAHORSE FEEDING (0.3.6) */
        { &icon_ms_tank_changed_someone, &icon_ms_tank_changed_someone_24 },
        { &icon_ms_crab_find, &icon_ms_crab_find_24 },             /* (v0.3.9's sync) the tank row's FIRST CRAB FIND (0.3.9) */
    };
    for (unsigned i = 0; i < sizeof SMALL / sizeof SMALL[0]; i++)
        if (SMALL[i].full == ic) return SMALL[i].small;
#endif
    return ic;
}
static void badge(ctx_t *c, int x, int y, const icon_t *ic, bool on, bool fresh) {
    ic = badge_art(ic);
    if (on) blit_icon(c, x, y, ic, 255); else blit_icon_locked(c, x, y, ic);
    if (on && fresh) {
        rect_edge(c, x - UI(3), y - UI(3), MSP_ICON + 2 * UI(3), MSP_ICON + 2 * UI(3), MSP_TEAL);
        rect_edge(c, x - UI(4), y - UI(4), MSP_ICON + 2 * UI(4), MSP_ICON + 2 * UI(4), 0x3f6a72);
    }
}
static int bit_index(uint32_t bit) { int i = 0; while (bit > 1u) { bit >>= 1; i++; } return i; }
/* the art for a gate of the fry checklist: the card's trust icon, the tank's
   own badges for meals / a hold / grass / a change / a wiped glass, or NULL = the youngest
   fish itself (the GROW gate) */
static const icon_t *fry_req_icon(int kind) {
    switch (kind) {
    case FRY_REQ_TRUST:  return &icon_trust;
    case FRY_REQ_FEED:   return &icon_ms_first_feeding;
    case FRY_REQ_HOLD:   return &icon_ms_first_hold_approach;
    case FRY_REQ_CHANGE: return &icon_ms_tank_changed_someone;
    case FRY_REQ_GRASS:  return &icon_ms_first_trimming;
    case FRY_REQ_GLASS:  return &icon_ms_first_glass_cleaning;
    default:             return NULL;
    }
}
/* a gate as a badge in a 32 px cell: lit when met, the silhouette until then */
static void fry_badge(ctx_t *c, uint16_t *fb, int stride, const tank_t *t, int x, int y, const fry_req_t *r) {
    const icon_t *ic = fry_req_icon(r->kind);
    if (ic) ic = badge_art(ic);
    if (ic) {
        int off = (MSP_ICON - ic->w) / 2;        /* the 24 px card icon sits centred */
        if (r->met) blit_icon(c, x + off, y + off, ic, 255); else blit_icon_locked(c, x + off, y + off, ic);
    } else {                                     /* GROW: the youngest fish, its own colours once grown */
        const fish_t *f = &t->fish[t->n_fish - 1];
        float sz = (f->size > 0.7f ? 0.7f : f->size) * UI_F;
        if (r->met) render_fish_preview(fb, stride, x + MSP_ICON / 2, y + MSP_ICON / 2, sz, f->color, f->fin, f->accent, t->clock);
        else        render_fish_preview(fb, stride, x + MSP_ICON / 2, y + MSP_ICON / 2, sz, MSP_DIM, MSP_DIM, MSP_DIM, t->clock);
    }
}

/* a chevron pointing left or right, its tip at (tx, cy): 3 px steps, 24 px tall */
static void ms_chevron(ctx_t *c, int tx, int cy, bool left, uint32_t rgb) {
    for (int i = 0; i < 4; i++) {
        int xx = left ? tx + i * 3 : tx - 3 - i * 3;
        rect_fill(c, xx, cy - 3 - i * 3, 3, 3, rgb);
        rect_fill(c, xx, cy + i * 3, 3, 3, rgb);
    }
}
/* the modal's group: how many things the arrows cycle through and where
   this one sits. nreq = the fry checklist's gate count (the caller has it). */
static int ms_group(const tank_t *t, int nreq, int *idx) {
    if (g_ms_fryrow)  { *idx = g_ms_k; return g_ms_k < 0 ? 1 : nreq; }
    if (g_ms_tankrow) { *idx = g_ms_k; return g_ms_k < 0 ? 1 : tank_badge_n(t); }
    if (g_ms_k < 0)   { *idx = g_ms_row; return t->n_fish; }    /* a fish's name: the fish */
    *idx = g_ms_k; return 6;
}
static int ms_open(const tank_t *t, int row, bool tank_row, bool fry_row, int k,
                   const fry_req_t *req, int nreq, bool staged);
static void ms_close(void);
/* the modal's box: a gate's and a fish's card are taller (a button row at the
   foot) and stand higher, clear of the CLOSE button */
static bool ms_card(const tank_t *t) { return g_ms_caption[0] && !g_ms_tip && !g_ms_fryrow && !g_ms_tankrow && g_ms_k < 0 && g_ms_row >= 0 && g_ms_row < t->n_fish; }
static int  ms_modal_y(const tank_t *t) { return g_ms_kind >= 0 || ms_card(t) ? MSP_FRY_MODAL_Y : MSP_MODAL_Y; }
static int  ms_modal_h(const tank_t *t) {
    return MSP_MODAL_H + (g_ms_caption2[0] ? UI(20) : 0) + (g_ms_sub[0] || ms_card(t) ? UI(24) : 0) + (g_ms_kind >= 0 || ms_card(t) ? MSP_HOW_H + UI(14) : 0);
}
static int  ms_card_btn_y(const tank_t *t) { return ms_modal_y(t) + ms_modal_h(t) - UI(10) - MSP_HOW_H; }
static int  ms_card_btn_x(int which) {            /* 0 = RENAME, 1 = SELL: the pair centred */
    return MSP_MODAL_X + (MSP_MODAL_W - 2 * MSP_CARD_BTN_W - MSP_CARD_BTN_GAP) / 2 + which * (MSP_CARD_BTN_W + MSP_CARD_BTN_GAP);
}
/* the arrows: the previous / next thing in the modal's group, wrapping */
static void ms_step(const tank_t *t, int dir) {
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    int idx, n = ms_group(t, nreq, &idx);
    if (n < 2) return;
    g_ms_sell_armed = false; g_ms_sell_asked = false;
    idx = (idx + dir + n) % n;
    if (!g_ms_fryrow && !g_ms_tankrow && g_ms_k < 0) ms_open(t, idx, false, false, -1, req, nreq, staged);
    else ms_open(t, g_ms_row, g_ms_tankrow, g_ms_fryrow, idx, req, nreq, staged);
}

void render_milestones(const tank_t *t, uint16_t *fb, int stride) {
    ctx_t c = ctx_page(fb, stride);
    for (int y = 0; y < TANK_H; y++)
        for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = FB_SWAP16(rgb565(MSP_INK, 1));
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i];
        int top = MSP_ROW_Y0 + i * MSP_ROW_H;
        render_fish_preview(fb, stride, MSP_FISH_X, top + MSP_ROW_MID, f->size * UI_F, f->color, f->fin, f->accent, t->clock);
        draw_text(&c, MSP_NAME_X, top + UI(2), UI_TEXT(2), 0xffffff, f->name);
        static const int GX[4] = { UI(4), UI(19), UI(37), UI(59) };   /* each glyph's own pitch: a 4 px gap as they grow */
        for (int s = 0; s < 4; s++)          /* growth strip: fry -> elder, lit up to the stage reached */
            fish_glyph(&c, MSP_NAME_X + GX[s], top + MSP_ROW_STRIP, (2.2f + s * 0.9f) * UI_F, s <= (int)f->stage ? f->accent : MSP_DIM);
        for (int k = 0; k < 6; k++) {
            uint32_t bit = FISH_BADGES[k].bit;
            badge(&c, MSP_BADGE_X0 + k * MSP_BADGE_DX, top + MSP_ROW_BADGE, FISH_BADGES[k].icon,
                  (f->ms_bits & bit) != 0, (f->ms_seen & bit) == 0);
        }
    }
    /* the NEW FRY row (2026-09-14): under the last fish while the tank can
       still grow - what the next arrival needs, as badges that light up when
       met (the fry-to-be a silhouette at the left, a tick per gate under
       its name, a filling bar under each badge still owed) */
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    if (nreq > 0) {
        int top = MSP_ROW_Y0 + t->n_fish * MSP_ROW_H;
        uint32_t fry_rgb = staged ? MSP_TEAL : MSP_DIM;
        render_fish_preview(fb, stride, MSP_FISH_X, top + MSP_ROW_MID, 0.55f * UI_F, fry_rgb, fry_rgb, fry_rgb, t->clock);
        draw_text(&c, MSP_NAME_X, top + UI(2), UI_TEXT(2), MSP_TEAL, "NEW FRY");
        for (int k = 0; k < nreq; k++)           /* one tick per gate, lit when met */
            rect_fill(&c, MSP_NAME_X + UI(4) + k * UI(14), top + MSP_ROW_STRIP - UI(3), UI(10), UI(6), req[k].met ? MSP_TEAL : MSP_DIM);
        for (int k = 0; k < nreq; k++) {
            int x = MSP_BADGE_X0 + k * MSP_BADGE_DX;
            fry_badge(&c, fb, stride, t, x, top + MSP_ROW_BADGE, &req[k]);
            if (!req[k].met) {                   /* the bar: how far along */
                int w = (int)(MSP_ICON * req[k].frac + 0.5f);
                rect_fill(&c, x, top + MSP_ROW_BAR, MSP_ICON, 2, MSP_DIM);
                if (w > 0) rect_fill(&c, x, top + MSP_ROW_BAR, w, 2, MSP_TEAL);
            }
        }
    }
    /* the tank's row: the sand dollar at the left (the fish rows' portrait
       slot) opens the shop (2026-09-15) */
    for (int x = MSP_RULE_X0; x < MSP_RULE_X1; x++) px_blend(&c, x, MSP_TANK_Y - UI(4), MSP_DIM, 200);
    blit_icon(&c, MSP_SD_X, MSP_TANK_Y, badge_art(&icon_ms_sand_dollar), 255);
    {   /* the balance under the coin (Strato, 2026-09-15), centred on it - the
           only room: the divider and the last row sit above, 12 px to the left */
        char bal[16]; snprintf(bal, sizeof bal, "%d", (int)t->sd_balance);
        draw_text(&c, MSP_SD_X + (MSP_ICON - text_w(bal, UI_TEXT(2))) / 2, MSP_TANK_Y + MSP_ICON + 2, UI_TEXT(2), 0xffffff, bal);
    }
    draw_text(&c, MSP_NAME_X, MSP_TANK_Y + UI(2), UI_TEXT(2), MSP_TEAL, "TANK");
    for (int k = 0; k < POP_CAP; k++)        /* population strip: who is here, who could still arrive */
        fish_glyph(&c, MSP_NAME_X + UI(4) + k * UI(14), MSP_TANK_Y + UI(30), 2.8f * UI_F, k < t->n_fish ? MSP_TEAL : MSP_DIM);
    int nb = tank_badge_n(t), np = tank_pages(t);
    if (g_ms_tpage >= np) g_ms_tpage = 0;
    for (int j = 0; j < MSP_PER_ROW; j++) {
        int k = g_ms_tpage * MSP_PER_ROW + j;
        if (k >= nb) break;
        const badge_t *tb = tank_badge(t, k);
        badge(&c, MSP_BADGE_X0 + j * MSP_BADGE_DX, MSP_TANK_Y + MSP_TANK_BADGE, tb->icon,
              (t->tank_ms_bits & tb->bit) != 0, (t->tank_ms_seen & tb->bit) == 0);
    }
    if (np > 1) {   /* the page arrow: toward the next page, back from the last; the badges' two-tone
                       ring round it while another page holds a badge not seen yet */
        bool last = g_ms_tpage == np - 1, fresh = false;
        for (int k = 0; k < nb; k++)
            if (k / MSP_PER_ROW != g_ms_tpage && (t->tank_ms_bits & ~t->tank_ms_seen & tank_badge(t, k)->bit)) fresh = true;
        int ax = MSP_TPG_X + 4, ay = MSP_TANK_Y + MSP_TANK_BADGE, aw = 16;
        ms_chevron(&c, last ? ax + 2 : ax + aw - 2, ay + MSP_ICON / 2, last, 0xffffff);
        if (fresh) { rect_edge(&c, ax - 3, ay - 3, aw + 6, MSP_ICON + 6, MSP_TEAL);
                     rect_edge(&c, ax - 4, ay - 4, aw + 8, MSP_ICON + 8, 0x3f6a72); }
        for (int p = 0; p < np; p++)        /* a pip per page under it, the current one lit */
            rect_fill(&c, ax + aw / 2 - np * 3 + p * 6 + 1, MSP_TANK_Y + UI(40), 4, 2, p == g_ms_tpage ? MSP_TEAL : MSP_DIM);
    }
    /* the way out: a CLOSE button in the prompt's calm dress (a tap anywhere
       else never drops the page - too much to tap for that) */
    button(&c, MSP_FOOT_CLOSE_X, MSP_FOOT_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", UI_TEXT(2));   /* (the foot: render.h MSP_FOOT_Y, pinned to the bottom) */
    button(&c, MSP_FOOT_UPG_X, MSP_FOOT_Y, MSP_UPG_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "UPGRADES", UI_TEXT(2));   /* the shop: bottom left (the bowl: left of CLOSE) */
    button(&c, MSP_FOOT_SET_X, PAGE_BOWL ? MSP_SET_Y : MSP_FOOT_Y, MSP_SET_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "SETTINGS", UI_TEXT(2));   /* between them (Strato, 2026-10-24: swapped with
                                                                                                        UPGRADES, bottom left since 2026-09-15; the bowl's sits in the cap) */
    /* a modal up: the page under it is out of reach (any tap only closes the
       modal), so it LOOKS out of reach - every pixel at half (Strato: with
       CLOSE lit it looked like you could still tap it). One shift per
       pixel, so cheap enough for every frame. */
    if (g_ms_caption[0])
        for (int y = 0; y < TANK_H; y++)
            for (int x = 0; x < TANK_W; x++) { uint16_t n = FB_SWAP16(fb[y * stride + x]); fb[y * stride + x] = FB_SWAP16((uint16_t)((n >> 1) & 0x7bef)); }
    /* the detail modal, in the reset prompt's dress: the art at 2x, a title
       (the fish's name / TANK, or NOT YET), the milestone's words */
    if (g_ms_caption[0] && g_ms_tip) {
        /* the tip page: HOW, then the gate's lines; any tap closes it */
        const int X = MSP_MODAL_X, Y = MSP_FRY_MODAL_Y, W = MSP_MODAL_W;
        const char *const *tip = progression_fry_tip(g_ms_kind);
        int n = 0; while (tip[n]) n++;
        const int H = UI(56) + n * UI(20) + UI(16);
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        char title[24]; snprintf(title, sizeof title, "%s: HOW", g_ms_title);
        draw_text(&c, X + (W - text_w(title, UI_TEXT(3))) / 2, Y + UI(16), UI_TEXT(3), 0xffffff, title);
        for (int i = 0; i < n; i++) draw_text(&c, X + (W - text_w(tip[i], UI_TEXT(2))) / 2, Y + UI(56) + i * UI(20), UI_TEXT(2), MSP_TEAL, tip[i]);
    } else if (g_ms_caption[0]) {
        const int X = MSP_MODAL_X, W = MSP_MODAL_W;
        const int Y = ms_modal_y(t), H = ms_modal_h(t);
        const bool card = ms_card(t);
        if (card) {                                /* a fish's card is live: its name and stage as they are now, what it
                                                      would fetch (or why it stays), and the two buttons */
            const fish_t *f = &t->fish[g_ms_row];
            bool can = progression_fish_sellable(t, g_ms_row);
            int worth = progression_fish_value(t, g_ms_row);
            if (!can) g_ms_sell_armed = false; else g_ms_sell_asked = false;
            snprintf(g_ms_title, sizeof g_ms_title, "%s", f->name);
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", STAGE_WORDS[f->stage & 3]);
            if (g_ms_sell_armed) snprintf(g_ms_sub, sizeof g_ms_sub, "TAP AGAIN TO SELL");
            else if (can) snprintf(g_ms_sub, sizeof g_ms_sub, "WORTH %d SAND DOLLARS", worth);
            else if (g_ms_sell_asked) snprintf(g_ms_sub, sizeof g_ms_sub, t->n_fish <= FISH_KEEP_MIN ? "KEEP AT LEAST TWO FISH" : "NAME THE NEW FRY FIRST");
            else g_ms_sub[0] = 0;                  /* (the line's room stays: the buttons never move) */
        }
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        if (g_ms_kind >= 0)                        /* the way further in: HOW?, centred at the foot (Strato: bottom
                                                      right sat too close to CLOSE for comfort) */
            button(&c, X + (W - MSP_HOW_W) / 2, Y + H - UI(10) - MSP_HOW_H, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "HOW?", UI_TEXT(2));
        if (card) {
            const int by = ms_card_btn_y(t);
            button(&c, ms_card_btn_x(0), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "RENAME", UI_TEXT(2));
            if (!progression_fish_sellable(t, g_ms_row)) {           /* not for sale: the button in the page's dim ink, the words too */
                rect_fill(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x04141a);
                rect_edge(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, MSP_DIM);
                draw_text(&c, ms_card_btn_x(1) + (MSP_CARD_BTN_W - text_w("SELL", UI_TEXT(2))) / 2, by + (MSP_HOW_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_QUIET, "SELL");
            } else if (g_ms_sell_armed) {
                char ok[16]; snprintf(ok, sizeof ok, "+%d OK?", progression_fish_value(t, g_ms_row));
                button(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x155e58, 0x38dcc7, ok, UI_TEXT(2));
            } else button(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "SELL", UI_TEXT(2));
        }
        if (g_ms_icon) blit_icon_scaled(&c, X + (W - g_ms_icon->w * UI_TEXT(2)) / 2, Y + UI(16) + (UI(32) - g_ms_icon->w * UI_TEXT(2) / 2), g_ms_icon, UI_TEXT(2), g_ms_lit);
        else if (g_ms_fish >= 0 && g_ms_fish < t->n_fish) {
            const fish_t *f = &t->fish[g_ms_fish];
            if (g_ms_lit) render_fish_preview(fb, stride, X + W / 2, Y + UI(48), f->size * 1.6f * UI_F, f->color, f->fin, f->accent, t->clock);
            else          render_fish_preview(fb, stride, X + W / 2, Y + UI(48), f->size * 1.6f * UI_F, MSP_DIM, MSP_DIM, MSP_DIM, t->clock);
        } else if (g_ms_fry)
            render_fish_preview(fb, stride, X + W / 2, Y + UI(48), 0.9f * UI_F, g_ms_lit ? MSP_TEAL : MSP_DIM, g_ms_lit ? MSP_TEAL : MSP_DIM, g_ms_lit ? MSP_TEAL : MSP_DIM, t->clock);
        else if (g_ms_tankrow && g_ms_k < 0) ms_school(t, fb, stride, X, Y, W);   /* TANK's tally: the school */
        draw_text(&c, X + (W - text_w(g_ms_title, UI_TEXT(3))) / 2, Y + UI(92), UI_TEXT(3), g_ms_lit ? 0xffffff : MSP_TEAL, g_ms_title);
        draw_text(&c, X + (W - text_w(g_ms_caption, UI_TEXT(2))) / 2, Y + UI(124), UI_TEXT(2), g_ms_lit ? MSP_TEAL : 0x5f8a92, g_ms_caption);
        int ly = Y + UI(144);
        if (g_ms_caption2[0]) { draw_text(&c, X + (W - text_w(g_ms_caption2, UI_TEXT(2))) / 2, ly, UI_TEXT(2), g_ms_lit ? MSP_TEAL : 0x5f8a92, g_ms_caption2); ly += UI(20); }
        if (g_ms_sub[0]) draw_text(&c, X + (W - text_w(g_ms_sub, UI_TEXT(2))) / 2, ly + UI(4), UI_TEXT(2), g_ms_lit ? 0xffffff : MSP_TEAL, g_ms_sub);
        {   /* the arrows (2026-09-16): the previous / next of the group at the
               top corners, in the buttons' dress, only when there is a group */
            int idx, n = ms_group(t, nreq, &idx);
            if (n > 1) {
                int ax = X + MSP_ARROW_IN, ay = Y + MSP_ARROW_IN, bx = X + W - MSP_ARROW_IN - MSP_ARROW_W;
                rect_fill(&c, ax, ay, MSP_ARROW_W, MSP_ARROW_H, 0x1c2f36);
                rect_edge(&c, ax, ay, MSP_ARROW_W, MSP_ARROW_H, MSP_TEAL); rect_edge(&c, ax + 1, ay + 1, MSP_ARROW_W - 2, MSP_ARROW_H - 2, MSP_TEAL);
                ms_chevron(&c, ax + UI(14), ay + MSP_ARROW_H / 2, true, 0xffffff);
                rect_fill(&c, bx, ay, MSP_ARROW_W, MSP_ARROW_H, 0x1c2f36);
                rect_edge(&c, bx, ay, MSP_ARROW_W, MSP_ARROW_H, MSP_TEAL); rect_edge(&c, bx + 1, ay + 1, MSP_ARROW_W - 2, MSP_ARROW_H - 2, MSP_TEAL);
                ms_chevron(&c, bx + MSP_ARROW_W - UI(14), ay + MSP_ARROW_H / 2, false, 0xffffff);
            }
        }
    }
}

int render_milestones_tap(const tank_t *t, float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;                    /* the page's own coordinates */
    if (g_ms_caption[0]) {                       /* a modal is up */
        if (!g_ms_tip) {                         /* the arrows at its top corners: the previous / next of the group */
            fry_req_t req[FRY_REQ_MAX]; bool staged; int idx;
            int n = ms_group(t, progression_next_fry(t, req, &staged), &idx);
            const int Y = ms_modal_y(t);
            if (n > 1 && y >= Y - UI(12) && y < Y + MSP_ARROW_IN + MSP_ARROW_H + UI(24)) {
                if (x < MSP_MODAL_X + MSP_ARROW_HIT)               { ms_step(t, -1); return MS_TAP_KEPT; }
                if (x >= MSP_MODAL_X + MSP_MODAL_W - MSP_ARROW_HIT) { ms_step(t, +1); return MS_TAP_KEPT; }
            }
        }
        if (ms_card(t)) {                        /* a fish's card: RENAME / SELL across its foot - each button's half of
                                                    the panel, the HOW? button's depth (a miss costs the modal) */
            const int by = ms_card_btn_y(t), fish = g_ms_row;
            if (x >= MSP_MODAL_X && x < MSP_MODAL_X + MSP_MODAL_W && y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN) {
                if (x < MSP_MODAL_X + MSP_MODAL_W / 2) { ms_close(); return MS_TAP_RENAME + fish; }   /* the platform opens the wheel */
                if (!progression_fish_sellable(t, fish)) { g_ms_sell_asked = true; return MS_TAP_KEPT; }   /* the line says why, from now on */
                if (!g_ms_sell_armed) { g_ms_sell_armed = true; g_ms_sell_clock = t->clock; return MS_TAP_KEPT; }
                if (t->clock - g_ms_sell_clock < MSP_SELL_WAIT_S) return MS_TAP_KEPT;                /* a doubled tap: still armed */
                ms_close(); return MS_TAP_SELL + fish;                                               /* the platform sells it */
            }
        }
        if (g_ms_kind >= 0 && !g_ms_tip) {       /* a gate's: the HOW? button opens its tip page */
            const int H = ms_modal_h(t);
            const int bx = MSP_MODAL_X + (MSP_MODAL_W - MSP_HOW_W) / 2, by = MSP_FRY_MODAL_Y + H - UI(10) - MSP_HOW_H;
            if (x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X && y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN) {
                g_ms_tip = true; return MS_TAP_KEPT; }
        }
        ms_close(); return MS_TAP_KEPT;                  /* any other tap: back to the page */
    }
#ifdef TANK_ROUND                                    /* the bowl: SETTINGS has the cap above the rows; the foot is UPGRADES | CLOSE */
    if (y < MSP_SET_Y + MSP_CLOSE_H + 14) return x >= MSP_SET_X - 24 && x < MSP_SET_X + MSP_SET_W + 24 ? MS_TAP_SETTINGS : MS_TAP_NONE;
    if (y >= MSP_FOOT_HIT_Y) return x >= MSP_FOOT_CLOSE_X - 6 ? MS_TAP_CLOSE : MS_TAP_SHOP;
#else
    if (x >= MSP_FOOT_CLOSE_X - MSP_FOOT_SLOP && y >= MSP_FOOT_HIT_Y) return MS_TAP_CLOSE;           /* slop out to the glass edge */
    if (x < MSP_FOOT_UPG_X + MSP_UPG_W + MSP_FOOT_SLOP && y >= MSP_FOOT_HIT_Y) return MS_TAP_SHOP;  /* UPGRADES, bottom left: the shop */
    if (y >= MSP_FOOT_HIT_Y) return MS_TAP_SETTINGS;                                     /* SETTINGS: the rest of the strip (2026-10-24: the two swapped) */
#endif
    int row = -1; bool tank_row = false, fry_row = false;
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    if (y >= MSP_ROW_Y0 - UI(2) && y < MSP_ROW_Y0 + N_FISH_MAX * MSP_ROW_H) {
        row = (int)((y - MSP_ROW_Y0) / MSP_ROW_H);
        if (row < 0) row = 0;
        if (row == t->n_fish && nreq > 0) fry_row = true;   /* the NEW FRY row */
        else if (row >= t->n_fish) return MS_TAP_NONE;      /* an empty row */
    } else if (y >= MSP_TANK_Y - UI(6) && y < MSP_FOOT_HIT_Y) tank_row = true;    /* down to the button strip: fingers near the
                                                                              bottom bezel report LOW */
    else return MS_TAP_NONE;
    if (tank_row && x >= MSP_TPG_X && tank_pages(t) > 1) {   /* the page arrow: the next page, wrapping */
        g_ms_tpage = (g_ms_tpage + 1) % tank_pages(t); return MS_TAP_KEPT; }
    int k;                                           /* badge column, or -1 for the name / strip cluster */
    if (x >= MSP_BADGE_X0 - UI(4) && x < MSP_BADGE_X0 + 6 * MSP_BADGE_DX) {
        k = (int)((x - MSP_BADGE_X0 + UI(4)) / MSP_BADGE_DX);
        if (k > 5) k = 5;
    } else if (x >= MSP_HIT_X0 && x < MSP_BADGE_X0 - UI(4)) k = -1;
    else return MS_TAP_NONE;
    if (tank_row && x < MSP_NAME_X - UI(4)) return MS_TAP_SHOP;      /* the sand dollar: the shop page */
    if (tank_row && k >= 0) {                        /* this page's badge; an empty cell on the last page is nothing */
        k += g_ms_tpage * MSP_PER_ROW;
        if (k >= tank_badge_n(t)) return MS_TAP_NONE;
    }
    return ms_open(t, row, tank_row, fry_row, k, req, nreq, staged);
}
/* open the detail modal for a row's name / strip (k < 0) or its k-th badge
   or gate: the words, the art, and where it came from (the arrows' group) */
static int ms_open(const tank_t *t, int row, bool tank_row, bool fry_row, int k,
                   const fry_req_t *req, int nreq, bool staged) {
    g_ms_caption2[0] = 0; g_ms_sub[0] = 0; g_ms_fry = false; g_ms_kind = -1; g_ms_tip = false; g_ms_sell_armed = false; g_ms_sell_asked = false;
    g_ms_row = row; g_ms_k = k; g_ms_tankrow = tank_row; g_ms_fryrow = fry_row;
    if (fry_row) {
        if (k < 0) {                                 /* the name: the tally, and when it comes */
            int met = 0; for (int i = 0; i < nreq; i++) met += req[i].met;
            snprintf(g_ms_title, sizeof g_ms_title, "NEW FRY");
            if (staged) { snprintf(g_ms_caption, sizeof g_ms_caption, "EVERY STEP IS DONE. A FRY");   /* the spawning, 2026-09-24 */
                          snprintf(g_ms_caption2, sizeof g_ms_caption2, "WILL BE BORN IN THE GRASS");
                          snprintf(g_ms_sub, sizeof g_ms_sub, "ON ITS WAY"); }
            else { snprintf(g_ms_caption, sizeof g_ms_caption, "WHEN ALL NEEDS ARE MET, A");     /* Strato's words, 2026-09-14 */
                   snprintf(g_ms_caption2, sizeof g_ms_caption2, "NEW FRY IS READY TO BE BORN");
                   snprintf(g_ms_sub, sizeof g_ms_sub, "%d OF %d DONE", met, nreq); }
            g_ms_lit = staged; g_ms_icon = NULL; g_ms_fish = -1; g_ms_fry = true;
        } else if (k < nreq) {                       /* a gate: the words, and where it stands */
            const fry_req_t *r = &req[k];
            snprintf(g_ms_title, sizeof g_ms_title, "%s", r->title);
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", r->words);
            snprintf(g_ms_caption2, sizeof g_ms_caption2, "%s", r->words2);
            snprintf(g_ms_sub, sizeof g_ms_sub, "%s", r->progress);
            g_ms_lit = r->met; g_ms_icon = fry_req_icon(r->kind); g_ms_kind = r->kind;
            g_ms_fish = g_ms_icon ? -1 : t->n_fish - 1;
        } else return MS_TAP_NONE;
    } else if (tank_row) {
        if (k < 0) {
            snprintf(g_ms_title, sizeof g_ms_title, "TANK");
            snprintf(g_ms_caption, sizeof g_ms_caption, "%d OF %d FISH SO FAR", t->n_fish, POP_CAP);
            g_ms_lit = true; g_ms_icon = NULL; g_ms_fish = -1;
        } else {
            uint32_t bit = tank_badge(t, k)->bit; bool on = (t->tank_ms_bits & bit) != 0;
            g_ms_tpage = k / MSP_PER_ROW;            /* the arrows can cross pages: the row follows */
            snprintf(g_ms_title, sizeof g_ms_title, on ? "TANK" : "NOT YET");
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", TMS_NAMES[bit_index(bit)]);
            g_ms_lit = on; g_ms_icon = tank_badge(t, k)->icon; g_ms_fish = -1;
        }
    } else {
        const fish_t *f = &t->fish[row];
        if (k < 0) {
            snprintf(g_ms_title, sizeof g_ms_title, "%s", f->name);
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", STAGE_WORDS[f->stage & 3]);
            g_ms_lit = true; g_ms_icon = NULL; g_ms_fish = row;
        } else {
            uint32_t bit = FISH_BADGES[k].bit; bool on = (f->ms_bits & bit) != 0;
            snprintf(g_ms_title, sizeof g_ms_title, "%s", on ? f->name : "NOT YET");
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", MS_NAMES[bit_index(bit)]);
            g_ms_lit = on; g_ms_icon = FISH_BADGES[k].icon; g_ms_fish = -1;
        }
    }
    return MS_TAP_KEPT;
}
/* a sideways swipe along the TANK row: the next page (leftward) or the
   previous (rightward), no wrap. False = not a page swipe (the platforms then
   treat the release as they did before). */
bool render_milestones_swipe(const tank_t *t, float x, float y, float dx) {
    (void)x; y -= PAGE_Y;
    if (g_ms_caption[0] || tank_pages(t) < 2 || y < MSP_TANK_Y - UI(6) || y >= MSP_FOOT_HIT_Y) return false;
    int p = g_ms_tpage + (dx < 0 ? 1 : -1);
    if (p >= 0 && p < tank_pages(t)) g_ms_tpage = p;
    return true;
}
/* the modal down, the page (and the tank row's page) kept */
static void ms_close(void) { g_ms_sell_armed = false; g_ms_sell_asked = false; g_ms_kind = -1; g_ms_tip = false; g_ms_caption[0] = 0; g_ms_caption2[0] = 0; g_ms_title[0] = 0; g_ms_sub[0] = 0; g_ms_icon = NULL; g_ms_fish = -1; g_ms_fry = false;
                                     g_ms_row = -1; g_ms_k = -1; g_ms_tankrow = false; g_ms_fryrow = false; }
void render_milestones_leave(void) { ms_close(); g_ms_tpage = 0; }
void render_milestones_show_fish(const tank_t *t, int fish) {
    if (fish >= 0 && fish < t->n_fish) ms_open(t, fish, false, false, -1, NULL, 0, false);
}
bool render_milestones_card(const tank_t *t, int *fish, int *rename_x, int *sell_x, int *btn_y) {
    if (!ms_card(t)) return false;
    if (fish) *fish = g_ms_row;
    if (rename_x) *rename_x = PAGE_X + ms_card_btn_x(0) + MSP_CARD_BTN_W / 2;
    if (sell_x)   *sell_x   = PAGE_X + ms_card_btn_x(1) + MSP_CARD_BTN_W / 2;
    if (btn_y)    *btn_y    = PAGE_Y + ms_card_btn_y(t) + MSP_HOW_H / 2;
    return true;
}
bool render_milestones_arrow(const tank_t *t, bool right, int *x, int *y) {
    if (!g_ms_caption[0] || g_ms_tip) return false;
    if (x) *x = PAGE_X + (right ? MSP_MODAL_X + MSP_MODAL_W - MSP_ARROW_IN - MSP_ARROW_W / 2 : MSP_MODAL_X + MSP_ARROW_IN + MSP_ARROW_W / 2);
    if (y) *y = PAGE_Y + ms_modal_y(t) + MSP_ARROW_IN + MSP_ARROW_H / 2;
    return true;
}
void render_milestones_row(int row, int *name_x, int *y) {
    if (name_x) *name_x = PAGE_X + MSP_NAME_X + UI(8);
    if (y)      *y      = PAGE_Y + MSP_ROW_Y0 + row * MSP_ROW_H + MSP_ROW_MID;
}

/* ---- announcement modal (notice.h, 2026-09-15) ----
 * The milestones page's detail modal, over the live tank: the badge art at
 * 2x (a fish's own sprite for a stage), the name, the caption, and a thin
 * bar along the foot that runs out with the notice's time. */
void render_notice(const tank_t *t, uint16_t *fb, int stride, int kind, int fish, uint32_t bit, float frac_left) {
    ctx_t c = ctx_page(fb, stride);
    const int X = MSP_MODAL_X, W = MSP_MODAL_W, Y = MSP_MODAL_Y, H = MSP_MODAL_H;
    rect_fill(&c, X, Y, W, H, 0x04141a);
    rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    char title[FISH_NAME_MAX + 16] = "THE TANK", caption[40] = "";
    const char *caption2 = NULL;                              /* a second line: the title and both move up */
    const icon_t *ic = NULL;
    const fish_t *f = fish >= 0 && fish < t->n_fish ? &t->fish[fish] : NULL;
    if (kind == 6) {                                          /* NOTICE_MODEL_DAMAGED (2026-10-21): the AI model is damaged */
        snprintf(title, sizeof title, "AI MODEL DAMAGED");
        snprintf(caption, sizeof caption, "REINSTALL AT POCKETANK.COM");
        caption2 = "YOUR FISH ARE SAFE";
        /* a warning triangle with an exclamation mark: the shape says it, not the hue */
        const int TH = 34, PX = X + W / 2, PY = Y + 14;      /* apex at the top, base TH below */
        for (int r = 0; r <= TH; r++) {
            int half = r * 20 / TH;
            rect_fill(&c, PX - half, PY + r, 2, 1, MSP_TEAL); rect_fill(&c, PX + half - 1, PY + r, 2, 1, MSP_TEAL);
        }
        rect_fill(&c, PX - 20, PY + TH - 1, 41, 2, MSP_TEAL);
        rect_fill(&c, PX - 1, PY + 11, 3, 13, MSP_TEAL);      /* ! */
        rect_fill(&c, PX - 1, PY + 27, 3, 3, MSP_TEAL);
    } else if (kind == 5) {                                   /* NOTICE_LIGHTS_OUT (2026-10-03): the first double-tap that turned the light off */
        snprintf(title, sizeof title, "LIGHTS OUT");
        snprintf(caption, sizeof caption, "YOU DOUBLE-TAPPED THE GLASS");
        caption2 = "DOUBLE-TAP TO TURN IT ON";
        /* a crescent moon: a disc with a bite out of it */
        const int R = UI(20), PX = X + W / 2 - UI(3), PY = Y + UI(36), BR = UI(17);
        for (int dy = -R; dy <= R; dy++) for (int dx = -R; dx <= R; dx++) {
            int bx = dx - UI(10), by = dy + UI(7);
            if (dx * dx + dy * dy <= R * R && bx * bx + by * by > BR * BR) rect_fill(&c, PX + dx, PY + dy, 1, 1, MSP_TEAL);
        }
    } else if (kind == 4) {                                   /* NOTICE_UPDATED (2026-09-30): the first boot of a new release */
        snprintf(title, sizeof title, "UPDATED");
        snprintf(caption, sizeof caption, "YOUR TANK IS NOW V%s", PT_RELEASE);
        /* a tick in a ring, teal: the shape says it, not the hue */
        const int R = UI(22), PX = X + W / 2, PY = Y + UI(48);
        ring(&c, PX, PY, R, MSP_TEAL); ring(&c, PX, PY, R - 1, MSP_TEAL);
        for (int i = 0; i < UI(6); i++)  rect_fill(&c, PX - UI(11) + i, PY + i - 1, UI(3), UI(3), MSP_TEAL);
        for (int i = 0; i < UI(12); i++) rect_fill(&c, PX - UI(6) + i, PY + UI(4) - i, UI(3), UI(3), MSP_TEAL);
    } else if (kind == 3) {                                   /* NOTICE_LOW_BATTERY */
        snprintf(title, sizeof title, "LOW BATTERY");
        snprintf(caption, sizeof caption, "PLEASE CHARGE THE TANK");
        /* the pill, large: outline + nub, the last sliver lit red */
        const int PW = UI(60), PH = UI(28), PX = X + (W - PW) / 2, PY = Y + UI(34);
        rect_edge(&c, PX, PY, PW, PH, 0x9fb4b8); rect_edge(&c, PX + 1, PY + 1, PW - 2, PH - 2, 0x9fb4b8);
        rect_fill(&c, PX + PW, PY + UI(8), UI(5), PH - UI(16), 0x9fb4b8);
        rect_fill(&c, PX + UI(4), PY + UI(4), UI(7), PH - UI(8), 0xf25b65);
    } else if (kind == 2) {                                   /* NOTICE_STAGE */
        if (f) { snprintf(title, sizeof title, "%s", f->name);
                 snprintf(caption, sizeof caption, "IS NOW %s %s", f->stage == STAGE_ADULT || f->stage == STAGE_ELDER ? "AN" : "A", STAGE_WORDS[f->stage & 3]);
                 render_fish_preview(fb, stride, X + W / 2, Y + UI(48), f->size * 1.6f * UI_F, f->color, f->fin, f->accent, t->clock); }
    } else if (kind == 1) {                                   /* NOTICE_TANK_MILESTONE */
        for (int k = 0; k < TANK_BADGE_N; k++) if (TANK_BADGES[k].bit == bit) ic = TANK_BADGES[k].icon;
        int bi = 0; while (bi < 31 && !(bit & (1u << bi))) bi++;
        snprintf(caption, sizeof caption, "%s", bi < TMS_COUNT ? TMS_NAMES[bi] : "");
        if (bit == TMS_FIRST_SALE) caption2 = "FRY NOW IN THE SHOP";   /* what the sale opened (0.3.5) */
        if (!ic && t->n_fish) {                               /* a population milestone: the newest fish */
            const fish_t *n = &t->fish[t->n_fish - 1];
            render_fish_preview(fb, stride, X + W / 2, Y + UI(48), n->size * 1.6f * UI_F, n->color, n->fin, n->accent, t->clock);
        }
    } else {                                                  /* NOTICE_MILESTONE */
        for (int k = 0; k < 6; k++) if (FISH_BADGES[k].bit == bit) ic = FISH_BADGES[k].icon;
        int bi = 0; while (bi < 31 && !(bit & (1u << bi))) bi++;
        if (f) snprintf(title, sizeof title, "%s", f->name);
        snprintf(caption, sizeof caption, "%s", bi < MS_FISH_COUNT ? MS_NAMES[bi] : "");
        if (!ic && f) render_fish_preview(fb, stride, X + W / 2, Y + UI(48), f->size * 1.6f * UI_F, f->color, f->fin, f->accent, t->clock);
    }
    if (ic) blit_icon_scaled(&c, X + (W - ic->w * UI_TEXT(2)) / 2, Y + UI(16) + (UI(32) - ic->w * UI_TEXT(2) / 2), ic, UI_TEXT(2), true);
    draw_text(&c, X + (W - text_w(title, UI_TEXT(3))) / 2, Y + (caption2 ? UI(70) : UI(92)), UI_TEXT(3), 0xffffff, title);
    draw_text(&c, X + (W - text_w(caption, UI_TEXT(2))) / 2, Y + (caption2 ? UI(104) : UI(124)), UI_TEXT(2), MSP_TEAL, caption);
    if (caption2) draw_text(&c, X + (W - text_w(caption2, UI_TEXT(2))) / 2, Y + UI(126), UI_TEXT(2), MSP_TEAL, caption2);
    if (frac_left < 0) frac_left = 0;
    if (frac_left > 1) frac_left = 1;
    rect_fill(&c, X + 2, Y + H - 4, (int)((W - 4) * frac_left), 2, 0x1c2f36);
}

/* ---- the shop (2026-09-15): sand dollars, and what they buy ----
 * The milestones page's dress. A 64 px coin and the balance at the top, a
 * row per item (SD_ITEMS), HOW TO EARN bottom left, CLOSE bottom right. A
 * row opens the item's modal (the art at 2x, the words, the price, UNLOCK -
 * dim when the balance is short, IN THE TANK once owned); HOW TO EARN a
 * modal of the sources. The page dims under a modal like the milestones
 * page. Fingers land low here too: the row bands run 8 px above and to the
 * next row, the buttons' bands to the glass edge. */
/* (the page's numbers, SHP_*: render.h) */
static int  g_shp_modal = -1;        /* the item whose modal is up, or -1 */
static bool g_shp_earn;              /* the HOW TO EARN modal is up */
static bool g_shp_sell_armed;        /* SELL tapped once: the next tap on it sells */
#ifdef CONFIG_POCKET_TANK_320X240
#define SHP_COIN_ART  (&icon_ms_sand_dollar)        /* the CYD's header is a row shorter: the 32 px coin */
#else
#define SHP_COIN_ART  (&icon_shop_sand_dollar_64)
#endif
static const icon_t *shop_icon(int item) { return item == 0 ? &icon_shop_plant : item == 1 ? &icon_shop_snail : item == 2 ? &icon_shop_castle : item == 3 ? &icon_shop_coral : item == 4 ? &icon_shop_cluster : item == 5 ? &icon_shop_shrimp : item == 6 ? &icon_shop_urchin : item == 7 ? &icon_shop_seahorse : item == 8 ? &icon_shop_pumpkin : item == 9 ? &icon_shop_volcano : &icon_shop_crab; }
/* pages (2026-09-23, the fourth item): SHP_PER_PAGE rows fit between the
 * coin and the foot buttons once the filler caption went (HOW TO EARN says
 * the same). With more items than a page holds, arrows at the header's
 * right flip through the pages; with one page nothing shows. */
static int g_shp_page;
static void shop_arrow(ctx_t *c, int x, int y, bool right, uint32_t rgb) {   /* a chevron in a button */
    button(c, x, y, SHP_ARROW_W, SHP_ARROW_H, 0x1c2f36, rgb, "", UI_TEXT(2));
    int cx = x + SHP_ARROW_W / 2, cy = y + SHP_ARROW_H / 2;
    for (int i = 0; i < 7; i++) {                            /* a chevron: two strokes meeting at the tip */
        int x = right ? cx - 3 + i : cx + 3 - i;
        rect_fill(c, x, cy - 6 + i, 2, 1, rgb);
        rect_fill(c, x, cy + 6 - i, 2, 1, rgb);
    }
}
static void price_tag(ctx_t *c, int x, int y, int price, uint32_t rgb) {   /* the small coin + the number */
    blit_icon(c, x, y - 1, &icon_shop_sand_dollar_16, 255);
    char n[16]; snprintf(n, sizeof n, "%d", price);
    draw_text(c, x + 16 + UI(4), y, UI_TEXT(2), rgb, n);
}
/* the FRY row (0.3.5): the first row once a fish has been sold. Rows are
 * numbered across the pages; row 0 is FRY when it shows, the items after */
#define SHP_MODAL_FRY 99             /* g_shp_modal: the fry's modal */
static int g_shp_fry_pick;           /* the fry picked in its modal */
static bool shp_fry(const tank_t *t) { return progression_fry_for_sale(t); }
static int  shp_rows(const tank_t *t) { return SD_ITEM_COUNT + shp_fry(t) - !progression_pumpkin_offered(t); }   /* the pumpkin, the last item, only while this build sells it (or owned) */
static int  shp_pages(const tank_t *t) { return (shp_rows(t) + SHP_PER_PAGE - 1) / SHP_PER_PAGE; }
static int  shp_item(const tank_t *t, int row) {                                       /* -1 = the FRY row; the pumpkin's row (8) is skipped while it is out of the shop */
    int i = shp_fry(t) ? row - 1 : row;
    if (i >= 8 && !progression_pumpkin_offered(t)) i++;
    return i;
}
static void fry_hours(char *out, size_t n) {   /* "NEW FRY IN 14 H" / "IN 40 MIN" */
    float s = progression_fry_new_in_s();
    if (s >= 3600) snprintf(out, n, "NEW FRY IN %d H", (int)((s + 1799) / 3600));
    else snprintf(out, n, "NEW FRY IN %d MIN", (int)(s / 60) + 1);
}
static void fry_draw(uint16_t *fb, int stride, float x, float y, float size, const fry_offer_t *o, float clock) {
    fish_t f; memset(&f, 0, sizeof f);
    f.stage = STAGE_FRY; f.color = o->body; f.fin = tank_fin_for(o->body); f.accent = o->accent;
    render_fish_portrait(fb, stride, x, y, size, &f, clock);
}
static int fry_first_left(void) {
    for (int k = 0; k < FRY_OFFER_N; k++) { fry_offer_t o; progression_fry_offer(k, &o); if (!o.sold) return k; }
    return -1;
}
static void shop_fry_row(ctx_t *c, const tank_t *t, uint16_t *fb, int stride, int top) {
    int k = fry_first_left();
    bool room = progression_fry_room(t), can = t->sd_balance >= SD_PRICE_FRY;
    const int by = top + SHP_BTN_DY;                         /* (this fork) the item rows' columns and button (render.h): the literals elsewhere */
    if (k >= 0) { fry_offer_t o; progression_fry_offer(k, &o); fry_draw(fb, stride, SHP_ROW_X + SHP_ROW_ICON / 2, top + SHP_ROW_ICON / 2, 0.9f * UI_F, &o, t->clock); }
    draw_text(c, SHP_TEXT_X, top + UI(2), UI_TEXT(2), 0xffffff, "FRY");
    if (k < 0) { char l[24]; fry_hours(l, sizeof l); draw_text(c, SHP_TEXT_X, top + UI(20), UI_TEXT(2), MSP_QUIET, l); }
    else price_tag(c, SHP_TEXT_X, top + UI(20), SD_PRICE_FRY, can && room ? MSP_TEAL : MSP_QUIET);
    const char *b = k < 0 ? "SOLD OUT" : !room ? "FULL" : "CHOOSE";
    if (k >= 0 && room && can) { button(c, SHP_BTN_X, by, SHP_BTN_W, SHP_BTN_H, MSP_TEAL, MSP_TEAL, b, UI_TEXT(2));
                                 draw_text(c, SHP_BTN_X + (SHP_BTN_W - text_w(b, UI_TEXT(2))) / 2, by + (SHP_BTN_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_INK, b); }
    else button(c, SHP_BTN_X, by, SHP_BTN_W, SHP_BTN_H, 0x1c2f36, MSP_DIM, b, UI_TEXT(2));
}
static void shop_fry_modal(ctx_t *c, const tank_t *t, uint16_t *fb, int stride) {
    const int X = SHP_MODAL_X, Y = SHP_MODAL_Y, W = SHP_MODAL_W, H = SHP_MODAL_H;
    rect_fill(c, X, Y, W, H, 0x04141a);
    rect_edge(c, X, Y, W, H, MSP_TEAL); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    draw_text(c, X + (W - text_w("FRY FOR SALE", UI_TEXT(3))) / 2, Y + UI(14), UI_TEXT(3), 0xffffff, "FRY FOR SALE");
    for (int k = 0; k < FRY_OFFER_N; k++) {
        fry_offer_t o; progression_fry_offer(k, &o);
        int bx = SHP_FRY_BOX_X(k), by = SHP_FRY_BOX_Y;
        bool lit = !o.sold && k == g_shp_fry_pick;
        rect_fill(c, bx, by, SHP_FRY_BOX_W, SHP_FRY_BOX_H, lit ? 0x14303a : 0x0b1d23);
        uint32_t e = lit ? 0xffffff : o.sold ? 0x1c2f36 : 0x4d6a72;
        rect_edge(c, bx, by, SHP_FRY_BOX_W, SHP_FRY_BOX_H, e);
        if (lit) rect_edge(c, bx + 1, by + 1, SHP_FRY_BOX_W - 2, SHP_FRY_BOX_H - 2, e);
        if (o.sold) continue;                                    /* bought: a blank until the fresh three */
        fry_draw(fb, stride, bx + SHP_FRY_BOX_W / 2, by + UI(34), 1.5f * UI_F, &o, t->clock + k * 0.7f);
        /* its colors, both: the body and the markings it grows into */
        int sx = bx + SHP_FRY_BOX_W / 2 - UI(19), sy = by + SHP_FRY_BOX_H - UI(22);
        rect_fill(c, sx, sy, UI(16), UI(14), o.body);   rect_edge(c, sx, sy, UI(16), UI(14), 0x1c2f36);
        rect_fill(c, sx + UI(22), sy, UI(16), UI(14), o.accent); rect_edge(c, sx + UI(22), sy, UI(16), UI(14), 0x1c2f36);
    }
    bool room = progression_fry_room(t), can = t->sd_balance >= SD_PRICE_FRY;
    fry_offer_t pk; progression_fry_offer(g_shp_fry_pick, &pk);
    bool go = room && can && !pk.sold && progression_fry_left() > 0;
    char line[32]; snprintf(line, sizeof line, "%d", SD_PRICE_FRY);
#ifdef CONFIG_POCKET_TANK_320X240
    const int py = Y + UI(134);   /* (this fork) the 16 px coin is not scaled: at UI(138) it sat on the line UI(160), 14 px below */
#else
    const int py = Y + UI(138);
#endif
    price_tag(c, X + (W - (16 + UI(4)) - text_w(line, UI_TEXT(2))) / 2, py, SD_PRICE_FRY, go ? 0xffffff : MSP_QUIET);   /* (price_tag's coin and gap) */
    if (!room) snprintf(line, sizeof line, t->n_fish >= POP_CAP ? "THE TANK IS FULL" : "WELCOME THE NEW FRY");
    else if (!can) snprintf(line, sizeof line, "YOU HAVE %d", (int)t->sd_balance);
    else line[0] = 0;
    if (line[0]) draw_text(c, X + (W - text_w(line, UI_TEXT(2))) / 2, Y + UI(160), UI_TEXT(2), MSP_QUIET, line);
    fry_hours(line, sizeof line);
    draw_text(c, X + (W - text_w(line, UI_TEXT(2))) / 2, Y + UI(180), UI_TEXT(2), MSP_QUIET, line);
    const int bx = X + (W - MSP_HOW_W) / 2, by = Y + H - UI(12) - MSP_HOW_H;
    if (go) { button(c, bx, by, MSP_HOW_W, MSP_HOW_H, MSP_TEAL, MSP_TEAL, "BUY", UI_TEXT(2));
              draw_text(c, bx + (MSP_HOW_W - text_w("BUY", UI_TEXT(2))) / 2, by + (MSP_HOW_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_INK, "BUY"); }
    else button(c, bx, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "BUY", UI_TEXT(2));
}
/* the shelf's words (0.3.9): the line under IN THE TANK / ON THE SHELF in a
 * placeable piece's modal - a living piece on the shelf says how far it has
 * grown (it keeps growing there), the pumpkin says when this build no longer
 * sells it (so a SELL is an informed one). NULL = nothing to say. */
static const char *shelf_line(const tank_t *t, int item, char *out, size_t n) {
    bool shelved = (t->sd_shelved & SD_ITEMS[item].bit) != 0;
    if (item == 8 && !SD_PUMPKIN_IN_SHOP) return "NOT IN THE SHOP NOW";
    if (!shelved) return tank_decor_locked(t, item) ? "LOCKED IN PLACE" : NULL;   /* (0.3.9: why a hold on it opens nothing; MOVE is the way to its page) */
    if (item == 3) { snprintf(out, n, "GROWING  %d%%", (int)(tank_coral_growth(t) * 100 / CORAL_FULL + 0.5f)); return out; }
    if (item == 4) { snprintf(out, n, "GROWING  %d%%", (int)(tank_cluster_growth(t) * 100 / CLUSTER_FULL + 0.5f)); return out; }
    if (item == 0) return "GROWING";
    return NULL;
}
/* the shelf's little tanks (0.3.9, Strato: "represented inside a small
 * dedicated tank for each living decoration that is shelved"). A living piece
 * on the shelf (the plant, the coral, the cluster) is painted AS IT IS - its
 * color, its look, its growth - by its own painter at full size into a square
 * window of tank coordinates whose scratch is the frame itself (the page
 * paints over it a moment later: no buffer of its own), then box-filtered 3:1
 * into a 32 x 32 thumb (the icon's size) in the decor scratch (PSRAM;
 * internal RAM is spoken for); the window is 72..256 px a side, as tight as
 * the piece. The row shows it as is, the modal doubled (as the icons are). Rebuilt only when its key changes (growth to the percent, color /
 * look, the shop reopened), never per frame. The thumb stands in the row's
 * icon slot and, doubled, in the modal; the castle and the pumpkin, which do
 * not live, sit as their icon on a shelf board. */
#ifdef CONFIG_POCKET_TANK_320X240
#define SHELF_FULL  TANK_H                                             /* (this fork) the 240 px frame is the window's scratch: 256 rows would run past its end */
#else
#define SHELF_FULL  256                                                /* the widest window: a sword plant at its ceiling (the frame is wider on every glass) */
#endif
#define SHELF_THUMB 32                                                 /* the shop icon's size: the row's slot (Strato 10-19: the 64 px one ran into the next row) */
#define SHELF_WATER 0x0c2e3e
#define SHELF_SAND  0x8c7a4e
static int g_shelf_key[3];                                             /* the plant, the coral, the cluster: 0 = no thumb */
static int shelf_idx(int item) { return item == 0 ? 0 : item == 3 ? 1 : item == 4 ? 2 : -1; }
static int shelf_pct(const tank_t *t, int item) {
    return item == 3 ? (int)(tank_coral_growth(t) * 100 / CORAL_FULL + 0.5f) : item == 4 ? (int)(tank_cluster_growth(t) * 100 / CLUSTER_FULL + 0.5f) : (int)(t->veg_growth[3] * 100 + 0.5f);
}
static int shelf_key(const tank_t *t, int item) {
    uint32_t look = item == 3 ? tank_coral_rgb(t) : item == 4 ? (uint32_t)tank_cluster_scheme(t) : 0;
    return (int)((((look * 2654435761u) >> 12) & 0x7fff00u) + (uint32_t)shelf_pct(t, item) + 1u);
}
/* the window: square, as tight as the piece - its top 10 px over the piece's
 * highest pixel (the bloom and the crown included), 4 px of sand under the
 * floor, at least as wide as the piece and its mound, 72..192 px a side */
static int shelf_window(const tank_t *t, int item, int *top) {
    float ptop; int hw;
    if (item == 0)      { tank_veg_bed(t, 3, NULL, NULL, &ptop, NULL); hw = PLANT_HALF_W + 14; }
    else if (item == 3) { ptop = TANK_BOT - 14 + DECOR_SINK - 92 - (tank_coral_growth(t) > 1 ? 18 : 0); hw = CORAL_HALF_W + 10; }
    else                { ptop = TANK_BOT - 14 + DECOR_SINK - 120 - (tank_cluster_growth(t) > 1 ? 26 : 0); hw = CLUSTER_HALF_W + 10; }
    int s = TANK_BOT + 4 - ((int)ptop - 10);
    if (s < 2 * hw) s = 2 * hw;
    if (s < 72) s = 72;
    if (s > SHELF_FULL) s = SHELF_FULL;
    *top = TANK_BOT + 4 - s;
    return s;
}
static void shelf_build(const tank_t *t, int item, uint16_t *fb, int stride) {
    int k = shelf_idx(item), key = shelf_key(t, item);
    if (k < 0 || g_shelf_key[k] == key) return;
    g_shelf_key[k] = key;
    int cx = (int)tank_decor_x(t, item), top, side = shelf_window(t, item, &top);
    ctx_t c = { fb, stride, 1.0f, cx - side / 2, top, side, side };   /* the window lands at the frame's top left */
    rect_fill(&c, c.ox, c.oy, side, side, SHELF_WATER);
    rect_fill(&c, c.ox, TANK_BOT - 16, side, c.oy + side - (TANK_BOT - 16), SHELF_SAND);
    int bx0, by0, bx1, by1;
    if (item == 3)      { draw_coral(&c, cx, tank_coral_rgb(t), tank_coral_growth(t), false);
                          draw_coral_crown(&c, cx, tank_coral_rgb(t), tank_coral_growth(t), 0, &bx0, &by0, &bx1, &by1); }
    else if (item == 4) { draw_cluster(&c, cx, tank_cluster_scheme(t), tank_cluster_growth(t), false);
                          draw_cluster_crown(&c, cx, tank_cluster_scheme(t), tank_cluster_growth(t), 0, &bx0, &by0, &bx1, &by1); }
    else                  draw_veg(&c, t, 3, VEG_SEED[3], -1, false);
    uint16_t *th = ds()->shelf_thumb[k];                             /* the box filter: each thumb pixel averages its share of the window */
    for (int y = 0; y < SHELF_THUMB; y++) {
        int sy0 = y * side / SHELF_THUMB, sy1 = (y + 1) * side / SHELF_THUMB; if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < SHELF_THUMB; x++) {
            int sx0 = x * side / SHELF_THUMB, sx1 = (x + 1) * side / SHELF_THUMB; if (sx1 <= sx0) sx1 = sx0 + 1;
            int r = 0, g = 0, b = 0, n = (sy1 - sy0) * (sx1 - sx0);
            for (int sy = sy0; sy < sy1; sy++) for (int sx = sx0; sx < sx1; sx++) { uint16_t v = FB_SWAP16(fb[sy * stride + sx]); r += v >> 11; g += (v >> 5) & 63; b += v & 31; }
            th[y * SHELF_THUMB + x] = (uint16_t)(((r / n) << 11) | ((g / n) << 5) | (b / n));
        }
    }
}
static void shelf_prepare(const tank_t *t, uint16_t *fb, int stride) {   /* before the page paints: every shelved living piece has its thumb */
    static const int living[3] = { 0, 3, 4 };
    for (int i = 0; i < 3; i++) if (t->sd_shelved & SD_ITEMS[living[i]].bit) shelf_build(t, living[i], fb, stride);
}
static void shelf_slot(ctx_t *c, int x, int y, const tank_t *t, int item, int s) {   /* the icon slot: s = 1 on the row (32 px), 2 in the modal (64, as the icons) */
    const int W = SHELF_THUMB * s, H = SHELF_THUMB * s; int k = shelf_idx(item);
    if (k >= 0 && g_shelf_key[k]) {                                   /* the little tank: the thumb, the glass, a glint */
        const uint16_t *th = ds()->shelf_thumb[k];
        for (int yy = 0; yy < H; yy++) for (int xx = 0; xx < W; xx++) if (CTX_IN(c, x + xx, y + yy)) CTX_PX(c, x + xx, y + yy) = FB_SWAP16(th[(yy / s) * SHELF_THUMB + xx / s]);
        rect_edge(c, x, y, W, H, MSP_TEAL);
        for (int i = 1; i < 3 * s; i++) px_blend(c, x + 2 * s + i, y + 2 * s, 0xffffff, 110);
        return;
    }
    rect_fill(c, x, y + H - 3 * s, W, 3 * s, 0x5a3c22);                /* the shelf board under a piece that does not live */
    rect_fill(c, x, y + H - 3 * s, W, 1, 0x8a6a42);
    if (s == 1) blit_icon(c, x, y, shop_icon(item), 255); else blit_icon_scaled(c, x, y, shop_icon(item), s, true);
}
void render_shop(const tank_t *t, uint16_t *fb, int stride) {
    shelf_prepare(t, fb, stride);                             /* (the shelf's thumbs first: the frame is their scratch) */
    ctx_t c = ctx_page(fb, stride);
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    blit_icon(&c, SHP_COIN_X, SHP_COIN_Y, SHP_COIN_ART, 255);
    draw_text(&c, SHP_HEAD_X, SHP_COIN_Y + UI(6), UI_TEXT(2), MSP_TEAL, "SAND DOLLARS");
    char bal[16]; snprintf(bal, sizeof bal, "%d", (int)t->sd_balance);
    draw_text(&c, SHP_HEAD_X, SHP_COIN_Y + UI(28), UI_TEXT(4), 0xffffff, bal);
    for (int x = MSP_RULE_X0; x < MSP_RULE_X1; x++) px_blend(&c, x, SHP_ROW_Y0 - UI(10), MSP_DIM, 200);
    if (shp_pages(t) > 1) {
        shop_arrow(&c, SHP_ARROW_X0, SHP_ARROW_Y, false, g_shp_page > 0 ? MSP_TEAL : MSP_DIM);
        shop_arrow(&c, SHP_ARROW_X1, SHP_ARROW_Y, true, g_shp_page < shp_pages(t) - 1 ? MSP_TEAL : MSP_DIM);
    }
    for (int r = g_shp_page * SHP_PER_PAGE; r < shp_rows(t) && r < (g_shp_page + 1) * SHP_PER_PAGE; r++) {
        int top = SHP_ROW_Y0 + (r - g_shp_page * SHP_PER_PAGE) * SHP_ROW_DY, i = shp_item(t, r);
        if (i < 0) { shop_fry_row(&c, t, fb, stride, top); continue; }
        const sd_item_t *it = &SD_ITEMS[i];
        bool owned = (t->sd_unlocks & it->bit) != 0, can = t->sd_balance >= it->price, shelved = owned && (t->sd_shelved & it->bit);
        if (shelved) shelf_slot(&c, SHP_ROW_X, top, t, i, 1);                                                             /* the shelf (0.3.9): the piece as it is, in its little tank */
        else if (owned) blit_icon(&c, SHP_ROW_X, top, shop_icon(i), 255); else blit_icon_locked(&c, SHP_ROW_X, top, shop_icon(i));   /* (the rows are where the bowl is wide: their own column) */
        const int by = top + SHP_BTN_DY;
        draw_text(&c, SHP_TEXT_X, top + UI(2), UI_TEXT(2), 0xffffff, it->name);
        if (shelved) draw_text(&c, SHP_TEXT_X, top + UI(20), UI_TEXT(2), MSP_TEAL, "ON THE SHELF");
        else if (owned) draw_text(&c, SHP_TEXT_X, top + UI(20), UI_TEXT(2), MSP_TEAL, "IN THE TANK");
        else price_tag(&c, SHP_TEXT_X, top + UI(20), it->price, can ? MSP_TEAL : MSP_QUIET);
        if (shelved) { button(&c, SHP_BTN_X, by, SHP_BTN_W, SHP_BTN_H, MSP_TEAL, MSP_TEAL, "PUT BACK", UI_TEXT(2));          /* live: one tap returns it */
                       draw_text(&c, SHP_BTN_X + (SHP_BTN_W - text_w("PUT BACK", UI_TEXT(2))) / 2, by + (SHP_BTN_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_INK, "PUT BACK"); }
        else if (owned) button(&c, SHP_BTN_X, by, SHP_BTN_W, SHP_BTN_H, MSP_INK, MSP_DIM, "IN TANK", UI_TEXT(2));
        else if (can) { button(&c, SHP_BTN_X, by, SHP_BTN_W, SHP_BTN_H, MSP_TEAL, MSP_TEAL, "UNLOCK", UI_TEXT(2));
                        draw_text(&c, SHP_BTN_X + (SHP_BTN_W - text_w("UNLOCK", UI_TEXT(2))) / 2, by + (SHP_BTN_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_INK, "UNLOCK"); }
        else           button(&c, SHP_BTN_X, by, SHP_BTN_W, SHP_BTN_H, 0x1c2f36, MSP_DIM, "UNLOCK", UI_TEXT(2));
    }
    button(&c, SHP_EARN_X, MSP_CLOSE_Y, SHP_EARN_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "HOW TO EARN", UI_TEXT(2));
    button(&c, SHP_CLOSE_X, MSP_CLOSE_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", UI_TEXT(2));
    if (g_shp_modal < 0 && !g_shp_earn) return;
    for (int y = 0; y < TANK_H; y++)                          /* the page out of reach under a modal */
        for (int x = 0; x < TANK_W; x++) { uint16_t n = FB_SWAP16(fb[y * stride + x]); fb[y * stride + x] = FB_SWAP16((uint16_t)((n >> 1) & 0x7bef)); }
    const int X = SHP_MODAL_X, W = SHP_MODAL_W;
    if (g_shp_earn) {
        const int Y = SHP_EARN_MODAL_Y, H = SHP_EARN_MODAL_H;
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        draw_text(&c, X + (W - text_w("HOW TO EARN", UI_TEXT(3))) / 2, Y + UI(14), UI_TEXT(3), 0xffffff, "HOW TO EARN");
        const char *const *lines = progression_sd_earn_lines();
        for (int i = 0; lines[i]; i++) draw_text(&c, X + UI(14), Y + UI(50) + i * UI(24), UI_TEXT(2), MSP_TEAL, lines[i]);
        draw_text(&c, X + (W - text_w("TAP TO CLOSE", UI_TEXT(2))) / 2, Y + H - UI(24), UI_TEXT(2), MSP_QUIET, "TAP TO CLOSE");
        return;
    }
    if (g_shp_modal == SHP_MODAL_FRY) { shop_fry_modal(&c, t, fb, stride); return; }
    const sd_item_t *it = &SD_ITEMS[g_shp_modal];
    const int Y = SHP_MODAL_Y, H = SHP_MODAL_H;
    bool owned = (t->sd_unlocks & it->bit) != 0, can = t->sd_balance >= it->price;
    rect_fill(&c, X, Y, W, H, 0x04141a);
    rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    const icon_t *art = shop_icon(g_shp_modal);
    const int ax = X + (W - art->w * UI_TEXT(2)) / 2, ay = Y + UI(14);   /* (this fork) the art at the page's text scale: 1x on the CYD */
    blit_icon_scaled(&c, ax, ay, art, UI_TEXT(2), true);
    draw_text(&c, X + (W - text_w(it->name, UI_TEXT(3))) / 2, Y + UI(88), UI_TEXT(3), 0xffffff, it->name);
    draw_text(&c, X + (W - text_w(it->words, UI_TEXT(2))) / 2, Y + UI(118), UI_TEXT(2), MSP_TEAL, it->words);
    draw_text(&c, X + (W - text_w(it->words2, UI_TEXT(2))) / 2, Y + UI(138), UI_TEXT(2), MSP_TEAL, it->words2);
    const int bx = X + (W - MSP_HOW_W) / 2, by = Y + H - UI(12) - MSP_HOW_H;
    if (owned && tank_decor_placeable(g_shp_modal)) {         /* a placeable piece (0.3.9): in the tank MOVE | SHELVE; on the shelf PLACE | SELL (twice) */
        bool shelved = (t->sd_shelved & it->bit) != 0; char sub[28];
        if (shelved) shelf_slot(&c, ax, ay, t, g_shp_modal, UI_TEXT(2));   /* over the icon: the piece as it is on the shelf */
        const char *state = shelved ? "ON THE SHELF" : "IN THE TANK";
        draw_text(&c, X + (W - text_w(state, UI_TEXT(2))) / 2, Y + UI(164), UI_TEXT(2), MSP_TEAL, state);
        if (shelf_line(t, g_shp_modal, sub, sizeof sub)) draw_text(&c, X + (W - text_w(sub, UI_TEXT(2))) / 2, Y + UI(184), UI_TEXT(2), MSP_QUIET, sub);
        button(&c, SHP_TWO_X0, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, shelved ? "PLACE" : "MOVE", UI_TEXT(2));
        if (!shelved) button(&c, SHP_TWO_X1, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "SHELVE", UI_TEXT(2));
        else if (g_shp_sell_armed) { snprintf(sub, sizeof sub, "+%d OK?", progression_sell_value(g_shp_modal));
                                     button(&c, SHP_TWO_X1, by, MSP_HOW_W, MSP_HOW_H, MSP_TEAL, MSP_TEAL, sub, UI_TEXT(2));
                                     draw_text(&c, SHP_TWO_X1 + (MSP_HOW_W - text_w(sub, UI_TEXT(2))) / 2, by + (MSP_HOW_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_INK, sub); }
        else button(&c, SHP_TWO_X1, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "SELL", UI_TEXT(2));
    } else if (owned) draw_text(&c, X + (W - text_w("IN THE TANK", UI_TEXT(2))) / 2, by + UI(8), UI_TEXT(2), MSP_TEAL, "IN THE TANK");   /* the snail: a permanent resident */
    else {
        char line[32]; snprintf(line, sizeof line, "%d", it->price);
        int pw = 16 + UI(4) + text_w(line, UI_TEXT(2));
        price_tag(&c, X + (W - pw) / 2, Y + UI(164), it->price, can ? 0xffffff : MSP_QUIET);
        if (!can) { snprintf(line, sizeof line, "YOU HAVE %d", (int)t->sd_balance);
                    draw_text(&c, X + (W - text_w(line, UI_TEXT(2))) / 2, Y + UI(184), UI_TEXT(2), MSP_QUIET, line); }
        if (can) { button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, MSP_TEAL, MSP_TEAL, "UNLOCK", UI_TEXT(2));
                   draw_text(&c, bx + (MSP_HOW_W - text_w("UNLOCK", UI_TEXT(2))) / 2, by + (MSP_HOW_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_INK, "UNLOCK"); }
        else button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "UNLOCK", UI_TEXT(2));
    }
}
int render_shop_tap(const tank_t *t, float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;                    /* the page's own coordinates */
    if (g_shp_earn) { g_shp_earn = false; return SHOP_TAP_KEPT; }
    if (g_shp_modal == SHP_MODAL_FRY) {                       /* the fry: a box picks, BUY buys the pick, elsewhere closes */
        if (y >= SHP_FRY_BOX_Y - UI(8) && y < SHP_FRY_BOX_Y + SHP_FRY_BOX_H + UI(6))
            for (int k = 0; k < FRY_OFFER_N; k++)
                if (x >= SHP_FRY_BOX_X(k) - SHP_FRY_BOX_GAP / 2 && x < SHP_FRY_BOX_X(k) + SHP_FRY_BOX_W + SHP_FRY_BOX_GAP / 2) {
                    fry_offer_t o; progression_fry_offer(k, &o);
                    if (!o.sold) g_shp_fry_pick = k;
                    return SHOP_TAP_KEPT;
                }
        const int bx = SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2, by = SHP_MODAL_Y + SHP_MODAL_H - UI(12) - MSP_HOW_H;
        bool on_btn = y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN && x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X;
        fry_offer_t pk; progression_fry_offer(g_shp_fry_pick, &pk);
        g_shp_modal = -1;
        if (on_btn && !pk.sold && progression_fry_room(t) && t->sd_balance >= SD_PRICE_FRY) return SHOP_TAP_FRY + g_shp_fry_pick;
        return SHOP_TAP_KEPT;
    }
    if (g_shp_modal >= 0) {
        int item = g_shp_modal; const sd_item_t *it = &SD_ITEMS[item];
        bool owned = (t->sd_unlocks & it->bit) != 0, can = t->sd_balance >= it->price;
        const int bx = SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2, by = SHP_MODAL_Y + SHP_MODAL_H - UI(12) - MSP_HOW_H;
        bool row = y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN;
        bool on_btn = row && x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X;
        if (owned && tank_decor_placeable(item)) {                /* two buttons: in the tank MOVE | SHELVE; on the shelf PLACE | SELL (armed, then confirmed) */
            bool shelved = (t->sd_shelved & it->bit) != 0;
            bool on_left  = row && x >= SHP_TWO_X0 - MSP_HOW_SLOP_X && x < SHP_TWO_X0 + MSP_HOW_W + SHP_TWO_GAP / 2;
            bool on_right = row && x >= SHP_TWO_X1 - SHP_TWO_GAP / 2 && x < SHP_TWO_X1 + MSP_HOW_W + MSP_HOW_SLOP_X;
            if (shelved && on_right && !g_shp_sell_armed) { g_shp_sell_armed = true; return SHOP_TAP_KEPT; }
            g_shp_sell_armed = false; g_shp_modal = -1;
            if (on_right) return (shelved ? SHOP_TAP_SELL : SHOP_TAP_SHELVE) + item;
            if (on_left)  return (shelved ? SHOP_TAP_PLACE : SHOP_TAP_MOVE) + item;
            return SHOP_TAP_KEPT;
        }
        g_shp_modal = -1; g_shp_sell_armed = false;
        if (on_btn && !owned && can) return SHOP_TAP_BUY + item;
        return SHOP_TAP_KEPT;
    }
    if (x >= SHP_CLOSE_X - UI(6) && y >= MSP_CLOSE_Y - UI(4)) return SHOP_TAP_CLOSE;
    if (x < SHP_EARN_X + SHP_EARN_W + UI(8) && y >= MSP_CLOSE_Y - UI(4)) { g_shp_earn = true; return SHOP_TAP_KEPT; }
    if (shp_pages(t) > 1 && y < SHP_ROW_Y0 - UI(10) && x >= SHP_ARROW_X0 - UI(8)) {   /* the page arrows, in the header band */
        if (x < SHP_ARROW_X1 - UI(4)) { if (g_shp_page > 0) g_shp_page--; }
        else if (g_shp_page < shp_pages(t) - 1) g_shp_page++;
        return SHOP_TAP_KEPT;
    }
    for (int r = g_shp_page * SHP_PER_PAGE; r < shp_rows(t) && r < (g_shp_page + 1) * SHP_PER_PAGE; r++) {
        int top = SHP_ROW_Y0 + (r - g_shp_page * SHP_PER_PAGE) * SHP_ROW_DY, i = shp_item(t, r);
        if (x >= MSP_HIT_X0 && y >= top - UI(8) && y < top + SHP_ROW_DY - UI(8)) {
            if (i >= 0 && (t->sd_shelved & SD_ITEMS[i].bit) && x >= SHP_BTN_X - UI(8)) return SHOP_TAP_PLACE + i;   /* the row's PUT BACK (0.3.9) */
            if (i < 0) { int k = fry_first_left(); g_shp_fry_pick = k < 0 ? 0 : k; g_shp_modal = SHP_MODAL_FRY; }   /* the fry's modal, the first one left picked */
            else g_shp_modal = i;
            return SHOP_TAP_KEPT;
        }
    }
    return SHOP_TAP_NONE;
}
void render_shop_leave(void) { g_shp_modal = -1; g_shp_earn = false; g_shp_page = 0; g_shp_sell_armed = false; g_shelf_key[0] = g_shelf_key[1] = g_shelf_key[2] = 0; }

/* the toast: "+N" by a coin, top centre, for TOAST_S on the tank clock */
#define TOAST_S 2.5f
static int   g_toast_n;
static float g_toast_until = -1;
void render_sd_toast(const tank_t *t, uint16_t *fb, int stride) {
    int n = progression_sd_take_award();
    if (n > 0) {
        if (t->clock < g_toast_until) g_toast_n += n; else g_toast_n = n;
        g_toast_until = t->clock + TOAST_S;
    }
    if (t->clock >= g_toast_until || t->clock < g_toast_until - TOAST_S - 1) return;   /* (a reset sends the clock back) */
    ctx_t c = ctx_full(fb, stride, 1.0f);
    char txt[16]; snprintf(txt, sizeof txt, "+%d", g_toast_n);
    /* the 16 px coin and the number: a margin, the coin, a gap, the number, a margin */
    const int tx = UI(5) + 16 + UI(4);
    const int W = tx + text_w(txt, UI_TEXT(2)) + UI(5), H = 16 + 2 * UI(3), X = (TANK_W - W) / 2, Y = PAGE_BOWL ? 44 : UI(8);   /* (the bowl's top is a narrow cap: lower) */
    for (int y = Y; y < Y + H; y++)
        for (int x = X; x < X + W; x++) px_blend(&c, x, y, 0x04141a, 215);
    rect_edge(&c, X, Y, W, H, MSP_TEAL);
    blit_icon(&c, X + UI(5), Y + UI(3), &icon_shop_sand_dollar_16, 255);
    draw_text(&c, X + tx, Y + (H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), 0xffffff, txt);
}

/* ---- settings page (2026-09-15) ----
 * The milestones page's foot used to carry the brightness row; Strato:
 * "a new UI for settings and leave milestones alone - we may need more
 * room there anyway". Rows of segment buttons, generous hit bands (fingers
 * land low near the bezel, as on the milestones page). 0.3.2: LIGHTS OUT is
 * one row (a value between two arrows, where MANUAL / AUTO and a big seconds
 * selector stood), and AUTO FEED and ROTATION have the room it gave back.
 * 0.3.9: BRIGHTNESS is a slider (30 / 60 / 100 % was too coarse at the dim
 * end - Bananna on Discord, light sensitive: "even the 30% brightness can be
 * a bit bright"). */
/* (the page's numbers, SET_*: render.h) */
static const char *const SET_VOLUME[3] = { "OFF", "QUIET", "NORMAL" };
static const char *const SET_LIGHT[LIGHT_IDLE_N + 1] = { "DOUBLE-TAP",   /* MANUAL, the default: the row says how the light is worked */
    "5 SEC", "15 SEC", "30 SEC", "1 MIN", "3 MIN", "5 MIN", "10 MIN", "30 MIN" };
static const char *const SET_FEED[2]   = { "ON", "OFF" };          /* the default first */
#if TANK_WORN
static const char *const SET_SCREEN[2] = { "NORMAL", "TURNED" };   /* the default first */
#endif
/* this fork, the 320x240 boards: the row where the others have ROTATION is
   chosen at run time (render.h, SET_TAP_FLIP), and the SLEEP row under it
   (SET_TAP_SLEEP) */
#ifdef CONFIG_POCKET_TANK_320X240
static const char *const SET_FLIP[2] = { "UPRIGHT", "FLIPPED" };
static const char *const SET_SLEEP[3] = { "NEVER", "SCREEN", "LIGHT" };   /* SET_SLEEP_*'s order */
#endif
static bool g_set_flipped;                                       /* the SCREEN row's state, from the platform */
static bool g_set_imu;                                           /* an IMU answered */
static int g_set_sleep = -1;                                     /* the SLEEP row's choice, SET_SLEEP_*; -1: no row (deepsleep) */
void render_settings_set_flip(bool flipped) { g_set_flipped = flipped; }
void render_settings_set_imu(bool imu) { g_set_imu = imu; }
void render_settings_set_sleep(int choice) { g_set_sleep = choice; }

static void set_row(ctx_t *c, int row_y, const char *label, const char *const names[], int n, int chosen) {
    draw_text(c, SET_LABEL_X, row_y, UI_TEXT(2), MSP_TEAL, label);
    for (int i = 0; i < n; i++) {
        int x = SET_SEG_X + i * SET_SEG_DX, y = SET_SEG_Y(row_y);
        if (i == chosen) {                       /* lit: teal, ink lettering */
            button(c, x, y, SET_SEG_W, SET_SEG_H, MSP_TEAL, MSP_TEAL, names[i], UI_TEXT(2));
            draw_text(c, x + (SET_SEG_W - text_w(names[i], UI_TEXT(2))) / 2, y + (SET_SEG_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_INK, names[i]);
        } else button(c, x, y, SET_SEG_W, SET_SEG_H, 0x1c2f36, MSP_DIM, names[i], UI_TEXT(2));
    }
}
/* the percent a finger at page x asks of the BRIGHTNESS slider: the nearest step */
static int set_bright_at(float x) {
    float f = (x - SET_BRT_X0) / (float)(SET_BRT_X1 - SET_BRT_X0);
    int pct = SET_BRT_MIN + (int)lroundf(f * (100 - SET_BRT_MIN) / SET_BRT_STEP) * SET_BRT_STEP;
    return pct < SET_BRT_MIN ? SET_BRT_MIN : pct > 100 ? 100 : pct;
}
/* the slider: the rail lit (teal) up to the knob and dim past it - the lit
 * part is the brighter one, so it reads without the hue - and the knob, a
 * lit button with the percent on it in ink */
static void set_slider(ctx_t *c, int row_y, int pct) {
    int y = SET_SEG_Y(row_y), cy = y + SET_SEG_H / 2;
    int kx = SET_BRT_X(pct < SET_BRT_MIN ? SET_BRT_MIN : pct > 100 ? 100 : pct);
    draw_text(c, SET_LABEL_X, row_y, UI_TEXT(2), MSP_TEAL, "BRIGHTNESS");
    rect_fill(c, SET_SEG_X + UI(4), cy - UI(3), kx - SET_SEG_X - UI(4), UI(6), MSP_TEAL);
    rect_fill(c, kx, cy - UI(3), SET_SEG_X + SET_SPAN_W - UI(4) - kx, UI(6), 0x1c2f36);
    rect_edge(c, kx, cy - UI(3), SET_SEG_X + SET_SPAN_W - UI(4) - kx, UI(6), MSP_DIM);
    char txt[8]; snprintf(txt, sizeof txt, "%d%%", pct);
    button(c, kx - SET_KNOB_W / 2, y, SET_KNOB_W, SET_SEG_H, MSP_TEAL, MSP_TEAL, "", UI_TEXT(2));
    draw_text(c, kx - text_w(txt, UI_TEXT(2)) / 2, y + (SET_SEG_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_INK, txt);
}
/* an arrow button of the LIGHTS OUT row: dim at the end of the list */
static void set_arrow(ctx_t *c, int x, int y, bool right, bool live) {
    uint32_t rgb = live ? MSP_TEAL : 0x2c4a52;
    button(c, x, y, SET_ARW_W, SET_SEG_H, 0x1c2f36, rgb, "", UI_TEXT(2));
    int cx = x + SET_ARW_W / 2, cy = y + SET_SEG_H / 2;
    for (int i = 0; i < 7; i++) {                           /* a solid triangle, 7 wide, 13 tall */
        int px = right ? cx - 3 + i : cx + 3 - i, hh = 6 - i;
        rect_fill(c, px, cy - hh, 1, 2 * hh + 1, rgb);
    }
}
#if !TANK_WORN                                              /* (the watch has SCREEN where ROTATION is) */
/* ROTATION's picture: a padlock inside a turning arrow - shut when the way
 * up is locked, its shackle swung open while the picture follows the tank
 * (the two differ in shape, not in color alone) */
#ifdef CONFIG_POCKET_TANK_320X240
/* (this fork) the CYD's segment is 24 px tall: the same picture a size down -
   a 21 px ring, its arrowhead, an 8 px padlock */
static void set_lock_icon(ctx_t *c, int cx, int cy, bool locked, uint32_t rgb) {
    for (int dy = -11; dy <= 11; dy++)                      /* the ring, open at the top right */
        for (int dx = -11; dx <= 11; dx++) {
            float r2 = (float)(dx * dx + dy * dy);
            if (r2 < 8.0f * 8.0f || r2 > 10.2f * 10.2f) continue;
            if (dx > 1 && dy < 0 && dy < -dx * 0.45f) continue;
            px_blend(c, cx + dx, cy + dy, rgb, 255);
        }
    for (int i = 0; i < 4; i++)                             /* its arrowhead, at the top, pointing clockwise */
        rect_fill(c, cx + 1 + i, cy - 9 - (3 - i), 1, 2 * (3 - i) + 1, rgb);
    rect_fill(c, cx - 4, cy - 1, 8, 6, rgb);               /* the padlock's body */
    int top = locked ? cy - 5 : cy - 7;                     /* the shackle: down in the body, or lifted with one leg free */
    rect_fill(c, cx - 3, top, 6, 1, rgb);
    rect_fill(c, cx - 3, top, 1, cy - 1 - top, rgb);
    rect_fill(c, cx + 2, top, 1, locked ? cy - 1 - top : 2, rgb);
}
#else
static void set_lock_icon(ctx_t *c, int cx, int cy, bool locked, uint32_t rgb) {
    for (int dy = -15; dy <= 15; dy++)                      /* the ring, open at the top right */
        for (int dx = -15; dx <= 15; dx++) {
            float r2 = (float)(dx * dx + dy * dy);
            if (r2 < 11.5f * 11.5f || r2 > 14.0f * 14.0f) continue;
            if (dx > 1 && dy < 0 && dy < -dx * 0.45f) continue;
            px_blend(c, cx + dx, cy + dy, rgb, 255);
        }
    for (int i = 0; i < 6; i++)                             /* its arrowhead, at the top, pointing clockwise */
        rect_fill(c, cx + 1 + i, cy - 13 - (5 - i), 1, 2 * (5 - i) + 1, rgb);
    rect_fill(c, cx - 6, cy - 1, 12, 8, rgb);              /* the padlock's body */
    int top = locked ? cy - 7 : cy - 10;                    /* the shackle: down in the body, or lifted with one leg free */
    rect_fill(c, cx - 4, top, 8, 2, rgb);
    rect_fill(c, cx - 4, top, 2, cy - 1 - top, rgb);
    rect_fill(c, cx + 2, top, 2, locked ? cy - 1 - top : 4, rgb);
}
#endif  /* (this fork) the 320x240 boards' smaller lock */
#endif  /* !TANK_WORN */
void render_settings(const tank_t *t, uint16_t *fb, int stride, int bright_pct, int volume) {
    ctx_t c = ctx_page(fb, stride);
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    draw_text(&c, (PAGE_W - text_w("SETTINGS", UI_TEXT(3))) / 2, SET_TITLE_Y, UI_TEXT(3), 0xffffff, "SETTINGS");
    set_slider(&c, SET_ROW1_Y, bright_pct);
    set_row(&c, SET_ROW2_Y, "VOLUME", SET_VOLUME, 3, volume < 0 ? 0 : volume > 2 ? 2 : volume);
    draw_text(&c, SET_LABEL_X, SET_NOTE_Y, UI_TEXT(2), MSP_QUIET, "FISH ARE QUIET AT NIGHT");
    /* LIGHTS OUT: < the choice > */
    {
        int ch = tank_light_choice(t), y = SET_SEG_Y(SET_ROW3_Y);
        int bx = SET_SEG_X + SET_ARW_W + UI(4), bw = SET_SPAN_W - 2 * (SET_ARW_W + UI(4));
        draw_text(&c, SET_LABEL_X, SET_ROW3_Y, UI_TEXT(2), MSP_TEAL, "LIGHTS OUT");
        set_arrow(&c, SET_SEG_X, y, false, ch > 0);
        button(&c, bx, y, bw, SET_SEG_H, MSP_TEAL, MSP_TEAL, "", UI_TEXT(2));
        draw_text(&c, bx + (bw - text_w(SET_LIGHT[ch], UI_TEXT(2))) / 2, y + (SET_SEG_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_INK, SET_LIGHT[ch]);
        set_arrow(&c, SET_SEG_X + SET_SPAN_W - SET_ARW_W, y, true, ch < LIGHT_IDLE_N);
    }
    set_row(&c, SET_ROW4_Y, "AUTO FEED", SET_FEED, 2, t->autofeed_off ? 1 : 0);
#if TANK_WORN
    /* SCREEN (2026-10-02): the way up of a watch worn either way round - TURNED
       for buttons toward the elbow. The picture turns as the finger lifts. */
    set_row(&c, SET_ROW5_Y, "SCREEN", SET_SCREEN, 2, t->screen_turned ? 1 : 0);
    draw_text(&c, SET_LABEL_X, SET_NOTE5_Y, 2, MSP_QUIET, "WORN THE OTHER WAY AROUND?");
#else
#ifdef CONFIG_POCKET_TANK_320X240
    /* (this fork) a 320x240 board with no IMU to turn the picture - the CYD
       without its breakout, the Touch-LCD-2, whose IMU does not turn it
       (2026-10-08, adampog): the keeper's SCREEN */
    if (!g_set_imu) set_row(&c, SET_ROW5_Y, "SCREEN", SET_FLIP, 2, g_set_flipped ? 1 : 0);
    else
#endif
    /* ROTATION (0.3.2): the picture turns over with the tank, unless locked */
    {
        int x = SET_SEG_X, y = SET_SEG_Y(SET_ROW5_Y);
        draw_text(&c, SET_LABEL_X, SET_ROW5_Y, UI_TEXT(2), MSP_TEAL, "ROTATION");
        if (t->orient_lock) button(&c, x, y, SET_SEG_W, SET_SEG_H, MSP_TEAL, MSP_TEAL, "", UI_TEXT(2));
        else                button(&c, x, y, SET_SEG_W, SET_SEG_H, 0x1c2f36, MSP_DIM, "", UI_TEXT(2));
        set_lock_icon(&c, x + SET_SEG_W / 2, y + SET_SEG_H / 2 + 1, t->orient_lock, t->orient_lock ? MSP_INK : MSP_TEAL);
        draw_text(&c, SET_ROT_WORD_X, SET_ROW5_Y, UI_TEXT(2), t->orient_lock ? 0xffffff : MSP_QUIET, t->orient_lock ? "LOCKED" : "UNLOCKED");
    }
#ifdef CONFIG_POCKET_TANK_320X240
    /* (this fork) the SLEEP row under it, IMU or not: what the ways into
       sleep do. A deepsleep build has no choice to offer and no row. */
    if (g_set_sleep >= 0) set_row(&c, SET_ROW6_Y, "SLEEP", SET_SLEEP, 3, g_set_sleep);
#endif
#endif
    /* the firmware version, hugging the bottom left of the frame (6 px up,
       on the labels' x; the bezel's curve is clear there), small (8 px) and
       dim: it is for the keeper who asks "how do I update?", not for
       reading - Strato: "FISH ARE QUIET AT NIGHT is meant to be read; the
       fw version is something most people should not care about. minimize
       it", "make it 8px tall", "hug the bottom of the frame" (2026-09-16).
       The installer page shows the version it would write in the same words. */
    /* 2026-09-29: the release number first ("V0.2.0 ALPHA"), the build id after it */
    char ver[64]; snprintf(ver, sizeof ver, "V%s %s  BUILD %s", PT_RELEASE, PT_RELEASE_STAGE, version_port_string());
#if TANK_WORN                                    /* the watch: centred under the foot, between the lower corners */
    draw_text_8px(&c, (PAGE_W - ((int)strlen(ver) * 6 - 1)) / 2, SET_FOOT_Y + MSP_CLOSE_H + 2, MSP_DIM, ver);
#elif defined(CONFIG_POCKET_TANK_320X240)
    /* (this fork) a 320x240 board: no UPDATES (no update channel), so the version line
       has the foot left of CLOSE - clipped short of it (6 px a character in
       the 8 px font): the build id's tail is what gives way, the release
       number always shows */
    { int room = (SET_CLOSE_X - UI(8) - SET_LABEL_X) / 6;
      if (room >= 0 && room < (int)sizeof ver) ver[room] = 0; }
    draw_text_8px(&c, SET_LABEL_X, PAGE_H - 8 - 6, MSP_DIM, ver);
#else
    draw_text_8px(&c, SET_LABEL_X + (PAGE_BOWL ? 96 : 0), PAGE_H - 8 - 6, MSP_DIM, ver);
#endif
    button(&c, SET_CLOSE_X, SET_FOOT_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", UI_TEXT(2));
#ifndef CONFIG_POCKET_TANK_320X240 /* (this fork) a 320x240 board has no update channel: no UPDATES (CYD.md) */
    /* UPDATES (2026-09-30, docs/OTA.md): bottom left, the same size as CLOSE */
    button(&c, SET_UPD_X, SET_FOOT_Y, SET_UPD_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "UPDATES", 2);
#endif
}
static int set_segment(float x, int n) {
    if (x < SET_SEG_X - UI(10)) return -1;
    int i = (int)((x - SET_SEG_X + UI(3)) / SET_SEG_DX);
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}
/* the hit test: what a TAP at (x,y) means. *value: BRIGHT the percent the
 * slider's step nearest x,
 * VOLUME 0..2, FEED 1 = ON, SCREEN 1 = TURNED; ROTATE carries none (a
 * toggle); the LIGHTS OUT row's own hits are LIGHT_PREV / LIGHT_NEXT (its
 * left and right halves). */
enum { SET_HIT_LIGHT_PREV = 100, SET_HIT_LIGHT_NEXT };
int render_settings_tap(float x, float y, int *value) {
    x -= PAGE_X; y -= PAGE_Y;                    /* the page's own coordinates */
    if (x >= SET_CLOSE_X - UI(8) && y >= SET_FOOT_Y - UI(4)) return SET_TAP_CLOSE;
#ifndef CONFIG_POCKET_TANK_320X240 /* (this fork) a 320x240 board has no UPDATES button */
    if (x < SET_UPD_X + SET_UPD_W + 8 && y >= SET_FOOT_Y - 4) return SET_TAP_UPDATES;
#endif
    /* the row bands: from a little above each segment down to the next row
       (fingers report low); the last one stops at the foot's */
    int seg = set_segment(x, 3), two = set_segment(x, 2);
    if (y >= SET_SEG_Y(SET_ROW1_Y) - UI(12) && y < SET_SEG_Y(SET_ROW2_Y) - UI(12)) { if (seg < 0) return SET_TAP_NONE; *value = set_bright_at(x); return SET_TAP_BRIGHT; }
    if (y >= SET_SEG_Y(SET_ROW2_Y) - UI(12) && y < SET_SEG_Y(SET_ROW3_Y) - UI(12)) { if (seg < 0) return SET_TAP_NONE; *value = seg; return SET_TAP_VOLUME; }
    if (y >= SET_SEG_Y(SET_ROW3_Y) - UI(12) && y < SET_SEG_Y(SET_ROW4_Y) - UI(12)) { if (seg < 0) return SET_TAP_NONE; *value = 0; return x < SET_SPAN_MID ? SET_HIT_LIGHT_PREV : SET_HIT_LIGHT_NEXT; }
    if (y >= SET_SEG_Y(SET_ROW4_Y) - UI(12) && y < SET_SEG_Y(SET_ROW5_Y) - UI(12)) { if (two < 0) return SET_TAP_NONE; *value = two == 0; return SET_TAP_FEED; }
#ifdef CONFIG_POCKET_TANK_320X240
    /* (this fork) a 320x240 board: SCREEN (no IMU) or ROTATION, then SLEEP (not on a deepsleep build) */
    if (y >= SET_SEG_Y(SET_ROW5_Y) - UI(12) && y < SET_SEG_Y(SET_ROW6_Y) - UI(12)) {
        if (two < 0) return SET_TAP_NONE;
        if (!g_set_imu) { *value = two == 1; return SET_TAP_FLIP; }
        *value = 0; return SET_TAP_ROTATE;
    }
    if (y >= SET_SEG_Y(SET_ROW6_Y) - UI(12) && y < SET_FOOT_Y - UI(4)) {
        if (seg < 0 || g_set_sleep < 0) return SET_TAP_NONE;
        *value = seg; return SET_TAP_SLEEP;      /* three segments, as BRIGHTNESS: SET_SLEEP_* */
    }
#else
    if (y >= SET_SEG_Y(SET_ROW5_Y) - 12 && y < SET_FOOT_Y - 4) {
        if (two < 0) return SET_TAP_NONE;
#if TANK_WORN
        *value = two == 1; return SET_TAP_SCREEN;
#else
        *value = 0; return SET_TAP_ROTATE;       /* the button or its word: one toggle */
#endif
    }
#endif
    return SET_TAP_NONE;
}
static bool s_set_down; static float s_set_px, s_set_py; static int s_set_hit, s_set_hv;   /* render_settings_touch's press in hand */
void render_settings_cancel(void) { s_set_down = false; s_set_hit = SET_TAP_NONE; }
int render_settings_touch(tank_t *t, float x, float y, bool down, int *value) {
    int r = SET_TAP_NONE; *value = 0;
    if (down && !s_set_down) {                                  /* press */
        s_set_px = x; s_set_py = y;
        s_set_hit = render_settings_tap(x, y, &s_set_hv);
        if (s_set_hit == SET_TAP_BRIGHT) { r = SET_TAP_BRIGHT_LIVE; *value = s_set_hv; }   /* the slider: the knob jumps to the finger */
    } else if (down && s_set_hit == SET_TAP_BRIGHT) {          /* the slider under a finger: the knob follows it along the row, */
        int v = set_bright_at(x - PAGE_X);                  /* however far up or down the finger wanders */
        if (v != s_set_hv) { s_set_hv = v; r = SET_TAP_BRIGHT_LIVE; *value = v; }
    } else if (!down && s_set_down && s_set_hit == SET_TAP_BRIGHT) {   /* let go: the last step it showed is the setting */
        r = SET_TAP_BRIGHT; *value = s_set_hv;
    } else if (!down && s_set_down) {                           /* release: a tap, if it stayed on what it pressed */
        int v = 0, h = render_settings_tap(x, y, &v);
        float dx = x - s_set_px, dy = y - s_set_py;
        if (h == s_set_hit && dx * dx + dy * dy < 24 * 24) {
            if (h == SET_HIT_LIGHT_PREV || h == SET_HIT_LIGHT_NEXT) {   /* one choice along; either way the light comes on */
                int was = tank_light_choice(t), now = was + (h == SET_HIT_LIGHT_NEXT ? 1 : -1);
                if (now >= 0 && now <= LIGHT_IDLE_N) {
                    tank_light_choice_set(t, now); tank_emit(TEV_WHEEL_TICK, -1); progression_settings_changed();
                    if ((was == 0) != (now == 0)) { r = SET_TAP_LIGHT; *value = now != 0; }
                    else { r = SET_TAP_IDLE; *value = t->light_idle_s; }
                }
            } else if (h == SET_TAP_FEED) {                    /* ON (1, the default) / OFF */
                if ((v == 0) != t->autofeed_off) { t->autofeed_off = v == 0; progression_settings_changed(); }
                r = SET_TAP_FEED; *value = v;
            } else if (h == SET_TAP_ROTATE) {                  /* lock the way up it has now, or let it turn again */
                tank_orient_lock(t, !t->orient_lock); progression_settings_changed();
                r = SET_TAP_ROTATE; *value = t->orient_lock;
            } else if (h == SET_TAP_SCREEN) {                  /* the way up: NORMAL (0, the default) / TURNED (1) */
                if ((v != 0) != t->screen_turned) { tank_screen_set(t, v != 0); progression_settings_changed(); }
                r = SET_TAP_SCREEN; *value = v;
            } else if (h == SET_TAP_CLOSE || h == SET_TAP_VOLUME || h == SET_TAP_UPDATES
                       || h == SET_TAP_FLIP || h == SET_TAP_SLEEP) { r = h; *value = v; }   /* FLIP, SLEEP: the 320x240 boards', the platform's to apply */
        }
    }
    s_set_down = down;
    return r;
}

/* ---- THE ADVANCED PAGE (0.3.9.2) ---------------------------------------
 * Hidden behind a BOOT chord (common/advanced.c). Two rows on the settings
 * page's grid: TOUCH (one wide CALIBRATE button) and BATTERY (the size
 * between two arrows, as LIGHTS OUT steps its choice), a quiet note under
 * each, BACK where CLOSE stands; FOR GROWN-UPS small under the title.
 * (this fork: every measurement in UI(), every text scale in UI_TEXT() - the
 * design on the 1.8 and its kin, scaled on a 320 x 240 board's page. There
 * the page is TOUCH alone: no charger the firmware sets, ADV_BATTERY_ROW.) */
#define ADV_WARN_RGB  0xffc94a          /* the warning's triangle (its shape says it; the hue only helps) */
#ifdef CONFIG_POCKET_TANK_320X240
#define ADV_ROW1_Y    SET_ROW2_Y        /* (this fork) the TOUCH row's label line: the settings grid's first row is 20 px under
                                           the title here, where FOR GROWN-UPS goes, so the page's two rows start a row lower */
#else
#define ADV_ROW1_Y    SET_ROW1_Y
#endif
#define ADV_BAT_ROW   (ADV_ROW1_Y + UI(108))                      /* the BATTERY row's label line */
#define ADV_SET_Y     (SET_SEG_Y(ADV_BAT_ROW) + SET_SEG_H + UI(34))   /* SET <size>, under the row's note */
#define ADV_SET_H     UI(36)
#define ADV_WARN_BOX_Y (SET_TITLE_Y + UI(202))                    /* the warning's box to tick: 20 px under READ THE SIZE PRINTED ON IT. */
#define ADV_WARN_BOX   UI(30)
#define ADV_CONT_W     UI(128)
#define ADV_CONT_X     (SET_CLOSE_X + MSP_CLOSE_W - ADV_CONT_W > SET_UPD_X + SET_UPD_W + UI(10) ? SET_CLOSE_X + MSP_CLOSE_W - ADV_CONT_W : SET_UPD_X + SET_UPD_W + UI(10))
#define ADV_START_Y    (SET_TITLE_Y + UI(164))                    /* the touch page's START */
#define ADV_START_W    UI(140)
#define ADV_RESET_Y    (ADV_START_Y + SET_SEG_H + UI(16))         /* ... RESET TOUCH under it */
#define ADV_RESET_W    UI(160)
/* the warning sign: a solid triangle, point up, w wide, its ! cut out in ink */
static void adv_warn_sign(ctx_t *c, int cx, int top, int w, uint32_t rgb) {
    int h = w * 7 / 8;
    for (int dy = 0; dy < h; dy++) {
        int half = (w / 2) * dy / h;
        rect_fill(c, cx - half, top + dy, 2 * half + 1, 1, rgb);
    }
    int bw = w / 9 < 2 ? 2 : w / 9;
    rect_fill(c, cx - bw / 2, top + h * 3 / 8, bw, h * 3 / 8, MSP_INK);
    rect_fill(c, cx - bw / 2, top + h * 13 / 16, bw, bw, MSP_INK);
}
static void text_centre(ctx_t *c, int y, int scale, uint32_t rgb, const char *s) {
    draw_text(c, (PAGE_W - text_w(s, scale)) / 2, y, scale, rgb, s);
}
static void lit_button(ctx_t *c, int x, int y, int w, int h, const char *label) {   /* teal, ink lettering */
    button(c, x, y, w, h, MSP_TEAL, MSP_TEAL, "", UI_TEXT(2));
    draw_text(c, x + (w - text_w(label, UI_TEXT(2))) / 2, y + (h - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_INK, label);
}
void render_advanced(uint16_t *fb, int stride, int pick_mah, int pick_ma, int set_mah, bool stock, bool fast) {
    ctx_t c = ctx_page(fb, stride);
    char txt[40];
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    text_centre(&c, SET_TITLE_Y, UI_TEXT(3), 0xffffff, "ADVANCED");
    draw_text_8px(&c, (PAGE_W - (13 * 6 - 1)) / 2, SET_TITLE_Y + UI(25), MSP_QUIET, "FOR GROWN-UPS");
    /* TOUCH */
    draw_text(&c, SET_LABEL_X, ADV_ROW1_Y, UI_TEXT(2), MSP_TEAL, "TOUCH");
    button(&c, SET_SEG_X, SET_SEG_Y(ADV_ROW1_Y), SET_SPAN_W, SET_SEG_H, 0x1c2f36, MSP_TEAL, "CALIBRATE", UI_TEXT(2));
    draw_text(&c, SET_LABEL_X, SET_SEG_Y(ADV_ROW1_Y) + SET_SEG_H + UI(10), UI_TEXT(2), MSP_QUIET, "TAPS MISSING THEIR MARK?");
    if (!ADV_BATTERY_ROW) {                       /* (this fork) a 320 x 240 board: no charger the firmware sets, so no BATTERY row (advanced.h) */
        button(&c, SET_CLOSE_X, SET_FOOT_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "BACK", UI_TEXT(2));
        return;
    }
    /* BATTERY: < 800 MAH > */
    int y = SET_SEG_Y(ADV_BAT_ROW);
    int bx = SET_SEG_X + SET_ARW_W + UI(4), bw = SET_SPAN_W - 2 * (SET_ARW_W + UI(4));
    const int *sz; int n = adv_cell_sizes(&sz);
    draw_text(&c, SET_LABEL_X, ADV_BAT_ROW, UI_TEXT(2), MSP_TEAL, "BATTERY");
    set_arrow(&c, SET_SEG_X, y, false, pick_mah > sz[0]);
    button(&c, bx, y, bw, SET_SEG_H, MSP_TEAL, MSP_TEAL, "", UI_TEXT(2));
    snprintf(txt, sizeof txt, "%d MAH", pick_mah);
    int tw = text_w(txt, UI_TEXT(2)), sx = bx + (bw - tw - (fast ? UI(22) : 0)) / 2;
    if (fast) adv_warn_sign(&c, sx + UI(8), y + (SET_SEG_H - UI(15)) / 2, UI(17), MSP_INK);   /* faster than the board's own: the sign rides along */
    draw_text(&c, sx + (fast ? UI(22) : 0), y + (SET_SEG_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_INK, txt);
    set_arrow(&c, SET_SEG_X + SET_SPAN_W - SET_ARW_W, y, true, pick_mah < sz[n - 1]);
    if (stock) snprintf(txt, sizeof txt, "STANDARD - CHARGES AT %d MA", pick_ma);
    else       snprintf(txt, sizeof txt, "CHARGES AT %d MA", pick_ma);
    draw_text(&c, SET_LABEL_X, y + SET_SEG_H + UI(10), UI_TEXT(2), MSP_QUIET, txt);
    if (pick_mah != set_mah) {                    /* a size picked, not in use yet: SET applies it (the warning first, when it is faster) */
        snprintf(txt, sizeof txt, "SET %d MAH", pick_mah);
        lit_button(&c, SET_SEG_X, ADV_SET_Y, SET_SPAN_W, ADV_SET_H, txt);
        snprintf(txt, sizeof txt, "NOW %d MAH", set_mah);
        draw_text(&c, SET_LABEL_X, ADV_SET_Y + (ADV_SET_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_QUIET, txt);
    } else draw_text(&c, SET_LABEL_X, y + SET_SEG_H + UI(32), UI_TEXT(2), MSP_QUIET, "MATCH THE SIZE ON YOUR BATTERY");
    button(&c, SET_CLOSE_X, SET_FOOT_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "BACK", UI_TEXT(2));
}
int render_advanced_tap(float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;
    if (x >= SET_CLOSE_X - UI(8) && y >= SET_FOOT_Y - UI(4)) return ADV_HIT_BACK;
    if (x < SET_SEG_X - UI(10)) return ADV_HIT_NONE;
    if (y >= SET_SEG_Y(ADV_ROW1_Y) - UI(12) && y < SET_SEG_Y(ADV_ROW1_Y) + SET_SEG_H + UI(12)) return ADV_HIT_CALIBRATE;
    if (!ADV_BATTERY_ROW) return ADV_HIT_NONE;    /* (this fork) no BATTERY row drawn, so none of its taps: no size, no SET, no warning */
    if (y >= SET_SEG_Y(ADV_BAT_ROW) - UI(12) && y < SET_SEG_Y(ADV_BAT_ROW) + SET_SEG_H + UI(12))
        return x < SET_SPAN_MID ? ADV_HIT_PREV : ADV_HIT_NEXT;                         /* the row's halves, as LIGHTS OUT's */
    if (y >= ADV_SET_Y - UI(8) && y < ADV_SET_Y + ADV_SET_H + UI(12)) return ADV_HIT_SET;   /* (only while it is drawn: advanced.c asks) */
    return ADV_HIT_NONE;
}
/* the battery warning: a whole page - the sign and WARNING, what the size
 * does in plain words, the box to tick (the size again, in the keeper's own
 * hand), CANCEL and CONTINUE. CONTINUE is an outline with dim words until
 * the box is ticked, then a lit button (brightness and fill, not hue). */
void render_battery_warning(uint16_t *fb, int stride, int cell_mah, int charge_ma, bool checked) {
    ctx_t c = ctx_page(fb, stride);
    char txt[48];
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    {   /* the sign and the word, one group, centred */
        int sw = UI(36), gap = UI(12), tw = text_w("WARNING", UI_TEXT(3)), x0 = (PAGE_W - sw - gap - tw) / 2;
        adv_warn_sign(&c, x0 + sw / 2, SET_TITLE_Y - UI(6), sw, ADV_WARN_RGB);
        draw_text(&c, x0 + sw + gap, SET_TITLE_Y, UI_TEXT(3), 0xffffff, "WARNING");
    }
    int y = SET_TITLE_Y + UI(42);
    text_centre(&c, y, UI_TEXT(2), 0xffffff, "ONLY FOR A BIGGER BATTERY."); y += UI(30);
    snprintf(txt, sizeof txt, "THE TANK WILL CHARGE AT %d MA.", charge_ma);
    text_centre(&c, y, UI_TEXT(2), MSP_TEAL, txt); y += UI(22);
    text_centre(&c, y, UI_TEXT(2), MSP_TEAL, "A SMALLER BATTERY CHARGED THIS"); y += UI(22);
    text_centre(&c, y, UI_TEXT(2), MSP_TEAL, "FAST CAN GET HOT, SWELL UP"); y += UI(22);
    text_centre(&c, y, UI_TEXT(2), MSP_TEAL, "OR CATCH FIRE."); y += UI(30);
    text_centre(&c, y, UI_TEXT(2), 0xffffff, "READ THE SIZE PRINTED ON IT.");
    {   /* the box: [x]  MY BATTERY IS 800 MAH / OR BIGGER */
        snprintf(txt, sizeof txt, "MY BATTERY IS %d MAH", cell_mah);
        int bs = ADV_WARN_BOX, gap = UI(14), tw = text_w(txt, UI_TEXT(2)), x0 = (PAGE_W - bs - gap - tw) / 2, by = ADV_WARN_BOX_Y;
        rect_fill(&c, x0, by, bs, bs, checked ? MSP_TEAL : 0x1c2f36);
        rect_edge(&c, x0, by, bs, bs, MSP_TEAL); rect_edge(&c, x0 + 1, by + 1, bs - 2, bs - 2, MSP_TEAL);
        if (checked)                                         /* the tick: a short stroke down, a long one up */
            for (int k = 0; k < UI(18); k++) {
                int px = x0 + UI(6) + k, py = k < UI(6) ? by + UI(14) + k : by + UI(20) - (k - UI(6)) * 13 / 12;
                rect_fill(&c, px, py - 1, 2, UI(4), MSP_INK);
            }
        draw_text(&c, x0 + bs + gap, by, UI_TEXT(2), 0xffffff, txt);
        draw_text(&c, x0 + bs + gap, by + UI(20), UI_TEXT(2), 0xffffff, "OR BIGGER");
    }
    button(&c, SET_UPD_X, SET_FOOT_Y, SET_UPD_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CANCEL", UI_TEXT(2));
    if (checked) lit_button(&c, ADV_CONT_X, SET_FOOT_Y, ADV_CONT_W, MSP_CLOSE_H, "CONTINUE");
    else {
        rect_edge(&c, ADV_CONT_X, SET_FOOT_Y, ADV_CONT_W, MSP_CLOSE_H, MSP_DIM);
        draw_text(&c, ADV_CONT_X + (ADV_CONT_W - text_w("CONTINUE", UI_TEXT(2))) / 2, SET_FOOT_Y + (MSP_CLOSE_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_DIM, "CONTINUE");
    }
}
int render_battery_warning_tap(float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;
    if (y >= SET_FOOT_Y - UI(8)) {
        if (x >= SET_UPD_X - UI(8) && x < SET_UPD_X + SET_UPD_W + UI(4)) return ADV_HIT_CANCEL;
        if (x >= ADV_CONT_X - UI(4) && x < ADV_CONT_X + ADV_CONT_W + UI(8)) return ADV_HIT_CONTINUE;
        return ADV_HIT_NONE;
    }
    if (y >= ADV_WARN_BOX_Y - UI(12) && y < ADV_WARN_BOX_Y + UI(46)) return ADV_HIT_BOX;    /* the box or its words: the whole line */
    return ADV_HIT_NONE;
}
/* touch calibration's pages */
void render_touch_intro(uint16_t *fb, int stride, bool own_in_use, const char *msg) {
    ctx_t c = ctx_page(fb, stride);
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    text_centre(&c, SET_TITLE_Y, UI_TEXT(3), 0xffffff, "TOUCH");
    int y = SET_TITLE_Y + UI(46);
    text_centre(&c, y, UI_TEXT(2), MSP_TEAL, "HOLD THE TANK THE WAY"); y += UI(22);
    text_centre(&c, y, UI_TEXT(2), MSP_TEAL, "YOU USUALLY DO."); y += UI(34);
    text_centre(&c, y, UI_TEXT(2), MSP_TEAL, "NINE CROSSES COME UP."); y += UI(22);
    text_centre(&c, y, UI_TEXT(2), MSP_TEAL, "TAP THE MIDDLE OF EACH ONE.");
    lit_button(&c, (PAGE_W - ADV_START_W) / 2, ADV_START_Y, ADV_START_W, SET_SEG_H, "START");
    if (own_in_use) button(&c, (PAGE_W - ADV_RESET_W) / 2, ADV_RESET_Y, ADV_RESET_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "RESET TOUCH", UI_TEXT(2));
    if (msg) text_centre(&c, ADV_RESET_Y + MSP_CLOSE_H + UI(10), UI_TEXT(2), 0xffffff, msg);
    button(&c, SET_CLOSE_X, SET_FOOT_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "BACK", UI_TEXT(2));
}
int render_touch_intro_tap(float x, float y, bool own_in_use) {
    x -= PAGE_X; y -= PAGE_Y;
    if (x >= SET_CLOSE_X - UI(8) && y >= SET_FOOT_Y - UI(4)) return ADV_HIT_BACK;
    if (fabsf(x - PAGE_W / 2) < ADV_START_W / 2 + UI(12) && y >= ADV_START_Y - UI(12) && y < ADV_START_Y + SET_SEG_H + UI(6)) return ADV_HIT_START;
    if (own_in_use && fabsf(x - PAGE_W / 2) < ADV_RESET_W / 2 + UI(12) && y >= ADV_RESET_Y - UI(6) && y < ADV_RESET_Y + MSP_CLOSE_H + UI(12)) return ADV_HIT_RESET;
    return ADV_HIT_NONE;
}
static void touch_cross(ctx_t *c, int fx, int fy, uint32_t rgb) {      /* a cross at FRAME px */
    int x = fx - PAGE_X, y = fy - PAGE_Y;
    rect_fill(c, x - UI(24), y - 1, 2 * UI(24) + 1, 3, rgb); rect_fill(c, x - 1, y - UI(24), 3, 2 * UI(24) + 1, rgb);
    rect_fill(c, x - UI(4), y - UI(4), 2 * UI(4) + 1, 2 * UI(4) + 1, 0xffffff);
}
void render_touch_cross(uint16_t *fb, int stride, int i, int n, int tx, int ty) {
    ctx_t c = ctx_page(fb, stride);
    char txt[32];
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    touch_cross(&c, tx, ty, MSP_TEAL);
    snprintf(txt, sizeof txt, "TAP THE CROSS  %d OF %d", i + 1, n);
    text_centre(&c, ty - PAGE_Y < PAGE_H / 2 ? PAGE_H - UI(60) : UI(46), UI_TEXT(2), 0xffffff, txt);
}
void render_touch_check(uint16_t *fb, int stride, int tx, int ty, float secs_left, float secs_total) {
    ctx_t c = ctx_page(fb, stride);
    char txt[40];
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    touch_cross(&c, tx, ty, MSP_TEAL);
    int y = ty - PAGE_Y < PAGE_H / 2 ? PAGE_H - UI(96) : UI(30);
    text_centre(&c, y, UI_TEXT(2), 0xffffff, "TAP THIS CROSS TO KEEP"); y += UI(22);
    text_centre(&c, y, UI_TEXT(2), 0xffffff, "THE NEW TOUCH"); y += UI(28);
    int s = (int)ceilf(secs_left < 0 ? 0 : secs_left);
    snprintf(txt, sizeof txt, "THE OLD ONE COMES BACK IN %d", s);
    text_centre(&c, y, UI_TEXT(2), MSP_QUIET, txt); y += UI(24);
    int bw = UI(240), bx = (PAGE_W - bw) / 2;                /* the time left, a bar that shrinks */
    float f = secs_total > 0 ? secs_left / secs_total : 0; if (f < 0) f = 0;
    rect_edge(&c, bx, y, bw, UI(8), MSP_DIM);
    rect_fill(&c, bx + UI(2), y + UI(2), (int)((bw - 2 * UI(2)) * f), UI(8) - 2 * UI(2), MSP_TEAL);
}
