/* render.c — porthole look: deep gradient water, AMOLED-black floor, procedural
 * fish (body polygon + animated tail + earned markings), bubbles, reef fronds
 * that grow with the tank's milestones. Everything is drawn
 * into a bare RGB565 buffer; night dims the palette. */
#include "render.h"
#include "ui.h"
#include "icons.h"
#include "progression.h"
#include "tank_events.h"
#include "setup.h"
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
#define DIRTY_WORDS_PER_ROW (TANK_W / 32)          /* 14 */
static uint32_t *g_dirty = NULL;
void render_set_dirty_mask(uint32_t *buf) { g_dirty = buf; }
static inline void dirty_px(int x, int y) {
    if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] |= 1u << (x & 31);
}
static inline void dirty_span(int x0, int x1, int y) {
    if (!g_dirty) return;
    uint32_t *row = g_dirty + y * DIRTY_WORDS_PER_ROW;
    int w0 = x0 >> 5, w1 = x1 >> 5;
    if (w0 == w1) { row[w0] |= (0xFFFFFFFFu >> (31 - (x1 & 31))) & (0xFFFFFFFFu << (x0 & 31)); return; }
    row[w0] |= 0xFFFFFFFFu << (x0 & 31);
    for (int w = w0 + 1; w < w1; w++) row[w] = 0xFFFFFFFFu;
    row[w1] |= 0xFFFFFFFFu >> (31 - (x1 & 31));
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
static void px(ctx_t *c, int x, int y, uint16_t col) {
    if (CTX_IN(c, x, y)) {
        CTX_PX(c, x, y) = col;
        bb_add(x, x, y);
        dirty_px(x, y);
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
    int dr = (*p >> 11) << 3, dg = ((*p >> 5) & 63) << 2, db = (*p & 31) << 3;
    int r = dr + ((s->r - dr) * a >> 8), g = dg + ((s->g - dg) * a >> 8), b = db + ((s->b - db) * a >> 8);
    *p = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static inline void px_blend_s(ctx_t *c, int x, int y, const src_t *s, int a) {
    if (CTX_IN(c, x, y)) { blend565(&CTX_PX(c, x, y), s, a); dirty_px(x, y); }
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
    bb_add(x0, x1, y);
    dirty_span(x0, x1, y);
    uint16_t *p = &CTX_PX(c, x0, y);
    if (alpha >= 255) for (int x = x0; x <= x1; x++) *p++ = s->v;
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

static int popcount32(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

/* per-fish roll state: +1 = normal, -1 = mirrored (facing left). Fish roll to
 * keep their back up instead of swimming inverted; animating the transition
 * reads as the fish turning over. Render-local so tank state stays pure. */
static float g_roll[N_FISH_MAX] = { 1, 1, 1, 1, 1, 1 };

/* The fish's body is a ledger of its life: stage sets size (tank.c) and fin
 * elaboration, hunger drains saturation, and earned markings stay:
 *   elder -> a longer tail and a dorsal crest. All procedural, zero flash. */
/* `mark` scales the accent stripes: 1 in the tank (every stage wears the
 * same 2 x 5 px marks), the preview's size so they read on a big fish */
static void draw_fish_core(ctx_t *c, const fish_t *f, float clock, bool asleep, float roll, float mark) {
    float tail = sinf(clock * (8 + f->speed * 0.055f) + f->wander) *
                 (0.32f + f->speed * 0.007f);
    float stress = f->stress / 10.0f;
    float pale = f->hunger > 7 ? (f->hunger - 7) / 3.0f * 0.35f : 0;   /* hungry = washed out */
    float ch = cosf(f->heading), sh = sinf(f->heading);
    bool elder = f->stage == STAGE_ELDER, grown = f->stage >= STAGE_ADULT;
    float tail_len = elder ? 1.22f : 1.0f;
    uint32_t body = mix(mix(f->color, 0xf25b65, stress * 0.22f), 0x7a8a8e, pale);
    uint32_t fin  = mix(mix(f->fin, 0xf25b65, stress * 0.18f), 0x5a6a6e, pale);
#define TX(lx, ly) (f->x + ((lx) * ch - (ly) * roll * sh) * f->size)
#define TY(lx, ly) (f->y + ((lx) * sh + (ly) * roll * ch) * f->size)

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
    }
    /* accent stripes (height tracks the roll so they flatten with the body).
     * A FRY has none yet: its markings come in with its first growth spurt
     * (the setup's colour page keeps the accent a "?" for that reason) */
    float rmag = roll < 0 ? -roll : roll;
    if (f->stage >= STAGE_JUV) for (int i = -1; i <= 1; i++)
        fill_ellipse(c, TX(-3 + i * 6, 0), TY(-3 + i * 6, 0),
                     2 * mark, (1.5f + 3.5f * rmag) * mark, f->accent, 150);
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
/* the tank's fish: roll state per slot (the preview above has none) */
static void draw_fish(ctx_t *c, const tank_t *t, const fish_t *f, int idx) {
    float ch = cosf(f->heading);
    float want = ch < -0.05f ? -1.0f : ch > 0.05f ? 1.0f : g_roll[idx];
    g_roll[idx] += (want - g_roll[idx]) * 0.18f;
    draw_fish_core(c, f, t->clock, t->night && f->goal.id == GOAL_REST, g_roll[idx], 1.0f);
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
int64_t render_prof_us[7];
#define PROF_MARK() (render_clock_us ? render_clock_us() : 0)
#define PROF_ADD(i, t0) do { if (render_clock_us) { int64_t _n = render_clock_us(); render_prof_us[i] += _n - (t0); (t0) = _n; } } while (0)

/* ---- static scene: gradient, pebbles, reef rock (cacheable) ---- */
static uint16_t *g_scene = NULL;
static float     g_scene_dim = -1;
static uint32_t  g_scene_ms = 0;        /* tank milestones baked into the reef */
static unsigned  g_scene_epoch = 0;
static const uint16_t *g_primed_fb = NULL;
static unsigned  g_primed_epoch = 0;
static uint8_t  *g_vig = NULL;                  /* per-pixel vignette alpha (static) */
static bool      g_vig_filled = false;
void render_set_scene_cache(uint16_t *buf) { g_scene = buf; g_scene_dim = -1; }
void render_set_vignette_cache(uint8_t *buf) { g_vig = buf; g_vig_filled = false; }

const uint16_t *render_scene_buf(unsigned *epoch) {
    if (epoch) *epoch = g_scene_epoch;
    return g_scene_epoch ? g_scene : NULL;
}
void render_fb_primed(const uint16_t *fb, unsigned epoch) { g_primed_fb = fb; g_primed_epoch = epoch; }

/* vignette alpha at (x,y); matches the classic per-pixel loop */
static inline int vig_alpha(int x, int y) {
    float dx = (x - TANK_W * 0.5f) / (TANK_W * 0.5f);
    float dy = (y - TANK_H * 0.5f) / (TANK_H * 0.5f);
    float d2 = dx * dx + dy * dy;
    if (d2 <= 0.72f) return 0;
    int a = (int)((d2 - 0.72f) * 220);
    return a > 255 ? 255 : a;
}

/* pure darkening (blend toward black), independent of ctx dim */
static inline void px_darken(uint16_t *p, int a) {
    int inv = 256 - a;
    *p = (uint16_t)(((((*p >> 11) * inv) >> 8) << 11) |
                    ((((((*p) >> 5) & 63) * inv) >> 8) << 5) |
                    (((*p & 31) * inv) >> 8));
}

/* a span that is FINAL: blended, then vignetted right here, and NOT marked
 * dirty, so the re-apply sweep never touches it (scene cache mode only) */
static inline void span_final(ctx_t *c, int x0, int x1, int y, const src_t *s, int alpha) {
    if ((unsigned)(y - c->oy) >= (unsigned)c->h) return;
    if (x0 < c->ox) x0 = c->ox;
    if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
    if (x0 > x1) return;
    uint16_t *p = &CTX_PX(c, x0, y);
    /* ONE vignette alpha per span, computed (a 5 px frond span changes it by
       < 4%, invisible) rather than read: the PSRAM LUT costs a cache line
       per span and fronds are vertical, which was most of the frond cost */
    int a = vig_alpha((x0 + x1) >> 1, y);
    if (alpha >= 255) {                         /* opaque: a store, no PSRAM read */
        for (int x = x0; x <= x1; x++, p++) { *p = s->v; if (a) px_darken(p, a); }
        return;
    }
    for (int x = x0; x <= x1; x++, p++) {
        blend565(p, s, alpha);
        if (a) px_darken(p, a);
    }
}

/* water gradient colour of row y (shared by the scene bake and the frond
 * pre-tint below) */
static inline uint32_t water_rgb(int y) {
    float p = (float)y / TANK_H;
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

#define VEG_SEG_DY   3.2f                 /* segment pitch, px of height */
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
#define CASTLE_FY   (TANK_H - 16)
#define CASTLE_ROWS 164
static uint32_t g_castle_mask[CASTLE_ROWS + 12][224 / 32];
static int      g_veg_mask_cx;
static inline void veg_span(ctx_t *c, int x0, int x1, int y, const src_t *s, bool final) {
    if (g_veg_mask_cx >= 0 && y >= CASTLE_FY - CASTLE_ROWS && y < CASTLE_FY + 12 && x1 >= g_veg_mask_cx - 112 && x0 <= g_veg_mask_cx + 111) {
        const uint32_t *row = g_castle_mask[y - (CASTLE_FY - CASTLE_ROWS)];
        int run = -1;
        for (int x = x0; x <= x1 + 1; x++) {
            int lx = x - g_veg_mask_cx + 112;
            bool in = x <= x1 && (unsigned)lx < 224 && ((row[lx >> 5] >> (lx & 31)) & 1);
            if (x <= x1 && !in) { if (run < 0) run = x; }
            else if (run >= 0) { if (final) span_final(c, run, x - 1, y, s, 255); else span(c, run, x - 1, y, s, 255); run = -1; }
        }
        return;
    }
    if (final) span_final(c, x0, x1, y, s, 255);
    else       span(c, x0, x1, y, s, 255);
}
/* layer: 0 = the even fronds, 1 = the odd ones, -1 = every frond */
static void draw_veg(ctx_t *c, const tank_t *t, int b, int seed, int layer, bool final) {
    veg_tint_fill(c->dim);
    int n; tank_veg_bed(t, b, NULL, NULL, NULL, &n);
    /* the sword plant (bed 3, the shop): broad lanceolate leaves - a narrow
       stem, 7 px half-width at 40% of the way up, a point at the tip - a
       lazier sway, its own yellow-green pair */
    bool sword = tank_veg_kind(t, b) == VEG_KIND_SWORD;
    float root = 2.4f, tip = 0.4f, amp = sword ? 2.5f : 4.0f;
    for (int i = 0; i < n; i++) {
        if (layer >= 0 && (i & 1) != layer) continue;
        float bx;
        /* each frond's own height (tank_t.veg_h): what the keeper cut is
           exactly what shows - no render-side variation on top */
        int segs = tank_veg_frond(t, b, i, &bx);
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
         * pixels and without a sqrt per row. Full canopy = ~1000 segments. */
        const uint16_t *pal = g_veg_row[(sword ? 2 : 0) + ((i + seed) & 1)];
        int y_bot = (int)(TANK_H - 16 + VEG_SEG_RY);
        int y_top = (int)(TANK_H - 16 - (segs - 1) * VEG_SEG_DY - VEG_SEG_RY);
        for (int y = y_bot; y >= y_top; y--) {
            float sp = (TANK_H - 16 - y) / VEG_SEG_DY;          /* fractional segment */
            if (sp < 0) sp = 0;
            if (sp > segs - 1) sp = (float)(segs - 1);
            /* taper relative to the frond's own length (2.4 px at the root,
             * 0.4 at the tip): the old fixed 0.07/segment thinned every frond
             * to nothing at 29 segments, a hidden height cap now that fronds
             * grow to the ceiling (VEG_SEGS_FULL) */
            float u = sp / (segs > 1 ? segs - 1 : 1);
            float half = sword ? 1.2f + 6.0f * (u < 0.4f ? u / 0.4f : (1 - u) / 0.6f)
                               : root - (root - tip) * u;
            if (half < tip) half = tip;
            int k = (int)sp; float fr = sp - k;
            float cx = xs[k] + (xs[k + 1] - xs[k]) * fr;
            src_t s; s.v = pal[y];                              /* opaque: only .v is read */
            veg_span(c, (int)(cx - half), (int)(cx + half), y, &s, final);
        }
    }
}


/* ---- the castle (2026-09-16, from Strato's castle-v2 mockup) ----
 * A swim-through decoration drawn the way the fish and the fronds are -
 * spans and fills from a little geometry, no bitmap - so it sits in the same
 * water as everything else instead of a pixel-art sprite clashing with it.
 * Two layers: the KEEP (towers, battlements, the base, the dark courtyard
 * behind the arch) is static and bakes into the scene cache with the reef;
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
#define CASTLE_ARCH_R 26                     /* the opening's half-width (and the vault's radius) */
#define CASTLE_ARCH_S 24                     /* the spring line: straight jambs below, the vault above */
#define CASTLE_TRIM   5                      /* the brick trim's width */
enum { CT_LIGHT, CT_MID, CT_MIDDK, CT_DARK, CT_MORTAR, CT_MOSS_A, CT_MOSS_B,
       CT_BRICK_L, CT_BRICK, CT_BRICK_D, CT_ROOF_L, CT_ROOF, CT_ROOF_D, CT_INSIDE, CT_N };
static const uint32_t CASTLE_RGB[CT_N] = {
    0xd9c58c, 0xa99b7b, 0x87795f, 0x5d5446, 0x6a6151, 0x7aa33c, 0x527f2e,
    0xe08a58, 0xc25f38, 0x8b3d24, 0xe4cd8a, 0xc4a95e, 0x8f7a42, 0x061219 };
/* [keep | gate][tone][row], row 0 = CASTLE_FY - CASTLE_ROWS */
static uint16_t g_castle_row[2][CT_N][CASTLE_ROWS + 12];
static float    g_castle_dim = -1;
/* the castle's silhouette, one bit per pixel in local coords (x - cx + 112,
 * 0..223; row 0 = CASTLE_FY - CASTLE_ROWS): every pixel castle_put writes
 * sets its bit, so the first full draw fills it. While the castle stands IN
 * FRONT of the grass, frond spans skip the pixels inside it (veg_span). */
#define CASTLE_MASK_W 224
static void castle_tint_fill(float dim) {
    if (g_castle_dim == dim) return;
    for (int r = 0; r < CASTLE_ROWS + 12; r++) {
        int y = CASTLE_FY - CASTLE_ROWS + r;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        for (int k = 0; k < CT_N; k++) {
            g_castle_row[0][k][r] = rgb565(mix(w, CASTLE_RGB[k], k == CT_INSIDE ? 0.6f : 0.70f), dim);
            g_castle_row[1][k][r] = rgb565(mix(w, CASTLE_RGB[k], k == CT_INSIDE ? 0.6f : 0.82f), dim);
        }
    }
    g_castle_dim = dim;
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
    uint16_t *p = &CTX_PX(c, x, y);
    *p = v;
    { int lx = x - k->cx + CASTLE_MASK_W / 2, r = y - (CASTLE_FY - CASTLE_ROWS);
      if ((unsigned)lx < CASTLE_MASK_W && (unsigned)r < CASTLE_ROWS + 12) g_castle_mask[r][lx >> 5] |= 1u << (lx & 31); }
    if (k->final) {                          /* vignetted here, and untagged: the sweep must not darken it again */
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
static void castle_opening(const cst_t *k) {
    int ys = CASTLE_FY - CASTLE_ARCH_S;
    for (int y = CASTLE_FY; y >= ys - CASTLE_ARCH_R; y--) {
        float half = y > ys ? CASTLE_ARCH_R : CASTLE_ARCH_R * ell_half((ys - y) / (float)CASTLE_ARCH_R);
        if (half < 1) continue;
        castle_span(k, (int)(-half + 0.5f), (int)(half - 0.5f), y, 4, 0, 1);
    }
}
/* the gate wall: the curtain between the left tower and the right, minus the
 * opening. Drawn in the keep pass (baked) and again after the fish. */
#define CASTLE_GATE_X0 (-34)
#define CASTLE_GATE_X1  55
#define CASTLE_WALL_TOP (CASTLE_FY - 58)
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
    castle_rect(&k, -44, 44, CASTLE_WALL_TOP, FY, 0);
    castle_opening(&k);
    k.layer = 1;
    castle_front_row(&k);
}
/* the front row over a rect of the frame (a fish's box): only what the rect
 * covers is recomputed */
static void draw_castle_front_rect(ctx_t *c, int cx, int x0, int y0, int x1, int y1) {
    if (x1 < cx - 92 || x0 > cx + 92 || y1 < CASTLE_FY - CASTLE_ROWS || y0 > CASTLE_FY) return;
    if (x0 < c->ox) x0 = c->ox;
    if (y0 < c->oy) y0 = c->oy;
    if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
    if (y1 > c->oy + c->h - 1) y1 = c->oy + c->h - 1;
    if (x0 > x1 || y0 > y1) return;
    ctx_t cc = { c->fb + (y0 - c->oy) * c->stride + (x0 - c->ox), c->stride, c->dim, x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
    draw_castle(&cc, cx, 1, true);
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
#define CORAL_FY   (TANK_H - 14 + DECOR_SINK)   /* the base, sunk into the pebbles (a mound covers the joint) */
enum { CO_NONE = 0, CO_RIM, CO_BODY, CO_SHADE, CO_LIT, CO_TIP, CO_N };
static int      g_coral_q = -1;               /* the growth step the sprite was built for (CORAL_Q steps) */
#define CORAL_Q 128                           /* ~5.6 h per step over the 30 days: a rebuild each */
static int      g_coral_cells;                /* filled cells in the sprite (the selftest) */
static uint32_t g_coral_rgb = 1;              /* what the rows hold (1 = never) */
static float    g_coral_dim = -1;
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
typedef struct {
    uint8_t  co_sprite[CORAL_CH][CORAL_CW];
    uint16_t co_row[CO_N][CORAL_H];           /* [tone][row], row 0 = CORAL_FY - CORAL_H */
    int8_t   co_sd[CORAL_CH][CORAL_CW];
    uint8_t  co_tp[CORAL_CH][(CORAL_CW + 7) / 8];
    uint8_t  cl_sprite[CL_CH][CL_CW];
    uint16_t cl_row[CL_TONES][CL_H];
    int8_t   cl_sd[CL_CH][CL_CW];
    uint8_t  cl_dp[CL_CH][(CL_CW + 7) / 8];
} decor_scratch_t;
static decor_scratch_t *g_ds;
size_t render_decor_scratch_size(void) { return sizeof(decor_scratch_t); }
void   render_set_decor_scratch(void *buf) { g_ds = (decor_scratch_t *)buf; if (g_ds) memset(g_ds, 0, sizeof *g_ds); g_coral_q = -1; g_coral_rgb = 1; }
static inline decor_scratch_t *ds(void) { if (!g_ds) g_ds = (decor_scratch_t *)calloc(1, sizeof(decor_scratch_t)); return g_ds; }
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
    if (g_coral_rgb == rgb && g_coral_dim == dim) return;
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
        for (int k = 1; k < CO_N; k++) ds()->co_row[k][r] = rgb565(mix(w, tone[k], k == CO_TIP ? 0.94f : 0.90f), dim);   /* a light haze: the art's colour stays saturated */
    }
    g_coral_rgb = rgb; g_coral_dim = dim;
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
static void draw_floor_mound(ctx_t *c, int cx, int half_w, int rise, bool final) {
    const int fl = TANK_H - 14;                                          /* the floor line: the pebbles' highest top */
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
            *p = rgb565(col, c->dim);
            if (final) {
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
                    *p = v;
                    if (final) {
                        int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
                        if (a) px_darken(p, a);
                        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
                    }
                }
            }
        }
    draw_floor_mound(c, cx, CORAL_HALF_W - 4, 9, final);              /* anchored: the trunk grows out of a hump of stones */
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
static int snail_upright_tone(int lx, int ly, float top, float clock) {
    float heave = 0.6f * fast_sin(clock * 2.4f);
    float sw1 = fast_sin(clock * 1.7f), sw2 = fast_sin(clock * 1.7f + 2.2f);
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
static int snail_glass_tone(float lx, float ly, float sx, float sy, float clock) {
    float u;
    for (int sgn = -1; sgn <= 1; sgn += 2)                        /* the stalks: stubs by the head */
        if (sn_seg(lx, ly, sgn * 2.2f, 10.0f, sgn * 3.6f, 11.6f + 0.4f * fast_sin(clock * 2 + sgn + TAU), 0.6f, &u))
            return u > 0.7f ? SN_TIP : SN_F2;
    float lit = (-sx * 0.55f - sy * 0.83f) / 11.0f;
    /* the foot: an egg, broad under the shell, narrowing to the head */
    float ey = ly / 11.0f;
    if (ey >= -1 && ey <= 1) {
        float rx = 6.6f * sqrtf(1 - ey * ey) * (1 - 0.18f * ey);
        float alx = lx < 0 ? -lx : lx;
        if (rx > 0.3f && alx <= rx) {
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
static inline void snail_put(ctx_t *c, int x, int y, int tone, float tint) {
    if (tone == SN_NONE || !CTX_IN(c, x, y)) return;
    uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
    px_blend(c, x, y, mix(w, SNAIL_RGB[tone], tint), 255);
}
/* UPRIGHT on the floor - a side view, mirrored to face the way it walks,
 * drawn in the scene with the fish (vignetted, behind the front fronds) -
 * or flat ON THE GLASS, its underside to the viewer, drawn after the algae
 * like the film itself (it is on the pane). */
static void draw_snail(ctx_t *c, const tank_t *t, bool upright_pass) {
    if (!(t->sd_unlocks & SD_ITEM_SNAIL) || t->snail_x < 0) return;
    bool upright = tank_snail_upright(t);
    if (upright != upright_pass) return;
    int ox = (int)t->snail_x, oy = (int)(t->snail_y + SNAIL_SOLE_DY);
    if (upright) {
        bool flip = cosf(t->snail_heading) < 0;
        for (int lx = -16; lx <= 20; lx++) {
            float xf = (lx + 15) / 28.5f;
            float top = -(2.4f + 3.2f * powf(sinf(xf < 0 ? 0 : xf > 1 ? 1 : xf * 3.14159f), 0.6f));
            if (lx > 8) { float h = -(4.8f + (lx - 8) * 0.55f); if (h < top) top = h; }
            int x = flip ? ox - lx : ox + lx;
            for (int ly = -18; ly <= 0; ly++)
                snail_put(c, x, oy + ly, snail_upright_tone(lx, ly, top, t->clock), SNAIL_TINT_FLOOR);
        }
    } else {
        float a = t->snail_heading - 1.5708f;                      /* the head points +y locally */
        float ca = cosf(a), sa = sinf(a);
        int cx = (int)t->snail_x, cy = (int)t->snail_y;
        for (int dy = -17; dy <= 17; dy++)
            for (int dx = -17; dx <= 17; dx++) {
                float lx = ca * dx + sa * dy, ly = -sa * dx + ca * dy;   /* inverse rotation */
                snail_put(c, cx + dx, cy + dy, snail_glass_tone(lx, ly, (float)dx, (float)dy, t->clock), SNAIL_TINT_GLASS);
            }
    }
}
/* where the castle stands this frame: its centre x (-1 = not bought), its
 * depth, and whether the keeper is dragging it on the placement page (then
 * it is drawn live over a scene baked WITHOUT it, instead of a rebake per
 * frame) */
static bool castle_state(const tank_t *t, int *cx, int *z, bool *placing) {
    if (!(t->sd_unlocks & SD_ITEM_CASTLE)) { *cx = -1; *z = DECOR_Z_FRONT; *placing = false; return false; }
    *cx = (int)tank_decor_x(t, 2); *z = tank_decor_z(t, 2);
    *placing = setup_is_place() && setup_item() == 2;
    return true;
}
static int g_scene_castle_x = -2, g_scene_castle_z = -1;   /* what the baked scene holds (-1 = no castle) */
/* the coral likewise (2026-09-23): BEHIND it is baked into the scene; AMONG
 * and IN FRONT it is drawn every frame (a 60 x 92 sprite - cheap) */
static bool coral_state(const tank_t *t, int *cx, int *z, bool *placing) {
    if (!(t->sd_unlocks & SD_ITEM_CORAL)) { *cx = -1; *z = DECOR_Z_MIDDLE; *placing = false; return false; }
    *cx = (int)tank_decor_x(t, 3); *z = tank_decor_z(t, 3);
    *placing = setup_is_place() && setup_item() == 3;
    return true;
}

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
#define CL_FY   (TANK_H - 14 + DECOR_SINK)
#define CL_Q    64                                           /* sprite rebuilds over the size phase */
enum { CLE_ROCK, CLE_WEED, CLE_CORAL, CLE_TUBE, CLE_BRAIN, CLE_N };
enum { CLV_BODY, CLV_LIT, CLV_RIM, CLV_SHADE, CLV_DEEP, CLV_N };
#define CL_TONE(e, v) (1 + (e) * CLV_N + (v))
static int      g_cl_q = -1, g_cl_cells;
static int      g_cl_scheme = -1; static float g_cl_dim = -1;
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
    if (g_cl_scheme == scheme && g_cl_dim == dim) return;
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
        for (int k = 1; k < CL_TONES; k++) ds()->cl_row[k][r] = rgb565(mix(w, tone[k], 0.90f), dim);
    }
    g_cl_scheme = scheme; g_cl_dim = dim;
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
                    *p = v;
                    if (final) {
                        int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
                        if (a) px_darken(p, a);
                        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
                    }
                }
            }
        }
    draw_floor_mound(c, cx, CLUSTER_HALF_W - 2, 6, final);            /* anchored: the rock sits in a low hump of the floor's stones */
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

static bool cluster_state(const tank_t *t, int *cx, int *z, bool *placing) {
    if (!(t->sd_unlocks & SD_ITEM_CLUSTER)) { *cx = -1; *z = DECOR_Z_MIDDLE; *placing = false; return false; }
    *cx = (int)tank_decor_x(t, 4); *z = tank_decor_z(t, 4);
    *placing = setup_is_place() && setup_item() == 4;
    return true;
}
static int g_scene_coral_x = -2, g_scene_coral_z = -1, g_scene_coral_q = -1; static uint32_t g_scene_coral_rgb;
static int g_scene_cl_x = -2, g_scene_cl_z = -1, g_scene_cl_q = -1, g_scene_cl_scheme = -1;

static void draw_scene(const tank_t *t, uint16_t *fb, int stride, float dim) {
    ctx_t c = ctx_full(fb, stride, dim);
    /* water gradient #0a3c46 → #08272f → #031015 */
    for (int y = 0; y < TANK_H; y++) {
        float p = (float)y / TANK_H;
        uint32_t col = p < 0.45f ? mix(0x0a3c46, 0x08272f, p / 0.45f)
                                 : mix(0x08272f, 0x031015, (p - 0.45f) / 0.55f);
        uint16_t v = rgb565(col, dim);
        for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = v;
    }
    /* pebbled bottom: irregular top edge, speckled stones in three tones.
       All variation comes from a position hash so it is stable every frame. */
    for (int x = 0; x < TANK_W; x++) {
        uint32_t h = (uint32_t)(x * 2654435761u);
        int top = TANK_H - 14 - (int)((h >> 8) % 5);
        for (int y = top; y < TANK_H; y++) {
            uint32_t h2 = (uint32_t)((x * 73856093u) ^ (y * 19349663u));
            uint32_t tone = (h2 >> 4) % 16;
            uint32_t col = tone < 2 ? 0x2e3b2c : tone < 5 ? 0x22301f
                         : tone < 8 ? 0x1a2418 : 0x101a12;
            fb[y * stride + x] = rgb565(col, dim);
        }
    }
    /* reef rock (the entity fish know); it widens as the tank earns milestones */
    float grow = 1.0f + 0.06f * popcount32(t->tank_ms_bits);
    fill_ellipse(&c, t->reef_x, TANK_H - 16, 34 * grow, 10 + 2 * (grow - 1) * 10, 0x123028, 255);
    { int cx, z; bool placing; if (castle_state(t, &cx, &z, &placing)) draw_castle(&c, cx, 0, false); }   /* uncached: always drawn here */
    { int cx, z; bool placing; if (coral_state(t, &cx, &z, &placing) && z == DECOR_Z_BACK) draw_coral(&c, cx, tank_coral_rgb(t), tank_coral_growth(t), false); }
    { int cx, z; bool placing; if (cluster_state(t, &cx, &z, &placing) && z == DECOR_Z_BACK) draw_cluster(&c, cx, tank_cluster_scheme(t), tank_cluster_growth(t), false); }
}


/* The baked scene (scene cache mode). Water gradient x porthole vignette
 * computed in 8-bit and ORDERED-DITHERED to RGB565 (4x4 Bayer: at 322 ppi
 * the pattern is invisible, the 5/6-bit banding of a dark gradient and of
 * the vignette falloff is not - Strato saw it at the edges), then the floor
 * and the reef on top, vignetted per pixel. Fills the vignette LUT on the
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
            sc[y * TANK_W + x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
    }
    /* pebbled bottom: irregular top edge, speckled stones in three tones,
       stable position hash; vignetted in 8-bit before quantising */
    for (int x = 0; x < TANK_W; x++) {
        uint32_t h = (uint32_t)(x * 2654435761u);
        int top = TANK_H - 14 - (int)((h >> 8) % 5);
        for (int y = top; y < TANK_H; y++) {
            uint32_t h2 = (uint32_t)((x * 73856093u) ^ (y * 19349663u));
            uint32_t tone = (h2 >> 4) % 16;
            uint32_t col = tone < 2 ? 0x2e3b2c : tone < 5 ? 0x22301f
                         : tone < 8 ? 0x1a2418 : 0x101a12;
            int inv = 256 - (g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y));
            int r = (int)(((col >> 16) & 255) * dim) * inv >> 8;
            int g = (int)(((col >> 8) & 255) * dim) * inv >> 8;
            int b = (int)((col & 255) * dim) * inv >> 8;
            sc[y * TANK_W + x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    /* reef rock (the entity fish know); widens as the tank earns milestones */
    ctx_t c = ctx_full(sc, TANK_W, dim);
    src_t s = src_color(0x123028, dim);
    float grow = 1.0f + 0.06f * popcount32(t->tank_ms_bits);
    float rx = 34 * grow, ry = 10 + 2 * (grow - 1) * 10, cy = TANK_H - 16;
    for (int y = (int)(cy - ry); y <= (int)(cy + ry); y++) {
        float w = ell_half((y - cy) / ry);
        if (w <= 0) continue;
        span_final(&c, (int)(t->reef_x - rx * w), (int)(t->reef_x + rx * w), y, &s, 255);
    }
    if (g_scene_castle_x >= 0) draw_castle(&c, g_scene_castle_x, 0, true);   /* the castle, unless it is being dragged */
    if (g_scene_coral_x >= 0 && g_scene_coral_z == DECOR_Z_BACK) draw_coral(&c, g_scene_coral_x, g_scene_coral_rgb, (g_scene_coral_q + 0.5f) / CORAL_Q, true);   /* the coral BEHIND, likewise */
    if (g_scene_cl_x >= 0 && g_scene_cl_z == DECOR_Z_BACK) draw_cluster(&c, g_scene_cl_x, g_scene_cl_scheme, (g_scene_cl_q + 0.5f) / CL_Q, true);   /* the cluster BEHIND */
}

void render_tank(const tank_t *t, uint16_t *fb, int stride) {
    float dim = t->night ? 0.45f : 1.0f;
    ctx_t c = ctx_full(fb, stride, dim);
    int64_t p0 = PROF_MARK();
    bool cached = g_scene && g_dirty && stride == TANK_W;
    if (cached) memset(g_dirty, 0, TANK_H * DIRTY_WORDS_PER_ROW * sizeof(uint32_t));
    struct { short x0, y0, x1, y1; } rects[5 + MAX_FOOD + MAX_BUBBLE + N_FISH_MAX + VEG_BEDS_MAX];
    int nr = 0;
#define DYN_RECT(cx0, cy0, cx1, cy1) do { if (cached && nr < (int)(sizeof rects / sizeof rects[0])) { \
        rects[nr].x0 = (short)(cx0); rects[nr].y0 = (short)(cy0); \
        rects[nr].x1 = (short)(cx1); rects[nr].y1 = (short)(cy1); nr++; } } while (0)

    int ccx, cz; bool placing; castle_state(t, &ccx, &cz, &placing);
    int scene_cx = placing ? -1 : ccx;
    int kx, kz; bool kplacing; coral_state(t, &kx, &kz, &kplacing);
    uint32_t krgb = tank_coral_rgb(t);
    float kg = tank_coral_growth(t);
    int kq = (int)(kg * (CORAL_Q - 0.01f));
    int scene_kx = kplacing ? -1 : kx;
    int lx, lz; bool lplacing; cluster_state(t, &lx, &lz, &lplacing);
    float lg = tank_cluster_growth(t); int lscheme = tank_cluster_scheme(t);
    int lq = (int)((lg > 1 ? 1 : lg) * (CL_Q - 0.01f));
    int scene_lx = lplacing ? -1 : lx;
    g_veg_mask_cx = ccx >= 0 && cz == DECOR_Z_FRONT ? ccx : -1;
    if (cached) {
        if (g_scene_dim != dim || g_scene_ms != t->tank_ms_bits || g_scene_castle_x != scene_cx || g_scene_castle_z != cz
            || g_scene_coral_x != scene_kx || g_scene_coral_z != kz || g_scene_coral_rgb != krgb || g_scene_coral_q != kq
            || g_scene_cl_x != scene_lx || g_scene_cl_z != lz || g_scene_cl_q != lq || g_scene_cl_scheme != lscheme) {
            g_scene_castle_x = scene_cx; g_scene_castle_z = cz;
            g_scene_coral_x = scene_kx; g_scene_coral_z = kz; g_scene_coral_rgb = krgb; g_scene_coral_q = kq;
            g_scene_cl_x = scene_lx; g_scene_cl_z = lz; g_scene_cl_q = lq; g_scene_cl_scheme = lscheme;
            /* rebuild the static scene with the vignette baked in (the
               per-frame pass then only re-darkens dynamic patches). The
               reef's lushness comes from the tank milestones, so a new
               milestone - or a reset back to a bare pair - rebakes too
               (it used to wait for the next day/night change). */
            bake_scene(t, g_scene, dim);
            if (g_vig) g_vig_filled = true;
            g_scene_dim = dim; g_scene_ms = t->tank_ms_bits; g_scene_epoch++;
        }
        if (!(g_primed_fb == fb && g_primed_epoch == g_scene_epoch))
            memcpy(fb, g_scene, TANK_W * TANK_H * sizeof(uint16_t));
        g_primed_fb = NULL;
        if (placing) draw_castle(&c, ccx, 0, true);      /* dragged: drawn live over the castle-less scene */
        if (kplacing && kz == DECOR_Z_BACK) draw_coral(&c, kx, krgb, kg, true);   /* the coral dragged BEHIND: live too */
        if (lplacing && lz == DECOR_Z_BACK) draw_cluster(&c, lx, lscheme, lg, true);
    } else draw_scene(t, fb, stride, dim);
#define CORAL_CROWN() do { int qx0, qy0, qx1, qy1; draw_coral_crown(&c, kx, krgb, kg, t->clock, &qx0, &qy0, &qx1, &qy1); \
        if (qx1 >= qx0) DYN_RECT(qx0, qy0, qx1, qy1); } while (0)
#define CLUSTER_CROWN() do { int qx0, qy0, qx1, qy1; draw_cluster_crown(&c, lx, lscheme, lg, t->clock, &qx0, &qy0, &qx1, &qy1); \
        if (qx1 >= qx0) DYN_RECT(qx0, qy0, qx1, qy1); } while (0)
    if (kx >= 0 && kz == DECOR_Z_BACK) CORAL_CROWN();   /* the crown BEHIND: over the baked fan, under the grass */
    if (lx >= 0 && lz == DECOR_Z_BACK) CLUSTER_CROWN();
    PROF_ADD(0, p0);

    PROF_ADD(1, p0);   /* stage 1 (light shafts) retired 2026-09-01 */
    /* vegetation, BACK layer: the reef bed (left - milestone lushness widens
       its base, never withers) and two decor beds; all three keep growing up
       and out with tank_t.veg_growth until the keeper trims them (slash the
       canopy). Geometry comes from tank_veg_bed so physics and pixels agree.
       The alternating FRONT fronds draw after the fish, below. */
    static const int veg_seed[VEG_BEDS_MAX] = { 0, 7, 3, 5 };
    for (int b = 0; b < tank_veg_beds(t); b++)          /* a BACK-layer piece first: behind the grass too */
        if (bed_z(t, b) == DECOR_Z_BACK) draw_veg(&c, t, b, veg_seed[b], -1, cached);
    for (int b = 0; b < tank_veg_beds(t); b++)
        if (bed_z(t, b) == DECOR_Z_MIDDLE) draw_veg(&c, t, b, veg_seed[b], 0, cached);
        /* no DYN_RECT: with the scene cache each frond span vignettes itself */
    PROF_ADD(2, p0);
    /* the airstone the column rises from, on the floor where the keeper put
       it (setup): three stones and a glint - dynamic, since it can move */
    {
        float ax = t->bubble_x, ay = TANK_H - 17;
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
    PROF_ADD(3, p0);
    /* fish */
    for (int i = 0; i < t->n_fish; i++) {
        g_bb_on = true; g_bb_x0 = g_bb_y0 = 1 << 20; g_bb_x1 = g_bb_y1 = -1;
        draw_fish(&c, t, &t->fish[i], i);
        g_bb_on = false;
        if (g_bb_x1 >= g_bb_x0) DYN_RECT(g_bb_x0, g_bb_y0, g_bb_x1, g_bb_y1);
    }
    /* the snail on the floor (upright): in the scene, under the front fronds */
    if (tank_snail_upright(t)) {
        draw_snail(&c, t, true);
        DYN_RECT((int)t->snail_x - 21, (int)t->snail_y - 13, (int)t->snail_x + 21, (int)t->snail_y + 7);
    }
    /* the castle IN FRONT: its front row back over the fish (and the snail,
       the food, the bubbles), only where they were drawn - a fish in the arch
       swims THROUGH. BEHIND: nothing here; it is a backdrop the fish pass. */
    if (ccx >= 0 && cz == DECOR_Z_FRONT) {
        if (cached) for (int i = 0; i < nr; i++)
            draw_castle_front_rect(&c, ccx, rects[i].x0, rects[i].y0, rects[i].x1, rects[i].y1);
        else draw_castle(&c, ccx, 1, false);
    }
    /* vegetation, FRONT layer: the alternating fronds drawn over the fish,
       so a fish inside a canopy is woven through it (each span applies its
       own vignette and untags itself, so the fish rects below skip it) */
    for (int b = 0; b < tank_veg_beds(t); b++)
        if (bed_z(t, b) == DECOR_Z_MIDDLE) draw_veg(&c, t, b, veg_seed[b], 1, cached);
    for (int b = 0; b < tank_veg_beds(t); b++)          /* a FRONT-layer piece last: over the grass and the fish */
        if (bed_z(t, b) == DECOR_Z_FRONT) draw_veg(&c, t, b, veg_seed[b], -1, cached);
    if (lx >= 0 && lz == DECOR_Z_FRONT) { draw_cluster(&c, lx, lscheme, lg, cached); CLUSTER_CROWN(); }
    if (kx >= 0 && kz == DECOR_Z_FRONT) { draw_coral(&c, kx, krgb, kg, cached); CORAL_CROWN(); }   /* the coral IN FRONT: over everything */
    PROF_ADD(4, p0);
    /* porthole vignette: darken corners toward AMOLED black. With a scene
       cache the full-frame pass is baked into the scene and only the dynamic
       patches are re-darkened; without one, the classic per-pixel pass runs. */
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
    uint16_t *p = &CTX_PX(c, x, y);
    int r = (*p >> 11)       + (((v >> 11)       - (*p >> 11))       * a >> 8);
    int g = ((*p >> 5) & 63) + ((((v >> 5) & 63) - ((*p >> 5) & 63)) * a >> 8);
    int b = (*p & 31)        + (((v & 31)        - (*p & 31))        * a >> 8);
    *p = (uint16_t)((r << 11) | (g << 5) | b);
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

    /* identity row: swatch, stage pips (earned ones lit), certainty dot -
     * bright = the model was sure of its last decision, dim = torn */
    fill_ellipse(&c, X + 14, Y + 14, 6, 6, f->color, 255);
    fill_ellipse(&c, X + 11, Y + 11, 1.6f, 1.6f, 0xffffff, 150);
    for (int i = 0; i < 4; i++) {
        if (i <= (int)f->stage) fill_ellipse(&c, X + 30 + i * 10, Y + 14, 2.8f, 2.8f, f->accent, 255);
        else ring(&c, X + 30 + i * 10, Y + 14, 2.8f, 0x2a3f45);
    }
    fill_ellipse(&c, X + W - 14, Y + 14, 3.2f, 3.2f, 0xffffff, (int)(40 + 200 * f->goal.confidence));
    /* growth: a thin bar under the pips filling toward the next stage (tended
     * time only - fish grow while the tank is lit and lived-in). Elders are
     * done growing, so no bar. */
    if (f->stage < STAGE_ELDER) {
        static const float edge[5] = { 0, STAGE_JUV_AGE, STAGE_ADULT_AGE, STAGE_ELDER_AGE, STAGE_ELDER_AGE };
        float a0 = edge[f->stage], a1 = edge[f->stage + 1];
        float frac = (progression_age_s(t, fish_idx) - a0) / (a1 - a0);
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        const int gx = X + 27, gw = 37;                        /* spans the pip row */
        for (int xx = 0; xx < gw; xx++)
            for (int yy = 0; yy < 2; yy++)
                px_blend(&c, gx + xx, Y + 21 + yy, xx < (int)(gw * frac + 0.5f) ? f->accent : 0x2a3f45,
                         xx < (int)(gw * frac + 0.5f) ? 235 : 150);
    }

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
       middle of the two-column one */
#if RENDER_CARD_COMPACT
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
    bool saw_curious = f->ms_bits & (MS_FIRST_REEF | MS_FIRST_BUBBLES);
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
#define SNAIL_CARD_W UI(336)
#define SNAIL_CARD_H UI(176)
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

void render_stats_card(const tank_t *t, int fish_idx, uint16_t *fb, int stride) {
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
        for (int y = 0; y < RENDER_CARD_H; y++)
            memcpy(fb + (RENDER_CARD_Y + y) * stride + RENDER_CARD_X,
                   g_card + y * RENDER_CARD_W, RENDER_CARD_W * 2);
    } else card_draw(c, t, fish_idx);
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
 * The keeper's "start over" (device: hold BOOT, tap the glass; sim: X). A
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
    ctx_t c = ctx_full(fb, stride, 1.0f); draw_text(&c, x, y, scale, rgb, s);
}
void render_rect(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb) {
    ctx_t c = ctx_full(fb, stride, 1.0f); rect_fill(&c, x, y, w, h, rgb);
}
void render_rect_edge(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb) {
    ctx_t c = ctx_full(fb, stride, 1.0f); rect_edge(&c, x, y, w, h, rgb);
}
void render_rect_blend(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb, int alpha) {
    ctx_t c = ctx_full(fb, stride, 1.0f); src_t s = src_color(rgb, 1.0f);
    for (int yy = y; yy < y + h; yy++) span(&c, x, x + w - 1, yy, &s, alpha);
}
void render_ring(uint16_t *fb, int stride, float cx, float cy, float r, uint32_t rgb) {
    ctx_t c = ctx_full(fb, stride, 1.0f); ring(&c, cx, cy, r, rgb);
}
void render_button(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t fill, uint32_t edge, const char *label, int scale) {
    ctx_t c = ctx_full(fb, stride, 1.0f); button(&c, x, y, w, h, fill, edge, label, scale);
}
void render_fish_preview(uint16_t *fb, int stride, float x, float y, float size,
                         uint32_t body, uint32_t fin, uint32_t accent, float clock) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    fish_t f; memset(&f, 0, sizeof f);
    f.x = x; f.y = y; f.heading = 0; f.size = size; f.speed = 40;
    f.stage = STAGE_ADULT;                       /* grown: the crest shows the fin colour */
    f.hunger = 4; f.stress = 0; f.goal.id = GOAL_EXPLORE;
    f.color = body; f.fin = fin; f.accent = accent;
    draw_fish_core(&c, &f, clock, false, 1.0f, size);
}
void render_fish_portrait(uint16_t *fb, int stride, float x, float y, float size, const fish_t *who, float clock) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    fish_t f; memset(&f, 0, sizeof f);
    f.x = x; f.y = y; f.heading = 0; f.size = size; f.speed = 40;
    f.stage = who->stage;
    f.hunger = 4; f.stress = 0; f.goal.id = GOAL_EXPLORE;
    f.color = who->color; f.fin = who->fin; f.accent = who->accent;
    draw_fish_core(&c, &f, clock, false, 1.0f, size);
}
void render_confirm_reset(uint16_t *fb, int stride, float frac) {
    ctx_t c = ctx_full(fb, stride, 1.0f);              /* ignores night dimming, like the card */
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
#if UI_COMPACT
/* The 2.8" CYD's 240 px: seven rows of 32 px badges do not fit at any pitch,
 * so the badges are the generated 24 px copies (tools/gen_icons.py) at a
 * 28 px row pitch, and the three buttons spread across the whole foot - this
 * glass has no curved bezel to keep clear of. */
#define MSP_ROW_Y0    3
#define MSP_ROW_H     28
#define MSP_TANK_Y    (MSP_ROW_Y0 + N_FISH_MAX * MSP_ROW_H + 7)
#define MSP_BADGE_X0  112
#define MSP_BADGE_DX  30
#define MSP_BADGE_Y   2                 /* the badge's top within its row */
#define MSP_ICON      24
#define MSP_NAME_X    60                /* the name, the strips and the ticks */
#define MSP_PORTRAIT_X 34               /* the fish portraits' centre (and the sand dollar's) */
#define MSP_CLOSE_X   216
#define MSP_CLOSE_Y   (MSP_TANK_Y + MSP_ROW_H + 8)
#define MSP_CLOSE_W   96
#define MSP_CLOSE_H   22
#define MSP_SET_X     8
#define MSP_SET_W     96
#define MSP_SD_X      (MSP_PORTRAIT_X - MSP_ICON / 2)
#define MSP_UPG_X     112
#define MSP_UPG_W     96
#else
#define MSP_ROW_Y0    4
#define MSP_ROW_H     40
#define MSP_TANK_Y    254
#define MSP_BADGE_X0  176
#define MSP_BADGE_DX  40
#define MSP_BADGE_Y   4                 /* the badge's top within its row */
#define MSP_ICON      32
#define MSP_NAME_X    92                /* the name, the strips and the ticks */
#define MSP_PORTRAIT_X 52               /* the fish portraits' centre */
#define MSP_CLOSE_X   324               /* the CLOSE button, bottom right, inside the bezel curve; clear of the brightness row's number */
#define MSP_CLOSE_Y   312
#define MSP_CLOSE_W   92
#define MSP_CLOSE_H   30
#define MSP_SET_X     32                /* the SETTINGS button, bottom left, where the brightness row was */
#define MSP_SET_W     116
#define MSP_SD_X      36                /* the sand dollar on the TANK row (the shop), centred like the fish portraits */
#define MSP_UPG_X     178               /* the UPGRADES button, centred between SETTINGS and CLOSE: the shop too (Strato, 2026-09-15) */
#define MSP_UPG_W     116
#endif
_Static_assert(MSP_CLOSE_Y + MSP_CLOSE_H <= TANK_H, "the milestones page's buttons must fit on the glass");
#define MSP_MODAL_X   UI(56)
#define MSP_MODAL_Y   UI(100)
#define MSP_MODAL_W   UI(336)
#define MSP_MODAL_H   UI(156)
#define MSP_INK       0x031015
#define MSP_DIM       0x2a3f45
#define MSP_TEAL      0x9fd8e2

typedef struct { uint32_t bit; const icon_t *icon; } badge_t;
static const badge_t FISH_BADGES[6] = {
    { MS_FIRST_MEAL_FROM_YOU, &icon_ms_first_meal },  { MS_FIRST_HOLD_APPROACH, &icon_ms_first_hold_approach },
    { MS_FIRST_REEF, &icon_ms_first_reef },           { MS_FIRST_BUBBLES, &icon_ms_first_bubbles },
    { MS_FIRST_FOLLOW, &icon_ms_first_follow },       { MS_FIRST_DART, &icon_ms_first_dart },
};
static const badge_t TANK_BADGES[6] = {
    { TMS_FIRST_FEEDING, &icon_ms_first_feeding },    { TMS_FIRST_TRIM, &icon_ms_first_trimming },
    { TMS_FIRST_CLEANING, &icon_ms_first_glass_cleaning }, { TMS_FIRST_FULL_NIGHT, &icon_ms_first_quiet_night },
    { TMS_FIRST_PLAY_SESSION, &icon_ms_first_play_session }, { TMS_CHANGED_SOMEONE, &icon_ms_tank_changed_someone },
};
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
#define MSP_ARROW_W   UI(40)             /* the arrow buttons, inset at the modal's top corners */
#define MSP_ARROW_H   UI(32)
#define MSP_ARROW_IN  UI(10)
#define MSP_ARROW_HIT UI(100)            /* the hit box: the corner's whole width in from each side, 12 above, 24 below
                                        (a miss closes the modal, so the box is wide) */
/* a gate's modal is taller (two sentence lines, progress, the HOW? button)
   so it sits higher than the badge modal, clear of the CLOSE button */
#define MSP_FRY_MODAL_Y UI(60)
#define MSP_HOW_W UI(100)                 /* CLOSE-sized (Strato hit the 76 x 26 one a third of the time) */
#define MSP_HOW_H UI(32)
/* its hit box: wide and deep. Fingers on this panel land low and wide of
   where they feel, and a miss here costs the modal (any other tap closes
   it), so the box runs 36 px past each side, 12 above and 28 below - the
   whole foot of the panel, down to its edge. */
#define MSP_HOW_SLOP_X UI(36)
#define MSP_HOW_SLOP_UP UI(12)
#define MSP_HOW_SLOP_DN UI(28)

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
/* the page's art for a badge: the 24 px copy on a compact tank (see
   MSP_ICON), the art itself otherwise. The detail modal keeps the full size. */
static const icon_t *badge_art(const icon_t *ic) {
#if UI_COMPACT
    static const struct { const icon_t *full, *small; } SMALL[] = {
        { &icon_ms_first_bubbles, &icon_ms_first_bubbles_24 },     { &icon_ms_first_dart, &icon_ms_first_dart_24 },
        { &icon_ms_first_feeding, &icon_ms_first_feeding_24 },     { &icon_ms_first_follow, &icon_ms_first_follow_24 },
        { &icon_ms_first_glass_cleaning, &icon_ms_first_glass_cleaning_24 },
        { &icon_ms_first_hold_approach, &icon_ms_first_hold_approach_24 },
        { &icon_ms_first_meal, &icon_ms_first_meal_24 },           { &icon_ms_first_play_session, &icon_ms_first_play_session_24 },
        { &icon_ms_first_quiet_night, &icon_ms_first_quiet_night_24 }, { &icon_ms_first_reef, &icon_ms_first_reef_24 },
        { &icon_ms_first_trimming, &icon_ms_first_trimming_24 },   { &icon_ms_sand_dollar, &icon_ms_sand_dollar_24 },
        { &icon_ms_tank_changed_someone, &icon_ms_tank_changed_someone_24 },
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
    if (g_ms_tankrow) { *idx = g_ms_k; return g_ms_k < 0 ? 1 : 6; }
    if (g_ms_k < 0)   { *idx = g_ms_row; return t->n_fish; }    /* a fish's name: the fish */
    *idx = g_ms_k; return 6;
}
static int ms_open(const tank_t *t, int row, bool tank_row, bool fry_row, int k,
                   const fry_req_t *req, int nreq, bool staged);
/* the arrows: the previous / next thing in the modal's group, wrapping */
static void ms_step(const tank_t *t, int dir) {
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    int idx, n = ms_group(t, nreq, &idx);
    if (n < 2) return;
    idx = (idx + dir + n) % n;
    if (!g_ms_fryrow && !g_ms_tankrow && g_ms_k < 0) ms_open(t, idx, false, false, -1, req, nreq, staged);
    else ms_open(t, g_ms_row, g_ms_tankrow, g_ms_fryrow, idx, req, nreq, staged);
}

void render_milestones(const tank_t *t, uint16_t *fb, int stride) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    for (int y = 0; y < TANK_H; y++)
        for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = rgb565(MSP_INK, 1);
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i];
        int top = MSP_ROW_Y0 + i * MSP_ROW_H;
        render_fish_preview(fb, stride, MSP_PORTRAIT_X, top + UI(20), f->size * UI_F, f->color, f->fin, f->accent, t->clock);
        draw_text(&c, MSP_NAME_X, top + UI(2), UI_TEXT(2), 0xffffff, f->name);
        static const int GX[4] = { UI(4), UI(19), UI(37), UI(59) };   /* each glyph's own pitch: a 4 px gap as they grow */
        for (int s = 0; s < 4; s++)          /* growth strip: fry -> elder, lit up to the stage reached */
            fish_glyph(&c, MSP_NAME_X + GX[s], top + UI(30), (2.2f + s * 0.9f) * UI_F, s <= (int)f->stage ? f->accent : MSP_DIM);
        for (int k = 0; k < 6; k++) {
            uint32_t bit = FISH_BADGES[k].bit;
            badge(&c, MSP_BADGE_X0 + k * MSP_BADGE_DX, top + MSP_BADGE_Y, FISH_BADGES[k].icon,
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
        render_fish_preview(fb, stride, MSP_PORTRAIT_X, top + UI(20), 0.55f * UI_F, fry_rgb, fry_rgb, fry_rgb, t->clock);
        draw_text(&c, MSP_NAME_X, top + UI(2), UI_TEXT(2), MSP_TEAL, "NEW FRY");
        for (int k = 0; k < nreq; k++)           /* one tick per gate, lit when met */
            rect_fill(&c, MSP_NAME_X + UI(4) + k * UI(14), top + UI(27), UI(10), UI(6), req[k].met ? MSP_TEAL : MSP_DIM);
        for (int k = 0; k < nreq; k++) {
            int x = MSP_BADGE_X0 + k * MSP_BADGE_DX;
            fry_badge(&c, fb, stride, t, x, top + MSP_BADGE_Y, &req[k]);
            if (!req[k].met) {                   /* the bar: how far along */
                int w = (int)(MSP_ICON * req[k].frac + 0.5f), by = top + MSP_BADGE_Y + MSP_ICON + 2;
                rect_fill(&c, x, by, MSP_ICON, 2, MSP_DIM);
                if (w > 0) rect_fill(&c, x, by, w, 2, MSP_TEAL);
            }
        }
    }
    /* the tank's row: the sand dollar at the left (the fish rows' portrait
       slot) opens the shop (2026-09-15) */
    for (int x = UI(24); x < TANK_W - UI(24); x++) px_blend(&c, x, MSP_TANK_Y - UI(4), MSP_DIM, 200);
    blit_icon(&c, MSP_SD_X, MSP_TANK_Y, badge_art(&icon_ms_sand_dollar), 255);
    {   /* the balance under the coin (Strato, 2026-09-15), centred on it - the
           only room: the divider and the last row sit above, 12 px to the left */
        char bal[16]; snprintf(bal, sizeof bal, "%d", (int)t->sd_balance);
        draw_text(&c, MSP_SD_X + (MSP_ICON - text_w(bal, UI_TEXT(2))) / 2, MSP_TANK_Y + MSP_ICON + 2, UI_TEXT(2), 0xffffff, bal);
    }
    draw_text(&c, MSP_NAME_X, MSP_TANK_Y + UI(2), UI_TEXT(2), MSP_TEAL, "TANK");
    for (int k = 0; k < POP_CAP; k++)        /* population strip: who is here, who could still arrive */
        fish_glyph(&c, MSP_NAME_X + UI(4) + k * UI(14), MSP_TANK_Y + UI(30), 2.8f * UI_F, k < t->n_fish ? MSP_TEAL : MSP_DIM);
    for (int k = 0; k < 6; k++) {
        uint32_t bit = TANK_BADGES[k].bit;
        badge(&c, MSP_BADGE_X0 + k * MSP_BADGE_DX, MSP_TANK_Y + MSP_BADGE_Y, TANK_BADGES[k].icon,
              (t->tank_ms_bits & bit) != 0, (t->tank_ms_seen & bit) == 0);
    }
    /* the way out: a CLOSE button in the prompt's calm dress (a tap anywhere
       else never drops the page - too much to tap for that) */
    button(&c, MSP_CLOSE_X, MSP_CLOSE_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", UI_TEXT(2));
    button(&c, MSP_SET_X, MSP_CLOSE_Y, MSP_SET_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "SETTINGS", UI_TEXT(2));   /* bottom left (2026-09-15) */
    button(&c, MSP_UPG_X, MSP_CLOSE_Y, MSP_UPG_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "UPGRADES", UI_TEXT(2));   /* the shop, between them */
    /* a modal up: the page under it is out of reach (any tap only closes the
       modal), so it LOOKS out of reach - every pixel at half (Strato: with
       CLOSE lit it looked like you could still tap it). One shift per
       pixel, so cheap enough for every frame. */
    if (g_ms_caption[0])
        for (int y = 0; y < TANK_H; y++)
            for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = (uint16_t)((fb[y * stride + x] >> 1) & 0x7bef);
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
        const int Y = g_ms_kind >= 0 ? MSP_FRY_MODAL_Y : MSP_MODAL_Y;
        const int H = MSP_MODAL_H + (g_ms_caption2[0] ? UI(20) : 0) + (g_ms_sub[0] ? UI(24) : 0) + (g_ms_kind >= 0 ? MSP_HOW_H + UI(14) : 0);
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        if (g_ms_kind >= 0)                        /* the way further in: HOW?, centred at the foot (Strato: bottom
                                                      right sat too close to CLOSE for comfort) */
            button(&c, X + (W - MSP_HOW_W) / 2, Y + H - UI(10) - MSP_HOW_H, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "HOW?", UI_TEXT(2));
        if (g_ms_icon) blit_icon_scaled(&c, X + (W - g_ms_icon->w * UI_TEXT(2)) / 2, Y + UI(16) + (UI(32) - g_ms_icon->w * UI_TEXT(2) / 2), g_ms_icon, UI_TEXT(2), g_ms_lit);
        else if (g_ms_fish >= 0 && g_ms_fish < t->n_fish) {
            const fish_t *f = &t->fish[g_ms_fish];
            if (g_ms_lit) render_fish_preview(fb, stride, X + W / 2, Y + UI(48), f->size * 1.6f * UI_F, f->color, f->fin, f->accent, t->clock);
            else          render_fish_preview(fb, stride, X + W / 2, Y + UI(48), f->size * 1.6f * UI_F, MSP_DIM, MSP_DIM, MSP_DIM, t->clock);
        } else if (g_ms_fry)
            render_fish_preview(fb, stride, X + W / 2, Y + UI(48), 0.9f * UI_F, g_ms_lit ? MSP_TEAL : MSP_DIM, g_ms_lit ? MSP_TEAL : MSP_DIM, g_ms_lit ? MSP_TEAL : MSP_DIM, t->clock);
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
    if (g_ms_caption[0]) {                       /* a modal is up */
        if (!g_ms_tip) {                         /* the arrows at its top corners: the previous / next of the group */
            fry_req_t req[FRY_REQ_MAX]; bool staged; int idx;
            int n = ms_group(t, progression_next_fry(t, req, &staged), &idx);
            const int Y = g_ms_kind >= 0 ? MSP_FRY_MODAL_Y : MSP_MODAL_Y;
            if (n > 1 && y >= Y - UI(12) && y < Y + MSP_ARROW_IN + MSP_ARROW_H + UI(24)) {
                if (x < MSP_MODAL_X + MSP_ARROW_HIT)               { ms_step(t, -1); return MS_TAP_KEPT; }
                if (x >= MSP_MODAL_X + MSP_MODAL_W - MSP_ARROW_HIT) { ms_step(t, +1); return MS_TAP_KEPT; }
            }
        }
        if (g_ms_kind >= 0 && !g_ms_tip) {       /* a gate's: the HOW? button opens its tip page */
            const int H = MSP_MODAL_H + (g_ms_caption2[0] ? UI(20) : 0) + (g_ms_sub[0] ? UI(24) : 0) + MSP_HOW_H + UI(14);
            const int bx = MSP_MODAL_X + (MSP_MODAL_W - MSP_HOW_W) / 2, by = MSP_FRY_MODAL_Y + H - UI(10) - MSP_HOW_H;
            if (x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X && y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN) {
                g_ms_tip = true; return MS_TAP_KEPT; }
        }
        render_milestones_leave(); return MS_TAP_KEPT;   /* any other tap: back to the page */
    }
    if (x >= MSP_CLOSE_X - UI(8) && y >= MSP_CLOSE_Y - UI(4)) return MS_TAP_CLOSE;      /* slop out to the glass edge */
    if (x < MSP_SET_X + MSP_SET_W + UI(8) && y >= MSP_CLOSE_Y - UI(4)) return MS_TAP_SETTINGS;   /* the settings page */
    if (y >= MSP_CLOSE_Y - UI(4)) return MS_TAP_SHOP;                                            /* UPGRADES: the rest of the strip is the shop */
    int row = -1; bool tank_row = false, fry_row = false;
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    if (y >= MSP_ROW_Y0 - UI(2) && y < MSP_ROW_Y0 + N_FISH_MAX * MSP_ROW_H) {
        row = (int)((y - MSP_ROW_Y0) / MSP_ROW_H);
        if (row < 0) row = 0;
        if (row == t->n_fish && nreq > 0) fry_row = true;   /* the NEW FRY row */
        else if (row >= t->n_fish) return MS_TAP_NONE;      /* an empty row */
    } else if (y >= MSP_TANK_Y - UI(6) && y < MSP_CLOSE_Y - UI(4)) tank_row = true;   /* down to the button strip: fingers near the
                                                                              bottom bezel report LOW */
    else return MS_TAP_NONE;
    int k;                                           /* badge column, or -1 for the name / strip cluster */
    if (x >= MSP_BADGE_X0 - UI(4) && x < MSP_BADGE_X0 + 6 * MSP_BADGE_DX) {
        k = (int)((x - MSP_BADGE_X0 + UI(4)) / MSP_BADGE_DX);
        if (k > 5) k = 5;
    } else if (x >= UI(20) && x < MSP_BADGE_X0 - UI(4)) k = -1;
    else return MS_TAP_NONE;
    if (tank_row && x < MSP_NAME_X - UI(4)) return MS_TAP_SHOP;      /* the sand dollar: the shop page */
    return ms_open(t, row, tank_row, fry_row, k, req, nreq, staged);
}
/* open the detail modal for a row's name / strip (k < 0) or its k-th badge
   or gate: the words, the art, and where it came from (the arrows' group) */
static int ms_open(const tank_t *t, int row, bool tank_row, bool fry_row, int k,
                   const fry_req_t *req, int nreq, bool staged) {
    g_ms_caption2[0] = 0; g_ms_sub[0] = 0; g_ms_fry = false; g_ms_kind = -1; g_ms_tip = false;
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
            uint32_t bit = TANK_BADGES[k].bit; bool on = (t->tank_ms_bits & bit) != 0;
            snprintf(g_ms_title, sizeof g_ms_title, on ? "TANK" : "NOT YET");
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", TMS_NAMES[bit_index(bit)]);
            g_ms_lit = on; g_ms_icon = TANK_BADGES[k].icon; g_ms_fish = -1;
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
void render_milestones_leave(void) { g_ms_kind = -1; g_ms_tip = false; g_ms_caption[0] = 0; g_ms_caption2[0] = 0; g_ms_title[0] = 0; g_ms_sub[0] = 0; g_ms_icon = NULL; g_ms_fish = -1; g_ms_fry = false;
                                     g_ms_row = -1; g_ms_k = -1; g_ms_tankrow = false; g_ms_fryrow = false; }

/* ---- announcement modal (notice.h, 2026-09-15) ----
 * The milestones page's detail modal, over the live tank: the badge art at
 * 2x (a fish's own sprite for a stage), the name, the caption, and a thin
 * bar along the foot that runs out with the notice's time. */
void render_notice(const tank_t *t, uint16_t *fb, int stride, int kind, int fish, uint32_t bit, float frac_left) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    const int X = MSP_MODAL_X, W = MSP_MODAL_W, Y = MSP_MODAL_Y, H = MSP_MODAL_H;
    rect_fill(&c, X, Y, W, H, 0x04141a);
    rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    char title[FISH_NAME_MAX + 8] = "THE TANK", caption[40] = "";
    const icon_t *ic = NULL;
    const fish_t *f = fish >= 0 && fish < t->n_fish ? &t->fish[fish] : NULL;
    if (kind == 3) {                                          /* NOTICE_LOW_BATTERY */
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
        for (int k = 0; k < 6; k++) if (TANK_BADGES[k].bit == bit) ic = TANK_BADGES[k].icon;
        int bi = 0; while (bi < 31 && !(bit & (1u << bi))) bi++;
        snprintf(caption, sizeof caption, "%s", bi < TMS_COUNT ? TMS_NAMES[bi] : "");
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
    draw_text(&c, X + (W - text_w(title, UI_TEXT(3))) / 2, Y + UI(92), UI_TEXT(3), 0xffffff, title);
    draw_text(&c, X + (W - text_w(caption, UI_TEXT(2))) / 2, Y + UI(124), UI_TEXT(2), MSP_TEAL, caption);
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
#if UI_COMPACT
/* The CYD's 240 px: the 32 px coin for the 64 px one (the header is a row
 * shorter), rows at a 38 px pitch, the buttons from the glass's own edge. */
#define SHP_COIN_ART  (&icon_ms_sand_dollar)
#define SHP_COIN_X    8
#define SHP_COIN_Y    6
#define SHP_TEXT_X    50             /* each row's name and price, under the header's words */
#define SHP_ROW_Y0    56
#define SHP_ROW_DY    38
#define SHP_ROW_ICON  32
#define SHP_BTN_W     76
#define SHP_BTN_X     (TANK_W - 8 - SHP_BTN_W)
#define SHP_BTN_H     24
#define SHP_EARN_X    8
#define SHP_EARN_W    96
#else
#define SHP_COIN_ART  (&icon_shop_sand_dollar_64)
#define SHP_COIN_X    32
#define SHP_COIN_Y    10
#define SHP_TEXT_X    76             /* each row's name and price (the header's words sit at 112) */
#define SHP_ROW_Y0    98
#define SHP_ROW_DY    56
#define SHP_ROW_ICON  32
#define SHP_BTN_X     300
#define SHP_BTN_W     116
#define SHP_BTN_H     32
#define SHP_EARN_X    32
#define SHP_EARN_W    150
#endif
#define SHP_BTN_DY    ((SHP_ROW_ICON - SHP_BTN_H) / 2)   /* the row's button, centred on its icon */
#define SHP_MODAL_X   UI(48)         /* wider than the milestones modal (56 / 336): an item's second line runs to 28 chars = 334 px */
#define SHP_MODAL_W   UI(352)
#define SHP_MODAL_Y   UI(48)
#define SHP_MODAL_H   UI(244)
#define SHP_EARN_MODAL_Y UI(40)
/* the caption under the rows sits SHP_BTN_H + 12 under the last row (the third row, the castle, 2026-09-16: it used to be fixed at 224 / 244 and the castle's row ran into it) */
#define SHP_EARN_MODAL_H UI(224)
static int  g_shp_modal = -1;        /* the item whose modal is up, or -1 */
static bool g_shp_earn;              /* the HOW TO EARN modal is up */
static bool g_shp_sell_armed;        /* SELL tapped once: the next tap on it sells */
#define SHP_TWO_GAP UI(16)           /* MOVE and SELL side by side in the modal */
#define SHP_TWO_X0  (SHP_MODAL_X + (SHP_MODAL_W - 2 * MSP_HOW_W - SHP_TWO_GAP) / 2)
#define SHP_TWO_X1  (SHP_TWO_X0 + MSP_HOW_W + SHP_TWO_GAP)
static const icon_t *shop_icon(int item) { return item == 0 ? &icon_shop_plant : item == 1 ? &icon_shop_snail : item == 2 ? &icon_shop_castle : item == 3 ? &icon_shop_coral : &icon_shop_cluster; }
/* pages (2026-09-23, the fourth item): SHP_PER_PAGE rows fit between the
 * coin and the foot buttons once the filler caption went (HOW TO EARN says
 * the same). With more items than a page holds, arrows at the header's
 * right flip through the pages; with one page nothing shows. */
#define SHP_PER_PAGE 4
#define SHP_PAGES    ((SD_ITEM_COUNT + SHP_PER_PAGE - 1) / SHP_PER_PAGE)
#define SHP_ARROW_W  (UI(36) < 30 ? 30 : UI(36))
#define SHP_ARROW_H  (UI(32) < 24 ? 24 : UI(32))
#define SHP_ARROW_Y  (SHP_COIN_Y + UI(16))
#define SHP_ARROW_X1 (TANK_W - UI(28) - (SHP_ARROW_W))        /* next */
#define SHP_ARROW_X0 (SHP_ARROW_X1 - (SHP_ARROW_W) - UI(8))   /* previous */
static int g_shp_page;
static void shop_arrow(ctx_t *c, int x, int y, bool right, uint32_t rgb) {   /* a chevron in a button */
    button(c, x, y, (SHP_ARROW_W), SHP_ARROW_H, 0x1c2f36, rgb, "", UI_TEXT(2));
    int cx = x + (SHP_ARROW_W) / 2, cy = y + SHP_ARROW_H / 2;
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
void render_shop(const tank_t *t, uint16_t *fb, int stride) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    const int hx = SHP_COIN_X + SHP_COIN_ART->w + UI(16);          /* the header's words, right of the coin */
    blit_icon(&c, SHP_COIN_X, SHP_COIN_Y, SHP_COIN_ART, 255);
    draw_text(&c, hx, SHP_COIN_Y + UI(6), UI_TEXT(2), MSP_TEAL, "SAND DOLLARS");
    char bal[16]; snprintf(bal, sizeof bal, "%d", (int)t->sd_balance);
    draw_text(&c, hx, SHP_COIN_Y + UI(28), UI_TEXT(4), 0xffffff, bal);
    for (int x = UI(24); x < TANK_W - UI(24); x++) px_blend(&c, x, SHP_ROW_Y0 - UI(10), MSP_DIM, 200);
    if (SHP_PAGES > 1) {
        shop_arrow(&c, SHP_ARROW_X0, SHP_ARROW_Y, false, g_shp_page > 0 ? MSP_TEAL : MSP_DIM);
        shop_arrow(&c, SHP_ARROW_X1, SHP_ARROW_Y, true, g_shp_page < SHP_PAGES - 1 ? MSP_TEAL : MSP_DIM);
    }
    for (int i = g_shp_page * SHP_PER_PAGE; i < SD_ITEM_COUNT && i < (g_shp_page + 1) * SHP_PER_PAGE; i++) {
        const sd_item_t *it = &SD_ITEMS[i];
        int top = SHP_ROW_Y0 + (i - g_shp_page * SHP_PER_PAGE) * SHP_ROW_DY;
        bool owned = (t->sd_unlocks & it->bit) != 0, can = t->sd_balance >= it->price;
        if (owned) blit_icon(&c, SHP_COIN_X, top, shop_icon(i), 255); else blit_icon_locked(&c, SHP_COIN_X, top, shop_icon(i));
        const int by = top + SHP_BTN_DY;
        draw_text(&c, SHP_TEXT_X, top + UI(2), UI_TEXT(2), 0xffffff, it->name);
        if (owned) draw_text(&c, SHP_TEXT_X, top + UI(20), UI_TEXT(2), MSP_TEAL, "IN THE TANK");
        else price_tag(&c, SHP_TEXT_X, top + UI(20), it->price, can ? MSP_TEAL : MSP_DIM);
        if (owned)     button(&c, SHP_BTN_X, by, SHP_BTN_W, SHP_BTN_H, MSP_INK, MSP_DIM, "IN TANK", UI_TEXT(2));
        else if (can) { button(&c, SHP_BTN_X, by, SHP_BTN_W, SHP_BTN_H, MSP_TEAL, MSP_TEAL, "UNLOCK", UI_TEXT(2));
                        draw_text(&c, SHP_BTN_X + (SHP_BTN_W - text_w("UNLOCK", UI_TEXT(2))) / 2, by + (SHP_BTN_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_INK, "UNLOCK"); }
        else           button(&c, SHP_BTN_X, by, SHP_BTN_W, SHP_BTN_H, 0x1c2f36, MSP_DIM, "UNLOCK", UI_TEXT(2));
    }
    button(&c, SHP_EARN_X, MSP_CLOSE_Y, SHP_EARN_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "HOW TO EARN", UI_TEXT(2));
    button(&c, MSP_CLOSE_X, MSP_CLOSE_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", UI_TEXT(2));
    if (g_shp_modal < 0 && !g_shp_earn) return;
    for (int y = 0; y < TANK_H; y++)                          /* the page out of reach under a modal */
        for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = (uint16_t)((fb[y * stride + x] >> 1) & 0x7bef);
    const int X = SHP_MODAL_X, W = SHP_MODAL_W;
    if (g_shp_earn) {
        const int Y = SHP_EARN_MODAL_Y, H = SHP_EARN_MODAL_H;
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        draw_text(&c, X + (W - text_w("HOW TO EARN", UI_TEXT(3))) / 2, Y + UI(14), UI_TEXT(3), 0xffffff, "HOW TO EARN");
        const char *const *lines = progression_sd_earn_lines();
        for (int i = 0; lines[i]; i++) draw_text(&c, X + UI(14), Y + UI(50) + i * UI(24), UI_TEXT(2), MSP_TEAL, lines[i]);
        draw_text(&c, X + (W - text_w("TAP TO CLOSE", UI_TEXT(2))) / 2, Y + H - UI(24), UI_TEXT(2), 0x3f6a72, "TAP TO CLOSE");
        return;
    }
    const sd_item_t *it = &SD_ITEMS[g_shp_modal];
    const int Y = SHP_MODAL_Y, H = SHP_MODAL_H;
    bool owned = (t->sd_unlocks & it->bit) != 0, can = t->sd_balance >= it->price;
    rect_fill(&c, X, Y, W, H, 0x04141a);
    rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    const icon_t *art = shop_icon(g_shp_modal);
    blit_icon_scaled(&c, X + (W - art->w * UI_TEXT(2)) / 2, Y + UI(14), art, UI_TEXT(2), true);
    draw_text(&c, X + (W - text_w(it->name, UI_TEXT(3))) / 2, Y + UI(88), UI_TEXT(3), 0xffffff, it->name);
    draw_text(&c, X + (W - text_w(it->words, UI_TEXT(2))) / 2, Y + UI(118), UI_TEXT(2), MSP_TEAL, it->words);
    draw_text(&c, X + (W - text_w(it->words2, UI_TEXT(2))) / 2, Y + UI(138), UI_TEXT(2), MSP_TEAL, it->words2);
    const int bx = X + (W - MSP_HOW_W) / 2, by = Y + H - UI(12) - MSP_HOW_H;
    if (owned && tank_decor_placeable(g_shp_modal)) {         /* a placeable piece: MOVE re-opens the placement page, SELL (twice) sells it back */
        char sell[24]; snprintf(sell, sizeof sell, "SELLS BACK FOR %d", progression_sell_value(g_shp_modal));
        draw_text(&c, X + (W - text_w("IN THE TANK", UI_TEXT(2))) / 2, Y + UI(164), UI_TEXT(2), MSP_TEAL, "IN THE TANK");
        draw_text(&c, X + (W - text_w(sell, UI_TEXT(2))) / 2, Y + UI(184), UI_TEXT(2), MSP_DIM, sell);
        button(&c, SHP_TWO_X0, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "MOVE", UI_TEXT(2));
        if (g_shp_sell_armed) { snprintf(sell, sizeof sell, "+%d OK?", progression_sell_value(g_shp_modal));
                                button(&c, SHP_TWO_X1, by, MSP_HOW_W, MSP_HOW_H, MSP_TEAL, MSP_TEAL, sell, UI_TEXT(2));
                                draw_text(&c, SHP_TWO_X1 + (MSP_HOW_W - text_w(sell, UI_TEXT(2))) / 2, by + (MSP_HOW_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_INK, sell); }
        else button(&c, SHP_TWO_X1, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "SELL", UI_TEXT(2));
    } else if (owned) draw_text(&c, X + (W - text_w("IN THE TANK", UI_TEXT(2))) / 2, by + UI(8), UI_TEXT(2), MSP_TEAL, "IN THE TANK");   /* the snail: a permanent resident */
    else {
        char line[32]; snprintf(line, sizeof line, "%d", it->price);
        int pw = 16 + UI(4) + text_w(line, UI_TEXT(2));
        price_tag(&c, X + (W - pw) / 2, Y + UI(164), it->price, can ? 0xffffff : MSP_DIM);
        if (!can) { snprintf(line, sizeof line, "YOU HAVE %d", (int)t->sd_balance);
                    draw_text(&c, X + (W - text_w(line, UI_TEXT(2))) / 2, Y + UI(184), UI_TEXT(2), MSP_DIM, line); }
        if (can) { button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, MSP_TEAL, MSP_TEAL, "UNLOCK", UI_TEXT(2));
                   draw_text(&c, bx + (MSP_HOW_W - text_w("UNLOCK", UI_TEXT(2))) / 2, by + (MSP_HOW_H - 7 * UI_TEXT(2)) / 2, UI_TEXT(2), MSP_INK, "UNLOCK"); }
        else button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "UNLOCK", UI_TEXT(2));
    }
}
int render_shop_tap(const tank_t *t, float x, float y) {
    if (g_shp_earn) { g_shp_earn = false; return SHOP_TAP_KEPT; }
    if (g_shp_modal >= 0) {
        int item = g_shp_modal; const sd_item_t *it = &SD_ITEMS[item];
        bool owned = (t->sd_unlocks & it->bit) != 0, can = t->sd_balance >= it->price;
        const int bx = SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2, by = SHP_MODAL_Y + SHP_MODAL_H - UI(12) - MSP_HOW_H;
        bool row = y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN;
        bool on_btn = row && x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X;
        if (owned && tank_decor_placeable(item)) {                /* two buttons: MOVE, and SELL armed then confirmed */
            bool on_move = row && x >= SHP_TWO_X0 - MSP_HOW_SLOP_X && x < SHP_TWO_X0 + MSP_HOW_W + SHP_TWO_GAP / 2;
            bool on_sell = row && x >= SHP_TWO_X1 - SHP_TWO_GAP / 2 && x < SHP_TWO_X1 + MSP_HOW_W + MSP_HOW_SLOP_X;
            if (on_sell && !g_shp_sell_armed) { g_shp_sell_armed = true; return SHOP_TAP_KEPT; }
            g_shp_sell_armed = false; g_shp_modal = -1;
            if (on_sell) return SHOP_TAP_SELL + item;
            if (on_move) return SHOP_TAP_MOVE + item;
            return SHOP_TAP_KEPT;
        }
        g_shp_modal = -1; g_shp_sell_armed = false;
        if (on_btn && !owned && can) return SHOP_TAP_BUY + item;
        return SHOP_TAP_KEPT;
    }
    if (x >= MSP_CLOSE_X - UI(8) && y >= MSP_CLOSE_Y - UI(4)) return SHOP_TAP_CLOSE;
    if (x < SHP_EARN_X + SHP_EARN_W + UI(8) && y >= MSP_CLOSE_Y - UI(4)) { g_shp_earn = true; return SHOP_TAP_KEPT; }
    if (SHP_PAGES > 1 && y < SHP_ROW_Y0 - UI(10) && x >= SHP_ARROW_X0 - UI(8)) {   /* the page arrows, in the header band */
        if (x < SHP_ARROW_X1 - UI(4)) { if (g_shp_page > 0) g_shp_page--; }
        else if (g_shp_page < SHP_PAGES - 1) g_shp_page++;
        return SHOP_TAP_KEPT;
    }
    for (int i = g_shp_page * SHP_PER_PAGE; i < SD_ITEM_COUNT && i < (g_shp_page + 1) * SHP_PER_PAGE; i++) {
        int top = SHP_ROW_Y0 + (i - g_shp_page * SHP_PER_PAGE) * SHP_ROW_DY;
        if (x >= UI(20) && y >= top - UI(8) && y < top + SHP_ROW_DY - UI(8)) { g_shp_modal = i; return SHOP_TAP_KEPT; }
    }
    return SHOP_TAP_NONE;
}
void render_shop_leave(void) { g_shp_modal = -1; g_shp_earn = false; g_shp_page = 0; g_shp_sell_armed = false; }

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
    const int W = tx + text_w(txt, UI_TEXT(2)) + UI(5), H = 16 + 2 * UI(3), X = (TANK_W - W) / 2, Y = UI(8);
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
 * land low near the bezel, as on the milestones page), and since the light
 * became the idle detector's (the same evening) a LIGHTS OUT row - ON / OFF
 * (MANUAL, the default = the double-tap on the glass; AUTO = the idle rule,
 * the keeper's opt-in) with the idle time under it as one number: swipe it
 * up or down, or tap the chevrons; the default is LIGHT_IDLE_S. */
/* A SCREEN row (UPRIGHT / FLIPPED) on a board with no IMU to turn the
 * picture by itself - the CYD, the only compact board (render_settings_set_flip). */
#define SET_HAS_FLIP  UI_COMPACT
#if UI_COMPACT
#define SET_TITLE_Y   6
#define SET_ROW1_Y    34             /* BRIGHTNESS */
#define SET_ROW2_Y    62             /* VOLUME */
#define SET_NOTE_Y    83             /* "FISH ARE QUIET AT NIGHT" */
#define SET_ROW4_Y    104            /* SCREEN */
#define SET_ROW3_Y    132            /* LIGHTS OUT */
#define SET_LABEL_X   8
#define SET_SEG_X     118            /* first segment */
#define SET_SEG_W     62
#define SET_SEG_DX    66
#define SET_SEG_H     24
#define SET_SEG_Y(row) ((row) - 8)   /* the segment sits on the label's line */
#else
#define SET_TITLE_Y   14
#define SET_ROW1_Y    58             /* BRIGHTNESS */
#define SET_ROW2_Y    108            /* VOLUME */
#define SET_NOTE_Y    146            /* "FISH ARE QUIET AT NIGHT" */
#define SET_ROW3_Y    176            /* LIGHTS OUT */
#define SET_LABEL_X   32
#define SET_SEG_X     190            /* first segment */
#define SET_SEG_W     76
#define SET_SEG_DX    82
#define SET_SEG_H     40
#define SET_SEG_Y(row) ((row) - 10)  /* the segment sits on the label's line */
#endif
/* the seconds selector: ONE number in the 4x font (28 px tall), chevrons
 * above and below; a swipe up or down anywhere on it steps the whole value
 * (2026-09-15 evening, Strato: "18, 17 ... 10, then 9 - not 19"; the first
 * cut was three letter-wheel digits). Clamped live to LIGHT_IDLE_MIN_S ..
 * LIGHT_IDLE_MAX_S. Sits well clear of the LIGHTS OUT segments: a finger
 * aiming at the up chevron used to land on MANUAL. */
#define SET_NUM_SCALE UI_TEXT(4)
#define SET_NUM_H     (7 * SET_NUM_SCALE)
#define SET_NUM_X     SET_SEG_X       /* the number's left edge (right-aligned in a 3-digit box) */
#define SET_NUM_BOX_W (3 * 6 * SET_NUM_SCALE - SET_NUM_SCALE)
#if UI_COMPACT
#define SET_NUM_Y     176
#define SET_NUM_GAP   16              /* chevron tip to the number */
#else
#define SET_NUM_Y     266
#define SET_NUM_GAP   30              /* chevron tip to the number */
#endif
#define SET_AFTER_Y   (SET_NUM_Y + (SET_NUM_H - 7 * UI_TEXT(2)) / 2)
#define SET_STEP_PX   UI(15)          /* drag travel per step */
#define SET_LIGHT_BAND_END (SET_SEG_Y(SET_ROW3_Y) + SET_SEG_H + UI(8))   /* the segments' band stops just under them */
static const char *const SET_BRIGHT[3] = { "30%", "60%", "100%" };
static const int         SET_BRIGHT_PCT[3] = { 30, 60, 100 };
static const char *const SET_VOLUME[3] = { "OFF", "QUIET", "NORMAL" };
static const char *const SET_LIGHT[2]  = { "MANUAL", "AUTO" };   /* the default first */
#if SET_HAS_FLIP
static const char *const SET_SCREEN[2] = { "UPRIGHT", "FLIPPED" };
#endif
static bool g_set_flipped;                                       /* the SCREEN row's state, from the platform */
void render_settings_set_flip(bool flipped) { g_set_flipped = flipped; }

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
static void set_chevron(ctx_t *c, int cx, int y, bool up, uint32_t rgb) {   /* the setup's, a size down */
    const int b = UI(3);                         /* the block */
    for (int i = 0; i < 4; i++) {
        int yy = up ? y + i * b : y - i * b;
        rect_fill(c, cx - b - i * b, yy, b, b, rgb);
        rect_fill(c, cx + i * b, yy, b, b, rgb);
    }
}
void render_settings(const tank_t *t, uint16_t *fb, int stride, int bright_pct, int volume) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    draw_text(&c, (TANK_W - text_w("SETTINGS", UI_TEXT(3))) / 2, SET_TITLE_Y, UI_TEXT(3), 0xffffff, "SETTINGS");
    int bi = bright_pct <= 30 ? 0 : bright_pct <= 60 ? 1 : 2;
    set_row(&c, SET_ROW1_Y, "BRIGHTNESS", SET_BRIGHT, 3, bi);
    set_row(&c, SET_ROW2_Y, "VOLUME", SET_VOLUME, 3, volume < 0 ? 0 : volume > 2 ? 2 : volume);
    draw_text(&c, SET_LABEL_X, SET_NOTE_Y, UI_TEXT(2), MSP_DIM, "FISH ARE QUIET AT NIGHT");
#if SET_HAS_FLIP
    set_row(&c, SET_ROW4_Y, "SCREEN", SET_SCREEN, 2, g_set_flipped ? 1 : 0);
#endif
    set_row(&c, SET_ROW3_Y, "LIGHTS OUT", SET_LIGHT, 2, t->light_auto ? 1 : 0);
    if (t->light_auto) {
        /* AUTO: AFTER [ n ] SEC, the number with its chevrons */
        char num[8]; snprintf(num, sizeof num, "%d", t->light_idle_s);
        int nw = text_w(num, SET_NUM_SCALE), nx = SET_NUM_X + SET_NUM_BOX_W - nw, cx = SET_NUM_X + SET_NUM_BOX_W / 2;
        draw_text(&c, SET_LABEL_X, SET_AFTER_Y, UI_TEXT(2), MSP_TEAL, "AFTER");
        draw_text(&c, nx, SET_NUM_Y, SET_NUM_SCALE, 0xffffff, num);
        rect_fill(&c, SET_NUM_X, SET_NUM_Y + SET_NUM_H + UI(6), SET_NUM_BOX_W, 3, 0x3f6a72);
        set_chevron(&c, cx, SET_NUM_Y - SET_NUM_GAP, true, MSP_TEAL);
        set_chevron(&c, cx, SET_NUM_Y + SET_NUM_H + SET_NUM_GAP + 3, false, MSP_TEAL);
        draw_text(&c, SET_NUM_X + SET_NUM_BOX_W + UI(10), SET_AFTER_Y, UI_TEXT(2), MSP_TEAL, "SEC");
    } else {
        /* MANUAL: how to work the light instead */
        draw_text(&c, SET_LABEL_X, SET_NUM_Y - UI(8), UI_TEXT(2), MSP_TEAL, "DOUBLE-TAP THE GLASS TO");
        draw_text(&c, SET_LABEL_X, SET_NUM_Y + UI(14), UI_TEXT(2), MSP_TEAL, "TURN THE LIGHT ON OR OFF");
    }
    /* the firmware version, hugging the bottom left of the frame (6 px up,
       on the labels' x; the bezel's curve is clear there), small (8 px) and
       dim: it is for the keeper who asks "how do I update?", not for
       reading - Strato: "FISH ARE QUIET AT NIGHT is meant to be read; the
       fw version is something most people should not care about. minimize
       it", "make it 8px tall", "hug the bottom of the frame" (2026-09-16).
       The installer page shows the version it would write in the same words. */
    char ver[40]; snprintf(ver, sizeof ver, "FW %s", version_port_string());
    /* clipped short of CLOSE: on the CYD the button shares its line (6 px a
       character in the 8 px font); a release version never gets that long */
    int room = (MSP_CLOSE_X - UI(8) - SET_LABEL_X) / 6;
    if (room >= 0 && room < (int)sizeof ver) ver[room] = 0;
    draw_text_8px(&c, SET_LABEL_X, TANK_H - 8 - 6, MSP_DIM, ver);
    button(&c, MSP_CLOSE_X, MSP_CLOSE_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", UI_TEXT(2));
}
static int set_segment(float x, int n) {
    if (x < SET_SEG_X - UI(10)) return -1;
    int i = (int)((x - SET_SEG_X + UI(3)) / SET_SEG_DX);
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}
/* the hit test: what a TAP at (x,y) means. *value: BRIGHT the percent,
 * VOLUME 0..2, LIGHT 1 = AUTO / 0 = MANUAL; the number's own hits carry no
 * value (IDLE_UP / IDLE_DOWN the chevrons, IDLE_NUM the number itself). */
enum { SET_HIT_IDLE_NUM = 100, SET_HIT_IDLE_UP, SET_HIT_IDLE_DOWN };
int render_settings_tap(float x, float y, int *value) {
    if (x >= MSP_CLOSE_X - UI(8) && y >= MSP_CLOSE_Y - UI(4)) return SET_TAP_CLOSE;
    /* the row bands: from a little above each segment down to the next row
       (fingers report low); the LIGHTS OUT band ends just under its
       segments so the number's up chevron below is its own */
    int seg = set_segment(x, 3);
#if SET_HAS_FLIP
    const int after_volume = SET_ROW4_Y;         /* the SCREEN row sits between VOLUME and LIGHTS OUT */
#else
    const int after_volume = SET_ROW3_Y;
#endif
    if (y >= SET_SEG_Y(SET_ROW1_Y) - UI(12) && y < SET_SEG_Y(SET_ROW2_Y) - UI(12)) { if (seg < 0) return SET_TAP_NONE; *value = SET_BRIGHT_PCT[seg]; return SET_TAP_BRIGHT; }
    if (y >= SET_SEG_Y(SET_ROW2_Y) - UI(12) && y < SET_SEG_Y(after_volume) - UI(12)) { if (seg < 0) return SET_TAP_NONE; *value = seg; return SET_TAP_VOLUME; }
#if SET_HAS_FLIP
    if (y >= SET_SEG_Y(SET_ROW4_Y) - UI(12) && y < SET_SEG_Y(SET_ROW3_Y) - UI(12)) { seg = set_segment(x, 2); if (seg < 0) return SET_TAP_NONE; *value = seg == 1; return SET_TAP_FLIP; }
#endif
    if (y >= SET_SEG_Y(SET_ROW3_Y) - UI(12) && y < SET_LIGHT_BAND_END)          { seg = set_segment(x, 2); if (seg < 0) return SET_TAP_NONE; *value = seg == 1; return SET_TAP_LIGHT; }
    if (y >= SET_LIGHT_BAND_END && x >= SET_NUM_X - UI(30) && x < SET_NUM_X + SET_NUM_BOX_W + UI(30)) {
        *value = 0;
        if (y < SET_NUM_Y - UI(8)) return SET_HIT_IDLE_UP;                     /* the band above the number */
        if (y < SET_NUM_Y + SET_NUM_H + UI(14)) return SET_HIT_IDLE_NUM;      /* the number */
        return SET_HIT_IDLE_DOWN;                                          /* below, down to the bezel */
    }
    return SET_TAP_NONE;
}
static void set_step(tank_t *t, int dir) {
    int v = t->light_idle_s + dir;
    if (v < LIGHT_IDLE_MIN_S) v = LIGHT_IDLE_MIN_S;
    if (v > LIGHT_IDLE_MAX_S) v = LIGHT_IDLE_MAX_S;
    if (v != t->light_idle_s) { t->light_idle_s = v; tank_emit(TEV_WHEEL_TICK, -1); }
}
int render_settings_touch(tank_t *t, float x, float y, bool down, int *value) {
    static bool s_down, s_spun; static float s_px, s_py, s_ly, s_acc; static int s_hit, s_hv;
    int r = SET_TAP_NONE; *value = 0;
    bool on_num = s_hit == SET_HIT_IDLE_NUM || s_hit == SET_HIT_IDLE_UP || s_hit == SET_HIT_IDLE_DOWN;
    if (down && !s_down) {                                  /* press */
        s_px = x; s_py = y; s_ly = y; s_acc = 0; s_spun = false;
        s_hit = render_settings_tap(x, y, &s_hv);
    } else if (down && on_num && t->light_auto) {            /* the number: vertical travel steps the value */
        s_acc += y - s_ly;
        while (s_acc <= -SET_STEP_PX) { set_step(t, +1); s_acc += SET_STEP_PX; s_spun = true; }   /* up = more seconds */
        while (s_acc >=  SET_STEP_PX) { set_step(t, -1); s_acc -= SET_STEP_PX; s_spun = true; }
        s_ly = y;
    } else if (!down && s_down) {                           /* release: a tap, unless the number was swiped */
        if (s_spun) { progression_settings_changed(); r = SET_TAP_IDLE; *value = t->light_idle_s; }
        else {
            int v = 0, h = render_settings_tap(x, y, &v);
            float dx = x - s_px, dy = y - s_py;
            if (h == s_hit && dx * dx + dy * dy < 24 * 24) {
                if (h == SET_TAP_LIGHT) {                           /* MANUAL (0, the default) / AUTO (1); either way the light comes on */
                    t->light_auto = v != 0; t->light_manual_off = false; progression_settings_changed();
                    r = SET_TAP_LIGHT; *value = v;
                } else if ((h == SET_HIT_IDLE_UP || h == SET_HIT_IDLE_DOWN) && t->light_auto) {
                    set_step(t, h == SET_HIT_IDLE_UP ? +1 : -1); progression_settings_changed();
                    r = SET_TAP_IDLE; *value = t->light_idle_s;
                } else if (h == SET_TAP_CLOSE || h == SET_TAP_BRIGHT || h == SET_TAP_VOLUME || h == SET_TAP_FLIP) { r = h; *value = v; }
            }
        }
    }
    s_down = down;
    return r;
}
