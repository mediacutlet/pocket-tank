#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_dark_notices.c - --selftest-dark-notices (0.4.2, the dark holds notices). A keeper's dark tank lit up at 5 am
 * for a fry's NAME page; Strato: "tank light off should be treated largely as a notification suppressor too", and a fry
 * home from a linked neighbor waits for the light before its welcome shows. The frames run in FIRMWARE main.c's order
 * (hold_light / ui_cover, tank_tick, progression_tick, notice_tick with the welcome blocking, then setup_poll_birth).
 *
 *   D1  the dark holds: a stage card, a fish's badge and a tank's badge earned in the dark - nothing up, no chime, all
 *       queued; the light on: each comes up in the order earned, with its chime
 *   D2  a notice UP when the light goes off steps aside; it comes back first, without a second chime
 *   D3  the four the dark still shows (LIGHTS OUT, LOW BATTERY, AI MODEL DAMAGED, WRONG VERSION) come up from behind
 *       held ones, the held ones keep their order
 *   D4  a fry home from next door in the dark: no welcome, the light stays off, the welcome stays owed (and blocks
 *       nothing: LOW BATTERY still shows); the light on: the welcome first, then its A TRIO badge with the chime
 *   D5  a fry home in a lit tank: the welcome opens that same frame, as ever
 *   D6  a boot into a dark tank (the double-tap's state from the save, t->night not computed yet): no welcome, no card
 *   D7  a fry bought in the shop of a dark tank: its welcome at once (the keeper's own tap)
 *   D8  a long dark night fills the queue: LOW BATTERY still gets a place and shows; the newest held badge gave it
 *   D9  LIGHTS OUT on AUTO at 5 s (dark again before a card's 6 s): three held, the tank handled every 8 s - a card a
 *       wake, the queue drains, no card chimes twice (the review of d72d78d: the head came back whole for ever)
 *   D10 a card the dark stepped aside comes back for the REST of its time: no chime, no second save of a stage card, a
 *       press begun before it came back is spent
 *   D11 a full queue and the light goes off under a card that is up: the card is kept (the newest held one goes)
 *   D12 the director's light (light_override) is the light: on over a double-tap's off shows, off holds
 *   D13 a fish sold with its badge held in the dark: its notice goes, the next fish's follows it down a slot
 *   D15 SELL / GIVE held by a fry not yet named: the line asks for the light in the dark
 *   D16 a fry waiting for the light holds itself only: another fish sells, the welcome follows its fry
 *   D14 notice_sync (a reset, a MY TANKS swap, a boot) with notices held in the dark: dropped, as under a page - pinned
 *   T1  (audit T1-F1, the audit's probe Q1) one of ours home from a visit, the power cut 5 s later inside the 30 s
 *       coalesce: home, its badge, its age and its pay kept (grown up there: its stage paid once); three home in one
 *       tick: one save; a fry and a grown fish in one tick, in either order, and from the boot's restore: one save
 *       (every write of the tank's save is counted, the sim's persist port) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tank.h"
#include "advisor.h"
#include "progression.h"
#include "notice.h"
#include "setup.h"
#include "sounds.h"
#include "passport.h"
#include "link.h"
#include "tank_link.h"
#include "collection.h"

const char *sim_shelf_dir(void);
extern int g_sim_tank_saves;                     /* persist_port_sim.c: every write of the tank's save */

static tank_t T;
static bool g_page;                              /* a page covers the tank (milestones, settings: main.c's ui_cover) */
static int dn_bad;
#define DN_FAIL(...) do { printf("dark-notices: FAIL: "); printf(__VA_ARGS__); printf("\n"); dn_bad++; } while (0)
#define DT 0.05f

static void dn_board(void) {                     /* a scratch board of its own ($PT_SHELF_DIR/dark-notices) */
    char cmd[800], d[600], p[700];
    snprintf(d, sizeof d, "%s/dark-notices", sim_shelf_dir());
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", d, d); (void)system(cmd);
    snprintf(p, sizeof p, "%s/tank.sav", d); setenv("POCKET_TANK_SAVE", p, 1);
    snprintf(p, sizeof p, "%s/storage.bin", d); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", d); setenv("POCKET_TANK_FRAME", p, 1);
}
/* a calm tank: the tests count cards exactly, so nothing may be earned by itself. The fish keep the goal they have (no
   dart, no visit to the bubbles: dn_keep), and what a resting fish or the tank earns alone is earned already */
static goal_t dn_keep(const tank_t *t, int i, bool request) { (void)request; return t->fish[i].goal; }
static void dn_calm(void) {
    for (int i = 0; i < T.n_fish; i++) T.fish[i].ms_bits |= MS_FIRST_GRASS | MS_FIRST_FOLLOW | MS_FIRST_HOLD_APPROACH | MS_FIRST_MEAL_FROM_YOU | MS_INSPECTED;
    T.tank_ms_bits |= TMS_FIRST_PLAY_SESSION | TMS_CHANGED_SOMEONE | TMS_FIRST_REEF | TMS_FULL_SCHOOL | TMS_SEAHORSE_FED | TMS_CRAB_FIND | TMS_FIRST_FULL_NIGHT;
}
static void dn_boot(uint32_t seed) {             /* the power on: the tank as its save has it */
    setup_cancel(&T);
    memset(&T, 0, sizeof T); tank_init(&T, seed); progression_boot(&T); notice_sync(&T);
}
static void dn_fresh(uint32_t seed) {
    g_page = false;
    dn_board(); collection_reset();
    dn_boot(seed); progression_setup_done(&T); progression_tick(&T, 0.1f); dn_calm(); notice_sync(&T);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    dn_calm(); notice_tick(&T, DT, true);
    setup_poll_birth(&T);                        /* (nothing owed: the last offer forgotten) */
}
static void dn_light(bool on) { T.light_auto = false; T.light_manual_off = !on; }   /* the keeper's double-tap */

/* one frame, as the tank task runs it; what came up in it */
static int g_cue, g_born; static const notice_t *g_up; static bool g_was_up;
static int dn_frame(void) {
    T.hold_light = setup_active() && !setup_is_about(); T.ui_cover = T.hold_light || g_page;
    tank_tick(&T, DT, dn_keep);
    progression_tick(&T, DT);
    notice_tick(&T, DT, T.ui_cover || setup_birth_due(&T));
    g_cue = notice_take_cue();
    g_up = notice_current();
    int came = g_up && !g_was_up ? g_up->kind : -1;                  /* (it came up, or came back) */
    g_was_up = g_up != NULL;
    g_born = setup_poll_birth(&T);
    return came;
}
/* frames that must stay silent: nothing up, no chime, no welcome */
static bool dn_quiet(float secs, const char *what) {
    for (float at = 0; at < secs; at += DT) {
        int came = dn_frame();
        if (came >= 0 || g_up || g_cue >= 0) { DN_FAIL("%s: a notice in the dark (kind %d, cue %d) after %.2f s", what, g_up ? g_up->kind : -1, g_cue, (double)at); return false; }
        if (g_born >= 0 || setup_active()) { DN_FAIL("%s: a welcome opened in the dark after %.2f s", what, (double)at); setup_cancel(&T); return false; }
    }
    return true;
}
/* the next notice to come up within secs (dismissed once read); its cue in *cue. NULL = none */
static notice_t s_got;
static const notice_t *dn_next(float secs, int *cue) {
    for (float at = 0; at < secs; at += DT) {
        int came = dn_frame();
        if (came >= 0) {
            s_got = *g_up; if (cue) *cue = g_cue;
            for (int k = 0; k < 8; k++) dn_frame();
            notice_dismiss(); g_was_up = false;
            return &s_got;
        }
    }
    return NULL;
}
static void dn_drain(void) {                                /* (a frame first: what was just earned is queued by the next notice_tick) */
    for (int k = 0; k < 4000 && (k < 2 || notice_current() || notice_pending()); k++) { dn_frame(); if (notice_current() && notice_current()->age > 0.2f) notice_dismiss(); }
    g_was_up = false;
}
static passport_t dn_fry_next_door(uint32_t seed) {      /* a fry of ours, born next door */
    static tank_t there; tank_init(&there, seed); tank_new_population(&there);
    fish_t fry = there.fish[0]; fry.stage = STAGE_FRY; fry.size = 0.4f; fry.ms_bits = MS_CROSSBRED;
    fry.parent_uid[0] = T.fish[0].uid; fry.parent_uid[1] = 0xc0ffee02u; fry.mutated = 0;
    passport_t pp; passport_from_fish(&pp, &fry, T.uid, 0);
    return pp;
}
static int dn_fry_home(uint32_t seed) {                  /* ... and home (LEV_FISH_BACK): its welcome owed */
    passport_t pp = dn_fry_next_door(seed);
    int s = progression_fry_adopt(&T, &pp, LINK_LEFT);
    if (s < 0) return -1;
    link_ev_t e; memset(&e, 0, sizeof e);                /* the link's own path: the event, then the tick (link_dev_poll) */
    e.type = LEV_FISH_BACK; e.uid = T.fish[s].uid; e.pp = pp; e.edge = LINK_LEFT; e.depth = 0.5f; e.speed = 30;
    tank_link_event(&T, NULL, &e);
    tank_link_tick(&T, NULL);
    return T.fish[s].away || T.fish[s].born_away ? -1 : s;
}
static void dn_welcome_done(void) { for (int g = 0; g < 12 && setup_active(); g++) setup_activate(&T, SETUP_HIT_NEXT); }

static void d1_holds(void) {
    dn_fresh(501); dn_light(false);
    if (!dn_quiet(1.0f, "D1")) return;
    if (!T.night) { DN_FAIL("D1: the double-tap's light off is not night"); return; }
    dn_drain();
    progression_set_age(&T, 1, (float)STAGE_JUV_AGE + 60);          /* a fish grows up in the dark */
    if (!dn_quiet(1.0f, "D1 (stage)")) return;
    T.fish[0].ms_bits |= MS_FIRST_BUBBLES;                          /* ... a fish's badge */
    if (!dn_quiet(1.0f, "D1 (badge)")) return;
    T.tank_ms_bits |= TMS_FIRST_TRIM;                               /* ... a tank's */
    if (!dn_quiet(20.0f, "D1 (a long dark)")) return;
    int held = notice_pending();
    if (held != 3) { DN_FAIL("D1: %d held in the dark, 3 were earned", held); return; }
    dn_light(true);
    static const int want[3] = { NOTICE_STAGE, NOTICE_MILESTONE, NOTICE_TANK_MILESTONE };
    static const int chime[3] = { SND_STAGE_UP, SND_MILESTONE, SND_MILESTONE };
    static const uint32_t bit[3] = { 0, MS_FIRST_BUBBLES, TMS_FIRST_TRIM };
    static const int fish[3] = { 1, 0, -1 };
    for (int k = 0; k < 3; k++) {
        int cue = -2; const notice_t *n = dn_next(10.0f, &cue);
        if (!n) { DN_FAIL("D1: held notice %d of 3 never came once the light was on", k + 1); return; }
        if (n->kind != want[k] || n->bit != bit[k] || n->fish != fish[k]) { DN_FAIL("D1: notice %d out is kind %d bit %x fish %d, not the one earned %d%s", k + 1, n->kind, (unsigned)n->bit, n->fish, k + 1, k == 0 ? "st" : k == 1 ? "nd" : "rd"); return; }
        if (cue != chime[k]) { DN_FAIL("D1: notice %d (kind %d) came with cue %d, not its chime %d", k + 1, n->kind, cue, chime[k]); return; }
    }
    { int cue; const notice_t *n = dn_next(4.0f, &cue); if (n || notice_pending()) { DN_FAIL("D1: an extra notice after the three (kind %d, %d more)", n ? n->kind : -1, notice_pending()); return; } }
    printf("dark-notices: D1 PASS - 3 held through 23 s of dark with no card and no chime; the light on: stage, badge, tank badge in order, each with its chime, nothing else\n");
}
static void d2_steps_aside(void) {
    dn_fresh(502); dn_light(true); dn_quiet(0.5f, "D2 (lit, nothing earned)"); dn_drain();
    T.tank_ms_bits |= TMS_FIRST_TRIM;
    int came = -1, cue = -1; for (int k = 0; k < 60 && came < 0; k++) { came = dn_frame(); cue = g_cue; }
    if (came != NOTICE_TANK_MILESTONE || cue != SND_MILESTONE) { DN_FAIL("D2: the lit tank's badge did not come up with its chime (kind %d cue %d)", came, cue); return; }
    T.fish[0].ms_bits |= MS_FIRST_BUBBLES;                          /* another behind it */
    for (int k = 0; k < 10; k++) dn_frame();
    if (!notice_current() || notice_pending() != 1) { DN_FAIL("D2: setup (up %d, pending %d)", notice_current() != NULL, notice_pending()); return; }
    dn_light(false);
    dn_frame();
    if (notice_current()) { DN_FAIL("D2: the notice stayed up when the light went off"); return; }
    if (notice_pending() != 2) { DN_FAIL("D2: stepped aside, %d queued (want 2)", notice_pending()); return; }
    if (!dn_quiet(10.0f, "D2")) return;
    dn_light(true);
    came = -1; cue = -1; for (int k = 0; k < 60 && came < 0; k++) { came = dn_frame(); cue = g_cue; }
    if (came != NOTICE_TANK_MILESTONE || g_up->bit != TMS_FIRST_TRIM) { DN_FAIL("D2: the one that stepped aside did not come back first (kind %d)", came); return; }
    if (cue >= 0) { DN_FAIL("D2: it chimed a second time (cue %d)", cue); return; }
    if (!notice_came_back()) { DN_FAIL("D2: back, and not marked as come back"); return; }
    for (int k = 0; k < 8; k++) dn_frame(); notice_dismiss();
    const notice_t *n = dn_next(10.0f, &cue);
    if (!n || n->kind != NOTICE_MILESTONE || cue != SND_MILESTONE) { DN_FAIL("D2: the one behind it did not follow with its chime (kind %d cue %d)", n ? n->kind : -1, cue); return; }
    printf("dark-notices: D2 PASS - a notice up when the light went off stepped aside, came back first and quiet; the next with its chime\n");
}
static void d3_exempt(void) {
    dn_fresh(503); dn_light(false); dn_quiet(1.0f, "D3"); dn_drain();
    T.tank_ms_bits |= TMS_FIRST_TRIM;                               /* two held ones first */
    dn_quiet(0.5f, "D3");
    T.fish[0].ms_bits |= MS_FIRST_BUBBLES;
    if (!dn_quiet(2.0f, "D3")) return;
    int cue; const notice_t *n;
    notice_low_battery();
    n = dn_next(3.0f, &cue);
    if (!n || n->kind != NOTICE_LOW_BATTERY || cue != SND_LOW_BATTERY) { DN_FAIL("D3: LOW BATTERY did not come up in the dark with its sound (kind %d cue %d)", n ? n->kind : -1, n ? cue : -1); return; }
    notice_model_damaged();
    n = dn_next(3.0f, &cue);
    if (!n || n->kind != NOTICE_MODEL_DAMAGED) { DN_FAIL("D3: AI MODEL DAMAGED did not come up in the dark"); return; }
    notice_wrong_board();
    n = dn_next(3.0f, &cue);
    if (!n || n->kind != NOTICE_WRONG_BOARD) { DN_FAIL("D3: WRONG VERSION did not come up in the dark"); return; }
    T.light_tip_seen = false; dn_frame(); T.light_tip_seen = true;  /* the first double-tap that turned the light off */
    n = dn_next(3.0f, &cue);
    if (!n || n->kind != NOTICE_LIGHTS_OUT) { DN_FAIL("D3: the LIGHTS OUT notice did not come up in the dark"); return; }
    if (!T.night) { DN_FAIL("D3: the tank lit up"); return; }
    if (notice_pending() != 2) { DN_FAIL("D3: %d held behind them (want 2)", notice_pending()); return; }
    if (!dn_quiet(3.0f, "D3 (after the four)")) return;
    dn_light(true);
    n = dn_next(10.0f, &cue);
    if (!n || n->kind != NOTICE_TANK_MILESTONE || cue != SND_MILESTONE) { DN_FAIL("D3: the first held one was not first out (kind %d cue %d)", n ? n->kind : -1, cue); return; }
    n = dn_next(10.0f, &cue);
    if (!n || n->kind != NOTICE_MILESTONE || cue != SND_MILESTONE) { DN_FAIL("D3: the second held one was not second out (kind %d cue %d)", n ? n->kind : -1, cue); return; }
    printf("dark-notices: D3 PASS - LOW BATTERY, AI MODEL DAMAGED, WRONG VERSION and LIGHTS OUT came up in the dark from behind two held badges; those kept their order\n");
}
static void d4_welcome_waits(void) {
    dn_fresh(504); dn_light(false); dn_quiet(1.0f, "D4"); dn_drain();
    int s = dn_fry_home(97);
    if (s < 0 || progression_newborn() != s) { DN_FAIL("D4: the fry did not come home owed its welcome (%d, owed %d)", s, progression_newborn()); return; }
    if (!dn_quiet(20.0f, "D4 (a fry home)")) return;
    if (!T.night || T.hold_light) { DN_FAIL("D4: the tank lit up (night %d, hold %d)", T.night, T.hold_light); return; }
    if (progression_newborn() != s) { DN_FAIL("D4: the welcome is no longer owed (%d)", progression_newborn()); return; }
    if (setup_birth_due(&T)) { DN_FAIL("D4: a welcome waiting for the light blocks the dark's notices"); return; }
    int cue = -1; notice_low_battery();
    const notice_t *n = dn_next(3.0f, &cue);
    if (!n || n->kind != NOTICE_LOW_BATTERY) { DN_FAIL("D4: LOW BATTERY held all night behind a welcome waiting for the light"); return; }
    if (setup_active() || !T.night) { DN_FAIL("D4: the welcome opened behind LOW BATTERY"); return; }
    if (!dn_quiet(3.0f, "D4 (after LOW BATTERY)")) return;
    dn_light(true);
    int opened = -1;
    for (int k = 0; k < 40 && opened < 0; k++) {
        int came = dn_frame();
        if (came >= 0) { DN_FAIL("D4: a notice (kind %d) came up before the welcome", came); return; }
        if (g_born >= 0) opened = k;
    }
    if (opened < 0 || g_born != s || !setup_is_birth() || setup_page() != SETUP_PG_BORN) { DN_FAIL("D4: the light on, and no welcome (frame %d, slot %d)", opened, g_born); return; }
    for (int k = 0; k < 100; k++) if (dn_frame() >= 0 || notice_current()) { DN_FAIL("D4: a notice over the welcome"); setup_cancel(&T); return; }
    dn_welcome_done();
    if (setup_active() || progression_newborn() >= 0) { DN_FAIL("D4: could not finish the welcome"); setup_cancel(&T); return; }
    n = dn_next(10.0f, &cue);
    if (!n || n->kind != NOTICE_TANK_MILESTONE || n->bit != TMS_TRIO || cue != SND_MILESTONE) { DN_FAIL("D4: its A TRIO badge did not follow the welcome with the chime (kind %d cue %d)", n ? n->kind : -1, cue); return; }
    printf("dark-notices: D4 PASS - a fry home in the dark: 23 s with no welcome and the light off, LOW BATTERY shown; the light on: the welcome %d frame(s) later, then A TRIO with its chime\n", opened + 1);
}
static void d5_lit_as_ever(void) {
    dn_fresh(505); dn_light(true); dn_quiet(1.0f, "D5 (lit, nothing earned)"); dn_drain();
    int s = dn_fry_home(98);
    int came = dn_frame();
    if (came >= 0) { DN_FAIL("D5: a notice before the welcome"); return; }
    if (g_born != s || !setup_is_birth()) { DN_FAIL("D5: a fry home in a lit tank: no welcome that frame (%d)", g_born); return; }
    dn_frame();
    if (T.night || !T.hold_light) { DN_FAIL("D5: the welcome does not hold the light"); return; }
    dn_light(false);                                                 /* (a flow up cannot go dark: hold_light) */
    for (int k = 0; k < 40; k++) dn_frame();
    if (T.night || !setup_is_birth()) { DN_FAIL("D5: the welcome up went dark or closed"); return; }
    dn_light(true); dn_welcome_done();
    int cue = -1; const notice_t *n = dn_next(10.0f, &cue);
    if (!n || n->bit != TMS_TRIO || cue != SND_MILESTONE) { DN_FAIL("D5: A TRIO did not follow the welcome"); return; }
    printf("dark-notices: D5 PASS - a fry home in a lit tank: its welcome the same frame, the light held, A TRIO after it\n");
}
static void d6_boot_dark(void) {
    dn_fresh(506); dn_light(false); dn_quiet(1.0f, "D6"); dn_drain();
    int s = dn_fry_home(99);
    if (s < 0) { DN_FAIL("D6: no fry"); return; }
    progression_save(&T);
    dn_boot(506);                                                    /* the power off and on, the light left off */
    if (T.night) { DN_FAIL("D6: (the test) night is computed at a boot now"); return; }
    if (!T.light_manual_off || progression_newborn() != s) { DN_FAIL("D6: the save lost the light off (%d) or the welcome owed (%d)", T.light_manual_off, progression_newborn()); return; }
    notice_updated();                                                /* a boot's own card, not one the dark shows */
    if (setup_birth_due(&T)) { DN_FAIL("D6: a welcome due at a dark boot"); return; }
    notice_tick(&T, DT, false);                                      /* (before the first tank_tick) */
    if (notice_current() || notice_take_cue() >= 0) { DN_FAIL("D6: a card at a dark boot before the first tick"); return; }
    if (setup_poll_birth(&T) >= 0 || setup_active()) { DN_FAIL("D6: the welcome opened at a dark boot"); setup_cancel(&T); return; }
    if (!dn_quiet(10.0f, "D6 (after the boot)")) return;
    dn_light(true);
    int opened = -1; for (int k = 0; k < 40 && opened < 0; k++) { if (dn_frame() >= 0) { DN_FAIL("D6: a card before the welcome"); return; } if (g_born >= 0) opened = k; }
    if (g_born != s) { DN_FAIL("D6: the light on after the boot, and no welcome"); return; }
    dn_welcome_done();
    int cue = -1; const notice_t *n = dn_next(10.0f, &cue);
    if (!n || n->kind != NOTICE_UPDATED) { DN_FAIL("D6: the boot's held card never came (kind %d)", n ? n->kind : -1); return; }
    printf("dark-notices: D6 PASS - a boot into a dark tank: the welcome and the boot's card wait; the light on: the welcome, then the card\n");
}
static void d7_bought(void) {
    dn_fresh(507); dn_light(true);
    progression_force_arrival(&T); progression_tick(&T, 0.1f);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    dn_calm(); notice_tick(&T, DT, true);
    setup_poll_birth(&T);
    if (T.n_fish < 3 || !progression_sell_fish(&T, 2)) { DN_FAIL("D7: (the test) no sale (%d fish)", T.n_fish); return; }
    T.sd_balance = 1000; dn_drain();
    dn_light(false); if (!dn_quiet(2.0f, "D7")) return;
    if (!progression_buy_fry(&T, 0)) { DN_FAIL("D7: (the test) the fry was not sold"); return; }
    int s = progression_newborn();
    dn_frame();
    if (g_born != s || s < 0 || !setup_is_birth()) { DN_FAIL("D7: a fry bought in the dark: no welcome that frame (%d, owed %d)", g_born, s); return; }
    dn_welcome_done(); dn_drain();
    printf("dark-notices: D7 PASS - a fry bought in a dark tank's shop: its welcome at once\n");
}
/* a full queue held in the dark, by notice_tick alone: three tank badges, UPDATED cards, and the NEWEST a fish's badge */
static bool dn_fill(const char *who) {
    T.tank_ms_bits |= TMS_FIRST_TRIM | TMS_FIRST_CLEANING | TMS_FIRST_FEEDING;
    notice_tick(&T, DT, false);
    for (int k = 0; k < NOTICE_QUEUE - 4; k++) notice_updated();
    T.fish[0].ms_bits |= MS_FIRST_BUBBLES; notice_tick(&T, DT, false);
    if (notice_pending() != NOTICE_QUEUE || notice_current()) { DN_FAIL("%s: (the test) %d queued, the queue is %d", who, notice_pending(), NOTICE_QUEUE); return false; }
    return true;
}
/* the light on: what comes out, in order (each with its chime unless quiet_first) */
typedef struct { int n, tank, updated, fish_badge, other, first_kind; uint32_t first_bit, fish_bits; int first_cue, late_quiet; } out_t;
static out_t dn_out(void) {
    out_t o; memset(&o, 0, sizeof o); o.first_kind = -1;
    dn_light(true); T.night = false;
    bool was = false;
    for (int k = 0; k < 8000 && (notice_current() || notice_pending()); k++) {
        notice_tick(&T, DT, false);
        const notice_t *c = notice_current(); int cue = notice_take_cue();
        if (c && !was) {
            if (!o.n) { o.first_kind = c->kind; o.first_bit = c->bit; o.first_cue = cue; } else if (cue < 0) o.late_quiet++;
            o.n++;
            if (c->kind == NOTICE_TANK_MILESTONE) o.tank++; else if (c->kind == NOTICE_UPDATED) o.updated++; else if (c->kind == NOTICE_MILESTONE) { o.fish_badge++; o.fish_bits |= c->bit; } else o.other++;
        }
        was = c != NULL;
        if (c && c->since > 0.2f) { notice_dismiss(); was = false; }
    }
    return o;
}
static void d8_full_queue(void) {
    dn_fresh(508); dn_light(false); dn_quiet(1.0f, "D8"); dn_drain();
    if (!dn_fill("D8")) return;
    notice_low_battery();
    if (notice_pending() != NOTICE_QUEUE) { DN_FAIL("D8: the queue is %d long", notice_pending()); return; }
    const notice_t *n = NULL;
    for (int k = 0; k < 60 && !n; k++) { notice_tick(&T, DT, false); n = notice_current(); }
    if (!n || n->kind != NOTICE_LOW_BATTERY || notice_take_cue() != SND_LOW_BATTERY) { DN_FAIL("D8: LOW BATTERY lost behind a full queue of held notices"); return; }
    for (int k = 0; k < 8; k++) notice_tick(&T, DT, false);
    notice_dismiss();
    out_t o = dn_out();
    if (o.n != NOTICE_QUEUE - 1 || o.first_kind != NOTICE_TANK_MILESTONE || o.first_bit != TMS_FIRST_FEEDING || o.first_cue != SND_MILESTONE || o.late_quiet)
        { DN_FAIL("D8: %d came after the light (want %d), the first kind %d bit %x cue %d, %d without a chime", o.n, NOTICE_QUEUE - 1, o.first_kind, (unsigned)o.first_bit, o.first_cue, o.late_quiet); return; }
    if (o.fish_badge || o.fish_bits || o.tank != 3 || o.updated != NOTICE_QUEUE - 4 || o.other) { DN_FAIL("D8: the one that gave its place is not the newest held one: out came %d tank, %d UPDATED, %d fish badge (want 3, %d, 0)", o.tank, o.updated, o.fish_badge, NOTICE_QUEUE - 4); return; }
    printf("dark-notices: D8 PASS - a full queue of %d held in the dark: LOW BATTERY still shown, the NEWEST held one (the fish's badge) gave it its place; the other %d came with the light, the oldest first\n", NOTICE_QUEUE, o.n);
}
/* D9: LIGHTS OUT on AUTO at 5 s */
static void d9_auto_five(void) {
    dn_fresh(509); T.light_auto = true; T.light_manual_off = false; T.light_idle_s = 5;
    tank_handled(&T); dn_drain();
    for (int k = 0; k < 140; k++) dn_frame();                        /* 7 s still: the light off */
    if (!T.night) { DN_FAIL("D9: (the test) AUTO at 5 s did not go dark"); return; }
    progression_set_age(&T, 1, (float)STAGE_JUV_AGE + 60);
    dn_quiet(0.5f, "D9"); T.fish[0].ms_bits |= MS_FIRST_BUBBLES;
    dn_quiet(0.5f, "D9"); T.tank_ms_bits |= TMS_FIRST_TRIM;
    if (!dn_quiet(1.0f, "D9")) return;
    int held = notice_pending();
    if (held != 3) { DN_FAIL("D9: (the test) %d held", held); return; }
    int ups[8] = {0}, wakes = 0, drained_at = -1, n_chimed = 0; notice_t chimed[16];
    for (int k = 0; k < 20 * 80 && drained_at < 0; k++) {             /* 80 s: picked up every 8 s, the light on 5 of them */
        if (k % 160 == 0) { tank_handled(&T); wakes++; }
        int came = dn_frame();
        if (came >= 0 && came < 8) ups[came]++;
        if (g_cue >= 0 && g_up) {
            for (int c = 0; c < n_chimed; c++) if (chimed[c].kind == g_up->kind && chimed[c].bit == g_up->bit && chimed[c].fish == g_up->fish) { DN_FAIL("D9: a card chimed twice (kind %d bit %x fish %d)", g_up->kind, (unsigned)g_up->bit, g_up->fish); return; }
            if (n_chimed < 16) chimed[n_chimed++] = *g_up;
        }
        if (!notice_current() && !notice_pending()) drained_at = wakes;
    }
    if (drained_at < 0) { DN_FAIL("D9: AUTO at 5 s: the queue never drained in 10 wakes (%d of %d still held; the stage card came up %d times, a fish's badge %d, a tank badge %d)", notice_pending(), held, ups[NOTICE_STAGE], ups[NOTICE_MILESTONE], ups[NOTICE_TANK_MILESTONE]); return; }
    if (!ups[NOTICE_STAGE] || !ups[NOTICE_MILESTONE] || !ups[NOTICE_TANK_MILESTONE]) { DN_FAIL("D9: a card was never shown (%d %d %d)", ups[NOTICE_STAGE], ups[NOTICE_MILESTONE], ups[NOTICE_TANK_MILESTONE]); return; }
    if (n_chimed != 3 || drained_at > 4) { DN_FAIL("D9: three held, %d cards chimed in %d wakes (want 3, a card a wake)", n_chimed, drained_at); return; }
    printf("dark-notices: D9 PASS - AUTO at 5 s, three held, picked up every 8 s: all shown in %d wakes, one chime each\n", drained_at);
}
/* D10: back from the dark for the rest of its time */
static void d10_rest_of_its_time(void) {
    dn_fresh(510); dn_light(true); dn_quiet(0.5f, "D10 (lit, nothing earned)"); dn_drain();
    progression_set_age(&T, 1, (float)STAGE_JUV_AGE + 60);
    int came = -1, cue = -1; for (int k = 0; k < 60 && came < 0; k++) { came = dn_frame(); cue = g_cue; }
    if (came != NOTICE_STAGE || cue != SND_STAGE_UP) { DN_FAIL("D10: (the test) no stage card (kind %d cue %d)", came, cue); return; }
    for (int k = 0; k < 50; k++) dn_frame();                         /* 2.5 s up */
    dn_light(false); dn_frame();
    if (notice_current() || notice_pending() != 1) { DN_FAIL("D10: it did not step aside for the dark"); return; }
    if (!dn_quiet(3.0f, "D10")) return;
    int saves0 = g_sim_tank_saves;
    dn_light(true);
    came = -1; cue = -1; for (int k = 0; k < 60 && came < 0; k++) { came = dn_frame(); cue = g_cue; }
    if (came != NOTICE_STAGE || cue >= 0 || !notice_came_back()) { DN_FAIL("D10: it did not come back quiet (kind %d cue %d back %d)", came, cue, notice_came_back()); return; }
    if (!notice_dismiss_held(0.3f) || !notice_current()) { DN_FAIL("D10: a press begun before it came back closed it"); return; }
    for (int k = 0; k < 20; k++) dn_frame();                         /* 1 s back: 3.5 s shown in all */
    if (g_sim_tank_saves != saves0) { DN_FAIL("D10: the stage card back from the dark was saved again (%d saves)", g_sim_tank_saves - saves0); return; }
    if (!notice_current()) { DN_FAIL("D10: gone 1 s after it came back"); return; }
    for (int k = 0; k < 30; k++) dn_frame();                         /* 2.5 s back: 5 s in all - still up */
    if (!notice_current()) { DN_FAIL("D10: gone after 5 s shown in all (its time is %.0f s)", (double)NOTICE_UP_S); return; }
    for (int k = 0; k < 34; k++) dn_frame();                         /* 4.2 s back: 6.7 s in all */
    if (notice_current()) { DN_FAIL("D10: still up 4.2 s after it came back with 2.5 s already shown: it started its %.0f s over", (double)NOTICE_UP_S); return; }
    /* ... one that had NOTICE_SEEN_S of its time is done: it does not come back */
    dn_drain(); T.tank_ms_bits |= TMS_FIRST_TRIM;
    came = -1; for (int k = 0; k < 60 && came < 0; k++) came = dn_frame();
    if (came != NOTICE_TANK_MILESTONE) { DN_FAIL("D10: (the test) no tank badge"); return; }
    for (int k = 0; k < (int)(NOTICE_SEEN_S / DT) + 6; k++) dn_frame();
    if (!notice_current()) { DN_FAIL("D10: (the test) the badge left before the light went off"); return; }
    dn_light(false); dn_frame();
    if (notice_current() || notice_pending()) { DN_FAIL("D10: a card shown %.1f s when the light went off was kept for another showing (%d queued)", (double)NOTICE_SEEN_S + 0.3, notice_pending()); return; }
    dn_light(true);
    { int c2; const notice_t *n = dn_next(3.0f, &c2); if (n) { DN_FAIL("D10: a card already read came back"); return; } }
    /* ... and from under a PAGE a stage card comes back whole, as ever - but is not saved a second time */
    progression_set_age(&T, 0, (float)STAGE_JUV_AGE + 60);
    came = -1; for (int k = 0; k < 60 && came < 0; k++) came = dn_frame();
    if (came != NOTICE_STAGE) { DN_FAIL("D10: (the test) no second stage card"); return; }
    for (int k = 0; k < 50; k++) dn_frame();
    saves0 = g_sim_tank_saves;
    g_page = true; dn_frame(); dn_frame();
    if (notice_current() || notice_pending() != 1) { DN_FAIL("D10: under a page it did not step aside"); g_page = false; return; }
    g_page = false;
    came = -1; cue = -1; for (int k = 0; k < 60 && came < 0; k++) { came = dn_frame(); cue = g_cue; }
    if (came != NOTICE_STAGE || cue >= 0 || g_up->age != 0) { DN_FAIL("D10: back from under a page: kind %d cue %d age %.2f (want the stage card, quiet, whole)", came, cue, g_up ? (double)g_up->age : -1.0); return; }
    for (int k = 0; k < 10; k++) dn_frame();
    if (g_sim_tank_saves != saves0) { DN_FAIL("D10: the stage card back from under a page was saved again (%d saves)", g_sim_tank_saves - saves0); return; }
    for (int k = 0; k < 90; k++) dn_frame();                         /* 5 s back */
    if (!notice_current()) { DN_FAIL("D10: back from under a page it did not get its whole time"); return; }
    dn_drain();
    printf("dark-notices: D10 PASS - a stage card 2.5 s up when the light went off: back quiet, not saved again, a press from before it is spent, gone after the rest of its %.0f s; one shown %.0f s is done; from under a page: whole, as ever, and not saved again\n", (double)NOTICE_UP_S, (double)NOTICE_SEEN_S);
}
/* D11: the light off under a card that is up, the queue full */
static void d11_full_step_aside(void) {
    dn_fresh(511); dn_light(false); dn_quiet(1.0f, "D11"); dn_drain();
    if (!dn_fill("D11")) return;
    dn_light(true); T.night = false;
    const notice_t *n = NULL;
    for (int k = 0; k < 60 && !n; k++) { notice_tick(&T, DT, false); n = notice_current(); }
    if (!n || n->bit != TMS_FIRST_FEEDING || notice_take_cue() != SND_MILESTONE) { DN_FAIL("D11: (the test) the head did not come up"); return; }
    for (int k = 0; k < 20; k++) notice_tick(&T, DT, false);         /* 1 s up */
    T.fish[1].ms_bits |= MS_FIRST_DART; notice_tick(&T, DT, false);  /* one more earned: the queue full again under it, this the newest */
    if (notice_pending() != NOTICE_QUEUE) { DN_FAIL("D11: (the test) %d queued under the card", notice_pending()); return; }
    dn_light(false); T.night = true;
    notice_tick(&T, DT, false);
    if (notice_current() || notice_pending() != NOTICE_QUEUE) { DN_FAIL("D11: the light off: up %d, %d queued", notice_current() != NULL, notice_pending()); return; }
    out_t o = dn_out();
    if (o.first_kind != NOTICE_TANK_MILESTONE || o.first_bit != TMS_FIRST_FEEDING) { DN_FAIL("D11: the card that was up is LOST: a full queue had no place for it (first out kind %d bit %x)", o.first_kind, (unsigned)o.first_bit); return; }
    if (o.first_cue >= 0) { DN_FAIL("D11: it chimed again"); return; }
    if (o.n != NOTICE_QUEUE || o.tank != 3 || o.updated != NOTICE_QUEUE - 4 || o.fish_badge != 1 || o.fish_bits != MS_FIRST_BUBBLES || o.late_quiet)
        { DN_FAIL("D11: out came %d: %d tank, %d UPDATED, %d fish badge (bits %x), %d quiet (want %d: 3, %d, 1 (bits %x), 0 - the NEWEST held one gives its place)", o.n, o.tank, o.updated, o.fish_badge, (unsigned)o.fish_bits, o.late_quiet, NOTICE_QUEUE, NOTICE_QUEUE - 4, (unsigned)MS_FIRST_BUBBLES); return; }
    printf("dark-notices: D11 PASS - the light off under a card with %d queued: the card kept and back first, quiet; the newest held one (the last fish's badge) gave its place\n", NOTICE_QUEUE);
}
static void d12_override(void) {
    dn_fresh(512); dn_light(false); dn_quiet(1.0f, "D12"); dn_drain();
    T.tank_ms_bits |= TMS_FIRST_TRIM;
    if (!dn_quiet(2.0f, "D12")) return;
    T.light_override = true; T.light_on = true;                      /* the director's light ON over the double-tap's off */
    int cue = -1; const notice_t *n = dn_next(3.0f, &cue);
    if (!n || n->bit != TMS_FIRST_TRIM || cue != SND_MILESTONE) { DN_FAIL("D12: the director's light on: the held badge did not come (kind %d cue %d)", n ? n->kind : -1, cue); return; }
    dn_drain();
    T.light_manual_off = false; T.light_on = false;                  /* ... and OFF over a double-tap's on */
    dn_frame();
    T.fish[0].ms_bits |= MS_FIRST_BUBBLES;
    if (!dn_quiet(3.0f, "D12 (the director's light off)") || notice_pending() != 1) { DN_FAIL("D12: the director's light off did not hold (%d held)", notice_pending()); return; }
    tank_light_auto(&T);                                             /* the override ends: the keeper's light, on */
    n = dn_next(3.0f, &cue);
    if (!n || n->kind != NOTICE_MILESTONE || cue != SND_MILESTONE) { DN_FAIL("D12: the override over: the held badge did not come"); return; }
    printf("dark-notices: D12 PASS - the director's light is the light: on over a double-tap's off shows, off holds, handed back it shows\n");
}
static void d13_fish_removed(void) {
    dn_fresh(513); dn_light(true);
    progression_force_arrival(&T); progression_tick(&T, 0.1f);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    dn_calm(); notice_tick(&T, DT, true);
    setup_poll_birth(&T); dn_drain();
    if (T.n_fish != 3) { DN_FAIL("D13: (the test) %d fish", T.n_fish); return; }
    dn_light(false); dn_quiet(1.0f, "D13"); dn_drain();
    T.fish[1].ms_bits |= MS_FIRST_BUBBLES; T.fish[2].ms_bits |= MS_FIRST_DART;
    if (!dn_quiet(1.0f, "D13") || notice_pending() != 2) { DN_FAIL("D13: (the test) %d held", notice_pending()); return; }
    uint32_t keep = T.fish[2].uid;
    if (!progression_sell_fish(&T, 1)) { DN_FAIL("D13: (the test) the sale was refused"); return; }
    if (!dn_quiet(2.0f, "D13 (after the sale)")) return;
    dn_light(true);
    int dart = 0, bubbles = 0, n_out = 0;
    for (int k = 0; k < 6; k++) {
        int cue; const notice_t *n = dn_next(4.0f, &cue); if (!n) break;
        n_out++;
        if (n->kind == NOTICE_MILESTONE && n->bit == MS_FIRST_BUBBLES) bubbles++;
        if (n->kind == NOTICE_MILESTONE && n->bit == MS_FIRST_DART) { dart++; if (n->fish != 1 || T.fish[1].uid != keep) { DN_FAIL("D13: the badge held for the fish after the sold one names slot %d", n->fish); return; } }
    }
    if (bubbles || dart != 1) { DN_FAIL("D13: the sold fish's held badge came %d time(s), the other fish's %d (want 0, 1; %d out)", bubbles, dart, n_out); return; }
    printf("dark-notices: D13 PASS - a fish sold in the dark with its badge held: that notice gone, the next fish's followed it down a slot\n");
}
static void d14_sync_drops(void) {
    dn_fresh(514); dn_light(false); dn_quiet(1.0f, "D14"); dn_drain();
    T.tank_ms_bits |= TMS_FIRST_TRIM; T.fish[0].ms_bits |= MS_FIRST_BUBBLES;
    if (!dn_quiet(1.0f, "D14") || notice_pending() != 2) { DN_FAIL("D14: (the test) %d held", notice_pending()); return; }
    notice_sync(&T);                                                 /* a reset, a MY TANKS swap, a boot */
    if (notice_pending()) { DN_FAIL("D14: notice_sync kept %d held notice(s): the behavior changed (pinned: dropped)", notice_pending()); return; }
    dn_light(true);
    int cue; const notice_t *n = dn_next(3.0f, &cue);
    if (n) { DN_FAIL("D14: a notice held before the sync came after it (kind %d)", n->kind); return; }
    printf("dark-notices: D14 PASS - (pinned) notice_sync drops what the dark held, as it drops what a page held: their rings are on the page\n");
}

/* D15: SELL / GIVE wait on a fry not yet named - dark, the line says the light first (its welcome waits for it) */
static void d15_fry_line(void) {
    dn_fresh(515); dn_light(false);
    if (!dn_quiet(0.5f, "D15")) return;
    const char *dark = render_fry_first_line(&T);
    if (strcmp(dark, "LIGHT ON TO NAME THIS FRY")) { DN_FAIL("D15: dark, the line is \"%s\"", dark); return; }
    if (strlen(dark) > 26) { DN_FAIL("D15: \"%s\" is wider than the fish card's line (26 letters)", dark); return; }
    dn_light(true); dn_drain();
    const char *lit = render_fry_first_line(&T);
    if (T.night || strcmp(lit, "NAME THIS FRY FIRST")) { DN_FAIL("D15: lit (night %d), the line is \"%s\"", T.night, lit); return; }
    printf("dark-notices: D15 PASS - a fry not yet named: \"%s\" in the dark, \"%s\" in the light\n", dark, lit);
}

/* D16 (Strato on the watch, 11-10): a fry waiting for the light holds ITSELF, never the other fish. Another fish is
   sold in the dark, the welcome owed follows its fry down a slot, and the light opens it for that fry */
static void d16_fry_holds_itself(void) {
    dn_fresh(516); dn_light(false); dn_quiet(1.0f, "D16"); dn_drain();
    while (T.n_fish < 3 && progression_fry_room(&T)) { progression_force_arrival(&T); while (progression_newborn() >= 0) progression_newborn_done(&T); }
    int s = dn_fry_home(98);
    if (s < 1 || T.n_fish < 4 || progression_newborn() != s) { DN_FAIL("D16: (the test) no fry home owed its welcome above another fish (slot %d of %d, owed %d)", s, T.n_fish, progression_newborn()); return; }
    if (!dn_quiet(2.0f, "D16 (a fry home)")) return;
    const uint32_t fuid = T.fish[s].uid;
    if (!progression_welcome_owed(&T, s) || progression_fish_sellable(&T, s) || progression_fish_givable(&T, s)) { DN_FAIL("D16: a fry not yet welcomed may be sold or given"); return; }
    int o = -1; for (int i = 0; i < s && o < 0; i++) if (!T.fish[i].away) o = i;
    if (o < 0) { DN_FAIL("D16: (the test) no fish at home below the fry"); return; }
    if (progression_welcome_owed(&T, o) || !progression_fish_sellable(&T, o) || !progression_fish_givable(&T, o)) { DN_FAIL("D16: a fry waiting for the light holds another fish's SELL or GIVE (sellable %d, givable %d)", progression_fish_sellable(&T, o), progression_fish_givable(&T, o)); return; }
    const int n0 = T.n_fish;
    if (!progression_sell_fish(&T, o) || T.n_fish != n0 - 1) { DN_FAIL("D16: the other fish did not sell (%d fish, were %d)", T.n_fish, n0); return; }
    int nb = progression_newborn();
    if (nb != s - 1 || T.fish[nb].uid != fuid) { DN_FAIL("D16: the welcome owed did not follow its fry (owed slot %d, the fry was %d)", nb, s); return; }
    if (!dn_quiet(2.0f, "D16 (after the sale)")) return;
    dn_light(true);
    int opened = -1;
    for (int k = 0; k < 40 && opened < 0; k++) { dn_frame(); if (g_born >= 0) opened = g_born; }
    if (opened != nb || !setup_is_birth() || T.fish[opened].uid != fuid) { DN_FAIL("D16: the light on, and the welcome is not the fry's (slot %d, want %d)", opened, nb); setup_cancel(&T); return; }
    dn_welcome_done();
    if (setup_active() || progression_newborn() >= 0 || progression_welcome_owed(&T, nb)) { DN_FAIL("D16: the welcome did not finish (owed %d)", progression_newborn()); setup_cancel(&T); return; }
    printf("dark-notices: D16 PASS - a fry waiting for the light holds itself only: another fish sold in the dark, the welcome followed its fry\n");
}

/* T1 (audit T1-F1): the audit's probe Q1 (r-tables-T1T2/t1t2probe.inc), as a test */
static int s_saves;                                              /* (every write of the tank's save: g_sim_tank_saves) */
static void t1_back(int i, float age_s, uint32_t badge) {        /* fish i comes home by the door, with what it lived next door */
    fish_t there = T.fish[i];
    there.ms_bits |= badge; there.away = false;
    passport_t pp; passport_from_fish(&pp, &there, T.uid, age_s);
    link_ev_t e; memset(&e, 0, sizeof e);
    e.type = LEV_FISH_BACK; e.uid = T.fish[i].uid; e.pp = pp; e.edge = LINK_LEFT; e.depth = 0.5f; e.speed = 30;
    tank_link_event(&T, NULL, &e);
}
/* grows: it comes home grown up (its stage card saves that tick since D-5, so the cut alone never showed the bug there:
   the pin is that the stage is paid once); else home with a badge and the time it lived there, no card that saves */
static void t1_home_saved(bool grows) {
    const char *who = grows ? "T1 (grown up there)" : "T1";
    dn_fresh(grows ? 403 : 401); dn_light(true);
    progression_set_age(&T, 0, (float)STAGE_ADULT_AGE - (grows ? 60 : 900));   /* (LK-1: never older than it has lived + 2 min) */
    progression_tick(&T, DT); dn_drain();
    T.fish[0].away = true; T.fish[0].door_edge = LINK_LEFT;
    progression_save(&T);                                         /* the save has it next door */
    uint32_t uid = T.fish[0].uid; int st0 = T.fish[0].stage;
    int saves0 = g_sim_tank_saves;
    t1_back(0, (float)STAGE_ADULT_AGE - (grows ? -50.0f : 800.0f), MS_FIRST_BUBBLES);
    tank_link_tick(&T, NULL);                                     /* (link_dev_poll: the events, then the tick) */
    s_saves = g_sim_tank_saves - saves0;
    for (int k = 0; k < 100; k++) dn_frame();                     /* 5 s: the keeper sees it home, its card, its pay */
    int32_t seen = T.sd_balance; int st1 = T.fish[0].stage; float age1 = progression_age_s(&T, 0);
    if (T.fish[0].away || (st1 > st0) != grows || !(T.fish[0].ms_bits & MS_FIRST_BUBBLES)) { DN_FAIL("%s: (the test) home: away %d stage %d (was %d) bubbles %d", who, T.fish[0].away, st1, st0, !!(T.fish[0].ms_bits & MS_FIRST_BUBBLES)); return; }
    dn_boot(grows ? 403 : 401);                                                 /* the power gone 5 s later, inside the 30 s coalesce */
    int s = -1; for (int i = 0; i < T.n_fish; i++) if (T.fish[i].uid == uid) s = i;
    if (s < 0) { DN_FAIL("%s: the fish is gone after the cut", who); return; }
    if (T.fish[s].away) { DN_FAIL("%s: after the cut the save says it is next door again (home from a visit was not saved at once)", who); return; }
    if ((int)T.fish[s].stage != st1 || !(T.fish[s].ms_bits & MS_FIRST_BUBBLES) || progression_age_s(&T, s) < age1 - 10)
        { DN_FAIL("%s: the cut took back stage %d -> %d, bubbles %d, age %.0f -> %.0f", who, st1, T.fish[s].stage, !!(T.fish[s].ms_bits & MS_FIRST_BUBBLES), (double)age1, (double)progression_age_s(&T, s)); return; }
    for (int k = 0; k < 20; k++) dn_frame();
    if (T.sd_balance < seen) { DN_FAIL("%s: the wallet after the cut %d, the keeper saw %d", who, (int)T.sd_balance, (int)seen); return; }
    if (T.sd_balance > seen) { DN_FAIL("%s: the cut paid the visit's stage twice (%d, the keeper saw %d)", who, (int)T.sd_balance, (int)seen); return; }
    if (s_saves != 1) { DN_FAIL("%s: %d saves for one fish home", who, s_saves); return; }
    printf("dark-notices: %s PASS - home from a visit, cut 5 s later: home, stage %d (was %d), its badge, its age %.0f s and the wallet %d kept; one save\n", who, st1, st0, (double)age1, (int)seen);
}
static void t1_one_save(void) {
    dn_fresh(402); dn_light(true);
    progression_force_arrival(&T); progression_tick(&T, 0.1f);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    dn_calm(); notice_tick(&T, DT, true);
    setup_poll_birth(&T);
    if (T.n_fish < 3) { DN_FAIL("T1b: (the test) %d fish", T.n_fish); return; }
    for (int i = 0; i < 3; i++) { T.fish[i].away = true; T.fish[i].door_edge = LINK_LEFT; }
    progression_save(&T);
    int saves0 = g_sim_tank_saves;
    for (int i = 0; i < 3; i++) t1_back(i, 100.0f, 0);
    if (g_sim_tank_saves != saves0) { DN_FAIL("T1b: %d save(s) before the tick for three fish home", g_sim_tank_saves - saves0); return; }
    tank_link_tick(&T, NULL);
    int at_once = g_sim_tank_saves - saves0;
    for (int k = 0; k < 40; k++) { tank_link_tick(&T, NULL); dn_frame(); }
    s_saves = g_sim_tank_saves - saves0;
    if (at_once != 1 || s_saves != 1) { DN_FAIL("T1b: three fish home in one tick: %d save(s) at once, %d after 2 s (want 1 and 1)", at_once, s_saves); return; }
    dn_boot(402);
    for (int i = 0; i < 3; i++) if (T.fish[i].away) { DN_FAIL("T1b: fish %d next door again after the cut", i); return; }
    printf("dark-notices: T1b PASS - three of ours home in one tick: one save, all three home after a cut\n");
}
/* T1c: a fry of ours and a grown one home in the same tick - the grown one first, the fry first, and both from the boot's
   restore with the fry in the lower slot: ONE save each time, and a cut after it finds both home, the welcome owed */
static void t1_fry_and_fish(int order) {
    static const char *const how[3] = { "the grown fish first", "the fry first", "the boot's restore, the fry in the lower slot" };
    dn_fresh(404 + (uint32_t)order); dn_light(true); dn_drain();
    passport_t fp = dn_fry_next_door(96);
    int fs = progression_fry_adopt(&T, &fp, LINK_LEFT), g = 0;
    if (fs < 0) { DN_FAIL("T1c: (the test) no room for the fry"); return; }
    if (order == 2) {                                             /* a grown fish in a slot after the fry's */
        progression_force_arrival(&T); progression_tick(&T, 0.1f);
        while (progression_newborn() >= 0) progression_newborn_done(&T);
        setup_poll_birth(&T);
        g = T.n_fish - 1;
        if (g <= fs) { DN_FAIL("T1c (%s): (the test) no slot after the fry's (%d fish, fry %d)", how[order], T.n_fish, fs); return; }
    }
    uint32_t guid = T.fish[g].uid, fuid = T.fish[fs].uid;
    T.fish[g].away = true; T.fish[g].door_edge = LINK_LEFT;
    progression_save(&T);
    int saves0;
    if (order == 2) {
        dn_boot(404 + (uint32_t)order);
        if (T.link_restore_at <= 0 || !T.fish[g].away || !T.fish[fs].away) { DN_FAIL("T1c (%s): (the test) the boot found them home", how[order]); return; }
        T.clock = T.link_restore_at;
        saves0 = g_sim_tank_saves;
        tank_link_tick(&T, NULL);
    } else {
        link_ev_t e; memset(&e, 0, sizeof e); e.type = LEV_FISH_BACK; e.uid = fuid; e.pp = fp; e.edge = LINK_LEFT; e.depth = 0.5f; e.speed = 30;
        saves0 = g_sim_tank_saves;
        if (order == 0) t1_back(g, 100.0f, 0);
        tank_link_event(&T, NULL, &e);
        if (order == 1) t1_back(g, 100.0f, 0);
        tank_link_tick(&T, NULL);
    }
    int n = g_sim_tank_saves - saves0;
    tank_link_tick(&T, NULL);
    if (n != 1 || g_sim_tank_saves - saves0 != 1) { DN_FAIL("T1c (%s): %d save(s) in the tick, %d after the next (want 1 and 1)", how[order], n, g_sim_tank_saves - saves0); return; }
    int owed = progression_newborn();
    dn_boot(404 + (uint32_t)order);
    int gi = -1, fi = -1; for (int i = 0; i < T.n_fish; i++) { if (T.fish[i].uid == guid) gi = i; if (T.fish[i].uid == fuid) fi = i; }
    if (gi < 0 || fi < 0 || T.fish[gi].away || T.fish[fi].away || T.fish[fi].born_away || progression_newborn() != owed || owed != fs)
        { DN_FAIL("T1c (%s): after the cut: fish away %d, fry away %d born_away %d, welcome owed %d (was %d, the fry is %d)", how[order], gi < 0 ? -1 : T.fish[gi].away, fi < 0 ? -1 : T.fish[fi].away, fi < 0 ? -1 : T.fish[fi].born_away, progression_newborn(), owed, fs); return; }
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    printf("dark-notices: T1c PASS - a fry and a grown fish of ours home in one tick (%s): one save, both home after a cut\n", how[order]);
}
int selftest_dark_notices(void) {
    d1_holds(); d2_steps_aside(); d3_exempt(); d4_welcome_waits(); d5_lit_as_ever(); d6_boot_dark(); d7_bought(); d8_full_queue();
    d9_auto_five(); d10_rest_of_its_time(); d11_full_step_aside(); d12_override(); d13_fish_removed(); d14_sync_drops(); d15_fry_line(); d16_fry_holds_itself();
    t1_home_saved(false); t1_home_saved(true); t1_one_save(); t1_fry_and_fish(0); t1_fry_and_fish(1); t1_fry_and_fish(2);
    setup_cancel(&T);
    if (dn_bad) { printf("dark-notices: FAILED (%d)\n", dn_bad); return 2; }
    printf("dark-notices: PASS\n");
    return 0;
}
