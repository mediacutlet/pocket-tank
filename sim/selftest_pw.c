#define _DEFAULT_SOURCE
/* selftest_pw.c - --selftest-pw (0.4.3 audit round 1, reviewer PW: power, time and OTA). NOT a release test yet: a harness.
 *   A  THE OCTOPUS'S NEXT FIND across boots: a tank slept often (each sleep past the grace is a deep sleep = a boot on the
 *      boards) against one never slept; the crab beside it as the control (its wait is saved: cr_find_wait 1936)
 *   B  a coin held up at a deep sleep: the crab's is due again at the wake (its rule), the octopus's?
 *   C  THE PARAGON's KEEP, then the power cut before the next save (progression_evo_keep only marks dirty)
 *   D  THE FISH LAB: the unlock and a pour, the power cut at every write: paid and the fish, or neither
 *   E  THE OCTOPUS bought / THE CASTLE upgraded: the power cut at every write
 *   F  the sparkle hour and the roll across a night (sleep counts at a quarter): ready at the first lit tick
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tank.h"
#include "advisor.h"
#include "progression.h"
#include "render.h"
#include "octopus.h"
#include "shelf.h"
#include "notice.h"
#include "setup.h"
#include "swap.h"
#include "link.h"
#include "tank_link.h"
#include "passport.h"

const char *sim_shelf_dir(void);
goal_t advisor_rules(const tank_t *t, int fish_idx, bool request);
void sim_clock_shift(int64_t s);
int shelf_port_sim_cut(int writes);
int64_t clock_port_now_unix(void);

static tank_t T;
static char s_dir[700];
static int s_boots, fails;
#define PW_FAIL(...) do { printf("FAIL: pw: "); printf(__VA_ARGS__); printf("\n"); fails++; } while (0)
enum { I_CASTLE = 2, I_CRAB = 10, I_OCTO = 11 };

static void board(const char *name) {
    char p[800], cmd[1700];
    snprintf(s_dir, sizeof s_dir, "%s/pw-%s-%dx%d", sim_shelf_dir(), name, TANK_W, TANK_H);
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", s_dir, s_dir); (void)system(cmd);
    snprintf(p, sizeof p, "%s/tank.sav", s_dir); setenv("POCKET_TANK_SAVE", p, 1);
    snprintf(p, sizeof p, "%s/storage.bin", s_dir); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", s_dir); setenv("POCKET_TANK_FRAME", p, 1);
}
static void boot(void) {                              /* main.c's order: the shelf, the tank, a swap's carry, the cards */
    if (setup_active()) setup_cancel(&T);
    shelf_boot(clock_port_now_unix());
    memset(&T, 0, sizeof T);
    tank_init(&T, 5 + (getenv("PW_SEED") ? (uint32_t)atoi(getenv("PW_SEED")) * 7919u : 0) + 104729u * ++s_boots);
    progression_set_jelly(false);
    progression_boot(&T);
    progression_shelf_settle(&T);
    progression_swap_carry(&T);
    notice_sync(&T);
}
/* awake for `secs`, the keeper about (handled), the light on; every coin taken the moment it is up. dt 0.1 s */
static void awake(float secs, int *oc_coins, int *cr_coins) {
    const float dt = 0.1f;
    for (float s = 0; s < secs; s += dt) {
        progression_tick(&T, dt);
        tank_tick(&T, dt, advisor_rules); tank_handled(&T);
        if (octopus_find_left() > 0 && tank_octopus_collect(&T)) (*oc_coins)++;
        if (T.cr_find > 0) { tank_crab_tap(&T, T.cr_x); (*cr_coins)++; }
    }
}
static void deep_sleep(int64_t secs) { progression_save(&T); sim_clock_shift(secs); boot(); }   /* the save, RAM gone, the boot lives the span */

static void case_a(void) {
    int pat[2][2] = { { 0, 0 }, { 0, 0 } };
    for (int p = 0; p < 2; p++) {
        board(p ? "a-often" : "a-never");
        boot(); progression_setup_done(&T); T.sd_balance = 100000;
        progression_buy(&T, I_CASTLE); progression_buy(&T, I_CRAB); progression_buy(&T, I_OCTO);
        T.light_override = true; T.light_on = true;
        for (int i = 0; i < T.n_fish; i++) T.fish[i].hunger = 2;
        int oc = 0, cr = 0;
        awake(2 * 3600.0f, &oc, &cr);                  /* the first finds (45..90 min) */
        int oc0 = oc, cr0 = cr;
        /* 24 h by the wall clock: never slept; or 40 min awake, 40 min asleep (18 times) */
        if (!p) awake(24 * 3600.0f, &oc, &cr);
        else for (int k = 0; k < 18; k++) { awake(40 * 60.0f, &oc, &cr); deep_sleep(40 * 60); T.light_override = true; T.light_on = true; for (int i = 0; i < T.n_fish; i++) T.fish[i].hunger = 2; }
        pat[p][0] = oc - oc0; pat[p][1] = cr - cr0;
        printf("pw A: %s: in the 24 h after the first finds - octopus %d coins, crab %d (finds so far %u / %u, paid %u / %u)\n",
               p ? "40 min awake / 40 min asleep" : "never slept", oc - oc0, cr - cr0, T.oc_found, T.cr_found, T.oc_found_paid, T.cr_found_paid);
    }
    if (pat[1][0] * 2 < pat[0][0] && pat[1][1] * 2 >= pat[0][1])
        PW_FAIL("A: the octopus slept often found %d in 24 h (fewer than half) (never slept: %d); the crab slept often %d (never slept %d) - its next find's wait is not saved, every boot starts a fresh one", pat[1][0], pat[0][0], pat[1][1], pat[0][1]);
}
static void case_b(void) {
    board("b");
    boot(); progression_setup_done(&T); T.sd_balance = 100000;
    progression_buy(&T, I_CASTLE); progression_buy(&T, I_CRAB); progression_buy(&T, I_OCTO);
    T.light_override = true; T.light_on = true;
    for (int i = 0; i < T.n_fish; i++) T.fish[i].hunger = 2;
    const float dt = 0.1f; int got = 0;
    for (float s = 0; s < 6 * 3600.0f; s += dt) {   /* until each holds a coin up at the same time... or one, then the other */
        progression_tick(&T, dt); tank_tick(&T, dt, advisor_rules); tank_handled(&T);
        if (octopus_find_left() > 0) got |= 1;
        if (T.cr_find > 0) got |= 2;
        if ((got & 1) && !(octopus_find_left() > 0)) got &= ~1;
        if ((got & 2) && !(T.cr_find > 0)) got &= ~2;
        if (got == 1 && s > 5 * 3600.0f) break;
        if (got & 1) break;
    }
    const int oc_up = octopus_find_left() > 0, cr_up = T.cr_find > 0;
    printf("pw B: (octopus in %d live %d, crab in %d, found %u/%u, clock %.0f)\n", tank_crit_in(&T, SD_ITEM_OCTOPUS), tank_octopus_live(&T), tank_crit_in(&T, SD_ITEM_CRAB), T.oc_found, T.cr_found, T.clock);
    deep_sleep(60);                                     /* a minute's deep sleep (past the grace): a boot */
    T.light_override = true; T.light_on = true;
    tank_tick(&T, 0.1f, advisor_rules);
    const float ow = octopus_find_wait();
    printf("pw B: a coin up at the sleep (octopus %d, crab %d): after a minute's deep sleep the octopus's next find is %.0f s off, the crab's %.0f s\n",
           oc_up, cr_up, (double)ow, (double)T.cr_find_cool);
    if (oc_up && ow > 600) PW_FAIL("B: the octopus held a coin up at the sleep; after the boot its next find is %.0f s off (the crab's rule: due again at once)", (double)ow);
}
static void case_c(void) {
    board("c");
    boot(); progression_setup_done(&T);
    T.light_override = true; T.light_on = true;
    for (int i = 0; i < T.n_fish; i++) T.fish[i].hunger = 2;
    progression_set_age(&T, 0, EVO_AGE + 5);
    progression_tick(&T, 1.0f); tank_tick(&T, 0.04f, advisor_rules);
    if (!tank_evo_ready(&T.fish[0])) { PW_FAIL("C: no ready fish to test (stage %d)", T.fish[0].stage); return; }
    notice_sync(&T);                                    /* (the elder card and the READY card said: adopted, as a keeper who saw them) */
    for (int k = 0; k < 2000; k++) { progression_tick(&T, 0.04f); tank_tick(&T, 0.04f, advisor_rules); notice_tick(&T, 0.04f, false); }
    progression_save(&T);                               /* the three on the flash (the heartbeat) */
    const uint32_t uid = T.fish[0].uid;
    for (int k = 0; k < 200; k++) { progression_tick(&T, 0.04f); tank_tick(&T, 0.04f, advisor_rules); }   /* 8 s: nothing saves */
    extern int g_sim_tank_saves; const int sv0 = g_sim_tank_saves;
    if (!progression_evo_keep(&T, 0, 1)) { PW_FAIL("C: KEEP refused"); return; }
    const int part = T.fish[0].evo;
    printf("pw C: ready before KEEP was on the flash; saves from the pre-KEEP save to KEEP: %d\n", g_sim_tank_saves - sv0);
    for (int k = 0; k < 250; k++) { int s0 = g_sim_tank_saves; progression_tick(&T, 0.04f); if (g_sim_tank_saves != s0) printf("pw C: a save at tick %d after KEEP (notice up %d)\n", k, notice_current() ? notice_current()->kind : -1); tank_tick(&T, 0.04f, advisor_rules); notice_tick(&T, 0.04f, false); }   /* 10 s: IS NOW A PARAGON up */
    printf("pw C: saves between KEEP and the cut: %d\n", g_sim_tank_saves - sv0);
    sim_clock_shift(15); boot();                         /* the cable pulled 10 s after KEEP (no save since) */
    int i = -1; for (int j = 0; j < T.n_fish; j++) if (T.fish[j].uid == uid) i = j;
    printf("pw C: KEEP (part %d), 10 s later the power cut: after the boot the fish has part %d, ready again %d\n", part, i >= 0 ? T.fish[i].evo : -1, i >= 0 && tank_evo_ready(&T.fish[i]));
    if (i < 0 || T.fish[i].evo != part) PW_FAIL("C: the kept part (%d) is gone after a power cut 10 s after KEEP (fish has %d, offered the three again: %d) - KEEP is not saved at once as a rename, a sale, a shelve are",
                                                part, i >= 0 ? T.fish[i].evo : -1, i >= 0 && tank_evo_ready(&T.fish[i]));
}
static int fish_live(void) { return T.n_fish; }
static void case_d(void) {
    /* the unlock */
    int n_ok = 0, n_none = 0, n_bad = 0;
    for (int cut = 0; cut < 12; cut++) {
        board("d-buy"); boot(); progression_setup_done(&T); T.sd_balance = 2000; progression_save(&T);
        const int32_t b0 = T.sd_balance;
        shelf_port_sim_cut(cut);
        bool did = progression_lab_buy(&T);
        const int left = shelf_port_sim_cut(-1);
        boot();
        const bool owned = progression_lab_owned(&T);
        if (owned && T.sd_balance == b0 - SD_PRICE_LAB) n_ok++; else if (!owned && T.sd_balance == b0) n_none++; else { n_bad++; PW_FAIL("D: LAB buy cut at write %d: owned %d, balance %d (was %d)", cut, owned, (int)T.sd_balance, (int)b0); }
        if (left > 0 && did) break;
    }
    printf("pw D: LAB unlock cut at every write: %d whole, %d nothing, %d split\n", n_ok, n_none, n_bad);
    n_ok = n_none = n_bad = 0;
    for (int cut = 0; cut < 12; cut++) {
        board("d-pour"); boot(); progression_setup_done(&T); T.sd_balance = 3000; progression_lab_buy(&T); progression_save(&T);
        const int32_t b0 = T.sd_balance; const int n0 = fish_live();
        fish_t r = T.fish[0]; r.name[0] = 0; memcpy(r.name, "LABBY", 6); r.body[0] = 200; r.body[1] = 90;
        shelf_port_sim_cut(cut);
        int slot = progression_lab_pour(&T, &r);
        const int left = shelf_port_sim_cut(-1);
        boot();
        int lab = 0; for (int i = 0; i < T.n_fish; i++) lab += progression_from_lab(&T, i);
        if (fish_live() == n0 + 1 && lab == 1 && T.sd_balance == b0 - SD_PRICE_LAB_FRY) n_ok++;
        else if (fish_live() == n0 && !lab && T.sd_balance == b0) n_none++;
        else { n_bad++; PW_FAIL("D: pour cut at write %d: fish %d (was %d), lab fish %d, balance %d (was %d)", cut, fish_live(), n0, lab, (int)T.sd_balance, (int)b0); }
        if (left > 0 && slot >= 0) break;
    }
    printf("pw D: LAB pour cut at every write: %d whole, %d nothing, %d split\n", n_ok, n_none, n_bad);
}
static void case_e(void) {
    int n_ok = 0, n_none = 0, n_bad = 0;
    for (int cut = 0; cut < 12; cut++) {
        board("e-oc"); boot(); progression_setup_done(&T); T.sd_balance = 1000; progression_save(&T);
        const int32_t b0 = T.sd_balance;
        shelf_port_sim_cut(cut);
        bool did = progression_buy(&T, I_OCTO);
        const int left = shelf_port_sim_cut(-1);
        boot();
        const bool own = (T.sd_unlocks & SD_ITEM_OCTOPUS) != 0;
        if (own && T.sd_balance == b0 - SD_PRICE_OCTOPUS) n_ok++; else if (!own && T.sd_balance == b0) n_none++;
        else { n_bad++; PW_FAIL("E: octopus buy cut at write %d: owned %d balance %d (was %d)", cut, own, (int)T.sd_balance, (int)b0); }
        if (left > 0 && did) break;
    }
    printf("pw E: octopus buy cut at every write: %d whole, %d nothing, %d split\n", n_ok, n_none, n_bad);
    n_ok = n_none = n_bad = 0;
    for (int cut = 0; cut < 12; cut++) {
        board("e-castle"); boot(); progression_setup_done(&T); T.sd_balance = 1000; progression_buy(&T, I_CASTLE); progression_save(&T);
        const int32_t b0 = T.sd_balance; const int st0 = T.castle_stage;
        shelf_port_sim_cut(cut);
        bool did = progression_buy(&T, I_CASTLE);
        const int left = shelf_port_sim_cut(-1);
        boot();
        if (T.castle_stage == st0 + 1 && T.sd_balance == b0 - SD_PRICE_CASTLE_2) n_ok++; else if (T.castle_stage == st0 && T.sd_balance == b0) n_none++;
        else { n_bad++; PW_FAIL("E: castle upgrade cut at write %d: stage %d (was %d) balance %d (was %d)", cut, T.castle_stage, st0, (int)T.sd_balance, (int)b0); }
        if (left > 0 && did) break;
    }
    printf("pw E: castle upgrade cut at every write: %d whole, %d nothing, %d split\n", n_ok, n_none, n_bad);
}
static void case_f(void) {
    board("f");
    boot(); progression_setup_done(&T);
    T.light_override = true; T.light_on = true;
    for (int i = 0; i < T.n_fish; i++) T.fish[i].hunger = 2;
    progression_set_age(&T, 0, EVO_AGE - 1800);          /* half an hour before: sparkling */
    progression_tick(&T, 1.0f); tank_tick(&T, 0.04f, advisor_rules);
    const int sp0 = progression_evo_sparkle(&T, 0);
    const uint32_t uid = T.fish[0].uid;
    deep_sleep(8 * 3600);                                /* a night: 2 h of age at a quarter */
    T.light_override = true; T.light_on = true;
    int i = -1; for (int j = 0; j < T.n_fish; j++) if (T.fish[j].uid == uid) i = j;
    if (i < 0) { PW_FAIL("F: the fish is gone"); return; }
    const float hung = T.fish[i].hunger;
    const int ready_dark = tank_evo_ready(&T.fish[i]);
    T.fish[i].hunger = 2;
    progression_tick(&T, 0.04f); tank_tick(&T, 0.04f, advisor_rules);
    printf("pw F: sparkling %d before a night; at the morning boot ready %d (hunger %.1f), after one lit fed tick ready %d, sparkle %d\n",
           sp0, ready_dark, (double)hung, tank_evo_ready(&T.fish[i]), progression_evo_sparkle(&T, i));
    if (!sp0 || !tank_evo_ready(&T.fish[i])) PW_FAIL("F: the night did not bring it to its roll (sparkle %d, ready %d)", sp0, tank_evo_ready(&T.fish[i]));
}

/* ===================== audit r2 (0.4.3), reviewer PW: the fixes' new writes, the swap's awake-only day ===================== */
static swap_book_t PB;                                /* this tank's swap book, on the "flash" (its own blob file beside the save) */
#define PEER 0xB0B0B0B1u
static int slot_of(uint32_t u) { for (int i = 0; i < T.n_fish; i++) if (T.fish[i].uid == u) return i; return -1; }
static int n_notes(void) { int n = 0; for (int k = 0; k < SWAP_NOTES; k++) n += PB.notes[k].st != SWN_NONE; return n; }
static swap_note_t *note1(void) { for (int k = 0; k < SWAP_NOTES; k++) if (PB.notes[k].st) return &PB.notes[k]; return NULL; }
static void attach_book(void) { memset(&PB, 0, sizeof PB); PB.persist = true; PB.peer_edge = -1; swap_attach(&T, &PB); }
static passport_t foreign(uint32_t uid, const char *name) {      /* a fish of the tank next door: one of ours, re-badged */
    passport_t p; passport_from_fish(&p, &T.fish[1], PEER, progression_age_s(&T, 1));
    p.uid = uid; p.home_tank = PEER; snprintf(p.name, sizeof p.name, "%s", name);
    return p;
}
static void rx(int kind, uint32_t sid, uint32_t uid, const passport_t *pp) {
    link_ev_t e; memset(&e, 0, sizeof e);
    e.type = LEV_SWAP; e.peer = PEER; e.reason = (uint8_t)kind; e.nonce = sid; e.uid = uid;
    if (pp) { e.pp = *pp; e.ms_left = 1; }
    tank_link_event(&T, NULL, &e);
}
static void fresh_tank(const char *name) {
    board(name); attach_book(); boot(); progression_setup_done(&T);
    while (T.n_fish < 5) { tank_make_fish(&T, T.n_fish, T.n_fish % 4, 0.5f, 0.5f, STAGE_ADULT); T.n_fish++; }
    tank_ensure_ids(&T);
    for (int i = 0; i < T.n_fish; i++) { progression_set_age(&T, i, STAGE_ADULT_AGE + 600); T.fish[i].stage = STAGE_ADULT; T.fish[i].ms_bits |= MS_REACHED_JUV | MS_REACHED_ADULT; }
    T.sd_balance = 777; progression_save(&T);
}
/* the I side's hard-case note, as swap_tick's COMMIT writes it: the note (COMMITTED) on the flash, a out (its save), applied */
static uint32_t stage_commit(int ia, uint32_t b_uid, int cut, int *left) {
    passport_t a; passport_from_fish(&a, &T.fish[ia], T.uid, progression_age_s(&T, ia));
    swap_note_t m; memset(&m, 0, sizeof m);
    m.st = SWN_COMMITTED; m.role = SWAP_ROLE_I; m.sid = 4242; m.peer = PEER; m.tank = T.uid; m.out = a; m.in = foreign(b_uid, "theirs");
    if (cut >= 0) shelf_port_sim_cut(cut);
    PB.notes[0] = m;
    if (!swap_save(&PB)) memset(&PB.notes[0], 0, sizeof PB.notes[0]);
    else { progression_swap_out(&T, ia); if (!progression_dirty()) { PB.notes[0].applied = 1; swap_save(&PB); } }
    if (cut >= 0) *left = shelf_port_sim_cut(-1);
    return a.uid;
}
typedef struct { const char *name; float awake_min; int sessions; } pat_t;
/* G: the hard case's day (swap.c:313-319): awake minutes only, the count saved every 10 - how many days a's place is held */
static void case_g(void) {
    static const pat_t P[] = { { "awake all day", 1440, 1 }, { "awake 8 h a day, one sitting", 480, 1 }, { "pendant: 1 h a day, one sitting", 60, 1 },
                               { "4 x 15 min a day", 15, 4 }, { "6 x 9 min a day", 9, 6 }, { "12 x 5 min a day", 5, 12 } };
    for (unsigned p = 0; p < sizeof P / sizeof P[0]; p++) {
        fresh_tank("g");
        const uint32_t b = 0x77770001u; int left;
        const uint32_t a = stage_commit(0, b, -1, &left);
        if (!note1() || !note1()->applied || slot_of(a) >= 0) { PW_FAIL("G: staging (note %d)", n_notes()); continue; }
        double awake_s = 0; int day = 0, home_day = -1;
        for (day = 1; day <= 60 && home_day < 0; day++) {
            for (int s = 0; s < P[p].sessions && home_day < 0; s++) {
                const double secs = P[p].awake_min * 60.0;
                for (double x = 0; x < secs; x += 0.25) { T.clock += 0.25; tank_link_tick(&T, NULL); if (slot_of(a) >= 0) { home_day = day; break; } }
                awake_s += secs;
                if (home_day < 0 && P[p].sessions * P[p].awake_min < 1440) deep_sleep((int64_t)((1440 - P[p].sessions * P[p].awake_min) * 60 / P[p].sessions));   /* (past the grace: a boot) */
            }
        }
        const swap_note_t *n = note1();
        printf("pw G: %-34s a home on day %d (%.1f h awake in all); note left %d, its count %u min, a's place held %d days\n", P[p].name, home_day,
               awake_s / 3600.0, n_notes(), n ? n->wait_s / 60 : 0, home_day < 0 ? 60 : home_day);
        if (home_day < 0) PW_FAIL("G: %s: a never came home in 60 days (count %u of %d min) - decision 1's 'after a day apart' never comes", P[p].name, n ? n->wait_s / 60 : 0, SWAP_WAIT_S / 60);
    }
}
/* the outcome of one cut, on this tank alone: whole one way or the other, or split */
static int fish_count(uint32_t u) { int c = 0; for (int i = 0; i < T.n_fish; i++) c += T.fish[i].uid == u; return c; }
static const char *H_KIND[4] = { "R applies a COMMIT (DONE, b out, a in)", "I commits (COMMITTED, a out)", "I receives on DONE (RECEIVING, b in, the note goes)", "I brings a back on ABORT (BACK, a in, the note goes)" };
static int h_judge(int kind, uint32_t a, uint32_t b, char *why, size_t wn) {   /* 1 whole-yes, 0 whole-no, -1 split */
    const swap_note_t *n = note1(); const int st = n ? n->st : 0, ap = n ? n->applied : 0;
    const int ca = fish_count(a), cb = fish_count(b), held = progression_fish_held(&T);
    snprintf(why, wn, "note %d applied %d, a here %d, b here %d, fish %d, held %d, balance %d", st, ap, ca, cb, T.n_fish, held, (int)T.sd_balance);
    if (ca > 1 || cb > 1 || held > POP_CAP || T.sd_balance != 777) return -1;
    switch (kind) {
    case 0: if (st == SWN_DONE && ap && !cb && ca && T.n_fish == 5) return 1; if (!st && cb && !ca && T.n_fish == 5) return 0; return -1;
    case 1: if (st == SWN_COMMITTED && ap && !ca && T.n_fish == 4 && held == 5) return 1; if (!st && ca && T.n_fish == 5) return 0; return -1;
    case 2: if (!st && cb && !ca && T.n_fish == 5) return 1; if (st == SWN_COMMITTED && ap && !cb && !ca && T.n_fish == 4 && held == 5) return 0; return -1;
    case 3: if (!st && ca && !cb && T.n_fish == 5) return 1; if (st == SWN_COMMITTED && ap && !cb && !ca && T.n_fish == 4 && held == 5) return 0; return -1;
    }
    return -1;
}
/* H: the power cut at every write of each note step (and then at every write of the boot that repairs it) */
static void case_h(void) {
    for (int kind = 0; kind < 4; kind++) {
        int whole = 0, none = 0, split = 0, runs = 0;
        for (int cut = 0; cut < 20; cut++) {
            int boot_writes = -1, step_left = 0;
            for (int bc = -1; bc < 12; bc++) {
                fresh_tank("h");
                const uint32_t bu = 0x77770002u; uint32_t a = 0, b = 0; int left = 0;
                if (kind == 0) {                                  /* R: its b = fish 2; their a comes in */
                    b = T.fish[2].uid; a = 0x77770003u;
                    passport_t ap = foreign(a, "visitor"), bp; passport_from_fish(&bp, &T.fish[2], T.uid, progression_age_s(&T, 2));
                    PB.in.st = SI_GOING; PB.in.sid = 99; PB.in.from = PEER; PB.in.a = ap; PB.in.b = bp; PB.in.t0 = 0;
                    shelf_port_sim_cut(cut); rx(SWK_COMMIT, 99, b, &ap); left = shelf_port_sim_cut(-1);
                } else if (kind == 1) {
                    b = bu; a = stage_commit(0, b, cut, &left);
                } else {
                    b = bu; a = stage_commit(0, b, -1, &left);
                    if (!note1() || !note1()->applied) { PW_FAIL("H: staging %d", kind); break; }
                    passport_t bp = note1()->in;
                    shelf_port_sim_cut(cut); rx(kind == 2 ? SWK_DONE : SWK_ABORT, 4242, kind == 2 ? b : 0, kind == 2 ? &bp : NULL); left = shelf_port_sim_cut(-1);
                }
                /* the power cut here: RAM gone; the boot repairs what the note says (itself cut at its write bc, then a clean boot) */
                step_left = left;
                if (bc >= 0) { shelf_port_sim_cut(bc); boot(); boot_writes = shelf_port_sim_cut(-1); }
                boot();
                char why[200]; const int r = h_judge(kind, a, b, why, sizeof why); runs++;
                if (r == 1) whole++; else if (r == 0) none++;
                else { split++; PW_FAIL("H: %s, cut at write %d, boot cut at %d: split - %s", H_KIND[kind], cut, bc, why); }
                if (bc >= 0 && boot_writes > 0) break;           /* (the boot had fewer writes than bc: every boot cut seen) */
            }
            if (step_left > 0) break;                             /* (the step had fewer writes than cut: every cut seen) */
        }
        printf("pw H: %-52s cut at every write (x every boot write): %d runs - %d done, %d not done (whole), %d split\n", H_KIND[kind], runs, whole, none, split);
    }
}
/* K: KEEP (progression_evo_keep saves at once, SV-3): the cut at every write - kept, or READY with the same three */
static void case_k(void) {
    int ok = 0, again = 0, bad = 0;
    for (int cut = 0; cut < 6; cut++) {
        board("k"); attach_book(); boot(); progression_setup_done(&T);
        T.light_override = true; T.light_on = true;
        for (int i = 0; i < T.n_fish; i++) T.fish[i].hunger = 2;
        progression_set_age(&T, 0, EVO_AGE + 5);
        progression_tick(&T, 1.0f); tank_tick(&T, 0.04f, advisor_rules);
        if (!tank_evo_ready(&T.fish[0])) { PW_FAIL("K: no ready fish"); return; }
        progression_save(&T);
        const uint32_t u = T.fish[0].uid; const uint16_t off = T.fish[0].evo_offer & 0x7fff;
        shelf_port_sim_cut(cut);
        const bool did = progression_evo_keep(&T, 0, 1); const int part = T.fish[0].evo;
        const int left = shelf_port_sim_cut(-1);
        boot();
        const int i = slot_of(u);
        if (i >= 0 && T.fish[i].evo == part && part) ok++;
        else if (i >= 0 && !T.fish[i].evo && tank_evo_ready(&T.fish[i]) && (T.fish[i].evo_offer & 0x7fff) == off) again++;
        else { bad++; PW_FAIL("K: KEEP cut at write %d: fish %d evo %d ready %d offer %04x (was %04x)", cut, i, i >= 0 ? T.fish[i].evo : -1, i >= 0 && tank_evo_ready(&T.fish[i]), i >= 0 ? T.fish[i].evo_offer : 0, off); }
        if (left > 0 && did) break;
    }
    printf("pw K: KEEP cut at every write: %d kept, %d READY again with the same three, %d split\n", ok, again, bad);
}
/* V: the first boot after an OTA from an early 0.4.3 dev build: a version 1 blob on the flash (its bytes 18-19 another count) */
static void case_v(void) {
    for (int role = 0; role < 2; role++) {
        fresh_tank("v");
        uint32_t a, b; int left;
        if (!role) { b = 0x77770004u; a = stage_commit(0, b, -1, &left); note1()->wait_s = 0; }   /* I: COMMITTED, applied */
        else {                                                    /* R: DONE on the flash, not applied (the cut came before b went) */
            b = T.fish[2].uid; a = 0x77770005u;
            swap_note_t m; memset(&m, 0, sizeof m); m.st = SWN_DONE; m.role = SWAP_ROLE_R; m.sid = 7; m.peer = PEER; m.tank = T.uid;
            passport_from_fish(&m.out, &T.fish[2], T.uid, progression_age_s(&T, 2)); m.in = foreign(a, "inbound");
            PB.notes[0] = m; swap_save(&PB);
        }
        char p[900]; snprintf(p, sizeof p, "%s/tank.sav.%s", s_dir, SWAP_NVS_KEY);
        FILE *f = fopen(p, "r+b"); uint8_t buf[1300]; size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
        if (!f || n < 4 + 278 + 5) { PW_FAIL("V: no blob (%zu)", n); if (f) fclose(f); continue; }
        memmove(buf + 4 + 20, buf + 4 + 24, n - (4 + 24)); n -= 4;       /* (the note written as version 3: its head to version 1's 20 bytes) */
        buf[2] = 1; buf[3] = 1; buf[4 + 18] = 0x34; buf[4 + 19] = 0x12;   /* version 1: flag bit 0, its held_min 0x1234 */
        fclose(f); f = fopen(p, "wb"); if (f) { fwrite(buf, 1, n, f); fclose(f); }
        boot();
        const swap_note_t *m = note1();
        char why[200]; const int r = h_judge(role ? 0 : 1, a, b, why, sizeof why);
        printf("pw V: a version 1 blob, %s note: after the boot %s (wait %u)\n", role ? "R DONE (not applied)" : "I COMMITTED", why, m ? m->wait_s : 0);
        if (role && r != 1) PW_FAIL("V: the v1 R note was not finished at the boot: %s", why);
        if (!role && (r != 1 || !m || m->wait_s != 0)) PW_FAIL("V: the v1 I note: %s (wait %u, want 0)", why, m ? m->wait_s : 0);
    }
}
int selftest_pw(void) {
    const char *only = getenv("PW_ONLY");
    if (!only || strchr(only, 'D')) case_d();
    if (!only || strchr(only, 'E')) case_e();
    if (!only || strchr(only, 'C')) case_c();
    if (!only || strchr(only, 'F')) case_f();
    if (!only || strchr(only, 'B')) case_b();
    if (!only || strchr(only, 'A')) case_a();
    if (!only || strchr(only, 'K')) case_k();             /* (audit r2's PW harness, adopted: K, V, H; G the gate for R2-2) */
    if (!only || strchr(only, 'V')) case_v();
    if (!only || strchr(only, 'H')) case_h();
    if (!only || strchr(only, 'G')) case_g();
    printf("selftest-pw: %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
