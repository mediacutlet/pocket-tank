#define _DEFAULT_SOURCE
/* selftest_breedwait.c - --selftest-breed-wait (0.4.2 audit round 2: SLP-1, and the plain part of SPS-1 / PR2-2).
 * SELECTIVE BREEDING: a set pair is undone only when one of its fish is no longer ours (sold, given, moved to the
 * bowl) or by UNPAIR. A pair fish NEXT DOOR = the pair and the staged fry WAIT, on every path to the birth:
 *   W1  a save that says AWAY, 8 h off, a boot (progression_boot -> progression_woke): no fry, the pair kept, nothing
 *       refunded, no toast, the fry still staged; the fish home from bubbles -> the PAIR's fry, the pair spent, one fry
 *   W2  a nap's wake (progression_slept + progression_woke, firmware main.c:601): the same
 *   W3  a forced birth (the director): the same; then UNPAIR: the 50 back and the usual pick breeds (never stuck)
 *   W4  awake, 60 s (what it always did): the same
 * NEW FRY's report (progression_next_fry's `staged`, the keeper page's ON ITS WAY):
 *   S1  no pair: staged as it was          S2  a pair that cannot court (a parent growing, set AFTER the staging; a parent
 *   next door): not staged, the gates and their count untouched; UNPAIR / the fish home: staged again
 *   S3  a pair home and adult: staged
 * Siblings that must not move:
 *   B1  no pair, a staged fry, a lit wake: born at the wake to the usual pick       B2  a pair home and adult at a lit
 *   wake: the pair's fry, one fry, the pair spent, no refund       B3  a pair fish SOLD / GIVEN / MOVED to the bowl:
 *   refunded at the next tick       B4  the light off at the wake: no birth, the pair kept       B5  the cap: a forced
 *   birth is called off, the pair kept, nothing refunded
 * NEW FRY's words (the row's modal, Strato 11-13: "WAITING FOR <NAME>"):
 *   N   a pair fish next door: WAITING FOR <NAME> / TO COME HOME / THEIR FRY COMES NEXT (W1-W4); both next door: AND <NAME>
 *       TO COME HOME; a parent growing: TO GROW UP (S2a); no pair, or the pair home: the old words; steps still owed: N OF M DONE
 * The cap (the blind review's open corner: at the cap a staged fry now stays staged while a pair fish is away):
 *   C1  a full tank, a pair fish next door, a lit wake: no fry, the pair and the 50 kept; home while still full: no fry,
 *       the pair, the 50 and the staged fry kept; a place made (a sale, whose own rule calls the staged fry off until
 *       SELL_FRY_MEALS more meals): the PAIR's fry, one fry, the pair spent, never refunded or charged twice      C2  the same with the place made while the fish is still next door, then home
 * With OWED CARDS (rc-0.4.2b, the nightcards fix beside this one):
 *   K   a pair waiting for a fish next door, its fry staged, a tank badge earned in the dark: slept, a dark boot, slept
 *       again, the lit boot - the pair, the 50, the staged fry and the owed card all kept in the one save (2200 B); the
 *       card said once at the light, the fry the pair's once the fish is home
 * Red before the fix (86b4b9c, and main 7934cb0): W1, W2, W3 (the pair undone, +50, a fry of two other fish), S2 (staged 1)
 * and N (the words). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "tank.h"
#include "advisor.h"
#include "progression.h"
#include "render.h"
#include "shelf.h"
#include "gift.h"
#include "notice.h"
#include "setup.h"
#include "link.h"
#include "tank_link.h"

const char *sim_shelf_dir(void);
goal_t advisor_rules(const tank_t *t, int fish_idx, bool request);
void sim_clock_shift(int64_t s);

static tank_t T;
static gift_book_t G;
static char s_dir[400];
static int bw_bad;
static uint32_t s_boots;
#define BW_FAIL(...) do { printf("breed-wait: FAIL: "); printf(__VA_ARGS__); printf("\n"); bw_bad++; } while (0)

static void board_env(void) {
    char p[600];
    snprintf(s_dir, sizeof s_dir, "%s/breed-wait-%dx%d", sim_shelf_dir(), TANK_W, TANK_H);
    snprintf(p, sizeof p, "%s/tank.sav", s_dir); setenv("POCKET_TANK_SAVE", p, 1);
    snprintf(p, sizeof p, "%s/storage.bin", s_dir); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", s_dir); setenv("POCKET_TANK_FRAME", p, 1);
    snprintf(p, sizeof p, "%s/wifi.txt", s_dir); setenv("POCKET_TANK_WIFI", p, 1);
    snprintf(p, sizeof p, "%s/code.txt", s_dir); setenv("POCKET_TANK_BOARD_CODE_FILE", p, 1);
    unsetenv("POCKET_TANK_BOARD_CODE");
}
static void board_new(void) { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", s_dir, s_dir); (void)system(cmd); }
static void boot(void) {
    if (setup_active()) setup_cancel(&T);
    shelf_boot(clock_port_now_unix());
    tank_init(&T, 5 + 7919u * shelf_now()->id + 104729u * ++s_boots);
    memset(&G, 0, sizeof G); G.persist = true; gift_attach(&T, &G);
    progression_set_jelly(false);
    progression_boot(&T);
    progression_swap_carry(&T);
    notice_sync(&T);
}
static void gates(void) {                             /* every care gate met, a nursery bed, the light on */
    for (int i = 0; i < T.n_fish; i++) { T.fish[i].trust = 9.5f; T.fish[i].hunger = 2; }
    if (T.player_feedings < 200) T.player_feedings = 200;   /* (a sale's SELL_FRY_MEALS on top: case C feeds them) */
    T.hold_approaches = 5;
    if (tank_nursery_bed(&T) < 0) tank_veg_set(&T, 0, 0.6f);   /* (real fronds: a boot recounts the beds from them) */
    T.night = false;
}
static void frame(float dt) {
    tank_tick(&T, dt, advisor_rules); progression_tick(&T, dt);
    notice_tick(&T, dt, false); if (notice_current()) notice_dismiss();
}
static void live(float s) { for (int i = 0; i < (int)(s * 25); i++) { gates(); frame(0.04f); } }
static int find(uint32_t uid) { for (int i = 0; i < T.n_fish; i++) if (T.fish[i].uid == uid) return i; return -1; }
static void welcome_all(void) { while (progression_newborn() >= 0) progression_newborn_done(&T); }
static void stage_tank(int n, int32_t sd) {           /* n adults, welcomed, every payout taken, the wallet set, the unlock owned */
    board_new(); s_boots = 0; boot(); progression_setup_done(&T);
    while (T.n_fish < n) tank_add_fish(&T, 0, 1);
    welcome_all();
    for (int i = 0; i < T.n_fish; i++) progression_set_age(&T, i, STAGE_ADULT_AGE + 900.0f + i * 50);
    tank_ensure_ids(&T);
    live(2.0f); welcome_all();
    T.sd_unlocks |= SD_ITEM_BREED;
    T.sd_balance = sd; (void)progression_sd_take_award();
    progression_save(&T); notice_sync(&T);
}
static bool fry_of(int slot, uint32_t pa, uint32_t pb) {
    if (slot < 0 || slot >= T.n_fish) return false;
    uint32_t p0 = T.fish[slot].parent_uid[0], p1 = T.fish[slot].parent_uid[1];
    return (p0 == pa && p1 == pb) || (p0 == pb && p1 == pa);
}
static bool staged_says(int *rows, int *met) {
    fry_req_t r[FRY_REQ_MAX]; bool st = false;
    int n = progression_next_fry(&T, r, &st);
    if (rows) *rows = n;
    if (met) { *met = 0; for (int i = 0; i < n; i++) *met += r[i].met; }
    return st;
}
/* NEW FRY's modal (the row's name tapped): its two caption lines and the line under them, then closed */
static char s_w0[80], s_w1[40], s_ws[40];
static bool newfry_words(void) {
    s_w0[0] = s_w1[0] = s_ws[0] = 0;
    render_milestones_leave();
    int x, y; render_milestones_row(T.n_fish, &x, &y);
    if (render_milestones_tap(&T, (float)x, (float)y) != MS_TAP_KEPT) return false;
    snprintf(s_w0, sizeof s_w0, "%s", render_milestones_caption(0)); snprintf(s_w1, sizeof s_w1, "%s", render_milestones_caption(1));
    snprintf(s_ws, sizeof s_ws, "%s", render_milestones_line());
    render_milestones_leave();
    return true;
}
static void words_want(const char *tag, const char *w0, const char *w1, const char *ws) {
    if (!newfry_words()) { BW_FAIL("%s: NEW FRY's name opened no modal", tag); return; }
    if (strcasecmp(s_w0, w0) || strcasecmp(s_w1, w1) || (ws && strcasecmp(s_ws, ws)))   /* (names are kept in small letters; the font has capitals only) */
        BW_FAIL("%s: NEW FRY says \"%s / %s / %s\", want \"%s / %s / %s\"", tag, s_w0, s_w1, s_ws, w0, w1, ws ? ws : "*");
}
static bool words_waiting(void) { return newfry_words() && strncmp(s_w0, "WAITING FOR ", 12) == 0; }
/* the refund, apart from anything a tick earned (stages, trust): the balance's change less the earned ledger's */
static int32_t s_b0, s_e0;
static void mark(void) { (void)progression_sd_take_award(); s_b0 = T.sd_balance; s_e0 = T.sd_earned; }
static int unearned(void) { return (int)((T.sd_balance - s_b0) - (T.sd_earned - s_e0)); }

/* ---- W: a pair fish next door at the birth, by four paths ---- */
static void case_w(int how) {
    static const char *const HOW[4] = { "W1 a boot", "W2 a nap's wake", "W3 a forced birth", "W4 awake 60 s" };
    const char *w = HOW[how];
    stage_tank(4, 2000);
    T.fish[2].trust = 1; T.fish[3].trust = 1;         /* (whatever the usual pick is, the pair below is 2 + 3 or it is not theirs) */
    if (!progression_breed_pair(&T, 2, 3)) { BW_FAIL("%s: could not pair", w); return; }
    const uint32_t pa = T.fish[2].uid, pb = T.fish[3].uid;
    tank_set_name(&T, 3, "BUBBLES");                  /* (a name as long as the wheel takes: FISH_NAME_MAX) */
    gates();
    T.fish[3].away = true;                            /* it swam next door (tank_link's link_cross sets exactly this; the save keeps it) */
    progression_stage_arrival(&T);
    const int n0 = T.n_fish;
    mark();
    if (how == 0) { progression_save(&T); sim_clock_shift(8 * 3600); boot(); }
    else if (how == 1) { progression_slept(&T, 30); progression_woke(&T); }
    else if (how == 2) progression_force_arrival(&T);
    else live(60.0f);
    int ib = find(pb);
    if (ib < 0 || (how != 0 && !T.fish[ib].away)) { BW_FAIL("%s: the pair fish is not next door any more (staging)", w); return; }
    if (T.n_fish != n0) BW_FAIL("%s: a fry was born with one of the pair next door (%d -> %d fish; its parents %08x + %08x, the pair %08x + %08x)", w, n0, T.n_fish,
                                (unsigned)T.fish[T.n_fish - 1].parent_uid[0], (unsigned)T.fish[T.n_fish - 1].parent_uid[1], (unsigned)pa, (unsigned)pb);
    if (T.breed_a != pa || T.breed_b != pb || T.breed_paid != SD_PRICE_PAIR) BW_FAIL("%s: the pair was undone (%08x + %08x paid %d)", w, (unsigned)T.breed_a, (unsigned)T.breed_b, T.breed_paid);
    if (unearned() != 0) BW_FAIL("%s: the wallet moved %+d with the pair only waiting", w, unearned());
    { int toast = progression_sd_take_award(); if (toast >= SD_PRICE_PAIR) BW_FAIL("%s: a +%d toast", w, toast); }
    if (!progression_arrival_pending()) BW_FAIL("%s: the staged fry was dropped", w);
    if (staged_says(NULL, NULL)) BW_FAIL("%s: NEW FRY says ON ITS WAY while a parent is next door", w);
    { char tag[48]; snprintf(tag, sizeof tag, "N (%s)", w); words_want(tag, "WAITING FOR BUBBLES", "TO COME HOME", "THEIR FRY COMES NEXT"); }
    welcome_all();
    if (how == 2) {                                   /* the keeper's way out: UNPAIR, the 50 back, the usual pick breeds */
        mark();
        if (!progression_breed_unpair(&T) || T.breed_a || unearned() != SD_PRICE_PAIR) BW_FAIL("%s: UNPAIR gave %+d back (want %d)", w, unearned(), SD_PRICE_PAIR);
        if (!staged_says(NULL, NULL)) BW_FAIL("%s: unpaired, the staged fry is not ON ITS WAY", w);
        words_want("N unpaired", "EVERY STEP IS DONE. A FRY", "WILL BE BORN IN THE GRASS", "ON ITS WAY");
        progression_force_arrival(&T);
        if (T.n_fish != n0 + 1) BW_FAIL("%s: unpaired, no fry from the usual pick (%d fish)", w, T.n_fish);
        else if (T.fish[n0].parent_uid[0] == pb || T.fish[n0].parent_uid[1] == pb) BW_FAIL("%s: the fish next door parented the fry", w);
        return;
    }
    /* home again (a boot brings it home from bubbles by itself, LINK_BOOT_RESTORE_S): the fry is the PAIR's */
    if (how != 0) T.fish[ib].away = false;
    else {
        if (T.link_restore_at <= 0) { BW_FAIL("%s: no homecoming set for the fish next door", w); return; }
        T.clock = T.link_restore_at; tank_link_tick(&T, NULL);
        if (T.fish[find(pb)].away) { BW_FAIL("%s: the fish did not come home from bubbles", w); return; }
    }
    if (words_waiting()) BW_FAIL("N (%s): the pair home and grown, NEW FRY still says %s", w, s_w0);
    mark();
    for (int k = 0; k < 25 * 120 && T.n_fish == n0; k++) { gates(); frame(0.04f); }
    if (T.n_fish != n0 + 1) { BW_FAIL("%s: the pair home, no fry within 2 min (%d fish, away %d, staged %d, courting %d)", w, T.n_fish, T.fish[find(pb)].away, progression_arrival_pending(), T.courting); return; }
    if (!fry_of(n0, pa, pb)) BW_FAIL("%s: the fry's parents %08x + %08x, not the pair %08x + %08x", w, (unsigned)T.fish[n0].parent_uid[0], (unsigned)T.fish[n0].parent_uid[1], (unsigned)pa, (unsigned)pb);
    if (T.breed_a || T.breed_b || T.breed_paid) BW_FAIL("%s: the pair not spent at its birth", w);
    if (unearned() != 0) BW_FAIL("%s: the pair's birth moved the wallet %+d beyond what was earned", w, unearned());
    welcome_all(); live(30.0f);
    if (T.n_fish != n0 + 1) BW_FAIL("%s: a second fry for one payment (%d fish)", w, T.n_fish);
}

/* ---- S: what NEW FRY reports ---- */
static void case_s(void) {
    stage_tank(3, 2000);
    gates(); progression_stage_arrival(&T);
    int rows0, met0, rows, met;
    if (!staged_says(&rows0, &met0) || rows0 < 1) BW_FAIL("S1: no pair, a staged fry: staged %d rows %d", staged_says(NULL, NULL), rows0);
    words_want("N no pair", "EVERY STEP IS DONE. A FRY", "WILL BE BORN IN THE GRASS", "ON ITS WAY");
    /* S2a: a pair with a parent still growing, made AFTER the fry was staged */
    progression_set_age(&T, 2, (float)STAGE_JUV_AGE + 60); T.fish[2].stage = STAGE_JUV;
    tank_set_name(&T, 2, "SPARKLE");
    (void)staged_says(&rows0, &met0);                 /* (the GROW gate may read the young one: the baseline is with it young) */
    if (!progression_breed_pair(&T, 0, 2)) { BW_FAIL("S2: could not pair"); return; }
    if (progression_breed_state(&T, NULL) != BREED_WAITING) BW_FAIL("S2: state %d, not WAITING", progression_breed_state(&T, NULL));
    if (!progression_arrival_pending()) BW_FAIL("S2: staging lost (the test's own)");
    if (staged_says(&rows, &met)) BW_FAIL("S2a: NEW FRY says ON ITS WAY while a parent of the pair is still growing");
    if (rows != rows0 || met != met0) BW_FAIL("S2a: the gates moved with the pair (%d rows %d met, were %d / %d)", rows, met, rows0, met0);
    { int n0 = T.n_fish; live(20.0f); if (T.n_fish != n0) BW_FAIL("S2a: a fry came while a parent was growing"); }
    if (staged_says(NULL, NULL)) BW_FAIL("S2a: 20 s on, ON ITS WAY");
    words_want("N growing", "WAITING FOR SPARKLE", "TO GROW UP", "THEIR FRY COMES NEXT");
    mark();
    progression_breed_unpair(&T);
    if (unearned() != SD_PRICE_PAIR) BW_FAIL("S2a: UNPAIR gave %+d", unearned());
    if (staged_says(NULL, NULL) != progression_arrival_pending()) BW_FAIL("S2a: unpaired, staged %d but pending %d", staged_says(NULL, NULL), progression_arrival_pending());
    /* S2b: a pair of adults, one next door */
    stage_tank(3, 2000); gates(); progression_stage_arrival(&T);
    progression_breed_pair(&T, 0, 2);
    if (!staged_says(&rows0, &met0)) BW_FAIL("S3: a pair home and adult, a staged fry: not staged");
    words_want("N a pair home", "EVERY STEP IS DONE. A FRY", "WILL BE BORN IN THE GRASS", "ON ITS WAY");
    tank_set_name(&T, 0, "MOONPIE"); tank_set_name(&T, 2, "BUBBLES");
    T.fish[2].away = true;
    if (staged_says(&rows, &met)) BW_FAIL("S2b: NEW FRY says ON ITS WAY while a parent of the pair is next door");
    words_want("N next door", "WAITING FOR BUBBLES", "TO COME HOME", "THEIR FRY COMES NEXT");
    T.fish[0].away = true;
    words_want("N both next door", "WAITING FOR MOONPIE", "AND BUBBLES TO COME HOME", "THEIR FRY COMES NEXT");
    T.fish[0].away = false;
    T.fish[2].away = false;
    if (!staged_says(NULL, NULL)) BW_FAIL("S2b: home again, not staged");
    /* nothing staged: never staged, pair or not */
    stage_tank(3, 2000); progression_breed_pair(&T, 0, 2);
    if (progression_arrival_pending() || staged_says(NULL, NULL)) BW_FAIL("S: nothing staged reads staged");
    {   T.player_feedings = 0;                        /* steps still owed (MEALS), a pair fish next door: the steps are the wait */
        int rows2, met2; (void)staged_says(&rows2, &met2);
        T.fish[2].away = true;
        if (met2 >= rows2) BW_FAIL("N steps owed: every step met (staging)");
        else { char m[24]; snprintf(m, sizeof m, "%d OF %d DONE", met2, rows2); words_want("N steps owed", "WHEN ALL NEEDS ARE MET, A", "NEW FRY IS READY TO BE BORN", m); }
        T.fish[2].away = false; }
}

/* ---- B: the siblings ---- */
static void case_b(void) {
    /* B1: no pair, a lit wake: the staged fry is born there, the usual pick */
    for (int how = 0; how < 2; how++) {
        stage_tank(4, 2000); gates(); progression_stage_arrival(&T);
        int n0 = T.n_fish; mark();
        if (how == 0) { progression_save(&T); sim_clock_shift(8 * 3600); boot(); } else { progression_slept(&T, 30); progression_woke(&T); }
        if (T.n_fish != n0 + 1) BW_FAIL("B1 (%s): no pair, the staged fry was not born at the lit wake (%d fish)", how ? "nap" : "boot", T.n_fish);
        if (unearned() != 0) BW_FAIL("B1: the wallet moved %+d unearned", unearned());
    }
    /* B2: a pair home and adult at a lit wake: theirs, once */
    for (int how = 0; how < 2; how++) {
        stage_tank(4, 2000); T.fish[2].trust = 1; T.fish[3].trust = 1;
        progression_breed_pair(&T, 2, 3);
        const uint32_t pa = T.fish[2].uid, pb = T.fish[3].uid;
        gates(); progression_stage_arrival(&T);
        int n0 = T.n_fish; mark();
        if (how == 0) { progression_save(&T); sim_clock_shift(8 * 3600); boot(); } else { progression_slept(&T, 30); progression_woke(&T); }
        if (T.n_fish != n0 + 1 || !fry_of(n0, pa, pb)) BW_FAIL("B2 (%s): the pair home at a lit wake: %d fish, the fry not theirs", how ? "nap" : "boot", T.n_fish);
        if (T.breed_a || T.breed_paid || unearned() != 0) BW_FAIL("B2: the pair not spent, or the wallet moved (%08x, %+d)", (unsigned)T.breed_a, unearned());
    }
    /* B3: a pair fish no longer ours: refunded at the next tick */
    for (int how = 0; how < 3; how++) {
        static const char *const HOW[3] = { "sold", "given", "moved to the bowl" };
        stage_tank(4, 2000);
        progression_breed_pair(&T, 0, 3);
        live(0.2f); mark();
        int32_t before = T.sd_balance; bool ok;
        if (how == 0) ok = progression_sell_fish(&T, 3);
        else if (how == 1) ok = progression_give_out(&T, 3);
        else ok = progression_move_out(&T, 3) == BOWL_OK;
        if (!ok) { BW_FAIL("B3: a pair fish could not be %s (staging)", HOW[how]); continue; }
        int price = (int)(T.sd_balance - before), earned0 = T.sd_earned;
        frame(0.04f);
        int back = (int)(T.sd_balance - before) - price - (T.sd_earned - earned0);
        if (T.breed_a || T.breed_b || back != SD_PRICE_PAIR) BW_FAIL("B3: a pair fish %s: pair %08x, %d back (want %d)", HOW[how], (unsigned)T.breed_a, back, SD_PRICE_PAIR);
    }
    /* B4: the light off at the wake: nothing is born, the pair stays */
    {   stage_tank(4, 2000); progression_breed_pair(&T, 2, 3);
        gates(); progression_stage_arrival(&T);
        int n0 = T.n_fish; mark();
        T.night = true;
        progression_slept(&T, 30); progression_woke(&T);
        if (T.n_fish != n0 || !T.breed_a || unearned() != 0 || !progression_arrival_pending()) BW_FAIL("B4: a dark wake: %d fish (were %d), pair %08x, wallet %+d, staged %d", T.n_fish, n0, (unsigned)T.breed_a, unearned(), progression_arrival_pending());
        T.night = false; }
    /* B5: the cap: a forced birth is called off, the pair kept, with the pair home and with one next door */
    for (int away = 0; away < 2; away++) {
        stage_tank(POP_CAP, 2000); progression_breed_pair(&T, 2, 3);
        if (progression_breed_state(&T, NULL) != BREED_FULL) BW_FAIL("B5: state %d at the cap, not FULL", progression_breed_state(&T, NULL));
        T.fish[3].away = away;
        gates(); mark(); int n0 = T.n_fish;
        progression_force_arrival(&T);
        if (T.n_fish != n0 || !T.breed_a || unearned() != 0) BW_FAIL("B5 (away %d): the cap: %d fish, pair %08x, wallet %+d", away, T.n_fish, (unsigned)T.breed_a, unearned());
        int rows; (void)staged_says(&rows, NULL);
        if (rows != 0) BW_FAIL("B5: NEW FRY has %d rows at the cap", rows);
    }
}

/* ---- C: the cap with a pair fish next door (the staged fry stays staged): born once there is room, nothing lost or doubled ---- */
static void case_c(int room_first) {
    const char *w = room_first ? "C2 (a place made while it is next door)" : "C1 (home while full, then a place)";
    stage_tank(POP_CAP, 2000);
    T.fish[2].trust = 1; T.fish[3].trust = 1;
    if (!progression_breed_pair(&T, 2, 3)) { BW_FAIL("%s: could not pair", w); return; }
    const uint32_t pa = T.fish[2].uid, pb = T.fish[3].uid, sold = T.fish[0].uid;
    uint32_t keep[POP_CAP]; for (int i = 0; i < POP_CAP; i++) keep[i] = T.fish[i].uid;
    gates(); progression_stage_arrival(&T);
    T.fish[3].away = true;
    mark();
    progression_slept(&T, 30); progression_woke(&T);   /* a lit wake, full */
    live(10.0f);
    if (T.n_fish != POP_CAP) BW_FAIL("%s: full, a lit wake: %d fish", w, T.n_fish);
    if (T.breed_a != pa || T.breed_b != pb || T.breed_paid != SD_PRICE_PAIR) BW_FAIL("%s: full, the pair undone at the wake (paid %d)", w, T.breed_paid);
    if (unearned() != 0) BW_FAIL("%s: full, the wallet moved %+d", w, unearned());
    if (!room_first) {                                /* home, still full: nothing is born, nothing comes back */
        T.fish[find(pb)].away = false;
        for (int k = 0; k < 25 * 120; k++) { gates(); frame(0.04f); }
        if (T.n_fish != POP_CAP) BW_FAIL("%s: home but full: %d fish", w, T.n_fish);
        if (T.breed_a != pa || T.breed_b != pb || T.breed_paid != SD_PRICE_PAIR) BW_FAIL("%s: home but full: the pair undone (paid %d)", w, T.breed_paid);
        if (unearned() != 0) BW_FAIL("%s: home but full: the wallet moved %+d", w, unearned());
    }
    if (!progression_arrival_pending()) BW_FAIL("%s: full, the staged fry was dropped before any place was made", w);
    /* a place: a fish that is not of the pair sold (its price is the only money that moves) */
    if (!progression_sell_fish(&T, find(sold))) { BW_FAIL("%s: the sale (staging)", w); return; }
    frame(0.04f); welcome_all();
    T.player_feedings += SELL_FRY_MEALS;              /* (a sale calls a staged fry off and the place is earned again by care - the
                                                         sale's own rule, progression_sell_fish, untouched: these are those meals) */
    mark();
    const int n0 = T.n_fish;
    if (room_first) {                                 /* room, but the pair fish still next door: still nobody */
        for (int k = 0; k < 25 * 60; k++) { gates(); frame(0.04f); }
        if (T.n_fish != n0) BW_FAIL("%s: room, the fish still next door: a fry came (%d fish)", w, T.n_fish);
        if (T.breed_paid != SD_PRICE_PAIR || unearned() != 0) BW_FAIL("%s: room, next door: paid %d, wallet %+d", w, T.breed_paid, unearned());
        T.fish[find(pb)].away = false;
    }
    int k = 0;
    for (; k < 25 * 600 && T.n_fish == n0; k++) { gates(); frame(0.04f); }
    if (T.n_fish != n0 + 1 && getenv("BW_DEBUG")) { fry_req_t r[FRY_REQ_MAX]; bool st; int n = progression_next_fry(&T, r, &st);
        for (int i = 0; i < n; i++) printf("  gate %s: %s met %d frac %.2f (%s)\n", r[i].title, r[i].words, r[i].met, r[i].frac, r[i].progress); }
    if (T.n_fish != n0 + 1) { BW_FAIL("%s: a place and the pair home, no fry within 10 min (%d fish, staged %d, courting %d)", w, T.n_fish, progression_arrival_pending(), T.courting); return; }
    if (!fry_of(n0, pa, pb)) BW_FAIL("%s: the fry is not the pair's", w);
    if (T.breed_a || T.breed_b || T.breed_paid) BW_FAIL("%s: the pair not spent at its birth", w);
    if (unearned() != 0) BW_FAIL("%s: the wallet moved %+d beyond what was earned (a refund, or a second charge)", w, unearned());
    { int toast = progression_sd_take_award(); if (toast >= SD_PRICE_PAIR) BW_FAIL("%s: a +%d toast", w, toast); }
    for (int i = 1; i < POP_CAP; i++) if (find(keep[i]) < 0) BW_FAIL("%s: fish %08x lost", w, (unsigned)keep[i]);
    welcome_all(); mark();
    for (int q = 0; q < 25 * 120; q++) { gates(); frame(0.04f); welcome_all(); }
    if (T.n_fish != n0 + 1) BW_FAIL("%s: a second fry for one payment (%d fish)", w, T.n_fish);
    if (unearned() != 0) BW_FAIL("%s: after the birth the wallet moved %+d", w, unearned());
    printf("breed-wait: %s: the pair's fry %.1f s after the pair could court\n", w, k * 0.04f);
}

/* ---- K: with OWED CARDS (fix042-nightcards, rc-0.4.2b): a pair waiting for a fish next door, its fry staged, and a card the
   dark holds - slept, a dark boot, slept again, the lit boot. The pair, the staged fry and the owed card all survive the
   nights in one save; the card is said once, the fry is the pair's once the fish is home ---- */
static bool s_k_was;                                 /* (the card up at the end of the last k_frames: not a new one; a boot clears it) */
static int k_frames(float secs, uint32_t bit) {      /* frames as the tank runs them (no dismiss): how often tank badge `bit` came up */
    int n = 0; bool was = s_k_was;
    for (int i = 0; i < (int)(secs * 25); i++) {
        gates();
        tank_tick(&T, 0.04f, advisor_rules); progression_tick(&T, 0.04f); notice_tick(&T, 0.04f, false);
        const notice_t *c = notice_current();
        bool up = c && c->kind == NOTICE_TANK_MILESTONE && c->bit == bit;
        if (up && !was && !notice_came_back()) { n++; if (getenv("BW_DEBUG")) printf("  K: up at clock %.1f since %.2f age %.2f pending %d\n", T.clock, c->since, c->age, notice_pending()); }
        if (getenv("BW_DEBUG") && was && !up) printf("  K: down at clock %.1f (now %s kind %d)\n", T.clock, c ? "other" : "none", c ? c->kind : -1);
        was = up;
    }
    s_k_was = was;
    return n;
}
static void case_k(void) {
    const char *w = "K a pair waiting + an owed card, two nights";
    stage_tank(4, 2000);
    T.fish[2].trust = 1; T.fish[3].trust = 1;
    if (!progression_breed_pair(&T, 2, 3)) { BW_FAIL("%s: could not pair", w); return; }
    const uint32_t pa = T.fish[2].uid, pb = T.fish[3].uid;
    if (T.tank_ms_bits & TMS_FIRST_REEF) { BW_FAIL("%s: (the test) the reef badge was already earned", w); return; }
    gates();
    T.fish[3].away = true;
    progression_stage_arrival(&T);
    const int n0 = T.n_fish;
    T.light_auto = false; T.light_manual_off = true; progression_settings_changed();   /* the light off */
    T.tank_ms_bits |= TMS_FIRST_REEF;                  /* earned in the dark: held */
    s_k_was = false; int said = k_frames(1.0f, TMS_FIRST_REEF);
    mark();
    progression_save(&T); sim_clock_shift(8 * 3600); boot(); s_k_was = false;   /* night 1, a dark morning */
    said += k_frames(20.0f, TMS_FIRST_REEF);
    if (said) BW_FAIL("%s: the dark said the card (%d)", w, said);
    if (T.n_fish != n0 || T.breed_a != pa || T.breed_b != pb || T.breed_paid != SD_PRICE_PAIR || unearned() != 0)
        BW_FAIL("%s: the dark morning: %d fish (were %d), pair %08x + %08x paid %d, wallet %+d", w, T.n_fish, n0, (unsigned)T.breed_a, (unsigned)T.breed_b, T.breed_paid, unearned());
    if (!progression_arrival_pending()) BW_FAIL("%s: the dark morning dropped the staged fry", w);
    progression_save(&T);
    {   static uint8_t b[8192]; uint32_t chk = 0, tms = 0; long nb = 0;
        FILE *f = fopen(getenv("POCKET_TANK_SAVE"), "rb"); if (f) { nb = (long)fread(b, 1, sizeof b, f); fclose(f); }
        if (nb >= 2200) { memcpy(&chk, b + 2176, 4); memcpy(&tms, b + 2180, 4); }
        if (nb != 2200 || !chk || !(tms & TMS_FIRST_REEF)) BW_FAIL("%s: the save (%ld B) does not owe the card (check %08x, tank badges %05x)", w, nb, (unsigned)chk, (unsigned)tms);
    }
    sim_clock_shift(1 * 3600); boot(); s_k_was = false;   /* night 2 (slept again without the light), then the light on */
    T.light_manual_off = false; progression_settings_changed();
    if (!T.breed_a || !progression_arrival_pending()) BW_FAIL("%s: the second boot: pair %08x, staged %d", w, (unsigned)T.breed_a, progression_arrival_pending());
    int ib = find(pb);
    if (ib >= 0 && T.fish[ib].away && T.link_restore_at > 0) { T.clock = T.link_restore_at; tank_link_tick(&T, NULL); }
    if (ib < 0 || T.fish[find(pb)].away) { BW_FAIL("%s: the fish did not come home", w); return; }
    for (int k = 0; k < 120 && T.n_fish == n0; k++) said += k_frames(1.0f, TMS_FIRST_REEF);
    said += k_frames(30.0f, TMS_FIRST_REEF);
    if (said != 1) BW_FAIL("%s: the owed card said %d time(s) (want 1)", w, said);
    if (T.n_fish != n0 + 1 || !fry_of(n0, pa, pb)) BW_FAIL("%s: the lit boot, the fish home: %d fish (were %d), the fry not the pair's", w, T.n_fish, n0);
    if (T.breed_a || T.breed_b || T.breed_paid) BW_FAIL("%s: the pair not spent at its birth", w);
    if (unearned() != 0) BW_FAIL("%s: the wallet moved %+d beyond what was earned", w, unearned());
    printf("breed-wait: %s: kept through both nights, the card said %d time(s), the pair's fry %s\n", w, said, T.n_fish == n0 + 1 ? "born" : "NOT born");
}

int selftest_breedwait(void) {
    board_env();
    for (int how = 0; how < 4; how++) case_w(how);
    case_s();
    case_b();
    case_c(0); case_c(1);
    case_k();
    board_new();
    if (bw_bad) { printf("selftest-breed-wait: %d FAILED\n", bw_bad); return 1; }
    printf("selftest-breed-wait: a pair fish next door waits at a boot, a nap's wake, a forced birth and awake (no refund, no stand-in, the pair's fry once home); "
           "NEW FRY not staged while the pair cannot court and says WAITING FOR <NAME>; at the cap the staged fry waits and is the pair's once there is room "
           "(one fry, one payment); no pair, a pair at home, sold / given / moved, the dark, the cap: as before\n");
    return 0;
}

/* --breed-wait-shot <prefix> (0.4.2 audit r2): the keeper page's NEW FRY in the wait, for tools/board_sheet.py, with names as long
   as the wheel takes: <prefix>_page.ppm (the row: the fry-to-be dim), _away.ppm (WAITING FOR BUBBLES / TO COME HOME), _both.ppm
   (... AND SPARKLE TO COME HOME), _grow.ppm (... TO GROW UP) */
static uint16_t s_fb[TANK_W * TANK_H];
static void bw_ppm(const char *prefix, const char *name) {
    char path[600]; snprintf(path, sizeof path, "%s_%s.ppm", prefix, name);
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        uint16_t v = FB_SWAP16(s_fb[i]);
        uint8_t px[3] = { (uint8_t)(((v >> 11) & 31) << 3), (uint8_t)(((v >> 5) & 63) << 2), (uint8_t)((v & 31) << 3) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}
static void bw_shot_modal(const char *prefix, const char *name) {
    render_milestones_leave(); render_milestones(&T, s_fb, TANK_W);
    int x, y; render_milestones_row(T.n_fish, &x, &y);
    if (render_milestones_tap(&T, (float)x, (float)y) != MS_TAP_KEPT) printf("breed-wait-shot: %s: no modal\n", name);
    render_milestones(&T, s_fb, TANK_W); bw_ppm(prefix, name);
    printf("breed-wait-shot: %s: %s / %s / %s\n", name, render_milestones_caption(0), render_milestones_caption(1), render_milestones_line());
}
int breed_wait_shot(const char *prefix) {
    board_env();
    stage_tank(4, 2000);
    static const char *const NM[4] = { "MOONPIE", "WIGGLES", "BUBBLES", "SPARKLE" };
    for (int i = 0; i < 4; i++) tank_set_name(&T, i, NM[i]);
    progression_breed_pair(&T, 2, 3);
    gates(); progression_stage_arrival(&T);
    T.fish[2].away = true;
    render_milestones_leave(); render_milestones(&T, s_fb, TANK_W); bw_ppm(prefix, "page");
    bw_shot_modal(prefix, "away");
    T.fish[3].away = true; bw_shot_modal(prefix, "both");
    T.fish[2].away = T.fish[3].away = false;
    progression_set_age(&T, 2, (float)STAGE_JUV_AGE + 60); T.fish[2].stage = STAGE_JUV;
    bw_shot_modal(prefix, "grow");
    render_milestones_leave();
    board_new();
    return 0;
}
