#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_clock_rebase.c - --selftest-clock-rebase (0.4.2 audit round 2, PW2-4 / PW2-5): the FIRST time sync moves the
 * clock by a large jump (firmware main.c net_clock_rebase, "nothing lived for the jump"). Everything else stamped on the
 * old clock must move with it: a tank DORMANT on the shelf (its next SWAP IN lived the jump, capped at 7 days, at once)
 * and a sand dollar gift in flight (it came back at once). In main.c's boot order (shelf_boot + progression_boot on the
 * OLD clock, then the rebase), after the reviewer's harness case P7 (selftest_pw2.c). The siblings that must not change:
 * an ordinary span on the shelf is still lived, a gift's day still runs through a night's power-off, a PAUSED tank lives
 * what it banked. PW4_NOFIX=1 leaves the rebase's call out (the firmware before the fix): every R case goes red. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tank.h"
#include "advisor.h"
#include "progression.h"
#include "notice.h"
#include "shelf.h"
#include "setup.h"
#include "sdgift.h"
#include "tank_link.h"

void sim_clock_shift(int64_t s);
void sim_clock_freeze(int on);
const char *sim_shelf_dir(void);
void render_shelf_thumb_size(int *w, int *h);

static tank_t T;
static int s_fail;
#define H (3600)
#define DAY (24 * 3600)
#define CR_FAIL(...) do { printf("selftest-clock-rebase: FAIL - "); printf(__VA_ARGS__); printf("\n"); s_fail++; } while (0)

static void cr_board(const char *name) {
    char dir[600], cmd[1400], p[700];
    snprintf(dir, sizeof dir, "%s/cr-%s", sim_shelf_dir(), name);
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", dir, dir); (void)system(cmd);
    snprintf(p, sizeof p, "%s/tank.sav", dir); setenv("POCKET_TANK_SAVE", p, 1);
    snprintf(p, sizeof p, "%s/storage.bin", dir); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", dir); setenv("POCKET_TANK_FRAME", p, 1);
}
static float cr_boot(uint32_t seed) {                /* main.c's order: the shelf, the save lived through, the carry */
    memset(&T, 0, sizeof T); tank_init(&T, seed);
    shelf_boot(clock_port_now_unix());
    float h = progression_boot(&T);
    progression_shelf_settle(&T);
    progression_swap_carry(&T);
    notice_sync(&T);
    return h;
}
static void cr_frames(float secs) {                  /* the tank running, the link ticked with no engine (the gift's look) */
    const float dt = 0.05f;
    for (float at = 0; at < secs; at += dt) {
        tank_tick(&T, dt, advisor_rules);
        tank_link_tick(&T, NULL);
        progression_tick(&T, dt);
        notice_tick(&T, dt, true);
        (void)notice_take_cue();
    }
}
static void cr_found(uint32_t seed) { cr_boot(seed); progression_setup_done(&T); progression_save(&T); }
static bool cr_put_up(int i) {                       /* SWAP IN shelf tank i (main.c: the save, the carry, the swap; then a restart) */
    static uint16_t thumb[200 * 200]; int w, h; render_shelf_thumb_size(&w, &h);
    if (!progression_save(&T)) return false;
    progression_carry_t c; progression_carry_take(&T, &c);
    return shelf_swap(i, thumb, w, h, &c, sizeof c, clock_port_now_unix());
}
static int cr_index_of(uint32_t id) { for (int i = 0; i < shelf_count(); i++) if (shelf_get(i)->id == id) return i; return -1; }
/* tank A (founded) onto the shelf DORMANT, a new tank B on the glass; returns A's shelf ID (0 = staging failed) */
static uint32_t cr_shelve_a(uint32_t seed) {
    cr_found(seed);
    uint32_t idA = shelf_now()->id;
    int i = shelf_new(0, clock_port_now_unix());
    if (i < 0 || !cr_put_up(i)) { CR_FAIL("staging: tank A would not go onto the shelf"); return 0; }
    cr_boot(seed + 1); progression_setup_done(&T); progression_save(&T);
    return idA;
}
/* net_clock_rebase: the fix's call BEFORE the clock moves, the clock, the save marked (as main.c) */
static void cr_rebase(int64_t jump) {
    if (!getenv("PW4_NOFIX")) progression_clock_rebase(&T, clock_port_now_unix() + jump);
    sim_clock_shift(jump);
    progression_settings_changed();
}
static float cr_swap_in(uint32_t id, uint32_t seed) {   /* returns the hours the boot lived (-9 = refused) */
    int k = cr_index_of(id);
    if (k < 0 || !cr_put_up(k)) { CR_FAIL("staging: the SWAP IN was refused"); return -9; }
    return cr_boot(seed);
}
static void cr_give(void) {                           /* 100 of 500 in flight to a neighbor never met again */
    uint32_t nonce = 0; T.sd_balance = 500;
    if (sdg_out_begin(&T.sdg, &T.sd_balance, 0xBEEF01, 100, (uint32_t)clock_port_now_unix(), &nonce) != SDG_SENT) CR_FAIL("staging: the gift did not leave");
    progression_save(&T);
}

int selftest_clock_rebase(void) {
    sim_clock_freeze(1);
    uint32_t idA; float h; int32_t bal;

    /* R1 (PW2-4): A DORMANT on the shelf, the first sync's boot a minute on (+60 days), SWAP IN two minutes later */
    cr_board("r1"); idA = cr_shelve_a(6101);
    sim_clock_shift(60); cr_boot(6103); cr_rebase(60LL * DAY); cr_frames(5);
    { int k = cr_index_of(idA); int64_t d = k < 0 ? -1 : clock_port_now_unix() - shelf_get(k)->shelved_unix;
      if (d < 0 || d > 600) CR_FAIL("R1: A's card would count %lld days on the shelf, 65 s after it was shelved", (long long)(d / DAY)); }
    sim_clock_shift(120);
    h = cr_swap_in(idA, 6104);
    printf("selftest-clock-rebase: R1 a DORMANT tank swapped in 3 min after it was shelved, the first sync (+60 days) between: lived %.2f h (the shelf says %.2f h), fish 0 hunger %.1f stage %d\n",
           h, shelf_lived_h(), T.n_fish ? T.fish[0].hunger : -1.0f, T.n_fish ? T.fish[0].stage : -1);
    if (h > 0.2f || shelf_lived_h() > 0.2f) CR_FAIL("R1 (PW2-4): the shelved tank lived %.1f h at its SWAP IN (the shelf says %.1f h), want the ~3 minutes it was away", h, shelf_lived_h());

    /* R2 (PW2-5): a gift in flight on the glass through the first sync; then its day on the new clock */
    cr_board("r2"); cr_found(6201); cr_give();
    sim_clock_shift(60); cr_boot(6202); cr_rebase(60LL * DAY); cr_frames(35);
    printf("selftest-clock-rebase: R2 a gift 95 s in flight at the first sync: balance %d, in flight %d\n", (int)T.sd_balance, sdg_out_amount(&T.sdg));
    if (T.sd_balance != 400 || sdg_out_amount(&T.sdg) != 100) CR_FAIL("R2 (PW2-5): the gift came back at the first sync (balance %d, in flight %d), want 400 and 100", (int)T.sd_balance, sdg_out_amount(&T.sdg));
    progression_save(&T); sim_clock_shift(23 * H); cr_boot(6203); cr_frames(5);
    if (sdg_out_amount(&T.sdg) != 100) CR_FAIL("R2: 23 h after the sync the gift is back (balance %d): its day was shortened", (int)T.sd_balance);
    bal = T.sd_balance;                               /* (the tank earns its own sand dollars over a night: the gift's 100 on top) */
    progression_save(&T); sim_clock_shift(2 * H); cr_boot(6204); cr_frames(5);
    if (T.sd_balance < bal + 100 || sdg_out_amount(&T.sdg) != 0) CR_FAIL("R2: 25 h after the sync the gift is not back (balance %d, in flight %d): nothing may be lost", (int)T.sd_balance, sdg_out_amount(&T.sdg));

    /* R3 (PW2-5 on the shelf): the giving tank DORMANT on the shelf through the first sync, swapped in after it */
    cr_board("r3"); cr_found(6301); cr_give();
    {   uint32_t idG = shelf_now()->id; int i = shelf_new(0, clock_port_now_unix());
        if (i < 0 || !cr_put_up(i)) CR_FAIL("R3 staging: the giving tank would not go onto the shelf");
        cr_boot(6302); progression_setup_done(&T); progression_save(&T);
        sim_clock_shift(60); cr_boot(6303); cr_rebase(60LL * DAY); cr_frames(5);
        sim_clock_shift(120); h = cr_swap_in(idG, 6304); cr_frames(35);
        printf("selftest-clock-rebase: R3 the giving tank on the shelf through the first sync, swapped in: lived %.2f h, in flight %d\n", h, sdg_out_amount(&T.sdg));
        if (sdg_out_amount(&T.sdg) != 100) CR_FAIL("R3 (PW2-5): a shelved tank's gift came back at its SWAP IN after the first sync (in flight %d)", sdg_out_amount(&T.sdg));
    }

    /* R4: the clock was AHEAD (the first sync sets it 5 days back): two real hours on the shelf are lived (it read 0) */
    cr_board("r4"); idA = cr_shelve_a(6401);
    sim_clock_shift(60); cr_boot(6403); cr_rebase(-5LL * DAY); cr_frames(5);
    sim_clock_shift(2 * H);
    h = cr_swap_in(idA, 6404);
    printf("selftest-clock-rebase: R4 the clock set 5 days BACK, 2 h on the shelf: lived %.2f h\n", h);
    if (h < 1.9f || h > 2.2f) CR_FAIL("R4: after a clock set back the shelved tank lived %.2f h of its 2 h", h);

    /* R5: the rebase cut short after the shelf, before the clock - and done again at the next boot: never the jump */
    cr_board("r5"); idA = cr_shelve_a(6501);
    sim_clock_shift(60); cr_boot(6503);
    if (!getenv("PW4_NOFIX")) progression_clock_rebase(&T, clock_port_now_unix() + 60LL * DAY);   /* (the cut: the clock never moved) */
    cr_boot(6504); cr_rebase(60LL * DAY); cr_frames(5);
    sim_clock_shift(120);
    h = cr_swap_in(idA, 6505);
    printf("selftest-clock-rebase: R5 the rebase done twice (a cut between the shelf and the clock): lived %.2f h\n", h);
    if (h > 0.2f) CR_FAIL("R5: a rebase done twice made the shelved tank live %.1f h", h);

    /* S1 (must not change): no sync - two days DORMANT on the shelf are lived at the SWAP IN */
    cr_board("s1"); idA = cr_shelve_a(6601);
    sim_clock_shift(2 * DAY);
    h = cr_swap_in(idA, 6603);
    printf("selftest-clock-rebase: S1 no sync, 2 days DORMANT on the shelf: lived %.2f h\n", h);
    if (h < 47.9f || h > 48.2f) CR_FAIL("S1: an ordinary 2 days on the shelf lived %.2f h, want 48", h);

    /* S2 (must not change): the first sync, THEN two days on the shelf (a sleep span after it): lived whole */
    cr_board("s2"); idA = cr_shelve_a(6701);
    sim_clock_shift(60); cr_boot(6703); cr_rebase(60LL * DAY); cr_frames(5); progression_save(&T);
    sim_clock_shift(2 * DAY); cr_boot(6704);          /* (the night's power-off and its boot: B lives it on the glass) */
    h = cr_swap_in(idA, 6705);
    printf("selftest-clock-rebase: S2 the first sync, then 2 days on the shelf: lived %.2f h\n", h);
    if (h < 47.9f || h > 48.2f) CR_FAIL("S2: 2 days on the shelf after the first sync lived %.2f h, want 48", h);

    /* S3 (must not change): a gift's day runs through a night's power-off with no sync */
    cr_board("s3"); cr_found(6801); cr_give();
    sim_clock_shift(23 * H); cr_boot(6802); cr_frames(5);
    if (sdg_out_amount(&T.sdg) != 100) CR_FAIL("S3: a gift 23 h out came back (balance %d)", (int)T.sd_balance);
    bal = T.sd_balance;
    progression_save(&T); sim_clock_shift(2 * H); cr_boot(6803); cr_frames(5);
    printf("selftest-clock-rebase: S3 a gift through a night with no sync, 25 h: balance %d, in flight %d\n", (int)T.sd_balance, sdg_out_amount(&T.sdg));
    if (T.sd_balance < bal + 100 || sdg_out_amount(&T.sdg) != 0) CR_FAIL("S3: a gift 25 h out is not back (balance %d, in flight %d)", (int)T.sd_balance, sdg_out_amount(&T.sdg));

    /* S4 (must not change): a PAUSED tank lives what it banked (3 h), its bank untouched by the rebase */
    cr_board("s4"); idA = cr_shelve_a(6901);
    sim_clock_shift(3 * H);
    if (!shelf_set_time(cr_index_of(idA), SHELF_TIME_PAUSED, clock_port_now_unix())) CR_FAIL("S4 staging: PAUSED refused");
    {   uint32_t bank0 = shelf_get(cr_index_of(idA))->banked_s;
        cr_boot(6903); cr_rebase(60LL * DAY); cr_frames(5);
        int k = cr_index_of(idA);
        if (k < 0 || shelf_get(k)->banked_s != bank0 || shelf_get(k)->time_mode != SHELF_TIME_PAUSED) CR_FAIL("S4: the rebase changed a PAUSED tank's bank (%u -> %u) or its mode", (unsigned)bank0, k < 0 ? 0u : (unsigned)shelf_get(k)->banked_s);
        sim_clock_shift(5 * H);
        h = cr_swap_in(idA, 6904);
        printf("selftest-clock-rebase: S4 PAUSED with 3 h banked, the first sync, 5 h more: lived %.2f h\n", h);
        if (h < 2.9f || h > 3.1f) CR_FAIL("S4: a PAUSED tank lived %.2f h, want its banked 3", h);
    }

    /* S5 (must not change): a correction under a minute rewrites no slot; a jump of 0 is nothing */
    cr_board("s5"); idA = cr_shelve_a(7001);
    {   shelf_tank_t was = *shelf_get(cr_index_of(idA));
        cr_boot(7003); cr_rebase(30); cr_rebase(0);
        int k = cr_index_of(idA);
        if (k < 0 || memcmp(&was, shelf_get(k), sizeof was)) CR_FAIL("S5: a 30 s correction rewrote the shelved tank's slot");
    }

    printf("selftest-clock-rebase: %s\n", s_fail ? "FAIL" : "PASS");
    return s_fail ? 1 : 0;
}
