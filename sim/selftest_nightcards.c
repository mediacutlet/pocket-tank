#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_nightcards.c - --selftest-night-cards (0.4.2, audit round 2 PW2-2 + PW2-3; Strato 11-13: "save the owed cards
 * until the light is on"). OWED CARDS (notice.h): what the dark held - the night's FIRST FULL NIGHT'S SLEEP and "IS NOW A
 * ..." cards on a morning that boots dark (LIGHTS OUT MANUAL, the light left off), a card earned awake in the dark - is
 * saved with the tank and said once the light is on, whatever sleeps and restarts come between. Boots in FIRMWARE main.c's
 * order (as selftest_night_badge.c: shelf_boot, progression_boot, settle, swap carry, notice_sync, UPDATED) and runs the
 * frames as the tank task does. A "sleep" is the key's save and RAM gone; a "cut" is RAM gone with no save.
 *
 *   C1  (PW2-2, the red test) the light off at bedtime, a 9 h night (a fish grows up, the tank's first full night): the
 *       morning boots dark, nothing said; slept again without the light; the next boot, light on: the badge and the stage
 *       card, each once. Then another boot: nothing (once and only once)
 *   C2  (PW2-3) earned awake in the dark (a fish badge, a tank badge, a stage), slept, two dark boots (a restart between):
 *       said at the lit boot, each once; the boot after: nothing
 *   C3  a power cut: after the save, before the light - still owed; once a card is up - never said again; between earning
 *       and saving - nothing on the flash, nothing said (the badge is not on the flash either). (A cut inside the one frame
 *       between a badge earned and the diff that queues it, with a save in that frame, is not covered: as before 0.4.2)
 *   C4  a full queue: every badge a tank and its fish could owe at once - no card twice, at most NOTICE_QUEUE said, LOW
 *       BATTERY still shown; the boot after says nothing
 *   C5  MY TANKS: tank A owes cards and is swapped out; tank B (lit) says none of A's; A swapped back in, lit, says its own
 *   C6  an older save (2176 B, before the record) loads as before - nothing owed, its bytes saved back unchanged; a rollback
 *       (an older build's head over this record) owes nothing; a reset with cards owed: the new tank owes nothing
 *   C7  a lit tank: what is said in the session is not said again; the dark's control (as before): LOW BATTERY in the dark
 *   C8  a lit tank in play: cards queued when an ordinary save comes force no save as they come up (the normal save's 30 s
 *       gap clears them); restored from the flash at a boot (C1), each IS saved the frame it comes up
 *   (C4 checks which survive a full queue: exactly the record's, each once; C6b4 a rollback whose release and clock cancel
 *   under a plain XOR)
 * Red at 7934cb0 (the same file built there): C1, C2, C3a, C3b2, C4, C5, C6b3 - "said 0 times" - and C6a (the save is 2176 B). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tank.h"
#include "advisor.h"
#include "progression.h"
#include "notice.h"
#include "shelf.h"
#include "setup.h"
#include "sounds.h"
#include "version.h"

void sim_clock_shift(int64_t s);
void sim_clock_freeze(int on);
const char *sim_shelf_dir(void);
void render_shelf_thumb_size(int *w, int *h);

static tank_t T;
static int nc_bad;
#define NC_FAIL(...) do { printf("night-cards: FAIL: "); printf(__VA_ARGS__); printf("\n"); nc_bad++; } while (0)
#define H (3600)
static char s_dir[600];

typedef struct { uint32_t kind, uid, bit; int n, forced; } led_t;   /* forced: a save in the frame after it came up */
extern int g_sim_tank_saves;                     /* persist_port_sim.c: every write of the tank's save */   /* every card said on this board, across its boots */
#define NC_LED 96
static led_t s_led[NC_LED]; static int s_nled;
static bool s_was_up;                            /* a card up at the end of the last call is not a new one (a boot clears it) */
static void nc_board(const char *name) {
    char cmd[1400], p[700];
    snprintf(s_dir, sizeof s_dir, "%s/night-cards-%s", sim_shelf_dir(), name);
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", s_dir, s_dir); (void)system(cmd);
    snprintf(p, sizeof p, "%s/tank.sav", s_dir); setenv("POCKET_TANK_SAVE", p, 1);
    snprintf(p, sizeof p, "%s/storage.bin", s_dir); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", s_dir); setenv("POCKET_TANK_FRAME", p, 1);
    s_nled = 0;
}
static void nc_boot(uint32_t seed) {             /* firmware main.c's boot order (a cold boot: a sleep's morning lives the same span) */
    memset(&T, 0, sizeof T); tank_init(&T, seed); s_was_up = false;
    shelf_boot(clock_port_now_unix());
    (void)progression_boot(&T);
    progression_shelf_settle(&T);
    progression_swap_carry(&T);
    notice_sync(&T);
    uint32_t r = progression_loaded_release();
    if (r && r < (uint32_t)PT_RELEASE_NUM) notice_updated();
}
typedef struct { int night, stage, fish_ms, tank_ms, updated, low, other, n; } said_t;
/* a card's identity: its kind, its fish (by uid) and badge - a stage card by the stage it says */
static led_t nc_key(const notice_t *n) {
    led_t k = { (uint32_t)n->kind, 0, n->bit, 0, 0 };
    if (n->fish >= 0 && n->kind != NOTICE_TANK_MILESTONE) k.uid = T.fish[n->fish].uid;
    if (n->kind == NOTICE_STAGE && n->fish >= 0) k.bit = (uint32_t)T.fish[n->fish].stage;
    return k;
}
static int nc_count(int kind, uint32_t uid, uint32_t bit) {
    for (int i = 0; i < s_nled; i++) if (s_led[i].kind == (uint32_t)kind && s_led[i].uid == uid && s_led[i].bit == bit) return s_led[i].n;
    return 0;
}
static int nc_forced(int kind, uint32_t uid, uint32_t bit) {
    for (int i = 0; i < s_nled; i++) if (s_led[i].kind == (uint32_t)kind && s_led[i].uid == uid && s_led[i].bit == bit) return s_led[i].forced;
    return 0;
}
static int nc_twice(void) { int n = 0; for (int i = 0; i < s_nled; i++) if (s_led[i].n > 1 && s_led[i].kind != NOTICE_LOW_BATTERY && s_led[i].kind != NOTICE_UPDATED) n++; return n; }
static said_t nc_frames(float secs, float boot_show_s) {
    said_t s; memset(&s, 0, sizeof s);
    const float dt = 0.05f; bool was_up = s_was_up; int just = -1;
    for (float at = 0; at < secs; at += dt) {
        tank_tick(&T, dt, advisor_rules);
        const int sv0 = g_sim_tank_saves;
        progression_tick(&T, dt);
        if (just >= 0 && g_sim_tank_saves != sv0) s_led[just].forced++;   /* (the frame after it came up: its save, or none) */
        just = -1;
        notice_tick(&T, dt, T.ui_cover || setup_active() || setup_birth_due(&T) || at < boot_show_s);
        (void)notice_take_cue();
        const notice_t *n = notice_current();
        if (n && !was_up && !notice_came_back()) {
            s.n++;
            if (n->kind == NOTICE_TANK_MILESTONE && n->bit == TMS_FIRST_FULL_NIGHT) s.night++;
            else if (n->kind == NOTICE_TANK_MILESTONE) s.tank_ms++;
            else if (n->kind == NOTICE_MILESTONE) s.fish_ms++;
            else if (n->kind == NOTICE_STAGE) s.stage++;
            else if (n->kind == NOTICE_UPDATED) s.updated++;
            else if (n->kind == NOTICE_LOW_BATTERY) s.low++;
            else s.other++;
            led_t k = nc_key(n); int i = 0;
            while (i < s_nled && !(s_led[i].kind == k.kind && s_led[i].uid == k.uid && s_led[i].bit == k.bit)) i++;
            if (i == s_nled && s_nled < NC_LED) s_led[s_nled++] = k;
            if (i < NC_LED) { s_led[i].n++; just = i; }
            if (getenv("NC_V")) printf("night-cards:      (at %.1f s: kind %d bit %05x fish %d)\n", at, n->kind, (unsigned)n->bit, n->fish);
        }
        was_up = n != NULL;
    }
    s_was_up = was_up;
    return s;
}
static void nc_show(const char *what, said_t s) {
    printf("night-cards: %-56s light %-3s | night %d stage %d fish badges %d tank badges %d updated %d low %d other %d | held %d, said twice on this board %d\n", what,
           tank_light_off(&T) ? "OFF" : "on", s.night, s.stage, s.fish_ms, s.tank_ms, s.updated, s.low, s.other, notice_pending(), nc_twice());
}
static bool nc_none(said_t s) { return !s.night && !s.stage && !s.fish_ms && !s.tank_ms; }   /* no earned card at all */
static void nc_dark(bool off) { T.light_auto = false; T.light_manual_off = off; progression_settings_changed(); }
/* a founded tank, setup done, everything it had to say said, lit; no first full night yet */
static void nc_found(uint32_t seed) {
    nc_boot(seed); progression_setup_done(&T);
    T.tank_ms_bits |= TMS_FIRST_FEEDING; T.tank_ms_seen = T.tank_ms_bits;
    nc_frames(60, 0);
    for (int k = 0; k < 120 && (notice_current() || notice_pending()); k++) nc_frames(1, 0);   /* (all of it said, none left up) */
}
static void nc_sleep(int64_t secs) { progression_save(&T); sim_clock_shift(secs); }   /* the key: the save, then RAM gone */
static long nc_read(uint8_t *b, size_t cap) {
    FILE *f = fopen(getenv("POCKET_TANK_SAVE"), "rb"); if (!f) return -1;
    long n = (long)fread(b, 1, cap, f); fclose(f); return n;
}
static void nc_write(const uint8_t *b, size_t n) {
    FILE *f = fopen(getenv("POCKET_TANK_SAVE"), "wb"); if (!f) return;
    fwrite(b, 1, n, f); fclose(f);
}

/* C1 (PW2-2): the dark morning, slept again without the light */
static void c1(void) {
    nc_board("c1"); nc_found(7101);
    progression_set_age(&T, 0, 1.0f * H);        /* due to grow in a 9 h night (a quarter of it is growth) */
    nc_dark(true); nc_frames(2, 0);
    const stage_t a0 = T.fish[0].stage; const uint32_t u0 = T.fish[0].uid;
    nc_sleep(9 * H);
    nc_boot(7102); said_t s = nc_frames(90, 4); nc_show("C1 the morning, the light left off (9 h night)", s);
    if (!(T.tank_ms_bits & TMS_FIRST_FULL_NIGHT) || T.fish[0].stage <= a0) NC_FAIL("C1: (the test) no night earned or no growth (stage %d -> %d)", a0, T.fish[0].stage);
    const uint32_t st = (uint32_t)T.fish[0].stage;
    if (!nc_none(s)) NC_FAIL("C1: the dark morning said a card (night %d stage %d)", s.night, s.stage);
    nc_sleep(1 * H);                             /* PWR again, the light never turned on */
    nc_boot(7103); s = nc_frames(20, 4); nc_show("C1 an hour later, still dark", s);
    if (!nc_none(s)) NC_FAIL("C1: a dark boot said a card");
    nc_sleep(1 * H);
    nc_boot(7104); nc_dark(false); s = nc_frames(90, 4); nc_show("C1 the next boot, the light turned on", s);
    if (nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_FULL_NIGHT) != 1 || nc_count(NOTICE_STAGE, u0, st) != 1)
        NC_FAIL("C1: the night's badge said %d time(s), fish 0's stage card %d (want 1, 1) - PW2-2", nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_FULL_NIGHT), nc_count(NOTICE_STAGE, u0, st));
    if (nc_forced(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_FULL_NIGHT) != 1 || nc_forced(NOTICE_STAGE, u0, st) != 1)   /* restored from the flash: saved as it comes */
        NC_FAIL("C1: a card restored at the boot not saved the frame it came up (badge %d, stage %d)", nc_forced(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_FULL_NIGHT), nc_forced(NOTICE_STAGE, u0, st));
    nc_sleep(1 * H);
    nc_boot(7105); s = nc_frames(40, 4); nc_show("C1 the boot after", s);
    if (nc_twice()) NC_FAIL("C1: %d card(s) said twice on this board", nc_twice());
}
/* C2 (PW2-3): earned awake in the dark */
static void c2(void) {
    nc_board("c2"); nc_found(7201);
    nc_dark(true); nc_frames(1, 0);
    const stage_t a0 = T.fish[1].stage; float age = 0.5f * H; const uint32_t u0 = T.fish[0].uid, u1 = T.fish[1].uid;
    for (int k = 0; k < 80 && T.fish[1].stage == a0; k++) { age += 0.25f * H; progression_set_age(&T, 1, age); nc_frames(0.3f, 0); }
    const uint32_t st = (uint32_t)T.fish[1].stage;
    T.fish[0].ms_bits |= MS_FIRST_BUBBLES; T.tank_ms_bits |= TMS_FIRST_REEF;
    said_t s = nc_frames(20, 0); nc_show("C2 earned awake in the dark", s);
    if (T.fish[1].stage == a0) NC_FAIL("C2: (the test) fish 1 did not grow");
    if (!nc_none(s)) NC_FAIL("C2: the dark said a card");
    nc_sleep(1 * H);
    nc_boot(7202); s = nc_frames(20, 4); nc_show("C2 the next boot, dark", s);
    if (!nc_none(s)) NC_FAIL("C2: a dark boot said a card");
    sim_clock_shift(60); nc_boot(7203); s = nc_frames(10, 4); nc_show("C2 a restart (no save), dark", s);
    if (!nc_none(s)) NC_FAIL("C2: a dark restart said a card");
    nc_sleep(1 * H);
    nc_boot(7204); nc_dark(false); s = nc_frames(90, 4); nc_show("C2 the lit boot", s);
    if (nc_count(NOTICE_STAGE, u1, st) != 1 || nc_count(NOTICE_MILESTONE, u0, MS_FIRST_BUBBLES) != 1 || nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF) != 1)
        NC_FAIL("C2: said stage %d fish badge %d tank badge %d (want 1 each) - PW2-3", nc_count(NOTICE_STAGE, u1, st), nc_count(NOTICE_MILESTONE, u0, MS_FIRST_BUBBLES), nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF));
    nc_sleep(1 * H);
    nc_boot(7205); s = nc_frames(40, 4); nc_show("C2 the boot after", s);
    if (nc_twice()) NC_FAIL("C2: %d card(s) said twice on this board", nc_twice());
}
/* C3: power cuts */
static void c3(void) {
    /* a: earned in the dark, saved (the 30 s coalesce, a heartbeat), then a cut before the light: still owed */
    nc_board("c3a"); nc_found(7301);
    nc_dark(true); nc_frames(1, 0);
    T.tank_ms_bits |= TMS_FIRST_MUTATION; nc_frames(1, 0); progression_save(&T);   /* (the diff has queued it: held) */
    sim_clock_shift(30); nc_boot(7302); nc_dark(false); said_t s = nc_frames(40, 4); nc_show("C3a saved in the dark, a cut, the light on", s);
    if (nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_MUTATION) != 1) NC_FAIL("C3a: a cut after the save: the card said %d time(s) (want 1)", nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_MUTATION));
    /* b: a cut once it was up: never again */
    sim_clock_shift(30); nc_boot(7303); s = nc_frames(40, 4); nc_show("C3b a cut after it was said", s);
    if (nc_twice()) NC_FAIL("C3b: a cut after the card was up said it again");
    /* b2: the cut the very frame after it came up (its first frame's save is all there is) */
    nc_board("c3b2"); nc_found(7311);
    nc_dark(true); nc_frames(1, 0);
    T.tank_ms_bits |= TMS_FULL_SCHOOL; nc_frames(1, 0); progression_save(&T);
    sim_clock_shift(30); nc_boot(7312); nc_dark(false);
    bool up = false;
    for (int k = 0; k < 2000 && !up; k++) { nc_frames(0.05f, k < 80 ? 4 : 0); const notice_t *n = notice_current(); up = n && n->kind == NOTICE_TANK_MILESTONE && n->bit == TMS_FULL_SCHOOL; }
    nc_frames(0.05f, 0);                         /* (the frame after it came up: its save) */
    if (!up) NC_FAIL("C3b2: the owed badge never came up");
    sim_clock_shift(10); nc_boot(7313); s = nc_frames(40, 4); nc_show("C3b2 a cut a frame after the card came up", s);
    if (nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FULL_SCHOOL) != 1) NC_FAIL("C3b2: said %d time(s) (want 1)", nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FULL_SCHOOL));
    /* c: a cut between earning and saving: the flash has neither the badge nor a card owed */
    nc_board("c3c"); nc_found(7321);
    nc_dark(true); nc_frames(1, 0); progression_save(&T);
    T.tank_ms_bits |= TMS_FIRST_REEF; nc_frames(1, 0);   /* (under the coalesce: not yet saved) */
    sim_clock_shift(10); nc_boot(7322); nc_dark(false); s = nc_frames(40, 4); nc_show("C3c a cut before the save", s);
    if (T.tank_ms_bits & TMS_FIRST_REEF) NC_FAIL("C3c: (the test) the badge reached the flash");
    if (nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF)) NC_FAIL("C3c: a card for a badge the flash never had");
}
/* C4: a full queue */
static void c4(void) {
    nc_board("c4"); nc_found(7401);
    nc_dark(true); nc_frames(1, 0);
    T.tank_ms_bits |= TMS_FIRST_PLAY_SESSION | TMS_CHANGED_SOMEONE | TMS_FIRST_TRIM | TMS_FIRST_CLEANING | TMS_FULL_SCHOOL | TMS_FIRST_REEF | TMS_FIRST_MUTATION | TMS_FIRST_SALE;
    for (int i = 0; i < T.n_fish; i++) T.fish[i].ms_bits |= MS_FIRST_MEAL_FROM_YOU | MS_FIRST_HOLD_APPROACH | MS_FIRST_DART | MS_FIRST_BUBBLES | MS_FIRST_FOLLOW | MS_FIRST_GRASS;
    nc_frames(1, 0);
    const int held = notice_pending();
    nc_sleep(1 * H);
    nc_boot(7402); notice_low_battery();
    said_t s = nc_frames(10, 4); nc_show("C4 a full queue, dark: LOW BATTERY only", s);
    if (s.low != 1 || !nc_none(s)) NC_FAIL("C4: in the dark: low %d, others %d", s.low, s.n - s.low);
    nc_sleep(1 * H);
    /* what survives: the record on the flash - the queue's 12, less the place LOW BATTERY took (the newest held one) */
    static uint8_t b[8192]; long nb = nc_read(b, sizeof b);
    uint32_t chk = 0, rtms = 0; uint16_t rms[N_FISH_MAX] = { 0 }; uint8_t rst = 0;
    if (nb >= 2200) { memcpy(&chk, b + 2176, 4); memcpy(&rtms, b + 2180, 4); memcpy(rms, b + 2184, sizeof rms); rst = b[2196]; }
    int nrec = __builtin_popcount(rtms) + __builtin_popcount(rst);
    for (int i = 0; i < N_FISH_MAX; i++) nrec += __builtin_popcount(rms[i]);
    if (held != NOTICE_QUEUE || !chk || nrec != NOTICE_QUEUE - 1) NC_FAIL("C4: held %d, the record owes %d card(s) (want %d and %d)", held, nrec, NOTICE_QUEUE, NOTICE_QUEUE - 1);
    nc_boot(7403); nc_dark(false); s = nc_frames(150, 4); nc_show("C4 the lit boot", s);
    printf("night-cards: C4 (held %d in the dark: 8 tank badges and %d fish x 6 owed; the queue holds %d; the record %d)\n", held, T.n_fish, NOTICE_QUEUE, nrec);
    int said_rec = 0;
    for (uint32_t bit = 1; bit; bit <<= 1) {
        if (rtms & bit) { if (nc_count(NOTICE_TANK_MILESTONE, 0, bit) != 1) NC_FAIL("C4: owed tank badge %05x said %d time(s)", (unsigned)bit, nc_count(NOTICE_TANK_MILESTONE, 0, bit)); else said_rec++; }
        else if ((bit & (TMS_CHANGED_SOMEONE | TMS_FIRST_TRIM | TMS_FIRST_CLEANING | TMS_FULL_SCHOOL | TMS_FIRST_REEF | TMS_FIRST_MUTATION | TMS_FIRST_SALE)) && nc_count(NOTICE_TANK_MILESTONE, 0, bit))
            NC_FAIL("C4: tank badge %05x, beyond the full queue, said", (unsigned)bit);
        for (int i = 0; i < T.n_fish && i < N_FISH_MAX && bit < 0x10000u; i++)
            if (rms[i] & bit) { if (nc_count(NOTICE_MILESTONE, T.fish[i].uid, bit) != 1) NC_FAIL("C4: owed badge %04x of fish %d said %d time(s)", (unsigned)bit, i, nc_count(NOTICE_MILESTONE, T.fish[i].uid, bit)); else said_rec++; }
    }
    if (said_rec != nrec || nc_twice()) NC_FAIL("C4: %d of the %d owed said, %d twice", said_rec, nrec, nc_twice());
    nc_sleep(1 * H);
    nc_boot(7404); s = nc_frames(60, 4); nc_show("C4 the boot after", s);
    if (nc_twice()) NC_FAIL("C4: %d card(s) said twice across boots", nc_twice());
}
/* C5: MY TANKS with cards owed */
static bool nc_put_up(int i) {
    static uint16_t thumb[200 * 200]; int w, h; render_shelf_thumb_size(&w, &h);
    if (!progression_save(&T)) return false;
    progression_carry_t c; progression_carry_take(&T, &c);
    return shelf_swap(i, thumb, w, h, &c, sizeof c, clock_port_now_unix());
}
static void c5(void) {
    nc_board("c5"); nc_found(7501);
    const uint32_t idA = shelf_now()->id, uA = T.fish[0].uid;
    nc_dark(true); nc_frames(1, 0);
    T.fish[0].ms_bits |= MS_FIRST_DART; T.tank_ms_bits |= TMS_FIRST_REEF; nc_frames(1, 0);
    int i = shelf_new(0, clock_port_now_unix());
    if (i < 0 || !nc_put_up(i)) { NC_FAIL("C5: staging: no new tank"); return; }
    nc_boot(7502); progression_setup_done(&T); nc_dark(false);
    said_t s = nc_frames(60, 4); nc_show("C5 tank B swapped in, lit", s);
    if (nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF) || nc_count(NOTICE_MILESTONE, uA, MS_FIRST_DART)) NC_FAIL("C5: tank B said tank A's cards");
    int k = -1; for (int q = 0; q < shelf_count(); q++) if (shelf_get(q)->id == idA) k = q;
    sim_clock_shift(120);
    if (k < 0 || !nc_put_up(k)) { NC_FAIL("C5: the swap back refused"); return; }
    nc_boot(7503); nc_dark(false); s = nc_frames(60, 4); nc_show("C5 tank A swapped back in, light on", s);
    if (nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF) != 1 || nc_count(NOTICE_MILESTONE, uA, MS_FIRST_DART) != 1)
        NC_FAIL("C5: tank A said its tank badge %d, its fish's %d (want 1, 1)", nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF), nc_count(NOTICE_MILESTONE, uA, MS_FIRST_DART));
    nc_sleep(60); nc_boot(7504); s = nc_frames(30, 4); nc_show("C5 tank A, the boot after", s);
    if (nc_twice()) NC_FAIL("C5: %d card(s) said twice", nc_twice());
}
/* C6: an older save; a rollback; a reset */
static void c6(void) {
    static uint8_t b[8192], b2[8192];
    /* a: a save from before the record (2176 B) with a card held when it was written: owes nothing, saved back unchanged */
    nc_board("c6a"); nc_found(7601);
    nc_dark(true); nc_frames(1, 0); T.tank_ms_bits |= TMS_FIRST_REEF; nc_frames(1, 0);
    progression_save(&T);
    long n = nc_read(b, sizeof b);
    if (n != 2200) NC_FAIL("C6a: the save is %ld B (want 2200)", n);
    { uint32_t chk, tms; memcpy(&chk, b + 2176, 4); memcpy(&tms, b + 2180, 4);
      if (!chk || !(tms & TMS_FIRST_REEF)) NC_FAIL("C6a: (the test) the record was not written (check %08x, tank badges %05x)", (unsigned)chk, (unsigned)tms); }
    nc_write(b, 2176);
    nc_boot(7602); nc_dark(false); said_t s = nc_frames(30, 4); nc_show("C6a an older save (2176 B), a card held", s);
    if (nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF)) NC_FAIL("C6a: an older save owed a card");
    long n2 = (progression_save(&T), nc_read(b2, sizeof b2));
    int diffs = 0;
    for (long i = 2176; i < n2; i++) if (b2[i]) diffs++;
    if (n2 != 2200 || diffs) NC_FAIL("C6a: saved back %ld B, %d nonzero byte(s) past 2176 (want 2200, none)", n2, diffs);
    /* b: a rollback - the record kept as this build wrote it, the head rewritten by 0.4.1 */
    nc_board("c6b"); nc_found(7611);
    nc_dark(true); nc_frames(1, 0); T.tank_ms_bits |= TMS_FIRST_REEF; nc_frames(1, 0);
    progression_save(&T); n = nc_read(b, sizeof b);
    static uint8_t keep[8192]; memcpy(keep, b, (size_t)n);
    uint32_t rel = 0x00040000u; float clk; memcpy(b + 1664, &rel, 4); memcpy(&clk, b + 16, 4); clk += 5; memcpy(b + 16, &clk, 4);
    nc_write(b, (size_t)n);
    nc_boot(7612); nc_dark(false); s = nc_frames(30, 4); nc_show("C6b a rollback's head over the record", s);
    if (nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF)) NC_FAIL("C6b: a record under another build's head owed a card");
    /* ... and one of the same release number (a dev build stamped as the release before it): its clock moved on */
    nc_board("c6b2"); memcpy(b, keep, (size_t)n); memcpy(&clk, b + 16, 4); clk += 5; memcpy(b + 16, &clk, 4); nc_write(b, (size_t)n);
    nc_boot(7613); nc_dark(false); s = nc_frames(30, 4); nc_show("C6b2 the same, the release number the same", s);
    if (nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF)) NC_FAIL("C6b2: a record under a head written later owed a card");
    /* ... the control: the bytes as this build left them owe it */
    nc_board("c6b3"); nc_write(keep, (size_t)n);
    nc_boot(7614); nc_dark(false); s = nc_frames(30, 4); nc_show("C6b3 the control: the save as written", s);
    if (nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF) != 1) NC_FAIL("C6b3: the control did not owe its card (%d)", nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF));
    /* b4: a rollback whose head's release AND clock both changed, by bits that cancel under a plain XOR of the two (the first
       version's owed_sig would have taken it) */
    nc_board("c6b4"); memcpy(b, keep, (size_t)n);
    { uint32_t r0, c0, r1 = 0x00040000u, c1; memcpy(&r0, b + 1664, 4); memcpy(&c0, b + 16, 4);
      c1 = c0 ^ r0 ^ r1; memcpy(b + 1664, &r1, 4); memcpy(b + 16, &c1, 4);
      if ((r0 ^ c0) != (r1 ^ c1)) NC_FAIL("C6b4: (the test) the XOR does not cancel"); }
    nc_write(b, (size_t)n);
    nc_boot(7615); nc_dark(false); s = nc_frames(30, 4); nc_show("C6b4 a rollback's head, cancelling under XOR", s);
    if (nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF)) NC_FAIL("C6b4: a head whose release and clock cancel under XOR still owed a card");
    /* c: a reset with cards owed */
    nc_board("c6c"); nc_found(7621);
    nc_dark(true); nc_frames(1, 0); T.tank_ms_bits |= TMS_FIRST_REEF; nc_frames(1, 0); progression_save(&T);
    progression_reset(&T, 7622); notice_sync(&T);
    nc_dark(false); s = nc_frames(30, 0); nc_show("C6c the keeper's reset, cards owed", s);
    nc_sleep(60); nc_boot(7623); nc_dark(false); s = nc_frames(30, 4); nc_show("C6c the boot after the reset", s);
    if (nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF)) NC_FAIL("C6c: the new tank said the old one's card");
}
/* C7: a lit tank, as before */
static void c7(void) {
    nc_board("c7"); nc_found(7701);
    T.tank_ms_bits |= TMS_FIRST_REEF; said_t s = nc_frames(30, 0); nc_show("C7 lit: a badge earned", s);
    if (nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF) != 1) NC_FAIL("C7: a lit badge said %d time(s)", nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF));
    nc_sleep(60); nc_boot(7702); s = nc_frames(30, 4); nc_show("C7 lit: the boot after", s);
    if (nc_twice()) NC_FAIL("C7: a card said in the session came again (%d)", nc_twice());
}
/* C8: a lit tank in ordinary play - cards queued when an ordinary save comes: no save forced as each comes up (the normal
   save, its 30 s gap, clears them), and a boot after that save says none again */
static void c8(void) {
    nc_board("c8"); nc_found(7801);
    T.tank_ms_bits |= TMS_FIRST_REEF | TMS_FIRST_MUTATION | TMS_FULL_SCHOOL; nc_frames(0.2f, 0);   /* three at once: queued */
    const int q = notice_pending() + (notice_current() ? 1 : 0);
    progression_save(&T);                        /* an ordinary save with them queued (the coalesce, a heartbeat) */
    const int sv0 = g_sim_tank_saves;
    said_t s = nc_frames(29, 0);                 /* inside the save's 30 s gap: the three come up */
    const int during = g_sim_tank_saves - sv0;
    nc_show("C8 lit: three queued at a save, said in the next 29 s", s);
    printf("night-cards: C8 (%d queued at the save; saves while they came up: %d)\n", q, during);
    if (nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_REEF) != 1 || nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FIRST_MUTATION) != 1 || nc_count(NOTICE_TANK_MILESTONE, 0, TMS_FULL_SCHOOL) != 1)
        NC_FAIL("C8: (the test) not all three said inside the gap (%d tank cards)", s.tank_ms);
    if (during) NC_FAIL("C8: %d save(s) forced while the queued cards came up (want 0: the normal save's gap)", during);
    nc_frames(40, 0);
    if (g_sim_tank_saves == sv0) NC_FAIL("C8: no save after the gap: the cards said stay owed on the flash");
    sim_clock_shift(10); nc_boot(7802); s = nc_frames(40, 4); nc_show("C8 a cut after the normal save", s);
    if (nc_twice()) NC_FAIL("C8: %d card(s) said again after the normal save", nc_twice());
}
int selftest_night_cards(void) {
    sim_clock_freeze(1);
    c1(); c2(); c3(); c4(); c5(); c6(); c7(); c8();
    sim_clock_freeze(0);
    if (nc_bad) { printf("selftest-night-cards: %d FAIL\n", nc_bad); return 1; }
    printf("selftest-night-cards: what the dark held is saved and said once the light is on - a dark morning slept again (PW2-2), "
           "news earned in the dark (PW2-3), restarts, power cuts either side of the save and of the card, a full queue, MY TANKS, "
           "an older save, a rollback, a reset, a lit tank as before\n");
    return 0;
}
