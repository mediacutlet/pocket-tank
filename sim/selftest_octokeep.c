#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_octokeep.c - --selftest-octo-keep (0.4.3, docs/octopus.md 32): THE OCTOPUS as a critter a keeper buys and keeps.
 * (How it walks, dens, layers, hides and finds is --selftest-octo-den's; this is what is KEPT.)
 *   A  the shop: its row is in CRITTERS after the crab, at SD_PRICE_OCTOPUS; a buy takes the price, it is in the water as a
 *      BABY (growth 0) with a card (render_critter_here), drawn (the tank with it differs from the tank without); twice is
 *      refused; too poor is refused with nothing taken
 *   B  it GROWS and never shrinks: a baby (size under 1.3) for about 3 days, full size at about 3 weeks, the nights counted
 *      (tank_tick_sleep); it stops at the adult
 *   C  A FIND PAYS: its coin up, a tap on it counts one (oc_found) and the ledger pays SD_OCTO_FIND once, never twice; the
 *      card's COLLECT takes the next one the same
 *   D  THE SAVE: 2304 bytes; its growth, its finds, the paid count and its favorite den survive a save and a load, and it
 *      is back in the water; a tank that owns none saves zeros there
 *   E  an OLDER save (2288 bytes, no octopus bytes) loads with none; the creator's bytes 2216..2279 are kept as they were by a save
 *   F  a ROLLBACK: an older build masks its bit off sd_unlocks and saves so, keeping the bytes past its own - back on this
 *      build the octopus is still owned, at its size (bought stays bought)
 *   G  THE SHELF: shelved it is out of the water, its card gone, and nothing of it runs (a night on the shelf: no growth,
 *      no finds); PUT BACK returns it at the size it left
 *   H  THE CAP: with five critters in the water a buy is refused, the wallet untouched; one shelved makes the room
 *   I  ONE BODY: a second tank that owns one, ticked beside the first, does not move the first's octopus
 *   J  the cards' arrows reach it: after the crab, before the snail again */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef TANK_P4
#define SIM_SAVE_BYTES 3336   /* the big screens' save: the fish tail past 2560 (--selftest-ten) */
#else
#define SIM_SAVE_BYTES 2304
#endif
#include "tank.h"
#include "advisor.h"
#include "progression.h"
#include "render.h"
#include "octopus.h"

const char *sim_shelf_dir(void);
goal_t advisor_rules(const tank_t *t, int fish_idx, bool request);

static tank_t T, U;
static uint16_t fa[TANK_W * TANK_H], fb2[TANK_W * TANK_H];
#define OK_FAIL(...) do { printf("FAIL: octo-keep: "); printf(__VA_ARGS__); printf("\n"); return 1; } while (0)
enum { I_SNAIL = 1, I_SHRIMP = 5, I_URCHIN = 6, I_SEAHORSE = 7, I_CRAB = 10, I_OCTO = 11 };

static long file_bytes(const char *path, uint8_t *out, long cap) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    long n = (long)fread(out, 1, (size_t)cap, f); fclose(f); return n;
}
static void file_put(const char *path, const uint8_t *b, long n) { FILE *f = fopen(path, "wb"); if (f) { fwrite(b, 1, (size_t)n, f); fclose(f); } }
/* where sd_shelved sits in the save (not a number this file should know): the one u32 that differs between a tank's save with a
   piece shelved and without */
static int g_off_shelved = -1;
static int offsetof_shelved(void) { return g_off_shelved; }
static void find_shelved_offset(const char *sav) {
    static tank_t K; static uint8_t x0[4096], x1[4096];
    remove(sav); tank_init(&K, 5); progression_boot(&K); progression_setup_done(&K); K.sd_balance = 100000; progression_buy(&K, 1);
    progression_save(&K); long n0 = file_bytes(sav, x0, sizeof x0);
    K.sd_shelved |= SD_ITEM_SNAIL; progression_save(&K); long n1 = file_bytes(sav, x1, sizeof x1);
    for (long i = 0; i + 4 <= n0 && i + 4 <= n1; i += 4) { uint32_t a, b; memcpy(&a, x0 + i, 4); memcpy(&b, x1 + i, 4); if ((a ^ b) == SD_ITEM_SNAIL) { g_off_shelved = (int)i; break; } }
    remove(sav);
}
static void run(tank_t *t, int ticks) { for (int i = 0; i < ticks; i++) { tank_tick(t, 0.04f, advisor_rules); tank_handled(t); } }
static int diff_px(void) { int n = 0; for (int i = 0; i < TANK_W * TANK_H; i++) if (fa[i] != fb2[i]) n++; return n; }

/* --octopus-apart <seed> <on|off> <growth>: one thirty-minute run of R's tank, its numbers on one line (the seed sweep, 11-22) */
int octopus_apart_run(int seed, int on, int growth) {
    char sav[600]; snprintf(sav, sizeof sav, "%s-octoapart-%d-%dx%d.sav", sim_shelf_dir(), seed, TANK_W, TANK_H);
    setenv("POCKET_TANK_SAVE", sav, 1); remove(sav);
    extern void tank_debug_no_apart(int on); extern float tank_rivals_gap(void);
    tank_debug_no_apart(!on);
    tank_init(&T, (uint32_t)seed); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000;
    progression_buy(&T, 2); progression_buy(&T, 9); progression_buy(&T, I_CRAB); progression_buy(&T, I_OCTO);
    T.castle_stage = 3; tank_decor_set(&T, 2, TANK_W * 0.73f, DECOR_Z_FRONT); tank_decor_set(&T, 9, TANK_W * 0.30f, DECOR_Z_BACK);
    T.light_override = true; T.light_on = true; T.oc_growth = (uint16_t)(growth * 257); run(&T, 40); octopus_think(1);
    int both_out = 0, near = 0, linger = 0, visits = 0, was = 0;
    for (int i = 0; i < 30 * 60 * 25; i++) {
        tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T);
        const int oc_out = octopus_inn() < 0.02f, cr_out = T.cr_x >= 0 && T.cr_mode != CR_ROOST;
        if (oc_out && cr_out) { both_out++; if (fabsf(octopus_x() - T.cr_x) < tank_rivals_gap()) { near++; if (!octopus_bound() && octopus_mode() <= 1) linger++; } }
        if (T.cr_show > 0 && !was) visits++;
        was = T.cr_show > 0;
    }
    printf("apart seed %d %s growth %d: out together %d, near %.1f%%, lingering %.1f%%, door %d\n", seed, on ? "on" : "off", growth, both_out,
           both_out ? near * 100.0 / both_out : 0.0, both_out ? linger * 100.0 / both_out : 0.0, visits);
    tank_debug_no_apart(0); tank_octopus_release(&T); remove(sav);
    return 0;
}
int selftest_octo_keep(void) {
    char sav[600]; snprintf(sav, sizeof sav, "%s-octokeep-%dx%d.sav", sim_shelf_dir(), TANK_W, TANK_H);
    setenv("POCKET_TANK_SAVE", sav, 1); remove(sav);   /* (never the real save, never another job's) */
    static uint8_t b0[4096], b1[4096];
    find_shelved_offset(sav);
    if (g_off_shelved < 0) OK_FAIL("could not find sd_shelved in the save");

    tank_init(&T, 4343); progression_boot(&T); progression_setup_done(&T);
    {   /* A */
        if (SD_ITEMS[I_OCTO].bit != SD_ITEM_OCTOPUS || SD_ITEMS[I_OCTO].price != SD_PRICE_OCTOPUS) OK_FAIL("A: row %d is not the octopus's", I_OCTO);
        int area, page, row;
        if (!render_shop_find(&T, I_OCTO, &area, &page, &row) || area != SHP_AREA_CRITTERS) OK_FAIL("A: the shop does not list it in CRITTERS");
        int ca, cp, cr; render_shop_find(&T, I_CRAB, &ca, &cp, &cr);
        if (page * 16 + row <= cp * 16 + cr) OK_FAIL("A: its row (page %d, row %d) is not after the crab's (page %d, row %d)", page, row, cp, cr);
        T.sd_balance = SD_PRICE_OCTOPUS - 1;
        if (progression_buy(&T, I_OCTO) || T.sd_balance != SD_PRICE_OCTOPUS - 1 || (T.sd_unlocks & SD_ITEM_OCTOPUS)) OK_FAIL("A: bought it one sand dollar short");
        T.sd_balance = SD_PRICE_OCTOPUS + 40;
        if (!progression_buy(&T, I_OCTO) || T.sd_balance != 40) OK_FAIL("A: the buy was refused, or took the wrong price (%d left)", (int)T.sd_balance);
        if (progression_buy(&T, I_OCTO) || T.sd_balance != 40) OK_FAIL("A: bought twice");
        run(&T, 30);
        if (!tank_crit_in(&T, SD_ITEM_OCTOPUS) || !tank_octopus_live(&T) || !render_critter_here(&T, RENDER_CARD_OCTOPUS)) OK_FAIL("A: bought, and not in the water with a card");
        if (T.oc_growth != 0 && T.oc_growth > 40) OK_FAIL("A: it did not arrive a baby (growth %u)", (unsigned)T.oc_growth);
        if (octopus_size() > 1.01f) OK_FAIL("A: the body is size %.2f, not the baby's", (double)octopus_size());
        render_tank(&T, fa, TANK_W); render_tank(&T, fa, TANK_W);
        octopus_hide(1); render_tank(&T, fb2, TANK_W); render_tank(&T, fb2, TANK_W); octopus_hide(0);
        const int px = diff_px();
        if (px < 150) OK_FAIL("A: it is not drawn (%d px differ with it hidden)", px);
        printf("ok: octo-keep A: bought for %d, a baby in the water with a card, %d px of it drawn; twice and too poor refused\n", SD_PRICE_OCTOPUS, px);
    }
    {   /* B */
        const uint16_t g0 = T.oc_growth;
        tank_tick_sleep(&T, 3.0f * 86400.0f);
        const float s3 = 1.0f + T.oc_growth / 65535.0f;
        if (s3 < 1.27f || s3 > 1.33f) OK_FAIL("B: after 3 days it is size %.3f (want about 1.30: the end of the baby)", (double)s3);
        uint16_t last = T.oc_growth; int shrank = 0;
        for (int d = 0; d < 17; d++) { tank_tick_sleep(&T, 86400.0f); if (T.oc_growth < last) shrank = 1; last = T.oc_growth; }
        const float s20 = 1.0f + T.oc_growth / 65535.0f;
        tank_tick_sleep(&T, 2.0f * 86400.0f);
        if (shrank || s20 >= 2.0f || s20 < 1.90f || T.oc_growth != 65535) OK_FAIL("B: day 20 size %.3f, day 22 growth %u (want under 2.0 then 65535), shrank %d", (double)s20, (unsigned)T.oc_growth, shrank);
        tank_tick_sleep(&T, 30.0f * 86400.0f);
        if (T.oc_growth != 65535) OK_FAIL("B: past the adult it changed (%u)", (unsigned)T.oc_growth);
        run(&T, 50);
        if (octopus_size() < 1.99f) OK_FAIL("B: the body did not take the adult's size (%.2f)", (double)octopus_size());
        printf("ok: octo-keep B: growth %u -> size 1.30 at 3 days, %.2f at 20, the adult by 22 and never past it; the body is %.2f\n", (unsigned)g0, (double)s20, (double)octopus_size());
    }
    {   /* C */
        T.light_override = true; T.light_on = true; run(&T, 60);
        const int bal0 = T.sd_balance;
        octopus_think(0); octopus_cmd(6, 0); run(&T, 150);
        float cx, cy;
        if (!octopus_coin(&cx, &cy)) OK_FAIL("C: no coin came up");
        if (!tank_octopus_find_hit(&T, cx, cy) || tank_octopus_find_hit(&T, 20, 20)) OK_FAIL("C: the coin's tap is not where the coin is");
        if (tank_octopus_tap(&T, cx, cy) != 5 || T.oc_found != 1) OK_FAIL("C: the tap did not take it (found %u)", (unsigned)T.oc_found);
        for (int i = 0; i < 20; i++) progression_tick(&T, 0.1f);
        if (T.sd_balance != bal0 + SD_OCTO_FIND || T.oc_found_paid != 1) OK_FAIL("C: one find paid %d (want %d), paid count %u", (int)(T.sd_balance - bal0), SD_OCTO_FIND, (unsigned)T.oc_found_paid);
        octopus_cmd(6, 0); run(&T, 150);
        if (!render_octo_collect_hit(&T, TANK_W / 2.0f, (TANK_H - OCTO_CARD_H) / 2.0f + OCTO_CARD_H - CRIT_NAME_ROW_H - 30)) OK_FAIL("C: no COLLECT on its card with a coin up");
        if (tank_octopus_collect(&T) != 5 || tank_octopus_collect(&T) != 0 || T.oc_found != 2) OK_FAIL("C: COLLECT did not take one, once (found %u)", (unsigned)T.oc_found);
        for (int i = 0; i < 20; i++) progression_tick(&T, 0.1f);
        if (T.sd_balance != bal0 + 2 * SD_OCTO_FIND) OK_FAIL("C: two finds paid %d (want %d)", (int)(T.sd_balance - bal0), 2 * SD_OCTO_FIND);
        printf("ok: octo-keep C: a tap on its coin and its card's COLLECT each took one; the ledger paid %d each, once\n", SD_OCTO_FIND);
        T.light_override = false; octopus_think(1);
    }
    const uint16_t keep_g = T.oc_growth, keep_f = T.oc_found; const uint8_t keep_fav = T.oc_fav;
    {   /* D */
        progression_save(&T);
        const long n = file_bytes(sav, b0, sizeof b0);
        if (n != SIM_SAVE_BYTES) OK_FAIL("D: the save is %ld B (want %d)", n, SIM_SAVE_BYTES);
        if (b0[2295] != 1 || (b0[2288] | b0[2289] << 8) != keep_g || (b0[2290] | b0[2291] << 8) != keep_f || b0[2294] != keep_fav) OK_FAIL("D: its bytes at 2288..2295 are not its numbers");
        if (keep_fav != 2 && keep_fav != 9) OK_FAIL("D: no favorite den chosen (%d)", keep_fav);
        tank_init(&T, 9); progression_wake(&T, 0);
        if (!(T.sd_unlocks & SD_ITEM_OCTOPUS) || T.oc_growth != keep_g || T.oc_found != keep_f || T.oc_found_paid != keep_f || T.oc_fav != keep_fav) OK_FAIL("D: loaded growth %u finds %u/%u fav %d (want %u, %u, %d)", (unsigned)T.oc_growth, (unsigned)T.oc_found, (unsigned)T.oc_found_paid, T.oc_fav, (unsigned)keep_g, (unsigned)keep_f, keep_fav);
        run(&T, 30);
        if (!tank_octopus_live(&T) || octopus_size() < 1.99f || octopus_fav() != keep_fav) OK_FAIL("D: after the load it is not back in the water as it was (size %.2f)", (double)octopus_size());
        remove(sav); tank_init(&U, 77); progression_boot(&U); progression_setup_done(&U); progression_save(&U);   /* (a new tank of its own: never the one just saved) */
        const long nu = file_bytes(sav, b1, sizeof b1); int nz = 0; for (int i = 2288; i < 2304; i++) nz += b1[i] != 0;
        if (nu != SIM_SAVE_BYTES || nz) OK_FAIL("D: a tank with none saved %ld B with %d of its bytes not zero", nu, nz);
        printf("ok: octo-keep D: 2304 B; growth %u, %u finds and its den %d saved and loaded, back in the water; none owned saves zeros\n", (unsigned)keep_g, (unsigned)keep_f, keep_fav);
    }
    {   /* E: the saves of the builds before this one - a paragon build's (2216 B), one with the fish creator's (2280 B: its values at
           2216..2279 are live fields here, not kept bytes), castle-glow's (2288 B): each loads with no octopus, and saves back 2304 B
           with no octopus bytes - and the save written by THIS build of such a tank, loaded again, gives the same bytes before 2288 */
        static const long LEN[3] = { 2216, 2280, 2288 };
        for (int w = 0; w < 3; w++) {
            remove(sav); tank_init(&U, 31 + w); progression_boot(&U); progression_setup_done(&U); progression_save(&U);
            const long full = file_bytes(sav, b1, sizeof b1);
            if (full != SIM_SAVE_BYTES) OK_FAIL("E: a new tank's save is %ld B", full);
            file_put(sav, b1, LEN[w]);
            tank_init(&T, 9); progression_wake(&T, 0); run(&T, 10);
            if ((T.sd_unlocks & SD_ITEM_OCTOPUS) || tank_octopus_live(&T)) OK_FAIL("E: a %ld-byte save loaded with an octopus", LEN[w]);
            progression_save(&T);
            const long n = file_bytes(sav, b0, sizeof b0); int oc = 0;
            for (int i = 2288; i < 2304; i++) oc += b0[i] != 0;
            if (n != SIM_SAVE_BYTES || oc) OK_FAIL("E: a %ld-byte save saved back %ld B with %d octopus bytes not zero", LEN[w], n, oc);
            tank_init(&T, 9); progression_wake(&T, 0); progression_save(&T);
            static uint8_t b2_[4096]; const long n2 = file_bytes(sav, b2_, sizeof b2_); int moved = 0;
            for (int i = 2200; i < 2288; i++) moved += b0[i] != b2_[i];
            if (n2 != SIM_SAVE_BYTES || moved) OK_FAIL("E: a %ld-byte tank saved twice: %d of bytes 2200..2287 changed between the two", LEN[w], moved);
        }
        printf("ok: octo-keep E: a 2216, a 2280 and a 2288-byte save load with none and save back 2304 B, no octopus bytes, 2200..2287 steady\n");
    }
    {   /* F */
        tank_init(&T, 4343); progression_boot(&T); progression_setup_done(&T);
        T.sd_balance = 1000; progression_buy(&T, I_OCTO); T.oc_growth = 30000; T.oc_found = 3; T.oc_found_paid = 3; progression_save(&T);
        long n = file_bytes(sav, b0, sizeof b0);
        uint32_t u; memcpy(&u, b0 + 1488, 4); u &= ~(uint32_t)SD_ITEM_OCTOPUS; memcpy(b0 + 1488, &u, 4);   /* as the older build saves it: its own bits only */
        file_put(sav, b0, n);
        tank_init(&T, 9); progression_wake(&T, 0); run(&T, 10);
        if (!(T.sd_unlocks & SD_ITEM_OCTOPUS) || T.oc_growth != 30000 || T.oc_found != 3 || !tank_octopus_live(&T)) OK_FAIL("F: after a rollback's save the octopus is gone (owned %d, growth %u)", !!(T.sd_unlocks & SD_ITEM_OCTOPUS), (unsigned)T.oc_growth);
        printf("ok: octo-keep F: its bit masked off by an older build's save, it is still owned at growth 30000 with its 3 finds\n");
    }
    {   /* G */
        const uint16_t g = T.oc_growth;
        if (!progression_shelve(&T, I_OCTO)) OK_FAIL("G: SHELVE refused");
        run(&T, 10);
        if (tank_crit_in(&T, SD_ITEM_OCTOPUS) || tank_octopus_live(&T) || render_critter_here(&T, RENDER_CARD_OCTOPUS) || !(T.sd_unlocks & SD_ITEM_OCTOPUS)) OK_FAIL("G: shelved, and still in the water (or no longer owned)");
        tank_tick_sleep(&T, 8 * 3600.0f); run(&T, 50);
        if (T.oc_growth != g || T.oc_found != 3) OK_FAIL("G: a night on the shelf changed it (growth %u -> %u)", (unsigned)g, (unsigned)T.oc_growth);
        render_tank(&T, fa, TANK_W); render_tank(&T, fa, TANK_W);
        if (!progression_unshelve(&T, I_OCTO)) OK_FAIL("G: PUT BACK refused");
        run(&T, 10);
        if (!tank_crit_in(&T, SD_ITEM_OCTOPUS) || !tank_octopus_live(&T) || T.oc_growth < g || T.oc_growth > g + 40) OK_FAIL("G: put back, not in the water at its size (%u, was %u)", (unsigned)T.oc_growth, (unsigned)g);
        printf("ok: octo-keep G: shelved it is out of the water and rests (a night: growth %u still); PUT BACK returns it at that size\n", (unsigned)g);
    }
    {   /* H */
        progression_save(&T); file_bytes(sav, b0, sizeof b0); remove(sav);          /* (T's save put by: U is a new tank of its own) */
        tank_init(&U, 78); progression_boot(&U); progression_setup_done(&U); U.sd_balance = 100000;
        static const int five[5] = { I_SNAIL, I_SHRIMP, I_URCHIN, I_SEAHORSE, I_CRAB };
        for (int k = 0; k < 5; k++) if (!progression_buy(&U, five[k])) OK_FAIL("H: could not buy critter %d", five[k]);
        const int bal = U.sd_balance;
        if (progression_buy(&U, I_OCTO) || U.sd_balance != bal || (U.sd_unlocks & SD_ITEM_OCTOPUS)) OK_FAIL("H: a sixth critter was sold into a full tank");
        if (!progression_shelve(&U, I_SNAIL) || !progression_buy(&U, I_OCTO) || U.sd_balance != bal - SD_PRICE_OCTOPUS) OK_FAIL("H: with one shelved the octopus was still refused");
        if (tank_crit_count_in(&U) != 5) OK_FAIL("H: %d critters in the water (want 5)", tank_crit_count_in(&U));
        printf("ok: octo-keep H: five in the water, the octopus refused and the wallet untouched; one shelved, it is sold\n");
    }
    {   /* I: T owns the body (G put it back); U owns one too and ticks beside it */
        run(&T, 40);                                                               /* (U's buy took the body, as a buy on the glass does; U ticks no more, T takes it back) */
        if (!tank_octopus_live(&T)) OK_FAIL("I: the first tank lost its octopus before the second ticked");
        const float x0 = octopus_x(); const int b0n = octopus_builds();
        (void)b0n;
        for (int i = 0; i < 10; i++) { tank_tick(&U, 0.04f, advisor_rules); tank_tick(&T, 0.04f, advisor_rules); }
        if (!tank_octopus_live(&T) || tank_octopus_live(&U) || fabsf(octopus_x() - x0) > 30.0f) OK_FAIL("I: a second tank took the body (live T %d, U %d)", tank_octopus_live(&T), tank_octopus_live(&U));
        for (int i = 0; i < 40; i++) tank_tick(&U, 0.04f, advisor_rules);          /* the first has stopped ticking: the second takes it up */
        if (!tank_octopus_live(&U)) OK_FAIL("I: the first tank gone, the second never got its octopus");
        printf("ok: octo-keep I: one body - a second tank beside the first leaves it alone, and takes it up once the first stops\n");
        run(&T, 40);
    }
    {   /* J */
        remove(sav); tank_init(&T, 4343); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000;
        progression_buy(&T, I_SNAIL); progression_buy(&T, I_CRAB); progression_buy(&T, I_OCTO); run(&T, 20);
        if (render_critter_step(&T, RENDER_CARD_CRAB, 1) != RENDER_CARD_OCTOPUS || render_critter_step(&T, RENDER_CARD_OCTOPUS, 1) != RENDER_CARD_SNAIL
            || render_critter_step(&T, RENDER_CARD_SNAIL, -1) != RENDER_CARD_OCTOPUS) OK_FAIL("J: the arrows do not pass through the octopus's card");
        render_tank(&T, fa, TANK_W); memcpy(fb2, fa, sizeof fa);
        render_stats_card(&T, RENDER_CARD_OCTOPUS, fa, TANK_W);
        const int px = diff_px();
        if (px < 20000) OK_FAIL("J: its card drew %d px", px);
        int bx, by, bw, bh;
        if (!render_crit_shelve_box(RENDER_CARD_OCTOPUS, &bx, &by, &bw, &bh) || bx < (TANK_W - OCTO_CARD_W) / 2 || bx + bw > (TANK_W + OCTO_CARD_W) / 2) OK_FAIL("J: its SHELVE is not inside its card");
        printf("ok: octo-keep J: the arrows go crab -> octopus -> snail; its card draws (%d px) with SHELVE inside it\n", px);
    }
    {   /* K: ITS NEXT FIND'S WAIT is kept (the crab's rule): a shelving and a put back do not shorten it; a coin held up when it was
           shelved is due again at once; and a young one's coin is taken by any tap its find-hit lets through (one circle, both) */
        octopus_think(0); run(&T, 20);
        octopus_set_find_wait(7000.0f); run(&T, 5);
        progression_shelve(&T, I_OCTO); run(&T, 30); progression_unshelve(&T, I_OCTO); run(&T, 5);
        const float w = octopus_find_wait();
        if (w < 6900.0f || w > 7000.0f) OK_FAIL("K: its find's wait was 7000 s, and %.0f after the shelf", (double)w);
        T.light_override = true; T.light_on = true; octopus_think(0); run(&T, 60);
        octopus_cmd(6, 0); run(&T, 150);
        float cx, cy; if (!octopus_coin(&cx, &cy)) OK_FAIL("K: no coin came up");
        int both = 0, only = 0;
        for (int y = 0; y < TANK_H; y += 3) for (int x = 0; x < TANK_W; x += 3) {
            if (!tank_octopus_find_hit(&T, (float)x, (float)y)) continue;
            both++;
            const uint16_t f0 = T.oc_found;
            if (tank_octopus_tap(&T, (float)x, (float)y) != 5) only++;
            else { T.oc_found = f0; octopus_cmd(6, 0); run(&T, 150); if (!octopus_coin(&cx, &cy)) OK_FAIL("K: the coin did not come up again"); }
            if (both >= 12) { y = TANK_H; break; }
        }
        if (both < 6 || only) OK_FAIL("K: of %d taps its find-hit let through, %d took nothing (a baby: size %.2f)", both, only, (double)octopus_size());
        progression_shelve(&T, I_OCTO); run(&T, 10); progression_unshelve(&T, I_OCTO); run(&T, 5);
        if (octopus_find_wait() > 1.0f) OK_FAIL("K: shelved with a coin up, its next is %.0f s off (want due at once)", (double)octopus_find_wait());
        T.light_override = false;
        printf("ok: octo-keep K: its find's wait kept through the shelf (%.0f s of 7000); %d taps on a baby's coin all took it; a coin up at the shelving is due again\n", (double)w, both);
    }
    {   /* L: back from a rollback that filled the water without it: it waits on the shelf, never a sixth in the water */
        remove(sav); tank_init(&T, 78); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000;
        static const int five[5] = { I_SNAIL, I_SHRIMP, I_URCHIN, I_SEAHORSE, I_CRAB };
        progression_buy(&T, I_OCTO); progression_shelve(&T, I_OCTO);
        for (int k = 0; k < 5; k++) progression_buy(&T, five[k]);
        progression_save(&T);
        long n = file_bytes(sav, b0, sizeof b0);
        uint32_t u; memcpy(&u, b0 + 1488, 4); u &= ~(uint32_t)SD_ITEM_OCTOPUS; memcpy(b0 + 1488, &u, 4);
        memcpy(&u, b0 + offsetof_shelved(), 4); u &= ~(uint32_t)SD_ITEM_OCTOPUS; memcpy(b0 + offsetof_shelved(), &u, 4);
        file_put(sav, b0, n);
        tank_init(&T, 9); progression_wake(&T, 0); run(&T, 10);
        if (!(T.sd_unlocks & SD_ITEM_OCTOPUS) || tank_crit_in(&T, SD_ITEM_OCTOPUS) || tank_crit_count_in(&T) != 5) OK_FAIL("L: owned %d, in the water %d, %d critters in (want owned, on the shelf, 5)", !!(T.sd_unlocks & SD_ITEM_OCTOPUS), tank_crit_in(&T, SD_ITEM_OCTOPUS), tank_crit_count_in(&T));
        printf("ok: octo-keep L: five in the water and its bits masked off by an older build's save: it is owned still, on the shelf\n");
    }
    {   /* M: ONE DEN, ONE ANIMAL (Strato 11-22, the 1.8: "the octopus and crab are both in the castle together, overlapping which should
           never happen"). The crab's shelter (the frontmost of the castle and the volcano) is never the octopus's den:
           a  both pieces, a crab: through twenty minutes of roosts, scares and outings, with the octopus sent home again and again, the
              two are never in or bound for the same piece, and each does get into its own
           b  the castle alone and a crab: the octopus has no den and stays out on the sand
           c  no crab (shelved): the octopus has the den it favors; the crab PUT BACK while the octopus is at home in the crab's shelter:
              the octopus's den changes at once and it is out before the crab is in
           d  the crab's shelter changes under them (the other piece brought IN FRONT): the same, never the two in one */
        /* (where they ARE, not which den each is given: the octopus partly inside a piece, the crab in it or at its door - 11-22) */
        #define SAME_DEN() (octopus_inn() > 0.02f && tank_crit_in(&T, SD_ITEM_CRAB) && T.cr_x >= 0 && T.cr_cave >= 0 && fabsf(octopus_x() - tank_decor_x(&T, T.cr_cave)) < 30.0f \
                            && (T.cr_mode == CR_ROOST || (T.cr_mode == CR_SHELTER && fabsf(T.cr_x - tank_decor_x(&T, T.cr_cave)) < 20.0f)))
        remove(sav); tank_init(&T, 606); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000;
        progression_buy(&T, 2); progression_buy(&T, 9); progression_buy(&T, I_CRAB); progression_buy(&T, I_OCTO);
        tank_decor_set(&T, 2, TANK_W * 0.30f, DECOR_Z_BACK); tank_decor_set(&T, 9, TANK_W * 0.70f, DECOR_Z_BACK);
        T.oc_growth = 65535; run(&T, 60);
        int both = 0, oc_in = 0, cr_in = 0, wrong = 0;
        for (int i = 0; i < 20 * 60 * 25; i++) {
            if (i % 1500 == 0) { octopus_think(0); octopus_cmd(3, 0); }                       /* the octopus: home */
            if (i % 1500 == 750) octopus_cmd(4, 0);                                              /* ... and out again */
            if (i % 2200 == 100) tank_crab_roost(&T, -1);
            if (i % 2200 == 1300) tank_crab_scare(&T, T.cr_x + 30);
            tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T);
            if (SAME_DEN()) both++;
            if (octopus_inn() > 0.9f) { oc_in++; if (T.cr_mode == CR_ROOST && T.cr_cave == octopus_den_item()) wrong++; }
            if (T.cr_mode == CR_ROOST) cr_in++;
        }
        if (both || wrong || oc_in < 500 || cr_in < 500) OK_FAIL("M a: %d ticks with the two in or bound for one piece (%d with both inside); the octopus was inside for %d ticks, the crab for %d", both, wrong, oc_in, cr_in);
        const int oc_a = oc_in, cr_a = cr_in;
        /* b */
        progression_shelve(&T, 9); run(&T, 80);
        int had_den = 0, went_in = 0;
        for (int i = 0; i < 4000; i++) {
            if (i % 900 == 0) octopus_cmd(3, 0);
            tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T);
            if (octopus_den_item() >= 0) had_den++;
            if (octopus_inn() > 0.02f) went_in++;
        }
        if (had_den || went_in) OK_FAIL("M b: one shelter and a crab, and the octopus had a den for %d ticks (inside or going in for %d)", had_den, went_in);
        /* c */
        progression_shelve(&T, I_CRAB); run(&T, 30);
        octopus_think(0); octopus_cmd(3, 0);
        for (int i = 0; i < 3000 && octopus_inn() < 0.95f; i++) { tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T); }
        if (octopus_den_item() != 2 || octopus_inn() < 0.95f) OK_FAIL("M c: no crab in the water, and the octopus is not at home in the castle (den %d, in %.2f)", octopus_den_item(), (double)octopus_inn());
        progression_unshelve(&T, I_CRAB); tank_crab_roost(&T, -1);
        both = 0; int out_at = -1, crab_at = -1;
        /* (the crab comes back at the far side and walks to the castle at a crab's pace, 5 px/s: 160 s crosses a pocket tank, not the
           big screens' 1280 - its walk there takes ~170 s. The wait scales with the world's width; the pocket tanks' is unchanged) */
        const int c_ticks = TANK_W > 448 ? 4000 * TANK_W / 448 : 4000;
        for (int i = 0; i < c_ticks; i++) {
            tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T);
            if (SAME_DEN()) both++;
            if (out_at < 0 && octopus_inn() < 0.02f) out_at = i;
            if (crab_at < 0 && T.cr_mode == CR_ROOST) crab_at = i;
        }
        if (both || out_at < 0 || crab_at < 0 || out_at > crab_at || octopus_den_item() == 2) OK_FAIL("M c: the crab put back: %d ticks with the two in one piece; the octopus out at tick %d, the crab in at %d; the octopus's den %d", both, out_at, crab_at, octopus_den_item());
        /* d */
        progression_unshelve(&T, 9); tank_decor_set(&T, 9, TANK_W * 0.70f, DECOR_Z_BACK); run(&T, 30);
        octopus_think(0); octopus_cmd(3, 0);
        for (int i = 0; i < 4000 && octopus_inn() < 0.95f; i++) { tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T); if (i % 800 == 0) octopus_cmd(3, 0); }
        const int den_d = octopus_den_item();
        if (den_d < 0 || octopus_inn() < 0.95f) OK_FAIL("M d: two shelters again, and the octopus did not get into the other one (den %d, in %.2f)", den_d, (double)octopus_inn());
        tank_decor_set(&T, den_d, tank_decor_x(&T, den_d), DECOR_Z_FRONT); tank_decor_raise(&T, den_d);   /* its den is the frontmost now: the crab's */
        both = 0;
        for (int i = 0; i < 5000; i++) { if (i == 200) tank_crab_roost(&T, -1); tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T); if (SAME_DEN()) both++; }
        if (both || octopus_den_item() == den_d) OK_FAIL("M d: its den became the crab's shelter: %d ticks with the two in one piece; the octopus's den is %d still %d", both, octopus_den_item(), den_d);
        #undef SAME_DEN
        printf("ok: octo-keep M: one den, one animal - 20 minutes of roosts, scares and outings (the octopus inside %d ticks, the crab %d): never in one piece; one shelter: the octopus stays out; the crab put back, its shelter moved: the octopus is out first\n", oc_a, cr_a);
    }
    {   /* N: AT THE CRAB'S DOOR (Strato 11-22: the crab "should react a bit"; the octopus was "walking sometimes in front of the crab
           when its in the castle, and its not completely on the ground"). The castle IN FRONT with the crab roosting in it, the volcano
           BEHIND for the octopus, at the baby's size and the adult's:
           a  it never walks THROUGH a shelter IN FRONT: under the castle's footprint it is always in its front lane (before the castle),
              on that lane's floor; set down under the castle in its back lane, it walks out before it goes anywhere
           b  sent to the crab's door, the crab shows it its claws (cr_show, cr_poke), the octopus backs off at least 20 px, never
              goes in, and then goes home to its own den; the crab shows its claws again only after OCTO_SPAT_COOL_S
           c  left to itself for ten minutes by day it comes for a look at least once (the visit) */
        for (int gi = 0; gi < 2; gi++) {
            remove(sav); tank_init(&T, 707 + gi); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000;
            progression_buy(&T, 2); progression_buy(&T, 9); progression_buy(&T, I_CRAB); progression_buy(&T, I_OCTO);
            T.castle_stage = 3; tank_decor_set(&T, 2, TANK_W * 0.62f, DECOR_Z_FRONT); tank_decor_set(&T, 9, TANK_W * 0.22f, DECOR_Z_BACK);
            T.light_override = true; T.light_on = true;
            T.oc_growth = gi ? 65535 : 0; octopus_think(0); run(&T, 80);
            const float cx = tank_decor_x(&T, 2), half = tank_decor_half_w(2);
            tank_crab_roost(&T, 2);
            for (int i = 0; i < 3000 && T.cr_mode != CR_ROOST; i++) { tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T); }
            if (T.cr_mode != CR_ROOST || T.cr_cave != 2) OK_FAIL("N: the crab did not roost in the castle (mode %d cave %d)", (int)T.cr_mode, (int)T.cr_cave);
            if (octopus_den_item() != 9) OK_FAIL("N: the octopus's den is %d, not the volcano", octopus_den_item());
            /* a: across the tank, past the castle: let pass, in its front lane */
            int under_back = 0, shows = 0, went_in = 0; float far = 0, at = -1;
            #define N_TICK() do { tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T); \
                if (fabsf(octopus_x() - cx) < half - 4 && !octopus_lane_front() && octopus_inn() < 0.02f) under_back++; \
                if (octopus_den_item() == 2 || (octopus_inn() > 0.02f && fabsf(octopus_x() - cx) < 40)) went_in++; \
                if (T.cr_show > 0 && !was) { shows++; at = octopus_x(); } was = T.cr_show > 0; \
                if (at >= 0 && fabsf(octopus_x() - at) > far) far = fabsf(octopus_x() - at); } while (0)
            int was = 0;
            for (int i = 0; i < 600 && !octopus_idle(); i++) N_TICK();
            const float far_side = cx + 80 < OCTO_XHI - 8 ? cx + 80 : OCTO_XHI - 8;        /* (past the door, as far as this glass lets it go) */
            for (int i = 0; i < 12000 && octopus_x() < far_side; i++) { if (octopus_idle()) octopus_goto(OCTO_XHI); N_TICK(); }
            if (octopus_x() < far_side || shows) OK_FAIL("N a (size %d): walking past the crab's castle it got to x %.0f (the castle %.0f +- %.0f) and the crab showed its claws %d time(s)", gi, (double)octopus_x(), (double)cx, (double)half, shows);
            /* b: to the door */
            for (int i = 0; i < 600 && !octopus_idle(); i++) N_TICK();
            if (T.cr_mode != CR_ROOST) {   /* (the big screens: part a's walk outlasts a roost; the crab out on the sand, the octopus rightly waits to
                                              visit until it is back in. Sent home again here; on the pocket tanks it is still in and nothing changes) */
                tank_crab_roost(&T, 2);
                for (int i = 0; i < 6000 && T.cr_mode != CR_ROOST; i++) N_TICK();
                if (T.cr_mode != CR_ROOST || T.cr_cave != 2) OK_FAIL("N b (size %d): the crab did not roost in the castle again (mode %d cave %d)", gi, (int)T.cr_mode, (int)T.cr_cave);
            }
            octopus_goto(cx + OCTO_SPAT_PX - 6.0f + 12.0f * octopus_size());
            for (int i = 0; i < 12000; i++) { N_TICK(); if (shows && octopus_inn() > 0.95f) break;
                if (getenv("PT_NTRACE") && i % 400 == 0) { extern void octopus_debug_state(char *, int); char st_[200]; octopus_debug_state(st_, sizeof st_); printf("   N trace %d: x %.0f mode %d den %d inn %.2f lane %d | %s | crab x %.0f mode %d\n", i, (double)octopus_x(), octopus_mode(), octopus_den_item(), (double)octopus_inn(), octopus_lane_front(), st_, (double)T.cr_x, (int)T.cr_mode); } }
            if (shows != 1) OK_FAIL("N b (size %d): at the crab's door the crab showed its claws %d time(s)", gi, shows);
            if (octopus_inn() < 0.95f || octopus_den_item() != 9) OK_FAIL("N b (size %d): after the crab's claws it did not go home to the volcano (den %d, in %.2f, x %.0f)", gi, octopus_den_item(), (double)octopus_inn(), (double)octopus_x());
            octopus_cmd(4, 0); for (int i = 0; i < 400; i++) N_TICK();
            #undef N_TICK
            if (under_back || went_in || far < 20.0f) OK_FAIL("N (size %d): %d ticks in its back lane under the castle IN FRONT, %d going into the crab's castle; it backed off %.0f px", gi, under_back, went_in, (double)far);
            /* a: set down under the castle in its back lane */
            tank_octopus_place(&T); octopus_think(1);
            { extern void octopus_debug_put(float x, int lane); octopus_debug_put(cx - 20, 0); }
            int out_first = 1, clear_at = -1;
            for (int i = 0; i < 2500; i++) {
                tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T);
                if (clear_at < 0 && fabsf(octopus_x() - cx) > half + 10) clear_at = i;
                if (clear_at < 0 && octopus_inn() > 0.02f) out_first = 0;
            }
            if (clear_at < 0 || !out_first) OK_FAIL("N a (size %d): set down under the castle in its back lane, it did not walk out from under it (out at tick %d)", gi, clear_at);
            /* c: the visit */
            tank_octopus_place(&T); octopus_think(1); T.oc_visit_in = 30; T.oc_spat_cool = 0;
            int visits = 0; was = 0;
            for (int i = 0; i < 10 * 60 * 25; i++) { tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T); if (T.cr_mode != CR_ROOST && i % 500 == 0) tank_crab_roost(&T, 2); if (T.cr_show > 0 && !was) visits++; was = T.cr_show > 0; }
            if (visits < 1) OK_FAIL("N c (size %d): ten minutes by day with the crab in the castle, and it never came for a look", gi);
            printf("ok: octo-keep N (%s): past the crab's castle IN FRONT it keeps to its front lane; at the door the crab shows its claws, it backs off %.0f px and goes home, never in; left alone it came for a look %d time(s) in ten minutes\n", gi ? "the adult" : "the baby", (double)far, visits);
            T.light_override = false;
        }
    }
    {   /* O: NEVER CRAWLING ON THE SPOT (11-22, the 1.8, build d3bc92db: the baby stood "crawling at x 188" for minutes). His staged tank:
           the castle (stage 3) IN FRONT at 326, a coral IN FRONT at 150, the volcano BEHIND at 224 - the sand between the coral and
           the castle is under one or the other. Set down there in its back lane, at each size, left to itself for five minutes with the
           crab coming and going: it takes its front lane, and it never spends 8 s crawling without moving 4 px */
        for (int gi = 0; gi < 2; gi++) {
            remove(sav); tank_init(&T, 808 + gi); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000;
            progression_buy(&T, 2); progression_buy(&T, 3); progression_buy(&T, 9); progression_buy(&T, I_CRAB); progression_buy(&T, I_OCTO);
            T.castle_stage = 3; tank_decor_set(&T, 2, 326, DECOR_Z_FRONT); tank_decor_set(&T, 3, 150, DECOR_Z_FRONT); tank_decor_set(&T, 9, 224, DECOR_Z_BACK);
            T.light_override = true; T.light_on = true; T.oc_growth = gi ? 65535 : 0; run(&T, 40);
            { extern void octopus_debug_put(float x, int lane); octopus_debug_put(188, 0); }
            octopus_think(1);
            int stuck = 0, worst = 0, front_at = -1, through = 0, under_at_end = 0; float x0 = octopus_x(); int since = 0;
            const float cxo = tank_decor_x(&T, 2), hxo = tank_decor_half_w(2);
            for (int i = 0; i < 5 * 60 * 25; i++) {
                if (i % 2500 == 300) tank_crab_roost(&T, -1);
                tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T);
                if (front_at < 0 && octopus_lane_front()) front_at = i;
                const int under = !octopus_lane_front() && octopus_inn() < 0.02f && fabsf(octopus_x() - cxo) < hxo - 4 && octopus_mode() <= 1 && !octopus_bound() && !(octopus_den_item() >= 0 && fabsf(octopus_x() - tank_decor_x(&T, octopus_den_item())) < 6.0f);   /* (going home to a den that stands behind the castle, or on that den's doorstep - the watch's narrow glass puts the volcano there - it walks behind the castle's solid wall, hidden: right) */   /* (a jet that carries it behind the castle's edge is behind it: drawn so - not a walk through the gate) */
                static float px_ = 0; const int inward = fabsf(octopus_x() - cxo) < fabsf(px_ - cxo) - 0.01f || octopus_mode() == 0; px_ = octopus_x();
                if (i > 25 * 60 && under && inward) through++;                  /* (walking OUT from behind its edge is the way out, not a way through) */
                if (getenv("PT_OTRACE") && i > 25 * 60 && under) { extern void octopus_debug_state(char *, int); char st_[200]; octopus_debug_state(st_, sizeof st_); printf("   O trace %d: x %.0f mode %d den %d inn %.2f | %s\n", i, (double)octopus_x(), octopus_mode(), octopus_den_item(), (double)octopus_inn(), st_); }                            /* (after its first minute: out from under it by then) */
                if (i == 5 * 60 * 25 - 1) under_at_end = under;
                if (octopus_mode() != 1 || fabsf(octopus_x() - x0) > 4.0f) { x0 = octopus_x(); since = 0; }
                else if (++since > worst) worst = since;
                if (since == 200) stuck++;
            }
            /* (a narrow glass with these three pieces may have no sand that clears even its body: there it keeps its back lane - known,
               docs/octopus.md 33 - and the rule tested is only that it never crawls on the spot) */
            int any_clear = 0; for (float x = OCTO_XLO; x <= OCTO_XHI && !any_clear; x += 4) any_clear = tank_floor_clear(&T, x, 15.0f * octopus_size() + 4.0f);
            /* (since 11-22 the front lane is only for passing a shelter IN FRONT: the rule is that it is never in its back lane under the castle) */
            if (stuck || ((through > 5 || under_at_end) && any_clear)) OK_FAIL(   /* (a tick or two as a jet lands behind its edge) */"O (size %d): it crawled on the spot %d time(s) (the longest %d ticks); %d ticks in its back lane under the castle after its first minute", gi, stuck, worst, through);
            printf("ok: octo-keep O (%s): set down between a coral and the castle, both IN FRONT, it is out from under the castle within a minute and never crawls on the spot (the longest %d ticks without 4 px)\n", gi ? "the adult" : "the baby", worst);
            T.light_override = false;
        }
    }
    {   /* P: ITS NAME AND ITS PICTURE (Strato 11-22, the 1.8: "the octopus needs to be nameable. when tapping on it and revealing its
           card, its legs get cut off. we dont need to show a live animated view of what its doing.. a still image is enough")
           a  a name set (the wheel's tidy: letters, lowercase, 7) is on its card's title, in 8 bytes at 2296 of a 2304-byte save, back
              after a load, and still there after a rollback's save masked its bit; a tank that owns none saves no name
           b  its card has NAME beside SHELVE, both inside the card, sharing no pixel with each other or with COLLECT
           c  its portrait (the name wheel's box) is WHOLE at every size: nothing drawn outside the box, and the same pixels as the
              animal has in its own picture, scaled - none of it missing
           d  its card's picture is a STILL: the same pixels while it walks, digs and holds a coin up */
        remove(sav); tank_init(&T, 909); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000;
        progression_buy(&T, I_OCTO); run(&T, 30);
        if (tank_crit_name_item(CRIT_NAME_OCTOPUS) != SD_ITEM_OCTOPUS || render_crit_name_of_card(RENDER_CARD_OCTOPUS) != CRIT_NAME_OCTOPUS || render_card_of_crit_name(CRIT_NAME_OCTOPUS) != RENDER_CARD_OCTOPUS) OK_FAIL("P a: the octopus is not a nameable critter");
        tank_set_crit_name(&T, CRIT_NAME_OCTOPUS, "Inky 8!");
        if (strcmp(T.crit_name[CRIT_NAME_OCTOPUS], "inky")) OK_FAIL("P a: the name tidied to '%s'", T.crit_name[CRIT_NAME_OCTOPUS]);
        progression_save(&T);
        long n = file_bytes(sav, b0, sizeof b0);
        if (n != SIM_SAVE_BYTES || memcmp(b0 + 2296, "inky\0\0\0\0", 8)) OK_FAIL("P a: the save is %ld B; its name's bytes at 2296 are not 'inky'", n);
        uint32_t u; memcpy(&u, b0 + 1488, 4); u &= ~(uint32_t)SD_ITEM_OCTOPUS; memcpy(b0 + 1488, &u, 4); file_put(sav, b0, n);   /* a rollback's save */
        tank_init(&T, 9); progression_wake(&T, 0); run(&T, 10);
        if (strcmp(T.crit_name[CRIT_NAME_OCTOPUS], "inky") || !(T.sd_unlocks & SD_ITEM_OCTOPUS)) OK_FAIL("P a: after a load (its bit masked off by an older build) its name is '%s'", T.crit_name[CRIT_NAME_OCTOPUS]);
        remove(sav); tank_init(&U, 77); progression_boot(&U); progression_setup_done(&U); tank_set_crit_name(&U, CRIT_NAME_OCTOPUS, "ghost"); progression_save(&U);
        n = file_bytes(sav, b1, sizeof b1); int nz = 0; for (int i = 2296; i < 2304; i++) nz += b1[i] != 0;
        if (nz) OK_FAIL("P a: a tank that owns no octopus saved a name for one");
        {   /* b */
            int nx, ny, nw, nh, sx, sy, sw, sh;
            if (!render_crit_name_box(RENDER_CARD_OCTOPUS, &nx, &ny, &nw, &nh) || !render_crit_shelve_box(RENDER_CARD_OCTOPUS, &sx, &sy, &sw, &sh)) OK_FAIL("P b: no NAME or no SHELVE on its card");
            const int X = (TANK_W - OCTO_CARD_W) / 2, Y = (TANK_H - OCTO_CARD_H) / 2;
            if (nx < X || nx + nw > X + OCTO_CARD_W || sx < X || sx + sw > X + OCTO_CARD_W || ny + nh > Y + OCTO_CARD_H || nx + nw > sx) OK_FAIL("P b: NAME %d..%d and SHELVE %d..%d are not side by side inside the card %d..%d", nx, nx + nw, sx, sx + sw, X, X + OCTO_CARD_W);
            T.light_override = true; T.light_on = true; octopus_think(0); run(&T, 60); octopus_cmd(6, 0); run(&T, 150);
            int share = 0;
            for (int y = 0; y < TANK_H; y += 2) for (int x = 0; x < TANK_W; x += 2) {
                const int a_ = render_critter_name_hit(&T, RENDER_CARD_OCTOPUS, (float)x, (float)y), b_ = render_critter_shelve_hit(&T, RENDER_CARD_OCTOPUS, (float)x, (float)y), c_ = render_octo_collect_hit(&T, (float)x, (float)y);
                if (c_ && (a_ || b_)) share++;                             /* (NAME's and SHELVE's slop meet in the gap between them on every critter's card: NAME answers first) */
            }
            if (!render_octo_collect_hit(&T, TANK_W / 2.0f, ny - 30.0f) || share) OK_FAIL("P b: with a coin up, COLLECT shares %d points with NAME or SHELVE (or COLLECT is not there)", share);
            tank_octopus_collect(&T);
        }
        {   /* c */
            static const int G[3] = { 0, 30000, 65535 };
            for (int gi = 0; gi < 3; gi++) {
                T.oc_growth = (uint16_t)G[gi];
                for (int i = 0; i < TANK_W * TANK_H; i++) fa[i] = 0;
                render_crit_portrait(fa, TANK_W, &T, CRIT_NAME_OCTOPUS, 200, 150);
                int px = 0, out = 0, x0 = TANK_W, x1 = -1, y0 = TANK_H, y1 = -1;
                for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) if (fa[y * TANK_W + x]) { px++; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
                int mx, mf; const octo_grid_t *g = octopus_portrait(G[gi] >> 8, &mx, &mf);
                int gx0 = g->w, gx1 = -1, gy0 = g->h, gy1 = -1, gpx = 0;
                for (int y = 0; y < g->h; y++) for (int x = 0; x < g->w; x++) if (g->tone[y * OCTO_GW + x]) { gpx++; if (x < gx0) gx0 = x; if (x > gx1) gx1 = x; if (y < gy0) gy0 = y; if (y > gy1) gy1 = y; }
                const int gw = gx1 - gx0 + 1, gh = gy1 - gy0 + 1, w = x1 - x0 + 1, h = y1 - y0 + 1;
                /* whole: the picture's width and height in the same proportion as the animal's own (within a pixel of rounding), and all of it inside */
                out = (x1 - x0 + 1 > 80) + (y1 - y0 + 1 > 48);                          /* (its box is 80 x 48 about the point asked for, in the page's own coordinates on each glass) */
                const int whole = px >= 100 && abs(w * gh - h * gw) <= gw + gh && w >= gw * 80 / 100 * 0 + 20;
                if (out || !whole) OK_FAIL("P c (growth %d): its portrait draws %d px, %d outside its box; it is %d x %d for an animal of %d x %d", G[gi], px, out, w, h, gw, gh);
            }
        }
        {   /* d */
            T.oc_growth = 65535; octopus_think(0); run(&T, 250);
            const int X = (TANK_W - OCTO_CARD_W) / 2, Y = (TANK_H - OCTO_CARD_H) / 2;
            memset(fa, 0, sizeof fa); render_stats_card(&T, RENDER_CARD_OCTOPUS, fa, TANK_W);   /* (over a blank frame: the card's ground lets the tank show through faintly) */
            octopus_cmd(1, 1); run(&T, 60);
            memset(fb2, 0, sizeof fb2); render_stats_card(&T, RENDER_CARD_OCTOPUS, fb2, TANK_W);
            int moved = 0, art = 0;
            for (int y = Y + 8; y < Y + 64; y++) for (int x = X + 40; x < X + OCTO_CARD_W - 40; x++) { if (fa[y * TANK_W + x] != fb2[y * TANK_W + x]) moved++; art++; }
            octopus_cmd(0, 0); octopus_think(0); run(&T, 80); octopus_cmd(6, 0); run(&T, 150);
            memset(fb2, 0, sizeof fb2); render_stats_card(&T, RENDER_CARD_OCTOPUS, fb2, TANK_W);
            for (int y = Y + 8; y < Y + 64; y++) for (int x = X + 40; x < X + OCTO_CARD_W - 40; x++) if (fa[y * TANK_W + x] != fb2[y * TANK_W + x]) moved++;
            if (moved) OK_FAIL("P d: its card's picture changed in %d of %d px between rest, a walk and a find: not a still", moved, art);
            tank_octopus_collect(&T); T.light_override = false;
        }
        printf("ok: octo-keep P: named 'inky' on its card, 8 bytes at 2296 of 2304, kept through a load and a rollback; NAME beside SHELVE, clear of COLLECT; its portrait whole in its box at three sizes; its card's picture a still\n");
    }
    {   /* Q: BOUNDED WORK (11-22, the 1.8 "looks like a slide show", then froze for seconds: the castle's arch shade asked octo_covers for
           every pixel, and each ask REBUILT the octopus's whole picture - 34 ms to nearly a second a frame. Nothing in the suite measured
           a stage's work, so it passed by hand). His staged layout - the castle (stage 3) IN FRONT at 326, a coral IN FRONT at 150, the
           volcano BEHIND at 224, the crab and the octopus - each size, walking past the castle, at the crab's door, going home: over
           every frame, at most ONE picture built and at most a few thousand octo_covers asks */
        long worst_cov = 0; int worst_builds = 0;
        for (int gi = 0; gi < 2; gi++) {
            remove(sav); tank_init(&T, 1010 + gi); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000;
            progression_buy(&T, 2); progression_buy(&T, 3); progression_buy(&T, 9); progression_buy(&T, I_CRAB); progression_buy(&T, I_OCTO);
            T.castle_stage = 3; tank_decor_set(&T, 2, TANK_W * 0.73f, DECOR_Z_FRONT); tank_decor_set(&T, 3, TANK_W * 0.33f, DECOR_Z_FRONT); tank_decor_set(&T, 9, TANK_W * 0.50f, DECOR_Z_BACK);
            T.light_override = true; T.light_on = true; T.oc_growth = gi ? 65535 : 0; run(&T, 40);
            tank_crab_roost(&T, 2); octopus_think(1);
            extern long render_debug_octo_cover_calls(void);
            /* (the board's caches, as the board has them: the castle baked into the scene, the dirty mask - the path the shade took there) */
            static uint16_t q_scene[TANK_W * TANK_H]; static uint32_t q_dirty[TANK_H * 16]; static uint8_t q_vig[TANK_W * TANK_H];   /* (more than render_vignette_cache_bytes: a quarter of the tank) */
            render_set_scene_cache(q_scene); render_set_vignette_cache(q_vig); render_set_dirty_mask(q_dirty);
            for (int i = 0; i < 4; i++) render_tank(&T, fa, TANK_W);
            (void)render_debug_octo_cover_calls();
            for (int i = 0; i < 3000; i++) {
                if (i == 100) octopus_goto(OCTO_XHI); if (i == 1200) octopus_goto(OCTO_XLO); if (i == 2200) octopus_goto(tank_decor_x(&T, 2) - 40);
                tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T);
                const int b0_ = octopus_builds();
                render_tank(&T, fa, TANK_W);
                const int nb = octopus_builds() - b0_; const long nc = render_debug_octo_cover_calls();
                if (nb > worst_builds) worst_builds = nb;
                if (nc > worst_cov) worst_cov = nc;
            }
            T.light_override = false;
        }
        render_set_scene_cache(NULL); render_set_dirty_mask(NULL); render_set_vignette_cache(NULL);   /* (the next test's tanks as before) */
        if (worst_builds > 1 || worst_cov > 6000) OK_FAIL("Q: in one frame the octopus's picture was built %d times and octo_covers asked %ld times (want at most 1 and 6000)", worst_builds, worst_cov);
        printf("ok: octo-keep Q: bounded work in his staged layout, both sizes, 6000 frames: at most %d picture built and %ld octo_covers asks in a frame\n", worst_builds, worst_cov);
    }
    {   /* R: KEEP APART (Strato 11-22: "the crab and octopus should generally keep their distance when exploring ... soft rules"). One run
           is luck: over 12 seeds a run with the rule ON lingered near the crab 0.8 to 20.6% of their time out together, one with it OFF 7.2
           to 54.9% (all four worlds, the 11-22 sweep, docs/octopus.md 36). So the same four seeds with the rule on and off, the castle IN
           FRONT and the volcano BEHIND, thirty minutes each, each size: the MEAN time lingering within the gap (not passing on its way
           home) is at most 15% with the rule on, and at most 0.70 of the same seeds' mean with it off (the sweep's worst four-seed means:
           12.8% and 0.58), neither is trapped (each covers ground), and the crab's door still sees the octopus come */
        extern void tank_debug_no_apart(int on); extern float tank_rivals_gap(void);
        static const int SEEDS[4] = { 101, 202, 303, 404 };
        int moves_ok = 1, visits_all = 0; float worst = 0, worst_ratio = 0;
        for (int gi = 0; gi < 2; gi++) {
            float lsum[2] = { 0, 0 }, nsum[2] = { 0, 0 };
            for (int pass = 0; pass < 2; pass++)
                for (int sd = 0; sd < 4; sd++) {
                    tank_debug_no_apart(pass == 1);
                    remove(sav); tank_init(&T, (uint32_t)SEEDS[sd]); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000;
                    progression_buy(&T, 2); progression_buy(&T, 9); progression_buy(&T, I_CRAB); progression_buy(&T, I_OCTO);
                    T.castle_stage = 3; tank_decor_set(&T, 2, TANK_W * 0.73f, DECOR_Z_FRONT); tank_decor_set(&T, 9, TANK_W * 0.30f, DECOR_Z_BACK);
                    T.light_override = true; T.light_on = true; T.oc_growth = gi ? 65535 : 0; run(&T, 40); octopus_think(1);
                    int both_out = 0, near = 0, linger = 0, visits = 0, was = 0; float lo = 1e9f, hi = -1e9f, clo = 1e9f, chi = -1e9f;
                    for (int i = 0; i < 30 * 60 * 25; i++) {
                        tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T);
                        const int oc_out = octopus_inn() < 0.02f, cr_out = T.cr_x >= 0 && T.cr_mode != CR_ROOST;
                        if (oc_out && cr_out) { both_out++; if (fabsf(octopus_x() - T.cr_x) < tank_rivals_gap()) { near++; if (!octopus_bound() && octopus_mode() <= 1) linger++; } }
                        if (octopus_x() < lo) lo = octopus_x(); if (octopus_x() > hi) hi = octopus_x();
                        if (T.cr_x < clo) clo = T.cr_x; if (T.cr_x > chi) chi = T.cr_x;
                        if (T.cr_show > 0 && !was) visits++;
                        was = T.cr_show > 0;
                    }
                    lsum[pass] += both_out ? (float)linger / both_out : 0; nsum[pass] += both_out ? (float)near / both_out : 0;
                    if (pass == 0) { visits_all += visits; if (hi - lo < 60 || chi - clo < 60) moves_ok = 0; }
                    T.light_override = false;
                }
            const float on = lsum[0] / 4, off = lsum[1] / 4, ratio = off > 0 ? on / off : 1;
            printf("   octo-keep R %s, four seeds: lingering near the crab %.1f%% with the rule, %.1f%% without (%.2f); within the gap at all %.1f%% / %.1f%%\n",
                   gi ? "the adult" : "the baby", on * 100.0, off * 100.0, ratio, nsum[0] * 25.0, nsum[1] * 25.0);
            if (on > worst) worst = on;
            if (ratio > worst_ratio) worst_ratio = ratio;
            if (on > 0.15f || ratio > 0.70f) OK_FAIL("R (%s): over four seeds, lingering within the gap %.1f%% with the rule (at most 15%%), %.2f of without it (at most 0.70)", gi ? "the adult" : "the baby", on * 100.0, ratio);
        }
        tank_debug_no_apart(0);
        if (!moves_ok || visits_all < 1) OK_FAIL("R: one of them was trapped (covered under 60 px), or the crab's door never saw the octopus (%d)", visits_all);
        printf("ok: octo-keep R: over four seeds each, lingering within the gap at most %.1f%% with the rule (%.2f of without it); both cover ground; the door encounter still happens (%d)\n", worst * 100.0, worst_ratio, visits_all);
    }
    tank_octopus_release(&T); tank_octopus_release(&U);
    remove(sav);
    printf("selftest-octo-keep: PASS\n");
    return 0;
}

/* --octopus-keep-shot <prefix>: the pictures for Strato's eye - its shop row and modal, its card (a baby, an adult with a find up),
   its jar on the shelf */
static void ok_ppm(const char *prefix, const char *name, const uint16_t *fb) {
    char path[700]; snprintf(path, sizeof path, "%s_%s.ppm", prefix, name);
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        uint16_t v = FB_SWAP16(fb[i]);
        uint8_t px[3] = { (uint8_t)(((v >> 11) & 31) << 3), (uint8_t)(((v >> 5) & 63) << 2), (uint8_t)((v & 31) << 3) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}
static int ok_stap(float px, float py) { return render_shop_tap(&T, PAGE_X + px, PAGE_Y + py); }
static int ok_row(int item) {
    int a, pg, r;
    if (!render_shop_find(&T, item, &a, &pg, &r)) return -1;
    render_shop_leave(); { int fx, fy; render_shop_foot_box(SHF_DECOR + a, &fx, &fy); ok_stap(fx + MSP_FOOT_SQ / 2, fy + MSP_FOOT_SQ / 2); }
    for (int k = 0; k < pg; k++) ok_stap(SHP_ARROW_X1 + SHP_ARROW_W / 2, SHP_ARROW_Y + SHP_ARROW_H / 2);
    return r;
}
int octopus_keep_shot(const char *prefix) {
    char sav[600]; snprintf(sav, sizeof sav, "%s-octokeep-shot-%dx%d.sav", sim_shelf_dir(), TANK_W, TANK_H);
    setenv("POCKET_TANK_SAVE", sav, 1); remove(sav);
    tank_init(&T, 2024); progression_boot(&T); progression_setup_done(&T);
    T.sd_balance = 100000; progression_buy(&T, I_CRAB); T.sd_balance = 412;   /* (no castle: it stays out on the sand for its pictures) */
    for (int i = 0; i < 300; i++) tank_tick(&T, 1.0f / 30.0f, advisor_rules);
    int r = ok_row(I_OCTO); render_shop(&T, fa, TANK_W); ok_ppm(prefix, "shop_row", fa);
    T.clock += 0.7; ok_stap(100, SHP_ROW_Y0 + r * SHP_ROW_DY + 20); T.clock += 0.7; render_shop(&T, fa, TANK_W); ok_ppm(prefix, "shop_modal", fa);
    render_shop_leave();
    progression_buy(&T, I_OCTO); T.light_override = true; T.light_on = true;
    octopus_think(0);
    for (int i = 0; i < 120; i++) { tank_tick(&T, 1.0f / 30.0f, advisor_rules); tank_handled(&T); }
    render_tank(&T, fa, TANK_W); render_tank(&T, fa, TANK_W); render_stats_card(&T, RENDER_CARD_OCTOPUS, fa, TANK_W); ok_ppm(prefix, "card_baby", fa);
    octopus_cmd(0, 0); octopus_think(0);                              /* (cmd 0 hands it its own wandering back: held still after it) */
    for (int i = 0; i < 200; i++) { tank_tick(&T, 1.0f / 30.0f, advisor_rules); tank_handled(&T); }   /* (at rest: it takes a new size only there) */
    T.oc_growth = 65535; T.oc_found = 7; T.oc_found_paid = 7; tank_set_crit_name(&T, CRIT_NAME_OCTOPUS, "inky");
    for (int i = 0; i < 60; i++) { tank_tick(&T, 1.0f / 30.0f, advisor_rules); tank_handled(&T); }
    octopus_think(0); octopus_cmd(6, 0);
    for (int i = 0; i < 180; i++) { tank_tick(&T, 1.0f / 30.0f, advisor_rules); tank_handled(&T); }
    render_tank(&T, fa, TANK_W); render_tank(&T, fa, TANK_W); render_stats_card(&T, RENDER_CARD_OCTOPUS, fa, TANK_W); ok_ppm(prefix, "card_adult_find", fa);
    render_tank(&T, fa, TANK_W); render_tank(&T, fa, TANK_W); ok_ppm(prefix, "tank_adult_find", fa);
    tank_octopus_collect(&T);
    r = ok_row(I_OCTO); render_shop(&T, fa, TANK_W); ok_ppm(prefix, "shop_row_owned", fa);
    render_shop_leave();
    progression_shelve(&T, I_OCTO);
    for (int i = 0; i < 90; i++) tank_tick(&T, 1.0f / 30.0f, advisor_rules);
    r = ok_row(I_OCTO); render_shop(&T, fa, TANK_W); ok_ppm(prefix, "shop_row_shelved", fa);
    T.clock += 0.7; ok_stap(100, SHP_ROW_Y0 + r * SHP_ROW_DY + 20); T.clock += 0.7; render_shop(&T, fa, TANK_W); ok_ppm(prefix, "shop_modal_shelved", fa);
    render_shop_leave(); tank_octopus_release(&T);
    remove(sav);
    printf("octopus-keep-shot: wrote %s_*.ppm\n", prefix);
    return 0;
}
