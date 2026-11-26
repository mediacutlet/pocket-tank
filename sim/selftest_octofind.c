#define _DEFAULT_SOURCE
/* selftest_octofind.c - --selftest-octo-find (0.4.3 audit round 1: PE-1, PE-5). THE OCTOPUS'S NEXT FIND across a load, and the
 * find ledgers of a damaged save. Sibling pins: the crab's finds and its wait, a shelving and PUT BACK, a coin held up.
 *   F1  (V4's --v4-wait, adopted): 1 h awake / 25 min asleep for 12 h. A tank whose every sleep is a save and a load (a board past
 *       the grace) finds about as many as one that sleeps in place; the crab, whose wait was always saved, finds the same either way
 *   F2  a coin held up at the save: due again at the load (the crab's rule), never a full wait
 *   F3  a save that says no wait (an older save, a 0.4.3 dev save: the bytes were pad): the fallback, a full wait (or the first
 *       find's short one for an octopus that never found one); a saved wait longer than any it starts is cut to 3.5 h
 *   F4  (PE-5) a damaged save: found 65535 / paid 0 for both; paid over found; an honest save one behind still pays that one
 *   F5  a shelving and PUT BACK keeps its wait; a save while it is shelved keeps it too
 *   F6  (SOC-7) the castle and the volcano stood 18 px apart, the octopus in its den in one of them: it is in that piece only, never
 *       in both (the crab's ONE DEN, ONE ANIMAL wait at the door and its put-back read octopus_in_piece)
 *   F7  MY TANKS: its tank swapped out for a new one and back in (a SWAP IN loads the save into the one tank_t): its wait kept
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tank.h"
#include "advisor.h"
#include "progression.h"
#include "octopus.h"
#include "render.h"
#include "shelf.h"
#include "setup.h"
#ifndef OC_FIND_WAIT_MAX_S
#define OC_FIND_WAIT_MAX_S 12600.0f   /* (tank.h has it since the fix; the test also builds on the tree before it) */
#endif

const char *sim_shelf_dir(void);
goal_t advisor_rules(const tank_t *t, int fish_idx, bool request);
void progression_slept(tank_t *t, float seconds);
static tank_t T;
static int fails;
static char s_sav[700];
#define OF_FAIL(...) do { printf("FAIL: octo-find: "); printf(__VA_ARGS__); printf("\n"); fails++; } while (0)
enum { I_CASTLE = 2, I_VOLCANO = 9, I_CRAB = 10, I_OCTO = 11 };
bool octopus_in_piece(const tank_t *t, int item);

static void fresh(int seed, int item) {
    snprintf(s_sav, sizeof s_sav, "%s-octofind-%dx%d.sav", sim_shelf_dir(), TANK_W, TANK_H);
    setenv("POCKET_TANK_SAVE", s_sav, 1); remove(s_sav);
    tank_init(&T, (uint32_t)seed); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000;
    if (item >= 0) progression_buy(&T, item);
    T.light_override = true; T.light_on = true;
}
static void load(int seed) {                          /* RAM gone: the save read back into a new tank, as a boot does */
    progression_save(&T); tank_init(&T, (uint32_t)seed); progression_wake(&T, 0);
    T.light_override = true; T.light_on = true;
}
static void ticks(int n) { for (int i = 0; i < n; i++) { tank_tick(&T, 0.05f, advisor_rules); tank_handled(&T); } }
static void patch(long at, const void *v, size_t n) {
    FILE *f = fopen(s_sav, "r+b"); if (!f) { OF_FAIL("no save to patch"); return; }
    fseek(f, at, SEEK_SET); fwrite(v, n, 1, f); fclose(f);
}

/* F1: mode 0 sleep = save + load + the sleep lived at the wake; 1 sleep in place; 2 never sleeps. crit 0 the octopus, 1 the crab */
static int run_one(int seed, int mode, int crit, float awake_s, float sleep_s, float hours, int *loads) {
    fresh(seed, crit ? I_CRAB : I_OCTO);
    const float dt = 0.05f; double lived = 0; int nl = 0;
    while (lived < hours * 3600.0) {
        const float aw = mode == 2 ? (float)(hours * 3600.0) : awake_s;
        for (float s = 0; s < aw && lived < hours * 3600.0; s += dt, lived += dt) {
            tank_tick(&T, dt, advisor_rules); tank_handled(&T);
            float cx, cy;
            if (!crit && octopus_coin(&cx, &cy)) tank_octopus_tap(&T, cx, cy);
            if (crit && T.cr_find > 0) tank_crab_tap(&T, T.cr_x);
        }
        if (mode == 2 || lived >= hours * 3600.0) break;
        if (mode == 0) { load(seed + 1000 + nl); nl++; }
        progression_slept(&T, sleep_s);
        lived += sleep_s;
    }
    *loads = nl;
    const int got = crit ? T.cr_found : T.oc_found;
    tank_octopus_release(&T);
    return got;
}
static void case_f1(void) {
    for (int seed = 11; seed <= 12; seed++) {
        int g[2][3], nl = 0, n;
        for (int crit = 0; crit < 2; crit++)
            for (int mode = 0; mode < 3; mode++) { g[crit][mode] = run_one(seed, mode, crit, 3600, 1500, 12, &n); if (mode == 0) nl = n; }
        printf("octo-find F1 seed %d, 1 h awake / 25 min asleep, 12 h: octopus %d save+load (%d loads), %d asleep in place, %d never asleep; crab %d / %d / %d\n",
               seed, g[0][0], nl, g[0][1], g[0][2], g[1][0], g[1][1], g[1][2]);
        if (abs(g[0][0] - g[0][1]) > 1) OF_FAIL("F1 seed %d: the octopus found %d with a load at every sleep, %d sleeping in place (its wait is not kept across a load)", seed, g[0][0], g[0][1]);
        if (g[0][1] < 2) OF_FAIL("F1 seed %d: the octopus found only %d in 12 h sleeping in place (the run proves nothing)", seed, g[0][1]);
        if (g[1][0] != g[1][1]) OF_FAIL("F1 seed %d: the crab found %d with loads, %d without (its saved wait changed)", seed, g[1][0], g[1][1]);
    }
}
static void case_f2(void) {
    fresh(21, I_OCTO); ticks(40);
    octopus_set_find_wait(0.0f);                       /* its find due now: it digs at its next rest */
    int up = 0; for (int i = 0; i < 144000 && !up; i++) { ticks(1); up = octopus_find_left() > 0; }
    if (!up) { OF_FAIL("F2: no coin came up to hold"); return; }
    load(22); ticks(2);
    const float w = octopus_find_wait();
    printf("octo-find F2: a coin held up at the save; after the load its next find is %.0f s off\n", (double)w);
    if (w > 1.0f) OF_FAIL("F2: a coin held up at the save is %.0f s off after the load (due again at once, the crab's rule)", (double)w);
    tank_octopus_release(&T);
}
static void case_f3(void) {
    const uint32_t none = 0, huge = 1u + 999999u;
    for (int found = 0; found < 2; found++) {
        fresh(31 + found, I_OCTO); ticks(40);
        if (found) { T.oc_found = 3; T.oc_found_paid = 3; }
        octopus_set_find_wait(600.0f); ticks(2);
        progression_save(&T); patch(2284, &none, 4);
        tank_init(&T, 33); progression_wake(&T, 0); T.light_override = true; T.light_on = true; ticks(2);
        const float w = octopus_find_wait();
        const float lo = found ? 9000.0f : 2700.0f, hi = found ? 12600.0f : 5400.0f;
        printf("octo-find F3: a save with no wait said, an octopus that %s: its next find %.0f s off (want %.0f..%.0f)\n", found ? "found 3" : "never found one", (double)w, (double)lo, (double)hi);
        if (w < lo - 5 || w > hi + 1) OF_FAIL("F3: no wait said, found %d: %.0f s (want the fallback %.0f..%.0f)", found ? 3 : 0, (double)w, (double)lo, (double)hi);
        tank_octopus_release(&T);
    }
    fresh(34, I_OCTO); ticks(40); progression_save(&T); patch(2284, &huge, 4);
    tank_init(&T, 35); progression_wake(&T, 0); ticks(2);
    if (octopus_find_wait() > OC_FIND_WAIT_MAX_S + 1) OF_FAIL("F3: a damaged wait loads as %.0f s (never longer than %.0f)", (double)octopus_find_wait(), (double)OC_FIND_WAIT_MAX_S);
    tank_octopus_release(&T);
}
static void case_f4(void) {
    struct { uint16_t of, op, cf, cp; int want_max, want_min; const char *what; } C[] = {
        { 65535, 0, 65535, 0, SD_OCTO_FIND + SD_CRAB_FIND_AGAIN, 0, "found 65535, paid 0 (both)" },
        { 4, 9, 4, 9, 0, 0, "paid over found" },
        { 3, 2, 1, 0, SD_OCTO_FIND + SD_CRAB_FIND, SD_OCTO_FIND + SD_CRAB_FIND, "honest, one behind each (the crab's first find)" },
        { 7, 7, 5, 5, 0, 0, "honest, all paid" },
    };
    for (unsigned k = 0; k < sizeof C / sizeof C[0]; k++) {
        fresh(41, I_OCTO); progression_buy(&T, I_CRAB); ticks(40);
        T.oc_found = T.oc_found_paid = 0; T.cr_found = T.cr_found_paid = 0;
        progression_tick(&T, 0.05f); progression_save(&T);
        patch(2290, &C[k].of, 2); patch(2292, &C[k].op, 2); patch(1932, &C[k].cf, 2); patch(1934, &C[k].cp, 2);
        tank_init(&T, 42); progression_wake(&T, 0); T.light_override = true; T.light_on = true;
        const int32_t b0 = T.sd_balance;
        for (int i = 0; i < 40; i++) { progression_tick(&T, 0.05f); tank_tick(&T, 0.05f, advisor_rules); }
        const int gain = (int)(T.sd_balance - b0);
        printf("octo-find F4: %s: paid %d sand dollars on load (octopus %u/%u, crab %u/%u)\n", C[k].what, gain, T.oc_found, T.oc_found_paid, T.cr_found, T.cr_found_paid);
        if (gain > C[k].want_max || gain < C[k].want_min) OF_FAIL("F4: %s: paid %d (want %d..%d)", C[k].what, gain, C[k].want_min, C[k].want_max);
        if (T.oc_found_paid > T.oc_found || T.cr_found_paid > T.cr_found) OF_FAIL("F4: %s: a ledger paid over its finds", C[k].what);
        if (T.oc_found != C[k].of || T.cr_found != C[k].cf) OF_FAIL("F4: %s: the finds changed (%u, %u)", C[k].what, T.oc_found, T.cr_found);
        tank_octopus_release(&T);
    }
}
static void case_f5(void) {
    fresh(51, I_OCTO); ticks(40);
    octopus_set_find_wait(1234.0f); ticks(1);
    const float w0 = octopus_find_wait();
    if (!progression_shelve(&T, I_OCTO)) { OF_FAIL("F5: the shelve was refused"); return; }
    ticks(20);
    load(52);                                          /* a save and a load while it is on the shelf */
    ticks(20);
    if (!progression_unshelve(&T, I_OCTO)) { OF_FAIL("F5: PUT BACK was refused"); return; }
    ticks(3);
    const float w1 = octopus_find_wait();
    printf("octo-find F5: shelved at %.1f s, a load on the shelf, put back: %.1f s\n", (double)w0, (double)w1);
    if (fabsf(w1 - w0) > 2.0f) OF_FAIL("F5: its wait %.1f s before the shelf, %.1f s after PUT BACK (a load between)", (double)w0, (double)w1);
    tank_octopus_release(&T);
}
static void case_f6(void) {
    for (int den = 0; den < 2; den++) {
        const int item = den ? 2 : 9, other = den ? 9 : 2;
        fresh(61, I_CASTLE); progression_buy(&T, I_VOLCANO); progression_buy(&T, I_OCTO); ticks(20);
        tank_decor_set(&T, 2, 300, DECOR_Z_FRONT); tank_decor_set(&T, 9, 318, DECOR_Z_BACK);   /* 18 px apart */
        octopus_set_fav(item); octopus_think(0); ticks(4);
        octopus_cmd(3, 0);
        for (int i = 0; i < 6000 && octopus_inn() < 0.99f; i++) ticks(1);
        if (octopus_inn() < 0.99f) { OF_FAIL("F6: it never got into the %s (inn %.2f, den %d)", den ? "castle" : "volcano", (double)octopus_inn(), octopus_den_item()); tank_octopus_release(&T); continue; }
        const int in_it = octopus_in_piece(&T, item), in_other = octopus_in_piece(&T, other);
        printf("octo-find F6: castle at 300, volcano at 318, the octopus in its den the %s (x %.0f): in that piece %d, in the other %d\n", den ? "castle" : "volcano", (double)octopus_x(), in_it, in_other);
        if (!in_it || in_other) OF_FAIL("F6: in the %s, two shelters 18 px apart: in it %d, in the %s %d (it is in one piece, its den)", den ? "castle" : "volcano", in_it, den ? "volcano" : "castle", in_other);
        tank_octopus_release(&T);
    }
}
/* F7: the board's boot order (main.c: the shelf, the tank, a swap's carry), as selftest-pw stages it */
int64_t clock_port_now_unix(void);
static char s_f7dir[700];
static void f7_boot(uint32_t seed) {
    if (setup_active()) setup_cancel(&T);
    shelf_boot(clock_port_now_unix());
    memset(&T, 0, sizeof T); tank_init(&T, seed);
    progression_set_jelly(false); progression_boot(&T); progression_shelf_settle(&T); progression_swap_carry(&T);
    T.light_override = true; T.light_on = true;
}
static bool f7_put_up(int i) {                        /* SWAP IN: the save, the carry, the swap (then the boot) */
    static uint16_t thumb[200 * 200]; int w, h; render_shelf_thumb_size(&w, &h);
    if (!progression_save(&T)) return false;
    progression_carry_t c; progression_carry_take(&T, &c);
    return shelf_swap(i, thumb, w, h, &c, sizeof c, clock_port_now_unix());
}
static void f7_env_put(const char *k, const char *v) { if (v) setenv(k, v, 1); else unsetenv(k); }
static void case_f7(void) {
    char p[800], cmd[1700], old_st[800] = "", old_fr[800] = "";
    const bool had_st = getenv("POCKET_TANK_STORAGE") != NULL, had_fr = getenv("POCKET_TANK_FRAME") != NULL;
    if (had_st) snprintf(old_st, sizeof old_st, "%s", getenv("POCKET_TANK_STORAGE"));
    if (had_fr) snprintf(old_fr, sizeof old_fr, "%s", getenv("POCKET_TANK_FRAME"));
    snprintf(s_f7dir, sizeof s_f7dir, "%s/octofind-f7-%dx%d", sim_shelf_dir(), TANK_W, TANK_H);
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", s_f7dir, s_f7dir); (void)system(cmd);
    snprintf(p, sizeof p, "%s/tank.sav", s_f7dir); setenv("POCKET_TANK_SAVE", p, 1); snprintf(s_sav, sizeof s_sav, "%s", p);
    snprintf(p, sizeof p, "%s/storage.bin", s_f7dir); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", s_f7dir); setenv("POCKET_TANK_FRAME", p, 1);
    f7_boot(71); progression_setup_done(&T); T.sd_balance = 100000; progression_buy(&T, I_OCTO); ticks(40);
    T.oc_found = T.oc_found_paid = 2;                 /* (found before: a wait not kept would be a fresh 2.5-3.5 h) */
    octopus_set_find_wait(2000.0f); ticks(1);
    const float w0 = octopus_find_wait(); const uint32_t idA = shelf_now()->id;
    const int i = shelf_new(0, clock_port_now_unix());
    if (i < 0 || !f7_put_up(i)) { OF_FAIL("F7: staging: no new tank to swap in"); goto out; }
    f7_boot(72); progression_setup_done(&T); ticks(40);
    int k = -1; for (int q = 0; q < shelf_count(); q++) if (shelf_get(q)->id == idA) k = q;
    if (k < 0 || !f7_put_up(k)) { OF_FAIL("F7: the swap back was refused"); goto out; }
    f7_boot(73); ticks(2);
    const float w1 = octopus_find_wait();
    printf("octo-find F7: its wait %.0f s; its tank swapped out (MY TANKS), a new tank in, then it swapped back in: %.0f s (found %u)\n", (double)w0, (double)w1, T.oc_found);
    if (!tank_crit_in(&T, SD_ITEM_OCTOPUS) || w1 > w0 + 1.0f || w1 < w0 - 600.0f) OF_FAIL("F7: its wait %.0f s before the swap out, %.0f s after the swap back in (kept, less the time lived)", (double)w0, (double)w1);
out:
    tank_octopus_release(&T);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", s_f7dir); (void)system(cmd);
    f7_env_put("POCKET_TANK_STORAGE", had_st ? old_st : NULL); f7_env_put("POCKET_TANK_FRAME", had_fr ? old_fr : NULL);
}
int selftest_octo_find(void) {
    case_f7(); case_f6(); case_f2(); case_f3(); case_f4(); case_f5(); case_f1();
    remove(s_sav);
    printf("selftest-octo-find: %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
