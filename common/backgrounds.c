/* backgrounds.c - THE BACKGROUNDS (0.4.1; backgrounds.h says what they are, docs/backgrounds-0.4.1.md why).
 *
 * Every look here was approved on the sim's renders first (2026-11-04, the mock-ups in wt-backdrop): Sunken
 * Ruins, Kelp Forest (still - its sway waits for band rendering), Twilight Deep (its drift is per frame, in
 * render.c), Lagoon (darker sand, lit at the crest), Rock Ridges, Reef Walls (from working-assets/corals: boulder
 * walls with coral on the stone) and Grotto (its walls measured from the glass's edge).
 *
 * Drawn ROW BY ROW inside render.c's water pass (bg_row): each pixel is quantized once, by the water pass.
 * bg_begin lays out what a background needs first (column heights, the reef's cell grids). Rules kept from the
 * mock-ups' measuring on the S3 (s3bench):
 * - no float divide, atan2, sqrt or sin per pixel (library calls on the S3): per-row and per-column tables;
 * - random draws are sequenced into locals, never several in one call's arguments (the order would differ
 *   between compilers: a different reef on the device than in the sim);
 * - every grid lives in the borrowed scratch.
 * Each pixel a row gives is tagged with its LAYER (far to near), which the boot show builds the background by.
 */
#include "backgrounds.h"
#include "tank.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

/* ---- the palettes ---- */
typedef struct { uint32_t top, mid, bot; float split, rest; } bg_water_t;   /* rest = 1 - split, as a literal */
static const bg_water_t BG_W[BG_N] = {
    [BG_OPEN]     = { 0x0a3c46, 0x08272f, 0x031015, 0.45f, 0.55f },   /* the tank as it always was (render.c's own constants) */
    [BG_RUINS]    = { 0x0c3842, 0x09252d, 0x031014, 0.45f, 0.55f },
    [BG_KELP]     = { 0x0e4536, 0x0a2b22, 0x03100c, 0.40f, 0.60f },
    [BG_TWILIGHT] = { 0x15235a, 0x0b1536, 0x040714, 0.35f, 0.65f },
    [BG_LAGOON]   = { 0x1a6670, 0x0e4750, 0x05222a, 0.55f, 0.45f },
    [BG_RIDGES]   = { 0x0b3b49, 0x082733, 0x030f16, 0.45f, 0.55f },
    [BG_REEF]     = { 0x0d4b54, 0x0a3138, 0x041417, 0.50f, 0.50f },
    [BG_GROTTO]   = { 0x0b3d48, 0x08272f, 0x031015, 0.45f, 0.55f },
    [BG_SHIP]     = { 0x16443f, 0x0c2a2a, 0x040f10, 0.45f, 0.55f },   /* (0.4.2) a murkier green-grey */
    [BG_VENT]     = { 0x1a3442, 0x10202a, 0x0e0c0a, 0.40f, 0.60f },   /* dark, warm at the floor */
    [BG_MANGROVE] = { 0x5e7448, 0x2c3e2c, 0x0d150d, 0.38f, 0.62f },   /* olive and silty, bright under the canopy, gone dark by the floor */
};
static const char *const BG_NAME[BG_N] = { "OPEN WATER", "ATLANTIS", "KELP FOREST", "TWILIGHT DEEP", "LAGOON", "ROCK RIDGES", "REEF WALLS", "GROTTO",
                                             "SUNKEN SHIP", "EMBERS", "MANGROVES" };   /* (EMBERS: Strato's title, 11-11, for the volcanic vent - BG_VENT in the code) */
static const uint8_t BG_LAYERS[BG_N] = { 0, 5, 2, 0, 1, 5, 5, 2, 4, 4, 3 };
const char *bg_name(int bg) { return (unsigned)bg < BG_N ? BG_NAME[bg] : BG_NAME[0]; }
static const char *const BG_WORDS[BG_N][2] = {     /* (approved by Strato 11-05) */
    { "THE TANK AS IT STARTED,", "CLEAR OPEN WATER" },
    { "OLD STONE COLUMNS,", "A TEMPLE LONG UNDER WATER" },
    { "A FOREST OF TALL KELP", "IN GREEN WATER" },
    { "DEEP BLUE WATER, A GLOW", "AND DRIFTING SPECKS" },
    { "BRIGHT SHALLOW WATER", "AND A SANDY SLOPE" },
    { "RIDGE AFTER RIDGE OF ROCK", "FADING INTO THE DEEP" },
    { "CORAL ON ROCKY WALLS,", "OPEN WATER BETWEEN" },
    { "LOOK OUT OF A CAVE", "INTO THE OPEN SEA" },
    { "AN OLD WOODEN SHIP", "RESTING ON THE SEABED" },          /* (0.4.2: Strato approved these three 2026-11-13) */
    { "HOT VENTS IN THE DARK,", "GLOWING CHIMNEYS" },
    { "TANGLED ROOTS REACHING", "DOWN INTO MURKY WATER" },
};
void bg_words(int bg, const char **l1, const char **l2) { if ((unsigned)bg >= BG_N) bg = 0; *l1 = BG_WORDS[bg][0]; *l2 = BG_WORDS[bg][1]; }
int bg_layers(int bg) { return (unsigned)bg < BG_N ? BG_LAYERS[bg] : 0; }

/* render.c's mix, to the bit (Open Water's water must come out exactly as it always has) */
static uint32_t mix(uint32_t a, uint32_t b, float p) {
    int ar = a >> 16 & 255, ag = a >> 8 & 255, ab = a & 255;
    int br = b >> 16 & 255, bg = b >> 8 & 255, bb = b & 255;
    return ((uint32_t)(ar + (br - ar) * p) << 16) |
           ((uint32_t)(ag + (bg - ag) * p) << 8) |
            (uint32_t)(ab + (bb - ab) * p);
}
uint32_t bg_water_rgb(int bg, int y) {
    const bg_water_t *w = &BG_W[(unsigned)bg < BG_N ? bg : 0];
    float p = (float)y / TANK_BOT;                 /* the gradient runs surface to floor; under a bowl's sand line it holds */
    if (p > 1) p = 1;
    return p < w->split ? mix(w->top, w->mid, p / w->split)
                        : mix(w->mid, w->bot, (p - w->split) / w->rest);
}

/* ---- helpers ---- */
static inline uint32_t bd_h(int a, int b) { uint32_t h = (uint32_t)a * 0x9E3779B1u ^ (uint32_t)b * 0x85EBCA77u; h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; return h; }
static inline float bd_n1(float x, int seed) {            /* 1-D value noise, 0..1 */
    int i = (int)floorf(x); float f = x - i; f = f * f * (3 - 2 * f);
    float a = (bd_h(i, seed) & 1023) / 1023.0f, b = (bd_h(i + 1, seed) & 1023) / 1023.0f;
    return a + (b - a) * f;
}
static inline uint32_t mix8(uint32_t a, uint32_t b, int t) {   /* t 0..256 */
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    return (uint32_t)(ar + ((((int)(b >> 16) & 255) - ar) * t >> 8)) << 16 | (uint32_t)(ag + ((((int)(b >> 8) & 255) - ag) * t >> 8)) << 8 | (uint32_t)(ab + (((int)(b & 255) - ab) * t >> 8));
}

/* ---- the scratch: one block for the bake, laid out per background ---- */
#define RW_CW (TANK_W / 2 + 1)
#define RW_CH (TANK_H / 2 + 1)
#define RG_GW 100
#define RG_GH 90
#define RL_MAX 48
#define KELP_MAX 18
#define CRUST_MAX 96
#define SH_LN 12                                  /* (0.4.2) the ship's lines, weed, gunports; the vent's chimneys and worms; the mangroves' roots */
#define SH_WD 28
#define SH_PT 8
#define VE_N 10
#define VE_WMAX 64
#define MG_MAX 80
#define MG_DMAX 24
typedef struct { float x0, amp, lean, wbase, haze, inv_h; int ytop, layer, s; uint8_t ph; uint32_t stalk, blade; } kelp_t;
typedef struct { int j, side; float tip, root; } ledge_t;
typedef struct { float x, y, r; uint32_t rgb; uint8_t lay; } disc_t;
/* THE REEF's layout, kept (0.4.1, D5): rock and coral in ONE byte a cell - 0 open water, low bits 7 = rock (its tone in
   bits 3..5), low bits 1..6 = coral (tone + 1, its color in bits 3..5, its group in bits 6..7) - and the per-row edges.
   None of it depends on the light, so while the reef is in use it is laid out once and kept (keep_take), and a light
   double-tap only colors its rows: the layout was ~70 ms of every rebuild on the 1.8. */
typedef struct { uint8_t mg[RW_CH][RW_CW]; float gle[RW_CH], gre[RW_CH]; int16_t fl[RW_CH], fr[RW_CH]; uint32_t tone[6][6]; } reef_keep_t;
typedef struct {
    uint32_t wrow[TANK_H];                        /* the water of each row */
    uint8_t  grow[TANK_H], gcol[TANK_W];          /* the glow's tables */
    bg_glow_t glow; bool has_glow;
    bool     glow_q;                              /* (0.4.2's three) the glow in steps of 4: a quarter of the mixes a row, and runs of one color for the water pass */
    uint32_t row[TANK_W]; uint8_t lay[TANK_W];    /* the row bg_row hands over */
    uint8_t  lit[TANK_W]; bool any_lit;           /* ... and how far each of its pixels lights itself, 0..255 (0.4.2: the vent) */
    uint8_t  nz[1024];                            /* (0.4.2) grain: a random byte a pixel by table, not a hash */
    uint32_t lut[256];                            /* a row's colors by weight (the glow's, Grotto's walls): one mix a weight, not a pixel */
    int      ymin;                                /* the first row with anything of the background on it */
    const reef_keep_t *reef;                      /* the reef's layout the rows read: the kept one, or this scratch's own */
    union {
        struct { kelp_t k[KELP_MAX]; int n; uint32_t row[TANK_W]; int8_t sn[256]; float bl[24]; } ke;
        struct { uint8_t ph[TANK_W]; int16_t top[TANK_W]; int8_t sn[256]; } la;
        struct { int16_t top[5][TANK_W]; int fsc[5]; } ri;
        struct { int16_t edge[TANK_H][2], gtop[TANK_W]; uint8_t ta[TANK_W + TANK_H], tb[TANK_W + 3 * TANK_H + 1]; disc_t d[CRUST_MAX]; int nd; } gr;
        struct { reef_keep_t k; uint8_t ct[RW_CH][RW_CW], rg[RG_GH][RG_GW]; ledge_t L[RL_MAX]; } re;
        struct { int16_t deck[TANK_W], keel[TANK_W], dep[TANK_W], mnd[TANK_W]; int32_t isd[TANK_W];                 /* (0.4.2) the ship */
                 uint8_t rail[TANK_W], hn[TANK_W], hz[TANK_W], sail[TANK_W], d7[TANK_H + 8], m7[TANK_H + 8], pk[1024], jag[32]; uint32_t tone[8];
                 struct { float x0, y0, y1, sl; uint8_t th; } ln[SH_LN]; struct { float x0, y0, x1, y1, w0, w1, inv; } ma[3];
                 struct { int16_t x, y0, y1; } wd[SH_WD]; struct { int16_t x0, x1, ya, yb; uint8_t at, open; } pt[SH_PT];
                 int nl, nw, np, xend, hy0, my0, mx0, post, bulw, bx0, bx1, bbeam, rat_a, rat_b, rat_y0, rat_y1, chx, chy0, chy1, plat_y, plat_x0, plat_x1;
                 float bx, by, ibrx, ibry, yx, yy, yhalf, ysl; } sh;
        struct { int16_t cx[VE_N][TANK_H], hw[VE_N][TANK_H], mt[TANK_W], wx[VE_WMAX], wy0[VE_WMAX], wy1[VE_WMAX]; int yt[VE_N], ph[VE_N], hr[VE_N], hu[VE_N], hinv[VE_N], nw, wtop, mtop, mlow; int8_t sn[64]; uint8_t ed[256]; } ve;
        struct { struct { float x0, reach, inv, r0, r1; int16_t y0, y1; uint8_t lay, trunk, ph; } r[MG_MAX]; int n, nd, ybank[2];
                 int16_t bank[2][TANK_W], dx[MG_DMAX], dl[MG_DMAX]; uint8_t pen[2][TANK_W], dlay[MG_DMAX], dph[MG_DMAX]; int8_t sn[64]; } mg;
    } u;
} scratch_t;
size_t bg_scratch_size(void) { return sizeof(scratch_t); }
static int bg_kept_now(void);                     /* (the keep, below) */
/* THE LOAN BY NEED (0.4.1 audit, the firmware reviewer's F1; Strato: "fix it"): a bake borrows only what this background's
   layout needs - the common rows (water, glow, the row handed over, its table) and its own part of the union. Reef Walls with
   its layout kept needs no union at all: a few KB, not ~130 - so a radio's buffers or a full grass loop can no longer squeeze
   the reef out of a light double-tap's rebuild. The layout bake (first time, or after another background) still takes it whole */
size_t bg_scratch_need(int bg) {
    const size_t base = offsetof(scratch_t, u);
    switch (bg) {
        case BG_KELP:   return base + sizeof(((scratch_t *)0)->u.ke);
        case BG_LAGOON: return base + sizeof(((scratch_t *)0)->u.la);
        case BG_RIDGES: return base + sizeof(((scratch_t *)0)->u.ri);
        case BG_GROTTO: return base + sizeof(((scratch_t *)0)->u.gr);
        case BG_SHIP:     return base + sizeof(((scratch_t *)0)->u.sh);
        case BG_VENT:     return base + sizeof(((scratch_t *)0)->u.ve);
        case BG_MANGROVE: return base + sizeof(((scratch_t *)0)->u.mg);
        case BG_REEF:   return bg_kept_now() == BG_REEF ? base : base + sizeof(((scratch_t *)0)->u.re);
        default:        return bg <= BG_OPEN || bg >= BG_N ? 0 : base;   /* (Atlantis, Twilight: rows and glow only) */
    }
}
static scratch_t *s_s;

/* THE KEEP (0.4.1, D5): a layout that does not change with the light, held between rebuilds while its background is in
   use - from the platform's lender (PSRAM on the boards; NULL = refused: the layout is built again each time, as before).
   One at a time; another background lets it go. */
static void *(*s_kalloc)(size_t); static void (*s_kfree)(void *);
static void *s_keep; static int s_keep_bg;
void bg_set_keep_lender(void *(*alloc)(size_t), void (*release)(void *)) { s_kalloc = alloc; s_kfree = release; }
void bg_keep_drop(void) { if (s_keep) { if (s_kalloc && s_kfree) s_kfree(s_keep); else free(s_keep); } s_keep = NULL; s_keep_bg = BG_OPEN; }
int  bg_kept(void) { return s_keep ? s_keep_bg : BG_OPEN; }
static int bg_kept_now(void) { return bg_kept(); }
static void *keep_take(int bg, size_t n) {
    bg_keep_drop();
    void *p = s_kalloc && s_kfree ? s_kalloc(n) : malloc(n);
    if (p) { s_keep = p; s_keep_bg = bg; }
    return p;
}
static inline uint32_t bd_far(uint32_t rgb, int y, float haze) { return mix(rgb, s_s->wrow[y], haze); }   /* a shape seen through the water: 0 its own, 1 the water */

/* the row's water - its glow mixed in - and no layers; then the shapes go over it */
static void row_water(int y) {
    const uint32_t w = s_s->wrow[y]; uint32_t *R = s_s->row;
    const int gk = s_s->has_glow ? s_s->grow[y] : 0;
    if (gk && s_s->glow_q) {                      /* (0.4.2: by fours - under the 565's own step and the dither's; by eights showed faint rings) */
        uint32_t *L = s_s->lut; const int qmax = gk * 255 >> 10;
        L[0] = w; for (int q = 1; q <= qmax; q++) L[q] = mix8(w, s_s->glow.rgb, q << 2);
        for (int x = 0; x < TANK_W; x++) R[x] = L[gk * s_s->gcol[x] >> 10];
    }
    else if (gk) {                                /* (the same mix as a pixel's, once per weight the row can reach) */
        uint32_t *L = s_s->lut; const int kmax = gk * 255 >> 8;
        L[0] = w; for (int k = 1; k <= kmax; k++) L[k] = mix8(w, s_s->glow.rgb, k);
        for (int x = 0; x < TANK_W; x++) R[x] = L[gk * s_s->gcol[x] >> 8];
    }
    else for (int x = 0; x < TANK_W; x++) R[x] = w;
    memset(s_s->lay, 0, TANK_W);
    if (s_s->any_lit) { memset(s_s->lit, 0, TANK_W); s_s->any_lit = false; }
}
static inline void row_span(int x0, int x1, uint32_t c, uint8_t l) {
    if (x0 < 0) x0 = 0;
    if (x1 > TANK_W - 1) x1 = TANK_W - 1;
    for (int x = x0; x <= x1; x++) { s_s->row[x] = c; s_s->lay[x] = l; }
}

/* ---- THE GLOWS: a soft light, separable - a row weight times a column weight ---- */
static void glow_layout(int bg) {
    scratch_t *s = s_s;
    float cx, rx, cy0, ry; int peak; uint32_t rgb;
    s->has_glow = false;
    memset(s->grow, 0, sizeof s->grow); memset(s->gcol, 0, sizeof s->gcol);
    switch (bg) {
        case BG_TWILIGHT: rgb = 0x3c5a9a; cx = TANK_W * 0.58f; rx = TANK_W * 0.55f; cy0 = 0; ry = TANK_BOT * 0.55f; peak = 115; break;   /* far above, a little right */
        case BG_VENT:     rgb = 0x863618; cx = TANK_W * 0.60f; rx = TANK_W * 0.44f; cy0 = TANK_BOT * 0.36f; ry = TANK_BOT * 0.44f; peak = 160; break;   /* (0.4.2) round the great chimney */
        case BG_MANGROVE: {                                                 /* (0.4.2) the light between the trees: soft shafts, gone by the floor */
            static const struct { float c, w; int k; } SHAFT[4] = { { 0.235f, 0.060f, 235 }, { 0.50f, 0.095f, 255 }, { 0.585f, 0.035f, 150 }, { 0.80f, 0.055f, 215 } };
            for (int x = 0; x < TANK_W; x++) {
                int g = 70;
                for (int i = 0; i < 4; i++) { float dx = (x - SHAFT[i].c * TANK_W) / (SHAFT[i].w * TANK_W), d = 1 - dx * dx; if (d > 0) { int v = 70 + (int)(d * d * (SHAFT[i].k - 70)); if (v > g) g = v; } }
                s->gcol[x] = (uint8_t)g;
            }
            for (int y = 0; y < TANK_BOT && y < TANK_H; y++) { float v = 1 - (float)y / (TANK_BOT * 0.82f); s->grow[y] = v > 0 ? (uint8_t)(v * v * 150) : 0; }
            s->glow = (bg_glow_t){ 0xb8cc94, s->grow, s->gcol }; s->has_glow = true; return;
        }
        case BG_GROTTO:   rgb = 0x2f8a96; cx = TANK_W * 0.52f; rx = TANK_W * 0.42f; cy0 = TANK_BOT * 0.40f; ry = TANK_W * 0.35f; peak = 120; break;   /* beyond the mouth */
        case BG_REEF:                                                       /* the light down the channel, strongest near the surface */
            for (int x = 0; x < TANK_W; x++) { float dx = (x - TANK_W * 0.5f) / (TANK_W * 0.32f), d = 1 - dx * dx; s->gcol[x] = d > 0 ? (uint8_t)(d * 255) : 0; }
            for (int y = 0; y < TANK_BOT && y < TANK_H; y++) { float v = (float)y / TANK_BOT; s->grow[y] = (uint8_t)(60 * (1 - v) * (1 - v)); }
            s->glow = (bg_glow_t){ 0x2c7c88, s->grow, s->gcol }; s->has_glow = true; return;
        default: return;
    }
    for (int x = 0; x < TANK_W; x++) { float dx = (x - cx) / rx, d = 1 - dx * dx; s->gcol[x] = d > 0 ? (uint8_t)(d * d * 255) : 0; }
    for (int y = 0; y < TANK_BOT && y < TANK_H; y++) { float dy = (y - cy0) / ry, d = 1 - dy * dy; s->grow[y] = d > 0 ? (uint8_t)(d * d * peak) : 0; }
    s->glow = (bg_glow_t){ rgb, s->grow, s->gcol }; s->has_glow = true;
}

/* ================================ 1 ATLANTIS ================================ */
/* five columns, a lintel across the first pair, a drum fallen on its side. Layers, far to near: the right pair, then
   the left pair and the lintel, then the broken one and the drum - a column at a time */
static const struct { float x, w, h; int broken; float haze; uint8_t lay; } RU_COL[] = {
    { 0.14f, 0.055f, 0.62f, 0, 0.58f, 3 }, { 0.30f, 0.055f, 0.62f, 0, 0.58f, 3 }, { 0.55f, 0.06f, 0.38f, 1, 0.45f, 4 },
    { 0.80f, 0.05f, 0.50f, 0, 0.60f, 2 }, { 0.92f, 0.05f, 0.30f, 1, 0.60f, 1 },
};
#define RU_N ((int)(sizeof RU_COL / sizeof RU_COL[0]))
static const uint32_t RU_STONE = 0x6a7a74;
static void ruins_begin(void) {
    int ymin = TANK_BOT;
    for (int k = 0; k < RU_N; k++) { int yt = (int)(TANK_BOT * (1 - RU_COL[k].h)); if (yt < ymin) ymin = yt; }
    int ly = (int)(TANK_BOT * 0.38f) - 10; if (ly < ymin) ymin = ly;
    s_s->ymin = ymin;
}
static const uint32_t *ruins_row(int y) {
    if (y < s_s->ymin || y >= TANK_BOT) return NULL;
    row_water(y);
    const uint32_t stone_dk = mix(RU_STONE, 0x000000, 0.35f);
    for (int k = 0; k < RU_N; k++) {
        int x0 = (int)((RU_COL[k].x - RU_COL[k].w / 2) * TANK_W), x1 = (int)((RU_COL[k].x + RU_COL[k].w / 2) * TANK_W), yt = (int)(TANK_BOT * (1 - RU_COL[k].h));
        if (y < yt) continue;
        uint32_t c = bd_far(RU_STONE, y, RU_COL[k].haze), dk = bd_far(stone_dk, y, RU_COL[k].haze);
        int a = x0, b = x1;
        if (RU_COL[k].broken) { int cut = yt + (int)((x1 - x0) * 0.6f); if (y < cut) a = x0 + (cut - y) * 2; }   /* a slanted break */
        if (y < yt + 6 && !RU_COL[k].broken) { a -= 4; b += 4; }                                                 /* the capital */
        if (a < 0) a = 0;
        if (b > TANK_W - 1) b = TANK_W - 1;
        for (int x = a; x <= b; x++) { s_s->row[x] = ((x - x0) % 4 == 3 && y > yt + 6) ? dk : c; s_s->lay[x] = RU_COL[k].lay; }   /* fluting */
    }
    int lx0 = (int)(0.10f * TANK_W), lx1 = (int)(0.34f * TANK_W), ly = (int)(TANK_BOT * 0.38f) - 10;     /* the lintel across the first pair */
    if (y >= ly && y < ly + 10) row_span(lx0, lx1, bd_far(RU_STONE, y, 0.68f), 3);
    if (y >= TANK_BOT - 22) row_span((int)(0.62f * TANK_W), (int)(0.74f * TANK_W), bd_far(y < TANK_BOT - 20 ? mix(RU_STONE, 0xc0d0c8, 0.3f) : RU_STONE, y, 0.5f), 5);   /* the drum */
    return s_s->row;
}

/* ================================ 2 KELP FOREST (still) ================================ */
static inline float cov(int x, float l, float r) { float a = x > l ? x : l, b = x + 1 < r ? x + 1 : r; return b > a ? b - a : 0; }
static void kelp_begin(void) {
    __typeof__(s_s->u.ke) *K = &s_s->u.ke;
    for (int i = 0; i < 256; i++) K->sn[i] = (int8_t)(sinf(i * 6.2832f / 256) * 127);
    for (int i = 0; i < 24; i++) K->bl[i] = i < 18 ? sinf(i / 18.0f * 3.14159f) : 0;
    K->n = 0; s_s->ymin = TANK_BOT;
    for (int layer = 0; layer < 2; layer++) {
        int n = layer ? 7 : 11;
        for (int s = 0; s < n; s++) {
            uint32_t h = bd_h(s, 40 + layer);
            kelp_t *k = &K->k[K->n++];
            k->layer = layer; k->s = s;
            k->x0 = (s + 0.5f + ((h & 255) / 255.0f - 0.5f) * 0.6f) * TANK_W / n;
            k->ytop = (int)(TANK_BOT * (0.02f + ((h >> 8) & 255) / 255.0f * 0.30f));
            k->amp = 3 + ((h >> 16) & 7); k->ph = (uint8_t)(((h >> 20) & 63) * 256 / 6.2832f);
            k->lean = (((h >> 26) & 31) - 15) * 0.03f;
            k->wbase = layer ? 2.6f : 1.8f; k->haze = layer ? 0.40f : 0.62f;
            k->stalk = layer ? 0x34461c : 0x2c4426; k->blade = mix(k->stalk, 0x6a7a2a, 0.3f);
            k->inv_h = 1.0f / (TANK_BOT - k->ytop);
            if (k->ytop < s_s->ymin) s_s->ymin = k->ytop;
        }
    }
}
static const uint32_t *kelp_row(int y) {
    if (y < s_s->ymin || y >= TANK_BOT) return NULL;
    __typeof__(s_s->u.ke) *K = &s_s->u.ke;
    row_water(y);
    const uint32_t w = s_s->wrow[y];
    const int sstep = (int)(0.035f * 256 / 6.2832f * 65536);   /* the stalk's wave: table steps a row, 16.16 */
    for (int i = 0; i < K->n; i++) {
        const kelp_t *k = &K->k[i];
        if (y < k->ytop) continue;
        int u = (int)((TANK_BOT - y) * k->inv_h * 255); if (u < 0) u = 0; if (u > 255) u = 255;
        float up = u / 255.0f;
        float cx = k->x0 + k->amp * K->sn[(uint8_t)(((y * sstep) >> 16) + k->ph)] * (1 / 127.0f) + k->lean * (TANK_BOT - y), wd = k->wbase * (1 - 0.5f * up);
        float sl = cx - wd, sr = cx + wd, bl = 1, br = 0;
        int seg = (y - k->ytop) % 24, side = ((y - k->ytop) / 24 + k->s) & 1;
        float len = K->bl[seg] * (k->layer ? 10 : 7) * (0.6f + 0.4f * up), off = seg / 18.0f * 4;
        if (len >= 0.5f) { if (side) { bl = cx + wd + off; br = bl + len; } else { br = cx - wd - off; bl = br - len; } }
        int ht = (int)((k->haze + 0.1f * up) * 256);
        uint32_t sc = mix8(k->stalk, w, ht), bc = mix8(k->blade, w, ht - 13);
        for (int pass = 0; pass < 2; pass++) {
            float l = pass ? bl : sl, r = pass ? br : sr; if (l > r) continue;
            int p0 = (int)l, p1 = (int)r; if (p0 < 0) p0 = 0; if (p1 > TANK_W - 1) p1 = TANK_W - 1;
            for (int x = p0; x <= p1; x++) { float c = cov(x, l, r); if (c > 0) { s_s->row[x] = c >= 1 ? (pass ? bc : sc) : mix8(s_s->row[x], pass ? bc : sc, (int)(c * 256)); s_s->lay[x] = (uint8_t)(1 + k->layer); } }
        }
    }
    return s_s->row;
}

/* ================================ 4 LAGOON ================================ */
static void lagoon_begin(void) {
    __typeof__(s_s->u.la) *L = &s_s->u.la;
    for (int i = 0; i < 256; i++) L->sn[i] = (int8_t)(sinf(i * 6.2832f / 256) * 127);
    s_s->ymin = TANK_BOT;
    for (int x = 0; x < TANK_W; x++) {
        L->ph[x] = (uint8_t)((x * 0.09f + 2 * bd_n1(x * 0.02f, 9)) * (256 / 6.2832f));
        L->top[x] = (int16_t)(TANK_BOT * (0.66f - 0.18f * bd_n1((float)x / TANK_W * 2.2f, 5)));
        if (L->top[x] < s_s->ymin) s_s->ymin = L->top[x];
    }
}
static const uint32_t *lagoon_row(int y) {
    if (y < s_s->ymin || y >= TANK_BOT) return NULL;
    __typeof__(s_s->u.la) *L = &s_s->u.la;
    row_water(y);
    const int ystep = (int)(0.6f * 256 / 6.2832f);   /* the ripple's phase per row, in table steps */
    /* (Strato 11-04: the sand "blends a little too closely with the grass") - darker sand, lit at the crest and sinking into
       shade down the slope, so a bed's green stands against it; 8 shades a row, by the depth below the crest at that column */
    float hz = 0.50f - 0.15f * (float)y / TANK_BOT;
    uint32_t lo[8], hi[8];
    for (int k = 0; k < 8; k++) { float d = k / 7.0f; lo[k] = bd_far(mix(0x9a8a62, 0x4a4232, d * 0.85f), y, hz); hi[k] = bd_far(mix(0xb4a478, 0x5c5240, d * 0.85f), y, hz); }
    for (int x = 0; x < TANK_W; x++) {
        if (y < L->top[x]) continue;
        int k = (y - L->top[x]) * 8 / (TANK_BOT - L->top[x] + 1); if (k > 7) k = 7;
        s_s->row[x] = L->sn[(uint8_t)(L->ph[x] + y * ystep)] > 119 ? hi[k] : lo[k]; s_s->lay[x] = 1;
    }
    return s_s->row;
}

/* ================================ 5 ROCK RIDGES ================================ */
#define RI_N 5
static const float RI_BASE[RI_N] = { 0.30f, 0.40f, 0.50f, 0.60f, 0.72f };    /* where each range sits, as a share of the water */
static const float RI_AMP[RI_N]  = { 0.07f, 0.10f, 0.13f, 0.15f, 0.16f };
static const float RI_FREQ[RI_N] = { 2.0f, 2.6f, 3.4f, 4.4f, 5.5f };
static const float RI_HAZE[RI_N] = { 0.62f, 0.48f, 0.34f, 0.20f, 0.06f };
static const uint32_t RI_ROCK[RI_N] = { 0x1a3a46, 0x15323d, 0x0f2730, 0x0a1c24, 0x061218 };
static void ridges_begin(void) {
    int16_t (*top)[TANK_W] = s_s->u.ri.top;
    s_s->ymin = TANK_BOT;
    for (int k = 0; k < RI_N; k++)
        for (int x = 0; x < TANK_W; x++) {
            float u = (float)x / TANK_W;
            float jag = k >= 2 ? (k - 1) * 0.012f * bd_n1(u * 40, 50 + k) : 0;      /* the near ranges are jagged */
            int t = (int)(TANK_BOT * (RI_BASE[k] - RI_AMP[k] * bd_n1(u * RI_FREQ[k], 31 + k) - RI_AMP[k] * 0.35f * bd_n1(u * RI_FREQ[k] * 3.3f, 41 + k) - jag));
            top[k][x] = (int16_t)t; if (t < s_s->ymin) s_s->ymin = t;
        }
    if (s_s->ymin < 0) s_s->ymin = 0;
    for (int k = 0; k < RI_N; k++)                  /* the mist's step: no divide a pixel - and none a row (0.4.1, D5: five float divides a row) */
        s_s->u.ri.fsc[k] = (int)((8 - 1) * 65536.0f / (TANK_BOT * (0.22f + 0.04f * k)));
}
static const uint32_t *ridges_row(int y) {
    if (y < s_s->ymin || y >= TANK_BOT) return NULL;
    int16_t (*top)[TANK_W] = s_s->u.ri.top;
    row_water(y);
    enum { FOG = 8 };
    uint32_t lut[RI_N][FOG], rim[RI_N]; int fsc[RI_N];
    for (int k = 0; k < RI_N; k++) {
        uint32_t c = bd_far(RI_ROCK[k], y, RI_HAZE[k]);
        int fm = (int)((0.10f + 0.05f * (RI_N - 1 - k)) * 256);             /* the far valleys the mistiest */
        for (int f = 0; f < FOG; f++) lut[k][f] = mix8(c, s_s->wrow[y], fm * f / (FOG - 1));
        rim[k] = bd_far(mix(RI_ROCK[k], 0x7aa4aa, 0.16f), y, RI_HAZE[k]);
        fsc[k] = s_s->u.ri.fsc[k];                                           /* (the mist's step, from the layout) */
    }
    for (int x = 0, m3 = 0; x < TANK_W; x++, m3 = m3 == 2 ? 0 : m3 + 1)
        for (int k = RI_N - 1; k >= 0; k--) {
            int d = y - top[k][x];
            if (d < 0) continue;
            uint32_t col;
            if (k >= 3 && d < 2) col = rim[k];                                   /* a lit crest on the near ranges */
            else {
                int f = (d * fsc[k]) >> 16; if (f > FOG - 1) f = FOG - 1;
                col = lut[k][f];
                if (k >= 3 && d > 2 && m3 == 1) { uint32_t h = bd_h(x / 3, k); if ((h & 15) == 0 && d < (int)(h >> 8 & 15) + 6) col = mix8(col, 0x000000, 77); }   /* short cracks under the crest */
            }
            s_s->row[x] = col; s_s->lay[x] = (uint8_t)(k + 1);
            break;
        }
    return s_s->row;
}

/* ================================ 7 GROTTO ================================ */
static const uint32_t GR_ROCK = 0x142022, GR_LIT = 0x4a6a66;
static void grotto_begin(void) {
    __typeof__(s_s->u.gr) *G = &s_s->u.gr;
    /* the rock's lumps: two 1-D noises along skewed lines, as tables (bd_n1 per pixel, twice, was most of the mock-up's bake) */
    for (int i = 0; i < TANK_W + TANK_H; i++) G->ta[i] = (uint8_t)(bd_n1(i * 0.08f, 21) * 255);                 /* along x + 5/8 y */
    for (int i = 0; i < TANK_W + 3 * TANK_H + 1; i++) G->tb[i] = (uint8_t)(bd_n1((i - TANK_W) * 0.03f, 22) * 255);   /* along 3y - x */
    for (int y = 0; y < TANK_BOT; y++) {                                  /* the walls, measured in from the glass's edge (the bowl: its circle) */
        float v = (float)y / TANK_BOT, gl = 0, gr = TANK_W - 1;
#ifdef TANK_ROUND
        { float R = TANK_W * 0.5f, dy = y - TANK_H * 0.5f; float hw = dy * dy < R * R ? sqrtf(R * R - dy * dy) : 0; gl = TANK_W * 0.5f - hw; gr = TANK_W * 0.5f + hw; }
#endif
        float tk = TANK_W * (0.07f + 0.13f * (1 - v) * (1 - v));
        G->edge[y][0] = (int16_t)(gl + tk + TANK_W * 0.05f * bd_n1(y * 0.06f, 12) + TANK_W * 0.015f * bd_n1(y * 0.21f, 13));
        G->edge[y][1] = (int16_t)(gr - tk - TANK_W * 0.05f * bd_n1(y * 0.06f, 14) - TANK_W * 0.015f * bd_n1(y * 0.19f, 15));
    }
    const float gx = TANK_W * 0.52f;
    for (int x = 0; x < TANK_W; x++) {                                    /* the overhang across the top, from the glass's top edge */
        float u = (x - gx) / (TANK_W * 0.5f), gt = 0;
#ifdef TANK_ROUND
        { float R = TANK_W * 0.5f, dx = x - TANK_W * 0.5f; gt = TANK_H * 0.5f - (dx * dx < R * R ? sqrtf(R * R - dx * dx) : 0); }
#endif
        G->gtop[x] = (int16_t)(gt + TANK_H * (0.05f + 0.07f * u * u + 0.04f * bd_n1(x * 0.03f, 6) + 0.015f * bd_n1(x * 0.13f, 7)));
    }
    static const uint32_t CR[4] = { 0xb0705a, 0x9a7aa8, 0xc0a060, 0x70a090 };   /* the crust: a few clumps low on the walls, where the light reaches */
    G->nd = 0;
    for (int i = 0; i < 9; i++) {
        uint32_t h = bd_h(i, 200);
        int y = (int)(TANK_BOT * (0.45f + (h & 1023) / 1023.0f * 0.45f)), side = (h >> 10) & 1;
        if (y >= TANK_BOT) continue;
        float x = side ? G->edge[y][1] + 3 : G->edge[y][0] - 3; uint32_t c = CR[(h >> 16) & 3];
        for (int k = 0; k < 5 && G->nd + 2 <= CRUST_MAX; k++) { uint32_t g = bd_h(i, k + 300); float r = 2.5f + (g & 3);
            float px = x + ((int)(g >> 4 & 15) - 7) * (side ? -1 : 1) * 0.8f, py = y - (float)(g >> 8 & 15);
            G->d[G->nd++] = (disc_t){ px, py, r, c, 2 };
            G->d[G->nd++] = (disc_t){ px - 0.6f, py - r * 0.4f, r * 0.45f, mix(c, 0xffffff, 0.25f), 2 }; }
    }
    s_s->ymin = 0;
}
static const uint32_t *grotto_row(int y) {
    if (y >= TANK_BOT) return NULL;
    __typeof__(s_s->u.gr) *G = &s_s->u.gr;
    row_water(y);
    const uint32_t w = s_s->wrow[y], rk = mix8(GR_ROCK, w, 20), lk = mix8(GR_LIT, w, 20);
    uint32_t lip[12]; for (int d = 0; d < 12; d++) lip[d] = mix8(lk, rk, d * 256 / 12);   /* the lip catches the light from outside */
    uint32_t *wl = s_s->lut; for (int t = 0; t < 90; t++) wl[t] = mix8(rk, lk, t);        /* the lumps' 90 shades (255 * 255 * 90 >> 16 = 89) */
    const int l = G->edge[y][0], r = G->edge[y][1], sk = (y * 5) >> 3, sb = 3 * y + TANK_W;
    for (int x = 0; x < TANK_W; x++) {
        int din = x < l ? l - x : x > r ? x - r : -1;
        if (din < 0) { if (r > x) x = r; continue; }                          /* (the open mouth: on to the far wall) */
        s_s->row[x] = din < 12 ? lip[din] : wl[(G->ta[x + sk] * G->tb[sb - x] * 90) >> 16];   /* rounded lumps */
        s_s->lay[x] = 1;
    }
    for (int k = 0; k < G->nd; k++) {                                       /* the crust */
        const disc_t *d = &G->d[k]; float dy = y - d->y, w2 = d->r * d->r - dy * dy;
        if (w2 < 0) continue;
        float hw = sqrtf(w2); row_span((int)(d->x - hw + 0.5f), (int)(d->x + hw - 0.5f), bd_far(d->rgb, y, 0.3f), d->lay);
    }
    for (int x = 0; x < TANK_W; x++) {                                      /* the overhang */
        int d = G->gtop[x] - y; if (d <= 0) continue;
        s_s->row[x] = d < 10 ? mix8(lk, rk, d * 256 / 10) : rk; s_s->lay[x] = 1;
    }
    return s_s->row;
}

/* ================================ 6 REEF WALLS ================================ */
/* The valley's walls are piles of rounded boulders (lit upper left, shaded lower right, a dark seam under each; the ones
   near the face bulge past it), moss on the flat tops, and coral ON the stone, saturated, two or three a ledge high up,
   small kinds low where a grown bed stands (Strato 11-04: walls up the sides, "less geometry behind where the grass
   grows"; then working-assets/corals for the look; then "the rock bits look too rectangular" -> boulders). Laid out in
   bg_begin as two cell grids (2 px a cell): the rock's tone (rwt) and the coral over it (ct: tone | color | group);
   bg_row reads them. A colony is built on its own grid and toned in the live coral's language (render.c coral_build). */
enum { RG_NONE, RG_BODY, RG_TIP, RG_GROOVE, RG_HOLE };
static uint8_t (*rg)[RG_GW];
static int rg_x0, rg_y0, rg_x1, rg_y1, rg_k, rg_dir = 1;
static uint32_t rg_seed;
static inline void rg_grow(int i, int j) { if (i < rg_x0) rg_x0 = i; if (i > rg_x1) rg_x1 = i; if (j < rg_y0) rg_y0 = j; if (j > rg_y1) rg_y1 = j; }
static inline float rg_rnd(void) { return (bd_h((int)rg_seed, rg_k++) & 1023) / 1023.0f; }   /* the colony's next number (one per call: sequence them) */
static void rg_disc(float cx, float cy, float r, uint8_t v) {
    int j0 = (int)(cy - r), j1 = (int)(cy + r) + 1, i0 = (int)(cx - r), i1 = (int)(cx + r) + 1; float r2 = r * r;
    for (int j = j0; j <= j1; j++) { if ((unsigned)j >= RG_GH) continue; float dy = j + 0.5f - cy, dy2 = dy * dy;
        for (int i = i0; i <= i1; i++) {
            if ((unsigned)i >= RG_GW) continue;
            float dx = i + 0.5f - cx; if (dx * dx + dy2 > r2) continue;
            if (v == RG_BODY && (rg[j][i] == RG_TIP || rg[j][i] == RG_HOLE)) continue;   /* a body never covers a tip or a mouth (a groove it may: a polyp's ring) */
            rg[j][i] = v; rg_grow(i, j);
        } }
}
static void rg_capsule(float x0, float y0, float x1, float y1, float r0, float r1, uint8_t v) {
    float dx = x1 - x0, dy = y1 - y0, rm = r0 < r1 ? r0 : r1, st = rm * 0.9f < 0.5f ? 0.5f : rm * 0.9f;   /* stamps spaced by the limb's own thickness (0.9: half the work of 0.55, the same look) */
    int n = (int)(sqrtf(dx * dx + dy * dy) / st) + 1;
    for (int k = 0; k <= n; k++) { float t = (float)k / n; rg_disc(x0 + dx * t, y0 + dy * t, r0 + (r1 - r0) * t, v); }
}
static void rg_branch(float x, float y, float ang, float len, float r, int depth, int kids, float spread, float curl) {
    float a = ang, px = x, py = y;
    for (int s = 0; s < 3; s++) {
        float ja = rg_rnd(), jr = rg_rnd();
        a += (ja - 0.5f) * curl;
        float nx = px + cosf(a) * len / 3, ny = py - sinf(a) * len / 3, rr = r * (1 - 0.08f * s);
        rg_capsule(px, py, nx, ny, rr * (0.9f + 0.2f * jr), rr * 0.92f, RG_BODY);
        px = nx; py = ny;
    }
    if (depth == 0) { rg_disc(px, py, r * 1.15f + 0.3f, RG_TIP); return; }
    int n = kids + (rg_rnd() < 0.3f);
    for (int k = 0; k < n; k++) {
        float off = n == 1 ? 0 : (k - (n - 1) * 0.5f) / ((n - 1) * 0.5f);
        float ja = (rg_rnd() - 0.5f) * 0.3f, jl = 0.68f + 0.18f * rg_rnd();
        rg_branch(px, py, a + off * spread + ja, len * jl, r * 0.74f, depth - 1, kids, spread, curl);
    }
}
enum { CK_ANTLER, CK_FAN, CK_FRONDS, CK_BUTTONS, CK_PLATES };
static void rg_colony(int kind, float sc) {
    const float bx = RG_GW * 0.5f, by = RG_GH - 2;
    switch (kind) {
        case CK_ANTLER: {                                                   /* antler coral: a bush of thin forking branches, knobbed tips (pink, orange) */
            int n = 2 + (rg_rnd() < 0.6f);
            for (int k = 0; k < n; k++) { float a = 1.5708f + (k - (n - 1) * 0.5f) * 0.55f + rg_dir * 0.18f, l = (5.5f + 2 * rg_rnd()) * sc;
                rg_branch(bx + (k - (n - 1) * 0.5f) * 2 * sc, by, a, l, 1.25f * sc, 3, 2, 0.72f, 0.45f); }
        } break;
        case CK_FAN: {                                                      /* sea fan: a short trunk under a dense ribbed half-disc, a pale rim (purple) */
            float R = (11 + 5 * rg_rnd()) * sc, cy = by - 3 * sc, ph = rg_rnd() * 6;
            rg_capsule(bx, by, bx, cy, 1.4f * sc, 1.1f * sc, RG_BODY);
            enum { FA = 64 };                                               /* drawn by stepping along it: no trigonometry per cell */
            float ca[FA], sa[FA], ed[FA];
            for (int k = 0; k < FA; k++) { float th = 0.25f + 2.64f * k / (FA - 1); ca[k] = cosf(th); sa[k] = sinf(th); ed[k] = R * (1 + 0.07f * sinf(th * 5 + ph) + 0.04f * sinf(th * 11 + ph * 2)); }
#define FAN_SET(xx, yy, vv) do { int _i = (int)(xx), _j = (int)(yy); if ((unsigned)_j < RG_GH && (unsigned)_i < RG_GW && (vv == RG_TIP || !rg[_j][_i])) { rg[_j][_i] = vv; rg_grow(_i, _j); } } while (0)
            for (int k = 0; k < FA; k += 4) for (float r = 0; r < ed[k]; r += 0.5f) { float bend = 0.15f * r / R; FAN_SET(bx + (ca[k] * (1 - bend) - sa[k] * bend) * r, cy - (sa[k] + ca[k] * bend) * r, RG_BODY); }   /* the ribs */
            for (float r = 2.4f; r < R; r += 2.4f) for (int k = 0; k < FA; k++) if (r < ed[k]) {                                                                   /* the arcs */
                float rr = r + 0.6f * ((k * 7 + (int)r) % 3 - 1) * 0.5f; FAN_SET(bx + ca[k] * rr, cy - sa[k] * rr, RG_BODY);
                if (k + 1 < FA) FAN_SET(bx + (ca[k] + ca[k + 1]) * 0.5f * rr, cy - (sa[k] + sa[k + 1]) * 0.5f * rr, RG_BODY); }
            for (int k = 0; k < FA; k++) for (float d = 0; d < 1.3f; d += 0.5f) FAN_SET(bx + ca[k] * (ed[k] - d), cy - sa[k] * (ed[k] - d), RG_TIP);   /* the pale rim */
            for (int k = 0; k + 1 < FA; k++) { float r = ed[k] - 0.6f; FAN_SET(bx + (ca[k] + ca[k + 1]) * 0.5f * r, cy - (sa[k] + sa[k + 1]) * 0.5f * r, RG_TIP); }
#undef FAN_SET
        } break;
        case CK_FRONDS: {                                                   /* fronds: upright stalks with little alternating nubs (teal) */
            int n = 3 + (int)(rg_rnd() * 3);
            for (int k = 0; k < n; k++) {
                float hj = rg_rnd(), bj = rg_rnd();
                float x = bx + (k - (n - 1) * 0.5f) * 2.4f * sc, h = (9 + 9 * hj) * sc, bend = (bj - 0.5f) * 0.6f + rg_dir * 0.15f;
                float px = x, py = by;
                for (int s = 1; s <= (int)h; s++) {
                    float t = (float)s / h, nx = x + bend * s * t, ny = by - s;
                    rg_capsule(px, py, nx, ny, 0.85f * sc, 0.8f * sc, RG_BODY);
                    if (s % 2 == 0 && s < h - 1) rg_disc(nx + ((s / 2) & 1 ? 1.4f : -1.4f) * sc, ny, 0.75f * sc, RG_BODY);
                    px = nx; py = ny;
                }
                rg_disc(px, py, 1.0f * sc, RG_TIP);
            }
        } break;
        case CK_BUTTONS: {                                                  /* button polyps: a heap of round heads, each lit and outlined (yellow) */
            int n = 5 + (int)(rg_rnd() * 4); float W = (3 + 2 * rg_rnd()) * sc;
            for (int k = 0; k < n; k++) {
                float uj = rg_rnd(), rj = rg_rnd(), yj = rg_rnd();
                float u = (k + 0.5f) / n * 2 - 1 + (uj - 0.5f) * 0.25f, r = (1.3f + 0.5f * rj) * sc;
                float x = bx + u * W, y = by - r - (1 - u * u) * W * 0.5f * yj;
                rg_disc(x, y, r, RG_GROOVE);                                 /* (a dark ring first: the heads stay apart) */
                rg_disc(x, y, r - 0.7f, RG_BODY);
                rg_disc(x - 0.35f * r, y - 0.4f * r, 0.45f * r, RG_TIP);
            }
        } break;
        default: {                                                          /* plate shelves: discs seen a little from above - a lit top, a dark underside (orange) */
            int n = 2 + (rg_rnd() < 0.5f);
            for (int k = 0; k < n; k++) {
                float w = (7 + 3 * rg_rnd() - k) * sc, h = w * 0.38f, cx = bx + rg_dir * (w * 0.5f + k * 2.5f * sc), cy = by - h * 0.6f - k * h * 1.05f;
                rg_capsule(bx, by, cx - rg_dir * w * 0.3f, cy + h * 0.3f, 1.1f * sc, 0.9f * sc, RG_BODY);
                float iw = 1 / w, ih = 1 / h;
                for (int j = (int)(cy - h) - 1; j <= (int)(cy + h) + 1; j++) for (int i = (int)(cx - w) - 1; i <= (int)(cx + w) + 1; i++) {
                    if ((unsigned)j >= RG_GH || (unsigned)i >= RG_GW) continue;
                    float dx = (i + 0.5f - cx) * iw, dy = (j + 0.5f - cy) * ih, d = dx * dx + dy * dy;
                    if (d > 1) continue;
                    rg[j][i] = d > 0.72f && dy < 0.2f ? RG_TIP : dy > 0.25f ? RG_GROOVE : RG_BODY; rg_grow(i, j);
                }
            }
        } break;
    }
}
/* the corals' colors (ct's color index) and their tones (ct's tone): saturated, a firm outline */
static const uint32_t RF_COL[6] = { 0xe8508a, 0xe8642a, 0x8a48c8, 0x2ab8a0, 0xd8b030, 0xd86040 };   /* pink, orange, purple, teal, yellow, plate orange */
enum { T_RIM, T_BODY, T_SHADE, T_LIT, T_TIP, T_HOLE, T_N };
#define CT(tone, col, grp) ((uint8_t)((1 + (tone)) | ((col) << 3) | ((grp) << 6)))   /* (0 = no coral) */
static const uint32_t RK[7] = { 0x2e4e4c, 0x3e6460, 0x1e3436, 0x4a706a, 0x122224, 0x9aae4c, 0x6e8636 };   /* the rock: body, light, dark, face, under, moss lit, moss */
static void reef_begin(void) {
    if (s_keep && s_keep_bg == BG_REEF) { s_s->reef = s_keep; s_s->ymin = 0; return; }   /* laid out already: the light changes only its colors */
    __typeof__(s_s->u.re) *R = &s_s->u.re;
    uint8_t (*rw)[RW_CW] = R->k.mg;                                       /* first the rock's shape (and its boulders' shading), then its tone in place */
    rg = R->rg; memset(R->rg, 0, sizeof R->rg); memset(R->ct, 0, sizeof R->ct); memset(&R->k, 0, sizeof R->k);
    const int gw = TANK_W / 2, gh = TANK_BOT / 2;
    const float S = TANK_W / 448.0f;
    for (int j = 0; j < gh; j++) {                                       /* the glass's edges per cell row (the bowl: its circle) */
        R->k.gle[j] = 0; R->k.gre[j] = TANK_W - 1;
#ifdef TANK_ROUND
        { float Rr = TANK_W * 0.5f, dy = j * 2.0f - TANK_H * 0.5f; float hw = dy * dy < Rr * Rr ? sqrtf(Rr * Rr - dy * dy) : 0; R->k.gle[j] = TANK_W * 0.5f - hw; R->k.gre[j] = TANK_W * 0.5f + hw; }
#endif
    }
    /* the walls, block by block: a ledge, a near-straight face, cut back under its foot; deep and shallow in turn */
    int nl = 0;
    for (int side = 0; side < 2; side++) {
        int j = (int)(gh * 0.03f) + (side ? 4 : 0), k = 0;
        while (j < gh && nl < RL_MAX) {
            uint32_t h = bd_h(k++, 400 + side);
            int len = (int)((14 + (h & 7)) * S); if (len < 10) len = 10;
            float p = ((k & 1) ? 4 + ((h >> 4) & 3) : 10 + ((h >> 4) & 7)) * S;
            for (int jj = j; jj < j + len && jj < gh; jj++) {
                float v = jj / (float)gh, t = (float)(jj - j) / len;
                float base = (TANK_W * (0.07f + 0.13f * v * v) + TANK_W * 0.025f * bd_n1(jj * 0.08f, 70 + side)) / 2;
                float prot = p * (t < 0.6f ? 1 - 0.12f * t : 0.93f - 2.6f * (t - 0.6f)) + ((bd_h(jj, 450 + side) & 3) == 0 ? 0.6f : 0);
                prot += 1.6f * bd_n1(jj * 0.22f + k * 5.1f, 470 + side) - 0.8f;  /* a ragged face, not a ruled line */
                if (t < 0.1f) prot -= (1 - t / 0.1f) * 1.8f;                    /* the ledge's lip rounded over */
                if (prot < 0) prot = 0;
                if (side == 0) { int edge = (int)(R->k.gle[jj] / 2 + base + prot); for (int i = 0; i <= edge && i < gw; i++) rw[jj][i] = RG_BODY; }
                else { int edge = (int)(R->k.gre[jj] / 2 - base - prot); for (int i = edge < 0 ? 0 : edge; i < gw; i++) rw[jj][i] = RG_BODY; }
            }
            { float v = j / (float)gh, base = (TANK_W * (0.07f + 0.13f * v * v)) / 2;
              float root = side ? R->k.gre[j] / 2 - base : R->k.gle[j] / 2 + base;
              R->L[nl++] = (ledge_t){ j, side, side ? root - p : root + p, root }; }
            j += len;
        }
    }
    /* the stone: rounded boulders stamped over one another (their shading in the cell's high bits) */
    for (int side = 0; side < 2; side++)
        for (int k = 0; k < (int)(260 * S * S); k++) {
            uint32_t h = bd_h(k, 520 + side);
            int j = (int)((h & 1023) / 1023.0f * gh);
            if (j >= gh) continue;
            int face;
            if (side == 0) { int i = 0; while (i < gw && rw[j][i]) i++; face = i - 1; if (face < 0) continue; }
            else { int i = gw - 1; while (i >= 0 && rw[j][i]) i--; face = i + 1; if (face >= gw) continue; }
            float depth = ((h >> 10) & 255) / 255.0f, r = (2.5f + ((h >> 18) & 7) * 0.6f) * S;
            float cx = side ? face + 1 + depth * depth * 34 * S - r * 0.35f : face - depth * depth * 34 * S + r * 0.35f, cy = j;
            bool bulge = depth < 0.15f;
            float irx = 1 / (r * 1.2f), iry = 1 / r;
            for (int jj = (int)(cy - r) - 1; jj <= (int)(cy + r) + 1; jj++) for (int ii = (int)(cx - r * 1.2f) - 1; ii <= (int)(cx + r * 1.2f) + 1; ii++) {
                if ((unsigned)jj >= (unsigned)gh || (unsigned)ii >= (unsigned)gw) continue;
                float dx = (ii + 0.5f - cx) * irx, dy = (jj + 0.5f - cy) * iry, d = dx * dx + dy * dy;
                if (d > 1) continue;
                if (!rw[jj][ii]) { if (!bulge || d > 0.8f) continue; rw[jj][ii] = RG_BODY; }
                float lit = -(dx * 0.7f + dy * 0.9f);                          /* the light from the upper left */
                int l = d > 0.78f && dy > 0.1f ? 3 : lit > 0.42f ? 1 : lit < -0.38f ? 2 : 0;
                rw[jj][ii] = (uint8_t)((rw[jj][ii] & 7) | l << 4);
            }
        }
    /* the near faces per cell row (the far rock shows only beyond them) */
    for (int j = 0; j < gh; j++) { int a = 0, b = gw - 1; while (a < gw && rw[j][a]) a++; while (b > 0 && rw[j][b]) b--; R->k.fl[j] = (int16_t)a; R->k.fr[j] = (int16_t)b; }
    /* the moss: along each ledge's top where nothing stands on it, cushions bumping up */
    for (int q = 0; q < nl; q++) {
        const ledge_t *l = &R->L[q]; int j = l->j;
        int a = (int)(l->side ? l->tip : l->root - 3), b = (int)(l->side ? l->root + 3 : l->tip);
        for (int i = a < 0 ? 0 : a; i <= b && i < gw; i++) {
            if (!rw[j][i] || (j > 0 && (rw[j - 1][i] & 7) == RG_BODY)) continue;
            rw[j][i] = RG_TIP; if (j + 1 < gh && rw[j + 1][i]) rw[j + 1][i] = RG_GROOVE;
            if (j > 1 && (bd_h(i / 2, j + 960) % 3) == 0) { rw[j - 1][i] = RG_TIP; if ((bd_h(i, j + 961) & 3) == 0 && j > 2) rw[j - 2][i] = RG_TIP; }
        }
    }
    /* the rock's tone per cell, in place, top to bottom: each cell reads the shape above it (cells above are
       already tones: 1 + tone, nonzero, as the shape was) and below it (not yet) - so the tone of a cell is
       decided from its neighbors' presence only, which tones keep */
    {
        static const uint8_t NONE = 0;
        for (int j = 0; j < gh; j++)
            for (int i = 0; i < gw; i++) {
                uint8_t cell = rw[j][i];
                if (cell == NONE) continue;
                int t, v = cell & 7;
                bool up0 = j == 0 || !rw[j - 1][i], dn = j == gh - 1 || !rw[j + 1][i];
                bool side = (i > 0 && !rw[j][i - 1]) || (i < gw - 1 && !rw[j][i + 1]);
                bool under = j + 2 < gh && !rw[j + 2][i], under2 = j + 3 < gh && !rw[j + 3][i];
                if (v == RG_TIP && !(cell & 0x80)) t = 5;                                             /* the moss cap and its cushions */
                else if (v == RG_GROOVE && !(cell & 0x80)) t = (bd_h(i, j + 900) & 1) ? 6 : 5;       /* its second row, ragged */
                else if (j > 1 && (rw[j - 2][i] & 0x80) && (rw[j - 2][i] & 7) == 7 && (bd_h(i, j + 901) % 3) == 0) t = 6;   /* (a few strands down the face, under the cap's second row) */
                else if (up0 && i > 0 && i < gw - 1 && (j == 0 || (!rw[j - 1][i - 1] && !rw[j - 1][i + 1]))) t = 5;   /* a flat run of stone open above: moss */
                else if (j > 1 && i > 0 && i < gw - 1 && rw[j - 1][i] && !rw[j - 2][i] && !rw[j - 2][i - 1] && !rw[j - 2][i + 1] && (bd_h(i, j + 902) & 1)) t = 6;
                else if (up0) t = 3;                                                     /* a lone top: lit rock */
                else if (dn || under) t = 4;                                             /* the overhang's shadow */
                else if (under2) t = 2;
                else if (side) t = 3;
                else { int l = (cell >> 4) & 3; t = l == 1 ? 1 : l == 2 ? 2 : l == 3 ? 4 : 0; }   /* the boulders' own shading */
                /* (a cell already toned is 0x80 | tone; a groove's moss row keeps 7 in the low bits for the strands test) */
                rw[j][i] = (uint8_t)(0x80 | (v == RG_GROOVE ? 7 : 0) << 0 | (uint8_t)t << 3);
            }
    }
    /* the coral, on the ledges: each colony built on its own grid, toned, and laid into ct by cell */
    for (int q = 0; q < nl; q++) {
        const ledge_t *l = &R->L[q];
        float v = l->j / (float)gh;
        if (v > 0.92f) continue;
        bool low = v > 0.58f;
        float span = l->side ? l->root - l->tip : l->tip - l->root;
        int n = low ? 1 + (span > 8 * S) : 2 + (span > 9 * S);
        for (int c = 0; c < n; c++) {
            uint32_t h = bd_h(q * 5 + c, 500);
            int pick = (int)((h >> 3) % 100), kind;
            if (low) kind = pick < 60 ? CK_BUTTONS : pick < 88 ? CK_FRONDS : CK_PLATES;   /* (plates rare down low: a column of them reads as bricks) */
            else kind = pick < 36 ? CK_ANTLER : pick < 60 ? CK_FAN : pick < 78 ? CK_FRONDS : pick < 92 ? CK_BUTTONS : CK_PLATES;
            int col = kind == CK_ANTLER ? ((h >> 12) & 1 ? 0 : 1) : kind == CK_FAN ? ((h >> 13) & 3 ? 2 : 0) : kind == CK_FRONDS ? 3 : kind == CK_BUTTONS ? 4 : 5;
            float u = n == 1 ? 0.65f : 0.15f + 0.75f * c / (n - 1) + ((h >> 26) & 7) * 0.012f;
            float xc = (l->root + (l->tip - l->root) * u) * 2;
            float sc = (low ? 1.1f : 1.75f) * S * (0.85f + 0.3f * ((h >> 20) & 15) / 15.0f);
            rg_dir = l->side ? -1 : 1;
            rg_x0 = RG_GW; rg_y0 = RG_GH; rg_x1 = -1; rg_y1 = -1; rg_seed = h; rg_k = 0;
            rg_colony(kind, sc);
            if (rg_x1 < rg_x0) continue;
            const int oi = ((int)xc - RG_GW) >> 1, oj = l->j + 2 - (RG_GH - 2), grp = q % 3;   /* (its grid laid by cell: x to the even px) */
            for (int j = rg_y0; j <= rg_y1; j++) {
                int cj = oj + j; if ((unsigned)cj >= (unsigned)gh) continue;
                for (int i = rg_x0; i <= rg_x1; i++) {
                    uint8_t cv = rg[j][i]; if (!cv) continue;
                    int ci = oi + i; if ((unsigned)ci >= (unsigned)gw) continue;
                    bool ol = i == 0 || !rg[j][i - 1], ou = j == 0 || !rg[j - 1][i], orr = i == RG_GW - 1 || !rg[j][i + 1], od = j == RG_GH - 1 || !rg[j + 1][i];
                    int t;
                    if (cv == RG_HOLE) t = T_HOLE;
                    else if (cv == RG_GROOVE) t = T_RIM;
                    else if (cv == RG_TIP) t = (orr || od) && !(ol || ou) ? T_RIM : T_TIP;
                    else if (ol || ou || orr || od) t = (orr || od) ? T_RIM : T_LIT;
                    else t = bd_h(i + q * 7 + c, j) % 100 < 10 ? T_SHADE : T_BODY;
                    R->ct[cj][ci] = CT(t, col, grp);
                }
            }
            for (int jj = rg_y0; jj <= rg_y1; jj++) memset(&rg[jj][rg_x0], 0, (size_t)(rg_x1 - rg_x0 + 1));
        }
    }
    for (int c = 0; c < 6; c++) {                                         /* the corals' tones, once */
        const uint32_t b = RF_COL[c];
        R->k.tone[c][T_RIM] = mix(b, 0x140a24, 0.62f); R->k.tone[c][T_BODY] = b; R->k.tone[c][T_SHADE] = mix(b, 0x000000, 0.22f);
        R->k.tone[c][T_LIT] = mix(b, 0xffffff, 0.26f); R->k.tone[c][T_TIP] = mix(b, 0xffffff, 0.50f); R->k.tone[c][T_HOLE] = mix(b, 0x05080a, 0.75f);
    }
    for (int j = 0; j < gh; j++)                                          /* rock and coral into one byte a cell (the coral over the rock) */
        for (int i = 0; i < gw; i++) {
            uint8_t c = R->ct[j][i], r = rw[j][i];
            rw[j][i] = c ? c : r ? (uint8_t)(7 | (r & 0x38)) : 0;
        }
    s_s->reef = &R->k;
    void *kp = keep_take(BG_REEF, sizeof(reef_keep_t));                   /* kept for the next rebuild, if the lender has room */
    if (kp) { memcpy(kp, &R->k, sizeof R->k); s_s->reef = kp; }
    s_s->ymin = 0;
}
static const uint32_t *reef_row(int y) {
    if (y >= TANK_BOT) return NULL;
    const reef_keep_t *K = s_s->reef;
    const int j = y >> 1, gh = TANK_BOT / 2;
    if (j >= gh) return NULL;
    row_water(y);
    const uint32_t w = s_s->wrow[y];
    /* the colors of this row: the rock's 7 tones, the corals' 6 x 6, the far rock's 2 */
    uint32_t rk[7], cc[6][T_N];
    for (int k = 0; k < 7; k++) rk[k] = mix8(RK[k], w, 41);                 /* (the rock: 0.16 of the water) */
    for (int c = 0; c < 6; c++) for (int t = 0; t < T_N; t++) cc[c][t] = mix8(K->tone[c][t], w, 31);   /* (the coral: 0.12 of the water - saturated) */
    float v = (float)y / TANK_BOT;                                          /* the faint far rock, beyond the near faces */
    float fl = K->gle[j] + TANK_W * (0.20f + 0.10f * v + 0.06f * bd_n1(y * 0.02f, 61) + 0.03f * bd_n1(y * 0.09f, 62));
    float fr = K->gre[j] - TANK_W * (0.20f + 0.10f * v + 0.06f * bd_n1(y * 0.02f, 63) + 0.03f * bd_n1(y * 0.09f, 64));
    const uint32_t fc = bd_far(0x163232, y, 0.72f), fe = bd_far(0x26464a, y, 0.72f);
    for (int x = K->fl[j] * 2; x <= (int)fl && x < TANK_W; x++) { s_s->row[x] = x > fl - 2 ? fe : fc; s_s->lay[x] = 1; }
    for (int x = (int)fr; x <= K->fr[j] * 2 + 1 && x < TANK_W; x++) if (x >= 0) { s_s->row[x] = x < fr + 2 ? fe : fc; s_s->lay[x] = 1; }
    const uint8_t *mr = K->mg[j];
    for (int i = 0; i < TANK_W / 2; i++) {
        uint8_t m = mr[i]; if (!m) continue;
        uint32_t v2; uint8_t l;
        if ((m & 7) == 7) { v2 = rk[(m >> 3) & 7]; l = 2; }                                  /* the rock */
        else { v2 = cc[(m >> 3) & 7][(m & 7) - 1]; l = (uint8_t)(3 + (m >> 6)); }            /* the coral over it */
        s_s->row[2 * i] = v2; s_s->row[2 * i + 1] = v2; s_s->lay[2 * i] = l; s_s->lay[2 * i + 1] = l;
    }
    return s_s->row;
}

/* ---- the entries ---- */
#include "bg_more.inc"                            /* (the 0.4.2 three: Sunken Ship, Volcanic Vent, Mangroves) */

const bg_glow_t *bg_begin(int bg, void *scratch) {
    if (s_keep && bg != s_keep_bg) bg_keep_drop();                       /* another background: the kept layout goes */
    s_s = scratch;
    if (!scratch || bg <= BG_OPEN || bg >= BG_N) return NULL;
    for (int y = 0; y < TANK_H; y++) s_s->wrow[y] = bg_water_rgb(bg, y);
    glow_layout(bg);
    s_s->ymin = TANK_BOT; s_s->any_lit = true;       /* (the first row clears the lit marks) */
    s_s->glow_q = bg >= BG_SHIP;
    switch (bg) {
        case BG_RUINS:  ruins_begin(); break;
        case BG_KELP:   kelp_begin(); break;
        case BG_LAGOON: lagoon_begin(); break;
        case BG_RIDGES: ridges_begin(); break;
        case BG_REEF:   reef_begin(); break;
        case BG_GROTTO: grotto_begin(); break;
        case BG_SHIP:     nz_fill(); ship_begin(); break;
        case BG_VENT:     nz_fill(); vent_begin(); break;
        case BG_MANGROVE: nz_fill(); mangrove_begin(); break;
        default: break;
    }
    return s_s->has_glow ? &s_s->glow : NULL;
}
const uint32_t *bg_row(int bg, int y, void *scratch, const uint8_t **lay) {
    s_s = scratch;
    if (!scratch || (unsigned)y >= TANK_H) return NULL;
    const uint32_t *r;
    switch (bg) {
        case BG_RUINS:  r = ruins_row(y); break;
        case BG_KELP:   r = kelp_row(y); break;
        case BG_LAGOON: r = lagoon_row(y); break;
        case BG_RIDGES: r = ridges_row(y); break;
        case BG_REEF:   r = reef_row(y); break;
        case BG_GROTTO: r = grotto_row(y); break;
        case BG_SHIP:     r = ship_row(y); break;
        case BG_VENT:     r = vent_row(y); break;
        case BG_MANGROVE: r = mangrove_row(y); break;
        default: r = NULL; break;
    }
    if (lay) *lay = r ? s_s->lay : NULL;
    return r;
}
const uint8_t *bg_row_lit(void *scratch) { scratch_t *s = scratch; return s && s->any_lit ? s->lit : NULL; }

/* ---- THE DRIFT: specks in the water, each a function of the clock (Twilight Deep's marine snow; 0.4.2: the vent's
   embers, the mangroves' silt and leaves) ---- */
int bg_specks(int bg) { return bg == BG_TWILIGHT ? TANK_W * TANK_H / 2400 : bg == BG_VENT ? VE_EMBERS : bg == BG_MANGROVE ? TANK_W * TANK_H / 4200 : 0; }
float bg_speck_clock(int bg, double clock) { return bg == BG_TWILIGHT ? (float)clock : tank_anim(clock); }
bool bg_debug_vent_mouth(int k, int *x, int *y) { if (k < 0 || k >= VE_N) return false; float mx; ve_mouth(k, &mx, y); *x = (int)mx; return true; }
bool bg_debug_vent_worm(const void *scratch, int i, int *x, int *y0) {
    const scratch_t *s = scratch; if (!s || i < 0 || i >= s->u.ve.nw) return false;
    *x = s->u.ve.wx[i]; *y0 = s->u.ve.wy0[i]; return true;
}
void bg_speck(int bg, int i, float T, float *x, float *y, int *alpha, bool *big, uint32_t *rgb, bool *lit) {
    if (bg == BG_VENT) { vent_speck(i, T, x, y, alpha, big, rgb, lit); return; }
    if (bg == BG_MANGROVE) { mangrove_speck(i, T, x, y, alpha, big, rgb, lit); return; }
    *rgb = BG_SPECK_RGB; *lit = false;
    uint32_t h = bd_h(i, 91);
    float vy = 1.2f + (h >> 20 & 7) * 0.35f;                                          /* px/s, sinking */
    *y = fmodf((h >> 10 & 1023) * (TANK_BOT / 1024.0f) + vy * T, (float)TANK_BOT - 24);
    *x = (h & 1023) * (TANK_W / 1024.0f) + 4.0f * sinf(T * (0.15f + (h >> 24 & 7) * 0.02f) + (h >> 27));
    *big = (h >> 13 & 3) == 0;
    *alpha = (int)((*big ? 255 : 210) * (0.7f + 0.3f * sinf(T * 0.9f + (h >> 5 & 63))));
}
