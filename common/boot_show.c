/* boot_show.c - THE BOOT SHOW (2026-10-31). A long sleep ends in a restart, and a tank's first seconds after any start
 * or a SWAP IN go to baking its grass loop. Machito asked on Discord why a wake said FILLING THE TANK; mysiak: "show an
 * empty tank, and decorations with inhabitants would materialise as they are loaded". Strato saw the sim's mock-ups
 * and chose both forms: a SWAP IN fills the tank (the water rises from the sand, a bright line on it), any other start
 * brings the light up. Then the pieces land one by one - the one nearest the glass first, so a piece behind never
 * shows a hole where another will stand - each dissolving in on an 8x8 Bayer grid in 2x2 cells with a bright edge as
 * it lands; the back grass grows up from the sand as fast as the loop really bakes; last the fish, the critters and
 * the bubbles. It is the tank's own pixels throughout: the scene (render_scene_buf), the empty tank baked once with the
 * pieces left out, and the loop's bands as they bake - or, with no loop (0.3.9.5 parked it, and the hotfix left nothing to
 * begin the show from: no start and no SWAP IN played it; 0.3.9.6), the back grass drawn live into the show's own frame
 * each frame (render_boot_grass_live), growing on the show's clock.
 *
 * Its memory: the empty tank (a frame) and a landing map, half a byte a pixel - which piece makes that pixel what the
 * scene has (0: the empty tank's own). Once the fish come the empty tank's frame holds the tank drawn without them. */
#include "boot_show.h"
#include "render.h"
#include <string.h>

#define BS_WATER_S   0.70f      /* the water rises (or the light comes up) */
#define BS_PIECE0_S  0.50f      /* the first piece starts landing */
#define BS_GAP_S     0.35f      /* ... the next this much later */
#define BS_PIECE_S   0.80f      /* ... each takes this long */
#define BS_GRASS_S   1.00f      /* the grass grows in this long at least (longer if the loop bakes slower) */
#define BS_GRASS_LIVE_S 2.40f   /* ... and with no loop (drawn live, on the show's clock): about the loop's pace on the boards */
#define BS_LIFE_S    0.70f      /* the fish, the critters, the bubbles */
#define BS_GLOW      7          /* of 64: how near its landing a pixel still glows */
#define BS_PIECES    14         /* (a nibble a pixel: 0 = the empty tank) */

static struct {
    bool on, fill, life;
    void (*release)(void *);
    uint16_t *empty;            /* the empty tank; in the life phase the tank drawn without its animals */
    uint8_t *land;              /* a nibble a pixel */
    uint8_t rowp[TANK_H];       /* a row a piece lands in (the rest are water and sand: a copy once the water is in) */
    int npieces;
    float grass_p, life_t0;
} s;
static tank_t s_bare;           /* (the tank without its animals, for the life phase) */

static const uint8_t B8[8][8] = {
    { 0, 32,  8, 40,  2, 34, 10, 42}, {48, 16, 56, 24, 50, 18, 58, 26}, {12, 44,  4, 36, 14, 46,  6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
    { 3, 35, 11, 43,  1, 33,  9, 41}, {51, 19, 59, 27, 49, 17, 57, 25}, {15, 47,  7, 39, 13, 45,  5, 37}, {63, 31, 55, 23, 61, 29, 53, 21} };
static inline int bayer(int x, int y) { return B8[(y >> 1) & 7][(x >> 1) & 7]; }
static inline int land_at(int i) { return (s.land[i >> 1] >> ((i & 1) << 2)) & 15; }
static inline void land_set(int i, int k) { int sh = (i & 1) << 2; s.land[i >> 1] = (uint8_t)((s.land[i >> 1] & ~(15 << sh)) | (k << sh)); }
static inline uint16_t glow(uint16_t v) {       /* toward bright sea-glass: the edge as it lands */
    uint16_t q = FB_SWAP16(v);
    int r = (q >> 11) & 31, g = (q >> 5) & 63, b = q & 31;
    r = (r + 60) >> 2; g = (g + 180) >> 2; b = (b + 90) >> 2;
    return FB_SWAP16((uint16_t)((r << 11) | (g << 5) | b));
}
static inline uint16_t dimmed(uint16_t v, int a) {   /* a/256 of its light */
    uint16_t q = FB_SWAP16(v);
    int r = (((q >> 11) & 31) * a) >> 8, g = (((q >> 5) & 63) * a) >> 8, b = ((q & 31) * a) >> 8;
    return FB_SWAP16((uint16_t)((r << 11) | (g << 5) | b));
}
static inline int prog64(float ts, float t0, float dur) {   /* 0..64+ */
    float p = (ts - t0) / dur;
    return p <= 0 ? -1 : (int)(p * 64.0f);
}

bool boot_show_on(void) { return s.on; }
bool boot_show_ready(void) { return render_scene_buf(NULL) && (render_grass_loop_n() > 0 || !render_grass_loop_warming()); }

void boot_show_end(void) {
    if (s.release) { if (s.empty) s.release(s.empty); if (s.land) s.release(s.land); }
    memset(&s, 0, sizeof s);
}

bool boot_show_begin(const tank_t *t, bool fill, void *(*alloc)(size_t), void (*release)(void *)) {
    boot_show_end();
    const uint16_t *sc = render_scene_buf(NULL);
    if (!sc || !alloc) return false;
    s.empty = alloc((size_t)TANK_W * TANK_H * sizeof(uint16_t));
    s.land = alloc(((size_t)TANK_W * TANK_H + 1) / 2);
    s.release = release;
    if (!s.empty || !s.land) { boot_show_end(); return false; }
    int items[DECOR_SLOTS], n = render_boot_pieces(t, items);   /* back to front */
    if (n > BS_PIECES) n = BS_PIECES;
    /* layer k = the k pieces nearest the glass. A pixel lands at the first layer from which it is what the scene has:
       the layers made from the front back, the empty tank last (it stays) */
    memset(s.land, (n << 4) | n, ((size_t)TANK_W * TANK_H + 1) / 2);
    if (!n) render_boot_scene(t, s.empty, 0);       /* (no pieces: the empty tank is the scene) */
    for (int k = n - 1; k >= 0; k--) {
        uint32_t hide = 0;
        for (int j = 0; j < n - k; j++) hide |= 1u << items[j];   /* the n-k pieces furthest back left out */
        render_boot_scene(t, s.empty, hide);
        for (int i = 0; i < TANK_W * TANK_H; i++)
            if (land_at(i) == k + 1 && s.empty[i] == sc[i]) land_set(i, k);
    }
    for (int y = 0; y < TANK_H; y++) {
        s.rowp[y] = 0;
        for (int x = 0; x < TANK_W && !s.rowp[y]; x++) if (land_at(y * TANK_W + x)) s.rowp[y] = 1;
    }
    render_boot_scene_done(t);
    s.npieces = n; s.fill = fill; s.on = true; s.life = false; s.grass_p = 0; s.life_t0 = 0;
    return true;
}

bool boot_show_frame(const tank_t *t, uint16_t *fb, float ts, bool loop_whole, float bake_frac) {
    if (!s.on) return false;
    const uint16_t *sc = render_scene_buf(NULL);
    if (!sc) sc = s.empty;
    const float statics_done = s.npieces ? BS_PIECE0_S + (s.npieces - 1) * BS_GAP_S + BS_PIECE_S : BS_WATER_S;
    if (!s.life && loop_whole && s.grass_p >= 1.0f && ts >= statics_done) { s.life = true; s.life_t0 = ts; }

    if (s.life) {                               /* the animals: the tank drawn with and without them, theirs dissolving in */
        int p = prog64(ts, s.life_t0, BS_LIFE_S);
        s_bare = *t;
        s_bare.n_fish = 0;
        s_bare.sd_unlocks &= ~(uint32_t)(SD_ITEM_SNAIL | SD_ITEM_SHRIMP | SD_ITEM_URCHIN | SD_ITEM_SEAHORSE | SD_ITEM_CRAB);
        for (int i = 0; i < MAX_BUBBLE; i++) s_bare.bubble[i].y = -1000;
        for (int i = 0; i < MAX_FOOD; i++) s_bare.food[i].alive = false;
        memset(s_bare.algae, 0, sizeof s_bare.algae);   /* (the film on the glass comes with them) */
        render_tank(&s_bare, s.empty, TANK_W);
        render_tank(t, fb, TANK_W);
        if (p >= 64 + BS_GLOW) return false;
        for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) {
            int i = y * TANK_W + x;
            if (fb[i] == s.empty[i]) continue;
            int d = p - bayer(x, y);
            fb[i] = d < 0 ? s.empty[i] : d < BS_GLOW ? glow(fb[i]) : fb[i];
        }
        return true;
    }

    int pw = prog64(ts, 0, BS_WATER_S); if (pw > 64) pw = 64;
    int pk[BS_PIECES + 1];
    for (int k = 1; k <= s.npieces; k++) pk[k] = prog64(ts, BS_PIECE0_S + (k - 1) * BS_GAP_S, BS_PIECE_S);
    const float tg0 = s.npieces ? BS_PIECE0_S + (s.npieces - 1) * BS_GAP_S + 0.2f : BS_WATER_S;
    float grass_s = BS_GRASS_S;
    if (!render_grass_loop_warming() && !render_grass_loop_n()) { bake_frac = 1.0f; grass_s = BS_GRASS_LIVE_S; }   /* (no loop baking: the show's clock alone) */
    float gp = (ts - tg0) / grass_s; if (gp > bake_frac) gp = bake_frac;
    if (gp > s.grass_p) s.grass_p = gp;         /* (it only grows) */
    if (s.grass_p > 1.0f) s.grass_p = 1.0f;
    int y0 = TANK_H; const uint16_t *band = render_boot_grass(t, &y0);
    if (!band && !render_grass_loop_warming() && render_boot_grass_live(t, fb, &y0)) band = fb + (size_t)y0 * TANK_W;   /* no loop to wait for: the grass
                                                                                        drawn live into this frame, grown on the show's clock (read in place below) */
    const int gq = (int)(s.grass_p * 1024), span = (int)(TANK_H * 0.72f);
    const int line = s.fill ? TANK_H - TANK_H * (pw < 0 ? 0 : pw) / 64 : 0, light = pw < 0 ? 0 : pw * 4;

    const bool landed = !s.npieces || pk[s.npieces] >= 64 + BS_GLOW;   /* (the last piece lands last) */
    for (int y = 0; y < TANK_H; y++) {
        const uint16_t *brow = band && y >= y0 ? band + (size_t)(y - y0) * TANK_W : NULL;
        const int gneed = (TANK_H - 8 - y) * 1024 / span;      /* the grass: grows up from the sand */
        uint16_t *frow = fb + (size_t)y * TANK_W; const uint16_t *srow = sc + (size_t)y * TANK_W;
        if (pw >= 64 && (landed || !s.rowp[y])) {             /* a row that is the scene now, but for its grass */
            if (!brow || gq - gneed + 32 < 0) { memcpy(frow, srow, TANK_W * sizeof(uint16_t)); continue; }   /* (no grass reaches it yet) */
            for (int x = 0; x < TANK_W; x++) {                   /* (brow may be frow itself - the live grass: each pixel read before it is written) */
                const uint16_t g = brow[x]; uint16_t v = srow[x];
                if (g != v && !land_at(y * TANK_W + x)) {
                    int d = gq - gneed - (bayer(x, y) >> 1);
                    if (d >= 0) v = d < 24 ? glow(g) : g;
                }
                frow[x] = v;
            }
            continue;
        }
        for (int x = 0; x < TANK_W; x++) {
            int i = y * TANK_W + x, k = land_at(i), th = bayer(x, y);
            uint16_t v;
            if (k && pk[k] >= th) v = pk[k] - th < BS_GLOW ? glow(sc[i]) : sc[i];   /* a piece's pixel, landed */
            else if (s.fill) v = y >= line ? (y < line + 2 && pw < 64 ? glow(s.empty[i]) : s.empty[i]) : 0;   /* the water */
            else v = light >= 256 ? s.empty[i] : dimmed(s.empty[i], light);
            if (!k && brow && brow[x] != sc[i]) {              /* a grass pixel (only where the empty tank shows: the back grass stands behind every piece) */
                int d = gq - gneed - (th >> 1);
                if (d >= 0) v = d < 24 ? glow(brow[x]) : brow[x];
            }
            fb[i] = v;
        }
    }
    return true;
}
