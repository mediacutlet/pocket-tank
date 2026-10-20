/* render.c — porthole look: deep gradient water, AMOLED-black floor, procedural
 * fish (body polygon + animated tail + earned markings), bubbles, seagrass
 * the keeper trims. Everything is drawn
 * into a bare RGB565 buffer; night dims the palette. */
#include "render.h"
#include "advanced.h"
#include "icons.h"
#include "progression.h"
#include "collection.h"
#include "sounds.h"
#include "notice.h"
#include "gift.h"
#include "tank_events.h"
#include "setup.h"
#include "version.h"
#include "lang.h"
#include "backgrounds.h"
#include "touch_gate.h"
static bool   g_tap_press_on;      /* render_tap_press (render.h): the press this release's taps belong to */
static double g_tap_press;
void render_tap_press(double clock) { g_tap_press = clock; g_tap_press_on = true; }
void render_tap_press_end(void) { g_tap_press_on = false; }
static double tap_press(const tank_t *t) { return g_tap_press_on && g_tap_press <= t->clock ? g_tap_press : t->clock; }
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define TAU 6.2831853f

/* a draw target: fb + stride + night dim, plus the window it covers in tank
 * coordinates (ox,oy,w,h) - the frame is (0,0,TANK_W,TANK_H); the stats card
 * cache is a small sprite that still gets drawn in tank coordinates */
typedef struct { uint16_t *fb; int stride; float dim; int ox, oy, w, h; } ctx_t;
/* every draw target made outside the keeper page counts: the page keeps a framebuffer only while nothing else drew
   (THE PAGE KEPT, render_milestones) */
static unsigned g_ctx_other; static bool g_ms_in;
#define CTX_MADE() do { if (!g_ms_in) g_ctx_other++; } while (0)
static ctx_t ctx_full(uint16_t *fb, int stride, float dim) {
    CTX_MADE();
    ctx_t c = { fb, stride, dim, 0, 0, TANK_W, TANK_H }; return c;
}
/* PAGE space (render.h): the full-screen pages, the prompts and the exported
 * drawing helpers put their 448 x 368 layout in the middle of the frame - a
 * ctx whose origin is the page's corner. In the rectangle that IS the frame. */
static bool g_dirty_hold;                          /* below, with the dirty mask */
static ctx_t ctx_page(uint16_t *fb, int stride) {
    CTX_MADE();
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
static uint32_t *g_keep_rec, *g_keep_rec2;       /* SWIM DEPTH phase 2: while set, the fish being drawn marks its every span here (g_keep; and
                                                     g_fbg_front / _thru against a bed IN FRONT) */
static uint32_t *g_fbg_clr1, *g_fbg_clr2;         /* (the bed's marks, for the front slot's clearing too) */
static uint32_t *g_keep_clr_m;                   /* (g_keep, for the clearing) */
static bool g_keep_clr;                           /* ... while set (a critter of the front slot or the top lane drawn after the pieces), its spans
                                                     clear the marks: what it paints over a fish is the critter's, shaded and covered as the critter */
static inline void bb_add(int x0, int x1, int y) {
    if (!g_bb_on) return;
    if ((g_keep_rec || g_keep_rec2 || g_keep_clr) && (unsigned)y < (unsigned)TANK_H) {
        int a = x0 < 0 ? 0 : x0, b = x1 > TANK_W - 1 ? TANK_W - 1 : x1;
        const int wpr = (TANK_W + 31) / 32;
        uint32_t *row = g_keep_rec ? g_keep_rec + y * wpr : NULL, *row2 = g_keep_rec2 ? g_keep_rec2 + y * wpr : NULL;
        uint32_t *c0 = g_keep_clr && g_keep_clr_m ? g_keep_clr_m + y * wpr : NULL, *c1 = g_keep_clr && g_fbg_clr1 ? g_fbg_clr1 + y * wpr : NULL,
                 *c2 = g_keep_clr && g_fbg_clr2 ? g_fbg_clr2 + y * wpr : NULL;
        for (int w = a >> 5; a <= b && w <= b >> 5; w++) {
            uint32_t m = 0xFFFFFFFFu;
            if (w == a >> 5) m &= 0xFFFFFFFFu << (a & 31);
            if (w == b >> 5) m &= 0xFFFFFFFFu >> (31 - (b & 31));
            if (c0) c0[w] &= ~m;                     /* (the clearing first: an animal of the front slot marking its own pixels keeps them) */
            if (c1) c1[w] &= ~m;
            if (c2) c2[w] &= ~m;
            if (row) row[w] |= m;
            if (row2) row2[w] |= m;
        }
    }
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
static double    g_sg_clock;
static int       g_sg_top = 0;                 /* the highest row the cached grass touched */
static const uint32_t *g_under;                /* while the overlays behind are drawn: pixels to leave alone */
static bool      g_bg_rec;                     /* while the cache's grass is painted: record into g_bgmask */
static int64_t   g_sg_us; static int g_sg_n;   /* the refreshes' cost (the director's line) */
void render_set_grass_cache(uint16_t *buf, uint32_t *mask) { g_sceneg = buf; g_bgmask = mask; g_sg_ok = false; }
void render_set_grass_cache_on(bool on, int period_ms) { bool was = g_sg_on; g_sg_on = on && g_sceneg && g_bgmask; if (period_ms > 0) g_sg_period = period_ms / 1000.0f; if (!was || !g_sg_on) g_sg_ok = false; }   /* (a new period alone keeps the cache) */
bool render_grass_cache_on(void) { return g_sg_on; }
int  render_grass_cache_ms(void) { return (int)(g_sg_period * 1000 + 0.5f); }
void render_grass_cache_prof(int64_t *us, int *n) { *us = g_sg_us; *n = g_sg_n; g_sg_us = 0; g_sg_n = 0; }
static int g_under_y0;                         /* the first row g_under holds (the grass loop's masks are bands) */
static inline bool under_bg(int x, int y) {
    return g_under && (unsigned)x < (unsigned)TANK_W && y >= g_under_y0 && y < TANK_H && ((g_under[y * DIRTY_WORDS_PER_ROW + (x >> 5)] >> (x & 31)) & 1);
}
/* THE SWAY REPEATS (0.3.9.3, the grass loop below): every motion of a frond is a whole number of turns in
 * VEG_PERIOD seconds - the sway at the grass's old 0.9 rad/s, and the bend that runs up a frond (0.6 rad/s
 * until now, which with the 0.9 repeated only every 21 s) and the sword plant's lazier 0.6 at that speed
 * too - so a few stored frames of it loop seamlessly. veg_clock is the sway's own clock, the frame's taken
 * round the period (or a phase forced by the loop's bake). */
#define VEG_PERIOD   7.0f
#define VEG_W        (6.2831853f / VEG_PERIOD)
static float g_veg_clock_force = -1;
static inline float veg_clock(double clock) { return g_veg_clock_force >= 0 ? g_veg_clock_force : (float)fmod(clock, VEG_PERIOD); }
/* the clock's drawing face (tank.h tank_anim), worked out once a frame - the S3 does a double's fmod in software */
static double g_anim_of = -1; static float g_anim;
static inline float anim(const tank_t *t) { if (t->clock != g_anim_of) { g_anim = tank_anim(t->clock); g_anim_of = t->clock; } return g_anim; }
/* THE GRASS LOOP (0.3.9.3, Strato: "refactoring the seagrass from the ground up ... the biggest source of lag").
 * With the weave gone every grass bed BEHIND the fish is background, and its sway repeats every VEG_PERIOD
 * seconds (veg_clock): so the back grass is BAKED. The loop holds up to GL_MAX BANDS - the scene's rows the grass
 * can reach with the grass of one moment of the sway in them (fewer if the memory budget says so) - and between
 * them GL_FINE fine moments a band (up to GL_FMAX, gl_pick_f), each kept as its DIFFERENCE from the band nearest it (the pixels that moved:
 * a blade's base hardly does, so a quarter of the grass's cache lines on average). A frame is primed with the
 * scene above the band and its band below (the firmware's prefetch: two DMA copies, render_grass_loop_parts),
 * then its fine moment's difference is laid over it. No back grass is drawn in a frame at all. Each band keeps a
 * mask of its grass, and a difference says which of its pixels are grass (the overlays behind it - a crown, the
 * candle, the lava - stay under it, as with the cache above). The loop goes stale when what the grass looks like
 * changes (the scene, a frond's height, a bed moving, its depth): until it is rebaked - one band or difference a
 * frame, the bands first, once the grass has held still a few frames - a frame of it draws its grass the old way
 * (a frame whose difference is not baked yet shows its band's own moment). Beds IN FRONT stay live after the fish.
 * Measured on the square: 14.6 fps with the grass drawn, 18.2 with ten bands alone (too steppy: a tall blade's
 * tip moved ~1.3 px between them, three times a second), 16.0 with every moment as runs over the scene (the CPU
 * touching the whole band again), 18.6 with no grass at all; four fine moments a band on the clock, 17.4 (Strato: "still
 * a little steppy" - moments on the clock's grid against frames on their own); eight a band STEPPED one a frame
 * (render_grass_loop_lock), 56 moments in 0.8 MB of differences, 17.3-18.9. */
#define GL_MAX 16                                /* bands at most (an epoch keeps 5 bits of one) */
#define GL_FINE 8                                /* fine moments a band, as planned: its own and the next seven */
#define GL_FMAX 12                               /* ... at most: stepping, a board whose memory holds fewer bands takes more (gl_pick_f) */
#define GL_F_LEAST 6                             /* ... and its fewest fine moments a band, before a band fewer (0.4: a tall bed's arena) */
#define GL_MIN_BANDS 3                           /* the fewest a loop is planned with (0.4, 2026-11-01, Strato: the tall-grass fix in 0.4 - four was the floor,
                                                    and a grown bed on the square (a 410-row band) needed ~2.9 MB for four: no loop, the grass live,
                                                    10.6 fps with visitors in. Three fit in ~2.4 MB, 36 moments of the sway instead of 48) */
static void   *(*g_gl_alloc)(size_t);
static void    (*g_gl_free)(void *);
static size_t  g_gl_budget;
static bool    g_gl_on, g_gl_bypass;          /* (bypass: the sim's A/B - the loop kept, not used) */
static int     g_gl_n;                          /* bands in the loop (fine moments: g_gl_f times as many) */
static int     g_gl_fcap_n;                     /* (0.4) the bands g_gl_fcap was measured for (an overflow: its moments a band fit at that many bands) */
static size_t  g_gl_fit_avg;
static int     g_gl_fsel, g_gl_fsel_n;          /* (0.4, THE MOST MOMENTS) the moments a band the budget holds at the planned count of bands */                    /* ... and the moment's difference it measured, bytes (0: none yet) */
static int     g_gl_f = GL_FINE, g_gl_fcap = GL_FMAX;   /* fine moments a band, and the most the differences have held (an overflow lowers it) */
static bool    g_gl_replan;                     /* a difference did not fit: the loop planned again with fewer */
static int     g_gl_y0, g_gl_rows, g_gl_cap;    /* the band's first row, its rows, the rows allocated */
static uint16_t *g_gl_band[GL_MAX];
static uint32_t *g_gl_mask[GL_MAX];
static uint32_t *g_gl_omask[GL_MAX];               /* SWIM DEPTH (0.3.9.4): each band's ODD fronds' pixels (a fish THROUGH the grass); none: THROUGH hides BEHIND */
static bool    g_gl_ok[GL_MAX];
static uint16_t g_gl_ver[GL_MAX];               /* a band's bake count: a frame primed with an older one copies again */
static uint16_t *g_gl_diff; static size_t g_gl_dwords, g_gl_dused;   /* the fine moments' differences, one after another as they are baked */
static uint32_t g_gl_doff[GL_MAX * GL_FMAX];    /* (where each starts) */
static int     g_gl_nomem;                      /* rows the platform could not spare a loop for (not tried again for them ... */
static double  g_gl_nomem_t = -1;               /* ... until GL_RETRY_S after: the tank clock it gave up at */
#define GL_RETRY_S 30.0f                        /* (2026-11-01: a plan at a boot, PSRAM still held by the start, gave up on the square
                                                   for the day - a minute later 3.7 MB was free and the loop never came; Strato: retry) */
/* THE BED ORDER (0.4.1, D4 - Strato 11-05): the sword plant BEHIND THE SEAGRASS (tank_plant_under) is bed 3 painted FIRST in every back
   pass - the loop's bake, its box patch, the live grass, the crab's weave, the grass cache; IN FRONT OF THE SEAGRASS (0.4's one look), last */
static inline int bed_ord(const tank_t *t, int k) { return tank_plant_under(t) && tank_veg_beds(t) > 3 ? (k == 0 ? 3 : k - 1) : k; }   /* the k-th back bed painted */
static int     g_gl_ceil_rows = TANK_H, g_gl_ceil_y = TANK_H; static uint32_t g_gl_ceil_key; static bool g_gl_ceil_old;   /* THE CEILING (gl_top): the band's rows at the tallest the grass grows; the control */
static int     g_gl_top_bed = -1, g_gl_top_segs, g_gl_top_n;   /* (what the band's top came from, for the log: the tallest frond's bed and segments, the beds counted) */
static int     g_gl_why;                        /* ... and where the plan gave up (1 the tables, 2 the bake's buffers, 3 fewer than four bands or no arena; render_grass_loop_nomem) */
static uint8_t g_gl_fok[GL_MAX * GL_FMAX];      /* a fine moment: 0 stale, 1 baked, 2 its difference too big (its band's moment instead) */
static uint16_t *g_gl_bake; static uint32_t *g_gl_bmask;   /* a fine moment's bake, and a frame's mask of its grass ... */
static uint32_t *g_gl_pmask;                    /* ... the bake's own mask of it (0.3.9.4: a bake in slices spans frames that use g_gl_bmask) */
static bool g_gl_skip_old;                        /* (--selftest-plant-order's control: the order out of the loop's look, as before) */
void render_debug_gl_skip_old(bool on) { g_gl_skip_old = on; }
void render_debug_gl_ceil_old(bool on) { g_gl_ceil_old = on; g_gl_ceil_key = 0; }   /* (THE CEILING's control: the band's room as before 0.4.1) */
static uint32_t g_gl_key, g_gl_key_last;        /* what the grass looks like (gl_key): the loop's, and the last frame's */
static int     g_gl_still;                      /* frames the look has held: a bake waits for GL_SETTLE (night falling, a trim: the first frames still move) */
#define GL_SETTLE 3
static int     g_gl_bakes; static int64_t g_gl_bake_us;   /* (the display line) */
static bool    g_gl_warm;                       /* the loop has been whole since it was turned on (LOADING: render_grass_loop_warming) */
static int     g_gl_lock, g_gl_cur = -1;        /* moments the loop steps through a frame at a time (0: by the clock), the last frame's */
static float   g_gl_dt_avg;                     /* (THE PACE) the frames' average length, for a loop short of moments */
static double  g_gl_step_t = -1; static float g_gl_step_acc;   /* THE PACE (gl_next): the clock of the last stepped frame; the fraction of a moment carried */
void render_set_grass_loop(void *(*alloc)(size_t), void (*release)(void *), size_t budget) { g_gl_alloc = alloc; g_gl_free = release; g_gl_budget = budget; }
static void gl_patch_reset(void);                /* THE PATCH and the ring (0.3.9.4, with the loop's pieces below) */
static void gl_ring_reset(void);
static void gl_meta_free(void);
static int  gl_exact(void);
static void gl_release(void) {
    for (int k = 0; k < GL_MAX; k++) { if (g_gl_band[k] && g_gl_free) g_gl_free(g_gl_band[k]); if (g_gl_mask[k] && g_gl_free) g_gl_free(g_gl_mask[k]); if (g_gl_omask[k] && g_gl_free) g_gl_free(g_gl_omask[k]); g_gl_band[k] = NULL; g_gl_mask[k] = NULL; g_gl_omask[k] = NULL; g_gl_ok[k] = false; }
    if (g_gl_free) { if (g_gl_diff) g_gl_free(g_gl_diff); if (g_gl_bake) g_gl_free(g_gl_bake); if (g_gl_bmask) g_gl_free(g_gl_bmask); if (g_gl_pmask) g_gl_free(g_gl_pmask); }
    g_gl_diff = NULL; g_gl_bake = NULL; g_gl_bmask = NULL; g_gl_pmask = NULL; g_gl_dused = 0;
    memset(g_gl_fok, 0, sizeof g_gl_fok);
    gl_patch_reset(); gl_ring_reset(); gl_meta_free();
    g_gl_n = 0; g_gl_cap = 0;
}
void render_grass_loop_on(bool on) { g_gl_on = on && g_gl_alloc && g_gl_free; g_gl_nomem = 0; g_gl_warm = false; if (!g_gl_on) gl_release(); }
void render_grass_loop_lock(int moments) { if (moments != g_gl_lock) { g_gl_lock = moments; g_gl_nomem = 0; g_gl_cur = -1; g_gl_step_t = -1; g_gl_step_acc = 0; g_gl_fcap = GL_FMAX; g_gl_fcap_n = 0; g_gl_fit_avg = 0; g_gl_fsel_n = 0; gl_release(); } }   /* (planned again for them) */
int  render_grass_loop_moment(void) { return g_gl_cur; }
int  render_grass_loop_nomem(int *why) { *why = g_gl_why; return g_gl_nomem; }   /* rows the plan gave up on (0: it did not), and where */
void render_grass_loop_top(int *bed, int *segs, int *beds) { *bed = g_gl_top_bed; *segs = g_gl_top_segs; *beds = g_gl_top_n; }   /* (the log) the tallest back frond the band is sized for */
void render_grass_loop_arena(int *used_kb, int *cap_kb) { *used_kb = (int)(g_gl_dused * 2 / 1024); *cap_kb = (int)(g_gl_dwords * 2 / 1024); }
bool render_grass_loop_is_on(void) { return g_gl_on; }
void render_grass_loop_bypass(bool on) { g_gl_bypass = on; }
void render_force_veg_clock(float c) { g_veg_clock_force = c; }
float render_sway_moment(const tank_t *t) { return veg_clock(t->clock); }
int  render_grass_loop_n(void) { return g_gl_n * g_gl_f; }
int  render_grass_loop_ready(void) { return gl_exact(); }   /* moments baked at the look as it is (THE PATCH: a moment stale inside the boxes is served, not counted) */
void render_grass_loop_prof(int *n, int *bakes, int64_t *us, int *rows) { *n = g_gl_n * g_gl_f; *bakes = g_gl_bakes; *us = g_gl_bake_us; *rows = g_gl_rows; g_gl_bakes = 0; g_gl_bake_us = 0; }
/* the loop spans HALF the sway's period: a frond's shape is its sway times the bend running up it, and half a
   period on flips the sign of both - the same shape (the bands of moments k and k + n/2 came out identical) */
#define GL_SPAN (VEG_PERIOD * 0.5f)
static inline int gl_fine(double clock) { int nf = g_gl_n * g_gl_f; return nf ? (int)((float)fmod(clock, GL_SPAN) * nf / GL_SPAN) % nf : 0; }
static inline float gl_ftime(int j) { return j * GL_SPAN / (g_gl_n * g_gl_f); }
static inline int gl_base(int j) { return (j + g_gl_f / 2) / g_gl_f % g_gl_n; }   /* the band nearest a fine moment */
/* a band's own fine moments, as gl_base groups them: from GL_DLO before its moment to GL_DHI after - f/2 either side for
   an odd f, one fewer after for an even one (0.3.9.3: the clamp took the even case for both, and with 11 a band - the
   watch, the square - a frame primed with its band was held a moment back every frame: the grass froze) */
#define GL_DLO (g_gl_f / 2)
#define GL_DHI (g_gl_f - 1 - g_gl_f / 2)
/* the next frame's fine moment: by the clock - or, stepping (render_grass_loop_lock), the one after the last frame's,
   so the grass moves exactly once a frame, with the fish (Strato: moments on the clock's grid "vary from the frame
   rate of the rest of the tank" - a frame that repeats one or skips one is a step the eye catches) */
/* THE PACE (0.3.9.4, Strato on the square at 13-14 fps: the fronds "glitchy and not smooth"; another keeper's light tank swaying
   twice as fast as the full one): stepping ran ONE moment a frame, so the sway's period was the frame rate's (GRASS_LOOP_MOMENTS
   planned for ~17.5 fps on the square) and every uneven frame was a jerk of the grass. Now a stepped frame advances by the clock -
   the moments the sway has walked since the last frame, carried fractionally - one at the least (never a repeat), GL_STEP_MAX at
   most (never a jump): the period is the sway's at any frame rate, a long frame is a slightly longer step, not a stall */
#define GL_STEP_MAX 3
static inline int gl_steps(double clock, float *acc) {   /* the moments a stepped frame at `clock` advances, and the fraction it would carry (nothing changed) */
    const int nf = g_gl_n * g_gl_f;
    const float dt = g_gl_step_t >= 0 && clock > g_gl_step_t ? (float)(clock - g_gl_step_t) : 0;
    float want = g_gl_step_acc + dt * nf / GL_SPAN;
    int steps = (int)want;
    const float rate = g_gl_dt_avg * nf / GL_SPAN;   /* moments a frame at the AVERAGE frame (one short frame never makes a repeat): under a half,
                                                         a step every frame sways it 2x+ too fast */
    const int least = g_gl_dt_avg <= 0 || rate >= 0.5f ? 1 : 0;    /* (0.4, 11-01: a loop SHORT of its moments - tall grass, three bands, 21 of 48 on the square - holds a
                                                      moment a few frames: one a frame swayed it 4x too fast, Strato: "smooth but wiggling too fast") */
    if (steps < least) { steps = least; want = least; }
    if (steps > GL_STEP_MAX) { steps = GL_STEP_MAX; want = GL_STEP_MAX; }
    if (acc) *acc = want - steps;
    return steps;
}
static inline int gl_next(double clock) {               /* PURE: the prefetch asks for the next frame's moment before the frame does (a first cut carried the fraction here
                                                          and both asks advanced it: the sway lurched 1-3 moments at random - Strato: the fronds "glitchy", a fish through them flickering) */
    const int nf = g_gl_n * g_gl_f;
    if (!(g_gl_lock && g_gl_cur >= 0 && g_gl_cur < nf)) return gl_fine(clock);
    return (g_gl_cur + gl_steps(clock, NULL)) % nf;
}
static inline void gl_stepped(int j, double clock) {    /* the frame took moment j: the carry and the clock move on here alone */
    if (g_gl_cur >= 0) { float acc; gl_steps(clock, &acc); g_gl_step_acc = acc; }
    if (g_gl_step_t >= 0 && clock > g_gl_step_t && clock - g_gl_step_t < 0.5f)
        g_gl_dt_avg = g_gl_dt_avg > 0 ? g_gl_dt_avg * 0.95f + (float)(clock - g_gl_step_t) * 0.05f : (float)(clock - g_gl_step_t);
    g_gl_cur = j; g_gl_step_t = clock;
}
static uint32_t *g_om;                           /* SWIM DEPTH: while set, the odd fronds' pixels recorded here too (g_veg_odd) */
static bool      g_veg_odd;                      /* (draw_veg: the frond being painted is an odd one) */
/* SWIM DEPTH (0.3.9.4, docs/swim-depth.md): a fish BEHIND or THROUGH the back grass. Before it is drawn the pixels
   under its box are kept (and their dirty bits); after, the grass's own pixels inside the box come back over it -
   every frond's (BEHIND) or the odd fronds' (THROUGH). The grass's pixels: the loop's band mask (its fine moment's,
   exact) by day, or the frame's own when the grass was drawn live (the night, a stale loop) - recorded as it is
   drawn; the odd fronds': each band's g_gl_omask, or the live frame's. Memory from the platform, one block */
static uint16_t *g_dep_save; static uint32_t *g_dep_dirty, *g_fl_all, *g_fl_odd;
static uint32_t *g_fbg_front, *g_fbg_thru; static bool g_fbg_on, g_fbg_used;   /* a bed IN FRONT (the sword plant): the pixels of fish IN FRONT of it, THROUGH it;
                                                       veg_put keeps its leaves off them (every leaf / the even ones) while g_fbg_on */
static uint32_t *g_keep; static bool g_keep_on;     /* phase 2: the pixels of fish IN FRONT of the pieces this frame - the restore of the pieces
                                                       IN FRONT, their overlays (crowns, lava, the lantern's face) and the arch's shade keep off them */
static inline bool keep_at(int x, int y) { return g_keep_on && ((g_keep[y * DIRTY_WORDS_PER_ROW + (x >> 5)] >> (x & 31)) & 1); }
static uint32_t *g_bedm; static bool g_bedm_on; static uint32_t g_bed_over_k;   /* the sword plant IN FRONT's leaves this frame (recorded as it is drawn), and
                                                       the pieces ranked below it: their per-frame overlays (the pumpkin's face and flame, the volcano's
                                                       lava, a coral's crown) keep off its leaves (0.3.9.4, Strato on the square: the plant raised over the
                                                       pumpkin, the face clipping through it) */
static inline bool bed_over(int k, int x, int y) {
    return g_bedm_on && k >= 0 && k < 32 && (g_bed_over_k >> k & 1) && ((g_bedm[y * DIRTY_WORDS_PER_ROW + (x >> 5)] >> (x & 31)) & 1);
}
#define DEP_CAP_PX (200 * 200)
#define DEP_CAP_ROWS 200
static short g_fbb[N_FISH_SLOTS][4]; static bool g_fbb_ok[N_FISH_SLOTS];   /* each swimmer's drawn box, the last frame - visitors too (their slots
                                                   run past N_FISH_MAX: sized for home fish, two visitors wrote past it and the square crashed, 2026-11-01) */
_Static_assert(sizeof g_fbb / sizeof g_fbb[0] >= N_FISH_SLOTS && sizeof g_fbb_ok >= N_FISH_SLOTS, "a box for every swimmer, visitors too");
static bool g_dep_off;                           /* (the sim's grass tests: every fish IN FRONT - THROUGH at a fine moment takes its
                                                    band's odd fronds, a pixel off at a frond's edge, which a pixel-for-pixel test of
                                                    the grass would read as the grass's own difference) */
void render_depth_off(bool off) { g_dep_off = off; }
static bool g_dep_keep_old;                         /* (--selftest-front-keep's control: the restore behind a piece ignores the fish IN FRONT, as before 0.3.9.6) */
void render_debug_front_keep_old(bool on) { g_dep_keep_old = on; }
static int64_t g_dep_us; static int g_dep_n;      /* the keep and the put-back's own time, and how many fish did it (render_depth_prof) */
void render_depth_prof(int64_t *us, int *n) { *us = g_dep_us; *n = g_dep_n; g_dep_us = 0; g_dep_n = 0; }
size_t render_depth_buf_bytes(void) {
    return DEP_CAP_PX * 2 + (size_t)DEP_CAP_ROWS * DIRTY_WORDS_PER_ROW * 4 + 6 * (size_t)TANK_H * DIRTY_WORDS_PER_ROW * 4;
}
void render_set_depth_buf(void *buf) {
    if (!buf) { g_dep_save = NULL; g_dep_dirty = g_fl_all = g_fl_odd = g_keep = g_fbg_front = g_fbg_thru = g_bedm = NULL; return; }
    uint8_t *p = buf;
    g_dep_save = (uint16_t *)p; p += DEP_CAP_PX * 2;
    g_dep_dirty = (uint32_t *)p; p += (size_t)DEP_CAP_ROWS * DIRTY_WORDS_PER_ROW * 4;
    g_fl_all = (uint32_t *)p; p += (size_t)TANK_H * DIRTY_WORDS_PER_ROW * 4;
    g_fl_odd = (uint32_t *)p; p += (size_t)TANK_H * DIRTY_WORDS_PER_ROW * 4;
    g_keep = (uint32_t *)p; p += (size_t)TANK_H * DIRTY_WORDS_PER_ROW * 4;
    g_fbg_front = (uint32_t *)p; p += (size_t)TANK_H * DIRTY_WORDS_PER_ROW * 4;
    g_fbg_thru = (uint32_t *)p; p += (size_t)TANK_H * DIRTY_WORDS_PER_ROW * 4;
    g_bedm = (uint32_t *)p;
}
static inline void mark_run(uint32_t *row, int x0, int x1) {
    int w0 = x0 >> 5, w1 = x1 >> 5;
    if (w0 == w1) row[w0] |= (0xFFFFFFFFu >> (31 - (x1 & 31))) & (0xFFFFFFFFu << (x0 & 31));
    else { row[w0] |= 0xFFFFFFFFu << (x0 & 31); for (int w = w0 + 1; w < w1; w++) row[w] = 0xFFFFFFFFu; row[w1] |= 0xFFFFFFFFu >> (31 - (x1 & 31)); }
}
static inline void mask_clear_run(uint32_t *row, int x0, int x1) {
    int w0 = x0 >> 5, w1 = x1 >> 5;
    uint32_t lo = 0xFFFFFFFFu << (x0 & 31), hi = 0xFFFFFFFFu >> (31 - (x1 & 31));
    if (w0 == w1) { row[w0] &= ~(lo & hi); return; }
    row[w0] &= ~lo; for (int w = w0 + 1; w < w1; w++) row[w] = 0; row[w1] &= ~hi;
}
/* THE PATCH (0.3.9.4, with THE GRASS LOOP's pieces): a box of the band repainted - the grass's marks clipped to it (g_bg_clip), the
   fronds and rows clear of it skipped (g_veg_cull: a frond reaches GL_BED_PAD px past its root at most) */
typedef struct { short x0, y0, x1, y1; } gl_rect_t;                    /* inclusive, frame coordinates */
static bool g_bg_clip; static gl_rect_t g_bg_clip_r;
static bool g_veg_cull;
static const uint16_t *(*g_veg_rowclip)(int y);                          /* THE PATCH: a frond row's span clipped to the row's stale intervals (gl_wrow_of; NULL: none) */
static int  g_span_xc = -1;                                              /* the vignette's middle for every run of one frond row's span (veg_span): a run a piece splits
                                                                            the span into keeps the whole span's alpha, so what a piece does inside its box never changes a
                                                                            pixel outside it (THE PATCH's rule); -1: a span on its own */
#define GL_BED_PAD 12                                                    /* a frond's reach past its root: the sway (4; the sword 2.5) and the half-width (2.4; the sword's 7.2) */
static inline void bg_mark(int x0, int x1, int y) {
    if (!g_bg_rec || (unsigned)y >= (unsigned)TANK_H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > TANK_W - 1) x1 = TANK_W - 1;
    if (g_bg_clip) {
        if (y < g_bg_clip_r.y0 || y > g_bg_clip_r.y1) return;
        if (x0 < g_bg_clip_r.x0) x0 = g_bg_clip_r.x0;
        if (x1 > g_bg_clip_r.x1) x1 = g_bg_clip_r.x1;
    }
    if (x0 > x1) return;
    mark_run(g_bgmask + y * DIRTY_WORDS_PER_ROW, x0, x1);
    if (g_om && g_veg_odd) mark_run(g_om + y * DIRTY_WORDS_PER_ROW, x0, x1);
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
    if (CTX_IN(c, x, y) && !under_bg(x, y)) { blend565(&CTX_PX(c, x, y), s, a); bb_add(x, x, y); dirty_px(x, y); TOUCH_PX(c, x, y); }   /* (bb_add: the fish's box and SWIM DEPTH's marks see blends too) */
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
static void fill_poly_a(ctx_t *c, const float *xs, const float *ys, int n, uint32_t rgb, int alpha) {
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
        span(c, (int)x0, (int)x1, y, &s, alpha);
    }
}
static void fill_poly(ctx_t *c, const float *xs, const float *ys, int n, uint32_t rgb) { fill_poly_a(c, xs, ys, n, rgb, 255); }

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

/* the trail's bend (0.4): a body point lx behind bb sits on an arc of
 * curvature k, its frame turned by th = k (bb - lx) - so a tail lagging a
 * turn curves the rear body smoothly instead of the whole fish swinging
 * rigid on its middle. Past the body's end (e) a fin curves on at k x tr:
 * a classic's tail fin rides stiff off the body, a long veil keeps
 * flowing. Small-angle forms (th held under 1 rad): a few multiplies a
 * vertex, nothing a pixel. k = 0 is the rigid fish exactly */
typedef struct { float bb, e, k, tr; } bend_t;
static inline void bend_pt(const bend_t *b, float lx, float ly, float *ox, float *oy) {
    if (lx >= b->bb || b->k == 0) { *ox = lx; *oy = ly; return; }
    float s = b->bb - lx, th, x, y;
    if (lx >= b->e) {                                    /* on the body: one arc */
        th = b->k * s;
        x = lx + s * th * th * (1 / 6.0f); y = -s * th * 0.5f;
    } else {                                             /* past it: the arc to e, then on at k x tr */
        float s0 = b->bb - b->e, t0 = b->k * s0, kr = b->k * b->tr, r = b->e - lx;
        th = t0 + kr * r;
        x = b->bb - s0 + s0 * t0 * t0 * (1 / 6.0f) - r + (t0 * t0 * r + t0 * kr * r * r + kr * kr * r * r * r * (1 / 3.0f)) * 0.5f;
        y = -s0 * t0 * 0.5f - (t0 * r + kr * r * r * 0.5f);
    }
    th = th > 1 ? 1 : th < -1 ? -1 : th;
    *ox = x - ly * th; *oy = y + ly * (1 - th * th * 0.5f);
}
static inline float bend_x(const bend_t *b, float lx, float ly) { float x, y; bend_pt(b, lx, ly, &x, &y); return x; }
static inline float bend_y(const bend_t *b, float lx, float ly) { float x, y; bend_pt(b, lx, ly, &x, &y); return y; }

/* the body shapes (SHAPE_*, 0.4; docs/genetics-0.4.md and its sketch): the
 * outline, sampled from the prototype's quadratic silhouette for CLASSIC;
 * where the tail joins (root); the half-height the fins and the pattern scale
 * by (h); how far the fins reach past the body (fh: a slender fish keeps a
 * real sail, a bulbous one's stays in proportion); the fins' x scale (k: the
 * body's half-length over CLASSIC's, so a dorsal sits on the back it belongs
 * to); the eye; the bars' center and spacing (bulbous bars spread clear of
 * the eye). (The tail beat's pace by form: tank.c, tank_fish_beat_rate - the beat runs in the swim since 0.4) */
typedef struct { float x[10], y[10], root, h, fh, k, eye_x, eye_y, bar_x, bar_dx; } fish_form_t;
static const fish_form_t FORMS[SHAPE_N] = {
    /* CLASSIC: every fish before 0.4 */
    { { 16, 10, 2, -8, -15, -18, -15, -8, 2, 10 }, { 0, -7, -10, -9, -6, 0, 6, 9, 10, 7 },
      -11, 10, 10, 1.0f, 9, -3, -3, 6 },
    /* SLENDER: long and low, a pointed nose (a danio) */
    { { 21, 14, 4, -8, -17, -21, -17, -8, 4, 14 }, { 0, -4.5f, -6.5f, -6, -4, 0, 4, 6, 6.5f, 4.5f },
      -15, 6.5f, 10, 1.235f, 13, -2, -3.7f, 7.4f },
    /* BULBOUS: tall and round (a discus) */
    { { 13, 9, 1, -6, -11, -13, -11, -6, 1, 9 }, { 0, -11, -15, -14, -9, 0, 9, 14, 15, 11 },
      -9, 15, 12, 0.765f, 7, -4, -4, 6.5f },
};

/* clip a convex polygon (local coords) to the side of the line a->b where
 * (b - a) x (p - a) <= 0 - for a line drawn downward, the side toward the nose */
static int clip_side(const float *xs, const float *ys, int n, float ax, float ay, float bx, float by,
                     float *ox, float *oy) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float si = (bx - ax) * (ys[i] - ay) - (by - ay) * (xs[i] - ax);
        float sj = (bx - ax) * (ys[j] - ay) - (by - ay) * (xs[j] - ax);
        if (si <= 0) { ox[m] = xs[i]; oy[m] = ys[i]; m++; }
        if ((si <= 0) != (sj <= 0)) {
            float u = si / (si - sj);
            ox[m] = xs[i] + (xs[j] - xs[i]) * u; oy[m] = ys[i] + (ys[j] - ys[i]) * u; m++;
        }
    }
    return m;
}

/* a fish's fins as polygons in its local coords, behind-the-body order:
 * the arrangement grows in at ADULT (younger fish: the plain tail), the
 * elder's are longer. tail / lag: the beat's phase (0, 0 = the still pose
 * fish_fit measures). Returns the count (at most 4). */
typedef struct { int n; float x[4], y[4]; } fin_poly_t;
static int fish_fins(const fish_form_t *fm, int fins, stage_t stage, float tail, float lag, fin_poly_t *o) {
    const float H = fm->h, FH = fm->fh, K = fm->k, R = fm->root;
    bool elder = stage == STAGE_ELDER, grown = stage >= STAGE_ADULT;
    float tail_len = elder ? 1.22f : 1.0f;
    int m = 0;
    if (!grown) fins = FINS_CLASSIC;
#define FIN(N, LX, LY) do { const float lx_[N] = LX, ly_[N] = LY; o[m].n = N; \
        for (int i_ = 0; i_ < N; i_++) { o[m].x[i_] = lx_[i_]; o[m].y[i_] = ly_[i_]; } m++; } while (0)
#define L(...) { __VA_ARGS__ }
#define TL(d) (R - (d) * tail_len)                     /* a point d px behind the tail root, the elder's longer */
    if (fins == FINS_CLASSIC) {
        /* tail fin (triangle, flaps with tail phase) */
        FIN(3, L(R, (R - 15) * tail_len, (R - 15) * tail_len), L(0, -10 - tail * 8, 10 + tail * 8));
        /* dorsal crest: adults a small fin, elders a taller one */
        if (grown) { float b = -H * 8 / 10, d = b - FH * (elder ? 9 : 5) / 10;   /* exact for CLASSIC: -8, -13 / -17 */
                     FIN(3, L(6 * K, -2 * K, -9 * K), L(b, d, b)); }
    } else if (fins == FINS_VEIL) {
        /* a long two-lobed tail that trails the beat, a trailing anal fin, a low dorsal fold */
        FIN(4, L(R, TL(10), TL(30), TL(26)), L(0, -7 - tail * 3, -13 - lag * 9, -1 - lag * 3));
        FIN(4, L(R, TL(26), TL(32), TL(12)), L(0, 1 + lag * 3, 16 + lag * 9, 8 + tail * 3));
        float a = elder ? 1.3f : 1.0f;
        FIN(4, L(2 * K, -6 * K, -14 * K - lag * 2, -4 * K), L(0.9f * H, 0.9f * H, 0.9f * H + a * FH, 0.9f * H + 0.6f * FH));
        FIN(3, L(4 * K, -8 * K, -12 * K), L(-0.9f * H, -0.9f * H - (elder ? 0.6f : 0.35f) * FH, -0.8f * H));
    } else {
        /* SAIL: a small lyre tail; a tall swept-back dorsal sail and its mirror below */
        FIN(3, L(R, TL(11), TL(7)), L(0, -9 - tail * 5, 0));
        FIN(3, L(R, TL(11), TL(7)), L(0, 9 + tail * 5, 0));
        float e = elder ? 1.15f : 1.0f;
        FIN(4, L(8 * K, 0, -12 * K + lag * 1.5f, -12 * K), L(-0.85f * H, -0.85f * H - 1.45f * e * FH, -0.85f * H - 1.05f * e * FH, -0.7f * H));
        FIN(4, L(4 * K, -4 * K, -12 * K + lag * 1.5f, -12 * K), L(0.85f * H, 0.85f * H + 1.15f * e * FH, 0.85f * H + 0.85f * e * FH, 0.7f * H));
    }
#undef TL
#undef L
#undef FIN
    return m;
}

/* the trail's drag on the fins (0.4): s > 0 a burst - the tail streams
 * out long and narrow, a dorsal or anal fin lays back along the body; s < 0
 * the fish braking - the tail spreads, the tall fins swing a little forward.
 * d: the tail's lag through a climb or dive (the bend's gap) - a tall fin's
 * reach past the body leans by it too, as the tail does, so a sail is not a
 * board bolted on. The fins move, never the body they grow from */
static void fins_drag(const fish_form_t *fm, fin_poly_t *fp, int nf, float s, float d) {
    const float R = fm->root, edge = 0.9f * fm->h, as = fabsf(s);
    for (int k = 0; k < nf; k++)
        for (int i = 0; i < fp[k].n; i++) {
            float *x = &fp[k].x[i], *y = &fp[k].y[i];
            if (*x < R) {                                       /* the tail */
                *y *= 1 - 0.3f * s;
                *x += (*x - R) * 0.14f * s;
            } else if (fabsf(*y) > edge) {                      /* a tall fin, past the body's edge */
                float e = fabsf(*y) - edge;
                *x -= e * 0.55f * s + (*y > 0 ? e : -e) * d;
                *y -= (*y > 0 ? 1 : -1) * e * 0.18f * as;
            }
        }
}

/* the previews' fit (0.4): a page draws a fish in the room a CLASSIC fish of
 * the same stage takes (the milestones rows are 40 px apart - 34 on the bowl
 * - and a bulbous sail fish is ~2.5x a classic's height). The still pose's
 * box, scaled to fit inside a full-grown (ELDER) classic's (times `room`:
 * FISH_ROOM_* - a card has space a row does not) and centred where a classic
 * of its stage would be; never enlarged; a classic fish is untouched (no
 * shift - exact). */
static void fish_box(int shape, int fins, stage_t stage, float b[4]) {
    const fish_form_t *fm = &FORMS[shape];
    b[0] = b[2] = 1e9f; b[1] = b[3] = -1e9f;
    fin_poly_t fp[4]; int nf = fish_fins(fm, fins, stage, 0, 0, fp);
    for (int k = -1; k < nf; k++) {
        const float *xs = k < 0 ? fm->x : fp[k].x, *ys = k < 0 ? fm->y : fp[k].y;
        int n = k < 0 ? 10 : fp[k].n;
        for (int i = 0; i < n; i++) {
            b[0] = fminf(b[0], xs[i]); b[1] = fmaxf(b[1], xs[i]);
            b[2] = fminf(b[2], ys[i]); b[3] = fmaxf(b[3], ys[i]);
        }
    }
}
static void fish_fit(fish_t *f, float x, float y, float size, float room) {
    f->x = x; f->y = y; f->size = size;
    int sh = f->shape < SHAPE_N ? f->shape : 0, fi = f->fins < FINS_N ? f->fins : 0;
    if (sh == SHAPE_CLASSIC && fi == FINS_CLASSIC) return;
    float b[4], r[4], e[4];
    fish_box(sh, fi, f->stage, b); fish_box(SHAPE_CLASSIC, FINS_CLASSIC, f->stage, r);
    /* the room is a full-grown classic's - the ELDER's box, the biggest a
       classic ever is - not a classic of this fish's own stage: a fry only
       shrinks if it would really overflow (Strato 10-04: a bulbous sail fry
       fitted into a classic fry's box was a speck in its row) */
    fish_box(SHAPE_CLASSIC, FINS_CLASSIC, STAGE_ELDER, e);
    float k = fminf(1, room * fminf((e[1] - e[0]) / (b[1] - b[0]), (e[3] - e[2]) / (b[3] - b[2])));
    f->size = size * k;
    f->x = x + (r[0] + r[1]) * 0.5f * size - (b[0] + b[1]) * 0.5f * f->size;
    f->y = y + (r[2] + r[3]) * 0.5f * size - (b[2] + b[3]) * 0.5f * f->size;
}

/* The fish's body is a ledger of its life: stage sets size (tank.c) and fin
 * elaboration, hunger drains saturation, and earned markings stay:
 *   elder -> a longer tail and a taller dorsal. All procedural, zero flash.
 * Its genes (0.4) pick the body, the fins and the pattern; CLASSIC / CLASSIC
 * / BARS draws exactly what every fish drew before. */
/* `mark` scales the pattern: 1 in the tank (every stage wears the same
 * 2 x 5 px bars), the preview's size so it reads on a big fish */
/* `floor_y`: the sand line under a fish in the tank (FISH_FLOOR_Y; the
 * previews pass none) - nose up, a fish close to it lifts off level */
/* `beat`: the tail beat's phase - in the tank the fish's own (fish_t.beat, run on by the swim), in a preview the clock's at
 * the preview's pace (draw_still) */
static void draw_fish_core(ctx_t *c, const fish_t *f, float beat, bool asleep, float mark, float floor_y) {
    const fish_form_t *fm = &FORMS[f->shape < SHAPE_N ? f->shape : 0];
    const float H = fm->h;
    int fins = f->fins < FINS_N ? f->fins : 0;
    float ph = beat + f->wander, amp = 0.32f + f->speed * 0.007f;
    float tail = sinf(ph) * amp;
    float lag = sinf(ph - 1.1f) * amp;                 /* the long fins follow the beat a moment late */
    float stress = f->stress / 10.0f;
    float pale = f->hunger > 7 ? (f->hunger - 7) / 3.0f * 0.35f : 0;   /* hungry = washed out */
    /* pitch: the heading folded to face right (the climb or dive), then the
     * yaw scales x: X = s(lx) * (lx cos p - ly sin p), Y = lx sin p + ly cos p.
     * The BODY tilts less than the path: a fish rises and sinks on its fins
     * with its back near level, so the travel angle is eased toward a cap of
     * ~35 deg (~55 at a dash) - never nose-straight-up (Strato 09-29: "too
     * much time in a vertical position") */
    float pmax = 0.61f + 0.35f * fminf(fmaxf((f->speed - 35) * (1 / 40.0f), 0), 1);
    /* the trail (0.4, tank_fish_trail): the body's pitch a moment behind the
     * heading's, the tail's further behind it - drawn as a bend of the rear
     * body (below). The springs' gaps are read against the heading, so a
     * fish posed by hand (a page, a card) is never drawn off its heading */
    float q = atan2f(sinf(f->heading), fabsf(cosf(f->heading)));
    float qb = q + fminf(fmaxf(f->pitch_b - q, -0.6f), 0.6f), qt = qb + fminf(fmaxf(f->pitch_t - f->pitch_b, -0.55f), 0.55f);
    float p = pmax * tanhf(0.8f * qb / pmax), pt = pmax * tanhf(0.8f * qt / pmax);
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
        k = k < 0 ? 0 : k > 1 ? 1 : k;
        p *= k; pt *= k;
    }
    float cp = cosf(p), sp = sinf(p);
    /* the bend: behind bb the spine arcs, its angle growing to the gap
     * (pt - p) at the body's tail end and on along a long tail (bend_x/y) */
    float tr = tank_form_trail(f);
    bend_t bd = { fm->x[0] * 0.25f, fm->x[5], 0, 0.7f * tr };
    bd.k = (pt - p) / (bd.bb - bd.e);
    /* a burst streams the long fins back, a stop lets them billow forward */
    float surge = fminf(fmaxf((f->speed - f->speed_t) * (1 / 20.0f), -1), 1) * tr;
    float yh = yaw_ease(f->yaw), yt = yaw_ease(f->yaw_tail);
    uint32_t body = mix(mix(f->color, 0xf25b65, stress * 0.22f), 0x7a8a8e, pale);
    uint32_t fin  = mix(mix(f->fin, 0xf25b65, stress * 0.18f), 0x5a6a6e, pale);
#define BX(lx, ly) bend_x(&bd, lx, ly)
#define BY(lx, ly) bend_y(&bd, lx, ly)
#define TX(lx, ly) (f->x + yaw_at(yh, yt, lx) * (BX(lx, ly) * cp - BY(lx, ly) * sp) * f->size)
#define TY(lx, ly) (f->y + (BX(lx, ly) * sp + BY(lx, ly) * cp) * f->size)
    {
        fin_poly_t fp[4]; int nf = fish_fins(fm, fins, f->stage, tail, lag, fp);
        if (surge != 0 || pt != p) fins_drag(fm, fp, nf, surge, (pt - p) * tr);
        for (int k = 0; k < nf; k++) {
            float xs[4], ys[4];
            for (int i = 0; i < fp[k].n; i++) { xs[i] = TX(fp[k].x[i], fp[k].y[i]); ys[i] = TY(fp[k].x[i], fp[k].y[i]); }
            fill_poly(c, xs, ys, fp[k].n, fin);
        }
    }
    /* body */
    {
        float xs[10], ys[10];
        for (int i = 0; i < 10; i++) { xs[i] = TX(fm->x[i], fm->y[i]); ys[i] = TY(fm->x[i], fm->y[i]); }
        fill_poly(c, xs, ys, 10, body);
        /* its thickness: the cross-section at the deepest point, widening as
         * the fish turns toward the glass (sin of the yaw angle) */
        float sm = yaw_at(yh, yt, 2), rx = FISH_THICK * f->size * sqrtf(fmaxf(0, 1 - sm * sm));
        if (rx >= 1) fill_ellipse(c, TX(2, 0), TY(2, 0), rx, 9.5f * (H / 10) * f->size, body, 255);
    }
    /* the pattern, in the accent color. A FRY has none yet: its markings
     * come in with its first growth spurt */
    if (f->stage >= STAGE_JUV && f->pattern != PATTERN_SOLID) {   /* SOLID: no markings at all */
        int pat = f->pattern < PATTERN_N ? f->pattern : 0;
        if (pat == PATTERN_BARS) {
            /* on the flank, so they narrow with the turn and fade as the fish
             * comes head-on (seen edge-on, a flank shows nothing) */
            for (int i = -1; i <= 1; i++) {
                float lx = fm->bar_x + i * fm->bar_dx, s = fabsf(yaw_at(yh, yt, lx));
                if (s > 0.25f)
                    fill_ellipse(c, TX(lx, 0), TY(lx, 0), 2 * mark * s, 0.5f * H * mark,
                                 f->accent, (int)(150 * (s - 0.25f) / 0.75f));
            }
        } else {
            /* MASK: the face, the body cut along a curved gill edge just
             * behind the eye (a convex cut of a convex body: four half-planes).
             * EYE BAND: a slanted band through the eye, the body cut between
             * two lines. Neither fades with the turn: the face shows head-on. */
            /* the cut lines, each a -> b: the side kept is the nose's for a
             * line drawn downward, the tail's for one drawn upward */
            float ex = fm->eye_x, cut[4][4]; int nc;
            if (pat == PATTERN_MASK) {
                float gx = ex - 5;
                const float px[5] = { gx, gx - 3, gx - 3.5f, gx - 3, gx }, py[5] = { -1.3f * H, -0.4f * H, 0, 0.4f * H, 1.3f * H };
                for (nc = 0; nc < 4; nc++) { cut[nc][0] = px[nc]; cut[nc][1] = py[nc]; cut[nc][2] = px[nc + 1]; cut[nc][3] = py[nc + 1]; }
            } else {
                /* 5.5 px wide, leaning back, the eye inside it */
                const float back[4]  = { ex - 0.5f, -1.3f * H, ex - 6, 1.3f * H };     /* down: keep the nose side */
                const float front[4] = { ex - 0.5f, 1.3f * H, ex + 5, -1.3f * H };     /* up: keep the tail side */
                memcpy(cut[0], back, sizeof back); memcpy(cut[1], front, sizeof front); nc = 2;
            }
            float ax[16], ay[16], bx[16], by[16]; int n = 10;
            memcpy(ax, fm->x, sizeof fm->x); memcpy(ay, fm->y, sizeof fm->y);
            for (int k = 0; k < nc && n >= 3; k++) {
                n = clip_side(ax, ay, n, cut[k][0], cut[k][1], cut[k][2], cut[k][3], bx, by);
                memcpy(ax, bx, sizeof(float) * n); memcpy(ay, by, sizeof(float) * n);
            }
            if (n >= 3) {
                for (int i = 0; i < n; i++) { float lx = ax[i], ly = ay[i]; ax[i] = TX(lx, ly); ay[i] = TY(lx, ly); }
                fill_poly_a(c, ax, ay, n, f->accent, 235);
            }
        }
    }
    /* eye — closed to a lid line when asleep (resting at night) */
    float ex = fm->eye_x, ey = fm->eye_y;
    if (asleep) {
        fill_ellipse(c, TX(ex, ey), TY(ex, ey), 2.2f, 0.7f, 0x9fb4b8, 200);
    } else {
        fill_ellipse(c, TX(ex, ey), TY(ex, ey), 2.2f, 2.2f, 0xffffff, 255);
        fill_ellipse(c, TX(ex + 0.7f, ey), TY(ex + 0.7f, ey), 1.1f, 1.1f, 0x031015, 255);
    }
#undef TX
#undef TY
#undef BX
#undef BY
}
/* the tank's fish (the previews below draw a fish of their own) */
static void link_badge_draw(ctx_t *c, int cx, int cy, int r, int badge, int a);   /* the link's badges, at the end */
static bool g_no_animals;                        /* THE EDITOR (0.3.9.4, Strato): a placement page up - every creature hidden, the frame rate to the drag */
static bool g_page_animals;                      /* (the tests: the creatures drawn under a placement page all the same - the layering checks) */
void render_debug_page_animals(bool show) { g_page_animals = show; }
static void draw_fish(ctx_t *c, const tank_t *t, const fish_t *f) {
    if (g_no_animals) return;
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
    if (sink > 0) { fish_t g = *f; g.y -= sink; draw_fish_core(&cf, &g, f->beat, asleep, 1.0f, FISH_FLOOR_Y); return; }
    draw_fish_core(&cf, f, f->beat, asleep, 1.0f, FISH_FLOOR_Y);
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
    if (g_no_animals) return;
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
float render_fish_beat(const tank_t *t, int i) { return t->fish[i].beat + t->fish[i].wander; }   /* (draw_fish_core's) */
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
    g_ctx_other++;                                   /* (written with no draw target: a kept keeper page is gone) */
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
    int xc = g_span_xc >= 0 ? g_span_xc : (x0 + x1) >> 1;   /* the span's middle as drawn WHOLE - the frond row's, not the run's (0.3.9.4: a span clipped to a
                                                               box or split by a piece - THE PATCH - keeps the vignette the whole span has); on the glass */
    if (xc < 0) xc = 0;
    if (xc > TANK_W - 1) xc = TANK_W - 1;
    if (x0 < c->ox) x0 = c->ox;
    if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
    if (x0 > x1) return;
    TOUCH_SPAN(c, x0, x1, y);
    uint16_t *p = &CTX_PX(c, x0, y);
    /* ONE vignette alpha per span, computed (a 5 px frond span changes it by
       < 4%, invisible) rather than read: the PSRAM LUT costs a cache line
       per span and fronds are vertical, which was most of the frond cost */
    int a = g_veg_bench_dry >= 2 ? 0 : vig_alpha_span(xc, y);
    uint32_t l = g_pk_lx >= 0 ? pk_light_at(xc, y) : 0;   /* the lantern's light, one per span likewise */
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
/* THE BACKGROUNDS (0.4.1, backgrounds.h): the water is the background's palette - Open Water's is the tank's own
 * gradient, to the bit. g_bg is the tank's background (tank_t.bg_now), taken at the top of render_tank. */
static int g_bg, g_bg_force = -1;
void render_debug_force_bg(int bg) { g_bg_force = bg >= 0 && bg < BG_N ? bg : -1; }   /* (the sim's --bg: every tank it draws, the selftests' too) */
static inline uint32_t water_rgb(int y) { return bg_water_rgb(g_bg, y); }

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
static int bed_z(const tank_t *t, int b) { return tank_bed_z(t, b); }   /* BACK or FRONT (0.3.9.3: no weave) */
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
/* the CONTACT SHADOW (pc_contact_shadow, below): a piece's silhouette shifted down and to the right darkens what is behind it in the
   scene - PC_SHADOW_NEAR px over, hard; PC_SHADOW_FAR, soft. A frond painted over that rim takes the same shadow (veg_rim_shade): the
   piece is in front of it, so its shadow falls on the frond too (0.3.9.4, Strato on the square: "grass clicking on the edges" of the
   pumpkin - the grass loop, the live grass and the sword plant all skipped the piece's BODY and painted bright over its shadow's rim, a
   1-2 px strip down its right side that came and went with the sway) */
#define PC_SHADOW_A_NEAR 72                /* (the offsets PC_SHADOW_NEAR / _FAR are render.h's: the tests read them) */
#define PC_SHADOW_A_FAR  34
static uint32_t g_veg_rim_n, g_veg_rim_f; static int g_veg_rim_x0;   /* this span's rim bits (near, far) from g_veg_rim_x0 on; 0 off the rim */
static bool pc_covers(int k, int x, int y);
static bool pc_in_box_rim(int k, int x0, int x1, int y);   /* the span meets piece k's box, its contact shadow's rim included */
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
    int xc = g_span_xc >= 0 ? g_span_xc : (x0 + x1) >> 1;   /* (the whole span's middle, as span_final) */
    if (xc < 0) xc = 0;
    if (xc > TANK_W - 1) xc = TANK_W - 1;
    if (x0 < c->ox) x0 = c->ox;
    if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
    if (x0 > x1) return;
    TOUCH_SPAN(c, x0, x1, y); TOUCH_SPAN(c, x0, x1, y - 1);
    int a = vig_alpha_span(xc, y);
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
static inline void veg_put_run(ctx_t *c, int x0, int x1, int y, const src_t *s, bool final, bool arch);
static inline void veg_put(ctx_t *c, int x0, int x1, int y, const src_t *s, bool final, bool arch) {
    if (g_fbg_on && (unsigned)y < (unsigned)TANK_H) {   /* SWIM DEPTH: a bed IN FRONT keeps off the fish in front of it (and its even leaves off those
                                                          going through) - the span in its unguarded runs */
        const uint32_t *rf = g_fbg_front + y * DIRTY_WORDS_PER_ROW, *rt = g_fbg_thru + y * DIRTY_WORDS_PER_ROW;
        if (x0 < 0) x0 = 0;
        if (x1 > TANK_W - 1) x1 = TANK_W - 1;
        int run = -1;
        for (int x = x0; x <= x1 + 1; x++) {
            bool gd = x <= x1 && ((rf[x >> 5] >> (x & 31) & 1) || (!g_veg_odd && (rt[x >> 5] >> (x & 31) & 1)));
            if (x > x1 || gd) { if (run >= 0) { veg_put_run(c, run, x - 1, y, s, final, arch); run = -1; } }
            else if (run < 0) run = x;
        }
        return;
    }
    veg_put_run(c, x0, x1, y, s, final, arch);
}
/* the piece's contact shadow over the frond's pixels in its rim (the run as painted: a pixel the run left to a fish is left alone) */
static bool g_rim_off;                    /* (the A/B, render_debug_rim_off: the fronds bright over the rim, as before 0.3.9.4) */
void render_debug_rim_off(bool off) { g_rim_off = off; }
static inline void veg_rim_shade(ctx_t *c, int x0, int x1, int y) {
    if (!(g_veg_rim_n | g_veg_rim_f) || g_rim_off) return;
    if ((unsigned)(y - c->oy) >= (unsigned)c->h) return;
    if (x0 < c->ox) x0 = c->ox;
    if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
    for (int x = x0; x <= x1; x++) {
        unsigned i = (unsigned)(x - g_veg_rim_x0);
        if (i >= 32) continue;
        if (g_veg_rim_n >> i & 1) px_darken(&CTX_PX(c, x, y), PC_SHADOW_A_NEAR);
        else if (g_veg_rim_f >> i & 1) px_darken(&CTX_PX(c, x, y), PC_SHADOW_A_FAR);
    }
}
static inline void veg_put_run(ctx_t *c, int x0, int x1, int y, const src_t *s, bool final, bool arch) {
    if (g_veg_bench_dry >= 3) return;
    bg_mark(x0, x1, y);
    if (final && g_veg_arch_pass >= 0) {
        span_final(c, x0, x1, y, s, 255);
        if (arch) {
            if (g_veg_arch_shade && g_veg_arch_pass == 0) veg_arch_shade(c, x0, x1, y);   /* (the late pass never enters the arch) */
            else dirty_clear_span(x0, x1, y);         /* a bed IN FRONT across the opening: final, so the arch's dirty pass must not shade it over a crab or a fish */
        }
    } else span(c, x0, x1, y, s, 255);               /* the early arch pass: marked, so the arch's shade finds it (2026-10-23) and the sweep vignettes it */
    veg_rim_shade(c, x0, x1, y);
}
static inline void veg_span_runs(ctx_t *c, int x0, int x1, int y, const src_t *s, bool final);
static inline void veg_span(ctx_t *c, int x0, int x1, int y, const src_t *s, bool final) {   /* one frond row: its runs, all with the span's vignette */
    int xc = (x0 + x1) >> 1;
    if (xc < 0) xc = 0;
    if (xc > TANK_W - 1) xc = TANK_W - 1;
    g_span_xc = xc;
    if (g_veg_rowclip) {                                                  /* THE PATCH: only the row's stale columns (a box's repaint follows the piece's footprint) */
        const uint16_t *iv = g_veg_rowclip(y);
        if (iv) for (int q = 0; q < 4; q += 2) {
            const int a = x0 > iv[q] ? x0 : iv[q], b = x1 < iv[q + 1] ? x1 : iv[q + 1];
            if (a <= b) veg_span_runs(c, a, b, y, s, final);
        }
    } else veg_span_runs(c, x0, x1, y, s, final);
    g_span_xc = -1;
}
static inline void veg_span_runs(ctx_t *c, int x0, int x1, int y, const src_t *s, bool final) {
    if ((unsigned)y < (unsigned)TANK_H && !g_veg_row_near[y]) {   /* this row meets no piece and no arch (draw_veg's table): the span, or nothing in the early arch pass */
        if (g_veg_arch_pass >= 0) veg_put(c, x0, x1, y, s, final, false);
        return;
    }
    uint32_t near = 0;
    for (int k = 0; k < g_npc; k++) if (((g_veg_skip >> k) & 1) && pc_in_box_rim(k, x0, x1, y)) near |= 1u << k;
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
    uint32_t m = 0, rn = 0, rf = 0, full = n < 32 ? (1u << n) - 1 : 0xffffffffu;
    if (n <= 32) {
        for (int k = 0; k < g_npc; k++) if ((near >> k) & 1) {
            m |= pc_row_bits(k, x0, n, y);
            rn |= pc_row_bits(k, x0 - PC_SHADOW_NEAR, n, y - PC_SHADOW_NEAR);   /* the piece's contact shadow: where its body stands up and to the left */
            rf |= pc_row_bits(k, x0 - PC_SHADOW_FAR, n, y - PC_SHADOW_FAR);
        }
        if (g_veg_arch_pass && arch_box)                 /* the arch: the early pass draws only it, the late one skips it */
            for (int i = 0; i < n; i++) { bool a = castle_arch(x0 + i, y); if (g_veg_arch_pass > 0 ? a : !a) m |= 1u << i; }
        rn &= ~m & full; rf &= ~m & ~rn & full;
        g_veg_rim_x0 = x0; g_veg_rim_n = rn; g_veg_rim_f = rf;   /* (veg_rim_shade, after each run) */
        if (!m) { veg_put(c, x0, x1, y, s, final, arch_box); g_veg_rim_n = g_veg_rim_f = 0; return; }
        if (m == full) { g_veg_rim_n = g_veg_rim_f = 0; return; }
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
    g_veg_rim_n = g_veg_rim_f = 0;
}
/* layer: 0 = the even fronds, 1 = the odd ones, -1 = every frond */
static const int VEG_SEED[VEG_BEDS_MAX] = { 0, 7, 3, 5 };   /* each bed's sway phase (the seahorse's blade reads it too) */
/* g_veg_row_near (declared with veg_span above): per row of the bed being drawn: the pieces whose boxes its spans can meet (bits by
                                                              * piece), and bit 7 = the row crosses a castle's open arch (draw_veg fills it once a
                                                              * bed; veg_span reads it: most rows meet nothing and take the fast path - 2026-10-23) */
static int g_veg_force_layer = -1;                 /* (EXPLORATION 10-31, the sim's --depth-shot: only the even or the odd fronds) */
void render_proto_veg_layer(int layer) { g_veg_force_layer = layer; }
/* THE HELD HEIGHTS (0.3.9.4, the flicker on Strato's square and 1.8): while the grass loop is up, EVERY drawing of the back grass -
   its bands and moments, a frame drawn live while the loop is stale or parked, a box repainted, the seahorse's blade - takes the
   frond heights the loop was last baked toward (g_gl_seg, the stale event's), not the tank's of this frame. Before, a live frame
   between band frames drew the current heights, and a frond the urchin had nibbled since the bake jumped 3-4 px every band
   frame for as long as a rebake took. A height change shows when the loop takes it (at once for a trim, GL_SEG_WAIT_S else). */
static const uint8_t (*g_veg_seg_hold)[VEG_FRONDS_MAX];
#define GL_SEG_JUMP   4                           /* (the loop's rule, below: a frond this many segments off is taken at once - the hold reaches no further,
                                                     so another tank drawn under the same loop, the sim's tests' bare copies, keeps its own heights) */
static inline int veg_segs_of(const tank_t *t, int b, int i, float *bx) {
    int sg = tank_veg_frond(t, b, i, bx);
    if (g_veg_seg_hold && b < VEG_BEDS_MAX && i < VEG_FRONDS_MAX && bed_z(t, b) == DECOR_Z_BACK) {
        const int h = g_veg_seg_hold[b][i], d = h > sg ? h - sg : sg - h;
        if (d < GL_SEG_JUMP) sg = h;
    }
    return sg;
}
static void draw_veg(ctx_t *c, const tank_t *t, int b, int seed, int layer, bool final) {
    if (!g_veg_step) return;
    if (g_veg_force_layer >= 0) layer = g_veg_force_layer;                         /* director `vegstep 0`: the A/B of the grass's whole cost */
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
          for (int k = 0; k < g_npc; k++) if (((g_veg_skip >> k) & 1) && pc_in_box_rim(k, xa, xb, y)) m |= (uint8_t)(1u << k);
          if (g_veg_mask_cx >= 0 && y <= CASTLE_FY && y >= CASTLE_FY - CASTLE_ARCH_S - CASTLE_ARCH_R
              && xb >= g_veg_mask_cx - CASTLE_ARCH_R && xa <= g_veg_mask_cx + CASTLE_ARCH_R) m |= 0x80;
          g_veg_row_near[y] = m;
      } }
    for (int i = 0; i < n; i++) {
        if (layer >= 0 && (i & 1) != layer) continue;
        g_veg_odd = i & 1;                         /* (SWIM DEPTH: its pixels into g_om too, when recording) */
        if (g_veg_thin && b != 3 && i % 3 == 2) continue;   /* (the cost probe: a sparser bed) */
        float bx;
        /* each frond's own height (tank_t.veg_h): what the keeper cut is
           exactly what shows - no render-side variation on top */
        int segs = veg_segs_of(t, b, i, &bx);
        if (g_veg_arch_pass < 0 && (bx + 14 < g_veg_mask_cx - CASTLE_ARCH_R || bx - 14 > g_veg_mask_cx + CASTLE_ARCH_R)) continue;   /* the early arch pass: only the fronds that reach the arch */
        if (g_veg_cull && (bx + GL_BED_PAD < c->ox || bx - GL_BED_PAD > c->ox + c->w - 1)) continue;   /* THE PATCH: a frond clear of the box */
        if (segs < 1) segs = 1;                    /* nubs: always a bit of green */
        if (segs > VEG_MAX_SEGS) segs = VEG_MAX_SEGS;
        const float vc = veg_clock(t->clock) * VEG_W;
        float sway = fast_sin(vc + (i + seed) * 1.7f) * amp;
        /* the swaying chain of segment centres (the frond's spine) */
        float xs[VEG_MAX_SEGS + 1];
        for (int seg = 0; seg <= segs; seg++)
            xs[seg] = bx + sway * seg / (float)segs * fast_sin(seg * 0.4f + vc + i + seed);
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
            if (g_veg_cull && (y + 1 < c->oy || y - 1 > c->oy + c->h - 1)) continue;   /* THE PATCH: a row clear of the box (the walk above has moved on) */
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
#define DPC_W 80                                          /* THE CRAB'S STACK's mask (dpc_m, below; render_tank) */
#define DPC_H 64
typedef struct {
    uint8_t  co_sprite[CORAL_CH][CORAL_CW];
    uint16_t co_row[CO_N][CORAL_H];           /* [tone][row], row 0 = CORAL_FY - CORAL_H */
    int8_t   co_sd[CORAL_CH][CORAL_CW];
    uint8_t  co_tp[CORAL_CH][(CORAL_CW + 7) / 8];
    uint8_t  cl_sprite[CL_CH][CL_CW];
    uint16_t cl_row[CL_TONES][CL_H];
    int8_t   cl_sd[CL_CH][CL_CW];
    uint8_t  cl_dp[CL_CH][(CL_CW + 7) / 8];
    uint32_t pm[5][VO_H + 4][(CASTLE_MASK_W + 31) / 32];           /* the pieces' masks (2026-10-17): what each piece covers, by its ITEM's slot (pc_mslot:
                                                                      the castle, the coral, the cluster, the pumpkin, the volcano - 0.3.9.4: by its place in
                                                                      the list before, which shifted under a baked piece when a piece ahead of it went live)
                                                                    * in the baked scene (sprite + mound); row 0 = its box's top, bit 0 its left
                                                                    * (the volcano, 0.3.9, is the tallest box) */
    uint32_t pm_live[VO_H + 4][(CASTLE_MASK_W + 31) / 32];        /* the page's LIVE piece's mask, this frame (0.4): apart from its baked one in pm, so a
                                                                      CANCEL leaves the scene's masks as the scene has them (the coral-sliver lesson) */
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
    uint32_t dpc_m[DPC_H][(DPC_W + 31) / 32];               /* THE CRAB'S STACK's pixel mask (render_tank's crab in a castle's arch; was .bss until the 0.4.1 audit) */
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
            if (keep_at(ix, iy)) continue;                                      /* a fish in front of the pieces */
            if (bed_over(g_clip_k, ix, iy)) continue;                           /* the sword plant ranked above it */
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
                if (pc_covered_above(kc, x, y) || keep_at(x, y)) continue;   /* (a fish in front of the castle: no tunnel shade on it) */
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
static void pk_casts_pieces(const tank_t *t, int px0, int px1);   /* (with the pieces, below) */
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
    pk_casts_pieces(t, px0, px1);
}
static bool g_baking;                                     /* bake_scene paints the pieces: a piece's mask is this bake's only once it has been painted */
static const tank_t *g_guard;                             /* the critters in the FRONT SLOT (before a piece IN FRONT; the crab in a front volcano's cave, on its
                                                             flank): the per-frame overlays - the volcano's lava and embers, the pumpkin's face and flame - keep off
                                                             them (docs/render-stack.md; the matrix found the pumpkin's face painted over a crab walking past; 0.3.9.1,
                                                             Strato on the 1.8: the lava in the cave over the urchin walking past it - the snail and the urchin too) */
enum { GUARD_CRAB = 1, GUARD_SNAIL = 2, GUARD_URCHIN = 4 };
static unsigned g_guard_who;                              /* ... which of them */
static bool slot_covers(int x, int y);
static inline void pk_store(ctx_t *c, int x, int y, uint16_t v, bool final) {
    if (!CTX_IN(c, x, y) || under_bg(x, y)) return;
    if (g_pk_k >= 0 && !g_baking && pc_covered_above(g_pk_k, x, y)) return;   /* under a piece above it (the castle's stone, a coral IN FRONT, ...) - never
                                                                    in the bake: there the pieces above are painted after it, and their masks are still the LAST bake's
                                                                    (a coral or a cluster a growth step smaller left a hole the water showed through: bake_scene) */
    if (g_guard && slot_covers(x, y)) return;                    /* a critter in the front slot before the lantern: its face stays under it */
    if (bed_over(g_pk_k, x, y)) return;                          /* the sword plant ranked above it: its leaves stay over the face */
    if (keep_at(x, y)) return;                                   /* a fish in front of the pieces (SWIM DEPTH phase 2) */
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
    if (bed_over(g_clip_k, x, y)) return;                            /* the sword plant ranked above it */
    if (keep_at(x, y)) return;                                       /* a fish in front of the pieces */
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
static void draw_volcano_bubbles(ctx_t *c, int cx, double clock, bool night, int *bx0, int *by0, int *bx1, int *by1) {
    ctx_t ce = *c;
    if (night) ce.dim = 1.0f;
    float py = VO_FY - VO_H + (VO_CH - VO_POOL_Y) * VO_CELL;
    const float an = tank_anim(clock);
    for (int k = 0; k < VO_NB; k++) {
        float P = 2.9f + 0.41f * k, ph = (float)fmod(clock / P + k * 0.618f, 1.0);   /* (each on its own period: from the double) */
        float y = py - ph * VO_RISE;
        float x = cx + (k % 3 - 1) * 5.0f * ph + 3.5f * ph * fast_sin(an * 1.3f + k * 2.1f + 8 * TAU);
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
    if (g_no_animals) return;
    if (!(t->sd_unlocks & SD_ITEM_SNAIL) || t->snail_x < 0) return;
    bool upright = tank_snail_upright(t);
    if (upright != upright_pass) return;
    int ox = (int)t->snail_x, oy = (int)(t->snail_y + SNAIL_SOLE_DY);
    sn_row_t row = { .y = -(1 << 20) };
    if (upright) {
        bool flip = cosf(t->snail_heading) < 0;
        const float a = anim(t);
        sn_sway_t sw = { 0.6f * fast_sin(a * 2.4f), fast_sin(a * 1.7f), fast_sin(a * 1.7f + 2.2f) };
        float top[37];                                             /* the foot's ridge per column, then row by row (one water colour a row) */
        for (int lx = -16; lx <= 20; lx++) {
            float xf = (lx + 15) / 28.5f;
            top[lx + 16] = -(2.4f + 3.2f * powf(sinf(xf < 0 ? 0 : xf > 1 ? 1 : xf * 3.14159f), 0.6f));
            if (lx > 8) { float h = -(4.8f + (lx - 8) * 0.55f); if (h < top[lx + 16]) top[lx + 16] = h; }
        }
        static double s_t = -1e9;                                  /* its tones a few times a second, not a frame (2026-10-23) */
        uint8_t *tn = ds()->sn_tones;
        if (t->clock - s_t >= 0.08f || t->clock < s_t) {
            for (int ly = -18; ly <= 0; ly++)
                for (int lx = -16; lx <= 20; lx++) tn[(ly + 18) * 37 + lx + 16] = (uint8_t)snail_upright_tone(lx, ly, top[lx + 16], a, &sw);
            s_t = t->clock;
        }
        for (int ly = -18; ly <= 0; ly++)
            for (int lx = -16; lx <= 20; lx++)
                snail_put(c, flip ? ox - lx : ox + lx, oy + ly, tn[(ly + 18) * 37 + lx + 16], SNAIL_TINT_FLOOR, &row);
    } else {
        float a = t->snail_heading - 1.5708f;                      /* the head points +y locally */
        float ca = cosf(a), sa = sinf(a);
        int cx = (int)t->snail_x, cy = (int)t->snail_y;
        const float an = anim(t);
        float stalk_y[2] = { 11.6f + 0.4f * fast_sin(an * 2 + -1 + TAU), 11.6f + 0.4f * fast_sin(an * 2 + 1 + TAU) };
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
                    k = snail_glass_tone(lx, ly, (float)dx, (float)dy, an, stalk_y);
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
    if (g_no_animals) return;
    if (!(t->sd_unlocks & SD_ITEM_URCHIN) || t->urchin_x < 0) return;
    /* its tones a few times a second, not a frame (2026-10-23: the spines' pose per pixel
       was 2-3 ms of a square frame); the row's palette once a row, not a pixel */
    static double s_t = -1e9; static bool s_chew;
    bool chew = tank_urchin_chewing(t);
    uint8_t *tn = ds()->ur_tones;
    if (t->clock - s_t >= 0.08f || t->clock < s_t || chew != s_chew) {
        ur_pose_t p; urchin_pose(&p, anim(t), chew);
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
        cr_pose_roost(p, t->cr_find > 0 ? 0 : settle, stretch, poke, fast_sin(anim(t) * 0.9f + 4 * TAU) > 0.35f);
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
static bool g_dpc_on;                                     /* (THE CRAB'S STACK in an animal's put-back: the crab's pixels as draw_crab paints them in a
                                                             castle IN FRONT's arch, recorded once a frame - dep_draw reads bits, not the painter's mapping) */
static int g_dpc_x0, g_dpc_y0;                            /* (its mask, DPC_W x DPC_H bits: ds()->dpc_m, PSRAM - 0.4.1 audit F3: 768 B off internal RAM) */
static inline void dpc_mark(int x, int y) {
    const int lx = x - g_dpc_x0, ly = y - g_dpc_y0;
    if ((unsigned)lx < DPC_W && (unsigned)ly < DPC_H) ds()->dpc_m[ly][lx >> 5] |= 1u << (lx & 31);
}
static bool g_dpc_ok; static const tank_t *g_dpc_t;   /* (the mask holds this frame's crab: from its draw to the front overlays) */
static inline uint32_t dpc_word(int xw, int y) {          /* the mask's 32 bits for x = xw..xw+31, bit b = x xw+b */
    const int ly = y - g_dpc_y0, lx = xw - g_dpc_x0;
    if ((unsigned)ly >= DPC_H || lx <= -32 || lx >= DPC_W) return 0;
    const uint32_t *r = ds()->dpc_m[ly];
    const int wi = lx >= 0 ? lx >> 5 : -1, sh = lx - (wi << 5);   /* (lx in -31..-1: word -1, sh = lx + 32) */
    const uint32_t lo = (wi >= 0 && wi < (DPC_W + 31) / 32) ? r[wi] : 0, hi = (wi + 1 >= 0 && wi + 1 < (DPC_W + 31) / 32) ? r[wi + 1] : 0;
    return sh ? (lo >> sh) | (hi << (32 - sh)) : lo;
}
static inline bool dpc_at(int x, int y) {
    const int lx = x - g_dpc_x0, ly = y - g_dpc_y0;
    return (unsigned)lx < DPC_W && (unsigned)ly < DPC_H && (ds()->dpc_m[ly][lx >> 5] >> (lx & 31) & 1);
}
static uint32_t g_cr_clip;                                /* THE CRAB'S STACK (crab_stack): the pieces before its shelter - it is not painted under them */
static bool cr_clipped(int x, int y);                     /* (below, with the pieces' masks) */
static void draw_crab(ctx_t *c, const tank_t *t) {
    if (g_no_animals) return;
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
                if (g_cr_clip && cr_clipped(x, y)) continue;
                if (g_dpc_on) dpc_mark(x, y);
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
            if (g_cr_clip && cr_clipped(x, y)) continue;
            if (g_dpc_on) dpc_mark(x, y);
            px_blend_s(c, x, y, &pal[tone], 255);
        }
    }
    if (t->cr_find > 0) cr_draw_disc(c, t, ox, oy - (CR_GH + CR_DISC_UP) * sc, sc, NULL, 0, 0);   /* the sand dollar over its head, scaled with it (never tilted: a find is a dig's) */
}

/* does the crab, as draw_crab painted it this frame (g_cr_tn: the grid it drew), cover (x, y)?
   The painter's two mappings run backwards: the 1:1 blit, or the sampler's rotation / scale about its feet. */
static bool crab_covers(const tank_t *t, int x, int y) {
    if (g_dpc_ok && t == g_dpc_t) return dpc_at(x, y);   /* the crab in a castle IN FRONT's arch, as draw_crab painted it this frame: the bits, not the
                                                            painter's mapping run backwards (its trig) for every lava cell and restored pixel - where a
                                                            piece above the castle clips it, that piece covers the lava and the restore anyway */
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
static uint8_t sh_tone(float lx, float ly, float t, float fin, const decor_scratch_t *sc, int k, float nod, const sh_row_t *row, float jut) {
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
            float L = 5.6f * (1 - 0.45f * a * a) + 0.5f * sinf(fin + a * 2.5f);
            if (rr <= L) {
                float ray = fmodf((ang - 180) / 14.0f + 0.25f * sinf(fin + rr) + 8.0f, 1.0f);
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
static void sh_sprite(decor_scratch_t *sc, uint8_t *out, float t, int pose, float u, float SX, float SW, float fin, float jut) {   /* t: tank_anim's; fin: the dorsal fin's phase */
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
        for (int x = 0; x < SH_W; x++) out[y * SH_W + x] = sh_tone(x + 0.5f, y + 0.5f, t, fin, sc, y * SH_W + x, nod, &row, jut);
    }
}
/* the sprite is rebuilt at ~10 Hz, not every frame (its animation - the nod,
 * the fin, the tail's breathing - is slow; a landing's morph is 0.7 s), and
 * at once when the pose or the morph moves: the build is the dear part on
 * the S3 (a sine, a power or a root for most pixels) */
static float g_sh_built_t = -1, g_sh_built_u = -1, g_sh_built_sw = -1, g_sh_built_jut = 0; static int g_sh_built_pose = -1;
static void sh_sprite_cached(decor_scratch_t *sc, float t, int pose, float u, float SX, float SW, float fin, float jut) {
    bool stale = g_sh_built_pose != pose || fabsf(g_sh_built_u - u) > 0.04f || fabsf(g_sh_built_sw - SW) > 0.1f
              || fabsf(g_sh_built_jut - jut) > 0.4f || t - g_sh_built_t >= 0.1f || t < g_sh_built_t;
    if (!stale) return;
    sh_sprite(sc, sc->sh_spr, t, pose, u, SX, SW, fin, jut);
    g_sh_built_t = t; g_sh_built_u = u; g_sh_built_sw = SW; g_sh_built_pose = pose; g_sh_built_jut = jut;
}
/* a blade's ribbon at a tank row, exactly as draw_veg lays it (its sway and
 * taper): the centre x and the half-width; false off the blade */
static bool veg_ribbon_at(const tank_t *t, int b, int i, int y, float moment, float *cx, float *half) {   /* moment: veg_clock's */
    float bx; int segs = veg_segs_of(t, b, i, &bx);   /* (the held heights: the blade as drawn) */
    if (segs < 1) segs = 1;
    if (segs > VEG_MAX_SEGS) segs = VEG_MAX_SEGS;
    bool sword = tank_veg_kind(t, b) == VEG_KIND_SWORD;
    int seed = VEG_SEED[b];
    const float vc = moment * VEG_W;             /* (draw_veg's sway, exactly) */
    float sway = fast_sin(vc + (i + seed) * 1.7f) * (sword ? 2.5f : 4.0f);
    if (g_veg_step == 2) { int yb = (int)(TANK_BOT - 16 + VEG_SEG_RY); y = yb - ((yb - y) & ~1); }   /* row pairs: the pair's lower row, as draw_veg paints it */
    float sp = (TANK_BOT - 16 - y) / VEG_SEG_DY;
    if (sp < -0.7f || sp > segs - 1 + 0.7f) return false;
    if (sp < 0) sp = 0;
    if (sp > segs - 1) sp = (float)(segs - 1);
    int k = (int)sp, k1 = k + 1 > segs ? segs : k + 1; float fr = sp - k;
    float xk  = bx + sway * k  / (float)segs * fast_sin(k  * 0.4f + vc + i + seed);
    float xk1 = bx + sway * k1 / (float)segs * fast_sin(k1 * 0.4f + vc + i + seed);
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
    if (g_no_animals) return;
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
    const float an = anim(t), vm = veg_clock(t->clock);                 /* (once a frame: the S3 does a double's fmod in software) */
    float SX = 5.0f, SW = 1.5f, ax = t->sh_x, ay = t->sh_y, lean = 1.2f * fast_sin(an * 0.7f), pitch = 0;
    float speed = sqrtf(t->sh_vx * t->sh_vx + t->sh_vy * t->sh_vy);
    float flutter = t->sh_mode == SH_HITCH && !t->sh_hitched ? 1.6f : speed > 2 ? 1.0f + speed / 40.0f : 1.0f;
    float free_u = held ? 1 - u : 1;                                     /* how much of the open water is still in it */
    if (onblade) {                                                       /* the blade's sway and lean come in with the hold */
        float cx0 = ax, h0 = SW, cx1 = ax, h1;
        if (veg_ribbon_at(t, t->sh_bed, t->sh_frond, (int)ay, vm, &cx0, &h0)) { ax += (cx0 - ax) * u; SW = fmaxf(1.0f, fminf(3.5f, h0)); }
        veg_ribbon_at(t, t->sh_bed, t->sh_frond, (int)ay, veg_clock(t->clock + 0.05f), &cx1, &h1);
        float l = (cx1 - cx0) / 0.05f * 0.9f; l = l < -3 ? -3 : l > 3 ? 3 : l;
        lean = lean * (1 - u) + l * u;
    } else if (onpost) { SW = fmaxf(1.0f, fminf(3.5f, t->sh_ahw)); lean *= 1 - u; }   /* a post stands still: the coil hugs its middle */
    else if (onsand) lean *= 1 - u * 0.6f;
    if (free_u > 0) {                                                    /* off a blade it bobs in the water and pitches with speed */
        ay += free_u * 1.6f * fast_sin(an * 1.7f); ax += free_u * 0.8f * fast_sin(an * 0.9f + 1.0f);
        float f = fabsf(t->sh_vx) / 42.0f + speed / 120.0f; if (f > 1) f = 1;
        pitch = SH_PITCH_MAX * f * 3.14159265f / 180.0f;                /* forward = the way it faces */
    }
    sh_spiral_t pa, pb; sh_spiral(&pa, SH_FREE, SX, SW); sh_spiral(&pb, pose, SX, SW);
    float coilx = sh_lerp(pa.cx, pb.cx, u), coily = sh_lerp(pa.cy, pb.cy, u);
    static double s_fin_at = -1; static float s_fin;                  /* the dorsal fin's flutter: its own phase, so a change of speed */
    float fdt = s_fin_at >= 0 && t->clock > s_fin_at ? (float)(t->clock - s_fin_at) : 0;   /* changes its pace and never jumps it */
    s_fin_at = t->clock; s_fin = fmodf(s_fin + (fdt < 0.5f ? fdt : 0) * 9.0f * flutter, TAU);
    sh_sprite_cached(sc, an, pose, u, SX, SW, s_fin, tank_seahorse_jut(t));
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
        float rcx = 0, rhalf = 0; bool ribbon = onblade ? veg_ribbon_at(t, t->sh_bed, t->sh_frond, py, vm, &rcx, &rhalf)
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
                if ((onpost || veg_ribbon_at(t, t->sh_bed, t->sh_frond, py + 1, vm, &r2cx, &r2half)) && fabsf(px + 0.5f - r2cx) <= r2half + 0.3f)
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
 *     recording the pixels it covers in its own MASK (ds()->pm, by its item's slot);
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
    bool placing;                         /* changed on its page (dragged, re-ranked, re-depthed, recolored): drawn LIVE this frame, out of the scene */
    bool baked;                           /* in the scene this frame (its mask is this frame's) */
    bool masked;                          /* a live piece whose mask was recorded this frame all the same (render_debug_live_mask: the tests) */
    int  x0, y0, w, rows;                 /* its box: the mask's frame */
} piece_t;
static piece_t g_pc[PIECES_MAX];
static bool g_scene_front;                /* the baked scene holds the pieces IN FRONT */
/* the scene's signature: what the bake depends on (render_tank rebakes on a change) */
typedef struct { int n, item[PIECES_MAX], cx[PIECES_MAX], z[PIECES_MAX], q[PIECES_MAX]; uint32_t col[PIECES_MAX]; float dim; bool front; int lx; int bg; int bgok; } scene_sig_t;   /* (bg: the background, 0.4.1; bgok: it was drawn - 0 when its scratch was refused) */
static scene_sig_t g_sig; static bool g_sig_ok;
/* THE PATCH (0.3.9.4, with THE GRASS LOOP's pieces below): the pieces' masks of the scene before the last bake, kept at bake_scene for the
   window's columns - whose scene they are, and whose the masks in ds()->pm are */
static uint32_t *g_gl_pm_old; static scene_sig_t g_gl_pm_old_sig, g_gl_pm_cur_sig;
#define GL_PM_BYTES (sizeof ((decor_scratch_t *)0)->pm)
static bool g_pk_hidden;                  /* director `pumpkin off`: the frame-rate A/B */
void render_set_pumpkin_hidden(bool hidden) { g_pk_hidden = hidden; }
bool render_pumpkin_hidden(void) { return g_pk_hidden; }
static float pc_half_w(int item) { return item == 2 ? CASTLE_HALF_W : item == 3 ? CORAL_HALF_W : item == 4 ? CLUSTER_HALF_W : item == 9 ? VOLCANO_HALF_W : PUMPKIN_HALF_W; }
static int   pc_index(int item) { for (int k = 0; k < g_npc; k++) if (g_pc[k].item == item) return k; return -1; }
/* the solid pieces above the lantern cast on the sand (pk_casts; the castle has its own rules) */
static void pk_casts_pieces(const tank_t *t, int px0, int px1) {
    (void)t;
    for (int k = g_pk_k + 1; k < g_npc; k++) {
        if (g_pc[k].item == 2 || g_pc[k].placing) continue;
        int hw = (int)pc_half_w(g_pc[k].item) - 3;
        /* only where a piece stands IN FRONT of the lantern - over its own patch of sand - is the sand shaded; one
           beside it, nearer the glass, leaves the light where it falls (0.3.9.3, Bananna on the pendant: "item
           placement can block light from pumpkin" - every piece above it in the order cut the floor's light
           at its edge, however far to the side) */
        int a = g_pc[k].cx - hw > px0 ? g_pc[k].cx - hw : px0, z = g_pc[k].cx + hw < px1 ? g_pc[k].cx + hw : px1;   /* ... and only where it covers it */
        if (z < a) continue;
        pk_cast_add(a - 6, a - 1, 1); pk_cast_add(a, z, 3); pk_cast_add(z + 1, z + 6, 1);   /* (a soft half step either side) */
    }
}
static uint32_t g_boot_hide;                       /* THE BOOT SHOW: pieces left out of a scene (bit = item; render_boot_scene) */
/* this frame's list, from the tank's order; front: the pieces IN FRONT ride in the scene */
static void pieces_build(const tank_t *t, bool cached, bool front) {
    int items[DECOR_SLOTS], n = tank_decor_order(t, items);
    g_npc = 0; g_pk_k = -1; g_castle_cx = -1;
    g_dz_castle = g_dz_coral = g_dz_cl = g_dz_pk = g_dz_vo = 0;
    for (int i = 0; i < n && g_npc < PIECES_MAX; i++) {
        int item = items[i];
        if (item == 0 || (item == 8 && g_pk_hidden) || (g_boot_hide >> item & 1)) continue;
        piece_t *p = &g_pc[g_npc];
        p->item = item; p->cx = (int)tank_decor_x(t, item); p->z = tank_decor_z(t, item);
        p->placing = setup_is_place() && setup_item() == item && setup_place_live();   /* (a page just opened leaves its piece in the scene) */
        p->baked = cached && !p->placing && (p->z != DECOR_Z_FRONT || front);
        p->masked = false;
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
static inline int pc_mslot(int item) { return item == 2 ? 0 : item == 3 ? 1 : item == 4 ? 2 : item == 8 ? 3 : 4; }   /* a piece's mask (ds()->pm), by its item */
/* piece k's silhouette at (x, y): its recorded mask (this frame's scene), or the castle's static one */
static bool pc_covers(int k, int x, int y) {
    const piece_t *p = &g_pc[k];
    int lx = x - p->x0, r = y - p->y0;
    if ((unsigned)lx >= (unsigned)p->w || (unsigned)r >= (unsigned)p->rows) return false;
    if (p->item == 2) return (g_castle_mask[r][lx >> 5] >> (lx & 31)) & 1;
    return (p->baked || p->masked) && (((p->placing ? ds()->pm_live : ds()->pm[pc_mslot(p->item)])[r][lx >> 5] >> (lx & 31)) & 1);
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
static bool cr_clipped(int x, int y) {
    for (int k = 0; k < g_npc && k < 32; k++) if ((g_cr_clip >> k & 1) && pc_covers(k, x, y)) return true;
    return false;
}
/* SWIM DEPTH phase 2: what of a piece stands before a fish passing BEHIND it, whatever the piece's own depth - the
   castle's front row (the arch stays open: the fish shows in it, between the keep and the wall), any other piece whole */
static bool pc_body_covers(int k, int x, int y) {
    const piece_t *p = &g_pc[k];
    if (p->item == 2) {
        int lx = x - p->x0, r = y - p->y0;
        return (unsigned)lx < (unsigned)p->w && (unsigned)r < (unsigned)p->rows && ((castle_front()[r][lx >> 5] >> (lx & 31)) & 1);
    }
    return pc_covers(k, x, y);
}
bool render_bed_leaf(int x, int y) {   /* (tests: a leaf of the sword plant IN FRONT, as the last frame recorded it) */
    return g_bedm_on && g_bedm && ((g_bedm[y * DIRTY_WORDS_PER_ROW + (x >> 5)] >> (x & 31)) & 1);
}
bool render_piece_covers(int item, int x, int y) {   /* (the sim's --selftest-depth: a piece's own body, as the last frame recorded it) */
    for (int k = 0; k < g_npc; k++) if (g_pc[k].item == item) return pc_body_covers(k, x, y);
    return false;
}
static bool pc_in_box_rim(int k, int x0, int x1, int y) {   /* the span meets piece k's box, with its contact shadow's rim down and to the right */
    const piece_t *p = &g_pc[k];
    return y >= p->y0 && y < p->y0 + p->rows + PC_SHADOW_FAR && x1 >= p->x0 && x0 < p->x0 + p->w + PC_SHADOW_FAR;
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
    const uint32_t *row = p->item == 2 ? g_castle_mask[r] : p->baked ? ds()->pm[pc_mslot(p->item)][r] : p->masked ? ds()->pm_live[r] : NULL;   /* (live: its own slot, as pc_covers) */
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
            int a = pc_covers(k, x - PC_SHADOW_NEAR, y - PC_SHADOW_NEAR) ? PC_SHADOW_A_NEAR : pc_covers(k, x - PC_SHADOW_FAR, y - PC_SHADOW_FAR) ? PC_SHADOW_A_FAR : 0;
            if (a) px_darken(&CTX_PX(c, x, y), a);
        }
}
/* the pieces a bed's spans leave alone (veg_span) */
static uint32_t veg_skip_for(const tank_t *t, int b) {
    int bz = bed_z(t, b); uint32_t m = 0;
    for (int k = 0; k < g_npc; k++) {
        if (!g_pc[k].baked && g_pc[k].item != 2 && !(b == 3 && g_pc[k].masked && g_pc[k].z == DECOR_Z_BACK)) continue;   /* drawn live this frame: IN FRONT over
                                                                         the grass anyway; BEHIND it is drawn under the grass, so the plant (over the seagrass since 0.4)
                                                                         skips it by its live mask where it is ranked above the plant (its page, the piece dragged) */
        if (g_pc[k].z == DECOR_Z_FRONT && bz != DECOR_Z_FRONT) m |= 1u << k;             /* in front of the plant layer */
        else if (b == 3 && bz != DECOR_Z_MIDDLE && g_pc[k].z == bz && tank_decor_above(t, g_pc[k].item, 0)) m |= 1u << k;   /* at the plant's depth, ranked above it */
    }
    return m;
}
/* a piece IN FRONT back over what was drawn on it this frame (the scene has it) - but not over the crab roosting in the arch of a
   castle IN FRONT ranked above the piece (g_rs_crab, set for that piece's restore): the piece stands behind the castle, so behind its
   arch and the crab in it (Strato 2026-11-02, the watch: both IN FRONT, the castle above, the volcano's rock restored over the crab) */
static const tank_t *g_rs_crab;
static bool g_rs_old;                                /* (--selftest-roost's control: THE CRAB'S STACK off - every piece's restore and overlay over the crab
                                                        in the arch, the crab over every piece before its shelter - as before 0.4) */
void render_debug_crab_stack_old(bool on) { g_rs_old = on; }
static bool g_rp_old;                                /* (--selftest-roost-pass's control: the put-back behind a piece over the crab in its shelter, as before) */
void render_debug_roost_pass_old(bool on) { g_rp_old = on; }
static void pc_restore(uint16_t *fb, int k) {
    const piece_t *p = &g_pc[k];
    int xa = p->x0 < 0 ? 0 : p->x0, xb = p->x0 + p->w - 1 > TANK_W - 1 ? TANK_W - 1 : p->x0 + p->w - 1;
    int ya = p->y0 < 0 ? 0 : p->y0, yb = p->y0 + p->rows - 1 > TANK_H - 1 ? TANK_H - 1 : p->y0 + p->rows - 1;
    for (int y = ya; y <= yb; y++) {
        uint32_t *drow = g_dirty + y * DIRTY_WORDS_PER_ROW;
        for (int w = xa >> 5; w <= xb >> 5; w++) {
            uint32_t bits = drow[w];
            if (g_keep_on) bits &= ~g_keep[y * DIRTY_WORDS_PER_ROW + w];   /* (a fish in front of the pieces stays in front) */
            while (bits) {
                int x = (w << 5) + __builtin_ctz(bits); bits &= bits - 1;
                if (x < xa || x > xb || !pc_front_covers(k, x, y) || (g_rs_crab && crab_covers(g_rs_crab, x, y))) continue;
                fb[y * TANK_W + x] = g_scene[y * TANK_W + x];
                drow[w] &= ~(1u << (x & 31));
            }
        }
    }
}
/* a piece drawn LIVE - the page's, changed on it, so out of the scene - is painted over whatever the scene holds inside its box,
   the pieces ranked above it included. Those come back from the scene where their masks cover it (a piece whole; the castle's
   keep and wall, its arch open), keeping off a fish IN FRONT of the pieces and the leaves of a bed IN FRONT that is ranked over
   the covering piece (0.3.9.4: before, every piece IN FRONT was live with a page up, drawn in order, so none needed this) */
static void pc_cover_live(uint16_t *fb, int k) {
    const piece_t *p = &g_pc[k];
    for (int j = k + 1; j < g_npc; j++) {
        const piece_t *q = &g_pc[j];
        if (!q->baked) continue;
        int xa = p->x0 > q->x0 ? p->x0 : q->x0, xb = (p->x0 + p->w < q->x0 + q->w ? p->x0 + p->w : q->x0 + q->w) - 1;
        int ya = p->y0 > q->y0 ? p->y0 : q->y0, yb = (p->y0 + p->rows < q->y0 + q->rows ? p->y0 + p->rows : q->y0 + q->rows) - 1;
        if (xa < 0) xa = 0;
        if (xb > TANK_W - 1) xb = TANK_W - 1;
        if (ya < 0) ya = 0;
        if (yb > TANK_H - 1) yb = TANK_H - 1;
        for (int y = ya; y <= yb; y++)
            for (int x = xa; x <= xb; x++) {
                if (!pc_covers(j, x, y) || keep_at(x, y) || bed_over(j, x, y)) continue;
                fb[y * TANK_W + x] = g_scene[y * TANK_W + x];
                if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
            }
    }
}
static bool g_live_rec;                   /* a live piece records its mask too (no shadow): the tests read it (render_piece_covers) */
void render_debug_live_mask(bool on) { g_live_rec = on; }
/* one piece, whole: in the bake (final, recording its mask) or live */
static void pc_draw(ctx_t *c, const tank_t *t, int k, bool final, bool record) {
    piece_t *p = &g_pc[k];
    if (record) {
        uint32_t (*pm)[(CASTLE_MASK_W + 31) / 32] = p->placing ? ds()->pm_live : ds()->pm[pc_mslot(p->item)];   /* (live: its own slot) */
        memset(pm, 0, sizeof ds()->pm_live);
        g_rec = &pm[0][0]; g_rec_x0 = p->x0; g_rec_y0 = p->y0; g_rec_w = p->w; g_rec_rows = p->rows; g_rec_wpr = (CASTLE_MASK_W + 31) / 32;
    }
    g_pk_shade_all = g_pk_lx >= 0 && g_pk_k >= 0 && k > g_pk_k && p->item != 2;   /* above the lantern: backlit (the castle has its own rules: the light through its arch and windows) */
    switch (p->item) {
        case 2:  draw_castle(c, p->cx, 0, final); if (p->z == DECOR_Z_BACK || record) arch_shade(c, k, p->cx, t->night, false); break;   /* IN FRONT: the bake carries the tunnel's shade too (2026-10-23); live, arch_shade_dirty does the rest */
        case 3:  draw_coral(c, p->cx, tank_coral_rgb(t), tank_coral_growth(t), final); break;
        case 4:  draw_cluster(c, p->cx, tank_cluster_scheme(t), tank_cluster_growth(t), final); break;
        case 9:  draw_volcano(c, p->cx, t->night, final); break;
        default: draw_pumpkin(c, p->cx, t->night, anim(t), final, true, final); break;
    }
    g_pk_shade_all = false;
    g_rec = NULL;
    if (record && !p->placing) pc_contact_shadow(c, k);
    if (record && p->placing) p->masked = true;      /* (a live piece casts no shadow; its mask: a live piece BEHIND ranked over the plant keeps its
                                                        leaves off (veg_skip_for), and the tests read it) */
}
/* a piece's overlay, drawn every frame over the baked body: a crown's
 * tentacles - clipped by the pieces above it (g_clip_k); its rect joins the
 * vignette sweep */
#define PC_CROWN(k) do { int qx0 = 0, qy0 = 0, qx1 = -1, qy1 = 0; \
        if (g_pc[k].item == 3) draw_coral_crown(&c, g_pc[k].cx, tank_coral_rgb(t), tank_coral_growth(t), anim(t), &qx0, &qy0, &qx1, &qy1); \
        else if (g_pc[k].item == 4) draw_cluster_crown(&c, g_pc[k].cx, tank_cluster_scheme(t), tank_cluster_growth(t), anim(t), &qx0, &qy0, &qx1, &qy1); \
        if (qx1 >= qx0) DYN_RECT(qx0, qy0, qx1, qy1); } while (0)
/* the volcano's: its lava breathes over the baked rock, its bubbles rise (their rect joins the sweep) */
#define PC_VOLCANO(k) do { if (g_pc[k].baked) draw_volcano_glow(&c, g_pc[k].cx, anim(t), t->night, false, true); \
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
/* the background's scratch, borrowed for a bake: the grass loop's PSRAM allocator on the board (internal RAM is spoken for),
   the heap in the sim; given back as soon as the background is painted */
static inline uint32_t bg_mix8(uint32_t a, uint32_t b, int t) {   /* t 0..256 (backgrounds.c's: the glow on rows with nothing else, the same as on rows with) */
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    return (uint32_t)(ar + ((((int)(b >> 16) & 255) - ar) * t >> 8)) << 16 | (uint32_t)(ag + ((((int)(b >> 8) & 255) - ag) * t >> 8)) << 8 | (uint32_t)(ab + (((int)(b & 255) - ab) * t >> 8));
}
/* THE BOOT SHOW's capture (render_boot_bg_layers): each row's background layers, as the bake draws them */
static void (*g_bg_lay_hook)(int y, const uint8_t *lay);
void render_boot_bg_layers(void (*hook)(int y, const uint8_t *lay)) { g_bg_lay_hook = hook; }
int render_bg_current(void) { return g_bg; }
/* the water alone at (x, y), as the bake lays it (the light up, vignetted, dithered; no glow): a background's pixel before
   the boot show lands it */
uint16_t render_boot_water_px(int x, int y) {
    static const uint8_t bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
    uint32_t col = water_rgb(y);
    int r = (col >> 16) & 255, g = (col >> 8) & 255, b = col & 255;
    int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y), inv = 256 - a, d = bayer[y & 3][x & 3];
    int r5 = (((r * inv) >> 8) + (d >> 1)) >> 3, g6 = (((g * inv) >> 8) + (d >> 2)) >> 2, b5 = (((b * inv) >> 8) + (d >> 1)) >> 3;
    if (r5 > 31) r5 = 31;
    if (g6 > 63) g6 = 63;
    if (b5 > 31) b5 = 31;
    return FB_SWAP16((uint16_t)((r5 << 11) | (g6 << 5) | b5));
}
/* the bake's scratch (0.4.1): the platform's lender (render_set_bake_scratch; PSRAM on the boards) - never the grass loop's, whose
   headroom is for memory it keeps: the loop fills PSRAM down to that headroom, and on 11-04 every rebake after it had (the
   light, the shop) was refused the reef's 126 KB and drew plain water. NULL from the lender: the water palette alone. */
static void *(*g_bs_alloc)(size_t); static void (*g_bs_free)(void *);
/* THE BACKGROUND REFUSED (0.4.1, Strato on the square 11-05: "a big rectangular shadow monolith" behind the sword plant): at boot the
   boot show holds ~700 KB, the lender refused the bake's scratch, and the scene - and the grass loop over it - came out as water
   alone, with nothing to try again until a light switch. A trim then patched the plant's box from a scene rebuilt WITH the reef: a
   block of rock in a reef-less tank. Now a refused bake is remembered (the scene's signature says bgok 0, so the loop is keyed to it
   too); about once a second the lender is asked again, and once it can lend, the scene - and the loop, its key changed - rebuild */
static bool g_bg_short;                         /* the last bake wanted its background and was refused the scratch */
static int  g_bg_probe_n;                       /* frames since the lender was last asked again */
void render_set_bake_scratch(void *(*alloc)(size_t), void (*release)(void *)) { g_bs_alloc = alloc; g_bs_free = release; bg_set_keep_lender(alloc, release); }
static void *bg_scratch_take(void) { size_t n = bg_scratch_need(g_bg); if (!n) return NULL; return g_bs_alloc && g_bs_free ? g_bs_alloc(n) : malloc(n); }   /* (by need: the audit's F1) */
static void  bg_scratch_give(void *p) { if (!p) return; if (g_bs_alloc && g_bs_free) g_bs_free(p); else free(p); }
static void draw_scene(const tank_t *t, uint16_t *fb, int stride, float dim) {
    ctx_t c = ctx_full(fb, stride, dim);
    /* water gradient #0a3c46 → #08272f → #031015 (Open Water's; a background's own palette, 0.4.1) */
    for (int y = 0; y < TANK_H; y++) {
        uint16_t v = FB_SWAP16(rgb565(water_rgb(y), dim));
        for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = v;
    }
    if (g_bg) {                                     /* THE BACKGROUND, uncached (the tests' path): its rows and its glow, unvignetted - the frame's own sweep does that */
        void *bs = bg_scratch_take();
        const bg_glow_t *gw = bg_begin(g_bg, bs);
        for (int y = 0; y < TANK_H; y++) {
            const uint32_t *row = bg_row(g_bg, y, bs, NULL);
            if (row) { for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = FB_SWAP16(rgb565(row[x], dim)); continue; }
            if (!gw || !gw->row[y]) continue;
            uint32_t w = water_rgb(y);
            for (int x = 0; x < TANK_W; x++) { int k = gw->row[y] * gw->col[x] >> 8; if (k) fb[y * stride + x] = FB_SWAP16(rgb565(bg_mix8(w, gw->rgb, k), dim)); }
        }
        bg_scratch_give(bs);
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
    /* the bake is the scene's own: nothing of a FRAME stands before the pieces in it. Called outside a frame (the boot show's scenes and
       its final bake, render_rebake_scene) the LAST frame's marks were still set - a fish IN FRONT of the pieces (g_keep), the sword
       plant's leaves (g_bedm), a critter in the front slot (g_guard) - and the pieces' stores keep off them: pieces baked with fish-shaped
       HOLES, unrecorded, so the grass loop painted fronds into them, until the next full rebake (0.3.9.4, Strato on the square right after
       a boot: "small cut outs in the left and right edge" of the pumpkin and fronds through the volcano where the pumpkin overlaps its foot;
       --square-boot stages it). Cleared for the bake, put back after. */
    const bool keep_on = g_keep_on, bedm_on = g_bedm_on, fbg_on = g_fbg_on;
    __typeof__(g_guard) guard = g_guard; __typeof__(g_guard_who) guard_who = g_guard_who; __typeof__(g_clip_k) clip_k = g_clip_k; __typeof__(g_under) under = g_under;
    g_keep_on = false; g_bedm_on = false; g_fbg_on = false; g_guard = NULL; g_guard_who = 0; g_clip_k = -1; g_under = NULL;
    if (g_gl_pm_old) { memcpy(g_gl_pm_old, ds()->pm, GL_PM_BYTES); g_gl_pm_old_sig = g_gl_pm_cur_sig; }   /* THE PATCH: the pieces' masks of the scene going out, and whose they are */
    /* THE BACKGROUND (0.4.1): its scratch borrowed for the bake; its glow (Twilight, Reef Walls, Grotto) folded in here - a
       table weight per row and per column - not painted again over the frame */
    void *bgs = g_bg ? bg_scratch_take() : NULL;
    g_bg_short = g_bg && !bgs;
    if (sc == g_scene) g_sig.bgok = !g_bg_short;   /* (EVERY bake of the tank's scene says what it drew - render_boot_scene_done's too: on the square
                                                      11-05 the boot show's own rebake was refused, the scene still claimed its background, the loop
                                                      baked over the water keyed to it, and the reef's return changed nothing the loop's key could see) */
    const bg_glow_t *bgw = bg_begin(g_bg, bgs);
    const int dimi = (int)(dim * 256);              /* (a background's pixels: integer light, one rule for every row it touches) */
    for (int y = 0; y < TANK_H; y++) {
        uint32_t col = water_rgb(y);
        int r = (int)(((col >> 16) & 255) * dim), g = (int)(((col >> 8) & 255) * dim), b = (int)((col & 255) * dim);
        const uint8_t *blay = NULL;
        const uint32_t *brow = bgs ? bg_row(g_bg, y, bgs, &blay) : NULL;   /* the background's own row (NULL: the water, and its glow) */
        if (brow && g_bg_lay_hook) g_bg_lay_hook(y, blay);
        const int gk = !brow && bgw ? bgw->row[y] : 0;
        if (bgs) { r = ((col >> 16) & 255) * dimi >> 8; g = ((col >> 8) & 255) * dimi >> 8; b = (col & 255) * dimi >> 8; }
        uint32_t pc = 0xffffffffu; int pr = 0, pg = 0, pb = 0;    /* (a background's last color, lit: its rows run in spans of one color) */
        uint8_t *vrow = g_vig ? g_vig + y * TANK_W : NULL;
        const bool vread = vrow && g_vig_filled;        /* the vignette never changes: read it after the first bake (0.4.1, D5: its floats
                                                           were ~4M instructions of every rebuild on the square) */
        for (int x = 0; x < TANK_W; x++) {
            int a = vread ? vrow[x] : vig_alpha(x, y);
            if (vrow && !vread) vrow[x] = (uint8_t)a;
            int inv = 256 - a, d = bayer[y & 3][x & 3];
            int r0 = r, g0 = g, b0 = b;
            if (brow) { uint32_t c = brow[x]; if (c != pc) { pc = c; pr = ((c >> 16) & 255) * dimi >> 8; pg = ((c >> 8) & 255) * dimi >> 8; pb = (c & 255) * dimi >> 8; } r0 = pr; g0 = pg; b0 = pb; }
            else if (gk) { int k = gk * bgw->col[x] >> 8; if (k) { uint32_t c = bg_mix8(col, bgw->rgb, k); r0 = ((c >> 16) & 255) * dimi >> 8; g0 = ((c >> 8) & 255) * dimi >> 8; b0 = (c & 255) * dimi >> 8; } }
            int rr = (r0 * inv) >> 8, gg = (g0 * inv) >> 8, bb = (b0 * inv) >> 8;
            int r5 = (rr + (d >> 1)) >> 3, g6 = (gg + (d >> 2)) >> 2, b5 = (bb + (d >> 1)) >> 3;
            if (r5 > 31) r5 = 31;
            if (g6 > 63) g6 = 63;
            if (b5 > 31) b5 = 31;
            sc[y * TANK_W + x] = FB_SWAP16((uint16_t)((r5 << 11) | (g6 << 5) | b5));
        }
    }
    bg_scratch_give(bgs);                          /* (the background is in: its scratch goes back) */
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
    const bool baking = g_baking; g_baking = true;  /* (the lantern keeps off no piece above it: those are painted after it - coral-sliver, 2026-11-02) */
    for (int k = 0; k < g_npc; k++)                 /* the pieces, back to front, each recording its mask */
        if (g_pc[k].baked) pc_draw(&c, t, k, true, true);
    g_baking = baking;
    g_gl_pm_cur_sig = g_sig;
    g_keep_on = keep_on; g_bedm_on = bedm_on; g_fbg_on = fbg_on; g_guard = guard; g_guard_who = guard_who; g_clip_k = clip_k; g_under = under;
}

/* ---- the grass loop's pieces (THE GRASS LOOP, above) ---- */
/* what the back grass looks like, but for its sway: the scene, the beds, their fronds - and apart from it, the fronds'
   heights (segs: each frond's segment count into seg[], when asked). A frond a segment taller or shorter (3.2 px; it grows,
   the urchin nibbles) left the whole loop stale: on the 1.8, 9 rebakes in a minute while the urchin ate, ~12-17 fps.
   Now a small change WAITS (GL_SEG_WAIT_S from when it first showed, the loop showing the heights it was baked with); a
   frond GL_SEG_JUMP segments off (a trim, a real bite), anything else, or a loop not yet whole rebakes at once. (A first
   cut waited 2 min from the BAKE: an old loop rebaked at the first segment, a growing tank every ~2 min, each some
   seconds at ~10 fps on the square - Strato's "once in a while the frame rate really slows down") */
#define GL_SEG_WAIT_S 900.0f
static uint8_t g_gl_seg[VEG_BEDS_MAX][VEG_FRONDS_MAX];   /* the heights the loop was baked with */
static uint32_t g_gl_seg_h, g_gl_hard_last;             /* ... their hash; the last frame's look but for the heights */
static double  g_gl_seg_t = -1;                        /* the tank clock the heights first moved at (-1: they have not) */
static bool    g_gl_seg_ok;                            /* g_gl_seg holds a look's heights (a stale event copied them): THE HELD HEIGHTS apply */
static uint32_t gl_skip_items(const tank_t *t, int b);   /* (below: the items a bed's spans keep off) */
static uint32_t gl_key(const tank_t *t, uint32_t *segs, uint8_t seg[VEG_BEDS_MAX][VEG_FRONDS_MAX]) {
    uint32_t h = 2166136261u, g = 2166136261u;
#define GL_MIX(h, v) (h = (h ^ (uint32_t)(v)) * 16777619u)
    { const uint8_t *p = (const uint8_t *)&g_sig; for (size_t k = 0; k < sizeof g_sig; k++) GL_MIX(h, p[k]); }   /* WHAT the scene is, not how often it
                                                                                    was drawn: the day comes back to its own loop (0.3.9.3: a light switch rebaked it
                                                                                    twice - ~17 s at 10-12 fps each way on the square - and a menu closing once) */
    { uint32_t d; memcpy(&d, &g_scene_dim, sizeof d); GL_MIX(h, d); }
    GL_MIX(h, g_veg_step); GL_MIX(h, g_veg_arch_shade); GL_MIX(h, g_veg_mask_cx); GL_MIX(h, tank_plant_under(t));   /* (THE BED ORDER) */
    for (int b = 0; b < tank_veg_beds(t); b++) {
        if (bed_z(t, b) != DECOR_Z_BACK) continue;
        int n; float bx0; tank_veg_bed(t, b, &bx0, NULL, NULL, &n);
        GL_MIX(h, b); GL_MIX(h, tank_veg_kind(t, b)); GL_MIX(h, (int)(bx0 * 4)); GL_MIX(h, n);
        if (!g_gl_skip_old) GL_MIX(h, gl_skip_items(t, b));   /* the pieces its spans keep off - the ORDER (0.4, Strato on the square 11-04: the sword plant
                                                                 raised over the volcano on its page, IN FRONT lit, its leaves kept cut round the volcano - the
                                                                 loop never saw a change, so it served the old order until a restart; the patch takes it, by
                                                                 the pieces' boxes, gl_window) */
        if (segs) for (int i = 0; i < n; i++) { float bx; int sg = tank_veg_frond(t, b, i, &bx); GL_MIX(g, sg); if (seg && b < VEG_BEDS_MAX && i < VEG_FRONDS_MAX) seg[b][i] = (uint8_t)sg; }
    }
#undef GL_MIX
    if (segs) *segs = g;
    return h;
}
/* once a frame, before the frame uses the loop: is it stale (the rule above)? The look but for the heights is returned;
   the whole look (heights too) is left in g_gl_full_now - the bake waits for it to hold still */
static uint32_t g_gl_full_now;
/* THE LIGHT OFF (0.3.9.3): the loop sits it out - its grass drawn live, as before the loop, stepping on with the frames so
   the sway never jumps - and is neither used nor marked stale, so the day's loop is there the moment the light comes
   back. There is no memory for a second (night) loop on the square: a light switch was a full rebake both ways */
static bool g_gl_dark;
static bool g_gl_parked;                        /* a piece is drawn live (its page): the loop sits it out - not used, not rebaked (render_tank) */

/* ---- THE PATCH and the ring (0.3.9.4) ----
 * Strato on the square, 10-31: DONE after a move - "a huge framerate drop". The loop's 66 moments were baked again whole,
 * 36 ms each with the back grass drawn live meanwhile: ~25 s at 7-9 fps (and once the fine moments' arena ran out, a replan
 * ran it twice - 155 bakes). Most of what makes the loop stale changes the look only inside a few BOXES: a piece moved,
 * bought, sold, shelved or hidden (its old place and its new one, each with its contact shadow's rim), a frond grown past
 * the wait (its strip), the sword plant BEHIND moved (its old strip and its new one), a piece newly ranked over a bed. A
 * frond is opaque and its color its own - the scene is never under a frond's pixel - so outside the boxes every band and
 * every difference is right pixel for pixel: only the runs a piece IN FRONT kept off, the rim it shaded and the scene under
 * them are in the boxes. So the loop keeps all of it. A frame is primed from its band and difference as ever and the
 * boxes alone are repainted live (the new scene copied in, the back grass of the frame's moment drawn over it, clipped
 * to the box - gl_patch_rect); the bake repaints a band inside the boxes in place, and a fine moment's difference is
 * SPLICED (gl_diff_splice: its rows outside the boxes copied word for word, inside them the old runs kept up to the box's
 * edge and the box encoded afresh) into the arena, which is a RING now - the oldest difference is replaced first, the new
 * one written at the head, so the live set never needs room for a second copy of itself. Whatever is not boxes (the
 * light's dim, a trim of a whole bed, the band growing past its room) still bakes whole - in SLICES (gl_bake_step: the
 * band's copy a few rows at a time, each bed, the encode a few rows at a time) under a frame's budget, so no frame
 * carries a 36 ms bake; and the fine moments' room is planned from the last whole loop's measured differences and
 * checked early in a bake, so a replan comes once, before most of the work, never after it. */
#define GL_WIN_MAX 6
#define GL_WIN_MOST 90                                                   /* the boxes' share of the band at most, in percent (past it the whole bake costs no more) */
static gl_rect_t g_gl_win[GL_WIN_MAX]; static int g_gl_nwin;             /* the boxes the loop is stale inside (inside the band) */
static struct { int k, j, phase, row, bed; size_t w, off, cap, op; } g_gl_bs;   /* the whole bake under way, in slices (gl_bake_step): its band (k >= 0) or fine moment, its
                                                                            phase (0 none, 1 the scene's copy, 2 the beds, 3 the encode), where it is in it */
static bool      g_gl_patch;                                             /* stale inside g_gl_win only: served with the boxes repainted, rebaked inside them */
static uint16_t  g_gl_gen;                                               /* the look's generation: a band or a moment baked at it is exact */
static uint16_t  g_gl_bgen[GL_MAX];
static uint16_t *g_gl_fgen;                                              /* ... each fine moment's (with the ring's tables, in the loop's memory) */
static scene_sig_t g_gl_sig;                                             /* the scene the loop was last baked toward */
static float     g_gl_dim_baked = -1; static int g_gl_step_baked, g_gl_mcx_baked = -1; static bool g_gl_ashade_baked;
static struct { bool back; int kind, n; float x0, x1; uint32_t skip; } g_gl_bed[VEG_BEDS_MAX];
static bool    g_gl_under_baked;                /* (THE BED ORDER) the sword plant under the seagrass as baked */   /* each bed as baked: in the back grass, its extent, the items its spans kept off */
/* the ring: the differences in the order written (g_gl_dq, the oldest first), each at g_gl_doff[j] for g_gl_dlen[j] words; the
   next one goes at the head - the room there runs to the arena's end (then it wraps to 0) or to the tail, the oldest's start */
static uint32_t *g_gl_dlen; static uint8_t *g_gl_dq; static int g_gl_dqn; static size_t g_gl_dhead, g_gl_dtail;
static void     *g_gl_meta;                                              /* (the block the three tables live in: gl_geometry) */
static size_t    g_gl_davg;                                              /* words a fine moment's difference ran, the last time the loop was whole (the next plan's estimate) */
static int       g_gl_pwin;                                              /* THE PATCH's rebake: the box being patched next (a band's or a moment's, one a slice) */
static bool      g_gl_abort_next;                                        /* (the tests) the next splice refused: the fallback */
static uint16_t *g_gl_wrow;                                              /* per band row: up to two stale intervals x0,x1,x0,x1 (x0 > x1: none) - the window's columns */
static bool      g_gl_patch_page;                                        /* THE EDITOR: this rebake follows a placement page's DONE - the creatures stay hidden until the grass has settled */
static int       g_gl_page_closed;                                       /* (frames since a placement page closed, counting down) */
static int       g_gl_pcap = -1;                                        /* THE PATCH: the fine moment whose boxes the last frame painted live - its pixels and grass bits
                                                                            copied into g_gl_bake / g_gl_pmask, so its bake is the splice alone (-1: none) */
static void gl_patch_reset(void) { g_gl_patch = false; g_gl_nwin = 0; g_gl_pwin = 0; g_gl_pcap = -1; g_gl_patch_page = false; }
static void gl_ring_reset(void) { g_gl_dqn = 0; g_gl_dhead = g_gl_dtail = 0; g_gl_dused = 0; }
static void gl_meta_free(void) { if (g_gl_meta && g_gl_free) g_gl_free(g_gl_meta); g_gl_meta = NULL; g_gl_fgen = NULL; g_gl_dlen = NULL; g_gl_dq = NULL; g_gl_pm_old = NULL; g_gl_wrow = NULL; }
static int  gl_exact(void) {                                             /* moments baked at the look as it is */
    int r = 0, nf = g_gl_n * g_gl_f;
    if (!g_gl_fgen) return 0;
    for (int j = 0; j < nf; j++) r += g_gl_ok[j / g_gl_f] && g_gl_fok[j] == 1 && g_gl_fgen[j] == g_gl_gen;
    return r;
}
static bool gl_serving(void) {                                           /* every band and every moment there (at this look, or stale inside the boxes only) */
    int nf = g_gl_n * g_gl_f;
    if (!nf) return false;
    for (int j = 0; j < nf; j++) if (!g_gl_ok[j / g_gl_f] || g_gl_fok[j] != 1) return false;
    return true;
}
static size_t gl_ring_room(size_t *off) {                                /* the words free at the head */
    if (g_gl_dqn == 0) { g_gl_dhead = g_gl_dtail = 0; }
    *off = g_gl_dhead;
    return g_gl_dhead >= g_gl_dtail ? g_gl_dwords - g_gl_dhead : g_gl_dtail - g_gl_dhead;
}
static bool gl_ring_wrap(size_t *off, size_t *cap) {                     /* the head's room ran out at the arena's end: from 0 to the tail instead, when that is more */
    if (g_gl_dqn == 0 || g_gl_dhead < g_gl_dtail || g_gl_dtail <= *cap) return false;
    *off = 0; *cap = g_gl_dtail; return true;
}
static void gl_ring_commit(int j, size_t off, size_t len) {
    g_gl_doff[j] = (uint32_t)off; g_gl_dlen[j] = (uint32_t)len;
    g_gl_dq[g_gl_dqn++] = (uint8_t)j; g_gl_dhead = off + len; g_gl_dused += len;
    if (g_gl_dqn == 1) g_gl_dtail = off;
}
static void gl_ring_drop(int j) {
    int i = 0; while (i < g_gl_dqn && g_gl_dq[i] != j) i++;
    if (i == g_gl_dqn) return;
    g_gl_dused -= g_gl_dlen[j];
    for (; i + 1 < g_gl_dqn; i++) g_gl_dq[i] = g_gl_dq[i + 1];
    g_gl_dqn--;
    if (g_gl_dqn == 0) g_gl_dhead = g_gl_dtail = 0; else g_gl_dtail = g_gl_doff[g_gl_dq[0]];
}
/* the ring PACKED: every live difference moved down in the order written, the holes (places of differences written anew at the
   head) closed - when the head's room runs out (the order shown is not the ring's: a moment's new difference that outgrew its
   old place went to the head and left a hole, and the tail moves only as the oldest is replaced). Never while the ring is
   wrapped (then the order written is not the order in memory); ~480 KB moved once in a while, never a second copy */
static bool gl_ring_pack(void) {
    if (g_gl_dqn == 0 || g_gl_dhead < g_gl_dtail) return false;
    size_t at = 0;
    for (int i = 0; i < g_gl_dqn; i++) {
        const int j = g_gl_dq[i];
        if (g_gl_doff[j] != at) memmove(g_gl_diff + at, g_gl_diff + g_gl_doff[j], (size_t)g_gl_dlen[j] * 2);
        g_gl_doff[j] = (uint32_t)at; at += g_gl_dlen[j];
    }
    g_gl_dtail = 0; g_gl_dhead = at; g_gl_dused = at;
    return true;
}
static void gl_davg_update(void) { size_t tot = 0; for (int i = 0; i < g_gl_dqn; i++) tot += g_gl_dlen[g_gl_dq[i]]; if (g_gl_dqn) g_gl_davg = tot / (size_t)g_gl_dqn; }
/* a piece's box in the scene from its item, its centre and its depth (pieces_build's rule), with its contact shadow's rim */
static void pc_box_of(int item, int cx, int z, int *x0, int *y0, int *x1, int *y1) {
    int fy = DECOR_FY(z), w, h, extra = 4;
    switch (item) {
        case 2:  w = CASTLE_MASK_W; h = CASTLE_ROWS; extra = 12; break;
        case 3:  w = CORAL_W; h = CORAL_H; break;
        case 4:  w = CL_W; h = CL_H; break;
        case 9:  w = VO_W; h = VO_H; break;
        default: w = PK_W; h = PK_H; break;
    }
    *x0 = cx - w / 2; *y0 = fy - h; *x1 = *x0 + w - 1 + PC_SHADOW_FAR; *y1 = *y0 + h + extra - 1 + PC_SHADOW_FAR;
}
static uint32_t gl_skip_items(const tank_t *t, int b) {                  /* the items a bed's spans keep off (veg_skip_for, by item) */
    uint32_t m = veg_skip_for(t, b), r = 0;
    for (int k = 0; k < g_npc; k++) if (m >> k & 1) r |= 1u << g_pc[k].item;
    return r;
}
static void gl_state_record(const tank_t *t) {                           /* the look the loop is baked toward, for the next window */
    g_gl_sig = g_sig; g_gl_dim_baked = g_scene_dim; g_gl_step_baked = g_veg_step; g_gl_mcx_baked = g_veg_mask_cx; g_gl_ashade_baked = g_veg_arch_shade;
    g_gl_under_baked = bed_ord(t, 0) == 3;
    for (int b = 0; b < VEG_BEDS_MAX; b++) {
        bool back = b < tank_veg_beds(t) && bed_z(t, b) == DECOR_Z_BACK;
        g_gl_bed[b].back = back;
        if (!back) continue;
        tank_veg_bed(t, b, &g_gl_bed[b].x0, &g_gl_bed[b].x1, NULL, &g_gl_bed[b].n);
        g_gl_bed[b].kind = tank_veg_kind(t, b); g_gl_bed[b].skip = gl_skip_items(t, b);
    }
}
/* a box into the window, merged with any it meets (a bed's strip, a piece's box; clipped to the band) - false: more boxes than GL_WIN_MAX */
static bool gl_rect_add(gl_rect_t *w, int *n, int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (x1 > TANK_W - 1) x1 = TANK_W - 1;
    if (y0 < g_gl_y0) y0 = g_gl_y0;
    if (y1 > TANK_H - 1) y1 = TANK_H - 1;
    if (x0 > x1 || y0 > y1) return true;                                 /* (nothing of it in the band) */
    for (;;) {
        int i = 0;
        while (i < *n && !(x0 <= w[i].x1 + 1 && x1 >= w[i].x0 - 1 && y0 <= w[i].y1 + 1 && y1 >= w[i].y0 - 1)) i++;
        if (i == *n) break;
        if (w[i].x0 < x0) x0 = w[i].x0;
        if (w[i].x1 > x1) x1 = w[i].x1;
        if (w[i].y0 < y0) y0 = w[i].y0;
        if (w[i].y1 > y1) y1 = w[i].y1;
        w[i] = w[--*n];                                                  /* (the merged box can meet another: round again) */
    }
    if (*n >= GL_WIN_MAX) return false;
    w[*n].x0 = (short)x0; w[*n].y0 = (short)y0; w[*n].x1 = (short)x1; w[*n].y1 = (short)y1; (*n)++;
    return true;
}
static int gl_top(const tank_t *t);
/* the window's COLUMNS: per band row up to two stale intervals - what a box's repaint, the frame's and the bake's, and the splice
   really touch. A piece's are its mask's rows, the body's reach on each row and PC_SHADOW_FAR right and down for its contact
   shadow's rim (the runs a piece splits and shades lie there); a bed's strip or a piece without a mask to read is its box whole */
static void gl_wrow_reset(void) { if (g_gl_wrow) for (int r = 0; r < TANK_H; r++) { g_gl_wrow[r * 4] = 1; g_gl_wrow[r * 4 + 1] = 0; g_gl_wrow[r * 4 + 2] = 1; g_gl_wrow[r * 4 + 3] = 0; } }
static void gl_wrow_add(int y, int x0, int x1) {
    if (!g_gl_wrow || y < g_gl_y0 || y >= g_gl_y0 + g_gl_rows) return;
    if (x0 < 0) x0 = 0;
    if (x1 > TANK_W - 1) x1 = TANK_W - 1;
    if (x0 > x1) return;
    uint16_t *iv = g_gl_wrow + (y - g_gl_y0) * 4;
    int a[3], b[3], n = 0;                                               /* the row's intervals and the new one, sorted; the nearest pair merged while there are three */
    for (int q = 0; q < 4; q += 2) if (iv[q] <= iv[q + 1]) { a[n] = iv[q]; b[n] = iv[q + 1]; n++; }
    a[n] = x0; b[n] = x1; n++;
    for (int i = 1; i < n; i++) { int ta = a[i], tb = b[i], j = i; while (j > 0 && a[j - 1] > ta) { a[j] = a[j - 1]; b[j] = b[j - 1]; j--; } a[j] = ta; b[j] = tb; }
    for (;;) {                                                           /* overlapping or touching: one */
        bool again = false;
        for (int i = 0; i + 1 < n && !again; i++) if (a[i + 1] <= b[i] + 1) { if (b[i + 1] > b[i]) b[i] = b[i + 1]; for (int k = i + 1; k + 1 < n; k++) { a[k] = a[k + 1]; b[k] = b[k + 1]; } n--; again = true; }
        if (!again) break;
    }
    while (n > 2) {                                                      /* three apart: the nearest two become one */
        int best = 0; for (int i = 1; i + 1 < n; i++) if (a[i + 1] - b[i] < a[best + 1] - b[best]) best = i;
        b[best] = b[best + 1]; for (int k = best + 1; k + 1 < n; k++) { a[k] = a[k + 1]; b[k] = b[k + 1]; } n--;
    }
    iv[0] = (uint16_t)a[0]; iv[1] = (uint16_t)b[0];
    if (n > 1) { iv[2] = (uint16_t)a[1]; iv[3] = (uint16_t)b[1]; } else { iv[2] = 1; iv[3] = 0; }
}
static const uint16_t *gl_wrow_of(int y) { return g_gl_wrow && y >= g_gl_y0 && y < g_gl_y0 + g_gl_rows ? g_gl_wrow + (y - g_gl_y0) * 4 : NULL; }
static void gl_wrow_rect(int x0, int y0, int x1, int y1) { for (int y = y0; y <= y1; y++) gl_wrow_add(y, x0, x1); }
/* a piece's footprint from its mask (rows of `w` px from (x0, y0), `nw` words a row): each row's reach, with the rim */
static void gl_wrow_mask(const uint32_t *m, int nw, int rows, int x0, int y0, int w) {
    for (int r = 0; r < rows; r++) {
        const uint32_t *row = m + (size_t)r * nw; int lo = -1, hi = -1;
        for (int wd = 0; wd < nw; wd++) { if (!row[wd]) continue; int a = wd * 32 + __builtin_ctz(row[wd]), b = wd * 32 + 31 - __builtin_clz(row[wd]); if (lo < 0) lo = a; hi = b; }
        if (lo < 0 || lo >= w) continue;
        if (hi > w - 1) hi = w - 1;
        for (int d = 0; d <= PC_SHADOW_FAR; d++) gl_wrow_add(y0 + r + d, x0 + lo, x0 + hi + PC_SHADOW_FAR);
    }
}
/* a piece's columns into the window: its mask's (the one in ds()->pm for the scene as it is, g_gl_pm_old's for the one before it -
   when that is the loop's) or, with no mask to read, its box whole */
static void gl_wrow_piece(int item, int cx, int z, bool old) {
    int x0, y0, x1, y1; pc_box_of(item, cx, z, &x0, &y0, &x1, &y1);
    const int w = x1 - x0 + 1 - PC_SHADOW_FAR, rows = y1 - y0 + 1 - PC_SHADOW_FAR;
    if (item == 2) { gl_wrow_mask(&g_castle_mask[0][0], 224 / 32, CASTLE_ROWS + 12, x0, y0, w); return; }
    const uint32_t *m = NULL;
    if (!old) m = &ds()->pm[pc_mslot(item)][0][0];
    else if (g_gl_pm_old && !memcmp(&g_gl_pm_old_sig, &g_gl_sig, sizeof g_gl_sig)) m = g_gl_pm_old + (size_t)pc_mslot(item) * (VO_H + 4) * ((CASTLE_MASK_W + 31) / 32);
    if (m) gl_wrow_mask(m, (CASTLE_MASK_W + 31) / 32, rows, x0, y0, w);
    else gl_wrow_rect(x0, y0, x1, y1);
}
/* the boxes the look changed inside since the loop was baked toward g_gl_sig (into w, on top of any there) - false: not a
   change of boxes (the dim, the grass in row pairs, the band's top; or more of them than the window holds, or most of
   the band): the loop is baked whole */
static bool gl_window(const tank_t *t, const uint8_t seg[VEG_BEDS_MAX][VEG_FRONDS_MAX], gl_rect_t *w, int *nw) {
    if (g_scene_dim != g_gl_dim_baked || g_veg_step != g_gl_step_baked || g_sig.front != g_gl_sig.front || g_sig.lx != g_gl_sig.lx || g_sig.bg != g_gl_sig.bg || g_sig.bgok != g_gl_sig.bgok) return false;   /* (a new background: the whole loop) */
    if (gl_top(t) != g_gl_y0) return false;
    bool castle = false;
    /* the pieces, by ITEM: one whose place, depth, growth or color changed puts its old box and its new one in; one that left or
       came, its one box; one unchanged whose order against a piece whose box meets its own flipped (ranks swapped), both boxes.
       (By place in the list, as first written, a piece sold or hidden dragged every piece after it in: on Strato's square
       `pumpkin off` took the volcano's whole box with it - 32 ms a patched moment, 9 fps) */
    for (int item = 0; item < 16; item++) {
        int io = -1, ic = -1;
        for (int i = 0; i < g_gl_sig.n; i++) if (g_gl_sig.item[i] == item) io = i;
        for (int i = 0; i < g_sig.n; i++) if (g_sig.item[i] == item) ic = i;
        if (io < 0 && ic < 0) continue;
        bool changed = io < 0 || ic < 0 || g_sig.cx[ic] != g_gl_sig.cx[io] || g_sig.z[ic] != g_gl_sig.z[io] || g_sig.q[ic] != g_gl_sig.q[io] || g_sig.col[ic] != g_gl_sig.col[io];
        if (!changed) {
            int ax0, ay0, ax1, ay1; pc_box_of(item, g_sig.cx[ic], g_sig.z[ic], &ax0, &ay0, &ax1, &ay1);
            for (int j = 0; j < g_sig.n && !changed; j++) {
                if (j == ic) continue;
                int jo = -1; for (int i = 0; i < g_gl_sig.n; i++) if (g_gl_sig.item[i] == g_sig.item[j]) jo = i;
                if (jo < 0 || (j < ic) == (jo < io)) continue;               /* (a piece new to the scene: its own box holds what it paints over) */
                int bx0, by0, bx1, by1; pc_box_of(g_sig.item[j], g_sig.cx[j], g_sig.z[j], &bx0, &by0, &bx1, &by1);
                changed = bx0 <= ax1 && bx1 >= ax0 && by0 <= ay1 && by1 >= ay0;
            }
        }
        if (!changed) continue;
        int x0, y0, x1, y1;
        if (io >= 0) { pc_box_of(item, g_gl_sig.cx[io], g_gl_sig.z[io], &x0, &y0, &x1, &y1); if (!gl_rect_add(w, nw, x0, y0, x1, y1)) return false; gl_wrow_piece(item, g_gl_sig.cx[io], g_gl_sig.z[io], true); }
        if (ic >= 0) { pc_box_of(item, g_sig.cx[ic], g_sig.z[ic], &x0, &y0, &x1, &y1); if (!gl_rect_add(w, nw, x0, y0, x1, y1)) return false; gl_wrow_piece(item, g_sig.cx[ic], g_sig.z[ic], false); }
        castle |= item == 2;
    }
    if ((g_veg_mask_cx != g_gl_mcx_baked || g_veg_arch_shade != g_gl_ashade_baked) && !castle) return false;   /* (the arch's rules moved without the castle: not understood) */
    for (int b = 0; b < VEG_BEDS_MAX; b++) {                              /* the beds: one in or out of the back grass, moved, a frond more; the pieces its spans keep off */
        const bool back = b < tank_veg_beds(t) && bed_z(t, b) == DECOR_Z_BACK, was = g_gl_bed[b].back;
        float x0 = 0, x1 = 0; int nf = 0; uint32_t skip = 0;
        if (back) { tank_veg_bed(t, b, &x0, &x1, NULL, &nf); skip = gl_skip_items(t, b); }
        if (back != was || (back && (g_gl_bed[b].kind != (int)tank_veg_kind(t, b) || g_gl_bed[b].n != nf || g_gl_bed[b].x0 != x0 || g_gl_bed[b].x1 != x1))) {
            if (was && !gl_rect_add(w, nw, (int)g_gl_bed[b].x0 - GL_BED_PAD, g_gl_y0, (int)g_gl_bed[b].x1 + GL_BED_PAD, TANK_H - 1)) return false;
            if (back && !gl_rect_add(w, nw, (int)x0 - GL_BED_PAD, g_gl_y0, (int)x1 + GL_BED_PAD, TANK_H - 1)) return false;
            if (was) gl_wrow_rect((int)g_gl_bed[b].x0 - GL_BED_PAD, g_gl_y0, (int)g_gl_bed[b].x1 + GL_BED_PAD, TANK_H - 1);
            if (back) gl_wrow_rect((int)x0 - GL_BED_PAD, g_gl_y0, (int)x1 + GL_BED_PAD, TANK_H - 1);
            continue;
        }
        if (!back) continue;
        for (uint32_t d = skip ^ g_gl_bed[b].skip; d; d &= d - 1) {       /* a piece newly kept off or no longer: its box, where it stood and where it stands */
            const int item = __builtin_ctz(d); int bx0, by0, bx1, by1;
            for (int i = 0; i < g_sig.n; i++) if (g_sig.item[i] == item) { pc_box_of(item, g_sig.cx[i], g_sig.z[i], &bx0, &by0, &bx1, &by1); if (!gl_rect_add(w, nw, bx0, by0, bx1, by1)) return false; gl_wrow_piece(item, g_sig.cx[i], g_sig.z[i], false); }
            for (int i = 0; i < g_gl_sig.n; i++) if (g_gl_sig.item[i] == item) { pc_box_of(item, g_gl_sig.cx[i], g_gl_sig.z[i], &bx0, &by0, &bx1, &by1); if (!gl_rect_add(w, nw, bx0, by0, bx1, by1)) return false; gl_wrow_piece(item, g_gl_sig.cx[i], g_gl_sig.z[i], true); }
        }
        for (int i = 0; i < nf && i < VEG_FRONDS_MAX; i++)                 /* a frond taller or shorter: its strip */
            if (seg[b][i] != g_gl_seg[b][i]) { float bx; tank_veg_frond(t, b, i, &bx); if (!gl_rect_add(w, nw, (int)bx - GL_BED_PAD, g_gl_y0, (int)bx + GL_BED_PAD, TANK_H - 1)) return false; gl_wrow_rect((int)bx - GL_BED_PAD, g_gl_y0, (int)bx + GL_BED_PAD, TANK_H - 1); }
    }
    if ((bed_ord(t, 0) == 3) != g_gl_under_baked && tank_veg_beds(t) > 3 && bed_z(t, 3) == DECOR_Z_BACK) {   /* THE BED ORDER flipped (0.4.1, the plant's page): the
                                                                                    plant's strip - where its leaves and the seagrass cross, every bed repainted in the new order */
        float x0, x1; tank_veg_bed(t, 3, &x0, &x1, NULL, NULL);
        if (!gl_rect_add(w, nw, (int)x0 - GL_BED_PAD, g_gl_y0, (int)x1 + GL_BED_PAD, TANK_H - 1)) return false;
        gl_wrow_rect((int)x0 - GL_BED_PAD, g_gl_y0, (int)x1 + GL_BED_PAD, TANK_H - 1);
    }
    if (!*nw) return false;                                              /* (the look changed and no box says where: not understood) */
    long area = 0;
    for (int r = 0; r < g_gl_rows; r++) { const uint16_t *iv = g_gl_wrow + r * 4; for (int q = 0; q < 4; q += 2) if (iv[q] <= iv[q + 1]) area += iv[q + 1] - iv[q] + 1; }
    return area * 100 < (long)TANK_W * g_gl_rows * GL_WIN_MOST;
}
static uint32_t gl_stale_check(const tank_t *t) {
    static uint8_t seg[VEG_BEDS_MAX][VEG_FRONDS_MAX];
    memset(seg, 0, sizeof seg);
    uint32_t sh, hard = gl_key(t, &sh, seg);
    g_gl_full_now = hard ^ (sh * 2654435761u);
    if (hard != g_gl_hard_last) g_gl_still = 0;        /* (the heights alone - a nibble, growth - do not hold a bake up: THE HELD HEIGHTS keep them out of the look) */
    bool stale = hard != g_gl_key;
    if (sh == g_gl_seg_h) g_gl_seg_t = -1;               /* (the heights as baked: nothing waits) */
    else if (!stale) {                                   /* the heights moved: at once, or later */
        int nf = g_gl_n * g_gl_f, jump = 0;
        if (g_gl_seg_t < 0 || t->clock < g_gl_seg_t) g_gl_seg_t = t->clock;   /* (the wait starts with the change) */
        for (int b = 0; b < VEG_BEDS_MAX; b++) for (int i = 0; i < VEG_FRONDS_MAX; i++) { int d = (int)seg[b][i] - (int)g_gl_seg[b][i]; if (d < 0) d = -d; if (d > jump) jump = d; }
        stale = !nf || !gl_serving() || jump >= GL_SEG_JUMP || t->clock - g_gl_seg_t >= GL_SEG_WAIT_S;   /* (a loop stale inside its boxes is serving: it waits like a whole one) */
    }
    if (stale) {
        /* THE PATCH: a loop that is whole (or stale inside boxes already) and a change that is boxes keeps its bands and differences */
        gl_rect_t w[GL_WIN_MAX]; int nw = 0;
        bool partial = g_gl_n > 0 && g_gl_fgen && (g_gl_patch || gl_serving());
        if (partial && !g_gl_patch) gl_wrow_reset();                        /* (a rebake under way keeps its columns: they stay stale) */
        if (partial && g_gl_patch) for (int i = 0; i < g_gl_nwin && partial; i++) partial = gl_rect_add(w, &nw, g_gl_win[i].x0, g_gl_win[i].y0, g_gl_win[i].x1, g_gl_win[i].y1);   /* (a rebake under way: its boxes stay stale) */
        if (partial && g_gl_dqn > 0 && g_gl_dwords - g_gl_dused < 3 * (g_gl_dused / (size_t)g_gl_dqn)) partial = false;   /* the ring with room for fewer than three differences: whole
                                                                                    again, packed from 0 (a share of the arena was wrong: on Strato's square the headroom rule
                                                                                    left the arena at 91% full from the first bake and every move baked whole at 9 fps) */
        if (partial) partial = gl_window(t, seg, w, &nw);
        g_gl_key = hard; g_gl_still = 0; g_gl_gen++;
        memcpy(g_gl_seg, seg, sizeof g_gl_seg); g_gl_seg_h = sh; g_gl_seg_t = -1; g_gl_seg_ok = true;
        if (partial) { memcpy(g_gl_win, w, sizeof w); g_gl_nwin = nw; g_gl_patch = true; g_gl_pwin = 0; g_gl_pcap = -1; if (g_gl_page_closed) g_gl_patch_page = true; }   /* (a frame's captured boxes were the old window's) */
        else {
            for (int k = 0; k < GL_MAX; k++) g_gl_ok[k] = false;
            memset(g_gl_fok, 0, sizeof g_gl_fok); gl_ring_reset(); gl_patch_reset();
        }
        gl_state_record(t);
        g_gl_bs.phase = 0;                                               /* (a sliced bake under way starts over) */
    }
    return hard;
}
/* fine moments a band: GL_FINE by the clock; stepping, the sway's moments over the bands the memory held - so each board's
   sway keeps its period at its frame rate (measured: the 1.8 8 bands x 11 = 88, the pendant 7 x 12 = 84) - no more than
   the differences have fit (g_gl_fcap) */
static int gl_pick_f(int bands) {
    if (!g_gl_lock || bands <= 0) return GL_FINE;
    int f = (g_gl_lock + bands / 2) / bands;
    if ((!g_gl_fcap_n || g_gl_fcap_n == bands) && f > g_gl_fcap) f = g_gl_fcap;   /* (an overflow's cap holds at the count it was measured at) */
    if (g_gl_fsel_n == bands && f > g_gl_fsel) f = g_gl_fsel;
    return f < 2 ? 2 : f;
}
#define GL_TOP_ROOM 16                             /* rows the band takes above the tallest frond's tip (THE PATCH: a frond a segment taller - every
                                                      half hour on a growing tank - stays boxes for the loop, not a new band; the band never shrinks) */
static int gl_top(const tank_t *t) {               /* the band's first row */
    int top = TANK_H;
    g_gl_top_bed = -1; g_gl_top_segs = 0; g_gl_top_n = tank_veg_beds(t);
    for (int b = 0; b < tank_veg_beds(t); b++) {
        if (bed_z(t, b) != DECOR_Z_BACK) continue;
        int n; tank_veg_bed(t, b, NULL, NULL, NULL, &n);
        for (int i = 0; i < n; i++) {
            float bx; int segs = tank_veg_frond(t, b, i, &bx);
            if (segs < 1) segs = 1;
            if (segs > VEG_MAX_SEGS) segs = VEG_MAX_SEGS;
            int y = (int)(TANK_BOT - 16 - (segs - 1) * VEG_SEG_DY - VEG_SEG_RY) - 3;
            if (y < top) { top = y; g_gl_top_bed = b; g_gl_top_segs = segs; }
        }
    }
    if (top >= TANK_H) top = TANK_H - 16;
    if (g_gl_n > 0 && top >= g_gl_y0) return g_gl_y0;   /* the band as it is holds the grass */
    uint32_t ck = 2166136261u;                       /* THE CEILING (0.4.1, D4): the tallest the back grass ever grows by itself - each frond at its
                                                        own ceiling (tank_veg_cap; a pure function of the bed and the slot), or as tall as it stands if
                                                        staged over it (top, above). The band's room to grow stops there: on the pendant's tallest grass
                                                        the room was 40 rows a band no frond could reach (~37 KB each, ~160 KB of the loop - the moments'
                                                        arena). Worked out again only when the beds change */
    for (int b = 0; b < tank_veg_beds(t); b++) { int n; tank_veg_bed(t, b, NULL, NULL, NULL, &n); ck = (ck ^ (uint32_t)(b | n << 4 | bed_z(t, b) << 12)) * 16777619u; }
    if (ck != g_gl_ceil_key) {
        g_gl_ceil_key = ck; g_gl_ceil_y = TANK_H;
        for (int b = 0; b < tank_veg_beds(t); b++) {
            if (bed_z(t, b) != DECOR_Z_BACK) continue;
            int n; tank_veg_bed(t, b, NULL, NULL, NULL, &n);
            for (int i = 0; i < n; i++) {
                int segs = tank_veg_cap_segs(b, i);
                if (segs < 1) segs = 1;
                if (segs > VEG_MAX_SEGS) segs = VEG_MAX_SEGS;
                int y = (int)(TANK_BOT - 16 - (segs - 1) * VEG_SEG_DY - VEG_SEG_RY) - 3;
                if (y < g_gl_ceil_y) g_gl_ceil_y = y;
            }
        }
    }
    int ceil = g_gl_ceil_y < top ? g_gl_ceil_y : top;
    if (g_gl_ceil_old) ceil = 0;                     /* (PT_GL_CEIL_OLD=1: the room as before, the control) */
    if (ceil < 0) ceil = 0;
    while (ceil > 0 && (ceil * TANK_W * 2) % 64) ceil--;
    g_gl_ceil_rows = TANK_H - ceil;
    top -= GL_TOP_ROOM;
    if (top < ceil) top = ceil;                      /* (no room above the ceiling: no frond reaches it) */
    if (top < 0) top = 0;
    while (top > 0 && (top * TANK_W * 2) % 64) top--;   /* the DMA's copies start on a 64-byte line */
    return top;
}
#define GL_META_TAB   ((GL_MAX * GL_FMAX) * (2 + 4 + 1))                /* THE PATCH's tables: each fine moment's generation, its difference's length, the ring's order ... */
#define GL_META_BYTES ((GL_META_TAB + GL_PM_BYTES + (size_t)TANK_H * 8 + 63) & ~(size_t)63)   /* ... the pieces' masks as baked (GL_PM_BYTES, with the scene's signature) and the window's columns per band row */
static bool gl_geometry(const tank_t *t) {         /* the band the back grass needs, and memory for the loop */
    const int top = gl_top(t);
    int rows = TANK_H - top;
    if (rows > g_gl_cap) {                           /* (taller grass: the loop again, with room to grow) */
        gl_release();
        if (g_gl_nomem && rows >= g_gl_nomem && g_gl_nomem_t >= 0 && t->clock >= g_gl_nomem_t && t->clock - g_gl_nomem_t < GL_RETRY_S)
            return false;                            /* (a band this tall or taller found no memory: a smaller one is tried at once, the same again
                                                        GL_RETRY_S later - 0.3.9.4: a first plan at a boot saw a band of
                                                        428 rows on Strato's square and every right-sized one after it was refused) */
        int cap = rows + 24 > TANK_H ? TANK_H : rows + 24;
        if (cap > g_gl_ceil_rows) cap = rows > g_gl_ceil_rows ? rows : g_gl_ceil_rows;   /* (THE CEILING: no room past the tallest the grass grows) */
        size_t band = (((size_t)cap * TANK_W * 2) + 127) & ~(size_t)63, mask = (size_t)cap * DIRTY_WORDS_PER_ROW * 4;
        size_t diff = band / 20;                     /* a fine moment's difference, about (they ran ~6 KB a 206 KB band on the square): the plan's floor */
        size_t dwant = diff;                         /* ... and what the last whole loop measured, a quarter over: the room asked for when there is room.
                                                        (A first cut made the measure the FLOOR too: after a replan on Strato's square the arena at that floor
                                                        failed the PSRAM headroom rule, the plan gave up with four bands fewer, and the loop was gone for the
                                                        day - the grass live at 11 fps and no one the wiser until the log) */
        if (g_gl_davg && g_gl_davg * 2 * 5 / 4 > dwant) dwant = g_gl_davg * 2 * 5 / 4;
        long n = ((long)g_gl_budget - (long)(band + mask)) / (long)(band + mask + (GL_FINE - 1) * diff);   /* (less a fine moment's bake) */
        if (g_gl_lock && n > (g_gl_lock + GL_FINE - 1) / GL_FINE) n = (g_gl_lock + GL_FINE - 1) / GL_FINE;   /* (stepping: the moments a sway wants, no more) */
        if (n > GL_MAX) n = GL_MAX;
        if (g_gl_lock && n == GL_MIN_BANDS + 1) {    /* (only three or four: a taller choice re-planned the pendant 48 -> 36, the 1.8 88 -> 84 - more bands
                                                        than memory holds, dropped again, fragment the arena's room) THE MOST MOMENTS (0.4, 11-01): a band costs a dozen moments' differences - on a tall bed (the square's
                                                        403 rows) four bands left the arena two moments a band, three would hold nine. So the count of bands
                                                        whose moments, as the budget holds them, are the most; on short grass that is the most bands, as before */
            const size_t dd = g_gl_fit_avg > diff ? g_gl_fit_avg : diff;   /* (a moment's difference: measured at an overflow, or the plan's floor - the
                                                        estimate the arena is sized by: a more careful one took 36 where 48 fit) */
            long best = -1; int bn = (int)n, bf = 0;
            for (int k = (int)n; k >= GL_MIN_BANDS; k--) {
                long room = (long)g_gl_budget - (long)(k + 1) * (long)(band + mask);
                if (room <= 0) continue;
                long f = 1 + room / ((long)k * (long)dd), fp = (g_gl_lock + k / 2) / k;
                if (fp > GL_FMAX) fp = GL_FMAX;
                if (f > fp) f = fp;
                if (k == g_gl_fcap_n && f > g_gl_fcap) f = g_gl_fcap;   /* (an overflow at this many: what fit) */
                if (f >= 2 && k * f > best) { best = k * f; bn = k; bf = (int)f; }
            }
            if (best > 0) { n = bn; g_gl_fsel = bf; g_gl_fsel_n = bn; }   /* (gl_pick_f: the moments a band the budget holds at this many) */
        }
        if (!(g_gl_meta = g_gl_alloc(GL_META_BYTES))) { g_gl_nomem = rows; g_gl_nomem_t = t->clock; g_gl_why = 1; return false; }
        memset(g_gl_meta, 0, GL_META_BYTES);
        g_gl_fgen = (uint16_t *)g_gl_meta; g_gl_dlen = (uint32_t *)((uint8_t *)g_gl_meta + GL_MAX * GL_FMAX * 2); g_gl_dq = (uint8_t *)g_gl_meta + GL_MAX * GL_FMAX * 6;
        g_gl_pm_old = (uint32_t *)(((uintptr_t)((uint8_t *)g_gl_meta + GL_META_TAB) + 3) & ~(uintptr_t)3); g_gl_wrow = (uint16_t *)((uint8_t *)g_gl_pm_old + GL_PM_BYTES);
        memset(&g_gl_pm_old_sig, 0xff, sizeof g_gl_pm_old_sig);
        g_gl_bake = g_gl_alloc(band); g_gl_bmask = g_gl_alloc(mask); g_gl_pmask = g_gl_alloc(mask);
        int got = 0;
        if (g_gl_bake && g_gl_bmask && g_gl_pmask)   /* as many bands as the platform spares */
            for (; got < n; got++) {
                g_gl_band[got] = g_gl_alloc(band); g_gl_mask[got] = g_gl_alloc(mask);
                if (!g_gl_band[got] || !g_gl_mask[got]) break;
            }
        if (got < n && got >= 0) { if (g_gl_band[got]) g_gl_free(g_gl_band[got]); if (g_gl_mask[got]) g_gl_free(g_gl_mask[got]); g_gl_band[got] = NULL; g_gl_mask[got] = NULL; }
        g_gl_f = gl_pick_f(got);
        size_t dmin = (size_t)got * (g_gl_f - 1) * diff, dbytes = (size_t)g_gl_budget - (size_t)(got + 1) * (band + mask);   /* the differences: the rest of
                                                                                    the budget (a tall bed's run bigger), at least the estimate */
        if ((long)g_gl_budget < (long)((got + 1) * (band + mask)) || dbytes < dmin) dbytes = dmin;
        if (dbytes < (size_t)got * (g_gl_f - 1) * dwant) dbytes = (size_t)got * (g_gl_f - 1) * dwant;   /* (the measure's room, when the headroom allows: the shrink below stops at the floor) */
        while (got >= GL_MIN_BANDS) {                /* (no room for them: fewer moments a band, then a band fewer, and try again) */
            while (!(g_gl_diff = g_gl_alloc(dbytes)) && dbytes > dmin) dbytes = dbytes / 4 * 3 < dmin ? dmin : dbytes / 4 * 3;   /* (the floor itself
                                                        tried last - 0.4, 11-01: it stopped a quarter above it, and the square's grass grown a few rows
                                                        more found no loop where its floor would have fit) */
            if (g_gl_diff) break;
            if (g_gl_lock && got == GL_MIN_BANDS && g_gl_f > GL_F_LEAST) {   /* (at the fewest bands, stepping: fewer moments a band, held at the
                                                        sway's pace (THE PACE) - above it a band fewer keeps more moments, as before: the pendant 48, not 30) */
                g_gl_f--; g_gl_fcap = g_gl_f; g_gl_fcap_n = got; dmin = (size_t)got * (g_gl_f - 1) * diff; dbytes = dmin;
                continue;
            }
            got--; g_gl_free(g_gl_band[got]); g_gl_free(g_gl_mask[got]); g_gl_band[got] = NULL; g_gl_mask[got] = NULL;
            g_gl_f = gl_pick_f(got); dmin = (size_t)got * (g_gl_f - 1) * diff; dbytes = dmin + (band + mask);
        }
        if (got < GL_MIN_BANDS || !g_gl_diff) { g_gl_why = !g_gl_bake || !g_gl_bmask || !g_gl_pmask ? 2 : 3; gl_release(); g_gl_nomem = rows; g_gl_nomem_t = t->clock; return false; }   /* too few to sway smoothly: the grass is drawn the old way */
        for (int k = 0; k < got; k++)                 /* (the odd fronds' masks beside the bands, outside the budget: all or none) */
            if (!(g_gl_omask[k] = g_gl_alloc(mask))) { for (int j = 0; j <= k; j++) { if (g_gl_omask[j]) g_gl_free(g_gl_omask[j]); g_gl_omask[j] = NULL; } break; }
        g_gl_n = got; g_gl_cap = cap; g_gl_dwords = dbytes / 2; gl_ring_reset(); gl_patch_reset(); g_gl_bs.phase = 0;
        g_gl_nomem = 0; g_gl_why = 0;                 /* (planned: a retry that found its memory) */
    }
    if (top != g_gl_y0 || rows != g_gl_rows) { for (int k = 0; k < g_gl_n; k++) g_gl_ok[k] = false; memset(g_gl_fok, 0, sizeof g_gl_fok); gl_ring_reset(); gl_patch_reset(); g_gl_bs.phase = 0; }
    g_gl_y0 = top; g_gl_rows = rows;
    return g_gl_n > 0;
}
/* THE RUN (0.4, 2026-11-01): a difference is, per row, its count of runs, then each run as ONE header word - x (bits 0-8),
   length - 1 (bits 9-13), CARRIED (bit 14: its colors follow), BARE (bit 15: not grass at the moment) - and its colors when
   carried. A bare run's pixels are the scene's (a bake copies the scene and paints only grass), so it carries none and is laid
   from g_scene; were one ever not, it carries them. The runs average 1.2 px (a blade shifts a pixel: one bare, one grass, rarely
   side by side), and the old run - x, length, then colors - spent two words of header on each: the moments ~half the size now,
   twice as many in an arena (the square's tall grass: 15 moments, too steppy - docs/grass-loop-compact.md) */
#define GLR_X(h)     ((h) & 0x1ff)
#define GLR_LEN(h)   ((((h) >> 9) & 31) + 1)
#define GLR_CARRY    0x4000u
#define GLR_BARE     0x8000u
#define GLR_MAXLEN   32
#define GLR_WORDS(h) (1 + (((h) & GLR_CARRY) ? GLR_LEN(h) : 0))
_Static_assert(TANK_W <= 512, "a run's x is 9 bits");
static inline bool gl_run_put(uint16_t *dst, size_t cap, size_t *w, int *run, int *count, int x, uint16_t c, bool bare, uint16_t sc) {   /* pixel x onto the row's runs */
    const uint16_t fl = bare ? (c == sc ? GLR_BARE : (GLR_BARE | GLR_CARRY)) : GLR_CARRY;
    if (*run >= 0) {                                 /* (the open run - the last written - extended when it is the same kind, next to it, not full) */
        const uint16_t h = dst[*run];
        if ((h & (GLR_BARE | GLR_CARRY)) == fl && GLR_X(h) + GLR_LEN(h) == x && GLR_LEN(h) < GLR_MAXLEN) {
            if (fl & GLR_CARRY) { if (*w >= cap) return false; dst[(*w)++] = c; }
            dst[*run] = (uint16_t)(h + (1u << 9));
            return true;
        }
    }
    if (*w + 2 > cap) return false;
    *run = (int)*w; dst[(*w)++] = (uint16_t)(x | fl);
    if (fl & GLR_CARRY) dst[(*w)++] = c;
    (*count)++;
    return true;
}
/* fine moment j's difference from band k, from its bake (g_gl_bake, g_gl_pmask), as THE RUN has it. Rows [r0, r1) appended at w
   (0.3.9.4: a slice at a time): the new size in words, or -1 past `cap` */
static long gl_diff_encode_rows(uint16_t *dst, size_t cap, size_t w, int r0, int r1, int k) {
    for (int r = r0; r < r1; r++) {
        const uint32_t *ma = g_gl_pmask + r * DIRTY_WORDS_PER_ROW, *mb = g_gl_mask[k] + r * DIRTY_WORDS_PER_ROW;
        const uint16_t *pa = g_gl_bake + r * TANK_W, *pb = g_gl_band[k] + r * TANK_W, *ps = g_scene + (size_t)(g_gl_y0 + r) * TANK_W;
        if (w >= cap) return -1;
        size_t at = w++; int n = 0, run = -1;
        for (int wd = 0; wd < DIRTY_WORDS_PER_ROW; wd++) {
            uint32_t any = ma[wd] | mb[wd];          /* (off both masks both are the scene: the same) */
            if (!any) { run = -1; continue; }
            for (int x = wd * 32, e = x + 32 < TANK_W ? x + 32 : TANK_W; x < e; x++) {
                uint32_t bit = 1u << (x & 31); bool ga = ma[wd] & bit;
                if (!((any & bit) && (pa[x] != pb[x] || ga != (bool)(mb[wd] & bit)))) { run = -1; continue; }
                if (!gl_run_put(dst, cap, &w, &run, &n, x, pa[x], !ga, ps[x])) return -1;
            }
        }
        dst[at] = (uint16_t)n;
    }
    return (long)w;
}
static void gl_diff_lay_rows(uint16_t *row, int j) {   /* fine moment j over its band's rows (row: the band's first) */
    const uint16_t *p = g_gl_diff + g_gl_doff[j], *sc = g_scene + (size_t)g_gl_y0 * TANK_W;
    for (int r = 0; r < g_gl_rows; r++, row += TANK_W, sc += TANK_W)
        for (int n = *p++; n > 0; n--) {
            const uint16_t h = *p++; const int x = GLR_X(h), len = GLR_LEN(h);
            if (h & GLR_CARRY) { memcpy(row + x, p, (size_t)len * 2); p += len; }
            else memcpy(row + x, sc + x, (size_t)len * 2);   /* (bare: the scene's) */
        }
}
static void gl_diff_lay(uint16_t *fb, int j) { gl_diff_lay_rows(fb + (size_t)g_gl_y0 * TANK_W, j); }   /* ... over its band's frame */
static void gl_diff_mask(int j, int k) {           /* fine moment j's grass as a mask, in g_gl_bmask: band k's, its difference applied */
    memcpy(g_gl_bmask, g_gl_mask[k], (size_t)g_gl_rows * DIRTY_WORDS_PER_ROW * 4);
    const uint16_t *p = g_gl_diff + g_gl_doff[j];
    for (int r = 0; r < g_gl_rows; r++) {
        uint32_t *m = g_gl_bmask + r * DIRTY_WORDS_PER_ROW;
        for (int n = *p++; n > 0; n--) {
            const uint16_t h = *p; const int x = GLR_X(h), len = GLR_LEN(h); const bool bare = h & GLR_BARE; p += GLR_WORDS(h);
            for (int i = x; i < x + len; i++) { if (bare) m[i >> 5] &= ~(1u << (i & 31)); else m[i >> 5] |= 1u << (i & 31); }
        }
    }
}
bool render_grass_loop_parts(double clock, const uint16_t **scene, size_t *top_bytes, const uint16_t **band, size_t *band_off, size_t *band_bytes, unsigned *ep) {
    if (!g_gl_on || g_gl_bypass || g_gl_dark || g_gl_parked || !g_gl_n || !g_scene || !g_scene_epoch || g_gl_key != g_gl_hard_last) return false;
    int k = gl_base(gl_next(clock));
    if (!g_gl_ok[k]) return false;
    *scene = g_scene; *top_bytes = (size_t)g_gl_y0 * TANK_W * 2;
    *band = g_gl_band[k]; *band_off = *top_bytes; *band_bytes = (((size_t)g_gl_rows * TANK_W * 2) + 63) & ~(size_t)63;
    *ep = 0x40000000u | ((uint32_t)g_gl_ver[k] << 5) | (uint32_t)k;
    return true;
}
static float g_gl_dim_last = 1; static int g_gl_pkz_last;   /* (the last frame's, for a bake outside one) */
/* the bake's recording: the grass's pixels into a band-shaped mask (dm; om: the odd fronds'), none of the frame's marks
   (the band is not the frame - and a bake outside a frame must never see a frame's marks), the sway at moment j */
typedef struct { uint32_t *touched, *bgmask, *dirty, *om; const uint32_t *under; float force; bool rec, fbg; } gl_rec_t;
static void gl_rec_begin(gl_rec_t *s, uint32_t *dm, uint32_t *om, int j) {
    s->touched = g_touched; s->bgmask = g_bgmask; s->dirty = g_dirty; s->om = g_om; s->under = g_under; s->force = g_veg_clock_force; s->rec = g_bg_rec; s->fbg = g_fbg_on;
    g_touched = NULL; g_dirty = NULL; g_under = NULL; g_fbg_on = false;   /* (a frame's marks a span reads: none for a bake, as bake_scene has it) */
    g_bgmask = dm - g_gl_y0 * DIRTY_WORDS_PER_ROW; g_bg_rec = true; g_veg_arch_pass = 0;
    g_om = om ? om - g_gl_y0 * DIRTY_WORDS_PER_ROW : NULL;
    g_veg_clock_force = gl_ftime(j);
    g_veg_seg_hold = g_gl_seg_ok ? g_gl_seg : NULL;
}
static void gl_rec_end(const gl_rec_t *s) { g_veg_clock_force = s->force; g_bg_rec = s->rec; g_bgmask = s->bgmask; g_touched = s->touched; g_dirty = s->dirty; g_om = s->om; g_under = s->under; g_fbg_on = s->fbg; }
static void gl_bed_draw(ctx_t *c, const tank_t *t, int b, int pkz) {   /* one back bed into a band or a box, as the bake paints it */
    g_veg_skip = veg_skip_for(t, b);
    g_pk_shade_all = g_pk_lx >= 0 && (b == 3 ? tank_decor_above(t, 0, 8) : pkz == DECOR_Z_BACK);
    draw_veg(c, t, b, VEG_SEED[b], -1, true); g_pk_shade_all = false;
}
/* THE PATCH: box i of the window repainted at moment j into buf (its rows from buf_y0: a band's g_gl_y0, a frame's 0) - the new
   scene copied in, its grass bits off, the back grass of the moment drawn over it clipped to the box, recorded into dm (and om) */
static void gl_patch_rect(const tank_t *t, uint16_t *buf, int buf_y0, uint32_t *dm, uint32_t *om, int j, float dim, int pkz, int i, int half) {
    gl_rect_t rr = g_gl_win[i]; const gl_rect_t *r = &rr;                  /* (half 0 / 1: its upper / lower rows - a slice each; -1: whole) */
    if (half >= 0) { const int mid = (rr.y0 + rr.y1) / 2; if (half) { if (rr.y1 == rr.y0) return; rr.y0 = (short)(mid + 1); } else rr.y1 = (short)mid; }
    const int wds = r->x1 - r->x0 + 1;
    for (int y = r->y0; y <= r->y1; y++) {                                /* the row's stale columns inside the box: the new scene, the grass bits off */
        const uint16_t *iv = gl_wrow_of(y);
        if (!iv) continue;
        for (int q = 0; q < 4; q += 2) {
            const int a = iv[q] > r->x0 ? iv[q] : r->x0, b = iv[q + 1] < r->x1 ? iv[q + 1] : r->x1;
            if (a > b) continue;
            memcpy(buf + (size_t)(y - buf_y0) * TANK_W + a, g_scene + (size_t)y * TANK_W + a, (size_t)(b - a + 1) * 2);
            mask_clear_run(dm + (size_t)(y - g_gl_y0) * DIRTY_WORDS_PER_ROW, a, b);
            if (om) mask_clear_run(om + (size_t)(y - g_gl_y0) * DIRTY_WORDS_PER_ROW, a, b);
        }
    }
    gl_rec_t s; gl_rec_begin(&s, dm, om, j);
    g_bg_clip = true; g_bg_clip_r = *r; g_veg_cull = true; g_veg_rowclip = gl_wrow_of;
    ctx_t cb = { buf + (size_t)(r->y0 - buf_y0) * TANK_W + r->x0, TANK_W, dim, r->x0, r->y0, wds, r->y1 - r->y0 + 1 };
    for (int kb = 0; kb < tank_veg_beds(t); kb++) {
        const int b = bed_ord(t, kb);
        if (bed_z(t, b) != DECOR_Z_BACK) continue;
        float bx0, bx1; tank_veg_bed(t, b, &bx0, &bx1, NULL, NULL);
        if (bx1 + GL_BED_PAD < r->x0 || bx0 - GL_BED_PAD > r->x1) continue;   /* (the bed clear of the box) */
        gl_bed_draw(&cb, t, b, pkz);
    }
    g_bg_clip = false; g_veg_cull = false; g_veg_rowclip = NULL;
    gl_rec_end(&s);
}
/* THE SPLICE: fine moment j's new difference against band k from its old one - the rows outside the boxes word for word,
   inside them the old runs kept up to a box's edge and the box encoded afresh from g_gl_bake / g_gl_pmask (the moment's
   pixels and grass inside the boxes, gl_patch_rect). Its size in words, or -1 past `cap` */
static bool gl_enc_row(uint16_t *dst, size_t cap, size_t *w, int *count, int r, int a, int b, int k) {   /* row r, x in [a, b] */
    const uint32_t *ma = g_gl_pmask + r * DIRTY_WORDS_PER_ROW, *mb = g_gl_mask[k] + r * DIRTY_WORDS_PER_ROW;
    const uint16_t *pa = g_gl_bake + r * TANK_W, *pb = g_gl_band[k] + r * TANK_W;
    int run = -1;
    for (int x = a; x <= b; x++) {
        const uint32_t bit = 1u << (x & 31); const int wd = x >> 5;
        const uint32_t any = ma[wd] | mb[wd];
        if (!any) { run = -1; x |= 31; continue; }                     /* (no grass in the word in either: on to the next - most of a tall box is bare water) */
        if (!(any & bit)) { run = -1; continue; }
        const bool ga = ma[wd] & bit;
        if (pa[x] == pb[x] && ga == (bool)(mb[wd] & bit)) { run = -1; continue; }
        if (!gl_run_put(dst, cap, w, &run, count, x, pa[x], !ga, g_scene[(size_t)(g_gl_y0 + r) * TANK_W + x])) return false;
    }
    return true;
}
static bool gl_copy_runs(uint16_t *dst, size_t cap, size_t *w, int *count, const uint16_t *runs, int n, int lo, int hi) {   /* the old runs cut to [lo, hi] */
    for (int i = 0; i < n; i++) {
        const uint16_t h = runs[0]; const int x = GLR_X(h), len = GLR_LEN(h); const uint16_t fl = h & (GLR_BARE | GLR_CARRY); const uint16_t *px = runs + 1;
        runs += GLR_WORDS(h);
        const int s = x > lo ? x : lo, e = x + len - 1 < hi ? x + len - 1 : hi;
        if (s > e) continue;
        const size_t carried = (fl & GLR_CARRY) ? (size_t)(e - s + 1) : 0;
        if (*w + 1 + carried > cap) return false;
        dst[(*w)++] = (uint16_t)(s | fl | ((e - s) << 9));
        if (carried) { memcpy(dst + *w, px + (s - x), carried * 2); *w += carried; }
        (*count)++;
    }
    return true;
}
static long gl_diff_splice_rows(uint16_t *dst, size_t cap, size_t w, int r0, int r1, int k, const uint16_t *old0, size_t *op) {   /* rows [r0, r1) appended at w; *op: words of the old one read so far */
    const uint16_t *old = old0 + *op;
    for (int r = r0; r < r1; r++) {
        const int y = g_gl_y0 + r, n = *old++; const uint16_t *runs = old;
        size_t ow = 0; for (int i = 0; i < n; i++) ow += GLR_WORDS(runs[ow]);   /* the old row's words */
        old += ow;
        int ia[2 * GL_WIN_MAX], ib[2 * GL_WIN_MAX], m = 0;                           /* the row's stale columns INSIDE each box, by x: what a repaint painted (gl_patch_rect
                                                                                        clips the row's intervals to its box). A row keeps two intervals and three boxes in a
                                                                                        row merge the nearest two: encoded whole, the gap between those boxes - never painted
                                                                                        in g_gl_bake - went into the moment as stale pixels. Strato's square, 10-31 night: dark
                                                                                        vertical lines up the fronds that stood in such a gap, on their own after growth's
                                                                                        strips or after a move; `--selftest-grasspatch`, three fronds nibbled across the beds */
        { const uint16_t *iv = g_gl_wrow + r * 4;
          for (int i = 0; i < g_gl_nwin; i++) {
              const gl_rect_t *rc = &g_gl_win[i];
              if (y < rc->y0 || y > rc->y1) continue;
              for (int q = 0; q < 4; q += 2) {
                  if (iv[q] > iv[q + 1]) continue;
                  const int a = iv[q] > rc->x0 ? iv[q] : rc->x0, b = iv[q + 1] < rc->x1 ? iv[q + 1] : rc->x1;
                  if (a <= b && m < 2 * GL_WIN_MAX) { ia[m] = a; ib[m] = b; m++; }
              }
          }
          for (int i = 1; i < m; i++) { int ta = ia[i], tb = ib[i], j = i; while (j > 0 && ia[j - 1] > ta) { ia[j] = ia[j - 1]; ib[j] = ib[j - 1]; j--; } ia[j] = ta; ib[j] = tb; } }
        if (!m) { if (w + 1 + ow > cap) return -1; dst[w++] = (uint16_t)n; memcpy(dst + w, runs, ow * 2); w += ow; continue; }
        if (w >= cap) return -1;
        const size_t at = w++; int count = 0, c = 0;
        for (int i = 0; i < m; i++) {
            int a = ia[i], b = ib[i];
            if (a < c) a = c;                                                        /* (boxes never overlap: gl_rect_add merges them) */
            if (a > b) continue;
            if (a > c && !gl_copy_runs(dst, cap, &w, &count, runs, n, c, a - 1)) return -1;
            if (!gl_enc_row(dst, cap, &w, &count, r, a, b, k)) return -1;
            c = b + 1;
        }
        if (c <= TANK_W - 1 && !gl_copy_runs(dst, cap, &w, &count, runs, n, c, TANK_W - 1)) return -1;
        dst[at] = (uint16_t)count;
    }
    *op = (size_t)(old - old0);
    return (long)w;
}
/* the fine moments' room checked early in a whole bake: the first differences say what the rest will take, and a loop that
   will not fit is planned again NOW, with the count that fits - not after it ran out (0.3.9.3: f - 1 at the overflow, and
   the whole loop again, twice on Strato's square: 155 bakes) */
static int gl_fit_f(void) {                        /* fine moments a band whose differences fit the arena, from the live average */
    if (!g_gl_dqn) return g_gl_f - 1;
    size_t avg = g_gl_dused / (size_t)g_gl_dqn; if (!avg) avg = 1;
    long f = (long)(g_gl_dwords * 93 / 100 / (avg * (size_t)g_gl_n)) + 1;
    if (f >= g_gl_f) f = g_gl_f - 1;
    return f < 2 ? 2 : (int)f;
}
/* (an EARLY check from the first few differences replanned a loop that would have fit: the first moments baked after a reset
   are the ones farthest from their bands, the biggest differences - on Strato's square it replanned every 20 s after a
   settle, whole bakes at 8 fps. The check is the overflow's alone now, with the measured fit) */
#define GL_COPY_ROWS 40                            /* rows of the scene copied into a band in a slice (~38 KB on the square) */
#define GL_ENC_ROWS  40                            /* rows of a fine moment encoded in a slice */
/* one SLICE of the whole bake of THE GRASS LOOP's next band or fine moment due (the next one shown first): 2 = a moment finished
   with it, 1 = a slice done, 0 = nothing left to bake. The band (or g_gl_bake) is written in place over the slices: a band's
   g_gl_ok is off until it is done, a fine moment's g_gl_fok */
static int gl_bake_step(const tank_t *t, float dim, int pkz) {
    const int nf = g_gl_n * g_gl_f;
    if (g_gl_bs.phase == 0) {
        const int due = g_gl_lock && g_gl_cur >= 0 && g_gl_cur < nf ? (g_gl_cur + 4) % nf : gl_fine(t->clock + 0.25f);
        int k = gl_base(due), j = -1;
        if (g_gl_ok[k]) { k = -1; for (int i = 0; i < g_gl_n; i++) if (!g_gl_ok[i]) { k = i; break; } }
        if (k < 0) for (int i = 0; i < nf; i++) {
            int f = (due + i) % nf;
            if (g_gl_fok[f]) continue;
            if (f % g_gl_f == 0) { g_gl_fok[f] = 1; g_gl_fgen[f] = g_gl_bgen[f / g_gl_f]; continue; }   /* (a band's own moment: nothing to lay) */
            j = f; break;
        }
        if (k < 0 && j < 0) { gl_davg_update(); return 0; }
        g_gl_bs.k = k; g_gl_bs.j = j; g_gl_bs.phase = 1; g_gl_bs.row = 0; g_gl_bs.bed = 0; g_gl_bs.w = 0;
        memset(k >= 0 ? g_gl_mask[k] : g_gl_pmask, 0, (size_t)g_gl_rows * DIRTY_WORDS_PER_ROW * 4);
        if (k >= 0 && g_gl_omask[k]) memset(g_gl_omask[k], 0, (size_t)g_gl_rows * DIRTY_WORDS_PER_ROW * 4);
    }
    const int k = g_gl_bs.k, j = g_gl_bs.j, m = k >= 0 ? k * g_gl_f : j;
    uint16_t *dst = k >= 0 ? g_gl_band[k] : g_gl_bake; uint32_t *dm = k >= 0 ? g_gl_mask[k] : g_gl_pmask;
    const int64_t q0 = render_clock_us ? render_clock_us() : 0;
    int r = 1;
    if (g_gl_bs.phase == 1) {                                   /* the scene's rows copied in, a slice of rows at a time */
        const int r0 = g_gl_bs.row, r1 = r0 + GL_COPY_ROWS < g_gl_rows ? r0 + GL_COPY_ROWS : g_gl_rows;
        memcpy(dst + (size_t)r0 * TANK_W, g_scene + (size_t)(g_gl_y0 + r0) * TANK_W, (size_t)(r1 - r0) * TANK_W * 2);
        g_gl_bs.row = r1;
        if (r1 >= g_gl_rows) { g_gl_bs.phase = 2; g_gl_bs.bed = 0; }
    } else if (g_gl_bs.phase == 2) {                            /* the back grass of the moment: a bed's upper half, its lower half, a slice each */
        int kb = g_gl_bs.bed / 2;
        while (kb < tank_veg_beds(t) && bed_z(t, bed_ord(t, kb)) != DECOR_Z_BACK) { kb++; g_gl_bs.bed = kb * 2; }
        if (kb < tank_veg_beds(t)) {
            const int b = bed_ord(t, kb);
            const int half = g_gl_bs.bed & 1, ry0 = half ? g_gl_y0 + g_gl_rows / 2 : g_gl_y0, ry1 = half ? g_gl_y0 + g_gl_rows - 1 : g_gl_y0 + g_gl_rows / 2 - 1;
            gl_rec_t s; gl_rec_begin(&s, dm, k >= 0 ? g_gl_omask[k] : NULL, m);
            g_bg_clip = true; g_bg_clip_r.x0 = 0; g_bg_clip_r.x1 = TANK_W - 1; g_bg_clip_r.y0 = (short)ry0; g_bg_clip_r.y1 = (short)ry1; g_veg_cull = true;
            ctx_t cb = { dst + (size_t)(ry0 - g_gl_y0) * TANK_W, TANK_W, dim, 0, ry0, TANK_W, ry1 - ry0 + 1 };
            gl_bed_draw(&cb, t, b, pkz);
            g_bg_clip = false; g_veg_cull = false;
            gl_rec_end(&s);
            g_gl_bs.bed++;
        } else if (k >= 0) {                                    /* a band, done: its fine moments' differences were from the old one */
            g_gl_ok[k] = true; g_gl_ver[k]++; g_gl_bgen[k] = g_gl_gen; g_gl_fok[m] = 1; g_gl_fgen[m] = g_gl_gen;
            for (int i = -GL_DLO; i <= GL_DHI; i++) { int f = (m + i + nf) % nf; if (f != m) { if (g_gl_fok[f] == 1) gl_ring_drop(f); g_gl_fok[f] = 0; } }
            g_gl_bs.phase = 0; r = 2;
        } else { g_gl_bs.phase = 3; g_gl_bs.row = 0; g_gl_bs.w = 0; g_gl_bs.cap = gl_ring_room(&g_gl_bs.off); }
    } else {                                                    /* a fine moment: its difference from its band, a slice of rows at a time, at the ring's head */
        const int r0 = g_gl_bs.row, r1 = r0 + GL_ENC_ROWS < g_gl_rows ? r0 + GL_ENC_ROWS : g_gl_rows;
        long w = gl_diff_encode_rows(g_gl_diff + g_gl_bs.off, g_gl_bs.cap, g_gl_bs.w, r0, r1, gl_base(j));
        if (w < 0 && g_gl_bs.off == g_gl_dhead) {               /* no room at the arena's end: the rows so far moved to 0, where there is more */
            size_t off = g_gl_bs.off, cap = g_gl_bs.cap;
            if (gl_ring_wrap(&off, &cap) && g_gl_bs.w <= cap) {
                memmove(g_gl_diff + off, g_gl_diff + g_gl_bs.off, g_gl_bs.w * 2);
                g_gl_bs.off = off; g_gl_bs.cap = cap;
                w = gl_diff_encode_rows(g_gl_diff + off, cap, g_gl_bs.w, r0, r1, gl_base(j));
            }
        }
        if (w < 0) {                                            /* the arena is full: the moment shows its band's (stepping: a step the eye would catch - planned again, with the count that fits) */
            g_gl_fok[j] = 2; g_gl_bs.phase = 0; r = 2;
            if (g_gl_lock && g_gl_f > 2) { g_gl_fcap = gl_fit_f(); g_gl_fcap_n = g_gl_n; if (g_gl_dqn) g_gl_fit_avg = g_gl_dused / (size_t)g_gl_dqn * 2; g_gl_replan = true; }
        } else {
            g_gl_bs.w = (size_t)w; g_gl_bs.row = r1;
            if (r1 >= g_gl_rows) {
                gl_ring_commit(j, g_gl_bs.off, g_gl_bs.w); g_gl_fok[j] = 1; g_gl_fgen[j] = g_gl_gen;
                g_gl_bs.phase = 0; r = 2;
            }
        }
    }
    if (render_clock_us) g_gl_bake_us += render_clock_us() - q0;
    if (r == 2) g_gl_bakes++;
    return r;
}
static void gl_patch_abort(void) {                  /* the ring could not take a spliced difference: the fine moments are baked whole again (their bands stay, at this look) */
    for (int j = 0; j < g_gl_n * g_gl_f; j++) g_gl_fok[j] = j % g_gl_f == 0 ? 1 : 0;
    gl_ring_reset(); gl_patch_reset(); g_gl_bs.phase = 0;
}
/* one slice of THE PATCH's rebake: a band's box repainted in place (the bands first: a fine moment's new difference is from its
   new band), a fine moment's box into g_gl_bake, or its splice into the ring - the oldest difference first. 2 = a band or moment
   finished, 1 = a slice done, 0 = every moment at this look (the loop exact again) */
static int gl_patch_step(const tank_t *t, float dim, int pkz) {
    const int64_t q0 = render_clock_us ? render_clock_us() : 0;
    int r = 1, k = -1;
    for (int i = 0; i < g_gl_n && k < 0; i++) if (g_gl_bgen[i] != g_gl_gen) k = i;
    if (k >= 0) {
        gl_patch_rect(t, g_gl_band[k], g_gl_y0, g_gl_mask[k], g_gl_omask[k], k * g_gl_f, dim, pkz, g_gl_pwin / 2, g_gl_pwin & 1);
        if (++g_gl_pwin >= 2 * g_gl_nwin) { g_gl_pwin = 0; g_gl_bgen[k] = g_gl_gen; g_gl_ver[k]++; g_gl_fok[k * g_gl_f] = 1; g_gl_fgen[k * g_gl_f] = g_gl_gen; r = 2; }
    } else {
        const int nf = g_gl_n * g_gl_f;
        int j = -1;
        if (g_gl_pcap >= 0 && g_gl_pwin == 0 && g_gl_bs.phase != 3 && g_gl_fok[g_gl_pcap] == 1 && g_gl_fgen[g_gl_pcap] != g_gl_gen) j = g_gl_pcap;   /* the frame painted its boxes: the splice alone */
        else {                                                   /* else the next one due (the next shown first, as the whole bake), its boxes painted a half a slice */
            g_gl_pcap = -1;
            const int due = g_gl_lock && g_gl_cur >= 0 && g_gl_cur < nf ? (g_gl_cur + 1) % nf : gl_fine(t->clock + 0.25f);
            if (g_gl_pwin > 0 || g_gl_bs.phase == 3) j = g_gl_bs.j;   /* (the one under way) */
            else for (int i = 0; i < nf && j < 0; i++) { int f = (due + i) % nf; if (g_gl_fok[f] == 1 && f % g_gl_f != 0 && g_gl_fgen[f] != g_gl_gen) j = f; }
        }
        if (j < 0) { gl_patch_reset(); gl_davg_update(); if (g_gl_dhead >= g_gl_dtail) gl_ring_pack(); return 0; }   /* every moment at this look (and the ring packed: the splices' holes closed) */
        if (j != g_gl_pcap && g_gl_bs.phase != 3 && g_gl_pwin < 2 * g_gl_nwin) { g_gl_bs.j = j; gl_patch_rect(t, g_gl_bake, g_gl_y0, g_gl_pmask, NULL, j, dim, pkz, g_gl_pwin / 2, g_gl_pwin & 1); g_gl_pwin++; }
        else {                                                   /* the splice, GL_ENC_ROWS rows a slice, at the ring's head */
            const int kb = gl_base(j);
            if (g_gl_bs.phase != 3) {
                size_t off, cap = gl_ring_room(&off);
                if (cap < (size_t)g_gl_dlen[j] * 2 + 2048 && gl_ring_pack()) cap = gl_ring_room(&off);   /* room for one bigger than the old, or the holes closed first */
                if (cap < (size_t)g_gl_dlen[j] + 1024) gl_ring_wrap(&off, &cap);
                if (g_gl_abort_next || cap < (size_t)g_gl_dlen[j] + 1024) { g_gl_abort_next = false; g_gl_pwin = 0; g_gl_pcap = -1; gl_patch_abort(); r = 2; goto out; }
                g_gl_bs.phase = 3; g_gl_bs.j = j; g_gl_bs.row = 0; g_gl_bs.w = 0; g_gl_bs.off = off; g_gl_bs.cap = cap; g_gl_bs.op = 0;
            }
            const int r1 = g_gl_bs.row + GL_ENC_ROWS < g_gl_rows ? g_gl_bs.row + GL_ENC_ROWS : g_gl_rows;
            long w = gl_diff_splice_rows(g_gl_diff + g_gl_bs.off, g_gl_bs.cap, g_gl_bs.w, g_gl_bs.row, r1, kb, g_gl_diff + g_gl_doff[j], &g_gl_bs.op);
            if (w < 0) { g_gl_bs.phase = 0; g_gl_pwin = 0; g_gl_pcap = -1; gl_patch_abort(); r = 2; goto out; }
            g_gl_bs.w = (size_t)w; g_gl_bs.row = r1;
            if (r1 >= g_gl_rows) {
                g_gl_bs.phase = 0; g_gl_pwin = 0; g_gl_pcap = -1;
                if (g_gl_bs.w <= g_gl_dlen[j]) {                 /* it fits where the old one was: in place (the moments come in the order shown, not the ring's) */
                    memcpy(g_gl_diff + g_gl_doff[j], g_gl_diff + g_gl_bs.off, g_gl_bs.w * 2);
                    g_gl_dused -= g_gl_dlen[j] - g_gl_bs.w; g_gl_dlen[j] = (uint32_t)g_gl_bs.w;
                } else { gl_ring_drop(j); gl_ring_commit(j, g_gl_bs.off, g_gl_bs.w); }   /* (bigger: at the head; its old place a hole until the tail passes it or the ring is packed) */
                g_gl_fgen[j] = g_gl_gen;
                r = 2;
            }
        }
    }
out:
    if (render_clock_us) g_gl_bake_us += render_clock_us() - q0;
    if (r == 2) g_gl_bakes++;
    return r;
}
static int g_gl_budget_us;                      /* (the tests) a frame's budget for the slices, in place of GL_BAKE_US (0: as built) */
void render_grass_loop_debug_budget(int us) { g_gl_budget_us = us; }
#define GL_BAKE_US 4500                            /* a frame's budget for the bake's slices (the square's frame is ~70 ms: with the live grass or the boxes
                                                      repainted too it holds ~11 fps) ... */
#define GL_SLICES  32                              /* ... or, with no clock (the sim's tests), slices enough for a moment a frame, as before */
static bool g_gl_task;                          /* THE SECOND CORE (0.3.9.4, Strato: "there's got to be a better solution"): the platform bakes the slices on a
                                                   task of its own between the frames (render_grass_loop_bake_slice, under its render lock) - none at a frame's end */
void render_set_grass_loop_task(bool on) { g_gl_task = on; }
static void gl_bake_frame(const tank_t *t, float dim, int pkz) {   /* the end of a frame: slices until the budget is spent (at least one) */
    const int64_t t0 = render_clock_us ? render_clock_us() : 0;
    for (int slices = 0;;) {
        const int r = g_gl_patch ? gl_patch_step(t, dim, pkz) : gl_bake_step(t, dim, pkz);
        if (!r || g_gl_replan) break;
        slices++;
        if (render_clock_us ? render_clock_us() - t0 >= (g_gl_budget_us ? g_gl_budget_us : GL_BAKE_US) : slices >= GL_SLICES) break;
    }
}
/* LOADING (0.3.9.3): a tank's first seconds - its grass not yet baked - are covered by render_loading while the platform
   bakes it at full speed (render_grass_loop_bake_more) instead of under a stuttering tank. Not with the light off (the
   loop sits out), not when the platform could not spare the memory, and only until the loop is first whole */
bool render_grass_loop_warming(void) {
    if (!g_gl_on || g_gl_bypass || g_gl_dark || g_gl_warm || g_gl_nomem) return false;
    int n = g_gl_n * g_gl_f;
    if (n && render_grass_loop_ready() == n) { g_gl_warm = true; return false; }
    return true;
}
int render_grass_loop_bake_slice(const tank_t *t) {       /* THE SECOND CORE: one slice, between frames (the frame's look must have settled: the frame itself counts
                                                              GL_SETTLE); 2 = a moment done with it, 1 = a slice, 0 = nothing to bake (the night, a page's live piece, whole) */
    if (!g_gl_on || g_gl_bypass || g_gl_dark || g_gl_parked || !g_scene || g_gl_still < GL_SETTLE || !gl_geometry(t)) return 0;
    const int r = g_gl_patch ? gl_patch_step(t, g_gl_dim_last, g_gl_pkz_last) : gl_bake_step(t, g_gl_dim_last, g_gl_pkz_last);
    if (g_gl_replan) { g_gl_replan = false; gl_release(); g_gl_nomem = 0; }
    return r;
}
int render_grass_loop_bake_more(const tank_t *t, int n) {     /* up to n whole moments (bands or fine moments), every slice of each */
    if (!g_gl_on || g_gl_bypass || g_gl_dark || !g_scene || g_gl_still < GL_SETTLE || !gl_geometry(t)) return 0;
    int baked = 0;
    while (baked < n && !g_gl_replan) {
        const int r = g_gl_patch ? gl_patch_step(t, g_gl_dim_last, g_gl_pkz_last) : gl_bake_step(t, g_gl_dim_last, g_gl_pkz_last);
        if (!r) break;
        if (r == 2) baked++;
    }
    if (g_gl_replan) { g_gl_replan = false; gl_release(); g_gl_nomem = 0; }
    return baked;
}
/* (the tests) THE PATCH's state: whether the loop is stale inside boxes, and the boxes (x0, y0, x1, y1 each) */
bool render_grass_loop_patching(void) { return g_gl_patch; }
void render_grass_loop_debug_abort(void) { g_gl_abort_next = true; }
int  render_grass_loop_boxes(int box[][4]) {
    for (int i = 0; i < g_gl_nwin; i++) { box[i][0] = g_gl_win[i].x0; box[i][1] = g_gl_win[i].y0; box[i][2] = g_gl_win[i].x1; box[i][3] = g_gl_win[i].y1; }
    return g_gl_nwin;
}
/* (the tests) moment j as the loop holds it: its band's rows with its difference laid (px: g_gl_rows x TANK_W, the band's first
   row first) and its grass mask (the same rows, DIRTY_WORDS_PER_ROW words each) - false: not baked */
bool render_grass_loop_dump(int j, uint16_t *px, uint32_t *mask, int *y0, int *rows) {
    const int nf = g_gl_n * g_gl_f;
    if (j < 0 || j >= nf) return false;
    const int k = gl_base(j);
    if (!g_gl_ok[k] || g_gl_fok[j] != 1) return false;
    memcpy(px, g_gl_band[k], (size_t)g_gl_rows * TANK_W * 2);
    if (j != k * g_gl_f) { gl_diff_lay_rows(px, j); gl_diff_mask(j, k); memcpy(mask, g_gl_bmask, (size_t)g_gl_rows * DIRTY_WORDS_PER_ROW * 4); }
    else memcpy(mask, g_gl_mask[k], (size_t)g_gl_rows * DIRTY_WORDS_PER_ROW * 4);
    *y0 = g_gl_y0; *rows = g_gl_rows;
    return true;
}
/* THE BOOT SHOW (2026-10-31, after mysiak on Discord: "decorations with inhabitants would materialise as they are
   loaded"; common/boot_show.c plays it). The pieces in the tank, back to front as they are drawn */
int render_boot_pieces(const tank_t *t, int items[DECOR_SLOTS]) {
    pieces_build(t, true, true);
    for (int k = 0; k < g_npc; k++) items[k] = g_pc[k].item;
    return g_npc;
}
/* the day scene - water, sand, pieces - with the items in `hide` left out, into dst. It records the pieces' masks as
   it draws them, which the grass loop's bake reads: render_boot_scene_done puts the tank's own back */
void render_boot_scene(const tank_t *t, uint16_t *dst, uint32_t hide) {
    g_boot_hide = hide; pieces_build(t, true, true);
    int keep_lx = g_pk_lx; g_pk_lx = -1;
    bake_scene(t, dst, 1.0f);
    g_pk_lx = keep_lx; g_boot_hide = 0;
}
void render_boot_scene_done(const tank_t *t) {   /* (the scene baked again, as it was: its pieces' masks with it) */
    if (!g_scene || !g_sig_ok) return;
    pieces_build(t, true, g_scene_front);
    bake_scene(t, g_scene, g_scene_dim < 0 ? 1.0f : g_scene_dim);
}
/* the grass loop's baked band nearest the clock's moment: the scene with the back grass over it, rows [*y0, TANK_H).
   NULL while none is baked */
const uint16_t *render_boot_grass(const tank_t *t, int *y0) {
    if (!g_gl_on || g_gl_bypass || !g_gl_n || !g_gl_f) return NULL;
    int k0 = gl_base(gl_fine(t->clock));
    for (int d = 0; d <= g_gl_n / 2; d++) {
        int a = (k0 + d) % g_gl_n, b = (k0 - d + g_gl_n) % g_gl_n;
        if (g_gl_ok[a] && g_gl_band[a]) { *y0 = g_gl_y0; return g_gl_band[a]; }
        if (g_gl_ok[b] && g_gl_band[b]) { *y0 = g_gl_y0; return g_gl_band[b]; }
    }
    return NULL;
}
/* ... and with no loop to read (0.3.9.6: 0.3.9.5 parked the loop, and the show's grass with it; or the loop could not
   have its memory): the same thing drawn live into dst - the scene's rows from *y0 copied in, the back grass of the
   clock's moment painted over them, as the bake paints a band - the way render_tank draws it live when the loop is
   stale or off. dst is the show's own frame; nothing is recorded. false: no scene yet */
bool render_boot_grass_live(const tank_t *t, uint16_t *dst, int *y0) {
    if (!g_scene || !g_sig_ok || !g_scene_epoch) return false;
    int top = TANK_H;
    for (int b = 0; b < tank_veg_beds(t); b++) {
        if (bed_z(t, b) != DECOR_Z_BACK) continue;
        int n; tank_veg_bed(t, b, NULL, NULL, NULL, &n);
        for (int i = 0; i < n; i++) {
            float bx; int segs = tank_veg_frond(t, b, i, &bx);
            if (segs < 1) segs = 1;
            if (segs > VEG_MAX_SEGS) segs = VEG_MAX_SEGS;
            int y = (int)(TANK_BOT - 16 - (segs - 1) * VEG_SEG_DY - VEG_SEG_RY) - 3;
            if (y < top) top = y;
        }
    }
    if (top >= TANK_H) top = TANK_H - 16;
    top -= GL_TOP_ROOM;
    if (top < 0) top = 0;
    memcpy(dst + (size_t)top * TANK_W, g_scene + (size_t)top * TANK_W, (size_t)(TANK_H - top) * TANK_W * sizeof(uint16_t));
    uint32_t *keep_t = g_touched, *keep_d = g_dirty, *keep_om = g_om; const uint32_t *keep_u = g_under;
    const bool keep_rec = g_bg_rec, keep_fbg = g_fbg_on; const int keep_top = g_sg_top; const float keep_force = g_veg_clock_force;
    g_touched = NULL; g_dirty = NULL; g_under = NULL; g_fbg_on = false; g_bg_rec = false; g_om = NULL; g_veg_arch_pass = 0; g_veg_clock_force = -1;
    g_bg_clip = true; g_bg_clip_r.x0 = 0; g_bg_clip_r.x1 = TANK_W - 1; g_bg_clip_r.y0 = (short)top; g_bg_clip_r.y1 = (short)(TANK_H - 1); g_veg_cull = true;
    ctx_t cb = { dst + (size_t)top * TANK_W, TANK_W, g_scene_dim < 0 ? 1.0f : g_scene_dim, 0, top, TANK_W, TANK_H - top };
    const int pkz = g_pk_k >= 0 ? g_pc[g_pk_k].z : DECOR_Z_BACK;
    for (int kb = 0; kb < tank_veg_beds(t); kb++) { const int b = bed_ord(t, kb); if (bed_z(t, b) == DECOR_Z_BACK) gl_bed_draw(&cb, t, b, pkz); }
    g_bg_clip = false; g_veg_cull = false;
    g_touched = keep_t; g_dirty = keep_d; g_om = keep_om; g_under = keep_u; g_bg_rec = keep_rec; g_fbg_on = keep_fbg; g_sg_top = keep_top; g_veg_clock_force = keep_force;
    *y0 = top;
    return true;
}
/* SWIM DEPTH (0.3.9.4, docs/swim-depth.md): one animal drawn at its own depth - against the back grass (dep: IN FRONT,
   BEHIND, THROUGH) and the pieces (pdep: IN FRONT, BEHIND) and a bed IN FRONT. Before it is drawn, what is under its box
   (last frame's drawn box bb with room for a frame's travel, or r round cx, cy) is kept; after, the grass's pixels it is
   behind come back (every frond, or the odd ones), and the pieces' it passes behind (pc_body_covers: a piece IN FRONT
   from the scene, one BEHIND from what was under it). IN FRONT of a piece IN FRONT, or of a bed IN FRONT, it marks its
   pixels (g_keep, g_fbg_*): the pieces' restore, their overlays, the arch's shade and the bed keep off them.
   The frame's masks: g_dm_* (set in render_tank). true: it drew - its box in bb */
static const uint32_t *g_dm_all, *g_dm_odd; static int g_dm_y0;
static bool g_dbg_grass_on; static const uint32_t *g_dbg_dm; static int g_dbg_dm_y0;   /* (tests: the frame's grass mask kept for render_debug_grass_at) */
static bool g_dbg_root_cut;                                                              /* (tests: the grass's put-back over an animal cut at the roots again, as 0.3.9.4 had it) */
void render_debug_root_cut(bool on) { g_dbg_root_cut = on; }
void render_debug_grass_mask(bool on) { g_dbg_grass_on = on; }
bool render_debug_grass_at(int x, int y) { return g_dbg_dm && (unsigned)x < (unsigned)TANK_W && y >= g_dbg_dm_y0 && y < TANK_H && ((g_dbg_dm[(y - g_dbg_dm_y0) * DIRTY_WORDS_PER_ROW + (x >> 5)] >> (x & 31)) & 1); }
static int g_fbed_n, g_fbed_x0[VEG_BEDS_MAX], g_fbed_x1[VEG_BEDS_MAX];
static uint32_t g_fbed_over;                         /* the pieces (g_pc bits) a bed IN FRONT stands before: ranked below it */
static bool g_dep_pieces;
static const tank_t *g_dp_crab; static uint32_t g_dp_crab_under;   /* (THE CRAB'S STACK in the put-back, 0.4 #help "Shrimp shadows?": the crab roosting in a
                                                        castle IN FRONT, drawn before the animal pass, and the pieces IN FRONT it stands before - its castle, the ones
                                                        ranked below it: one of those put back over an animal behind it comes from the scene, which has no crab) */
static uint32_t g_dep_over;                          /* (the caller's, for one dep_draw: pieces that stand before it WHOLE whatever its depths - the seahorse's
                                                        bed's stack, 0.3.9.7 - put back from what was under it; cleared by dep_draw) */
static bool g_dbg_sh_stack_off;                      /* (tests: the seahorse's bed's stack ignored, as 0.3.9.6 drew it) */
void render_debug_seahorse_stack_off(bool on) { g_dbg_sh_stack_off = on; }
static bool g_dbg_castle_row_only;                 /* (tests / director: 0.3.9.6's castle - only its front row over an animal behind the grass) */
void render_debug_castle_row_only(bool on) { g_dbg_castle_row_only = on; }
static bool g_dbg_fish_slot_order;                 /* (tests: the animal pass by the old ranks and slots alone, whatever their depths - before the pendant's fix) */
void render_debug_fish_slot_order(bool on) { g_dbg_fish_slot_order = on; }
static bool g_dbg_dep_home_only;                   /* (tests: only the home fish's depths ask for the grass masks, as before 0.4) */
void render_debug_dep_home_only(bool on) { g_dbg_dep_home_only = on; }
/* an animal's LAYER in the animal pass, back to front (docs/render-stack.md ranks 4-7): g its depth against the grass, p
   against the pieces - or DEP_LANE, its back lane (behind the pieces IN FRONT, before the pieces BEHIND). One stack (tank.h
   depth_piece_cover): the pieces BEHIND under the grass, the pieces IN FRONT over it, so
     L0 behind the grass and the pieces BEHIND        (BEHIND / BEHIND)
     L1 behind the grass, before the pieces BEHIND    (BEHIND / IN FRONT, BEHIND / its lane)
     L2 through the grass                             (THROUGH, whatever p: behind every piece IN FRONT, before every one BEHIND)
     L3 before the grass, behind the pieces IN FRONT  (IN FRONT / BEHIND, IN FRONT / its lane)
     L4 before the grass and every piece              (IN FRONT / IN FRONT) */
static inline int anim_layer(int g, int p) {
    if (g == FISH_DEPTH_THROUGH) return 2;
    const bool lane = p > FISH_DEPTH_THROUGH;        /* (DEP_LANE and its kin) */
    if (g == FISH_DEPTH_BEHIND) return !lane && p == FISH_DEPTH_BEHIND ? 0 : 1;
    return lane || p == FISH_DEPTH_BEHIND ? 3 : 4;
}
static uint32_t veg_skip_for(const tank_t *t, int b);   /* (below) */
static uint32_t g_dep_over_row;                      /* (... and the castle's front row alone before it: the seahorse on its rear tower) */
static uint32_t sh_stack(const tank_t *t) {          /* THE BED'S STACK (tank.h sh_obed): the pieces before the bed of the frond it is coiled on;
                                                        THE PIECE'S STACK (sh_opost, 0.4): the pieces before the piece whose post it holds -
                                                        and the castle's front row before its rear tower (g_dep_over_row) */
    g_dep_over_row = 0;
    if (g_dbg_sh_stack_off) return 0;
    if (t->sh_obed >= 0 && t->sh_obed < tank_veg_beds(t)) return veg_skip_for(t, t->sh_obed);
    if (t->sh_opost < 0) return 0;
    uint32_t m = 0;
    for (int k = 0; k < g_npc && k < 32; k++) {
        if (!g_pc[k].baked && g_pc[k].item != 2) continue;              /* (drawn live this frame: over it anyway, or dragged) */
        if (g_pc[k].item == t->sh_opost) { if (t->sh_orow && g_pc[k].item == 2) g_dep_over_row |= 1u << k; continue; }
        if (tank_decor_above(t, g_pc[k].item, t->sh_opost)) m |= 1u << k;
    }
    return m;
}
/* THE CRAB'S STACK (Strato 2026-11-02, the watch): the crab roosting in a shelter is INSIDE it - at the shelter's place in the stack.
   The pieces ranked above its shelter (tank_decor_above) stand before it whole, the ones below stand behind it - the castle and the
   volcano (tank.c crab_cave takes the frontmost of those), and the coral, the cluster and the pumpkin too. The crab is painted only
   where none of the pieces before it covers (g_cr_clip, draw_crab: nothing to put back, the grass and the lantern's glow before those
   pieces stay as they are); the pieces IN FRONT behind it keep their restore (g_rs_crab) and their overlays (g_guard) off it */
static uint32_t crab_stack(const tank_t *t) {
    if (g_rs_old || t->cr_cave < 0 || !tank_decor_in(t, t->cr_cave)) return 0;
    uint32_t m = 0;
    for (int k = 0; k < g_npc && k < 32; k++) {
        if (!g_pc[k].baked && g_pc[k].item != 2) continue;              /* (drawn live this frame: over it anyway, or dragged) */
        if (g_pc[k].item != t->cr_cave && tank_decor_above(t, g_pc[k].item, t->cr_cave)) m |= 1u << k;
    }
    return m;
}
typedef void (*dep_draw_t)(ctx_t *c, const tank_t *t, int i);
/* (DEP_ROOT_Y is render.h's: the tests read it) */
#define DEP_LANE 0xff                                /* (pdep: an animal whose lane decides the pieces - the seahorse, the floor critters) */
#define DEP_NOBED 0xfe                               /* (... and inside a piece: a crab in its shelter - a bed IN FRONT stays over it) */
#define DEP_LANE_F 0xfd                              /* (... in its FRONT lane: before every piece IN FRONT; DEP_LANE is the back lane, behind them) */
static void dep_fish(ctx_t *c, const tank_t *t, int i) { draw_fish(c, t, &t->fish[i]); }
static void draw_seahorse(ctx_t *c, const tank_t *t);   /* below */
static void dep_seahorse(ctx_t *c, const tank_t *t, int i) { (void)i; draw_seahorse(c, t); }
static short g_hbb[4]; static bool g_hbb_ok;         /* the seahorse's drawn box, the last frame */
static void draw_crab(ctx_t *c, const tank_t *t);    /* below */
static void draw_urchin(ctx_t *c, const tank_t *t);
static void dep_crab(ctx_t *c, const tank_t *t, int i) { (void)i; draw_crab(c, t); }
static void dep_snail(ctx_t *c, const tank_t *t, int i) { (void)i; draw_snail(c, t, true); }
static void dep_urchin(ctx_t *c, const tank_t *t, int i) { (void)i; draw_urchin(c, t); }
static short g_cbb[4], g_snbb[4], g_ubb[4]; static bool g_cbb_ok, g_snbb_ok, g_ubb_ok;   /* the floor critters' drawn boxes */
#define DEP_CRAB(c, fb, t)   dep_draw((c), (fb), (t), 0, dep_crab, g_cbb, &g_cbb_ok, (t)->cr_x, CR_FLOOR_Y - (t)->cr_y - 12, 44, (t)->cr_gdepth, \
                                      tank_crab_in_shelter(t) && (t)->cr_find <= 0 ? DEP_NOBED : DEP_LANE)
#define DEP_SNAIL(c, fb, t)  dep_draw((c), (fb), (t), 0, dep_snail, g_snbb, &g_snbb_ok, (t)->snail_x, (t)->snail_y, 34, (t)->snail_gdepth, DEP_LANE)
#define DEP_URCHIN(c, fb, t) dep_draw((c), (fb), (t), 0, dep_urchin, g_ubb, &g_ubb_ok, (t)->urchin_x, URCHIN_FLOOR_Y + (t)->urchin_dy - 6, 32, (t)->urchin_gdepth, DEP_LANE)
/* ... in the FRONT slot (their front lane: before the pieces IN FRONT, so before the grass those stand before): in front of it */
#define DEP_CRAB_F(c, fb, t)   dep_draw((c), (fb), (t), 0, dep_crab, g_cbb, &g_cbb_ok, (t)->cr_x, CR_FLOOR_Y - (t)->cr_y - 12, 44, FISH_DEPTH_FRONT, \
                                        tank_crab_in_shelter(t) && (t)->cr_find <= 0 ? DEP_NOBED : DEP_LANE_F)
#define DEP_SNAIL_F(c, fb, t)  dep_draw((c), (fb), (t), 0, dep_snail, g_snbb, &g_snbb_ok, (t)->snail_x, (t)->snail_y, 34, FISH_DEPTH_FRONT, DEP_LANE_F)
#define DEP_URCHIN_F(c, fb, t) dep_draw((c), (fb), (t), 0, dep_urchin, g_ubb, &g_ubb_ok, (t)->urchin_x, URCHIN_FLOOR_Y + (t)->urchin_dy - 6, 32, FISH_DEPTH_FRONT, DEP_LANE_F)
static src_t g_shr_pal[SHRIMP_TONES];                /* (this frame's shrimp tones, for dep_shrimp) */
static void draw_shrimp(ctx_t *c, const shrimp_t *q, const src_t *pal);   /* below */
static void dep_shrimp(ctx_t *c, const tank_t *t, int i) { draw_shrimp(c, &t->shrimp[i], g_shr_pal); }
static short g_sbb[SHRIMP_MAX][4]; static bool g_sbb_ok[SHRIMP_MAX];   /* each shrimp's drawn box, the last frame */
static bool dep_draw(ctx_t *c, uint16_t *fb, const tank_t *t, int i, dep_draw_t draw, short bb[4], bool *bb_ok,
                     float cx, float cy, int r, int dep, int pdep) {
    const bool grass = dep != FISH_DEPTH_FRONT && g_dm_all;
    const uint32_t *dm_all = g_dm_all, *dm_odd = g_dm_odd; const int dm_y0 = g_dm_y0;
    int bx0 = 0, by0 = 0, bx1 = -1, by1 = -1;
    uint32_t pk_behind = 0, pk_front = 0, pk_over = 0;   /* the pieces its box meets: to pass behind, or to stay in front of (IN FRONT ones);
                                                        whole before it (g_dep_over) */
    uint32_t pk_whole = 0;                           /* (of pk_behind: all of it over the animal, the castle's keep too - else its front row) */
    const uint32_t over_row = g_dep_pieces ? g_dep_over_row : 0; g_dep_over_row = 0;   /* (of over: by its front row - the castle's) */
    const uint32_t over = g_dep_pieces ? (g_dep_over | over_row) : 0; g_dep_over = 0;
    if (grass || (g_dep_pieces && pdep <= FISH_DEPTH_BEHIND) || over) {   /* its box: last frame's, with room for a frame's travel */
        if (*bb_ok) { bx0 = bb[0] - 8; by0 = bb[1] - 8; bx1 = bb[2] + 8; by1 = bb[3] + 8; }   /* (a dash is ~6 px a frame) */
        else { bx0 = (int)cx - r; bx1 = (int)cx + r; by0 = (int)cy - r; by1 = (int)cy + r; }
        if (bx0 < 0) bx0 = 0;
        if (bx1 > TANK_W - 1) bx1 = TANK_W - 1;
        if (by0 < 0) by0 = 0;
        if (by1 > TANK_H - 1) by1 = TANK_H - 1;
        if (g_dep_pieces && pdep <= FISH_DEPTH_BEHIND) for (int k = 0; k < g_npc && k < 32; k++) {   /* (pdep DEP_LANE: its lane decides the pieces) */
            const piece_t *p = &g_pc[k];
            if (!p->baked || p->x0 > bx1 || p->x0 + p->w <= bx0 || p->y0 > by1 || p->y0 + p->rows <= by0) continue;
            /* one stack, never a loop (Strato on the square 10-31: mira behind the grass but over the pumpkin IN FRONT - her fins
               between its edge and the fronds, bits of frond floating over her): a piece IN FRONT stands before the grass, so an
               animal behind or through the grass is behind it too; a piece BEHIND stands behind the grass, so one in front of
               the grass is in front of it too. Only the rest is the animal's choice */
            const int cover = depth_piece_cover(p->z, p->item == 2, dep, pdep);   /* (tank.h: behind a piece BEHIND only when it chose both,
                                                                         behind the whole castle IN FRONT when behind or through the grass; a depth
                                                                         change that changes this over a piece waits - tank.c pieces_keep) */
            if (cover) pk_behind |= 1u << k;
            else if (p->z == DECOR_Z_FRONT) pk_front |= 1u << k;
            if (cover == 2 && !(g_dbg_castle_row_only && p->item == 2)) pk_whole |= 1u << k;
        }
        for (int k = 0; over && k < g_npc && k < 32; k++) {   /* (whole before it: the castle's keep and towers too, not just its front row) */
            const piece_t *p = &g_pc[k];
            if (!(over >> k & 1) || p->x0 > bx1 || p->x0 + p->w <= bx0 || p->y0 > by1 || p->y0 + p->rows <= by0) continue;
            pk_over |= 1u << k; pk_behind &= ~(1u << k); pk_front &= ~(1u << k);
        }
        if (!grass && !pk_behind && !pk_front && !pk_over) bx1 = -1;   /* (nothing to be behind, nothing to stay in front of) */
        if (bx1 < bx0 || by1 < by0 || (bx1 - bx0 + 1) * (by1 - by0 + 1) > DEP_CAP_PX || by1 - by0 + 1 > DEP_CAP_ROWS) bx1 = -1;
    }
    const int bw = bx1 - bx0 + 1, w0 = bx0 >> 5, nw = bx1 >= 0 ? (bx1 >> 5) - w0 + 1 : 0;
    const bool keep_px = grass || pk_behind || pk_over;   /* (pixels to put back; only the dirty bits to stay in front) */
    int64_t dq0 = bx1 >= 0 ? PROF_MARK() : 0;
    if (bx1 >= 0) for (int y = by0; y <= by1; y++) {   /* what is under it, kept - the rows anything comes back to */
        if (g_dirty) memcpy(g_dep_dirty + (size_t)(y - by0) * DIRTY_WORDS_PER_ROW, g_dirty + (size_t)y * DIRTY_WORDS_PER_ROW + w0, (size_t)nw * 4);
        if (!keep_px) continue;
        if (!pk_behind && !pk_over) {                /* (behind the grass alone: the rows with grass in the box) */
            if (y < dm_y0) continue;
            const uint32_t *ma = dm_all + (size_t)(y - dm_y0) * DIRTY_WORDS_PER_ROW; uint32_t any = 0;
            for (int w = w0; w < w0 + nw; w++) any |= ma[w];
            if (!any) continue;
        }
        memcpy(g_dep_save + (size_t)(y - by0) * bw, fb + (size_t)y * TANK_W + bx0, (size_t)bw * 2);
    }
    int64_t dq1 = bx1 >= 0 ? PROF_MARK() : 0;
    if (bx1 >= 0 && pk_front && g_keep) {           /* in front of the pieces IN FRONT: its pixels kept from their restore and overlays */
        if (!g_keep_on) { memset(g_keep, 0, (size_t)TANK_H * DIRTY_WORDS_PER_ROW * 4); g_keep_on = true; }
        g_keep_rec = g_keep;
    }
    if (g_fbed_n && dep != FISH_DEPTH_BEHIND && pdep != DEP_NOBED && !(pk_behind & g_fbed_over) && g_fbg_front) {   /* a bed IN FRONT its box meets: in front
                                                                   of it, or through it (Strato 10-31, the snail on the square: the critters too) - not a crab in its
                                                                   shelter, nor an animal behind a piece the bed stands before (the bed, the piece, the animal: one stack) */
        int gx0 = (int)cx - r, gx1 = (int)cx + r;
        if (*bb_ok) { gx0 = bb[0] - 8; gx1 = bb[2] + 8; }
        bool meets = false;
        for (int q = 0; q < g_fbed_n && !meets; q++) meets = gx1 >= g_fbed_x0[q] && gx0 <= g_fbed_x1[q];
        if (meets && pdep == DEP_LANE)               /* a back lane: behind the pieces IN FRONT - so behind the bed where one it stands before is about */
            for (int k = 0; k < g_npc && k < 32 && meets; k++) {
                const piece_t *p = &g_pc[k];
                if ((g_fbed_over >> k & 1) && gx1 >= p->x0 && gx0 < p->x0 + p->w) meets = false;
            }
        if (meets) {
            if (!g_fbg_used) { memset(g_fbg_front, 0, (size_t)TANK_H * DIRTY_WORDS_PER_ROW * 8); g_fbg_used = true; }   /* (both masks: they sit side by side) */
            g_keep_rec2 = dep == FISH_DEPTH_THROUGH ? g_fbg_thru : g_fbg_front;
        }
    }
    const bool bb_was = g_bb_on;                     /* (the front slot's clearing runs with it on) */
    g_bb_on = true; g_bb_x0 = g_bb_y0 = 1 << 20; g_bb_x1 = g_bb_y1 = -1;
    draw(c, t, i);
    g_bb_on = bb_was; g_keep_rec = g_keep_rec2 = NULL;
    int64_t dq2 = bx1 >= 0 ? PROF_MARK() : 0;
    const bool drew = g_bb_x1 >= g_bb_x0;
    if (drew) { bb[0] = (short)g_bb_x0; bb[1] = (short)g_bb_y0; bb[2] = (short)g_bb_x1; bb[3] = (short)g_bb_y1; }
    *bb_ok = drew;
    if (bx1 < 0) return drew;
    const bool through = dep == FISH_DEPTH_THROUGH && dm_odd;
    /* the grass's own pixels back over it, EVERY ROW - a frond before an animal is before all of it, its foot on the sand
       included. 0.3.9.4 cut the put-back at DEP_ROOT_Y so a critter on the sand kept its feet and shadow over the roots;
       for the urchin (and the snail, the crab, a fish ducked into the grass) that cut every frond mid-body: over its spines
       and the top of the dome, the lower dome over the frond's foot - "a grass frond that should be drawn behind the urchin
       overlaps a bit of the top of the urchin" (Strato on the square, 10-31 night; 0.3.9.6). The crab in a shelter BEHIND
       already came back whole (0.3.9.4 item 21) */
    const int y_cut = g_dbg_root_cut && pdep != DEP_NOBED ? DEP_ROOT_Y : TANK_H;   /* (the A/B: PT_ROOT_CUT=1 in the sim's tests - the 0.3.9.4 cut back; they fail) */
    if (grass) for (int y = by0 < dm_y0 ? dm_y0 : by0; y <= by1 && y < y_cut; y++) {
        const uint32_t *ma = dm_all + (size_t)(y - dm_y0) * DIRTY_WORDS_PER_ROW, *mo = through ? dm_odd + (size_t)(y - dm_y0) * DIRTY_WORDS_PER_ROW : NULL;
        const uint16_t *sv = g_dep_save + (size_t)(y - by0) * bw; uint16_t *row = fb + (size_t)y * TANK_W;
        const uint32_t *sd = g_dep_dirty + (size_t)(y - by0) * DIRTY_WORDS_PER_ROW; uint32_t *dr = g_dirty ? g_dirty + (size_t)y * DIRTY_WORDS_PER_ROW : NULL;
        for (int w = w0; w <= bx1 >> 5; w++) {        /* 32 at a time: only the grass's pixels the frame drew on (dirty) are looked at */
            uint32_t m = ma[w];
            if (mo) m &= mo[w];
            if (dr) m &= dr[w];
            if (w == w0) m &= 0xFFFFFFFFu << (bx0 & 31);
            if (w == bx1 >> 5) m &= 0xFFFFFFFFu >> (31 - (bx1 & 31));
            for (int k = 0; pk_whole && m && k < g_npc && k < 32; k++)   /* (a piece whole over it - the castle's keep for one behind the grass - comes back
                                                                            below, whole: its pixels are left to it) */
                if (pk_whole >> k & 1) m &= ~pc_row_bits(k, w << 5, 32, y);
            while (m) {
                int x = (w << 5) + __builtin_ctz(m); uint32_t bit = m & -m; m &= m - 1;
                if (row[x] == sv[x - bx0]) continue;
                row[x] = sv[x - bx0];
                if (dr && !(sd[w - w0] & bit)) dr[w] &= ~bit;   /* (a pixel that was final before the animal is final again: no second vignette) */
            }
        }
    }
    for (int k = 0; pk_behind && k < g_npc && k < 32; k++) {   /* the pieces it passes behind: theirs back over it - a piece IN FRONT from the scene
                                                                  (what was under it may be a critter in its arch), one BEHIND from what was under
                                                                  it (the back grass stands over it) */
        if (!(pk_behind >> k & 1)) continue;
        const piece_t *p = &g_pc[k];
        const bool from_scene = p->z == DECOR_Z_FRONT;
        int ya = p->y0 > by0 ? p->y0 : by0, yb = p->y0 + p->rows - 1 < by1 ? p->y0 + p->rows - 1 : by1;
        int xa = p->x0 > bx0 ? p->x0 : bx0, xb = p->x0 + p->w - 1 < bx1 ? p->x0 + p->w - 1 : bx1;
        const bool whole = pk_whole >> k & 1;
        const tank_t *crab = from_scene && (g_dp_crab_under >> k & 1) ? g_dp_crab : NULL;   /* (the crab in its castle stands before this piece) */
        for (int y = ya; y <= yb; y++) {
            const uint16_t *sv = g_dep_save + (size_t)(y - by0) * bw; uint16_t *row = fb + (size_t)y * TANK_W;
            const uint32_t *sd = g_dep_dirty + (size_t)(y - by0) * DIRTY_WORDS_PER_ROW; uint32_t *dr = g_dirty ? g_dirty + (size_t)y * DIRTY_WORDS_PER_ROW : NULL;
            for (int xw = xa & ~31; xw <= xb; xw += 32) {   /* 32 at a time: a piece whole by its mask's row, the castle's front row pixel by pixel */
              const int w = xw >> 5;
              uint32_t m = 0;
              if (whole) m = pc_row_bits(k, xw, 32, y);
              else for (int b = xw < xa ? xa - xw : 0; b < 32 && xw + b <= xb; b++) if (pc_body_covers(k, xw + b, y)) m |= 1u << b;
              if (xw < xa) m &= 0xFFFFFFFFu << (xa - xw);
              if (xw + 31 > xb) m &= 0xFFFFFFFFu >> (xw + 31 - xb);
              if (dr) m &= dr[w];
              uint32_t cm = crab ? dpc_word(xw, y) : 0;   /* (the crab's pixels in this word: what was under the animal, not the scene) */
              if (cm) {                                         /* ... and only those the animal drew over this frame (its drawn box): the rest
                                                                   are the crab as it is - nothing to put back, so not looked at again */
                  uint32_t in = 0;
                  if (drew && y >= bb[1] && y <= bb[3]) { const int a = bb[0] - xw, b = bb[2] - xw;
                      if (b >= 0 && a <= 31) in = (b >= 31 ? 0xFFFFFFFFu : (0xFFFFFFFFu >> (31 - b))) & (a <= 0 ? 0xFFFFFFFFu : (0xFFFFFFFFu << a)); }
                  m &= ~(cm & ~in); cm &= in;
              }
              while (m) {
                const int x = xw + __builtin_ctz(m); const uint32_t bit = m & -m; m &= m - 1;
                const bool scene_px = from_scene && (g_dep_keep_old || !keep_at(x, y))   /* a fish drawn before it IN FRONT of the pieces: what was there (that
                                                                       fish), not the scene - 0.3.9.6, Bananna's pendant: a resting fish IN FRONT of the pumpkin had its
                                                                       tail cut by the carved face where a fish behind the pumpkin passed */
                                      && !(cm & bit);   /* ... and the crab roosting in the castle
                                                                       before this piece: what was there (the crab), not the scene - 0.4, abumaia's pendant on #help: the
                                                                       shrimp behind the castle or the volcano under it "cast a shadow on the resting crab, hiding it" */
                uint16_t v = scene_px ? g_scene[y * TANK_W + x] : sv[x - bx0];
                if (row[x] == v) continue;
                row[x] = v;
                if (dr && (scene_px || !(sd[w - w0] & bit))) dr[w] &= ~bit;   /* (the scene's pixel is final) */
              }
            }
        }
    }
    if (g_dp_crab && pk_whole) {                     /* behind the WHOLE castle the crab roosts in (behind or through the grass): behind the crab too, everywhere
                                                        - the arch is open to what is behind the castle, so the animal showed over the crab in it, the grass's
                                                        put-back bringing the crab back in frond-shaped stripes (0.4, #help "Shrimp shadows?", the volcano BEHIND) */
        bool behind_castle = false;
        for (int k = 0; k < g_npc && k < 32; k++) if ((pk_whole >> k & 1) && g_pc[k].item == g_dp_crab->cr_cave) behind_castle = true;
        int ya = g_dpc_y0 > by0 ? g_dpc_y0 : by0, yb = g_dpc_y0 + DPC_H - 1 < by1 ? g_dpc_y0 + DPC_H - 1 : by1;
        int xa = g_dpc_x0 > bx0 ? g_dpc_x0 : bx0, xb = g_dpc_x0 + DPC_W - 1 < bx1 ? g_dpc_x0 + DPC_W - 1 : bx1;
        for (int y = ya; behind_castle && y <= yb; y++) {   /* (the mask's set bits only: the crab, ~600 px, not its box) */
            const uint16_t *sv = g_dep_save + (size_t)(y - by0) * bw; uint16_t *row = fb + (size_t)y * TANK_W;
            const uint32_t *mr = ds()->dpc_m[y - g_dpc_y0];
            for (int w = 0; w < (DPC_W + 31) / 32; w++) {
                uint32_t m = mr[w];
                while (m) {
                    const int x = g_dpc_x0 + (w << 5) + __builtin_ctz(m); m &= m - 1;
                    if (x >= xa && x <= xb && row[x] != sv[x - bx0]) row[x] = sv[x - bx0];   /* (the crab's pixel was drawn this frame: its dirty bit stands) */
                }
            }
        }
    }
    for (int k = 0; pk_over && k < g_npc && k < 32; k++) {   /* the pieces whole before it (g_dep_over): theirs back over it, from what was under it -
                                                                  the piece as this frame had it there, the grass or a fish before it included */
        if (!(pk_over >> k & 1)) continue;
        const piece_t *p = &g_pc[k];
        int ya = p->y0 > by0 ? p->y0 : by0, yb = p->y0 + p->rows - 1 < by1 ? p->y0 + p->rows - 1 : by1;
        int xa = p->x0 > bx0 ? p->x0 : bx0, xb = p->x0 + p->w - 1 < bx1 ? p->x0 + p->w - 1 : bx1;
        for (int y = ya; y <= yb; y++) {
            const uint16_t *sv = g_dep_save + (size_t)(y - by0) * bw; uint16_t *row = fb + (size_t)y * TANK_W;
            const uint32_t *sd = g_dep_dirty + (size_t)(y - by0) * DIRTY_WORDS_PER_ROW; uint32_t *dr = g_dirty ? g_dirty + (size_t)y * DIRTY_WORDS_PER_ROW : NULL;
            for (int x = xa; x <= xb; x++) {
                int w = x >> 5; uint32_t bit = 1u << (x & 31);
                if ((dr && !(dr[w] & bit)) || !((over_row >> k & 1) ? pc_body_covers(k, x, y) : pc_covers(k, x, y))) continue;
                if (row[x] == sv[x - bx0]) continue;
                row[x] = sv[x - bx0];
                if (dr && !(sd[w - w0] & bit)) dr[w] &= ~bit;
            }
        }
    }
    if (render_clock_us) { g_dep_us += (dq1 - dq0) + (PROF_MARK() - dq2); g_dep_n++; }
    return drew;
}
/* (director `pkcheck`, 0.3.9.4: Strato saw fronds over a pumpkin's edges on the square) - every baked moment of the grass
   loop checked against the scene inside a piece's body: pixels where the loop holds grass over the piece. The bands as
   they are; each fine moment laid over its band in scratch (a frame from alloc). Returns the worst count; *bands, *fines:
   how many moments had any */
int render_debug_piece_grass(int item, int *bands, int *fines, void *(*alloc)(size_t), void (*release)(void *)) {
    *bands = *fines = 0;
    int k = -1; for (int q = 0; q < g_npc; q++) if (g_pc[q].item == item) k = q;
    if (k < 0 || !g_gl_n || !g_scene) return -1;
    uint16_t *tmp = alloc ? alloc((size_t)TANK_W * TANK_H * 2) : NULL;
    int worst = 0; const int nf = g_gl_n * g_gl_f;
    for (int j = 0; j < nf; j++) {
        int kb = j / g_gl_f; if (!g_gl_ok[kb]) continue;
        const bool fine = j % g_gl_f != 0;
        if (fine && (!tmp || g_gl_fok[j] != 1)) continue;
        const uint16_t *band = g_gl_band[kb];
        if (fine) { memcpy(tmp + (size_t)g_gl_y0 * TANK_W, band, (size_t)g_gl_rows * TANK_W * 2); gl_diff_lay(tmp, j); }
        int bad = 0;
        for (int r = 0; r < g_gl_rows; r++) for (int x = 0; x < TANK_W; x++) {
            int y = g_gl_y0 + r;
            if (!pc_body_covers(k, x, y)) continue;
            uint16_t v = fine ? tmp[(size_t)y * TANK_W + x] : band[(size_t)r * TANK_W + x];
            if (v != g_scene[(size_t)y * TANK_W + x]) bad++;
        }
        if (bad) { if (fine) (*fines)++; else (*bands)++; }
        if (bad > worst) worst = bad;
    }
    if (tmp && release) release(tmp);
    return worst;
}
/* THE DRIFT (0.4.1, Twilight Deep): specks in the far water - behind the back grass, in front of the background - drawn after
 * the bubbles each frame - sub-pixel (four weighted taps, the larger ones a soft plus), vignetted, MARKED (dirty: the restore puts the scene back next frame, and the pieces IN
 * FRONT are restored over them; touched: the flush sends them). Without the marks the restore loop left ghosts. */
static bool g_specks_off;                        /* (the tests: the frame without its drift, to compare) */
void render_debug_specks_off(bool off) { g_specks_off = off; }
static void bg_speck_px(uint16_t *fb, int x, int y, int a, int dimi) {
    if ((unsigned)x >= TANK_W || y < 0 || y >= (int)FISH_FLOOR_Y - 24 || a <= 2) return;   /* (above the critters' sand lane: down in the
                                                                                               floor's shade a speck is not seen, only in their way) */
    for (int k = 0; k < g_npc; k++) if (pc_covers(k, x, y)) return;   /* never over a piece, in front or behind: the drift is the far water's,
                                                                          behind everything but the backdrop (0.4.1 audit P9, Strato: "make sure
                                                                          they are only in the background" - the pieces are baked into the scene,
                                                                          so the scene test below let ~7% of the specks onto them) */
    const int j = y * TANK_W + x;
    /* in the far water, behind the back grass: only where the frame is still the scene - never on a frond, laid by the loop or
       drawn live (the crab's shelter lays the grass again over it: a speck there came and went with the crab, --selftest-cave;
       live and loop must agree, --selftest-grassloop) */
    if (g_scene && fb[j] != g_scene[j]) return;
    if (g_vig) a = a * (256 - g_vig[j]) >> 8;
    if (a > 255) a = 255;
    uint16_t d = FB_SWAP16(fb[j]);
    const uint32_t c = BG_SPECK_RGB;
    int r = (d >> 11) + ((((int)(c >> 19) & 31) * dimi >> 8) - (d >> 11)) * a / 256;
    int g = ((d >> 5) & 63) + ((((int)(c >> 10) & 63) * dimi >> 8) - ((d >> 5) & 63)) * a / 256;
    int b = (d & 31) + ((((int)(c >> 3) & 31) * dimi >> 8) - (d & 31)) * a / 256;
    fb[j] = FB_SWAP16((uint16_t)(r << 11 | g << 5 | b));
    dirty_px(x, y); touch_px_f(x, y);
}
static void bg_draw_specks(uint16_t *fb, float T, float dim) {
    const int n = bg_specks(g_bg), dimi = (int)(dim * 256);
    for (int i = 0; i < n; i++) {
        float x, y; int a0; bool big;
        bg_speck(g_bg, i, T, &x, &y, &a0, &big);
        if (x < 2 || x >= TANK_W - 3 || y < 1) continue;
        int ix = (int)x, iy = (int)y; float fx = x - ix, fy = y - iy;
        int wq[4] = { (int)((1 - fx) * (1 - fy) * a0), (int)(fx * (1 - fy) * a0), (int)((1 - fx) * fy * a0), (int)(fx * fy * a0) };
        int foot[4][4] = { { 0 } };                     /* the speck's footprint (ix-1..ix+2, iy-1..iy+2): its taps summed, each pixel written once */
        for (int q = 0; q < 4; q++) {
            int px = 1 + (q & 1), py = 1 + (q >> 1);
            foot[py][px] += wq[q];
            if (big) { foot[py][px - 1] += wq[q] / 3; foot[py][px + 1] += wq[q] / 3; foot[py - 1][px] += wq[q] / 3; foot[py + 1][px] += wq[q] / 3; }
        }
        for (int v = 0; v < 4; v++) for (int u = 0; u < 4; u++) if (foot[v][u]) bg_speck_px(fb, ix - 1 + u, iy - 1 + v, foot[v][u], dimi);
    }
}
void render_tank(const tank_t *t, uint16_t *fb, int stride) {
    {   /* the background (0.4.1): a switch tints everything toward its water anew */
        int bg = g_bg_force >= 0 ? g_bg_force : t->bg_now < BG_N ? t->bg_now : BG_OPEN;
        if (bg != g_bg) { g_bg = bg; g_veg_row_dim = -1; g_castle_dim = -1; g_coral_dim = -1; g_pk_dim = -1; g_vo_dim = -1; g_cl_dim = -1; }
    }
    float dim = t->night ? 0.45f : 1.0f;
    g_keep_on = false; g_fbg_on = g_fbg_used = false; g_bedm_on = false;   /* (SWIM DEPTH: set by the fish below, read through the passes after them) */
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

    bool front = cached;                           /* the pieces IN FRONT ride in the scene - a placement page's own piece too, until it is changed on
                                                      the page (dragged, re-ranked, re-depthed, recolored; setup_place_live): from then on that piece
                                                      alone is drawn live, out of the scene, until the page closes. (0.3.9.4. Before, a page up took
                                                      EVERY piece IN FRONT out of the scene, drew them all live every frame with the back grass live too,
                                                      and parked the grass loop - Strato on the square: "the frame rate drops to a crawl") */
    bool sgv = false;                              /* the frame is primed from the grass cache (below) */
    int gl_k = -1, gl_j = -1; bool gl_primed = false, gl_forced = false, gl_patched = false, gl_patched_odd = false; uint32_t gl_key_now = 0;   /* ... or from the grass loop: its moment */
    pieces_build(t, cached, front);
    {   /* THE EDITOR (Strato 10-31): the creatures out of every frame a placement page is up, and until the grass has settled after its DONE -
           they come back with the repaint, not over it */
        static bool was_place;
        const bool place = setup_is_place();
        if (was_place && !place) g_gl_page_closed = 4; else if (g_gl_page_closed) g_gl_page_closed--;
        was_place = place;
        g_no_animals = place && !g_page_animals;     /* (with the bake on the second core they come back at DONE itself; g_gl_patch_page kept for the log) */
    }
    bool pc_live = false;                          /* a piece drawn live this frame (the page's, changed on it) */
    for (int k = 0; k < g_npc; k++) pc_live |= g_pc[k].placing;
    const bool loop_ok = cached && !pc_live;       /* THE GRASS LOOP sits a live piece out: its bands hold the piece where it stood in the scene, so
                                                      they are neither used (the back grass drawn live meanwhile) nor rebaked; the scene's new look is
                                                      taken up when the page closes - one rebake, not two, and none if the piece comes back as it was */
    g_gl_parked = pc_live;
    g_veg_seg_hold = cached && g_gl_on && g_gl_n > 0 && g_gl_seg_ok && !t->night ? g_gl_seg : NULL;   /* THE HELD HEIGHTS, this frame (the night draws live: nothing to disagree with) */
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
        if (g_bg_short && g_bg && ++g_bg_probe_n >= 20) {   /* THE BACKGROUND REFUSED: can the lender give now? (about once a second) */
            g_bg_probe_n = 0;
            void *p = bg_scratch_take();
            if (p) { bg_scratch_give(p); g_bg_short = false; }
        }
        sig.dim = dim; sig.front = front; sig.lx = g_pk_lx; sig.bg = g_bg; sig.bgok = !g_bg_short;
        if (!g_sig_ok || memcmp(&sig, &g_sig, sizeof sig) || g_scene_dim != dim) {
            g_sig = sig; g_sig_ok = true; g_scene_front = front;
            /* rebuild the static scene with the vignette baked in (the
               per-frame pass then only re-darkens dynamic patches) */
            bake_scene(t, g_scene, dim);            /* (it sets g_sig.bgok to what it drew: refused, the scene - and the loop's key - say so) */
            if (g_vig) g_vig_filled = true;
            g_scene_dim = dim; g_scene_epoch++;
        }
        sgv = loop_ok && g_sg_on && g_sg_ok && g_sg_scene_ep == g_scene_epoch;   /* primed with the back grass in it */
        gl_k = -1;
        g_gl_dark = t->night;
        if (loop_ok && g_gl_on && !g_gl_bypass && g_gl_n && !sgv && g_gl_dark) {   /* the light off: live grass, at the stepped moment */
            int j = gl_next(t->clock);
            if (g_gl_lock) gl_stepped(j, t->clock);
            if (g_veg_clock_force < 0) { g_veg_clock_force = gl_ftime(j); gl_forced = true; }   /* (the loop's moments, so the day picks up where the night left it) */
        } else if (loop_ok && g_gl_on && !g_gl_bypass && g_gl_n && !sgv) {    /* THE GRASS LOOP: the frame's band and fine moment, baked */
            gl_key_now = gl_stale_check(t);
            int j = gl_next(t->clock), nf = g_gl_n * g_gl_f;
            if (gl_key_now == g_gl_key) {
                if (g_primed_fb == fb && (g_primed_epoch & 0xC0000000u) == 0x40000000u) {   /* the prefetch chose its band: the fine moment nearest the clock of it */
                    int k = (int)(g_primed_epoch & 31);
                    if (k < g_gl_n && g_gl_ok[k] && g_gl_ver[k] == (uint16_t)(g_primed_epoch >> 5)) {
                        int d = ((j - k * g_gl_f) % nf + nf + nf / 2) % nf - nf / 2;
                        if (d < -GL_DLO) d = -GL_DLO;
                        if (d > GL_DHI) d = GL_DHI;
                        j = (k * g_gl_f + d + nf) % nf; gl_k = k; gl_primed = true;
                    }
                }
                if (gl_k < 0) { int k = gl_base(j); if (g_gl_ok[k]) gl_k = k; }
                if (gl_k >= 0) gl_j = g_gl_fok[j] == 1 ? j : gl_k * g_gl_f;   /* (its difference not baked: the band's own moment) */
                if (gl_k >= 0 && g_gl_lock && g_gl_fok[j] == 0) gl_k = -1;   /* (... stepping, drawn live at its own: a repeated moment is a step) */
            }
            if (g_gl_lock) {
                gl_stepped(j, t->clock);
                if (gl_k < 0 && g_veg_clock_force < 0) { g_veg_clock_force = gl_ftime(j); gl_forced = true; }   /* (stale: the grass drawn live at the
                                                                                    frame's moment all the same - no jump as the loop comes back) */
            }
        }
        if (gl_k >= 0) {
            if (!gl_primed) {
                memcpy(fb, g_scene, (size_t)g_gl_y0 * TANK_W * sizeof(uint16_t));
                memcpy(fb + g_gl_y0 * TANK_W, g_gl_band[gl_k], (size_t)g_gl_rows * TANK_W * sizeof(uint16_t));
            }
            if (gl_j != gl_k * g_gl_f) gl_diff_lay(fb, gl_j);
            sgv = true;                                 /* (the back grass is in the frame: nothing below draws it) */
            if (g_veg_clock_force < 0) { g_veg_clock_force = gl_ftime(gl_j); gl_forced = true; }   /* every reader of the sway at its moment: a seahorse on a
                                                                                                                  blade, a bed IN FRONT */
            if (g_gl_patch && g_gl_fgen[gl_j] != g_gl_gen) {   /* THE PATCH: the moment is stale inside the boxes - repainted there now, its masks with it */
                if (gl_j != gl_k * g_gl_f) gl_diff_mask(gl_j, gl_k); else memcpy(g_gl_bmask, g_gl_mask[gl_k], (size_t)g_gl_rows * DIRTY_WORDS_PER_ROW * 4);
                uint32_t *om = NULL;
                if (g_fl_odd && g_gl_omask[gl_k]) { memcpy(g_fl_odd, g_gl_omask[gl_k], (size_t)g_gl_rows * DIRTY_WORDS_PER_ROW * 4); om = g_fl_odd; }
                for (int i = 0; i < g_gl_nwin; i++) gl_patch_rect(t, fb, 0, g_gl_bmask, om, gl_j, dim, pkz, i, -1);
                gl_patched = true; gl_patched_odd = om != NULL;
                if (gl_j != gl_k * g_gl_f && g_gl_fok[gl_j] == 1 && g_gl_bgen[gl_k] == g_gl_gen && g_gl_pwin == 0 && g_gl_bs.phase != 3 && g_gl_bake && g_gl_pmask) {   /* the boxes as painted are the moment's
                                                                                    bake: into g_gl_bake and its mask, so the bake at the frame's end is the splice alone */
                    for (int i = 0; i < g_gl_nwin; i++) {
                        const gl_rect_t *r = &g_gl_win[i];
                        for (int y = r->y0; y <= r->y1; y++) {
                            const uint16_t *iv = gl_wrow_of(y);
                            if (!iv) continue;
                            for (int q = 0; q < 4; q += 2) {
                                const int a = iv[q] > r->x0 ? iv[q] : r->x0, b = iv[q + 1] < r->x1 ? iv[q + 1] : r->x1;
                                if (a <= b) memcpy(g_gl_bake + (size_t)(y - g_gl_y0) * TANK_W + a, fb + (size_t)y * TANK_W + a, (size_t)(b - a + 1) * 2);
                            }
                            memcpy(g_gl_pmask + (size_t)(y - g_gl_y0) * DIRTY_WORDS_PER_ROW, g_gl_bmask + (size_t)(y - g_gl_y0) * DIRTY_WORDS_PER_ROW, DIRTY_WORDS_PER_ROW * 4);
                        }
                    }
                    g_gl_pcap = gl_j;
                }
            }
        } else { unsigned sep = sgv ? (0x80000000u | g_sg_ep) : g_scene_epoch;
          if (!(g_primed_fb == fb && g_primed_epoch == sep))
              memcpy(fb, sgv ? g_sceneg : g_scene, TANK_W * TANK_H * sizeof(uint16_t)); }
        g_primed_fb = NULL;
        /* a piece BEHIND that is not in the scene (dragged): live, right over it;
           the castle too, whole, when it is not baked (its front row again after
           the fish, below) */
        for (int k = 0; k < g_npc; k++)
            if (!g_pc[k].baked && (g_pc[k].z == DECOR_Z_BACK || g_pc[k].item == 2)) { pc_draw(&c, t, k, true, g_live_rec || (g_pc[k].z == DECOR_Z_BACK && g_pc[k].item != 2)); pc_cover_live(fb, k); }   /* (BEHIND: its mask, for the plant) */
    } else draw_scene(t, fb, stride, dim);
    /* the overlays of the pieces BEHIND: a crown over its baked fan, the
       candle's face over the lantern's body - under the grass */
    if (sgv) {                                       /* the grass is in the frame already: these stay under it */
        if (gl_k >= 0) {                                /* (the loop's: its band's mask - its fine moment's, when anything is under the grass) */
            const uint32_t *m = g_gl_mask[gl_k];
            if (gl_patched) m = g_gl_bmask;             /* (THE PATCH left the moment's mask there, the boxes repainted) */
            else if (gl_j != gl_k * g_gl_f) {
                bool ov = false;
                for (int k = 0; k < g_npc; k++) ov |= g_pc[k].z == DECOR_Z_BACK && (g_pc[k].item == 3 || g_pc[k].item == 4 || g_pc[k].item == 8 || g_pc[k].item == 9);
                if (ov) { gl_diff_mask(gl_j, gl_k); m = g_gl_bmask; }
            }
            g_under = m - g_gl_y0 * DIRTY_WORDS_PER_ROW; g_under_y0 = g_gl_y0;
        }
        else { g_under = g_bgmask; g_under_y0 = 0; }
    }
    for (int k = 0; k < g_npc; k++) {
        if (g_pc[k].z != DECOR_Z_BACK) continue;
        g_clip_k = k;
        if (g_pc[k].item == 3 || g_pc[k].item == 4) PC_CROWN(k);
        else if (g_pc[k].item == 8 && g_pc[k].baked) draw_pumpkin_glow(&c, g_pc[k].cx, anim(t), t->night, false, true);
        else if (g_pc[k].item == 9) PC_VOLCANO(k);
        g_clip_k = -1;
    }
    g_under = NULL; g_under_y0 = 0;
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
    bool dep_any = false;                            /* SWIM DEPTH: a fish BEHIND or THROUGH the grass this frame */
    for (int i = 0; i < N_FISH_SLOTS; i++)           /* (every fish here, visitors too - 0.4: only the home fish counted, so a visitor
                                                        behind the grass with every home fish in front had no grass mask, drawn over the fronds) */
        if (tank_swims(t, i) && (i < t->n_fish || !g_dbg_dep_home_only)) dep_any |= t->fish[i].gdepth != FISH_DEPTH_FRONT;
    if (t->sd_unlocks & SD_ITEM_SHRIMP) dep_any |= t->shr_gdepth != FISH_DEPTH_FRONT;   /* (the shrimp's school, phase 3) */
    if ((t->sd_unlocks & SD_ITEM_SEAHORSE) && t->sh_x >= 0) dep_any |= t->sh_gdepth != FISH_DEPTH_FRONT;   /* (the seahorse) */
    if ((t->sd_unlocks & SD_ITEM_CRAB) && t->cr_x >= 0) dep_any |= t->cr_gdepth != FISH_DEPTH_FRONT;              /* (the floor critters) */
    if (tank_snail_upright(t)) dep_any |= t->snail_gdepth != FISH_DEPTH_FRONT;
    if ((t->sd_unlocks & SD_ITEM_URCHIN) && t->urchin_x >= 0) dep_any |= t->urchin_gdepth != FISH_DEPTH_FRONT;
    dep_any = dep_any && g_dep_save && cached && !g_dep_off;
    g_dep_pieces = g_dep_save && cached && !g_dep_off && g_npc > 0;   /* ... and against the pieces (phase 2) */
    bool fl_live = false;
    if (!sgv) {                                      /* (the grass cache holds them: the frame was primed with them) */
        uint32_t *keep_bm = g_bgmask; bool keep_rec = g_bg_rec; int keep_top = g_sg_top;
        if (dep_any) {                               /* (drawn live: its pixels and its odd fronds' recorded for the fish) */
            memset(g_fl_all, 0, (size_t)TANK_H * DIRTY_WORDS_PER_ROW * 4); memset(g_fl_odd, 0, (size_t)TANK_H * DIRTY_WORDS_PER_ROW * 4);
            g_bgmask = g_fl_all; g_bg_rec = true; g_om = g_fl_odd; fl_live = true;
        }
        for (int kb = 0; kb < tank_veg_beds(t); kb++)   /* a BACK-layer piece first: behind the grass too */
            if (bed_z(t, bed_ord(t, kb)) == DECOR_Z_BACK) VEG_PASS(bed_ord(t, kb), -1);
        if (dep_any) { g_bgmask = keep_bm; g_bg_rec = keep_rec; g_om = NULL; g_sg_top = keep_top; }
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
                              && (t->cr_mode == CR_CLIMB || (t->cr_cave == 9 && tank_crab_in_shelter(t)));
    const bool cr_deep = cr_in && t->cr_find <= 0 && !cr_vol_front && tank_crab_in_shelter(t);   /* (in its shelter: roosting, or at the opening on its way in - 0.4) */
    /* ... and the deep crab follows its SHELTER'S DEPTH against the fronds (Strato 2026-10-24, the watch: the castle IN
       FRONT, the crab in its arch painted behind the fronds seen through it - "the castle is different in that you can
       see all the way to the back fronds through the arch"): a shelter BEHIND stands behind the fronds, so the crab in
       it is under every frond (drawn before the arch pass, the back fronds painted over it again); a castle IN FRONT
       stands before them, so the crab in its arch is over the fronds in the opening - drawn after the arch pass, still
       before the shrimp and the fish, and the castle's front row restores over it where it meets the wall */
    const bool cr_deep_front = cr_deep && t->cr_cave >= 0 && tank_decor_in(t, t->cr_cave) && tank_decor_z(t, t->cr_cave) == DECOR_Z_FRONT;
    const bool cr_deep_back = cr_deep && !cr_deep_front;
    g_cr_clip = (cr_deep || (cr_vol_front && t->cr_mode != CR_CLIMB)) ? crab_stack(t) : 0;   /* (THE CRAB'S STACK: in its shelter, not under the pieces before it) */
    g_dpc_ok = false;                               /* (a new frame: last frame's crab mask no longer holds) */
    g_dp_crab = NULL; g_dp_crab_under = 0;          /* (THE CRAB'S STACK in an animal's put-back: the pieces IN FRONT the crab in a castle IN FRONT stands before) */
    if (cr_deep_front && !g_rs_old && !g_rp_old) {
        for (int k = 0; k < g_npc && k < 32; k++)
            if (g_pc[k].z == DECOR_Z_FRONT && (g_pc[k].item == t->cr_cave || !tank_decor_above(t, g_pc[k].item, t->cr_cave))) g_dp_crab_under |= 1u << k;
        if (g_dp_crab_under) g_dp_crab = t;
    }
    bool cr_top = false, cr_slot = false;                        /* (decided with the lanes, below: with a page up or on the bare path the
                                                                    front volcano's crab goes to the top lane, after the live piece) */
    if (cr_deep_back) {
        /* the frame primed with the back grass in it (THE GRASS LOOP's band, the grass cache): the crab is drawn at
           depth BEHIND the grass - dep_draw puts every frond's pixels back over it from the frame's grass mask - and
           inside its shelter (DEP_NOBED: no piece comes back over it; a bed IN FRONT stays over it). Before, it was
           drawn plain over the primed grass and the weave-back below ran only when the grass was live: on every board
           since the loop (0.3.9.3) a crab in a volcano BEHIND's cave or a castle BEHIND's arch sat on top of the
           fronds (C1704 on Discord 10-31, "the crab always renders in front of the seagrass"; Strato on the 1.8) */
        bool deep_done = false;
        if (sgv && cached && g_dep_save && !g_dep_off) {
            const uint32_t *keep_all = g_dm_all, *keep_odd = g_dm_odd; const int keep_y0 = g_dm_y0;
            if (gl_k >= 0) {                                 /* the loop's mask for this frame's moment (THE PATCH: the boxes repainted, in g_gl_bmask) */
                if (gl_patched) { g_dm_all = g_gl_bmask; g_dm_odd = gl_patched_odd ? g_fl_odd : NULL; }
                else if (gl_j != gl_k * g_gl_f) { gl_diff_mask(gl_j, gl_k); g_dm_all = g_gl_bmask; g_dm_odd = g_gl_omask[gl_k]; }
                else { g_dm_all = g_gl_mask[gl_k]; g_dm_odd = g_gl_omask[gl_k]; }
                g_dm_y0 = g_gl_y0;
            }
            else if (g_bgmask) { g_dm_all = g_bgmask; g_dm_odd = g_fl_odd; g_dm_y0 = 0; }
            if (g_dm_all) {
                dep_draw(&c, fb, t, 0, dep_crab, g_cbb, &g_cbb_ok, t->cr_x, CR_FLOOR_Y - t->cr_y - 12, 44, FISH_DEPTH_BEHIND, DEP_NOBED);
                deep_done = true;
            }
            g_dm_all = keep_all; g_dm_odd = keep_odd; g_dm_y0 = keep_y0;
        }
        if (!deep_done) draw_crab(&c, t);                        /* (its rect is marked in the critter pass) */
        /* THE WEAVE kept (Strato 2026-10-24, the square: one frond still drawn behind the crab in the cave): the
           back fronds - a MIDDLE bed's even ones, a BACK bed's all - again over its rect, after it, not final (the
           sweep vignettes them with the rect), so no frond of any bed sits behind a crab in its cave - the live
           grass's way; with the grass primed the dep_draw above did it */
        if (!sgv && !deep_done) {
            const int feet = (int)floorf(CR_FLOOR_Y - t->cr_y);
            const int wx0 = (int)t->cr_x - 28, wy0 = feet - CR_GH - 8, wx1 = (int)t->cr_x + 28, wy1 = feet + 1;
            ctx_t cw = c; cw.fb = &CTX_PX(&c, wx0, wy0); cw.ox = wx0; cw.oy = wy0; cw.w = wx1 - wx0 + 1; cw.h = wy1 - wy0 + 1;
            for (int kb = 0; kb < tank_veg_beds(t); kb++) {
                const int b = bed_ord(t, kb);
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
    if (cached && bg_specks(g_bg) && !g_specks_off) bg_draw_specks(fb, (float)t->clock, dim);   /* the drift (Twilight Deep) */
    if (cr_deep_front) {                                         /* in a castle IN FRONT's arch: over the fronds seen through it, under the shrimp and the fish (its rect is marked in the critter pass) */
        g_dpc_on = g_dp_crab != NULL;                            /* (its pixels recorded for the animals' put-backs) */
        if (g_dpc_on) { memset(ds()->dpc_m, 0, sizeof ds()->dpc_m); g_dpc_x0 = (int)t->cr_x - DPC_W / 2; g_dpc_y0 = (int)floorf(CR_FLOOR_Y - t->cr_y) - (DPC_H - 16); }
        draw_crab(&c, t);
        if (g_dpc_on) { g_dpc_ok = true; g_dpc_t = t; }
        g_dpc_on = false;
    }
    /* SWIM DEPTH: this frame's grass masks and beds IN FRONT, for every animal's dep_draw below */
    g_dm_all = g_dm_odd = NULL; g_dm_y0 = 0; g_fbed_n = 0; g_fbed_over = 0;
    if (dep_any || g_dbg_grass_on) {
        if (gl_k >= 0) {                             /* the loop's: the fine moment's own mask, the band's odd fronds */
            if (gl_patched) { g_dm_all = g_gl_bmask; g_dm_odd = gl_patched_odd ? g_fl_odd : NULL; }   /* (THE PATCH: both with the boxes repainted) */
            else { if (gl_j != gl_k * g_gl_f) { gl_diff_mask(gl_j, gl_k); g_dm_all = g_gl_bmask; } else g_dm_all = g_gl_mask[gl_k]; g_dm_odd = g_gl_omask[gl_k]; }
            g_dm_y0 = g_gl_y0;
        } else if (fl_live) { g_dm_all = g_fl_all; g_dm_odd = g_fl_odd; }
        else if (sgv && g_bgmask) { g_dm_all = g_bgmask; g_dm_odd = g_fl_odd; }   /* (the grass cache's, recorded at its refresh) */
    }
    if (g_dbg_grass_on) { g_dbg_dm = g_dm_all; g_dbg_dm_y0 = g_dm_y0; }
    if (cached && g_dep_save && !g_dep_off)
        for (int b = 0; b < tank_veg_beds(t); b++) {   /* the beds IN FRONT (the sword plant placed so): their spans */
            if (bed_z(t, b) != DECOR_Z_FRONT || t->veg_growth[b] < VEG_BARE) continue;
            float x0, x1, top; int nf; tank_veg_bed(t, b, &x0, &x1, &top, &nf);
            g_fbed_x0[g_fbed_n] = (int)x0 - 16; g_fbed_x1[g_fbed_n] = (int)x1 + 16; g_fbed_n++;
            uint32_t skip = veg_skip_for(t, b);          /* (the pieces it keeps off: ranked above it, or before its layer) */
            for (int k = 0; k < g_npc && k < 32; k++) if (g_pc[k].z == DECOR_Z_FRONT && !(skip >> k & 1)) g_fbed_over |= 1u << k;
        }
    /* THE LANES' front slot holds only while every piece IN FRONT is in the scene. With a placement page up (a
       piece dragged live, over everything) - or on the bare path, which bakes nothing - a critter in its front
       lane goes to the TOP lane instead, after the live pieces, so it stays before them as it was before the
       baked ones (Strato 2026-10-24, the 1.8: the crab "dropped to the layer below behind the volcano when
       opening the move menu"); over the fronds for as long as the page is up */
    bool lanes = true;
    for (int k = 0; k < g_npc; k++) if (g_pc[k].z == DECOR_Z_FRONT && !g_pc[k].baked) lanes = false;
    const bool snail_lane = lanes && t->snail_front, urchin_lane = lanes && t->urchin_front;
    const bool snail_top = !lanes && t->snail_front, urchin_top = !lanes && t->urchin_front;
    /* the strawberry crab on the floor (0.3.9): its places are decided above (THE CRAB'S PLACES); in the animal pass
       below only its back lane on the sand */
    const bool cr_lane_case = (t->cr_lane_front || t->cr_mode == CR_DIG) && t->cr_mode != CR_CLIMB && !tank_crab_in_shelter(t);
    cr_top = cr_in && (t->cr_find > 0 || (!lanes && (cr_vol_front || cr_lane_case)));   /* a page up / the bare path: the live pieces are drawn over everything, so a crab before one goes after them */
    cr_slot = cr_in && !cr_top && lanes && (cr_vol_front || cr_lane_case);
    const bool sh_in = (t->sd_unlocks & SD_ITEM_SEAHORSE) && t->sh_x >= 0;
    if (t->sd_unlocks & SD_ITEM_SHRIMP) for (int k = 0; k < SHRIMP_TONES; k++) g_shr_pal[k] = src_color(SHRIMP_RGB[k], c.dim);
    PROF_ADD(3, p0);
    /* THE ANIMAL PASS, DEEPEST FIRST (docs/render-stack.md ranks 4-7; Strato 11-02, the pendant: "one fish behind the
       seagrass and one fish in front. when they layer, the seagrass gets the drawing wrong" - and then: the deeper animal
       is always under, every animal). dep_draw lays one animal over what is already drawn and puts the grass and the
       pieces it is behind back over it from what was under it - so an animal drawn after one in front of it went over that
       one everywhere but on the fronds, where the put-back brought it back: the front animal in frond-shaped stripes
       across the one behind. So one pass, sorted back to front by layer (anim_layer: L0 behind the grass and the pieces
       .. L4 before both), the old ranks deciding only at one layer (stable): the school of shrimp (4), the seahorse's
       back lane (5) - before the fish, so a fish passes between it and what it holds (Strato 10-17) - the fish by slot
       (6), visitors too with their badges, then the floor critters' back lane (7): THE STACK of the sand's critters
       (Strato 10-24, the square), the crab lowest, then the snail, then the urchin. The deep crab (1, 3), the front
       slot (9) and the top lane (13) stay where they are. `--selftest-fish-pair` (PT_FISH_PAIR_OLD=1: the ranks alone) */
    enum { AN_SHRIMP, AN_SEAHORSE, AN_FISH, AN_CRAB, AN_SNAIL, AN_URCHIN };
    uint16_t an[N_FISH_SLOTS + 5]; uint8_t al[N_FISH_SLOTS + 5]; int na = 0;
    const bool by_layer = !g_dep_off && !g_dbg_fish_slot_order;
    #define AN_ADD(kind, idx, g, p) do { int j_ = na++; const uint8_t l_ = by_layer ? (uint8_t)anim_layer((g), (p)) : 0;   /* (an insertion, stable: at most a dozen) */ \
                                         while (j_ > 0 && al[j_ - 1] > l_) { an[j_] = an[j_ - 1]; al[j_] = al[j_ - 1]; j_--; } \
                                         an[j_] = (uint16_t)((kind) << 8 | (idx)); al[j_] = l_; } while (0)
    if ((t->sd_unlocks & SD_ITEM_SHRIMP) && t->shrimp_n > 0) AN_ADD(AN_SHRIMP, 0, t->shr_gdepth, t->shr_pdepth);
    if (sh_in && !t->sh_front) AN_ADD(AN_SEAHORSE, 0, t->sh_gdepth, DEP_LANE);
    for (int i = 0; i < N_FISH_SLOTS; i++) if (tank_swims(t, i)) AN_ADD(AN_FISH, i, t->fish[i].gdepth, t->fish[i].pdepth);
    if (cr_in && !cr_top && !cr_deep && !cr_slot) AN_ADD(AN_CRAB, 0, t->cr_gdepth, DEP_LANE);
    if (tank_snail_upright(t) && !snail_lane && !snail_top) AN_ADD(AN_SNAIL, 0, t->snail_gdepth, DEP_LANE);           /* (its front lane: the slot below, or the top lane with a page up) */
    if ((t->sd_unlocks & SD_ITEM_URCHIN) && t->urchin_x >= 0 && !urchin_lane && !urchin_top) AN_ADD(AN_URCHIN, 0, t->urchin_gdepth, DEP_LANE);
    #undef AN_ADD
    for (int q = 0; q < na; q++) {
        const int i = an[q] & 0xff;
        switch (an[q] >> 8) {
        case AN_SHRIMP:                              /* each at the school's depth (SWIM DEPTH phase 3, dep_draw) */
            for (int s = 0; s < t->shrimp_n; s++) {
                dep_draw(&c, fb, t, s, dep_shrimp, g_sbb[s], &g_sbb_ok[s], t->shrimp[s].x, t->shrimp[s].y, 14, t->shr_gdepth, t->shr_pdepth);
                DYN_RECT((int)t->shrimp[s].x - 10, (int)t->shrimp[s].y - 5, (int)t->shrimp[s].x + 10, (int)t->shrimp[s].y + 5);
            }
            PROF_ADD(3, p0); break;
        case AN_SEAHORSE:                            /* its BACK lane: it holds something behind the fish - an even frond, a piece BEHIND;
                                                        its FRONT lane (an odd frond, a piece IN FRONT) is drawn after the pieces in front */
            g_dep_over = sh_stack(t); dep_draw(&c, fb, t, 0, dep_seahorse, g_hbb, &g_hbb_ok, t->sh_x, t->sh_y, 40, t->sh_gdepth, DEP_LANE);
            PROF_ADD(3, p0); break;
        case AN_FISH: {                              /* ours that are here, and visitors (a fish next door is not drawn); a visitor wears its
                                                        home tank's badge, small, above it: a guest, and from where */
            const fish_t *f = &t->fish[i];
            if (dep_draw(&c, fb, t, i, dep_fish, g_fbb[i], &g_fbb_ok[i], f->x, f->y, (int)(62 * f->size) + 8, f->gdepth, f->pdepth))
                DYN_RECT(g_fbb[i][0], g_fbb[i][1], g_fbb[i][2], g_fbb[i][3]);
            if (tank_is_visitor(i) && t->vis_badge[i - N_FISH_MAX] >= 0) {
                g_bb_on = true; g_bb_x0 = g_bb_y0 = 1 << 20; g_bb_x1 = g_bb_y1 = -1;
                link_badge_draw(&c, (int)f->x, (int)(f->y - 15 * f->size - 9), 5, t->vis_badge[i - N_FISH_MAX], 220);
                g_bb_on = false;
                if (g_bb_x1 >= g_bb_x0) DYN_RECT(g_bb_x0, g_bb_y0, g_bb_x1, g_bb_y1);
            }
            PROF_ADD(7, p0); break;                  /* (the split of the stage, 2026-10-23: the fish) */
        }
        case AN_CRAB:   DEP_CRAB(&c, fb, t);   PROF_ADD(8, p0); break;   /* (the snail, the urchin and the crab) */
        case AN_SNAIL:  DEP_SNAIL(&c, fb, t);  PROF_ADD(8, p0); break;
        case AN_URCHIN: DEP_URCHIN(&c, fb, t); PROF_ADD(8, p0); break;
        }
    }
    g_dp_crab = NULL; g_dp_crab_under = 0;          /* (the pass is done: the front slot's own put-backs are not under the crab) */
    /* their rects, wherever they were drawn this frame (the seahorse either lane; the crab in any place) */
    if (sh_in) { int bx0, by0, bx1, by1; seahorse_box(t, &bx0, &by0, &bx1, &by1); DYN_RECT(bx0, by0, bx1, by1); }
    if (cr_in) {
        const int feet = (int)floorf(CR_FLOOR_Y - t->cr_y);
        if (t->cr_tilt == 0) DYN_RECT((int)t->cr_x - 27, feet - CR_GH - (t->cr_find > 0 ? (int)(CR_DISC_UP + CR_DISC_R) + 1 : 0), (int)t->cr_x + 27, feet + 1);   /* (+ the sand dollar over its head) */
        else DYN_RECT((int)t->cr_x - 36, feet - 35, (int)t->cr_x + 36, feet + 36);
    }
    if (tank_snail_upright(t)) DYN_RECT((int)t->snail_x - 21, (int)t->snail_y - 13, (int)t->snail_x + 21, (int)t->snail_y + 7);
    if ((t->sd_unlocks & SD_ITEM_URCHIN) && t->urchin_x >= 0)
        DYN_RECT((int)t->urchin_x - 19, (int)URCHIN_FLOOR_Y - 17, (int)t->urchin_x + 19, (int)(URCHIN_FLOOR_Y + UR_BASE + t->urchin_dy) + 1);
    PROF_ADD(8, p0);
    /* the pieces IN FRONT back over the fish (and the snail, the food, the
       bubbles), only where they were drawn: the scene holds them. The castle's
       front row comes back the same way - a fish in the arch swims THROUGH,
       its keep stays behind the fish. A castle IN FRONT that is not in the
       scene (dragged, or a page up): its front row again, live. */
    for (int k = 0; k < g_npc; k++) {
        if (g_pc[k].z != DECOR_Z_FRONT) continue;
        g_rs_crab = cr_deep_front && !g_rs_old && g_pc[k].item != t->cr_cave && tank_decor_above(t, t->cr_cave, g_pc[k].item) ? t : NULL;   /* (behind the crab's castle) */
        if (g_pc[k].baked) pc_restore(fb, k);
        else if (g_pc[k].item == 2) draw_castle(&c, g_pc[k].cx, 1, cached);
    }
    g_rs_crab = NULL;
    PROF_ADD(9, p0);                                 /* the pieces IN FRONT back over them */
    /* THE FRONT SLOT (2026-10-24, a keeper's ask): the sand's critters in their front lane - over the pieces IN
       FRONT just restored, under the front fronds that follow (woven through the grass like a fish). Their rects
       are marked above. The snail's lane lived above the fronds until today. */
    const bool keep_clr = g_keep_on;                  /* (SWIM DEPTH: what the front slot paints over a fish is its own - the pieces' and the bed's marks) */
    if (keep_clr || g_fbg_used) { g_keep_clr_m = keep_clr ? g_keep : NULL; g_fbg_clr1 = g_fbg_used ? g_fbg_front : NULL; g_fbg_clr2 = g_fbg_used ? g_fbg_thru : NULL;
                                  g_keep_clr = true; g_bb_on = true; g_bb_x0 = g_bb_y0 = 1 << 20; g_bb_x1 = g_bb_y1 = -1; }
    if (cr_slot) {                                                                                 /* the crab lowest (the stack above) */
        /* ... in a front volcano's cave (roost, shelter) it is inside the volcano: a fish passing IN FRONT of the volcano
           stays over it (SWIM DEPTH phase 2; the stack's rule - the crab in its shelter is the deepest thing) */
        const bool hold = g_keep_on && cr_vol_front && t->cr_mode != CR_CLIMB;
        int hx0 = (int)t->cr_x - 48, hx1 = (int)t->cr_x + 48, hy1 = (int)(CR_FLOOR_Y - t->cr_y) + 8, hy0 = hy1 - 72, n = 0;
        if (hx0 < 0) hx0 = 0;
        if (hx1 > TANK_W - 1) hx1 = TANK_W - 1;
        if (hy0 < 0) hy0 = 0;
        if (hy1 > TANK_H - 1) hy1 = TANK_H - 1;
        if (hold) for (int y = hy0; y <= hy1; y++) for (int x = hx0; x <= hx1; x++) if (keep_at(x, y) && n < DEP_CAP_PX) g_dep_save[n++] = fb[y * TANK_W + x];
        const bool kc = g_keep_clr; if (hold) g_keep_clr = false;   /* (the marks stand: the fish goes back over it) */
        if (hold) draw_crab(&c, t); else DEP_CRAB_F(&c, fb, t);     /* (in its cave: no grass about it) */
        g_keep_clr = kc;
        n = 0;
        if (hold) for (int y = hy0; y <= hy1; y++) for (int x = hx0; x <= hx1; x++) if (keep_at(x, y) && n < DEP_CAP_PX) fb[y * TANK_W + x] = g_dep_save[n++];
    }
    if (tank_snail_upright(t) && snail_lane) DEP_SNAIL_F(&c, fb, t);
    if ((t->sd_unlocks & SD_ITEM_URCHIN) && t->urchin_x >= 0 && urchin_lane) DEP_URCHIN_F(&c, fb, t);
    if (keep_clr || g_fbg_used) { g_keep_clr = false; g_bb_on = false; g_fbg_clr1 = g_fbg_clr2 = NULL; }
    g_cr_clip = 0;                                   /* (the crab is drawn by now - a held sand dollar's top lane is out of its shelter) */
    /* vegetation, FRONT layer: the alternating fronds drawn over the fish,
       so a fish inside a canopy is woven through it (each span applies its
       own vignette and untags itself, so the fish rects below skip it) */
    if (g_veg_mask_cx >= 0) g_veg_arch_pass = 1;     /* (the arch's pixels were drawn before the fish) */
    for (int b = 0; b < tank_veg_beds(t); b++)
        if (bed_z(t, b) == DECOR_Z_MIDDLE) VEG_PASS(b, 1);
    g_veg_arch_pass = 0;                             /* a bed placed IN FRONT last, WHOLE: over the grass, the fish, the critters and every piece it is
                                                        ranked above - the castle's arch included, and its fronds there take no tunnel shade (2026-10-24,
                                                        Strato on the 1.8: the sword plant IN FRONT drawn behind the crab in the castle, and tinted) */
    /* a live piece IN FRONT (its page: dragged, re-ranked) ranked UNDER the sword plant IN FRONT is drawn here, before the plant's
       leaves, so they fall over it as the order says; one ranked above it comes after the leaves (rank 11, below). 0.3.9.4, Strato on
       the square: "behind the pumpkin" picked, the preview still had the plant on top - and the volcano's page the same: a live piece
       had no mask, so the bed's "over" set took it for one under the plant and its body kept off the leaves */
    const bool plant_front = tank_veg_beds(t) > 3 && bed_z(t, 3) == DECOR_Z_FRONT;
#define PC_LIVE_UNDER_PLANT(k) (plant_front && g_pc[k].z == DECOR_Z_FRONT && !g_pc[k].baked && g_pc[k].item != 2 && tank_decor_above(t, 0, g_pc[k].item))
    for (int k = 0; k < g_npc; k++)
        if (PC_LIVE_UNDER_PLANT(k)) { pc_draw(&c, t, k, cached, g_live_rec); if (cached) pc_cover_live(fb, k); }
    { const bool shade = g_veg_arch_shade; g_veg_arch_shade = false;
      for (int b = 0; b < tank_veg_beds(t); b++)
          if (bed_z(t, b) == DECOR_Z_FRONT) {
              g_fbg_on = g_fbg_used;                     /* (SWIM DEPTH: off the fish in front of it) */
              uint32_t *keep_bm = g_bgmask; bool keep_rec = g_bg_rec; int keep_top = g_sg_top;
              if (g_bedm) {                              /* (its leaves recorded: the overlays of the pieces it stands before keep off them) */
                  if (!g_bedm_on) memset(g_bedm, 0, (size_t)TANK_H * DIRTY_WORDS_PER_ROW * 4);
                  g_bgmask = g_bedm; g_bg_rec = true; g_om = NULL; g_bedm_on = true;
                  g_bed_over_k = 0;                      /* the pieces its leaves go over: every one but those IN FRONT ranked above it - a live piece
                                                            (no mask to skip) by its rank like the rest (0.3.9.4) */
                  for (int k = 0; k < g_npc && k < 32; k++)
                      if (!(g_pc[k].z == DECOR_Z_FRONT && b == 3 && tank_decor_above(t, g_pc[k].item, 0))) g_bed_over_k |= 1u << k;
              }
              VEG_PASS(b, -1);
              if (g_bedm) { g_bgmask = keep_bm; g_bg_rec = keep_rec; g_sg_top = keep_top; }
              g_fbg_on = false;
          }
      g_veg_arch_shade = shade; }
#undef VEG_PASS
    PROF_ADD(10, p0);                                /* the front grass */
    /* the pieces IN FRONT that are not in the scene (a placement page is up,
       or no scene cache): live, in order, over everything; then every piece
       IN FRONT's overlay - its crown, the candle - clipped by the pieces
       above it */
    for (int k = 0; k < g_npc; k++)
        if (g_pc[k].z == DECOR_Z_FRONT && !g_pc[k].baked && g_pc[k].item != 2 && !PC_LIVE_UNDER_PLANT(k)) { pc_draw(&c, t, k, cached, g_live_rec); if (cached) pc_cover_live(fb, k); }
#undef PC_LIVE_UNDER_PLANT
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
    const unsigned guard0 = g_guard_who;
    const uint32_t cr_st = (cr_deep_front || (cr_slot && cr_vol_front && t->cr_mode != CR_CLIMB)) ? crab_stack(t) : 0;
    for (int k = 0; k < g_npc; k++) {
        if (g_pc[k].z != DECOR_Z_FRONT) continue;
        g_clip_k = k;
        if (!g_rs_old && (cr_deep_front || (cr_slot && cr_vol_front && t->cr_mode != CR_CLIMB))) {   /* THE CRAB'S STACK: a piece before its shelter paints its overlay
                                                                                                   over the crab, one behind (and the volcano round it) keeps off it */
            g_guard_who = (k < 32 && (cr_st >> k & 1)) ? (guard0 & ~(unsigned)GUARD_CRAB) : (guard0 | (unsigned)GUARD_CRAB);
            g_guard = g_guard_who ? t : NULL;
        }
        if (g_pc[k].item == 3 || g_pc[k].item == 4) PC_CROWN(k);
        else if (g_pc[k].item == 8 && g_pc[k].baked) draw_pumpkin_glow(&c, g_pc[k].cx, anim(t), t->night, false, true);
        else if (g_pc[k].item == 9) PC_VOLCANO(k);
        g_clip_k = -1;
    }
    g_guard = NULL; g_guard_who = 0;
    g_dpc_ok = false;                                /* (the crab's mask is done with: crab_covers maps the painter again) */
    PROF_ADD(11, p0);                                /* the front pieces' overlays (crowns, the candle, the lava) */
    if ((t->sd_unlocks & SD_ITEM_SEAHORSE) && t->sh_x >= 0 && t->sh_front)   /* the seahorse's front lane (its rect is marked above), at its depth against the grass */
        g_dep_over = sh_stack(t), dep_draw(&c, fb, t, 0, dep_seahorse, g_hbb, &g_hbb_ok, t->sh_x, t->sh_y, 40, FISH_DEPTH_FRONT, DEP_LANE_F);   /* (its front lane: before the grass too) */
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
    if (loop_ok && g_sg_on && (!sgv || t->clock - g_sg_clock >= g_sg_period || t->clock < g_sg_clock)) {
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
        if (g_fl_odd) { memset(g_fl_odd, 0, (size_t)TANK_H * DIRTY_WORDS_PER_ROW * 4); g_om = g_fl_odd; }   /* (SWIM DEPTH: its odd fronds, held till the next refresh) */
#define SG_PASS(b, layer) do { g_veg_skip = veg_skip_for(t, (b)); \
        g_pk_shade_all = g_pk_lx >= 0 && ((b) == 3 ? tank_decor_above(t, 0, 8) : pkz == DECOR_Z_BACK); \
        draw_veg(&cg, t, (b), VEG_SEED[(b)], (layer), true); g_pk_shade_all = false; } while (0)
        for (int kb = 0; kb < tank_veg_beds(t); kb++) if (bed_z(t, bed_ord(t, kb)) == DECOR_Z_BACK) SG_PASS(bed_ord(t, kb), -1);
        for (int b = 0; b < tank_veg_beds(t); b++) if (bed_z(t, b) == DECOR_Z_MIDDLE) SG_PASS(b, 0);
#undef SG_PASS
        g_bg_rec = false; g_touched = keep_t; g_om = NULL;
        if (g_sg_top == TANK_H) g_sg_top = TANK_H - 1;
        g_sg_ok = true; g_sg_scene_ep = g_scene_epoch; g_sg_clock = t->clock; g_sg_ep = (g_sg_ep + 1) & 0x7fffffffu;
        if (render_clock_us) { g_sg_us += render_clock_us() - q0; g_sg_n++; }
    }
    if (gl_forced) g_veg_clock_force = -1;
    /* THE GRASS LOOP's bake: a new look (the scene, a bed, a frond's height past the waiting rule: gl_stale_check) marks the loop stale; once the grass
       has held still a few frames, one band or fine moment a frame is baked - the bands first, the next one due
       first: the scene's band copied in, the back grass of that moment painted over it, its pixels recorded (a
       fine moment's then kept as its difference from its band) */
    if (loop_ok && g_gl_on && !g_gl_bypass && !t->night) {   /* (not with a live piece: the loop waits for the page to close) */
        if (!gl_key_now) gl_key_now = gl_stale_check(t);
        const uint32_t hard = gl_key_now, full = g_gl_full_now;
        if (hard == g_gl_hard_last && ++g_gl_still >= GL_SETTLE && gl_geometry(t) && !g_gl_task) gl_bake_frame(t, dim, pkz);   /* (in slices, under a budget; THE PATCH's boxes first; or the platform's task) */
        g_gl_dim_last = dim; g_gl_pkz_last = pkz;
        g_gl_key_last = full; g_gl_hard_last = hard;
        if (g_gl_replan) { g_gl_replan = false; gl_release(); g_gl_nomem = 0; }   /* (the next frame plans the loop afresh) */
    }
    g_veg_seg_hold = NULL;
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
static void draw_battery(ctx_t *c, int X, int Y, int W, int H, int line, float frac, int state, double clock) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    bool pw = BAT_ON_POWER(state);
    int pct = (int)(frac * 100 + 0.5f);                       /* the percent the battery page shows: the pill and the page agree at 20 (0.3.9.3, C1704) */
    uint32_t col = pw ? 0x78d67d : pct < 20 ? 0xf25b65 : pct < 45 ? 0xffbd59 : 0x78d67d;
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
        float ph = (float)fmod(clock, 1.8) / 1.3f;
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
void render_battery(uint16_t *fb, int stride, float frac, int state, double clock) {
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
    *p = FB_SWAP16((uint16_t)((r << 11) | (g << 5) | b)); bb_add(x, x, y); TOUCH_PX(c, x, y);
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
static int  words_w(const char *s, int scale);                                                                            /* (a name: as typed) */
static void draw_words(ctx_t *c, int x, int y, int scale, uint32_t rgb, const char *s);
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
    draw_words(&c, X + 8, Y + 8, 2, 0xffffff, f->name);                   /* name: as typed */
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

    /* divider between the zones */
    for (int x = X + 8; x < X + W - 8; x++) px_blend(&c, x, Y + 142, 0x2a3f45, 200);

    /* who they are: spectrum sliders, revealed by behaviour you have seen
       this fish do (docs/progression.md habits) */
    bool saw_bold = f->ms_bits & MS_FIRST_DART;
    bool saw_social = f->ms_bits & MS_FIRST_FOLLOW;
    bool saw_curious = f->ms_bits & (MS_INSPECTED | MS_FIRST_BUBBLES);
    slider(&c, X + 8, Y + 152, W - 16, f->bold,             0xffffff, &icon_shy,      &icon_bold,    saw_bold);
    slider(&c, X + 8, Y + 178, W - 16, f->sociable,         0x38dcc7, &icon_solo,     &icon_social,  saw_social);
    slider(&c, X + 8, Y + 204, W - 16, f->curiosity / 10.0f, 0x6db9ff, &icon_cautious, &icon_curious, saw_curious);

    /* the way onward (2026-09-16): a MORE button in the pages' dress at the
       foot of the card. The whole card was already the tap that opens the
       milestones page (and from there SETTINGS / UPGRADES), but nothing said
       so - a keeper asked Strato how to get there. The button is the sign;
       the hit box is still the card (touch ports, RENDER_CARD_H). */
    button(&c, X + 8, Y + H - 8 - CARD_MORE_H, W - 16, CARD_MORE_H, 0x1c2f36, 0x9fd8e2, "MORE", 2);
}

/* ---- stats card cache (2026-09-01) ----
 * The card cost ~7 ms of every frame it was up (28k blended backdrop pixels
 * + icons + meters) - fps sagged whenever the keeper inspected a fish. With a
 * cache it is redrawn at most 4x a second over the STATIC water under it
 * (from the scene cache, so the translucent backdrop looks as before; only
 * things swimming behind the card stop showing through at 16%) and copied
 * into the frame otherwise. The selection ring follows the fish per frame. */
static uint16_t *g_card = NULL;
static int g_card_fish = -1; static double g_card_t = -1; static unsigned g_card_epoch;
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
    blit_icon_scaled(c, X + (W - ic->w * 2) / 2, Y + 10, ic, 2, true);
    draw_text(c, X + (W - text_w("SNAIL", 3)) / 2, Y + 82, 3, 0xffffff, "SNAIL");
    const char *cap = "ALGAE GRAZED SO FAR";
    draw_text(c, X + (W - text_w(cap, 2)) / 2, Y + 112, 2, 0x9fd8e2, cap);
    char n[24];
    if (t->snail_grazed <= 0) snprintf(n, sizeof n, "NOTHING YET");
    else snprintf(n, sizeof n, lang_tr(t->snail_grazed == 1 ? "%d SPOT" : "%d SPOTS"), (int)t->snail_grazed);   /* format translated, one per plural (lang.h) */
    draw_text(c, X + (W - text_w(n, 3)) / 2, Y + 134, 3, 0xffffff, n);
}

/* the urchin's card (2026-10-02), the snail's: a ring on it, the urchin
   at 3x with its spines waving, SEA URCHIN, the grass it has eaten so far
   (the trim's centimeters) and what it is up to - in words, never colour. */
/* the crab's card (0.3.9; Strato 10-23: "we need to add a modal for when the user taps it"), the urchin's way:
   a ring on it, a centred card - the crab facing the glass at 2x (its own grid: the tank's may be a profile),
   STRAWBERRY CRAB, the pellets it has found, what it is doing */
static void crab_collect_box(int *x, int *y, int *w, int *h) {   /* the crab card's COLLECT, where the status line stands */
    *w = 156; *h = MSP_CLOSE_H; *x = (TANK_W - *w) / 2; *y = (TANK_H - CRAB_CARD_H) / 2 + CRAB_CARD_H - *h - 12;
}
bool render_crab_collect_hit(const tank_t *t, float x, float y) {
    if (t->cr_find <= 0) return false;
    int bx, by, bw, bh; crab_collect_box(&bx, &by, &bw, &bh);
    return x >= bx - 8 && x < bx + bw + 8 && y >= by - 8 && y < by + bh + 8;
}
static void crab_card_draw(ctx_t *c, const tank_t *t) {
    ring(c, t->cr_x, CR_FLOOR_Y - t->cr_y - 10, 24, 0x9fd8e2);
    const int W = CRAB_CARD_W, H = CRAB_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    src_t bg = src_color(0x04141a, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 235);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    static uint32_t s_key = 0;
    cr_pose_t p; memset(&p, 0, sizeof p); p.salt = 7 + t->cr_variant;   /* the portrait: its roost, breathing; open-clawed while poked */
    cr_pose_roost(&p, 0, 0, t->cr_poke > 0, fast_sin(anim(t) * 0.9f + 4 * TAU) > 0.35f);
    const uint32_t key = cr_hash(&p);
    if (key != s_key) { cr_build(ds()->cr_card_tones, ds()->cr_card_parts, &p); s_key = key; }
    const int S = 2, ox = X + (W - CR_GW * S) / 2, oy = Y + 10;
    for (int gy = 0; gy < CR_GH; gy++)
        for (int gx = 0; gx < CR_GW; gx++) {
            int tone = ds()->cr_card_tones[gy * CR_GW + gx];
            if (tone == CRT_NONE) continue;
            src_t sc = src_color(CR_RGB[tone], 1.0f);
            for (int yy = 0; yy < S; yy++) span(c, ox + gx * S, ox + gx * S + S - 1, oy + gy * S + yy, &sc, 255);
        }
    draw_text(c, X + (W - text_w("STRAWBERRY CRAB", 3)) / 2, Y + 70, 3, 0xffffff, "STRAWBERRY CRAB");
    {   /* two counts side by side: the pellets it found, the sand dollars it dug up (0.3.9) - what they paid, not how many finds (0.3.9.1) */
        const int L = X + W / 4, R = X + 3 * W / 4;
        char n[16];
        draw_text(c, L - text_w("PELLETS", 2) / 2, Y + 104, 2, 0x9fd8e2, "PELLETS");
        if (t->cr_eaten == 0) snprintf(n, sizeof n, "NONE YET"); else snprintf(n, sizeof n, "%u", (unsigned)t->cr_eaten);
        draw_text(c, L - text_w(n, 3) / 2, Y + 126, 3, 0xffffff, n);
        draw_text(c, R - text_w("SAND DOLLARS", 2) / 2, Y + 104, 2, 0x9fd8e2, "SAND DOLLARS");
        if (t->cr_found == 0) snprintf(n, sizeof n, "NONE YET"); else snprintf(n, sizeof n, "%u", SD_CRAB_FINDS_VALUE(t->cr_found));
        draw_text(c, R - text_w(n, 3) / 2, Y + 126, 3, 0xffffff, n);
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
    if (t->cr_find > 0) {                         /* COLLECT (0.3.9.4, Strato: a keeper who came by the arrows takes the find from here) */
        int bx, by, bw, bh; crab_collect_box(&bx, &by, &bw, &bh);
        button(c, bx, by, bw, bh, 0x9fd8e2, 0x9fd8e2, "", 2);   /* (MSP_TEAL, lit; MSP_INK lettering - defined further down) */
        const int tw = text_w("COLLECT", 2), lx = bx + (bw - 16 - 8 - tw) / 2;
        blit_icon(c, lx, by + (bh - 16) / 2, &icon_shop_sand_dollar_16, 255);
        draw_text(c, lx + 16 + 8, by + (bh - 14) / 2, 2, 0x031015, "COLLECT");
    } else draw_text(c, X + (W - text_w(now, 2)) / 2, Y + 162, 2, 0x9fd8e2, now);
}
static void urchin_card_draw(ctx_t *c, const tank_t *t) {
    ring(c, t->urchin_x, URCHIN_FLOOR_Y + t->urchin_dy - 3, 22, 0x9fd8e2);
    const int W = URCHIN_CARD_W, H = URCHIN_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    src_t bg = src_color(0x04141a, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 235);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    ur_pose_t p; urchin_pose(&p, anim(t), tank_urchin_chewing(t));
    const int S = 3, ox = X + W / 2, oy = Y + 10 + 16 * S;               /* the dome's centre at 3x */
    for (int ly = -16; ly <= (int)UR_BASE; ly++)
        for (int lx = -18; lx <= 18; lx++) {
            int tone = urchin_tone((float)lx + 0.5f, (float)ly + 0.5f, &p);
            if (tone == UR_NONE) continue;
            src_t sc = src_color(URCHIN_RGB[tone], 1.0f);
            for (int yy = 0; yy < S; yy++) span(c, ox + lx * S, ox + lx * S + S - 1, oy + ly * S + yy, &sc, 255);
        }
    draw_text(c, X + (W - text_w("SEA URCHIN", 3)) / 2, Y + 90, 3, 0xffffff, "SEA URCHIN");
    const char *cap = "GRASS GRAZED SO FAR";
    draw_text(c, X + (W - text_w(cap, 2)) / 2, Y + 118, 2, 0x9fd8e2, cap);
    char n[24]; int cm = (int)(t->urchin_grazed_px / PX_PER_CM);
    /* CM under a meter, then M with one decimal, KM past a kilometer (0.3.9,
       Strato: "1.4K CM is odd"); the longest line, "999.9 M", fits the card */
    if (cm < 1) snprintf(n, sizeof n, "NOTHING YET");
    else if (cm < 100) snprintf(n, sizeof n, "%d CM", cm);
    else if (cm < 100000) snprintf(n, sizeof n, lang_tr("%d.%d M"), cm / 100, cm % 100 / 10);
    else snprintf(n, sizeof n, lang_tr("%d.%d KM"), cm / 100000, cm % 100000 / 10000);
    draw_text(c, X + (W - text_w(n, 3)) / 2, Y + 140, 3, 0xffffff, n);
    const char *now = tank_urchin_chewing(t) ? "CHEWING"
                    : t->urchin_frond >= 0 ? "OFF TO THE TALL GRASS"
                    : "RESTING";
    draw_text(c, X + (W - text_w(now, 2)) / 2, Y + 170, 2, 0x9fd8e2, now);
}

/* a visitor's card (2026-10-05, Strato: "it should be a bit different since
   it's a visiting fish, it should maybe look a little more like a passport"):
   the Pocket Tank Passport - a cover band, its photo, its fields, this tank's
   stamp, the machine line at the foot. Its home is named by its badge's SHAPE
   in words as well as drawn (never colour alone). */
static void rect_fill(ctx_t *c, int x, int y, int w, int h, uint32_t rgb);   /* below */
static const char *const BADGE_WORDS[LINK_BADGES] = { "CIRCLE", "TRIANGLE", "SQUARE", "DIAMOND" };
/* HOME on a passport (2026-11-01, Strato): its HOME WATERS (passport_home_waters, from the tank's ID - the badge's shape is only
   who is next door now), or a renamed tank's own name with its waters small in the label - two SAM'S REEFs still differ */
static void home_fields(uint32_t home, const char *name, const char *lbl0, char *lbl, size_t ln, char *val, size_t vn) {
    char w[PASSPORT_WATERS_MAX]; passport_home_waters(home, w, sizeof w);
    if (!home) snprintf(w, sizeof w, "%s", lang_tr("FAR WATERS"));   /* (the one word among the names: translated) */
    if (name && name[0]) { snprintf(lbl, ln, "%s - %s", lang_tr(lbl0), w); snprintf(val, vn, "%s", name); }   /* (lbl0 translated, */
    else { snprintf(lbl, ln, "%s", lang_tr(lbl0)); snprintf(val, vn, "%s", w); }                              /*  names as built) */
}
static void passport_card_draw(ctx_t *c, uint16_t *fb, int stride, const tank_t *t, int k) {
    const fish_t *f = &t->fish[N_FISH_MAX + k];
    const uint32_t GOLD = 0xe8c872, INK = 0x13324f, LBL = 0x7fa3b5;
    ring(c, f->x, f->y, 17 * f->size, GOLD);
    const int W = PASSPORT_CARD_W, H = PASSPORT_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    src_t bg = src_color(0x0e2233, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 240);
    for (int y = Y + 34; y < Y + H - 30; y += 7)              /* the page's fine wavy lines */
        for (int x = X + 3; x < X + W - 3; x++) px_blend(c, x, y + (int)(2.0f * sinf(x * 0.11f + y * 0.37f)), 0x2b4a63, 80);
    src_t band = src_color(INK, 1.0f);
    for (int y = Y; y < Y + 26; y++) span(c, X, X + W - 1, y, &band, 255);
    const char *title = "POCKET TANK PASSPORT";
    draw_text(c, X + (W - text_w(title, 2)) / 2, Y + 7, 2, GOLD, title);
    rect_edge(c, X, Y, W, H, GOLD); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, INK);
    /* the photo */
    const int PX = X + 12, PY = Y + 36, PW = 100, PH = 84;
    rect_fill(c, PX, PY, PW, PH, 0x0a1824); rect_edge(c, PX, PY, PW, PH, 0x9fd8e2);
    render_fish_portrait(fb, stride, PX + PW / 2.0f - PAGE_X, PY + PH / 2.0f - PAGE_Y, 1.3f, f, PW - 20, t->clock);   /* (page coordinates) */
    /* the fields */
    const int FX = PX + PW + 12;
    int vb = t->vis_badge[k];
    char home[24], hlbl[40], form[24], here[24];
    home_fields(t->vis_home[k], t->vis_home_name[k], "HOME", hlbl, sizeof hlbl, home, sizeof home);
    snprintf(form, sizeof form, "%s %s", lang_tr(SHAPE_NAMES[f->shape % SHAPE_N]), lang_tr(FINS_NAMES[f->fins % FINS_N]));
    float s = (float)(t->clock - t->vis_since[k]);
    if (f->homeward) snprintf(here, sizeof here, "%s", lang_tr("HEADING HOME"));
    else if (s < 60) snprintf(here, sizeof here, "%s", lang_tr("JUST ARRIVED"));
    else snprintf(here, sizeof here, lang_tr("HERE %d MIN"), (int)(s / 60));
    static const char *const STAGE_W[4] = { "FRY", "JUVENILE", "ADULT", "ELDER" };
    struct { const char *lbl, *val; } FL[4] = { { lang_tr("NAME"), f->name }, { hlbl, home }, { lang_tr("FORM"), form }, { lang_tr("STAGE"), lang_tr(STAGE_W[f->stage & 3]) } };   /* (translated here, drawn as built) */
    for (int i = 0; i < 4; i++) {
        int fy = Y + 33 + i * 26;
        draw_words(c, FX, fy, 1, LBL, FL[i].lbl);
        int vx = FX;
        if (i == 1 && vb >= 0) { link_badge_draw(c, FX + 6, fy + 17, 6, vb, 255); vx += 16; }
        draw_words(c, vx, fy + 10, 2, 0xffffff, FL[i].val);   /* a name, waters, the form: as built */
    }
    draw_text(c, PX, PY + PH + 12, 1, LBL, "VISITING");   /* (a gap under STAGE's value: HERE NN MIN reaches under it) */
    draw_words(c, PX, PY + PH + 21, 2, 0xffffff, here);
    /* time together (2026-10-06): CLOSE TO <one of ours> and five pips that
       fill - filled vs hollow, never colour - no minutes (the goal is random) */
    { uint32_t mate = 0; float b = progression_bond(t, f->uid, &mate);
      const fish_t *m = NULL; for (int i = 0; i < t->n_fish && !m; i++) if (mate && t->fish[i].uid == mate) m = &t->fish[i];
      if (m && b > 0.02f) {
          char cl[32]; snprintf(cl, sizeof cl, lang_tr("INTERESTED IN %s"), m->name);   /* (Strato 11-01: was CLOSE TO) */
          int cy = PY + PH + 39;
          draw_words(c, PX, cy, 1, LBL, cl);                 /* (a name in it) */
          int lit = (int)(b * 5 + 0.0001f);
          static const char *const HEART[10] = { ".XXX...XXX.", "XXXXX.XXXXX", "XXXXXXXXXXX", "XXXXXXXXXXX", "XXXXXXXXXXX",
                                                 ".XXXXXXXXX.", "..XXXXXXX..", "...XXXXX...", "....XXX....", ".....X....." };
          for (int i = 0; i < 5; i++) {                 /* five hearts (Strato 11-01): filled, or hollow - the shape, never the color */
              int bx = PX + i * 15, by = cy + 10;
              for (int yy = 0; yy < 10; yy++)
                  for (int xx = 0; xx < 11; xx++) {
                      if (HEART[yy][xx] != 'X') continue;
                      bool edge = xx == 0 || xx == 10 || yy == 0 || yy == 9 || HEART[yy][xx - 1] != 'X' || HEART[yy][xx + 1] != 'X'
                                  || HEART[yy - 1][xx] != 'X' || HEART[yy + 1][xx] != 'X';
                      if (i < lit) px_blend(c, bx + xx, by + yy, 0xffffff, 255);
                      else if (edge) px_blend(c, bx + xx, by + yy, 0x9fd8e2, 255);
                  }
          }
      } }
    /* this tank's stamp: two rings, our badge, VISITED */
    if (t->link_badge >= 0) {
        const float sx = X + W - 52, sy = Y + 142;
        for (int a = 0; a < 360; a += 2) {
            float r = a * 0.0174533f;
            px_blend(c, (int)(sx + cosf(r) * 30), (int)(sy + sinf(r) * 30), GOLD, 170);
            px_blend(c, (int)(sx + cosf(r) * 27), (int)(sy + sinf(r) * 27), GOLD, 120);
        }
        link_badge_draw(c, (int)sx, (int)sy - 5, 9, t->link_badge, 200);
        draw_text(c, (int)sx - text_w("VISITED", 1) / 2, (int)sy + 9, 1, GOLD, "VISITED");
    }
    /* the machine line */
    char m1[64], m2[64], nm[FISH_NAME_MAX + 1];
    int i = 0; for (; f->name[i] && i < FISH_NAME_MAX; i++) nm[i] = f->name[i]; nm[i] = 0;
    int cols = (W - 20) / 6; if (cols > 60) cols = 60;
    snprintf(m1, sizeof m1, "P<FISH<<%s", nm);
    snprintf(m2, sizeof m2, "%08X<<%08X<<%d", (unsigned)f->uid, (unsigned)t->vis_home[k], f->stage & 3);
    for (int j = (int)strlen(m1); j < cols; j++) m1[j] = '<';
    for (int j = (int)strlen(m2); j < cols; j++) m2[j] = '<';
    m1[cols] = m2[cols] = 0;
    draw_words(c, X + 10, Y + 186, 1, 0xc8d6de, m1);   /* (a name in it: as built) */
    draw_words(c, X + 10, Y + 197, 1, 0xc8d6de, m2);
    /* collected: our COLLECTED stamp beside the VISITED one (Strato: keepers
       tell at a glance), and the buttons - DEPORT | COLLECT, the second dim
       once it is in the collection */
    bool have = collection_find(f->uid) >= 0, full = !have && collection_full();   /* full: every page (0.4 pages) - FULL, dim */
    if (have) {
        const char *st = "COLLECTED";
        int sw = text_w(st, 2) + 12, sx0 = FX + 10, sy0 = Y + 152;   /* (clear of HERE N MIN and of the VISITED stamp) */
        rect_edge(c, sx0, sy0, sw, 24, GOLD); rect_edge(c, sx0 + 2, sy0 + 2, sw - 4, 20, GOLD);
        draw_text(c, sx0 + 6, sy0 + 5, 2, GOLD, st);
    }
    const int BY = Y + PASSPORT_BTN_Y, BW = PASSPORT_BTN_W, gap = W - 2 * BW - 24;
    button(c, X + 12, BY, BW, PASSPORT_BTN_H, 0x1c2f36, 0x9fd8e2, "DEPORT", 2);
    if (have || full) {
        const char *w = have ? "COLLECTED" : "FULL";
        rect_edge(c, X + 12 + BW + gap, BY, BW, PASSPORT_BTN_H, 0x3f6a72);
        draw_text(c, X + 12 + BW + gap + (BW - text_w(w, 2)) / 2, BY + 8, 2, 0x3f6a72, w);
    } else button(c, X + 12 + BW + gap, BY, BW, PASSPORT_BTN_H, 0x1c2f36, GOLD, "COLLECT", 2);
}
/* ---- THE CARDS BAKED (0.4, 11-02; Strato's rule: always look for frame-rate wins, never drop frames) ----
 * The GIVE modal and the gift card offered to us were drawn whole over the live tank every frame (+11-12 ms of render
 * on the 1.8 and the square: 24.9 -> 21.2 fps, 20.3 -> 16.7 with the pick). Both are opaque, so each is painted into
 * its own buffer (PSRAM, main.c) only when what it shows changes - its key: the step, the fish, the tanks, the rows
 * open, the words - and copied into the frame otherwise, as the stats card's cache is. Only what moves is drawn live
 * over the copy: the fish's portrait and the time bar's fill (neither overlaps anything else on the card, so the frame
 * is the card drawn whole, pixel for pixel - selftest-doors 7k checks). No buffer (a board short of RAM): drawn whole. */
typedef struct { uint16_t *buf; uint32_t key; int h; bool ok; } bake_t;
static bake_t g_bake_gm, g_bake_gift;
static uint16_t *g_bake_bufs[2];
void render_set_modal_cache(uint16_t *give, uint16_t *gift) {
    g_bake_bufs[0] = give; g_bake_bufs[1] = gift;
    g_bake_gm.buf = give; g_bake_gift.buf = gift; g_bake_gm.ok = g_bake_gift.ok = false;
}
void render_modal_cache_on(bool on) {   /* (the director's `link bake on|off`: the same build measured both ways) */
    g_bake_gm.buf = on ? g_bake_bufs[0] : NULL; g_bake_gift.buf = on ? g_bake_bufs[1] : NULL; g_bake_gm.ok = g_bake_gift.ok = false;
}
static uint32_t bake_hash(uint32_t h, uint32_t v) { for (int b = 0; b < 4; b++) { h ^= (v >> (b * 8)) & 255; h *= 16777619u; } return h; }
static uint32_t bake_hash_s(uint32_t h, const char *s) { while (s && *s) h = bake_hash(h, (uint8_t)*s++); return bake_hash(h, 0); }
/* the bake's ctx (the card's own frame coordinates), nothing marked while it is painted (the copy marks the frame) */
static bool bake_stale(bake_t *b, uint32_t key, int h) { return !b->ok || b->key != key || b->h != h; }
static uint32_t *bake_begin(bake_t *b, ctx_t *bc, int X, int Y, int W, int H, bool *hold) {
    uint32_t *tm = g_touched; g_touched = NULL; *hold = g_dirty_hold; g_dirty_hold = true;
    ctx_t c = { b->buf, W, 1.0f, X, Y, W, H }; *bc = c;
    return tm;
}
static void bake_end(bake_t *b, uint32_t key, int h, uint32_t *tm, bool hold) { g_touched = tm; g_dirty_hold = hold; b->key = key; b->h = h; b->ok = true; }
static void bake_blit(const bake_t *b, int X, int Y, int W, int H, uint16_t *fb, int stride) {
    for (int y = 0; y < H; y++) {
        int fy = Y + y;
        if ((unsigned)fy >= (unsigned)TANK_H) continue;
        memcpy(fb + fy * stride + X, b->buf + y * W, (size_t)W * 2);
        touch_span_f(X, X + W - 1, fy);
    }
}
/* a gift offered to us: its card (render.h) - baked (THE CARDS BAKED, above), its portrait and the time bar's fill live */
static void gift_paint(ctx_t *c, const tank_t *t) {
    const gift_book_t *g = gift_of(t);
    const passport_t *pp = &g->ask.pp;
    fish_t f; memset(&f, 0, sizeof f); passport_to_fish(pp, &f);
    const uint32_t GOLD = 0xe8c872, INK = 0x13324f, LBL = 0x7fa3b5;   /* (GIVE_GOLD, the passport's gold) */
    const int W = GIFT_CARD_W, H = GIFT_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    rect_fill(c, X, Y, W, H, 0x0e2233);
    rect_fill(c, X, Y, W, 26, INK);
    const char *title = "A GIFT FOR YOUR TANK";
    draw_text(c, X + (W - text_w(title, 2)) / 2, Y + 7, 2, GOLD, title);
    rect_edge(c, X, Y, W, H, GOLD); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, INK);
    const int PX = X + 12, PY = Y + 36, PW = 100, PH = 84;
    rect_fill(c, PX, PY, PW, PH, 0x0a1824); rect_edge(c, PX, PY, PW, PH, 0x9fd8e2);
    const int FX = PX + PW + 12;
    char from[24], flbl[40], form[24];
    int b = g->ask.badge;
    home_fields(pp->home_tank, pp->home_name, "FROM", flbl, sizeof flbl, from, sizeof from);
    snprintf(form, sizeof form, "%s %s", lang_tr(SHAPE_NAMES[f.shape % SHAPE_N]), lang_tr(FINS_NAMES[f.fins % FINS_N]));
    static const char *const STAGE_W[4] = { "FRY", "JUVENILE", "ADULT", "ELDER" };
    struct { const char *lbl, *val; } FL[4] = { { lang_tr("NAME"), f.name }, { flbl, from }, { lang_tr("FORM"), form }, { lang_tr("STAGE"), lang_tr(STAGE_W[pp->stage & 3]) } };
    for (int i = 0; i < 4; i++) {
        int fy = Y + 33 + i * 26;
        draw_words(c, FX, fy, 1, LBL, FL[i].lbl);
        int vx = FX;
        if (i == 1 && b >= 0 && b < LINK_BADGES) { link_badge_draw(c, FX + 6, fy + 17, 6, b, 255); vx += 16; }   /* the shape AND the word */
        draw_words(c, vx, fy + 10, 2, 0xffffff, FL[i].val);
    }
    const char *l1 = "IT WOULD LIVE HERE", *l2 = "FOR GOOD";
    draw_text(c, X + (W - text_w(l1, 2)) / 2, Y + 140, 2, 0xffffff, l1);
    draw_text(c, X + (W - text_w(l2, 2)) / 2, Y + 160, 2, 0xffffff, l2);
    rect_edge(c, X + 24, Y + 186, W - 48, 6, 0x3f6a72);              /* the time left to answer (its fill: live) */
    const int BY = Y + GIFT_BTN_Y, gap = W - 2 * GIFT_BTN_W - 24;
    button(c, X + 12, BY, GIFT_BTN_W, GIFT_BTN_H, 0x1c2f36, GOLD, "ACCEPT", 2);
    button(c, X + 12 + GIFT_BTN_W + gap, BY, GIFT_BTN_W, GIFT_BTN_H, 0x1c2f36, 0x9fd8e2, "NO THANKS", 2);
}
/* where it may come up (the 11-02 sweep: it hid under every page - no card, no sound, NO ANSWER CAME after 60 s): OVER any
   page - the keeper page, settings, the shop, VISITORS, MY TANKS, the battery page, the LINK page - and it waits under the
   tank's own setup (naming, placement), the reset prompt and ADVANCED (main: render_gift_allow). The keeper page draws it as
   part of itself (its kept frames stay kept); its hold counts from the first frame it is drawn, and that frame brings its cue */
static unsigned g_rframe;                            /* (THE PAGE KEPT's frame count, below) */
static bool g_gift_ok = true, g_gift_cue;
static unsigned g_gift_frame;
/* BACK FROM UNDER A PAGE (0.4.1 audit round 2, R2T-2): the card waited under the birth flow (or the reset prompt, ADVANCED) with its
   2 s hold counted from when it was first seen - a doubled DONE declined the gift. When it is allowed again it gets a fresh hold,
   as the GIVE modal does (gm_draw): the first look at it after (the port's poll, or the draw) stamps it */
static bool g_gift_back;
static double g_gift_back_clock = -1e9;   /* when it came back from under a page (a tap closed that page: its double's window, W) */
void render_gift_allow(bool ok) { if (ok && !g_gift_ok) g_gift_back = true; g_gift_ok = ok; }
static void gift_fresh(const tank_t *t, gift_book_t *g) {
    if (!g_gift_back || !g_gift_ok) return;
    g_gift_back = false;
    if (g && g->ask.on && g->ask.shown) { g->ask.seen = t->clock; g_gift_back_clock = t->clock; }
}
bool render_gift_back_window(const tank_t *t, double press) { (void)t; return press >= g_gift_back_clock && press - g_gift_back_clock < TAP_W_S; }
bool render_gift_up(const tank_t *t) { gift_book_t *g = gift_of(t); gift_fresh(t, g); return g && g->ask.on && g_gift_ok; }
bool render_card_over_tank(const tank_t *t) { return render_give_modal_step(t) != RENDER_GM_OFF || render_gift_up(t); }
int  render_gift_take_cue(void) { if (!g_gift_cue) return -1; g_gift_cue = false; return SND_ARRIVAL; }
void render_gift(const tank_t *t, uint16_t *fb, int stride) {
    gift_book_t *g = gift_of(t);
    if (!g || !g->ask.on || !g_gift_ok) return;
    gift_fresh(t, g);
    if (!g_ms_in && g_rframe && g_gift_frame == g_rframe) return;   /* (the keeper page drew it this frame) */
    g_gift_frame = g_rframe;
    if (!g->ask.shown) { g->ask.shown = true; g->ask.seen = t->clock; g_gift_cue = true; }   /* first seen: its hold and its cue from now */
    ctx_t cc = ctx_full(fb, stride, 1.0f); ctx_t *c = &cc;
    const passport_t *pp = &g->ask.pp;
    const int W = GIFT_CARD_W, H = GIFT_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    if (g_bake_gift.buf) {
        uint32_t key = bake_hash(bake_hash(bake_hash(2166136261u, g->ask.uid), g->ask.nonce), (uint32_t)(g->ask.badge + 1));
        key = bake_hash(key, lang_now);
        key = bake_hash_s(bake_hash(key, pp->home_tank), pp->home_name); key = bake_hash_s(key, pp->name);
        key = bake_hash(key, (uint32_t)pp->stage); key = bake_hash(key, (uint32_t)pp->shape | (uint32_t)pp->fins << 8);
        if (bake_stale(&g_bake_gift, key, H)) {
            ctx_t bc; bool hold; uint32_t *tm = bake_begin(&g_bake_gift, &bc, X, Y, W, H, &hold);
            gift_paint(&bc, t);
            bake_end(&g_bake_gift, key, H, tm, hold);
        }
        bake_blit(&g_bake_gift, X, Y, W, H, fb, stride);
    } else gift_paint(c, t);
    fish_t f; memset(&f, 0, sizeof f); passport_to_fish(pp, &f);      /* live: the fish in its box, the bar running down */
    const int PX = X + 12, PY = Y + 36, PW = 100, PH = 84;
    render_fish_portrait(fb, stride, PX + PW / 2.0f - PAGE_X, PY + PH / 2.0f - PAGE_Y, 1.3f, &f, PW - 20, t->clock);   /* (page coordinates) */
    float left = 1.0f - (float)(t->clock - g->ask.since) / (LINK_GIVE_ASK_MS / 1000.0f);   /* the time left to answer */
    left = left < 0 ? 0 : left > 1 ? 1 : left;
    if (left > 0) rect_fill(c, X + 25, Y + 187, (int)((W - 50) * left), 4, 0x9fd8e2);
}
int render_gift_hit(float x, float y) {
    const int W = GIFT_CARD_W, H = GIFT_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    const int BY = Y + GIFT_BTN_Y, gap = W - 2 * GIFT_BTN_W - 24;
    if (y >= BY - 10 && y < BY + GIFT_BTN_H + 14 && x >= X && x < X + W) return x < X + 12 + GIFT_BTN_W + gap / 2 ? GIFT_HIT_ACCEPT : GIFT_HIT_NO;
    return x >= X && x < X + W && y >= Y && y < Y + H ? GIFT_HIT_CARD : GIFT_HIT_NONE;
}
int render_passport_hit(float x, float y) {
    const int W = PASSPORT_CARD_W, H = PASSPORT_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    const int BY = Y + PASSPORT_BTN_Y, BW = PASSPORT_BTN_W, gap = W - 2 * BW - 24;
    if (y >= BY - 8 && y < BY + PASSPORT_BTN_H + 10) {
        if (x >= X + 4 && x < X + 12 + BW + gap / 2) return PASSPORT_HIT_DEPORT;
        if (x >= X + 12 + BW + gap / 2 && x < X + W - 4) return PASSPORT_HIT_COLLECT;
    }
    return x >= X && x < X + W && y >= Y && y < Y + H ? PASSPORT_HIT_CARD : PASSPORT_HIT_NONE;
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
    const char *const *fr = fmod(t->clock * 2.5, 1.0) < 0.5 ? SHRIMP_IDLE_A : SHRIMP_IDLE_B;
    const int S = 4, sx = X + (W - 18 * S) / 2, sy = Y + 8;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 18; x++) {
            const char *k = fr[y][x] == '.' ? NULL : strchr(SHRIMP_KEYS, fr[y][x]);
            if (!k) continue;
            int ti = (int)(k - SHRIMP_KEYS);
            for (int yy = 0; yy < S; yy++) span(c, sx + x * S, sx + x * S + S - 1, sy + y * S + yy, &pal[ti], SHRIMP_ALPHA[ti]);
        }
    char line[32];
    snprintf(line, sizeof line, lang_tr("%d SHRIMP"), n);
    draw_text(c, X + (W - text_w(line, 3)) / 2, Y + 46, 3, 0xffffff, line);
    snprintf(line, sizeof line, lang_tr(t->shrimp_eaten == 1 ? "%d PELLET EATEN" : "%d PELLETS EATEN"), (int)t->shrimp_eaten);
    draw_text(c, X + (W - text_w(line, 2)) / 2, Y + 76, 2, 0x9fd8e2, line);
    const char *cap = "NEXT SHRIMP";
    draw_text(c, X + (W - text_w(cap, 2)) / 2, Y + 104, 2, 0x9fd8e2, cap);
    bool full = n >= SHRIMP_MAX;
    int have = full ? SHRIMP_PER_JOIN : t->shrimp_food;
    const int pitch = 22, px0 = X + (W - (SHRIMP_PER_JOIN - 1) * pitch) / 2, py = Y + 132;
    for (int i = 0; i < SHRIMP_PER_JOIN; i++) {
        float x = px0 + i * pitch;
        if (i < have) {
            fill_ellipse(c, x, py, 6.5f, 6.5f, 0xffbd59, 255);
            fill_ellipse(c, x - 2, py - 2, 2.2f, 2.2f, 0xffe9bd, 255);
        } else ring(c, x, py, 6, 0x5a6a6e);
    }
    if (full) snprintf(line, sizeof line, "THE SCHOOL IS FULL");
    else if (tank_shrimp_refusing(t)) snprintf(line, sizeof line, "TOO MUCH ALGAE TO EAT");
    else if (t->shrimp_food >= SHRIMP_PER_JOIN && t->shrimp_cool > 0) {
        int m = (int)ceilf(t->shrimp_cool / 60.0f);
        snprintf(line, sizeof line, lang_tr("ARRIVES IN %d MIN"), m < 1 ? 1 : m);
    } else {
        int more = SHRIMP_PER_JOIN - t->shrimp_food;
        snprintf(line, sizeof line, lang_tr(more == 1 ? "%d MORE PELLET" : "%d MORE PELLETS"), more);
    }
    draw_text(c, X + (W - text_w(line, 2)) / 2, Y + 158, 2, 0xffffff, line);
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
    sh_sprite(sc, sc->sh_spr_card, anim(t), SH_FREE, 0, 11.0f, 1.5f, tank_phase(t->clock, 9.0), 0);
    const int S = 2, ox = X + 14, oy = Y + (H - SH_H * S) / 2;
    for (int ly = 0; ly < SH_H; ly++)
        for (int lx = 0; lx < SH_W; lx++) {
            uint8_t b = sc->sh_spr_card[ly * SH_W + lx];
            if (!(b & SHB_ON)) continue;
            src_t s = src_color(SH_RGB[b & 15], 1.0f);
            for (int yy = 0; yy < S; yy++) span(c, ox + lx * S, ox + lx * S + S - 1, oy + ly * S + yy, &s, SH_ALPHA[b & 15]);
        }
    const int TX = X + 14 + SH_W * S + 12;   /* "+N.N TRUST PASSED ON" is 21 chars at scale 2: 252 px, inside the card from here */
    draw_text(c, TX, Y + 18, 3, 0xffffff, "SEAHORSE");
    char line[32];
    float days = tank_seahorse_days(t);
    if (days < 1) snprintf(line, sizeof line, "WITH YOU SINCE TODAY");
    else snprintf(line, sizeof line, lang_tr((int)days == 1 ? "%d DAY WITH YOU" : "%d DAYS WITH YOU"), (int)days);
    draw_text(c, TX, Y + 52, 2, 0x9fd8e2, line);
    snprintf(line, sizeof line, lang_tr(t->sh_hitches == 1 ? "%d HITCH" : "%d HITCHES"), (int)t->sh_hitches);
    draw_text(c, TX, Y + 76, 2, 0xffffff, line);
    snprintf(line, sizeof line, lang_tr("+%d.%d TRUST PASSED ON"), (int)t->sh_trust_passed, (int)(t->sh_trust_passed * 10) % 10);
    draw_text(c, TX, Y + 100, 2, 0xffffff, line);
    snprintf(line, sizeof line, lang_tr(t->sh_eaten == 1 ? "%d PELLET SNAPPED" : "%d PELLETS SNAPPED"), (int)t->sh_eaten);
    draw_text(c, TX, Y + 124, 2, 0xffffff, line);
    char now[32];
    if (t->sh_hitched) snprintf(now, sizeof now, "ON YOUR FINGER");
    else if (t->sh_mode == SH_HITCH) snprintf(now, sizeof now, "SWIMMING TO YOU");
    else if (t->night && t->sh_mode == SH_PERCH) snprintf(now, sizeof now, "ASLEEP");   /* only once it holds on (0.3.7: it said ASLEEP on the finger) */
    else if (t->sh_mode == SH_PERCH && t->sh_akind == SH_ANCHOR_SAND) snprintf(now, sizeof now, "RESTING ON THE SAND");
    else if (t->sh_mode == SH_PERCH) snprintf(now, sizeof now, "HOLDING %s", tank_seahorse_holding(t));
    else snprintf(now, sizeof now, "ADRIFT");
    draw_text(c, TX, Y + 156, 2, 0x9fd8e2, now);
}

/* ---- the critter cards' arrows (0.3.9.3) ---- */
static void shade_box(ctx_t *c, int x, int y, int w, int h);           /* below */
static void rect_fill(ctx_t *c, int x, int y, int w, int h, uint32_t rgb);
static const int CRITTER_ORDER[] = { RENDER_CARD_SNAIL, RENDER_CARD_SHRIMP, RENDER_CARD_URCHIN, RENDER_CARD_SEAHORSE, RENDER_CARD_CRAB };
#define N_CRITTER_CARDS ((int)(sizeof CRITTER_ORDER / sizeof CRITTER_ORDER[0]))
bool render_critter_here(const tank_t *t, int card) {
    switch (card) {
    case RENDER_CARD_SNAIL:    return (t->sd_unlocks & SD_ITEM_SNAIL) && t->snail_x >= 0;
    case RENDER_CARD_SHRIMP:   return (t->sd_unlocks & SD_ITEM_SHRIMP) && t->shrimp_n > 0;
    case RENDER_CARD_URCHIN:   return (t->sd_unlocks & SD_ITEM_URCHIN) && t->urchin_x >= 0;
    case RENDER_CARD_SEAHORSE: return (t->sd_unlocks & SD_ITEM_SEAHORSE) && t->sh_x >= 0;
    case RENDER_CARD_CRAB:     return (t->sd_unlocks & SD_ITEM_CRAB) && t->cr_x >= 0;
    default:                   return false;
    }
}
static bool crit_card_rect(int card, int *x, int *y, int *w) {   /* every critter card is centred; their heights differ */
    int h;
    switch (card) {
    case RENDER_CARD_SNAIL:    *w = SNAIL_CARD_W; h = SNAIL_CARD_H; break;
    case RENDER_CARD_SHRIMP:   *w = SHRIMP_CARD_W; h = SHRIMP_CARD_H; break;
    case RENDER_CARD_URCHIN:   *w = URCHIN_CARD_W; h = URCHIN_CARD_H; break;
    case RENDER_CARD_SEAHORSE: *w = SEAHORSE_CARD_W; h = SEAHORSE_CARD_H; break;
    case RENDER_CARD_CRAB:     *w = CRAB_CARD_W; h = CRAB_CARD_H; break;
    default: return false;
    }
    *x = (TANK_W - *w) / 2; *y = (TANK_H - h) / 2;
    return true;
}
static int crit_count(const tank_t *t) {
    int n = 0;
    for (int i = 0; i < N_CRITTER_CARDS; i++) n += render_critter_here(t, CRITTER_ORDER[i]);
    return n;
}
int render_critter_step(const tank_t *t, int card, int dir) {
    int at = -1;
    for (int i = 0; i < N_CRITTER_CARDS; i++) if (CRITTER_ORDER[i] == card) at = i;
    if (at < 0) return card;
    for (int k = 1; k < N_CRITTER_CARDS; k++) {
        int c = CRITTER_ORDER[((at + (dir < 0 ? -k : k)) % N_CRITTER_CARDS + N_CRITTER_CARDS) % N_CRITTER_CARDS];
        if (render_critter_here(t, c)) return c;
    }
    return card;
}
int render_critter_nav_hit(int card, float x, float y) {
    int X, Y, W;
    if (!crit_card_rect(card, &X, &Y, &W)) return 0;
    if (y < Y - 12 || y >= Y + RENDER_CRIT_NAV_IN + RENDER_CRIT_NAV_W + 14) return 0;      /* the box plus slop, most of it below and out */
    if (x >= X - 12 && x < X + RENDER_CRIT_NAV_IN + RENDER_CRIT_NAV_W + 12) return -1;
    if (x >= X + W - RENDER_CRIT_NAV_IN - RENDER_CRIT_NAV_W - 12 && x < X + W + 12) return 1;
    return 0;
}
static void crit_nav_box(ctx_t *c, int bx, int by, int dir, uint32_t rgb) {
    shade_box(c, bx, by, RENDER_CRIT_NAV_W, RENDER_CRIT_NAV_W);
    rect_edge(c, bx, by, RENDER_CRIT_NAV_W, RENDER_CRIT_NAV_W, rgb); rect_edge(c, bx + 1, by + 1, RENDER_CRIT_NAV_W - 2, RENDER_CRIT_NAV_W - 2, 0x1c2f36);
    int cx = bx + RENDER_CRIT_NAV_W / 2, cy = by + RENDER_CRIT_NAV_W / 2;
    for (int i = 0; i < 9; i++) {                            /* a solid triangle, 9 wide, 17 tall, pointing its way */
        int hh = 8 - i;
        rect_fill(c, dir > 0 ? cx - 4 + i : cx + 4 - i, cy - hh, 1, 2 * hh + 1, rgb);
    }
}
static void crit_nav_draw(ctx_t *c, const tank_t *t, int card) {
    int X, Y, W;
    if (!crit_card_rect(card, &X, &Y, &W)) return;
    const uint32_t rgb = crit_count(t) > 1 ? 0x9fd8e2 : 0x2c4a52;   /* dim with one: nowhere to go (brightness, never hue) */
    crit_nav_box(c, X + RENDER_CRIT_NAV_IN, Y + RENDER_CRIT_NAV_IN, -1, rgb);
    crit_nav_box(c, X + W - RENDER_CRIT_NAV_IN - RENDER_CRIT_NAV_W, Y + RENDER_CRIT_NAV_IN, 1, rgb);
}

static void tools_draw(ctx_t *c, const tank_t *t);   /* the toolbox, below */
static void next_draw(ctx_t *c, const tank_t *t);    /* the NEXT FISH arrow, below */
static void give_box_draw(ctx_t *c, const tank_t *t, int fish);   /* GIVE under the toolbox while linked (0.4), below */
void render_stats_card(const tank_t *t, int fish_idx, uint16_t *fb, int stride) {
    if (render_critter_here(t, fish_idx)) {
        ctx_t sc = ctx_full(fb, stride, 1.0f);
        switch (fish_idx) {
        case RENDER_CARD_SHRIMP:   shrimp_card_draw(&sc, t); break;
        case RENDER_CARD_URCHIN:   urchin_card_draw(&sc, t); break;
        case RENDER_CARD_CRAB:     crab_card_draw(&sc, t); break;
        case RENDER_CARD_SEAHORSE: seahorse_card_draw(&sc, t); break;
        default:                   snail_card_draw(&sc, t); break;
        }
        crit_nav_draw(&sc, t, fish_idx);
        return;
    }
    if (tank_is_visitor(fish_idx)) {                  /* a visitor: its passport */
        if (!t->vis_used[fish_idx - N_FISH_MAX]) return;
        ctx_t sc = ctx_full(fb, stride, 1.0f);
        passport_card_draw(&sc, fb, stride, t, fish_idx - N_FISH_MAX);
        return;
    }
    if (fish_idx == RENDER_CARD_SNAIL || fish_idx == RENDER_CARD_SHRIMP || fish_idx == RENDER_CARD_URCHIN
        || fish_idx == RENDER_CARD_SEAHORSE || fish_idx == RENDER_CARD_CRAB) return;   /* its critter is gone */
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
    give_box_draw(&c, t, fish_idx);
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
#define GIVE_GOLD 0xe8c872
enum { GV_HIDDEN, GV_GIVE, GV_DIM, GV_ASKING, GV_LEAVING };   /* (the GIVE section below) */
/* GIVE on the stat card (0.4): the keeper page card's button in a box of its own, its states the same
   (give_look / give_button, the GIVE section below) - while the tank is linked, for a fish of ours */
static int give_look(const tank_t *t, int fish);
static void give_button(ctx_t *c, int x, int y, int w, int h, int look, int scale);
static void give_box_draw(ctx_t *c, const tank_t *t, int fish) {
    int look = give_look(t, fish);
    if (look == GV_HIDDEN) return;
    const int X = RENDER_GIVE_X, Y = RENDER_GIVE_Y, W = RENDER_GIVE_W, H = RENDER_GIVE_H;
    shade_box(c, X, Y, W, H);
    if (look == GV_LEAVING) {                        /* the stamp, as on the keeper page */
        rect_edge(c, X, Y, W, H, GIVE_GOLD); rect_edge(c, X + 3, Y + 3, W - 6, H - 6, GIVE_GOLD);
        draw_text(c, X + (W - text_w("GIVEN", 2)) / 2, Y + (H - 14) / 2, 2, GIVE_GOLD, "GIVEN");
        return;
    }
    give_button(c, X + 4, Y + 4, W - 8, H - 8, look, 2);
}
bool render_give_hit(const tank_t *t, int fish_idx, float x, float y) {
    if (give_look(t, fish_idx) == GV_HIDDEN) return false;
    return x >= RENDER_GIVE_X - 4 && x < RENDER_GIVE_X + RENDER_GIVE_W + 8 && y >= RENDER_GIVE_Y - 4 && y < RENDER_GIVE_Y + RENDER_GIVE_H + 10;
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
    { 0x00, 0x15, 0x0e, 0x1f, 0x0e, 0x15, 0x00 }, /* * (the family page's hidden copy, 0.4) */
    { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 }, /* < (the passport's machine line) */
};
static const char FONT_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789?!.,-:'/+%~*<";
static const uint8_t *glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    const char *p = ch ? strchr(FONT_CHARS, ch) : NULL;
    return p ? FONT5X7[p - FONT_CHARS] : NULL;
}
/* Ä Ö Ü for German (lang.h): dots on row 0, the letter in rows 1-6, so the
 * cell stays 7 rows. UTF-8; lower case draws as the capital. */
static const uint8_t FONT_UMLAUT[][7] = {
    { 0x11, 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11 }, /* Ä */
    { 0x11, 0x0e, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* Ö */
    { 0x0a, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* Ü */
};
static const uint8_t *glyph_next(const char **s) {    /* the glyph of the character at *s, and *s past it */
    const unsigned char *u = (const unsigned char *)*s;
    if (u[0] == 0xc3 && u[1]) {                        /* Ä Ö Ü (C3 84 / 96 / 9C), ä ö ü (C3 A4 / B6 / BC) */
        unsigned char b = u[1] & ~0x20; *s += 2;
        return b == 0x84 ? FONT_UMLAUT[0] : b == 0x96 ? FONT_UMLAUT[1] : b == 0x9c ? FONT_UMLAUT[2] : NULL;
    }
    return glyph(*(*s)++);
}
/* words_w / draw_words: as written (a name). text_w / draw_text: translated. */
static int words_w(const char *s, int scale) {
    int n = 0;
    while (*s) { glyph_next(&s); n++; }                        /* the cells draw_words steps */
    return n ? n * 6 * scale - scale : 0;
}
static void draw_words(ctx_t *c, int x, int y, int scale, uint32_t rgb, const char *s) {
    src_t col = src_color(rgb, c->dim);
    for (; *s; x += 6 * scale) {
        const uint8_t *g = glyph_next(&s);
        if (!g) continue;
        for (int r = 0; r < 7; r++)
            for (int k = 0; k < 5; k++)
                if (g[r] & (0x10 >> k))
                    for (int yy = 0; yy < scale; yy++)
                        span(c, x + k * scale, x + k * scale + scale - 1, y + r * scale + yy, &col, 255);
    }
}
static int text_w(const char *s, int scale) { return words_w(lang_tr(s), scale); }
static void draw_text(ctx_t *c, int x, int y, int scale, uint32_t rgb, const char *s) {
    const char *tr = lang_tr(s);
#ifdef LANG_AUDIT
    if (tr != s && (x - c->ox < 0 || x + words_w(tr, scale) - c->ox > c->w)) lang_audit("OFF THE SCREEN", tr);
#endif
    draw_words(c, x, y, scale, rgb, tr);
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
#ifdef LANG_AUDIT
    if (text_w(label, scale) > W - 4 && text_w(label, scale) > words_w(label, scale)) lang_audit("WIDER THAN ITS BUTTON", lang_tr(label));
#endif
    draw_text(c, x + (W - text_w(label, scale)) / 2, y + (H - 7 * scale) / 2, scale, 0xffffff, label);
}
/* the same primitives for panels built elsewhere (render.h) */
int  render_text_w(const char *s, int scale) { return text_w(s, scale); }
void render_text(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const char *s) {
    ctx_t c = ctx_page(fb, stride); draw_text(&c, x, y, scale, rgb, s);
}
int  render_name_w(const char *s, int scale) { return words_w(s, scale); }
void render_name(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const char *s) {
    ctx_t c = ctx_page(fb, stride); draw_words(&c, x, y, scale, rgb, s);
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
static void draw_still(uint16_t *fb, int stride, float x, float y, float size, fish_t *f, const fish_t *form, double clock, float room) {
    ctx_t c = ctx_page(fb, stride);
    f->heading = 0; f->speed = 40;
    f->hunger = 4; f->stress = 0; f->goal.id = GOAL_EXPLORE;
    if (form) { f->shape = form->shape; f->fins = form->fins; f->pattern = form->pattern; }
    if (room > 0) fish_fit(f, x, y, size, room); else { f->x = x; f->y = y; f->size = size; }
    tank_fish_face(f);                           /* side-on, facing right */
    draw_fish_core(&c, f, tank_phase(clock, tank_fish_beat_rate(f)), false, f->size, 1e9f);   /* (a still's pace never changes: the clock's) */
}
void render_fish_preview(uint16_t *fb, int stride, float x, float y, float size,
                         uint32_t body, uint32_t fin, uint32_t accent, const fish_t *form, float room, double clock) {
    fish_t f; memset(&f, 0, sizeof f);
    f.stage = STAGE_ADULT;                       /* grown: the crest shows the fin colour */
    f.color = body; f.fin = fin; f.accent = accent;
    draw_still(fb, stride, x, y, size, &f, form, clock, room);
}
void render_fish_portrait(uint16_t *fb, int stride, float x, float y, float size, const fish_t *who, float room, double clock) {
    fish_t f; memset(&f, 0, sizeof f);
    f.stage = who->stage;
    f.color = who->color; f.fin = who->fin; f.accent = who->accent;
    draw_still(fb, stride, x, y, size, &f, who, clock, room);
}
/* a fish AS IT IS NOW (its own stage) in the given colours - the milestones
   rows and its card: a fry shows a fry's plain tail, its fins come in at
   ADULT as they do in the tank (0.4; the rows drew every fish grown before).
   `cap_adult`: the rows draw an elder as an adult, as they always have - an
   elder's long tail would reach past the row's left edge (the watch's page
   starts 19 px off the glass); the card has the room for it */
static void fish_as_is(uint16_t *fb, int stride, float x, float y, float size, const fish_t *who,
                       uint32_t body, uint32_t fin, uint32_t accent, float room, bool cap_adult, double clock) {
    fish_t f; memset(&f, 0, sizeof f);
    f.stage = cap_adult && who->stage > STAGE_ADULT ? STAGE_ADULT : who->stage;
    f.color = body; f.fin = fin; f.accent = accent;
    draw_still(fb, stride, x, y, size, &f, who, clock, room);
}
void render_fish_sample(uint16_t *fb, int stride, float x, float y, float size, const fish_t *who, double clock) {
    fish_t f; memset(&f, 0, sizeof f);
    f.stage = who->stage;
    f.color = who->color; f.fin = who->fin; f.accent = who->accent;
    draw_still(fb, stride, x, y, size, &f, who, clock, 0);
}
void render_confirm_reset(uint16_t *fb, int stride, float frac) {
    ctx_t c = ctx_page(fb, stride);              /* ignores night dimming, like the card */
    const int X = RENDER_CONFIRM_X, Y = RENDER_CONFIRM_Y, W = RENDER_CONFIRM_W, H = RENDER_CONFIRM_H;
    rect_fill(&c, X, Y, W, H, 0x04141a);
    rect_edge(&c, X, Y, W, H, 0x9fd8e2); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    const char *title = "RESET TANK?";
    draw_text(&c, X + (W - text_w(title, 3)) / 2, Y + 20, 3, 0xffffff, title);
    const char *l1 = "START OVER WITH TWO FRY", *l2 = "EVERYTHING ELSE IS LOST";
    draw_text(&c, X + (W - text_w(l1, 2)) / 2, Y + 58, 2, 0x9fd8e2, l1);
    draw_text(&c, X + (W - text_w(l2, 2)) / 2, Y + 78, 2, 0x9fd8e2, l2);
    /* NO is the calm one; YES wears the stress red */
    button(&c, RENDER_CONFIRM_NO_X,  RENDER_CONFIRM_BTN_Y, RENDER_CONFIRM_BTN_W, RENDER_CONFIRM_BTN_H, 0x1c2f36, 0x9fd8e2, "NO", 3);
    button(&c, RENDER_CONFIRM_YES_X, RENDER_CONFIRM_BTN_Y, RENDER_CONFIRM_BTN_W, RENDER_CONFIRM_BTN_H, 0x7a2028, 0xf25b65, "YES", 3);
    /* the prompt lets itself go: a bar draining toward the timeout */
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    const int bw = W - 48, bx = X + 24, by = Y + H - 16;
    rect_fill(&c, bx, by, bw, 3, 0x2a3f45);
    int fw = (int)(bw * frac + 0.5f);
    if (fw > 0) rect_fill(&c, bx, by, fw, 3, 0x9fd8e2);
}
int render_confirm_hit(float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;                    /* the page's own coordinates */
    const int m = 10;                                   /* a fingertip's slop around each button */
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
void render_battery_info(uint16_t *fb, int stride, const bat_info_t *bi, double clock) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    bool pw = BAT_ON_POWER(bi->state);
    char lab[3][16], val[3][32], d[16]; int nr = 0;
    if (bi->since_min >= 0) {
        snprintf(lab[nr], sizeof lab[nr], pw ? "PLUGGED IN" : "UNPLUGGED");
        if (bi->since_min < 1) snprintf(val[nr], sizeof val[nr], "JUST NOW");
        else { battery_fmt_dur(d, sizeof d, bi->since_min); snprintf(val[nr], sizeof val[nr], lang_tr("%s AGO"), d); }
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
    if (bi->state == BAT_CHARGING) { battery_fmt_dur(d, sizeof d, bi->left_min); snprintf(est, sizeof est, lang_tr("FULL IN ABOUT %s"), d); }
    else if (bi->state == BAT_FULL) snprintf(est, sizeof est, "READY TO UNPLUG");
    else if (bi->state == BAT_PLUGGED) snprintf(est, sizeof est, "NOT CHARGING RIGHT NOW");
    else if (bi->left_min < 5) snprintf(est, sizeof est, "ALMOST EMPTY");
    else { battery_fmt_dur(d, sizeof d, bi->left_min); snprintf(est, sizeof est, lang_tr("ABOUT %s LEFT"), d); }
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
#define TANK_BADGE_N 12                         /* (11 lost the crab's badge off the row: 0.4's FIRST_MUTATION met 0.3.9's CRAB_FIND at the merge) */
static const badge_t TANK_BADGES[TANK_BADGE_N] = {
    { TMS_FIRST_FEEDING, &icon_ms_first_feeding },    { TMS_FIRST_TRIM, &icon_ms_first_trimming },
    { TMS_FIRST_CLEANING, &icon_ms_first_glass_cleaning }, { TMS_FIRST_FULL_NIGHT, &icon_ms_first_quiet_night },
    { TMS_FIRST_PLAY_SESSION, &icon_ms_first_play_session }, { TMS_CHANGED_SOMEONE, &icon_ms_tank_changed_someone },
    { TMS_FULL_SCHOOL, &icon_ms_full_school },        /* 2026-09-30: only once the shrimp are bought (tank_badge_shown) */
    { TMS_FIRST_REEF, &icon_ms_first_reef },          /* 2026-10-02: only once the reef cluster is bought */
    { TMS_FIRST_MUTATION, &icon_ms_first_mutation },  /* 0.4 (Strato's helix): only once the tank has bred - a trio */
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
    if (bit == TMS_FIRST_MUTATION) return (t->tank_ms_bits & (TMS_TRIO | bit)) != 0;
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
static int tank_pages(const tank_t *t) { return (tank_badge_n(t) + MSP_TANK_PER_ROW - 1) / MSP_TANK_PER_ROW; }
/* the tank badges drawn since the page opened (0.4.1 audit round 1, R-seam-keeper-sees S6): closing it marks seen only
   these - a badge on a page the keeper never flipped to keeps its ring (render_milestones_leave clears it) */
static uint32_t g_ms_tshown;
uint32_t render_milestones_tank_shown(void) { return g_ms_tshown; }
uint32_t render_tank_badge_bits(void) { uint32_t b = 0; for (int k = 0; k < TANK_BADGE_N; k++) b |= TANK_BADGES[k].bit; return b; }
static const char *const STAGE_WORDS[4] = { "FRY", "JUVENILE", "ADULT", "ELDER" };
/* the detail modal (a tap on a badge / name / strip): what to show until
 * the next tap. caption[0] == 0 means no modal. */
static char g_ms_caption[72], g_ms_title[16];
static char g_ms_caption2[48];       /* (translated where it is written, drawn as built) */       /* the caption's second line (the fry checklist's sentences), or empty */
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
   second sells - but not within MSP_SELL_WAIT_S (0.2 s) of the arm, so a
   doubled tap cannot sell a fish (THE TOUCH RULE, touch_gate.h: never
   restarted - before 11-06 every spent tap restarted a 0.6 s wait). A fish the tank will not part with (the last pair,
   or a fry still owed its welcome) shows SELL dim, and the line above stays
   empty until the dim button is tapped - then it gives the reason (Strato,
   2026-10-01: the reason "only if the gray sell button is tapped"). The card stands where a gate's modal does (the taller one), inside
   the box every board's glass shows whole: no per-board layout. */
#define MSP_CARD_BTN_W   116
#define MSP_CARD_BTN_GAP 24
#define MSP_SELL_WAIT_S  TAP_W_S   /* (0.6 s, restarted by every tap, before THE TOUCH RULE: touch_gate.h) */
static bool  g_ms_sell_armed;
static bool  g_ms_sell_asked;        /* the dim SELL was tapped: the line says why it stays */
static double g_ms_sell_clock;       /* the tank clock when SELL was armed */
/* GIVE (tank link phase 6; Strato 2026-10-20, from the mockups docs/media/give-sheet-*.png): on the
   fish card between RENAME and SELL, ONLY while a door is open. A tap opens the GIVE modal (below; it
   replaced SURE? on 11-02), the card's line says ASKING / GIVEN TO and the answer a while. */
static uint32_t g_ms_given_uid;      /* the fish whose card says GIVEN: the card closes once it has left (rows shift then) */
static const char *const BADGE_WORDS[LINK_BADGES];   /* (the passport's, below) */
static void gm_open(const tank_t *t, int fish);      /* the GIVE modal, below */
static int give_look(const tank_t *t, int fish) {
    const gift_book_t *g = gift_of(t);
    if (!g || fish < 0 || fish >= t->n_fish) return GV_HIDDEN;
    uint32_t uid = t->fish[fish].uid;
    if (g->leaving_uid == uid) return GV_LEAVING;
    if (g->asking_uid == uid || g->wait_uid == uid || g->want_uid == uid) return GV_ASKING;   /* (waiting for a fish in a doorway: asking already) */
    if (!t->door_open[0] && !t->door_open[1]) return GV_HIDDEN;   /* unlinked: the card is RENAME | SELL as ever */
    if (!progression_fish_givable(t, fish)) return GV_DIM;
    return GV_GIVE;
}
/* the card's line while GIVE has something to say (false: SELL's line, as before) */
static bool give_line(const tank_t *t, int fish, char *out, size_t n) {
    const gift_book_t *g = gift_of(t);
    if (!g || fish < 0 || fish >= t->n_fish) return false;
    int look = give_look(t, fish);
    char who[20];                                   /* "THE TRIANGLE TANK" / "THE TANK NEXT DOOR": 18 at most (GIVEN TO + it fits the line's 32) */
    if (g->peer_badge >= 0 && g->peer_badge < LINK_BADGES) snprintf(who, sizeof who, lang_tr("THE %s TANK"), lang_tr(BADGE_WORDS[g->peer_badge]));
    else snprintf(who, sizeof who, "%s", lang_tr("THE TANK NEXT DOOR"));
    if (look == GV_LEAVING) { snprintf(out, n, lang_tr("GIVEN TO %s"), who); return true; }
    if (look == GV_ASKING)  { snprintf(out, n, lang_tr("ASKING %s"), who); return true; }
    if (g->said_uid == t->fish[fish].uid && t->clock < g->said_until) {
        static const char *const SAID[] = { "THEY SAID NO THANKS", "THEIR TANK IS FULL", "NO ANSWER CAME", "", "CANNOT BE GIVEN RIGHT NOW" };
        snprintf(out, n, "%s", SAID[g->said_reason <= LINK_GIVE_NO_OTHER ? g->said_reason : LINK_GIVE_NO_OTHER]);
        return out[0] != 0;
    }
    return false;
}
static void give_button(ctx_t *c, int x, int y, int w, int h, int look, int scale) {
    if (look == GV_GIVE)   { button(c, x, y, w, h, 0x1c2f36, GIVE_GOLD, "GIVE", scale); return; }
    if (look == GV_ASKING) { button(c, x, y, w, h, 0x155e58, GIVE_GOLD, "ASKING", scale); return; }
    const char *s = look == GV_LEAVING ? "GIVEN" : "GIVE";                /* dim: not now (or on its way) */
    rect_fill(c, x, y, w, h, 0x04141a); rect_edge(c, x, y, w, h, MSP_DIM);
    draw_text(c, x + (w - text_w(s, scale)) / 2, y + (h - 7 * scale) / 2, scale, MSP_DIM, s);
}
/* a tap on GIVE - the keeper card's or the stat card's box: the GIVE modal (GIVE, ASKING and the dim GIVE alike; GIVEN is no button) */
static void give_tap(const tank_t *t, int fish) {
    int look = give_look(t, fish);
    g_ms_sell_armed = false;
    if (!gift_of(t) || look == GV_HIDDEN || look == GV_LEAVING) return;
    gm_open(t, fish);
}
void render_give_tap(const tank_t *t, int fish_idx) { give_tap(t, fish_idx); }   /* the stat card's GIVE box: the keeper card's taps */

/* ---- THE GIVE MODAL (0.4; Strato 2026-11-02 "Build option B", working-assets/link-edge/mock/sheet_B_*.png) ----
 * Both ways to give - the stat card's quick GIVE box and the keeper card's GIVE - open this one card, over the live tank
 * or the keeper page: the fish, the tank it would go to (the passport's HOME words, its badge's shape, an arrow to its
 * side), IT WOULD LIVE THERE FOR GOOD, GIVE | BACK (it replaced SURE?). Both doors open (the middle of three): the pick
 * comes first - GIVE <FISH> TO, a row a tank, BACK - and picking is the yes. Then ASKING THEIR KEEPER with the other
 * keeper's time as a bar, CALL OFF. A NO, a door or the tank picked gone, the fish gone: the words stay up until OK. A
 * dim GIVE opens it with why. Every button but BACK ignores a press from before its step and, where a tap brought the
 * step, that tap's double (THE TOUCH RULE, touch_gate.h); BACK is always the way out. A tap off the card: nothing. */
#define GM_W   336
#define GM_H   252                  /* the fish's card; the pick is GM_PICK_H */
#define GM_PICK_H 232
#define GM_BTN_W 140
#define GM_BTN_H 34
#define GM_ROW_Y0 38                /* the pick's rows (card coordinates) */
#define GM_ROW_PITCH 68
#define GM_ROW_H 58
_Static_assert(GM_W * GM_H <= GIFT_CARD_W * GIFT_CARD_H && GM_PICK_H <= GM_H, "the GIVE modal's bake is a gift card's size");
static struct {
    bool on; uint32_t uid; double opened, step_t, asked; int step;
    bool pick; int8_t edge; uint32_t peer[LINK_EDGES]; int8_t badge[LINK_EDGES];   /* latched as it opens: the door and tank, or the two */
    char line[32];                                                                  /* GM_SAID / GM_WHY: the words, until OK */
    bool by_tap;                                                                    /* the step a tap brought up (W applies; touch_gate.h) */
    unsigned drawn_frame;                                                           /* the frame it was last drawn in (0: not yet) */
} g_gm;
static int gm_fish(const tank_t *t) {
    for (int i = 0; i < t->n_fish; i++) if (t->fish[i].uid == g_gm.uid) return i;
    return -1;
}
static void gm_say(const tank_t *t, const char *why) { snprintf(g_gm.line, sizeof g_gm.line, "%s", why); g_gm.step = RENDER_GM_SAID; g_gm.step_t = t->clock; g_gm.by_tap = false; }
static const char *gm_why(const tank_t *t, int fish) {   /* keep-two as the GIVE counts it: fish of ours on their way out are gone (803f1d2) */
    return t->fish[fish].born_away ? "VISITING NEXT DOOR"   /* (a fry of ours not yet home: its welcome first - R-seam F4) */
         : !progression_keeps_two(t, fish) ? "KEEP AT LEAST TWO FISH" : "NAME THE NEW FRY FIRST"; }
static bool gm_row_open(const tank_t *t, const gift_book_t *g, int e) { return t->door_open[e] && g->door_peer[e] && g->door_peer[e] == g_gm.peer[e]; }
static void gm_open(const tank_t *t, int fish) {
    const gift_book_t *g = gift_of(t);
    memset(&g_gm, 0, sizeof g_gm);
    g_gm.on = true; g_gm.uid = t->fish[fish].uid; g_gm.opened = g_gm.step_t = t->clock; g_gm.edge = -1; g_gm.by_tap = true;
    g_gm.asked = t->clock + 1e-3f;                    /* (an answer from before it opened is not this modal's) */
    for (int e = 0; e < LINK_EDGES; e++) { g_gm.peer[e] = t->door_open[e] ? g->door_peer[e] : 0; g_gm.badge[e] = g->door_badge[e]; }
    const fish_t *f = &t->fish[fish];
    if (g->asking_uid == g_gm.uid || g->wait_uid == g_gm.uid || g->want_uid == g_gm.uid) {   /* (a tap on ASKING: its modal, CALL OFF) */
        g_gm.step = RENDER_GM_ASKING; g_gm.asked = g->asking_uid == g_gm.uid ? g->asking_since : g->wait_since;
        for (int e = 0; e < LINK_EDGES; e++) if (g->peer_badge >= 0 && g->peer_badge == g->door_badge[e]) g_gm.edge = (int8_t)e;
        return;
    }
    int e = f->away ? f->door_edge : t->door_open[LINK_LEFT] && t->door_open[LINK_RIGHT] ? -2 : t->door_open[LINK_LEFT] ? LINK_LEFT : t->door_open[LINK_RIGHT] ? LINK_RIGHT : -1;
    if (!progression_fish_givable(t, fish)) {          /* a dim GIVE: why (TO the one tank next door, as the mockup) */
        snprintf(g_gm.line, sizeof g_gm.line, "%s", gm_why(t, fish)); g_gm.step = RENDER_GM_WHY;
        if (e >= 0 && e < LINK_EDGES && g_gm.peer[e]) g_gm.edge = (int8_t)e;
        return;
    }
    if (e == -2) { g_gm.pick = true; g_gm.step = RENDER_GM_PICK; return; }
    if (e < 0 || e >= LINK_EDGES || !g_gm.peer[e]) { gm_say(t, "CANNOT BE GIVEN RIGHT NOW"); g_gm.by_tap = true; return; }   /* (a tap opened it: W on its OK) */
    g_gm.edge = (int8_t)e; g_gm.step = RENDER_GM_CONFIRM;
}
/* where the modal is now - the book and the doors move it on (and may close it) */
static int gm_eval(const tank_t *t) {
    if (!g_gm.on) return RENDER_GM_OFF;
    const gift_book_t *g = gift_of(t);
    int step = g_gm.step;
    if (step == RENDER_GM_SAID || step == RENDER_GM_WHY) return step;          /* (until OK) */
    int i = gm_fish(t);
    if (g && g->leaving_uid == g_gm.uid) { g_gm.on = false; return RENDER_GM_OFF; }   /* accepted: the card's GIVEN stamp takes it from here */
    if (!g || i < 0) { gm_say(t, "CANNOT BE GIVEN RIGHT NOW"); return g_gm.step; }
    if (g->asking_uid == g_gm.uid || g->wait_uid == g_gm.uid || g->want_uid == g_gm.uid) step = RENDER_GM_ASKING;
    else if (g->said_uid == g_gm.uid && g->said_until - GIFT_SAID_S >= g_gm.asked) {   /* this gift's NO */
        static const char *const SAID[] = { "THEY SAID NO THANKS", "THEIR TANK IS FULL", "NO ANSWER CAME", "", "CANNOT BE GIVEN RIGHT NOW" };
        gm_say(t, SAID[g->said_reason <= LINK_GIVE_NO_OTHER ? g->said_reason : LINK_GIVE_NO_OTHER]);
        if (!g_gm.line[0]) g_gm.on = false;                                     /* (called off: nothing to say) */
        return g_gm.on ? g_gm.step : RENDER_GM_OFF;
    }
    else if (step == RENDER_GM_ASKING) { g_gm.on = false; return RENDER_GM_OFF; }   /* (taken back without a word) */
    else if (!progression_fish_givable(t, i)) { snprintf(g_gm.line, sizeof g_gm.line, "%s", gm_why(t, i)); step = RENDER_GM_WHY; }
    else if (g_gm.pick) {
        if (!gm_row_open(t, g, LINK_LEFT) && !gm_row_open(t, g, LINK_RIGHT)) { gm_say(t, "CANNOT BE GIVEN RIGHT NOW"); return g_gm.step; }
    } else if (g_gm.edge < 0 || !gm_row_open(t, g, g_gm.edge)) { gm_say(t, "CANNOT BE GIVEN RIGHT NOW"); return g_gm.step; }
    if (step != g_gm.step) { g_gm.step = step; g_gm.step_t = t->clock; g_gm.by_tap = false; }   /* (an event moved it: G1 only) */
    return step;
}
/* a neighbor's keeper-given name, if one of its fish is here or in the visitor collection ("" = its HOME WATERS alone) */
static const char *gm_tank_name(const tank_t *t, uint32_t tank) {
    for (int k = 0; k < N_VISIT_MAX; k++) if (t->vis_used[k] && t->vis_home[k] == tank && t->vis_home_name[k][0]) return t->vis_home_name[k];
    for (int k = collection_count() - 1; k >= 0; k--) { const coll_entry_t *e = collection_get(k); if (e && e->home == tank && e->home_name[0]) return e->home_name; }
    return "";
}
static void gm_arrow(ctx_t *c, int cx, int cy, int half, int dir, uint32_t rgb) {   /* a solid triangle pointing dir: half+1 wide, 2*half+1 tall */
    for (int i = 0; i <= half; i++) { int hh = half - i; rect_fill(c, cx + (dir > 0 ? -half / 2 + i : half / 2 - i), cy - hh, 1, 2 * hh + 1, rgb); }
}
static void gm_split(const char *s, char *a, char *b, int n) {   /* two lines at the space nearest the middle */
    int len = (int)strlen(s), best = -1, cells = 0, mid, at = 0, bat = 0;   /* the middle in glyph cells (an umlaut is two bytes) */
    for (int i = 0; i < len; i++) cells += ((unsigned char)s[i] & 0xC0) != 0x80;
    mid = cells / 2;
    for (int i = 0; i < len; i++) {
        if (s[i] == ' ' && (best < 0 || abs(at - mid) < abs(bat - mid))) { best = i; bat = at; }
        at += ((unsigned char)s[i] & 0xC0) != 0x80;
    }
    if (best < 0) { snprintf(a, n, "%s", s); b[0] = 0; return; }
    snprintf(a, n, "%.*s", best, s); snprintf(b, n, "%s", s + best + 1);
}
static int gm_card_h(void) { return g_gm.step == RENDER_GM_PICK ? GM_PICK_H : GM_H; }
static void gm_frame(int *X, int *Y) { *X = (TANK_W - GM_W) / 2; *Y = (TANK_H - gm_card_h()) / 2; }
/* the card but its portrait and its bar's fill (gm_draw draws those live) */
static void gm_paint(ctx_t *c, const tank_t *t, int step) {
    const gift_book_t *g = gift_of(t);
    const uint32_t GOLD = GIVE_GOLD, INK = 0x13324f, LBL = 0x7fa3b5, DIM = 0x3f6a72;
    int i = gm_fish(t);
    const char *nm = i >= 0 ? t->fish[i].name : "";
    const int W = GM_W, H = gm_card_h(); int X, Y; gm_frame(&X, &Y);
    rect_fill(c, X, Y, W, H, 0x0e2233);
    rect_fill(c, X, Y, W, 26, INK);
    char title[40]; snprintf(title, sizeof title, lang_tr(step == RENDER_GM_PICK ? "GIVE %s TO" : "GIVE %s?"), nm);
    draw_words(c, X + (W - words_w(title, 2)) / 2, Y + 7, 2, GOLD, title);   /* (a name in it) */
    rect_edge(c, X, Y, W, H, GOLD); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, INK);
    if (step == RENDER_GM_PICK) {
        for (int s = 0; s < LINK_EDGES; s++) {   /* LINK_LEFT on top, its arrow at the left end; LINK_RIGHT under it, arrow right */
            const int RX = X + 12, RY = Y + GM_ROW_Y0 + s * GM_ROW_PITCH, RW = W - 24, RH = GM_ROW_H;
            bool off = !gm_row_open(t, g, s);
            uint32_t edge = off ? DIM : GOLD;
            rect_fill(c, RX, RY, RW, RH, 0x1c2f36);
            rect_edge(c, RX, RY, RW, RH, edge); rect_edge(c, RX + 1, RY + 1, RW - 2, RH - 2, edge);
            gm_arrow(c, s ? RX + RW - 18 : RX + 18, RY + RH / 2, 11, s ? 1 : -1, off ? DIM : 0xffffff);
            char lbl[40], val[24];
            home_fields(g_gm.peer[s], gm_tank_name(t, g_gm.peer[s]), "HOME", lbl, sizeof lbl, val, sizeof val);
            int tx = s ? RX + 16 : RX + 40, vx = tx, b = g_gm.badge[s];
            draw_words(c, tx, RY + 10, 1, off ? 0xc8d6de : LBL, off ? lang_tr("NOT LINKED NOW") : lbl);   /* (closed while up: which one, dim, no tap) */
            if (b >= 0 && b < LINK_BADGES) { link_badge_draw(c, tx + 7, RY + 33, 7, b, off ? 90 : 255); vx += 24; }   /* the shape AND the name */
            draw_words(c, vx, RY + 26, 2, off ? DIM : 0xffffff, val);
        }
        button(c, X + (W - GM_BTN_W) / 2, Y + H - GM_BTN_H - 12, GM_BTN_W, GM_BTN_H, 0x1c2f36, 0x9fd8e2, "BACK", 2);
        return;
    }
    const int PX = X + 12, PY = Y + 36, PW = 100, PH = 84;
    rect_fill(c, PX, PY, PW, PH, 0x0a1824); rect_edge(c, PX, PY, PW, PH, 0x9fd8e2);
    const int FX = PX + PW + 12;
    draw_text(c, FX, Y + 36, 1, LBL, "NAME");
    draw_words(c, FX, Y + 46, 2, 0xffffff, nm);           /* a name: as built */
    if (g_gm.edge >= 0 && g_gm.peer[g_gm.edge]) {     /* TO: the tank and its door */
        int e = g_gm.edge; char lbl[40], val[24];
        home_fields(g_gm.peer[e], gm_tank_name(t, g_gm.peer[e]), "TO", lbl, sizeof lbl, val, sizeof val);
        draw_words(c, FX, Y + 72, 1, LBL, lbl);
        int vx = FX, b = g_gm.badge[e];
        if (b >= 0 && b < LINK_BADGES) { link_badge_draw(c, FX + 6, Y + 89, 6, b, 255); vx += 18; }
        draw_words(c, vx, Y + 82, 2, 0xffffff, val);
        gm_arrow(c, vx + words_w(val, 2) + 14, Y + 89, 7, e == LINK_RIGHT ? 1 : -1, 0xffffff);
    }
    char a[48], b[48];                               /* (translated whole, then split: a half is no key) */
    if (step == RENDER_GM_CONFIRM) { snprintf(a, sizeof a, "%s", lang_tr("IT WOULD LIVE THERE")); snprintf(b, sizeof b, "%s", lang_tr("FOR GOOD")); }
    else if (step == RENDER_GM_ASKING) { snprintf(a, sizeof a, "%s", lang_tr("ASKING THEIR KEEPER")); b[0] = 0; }
    else if (words_w(lang_tr(g_gm.line), 2) <= W - 24) { snprintf(a, sizeof a, "%s", lang_tr(g_gm.line)); b[0] = 0; }
    else gm_split(lang_tr(g_gm.line), a, b, sizeof a);
    draw_words(c, X + (W - words_w(a, 2)) / 2, Y + 136, 2, 0xffffff, a);
    if (b[0]) draw_words(c, X + (W - words_w(b, 2)) / 2, Y + 156, 2, 0xffffff, b);
    if (step == RENDER_GM_ASKING) rect_edge(c, X + 24, Y + 162, W - 48, 6, DIM);   /* the other keeper's time (its fill: live) */
    const int BY = Y + H - GM_BTN_H - 12, gap = W - 2 * GM_BTN_W - 24;
    if (step == RENDER_GM_CONFIRM) {
        button(c, X + 12, BY, GM_BTN_W, GM_BTN_H, 0x1c2f36, GOLD, "GIVE", 2);
        button(c, X + 12 + GM_BTN_W + gap, BY, GM_BTN_W, GM_BTN_H, 0x1c2f36, 0x9fd8e2, "BACK", 2);
    } else button(c, X + (W - GM_BTN_W) / 2, BY, GM_BTN_W, GM_BTN_H, 0x1c2f36, 0x9fd8e2, step == RENDER_GM_ASKING ? "CALL OFF" : "OK", 2);
}
/* what the still card shows (a new key: painted again) */
static uint32_t gm_key(const tank_t *t, int step) {
    const gift_book_t *g = gift_of(t);
    int i = gm_fish(t);
    uint32_t h = bake_hash(2166136261u, (uint32_t)step);
    h = bake_hash(h, lang_now);                      /* (the words are the language's) */
    h = bake_hash(h, g_gm.uid); h = bake_hash_s(h, i >= 0 ? t->fish[i].name : ""); h = bake_hash(h, (uint32_t)(g_gm.edge + 1));
    h = bake_hash_s(h, g_gm.line);
    for (int e = 0; e < LINK_EDGES; e++) {
        h = bake_hash(h, g_gm.peer[e]); h = bake_hash(h, (uint32_t)(g_gm.badge[e] + 1));
        h = bake_hash(h, g && gm_row_open(t, g, e)); h = bake_hash_s(h, gm_tank_name(t, g_gm.peer[e]));
    }
    return h;
}
static void gm_draw(const tank_t *t, uint16_t *fb, int stride) {
    int step = gm_eval(t);
    if (step == RENDER_GM_OFF) return;
    if (g_gm.drawn_frame && g_rframe - g_gm.drawn_frame > 1) { g_gm.step_t = t->clock; g_gm.by_tap = true; }   /* (a tap closed what covered it: W) */   /* back from under a page (a birth flow): a
                                                                   fresh hold - its buttons were never seen (the 11-02 sweep) */
    g_gm.drawn_frame = g_rframe;
    const gift_book_t *g = gift_of(t);
    ctx_t cc = ctx_full(fb, stride, 1.0f); ctx_t *c = &cc;
    const int W = GM_W, H = gm_card_h(); int X, Y; gm_frame(&X, &Y);
    if (g_bake_gm.buf) {
        uint32_t key = gm_key(t, step);
        if (bake_stale(&g_bake_gm, key, H)) {
            ctx_t bc; bool hold; uint32_t *tm = bake_begin(&g_bake_gm, &bc, X, Y, W, H, &hold);
            gm_paint(&bc, t, step);
            bake_end(&g_bake_gm, key, H, tm, hold);
        }
        bake_blit(&g_bake_gm, X, Y, W, H, fb, stride);
    } else gm_paint(c, t, step);
    if (step == RENDER_GM_PICK) return;
    int i = gm_fish(t);                                /* live: the fish swims in its box, the bar runs down */
    const int PX = X + 12, PY = Y + 36, PW = 100, PH = 84;
    if (i >= 0) render_fish_portrait(fb, stride, PX + PW / 2.0f - PAGE_X, PY + PH / 2.0f - PAGE_Y, 1.3f, &t->fish[i], PW - 20, t->clock);   /* (page coordinates) */
    if (step == RENDER_GM_ASKING) {
        float left = g && g->asking_uid == g_gm.uid ? 1.0f - (float)(t->clock - g->asking_since) / (LINK_GIVE_ASK_MS / 1000.0f) : 1.0f;
        left = left < 0 ? 0 : left > 1 ? 1 : left;
        if (left > 0) rect_fill(c, X + 25, Y + 163, (int)((W - 50) * left), 4, 0x9fd8e2);
    }
}
int  render_give_modal_step(const tank_t *t) { return gm_eval(t); }
void render_give_modal_close(void) { g_gm.on = false; }   /* (the director's `link modal off`: as BACK) */
const char *render_give_modal_line(const tank_t *t) {
    int step = gm_eval(t);
    return step == RENDER_GM_CONFIRM ? "IT WOULD LIVE THERE" : step == RENDER_GM_ASKING ? "ASKING THEIR KEEPER" : step == RENDER_GM_PICK ? "" : g_gm.line;
}
void render_give_modal(const tank_t *t, uint16_t *fb, int stride) { if (!g_ms_in) gm_draw(t, fb, stride); }   /* (the keeper page draws its own) */
bool render_give_modal_button(const tank_t *t, int which, int *x, int *y) {
    int step = gm_eval(t);
    if (step == RENDER_GM_OFF) return false;
    int X, Y; gm_frame(&X, &Y); const int H = gm_card_h(), W = GM_W, BY = Y + H - GM_BTN_H - 12 + GM_BTN_H / 2;
    if (step == RENDER_GM_PICK) {
        if (which == RENDER_GM_TAP_LEFT || which == RENDER_GM_TAP_RIGHT) { *x = X + W / 2; *y = Y + GM_ROW_Y0 + (which == RENDER_GM_TAP_RIGHT) * GM_ROW_PITCH + GM_ROW_H / 2; return true; }
        if (which == RENDER_GM_TAP_BACK) { *x = X + W / 2; *y = BY; return true; }
        return false;
    }
    if (step == RENDER_GM_CONFIRM) {
        if (which == RENDER_GM_TAP_GIVE) { *x = X + 12 + GM_BTN_W / 2; *y = BY; return true; }
        if (which == RENDER_GM_TAP_BACK) { *x = X + W - 12 - GM_BTN_W / 2; *y = BY; return true; }
        return false;
    }
    if ((which == RENDER_GM_TAP_CALLOFF && step == RENDER_GM_ASKING) || (which == RENDER_GM_TAP_OK && step != RENDER_GM_ASKING)) { *x = X + W / 2; *y = BY; return true; }
    return false;
}
int render_give_modal_tap(const tank_t *t, float x, float y) {
    int step = gm_eval(t);
    if (step == RENDER_GM_OFF) return RENDER_GM_TAP_NONE;
    gift_book_t *g = gift_of(t);
    int X, Y; gm_frame(&X, &Y); const int H = gm_card_h(), W = GM_W, BY = Y + H - GM_BTN_H - 12;
    if (x < X || x >= X + W || y < Y || y >= Y + H + 10) return RENDER_GM_TAP_NONE;   /* (off the card: nothing - a kid's stray tap) */
    int hit = RENDER_GM_TAP_CARD;
    if (step == RENDER_GM_PICK) {
        for (int s = 0; s < LINK_EDGES; s++) {
            int RY = Y + GM_ROW_Y0 + s * GM_ROW_PITCH;
            if (y >= RY - 4 && y < RY + GM_ROW_H + 4) hit = gm_row_open(t, g, s) ? (s ? RENDER_GM_TAP_RIGHT : RENDER_GM_TAP_LEFT) : RENDER_GM_TAP_CARD;
        }
        if (y >= BY - 8 && x >= X + (W - GM_BTN_W) / 2 - 20 && x < X + (W + GM_BTN_W) / 2 + 20) hit = RENDER_GM_TAP_BACK;
    } else if (y >= BY - 10) {
        if (step == RENDER_GM_CONFIRM) hit = x < X + W / 2 ? RENDER_GM_TAP_GIVE : RENDER_GM_TAP_BACK;
        else if (x >= X + (W - GM_BTN_W) / 2 - 20 && x < X + (W + GM_BTN_W) / 2 + 20) hit = step == RENDER_GM_ASKING ? RENDER_GM_TAP_CALLOFF : RENDER_GM_TAP_OK;
    }
    if (hit == RENDER_GM_TAP_CARD) return hit;
    if (hit == RENDER_GM_TAP_BACK) { g_gm.on = false; return hit; }
    {   /* THE TOUCH RULE (touch_gate.h; Strato on the 1.8: GIVE and CALL OFF "dead" - its 2 s hold, restarted by every tap). A press
           begun before the step came up is no answer to it (G1: R2-6, a finger on CALL OFF as THEY SAID NO THANKS came); a step a
           tap brought up (the modal opened, ASKING after GIVE or a row) also spends its double's second half (W). No restart */
        const double press = tap_press(t);
        if (g_gm.step_t > t->clock) g_gm.step_t = -1e9;   /* (a clock gone back: no stamp to keep) */
        if (press <= g_gm.step_t || (g_gm.by_tap && press - g_gm.step_t < TAP_W_S)) return RENDER_GM_TAP_HELD;   /* (<=: a press in the poll before the step was drawn) */
    }
    if (hit == RENDER_GM_TAP_OK) { g_gm.on = false; return hit; }
    if (hit == RENDER_GM_TAP_CALLOFF) {
        if (g) { g->cancel_uid = g_gm.uid; if (g->want_uid == g_gm.uid) g->want_uid = 0; }
        g_gm.on = false; return hit;
    }
    int e = hit == RENDER_GM_TAP_GIVE ? g_gm.edge : hit == RENDER_GM_TAP_RIGHT ? LINK_RIGHT : LINK_LEFT;   /* GIVE, or a tank picked: the yes */
    if (!g || e < 0) return RENDER_GM_TAP_CARD;
    g->want_uid = g_gm.uid; g->want_door = (uint8_t)(GIFT_DOOR_ANY + 1 + e); g->want_peer = g_gm.peer[e]; g->said_uid = 0;
    g_gm.edge = (int8_t)e; g_gm.asked = t->clock; g_gm.step = RENDER_GM_ASKING; g_gm.step_t = t->clock; g_gm.by_tap = true;
    return hit;
}

/* the TANK tally's picture (2026-10-02, Strato: the modal "feels bare"): the
 * school itself across the top of the box - every fish in its own colours,
 * at its own size and stage, in the order they arrived - then a dim
 * silhouette for each place still open. The count is in the words under it;
 * here it is seen (lit against dim, never by hue alone). */
#define MSP_SCHOOL_PITCH 52
#define MSP_SCHOOL_SCALE 0.74f
#define MSP_SCHOOL_MAX   1.02f            /* the size that still fits its place (a big elder) */
static void ms_school(const tank_t *t, uint16_t *fb, int stride, int X, int Y, int W) {
    for (int i = 0; i < POP_CAP; i++) {
        float cx = X + (W - POP_CAP * MSP_SCHOOL_PITCH) / 2 + MSP_SCHOOL_PITCH / 2 + 4 + i * MSP_SCHOOL_PITCH;   /* (+4: a fish's centre sits ahead of its middle) */
        if (i < t->n_fish) {
            float sz = t->fish[i].size * MSP_SCHOOL_SCALE;
            render_fish_portrait(fb, stride, cx, Y + 50, sz > MSP_SCHOOL_MAX ? MSP_SCHOOL_MAX : sz, &t->fish[i], FISH_ROOM_ROW, t->clock + i * 0.9f);
        } else render_fish_preview(fb, stride, cx, Y + 50, 0.9f * MSP_SCHOOL_SCALE, MSP_DIM, MSP_DIM, MSP_DIM, NULL, FISH_ROOM_ROW, t->clock + i * 0.9f);
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
static void badge(ctx_t *c, int x, int y, const icon_t *ic, bool on, bool fresh) {
    const int ix = x + (MSP_ICON - ic->w) / 2, iy = y + (MSP_ICON - ic->h) / 2;   /* an icon under 32 px sits centered in its cell (0.4.1: the
                                                                             24 px helix sat in the top-left corner) */
    if (on) blit_icon(c, ix, iy, ic, 255); else blit_icon_locked(c, ix, iy, ic);
    if (on && fresh) {
        rect_edge(c, x - 3, y - 3, MSP_ICON + 6, MSP_ICON + 6, MSP_TEAL);
        rect_edge(c, x - 4, y - 4, MSP_ICON + 8, MSP_ICON + 8, 0x3f6a72);
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
    if (ic) {
        int off = (MSP_ICON - ic->w) / 2;        /* the 24 px card icon sits centred */
        if (r->met) blit_icon(c, x + off, y + off, ic, 255); else blit_icon_locked(c, x + off, y + off, ic);
    } else {                                     /* GROW: the youngest fish, its own colours once grown */
        const fish_t *f = &t->fish[progression_youngest(t)];
        float sz = f->size > 0.7f ? 0.7f : f->size;
        if (r->met) render_fish_preview(fb, stride, x + MSP_ICON / 2, y + MSP_ICON / 2, sz, f->color, f->fin, f->accent, f, FISH_ROOM_ROW, t->clock);
        else        render_fish_preview(fb, stride, x + MSP_ICON / 2, y + MSP_ICON / 2, sz, MSP_DIM, MSP_DIM, MSP_DIM, f, FISH_ROOM_ROW, t->clock);
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
/* a fish card's second line (0.4): its form - what it is, or will grow into */
static void ms_genes_line(const fish_t *f) {
    snprintf(g_ms_caption2, sizeof g_ms_caption2, "%s  %s  %s", lang_tr(SHAPE_NAMES[f->shape % SHAPE_N]), lang_tr(FINS_NAMES[f->fins % FINS_N]), lang_tr(PATTERN_NAMES[f->pattern % PATTERN_N]));
}
/* the modal's box: a gate's and a fish's card are taller (a button row at the
   foot) and stand higher, clear of the CLOSE button */
static bool ms_card(const tank_t *t) { return g_ms_caption[0] && !g_ms_tip && !g_ms_fryrow && !g_ms_tankrow && g_ms_k < 0 && g_ms_row >= 0 && g_ms_row < t->n_fish; }
static int  ms_modal_y(const tank_t *t) { return g_ms_kind >= 0 || ms_card(t) ? MSP_FRY_MODAL_Y : MSP_MODAL_Y; }
static int  ms_modal_h(const tank_t *t) {
    return MSP_MODAL_H + (g_ms_caption2[0] || ms_card(t) ? 20 : 0)   /* a card always has its genes line (0.4): its buttons
                                                                        never move, even before the first draw fills it */
           + (g_ms_sub[0] || ms_card(t) ? 24 : 0) + (g_ms_kind >= 0 || ms_card(t) ? MSP_HOW_H + 14 : 0);
}
static int  ms_card_btn_y(const tank_t *t) { return ms_modal_y(t) + ms_modal_h(t) - 10 - MSP_HOW_H; }
static int  ms_card_btn_x(int which) {            /* 0 = RENAME, 1 = SELL: the pair centred */
    return MSP_MODAL_X + (MSP_MODAL_W - 2 * MSP_CARD_BTN_W - MSP_CARD_BTN_GAP) / 2 + which * (MSP_CARD_BTN_W + MSP_CARD_BTN_GAP);
}
#define MSP_CARD_BTN3_W   100                     /* linked: RENAME | GIVE | SELL (0.4) */
#define MSP_CARD_BTN3_GAP 8
static int  ms_card_btn3_x(int which) {
    return MSP_MODAL_X + (MSP_MODAL_W - 3 * MSP_CARD_BTN3_W - 2 * MSP_CARD_BTN3_GAP) / 2 + which * (MSP_CARD_BTN3_W + MSP_CARD_BTN3_GAP);
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

/* ---- THE ICON FOOT (0.4; Strato 2026-11-01, approved from the sim's sketches) ----
 * The keeper page's foot as square icon buttons, a little taller than the text buttons and easier to hit: UPGRADES
 * (the sand dollar - its balance in the shop since footer icons v4), SETTINGS (a cog; the bowl's in the cap), MY TANKS
 * (two tanks on a bench - the switcher's way in, off the settings page), VISITORS (a passport: the visitor collection),
 * CLOSE (an X) - sprites since 11-01 (assets/icons/foot_*.png from working-assets/footer-icons-v4), spread evenly between the old foot's outer corners, so on curved glass the corners stand where the text
 * foot's stood (render.h MSP_FOOT_*). Tapping goes to the nearest square along the strip (render_milestones_tap). */
#define FS_SQ MSP_FOOT_SQ
static void fs_button(ctx_t *c, int x, int y) { button(c, x, y, FS_SQ, FS_SQ, 0x1c2f36, MSP_TEAL, "", 2); }
void render_ms_foot_box(int which, int *x, int *y) {
    if (PAGE_BOWL && which == MSF_SET) { *x = (PAGE_W - FS_SQ) / 2; *y = MSP_SET_Y - 6; return; }   /* (the bowl's cog: in the cap) */
    const int n = PAGE_BOWL ? MSF_N - 1 : MSF_N, i = PAGE_BOWL && which > MSF_SET ? which - 1 : which;
    const int x0 = MSP_FOOT_UPG_X, x1 = MSP_FOOT_CLOSE_X + MSP_CLOSE_W - FS_SQ;
    *x = x0 + (x1 - x0) * i / (n - 1); *y = MSP_FOOT_TOP;
}
static void ms_foot(const tank_t *t, ctx_t *c) {
    for (int b = 0; b < MSF_N; b++) {
        int x, y; render_ms_foot_box(b, &x, &y);
        fs_button(c, x, y);
        /* THE FOOT'S ICONS (0.4, 11-01: Strato's footer-icons-v4, working-assets/): 36 px sprites from 4 px in - the cog, two tanks on a
           bench, the PASSPORT (the visitor collection), a thick X - and OUR sand dollar (the shop's, 34 px) centered; no balance on the
           keeper page any more (the shop shows it) */
        const icon_t *ic = b == MSF_UPG ? &icon_foot_sand_dollar : b == MSF_SET ? &icon_foot_settings : b == MSF_TANKS ? &icon_foot_my_tanks
                         : b == MSF_VIS ? &icon_foot_passport : &icon_foot_close;
        blit_icon(c, x + (FS_SQ - ic->w) / 2, y + (FS_SQ - ic->h) / 2, ic, 255);
    }
}
/* ---- THE PAGE KEPT (0.4; Strato 11-01: "with four or five fish with animated tails the frame rate is pretty slow") ----
 * On the glass the keeper page drew at 8-10 fps on watch B, the square and the pendant (render 80-107 ms a frame, the
 * flush 13): every frame the whole page again - the ink, the names, ~1000 blended pixels a badge, the foot's icons (drawn
 * then with trig, 16 samples a pixel; sprites since) - while only the fish move. A framebuffer that holds this page from the last
 * time it was drawn into, with nothing drawn into it since and nothing still on the page changed (ms_key), now keeps it:
 * each moving thing's box (a row's portrait, the NEW FRY row's fry, the GROW gate's fish) goes back to the ink and the
 * moving things are drawn again, in the page's own order - nothing else. Pixel for pixel the page drawn whole:
 *  - a moving thing must stay inside its ZONE, where nothing still is drawn (the portraits' column left of the names and
 *    above the TANK row's line; the GROW gate's cell between its bar and the row above's rings) - a box outside it and
 *    the page is drawn whole that frame and the buffer is not kept;
 *  - "nothing drawn since": the page drawn in every frame since (render_frame_begin, once a frame - a caller that never
 *    calls it always gets the whole page), and no other draw target made since (CTX_MADE: an overlay over the page, the
 *    tank, any other page breaks it);
 *  - a modal up: the boxes grow to hold every place the thing has been (their pixels outside the box are the halved ink),
 *    are halved after the drawing, and the modal is drawn whole over them as before.
 * --selftest-keeper draws it against the whole page every frame. */
#define MS_ANIM_MAX (N_FISH_MAX + 2)
typedef struct { short x0, y0, x1, y1; } ms_box_t;           /* page coordinates, inclusive; x0 > x1: nothing drawn */
typedef struct { const uint16_t *fb; int stride; uint32_t key; unsigned frame, other; bool ok; int n; ms_box_t b[MS_ANIM_MAX]; } ms_kept_t;
static ms_kept_t g_ms_kept[2];
static unsigned g_rframe, g_ms_frame, g_ms_run0;            /* the frame count, the last frame the page was drawn in, the first of the run */
static int g_ms_kept_n, g_ms_whole_n, g_ms_why[MS_WHY_N];    /* frames kept, drawn whole, and why whole (render.h MS_WHY_*) */
static bool g_ms_whole_dbg;                                   /* the selftest's reference: drawn whole, nothing kept or counted */
void render_frame_begin(void) { g_rframe++; }
void render_milestones_counts(int *kept, int *whole, int why[MS_WHY_N]) { *kept = g_ms_kept_n; *whole = g_ms_whole_n; if (why) memcpy(why, g_ms_why, sizeof g_ms_why); }
void render_debug_page_whole(bool on) { g_ms_whole_dbg = on; }
static uint32_t ms_hash(uint32_t h, uint32_t v) { for (int b = 0; b < 4; b++) { h ^= (v >> (b * 8)) & 255; h *= 16777619u; } return h; }
/* everything the still page is drawn from (a new one: the page drawn whole) */
static uint32_t ms_key(const tank_t *t, const fry_req_t *req, int nreq, bool staged) {
    uint32_t h = ms_hash(2166136261u, (uint32_t)t->n_fish);
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i];
        for (int k = 0; k < (int)sizeof f->name && f->name[k]; k++) h = ms_hash(h, (uint8_t)f->name[k]);
        h = ms_hash(h, f->stage); h = ms_hash(h, f->accent); h = ms_hash(h, f->ms_bits); h = ms_hash(h, f->ms_seen);
    }
    h = ms_hash(h, (uint32_t)nreq); h = ms_hash(h, staged);
    for (int k = 0; k < nreq; k++) { h = ms_hash(h, (uint32_t)req[k].kind); h = ms_hash(h, req[k].met); h = ms_hash(h, (uint32_t)(int)(MSP_ICON * req[k].frac + 0.5f)); }
    h = ms_hash(h, t->sd_unlocks); h = ms_hash(h, t->tank_ms_bits); h = ms_hash(h, t->tank_ms_seen); h = ms_hash(h, (uint32_t)g_ms_tpage);
    h = ms_hash(h, g_ms_caption[0] != 0); h = ms_hash(h, g_gm.on); h = ms_hash(h, render_gift_up(t));
    if (g_ms_caption[0]) { h = ms_hash(h, g_ms_tip); h = ms_hash(h, (uint32_t)ms_modal_y(t)); h = ms_hash(h, (uint32_t)ms_modal_h(t)); }
    return h;
}
/* the NEW FRY row's label: the GROW gate's fish can reach over its last letter, so the kept page puts it back first
   inside that fish's box (`clip`; NULL: whole) */
static void ms_fry_label(ctx_t *c, int top, const ms_box_t *clip) {
    ctx_t cw = *c;
    if (clip) {
        int x0 = clip->x0, y0 = clip->y0, x1 = clip->x1, y1 = clip->y1;
        if (x0 < c->ox) x0 = c->ox;
        if (y0 < c->oy) y0 = c->oy;
        if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
        if (y1 > c->oy + c->h - 1) y1 = c->oy + c->h - 1;
        if (x0 > x1 || y0 > y1) return;
        cw.fb = &CTX_PX(c, x0, y0); cw.ox = x0; cw.oy = y0; cw.w = x1 - x0 + 1; cw.h = y1 - y0 + 1;
    }
    draw_text(&cw, 92, top + 2, 2, MSP_TEAL, "NEW FRY");
}
/* the moving things, in the page's order: each fish's portrait, the NEW FRY row's fry, the GROW gate's fish. Draws
   thing k (false: there is no thing k) and gives its zone (where nothing still is drawn) */
static bool ms_anim(const tank_t *t, uint16_t *fb, int stride, int k, const fry_req_t *req, int nreq, bool staged, ms_box_t *zone) {
    zone->x0 = (short)-PAGE_X; zone->y0 = (short)-PAGE_Y; zone->x1 = 88; zone->y1 = (short)(MSP_TANK_Y - 6);   /* left of the names (x 92) and
                                                                                                  the strips (~91), above the TANK row's line */
    if (k < t->n_fish) {
        const fish_t *f = &t->fish[k];
        fish_as_is(fb, stride, MSP_FISH_X, MSP_ROW_Y0 + k * MSP_ROW_H + MSP_ROW_MID, f->size, f, f->color, f->fin, f->accent, FISH_ROOM_ROW, true, t->clock);
        return true;
    }
    if (nreq <= 0) return false;
    int top = MSP_ROW_Y0 + t->n_fish * MSP_ROW_H;
    if (k == t->n_fish) {
        uint32_t fry_rgb = staged ? MSP_TEAL : MSP_DIM;
        render_fish_preview(fb, stride, MSP_FISH_X, top + MSP_ROW_MID, 0.55f, fry_rgb, fry_rgb, fry_rgb, NULL, FISH_ROOM_ROW, t->clock);
        return true;
    }
    for (int g = 0; g < nreq; g++)
        if (k == t->n_fish + 1 && !fry_req_icon(req[g].kind)) {
            int x = MSP_BADGE_X0 + g * MSP_BADGE_DX;
            zone->x0 = (short)(x - 8); zone->x1 = (short)(x + MSP_ICON);              /* the cell and its tail's reach left (the ticks end by x 162;
                                                                                         the NEW FRY label can reach in: ms_fry_label) */
            zone->y0 = (short)(top + MSP_ROW_BADGE + 3); zone->y1 = (short)(top + MSP_ROW_BAR - 1);   /* below the row above's rings, above the bar */
            const fish_t *f = &t->fish[progression_youngest(t)];    /* (fry_badge's GROW: the youngest - P15) */
            float sz = f->size > 0.7f ? 0.7f : f->size;
            if (req[g].met) render_fish_preview(fb, stride, x + MSP_ICON / 2, top + MSP_ROW_BADGE + MSP_ICON / 2, sz, f->color, f->fin, f->accent, f, FISH_ROOM_ROW, t->clock);
            else            render_fish_preview(fb, stride, x + MSP_ICON / 2, top + MSP_ROW_BADGE + MSP_ICON / 2, sz, MSP_DIM, MSP_DIM, MSP_DIM, f, FISH_ROOM_ROW, t->clock);
            return true;
        }
    return false;
}
static void ms_bb_begin(void) { g_bb_on = true; g_bb_x0 = g_bb_y0 = 1 << 20; g_bb_x1 = g_bb_y1 = -1; }
static ms_box_t ms_bb_end(void) { g_bb_on = false; ms_box_t b = { (short)g_bb_x0, (short)g_bb_y0, (short)g_bb_x1, (short)g_bb_y1 }; if (g_bb_x1 < 0) b.x0 = 1, b.x1 = 0; return b; }
static bool ms_box_in(ms_box_t b, ms_box_t z) { return b.x0 > b.x1 || (b.x0 >= z.x0 && b.x1 <= z.x1 && b.y0 >= z.y0 && b.y1 <= z.y1); }
static ms_box_t ms_box_or(ms_box_t a, ms_box_t b) {
    if (a.x0 > a.x1) return b;
    if (b.x0 > b.x1) return a;
    ms_box_t r = { a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1 };
    return r;
}
/* the boxes' pixels (each once where they overlap): back to the ink, or halved */
static void ms_boxes_do(uint16_t *fb, int stride, const ms_box_t *b, int n, bool halve) {
    int y0 = 1 << 20, y1 = -1;
    for (int i = 0; i < n; i++) if (b[i].x0 <= b[i].x1) { if (b[i].y0 < y0) y0 = b[i].y0; if (b[i].y1 > y1) y1 = b[i].y1; }
    const uint16_t ink = FB_SWAP16(rgb565(MSP_INK, 1));
    for (int y = y0; y <= y1; y++) {
        int fy = y + PAGE_Y;
        if ((unsigned)fy >= (unsigned)TANK_H) continue;
        int xs[MS_ANIM_MAX][2], m = 0;                  /* this row's runs, sorted and merged */
        for (int i = 0; i < n; i++) {
            if (b[i].x0 > b[i].x1 || y < b[i].y0 || y > b[i].y1) continue;
            int a = b[i].x0 + PAGE_X, e = b[i].x1 + PAGE_X, j = m++;
            if (a < 0) a = 0;
            if (e > TANK_W - 1) e = TANK_W - 1;
            while (j > 0 && xs[j - 1][0] > a) { xs[j][0] = xs[j - 1][0]; xs[j][1] = xs[j - 1][1]; j--; }
            xs[j][0] = a; xs[j][1] = e;
        }
        for (int r = 0; r < m; r++) {
            int a = xs[r][0], e = xs[r][1];
            while (r + 1 < m && xs[r + 1][0] <= e + 1) { if (xs[r + 1][1] > e) e = xs[r + 1][1]; r++; }
            uint16_t *p = fb + fy * stride;
            if (halve) for (int x = a; x <= e; x++) { uint16_t v = FB_SWAP16(p[x]); p[x] = FB_SWAP16((uint16_t)((v >> 1) & 0x7bef)); }
            else for (int x = a; x <= e; x++) p[x] = ink;
        }
    }
}
/* the kept page: false = not this buffer, or a box left its zone (the page is drawn whole) */
static bool ms_kept_draw(const tank_t *t, uint16_t *fb, int stride, ms_kept_t *k, const fry_req_t *req, int nreq, bool staged) {
    const bool modal = g_ms_caption[0] != 0;
    ms_boxes_do(fb, stride, k->b, k->n, false);
    if (k->n == t->n_fish + 2) { ctx_t c = ctx_page(fb, stride); ms_fry_label(&c, MSP_ROW_Y0 + t->n_fish * MSP_ROW_H, &k->b[t->n_fish + 1]); }   /* (GROW's fish is in) */
    ms_box_t nb[MS_ANIM_MAX], z;
    for (int i = 0; i < k->n; i++) {
        ms_bb_begin();
        ms_anim(t, fb, stride, i, req, nreq, staged, &z);
        nb[i] = ms_bb_end();
        if (!ms_box_in(nb[i], z) || (modal && !ms_box_in(nb[i], k->b[i]))) return false;   /* (under a modal the pixels round a box are halved ink) */
    }
    if (modal) ms_boxes_do(fb, stride, k->b, k->n, true);
    else memcpy(k->b, nb, sizeof nb[0] * (size_t)k->n);
    return true;
}
void render_milestones(const tank_t *t, uint16_t *fb, int stride) {
    if (g_ms_given_uid && !(g_ms_row >= 0 && g_ms_row < t->n_fish && t->fish[g_ms_row].uid == g_ms_given_uid)) ms_close();   /* a GIVEN fish has left:
                                                                                  its card goes with it (the rows have shifted) */
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    if (g_ms_tpage >= tank_pages(t)) g_ms_tpage = 0;
    const uint32_t key = ms_key(t, req, nreq, staged);
    ms_kept_t dbg = { 0 };
    if (!g_ms_whole_dbg) {
        if (g_ms_frame + 1 != g_rframe) g_ms_run0 = g_rframe;   /* a frame without the page: a new run */
        g_ms_frame = g_rframe;
    }
    ms_kept_t *kp = g_ms_whole_dbg ? &dbg : g_ms_kept[0].fb == fb ? &g_ms_kept[0] : g_ms_kept[1].fb == fb ? &g_ms_kept[1]
                  : g_ms_kept[0].frame <= g_ms_kept[1].frame ? &g_ms_kept[0] : &g_ms_kept[1];
    const bool keep = !g_ms_whole_dbg && kp->fb == fb && kp->ok && kp->stride == stride && kp->key == key && kp->other == g_ctx_other
                      && kp->frame < g_rframe && kp->frame >= g_ms_run0;
    g_ms_in = true;
    ctx_t c = ctx_page(fb, stride);
    if (keep && ms_kept_draw(t, fb, stride, kp, req, nreq, staged)) g_ms_kept_n++;
    else {
        if (!g_ms_whole_dbg) {
            g_ms_whole_n++;
            g_ms_why[keep ? MS_WHY_OUT : kp->fb != fb || kp->stride != stride ? MS_WHY_NEW : !kp->ok ? MS_WHY_ZONE : kp->key != key ? MS_WHY_CHANGED
                     : kp->other != g_ctx_other ? MS_WHY_OVER : MS_WHY_GAP]++;
        }
        ms_box_t ab[MS_ANIM_MAX]; int an = 0; bool in = true;
        const bool grow = kp->fb == fb && kp->ok && kp->key == key && g_ms_caption[0];   /* (under a modal: the boxes only grow while nothing changes) */
        for (int y = 0; y < TANK_H; y++)
            for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = FB_SWAP16(rgb565(MSP_INK, 1));
#define MS_ANIM(i) do { ms_box_t _z; ms_bb_begin(); ms_anim(t, fb, stride, (i), req, nreq, staged, &_z); ab[i] = ms_bb_end(); \
                        in = in && ms_box_in(ab[i], _z); if (grow && (i) < kp->n) ab[i] = ms_box_or(ab[i], kp->b[i]); an = (i) + 1; } while (0)
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i];
        int top = MSP_ROW_Y0 + i * MSP_ROW_H;
        MS_ANIM(i);                                  /* the portrait */
        draw_words(&c, 92, top + 2, 2, 0xffffff, f->name);
        static const int GX[4] = { 96, 111, 129, 151 };   /* each glyph's own pitch: a 4 px gap as they grow */
        /* (0.4.1, abumaia on Discord: a fish with dark markings lit its stages almost as dark as the ones still to come - a
           stage it has reached wears a white outline then: not color alone) */
        const uint32_t ac = f->accent;
        const bool dark = ((ac >> 16 & 255) * 2 + (ac >> 8 & 255) * 5 + (ac & 255)) / 8 < 80;
        for (int s = 0; s < 4; s++) {        /* growth strip: fry -> elder, lit up to the stage reached */
            const float gr = 2.2f + s * 0.9f; const int gy = top + MSP_ROW_STRIP;
            if (dark && s <= (int)f->stage) {
                fish_glyph(&c, GX[s] - 1, gy, gr, 0xffffff); fish_glyph(&c, GX[s] + 1, gy, gr, 0xffffff);
                fish_glyph(&c, GX[s], gy - 1, gr, 0xffffff); fish_glyph(&c, GX[s], gy + 1, gr, 0xffffff);
            }
            fish_glyph(&c, GX[s], gy, gr, s <= (int)f->stage ? ac : MSP_DIM);
        }
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
    if (nreq > 0) {
        int top = MSP_ROW_Y0 + t->n_fish * MSP_ROW_H;
        MS_ANIM(t->n_fish);                          /* the fry-to-be */
        ms_fry_label(&c, top, NULL);
        for (int k = 0; k < nreq; k++)           /* one tick per gate, lit when met */
            rect_fill(&c, 96 + k * 14, top + MSP_ROW_STRIP - 3, 10, 6, req[k].met ? MSP_TEAL : MSP_DIM);
        for (int k = 0; k < nreq; k++) {
            int x = MSP_BADGE_X0 + k * MSP_BADGE_DX;
            if (fry_req_icon(req[k].kind)) fry_badge(&c, fb, stride, t, x, top + MSP_ROW_BADGE, &req[k]);
            else MS_ANIM(t->n_fish + 1);         /* GROW: the youngest fish (fry_badge's, as a moving thing) */
            if (!req[k].met) {                   /* the bar: how far along */
                int w = (int)(MSP_ICON * req[k].frac + 0.5f);
                rect_fill(&c, x, top + MSP_ROW_BAR, MSP_ICON, 2, MSP_DIM);
                if (w > 0) rect_fill(&c, x, top + MSP_ROW_BAR, w, 2, MSP_TEAL);
            }
        }
    }
    /* the tank's row (its sand dollar and balance went to the foot's UPGRADES, 0.4) */
    for (int x = 24; x < PAGE_W - 24; x++) px_blend(&c, x, MSP_TANK_Y - 4, MSP_DIM, 200);
    draw_text(&c, MSP_TNAME_X, MSP_TANK_Y + 2, 2, MSP_TEAL, "TANK");
    for (int k = 0; k < POP_CAP; k++)        /* population strip: who is here, who could still arrive */
        fish_glyph(&c, MSP_TNAME_X + 4 + k * 14, MSP_TANK_Y + 30, 2.8f, k < t->n_fish ? MSP_TEAL : MSP_DIM);
    int nb = tank_badge_n(t), np = tank_pages(t);
    if (g_ms_tpage >= np) g_ms_tpage = 0;
    for (int j = 0; j < MSP_TANK_PER_ROW; j++) {
        int k = g_ms_tpage * MSP_TANK_PER_ROW + j;
        if (k >= nb) break;
        const badge_t *tb = tank_badge(t, k);
        g_ms_tshown |= tb->bit;
        badge(&c, MSP_TBADGE_X0 + j * MSP_BADGE_DX, MSP_TANK_Y + 4, tb->icon,
              (t->tank_ms_bits & tb->bit) != 0, (t->tank_ms_seen & tb->bit) == 0);
    }
    if (np > 1) {   /* the page arrow: toward the next page, back from the last; the badges' two-tone
                       ring round it while another page holds a badge not seen yet */
        bool last = g_ms_tpage == np - 1, fresh = false;
        for (int k = 0; k < nb; k++)
            if (k / MSP_TANK_PER_ROW != g_ms_tpage && (t->tank_ms_bits & ~t->tank_ms_seen & tank_badge(t, k)->bit)) fresh = true;
        int ax = MSP_TPG_X + 4, ay = MSP_TANK_Y + 4, aw = 16;
        ms_chevron(&c, last ? ax + 2 : ax + aw - 2, ay + MSP_ICON / 2, last, 0xffffff);
        if (fresh) { rect_edge(&c, ax - 3, ay - 3, aw + 6, MSP_ICON + 6, MSP_TEAL);
                     rect_edge(&c, ax - 4, ay - 4, aw + 8, MSP_ICON + 8, 0x3f6a72); }
        for (int p = 0; p < np; p++)        /* a pip per page under it, the current one lit */
            rect_fill(&c, ax + aw / 2 - np * 3 + p * 6 + 1, MSP_TANK_Y + 40, 4, 2, p == g_ms_tpage ? MSP_TEAL : MSP_DIM);
    }
    /* the way out: a CLOSE button in the prompt's calm dress (a tap anywhere
       else never drops the page - too much to tap for that) */
    ms_foot(t, &c);                                  /* the icon foot (0.4) */
    /* a modal up: the page under it is out of reach (any tap only closes the
       modal), so it LOOKS out of reach - every pixel at half (Strato: with
       CLOSE lit it looked like you could still tap it). One shift per
       pixel, so cheap enough for every frame. */
    if (g_ms_caption[0])
        for (int y = 0; y < TANK_H; y++)
            for (int x = 0; x < TANK_W; x++) { uint16_t n = FB_SWAP16(fb[y * stride + x]); fb[y * stride + x] = FB_SWAP16((uint16_t)((n >> 1) & 0x7bef)); }
#undef MS_ANIM
        kp->fb = fb; kp->stride = stride; kp->key = key; kp->ok = in; kp->n = an;     /* (the frame and the count: below, after the modal's own drawing) */
        memcpy(kp->b, ab, sizeof ab[0] * (size_t)an);
    }
    /* the detail modal, in the reset prompt's dress: the art at 2x, a title
       (the fish's name / TANK, or NOT YET), the milestone's words */
    if (g_ms_caption[0] && g_ms_tip) {
        /* the tip page: HOW, then the gate's lines; any tap closes it */
        const int X = MSP_MODAL_X, Y = MSP_FRY_MODAL_Y, W = MSP_MODAL_W;
        const char *const *tip = progression_fry_tip(g_ms_kind);
        int n = 0; while (tip[n]) n++;
        const int H = 56 + n * 20 + 16;
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        char title[24]; snprintf(title, sizeof title, lang_tr("%s: HOW"), g_ms_title);
        draw_words(&c, X + (W - words_w(title, 3)) / 2, Y + 16, 3, 0xffffff, title);
        for (int i = 0; i < n; i++) draw_text(&c, X + (W - text_w(tip[i], 2)) / 2, Y + 56 + i * 20, 2, MSP_TEAL, tip[i]);
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
            ms_genes_line(f);                    /* its form under its stage (0.4) */
            char gl[sizeof g_ms_sub];
            if (give_line(t, g_ms_row, gl, sizeof gl)) memcpy(g_ms_sub, gl, sizeof g_ms_sub);   /* GIVE has the line (0.4) */
            else if (g_ms_sell_armed) snprintf(g_ms_sub, sizeof g_ms_sub, "TAP AGAIN TO SELL");
            else if (can) snprintf(g_ms_sub, sizeof g_ms_sub, lang_tr("WORTH %d SAND DOLLARS"), worth);
            else if (g_ms_sell_asked) snprintf(g_ms_sub, sizeof g_ms_sub, f->away ? "VISITING NEXT DOOR" : !progression_keeps_two(t, g_ms_row) ? "KEEP AT LEAST TWO FISH" : "NAME THE NEW FRY FIRST");
            else g_ms_sub[0] = 0;                  /* (the line's room stays: the buttons never move) */
        }
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        if (g_ms_kind >= 0)                        /* the way further in: HOW?, centred at the foot (Strato: bottom
                                                      right sat too close to CLOSE for comfort) */
            button(&c, X + (W - MSP_HOW_W) / 2, Y + H - 10 - MSP_HOW_H, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "HOW?", 2);
        if (card && give_look(t, g_ms_row) == GV_LEAVING) {   /* accepted next door (Strato 11-01: RENAME still live "felt weird"): the
                                                                buttons give way to a GIVEN stamp in the passport's gold - nothing to tap
                                                                now - and the card closes by itself once the fish has gone */
            const int by = ms_card_btn_y(t), sw = text_w("GIVEN", 3) + 28, sh = MSP_HOW_H + 6, sx0 = X + (W - sw) / 2, sy0 = by - 3;
            rect_edge(&c, sx0, sy0, sw, sh, GIVE_GOLD); rect_edge(&c, sx0 + 3, sy0 + 3, sw - 6, sh - 6, GIVE_GOLD);
            draw_text(&c, sx0 + 14, sy0 + (sh - 21) / 2, 3, GIVE_GOLD, "GIVEN");
            g_ms_given_uid = t->fish[g_ms_row].uid;
        } else if (card && give_look(t, g_ms_row) != GV_HIDDEN) {   /* linked: RENAME | GIVE | SELL, narrower */
            const int by = ms_card_btn_y(t);
            if (give_look(t, g_ms_row) == GV_ASKING) {   /* asking next door: no new name while the other keeper decides (Strato 11-01) */
                rect_fill(&c, ms_card_btn3_x(0), by, MSP_CARD_BTN3_W, MSP_HOW_H, 0x04141a); rect_edge(&c, ms_card_btn3_x(0), by, MSP_CARD_BTN3_W, MSP_HOW_H, MSP_DIM);
                draw_text(&c, ms_card_btn3_x(0) + (MSP_CARD_BTN3_W - text_w("RENAME", 2)) / 2, by + (MSP_HOW_H - 14) / 2, 2, MSP_DIM, "RENAME");
            } else button(&c, ms_card_btn3_x(0), by, MSP_CARD_BTN3_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "RENAME", 2);
            give_button(&c, ms_card_btn3_x(1), by, MSP_CARD_BTN3_W, MSP_HOW_H, give_look(t, g_ms_row), 2);
            const int sx = ms_card_btn3_x(2);
            if (!progression_fish_sellable(t, g_ms_row)) {
                rect_fill(&c, sx, by, MSP_CARD_BTN3_W, MSP_HOW_H, 0x04141a); rect_edge(&c, sx, by, MSP_CARD_BTN3_W, MSP_HOW_H, MSP_DIM);
                draw_text(&c, sx + (MSP_CARD_BTN3_W - text_w("SELL", 2)) / 2, by + (MSP_HOW_H - 14) / 2, 2, MSP_DIM, "SELL");
            } else if (g_ms_sell_armed) {
                char ok[16]; snprintf(ok, sizeof ok, "+%d OK?", progression_fish_value(t, g_ms_row));
                button(&c, sx, by, MSP_CARD_BTN3_W, MSP_HOW_H, 0x155e58, 0x38dcc7, ok, 2);
            } else button(&c, sx, by, MSP_CARD_BTN3_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "SELL", 2);
        } else if (card) {
            const int by = ms_card_btn_y(t);
            button(&c, ms_card_btn_x(0), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "RENAME", 2);
            if (!progression_fish_sellable(t, g_ms_row)) {           /* not for sale: the button in the page's dim ink, the words too */
                rect_fill(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x04141a);
                rect_edge(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, MSP_DIM);
                draw_text(&c, ms_card_btn_x(1) + (MSP_CARD_BTN_W - text_w("SELL", 2)) / 2, by + (MSP_HOW_H - 14) / 2, 2, MSP_QUIET, "SELL");
            } else if (g_ms_sell_armed) {
                char ok[16]; snprintf(ok, sizeof ok, "+%d OK?", progression_fish_value(t, g_ms_row));
                button(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x155e58, 0x38dcc7, ok, 2);
            } else button(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "SELL", 2);
        }
        if (g_ms_icon) blit_icon_scaled(&c, X + (W - g_ms_icon->w * 2) / 2, Y + 16 + (32 - g_ms_icon->w), g_ms_icon, 2, g_ms_lit);
        else if (g_ms_fish >= 0 && g_ms_fish < t->n_fish) {
            const fish_t *f = &t->fish[g_ms_fish];
            if (g_ms_lit) fish_as_is(fb, stride, X + W / 2, Y + 48, f->size * 1.6f, f, f->color, f->fin, f->accent, FISH_ROOM_CARD, false, t->clock);
            else          fish_as_is(fb, stride, X + W / 2, Y + 48, f->size * 1.6f, f, MSP_DIM, MSP_DIM, MSP_DIM, FISH_ROOM_CARD, false, t->clock);
        } else if (g_ms_fry)
            render_fish_preview(fb, stride, X + W / 2, Y + 48, 0.9f, g_ms_lit ? MSP_TEAL : MSP_DIM, g_ms_lit ? MSP_TEAL : MSP_DIM, g_ms_lit ? MSP_TEAL : MSP_DIM, NULL, FISH_ROOM_CARD, t->clock);
        else if (g_ms_tankrow && g_ms_k < 0) ms_school(t, fb, stride, X, Y, W);   /* TANK's tally: the school */
        draw_words(&c, X + (W - words_w(g_ms_title, 3)) / 2, Y + 92, 3, g_ms_lit ? 0xffffff : MSP_TEAL, g_ms_title);   /* holds a name or already translated words */
        draw_text(&c, X + (W - text_w(g_ms_caption, 2)) / 2, Y + 124, 2, g_ms_lit ? MSP_TEAL : 0x5f8a92, g_ms_caption);
        int ly = Y + 144;
        if (g_ms_caption2[0]) { draw_words(&c, X + (W - words_w(g_ms_caption2, 2)) / 2, ly, 2, g_ms_lit ? MSP_TEAL : 0x5f8a92, g_ms_caption2); ly += 20; }
        if (g_ms_sub[0]) draw_text(&c, X + (W - text_w(g_ms_sub, 2)) / 2, ly + 4, 2, g_ms_lit ? 0xffffff : MSP_TEAL, g_ms_sub);
        {   /* the arrows (2026-09-16): the previous / next of the group at the
               top corners, in the buttons' dress, only when there is a group */
            int idx, n = ms_group(t, nreq, &idx);
            if (n > 1) {
                int ax = X + MSP_ARROW_IN, ay = Y + MSP_ARROW_IN, bx = X + W - MSP_ARROW_IN - MSP_ARROW_W;
                rect_fill(&c, ax, ay, MSP_ARROW_W, MSP_ARROW_H, 0x1c2f36);
                rect_edge(&c, ax, ay, MSP_ARROW_W, MSP_ARROW_H, MSP_TEAL); rect_edge(&c, ax + 1, ay + 1, MSP_ARROW_W - 2, MSP_ARROW_H - 2, MSP_TEAL);
                ms_chevron(&c, ax + 14, ay + MSP_ARROW_H / 2, true, 0xffffff);
                rect_fill(&c, bx, ay, MSP_ARROW_W, MSP_ARROW_H, 0x1c2f36);
                rect_edge(&c, bx, ay, MSP_ARROW_W, MSP_ARROW_H, MSP_TEAL); rect_edge(&c, bx + 1, ay + 1, MSP_ARROW_W - 2, MSP_ARROW_H - 2, MSP_TEAL);
                ms_chevron(&c, bx + MSP_ARROW_W - 14, ay + MSP_ARROW_H / 2, false, 0xffffff);
            }
        }
    }
    if (g_gm.on) gm_draw(t, fb, stride);             /* the GIVE modal over the page, whole every frame (ms_key: up or not) */
    if (render_gift_up(t)) render_gift(t, fb, stride);   /* a gift offered to us over both (the 11-02 sweep; ms_key: up or not) */
    g_ms_in = false;
    kp->frame = g_rframe; kp->other = g_ctx_other;   /* what this buffer holds now (anything drawn after: CTX_MADE) */
}

int render_milestones_tap(const tank_t *t, float x, float y) {
    if (gm_eval(t) != RENDER_GM_OFF) { render_give_modal_tap(t, x, y); return MS_TAP_KEPT; }   /* the GIVE modal over the page has every tap */
    x -= PAGE_X; y -= PAGE_Y;                    /* the page's own coordinates */
    if (g_ms_caption[0]) {                       /* a modal is up */
        if (!g_ms_tip) {                         /* the arrows at its top corners: the previous / next of the group */
            fry_req_t req[FRY_REQ_MAX]; bool staged; int idx;
            int n = ms_group(t, progression_next_fry(t, req, &staged), &idx);
            const int Y = ms_modal_y(t);
            if (n > 1 && y >= Y - 12 && y < Y + MSP_ARROW_IN + MSP_ARROW_H + 24) {
                if (x < MSP_MODAL_X + MSP_ARROW_HIT)               { ms_step(t, -1); return MS_TAP_KEPT; }
                if (x >= MSP_MODAL_X + MSP_MODAL_W - MSP_ARROW_HIT) { ms_step(t, +1); return MS_TAP_KEPT; }
            }
        }
        if (ms_card(t)) {                        /* a fish's card: RENAME / SELL across its foot - each button's half of
                                                    the panel, the HOW? button's depth (a miss costs the modal) */
            const int by = ms_card_btn_y(t), fish = g_ms_row;
            if (x >= MSP_MODAL_X && x < MSP_MODAL_X + MSP_MODAL_W && y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN) {
                if (give_look(t, fish) == GV_LEAVING) return MS_TAP_KEPT;   /* GIVEN: the stamp is no button */
                if (give_look(t, fish) != GV_HIDDEN) {   /* linked: three buttons, each its third of the panel */
                    if (x < ms_card_btn3_x(1) - MSP_CARD_BTN3_GAP / 2) {
                        if (give_look(t, fish) == GV_ASKING) return MS_TAP_KEPT;   /* (RENAME rests while the gift is asked) */
                        ms_close(); return MS_TAP_RENAME + fish; }
                    if (x < ms_card_btn3_x(2) - MSP_CARD_BTN3_GAP / 2) { give_tap(t, fish); return MS_TAP_KEPT; }
                } else if (x < MSP_MODAL_X + MSP_MODAL_W / 2) { ms_close(); return MS_TAP_RENAME + fish; }   /* the platform opens the wheel */
                if (!progression_fish_sellable(t, fish)) { g_ms_sell_asked = true; return MS_TAP_KEPT; }   /* the line says why, from now on */
                if (!g_ms_sell_armed) { g_ms_sell_armed = true; g_ms_sell_clock = t->clock; return MS_TAP_KEPT; }
                if (hold_spent_at(&g_ms_sell_clock, tap_press(t), t->clock, MSP_SELL_WAIT_S)) return MS_TAP_KEPT;   /* a doubled tap (or a mash:
                                                                   arm, then tap on at 5 Hz sold at the 0.6 s mark): still armed (THE TOUCH RULE: no restart) */
                ms_close(); return MS_TAP_SELL + fish;                                               /* the platform sells it */
            }
        }
        if (g_ms_kind >= 0 && !g_ms_tip) {       /* a gate's: the HOW? button opens its tip page */
            const int H = ms_modal_h(t);
            const int bx = MSP_MODAL_X + (MSP_MODAL_W - MSP_HOW_W) / 2, by = MSP_FRY_MODAL_Y + H - 10 - MSP_HOW_H;
            if (x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X && y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN) {
                g_ms_tip = true; return MS_TAP_KEPT; }
        }
        ms_close(); return MS_TAP_KEPT;                  /* any other tap: back to the page */
    }
    if (PAGE_BOWL) {                                 /* the bowl: the cog in the cap above the rows */
        int sx, sy; render_ms_foot_box(MSF_SET, &sx, &sy);
        if (y < sy + FS_SQ + 8) return x >= sx - 24 && x < sx + FS_SQ + 24 ? MS_TAP_SETTINGS : MS_TAP_NONE;
    }
    if (y >= MSP_FOOT_HIT_Y) {                       /* the foot's strip down to the glass: the nearest square along it */
        static const int TAP[MSF_N] = { MS_TAP_SHOP, MS_TAP_SETTINGS, MS_TAP_TANKS, MS_TAP_VISITORS, MS_TAP_CLOSE };
        int best = -1; float bd = 1e9f;
        for (int b = 0; b < MSF_N; b++) {
            if (PAGE_BOWL && b == MSF_SET) continue;
            int bx, by; render_ms_foot_box(b, &bx, &by);
            float d = fabsf(x - (bx + FS_SQ / 2.0f));
            if (d < bd) { bd = d; best = b; }
        }
        return TAP[best];
    }
    int row = -1; bool tank_row = false, fry_row = false;
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    if (y >= MSP_ROW_Y0 - 2 && y < MSP_ROW_Y0 + N_FISH_MAX * MSP_ROW_H) {
        row = (int)((y - MSP_ROW_Y0) / MSP_ROW_H);
        if (row < 0) row = 0;
        if (row == t->n_fish && nreq > 0) fry_row = true;   /* the NEW FRY row */
        else if (row >= t->n_fish) return MS_TAP_NONE;      /* an empty row */
    } else if (y >= MSP_TANK_Y - 6 && y < MSP_FOOT_HIT_Y) tank_row = true;    /* down to the button strip: fingers near the
                                                                              bottom bezel report LOW */
    else return MS_TAP_NONE;
    if (tank_row && x >= MSP_TPG_X && tank_pages(t) > 1) {   /* the page arrow: the next page, wrapping */
        g_ms_tpage = (g_ms_tpage + 1) % tank_pages(t); return MS_TAP_KEPT; }
    int k;                                           /* badge column, or -1 for the name / strip cluster */
    const int bx0 = tank_row ? MSP_TBADGE_X0 : MSP_BADGE_X0, ncol = tank_row ? MSP_TANK_PER_ROW : 6;   /* (the TANK row: slid left, 7 a page - 0.4.1) */
    if (x >= bx0 - 4 && x < bx0 + ncol * MSP_BADGE_DX) {
        k = (int)((x - bx0 + 4) / MSP_BADGE_DX);
        if (k > ncol - 1) k = ncol - 1;
    } else if (x >= 20 && x < bx0 - 4) k = -1;
    else return MS_TAP_NONE;
    if (tank_row && k >= 0) {                        /* this page's badge; an empty cell on the last page is nothing */
        k += g_ms_tpage * MSP_TANK_PER_ROW;
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
            snprintf(g_ms_title, sizeof g_ms_title, "%s", lang_tr("NEW FRY"));
            if (staged) { snprintf(g_ms_caption, sizeof g_ms_caption, "EVERY STEP IS DONE. A FRY");   /* the spawning, 2026-09-24 */
                          snprintf(g_ms_caption2, sizeof g_ms_caption2, "%s", lang_tr("WILL BE BORN IN THE GRASS"));
                          snprintf(g_ms_sub, sizeof g_ms_sub, "ON ITS WAY"); }
            else { snprintf(g_ms_caption, sizeof g_ms_caption, "WHEN ALL NEEDS ARE MET, A");     /* Strato's words, 2026-09-14 */
                   snprintf(g_ms_caption2, sizeof g_ms_caption2, "%s", lang_tr("NEW FRY IS READY TO BE BORN"));
                   snprintf(g_ms_sub, sizeof g_ms_sub, lang_tr("%d OF %d DONE"), met, nreq); }
            g_ms_lit = staged; g_ms_icon = NULL; g_ms_fish = -1; g_ms_fry = true;
        } else if (k < nreq) {                       /* a gate: the words, and where it stands */
            const fry_req_t *r = &req[k];
            snprintf(g_ms_title, sizeof g_ms_title, "%s", lang_tr(r->title));
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", r->words);
            snprintf(g_ms_caption2, sizeof g_ms_caption2, "%s", lang_tr(r->words2));
            snprintf(g_ms_sub, sizeof g_ms_sub, "%s", r->progress);
            g_ms_lit = r->met; g_ms_icon = fry_req_icon(r->kind); g_ms_kind = r->kind;
            g_ms_fish = g_ms_icon ? -1 : progression_youngest(t);
        } else return MS_TAP_NONE;
    } else if (tank_row) {
        if (k < 0) {
            snprintf(g_ms_title, sizeof g_ms_title, "%s", lang_tr("TANK"));
            snprintf(g_ms_caption, sizeof g_ms_caption, lang_tr("%d OF %d FISH SO FAR"), t->n_fish, POP_CAP);
            g_ms_lit = true; g_ms_icon = NULL; g_ms_fish = -1;
        } else {
            uint32_t bit = tank_badge(t, k)->bit; bool on = (t->tank_ms_bits & bit) != 0;
            g_ms_tpage = k / MSP_TANK_PER_ROW;       /* the arrows can cross pages: the row follows */
            snprintf(g_ms_title, sizeof g_ms_title, "%s", lang_tr(on ? "TANK" : "NOT YET"));
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", TMS_NAMES[bit_index(bit)]);
            g_ms_lit = on; g_ms_icon = tank_badge(t, k)->icon; g_ms_fish = -1;
        }
    } else {
        const fish_t *f = &t->fish[row];
        if (k < 0) {
            snprintf(g_ms_title, sizeof g_ms_title, "%s", f->name);
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", STAGE_WORDS[f->stage & 3]);
            ms_genes_line(f);                    /* its form under its stage (0.4) */
            g_ms_lit = true; g_ms_icon = NULL; g_ms_fish = row;
        } else {
            uint32_t bit = FISH_BADGES[k].bit; bool on = (f->ms_bits & bit) != 0;
            snprintf(g_ms_title, sizeof g_ms_title, "%s", on ? f->name : lang_tr("NOT YET"));
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
    if (g_ms_caption[0] || tank_pages(t) < 2 || y < MSP_TANK_Y - 6 || y >= MSP_FOOT_HIT_Y) return false;
    int p = g_ms_tpage + (dx < 0 ? 1 : -1);
    if (p >= 0 && p < tank_pages(t)) g_ms_tpage = p;
    return true;
}
/* the modal down, the page (and the tank row's page) kept */
static void ms_close(void) { g_ms_given_uid = 0; g_ms_sell_armed = false; g_ms_sell_asked = false; g_ms_kind = -1; g_ms_tip = false; g_ms_caption[0] = 0; g_ms_caption2[0] = 0; g_ms_title[0] = 0; g_ms_sub[0] = 0; g_ms_icon = NULL; g_ms_fish = -1; g_ms_fry = false;
                                     g_ms_row = -1; g_ms_k = -1; g_ms_tankrow = false; g_ms_fryrow = false; }
void render_milestones_leave(void) { ms_close(); g_ms_tpage = 0; g_ms_tshown = 0; }

/* ---- the VISITOR COLLECTION (2026-10-06, Strato: "a grid of friends made and
   if you tap one it shows their passport and whatever additional info you
   think would be nice") ---- */
#define COLL_COLS 4
#define COLL_CW   92
#define COLL_CH   80
#define COLL_X0   ((PAGE_W - COLL_COLS * COLL_CW) / 2)
#define COLL_Y0   60
static int g_coll_sel = -1;
/* PAGES (0.4, Strato 11-03, approved from the mockup): 12 a page, up to 10. The arrows in the critter cards' dress at
   the title's ends (only with more than one page: a small collection's page is as it was), wrapping like NEXT FISH;
   the page in words and a pip per page in the foot - the page's pip a filled block, the others hollow (shape and
   words, never hue); it opens on page 1 */
static int g_coll_page;
#ifdef TANK_ROUND
#define COLL_NAV_X 76                  /* the bowl: the arrows' top corners 6 px inside the circle */
#define COLL_NAV_Y 14
#else
#define COLL_NAV_X 72
#define COLL_NAV_Y 12
#endif
#define COLL_PIP 10                    /* a pip's pitch: 6 px hollow, the page's 8 px filled */
void render_collection_leave(void) { g_coll_sel = -1; g_coll_page = 0; }
int  render_collection_page(void) { return g_coll_page; }
void render_collection_set_page(int page) { int np = collection_pages(); g_coll_page = page < 0 ? 0 : page >= np ? np - 1 : page; }
bool render_collection_arrow(bool right, int *x, int *y) {
    if (collection_pages() < 2) return false;
    if (x) *x = PAGE_X + (right ? PAGE_W - COLL_NAV_X - RENDER_CRIT_NAV_W / 2 : COLL_NAV_X + RENDER_CRIT_NAV_W / 2);
    if (y) *y = PAGE_Y + COLL_NAV_Y + RENDER_CRIT_NAV_W / 2;
    return true;
}
static void fmt_dur(char *out, size_t n, uint32_t s) {
    if (s < 60) snprintf(out, n, "%u S", (unsigned)s);
    else if (s < 3600) snprintf(out, n, "%u MIN", (unsigned)(s / 60));
    else snprintf(out, n, lang_tr("%u H %u MIN"), (unsigned)(s / 3600), (unsigned)(s % 3600 / 60));
}
/* a fish left and the slots shifted (progression: a sale, a gift): the keeper page's card and its armed SELL follow their
   fish; the card of the one that left closes (the 11-02 sweep: SELL armed by row sold the fish that slid into it) */
void render_fish_removed(int idx) {
    if (!g_ms_tankrow && g_ms_row >= 0) {
        if (g_ms_row == idx && !g_ms_fryrow) { ms_close(); g_ms_row = -1; }
        else if (g_ms_row > idx) g_ms_row--;
    }
    if (g_ms_fish == idx) g_ms_fish = -1; else if (g_ms_fish > idx) g_ms_fish--;
}
static void fmt_met(char *out, size_t n, uint32_t met) {
    int64_t now = clock_port_now_unix();
    if (!met || now <= 0 || now < met) { snprintf(out, n, "ON THIS GLASS"); return; }   /* (a board without a clock) */
    uint32_t d = (uint32_t)((now - met) / 86400);
    if (d == 0) snprintf(out, n, "TODAY"); else if (d == 1) snprintf(out, n, "YESTERDAY"); else snprintf(out, n, lang_tr("%u DAYS AGO"), (unsigned)d);
}
static void entry_fish(fish_t *f, const coll_entry_t *e) {
    memset(f, 0, sizeof *f);
    f->color = e->color; f->fin = e->fin; f->accent = e->accent; f->stage = (stage_t)e->stage;
    f->shape = e->shape; f->fins = e->fins; f->pattern = e->pattern; f->uid = e->uid;
    memcpy(f->name, e->name, FISH_NAME_MAX); f->name[FISH_NAME_MAX] = 0;
}
/* a collected fish's passport, with its history here */
static void entry_card_draw(ctx_t *c, uint16_t *fb, int stride, const coll_entry_t *e, double clock) {
    const uint32_t GOLD = 0xe8c872, INK = 0x13324f, LBL = 0x7fa3b5;
    const int W = PASSPORT_CARD_W, H = PASSPORT_BTN_Y, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    src_t bg = src_color(0x0e2233, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 248);
    for (int y = Y + 34; y < Y + H - 30; y += 7)
        for (int x = X + 3; x < X + W - 3; x++) px_blend(c, x, y + (int)(2.0f * sinf(x * 0.11f + y * 0.37f)), 0x2b4a63, 80);
    src_t band = src_color(INK, 1.0f);
    for (int y = Y; y < Y + 26; y++) span(c, X, X + W - 1, y, &band, 255);
    draw_text(c, X + (W - text_w("POCKET TANK PASSPORT", 2)) / 2, Y + 7, 2, GOLD, "POCKET TANK PASSPORT");
    rect_edge(c, X, Y, W, H, GOLD); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, INK);
    const int PX = X + 12, PY = Y + 36, PW = 100, PH = 84;
    rect_fill(c, PX, PY, PW, PH, 0x0a1824); rect_edge(c, PX, PY, PW, PH, 0x9fd8e2);
    fish_t f; entry_fish(&f, e);
    render_fish_portrait(fb, stride, PX + PW / 2.0f - PAGE_X, PY + PH / 2.0f - PAGE_Y, 1.3f, &f, PW - 20, clock);
    const int FX = PX + PW + 12;
    static const char *const STAGE_W[4] = { "FRY", "JUVENILE", "ADULT", "ELDER" };
    char home[24], form[24], met[24], here[24], visits[24];
    char hlbl[40]; home_fields(e->home, e->home_name, "HOME", hlbl, sizeof hlbl, home, sizeof home);
    snprintf(form, sizeof form, "%s %s", lang_tr(SHAPE_NAMES[e->shape % SHAPE_N]), lang_tr(FINS_NAMES[e->fins % FINS_N]));
    fmt_met(met, sizeof met, e->met_unix); fmt_dur(here, sizeof here, e->here_s);
    snprintf(visits, sizeof visits, lang_tr(e->visits == 1 ? "%u VISIT" : "%u VISITS"), (unsigned)e->visits);   /* (a format each: the plural is the language's) */
    struct { const char *lbl, *val; } FL[4] = { { lang_tr("NAME"), f.name }, { hlbl, home }, { lang_tr("FORM"), form }, { lang_tr("STAGE"), lang_tr(STAGE_W[e->stage & 3]) } };
    for (int i = 0; i < 4; i++) {
        int fy = Y + 33 + i * 26;
        draw_words(c, FX, fy, 1, LBL, FL[i].lbl);
        int vx = FX;                                  /* (no shape: a collected fish's home is its waters, for good) */
        draw_words(c, vx, fy + 10, 2, 0xffffff, FL[i].val);
    }
    /* its history here: first met, visits, time in this water, fry */
    draw_text(c, PX, PY + PH + 6, 1, LBL, "FIRST MET");
    draw_text(c, PX, PY + PH + 15, 2, 0xffffff, met);
    draw_text(c, FX, Y + 139, 1, LBL, "IN YOUR TANK");
    draw_text(c, FX, Y + 148, 2, 0xffffff, here);
    draw_text(c, PX, PY + PH + 34, 1, LBL, "VISITS");
    draw_text(c, PX, PY + PH + 43, 2, 0xffffff, visits);
    if (e->fry) {
        char fr[24]; snprintf(fr, sizeof fr, lang_tr("%u FRY HERE"), (unsigned)e->fry);
        draw_text(c, FX, Y + 167, 1, LBL, "FAMILY");
        draw_text(c, FX, Y + 176, 2, 0xffffff, fr);
    }
    const char *st = "COLLECTED";                      /* the stamp, as on its card */
    int sw = text_w(st, 2) + 12, sx0 = X + W - sw - 10, sy0 = Y + 139;
    rect_edge(c, sx0, sy0, sw, 24, GOLD); rect_edge(c, sx0 + 2, sy0 + 2, sw - 4, 20, GOLD);
    draw_text(c, sx0 + 6, sy0 + 5, 2, GOLD, st);
    char m1[64]; int cols = (W - 20) / 6; if (cols > 60) cols = 60;
    snprintf(m1, sizeof m1, "P<FISH<<%s<<%08X", f.name, (unsigned)e->uid);
    for (int j = (int)strlen(m1); j < cols; j++) m1[j] = '<';
    m1[cols] = 0;
    draw_words(c, X + 10, Y + H - 13, 1, 0xc8d6de, m1);
}
/* the page's words and pips in the foot, left of CLOSE (the bowl: two lines in UPGRADES' place, the pips under the grid) */
static void coll_pager(ctx_t *c, int pg, int np) {
    char line[32];
#if defined(TANK_ROUND)
    char l2[24]; snprintf(line, sizeof line, lang_tr("PAGE %d"), pg + 1); snprintf(l2, sizeof l2, lang_tr("OF %d"), np);
    draw_text(c, 114 + (116 - text_w(line, 2)) / 2, MSP_CLOSE_Y, 2, 0xffffff, line);
    draw_text(c, 114 + (116 - text_w(l2, 2)) / 2, MSP_CLOSE_Y + 16, 2, 0xffffff, l2);
    int bx0 = (PAGE_W - (np * COLL_PIP - 4)) / 2, by = COLL_Y0 + 3 * COLL_CH + 3;
#else
    snprintf(line, sizeof line, lang_tr("PAGE %d OF %d"), pg + 1, np);
    int ty = MSP_CLOSE_Y + (MSP_CLOSE_H - 14) / 2, tx = 32;   /* (as far in as CLOSE stands from the right) */
    draw_text(c, tx, ty, 2, 0xffffff, line);
    char widest[32]; snprintf(widest, sizeof widest, lang_tr("PAGE %d OF %d"), np, np);   /* (the pips stand still page to page) */
    int bx0 = tx + text_w(widest, 2) + 12, by = ty + 4;
#endif
    for (int p = 0; p < np; p++) {
        int bx = bx0 + p * COLL_PIP;
        if (p == pg) rect_fill(c, bx - 1, by - 1, 8, 8, 0xffffff); else rect_edge(c, bx, by, 6, 6, MSP_TEAL);
    }
}
void render_collection(const tank_t *t, uint16_t *fb, int stride) {
    ctx_t c = ctx_page(fb, stride);
    for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = rgb565(MSP_INK, 1);   /* (the milestones page's backdrop) */
    char line[32]; int n = collection_count(), np = collection_pages();
    if (g_coll_page >= np) g_coll_page = np - 1;
    draw_text(&c, (PAGE_W - text_w("VISITOR COLLECTION", 2)) / 2, 22, 2, 0xe8c872, "VISITOR COLLECTION");
    snprintf(line, sizeof line, lang_tr("%d OF %d COLLECTED"), n, COLL_MAX);
    draw_text(&c, (PAGE_W - text_w(line, 1)) / 2, 42, 1, MSP_TEAL, line);
    if (np > 1) {
        crit_nav_box(&c, COLL_NAV_X, COLL_NAV_Y, -1, 0x9fd8e2);
        crit_nav_box(&c, PAGE_W - COLL_NAV_X - RENDER_CRIT_NAV_W, COLL_NAV_Y, 1, 0x9fd8e2);
        coll_pager(&c, g_coll_page, np);
    }
    for (int j = 0; j < COLL_PAGE; j++) {
        int i = g_coll_page * COLL_PAGE + j;
        int cx = COLL_X0 + (j % COLL_COLS) * COLL_CW, cy = COLL_Y0 + (j / COLL_COLS) * COLL_CH;
        if (i >= n) { rect_edge(&c, cx + 6, cy + 4, COLL_CW - 12, COLL_CH - 12, 0x1c2f36); continue; }   /* an empty place to fill */
        const coll_entry_t *e = collection_get(i);
        rect_edge(&c, cx + 6, cy + 4, COLL_CW - 12, COLL_CH - 12, 0x3f6a72);
        fish_t f; entry_fish(&f, e);
        render_fish_portrait(fb, stride, cx + COLL_CW / 2.0f, cy + 30, 0.9f, &f, COLL_CW - 26, t->clock + j * 0.7f);
        draw_words(&c, cx + (COLL_CW - words_w(f.name, 2)) / 2, cy + 52, 2, 0xffffff, f.name);
    }
    button(&c, MSP_CLOSE_X, MSP_CLOSE_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", 2);
    if (g_coll_sel >= 0 && g_coll_sel < n) {           /* its passport, the page dimmed under it */
        for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = (uint16_t)((fb[y * stride + x] >> 1) & 0x7bef);
        ctx_t fc = ctx_full(fb, stride, 1.0f);
        entry_card_draw(&fc, fb, stride, collection_get(g_coll_sel), t->clock);
    }
}
int render_collection_tap(const tank_t *t, float x, float y) {
    (void)t;
    if (g_coll_sel >= 0) { g_coll_sel = -1; return COLL_TAP_KEPT; }   /* any tap: back to the grid */
    float px = x - PAGE_X, py = y - PAGE_Y;
    if (px >= MSP_CLOSE_X - 8 && py >= MSP_CLOSE_Y - 6) return COLL_TAP_CLOSE;
    int np = collection_pages();
    if (np > 1 && py < COLL_NAV_Y + RENDER_CRIT_NAV_W + 12) {   /* the arrows: their box and 12 px round it (the grid starts below) */
        if (px >= COLL_NAV_X - 12 && px < COLL_NAV_X + RENDER_CRIT_NAV_W + 12) { g_coll_page = (g_coll_page + np - 1) % np; return COLL_TAP_KEPT; }
        if (px >= PAGE_W - COLL_NAV_X - RENDER_CRIT_NAV_W - 12 && px < PAGE_W - COLL_NAV_X + 12) { g_coll_page = (g_coll_page + 1) % np; return COLL_TAP_KEPT; }
    }
    if (px >= COLL_X0 && px < COLL_X0 + COLL_COLS * COLL_CW && py >= COLL_Y0 && py < COLL_Y0 + 3 * COLL_CH) {
        int i = g_coll_page * COLL_PAGE + (int)((py - COLL_Y0) / COLL_CH) * COLL_COLS + (int)((px - COLL_X0) / COLL_CW);
        if (i < collection_count()) { g_coll_sel = i; return COLL_TAP_KEPT; }
    }
    return COLL_TAP_NONE;
}
void render_milestones_show_fish(const tank_t *t, int fish) {
    if (fish >= 0 && fish < t->n_fish) ms_open(t, fish, false, false, -1, NULL, 0, false);
}
bool render_milestones_card(const tank_t *t, int *fish, int *rename_x, int *sell_x, int *btn_y) {
    if (!ms_card(t)) return false;
    if (fish) *fish = g_ms_row;
    bool three = give_look(t, g_ms_row) != GV_HIDDEN;
    if (rename_x) *rename_x = PAGE_X + (three ? ms_card_btn3_x(0) + MSP_CARD_BTN3_W / 2 : ms_card_btn_x(0) + MSP_CARD_BTN_W / 2);
    if (sell_x)   *sell_x   = PAGE_X + (three ? ms_card_btn3_x(2) + MSP_CARD_BTN3_W / 2 : ms_card_btn_x(1) + MSP_CARD_BTN_W / 2);
    if (btn_y)    *btn_y    = PAGE_Y + ms_card_btn_y(t) + MSP_HOW_H / 2;
    return true;
}
const char *render_milestones_line(void) { return g_ms_sub; }
bool render_milestones_give(const tank_t *t, int *x, int *y) {
    if (!ms_card(t) || give_look(t, g_ms_row) == GV_HIDDEN) return false;
    if (x) *x = PAGE_X + ms_card_btn3_x(1) + MSP_CARD_BTN3_W / 2;
    if (y) *y = PAGE_Y + ms_card_btn_y(t) + MSP_HOW_H / 2;
    return true;
}
bool render_milestones_arrow(const tank_t *t, bool right, int *x, int *y) {
    if (!g_ms_caption[0] || g_ms_tip) return false;
    if (x) *x = PAGE_X + (right ? MSP_MODAL_X + MSP_MODAL_W - MSP_ARROW_IN - MSP_ARROW_W / 2 : MSP_MODAL_X + MSP_ARROW_IN + MSP_ARROW_W / 2);
    if (y) *y = PAGE_Y + ms_modal_y(t) + MSP_ARROW_IN + MSP_ARROW_H / 2;
    return true;
}
void render_milestones_row(int row, int *name_x, int *y) {
    if (name_x) *name_x = PAGE_X + 100;
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
    bool named = false;                                       /* title is a fish name: as typed */
    const char *caption2 = NULL;                              /* a second line: the title and both move up */
    const icon_t *ic = NULL;
    const fish_t *f = fish >= 0 && fish < t->n_fish ? &t->fish[fish] : NULL;
    if (kind == 6 || kind == 7) {                             /* NOTICE_MODEL_DAMAGED (2026-10-21): the AI model is damaged; */
        if (kind == 7) {                                      /* NOTICE_WRONG_BOARD (0.3.9.3): this version is another board's */
            const char *w = pt_board_words(board_port_hw());
            snprintf(title, sizeof title, "WRONG VERSION");
            snprintf(caption, sizeof caption, lang_tr("THIS IS THE %s"), w ? w : "BOARD");
            caption2 = "CHECK FOR UPDATES FIXES IT";
        } else {
            snprintf(title, sizeof title, "AI MODEL DAMAGED");
            snprintf(caption, sizeof caption, "REINSTALL AT POCKETANK.COM");
            caption2 = "YOUR FISH ARE SAFE";
        }
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
        const int R = 20, PX = X + W / 2 - 3, PY = Y + 36;
        for (int dy = -R; dy <= R; dy++) for (int dx = -R; dx <= R; dx++) {
            int bx = dx - 10, by = dy + 7;
            if (dx * dx + dy * dy <= R * R && bx * bx + by * by > 17 * 17) rect_fill(&c, PX + dx, PY + dy, 1, 1, MSP_TEAL);
        }
    } else if (kind == 4) {                                   /* NOTICE_UPDATED (2026-09-30): the first boot of a new release */
        snprintf(title, sizeof title, "UPDATED");
        snprintf(caption, sizeof caption, lang_tr("YOUR TANK IS NOW V%s"), PT_RELEASE);
        /* a tick in a ring, teal: the shape says it, not the hue */
        const int R = 22, PX = X + W / 2, PY = Y + 48;
        ring(&c, PX, PY, R, MSP_TEAL); ring(&c, PX, PY, R - 1, MSP_TEAL);
        for (int i = 0; i < 6; i++)  rect_fill(&c, PX - 11 + i, PY + i - 1, 3, 3, MSP_TEAL);
        for (int i = 0; i < 12; i++) rect_fill(&c, PX - 6 + i, PY + 4 - i, 3, 3, MSP_TEAL);
    } else if (kind == 3) {                                   /* NOTICE_LOW_BATTERY */
        snprintf(title, sizeof title, "LOW BATTERY");
        snprintf(caption, sizeof caption, "PLEASE CHARGE THE TANK");
        /* the pill, large: outline + nub, the last sliver lit red */
        const int PW = 60, PH = 28, PX = X + (W - PW) / 2, PY = Y + 34;
        rect_edge(&c, PX, PY, PW, PH, 0x9fb4b8); rect_edge(&c, PX + 1, PY + 1, PW - 2, PH - 2, 0x9fb4b8);
        rect_fill(&c, PX + PW, PY + 8, 5, PH - 16, 0x9fb4b8);
        rect_fill(&c, PX + 4, PY + 4, 7, PH - 8, 0xf25b65);
    } else if (kind == 2) {                                   /* NOTICE_STAGE */
        if (f) { snprintf(title, sizeof title, "%s", f->name); named = true;
                 snprintf(caption, sizeof caption, "IS NOW %s %s", f->stage == STAGE_ADULT || f->stage == STAGE_ELDER ? "AN" : "A", STAGE_WORDS[f->stage & 3]);
                 render_fish_preview(fb, stride, X + W / 2, Y + 48, f->size * 1.6f, f->color, f->fin, f->accent, f, FISH_ROOM_CARD, t->clock); }
    } else if (kind == 1) {                                   /* NOTICE_TANK_MILESTONE */
        for (int k = 0; k < TANK_BADGE_N; k++) if (TANK_BADGES[k].bit == bit) ic = TANK_BADGES[k].icon;
        int bi = 0; while (bi < 31 && !(bit & (1u << bi))) bi++;
        snprintf(caption, sizeof caption, "%s", bi < TMS_COUNT ? TMS_NAMES[bi] : "");
        if (bit == TMS_FIRST_SALE) caption2 = "FRY NOW IN THE SHOP";   /* what the sale opened (0.3.5) */
        if (!ic && t->n_fish) {                               /* a population milestone: the fish that made the count (F3) */
            const fish_t *n = &t->fish[fish >= 0 && fish < t->n_fish ? fish : progression_newest(t)];   /* (the notice's own: R-seam S2) */
            render_fish_preview(fb, stride, X + W / 2, Y + 48, n->size * 1.6f, n->color, n->fin, n->accent, n, FISH_ROOM_CARD, t->clock);
        }
    } else {                                                  /* NOTICE_MILESTONE */
        for (int k = 0; k < 6; k++) if (FISH_BADGES[k].bit == bit) ic = FISH_BADGES[k].icon;
        int bi = 0; while (bi < 31 && !(bit & (1u << bi))) bi++;
        if (f) { snprintf(title, sizeof title, "%s", f->name); named = true; }
        snprintf(caption, sizeof caption, "%s", bi < MS_FISH_COUNT ? MS_NAMES[bi] : "");
        if (!ic && f) render_fish_preview(fb, stride, X + W / 2, Y + 48, f->size * 1.6f, f->color, f->fin, f->accent, f, FISH_ROOM_CARD, t->clock);
    }
    if (ic) blit_icon_scaled(&c, X + (W - ic->w * 2) / 2, Y + 16 + (32 - ic->w), ic, 2, true);
    if (named) draw_words(&c, X + (W - words_w(title, 3)) / 2, Y + (caption2 ? 70 : 92), 3, 0xffffff, title);
    else       draw_text(&c, X + (W - text_w(title, 3)) / 2, Y + (caption2 ? 70 : 92), 3, 0xffffff, title);
    draw_text(&c, X + (W - text_w(caption, 2)) / 2, Y + (caption2 ? 104 : 124), 2, MSP_TEAL, caption);
    if (caption2) draw_text(&c, X + (W - text_w(caption2, 2)) / 2, Y + 126, 2, MSP_TEAL, caption2);
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
static double g_shp_sell_clock;      /* ... when (0.4: a doubled tap within MSP_SELL_WAIT_S is not the yes) */
static double g_shp_open_clock;      /* the tank clock when a row opened its modal: a tap within MSP_SELL_WAIT_S of it is the
                                        same double tap (0.4 rc: a kid's double tap on rows 3-4 opened the modal, then its
                                        second tap landed on BUY, which stands where those rows do) */
static bool shp_fresh(const tank_t *t) { return hold_spent_at(&g_shp_open_clock, tap_press(t), t->clock, MSP_SELL_WAIT_S); }   /* (the row's tap doubled: THE TOUCH RULE) */
static const icon_t *shop_icon(int item) { return item == 0 ? &icon_shop_plant : item == 1 ? &icon_shop_snail : item == 2 ? &icon_shop_castle : item == 3 ? &icon_shop_coral : item == 4 ? &icon_shop_cluster : item == 5 ? &icon_shop_shrimp : item == 6 ? &icon_shop_urchin : item == 7 ? &icon_shop_seahorse : item == 8 ? &icon_shop_pumpkin : item == 9 ? &icon_shop_volcano : &icon_shop_crab; }
/* pages (2026-09-23, the fourth item): SHP_PER_PAGE rows fit between the
 * coin and the foot buttons once the filler caption went (HOW TO EARN says
 * the same). With more items than a page holds, arrows at the header's
 * right flip through the pages; with one page nothing shows. */
static int g_shp_page;
static void shop_arrow(ctx_t *c, int x, int y, bool right, uint32_t rgb) {   /* a chevron in a button */
    button(c, x, y, SHP_ARROW_W, SHP_ARROW_H, 0x1c2f36, rgb, "", 2);
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
    draw_text(c, x + 20, y, 2, rgb, n);
}
/* the FRY row (0.3.5): the first row once a fish has been sold. Rows are
 * numbered across the pages; row 0 is FRY when it shows, the items after */
#define SHP_MODAL_FRY 99             /* g_shp_modal: the fry's modal */
static int g_shp_fry_pick;           /* the fry picked in its modal */
static bool shp_fry(const tank_t *t) { return progression_fry_for_sale(t); }
/* THE SHOP'S AREAS (0.4.1; Strato 11-04, the look from working-assets/shop-navigation-v1): DECORATIONS, CRITTERS and
 * BACKGROUNDS, each a square tab in the foot, three rows a page. A row is an item (0..), the FRY (-1: CRITTERS' first
 * while one is for sale) or a background (SHP_ROW_BG + bg). The pumpkin's row shows while this build sells it (or owned). */
static const char *const SHP_AREA_NAME[SHP_AREAS] = { "DECORATIONS", "CRITTERS", "BACKGROUNDS" };
#define SHP_ROW_BG  SHOP_ROW_BG
#define SHP_MODAL_BG 200             /* g_shp_modal: a background's modal (+ bg) */
static int g_shp_area;
static int shp_list(const tank_t *t, int *out) {
    static const int DECOR[] = { 0, 2, 3, 4, 8, 9 }, CRIT[] = { 1, 5, 6, 7, 10 };
    int n = 0;
    if (g_shp_area == SHP_AREA_DECOR) { for (unsigned k = 0; k < sizeof DECOR / sizeof DECOR[0]; k++) if (DECOR[k] != 8 || progression_pumpkin_offered(t)) out[n++] = DECOR[k]; }
    else if (g_shp_area == SHP_AREA_CRITTERS) { if (shp_fry(t)) out[n++] = -1; for (unsigned k = 0; k < sizeof CRIT / sizeof CRIT[0]; k++) out[n++] = CRIT[k]; }
    else for (int bg = 0; bg < BG_N; bg++) out[n++] = SHP_ROW_BG + bg;
    return n;
}
static int  shp_rows(const tank_t *t) { int r[16]; return shp_list(t, r); }
static int  shp_pages(const tank_t *t) { return (shp_rows(t) + SHP_PER_PAGE - 1) / SHP_PER_PAGE; }
static int  shp_item(const tank_t *t, int row) { int r[16]; int n = shp_list(t, r); return row >= 0 && row < n ? r[row] : -2; }
bool render_shop_find(const tank_t *t, int code, int *area, int *page, int *row) {   /* (the tests: where a row is) */
    const int keep = g_shp_area; bool found = false;
    for (int a = 0; a < SHP_AREAS && !found; a++) {
        int r[16]; g_shp_area = a; const int n = shp_list(t, r);
        for (int i = 0; i < n; i++) if (r[i] == code) { *area = a; *page = i / SHP_PER_PAGE; *row = i % SHP_PER_PAGE; found = true; break; }
    }
    g_shp_area = keep;
    return found;
}
int render_shop_area_rows(const tank_t *t, int area) { const int keep = g_shp_area; g_shp_area = area; const int n = shp_rows(t); g_shp_area = keep; return n; }
static const icon_t *bg_thumb(int bg) {
    static const icon_t *const T[BG_N] = { &icon_bg_thumb_0, &icon_bg_thumb_1, &icon_bg_thumb_2, &icon_bg_thumb_3, &icon_bg_thumb_4, &icon_bg_thumb_5, &icon_bg_thumb_6, &icon_bg_thumb_7 };
    return T[(unsigned)bg < BG_N ? bg : 0];
}
static bool shp_bg_owned(const tank_t *t, int bg) { return bg == BG_OPEN || (t->sd_unlocks & SD_ITEM_BG(bg)); }
static void shop_bg_row(ctx_t *c, const tank_t *t, int bg, int top) {   /* a background's row: its picture, its name, IN USE / OWNED / its price, the button */
    const bool owned = shp_bg_owned(t, bg), now = t->bg_now == bg, can = t->sd_balance >= SD_PRICE_BG;
    blit_icon(c, 32, top, bg_thumb(bg), 255);
    draw_text(c, 76, top + 2, 2, 0xffffff, bg_name(bg));
    if (now) draw_text(c, 76, top + 20, 2, MSP_TEAL, "IN USE");
    else if (owned) draw_text(c, 76, top + 20, 2, MSP_TEAL, "OWNED");
    else price_tag(c, 76, top + 20, SD_PRICE_BG, can ? MSP_TEAL : MSP_QUIET);
    const char *lab = now ? "IN USE" : owned ? "USE" : "UNLOCK";
    if (now || (!owned && !can)) button(c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, now ? MSP_INK : 0x1c2f36, MSP_DIM, lab, 2);
    else { button(c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, MSP_TEAL, MSP_TEAL, lab, 2);
           draw_text(c, SHP_BTN_X + (SHP_BTN_W - text_w(lab, 2)) / 2, top + (SHP_BTN_H - 14) / 2, 2, MSP_INK, lab); }
}
/* THE SHOP'S FOOT: five squares where the keeper page has its five (render_ms_foot_box) - ?, the three areas, X. The bowl
   puts the ? in the cap, as the keeper page puts its cog */
void render_shop_foot_box(int b, int *x, int *y);
static void shop_foot_box(int b, int *x, int *y) { render_shop_foot_box(b, x, y); }
void render_shop_foot_box(int b, int *x, int *y) {
    static const int SLOT[SHF_N] = { MSF_UPG, MSF_SET, MSF_TANKS, MSF_VIS, MSF_CLOSE }, BOWL[SHF_N] = { MSF_SET, MSF_UPG, MSF_TANKS, MSF_VIS, MSF_CLOSE };
    render_ms_foot_box(PAGE_BOWL ? BOWL[b] : SLOT[b], x, y);
}
static void shop_foot(ctx_t *c) {
    for (int b = 0; b < SHF_N; b++) {
        int x, y; shop_foot_box(b, &x, &y);
        const bool sel = b >= SHF_DECOR && b <= SHF_BG && b - SHF_DECOR == g_shp_area;
        if (sel) {                                            /* the area showing: a brighter panel, a double edge, a small bar under it */
            button(c, x, y, MSP_FOOT_SQ, MSP_FOOT_SQ, 0x26444e, MSP_TEAL, "", 2);
            rect_edge(c, x + 2, y + 2, MSP_FOOT_SQ - 4, MSP_FOOT_SQ - 4, MSP_TEAL);
            rect_fill(c, x + MSP_FOOT_SQ / 2 - 8, y + MSP_FOOT_SQ + 4, 16, 2, MSP_TEAL);
        } else button(c, x, y, MSP_FOOT_SQ, MSP_FOOT_SQ, 0x1c2f36, MSP_TEAL, "", 2);
        const icon_t *ic = b == SHF_EARN ? &icon_shop_tab_earn : b == SHF_DECOR ? &icon_shop_tab_decor : b == SHF_CRIT ? &icon_shop_crab
                         : b == SHF_BG ? &icon_shop_tab_bg : &icon_foot_close;   /* (the crab: our own shop sprite, Strato 11-04 - it fills
                                                                                  *  the square like the 36 px glyphs; the snail read small) */
        const int lift = b == SHF_CRIT ? 5 : 0;                  /* (the crab's sprite stands on rows 11..31 of 32: centered by its body) */
        blit_icon(c, x + (MSP_FOOT_SQ - ic->w) / 2, y + (MSP_FOOT_SQ - ic->h) / 2 - lift, ic, 255);
    }
}
static void fry_hours(char *out, size_t n) {   /* "NEW FRY IN 14 H" / "IN 40 MIN" */
    float s = progression_fry_new_in_s();
    if (s >= 3600) snprintf(out, n, lang_tr("NEW FRY IN %d H"), (int)((s + 1799) / 3600));
    else snprintf(out, n, lang_tr("NEW FRY IN %d MIN"), (int)(s / 60) + 1);
}
static void fry_draw(uint16_t *fb, int stride, float x, float y, float size, const fry_offer_t *o, double clock) {
    fish_t f; memset(&f, 0, sizeof f);
    f.stage = STAGE_ADULT; f.color = o->body; f.fin = tank_fin_for(o->body); f.accent = o->accent;   /* (0.4, 11-01, Strato: the adult it grows into -
                                                                                                         its fins and markings show; a big form scaled down to fit) */
    f.shape = o->shape; f.fins = o->fins; f.pattern = o->pattern;
    render_fish_portrait(fb, stride, x, y, size, &f, FISH_ROOM_ROW, clock);
}
static int fry_first_left(void) {
    for (int k = 0; k < FRY_OFFER_N; k++) { fry_offer_t o; progression_fry_offer(k, &o); if (!o.sold) return k; }
    return -1;
}
static void shop_fry_row(ctx_t *c, const tank_t *t, uint16_t *fb, int stride, int top) {
    int k = fry_first_left();
    bool room = progression_fry_room_to_buy(t), can = t->sd_balance >= SD_PRICE_FRY;
    if (k >= 0) { fry_offer_t o; progression_fry_offer(k, &o); fry_draw(fb, stride, 48, top + 16, 0.9f, &o, t->clock); }
    draw_text(c, 76, top + 2, 2, 0xffffff, "FRY");
    if (k < 0) { char l[24]; fry_hours(l, sizeof l); draw_text(c, 76, top + 20, 2, MSP_QUIET, l); }
    else price_tag(c, 76, top + 20, SD_PRICE_FRY, can && room ? MSP_TEAL : MSP_QUIET);
    const char *b = k < 0 ? "SOLD OUT" : !room ? "FULL" : "CHOOSE";
    if (k >= 0 && room && can) { button(c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, MSP_TEAL, MSP_TEAL, b, 2);
                                 draw_text(c, SHP_BTN_X + (SHP_BTN_W - text_w(b, 2)) / 2, top + (SHP_BTN_H - 14) / 2, 2, MSP_INK, b); }
    else button(c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, 0x1c2f36, MSP_DIM, b, 2);
}
static void shop_fry_modal(ctx_t *c, const tank_t *t, uint16_t *fb, int stride) {
    const int X = SHP_MODAL_X, Y = SHP_MODAL_Y, W = SHP_MODAL_W, H = SHP_MODAL_H;
    rect_fill(c, X, Y, W, H, 0x04141a);
    rect_edge(c, X, Y, W, H, MSP_TEAL); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    draw_text(c, X + (W - text_w("FRY FOR SALE", 3)) / 2, Y + 14, 3, 0xffffff, "FRY FOR SALE");
    for (int k = 0; k < FRY_OFFER_N; k++) {
        fry_offer_t o; progression_fry_offer(k, &o);
        int bx = SHP_FRY_BOX_X(k), by = SHP_FRY_BOX_Y;
        bool lit = !o.sold && k == g_shp_fry_pick;
        rect_fill(c, bx, by, SHP_FRY_BOX_W, SHP_FRY_BOX_H, lit ? 0x14303a : 0x0b1d23);
        uint32_t e = lit ? 0xffffff : o.sold ? 0x1c2f36 : 0x4d6a72;
        rect_edge(c, bx, by, SHP_FRY_BOX_W, SHP_FRY_BOX_H, e);
        if (lit) rect_edge(c, bx + 1, by + 1, SHP_FRY_BOX_W - 2, SHP_FRY_BOX_H - 2, e);
        if (o.sold) continue;                                    /* bought: a blank until the fresh three */
        fry_draw(fb, stride, bx + SHP_FRY_BOX_W / 2, by + 34, 1.5f, &o, t->clock + k * 0.7f);
        /* its body in words, where the color squares were (the adult shows its colors) - the body alone, Strato 11-01: the fins and
           the pattern were too tiny to read, "and we can leave a bit of mystery there" */
        draw_text(c, bx + (SHP_FRY_BOX_W - text_w(SHAPE_NAMES[o.shape], 2)) / 2, by + SHP_FRY_BOX_H - 20, 2, lit ? 0xffffff : 0x9fd8e2, SHAPE_NAMES[o.shape]);
    }
    bool room = progression_fry_room_to_buy(t), can = t->sd_balance >= SD_PRICE_FRY;
    fry_offer_t pk; progression_fry_offer(g_shp_fry_pick, &pk);

    bool go = room && can && !pk.sold && progression_fry_left() > 0;
    char line[32]; snprintf(line, sizeof line, "%d", SD_PRICE_FRY);
    price_tag(c, X + (W - 20 - text_w(line, 2)) / 2, Y + 138, SD_PRICE_FRY, go ? 0xffffff : MSP_QUIET);
    if (!room) snprintf(line, sizeof line, progression_fish_held(t) >= POP_CAP ? "THE TANK IS FULL" : "WELCOME THE NEW FRY");   /* (a place held for a gift or a fry next door: full) */
    else if (!can) snprintf(line, sizeof line, lang_tr("YOU HAVE %d"), (int)t->sd_balance);
    else line[0] = 0;
    if (line[0]) draw_text(c, X + (W - text_w(line, 2)) / 2, Y + 160, 2, MSP_QUIET, line);
    fry_hours(line, sizeof line);
    draw_text(c, X + (W - text_w(line, 2)) / 2, Y + 180, 2, MSP_QUIET, line);
    const int bx = X + (W - MSP_HOW_W) / 2, by = Y + H - 12 - MSP_HOW_H;
    if (go) { button(c, bx, by, MSP_HOW_W, MSP_HOW_H, MSP_TEAL, MSP_TEAL, "BUY", 2);
              draw_text(c, bx + (MSP_HOW_W - text_w("BUY", 2)) / 2, by + (MSP_HOW_H - 14) / 2, 2, MSP_INK, "BUY"); }
    else button(c, bx, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "BUY", 2);
}
/* the shelf's words (0.3.9): the line under IN THE TANK / ON THE SHELF in a
 * placeable piece's modal - a living piece on the shelf says how far it has
 * grown (it keeps growing there), the pumpkin says when this build no longer
 * sells it (so a SELL is an informed one). NULL = nothing to say. */
static const char *shelf_line(const tank_t *t, int item, char *out, size_t n) {
    bool shelved = (t->sd_shelved & SD_ITEMS[item].bit) != 0;
    if (item == 8 && !SD_PUMPKIN_IN_SHOP) return "NOT IN THE SHOP NOW";
    if (!shelved) return tank_decor_locked(t, item) ? "LOCKED IN PLACE" : NULL;   /* (0.3.9: why a hold on it opens nothing; MOVE is the way to its page) */
    if (item == 3) { snprintf(out, n, lang_tr("GROWING  %d%%"), (int)(tank_coral_growth(t) * 100 / CORAL_FULL + 0.5f)); return out; }
    if (item == 4) { snprintf(out, n, lang_tr("GROWING  %d%%"), (int)(tank_cluster_growth(t) * 100 / CLUSTER_FULL + 0.5f)); return out; }
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
#define SHELF_FULL  256                                                /* the widest window: a sword plant at its ceiling (the frame is wider on every glass) */
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
    CTX_MADE();
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
    blit_icon(&c, SHP_COIN_X, SHP_COIN_Y, &icon_shop_sand_dollar_64, 255);
    draw_text(&c, SHP_HEAD_X, SHP_COIN_Y + 6, 2, MSP_TEAL, "SAND DOLLARS");
    char bal[16]; snprintf(bal, sizeof bal, "%d", (int)t->sd_balance);
    draw_text(&c, SHP_HEAD_X, SHP_COIN_Y + 28, 4, 0xffffff, bal);
    for (int x = 24; x < PAGE_W - 24; x++) px_blend(&c, x, SHP_ROW_Y0 - 10, MSP_DIM, 200);
    {   /* the area's name over the arrows */
        const char *an = SHP_AREA_NAME[g_shp_area]; const int mid = (SHP_ARROW_X0 + SHP_ARROW_X1 + SHP_ARROW_W) / 2;
        draw_text(&c, mid - text_w(an, 1) / 2, SHP_ARROW_Y - 13, 1, MSP_TEAL, an);
    }
    if (shp_pages(t) > 1) {
        shop_arrow(&c, SHP_ARROW_X0, SHP_ARROW_Y, false, g_shp_page > 0 ? MSP_TEAL : MSP_DIM);
        shop_arrow(&c, SHP_ARROW_X1, SHP_ARROW_Y, true, g_shp_page < shp_pages(t) - 1 ? MSP_TEAL : MSP_DIM);
    }
    for (int r = g_shp_page * SHP_PER_PAGE; r < shp_rows(t) && r < (g_shp_page + 1) * SHP_PER_PAGE; r++) {
        int top = SHP_ROW_Y0 + (r - g_shp_page * SHP_PER_PAGE) * SHP_ROW_DY, i = shp_item(t, r);
        if (i >= SHP_ROW_BG) { shop_bg_row(&c, t, i - SHP_ROW_BG, top); continue; }
        if (i < 0) { shop_fry_row(&c, t, fb, stride, top); continue; }
        const sd_item_t *it = &SD_ITEMS[i];
        bool owned = (t->sd_unlocks & it->bit) != 0, can = t->sd_balance >= it->price, shelved = owned && (t->sd_shelved & it->bit);
        if (shelved) shelf_slot(&c, 32, top, t, i, 1);                                                             /* the shelf (0.3.9): the piece as it is, in its little tank */
        else if (owned) blit_icon(&c, 32, top, shop_icon(i), 255); else blit_icon_locked(&c, 32, top, shop_icon(i));   /* (the rows are where the bowl is wide: their own column) */
        draw_text(&c, 76, top + 2, 2, 0xffffff, it->name);
        if (shelved) draw_text(&c, 76, top + 20, 2, MSP_TEAL, "ON THE SHELF");
        else if (owned) draw_text(&c, 76, top + 20, 2, MSP_TEAL, "IN THE TANK");
        else price_tag(&c, 76, top + 20, it->price, can ? MSP_TEAL : MSP_QUIET);
        if (shelved) { button(&c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, MSP_TEAL, MSP_TEAL, "PUT BACK", 2);          /* live: one tap returns it */
                       draw_text(&c, SHP_BTN_X + (SHP_BTN_W - text_w("PUT BACK", 2)) / 2, top + (SHP_BTN_H - 14) / 2, 2, MSP_INK, "PUT BACK"); }
        else if (owned) button(&c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, MSP_INK, MSP_DIM, "IN TANK", 2);
        else if (can) { button(&c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, MSP_TEAL, MSP_TEAL, "UNLOCK", 2);
                        draw_text(&c, SHP_BTN_X + (SHP_BTN_W - text_w("UNLOCK", 2)) / 2, top + (SHP_BTN_H - 14) / 2, 2, MSP_INK, "UNLOCK"); }
        else           button(&c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, 0x1c2f36, MSP_DIM, "UNLOCK", 2);
    }
    shop_foot(&c);                                            /* ?, the three areas, X (0.4.1) */
    if (g_shp_modal < 0 && !g_shp_earn) return;
    for (int y = 0; y < TANK_H; y++)                          /* the page out of reach under a modal */
        for (int x = 0; x < TANK_W; x++) { uint16_t n = FB_SWAP16(fb[y * stride + x]); fb[y * stride + x] = FB_SWAP16((uint16_t)((n >> 1) & 0x7bef)); }
    const int X = SHP_MODAL_X, W = SHP_MODAL_W;
    if (g_shp_earn) {
        const int Y = SHP_EARN_MODAL_Y, H = SHP_EARN_MODAL_H;
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        draw_text(&c, X + (W - text_w("HOW TO EARN", 3)) / 2, Y + 14, 3, 0xffffff, "HOW TO EARN");
        const char *const *lines = progression_sd_earn_lines();
        for (int i = 0; lines[i]; i++) draw_text(&c, X + 14, Y + 50 + i * 24, 2, MSP_TEAL, lines[i]);
        draw_text(&c, X + (W - text_w("TAP TO CLOSE", 2)) / 2, Y + H - 24, 2, MSP_QUIET, "TAP TO CLOSE");
        return;
    }
    if (g_shp_modal == SHP_MODAL_FRY) { shop_fry_modal(&c, t, fb, stride); return; }
    if (g_shp_modal >= SHP_MODAL_BG) {                        /* a background: its picture at 2x, its name, its words; UNLOCK, USE, or IN USE */
        const int bg = g_shp_modal - SHP_MODAL_BG, Y = SHP_MODAL_Y, H = SHP_MODAL_H;
        const bool owned = shp_bg_owned(t, bg), now = t->bg_now == bg, can = t->sd_balance >= SD_PRICE_BG;
        const char *w1, *w2; bg_words(bg, &w1, &w2);
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        blit_icon_scaled(&c, X + (W - 64) / 2, Y + 14, bg_thumb(bg), 2, true);
        draw_text(&c, X + (W - text_w(bg_name(bg), 3)) / 2, Y + 88, 3, 0xffffff, bg_name(bg));
        draw_text(&c, X + (W - text_w(w1, 2)) / 2, Y + 118, 2, MSP_TEAL, w1);
        draw_text(&c, X + (W - text_w(w2, 2)) / 2, Y + 138, 2, MSP_TEAL, w2);
        const int bx = X + (W - MSP_HOW_W) / 2, by = Y + H - 12 - MSP_HOW_H;
        if (now) draw_text(&c, X + (W - text_w("IN USE", 2)) / 2, by + 8, 2, MSP_TEAL, "IN USE");
        else if (owned) { button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, MSP_TEAL, MSP_TEAL, "USE", 2);
                          draw_text(&c, bx + (MSP_HOW_W - text_w("USE", 2)) / 2, by + (MSP_HOW_H - 14) / 2, 2, MSP_INK, "USE"); }
        else {
            char line[32]; snprintf(line, sizeof line, "%d", SD_PRICE_BG);
            int pw = 20 + text_w(line, 2);
            price_tag(&c, X + (W - pw) / 2, Y + 164, SD_PRICE_BG, can ? 0xffffff : MSP_QUIET);
            if (!can) { snprintf(line, sizeof line, lang_tr("YOU HAVE %d"), (int)t->sd_balance); draw_words(&c, X + (W - words_w(line, 2)) / 2, Y + 184, 2, MSP_QUIET, line); }
            if (can) { button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, MSP_TEAL, MSP_TEAL, "UNLOCK", 2);
                       draw_text(&c, bx + (MSP_HOW_W - text_w("UNLOCK", 2)) / 2, by + (MSP_HOW_H - 14) / 2, 2, MSP_INK, "UNLOCK"); }
            else button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "UNLOCK", 2);
        }
        return;
    }
    const sd_item_t *it = &SD_ITEMS[g_shp_modal];
    const int Y = SHP_MODAL_Y, H = SHP_MODAL_H;
    bool owned = (t->sd_unlocks & it->bit) != 0, can = t->sd_balance >= it->price;
    rect_fill(&c, X, Y, W, H, 0x04141a);
    rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    blit_icon_scaled(&c, X + (W - 64) / 2, Y + 14, shop_icon(g_shp_modal), 2, true);
    draw_text(&c, X + (W - text_w(it->name, 3)) / 2, Y + 88, 3, 0xffffff, it->name);
    draw_text(&c, X + (W - text_w(it->words, 2)) / 2, Y + 118, 2, MSP_TEAL, it->words);
    draw_text(&c, X + (W - text_w(it->words2, 2)) / 2, Y + 138, 2, MSP_TEAL, it->words2);
    const int bx = X + (W - MSP_HOW_W) / 2, by = Y + H - 12 - MSP_HOW_H;
    if (owned && tank_decor_placeable(g_shp_modal)) {         /* a placeable piece (0.3.9): in the tank MOVE | SHELVE; on the shelf PLACE | SELL (twice) */
        bool shelved = (t->sd_shelved & it->bit) != 0; char sub[28];
        if (shelved) shelf_slot(&c, X + (W - 64) / 2, Y + 14, t, g_shp_modal, 2);   /* over the icon: the piece as it is on the shelf */
        const char *state = shelved ? "ON THE SHELF" : "IN THE TANK";
        draw_text(&c, X + (W - text_w(state, 2)) / 2, Y + 164, 2, MSP_TEAL, state);
        if (shelf_line(t, g_shp_modal, sub, sizeof sub)) draw_text(&c, X + (W - text_w(sub, 2)) / 2, Y + 184, 2, MSP_QUIET, sub);
        button(&c, SHP_TWO_X0, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, shelved ? "PLACE" : "MOVE", 2);
        if (!shelved) button(&c, SHP_TWO_X1, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "SHELVE", 2);
        else if (g_shp_sell_armed) { snprintf(sub, sizeof sub, "+%d OK?", progression_sell_value(g_shp_modal));
                                     button(&c, SHP_TWO_X1, by, MSP_HOW_W, MSP_HOW_H, MSP_TEAL, MSP_TEAL, sub, 2);
                                     draw_text(&c, SHP_TWO_X1 + (MSP_HOW_W - text_w(sub, 2)) / 2, by + (MSP_HOW_H - 14) / 2, 2, MSP_INK, sub); }
        else button(&c, SHP_TWO_X1, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "SELL", 2);
    } else if (owned) draw_text(&c, X + (W - text_w("IN THE TANK", 2)) / 2, by + 8, 2, MSP_TEAL, "IN THE TANK");   /* the snail: a permanent resident */
    else {
        char line[32]; snprintf(line, sizeof line, "%d", it->price);
        int pw = 20 + text_w(line, 2);
        price_tag(&c, X + (W - pw) / 2, Y + 164, it->price, can ? 0xffffff : MSP_QUIET);
        if (!can) { snprintf(line, sizeof line, lang_tr("YOU HAVE %d"), (int)t->sd_balance);
                    draw_text(&c, X + (W - text_w(line, 2)) / 2, Y + 184, 2, MSP_QUIET, line); }
        if (can) { button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, MSP_TEAL, MSP_TEAL, "UNLOCK", 2);
                   draw_text(&c, bx + (MSP_HOW_W - text_w("UNLOCK", 2)) / 2, by + (MSP_HOW_H - 14) / 2, 2, MSP_INK, "UNLOCK"); }
        else button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "UNLOCK", 2);
    }
}
static int shp_tap(const tank_t *t, float x, float y);
/* A MODAL'S CLOSING TAP DOUBLED (0.4.1 audit P8, T1 F5d): SHELVE (or BUY, or a tap off it) closes the modal, and the second
   tap of a double lands on the rows under it - a shelved row's PUT BACK band stands under the modal's right button, and
   a doubled SHELVE put the piece straight back. The rows under a modal that a tap closed spend taps MSP_SELL_WAIT_S, a
   the double's second half alone (touch_gate.h THE TOUCH RULE: never restarted); the foot and the arrows stand clear
   of every modal's buttons and stay live. */
static double g_shp_shut_clock = -1e9;   /* the tank clock when a tap closed a modal */
int render_shop_tap(const tank_t *t, float x, float y) {
    const bool up = g_shp_modal >= 0 || g_shp_earn;
    const int r = shp_tap(t, x, y);
    if (up && g_shp_modal < 0 && !g_shp_earn) g_shp_shut_clock = t->clock;
    return r;
}
static int shp_tap(const tank_t *t, float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;                    /* the page's own coordinates */
    if (g_shp_earn) { g_shp_earn = false; return SHOP_TAP_KEPT; }
    if (g_shp_modal >= 0 && shp_fresh(t)) return SHOP_TAP_KEPT;   /* the row's tap doubled: spent, the modal stays (BUY,
                                                                   MOVE, SHELVE, a close) (THE TOUCH RULE: not restarted) */
    if (g_shp_modal == SHP_MODAL_FRY) {                       /* the fry: a box picks, BUY buys the pick, elsewhere closes */
        if (y >= SHP_FRY_BOX_Y - 8 && y < SHP_FRY_BOX_Y + SHP_FRY_BOX_H + 6)
            for (int k = 0; k < FRY_OFFER_N; k++)
                if (x >= SHP_FRY_BOX_X(k) - SHP_FRY_BOX_GAP / 2 && x < SHP_FRY_BOX_X(k) + SHP_FRY_BOX_W + SHP_FRY_BOX_GAP / 2) {
                    fry_offer_t o; progression_fry_offer(k, &o);
                    if (!o.sold) g_shp_fry_pick = k;
                    return SHOP_TAP_KEPT;
                }
        const int bx = SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2, by = SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H;
        bool on_btn = y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN && x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X;
        fry_offer_t pk; progression_fry_offer(g_shp_fry_pick, &pk);
        g_shp_modal = -1;
        if (on_btn && !pk.sold && progression_fry_room_to_buy(t) && t->sd_balance >= SD_PRICE_FRY) return SHOP_TAP_FRY + g_shp_fry_pick;
        return SHOP_TAP_KEPT;
    }
    if (g_shp_modal >= SHP_MODAL_BG) {                         /* a background's modal: its button, elsewhere closes */
        const int bg = g_shp_modal - SHP_MODAL_BG;
        const int bx = SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2, by = SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H;
        bool on_btn = y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN && x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X;
        g_shp_modal = -1;
        if (!on_btn || t->bg_now == bg) return SHOP_TAP_KEPT;
        if (shp_bg_owned(t, bg)) return SHOP_TAP_BG_USE + bg;
        return t->sd_balance >= SD_PRICE_BG ? SHOP_TAP_BG_BUY + bg : SHOP_TAP_KEPT;
    }
    if (g_shp_modal >= 0) {
        int item = g_shp_modal; const sd_item_t *it = &SD_ITEMS[item];
        bool owned = (t->sd_unlocks & it->bit) != 0, can = t->sd_balance >= it->price;
        const int bx = SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2, by = SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H;
        bool row = y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN;
        bool on_btn = row && x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X;
        if (owned && tank_decor_placeable(item)) {                /* two buttons: in the tank MOVE | SHELVE; on the shelf PLACE | SELL (armed, then confirmed) */
            bool shelved = (t->sd_shelved & it->bit) != 0;
            bool on_left  = row && x >= SHP_TWO_X0 - MSP_HOW_SLOP_X && x < SHP_TWO_X0 + MSP_HOW_W + SHP_TWO_GAP / 2;
            bool on_right = row && x >= SHP_TWO_X1 - SHP_TWO_GAP / 2 && x < SHP_TWO_X1 + MSP_HOW_W + MSP_HOW_SLOP_X;
            if (shelved && on_right && !g_shp_sell_armed) { g_shp_sell_armed = true; g_shp_sell_clock = t->clock; return SHOP_TAP_KEPT; }
            if (shelved && on_right && hold_spent_at(&g_shp_sell_clock, tap_press(t), t->clock, MSP_SELL_WAIT_S)) return SHOP_TAP_KEPT;   /* a doubled tap
                                                                   (or a mash): still armed (THE TOUCH RULE: no restart) */
            g_shp_sell_armed = false; g_shp_modal = -1;
            if (on_right) return (shelved ? SHOP_TAP_SELL : SHOP_TAP_SHELVE) + item;
            if (on_left)  return (shelved ? SHOP_TAP_PLACE : SHOP_TAP_MOVE) + item;
            return SHOP_TAP_KEPT;
        }
        g_shp_modal = -1; g_shp_sell_armed = false;
        if (on_btn && !owned && can) return SHOP_TAP_BUY + item;
        return SHOP_TAP_KEPT;
    }
    for (int b = 0; b < SHF_N; b++) {                         /* the foot (0.4.1): ?, the three areas, X - each square with a little room round it */
        int bx, by; shop_foot_box(b, &bx, &by);
        if (x < bx - 6 || x >= bx + MSP_FOOT_SQ + 6 || y < by - 8 || y >= by + MSP_FOOT_SQ + 12) continue;
        if (b == SHF_EARN) { g_shp_earn = true; return SHOP_TAP_KEPT; }
        if (b == SHF_CLOSE) return SHOP_TAP_CLOSE;
        if (g_shp_area != b - SHF_DECOR) { g_shp_area = b - SHF_DECOR; g_shp_page = 0; }
        return SHOP_TAP_KEPT;
    }
    if (shp_pages(t) > 1 && y < SHP_ROW_Y0 - 10 && x >= SHP_ARROW_X0 - 8) {   /* the page arrows, in the header band */
        if (x < SHP_ARROW_X1 - 4) { if (g_shp_page > 0) g_shp_page--; }
        else if (g_shp_page < shp_pages(t) - 1) g_shp_page++;
        return SHOP_TAP_KEPT;
    }
    for (int r = g_shp_page * SHP_PER_PAGE; r < shp_rows(t) && r < (g_shp_page + 1) * SHP_PER_PAGE; r++) {
        int top = SHP_ROW_Y0 + (r - g_shp_page * SHP_PER_PAGE) * SHP_ROW_DY, i = shp_item(t, r);
        if (x >= 20 && y >= top - 8 && y < top + SHP_ROW_DY - 8) {
            if (hold_spent_at(&g_shp_shut_clock, tap_press(t), t->clock, MSP_SELL_WAIT_S)) return SHOP_TAP_KEPT;   /* a modal's closing tap doubled (above) */
            if (i >= SHP_ROW_BG) { g_shp_modal = SHP_MODAL_BG + (i - SHP_ROW_BG); g_shp_open_clock = t->clock; return SHOP_TAP_KEPT; }   /* a background: its modal */
            if (i >= 0 && (t->sd_shelved & SD_ITEMS[i].bit) && x >= SHP_BTN_X - 8) return SHOP_TAP_PLACE + i;   /* the row's PUT BACK (0.3.9) */
            if (i < 0) { int k = fry_first_left(); g_shp_fry_pick = k < 0 ? 0 : k; g_shp_modal = SHP_MODAL_FRY; }   /* the fry's modal, the first one left picked */
            else g_shp_modal = i;
            g_shp_open_clock = t->clock;
            return SHOP_TAP_KEPT;
        }
    }
    return SHOP_TAP_NONE;
}
bool render_shop_modal_up(void) { return g_shp_modal >= 0 || g_shp_earn; }
void render_shop_leave(void) { g_shp_shut_clock = -1e9; g_shp_modal = -1; g_shp_earn = false; g_shp_page = 0; g_shp_area = SHP_AREA_DECOR; g_shp_sell_armed = false; g_shelf_key[0] = g_shelf_key[1] = g_shelf_key[2] = 0; }

/* the toast: "+N" by a coin, top centre, for TOAST_S on the tank clock */
#define TOAST_S 2.5f
static int   g_toast_n;
static double g_toast_until = -1;
void render_sd_toast(const tank_t *t, uint16_t *fb, int stride) {
    int n = progression_sd_take_award();
    if (n > 0) {
        if (t->clock < g_toast_until) g_toast_n += n; else g_toast_n = n;
        g_toast_until = t->clock + TOAST_S;
    }
    if (t->clock >= g_toast_until || t->clock < g_toast_until - TOAST_S - 1) return;   /* (a reset sends the clock back) */
    ctx_t c = ctx_full(fb, stride, 1.0f);
    char txt[16]; snprintf(txt, sizeof txt, "+%d", g_toast_n);
    const int W = 30 + text_w(txt, 2), H = 22, X = (TANK_W - W) / 2, Y = PAGE_BOWL ? 44 : 8;   /* (the bowl's top is a narrow cap: lower) */
    for (int y = Y; y < Y + H; y++)
        for (int x = X; x < X + W; x++) px_blend(&c, x, y, 0x04141a, 215);
    rect_edge(&c, X, Y, W, H, MSP_TEAL);
    blit_icon(&c, X + 5, Y + 3, &icon_shop_sand_dollar_16, 255);
    draw_text(&c, X + 25, Y + 4, 2, 0xffffff, txt);
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
static const char *const SET_LIGHT[LIGHT_IDLE_N + 1] = { "DOUBLE-TAP",   /* MANUAL, the default: the row says how the light is worked */
    "5 SEC", "15 SEC", "30 SEC", "1 MIN", "3 MIN", "5 MIN", "10 MIN", "30 MIN" };
static const char *const SET_FEED[2]   = { "ON", "OFF" };          /* the default first */
#if TANK_WORN
static const char *const SET_SCREEN[2] = { "NORMAL", "TURNED" };   /* the default first */
#endif

static void link_glyph(ctx_t *c, int x, int y, uint32_t rgb);   /* the link mode section, at the end */
static void set_row(ctx_t *c, int row_y, const char *label, const char *const names[], int n, int chosen) {
    draw_text(c, SET_LABEL_X, row_y, 2, MSP_TEAL, label);
    for (int i = 0; i < n; i++) {
        int x = SET_SEG_X + i * SET_SEG_DX, y = SET_SEG_Y(row_y);
        if (i == chosen) {                       /* lit: teal, ink lettering */
            button(c, x, y, SET_SEG_W, SET_SEG_H, MSP_TEAL, MSP_TEAL, names[i], 2);
            draw_text(c, x + (SET_SEG_W - text_w(names[i], 2)) / 2, y + (SET_SEG_H - 14) / 2, 2, MSP_INK, names[i]);
        } else button(c, x, y, SET_SEG_W, SET_SEG_H, 0x1c2f36, MSP_DIM, names[i], 2);
    }
}
/* the percent a finger at page x asks of a slider running lo .. 100: the nearest step (the rail is 0 .. 100 for all) */
static int set_slider_at(float x, int lo, int step) {
    float f = (x - SET_BRT_X0) / (float)(SET_BRT_X1 - SET_BRT_X0);
    int pct = (int)lroundf(f * 100 / step) * step;
    return pct < lo ? lo : pct > 100 ? 100 : pct;
}
static int set_bright_at(float x) { return set_slider_at(x, SET_BRT_MIN, SET_BRT_STEP); }
static int set_volume_at(float x) { return set_slider_at(x, 0, SET_VOL_STEP); }
/* the slider: the rail lit (teal) up to the knob and dim past it - the lit
 * part is the brighter one, so it reads without the hue - and the knob, a
 * lit button with the percent on it in ink. VOLUME's knob at 0 says OFF and
 * goes out (an outline, teal letters): off by fill and word, not by hue. */
static void set_slider(ctx_t *c, int row_y, const char *label, int lo, int pct) {
    int y = SET_SEG_Y(row_y), cy = y + SET_SEG_H / 2;
    pct = pct < lo ? lo : pct > 100 ? 100 : pct;
    int kx = SET_BRT_X(pct);                                /* (one scale: equal percents line up) */
    draw_text(c, SET_LABEL_X, row_y, 2, MSP_TEAL, label);
    rect_fill(c, SET_SEG_X + 4, cy - 3, kx - SET_SEG_X - 4, 6, MSP_TEAL);
    rect_fill(c, kx, cy - 3, SET_SEG_X + SET_SPAN_W - 4 - kx, 6, 0x1c2f36);
    rect_edge(c, kx, cy - 3, SET_SEG_X + SET_SPAN_W - 4 - kx, 6, MSP_DIM);
    char txt[8]; snprintf(txt, sizeof txt, pct ? "%d%%" : "OFF", pct);
    bool off = pct == 0;
    button(c, kx - SET_KNOB_W / 2, y, SET_KNOB_W, SET_SEG_H, off ? 0x1c2f36 : MSP_TEAL, MSP_TEAL, "", 2);
    draw_text(c, kx - text_w(txt, 2) / 2, y + (SET_SEG_H - 14) / 2, 2, off ? MSP_TEAL : MSP_INK, txt);
}
/* an arrow button of the LIGHTS OUT row: dim at the end of the list */
static void set_arrow(ctx_t *c, int x, int y, bool right, bool live) {
    uint32_t rgb = live ? MSP_TEAL : 0x2c4a52;
    button(c, x, y, SET_ARW_W, SET_SEG_H, 0x1c2f36, rgb, "", 2);
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
#endif
/* language chip after the title, both centred (the bowl and watch clip the
 * line's ends); a tap steps to the next language (lang.h) */
static int set_lang_x(void) { return (PAGE_W + text_w("SETTINGS", 3) + 8 - SET_LANG_W) / 2; }
void render_settings(const tank_t *t, uint16_t *fb, int stride, int bright_pct, int volume) {
    ctx_t c = ctx_page(fb, stride);
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    {
        int lx = set_lang_x(), ly = SET_TITLE_Y + 10 - SET_LANG_H / 2;
        draw_text(&c, lx - 8 - text_w("SETTINGS", 3), SET_TITLE_Y, 3, 0xffffff, "SETTINGS");
        button(&c, lx, ly, SET_LANG_W, SET_LANG_H, 0x1c2f36, MSP_TEAL, "", 2);
        draw_words(&c, lx + (SET_LANG_W - words_w(lang_code(lang_now), 2)) / 2, ly + (SET_LANG_H - 14) / 2, 2, 0xffffff, lang_code(lang_now));
    }
    set_slider(&c, SET_ROW1_Y, "BRIGHTNESS", SET_BRT_MIN, bright_pct);
    set_slider(&c, SET_ROW2_Y, "VOLUME", 0, volume);
    /* LIGHTS OUT: < the choice > */
    {
        int ch = tank_light_choice(t), y = SET_SEG_Y(SET_ROW3_Y);
        int bx = SET_SEG_X + SET_ARW_W + 4, bw = SET_SPAN_W - 2 * (SET_ARW_W + 4);
        draw_text(&c, SET_LABEL_X, SET_ROW3_Y, 2, MSP_TEAL, "LIGHTS OUT");
        set_arrow(&c, SET_SEG_X, y, false, ch > 0);
        button(&c, bx, y, bw, SET_SEG_H, MSP_TEAL, MSP_TEAL, "", 2);
        draw_text(&c, bx + (bw - text_w(SET_LIGHT[ch], 2)) / 2, y + (SET_SEG_H - 14) / 2, 2, MSP_INK, SET_LIGHT[ch]);
        set_arrow(&c, SET_SEG_X + SET_SPAN_W - SET_ARW_W, y, true, ch < LIGHT_IDLE_N);
    }
    set_row(&c, SET_ROW4_Y, "AUTO FEED", SET_FEED, 2, t->autofeed_off ? 1 : 0);
#if TANK_WORN
    /* SCREEN (2026-10-02): the way up of a watch worn either way round - TURNED
       for buttons toward the elbow. The picture turns as the finger lifts. */
    set_row(&c, SET_ROW5_Y, "SCREEN", SET_SCREEN, 2, t->screen_turned ? 1 : 0);
    draw_text(&c, SET_LABEL_X, SET_NOTE5_Y, 2, MSP_QUIET, "WORN THE OTHER WAY AROUND?");
#else
    /* ROTATION (0.3.2): the picture turns over with the tank, unless locked */
    {
        int x = SET_SEG_X, y = SET_SEG_Y(SET_ROW5_Y);
        draw_text(&c, SET_LABEL_X, SET_ROW5_Y, 2, MSP_TEAL, "ROTATION");
        if (t->orient_lock) button(&c, x, y, SET_SEG_W, SET_SEG_H, MSP_TEAL, MSP_TEAL, "", 2);
        else                button(&c, x, y, SET_SEG_W, SET_SEG_H, 0x1c2f36, MSP_DIM, "", 2);
        set_lock_icon(&c, x + SET_SEG_W / 2, y + SET_SEG_H / 2 + 1, t->orient_lock, t->orient_lock ? MSP_INK : MSP_TEAL);
        draw_text(&c, SET_ROT_WORD_X, SET_ROW5_Y, 2, t->orient_lock ? 0xffffff : MSP_QUIET, t->orient_lock ? "LOCKED" : "UNLOCKED");
    }
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
#else
    draw_text_8px(&c, SET_LABEL_X + (PAGE_BOWL ? 96 : 0), PAGE_H - 8 - 6, MSP_DIM, ver);
#endif
    button(&c, SET_CLOSE_X, SET_FOOT_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", 2);
    /* UPDATES (2026-09-30, docs/OTA.md): bottom left, the same size as CLOSE */
    button(&c, SET_UPD_X, SET_FOOT_Y, SET_UPD_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "UPDATES", 2);
    /* LINK (0.4, the tank link): the glyph - two tanks, a door between - and the word */
    button(&c, SET_LINK_X, SET_LINK_Y, SET_LINK_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "", 2);
    { int gw = 20 + 8 + text_w("LINK", 2), gx = SET_LINK_X + (SET_LINK_W - gw) / 2;
      link_glyph(&c, gx, SET_LINK_Y + (MSP_CLOSE_H - 12) / 2, 0xffffff);
      draw_text(&c, gx + 28, SET_LINK_Y + (MSP_CLOSE_H - 14) / 2, 2, 0xffffff, "LINK"); }
}
static int set_segment(float x, int n) {
    if (x < SET_SEG_X - 10) return -1;
    int i = (int)((x - SET_SEG_X + 3) / SET_SEG_DX);
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}
/* the hit test: what a TAP at (x,y) means. *value: BRIGHT the percent the
 * slider's step nearest x,
 * VOLUME the percent its step nearest x (0 = OFF), FEED 1 = ON, SCREEN 1 = TURNED; ROTATE carries none (a
 * toggle); the LIGHTS OUT row's own hits are LIGHT_PREV / LIGHT_NEXT (its
 * left and right halves). */
enum { SET_HIT_LIGHT_PREV = 100, SET_HIT_LIGHT_NEXT };
int render_settings_tap(float x, float y, int *value) {
    x -= PAGE_X; y -= PAGE_Y;                    /* the page's own coordinates */
    if (x >= SET_CLOSE_X - 8 && y >= SET_FOOT_Y - 4) return SET_TAP_CLOSE;
    if (y < SET_SEG_Y(SET_ROW1_Y) - 12 && x >= set_lang_x() - 8 && x < set_lang_x() + SET_LANG_W + 8) return SET_TAP_LANG;
    if (x < SET_UPD_X + SET_UPD_W + 8 && y >= SET_FOOT_Y - 4) return SET_TAP_UPDATES;
    if (x >= SET_LINK_X - 8 && x < SET_LINK_X + SET_LINK_W + 8 && y >= SET_LINK_Y - 6 && y < SET_LINK_Y + MSP_CLOSE_H + 10) return SET_TAP_LINK;
    /* the row bands: from a little above each segment down to the next row
       (fingers report low); the last one stops at the foot's */
    int seg = set_segment(x, 3), two = set_segment(x, 2);
    if (y >= SET_SEG_Y(SET_ROW1_Y) - 12 && y < SET_SEG_Y(SET_ROW2_Y) - 12) { if (seg < 0) return SET_TAP_NONE; *value = set_bright_at(x); return SET_TAP_BRIGHT; }
    if (y >= SET_SEG_Y(SET_ROW2_Y) - 12 && y < SET_SEG_Y(SET_ROW3_Y) - 12) { if (seg < 0) return SET_TAP_NONE; *value = set_volume_at(x); return SET_TAP_VOLUME; }
    if (y >= SET_SEG_Y(SET_ROW3_Y) - 12 && y < SET_SEG_Y(SET_ROW4_Y) - 12) { if (seg < 0) return SET_TAP_NONE; *value = 0; return x < SET_SPAN_MID ? SET_HIT_LIGHT_PREV : SET_HIT_LIGHT_NEXT; }
    if (y >= SET_SEG_Y(SET_ROW4_Y) - 12 && y < SET_SEG_Y(SET_ROW5_Y) - 12) { if (two < 0) return SET_TAP_NONE; *value = two == 0; return SET_TAP_FEED; }
    if (y >= SET_SEG_Y(SET_ROW5_Y) - 12 && y < SET_FOOT_Y - 4) {
        if (two < 0) return SET_TAP_NONE;
#if TANK_WORN
        *value = two == 1; return SET_TAP_SCREEN;
#else
        *value = 0; return SET_TAP_ROTATE;       /* the button or its word: one toggle */
#endif
    }
    return SET_TAP_NONE;
}
static bool s_set_down; static float s_set_px, s_set_py; static int s_set_hit, s_set_hv;   /* render_settings_touch's press in hand */
void render_settings_cancel(void) { s_set_down = false; s_set_hit = SET_TAP_NONE; }
/* A PRESS CUT, AND A SLIDER IN IT (0.4.1 audit round 2, R2T-3's regression found by the diff review): the drag only previews; the
   release is the save. Cut mid-drag (the gift card, BOOT twice, a birth flow), the press is forgotten as render_settings_cancel
   does - and a slider in hand gives its last shown step as the setting (SET_TAP_BRIGHT / SET_TAP_VOLUME, *value): what is on the
   glass is what is kept, and the port saves it. Anything else: SET_TAP_NONE. (The BOOT chord into ADVANCED reads the same answer
   but saves nothing: its slider is the chord's own tap, and main.c puts the saved level back - touch_port_open_advanced, DR2) */
int render_settings_cut(int *value) {
    const int h = s_set_down && (s_set_hit == SET_TAP_BRIGHT || s_set_hit == SET_TAP_VOLUME) ? s_set_hit : SET_TAP_NONE;
    *value = h ? s_set_hv : 0;
    render_settings_cancel();
    return h;
}
int render_settings_touch(tank_t *t, float x, float y, bool down, int *value) {
    int r = SET_TAP_NONE; *value = 0;
    if (down && !s_set_down) {                                  /* press */
        s_set_px = x; s_set_py = y;
        s_set_hit = render_settings_tap(x, y, &s_set_hv);
        if (s_set_hit == SET_TAP_BRIGHT || s_set_hit == SET_TAP_VOLUME) {   /* a slider: the knob jumps to the finger */
            r = s_set_hit == SET_TAP_BRIGHT ? SET_TAP_BRIGHT_LIVE : SET_TAP_VOLUME_LIVE; *value = s_set_hv;
        }
    } else if (down && (s_set_hit == SET_TAP_BRIGHT || s_set_hit == SET_TAP_VOLUME)) {   /* a slider under a finger: the knob follows it along the row, */
        bool brt = s_set_hit == SET_TAP_BRIGHT;
        int v = brt ? set_bright_at(x - PAGE_X) : set_volume_at(x - PAGE_X);   /* however far up or down the finger wanders */
        if (v != s_set_hv) { s_set_hv = v; r = brt ? SET_TAP_BRIGHT_LIVE : SET_TAP_VOLUME_LIVE; *value = v; }
    } else if (!down && s_set_down && (s_set_hit == SET_TAP_BRIGHT || s_set_hit == SET_TAP_VOLUME)) {   /* let go: the last step it showed is the setting */
        r = s_set_hit; *value = s_set_hv;
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
            } else if (h == SET_TAP_LANG) {                    /* next language */
                lang_now = (uint8_t)((lang_now + 1) % LANG_N); tank_emit(TEV_WHEEL_TICK, -1); progression_settings_changed();
                r = SET_TAP_LANG; *value = lang_now;
            } else if (h == SET_TAP_CLOSE || h == SET_TAP_UPDATES || h == SET_TAP_LINK) { r = h; *value = v; }
        }
    }
    s_set_down = down;
    return r;
}

/* ---- link mode (0.4, the tank link: render.h, docs/tank-link-0.4.md) ---- */
/* the badges: a shape AND a color (Strato is color blind: the shape is the
   identity, the color a second cue) - circle, triangle, square, diamond */
static const uint32_t LINK_BADGE_RGB[LINK_BADGES] = { 0xff725c, 0x78d67d, 0x4da3ff, 0xffd166 };
static void link_badge_draw(ctx_t *c, int cx, int cy, int r, int badge, int a) {
    badge = badge < 0 ? 0 : badge % LINK_BADGES;
    uint32_t rgb = LINK_BADGE_RGB[badge];
    for (int y = -r - 1; y <= r + 1; y++)
        for (int x = -r - 1; x <= r + 1; x++) {
            float fx = x + 0.5f, fy = y + 0.5f, d;            /* d: inside < 0 < outside, in px */
            switch (badge) {
            case 0: d = sqrtf(fx * fx + fy * fy) - r; break;                                  /* circle */
            case 1: { float t = (fy + r) / (2.0f * r); d = fmaxf(fabsf(fx) - t * r * 0.98f, fabsf(fy) - r); break; }   /* triangle, point up */
            case 2: d = fmaxf(fabsf(fx), fabsf(fy)) - r * 0.86f; break;                        /* square */
            default: d = fabsf(fx) + fabsf(fy) - r * 1.12f; break;                             /* diamond */
            }
            if (d < -1.5f) px_blend(c, cx + x, cy + y, rgb, a);
            else if (d < 0.5f) px_blend(c, cx + x, cy + y, 0xffffff, a);                      /* a light rim: it reads on any water */
        }
}
void render_link_badge(uint16_t *fb, int stride, int cx, int cy, int r, int badge) {
    ctx_t c = ctx_full(fb, stride, 1.0f); link_badge_draw(&c, cx, cy, r, badge, 255);
}
/* the link glyph: two little tanks side by side, an open door between them */
static void link_glyph(ctx_t *c, int x, int y, uint32_t rgb) {
    rect_edge(c, x, y, 8, 12, rgb); rect_edge(c, x + 12, y, 8, 12, rgb);
    rect_fill(c, x + 8, y + 5, 4, 2, rgb);                              /* the way through */
}

/* where things are (frame coordinates) */
#define LINK_TAB_W    46
#define LINK_TAB_H    104
#define LINK_TAB_GAP  6                                     /* from the glass */
/* the hint and NEARBY, down by the floor (Strato, 2026-10-05: out of the fish's
   way) - below the PAGE safe box, so checked per glass: page y 284..348 is on
   the 1.8's sand, inside a 330+ px chord of the bowl's circle (the widest line
   is 312 px) and above the watch's lower corners (page 335 = frame 402) */
#define LINK_HINT_Y   284
/* the DRIFTED notice's panel (page y) and its DISCONNECT button; link mode's
   DISCONNECT chip mirrors DONE's at the top right while a door is open */
#define LINK_DRIFT_Y  70
#define LINK_DISC_W   156
#define LINK_DISC_X   ((PAGE_W - LINK_DISC_W) / 2)
#define LINK_DISC_Y   (LINK_DRIFT_Y + 122)
static int link_dchip_w(void) { return 10 + text_w("DISCONNECT", 2) + 10; }
#ifdef TANK_ROUND          /* the bowl's top is a narrow cap: DISCONNECT gets its own row, centred under DONE and the badge */
static int link_dchip_x(void) { return (TANK_W - link_dchip_w()) / 2; }
static int link_dchip_y(void) { return TOOL_CHIP_Y + TOOL_CHIP_H + 10; }
#else
static int link_dchip_x(void) { return TANK_W - TOOL_CHIP_X - link_dchip_w(); }
static int link_dchip_y(void) { return TOOL_CHIP_Y; }
#endif
static float link_mid_y(void) { return TANK_BOT * 0.5f; }
static int link_tab_x(int e) {
    float y = link_mid_y();
    return e == LINK_LEFT ? (int)tank_glass_x0(y) + LINK_TAB_GAP : (int)tank_glass_x1(y) - LINK_TAB_GAP - LINK_TAB_W;
}
static int link_tab_y(void) { return (int)link_mid_y() - LINK_TAB_H / 2; }
static int link_chip_w(void) { return 8 + 20 + 8 + text_w("DONE", 2) + 10; }

static float g_link_hold[LINK_EDGES];
static int   g_link_down = -1;                              /* what the finger went down on: an edge, 2 = DONE, -1 = nothing */
static bool  g_link_fired;
void render_link_where(int what, float *x, float *y) {
    if (what == 2) { *x = TOOL_CHIP_X + link_chip_w() / 2.0f; *y = TOOL_CHIP_Y + TOOL_CHIP_H / 2.0f; return; }
    if (what == 3) { *x = link_dchip_x() + link_dchip_w() / 2.0f; *y = link_dchip_y() + TOOL_CHIP_H / 2.0f; return; }   /* DISCONNECT in link mode */
    if (what == 4) { *x = PAGE_X + LINK_DISC_X + LINK_DISC_W / 2.0f; *y = PAGE_Y + LINK_DISC_Y + MSP_CLOSE_H / 2.0f; return; }   /* ... on the DRIFTED notice */
    *x = link_tab_x(what) + LINK_TAB_W / 2.0f; *y = link_mid_y();
}
void render_link_leave(void) { g_link_hold[0] = g_link_hold[1] = 0; g_link_down = -1; g_link_fired = false; }
void render_link_set_holds(float left, float right) { g_link_hold[LINK_LEFT] = left; g_link_hold[LINK_RIGHT] = right; }

static void link_chevron(ctx_t *c, int cx, int cy, int dir, uint32_t rgb, int a) {   /* dir -1 points left, +1 right */
    for (int i = 0; i < 9; i++) {
        int x = cx - dir * 6 + dir * i, dy = 9 - i;
        for (int k = 0; k < 4; k++) { px_blend(c, x + dir * k, cy - dy, rgb, a); px_blend(c, x + dir * k, cy + dy, rgb, a); }
    }
}
static void link_tab(ctx_t *c, const link_view_t *v, int e, float clock) {   /* clock: tank_anim's */
    const int X = link_tab_x(e), Y = link_tab_y(), W = LINK_TAB_W, H = LINK_TAB_H, dir = e == LINK_LEFT ? -1 : 1;
    const int st = v->edge[e].state;
    float pulse = 0.5f + 0.5f * sinf(clock * 5.0f);
    shade_box(c, X, Y, W, H);
    uint32_t edge_rgb = st == LV_OPEN ? LINK_BADGE_RGB[v->edge[e].badge % LINK_BADGES] : st == LV_FREE ? 0x3f6a72 : 0xffffff;
    rect_edge(c, X, Y, W, H, edge_rgb);
    if (st != LV_FREE) rect_edge(c, X + 1, Y + 1, W - 2, H - 2, edge_rgb);
    /* the hold: the tab fills from the bottom */
    float h = g_link_hold[e] < 0 ? 0 : g_link_hold[e] > 1 ? 1 : g_link_hold[e];
    if (h > 0) { int fh = (int)((H - 4) * h); for (int y = Y + H - 2 - fh; y < Y + H - 2; y++) for (int x = X + 2; x < X + W - 2; x++) px_blend(c, x, y, 0xffffff, 70); }
    const int cx = X + W / 2, cy = Y + H / 2;
    switch (st) {
    case LV_FREE:  link_chevron(c, cx, cy, dir, 0x8fb4ba, 255); break;
    case LV_ARMED: link_chevron(c, cx, cy, dir, 0xffffff, 140 + (int)(115 * pulse)); break;          /* ours, asking */
    case LV_ASKED: link_badge_draw(c, cx, cy - 22, 12, v->edge[e].badge, 255);                       /* the tank asking here */
                   link_chevron(c, cx, cy + 18, dir, LINK_BADGE_RGB[v->edge[e].badge % LINK_BADGES], 120 + (int)(135 * pulse)); break;
    case LV_TWO:   link_badge_draw(c, cx, cy - 26, 10, v->edge[e].badge, 255);                       /* two asking: both shown, nothing joins */
                   link_badge_draw(c, cx, cy, 10, v->edge[e].badge2, 255);
                   draw_text(c, cx - text_w("2", 2) / 2, cy + 20, 2, 0xffffff, "2"); break;
    case LV_OPEN:  link_badge_draw(c, cx, cy - 18, 13, v->edge[e].badge, 255);                       /* the door is open: the neighbor */
                   link_chevron(c, cx - dir * 4, cy + 22, dir, 0xffffff, 200); link_chevron(c, cx + dir * 6, cy + 22, dir, 0xffffff, 200); break;
    }
}
static void link_line(ctx_t *c, int py, const char *s, uint32_t rgb) {   /* a centred hint line at PAGE y */
    int w = text_w(s, 2), x = PAGE_X + (PAGE_W - w) / 2, y = PAGE_Y + py;
    for (int yy = y - 4; yy < y + 18; yy++) for (int xx = x - 8; xx < x + w + 8; xx++) px_blend(c, xx, yy, 0x04141a, 170);
    draw_text(c, x, y, 2, rgb, s);
}
void render_link_mode(const link_view_t *v, uint16_t *fb, int stride, double tclock) {
    const float clock = tank_anim(tclock);
    ctx_t c = ctx_full(fb, stride, 1.0f);
    /* the chip: the glyph and DONE (a tool in hand's chip) */
    const int CX = TOOL_CHIP_X, CY = TOOL_CHIP_Y, CW = link_chip_w(), CH = TOOL_CHIP_H;
    shade_box(&c, CX, CY, CW, CH);
    rect_edge(&c, CX, CY, CW, CH, 0xffffff); rect_edge(&c, CX + 1, CY + 1, CW - 2, CH - 2, 0x9fd8e2);
    link_glyph(&c, CX + 8, CY + (CH - 12) / 2, 0xffffff);
    draw_text(&c, CX + 8 + 20 + 8, CY + (CH - 14) / 2, 2, 0xffffff, "DONE");
    /* linked and nothing pending: DONE is the next step - say it with the chip
       itself (Strato 2026-10-05: "not at all intuitive that the user is supposed
       to hit DONE"): a pulsing second outline and an arrow under it, bobbing up
       at it - motion and shape, not a color */
    bool done_next = (v->edge[0].state == LV_OPEN || v->edge[1].state == LV_OPEN) &&
                     v->edge[0].state != LV_ARMED && v->edge[1].state != LV_ARMED &&
                     v->edge[0].state != LV_ASKED && v->edge[1].state != LV_ASKED &&
                     v->edge[0].state != LV_TWO && v->edge[1].state != LV_TWO;
    if (done_next) {
        /* the chip fills in - bright with dark letters, breathing between two brightnesses - and an
           arrow under it bobs up at it */
        float pulse = 0.5f + 0.5f * sinf(clock * 4.0f);
        int a = 200 + (int)(55 * pulse);
        for (int y = CY + 2; y < CY + CH - 2; y++) for (int x = CX + 2; x < CX + CW - 2; x++) px_blend(&c, x, y, 0xe8fbff, a);
        link_glyph(&c, CX + 8, CY + (CH - 12) / 2, 0x04141a);
        draw_text(&c, CX + 8 + 20 + 8, CY + (CH - 14) / 2, 2, 0x04141a, "DONE");
        int ax = CX + CW / 2, ay = CY + CH + 10 + (int)(5 * (0.5f + 0.5f * sinf(clock * 4.0f)));
        for (int i = 0; i < 13; i++) for (int k = 0; k < 5; k++) {
            px_blend(&c, ax - 13 + i, ay + 13 - i + k, 0xffffff, 235);
            px_blend(&c, ax + 13 - i, ay + 13 - i + k, 0xffffff, 235);
        }
    }
    /* DISCONNECT: top right, mirroring DONE, while a door is open */
    if (v->edge[0].state == LV_OPEN || v->edge[1].state == LV_OPEN) {
        int dx = link_dchip_x(), dy = link_dchip_y(), dw = link_dchip_w();
        shade_box(&c, dx, dy, dw, CH);
        rect_edge(&c, dx, dy, dw, CH, 0xffffff); rect_edge(&c, dx + 1, dy + 1, dw - 2, CH - 2, 0x9fd8e2);
        draw_text(&c, dx + 10, dy + (CH - 14) / 2, 2, 0xffffff, "DISCONNECT");
    }
    /* ours: the badge, top centre */
    { int bx = TANK_W / 2, by = CY + CH / 2;
      for (int y = by - 22; y < by + 22; y++) for (int x = bx - 22; x < bx + 22; x++) px_blend(&c, x, y, 0x04141a, 150);
      link_badge_draw(&c, bx, by, 16, v->badge, 255); }
    for (int e = 0; e < LINK_EDGES; e++) link_tab(&c, v, e, clock);
    /* the hint, in plain words, for what the tank needs next */
    bool open = v->edge[0].state == LV_OPEN || v->edge[1].state == LV_OPEN;
    bool asked = v->edge[0].state == LV_ASKED || v->edge[1].state == LV_ASKED;
    bool two = v->edge[0].state == LV_TWO || v->edge[1].state == LV_TWO;
    bool ours = v->edge[0].state == LV_ARMED || v->edge[1].state == LV_ARMED;
    const char *l1, *l2;
    if (two)               { l1 = "TWO TANKS ARE ASKING";         l2 = "ONLY ONE AT A TIME"; }
    else if (asked)        { l1 = "A TANK IS ASKING";             l2 = "HOLD THE GLOWING ARROW"; }
    else if (ours)         { l1 = "ASKING...";                    l2 = "HOLD THE OTHER TANK'S ARROW"; }
    else if (open)         { l1 = "LINKED! THE DOOR IS OPEN";     l2 = "TAP DONE TO WATCH THEM SWIM"; }
    else if (!v->n_near)   { l1 = "OPEN LINK ON THE OTHER TANK";  l2 = "AND PUT THEM SIDE BY SIDE"; }
    else                   { l1 = "PUT THE TANKS SIDE BY SIDE";   l2 = "HOLD THE ARROWS THAT TOUCH"; }
    link_line(&c, LINK_HINT_Y, l1, 0xffffff);
    link_line(&c, LINK_HINT_Y + 22, l2, 0x9fd8e2);
    /* NEARBY: every tank in link mode that is heard */
    { const int py = LINK_HINT_Y + 46, r = 7;
      int w = text_w("NEARBY", 2) + 10 + (v->n_near ? v->n_near * (2 * r + 8) : text_w("NONE YET", 2));
      int x = PAGE_X + (PAGE_W - w) / 2, y = PAGE_Y + py;
      for (int yy = y - 4; yy < y + 18; yy++) for (int xx = x - 8; xx < x + w + 8; xx++) px_blend(&c, xx, yy, 0x04141a, 170);
      draw_text(&c, x, y, 2, 0x9fd8e2, "NEARBY");
      x += text_w("NEARBY", 2) + 10;
      if (!v->n_near) draw_text(&c, x, y, 2, 0x8fb4ba, "NONE YET");
      for (int i = 0; i < v->n_near; i++) link_badge_draw(&c, x + r + i * (2 * r + 8), y + 7, r, v->near_badge[i], 255); }
}
int render_link_touch(const link_view_t *v, float x, float y, bool down, float dt) {
    int r = LINK_UI_NONE;
    if (down && g_link_down == -1 && !g_link_fired) {                      /* the press: what is under it */
        float my = link_mid_y();
        bool open = v->edge[0].state == LV_OPEN || v->edge[1].state == LV_OPEN;
        if (x < TOOL_CHIP_X + link_chip_w() + 16 && y < TOOL_CHIP_Y + TOOL_CHIP_H + 24) g_link_down = 2;
        else if (open && x >= link_dchip_x() - 16 && x < link_dchip_x() + link_dchip_w() + 16 &&
                 y >= link_dchip_y() - 16 && y < link_dchip_y() + TOOL_CHIP_H + 16) g_link_down = 4;   /* DISCONNECT */
        else if (fabsf(y - my) < LINK_TAB_H / 2 + 30 && x < link_tab_x(LINK_LEFT) + LINK_TAB_W + 40) g_link_down = LINK_LEFT;
        else if (fabsf(y - my) < LINK_TAB_H / 2 + 30 && x > link_tab_x(LINK_RIGHT) - 40) g_link_down = LINK_RIGHT;
        else g_link_down = 3;                                              /* the water: nothing (a tool owns the glass) */
    }
    if (down && (g_link_down == LINK_LEFT || g_link_down == LINK_RIGHT) && !g_link_fired) {
        int e = g_link_down;
        bool open = v->edge[e].state == LV_OPEN;
        g_link_hold[e] += dt / (open ? LINK_CLOSE_HOLD_S : LINK_ARM_HOLD_S);
        if (g_link_hold[e] >= 1) {
            g_link_fired = true; g_link_hold[e] = 0;
            r = open ? (e == LINK_LEFT ? LINK_UI_CLOSE_LEFT : LINK_UI_CLOSE_RIGHT) : (e == LINK_LEFT ? LINK_UI_ARM_LEFT : LINK_UI_ARM_RIGHT);
        }
    }
    if (!down) {
        if (g_link_down == 2) r = LINK_UI_DONE;                             /* DONE: on the release */
        if (g_link_down == 4) r = LINK_UI_DISCONNECT;
        g_link_hold[0] = g_link_hold[1] = 0; g_link_down = -1; g_link_fired = false;
    }
    return r;
}
/* an open door outside link mode: a soft glow down the side glass in the
   neighbor's color and its badge, small (2026-10-05, Strato: a watch was
   linked but showed nothing - only the DRIFTED notice when it moved). The
   doorway proper comes with the doors, phase 4. */
#define LINK_DOOR_H   (TANK_BOT * 2 / 5)                    /* the door band: the middle of the water */
void render_link_doors(const link_view_t *v, uint16_t *fb, int stride, double tclock) {
    const float clock = tank_anim(tclock);
    ctx_t c = ctx_full(fb, stride, 1.0f);
    float my = link_mid_y(), pulse = 0.75f + 0.25f * sinf(clock * 1.6f);
    for (int e = 0; e < LINK_EDGES; e++) {
        if (v->edge[e].state != LV_OPEN) continue;
        uint32_t rgb = LINK_BADGE_RGB[v->edge[e].badge % LINK_BADGES];
        for (int y = (int)my - LINK_DOOR_H / 2; y < (int)my + LINK_DOOR_H / 2; y++) {
            float gx = e == LINK_LEFT ? tank_glass_x0((float)y) : tank_glass_x1((float)y);
            float ty = fabsf(y - my) / (LINK_DOOR_H / 2.0f);          /* fades toward the band's ends */
            int a0 = (int)(190 * pulse * (1 - ty * ty));
            for (int k = 0; k < 12; k++) {
                int x = e == LINK_LEFT ? (int)gx + k : (int)gx - 1 - k;
                px_blend(&c, x, y, rgb, a0 * (12 - k) / 12);
            }
        }
        int bx = e == LINK_LEFT ? (int)tank_glass_x0(my) + 24 : (int)tank_glass_x1(my) - 24;
        link_badge_draw(&c, bx, (int)my, 9, v->edge[e].badge, 230);
    }
}

/* the DRIFTED notice (link.h LEV_DRIFT): centred, plain words, the grace as a
   bar that empties; over the tank whether link mode is up or not */
void render_link_drift(const link_view_t *v, uint16_t *fb, int stride, double clock) {
    (void)clock;
    if (!v->drifting) return;
    ctx_t c = ctx_full(fb, stride, 1.0f);
    const int W = 336, H = 164, X = PAGE_X + (PAGE_W - W) / 2, Y = PAGE_Y + LINK_DRIFT_Y;
    shade_box(&c, X, Y, W, H);
    rect_edge(&c, X, Y, W, H, 0xffffff); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x9fd8e2);
    static const char *const GRACE[4] = { "THE TANKS DRIFTED", "PUT THEM BACK SIDE BY SIDE", "AND KEEP THEM STILL", "OR THE FISH WILL SWIM HOME" };
    static const char *const HOME[4]  = { "THE TANKS DRIFTED", "THE FISH ARE SWIMMING HOME", "THEN THE DOOR CLOSES", "" };   /* past the grace */
    const char *const *L = v->drift_homeward ? HOME : GRACE;
    for (int i = 0; i < 4; i++) draw_text(&c, X + (W - text_w(L[i], 2)) / 2, Y + 12 + i * 22, 2, i ? 0x9fd8e2 : 0xffffff, L[i]);
    float left = v->drift_ms_left / (float)(v->drift_homeward ? LINK_HOMEWARD_MS : LINK_DRIFT_GRACE_MS); left = left < 0 ? 0 : left > 1 ? 1 : left;
    rect_edge(&c, X + 24, Y + 104, W - 48, 8, 0x3f6a72);
    rect_fill(&c, X + 26, Y + 106, (int)((W - 52) * left), 4, 0xffffff);
    /* DISCONNECT (Strato, 2026-10-05: "an easy way to unlink"): every door of this tank shuts now */
    button(&c, PAGE_X + LINK_DISC_X, PAGE_Y + LINK_DISC_Y, LINK_DISC_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "DISCONNECT", 2);
}
/* the DRIFTED notice's DISCONNECT answers a tap only where the notice is seen (the 11-02 sweep: its hit ran under every
   page and card - a neighbor lifting their tank and a tap mid-page closed every door and ate the tap): no page up, and no
   milestone notice, GIVE modal or gift card drawn over it */
bool render_drift_live(const tank_t *t, bool page_up) {
    const gift_book_t *g = gift_of(t);
    return !page_up && !notice_current() && gm_eval(t) == RENDER_GM_OFF && !(g && g->ask.on);
}
bool render_link_drift_hit(const link_view_t *v, float x, float y) {
    if (!v->drifting) return false;
    x -= PAGE_X; y -= PAGE_Y;
    return x >= LINK_DISC_X - 10 && x < LINK_DISC_X + LINK_DISC_W + 10 && y >= LINK_DISC_Y - 8 && y < LINK_DISC_Y + MSP_CLOSE_H + 12;
}
/* ---- THE ADVANCED PAGE (0.3.9.2) ---------------------------------------
 * Hidden behind a BOOT chord (common/advanced.c). Two rows on the settings
 * page's grid: TOUCH (one wide CALIBRATE button) and BATTERY (the size
 * between two arrows, as LIGHTS OUT steps its choice), a quiet note under
 * each, BACK where CLOSE stands; FOR GROWN-UPS small under the title. */
#define ADV_WARN_RGB  0xffc94a          /* the warning's triangle (its shape says it; the hue only helps) */
#define ADV_BAT_ROW   (SET_ROW1_Y + 108)                          /* the BATTERY row's label line */
#define ADV_SET_Y     (SET_SEG_Y(ADV_BAT_ROW) + SET_SEG_H + 34)   /* SET <size>, under the row's note */
#define ADV_SET_H     36
#define ADV_WARN_BOX_Y (SET_TITLE_Y + 202)                        /* the warning's box to tick: 20 px under READ THE SIZE PRINTED ON IT. */
#define ADV_WARN_BOX   30
#define ADV_CONT_W     128
#define ADV_CONT_X     (SET_CLOSE_X + MSP_CLOSE_W - ADV_CONT_W > SET_UPD_X + SET_UPD_W + 10 ? SET_CLOSE_X + MSP_CLOSE_W - ADV_CONT_W : SET_UPD_X + SET_UPD_W + 10)
#define ADV_START_Y    (SET_TITLE_Y + 164)                        /* the touch page's START */
#define ADV_START_W    140
#define ADV_RESET_Y    (ADV_START_Y + SET_SEG_H + 16)             /* ... RESET TOUCH under it */
#define ADV_RESET_W    160
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
/* SWAP IN, tapped (0.3.9.4, Strato: "some feedback on screen immediately when 'swap in' is tapped"): the button
   pulses toward white - its light, not its hue - and says SWAPPING while the platform fades the glass out; the save,
   the move and the restart happen in the dark (render_shelf_card_swapping: seconds since the tap, < 0 off) */
static float g_shc_swap = -1;
static uint32_t sh_mix(uint32_t a, uint32_t b, int t);   /* below */
void render_shelf_card_swapping(float t) { g_shc_swap = t; }
static void swap_button(ctx_t *c, int x, int y, int w, int h, float t) {
    float a = 0.5f - 0.5f * cosf(t * 6.2831853f / SWAP_PULSE_S);   /* 0 -> 1 -> 0 each pulse */
    uint32_t fill = sh_mix(MSP_TEAL, 0xf2fbfc, (int)(a * 200));
    button(c, x, y, w, h, fill, fill, "", 2);
    rect_edge(c, x - 3, y - 3, w + 6, h + 6, sh_mix(0x0a2a36, 0xf2fbfc, (int)(a * 220)));   /* (a halo that breathes with it) */
    draw_text(c, x + (w - text_w("SWAPPING", 2)) / 2, y + (h - 14) / 2, 2, MSP_INK, "SWAPPING");
}
/* the whole frame at light/256 of its light (the swap's fade to dark) */
void render_fade(uint16_t *fb, int stride, int light) {
    if (light >= 256) return;
    if (light < 0) light = 0;
    for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) {
        uint16_t q = FB_SWAP16(fb[y * stride + x]);
        int r = (((q >> 11) & 31) * light) >> 8, g = (((q >> 5) & 63) * light) >> 8, b = ((q & 31) * light) >> 8;
        fb[y * stride + x] = FB_SWAP16((uint16_t)((r << 11) | (g << 5) | b));
    }
}
static void lit_button(ctx_t *c, int x, int y, int w, int h, const char *label) {   /* teal, ink lettering */
    button(c, x, y, w, h, MSP_TEAL, MSP_TEAL, "", 2);
    draw_text(c, x + (w - text_w(label, 2)) / 2, y + (h - 14) / 2, 2, MSP_INK, label);
}
void render_advanced(uint16_t *fb, int stride, int pick_mah, int pick_ma, int set_mah, bool stock, bool fast) {
    ctx_t c = ctx_page(fb, stride);
    char txt[40];
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    text_centre(&c, SET_TITLE_Y, 3, 0xffffff, "ADVANCED");
    const char *grown = lang_tr("FOR GROWN-UPS");
    draw_text_8px(&c, (PAGE_W - ((int)strlen(grown) * 6 - 1)) / 2, SET_TITLE_Y + 25, MSP_QUIET, grown);
    /* TOUCH */
    draw_text(&c, SET_LABEL_X, SET_ROW1_Y, 2, MSP_TEAL, "TOUCH");
    button(&c, SET_SEG_X, SET_SEG_Y(SET_ROW1_Y), SET_SPAN_W, SET_SEG_H, 0x1c2f36, MSP_TEAL, "CALIBRATE", 2);
    draw_text(&c, SET_LABEL_X, SET_SEG_Y(SET_ROW1_Y) + SET_SEG_H + 10, 2, MSP_QUIET, "TAPS MISSING THEIR MARK?");
    /* BATTERY: < 800 MAH > */
    int y = SET_SEG_Y(ADV_BAT_ROW);
    int bx = SET_SEG_X + SET_ARW_W + 4, bw = SET_SPAN_W - 2 * (SET_ARW_W + 4);
    const int *sz; int n = adv_cell_sizes(&sz);
    draw_text(&c, SET_LABEL_X, ADV_BAT_ROW, 2, MSP_TEAL, "BATTERY");
    set_arrow(&c, SET_SEG_X, y, false, pick_mah > sz[0]);
    button(&c, bx, y, bw, SET_SEG_H, MSP_TEAL, MSP_TEAL, "", 2);
    snprintf(txt, sizeof txt, "%d MAH", pick_mah);
    int tw = text_w(txt, 2), sx = bx + (bw - tw - (fast ? 22 : 0)) / 2;
    if (fast) adv_warn_sign(&c, sx + 8, y + (SET_SEG_H - 15) / 2, 17, MSP_INK);   /* faster than the board's own: the sign rides along */
    draw_text(&c, sx + (fast ? 22 : 0), y + (SET_SEG_H - 14) / 2, 2, MSP_INK, txt);
    set_arrow(&c, SET_SEG_X + SET_SPAN_W - SET_ARW_W, y, true, pick_mah < sz[n - 1]);
    if (stock) snprintf(txt, sizeof txt, lang_tr("STANDARD - CHARGES AT %d MA"), pick_ma);
    else       snprintf(txt, sizeof txt, lang_tr("CHARGES AT %d MA"), pick_ma);
    draw_text(&c, SET_LABEL_X, y + SET_SEG_H + 10, 2, MSP_QUIET, txt);
    if (pick_mah != set_mah) {                    /* a size picked, not in use yet: SET applies it (the warning first, when it is faster) */
        snprintf(txt, sizeof txt, lang_tr("SET %d MAH"), pick_mah);
        lit_button(&c, SET_SEG_X, ADV_SET_Y, SET_SPAN_W, ADV_SET_H, txt);
        snprintf(txt, sizeof txt, lang_tr("NOW %d MAH"), set_mah);
        draw_text(&c, SET_LABEL_X, ADV_SET_Y + (ADV_SET_H - 14) / 2, 2, MSP_QUIET, txt);
    } else draw_text(&c, SET_LABEL_X, y + SET_SEG_H + 32, 2, MSP_QUIET, "MATCH THE SIZE ON YOUR BATTERY");
    button(&c, SET_CLOSE_X, SET_FOOT_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "BACK", 2);
}
int render_advanced_tap(float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;
    if (x >= SET_CLOSE_X - 8 && y >= SET_FOOT_Y - 4) return ADV_HIT_BACK;
    if (x < SET_SEG_X - 10) return ADV_HIT_NONE;
    if (y >= SET_SEG_Y(SET_ROW1_Y) - 12 && y < SET_SEG_Y(SET_ROW1_Y) + SET_SEG_H + 12) return ADV_HIT_CALIBRATE;
    if (y >= SET_SEG_Y(ADV_BAT_ROW) - 12 && y < SET_SEG_Y(ADV_BAT_ROW) + SET_SEG_H + 12)
        return x < SET_SPAN_MID ? ADV_HIT_PREV : ADV_HIT_NEXT;                         /* the row's halves, as LIGHTS OUT's */
    if (y >= ADV_SET_Y - 8 && y < ADV_SET_Y + ADV_SET_H + 12) return ADV_HIT_SET;       /* (only while it is drawn: advanced.c asks) */
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
        int sw = 36, gap = 12, tw = text_w("WARNING", 3), x0 = (PAGE_W - sw - gap - tw) / 2;
        adv_warn_sign(&c, x0 + sw / 2, SET_TITLE_Y - 6, sw, ADV_WARN_RGB);
        draw_text(&c, x0 + sw + gap, SET_TITLE_Y, 3, 0xffffff, "WARNING");
    }
    int y = SET_TITLE_Y + 42;
    text_centre(&c, y, 2, 0xffffff, "ONLY FOR A BIGGER BATTERY."); y += 30;
    snprintf(txt, sizeof txt, lang_tr("THE TANK WILL CHARGE AT %d MA."), charge_ma);
    text_centre(&c, y, 2, MSP_TEAL, txt); y += 22;
    text_centre(&c, y, 2, MSP_TEAL, "A SMALLER BATTERY CHARGED THIS"); y += 22;
    text_centre(&c, y, 2, MSP_TEAL, "FAST CAN GET HOT, SWELL UP"); y += 22;
    text_centre(&c, y, 2, MSP_TEAL, "OR CATCH FIRE."); y += 30;
    text_centre(&c, y, 2, 0xffffff, "READ THE SIZE PRINTED ON IT.");
    {   /* the box: [x]  MY BATTERY IS 800 MAH / OR BIGGER */
        snprintf(txt, sizeof txt, lang_tr("MY BATTERY IS %d MAH"), cell_mah);
        int bs = ADV_WARN_BOX, gap = 14, tw = text_w(txt, 2), x0 = (PAGE_W - bs - gap - tw) / 2, by = ADV_WARN_BOX_Y;
        rect_fill(&c, x0, by, bs, bs, checked ? MSP_TEAL : 0x1c2f36);
        rect_edge(&c, x0, by, bs, bs, MSP_TEAL); rect_edge(&c, x0 + 1, by + 1, bs - 2, bs - 2, MSP_TEAL);
        if (checked)                                         /* the tick: a short stroke down, a long one up */
            for (int k = 0; k < 18; k++) {
                int px = x0 + 6 + k, py = k < 6 ? by + 14 + k : by + 20 - (k - 6) * 13 / 12;
                rect_fill(&c, px, py - 1, 2, 4, MSP_INK);
            }
        draw_text(&c, x0 + bs + gap, by, 2, 0xffffff, txt);
        draw_text(&c, x0 + bs + gap, by + 20, 2, 0xffffff, "OR BIGGER");
    }
    button(&c, SET_UPD_X, SET_FOOT_Y, SET_UPD_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CANCEL", 2);
    if (checked) lit_button(&c, ADV_CONT_X, SET_FOOT_Y, ADV_CONT_W, MSP_CLOSE_H, "CONTINUE");
    else {
        rect_edge(&c, ADV_CONT_X, SET_FOOT_Y, ADV_CONT_W, MSP_CLOSE_H, MSP_DIM);
        draw_text(&c, ADV_CONT_X + (ADV_CONT_W - text_w("CONTINUE", 2)) / 2, SET_FOOT_Y + (MSP_CLOSE_H - 14) / 2, 2, MSP_DIM, "CONTINUE");
    }
}
int render_battery_warning_tap(float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;
    if (y >= SET_FOOT_Y - 8) {
        if (x >= SET_UPD_X - 8 && x < SET_UPD_X + SET_UPD_W + 4) return ADV_HIT_CANCEL;
        if (x >= ADV_CONT_X - 4 && x < ADV_CONT_X + ADV_CONT_W + 8) return ADV_HIT_CONTINUE;
        return ADV_HIT_NONE;
    }
    if (y >= ADV_WARN_BOX_Y - 12 && y < ADV_WARN_BOX_Y + 46) return ADV_HIT_BOX;    /* the box or its words: the whole line */
    return ADV_HIT_NONE;
}
/* touch calibration's pages */
void render_touch_intro(uint16_t *fb, int stride, bool own_in_use, const char *msg) {
    ctx_t c = ctx_page(fb, stride);
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    text_centre(&c, SET_TITLE_Y, 3, 0xffffff, "TOUCH");
    int y = SET_TITLE_Y + 46;
    text_centre(&c, y, 2, MSP_TEAL, "HOLD THE TANK THE WAY"); y += 22;
    text_centre(&c, y, 2, MSP_TEAL, "YOU USUALLY DO."); y += 34;
    text_centre(&c, y, 2, MSP_TEAL, "NINE CROSSES COME UP."); y += 22;
    text_centre(&c, y, 2, MSP_TEAL, "TAP THE MIDDLE OF EACH ONE.");
    lit_button(&c, (PAGE_W - ADV_START_W) / 2, ADV_START_Y, ADV_START_W, SET_SEG_H, "START");
    if (own_in_use) button(&c, (PAGE_W - ADV_RESET_W) / 2, ADV_RESET_Y, ADV_RESET_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "RESET TOUCH", 2);
    if (msg) text_centre(&c, ADV_RESET_Y + MSP_CLOSE_H + 10, 2, 0xffffff, msg);
    button(&c, SET_CLOSE_X, SET_FOOT_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "BACK", 2);
}
int render_touch_intro_tap(float x, float y, bool own_in_use) {
    x -= PAGE_X; y -= PAGE_Y;
    if (x >= SET_CLOSE_X - 8 && y >= SET_FOOT_Y - 4) return ADV_HIT_BACK;
    if (fabsf(x - PAGE_W / 2) < ADV_START_W / 2 + 12 && y >= ADV_START_Y - 12 && y < ADV_START_Y + SET_SEG_H + 6) return ADV_HIT_START;
    if (own_in_use && fabsf(x - PAGE_W / 2) < ADV_RESET_W / 2 + 12 && y >= ADV_RESET_Y - 6 && y < ADV_RESET_Y + MSP_CLOSE_H + 12) return ADV_HIT_RESET;
    return ADV_HIT_NONE;
}
static void touch_cross(ctx_t *c, int fx, int fy, uint32_t rgb) {      /* a cross at FRAME px */
    int x = fx - PAGE_X, y = fy - PAGE_Y;
    rect_fill(c, x - 24, y - 1, 49, 3, rgb); rect_fill(c, x - 1, y - 24, 3, 49, rgb);
    rect_fill(c, x - 4, y - 4, 9, 9, 0xffffff);
}
void render_touch_cross(uint16_t *fb, int stride, int i, int n, int tx, int ty) {
    ctx_t c = ctx_page(fb, stride);
    char txt[32];
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    touch_cross(&c, tx, ty, MSP_TEAL);
    snprintf(txt, sizeof txt, lang_tr("TAP THE CROSS  %d OF %d"), i + 1, n);
    text_centre(&c, ty - PAGE_Y < PAGE_H / 2 ? PAGE_H - 60 : 46, 2, 0xffffff, txt);
}
void render_touch_check(uint16_t *fb, int stride, int tx, int ty, float secs_left, float secs_total) {
    ctx_t c = ctx_page(fb, stride);
    char txt[40];
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    touch_cross(&c, tx, ty, MSP_TEAL);
    int y = ty - PAGE_Y < PAGE_H / 2 ? PAGE_H - 96 : 30;
    text_centre(&c, y, 2, 0xffffff, "TAP THIS CROSS TO KEEP"); y += 22;
    text_centre(&c, y, 2, 0xffffff, "THE NEW TOUCH"); y += 28;
    int s = (int)ceilf(secs_left < 0 ? 0 : secs_left);
    snprintf(txt, sizeof txt, lang_tr("THE OLD ONE COMES BACK IN %d"), s);
    text_centre(&c, y, 2, MSP_QUIET, txt); y += 24;
    int bw = 240, bx = (PAGE_W - bw) / 2;                    /* the time left, a bar that shrinks */
    float f = secs_total > 0 ? secs_left / secs_total : 0; if (f < 0) f = 0;
    rect_edge(&c, bx, y, bw, 8, MSP_DIM);
    rect_fill(&c, bx + 2, y + 2, (int)((bw - 4) * f), 4, MSP_TEAL);
}

/* ---- MY TANKS + THE LIBRARY: the tank switcher (docs/tank-switcher.md) ----
 * ART FOR APPROVAL, 2026-10-29. Two wooden shelves of four little tanks on
 * a dark wall, each tank holding the snapshot taken when it was shelved.
 * The tank on the glass is lit, with a NOW tag over its lid; the others are
 * dimmed and wear a sign of how they rest - zZ dormant, two bars paused:
 * shapes, never hue alone. An empty slot is a dashed outline and a +. The
 * frame's own coordinates (not the 448 x 368 page): the shelves are as wide
 * as each glass lets them be (BOARDS.md rule 5). */
#if PAGE_BOWL                    /* the bowl: a square picture from the circle's middle; the footer on two lines */
#define SH_TW      80
#define SH_IH      80
#define SH_GAP     10
#define SH_TITLE_Y 32
#define SH_ROW1_Y  80
#define SH_PAGER_Y 362
#define SH_FOOT_Y  404
#define SH_LIB_X0  64            /* THE LIBRARY's rows, in from the circle */
#elif defined(TANK_WATCH)        /* portrait little tanks; the footer on two lines, clear of the 100 px corners */
#define SH_TW      84
#define SH_IH      103
#define SH_GAP     10
#define SH_TITLE_Y 30
#define SH_ROW1_Y  76
#define SH_PAGER_Y 414
#define SH_FOOT_Y  452
#define SH_LIB_X0  24
#elif defined(TANK_SQUARE)
#define SH_TW      96
#define SH_IH      96
#define SH_GAP     14
#define SH_TITLE_Y 20
#define SH_ROW1_Y  80
#define SH_FOOT_Y  416
#define SH_LIB_X0  28
#else                            /* the 1.8 */
#define SH_TW      88
#define SH_IH      72
#define SH_GAP     14
#define SH_TITLE_Y 10
#define SH_ROW1_Y  54
#define SH_FOOT_Y  326
#define SH_LIB_X0  24
#endif
#define SH_LID      5
#define SH_BASE     4
#define SH_PIC_W    (SH_TW - 4)                          /* the picture inside the glass's two sides */
#define SH_BOX_H    (SH_LID + SH_IH + SH_BASE)
#define SH_TOP      7                                    /* the plank's top, seen from a little above */
#define SH_FACE     36                                   /* its front: the name plates (two lines of scale 2 - Strato: the 6 px names "barely visible") */
#define SH_SHELF_H  (SH_BOX_H + SH_TOP + SH_FACE)
#if defined(TANK_SQUARE) || defined(TANK_WATCH)
#define SH_ROW_GAP  26                                   /* a plank's foot to the next row's lid: room for a NOW tag */
#else
#define SH_ROW_GAP  22                                   /* (the 1.8 and the bowl: as tight as the tag allows) */
#endif
#define SH_ROW_W    (4 * SH_TW + 3 * SH_GAP)
#define SH_X0       ((TANK_W - SH_ROW_W) / 2)
#define SH_OVER     12                                   /* the plank runs this far past the end tanks */
#define SH_BTN_W    116
#define SH_ARW_W    36
#define SH_WALL_TOP 0x241b16
#define SH_WALL_BOT 0x0d0a09
#define SH_BRASS    0xd2b878
static ctx_t ctx_frame(uint16_t *fb, int stride) {
    CTX_MADE();
    ctx_t c = { fb, stride, 1.0f, 0, 0, TANK_W, TANK_H };
    g_dirty_hold = true;
    return c;
}
static uint32_t sh_mix(uint32_t a, uint32_t b, int t) {   /* t 0..256 from a to b */
    int r = ((a >> 16) & 255) + ((((int)(b >> 16) & 255) - (int)((a >> 16) & 255)) * t >> 8);
    int g = ((a >> 8) & 255) + ((((int)(b >> 8) & 255) - (int)((a >> 8) & 255)) * t >> 8);
    int l = (a & 255) + ((((int)b & 255) - (int)(a & 255)) * t >> 8);
    return (uint32_t)(r << 16 | g << 8 | l);
}
static uint32_t sh_shade(uint32_t rgb, int pct) {          /* 100 = as is */
    int r = ((rgb >> 16) & 255) * pct / 100, g = ((rgb >> 8) & 255) * pct / 100, b = (rgb & 255) * pct / 100;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (uint32_t)(r << 16 | g << 8 | b);
}
static void sh_wall(ctx_t *c) {
    for (int y = 0; y < TANK_H; y++) rect_fill(c, 0, y, TANK_W, 1, sh_mix(SH_WALL_TOP, SH_WALL_BOT, y * 256 / TANK_H));
}
/* the wood: long streaks that wander, a little noise, darker end grain */
static inline float sh_sin(float a) {                     /* sinf to ~1e-4, without libm's range reduction (the wood's first draw, 0.3.9.3) */
    a -= 6.2831853f * floorf(a * 0.15915494f + 0.5f);       /* to -pi .. pi */
    if (a > 1.5707963f) a = 3.1415927f - a; else if (a < -1.5707963f) a = -3.1415927f - a;   /* to -pi/2 .. pi/2 */
    float a2 = a * a;
    return a * (1.0f + a2 * (-0.16666667f + a2 * (0.0083333310f + a2 * -0.00019840874f)));
}
static int sh_grain(int x, int y) {
    float g = sh_sin(x * 0.035f + sh_sin(x * 0.009f + y * 0.55f) * 2.4f + y * 1.15f);
    return (int)(g * 9) + (int)(chash(x >> 2, y) % 5) - 2;
}
static uint16_t *g_sh_strip[2]; static int g_sh_sx[2], g_sh_sy[2], g_sh_sw[2];   /* the planks' wood (sh_plank), while MY TANKS is open */
void render_shelf_free(void) {                            /* MY TANKS closed: the wood's memory back (0.4, the 11-02 evening sweep) */
    for (int k = 0; k < 2; k++) { free(g_sh_strip[k]); g_sh_strip[k] = NULL; }
}
static void sh_plank(ctx_t *c, int x, int y, int w) {     /* y = the top's back edge */
    for (int k = 1; k <= 10; k++)                          /* its shadow on the wall */
        rect_fill(c, x + 4, y + SH_TOP + SH_FACE + k - 1, w - 8, 1, sh_shade(sh_mix(SH_WALL_TOP, SH_WALL_BOT, (y + SH_TOP + SH_FACE + k) * 256 / TANK_H), 40 + k * 6));
    for (int b = 0; b < 2; b++) {                          /* two iron brackets under it */
        int bx = b ? x + w - 34 : x + 22;
        for (int dy = 0; dy < 14; dy++) rect_fill(c, bx, y + SH_TOP + SH_FACE + dy, 14 - dy, 1, dy == 0 ? 0x4a3c32 : 0x2e2620);
    }
    /* the wood (0.3.9.3, Strato: a tap on the shelves lagged "long enough to make it feel like it was a miss-tap"):
       its grain is two sinf a pixel - 340-470 ms a redraw on the 1.8, 730-960 on the square, every tap - so each plank's
       wood is worked out ONCE into a strip (heap: PSRAM on the board) and copied in after that */
    uint16_t **strip = g_sh_strip; int *sx = g_sh_sx, *sy = g_sh_sy, *sw = g_sh_sw;
    const int H = SH_TOP + SH_FACE;
    int slot = -1;
    for (int k = 0; k < 2 && slot < 0; k++) if (strip[k] && sx[k] == x && sy[k] == y && sw[k] == w) slot = k;
    if (slot < 0) for (int k = 0; k < 2 && slot < 0; k++) if (!strip[k]) {
        strip[k] = malloc((size_t)w * H * sizeof(uint16_t));
        if (!strip[k]) break;
        sx[k] = x; sy[k] = y; sw[k] = w; slot = k;
        for (int yy = 0; yy < H; yy++) {
            bool top = yy < SH_TOP;
            uint32_t base = top ? 0xb98652 : 0x8d5b31;
            for (int xx = 0; xx < w; xx++) {
                int pct = 100 + sh_grain(x + xx, y + yy);
                if (xx < 2 || xx >= w - 2) pct -= 22;      /* the ends */
                if (top) pct += yy - 3;                    /* lighter toward the front lip */
                strip[k][yy * w + xx] = src_color(sh_shade(base, pct), 1.0f).v;
            }
        }
    }
    for (int yy = 0; yy < H; yy++)
        for (int xx = 0; xx < w; xx++) {
            uint16_t v;
            if (slot >= 0) v = strip[slot][yy * w + xx];
            else { int pct = 100 + sh_grain(x + xx, y + yy) - (xx < 2 || xx >= w - 2 ? 22 : 0) + (yy < SH_TOP ? yy - 3 : 0);   /* (no memory: as before) */
                   v = src_color(sh_shade(yy < SH_TOP ? 0xb98652 : 0x8d5b31, pct), 1.0f).v; }
            if (CTX_IN(c, x + xx, y + yy)) CTX_PX(c, x + xx, y + yy) = FB_SWAP16(v);
        }
    rect_fill(c, x, y + SH_TOP - 1, w, 1, 0xdcab74);       /* the lip catches the light */
    rect_fill(c, x, y + SH_TOP + SH_FACE - 1, w, 1, 0x3e2613);
}
static void sh_flask(ctx_t *c, int x, int y, uint32_t rgb) {   /* 7 x 8: EXPERIMENTAL's sign */
    rect_fill(c, x + 2, y, 3, 1, rgb); rect_fill(c, x + 3, y + 1, 1, 2, rgb);
    for (int r = 0; r < 5; r++) rect_fill(c, x + 3 - (r + 1) / 2, y + 3 + r, 1 + 2 * ((r + 1) / 2), 1, rgb);
}
/* a name plate: scale 2 letters (0.3.9.3, Strato: the 6 px ones were "barely visible"), on two lines when the name is
   wider than its tank's share of the shelf - broken at the space nearest the middle, or where the line is full */
static void sh_plate(ctx_t *c, int cx, int y, const char *name) {
    const int maxw = SH_TW + SH_GAP - 8, fit = (maxw + 2) / 12;    /* (a plate keeps 4 px from the next - seven letters on every glass; letters a line holds) */
    char l1[16] = "", l2[16] = "";
    int n = (int)strlen(name); if (n > 15) n = 15;
    const char *nl = strchr(name, '\n');
    if (nl) { int a = (int)(nl - name); memcpy(l1, name, (size_t)(a < 15 ? a : 15)); snprintf(l2, sizeof l2, "%s", nl + 1); }   /* (the keeper's two lines) */
    else if (words_w(name, 2) <= maxw) memcpy(l1, name, (size_t)n);
    else {
        int cut = -1;                                      /* at a space, the nearest the middle that leaves both lines whole */
        for (int i = 1; i < n; i++) if (name[i] == ' ' && i <= fit && n - i - 1 <= fit && (cut < 0 || abs(i - n / 2) < abs(cut - n / 2))) cut = i;
        if (cut > 0) { memcpy(l1, name, (size_t)cut); memcpy(l2, name + cut + 1, (size_t)(n - cut - 1)); }
        else { int k = (n + 1) / 2; if (k > fit) k = fit; memcpy(l1, name, (size_t)k); memcpy(l2, name + k, (size_t)(n - k > fit ? fit : n - k)); }   /* (one long word: halves) */
    }
    int tw = words_w(l1, 2) > words_w(l2, 2) ? words_w(l1, 2) : words_w(l2, 2);   /* (a name: untranslated) */
    int w = tw + 4, h = l2[0] ? 34 : 18;
    if (w < 30) w = 30;
    int x = cx - w / 2;
    rect_fill(c, x, y, w, h, SH_BRASS);
    rect_edge(c, x, y, w, h, 0x7a6435);
    draw_words(c, x + 2 + (tw - words_w(l1, 2)) / 2, y + 2, 2, 0x2c2412, l1);
    if (l2[0]) draw_words(c, x + 2 + (tw - words_w(l2, 2)) / 2, y + 18, 2, 0x2c2412, l2);   /* (2 px between the lines - Strato: they touched) */
}
static void sh_pic(ctx_t *c, int x, int y, const uint16_t *pic, int w, int h, bool dim) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            uint16_t v = pic[j * w + i];
            if (dim) {                                     /* asleep on the shelf: half the light, a cool cast */
                int r = (v >> 11) * 9 / 20, g = ((v >> 5) & 63) * 9 / 20, b = (v & 31) * 11 / 20 + 1;
                v = (uint16_t)(r << 11 | g << 5 | (b > 31 ? 31 : b));
            }
            px(c, x + i, y + j, v);
        }
}
/* a little tank: lid, the picture between two glass sides, the base; a soft streak of reflection */
static void sh_tank(ctx_t *c, int x, int y, int w, int ih, const uint16_t *pic, bool dim) {
    rect_fill(c, x, y, w, SH_LID, 0x1b1f22);
    rect_fill(c, x, y, w, 1, 0x4d575c);
    if (pic) sh_pic(c, x + 2, y + SH_LID, pic, w - 4, ih, dim);
    else rect_fill(c, x + 2, y + SH_LID, w - 4, ih, 0x05080a);
    uint32_t side = dim ? 0x4e6268 : 0x9cc2ca;
    rect_fill(c, x, y + SH_LID, 2, ih, side); rect_fill(c, x + w - 2, y + SH_LID, 2, ih, side);
    for (int j = 0; j < ih * 2 / 3; j++)                   /* the streak, top left */
        for (int k = 0; k < 3; k++) px_blend(c, x + 8 + j / 3 + k, y + SH_LID + 2 + j, 0xffffff, dim ? 12 : 22);
    rect_fill(c, x, y + SH_LID + ih, w, SH_BASE, 0x15181a);
    rect_fill(c, x, y + SH_LID + ih + SH_BASE - 1, w, 1, 0x0a0c0d);
}
static void sh_rest_sign(ctx_t *c, int cx, int cy, int state) {   /* zZ dormant, || paused, on a dark disc */
    for (int dy = -9; dy <= 9; dy++)
        for (int dx = -9; dx <= 9; dx++) {
            int d = dx * dx + dy * dy;
            if (d <= 64) px_blend(c, cx + dx, cy + dy, 0x0b0f12, 220);
            else if (d <= 90) px_blend(c, cx + dx, cy + dy, 0x9fb4ba, 200);
        }
    uint32_t ink = 0xe2edf0;
    if (state == SHELF_PAUSED) { rect_fill(c, cx - 4, cy - 4, 3, 9, ink); rect_fill(c, cx + 2, cy - 4, 3, 9, ink); return; }
    draw_text(c, cx - 1, cy - 6, 1, ink, "Z");             /* the big Z, up and right ... */
    rect_fill(c, cx - 6, cy + 1, 4, 1, ink);               /* ... a little z, down and left */
    px(c, cx - 4, cy + 2, src_color(ink, 1).v); px(c, cx - 5, cy + 3, src_color(ink, 1).v);
    rect_fill(c, cx - 6, cy + 4, 4, 1, ink);
}
static void sh_dashed(ctx_t *c, int x, int y, int w, int h, uint32_t rgb) {
    for (int i = 0; i < w; i++) if ((i / 4) % 2 == 0) { rect_fill(c, x + i, y, 1, 2, rgb); rect_fill(c, x + i, y + h - 2, 1, 2, rgb); }
    for (int j = 0; j < h; j++) if ((j / 4) % 2 == 0) { rect_fill(c, x, y + j, 2, 1, rgb); rect_fill(c, x + w - 2, y + j, 2, 1, rgb); }
}
static void sh_arrow(ctx_t *c, int x, int y, bool right, bool live) {
    uint32_t rgb = live ? MSP_TEAL : 0x2c4a52;
    button(c, x, y, SH_ARW_W, MSP_CLOSE_H, 0x1c2f36, rgb, "", 2);
    int cx = x + SH_ARW_W / 2, cy = y + MSP_CLOSE_H / 2;
    for (int i = 0; i < 6; i++) {
        int px0 = right ? cx - 3 + i : cx + 2 - i, hh = 5 - i;
        rect_fill(c, px0, cy - hh, 1, 2 * hh + 1, rgb);
    }
}
static void sh_pager(ctx_t *c, int cx, int y, int page, int pages) {
    char t[16];
    snprintf(t, sizeof t, lang_tr("%d OF %d"), page, pages);
    int tw = text_w(t, 2);
    sh_arrow(c, cx - tw / 2 - 12 - SH_ARW_W, y, false, page > 1);
    draw_text(c, cx - tw / 2, y + (MSP_CLOSE_H - 14) / 2, 2, pages > 1 ? 0xffffff : MSP_QUIET, t);
    sh_arrow(c, cx + (tw + 1) / 2 + 12, y, true, page < pages);
}
/* the footer: [left] < 1 OF 2 > [right] on one line, or the pager over the pair (the bowl, the watch) */
static void sh_footer(ctx_t *c, const char *left, bool left_sd, int wallet, const char *right, int page, int pages) {
    int cw = MSP_CLOSE_W;
#if PAGE_BOWL || defined(TANK_WATCH)
    sh_pager(c, TANK_W / 2, SH_PAGER_Y, page, pages);
    int lx = (TANK_W - (SH_BTN_W + 12 + cw)) / 2, rx = lx + SH_BTN_W + 12, y = SH_FOOT_Y;   /* the pair centred as one group */
#else
    sh_pager(c, (SH_X0 + SH_BTN_W + TANK_W - SH_X0 - cw) / 2, SH_FOOT_Y, page, pages);
    int lx = SH_X0, rx = TANK_W - SH_X0 - cw, y = SH_FOOT_Y;
#endif
#if PAGE_BOWL || defined(TANK_WATCH)
    if (!right[0]) lx = (TANK_W - SH_BTN_W) / 2;           /* (alone: centred) */
#endif
    if (left_sd) {                                         /* the wallet: the coin and the count */
        char t[16]; snprintf(t, sizeof t, "%d", wallet);
        int w = 16 + 6 + text_w(t, 2), x = lx + (SH_BTN_W - w) / 2;
        blit_icon(c, x, y + (MSP_CLOSE_H - 16) / 2, &icon_shop_sand_dollar_16, 255);
        draw_text(c, x + 22, y + (MSP_CLOSE_H - 14) / 2, 2, 0xffffff, t);
    } else button(c, lx, y, SH_BTN_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, left, 2);
    (void)rx;
    if (right[0]) button(c, rx, y, cw, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, right, 2);
}

void render_shelf_thumb_size(int *w, int *h) { *w = SH_PIC_W; *h = SH_IH; }
void render_shelf_thumb(const uint16_t *frame, uint16_t *thumb) {
    int cw, ch;                                            /* the crop: the picture's shape from the frame's middle */
#if PAGE_BOWL
    ch = 340;                                              /* the square inside the circle, a little over */
#else
    ch = TANK_H;
#endif
    cw = ch * SH_PIC_W / SH_IH;
    if (cw > TANK_W) { cw = TANK_W; ch = cw * SH_IH / SH_PIC_W; }
    int ox = (TANK_W - cw) / 2, oy = (TANK_H - ch) / 2;
    for (int j = 0; j < SH_IH; j++)
        for (int i = 0; i < SH_PIC_W; i++) {               /* a box filter over the source pixels each one covers */
            int x0 = ox + i * cw / SH_PIC_W, x1 = ox + (i + 1) * cw / SH_PIC_W, y0 = oy + j * ch / SH_IH, y1 = oy + (j + 1) * ch / SH_IH;
            int r = 0, g = 0, b = 0, n = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++) {
                    uint16_t v = FB_SWAP16(frame[y * TANK_W + x]);
                    r += v >> 11; g += (v >> 5) & 63; b += v & 31; n++;
                }
            thumb[j * SH_PIC_W + i] = n ? (uint16_t)((r / n) << 11 | (g / n) << 5 | (b / n)) : 0;
        }
}
/* THE TOUCH LOCK (0.3.9.3, art approved by Strato): keepers asked for a way to stop taps reaching the tank. Taps can't
 * undo a lock, so its switch lives where only BOOT reaches - MY TANKS (BOOT twice). A padlock button by the title:
 * open and outlined while the glass takes taps, shut and filled while it does not (the shape and the fill say it,
 * never hue alone); a locked board's NOW tank wears the padlock where the others wear their rest signs, and a tap
 * on a locked tank shows how to get back (render_touch_lock_hint) */
static bool g_sh_touch_lock;
void render_shelf_set_touch_lock(bool on) { g_sh_touch_lock = on; }
#define SH_LOCK_W 44
static void sh_padlock(ctx_t *c, int cx, int cy, bool shut, uint32_t ink, uint32_t hole) {   /* 12 x 16: the body, its keyhole, the shackle */
    rect_fill(c, cx - 6, cy - 1, 12, 9, ink);
    rect_fill(c, cx - 1, cy + 1, 2, 4, hole);
    int top = shut ? cy - 8 : cy - 11;                     /* open: the shackle lifted, its right leg out of the body */
    rect_fill(c, cx - 4, top, 8, 2, ink);
    rect_fill(c, cx - 5, top + 1, 2, cy - 1 - top - 1, ink);
    rect_fill(c, cx + 3, top + 1, 2, shut ? cy - 1 - top - 1 : 4, ink);
}
static void sh_lock_btn(ctx_t *c, int x, int y, bool locked) {
    if (locked) button(c, x, y, SH_LOCK_W, MSP_CLOSE_H, MSP_TEAL, MSP_TEAL, "", 2);
    else        button(c, x, y, SH_LOCK_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "", 2);
    sh_padlock(c, x + SH_LOCK_W / 2, y + MSP_CLOSE_H / 2 + 1, locked, locked ? MSP_INK : MSP_TEAL, locked ? MSP_TEAL : 0x1c2f36);
}
static void sh_lock_xy(int *x, int *y) {                   /* by the title: at the shelves' right edge, or beside the title on the round glass and the watch */
#if PAGE_BOWL || defined(TANK_WATCH)
    *x = (TANK_W - text_w("MY TANKS", 3) - 14 - SH_LOCK_W) / 2 + text_w("MY TANKS", 3) + 14;   /* (the title and the button centered as one group) */
#else
    *x = SH_X0 + SH_ROW_W - SH_LOCK_W;
#endif
    *y = SH_TITLE_Y + 10 - MSP_CLOSE_H / 2;
}
void render_touch_lock_hint(uint16_t *fb, int stride) {    /* a tap on a locked tank: what happened and the way back */
    ctx_t c = ctx_frame(fb, stride);
    const char *l1 = "TOUCH IS LOCKED", *l2 = "PRESS BOOT TWICE TO UNLOCK";
    int tw = text_w(l1, 2) > text_w(l2, 1) ? text_w(l1, 2) : text_w(l2, 1);
    int w = 14 + 16 + 12 + tw + 14, h = 46, x = (TANK_W - w) / 2, y = TANK_H * 62 / 100;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) px_blend(&c, x + i, y + j, 0x0b0f12, 225);
    rect_fill(&c, x, y, w, 1, MSP_TEAL); rect_fill(&c, x, y + h - 1, w, 1, MSP_TEAL);
    rect_fill(&c, x, y, 1, h, MSP_TEAL); rect_fill(&c, x + w - 1, y, 1, h, MSP_TEAL);
    sh_padlock(&c, x + 14 + 6, y + h / 2 + 2, true, MSP_TEAL, 0x0b0f12);
    draw_text(&c, x + 14 + 16 + 12, y + 9, 2, 0xffffff, l1);
    draw_text(&c, x + 14 + 16 + 12, y + 29, 1, MSP_TEAL, l2);
}
/* MOVE (0.3.9.3, art approved by Strato): a tank held on the shelf lifts out of its place - raised, framed in teal - the
 * title says which, the empty places light as places to put it (a tank's place trades with it), CANCEL bottom left */
static bool g_sh_moving; static int g_sh_move = -1; static char g_sh_move_name[24];
void render_shelf_set_move(bool on, int i, const char *name) {
    g_sh_moving = on; g_sh_move = i; snprintf(g_sh_move_name, sizeof g_sh_move_name, "%s", name ? name : "");
    for (char *p = g_sh_move_name; *p; p++) if (*p == '\n') *p = ' ';
}
static int sh_slot_x(int i) { return SH_X0 + (i & 3) * (SH_TW + SH_GAP); }
static int sh_slot_y(int i) { return SH_ROW1_Y + (i >> 2) * (SH_SHELF_H + SH_ROW_GAP); }
void render_shelf(uint16_t *fb, int stride, const shelf_slot_t slot[8], int page, int pages) {
    ctx_t c = ctx_frame(fb, stride);
    sh_wall(&c);
    const bool moving = g_sh_moving;
    if (moving) {                                          /* MOVE: the title names the tank in hand */
        char t[48]; snprintf(t, sizeof t, lang_tr("MOVE %s"), g_sh_move_name);
        draw_words(&c, (TANK_W - words_w(t, 2)) / 2, SH_TITLE_Y + 4, 2, 0xffffff, t);   /* (with a name: untranslated) */
    } else {
#if PAGE_BOWL || defined(TANK_WATCH)
    draw_text(&c, (TANK_W - text_w("MY TANKS", 3) - 14 - SH_LOCK_W) / 2, SH_TITLE_Y, 3, 0xffffff, "MY TANKS");   /* (beside the padlock) */
#else
    draw_text(&c, (TANK_W - text_w("MY TANKS", 3)) / 2, SH_TITLE_Y, 3, 0xffffff, "MY TANKS");
#endif
    }
    for (int row = 0; row < 2; row++)
        sh_plank(&c, SH_X0 - SH_OVER, sh_slot_y(row * 4) + SH_BOX_H, SH_ROW_W + 2 * SH_OVER);
    for (int i = 0; i < 8; i++) {
        const shelf_slot_t *s = &slot[i];
        int x = sh_slot_x(i), y = sh_slot_y(i), face = y + SH_BOX_H + SH_TOP;
        if (s->state == SHELF_EMPTY) {
            uint32_t col = moving ? MSP_TEAL : 0x5d6e72;      /* (moving: a place to put it, lit) */
            sh_dashed(&c, x, y, SH_TW, SH_BOX_H, col);
            int cx = x + SH_TW / 2, cy = y + SH_BOX_H / 2;
            rect_fill(&c, cx - 9, cy - 1, 19, 3, moving ? MSP_TEAL : MSP_QUIET); rect_fill(&c, cx - 1, cy - 9, 3, 19, moving ? MSP_TEAL : MSP_QUIET);
            continue;
        }
        const bool held = moving && i == g_sh_move;
        if (held) {                                        /* the tank in hand: lifted 10 px, framed, its place a shadow */
            for (int yy = 0; yy < 6; yy++) rect_fill(&c, x + 4, y + SH_BOX_H - 6 + yy, SH_TW - 8, 1, sh_shade(0x3a2a1e, 60 + yy * 4));
            y -= 10;
            rect_edge(&c, x - 3, y - 3, SH_TW + 6, SH_BOX_H + 6, MSP_TEAL); rect_edge(&c, x - 2, y - 2, SH_TW + 4, SH_BOX_H + 4, MSP_TEAL);
        }
        bool now = s->state == SHELF_NOW;
        if (now)                                           /* its light falls on the plank */
            for (int yy = 0; yy < SH_TOP; yy++)
                for (int xx = -8; xx < SH_TW + 8; xx++) {
                    int edge = xx < 0 ? -xx : xx >= SH_TW ? xx - SH_TW + 1 : 0;
                    px_blend(&c, x + xx, y + SH_BOX_H + yy, 0xfff0c8, 110 - edge * 12);
                }
        sh_tank(&c, x, y, SH_TW, SH_IH, s->thumb, !now);
        if (now) {                                         /* the NOW tag over the lid (scale 2, 0.3.9.3) */
            int w = text_w("NOW", 2) + 12, tx = x + (SH_TW - w) / 2, ty = y - 20;
            rect_fill(&c, tx, ty, w, 18, MSP_TEAL);
            draw_text(&c, tx + 6, ty + 2, 2, MSP_INK, "NOW");
        } else sh_rest_sign(&c, x + SH_TW - 13, y + SH_LID + 12, s->state);
        if (now && g_sh_touch_lock) {                      /* the locked glass: the padlock where the others wear their rest signs */
            int cx = x + SH_TW - 13, cy = y + SH_LID + 12;
            for (int dy = -9; dy <= 9; dy++) for (int dx = -9; dx <= 9; dx++) { int d = dx * dx + dy * dy;
                if (d <= 64) px_blend(&c, cx + dx, cy + dy, 0x0b0f12, 220); else if (d <= 90) px_blend(&c, cx + dx, cy + dy, 0x9fb4ba, 200); }
            sh_padlock(&c, cx, cy + 1, true, 0xe2edf0, 0x0b0f12);
        }
        if (s->experimental) {                             /* EXPERIMENTAL: the flask on the glass, low left, in the rest signs' disc (the plate is the name's) */
            int cx = x + 13, cy = y + SH_BOX_H - 15;
            for (int dy = -9; dy <= 9; dy++) for (int dx = -9; dx <= 9; dx++) { int d = dx * dx + dy * dy;
                if (d <= 64) px_blend(&c, cx + dx, cy + dy, 0x0b0f12, 220); else if (d <= 90) px_blend(&c, cx + dx, cy + dy, 0x9fb4ba, 200); }
            sh_flask(&c, cx - 3, cy - 4, 0xe2edf0);
        }
        sh_plate(&c, x + SH_TW / 2, face + 1, s->name);
    }
    sh_footer(&c, moving ? "CANCEL" : "LIBRARY", false, 0, moving ? "" : "CLOSE", page, pages);
    if (!moving) { int lx, ly; sh_lock_xy(&lx, &ly); sh_lock_btn(&c, lx, ly, g_sh_touch_lock); }
}

int render_shelf_tap(float x, float y) {
    const int s = 8;                                       /* a fingertip's slop round each button */
#if PAGE_BOWL || defined(TANK_WATCH)
    int lx = (TANK_W - (SH_BTN_W + 12 + MSP_CLOSE_W)) / 2, rx = lx + SH_BTN_W + 12, fy = SH_FOOT_Y, py = SH_PAGER_Y, pcx = TANK_W / 2;
#else
    int lx = SH_X0, rx = TANK_W - SH_X0 - MSP_CLOSE_W, fy = SH_FOOT_Y, py = SH_FOOT_Y, pcx = (SH_X0 + SH_BTN_W + TANK_W - SH_X0 - MSP_CLOSE_W) / 2;
#endif
    { int kx, ky; sh_lock_xy(&kx, &ky);                   /* the padlock by the title */
      if (x >= kx - s && x < kx + SH_LOCK_W + s && y >= ky - s && y < ky + MSP_CLOSE_H + s) return SHELF_TAP_TOUCH_LOCK; }
    if (y >= fy - s && y < fy + MSP_CLOSE_H + s) {
        if (x >= lx - s && x < lx + SH_BTN_W + s) return SHELF_TAP_LIBRARY;
        if (x >= rx - s && x < rx + MSP_CLOSE_W + s) return SHELF_TAP_CLOSE;
    }
    if (y >= py - s && y < py + MSP_CLOSE_H + s) {         /* the pager's arrows: either side of "1 OF 2" */
        if (x >= pcx - 120 && x < pcx - 30) return SHELF_TAP_PREV;
        if (x >= pcx + 30 && x < pcx + 120) return SHELF_TAP_NEXT;
    }
    for (int i = 0; i < 8; i++) {
        int sx = sh_slot_x(i), sy = sh_slot_y(i);
        if (x >= sx - 4 && x < sx + SH_TW + 4 && y >= sy - 4 && y < sy + SH_BOX_H + SH_TOP + SH_FACE) return SHELF_TAP_SLOT0 + i;
    }
    return SHELF_TAP_NONE;
}

/* a tank's card: over the shelves, dimmed. The little tank, its name, what
 * it is, how long it has rested; TIME: DORMANT | PAUSED (the one chosen is
 * filled - fill, not hue); a plain line saying what that means; SWAP IN
 * (lit), RENAME, MORE. A tap outside the card closes it. */
#if PAGE_BOWL
#define SHC_W 330
#else
#define SHC_W 344
#endif
#define SHC_SEG_H 30
#define SHC_TOP   100                                      /* the card's lines beside its picture: name + four at scale 2 */
#define SHC_NOTE  36                                       /* under TIME: its note at scale 2, then the buttons */
/* RENAME on a tank's card (0.3.9.3, art approved by Strato): a tank's name is its plate's two lines of SHR_N, set
 * each on its own (Strato: "give the users both lines of text to configure independently"), TYPED on the Wi-Fi
 * password page's keyboard (Strato: the small letter wheel was "fatiguing" - tiny letters, a lot of precise scrolling).
 * 7 x 3 big keys: two rows of fourteen characters a page (MORE turns it: A-N, O-Z ' ., digits - ! ? /) and the function
 * row MORE | DEL | SPACE | LINE | BACK | DONE (on the round glass in its widest band, six keys, the letters under it).
 * Above, the name as its plate will show it, in brass: the line being typed carries its seven marks and the blinking
 * bar; a tap on a line (or LINE) moves to it, CLEAR beside the plate empties both. A key acts when the finger lifts on
 * the key it landed on. A letter goes at the end of its line; a full first line carries on into the second; DEL on an
 * empty second line goes back into the first */
#define SHR_N     7
#if PAGE_BOWL                    /* the round glass: the function row in its widest band, six wider keys; the letters under it */
#define NK_PX 44
#define NK_W  40
#define NK_Y  222
#define NK_PY 72
#define NK_H  66
#define NK_S  3                  /* the plate's letters */
#define NK_PLATE_Y 70
#define NK_FN_Y 156              /* (the function row's top, its key and pitch) */
#define NK_FN_W 60
#define NK_FN_PX 64
#define NK_FN_H 54
#elif defined(TANK_WATCH)
#define NK_PX 56
#define NK_W  52
#define NK_Y  184
#define NK_PY 78
#define NK_H  72
#define NK_S  4
#define NK_PLATE_Y 70
#elif defined(TANK_SQUARE)
#define NK_PX 66
#define NK_W  62
#define NK_Y  176
#define NK_PY 92
#define NK_H  84
#define NK_S  4
#define NK_PLATE_Y 58
#else                            /* the 1.8: the bottom row clear of the 40 px corners */
#define NK_PX 60
#define NK_W  56
#define NK_Y  126
#define NK_PY 76
#define NK_H  70
#define NK_S  4
#define NK_PLATE_Y 34
#endif
#define NK_X0 ((TANK_W - (6 * NK_PX + NK_W)) / 2)
#ifdef NK_FN_Y
#define NK_FH NK_FN_H
#else
#define NK_FH NK_H
#endif
#define NK_GW (6 * NK_S)
#define NK_LH (7 * NK_S)
#define NK_PW (SHR_N * NK_GW - NK_S + 24)
#define NK_PH (2 * NK_LH + 30)
#define NK_PX0 ((TANK_W - NK_PW) / 2 - 40)
#define NK_CLEAR_W 68
static const char *const NK_PAGES[3] = { "ABCDEFGHIJKLMN", "OPQRSTUVWXYZ'.", "1234567890-!?/" };
const char *render_shelf_rename_keys(int page) { return NK_PAGES[page < 0 || page > 2 ? 0 : page]; }
/* a function key's box: SHK_MORE .. SHK_DONE */
static void nk_fn_box(int key, int *x, int *y, int *w, int *h) {
    int col = key == SHK_MORE ? 0 : key == SHK_DEL ? 1 : key == SHK_SPACE ? 2 : key == SHK_LINE ? 4 : key == SHK_BACK ? 5 : 6;
#ifdef NK_FN_Y
    int k = col < 3 ? col : col - 1;
    *x = (TANK_W - (5 * NK_FN_PX + NK_FN_W)) / 2 + k * NK_FN_PX; *y = NK_FN_Y; *w = NK_FN_W; *h = NK_FN_H;
#else
    *x = NK_X0 + col * NK_PX; *y = NK_Y + 2 * NK_PY; *w = col == 2 ? NK_PX + NK_W : NK_W; *h = NK_H;
#endif
}
static void nk_box(int key, int *x, int *y, int *w, int *h) {   /* any key's box */
    if (key >= SHK_CHAR0 && key < SHK_CHAR0 + 14) { int cell = key - SHK_CHAR0; *x = NK_X0 + (cell % 7) * NK_PX; *y = NK_Y + (cell / 7) * NK_PY; *w = NK_W; *h = NK_H; return; }
    if (key == SHK_LINE1 || key == SHK_LINE2) { *x = NK_PX0; *y = NK_PLATE_Y + (key == SHK_LINE2 ? NK_PH / 2 : 0); *w = NK_PW; *h = NK_PH / 2; return; }
    if (key == SHK_CLEAR) { *x = NK_PX0 + NK_PW + 12; *y = NK_PLATE_Y + (NK_PH - MSP_CLOSE_H) / 2; *w = NK_CLEAR_W; *h = MSP_CLOSE_H; return; }
    nk_fn_box(key, x, y, w, h);
}
int render_shelf_rename_hit(float x, float y) {            /* the key under a finger (a few px of slop round each) */
    for (int key = SHK_CHAR0; key <= SHK_CLEAR; key++) {
        if (key >= SHK_CHAR0 + 14 && key < SHK_MORE) continue;
        int bx, by, bw, bh; nk_box(key, &bx, &by, &bw, &bh);
        int m = key == SHK_LINE1 || key == SHK_LINE2 ? 0 : 2;   /* (the keys stand 4 px apart: half of it each) */
        if (x >= bx - m && x < bx + bw + m && y >= by - m && y < by + bh + m) return key;
    }
    return SHK_NONE;
}
void render_shelf_rename_key_xy(int key, float *x, float *y) { int bx, by, bw, bh; nk_box(key, &bx, &by, &bw, &bh); *x = bx + bw * 0.5f; *y = by + bh * 0.5f; }
/* a stored name -> its two lines: split at the line break, or (a name from before the two lines) as the plate wraps it */
static void shr_split(const char *name, char l1[SHR_N + 1], char l2[SHR_N + 1]) {
    memset(l1, 0, SHR_N + 1); memset(l2, 0, SHR_N + 1);
    const char *nl = strchr(name, '\n');
    int n = (int)strlen(name);
    if (nl) { int a = (int)(nl - name); memcpy(l1, name, (size_t)(a < SHR_N ? a : SHR_N)); int b = n - a - 1; memcpy(l2, nl + 1, (size_t)(b < SHR_N ? b : SHR_N)); return; }
    if (n <= SHR_N) { memcpy(l1, name, (size_t)n); return; }
    int cut = -1;
    for (int i = 1; i < n; i++) if (name[i] == ' ' && i <= SHR_N && n - i - 1 <= SHR_N && (cut < 0 || abs(i - n / 2) < abs(cut - n / 2))) cut = i;
    if (cut > 0) { memcpy(l1, name, (size_t)cut); memcpy(l2, name + cut + 1, (size_t)(n - cut - 1)); }
    else { int k = (n + 1) / 2; if (k > SHR_N) k = SHR_N; memcpy(l1, name, (size_t)k); memcpy(l2, name + k, (size_t)(n - k > SHR_N ? SHR_N : n - k)); }
}
static bool nk_typable(char ch) { if (ch == ' ') return true; for (int p = 0; p < 3; p++) if (strchr(NK_PAGES[p], ch)) return true; return false; }
void render_shelf_rename_begin(shelf_rename_t *r, const char *name) {
    memset(r, 0, sizeof *r);
    char l[2][SHR_N + 1]; shr_split(name ? name : "", l[0], l[1]);
    for (int row = 0; row < 2; row++) {
        int n = 0;
        for (const char *p = l[row]; *p && n < SHR_N; p++) { char ch = *p >= 'a' && *p <= 'z' ? (char)(*p - 32) : *p; if (ch && nk_typable(ch)) r->line[row][n++] = ch; }   /* (what the keys cannot type: dropped) */
        r->line[row][n] = 0;
    }
    r->row = r->line[1][0] ? 1 : 0;                        /* at the end of the name: ready to add */
}
void render_shelf_rename_name(const shelf_rename_t *r, char *out, size_t cap) {   /* "LINE 1\nLINE 2" (spaces at the ends gone; one line when the second is blank) */
    char l[2][SHR_N + 1];
    for (int row = 0; row < 2; row++) {
        const char *p = r->line[row]; while (*p == ' ') p++;
        int n = (int)strlen(p); while (n > 0 && p[n - 1] == ' ') n--;
        memcpy(l[row], p, (size_t)n); l[row][n] = 0;
    }
    if (l[0][0] && l[1][0]) snprintf(out, cap, "%s\n%s", l[0], l[1]); else snprintf(out, cap, "%s", l[0][0] ? l[0] : l[1]);
}
static void nk_type(shelf_rename_t *r, char ch) {
    if ((int)strlen(r->line[r->row]) >= SHR_N && r->row == 0) r->row = 1;   /* a full first line carries on into the second */
    int n = (int)strlen(r->line[r->row]);
    if (n < SHR_N) { r->line[r->row][n] = ch; r->line[r->row][n + 1] = 0; }
}
int render_shelf_rename_touch(shelf_rename_t *r, float x, float y, bool down) {
    int ret = SHR_NONE;
    if (down && !r->down) { r->px = x; r->py = y; }
    else if (!down && r->down) {                           /* the lift: the key it landed on, if it lifted there too */
        int k = render_shelf_rename_hit(r->px, r->py);
        if (k && k == render_shelf_rename_hit(x, y)) {
            ret = SHR_TICK;
            if (k >= SHK_CHAR0 && k < SHK_CHAR0 + 14) nk_type(r, NK_PAGES[r->page][k - SHK_CHAR0]);
            else if (k == SHK_SPACE) nk_type(r, ' ');
            else if (k == SHK_DEL) {
                if (!r->line[r->row][0] && r->row == 1) r->row = 0;   /* an empty second line: back into the first */
                int n = (int)strlen(r->line[r->row]); if (n) r->line[r->row][n - 1] = 0;
            }
            else if (k == SHK_MORE) r->page = (r->page + 1) % 3;
            else if (k == SHK_LINE) r->row ^= 1;
            else if (k == SHK_LINE1 || k == SHK_LINE2) r->row = k == SHK_LINE2;
            else if (k == SHK_CLEAR) { r->line[0][0] = r->line[1][0] = 0; r->row = 0; }
            else if (k == SHK_BACK) ret = SHR_CANCEL;
            else if (k == SHK_DONE) ret = SHR_DONE;
        }
    }
    r->down = down;
    return ret;
}
void render_shelf_rename(uint16_t *fb, int stride, const shelf_slot_t *s, const shelf_rename_t *r, int part, double clock) {
    (void)s;
    ctx_t c = ctx_frame(fb, stride);
    if (part & 1) {
        for (int y = 0; y < TANK_H; y++)
            for (int x = 0; x < TANK_W; x++) px565_blend(&c, x, y, 0, 244);   /* (near black: the shelves only a hint behind) */
    }
    if (!(part & 2)) return;
    draw_text(&c, (TANK_W - text_w("RENAME THIS TANK", 2)) / 2, NK_PLATE_Y - 26, 2, 0xffffff, "RENAME THIS TANK");
    rect_fill(&c, NK_PX0, NK_PLATE_Y, NK_PW, NK_PH, SH_BRASS); rect_edge(&c, NK_PX0, NK_PLATE_Y, NK_PW, NK_PH, 0x7a6435);
    for (int k = 0; k < 2; k++) {
        int ty = NK_PLATE_Y + 9 + k * (NK_LH + 12), tx = NK_PX0 + 12;
        draw_words(&c, tx, ty, NK_S, 0x2c2412, r->line[k]);
        if (k == r->row) {
            for (int i = 0; i < SHR_N; i++) rect_fill(&c, tx + i * NK_GW, ty + NK_LH + 3, NK_GW - NK_S - 1, 2, 0x7a6435);   /* its seven places */
            int n = (int)strlen(r->line[k]);
            if (n < SHR_N && ((int)(clock * 2) & 1)) rect_fill(&c, tx + n * NK_GW, ty - 2, NK_S, NK_LH + 4, 0x2c2412);   /* the bar */
        }
    }
    { int x, y, w, h; nk_box(SHK_CLEAR, &x, &y, &w, &h); button(&c, x, y, w, h, 0x1c2f36, MSP_TEAL, "CLEAR", 2); }
    for (int cell = 0; cell < 14; cell++) {
        int x, y, w, h; nk_box(SHK_CHAR0 + cell, &x, &y, &w, &h);
        char lbl[2] = { NK_PAGES[r->page][cell], 0 };
        rect_fill(&c, x, y, w, h, 0x16262c); rect_edge(&c, x, y, w, h, 0x3d5d66);
        draw_text(&c, x + (w - text_w(lbl, 3)) / 2, y + (h - 21) / 2, 3, 0xffffff, lbl);
    }
    static const struct { int key; const char *lbl; } FK[6] = { { SHK_MORE, "MORE" }, { SHK_DEL, "DEL" }, { SHK_SPACE, "SPACE" }, { SHK_LINE, "LINE" }, { SHK_BACK, "BACK" }, { SHK_DONE, "DONE" } };
    for (int f = 0; f < 6; f++) {
        int x, y, w, h; nk_fn_box(FK[f].key, &x, &y, &w, &h);
        if (FK[f].key == SHK_DONE) lit_button(&c, x, y, w, h, "DONE");
        else if (FK[f].key == SHK_BACK) { button(&c, x, y, w, h, 0x16262c, 0x3d5d66, "", 2);   /* (it leaves the page: quieter) */
            draw_words(&c, x + (w - words_w("BACK", 2)) / 2, y + (h - 14) / 2, 2, 0xffffff, "BACK"); }   /* as given: ZURÜCK is too wide for the key */
        else button(&c, x, y, w, h, 0x1c2f36, MSP_TEAL, FK[f].lbl, 2);
        if (FK[f].key == SHK_SPACE) rect_fill(&c, x + 10, y + h / 2 + 10, w - 20, 3, MSP_TEAL);   /* the space bar's bar */
        if (FK[f].key == SHK_MORE) for (int i = 0; i < 3; i++) rect_fill(&c, x + w / 2 - 10 + i * 8, y + h - 10, 4, 4, i == r->page ? MSP_TEAL : 0x3d5d66);   /* which page */
    }
}

static bool g_shc_glass;                                   /* the card drawn last was the tank on the glass's (its tap map) */
void render_shelf_card(uint16_t *fb, int stride, const shelf_slot_t *s, const char *kind, int shelf_days, int age_days) {
    ctx_t c = ctx_frame(fb, stride);
    for (int y = 0; y < TANK_H; y++)
        for (int x = 0; x < TANK_W; x++) px565_blend(&c, x, y, 0, 150);
    int top = SH_BOX_H > SHC_TOP ? SH_BOX_H : SHC_TOP;     /* the picture or the text, whichever is taller */
    int h = 16 + top + 14 + SHC_SEG_H + SHC_NOTE + MSP_CLOSE_H + 16;
    int mx = (TANK_W - SHC_W) / 2, my = (TANK_H - h) / 2;
    rect_fill(&c, mx, my, SHC_W, h, MSP_INK);
    rect_edge(&c, mx, my, SHC_W, h, MSP_TEAL); rect_edge(&c, mx + 1, my + 1, SHC_W - 2, h - 2, MSP_TEAL);
    sh_tank(&c, mx + 16, my + 16, SH_TW, SH_IH, s->thumb, false);
    int tx = mx + 16 + SH_TW + 14, ty = my + 18;
    char t[48];
    { char nm[24]; snprintf(nm, sizeof nm, "%s", s->name); for (char *p = nm; *p; p++) if (*p == '\n') *p = ' ';   /* (its two lines as one) */
      draw_words(&c, tx, ty, 2, 0xffffff, nm); ty += 24; }                /* (every line scale 2 since 0.3.9.3: the 6 px ones read as specks) */
    draw_text(&c, tx, ty, 2, MSP_TEAL, kind); ty += 18;
    if (s->experimental) { sh_flask(&c, tx, ty + 2, MSP_QUIET); draw_text(&c, tx + 10, ty, 2, MSP_QUIET, "EXPERIMENTAL"); ty += 18; }
    const bool glass = s->state == SHELF_NOW;              /* the tank on the glass (0.3.9.3): its name and RENAME, nothing to swap or rest */
    g_shc_glass = glass;
    if (glass) snprintf(t, sizeof t, "ON THE GLASS"); else snprintf(t, sizeof t, lang_tr(shelf_days == 1 ? "SHELVED %d DAY" : "SHELVED %d DAYS"), shelf_days);
    draw_text(&c, tx, ty, 2, MSP_TEAL, t); ty += 18;
    if (age_days >= 0) { snprintf(t, sizeof t, lang_tr("%d DAYS OLD"), age_days); draw_text(&c, tx, ty, 2, MSP_TEAL, t); }
    int y = my + 16 + top + 14;                            /* TIME: DORMANT | PAUSED */
    if (glass) {                                           /* (in its place: what the shelf does with it) */
        const char *l1 = "SWAP IN ANOTHER TANK", *l2 = "TO PUT THIS ONE AWAY";
        draw_text(&c, mx + (SHC_W - text_w(l1, 2)) / 2, y + 4, 2, MSP_QUIET, l1);
        draw_text(&c, mx + (SHC_W - text_w(l2, 2)) / 2, y + 24, 2, MSP_QUIET, l2);
        y += SHC_SEG_H + SHC_NOTE;
        button(&c, mx + (SHC_W - 98) / 2, y, 98, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "RENAME", 2);
        return;
    }
    draw_text(&c, mx + 16, y + (SHC_SEG_H - 14) / 2, 2, MSP_TEAL, "TIME");
    int sx = mx + 16 + text_w("TIME", 2) + 14, sw = mx + SHC_W - 16 - sx, half = sw / 2;
    bool paused = s->state == SHELF_PAUSED;
    for (int k = 0; k < 2; k++) {
        bool on = (k == 1) == paused;
        const char *lab = k ? "PAUSED" : "DORMANT";
        int bx = sx + k * half, bw = k ? sw - half : half;
        if (on) { rect_fill(&c, bx, y, bw, SHC_SEG_H, MSP_TEAL); }
        else { rect_fill(&c, bx, y, bw, SHC_SEG_H, 0x1c2f36); rect_edge(&c, bx, y, bw, SHC_SEG_H, MSP_TEAL); }
        draw_text(&c, bx + (bw - text_w(lab, 2)) / 2, y + (SHC_SEG_H - 14) / 2, 2, on ? MSP_INK : MSP_QUIET, lab);
    }
    y += SHC_SEG_H + 9;
    const char *note = paused ? "WAITS AS YOU LEFT IT" : "LIVES ON, AS IF ASLEEP";
    draw_text(&c, mx + (SHC_W - text_w(note, 2)) / 2, y, 2, MSP_QUIET, note);
    y += SHC_NOTE - 9;
    int bw0 = 120, bw1 = 98, bw2 = 74, gap = (SHC_W - 32 - bw0 - bw1 - bw2) / 2, bx = mx + 16;
    if (g_shc_swap >= 0) swap_button(&c, bx, y, bw0, MSP_CLOSE_H, g_shc_swap);   /* SWAP IN tapped: it answers at once */
    else lit_button(&c, bx, y, bw0, MSP_CLOSE_H, "SWAP IN");
    bx += bw0 + gap;
    button(&c, bx, y, bw1, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "RENAME", 2); bx += bw1 + gap;
    button(&c, bx, y, bw2, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "MORE", 2);
}

/* MORE on a tank's card: REMOVE (0.3.9.3, art approved by Strato; D5). The card again, its picture and name, and what
 * removing means in plain words: the tank and its fish are gone for good, its pieces sell back (the sand dollar and
 * the sum). BACK, and HOLD TO SELL - held 2 s, the fill growing across the button as the finger stays (the fill
 * says how far, never a hue); lifted early, nothing happens */
#define SHM_TOP (SH_BOX_H > 92 ? SH_BOX_H : 92)
#define SHM_H   (16 + SHM_TOP + 14 + 4 * 20 + 16 + MSP_CLOSE_H + 16)   /* (four lines: 0.4.1's refund may take two) */
#define SHM_BW  92
static void shm_btns(int *mx, int *my, int *by, int *rx, int *rw) {   /* the panel, its row of buttons, HOLD TO SELL's box */
    *mx = (TANK_W - SHC_W) / 2; *my = (TANK_H - SHM_H) / 2;
    *by = *my + 16 + SHM_TOP + 14 + 4 * 20 + 16;
    *rw = SHC_W - 32 - SHM_BW - 12; *rx = *mx + 16 + SHM_BW + 12;
}
int render_shelf_remove_hit(float x, float y) {
    int mx, my, by, rx, rw; shm_btns(&mx, &my, &by, &rx, &rw); const int s = 6;
    if (x < mx || x >= mx + SHC_W || y < my || y >= my + SHM_H) return SHM_TAP_OUTSIDE;
    if (y >= by - s && y < by + MSP_CLOSE_H + s) {
        if (x >= mx + 16 - s && x < mx + 16 + SHM_BW + s) return SHM_TAP_BACK;
        if (x >= rx - s && x < rx + rw + s) return SHM_TAP_HOLD;
    }
    return SHM_TAP_NONE;
}
void render_shelf_remove(uint16_t *fb, int stride, const shelf_slot_t *s, int refund, const int n[3], float hold, int part) {
    ctx_t c = ctx_frame(fb, stride);
    if (part & 1)
        for (int y = 0; y < TANK_H; y++)
            for (int x = 0; x < TANK_W; x++) px565_blend(&c, x, y, 0, 150);
    if (!(part & 2)) return;
    const int top = SHM_TOP, h = SHM_H;
    const int mx = (TANK_W - SHC_W) / 2, my = (TANK_H - h) / 2;
    rect_fill(&c, mx, my, SHC_W, h, MSP_INK);
    rect_edge(&c, mx, my, SHC_W, h, MSP_TEAL); rect_edge(&c, mx + 1, my + 1, SHC_W - 2, h - 2, MSP_TEAL);
    sh_tank(&c, mx + 16, my + 16, SH_TW, SH_IH, s->thumb, true);
    int tx = mx + 16 + SH_TW + 14, ty = my + 18;
    char nm[24]; snprintf(nm, sizeof nm, "%s", s->name); for (char *p = nm; *p; p++) if (*p == '\n') *p = ' ';
    draw_text(&c, tx, ty, 2, 0xffffff, "REMOVE"); ty += 22;
    draw_words(&c, tx, ty, 2, 0xffffff, nm); ty += 22;
    draw_text(&c, tx, ty, 2, MSP_TEAL, "FROM THE SHELF?");
    /* what the refund is for (0.4.1, audit P6): only what this tank has, singular or plural - "+20 FOR ITS PIECES",
       "+60 FOR ITS PIECES" / "AND CRITTERS", "+90 FOR ITS PIECES," / "CRITTERS AND BACKGROUNDS"; nothing bought: PIECES,
       as before. <= 24 characters a line at scale 2 */
    static const char *const KN[3][2] = { { "PIECE", "PIECES" }, { "CRITTER", "CRITTERS" }, { "BACKGROUND", "BACKGROUNDS" } };
    const char *w3[3]; int m = 0;
    for (int k = 0; k < 3; k++) if (n && n[k] > 0) w3[m++] = KN[k][n[k] > 1];
    char t[40], t2[40] = "";
    for (int k = 0; k < m; k++) w3[k] = lang_tr(w3[k]);   /* (formats and words translated, the lines drawn as built: lang.h) */
    if (m == 0) snprintf(t, sizeof t, lang_tr("+%d FOR ITS PIECES"), refund);
    else if (m == 1) snprintf(t, sizeof t, lang_tr("+%d FOR ITS %s"), refund, w3[0]);
    else if (m == 2) { snprintf(t, sizeof t, lang_tr("+%d FOR ITS %s"), refund, w3[0]); snprintf(t2, sizeof t2, lang_tr("AND %s"), w3[1]); }
    else { snprintf(t, sizeof t, lang_tr("+%d FOR ITS %s,"), refund, w3[0]); snprintf(t2, sizeof t2, lang_tr("%s AND %s"), w3[1], w3[2]); }
    int y = my + 16 + top + 14 + (t2[0] ? 0 : 10);             /* (one refund line: the three centered in the four) */
    const char *l1 = "THIS TANK AND ITS FISH", *l2 = "ARE GONE FOR GOOD.";
    draw_text(&c, mx + (SHC_W - text_w(l1, 2)) / 2, y, 2, 0xffffff, l1); y += 20;
    draw_text(&c, mx + (SHC_W - text_w(l2, 2)) / 2, y, 2, 0xffffff, l2); y += 20;
    int w = 16 + 6 + words_w(t, 2), x = mx + (SHC_W - w) / 2;
    blit_icon(&c, x, y - 1, &icon_shop_sand_dollar_16, 255);
    draw_words(&c, x + 22, y, 2, MSP_TEAL, t);
    if (t2[0]) draw_words(&c, mx + (SHC_W - words_w(t2, 2)) / 2, y + 20, 2, MSP_TEAL, t2);
    int bmx, bmy, rx, rw; shm_btns(&bmx, &bmy, &y, &rx, &rw);
    const int bw = SHM_BW;
    button(&c, mx + 16, y, bw, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "BACK", 2);
    rect_fill(&c, rx, y, rw, MSP_CLOSE_H, 0x1c2f36);
    int fw = (int)(rw * (hold < 0 ? 0 : hold > 1 ? 1 : hold));
    if (fw > 0) rect_fill(&c, rx, y, fw, MSP_CLOSE_H, 0x2f7f8a);   /* the hold, filling (a deep teal: the white words read on both) */
    rect_edge(&c, rx, y, rw, MSP_CLOSE_H, MSP_TEAL);
    const char *lb = "HOLD TO SELL";   /* (Strato: "hold to sell") */
    draw_text(&c, rx + (rw - text_w(lb, 2)) / 2, y + (MSP_CLOSE_H - 14) / 2, 2, 0xffffff, lb);
}

/* LOADING (0.3.9.3, art approved by Strato: "i rather add a loading spinner if its some kind of first load churn"): a
 * tank's first seconds after a SWAP IN or a power-on bake its grass (THE GRASS LOOP, ~40 ms a moment) - behind this,
 * at full speed, instead of under a stuttering tank. Deep water, a ring of eight bubbles with a bright one going round
 * (the shape moves, not only the shade), the tank's name under it, then line (NULL: FILLING THE TANK). Strato 10-31,
 * after Machito asked why a wake said FILLING THE TANK: that line is a SWAP IN's alone - any other start (a long
 * sleep ends in a restart) says WAKING THE TANK, and the time check before it is this same screen (render_clock_sync),
 * so a board that asks the internet shows one screen whose words change, not two */
void render_loading(uint16_t *fb, int stride, const char *name, double clock, const char *line) {
    ctx_t c = ctx_frame(fb, stride);
    for (int y = 0; y < TANK_H; y++) {                     /* deep water, a little lighter at the top */
        uint32_t col = sh_mix(0x0e3a4c, 0x04121a, y * 256 / TANK_H);
        rect_fill(&c, 0, y, TANK_W, 1, col);
    }
    const int cx = TANK_W / 2, cy = TANK_H / 2 - 24, R = 40;
    const int lead = (int)(clock * 10) % 8;                /* (a step a tenth of a second: once round in 0.8 s) */
    for (int k = 0; k < 8; k++) {
        float a = k * 6.2831853f / 8 - 1.5707963f;
        int bx = cx + (int)lroundf(cosf(a) * R), by = cy + (int)lroundf(sinf(a) * R);
        int age = (lead - k + 8) % 8;                      /* 0 = the bright one, then its trail */
        int r = age == 0 ? 11 : age == 1 ? 10 : 9;
        uint32_t rim = age == 0 ? 0xe2f6fa : age == 1 ? 0x9fd8e2 : age == 2 ? 0x5f9fae : 0x2c5a66;
        for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
            int d = dx * dx + dy * dy;
            if (d > r * r) continue;
            px(&c, bx + dx, by + dy, src_color(d >= (r - 2) * (r - 2) ? rim : sh_mix(rim, 0x0a2a36, 150), 1.0f).v);   /* a bubble: its rim bright */
        }
        if (age == 0) rect_fill(&c, bx - 4, by - 4, 2, 2, 0xffffff);         /* (its glint) */
    }
    char nm[24]; snprintf(nm, sizeof nm, "%s", name ? name : ""); for (char *p = nm; *p; p++) if (*p == '\n') *p = ' ';
    draw_words(&c, (TANK_W - words_w(nm, 2)) / 2, cy + R + 26, 2, 0xffffff, nm);
    if (!line) line = "FILLING THE TANK";
    draw_text(&c, (TANK_W - text_w(line, 2)) / 2, cy + R + 48, 2, MSP_TEAL, line);
}

/* the card's places (as render_shelf_card draws them) */
int render_shelf_card_tap(float x, float y) {
    int top = SH_BOX_H > SHC_TOP ? SH_BOX_H : SHC_TOP, h = 16 + top + 14 + SHC_SEG_H + SHC_NOTE + MSP_CLOSE_H + 16;
    int mx = (TANK_W - SHC_W) / 2, my = (TANK_H - h) / 2, s = 6;
    if (x < mx || x >= mx + SHC_W || y < my || y >= my + h) return SHC_TAP_OUTSIDE;
    int sy = my + 16 + top + 14, sx = mx + 16 + text_w("TIME", 2) + 14, sw = mx + SHC_W - 16 - sx;
    if (g_shc_glass) {                                     /* the tank on the glass: RENAME alone */
        int by = sy + SHC_SEG_H + SHC_NOTE, bx = mx + (SHC_W - 98) / 2;
        return y >= by - s && y < by + MSP_CLOSE_H + s && x >= bx - s && x < bx + 98 + s ? SHC_TAP_RENAME : SHC_TAP_NONE;
    }
    if (y >= sy - s && y < sy + SHC_SEG_H + s && x >= sx - s) return x < sx + sw / 2 ? SHC_TAP_DORMANT : SHC_TAP_PAUSED;
    int by = sy + SHC_SEG_H + SHC_NOTE;
    if (y >= by - s && y < by + MSP_CLOSE_H + s) {
        int bw0 = 120, bw1 = 98, bw2 = 74, gap = (SHC_W - 32 - bw0 - bw1 - bw2) / 2, bx = mx + 16;
        if (x >= bx - s && x < bx + bw0 + gap / 2) return SHC_TAP_SWAP;
        bx += bw0 + gap;
        if (x >= bx - gap / 2 && x < bx + bw1 + gap / 2) return SHC_TAP_RENAME;
        return SHC_TAP_MORE;
    }
    return SHC_TAP_NONE;
}

/* THE LIBRARY: the tank types, a row each - the picture, the name, its one
 * line, EXPERIMENTAL (word + flask) or INSTALLED; GET / NEW TANK on the
 * right with what it costs under it. The wallet bottom left, BACK to the
 * shelves bottom right; LAST CHECKED under the title (the list works with
 * no Wi-Fi). */
static int sh_wrap(ctx_t *c, int x, int y, int maxw, int pitch, int max_lines, uint32_t rgb, const char *s) {
    char line[128]; int n = 0, nc = 0, lines = 0, maxc = maxw / 6;   /* n bytes, nc cells: an umlaut is two bytes, one cell */
    if (maxc > 63) maxc = 63;
    while (*s && lines < max_lines) {
        const char *w = s; while (*w == ' ') w++;
        const char *e = w; while (*e && *e != ' ') e++;
        int wl = (int)(e - w), wc = 0;
        for (const char *p = w; p < e; p++) wc += ((unsigned char)*p & 0xC0) != 0x80;
        if (wl > 63) wl = wc = 63;                                    /* (no word is that long; the buffer stays whole) */
        if (n && (nc + 1 + wc > maxc || n + 1 + wl >= (int)sizeof line)) { line[n] = 0;   /* (bytes bounded too: server text) */ draw_words(c, x, y, 1, rgb, line); y += pitch; lines++; n = nc = 0; continue; }
        if (n) { line[n++] = ' '; nc++; }
        memcpy(line + n, w, (size_t)wl); n += wl; nc += wc; s = e;
    }
    if (n && lines < max_lines) { line[n] = 0; draw_words(c, x, y, 1, rgb, line); lines++; }
    return lines;
}
void render_library(uint16_t *fb, int stride, const library_row_t *row, int n, int wallet, const char *checked) {
    ctx_t c = ctx_frame(fb, stride);
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    draw_text(&c, (TANK_W - text_w("THE LIBRARY", 3)) / 2, SH_TITLE_Y, 3, 0xffffff, "THE LIBRARY");
    draw_text(&c, (TANK_W - text_w(checked, 1)) / 2, SH_TITLE_Y + 27, 1, MSP_QUIET, checked);
    int x0 = SH_LIB_X0, x1 = TANK_W - SH_LIB_X0, bw = 116;
    int y = SH_TITLE_Y + 46, pitch = SH_BOX_H + 22;
    for (int i = 0; i < n && i < 2; i++, y += pitch) {
        const library_row_t *r = &row[i];
        sh_tank(&c, x0, y, SH_TW, SH_IH, r->thumb, false);
        int tx = x0 + SH_TW + 12, tw = x1 - bw - 10 - tx;
        draw_text(&c, tx, y + 2, 2, 0xffffff, r->name);
        int ln = sh_wrap(&c, tx, y + 24, tw, 11, (SH_BOX_H - 38) / 11, MSP_TEAL, lang_tr(r->line));   /* as many lines as the picture is tall */
        int tagy = y + 24 + ln * 11 + 6;
        if (r->experimental) { sh_flask(&c, tx, tagy - 1, MSP_QUIET); draw_text(&c, tx + 10, tagy, 1, MSP_QUIET, "EXPERIMENTAL"); }
        else if (r->installed) draw_text(&c, tx, tagy, 1, MSP_QUIET, "INSTALLED");
        int bx = x1 - bw, by = y + 10;
        const char *act = r->installed ? "NEW TANK" : "GET";
        if (r->price > wallet) {                           /* not enough yet: an outline with dim words (fill, not hue) */
            rect_edge(&c, bx, by, bw, MSP_CLOSE_H, MSP_DIM);
            draw_text(&c, bx + (bw - text_w(act, 2)) / 2, by + (MSP_CLOSE_H - 14) / 2, 2, MSP_DIM, act);
        } else lit_button(&c, bx, by, bw, MSP_CLOSE_H, act);
        if (r->price) {
            char t[16]; snprintf(t, sizeof t, "%d", r->price);
            int w = 16 + 5 + text_w(t, 2), px0 = bx + (bw - w) / 2;
            blit_icon(&c, px0, by + MSP_CLOSE_H + 8, &icon_shop_sand_dollar_16, 255);
            draw_text(&c, px0 + 21, by + MSP_CLOSE_H + 9, 2, 0xffffff, t);
        } else draw_text(&c, bx + (bw - text_w("FREE", 2)) / 2, by + MSP_CLOSE_H + 9, 2, 0xffffff, "FREE");
        if (i + 1 < n) rect_fill(&c, x0, y + pitch - 11, x1 - x0, 1, MSP_DIM);
    }
    sh_footer(&c, NULL, true, wallet, "BACK", 1, 1);
}
int render_library_tap(float x, float y, int n) {
    int r = render_shelf_tap(x, y);                        /* the footer is the shelf's: BACK stands where CLOSE does */
    if (r == SHELF_TAP_CLOSE) return LIB_TAP_BACK;
    int x1 = TANK_W - SH_LIB_X0, bw = 116, pitch = SH_BOX_H + 22, y0 = SH_TITLE_Y + 46;
    for (int i = 0; i < n && i < 2; i++) {
        int by = y0 + i * pitch + 10;
        if (x >= x1 - bw - 8 && x < x1 + 8 && y >= by - 8 && y < by + MSP_CLOSE_H + 30) return LIB_TAP_ROW0 + i;   /* the button and the price under it */
    }
    return LIB_TAP_NONE;
}
