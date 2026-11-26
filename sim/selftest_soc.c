#define _DEFAULT_SOURCE
/* selftest_soc.c - --selftest-soc (0.4.3 audit round 1, seam SOC: the octopus x the cap, the crab, dens, the shelf, the shop).
 * The audit's harness, in make check since octo-fix-043. Every frame is rendered as the boards render it: the scene cache, the
 * vignette cache and the dirty mask (firmware main.c; the audit's runs had none, and their pixel counts near a piece IN FRONT were
 * the no-cache path's - TB1). Each case prints "soc <id>: ..." with its numbers; a FAIL line marks a broken invariant.
 *   D1..D7  the octopus INSIDE its den when the den changes under it (sold, shelved, upgraded, moved, depth flipped, the crab put
 *           back, the other shelter raised over it): per tick for 4 s - its drawn pixels (render with it hidden vs shown), the
 *           ticks where the renderer clips it to a den opening it is not standing in, its biggest one-tick drop/rise in y
 *   C1..C3  the cap of five at a LOAD: six critter bits in the water in a save; the same with the octopus by oc_owned only; and a
 *           MY TANKS-style second load (the blob unlocks) agrees with the load
 *   N1..N3  oc_name: 8 letters, no terminator; a damaged byte; a name kept for an octopus not owned
 *   F1      paid finds: shelve / PUT BACK and save / load cycles never bring a find sooner than its wait
 *   V..V3   the octopus held in its FRONT lane along the sand, the castle and the volcano IN FRONT (both, each alone): never hidden by
 *           a piece IN FRONT - drawn at least 80% of what it draws at the same spot with the pieces off the glass
 *   P       (SOC-1b) the same before the volcano, the coral and the reef cluster IN FRONT with the piece's PLACEMENT PAGE up and the
 *           piece dragged (out of the scene, drawn live): still drawn as before the page (it was 418 -> 58 px) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tank.h"
#include "advisor.h"
#include "progression.h"
#include "render.h"
#include "octopus.h"
#include "setup.h"

const char *sim_shelf_dir(void);
goal_t advisor_rules(const tank_t *t, int fish_idx, bool request);
static tank_t T;
static uint16_t fa[TANK_W * TANK_H], fb2[TANK_W * TANK_H];
static int nfail;
#define SOCF(...) do { printf("FAIL: soc: "); printf(__VA_ARGS__); printf("\n"); nfail++; } while (0)
enum { I_SNAIL = 1, I_SHRIMP = 5, I_URCHIN = 6, I_SEAHORSE = 7, I_CRAB = 10, I_OCTO = 11 };
static char sav[600];
static void tk(void) { tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T); }
static void run(int n) { for (int i = 0; i < n; i++) tk(); }
static void dump_ppm(const char *path, const uint16_t *fb) {
    FILE *f = fopen(path, "wb"); if (!f) return; fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int i = 0; i < TANK_W * TANK_H; i++) { uint16_t v = FB_SWAP16(fb[i]); unsigned char p[3] = { (unsigned char)((v >> 11) << 3), (unsigned char)(((v >> 5) & 63) << 2), (unsigned char)((v & 31) << 3) }; fwrite(p, 1, 3, f); }
    fclose(f);
}
static int drawn_px(void) {
    render_tank(&T, fa, TANK_W); render_tank(&T, fa, TANK_W);
    octopus_hide(1); render_tank(&T, fb2, TANK_W); render_tank(&T, fb2, TANK_W); octopus_hide(0);
    int n = 0; for (int i = 0; i < TANK_W * TANK_H; i++) n += fa[i] != fb2[i];
    return n;
}
static long file_bytes(const char *path, uint8_t *out, long cap) { FILE *f = fopen(path, "rb"); if (!f) return -1; long n = (long)fread(out, 1, (size_t)cap, f); fclose(f); return n; }
static void file_put(const char *path, const uint8_t *b, long n) { FILE *f = fopen(path, "wb"); if (f) { fwrite(b, 1, (size_t)n, f); fclose(f); } }

/* a tank: castle (stage st) at cx depth cz, maybe the volcano at vx/vz, maybe a crab (shelved or not), the octopus favoring `fav`,
   sent home until it is inside */
static int setup(int seed, int st, float cx, int cz, int volcano, float vx, int vz, int crab, int crab_shelved, int fav) {
    remove(sav); tank_init(&T, (uint32_t)seed); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000;
    progression_buy(&T, 2); for (int s = 1; s < st; s++) progression_buy(&T, 2);
    tank_decor_set(&T, 2, cx, cz);
    if (volcano) { progression_buy(&T, 9); tank_decor_set(&T, 9, vx, vz); }
    if (crab) { progression_buy(&T, I_CRAB); if (crab_shelved) progression_shelve(&T, I_CRAB); }
    progression_buy(&T, I_OCTO); T.oc_fav = (uint8_t)fav; octopus_set_fav(fav);
    if (volcano && fav == 9) tank_decor_raise(&T, 2);   /* (the castle the frontmost: the crab's, the volcano the octopus's) */
    T.light_override = true; T.light_on = true;
    run(60); octopus_think(0); octopus_cmd(3, 0);
    for (int i = 0; i < 4000 && octopus_inn() < 0.97f; i++) { tk(); if (i % 600 == 599) octopus_cmd(3, 0); }
    return octopus_inn() >= 0.97f;
}
/* after the change: 100 ticks (4 s) watched */
static void watch(const char *id, const char *what) {
    const int behind = octopus_from_behind();   /* (it went in from BEHIND the piece IN FRONT: no sand clear of every piece to come round on - the small
                                                   glasses with two pieces IN FRONT. It comes out behind the piece too, hidden by its rock by design) */
    octopus_think(1);   /* (its own life again, as on the glass) */
    int min_px = 1 << 30, low = 0, wrong_clip = 0, first_out = -1; float max_dy = 0, y0 = octopus_y();
    const int NW = getenv("SOC_LONG") ? atoi(getenv("SOC_LONG")) : 100;
    for (int i = 0; i < NW; i++) {
        tk();
        const int px = drawn_px();
        if (getenv("SOC_DUMP") && !strcmp(getenv("SOC_DUMP"), id) && (i == 3 || i == 125 || i == 250)) { char p[700]; snprintf(p, sizeof p, "%s/soc-%s-%d.ppm", getenv("PT_TMP"), id, i); dump_ppm(p, fa); snprintf(p, sizeof p, "%s/soc-%s-%d-hid.ppm", getenv("PT_TMP"), id, i); dump_ppm(p, fb2); }
        if (px < min_px) min_px = px;
        if (px < 40) low++;
        const int d = octopus_den_item();
        if (d >= 0 && octopus_inn() >= 0.5f && fabsf(octopus_x() - tank_decor_x(&T, d)) > 26.0f) wrong_clip++;   /* (render's octo_in_den: clipped to THAT den's opening) */
        const float y = octopus_y(); if (fabsf(y - y0) > max_dy) max_dy = fabsf(y - y0); y0 = y;
        if (first_out < 0 && octopus_inn() < 0.02f) first_out = i;
        if (getenv("SOC_TRACE") && (i < 12 || i % (NW > 100 ? 125 : 10) == 0)) printf("  t%d px %d inn %.2f x %.0f y %.1f lane %d den %d denx %.0f mode %d camo %.2f\n", i, px, (double)octopus_inn(), (double)octopus_x(), (double)octopus_y(), octopus_lane_front(), d, d >= 0 ? (double)tank_decor_x(&T, d) : -1.0, octopus_mode(), (double)octopus_camo());
    }
    printf("soc %s (%s): min drawn %d px, %d ticks under 40 px, %d ticks clipped to a den it is not in, biggest one-tick y step %.1f px, out at tick %d, den now %d\n",
           id, what, min_px, low, wrong_clip, (double)max_dy, first_out, octopus_den_item());
    if (wrong_clip) SOCF("%s: %d ticks drawn clipped to the opening of a den it is not standing in", id, wrong_clip);
    if (low && !behind) SOCF("%s: %d ticks with the octopus (nearly) not drawn", id, low);
    else if (low) printf("soc %s: went in from behind the piece IN FRONT (no clear sand): %d ticks behind its rock after (by design, not counted)\n", id, low);
}

/* V: the octopus in its FRONT lane at x: drawn with the pieces as they stand, and at the same spot with every piece IN FRONT off */
static void front_lane_sweep(const char *id) {
    extern void octopus_debug_put(float x, int lane);
    const uint32_t keep = T.sd_unlocks; int worst = 1000; float worst_x = 0; int wpx = 0, wref = 0;
    printf("soc %s lane 1:", id);
    for (float x = OCTO_XLO; x <= OCTO_XHI; x += 24) {
        T.sd_unlocks = keep; octopus_debug_put(x, 1); run(8); const int px = drawn_px();
        T.sd_unlocks = keep & ~(uint32_t)(SD_ITEM_CASTLE | SD_ITEM_VOLCANO); octopus_debug_put(x, 1); run(8); const int ref = drawn_px();
        printf(" %.0f:%d/%d", (double)x, px, ref);
        if (ref > 40) { const int pc = px * 100 / ref; if (pc < worst) { worst = pc; worst_x = x; wpx = px; wref = ref; } }
    }
    T.sd_unlocks = keep; run(4);
    printf("  (castle %.0f+-%.0f, volcano %.0f+-%.0f)\n", (double)tank_decor_x(&T, 2), (double)tank_decor_half_w(2), (double)tank_decor_x(&T, 9), (double)tank_decor_half_w(9));
    if (worst < 80) SOCF("%s: in its FRONT lane at x %.0f it draws %d px, %d without the pieces IN FRONT (%d%%): a piece IN FRONT hides it", id, (double)worst_x, wpx, wref, worst);
}
int selftest_soc(void) {
    snprintf(sav, sizeof sav, "%s-soc-%dx%d.sav", sim_shelf_dir(), TANK_W, TANK_H);
    setenv("POCKET_TANK_SAVE", sav, 1); remove(sav);
    static uint16_t scene[TANK_W * TANK_H]; static uint32_t dirty[RENDER_DIRTY_WORDS]; static uint8_t *vig;   /* THE BOARD'S CACHES (TB1) */
    if (!vig) vig = malloc(render_vignette_cache_bytes());
    render_set_scene_cache(scene); render_set_vignette_cache(vig); render_set_dirty_mask(dirty);
    const float CX = TANK_W * 0.35f, VX = TANK_W * 0.72f;
    for (int z = 0; z < 2; z++) {
        const int cz = z ? DECOR_Z_FRONT : DECOR_Z_BACK; char id[16]; const char *zs = z ? "IN FRONT" : "BEHIND";
        snprintf(id, sizeof id, "D1%c", z ? 'f' : 'b');
        if (setup(501, 1, CX, cz, 0, 0, 0, 0, 0, 2)) { progression_sell(&T, 2); watch(id, z ? "castle IN FRONT sold" : "castle BEHIND sold"); } else printf("soc %s: never got in (%s)\n", id, zs);
        snprintf(id, sizeof id, "D2%c", z ? 'f' : 'b');
        if (setup(502, 1, CX, cz, 0, 0, 0, 0, 0, 2)) { progression_shelve(&T, 2); watch(id, "castle shelved"); } else printf("soc %s: never got in\n", id);
        snprintf(id, sizeof id, "D3%c", z ? 'f' : 'b');
        if (setup(503, 1, CX, cz, 0, 0, 0, 0, 0, 2)) { const float fy0 = tank_decor_fy(&T, 2); progression_buy(&T, 2); progression_buy(&T, 2);
            printf("soc %s: upgraded to stage %d inside (floor %.0f -> %.0f, half %.0f)\n", id, T.castle_stage, (double)fy0, (double)tank_decor_fy(&T, 2), (double)tank_decor_half_w(2)); watch(id, "castle upgraded 1->3"); } else printf("soc %s: never got in\n", id);
        snprintf(id, sizeof id, "D4%c", z ? 'f' : 'b');
        if (setup(504, 2, CX, cz, 0, 0, 0, 0, 0, 2)) { tank_decor_set(&T, 2, CX + 90, cz); watch(id, "castle moved 90 px"); } else printf("soc %s: never got in\n", id);
        snprintf(id, sizeof id, "D5%c", z ? 'f' : 'b');
        if (setup(505, 2, CX, cz, 0, 0, 0, 0, 0, 2)) { tank_decor_set(&T, 2, CX, z ? DECOR_Z_BACK : DECOR_Z_FRONT); watch(id, "castle depth flipped"); } else printf("soc %s: never got in\n", id);
        snprintf(id, sizeof id, "D6%c", z ? 'f' : 'b');
        if (setup(506, 2, CX, cz, 0, 0, 0, 1, 1, 2)) { progression_unshelve(&T, I_CRAB); tank_crab_roost(&T, -1); watch(id, "crab PUT BACK"); } else printf("soc %s: never got in\n", id);
        snprintf(id, sizeof id, "D7%c", z ? 'f' : 'b');
        if (setup(507, 2, CX, cz, 1, VX, cz, 1, 0, 9) && octopus_den_item() == 9) { tank_decor_raise(&T, 9); watch(id, "the volcano raised over the castle"); }
        else printf("soc %s: never got into the volcano (den %d in %.2f)\n", id, octopus_den_item(), (double)octopus_inn());
    }
    /* V: the same two pieces IN FRONT (castle stage 2, the volcano raised over it), the octopus with no den history, set down
       in its FRONT lane at points along the sand and held there: its drawn pixels at each */
    {
        extern void octopus_debug_put(float x, int lane);
        remove(sav); tank_init(&T, 508); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000;
        progression_buy(&T, 2); progression_buy(&T, 2); tank_decor_set(&T, 2, CX, DECOR_Z_FRONT); progression_buy(&T, 9); tank_decor_set(&T, 9, VX, DECOR_Z_FRONT); tank_decor_raise(&T, 9);
        progression_buy(&T, I_OCTO); T.light_override = true; T.light_on = true; run(60); octopus_think(0);
        front_lane_sweep("V (castle and volcano IN FRONT)");
        /* V2: the volcano alone IN FRONT; V3: the castle alone IN FRONT */
        for (int k = 0; k < 2; k++) { progression_shelve(&T, k ? 9 : 2); if (k) progression_unshelve(&T, 2); run(30); octopus_think(0);
            front_lane_sweep(k ? "V3 (the castle alone IN FRONT)" : "V2 (the volcano alone IN FRONT)"); }
        /* V4: the URCHIN in its front lane before the volcano alone IN FRONT (an older critter, the control) */
        progression_shelve(&T, 2); progression_unshelve(&T, 9); progression_shelve(&T, I_OCTO); progression_buy(&T, I_URCHIN); run(20);
        printf("soc V4 (urchin, volcano alone IN FRONT) front lane:");
        for (float x = OCTO_XLO; x <= OCTO_XHI; x += 24) { T.urchin_x = x; T.urchin_front = true; T.urchin_to = -1; T.urchin_rest = 1000;
            render_tank(&T, fa, TANK_W); render_tank(&T, fa, TANK_W); T.sd_shelved |= SD_ITEM_URCHIN; render_tank(&T, fb2, TANK_W); render_tank(&T, fb2, TANK_W); T.sd_shelved &= ~SD_ITEM_URCHIN;
            int n = 0; for (int i = 0; i < TANK_W * TANK_H; i++) n += fa[i] != fb2[i]; printf(" %.0f:%d", (double)x, n); }
        printf("\n");
        octopus_think(1);
    }
    /* P (SOC-1b): a piece's PLACEMENT PAGE up and the piece dragged - out of the scene, drawn live - with the octopus before it in its
       FRONT lane: the volcano, the coral, the reef cluster IN FRONT (the castle has its own pass) */
    {
        extern void octopus_debug_put(float x, int lane);
        static const struct { int item; const char *name; } PP[3] = { { 9, "volcano" }, { 3, "coral" }, { 4, "reef cluster" } };
        for (int k = 0; k < 3; k++) {
            remove(sav); tank_init(&T, 508); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000;
            progression_buy(&T, PP[k].item); tank_decor_set(&T, PP[k].item, VX, DECOR_Z_FRONT);
            progression_buy(&T, I_OCTO); T.light_override = true; T.light_on = true; run(60); octopus_think(0);
            octopus_debug_put(VX - 20, 1); run(8);
            const int before = drawn_px();
            setup_begin_place(&T, PP[k].item);
            setup_touch(&T, VX - 10, TANK_H - 40, true); setup_touch(&T, VX - 10, TANK_H - 40, true);
            run(2);
            const int live = setup_place_live(), lane = octopus_lane_front(), during = drawn_px();
            printf("soc P %s IN FRONT, its page up and dragged (live %d): the octopus at x %.0f lane %d: %d px before the page, %d with it up\n", PP[k].name, live, (double)octopus_x(), lane, before, during);
            if (!live || !lane) SOCF("P %s: the case was not reached (page live %d, front lane %d)", PP[k].name, live, lane);
            else if (during * 10 < before * 8) SOCF("P %s: its placement page up and the piece dragged, the octopus in its FRONT lane before it draws %d px (%d before the page): the live piece covers it", PP[k].name, during, before);
            setup_touch(&T, VX - 10, TANK_H - 40, false); setup_cancel(&T); octopus_think(1);
        }
    }
    /* C: the cap at a load */
    {
        static uint8_t b[4096];
        remove(sav); tank_init(&T, 61); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000;
        static const int five[5] = { I_SNAIL, I_SHRIMP, I_URCHIN, I_SEAHORSE, I_CRAB };
        progression_buy(&T, I_OCTO); progression_shelve(&T, I_OCTO);
        for (int k = 0; k < 5; k++) progression_buy(&T, five[k]);
        progression_save(&T); long n = file_bytes(sav, b, sizeof b);
        /* find sd_shelved: the u32 holding exactly the octopus bit (the only shelved piece) */
        int off_sh = -1; for (int i = 0; i + 4 <= n; i += 4) { uint32_t u; memcpy(&u, b + i, 4); if (u == SD_ITEM_OCTOPUS && i != 1488) { off_sh = i; break; } }
        uint32_t z0 = 0; if (off_sh >= 0) memcpy(b + off_sh, &z0, 4);   /* C1: six in the water, every bit set */
        file_put(sav, b, n); tank_init(&T, 9); progression_wake(&T, 0); run(5);
        printf("soc C1: six critter bits in the water in the save: %d in after the load, the octopus %s\n", tank_crit_count_in(&T), tank_crit_in(&T, SD_ITEM_OCTOPUS) ? "IN" : "on the shelf");
        if (tank_crit_count_in(&T) > CRIT_CAP) SOCF("C1: %d critters in the water", tank_crit_count_in(&T));
        uint32_t u; memcpy(&u, b + 1488, 4); u &= ~(uint32_t)SD_ITEM_OCTOPUS; memcpy(b + 1488, &u, 4);   /* C2: the octopus by oc_owned only */
        file_put(sav, b, n); tank_init(&T, 9); progression_wake(&T, 0); run(5);
        printf("soc C2: the same, its bit off sd_unlocks (oc_owned 1): %d in, octopus owned %d in %d\n", tank_crit_count_in(&T), !!(T.sd_unlocks & SD_ITEM_OCTOPUS), tank_crit_in(&T, SD_ITEM_OCTOPUS));
        if (tank_crit_count_in(&T) > CRIT_CAP) SOCF("C2: %d critters in the water", tank_crit_count_in(&T));
        printf("soc C3: blob unlocks has the octopus: %d (the REMOVE refund counts it)\n", !!(progression_blob_unlocks(b, (size_t)n) & SD_ITEM_OCTOPUS));
        /* C4: oc_owned a damaged value (2): not owned, and its bit off - and the save bytes then */
        b[2295] = 2; file_put(sav, b, n); tank_init(&T, 9); progression_wake(&T, 0); run(5);
        printf("soc C4: oc_owned 2 (damaged), bit off: owned %d\n", !!(T.sd_unlocks & SD_ITEM_OCTOPUS));
        /* N: names */
        b[2295] = 1; memcpy(b + 2296, "abcdefgh", 8); file_put(sav, b, n); tank_init(&T, 9); progression_wake(&T, 0);
        printf("soc N1: oc_name 8 letters no terminator: \"%s\" (%zu)\n", T.crit_name[CRIT_NAME_OCTOPUS], strlen(T.crit_name[CRIT_NAME_OCTOPUS]));
        if (strlen(T.crit_name[CRIT_NAME_OCTOPUS]) > FISH_NAME_MAX) SOCF("N1: a name past FISH_NAME_MAX");
        memcpy(b + 2296, "in\xffky\x01\0\0", 8); file_put(sav, b, n); tank_init(&T, 9); progression_wake(&T, 0);
        printf("soc N2: oc_name with damaged bytes: \"%s\"\n", T.crit_name[CRIT_NAME_OCTOPUS]);
        progression_save(&T); static uint8_t c[4096]; long m = file_bytes(sav, c, sizeof c);
        printf("soc N2b: saved back: %ld B, oc_name bytes %02x %02x %02x %02x %02x %02x %02x %02x\n", m, c[2296], c[2297], c[2298], c[2299], c[2300], c[2301], c[2302], c[2303]);
        b[2295] = 0; memcpy(&u, b + 1488, 4); u &= ~(uint32_t)SD_ITEM_OCTOPUS; memcpy(b + 1488, &u, 4); memcpy(b + 2296, "inky\0\0\0\0", 8);
        file_put(sav, b, n); tank_init(&T, 9); progression_wake(&T, 0);
        printf("soc N3: not owned, a name in the bytes: owned %d name \"%s\"\n", !!(T.sd_unlocks & SD_ITEM_OCTOPUS), T.crit_name[CRIT_NAME_OCTOPUS]);
        if (T.crit_name[CRIT_NAME_OCTOPUS][0]) SOCF("N3: a name for an octopus not owned");
    }
    /* F1: finds through shelving and loads */
    {
        remove(sav); tank_init(&T, 71); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 1000; progression_buy(&T, I_OCTO);
        T.light_override = true; T.light_on = true; run(50);
        const float w0 = octopus_find_wait();
        progression_shelve(&T, I_OCTO); run(5); progression_unshelve(&T, I_OCTO); run(5);
        const float w1 = octopus_find_wait();
        progression_save(&T); tank_init(&T, 9); progression_wake(&T, 0); run(5);
        const float w2 = octopus_find_wait();
        printf("soc F1: a never-found octopus's first wait %.0f s; after shelve+put back %.0f; after a save and load %.0f (kept: audit PE-1)\n", (double)w0, (double)w1, (double)w2);
        if (fabsf(w1 - w0) > 2.0f || fabsf(w2 - w0) > 2.0f) SOCF("F1: its first wait %.0f s became %.0f after a shelving, %.0f after a load", (double)w0, (double)w1, (double)w2);
        T.oc_found = 1; T.oc_found_paid = 1; progression_save(&T); tank_init(&T, 9); progression_wake(&T, 0); run(5);
        printf("soc F1b: one found: after a load the wait is %.0f s\n", (double)octopus_find_wait());
    }
    tank_octopus_release(&T); remove(sav);
    render_set_scene_cache(NULL); render_set_vignette_cache(NULL); render_set_dirty_mask(NULL);
    printf("selftest-soc: %s (%d failed)\n", nfail ? "FAIL" : "PASS", nfail);
    return nfail ? 1 : 0;
}
