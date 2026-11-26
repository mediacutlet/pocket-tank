#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_night_badge.c - --selftest-night-badge (0.4.2, audit R-progression PR-2 / V-prog-touch PR-2): FIRST FULL NIGHT'S
 * SLEEP is said on a BOARD. On a board a slept span of 6 h or more only comes through progression_wake at a boot (the
 * in-place nap is the grace, 20 min / 60 min clockless), and the boot's notice_sync adopted every badge bit silently: the
 * ring came onto the keeper page with no chime and no card (the manual: the tank chimes when a badge is earned). The sim's
 * own loop sleeps in place and said it, so no test saw it. This one boots in FIRMWARE main.c's order (app_main: shelf_boot,
 * progression_wake from a deep sleep / progression_boot cold, progression_shelf_settle, progression_swap_carry,
 * notice_sync, notice_updated for an older release, notice_wrong_board on a cold start) and then runs the frames as the
 * tank task does (tank_tick, progression_tick, notice_tick with the boot show / a page / a fry's welcome blocking).
 *
 * The siblings it pins (docs/fix042-night-badge.md):
 *   N1  a cold boot after 7 h away (a night power-off): the badge said once, with the milestone chime - after the boot
 *       show, never under it; the badges earned before the night and the fish's own say nothing; a fish that grew up in
 *       the night gets its "IS NOW A ..." card with the stage-up chime (D-5, below)
 *   N2  the boot after it (and another 7 h night): nothing - never said twice
 *   N3  a wake from deep sleep after 8 h: said once
 *   N4  a night under 6 h (5.9 h): no badge, nothing said
 *   N5  the first boot after an OTA from 0.4.0 (the save's release older) with the badge earned before, 7 h away: the
 *       UPDATED card only; the same OTA boot ending a tank's first long night: UPDATED and the badge, each once
 *   N6  a SWAP IN of a shelved tank that slept 7 h DORMANT on the shelf, never a full night before: said once - the shelf
 *       lived that night as the tank's own (shelf.c: the wake lives `lived`), and the badge is that tank's, new to it
 *       and to the keeper (THE JUDGMENT CALL, the doc). Swapped back, the other tank (its badge earned) says nothing;
 *       a PAUSED shelf lives 0 h: nothing
 *   N7  a restore from the backup page (the save stamped at the computer's now): the restored tank's badges say nothing
 *   N8  a fresh tank's first boot, and the keeper's reset after a long night: nothing
 *   N9  a power cut once the badge was seen: never said twice; before it was seen: the next boot still owes it
 *   N10 (0.4.2, Discord D-5 "After sleep fish grow up without notice"): the morning's stage cards - one per fish the night
 *       grew up, each once with the stage-up chime, after the boot show; a fish that didn't grow says nothing; a cut once
 *       they were seen never says them again, a cut before still owes them; a short night (no badge) says its growth; a
 *       reboot with no night and a reset say nothing; N10g an awake card seen, then a cut and 3 min off: not again
 *   N9c the badge before the night's stage card; a cut between them never says the badge twice nor loses it
 * Red before the fix (f562a21): N1, N3, N5b and N6 fail - "said 0 times". Red before D-5 (061a902): N1, N2, N3, N10a,
 * N10c, N10d - the night's growth said 0 times. */
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
static int nb_bad;
static uint32_t s_boot_tms, s_boot_ms[N_FISH_MAX];   /* earned when the boot's notice_sync ran (the night's badge apart): never said */
#define NB_FAIL(...) do { printf("night-badge: FAIL: "); printf(__VA_ARGS__); printf("\n"); nb_bad++; } while (0)
#define H (3600)

static void nb_board(void) {                     /* a scratch board of its own ($PT_SHELF_DIR/night-badge): save, shelf, frame */
    char cmd[800], d[600], p[700];
    snprintf(d, sizeof d, "%s/night-badge", sim_shelf_dir());
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", d, d); (void)system(cmd);
    snprintf(p, sizeof p, "%s/tank.sav", d); setenv("POCKET_TANK_SAVE", p, 1);
    snprintf(p, sizeof p, "%s/storage.bin", d); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", d); setenv("POCKET_TANK_FRAME", p, 1);
}
/* the board's boot, firmware main.c app_main (1984-2045): RAM gone, the save as the flash has it */
static float nb_boot(uint32_t seed, bool from_sleep) {
    memset(&T, 0, sizeof T); tank_init(&T, seed);
    shelf_boot(clock_port_now_unix());
    float h = from_sleep ? progression_wake(&T, clock_port_now_unix()) : progression_boot(&T);
    progression_shelf_settle(&T);
    progression_swap_carry(&T);
    notice_sync(&T);                             /* what is already earned stays unannounced */
    s_boot_tms = T.tank_ms_bits & ~TMS_FIRST_FULL_NIGHT;
    for (int i = 0; i < N_FISH_MAX; i++) s_boot_ms[i] = T.fish[i].ms_bits;
    uint32_t r = progression_loaded_release();
    if (r && r < (uint32_t)PT_RELEASE_NUM) notice_updated();
    const char *hw = board_port_hw();
    if (hw && strcmp(hw, PT_BOARD) != 0 && !from_sleep) notice_wrong_board();
    return h;
}
/* what came up in the frames after it */
typedef struct { int night, night_cues, old_tank, fish_ms, stage, stage_cues, stage_fish[N_FISH_MAX], seq, night_seq, stage_seq, updated_seq, updated, other_tank, other, up_blocked, live; } said_t;
static said_t nb_frames(float secs, float boot_show_s) {
    said_t s; memset(&s, 0, sizeof s);
    const float dt = 0.05f;
    bool was_up = false;
    s.night_seq = s.stage_seq = s.updated_seq = -1;
    for (float at = 0; at < secs; at += dt) {
        bool show = at < boot_show_s;            /* the boot show (s_loading_last): the tank builds up first */
        tank_tick(&T, dt, advisor_rules);
        progression_tick(&T, dt);
        notice_tick(&T, dt, T.ui_cover || setup_active() || setup_birth_due(&T) || show);
        int cue = notice_take_cue();
        const notice_t *n = notice_current();
        if (n && !was_up) {
            s.seq++;
            if (n->kind == NOTICE_TANK_MILESTONE && n->bit == TMS_FIRST_FULL_NIGHT && s.night_seq < 0) s.night_seq = s.seq;
            if (n->kind == NOTICE_STAGE && s.stage_seq < 0) s.stage_seq = s.seq;
            if (n->kind == NOTICE_UPDATED && s.updated_seq < 0) s.updated_seq = s.seq;
            if (show) s.up_blocked++;
            if (n->kind == NOTICE_TANK_MILESTONE && n->bit == TMS_FIRST_FULL_NIGHT) { s.night++; if (cue == SND_MILESTONE) s.night_cues++; }
            else if (n->kind == NOTICE_TANK_MILESTONE && (n->bit & s_boot_tms)) s.old_tank++;
            else if (n->kind == NOTICE_MILESTONE && n->fish >= 0 && (n->bit & s_boot_ms[n->fish])) s.fish_ms++;
            else if (n->kind == NOTICE_TANK_MILESTONE || n->kind == NOTICE_MILESTONE) s.live++;   /* earned in these frames, awake: said as ever */
            else if (n->kind == NOTICE_STAGE) { s.stage++; if (cue == SND_STAGE_UP) s.stage_cues++; if (n->fish >= 0 && n->fish < N_FISH_MAX) s.stage_fish[n->fish]++; }
            else if (n->kind == NOTICE_UPDATED) s.updated++;
            else s.other++;
            if (n->kind != NOTICE_TANK_MILESTONE || n->bit != TMS_FIRST_FULL_NIGHT)
                printf("night-badge:   (a notice: kind %d bit %05x fish %d)\n", n->kind, (unsigned)n->bit, n->fish);
        }
        was_up = n != NULL;
        if (n && n->age > 1.5f && (n->kind == NOTICE_MODEL_DAMAGED || n->kind == NOTICE_WRONG_BOARD)) notice_dismiss();   /* (they wait for a tap) */
    }
    return s;
}
static void nb_show(const char *what, float h, said_t s) {
    printf("night-badge: %-62s lived %5.1f h | FULL NIGHT said %d (chime %d) | earned before: tank %d fish %d | stage %d | UPDATED %d | other %d | under the boot show %d | (earned awake since: %d)\n",
           what, h, s.night, s.night_cues, s.old_tank, s.fish_ms, s.stage, s.updated, s.other_tank + s.other, s.up_blocked, s.live);
}
/* a founded tank, setup done, some badges earned before (a tank's and a fish's), saved */
#define HAD_TMS (TMS_FIRST_FEEDING | TMS_FIRST_TRIM)
static void nb_found(uint32_t seed, bool full_night_too) {
    nb_boot(seed, false);
    progression_setup_done(&T);
    T.tank_ms_bits |= HAD_TMS | (full_night_too ? TMS_FIRST_FULL_NIGHT : 0); T.tank_ms_seen = T.tank_ms_bits;
    T.fish[0].ms_bits |= MS_FIRST_MEAL_FROM_YOU; T.fish[0].ms_seen = T.fish[0].ms_bits;
    progression_save(&T);
}
static bool nb_night(void) { return (T.tank_ms_bits & TMS_FIRST_FULL_NIGHT) != 0; }
/* (D-5) each fish's stage as the flash has it before a night (marked right after a save), and how many grew past it */
static uint32_t s_mark_uid[N_FISH_MAX]; static stage_t s_mark_stage[N_FISH_MAX]; static int s_mark_n;
static void nb_mark(void) { s_mark_n = 0; for (int i = 0; i < T.n_fish; i++) { s_mark_uid[s_mark_n] = T.fish[i].uid; s_mark_stage[s_mark_n++] = T.fish[i].stage; } }
static int nb_grew(void) {
    int n = 0;
    for (int k = 0; k < s_mark_n; k++) for (int i = 0; i < T.n_fish; i++) if (T.fish[i].uid == s_mark_uid[k]) { n += T.fish[i].stage > s_mark_stage[k]; break; }
    return n;
}
/* the night's stage cards: one per fish that grew, each with the stage-up chime, none twice */
static bool nb_stages_ok(said_t s, int want) {
    if (s.stage != want || s.stage_cues != want) return false;
    for (int i = 0; i < N_FISH_MAX; i++) if (s.stage_fish[i] > 1) return false;
    return true;
}
static bool nb_quiet_but_stage(said_t s) { return !s.night && !s.old_tank && !s.fish_ms && !s.other_tank && !s.other && !s.up_blocked; }
static bool nb_quiet(said_t s) { return !s.night && !s.old_tank && !s.fish_ms && !s.stage && !s.other_tank && !s.other && !s.up_blocked; }
static bool nb_put_up(int i) {                   /* SWAP IN, as main.c does it (sim main.c shelf_put_up) */
    static uint16_t thumb[200 * 200]; int w, h; render_shelf_thumb_size(&w, &h);
    if (!progression_save(&T)) return false;
    progression_carry_t c; progression_carry_take(&T, &c);
    return shelf_swap(i, thumb, w, h, &c, sizeof c, clock_port_now_unix());
}
static int nb_index_of(uint32_t id) { for (int i = 0; i < shelf_count(); i++) if (shelf_get(i)->id == id) return i; return -1; }
static void nb_patch_release(uint32_t rel) {    /* the save as an older build wrote it (save_t.saved_release, 1664) */
    FILE *f = fopen(getenv("POCKET_TANK_SAVE"), "r+b"); if (!f) { NB_FAIL("no save to stamp"); return; }
    fseek(f, 1664, SEEK_SET); fwrite(&rel, sizeof rel, 1, f); fclose(f);
}

int selftest_night_badge(void) {
    sim_clock_freeze(1);                         /* (no second ticks by itself: the spans are the ones staged) */
    said_t s; float h;

    /* N1: the night power-off, the morning's cold boot - and a fish that grows up in the night */
    nb_board(); nb_found(4201, false);
    progression_set_age(&T, 0, 1.0f * H);        /* fry; the night's 7 h x SLEEP_GROWTH_FRAC = 1.75 h makes it a juvenile */
    stage_t st0 = T.fish[0].stage; progression_save(&T);
    sim_clock_shift(7 * H);
    h = nb_boot(4202, false);
    stage_t st1 = T.fish[0].stage;
    s = nb_frames(30, 4); nb_show("N1 cold boot after 7 h (a night power-off)", h, s);
    if (!nb_night()) NB_FAIL("N1: the 7 h boot did not earn the badge");
    if (s.night != 1 || s.night_cues != 1) NB_FAIL("N1: FIRST FULL NIGHT'S SLEEP said %d times (chime %d), want once with the milestone chime", s.night, s.night_cues);
    if (s.old_tank || s.fish_ms || s.other_tank || s.other) NB_FAIL("N1: badges earned before the night said at the boot (tank %d, fish %d, other %d)", s.old_tank, s.fish_ms, s.other_tank + s.other);
    if (st1 <= st0) NB_FAIL("N1: staging: fish 0 did not grow up in the night (stage %d -> %d)", st0, st1);
    if (s.stage != 1 || s.stage_cues != 1 || s.stage_fish[0] != 1) NB_FAIL("N1: fish 0 grew up in the night: its card said %d times (chime %d, fish 0: %d), want once with the stage-up chime (D-5)", s.stage, s.stage_cues, s.stage_fish[0]);
    if (s.up_blocked) NB_FAIL("N1: a notice came up under the boot show");
    if (s.night_seq < 0 || s.stage_seq < 0 || s.night_seq > s.stage_seq) NB_FAIL("N1: the night badge (notice #%d) must come before the night's stage card (#%d): a card's save would put it on the flash unseen", s.night_seq, s.stage_seq);

    /* N2: never twice - the next boot at once, and another 7 h night */
    progression_save(&T);
    h = nb_boot(4203, false); s = nb_frames(20, 0); nb_show("N2 the boot after it (0 h)", h, s);
    if (!nb_quiet(s)) NB_FAIL("N2: the boot after said something (night %d)", s.night);
    progression_save(&T); nb_mark(); sim_clock_shift(7 * H);
    h = nb_boot(4204, false); s = nb_frames(30, 0); nb_show("N2 a second 7 h night", h, s);
    if (!nb_quiet_but_stage(s)) NB_FAIL("N2: the second long night said a badge (night %d)", s.night);
    if (nb_grew() < 1) NB_FAIL("N2: staging: no fish grew in the second night");
    if (!nb_stages_ok(s, nb_grew())) NB_FAIL("N2: %d fish grew up in the second night, %d cards (chime %d)", nb_grew(), s.stage, s.stage_cues);

    /* N3: the deep-sleep wake (BOOT / the PWR key / the timer) after 8 h */
    nb_board(); nb_found(4211, false); nb_mark();
    sim_clock_shift(8 * H);
    h = nb_boot(4212, true); s = nb_frames(30, 4); nb_show("N3 wake from deep sleep after 8 h", h, s);
    if (s.night != 1 || s.night_cues != 1) NB_FAIL("N3: said %d times (chime %d), want once", s.night, s.night_cues);
    if (s.old_tank || s.fish_ms || s.other_tank || s.other) NB_FAIL("N3: something else said at the wake");
    if (nb_grew() < 1) NB_FAIL("N3: staging: no fish grew in the deep sleep");
    if (!nb_stages_ok(s, nb_grew())) NB_FAIL("N3: %d fish grew up in the deep sleep, %d cards (chime %d)", nb_grew(), s.stage, s.stage_cues);

    /* N4: a night under 6 h earns nothing */
    nb_board(); nb_found(4221, false);
    sim_clock_shift((int64_t)(5.9 * H));
    h = nb_boot(4222, false); s = nb_frames(20, 0); nb_show("N4 cold boot after 5.9 h", h, s);
    if (nb_night()) NB_FAIL("N4: 5.9 h earned the badge");
    if (!nb_quiet(s)) NB_FAIL("N4: a 5.9 h boot said something (night %d)", s.night);

    /* N5: the first boot after an OTA from 0.4.0 */
    nb_board(); nb_found(4231, true);
    nb_patch_release(0x00040000u); sim_clock_shift(7 * H);
    h = nb_boot(4232, false); s = nb_frames(30, 4); nb_show("N5a OTA boot (0.4.0 -> this), badge earned before, 7 h", h, s);
    if (!nb_quiet(s) || s.updated != 1) NB_FAIL("N5a: the OTA boot said night %d, old %d, fish %d, UPDATED %d (want the UPDATED card only)", s.night, s.old_tank, s.fish_ms, s.updated);
    nb_board(); nb_found(4233, false);
    nb_patch_release(0x00040000u); sim_clock_shift(7 * H);
    h = nb_boot(4234, false); s = nb_frames(40, 4); nb_show("N5b OTA boot ending the tank's first long night", h, s);
    if (s.updated != 1 || s.night != 1 || s.old_tank || s.fish_ms || s.other_tank || s.other) NB_FAIL("N5b: UPDATED %d, night %d, old %d, fish %d (want UPDATED and the badge, each once)", s.updated, s.night, s.old_tank, s.fish_ms);
    if (s.updated_seq < 0 || s.updated_seq > s.night_seq) NB_FAIL("N5b: UPDATED (#%d) must come before the badge (#%d): the badge's save stamps the release, so a cut would lose UPDATED", s.updated_seq, s.night_seq);

    /* N6: MY TANKS - tank 1 (its full night earned long ago), tank 2 and tank 3 (none yet), each founded on the glass */
    nb_board(); nb_found(4241, true);
    uint32_t id[4] = { 0, shelf_now()->id, 0, 0 };
    for (int k = 2; k <= 3; k++) {               /* a NEW TANK, swapped in fresh: founded, set up, a few badges, saved */
        int i = shelf_new(0, clock_port_now_unix());
        if (i < 0 || !nb_put_up(i)) { NB_FAIL("N6: staging: tank %d", k); break; }
        nb_boot(4241 + k, false); id[k] = shelf_now()->id;
        progression_setup_done(&T); T.tank_ms_bits |= HAD_TMS; T.tank_ms_seen = T.tank_ms_bits; progression_save(&T);
    }
    if (id[3]) {
        if (!nb_put_up(nb_index_of(id[1]))) NB_FAIL("N6: staging: tank 1 back");
        nb_boot(4244, false);                    /* tank 1 on the glass; tank 2 DORMANT (the default) on the shelf, tank 3 PAUSED */
        if (!shelf_set_time(nb_index_of(id[3]), SHELF_TIME_PAUSED, clock_port_now_unix())) NB_FAIL("N6: staging: PAUSED");
        sim_clock_shift(7 * H);
        /* N6a: tank 2 slept 7 h DORMANT on the shelf: it comes in, its first full night lived there */
        if (!nb_put_up(nb_index_of(id[2]))) NB_FAIL("N6a: swap to tank 2");
        h = nb_boot(4245, false); s = nb_frames(30, 4); nb_show("N6a SWAP IN tank 2, 7 h DORMANT on the shelf, no night before", h, s);
        if (shelf_now()->id != id[2]) NB_FAIL("N6a: tank 2 not on the glass");
        if (s.night != 1 || s.night_cues != 1) NB_FAIL("N6a: tank 2's first full night (on the shelf) said %d times (chime %d), want once", s.night, s.night_cues);
        if (s.old_tank || s.fish_ms || s.other_tank || s.other || s.stage) NB_FAIL("N6a: tank 2's older badges said at its SWAP IN");
        /* N6b: tank 1 slept 7 h DORMANT too, its badge old: nothing */
        sim_clock_shift(7 * H);
        if (!nb_put_up(nb_index_of(id[1]))) NB_FAIL("N6b: swap to tank 1");
        h = nb_boot(4246, false); s = nb_frames(30, 4); nb_show("N6b SWAP IN tank 1 (badge earned before), 7 h DORMANT", h, s);
        if (shelf_now()->id != id[1]) NB_FAIL("N6b: tank 1 not on the glass");
        if (!nb_quiet(s)) NB_FAIL("N6b: tank 1's old badge said at its SWAP IN (night %d, old %d)", s.night, s.old_tank);
        /* N6c: tank 3 PAUSED 14 h lives nothing: no badge, nothing said */
        if (!nb_put_up(nb_index_of(id[3]))) NB_FAIL("N6c: swap to tank 3");
        h = nb_boot(4247, false); s = nb_frames(20, 4); nb_show("N6c SWAP IN tank 3, 14 h PAUSED, no night before", h, s);
        if (shelf_now()->id != id[3]) NB_FAIL("N6c: tank 3 not on the glass");
        if (nb_night() || !nb_quiet(s)) NB_FAIL("N6c: a PAUSED tank's SWAP IN: badge %d, said %d", nb_night(), s.night);
    }

    /* N7: a restore from the backup page - the save written back whole, stamped at the computer's now */
    nb_board(); nb_found(4251, true);
    {   static uint8_t blob[8192]; size_t n = 0;
        if (!persist_port_load(blob, sizeof blob, &n) || !n) NB_FAIL("N7: staging: no blob");
        sim_clock_shift(7 * H);
        progression_blob_set_saved_unix(blob, n, clock_port_now_unix());
        nb_board(); persist_port_save(blob, n);  /* (an erased board, the backup written back) */
        h = nb_boot(4252, false); s = nb_frames(20, 4); nb_show("N7 restore from the backup page (badge in it)", h, s);
        if (!nb_night()) NB_FAIL("N7: the restored tank lost its badge");
        if (!nb_quiet(s)) NB_FAIL("N7: a restore said its old badges (night %d, old %d, fish %d)", s.night, s.old_tank, s.fish_ms);
    }

    /* N8: a fresh tank's first boot; the keeper's reset after a long night's boot */
    nb_board();
    h = nb_boot(4261, false); s = nb_frames(10, 4); nb_show("N8a a fresh tank's first boot", h, s);
    if (nb_night() || !nb_quiet(s)) NB_FAIL("N8a: a fresh tank said something (night %d)", s.night);
    nb_board(); nb_found(4262, false);
    sim_clock_shift(7 * H);
    nb_boot(4263, false);
    progression_reset(&T, 4264); notice_sync(&T);   /* (the reset prompt's YES before the first frame: main.c reset_tank) */
    s = nb_frames(10, 0); nb_show("N8b the keeper's reset right after a 7 h boot", 0, s);
    if (nb_night() || !nb_quiet(s)) NB_FAIL("N8b: the fresh tank after a reset said something (night %d)", s.night);

    /* N9 (PR-2's blind review): a power cut in the ~30 s before the coalesced save. The wake only marks the save dirty, so a
       cut there boots from the save of before the night: it is lived again and earns the badge again. Once its notice has
       been on the glass, that must not say it a second time; before it was seen, the next boot still owes it */
    nb_board(); nb_found(4271, false);
    sim_clock_shift(7 * H);
    h = nb_boot(4272, false); s = nb_frames(12, 4); nb_show("N9a 7 h boot, the badge seen, then a cut at 12 s", h, s);
    if (s.night != 1) NB_FAIL("N9a: staging: the badge said %d times before the cut", s.night);
    h = nb_boot(4273, false); s = nb_frames(30, 4); nb_show("N9a the boot after the cut", h, s);   /* (no save since: RAM gone, the flash as it was) */
    if (s.night) NB_FAIL("N9a: the badge said AGAIN after a cut %d s into the boot that said it (%d)", 12, s.night);
    if (!nb_night()) NB_FAIL("N9a: the badge lost by the cut");
    nb_board(); nb_found(4274, false);
    sim_clock_shift(7 * H);
    h = nb_boot(4275, false); s = nb_frames(2, 4); nb_show("N9b 7 h boot, a cut at 2 s (under the boot show, not seen)", h, s);
    if (s.night) NB_FAIL("N9b: staging: said under the boot show");
    h = nb_boot(4276, false); s = nb_frames(30, 4); nb_show("N9b the boot after the cut", h, s);
    if (s.night != 1 || s.night_cues != 1) NB_FAIL("N9b: a badge never seen before the cut said %d times after it, want once", s.night);

    /* N10 (D-5, Discord "After sleep fish grow up without notice"): the morning's stage cards */
    {   nb_board(); nb_found(4281, true);           /* (the badge earned before: only the cards) */
        if (T.n_fish < 2) NB_FAIL("N10: staging: %d fish, want 2", T.n_fish);
        progression_set_age(&T, 0, 1.0f * H);        /* fry -> juvenile in a 7 h night (1.75 h) */
        progression_set_age(&T, 1, 11.0f * H);       /* juvenile -> adult (12 h) */
        stage_t a0 = T.fish[0].stage, a1 = T.fish[1].stage;
        progression_save(&T); nb_mark();
        sim_clock_shift(7 * H);
        /* N10a: both fish grew up in the night: two cards after the boot show, each once with its chime */
        h = nb_boot(4282, false); s = nb_frames(14, 4); nb_show("N10a 7 h night: two fish grew up (a cut at 14 s)", h, s);   /* (the cards
                                                     at 4 s and 11 s; the coalesced save would come at 30 s) */
        if (T.fish[0].stage <= a0 || T.fish[1].stage <= a1) NB_FAIL("N10a: staging: stages %d %d -> %d %d", a0, a1, T.fish[0].stage, T.fish[1].stage);
        if (!nb_stages_ok(s, 2) || s.stage_fish[0] != 1 || s.stage_fish[1] != 1) NB_FAIL("N10a: cards %d (chime %d; fish 0 %d, 1 %d), want each once", s.stage, s.stage_cues, s.stage_fish[0], s.stage_fish[1]);
        if (!nb_quiet_but_stage(s)) NB_FAIL("N10a: something else said (night %d, old %d, under the show %d)", s.night, s.old_tank, s.up_blocked);
        /* N10b: the power cut at 14 s, both seen, before the coalesced save: never said again */
        h = nb_boot(4283, false); s = nb_frames(30, 4); nb_show("N10b the boot after a cut, both cards seen", h, s);
        if (s.stage) NB_FAIL("N10b: a night's card said AGAIN after a cut (%d)", s.stage);
        if (T.fish[0].stage <= a0 || T.fish[1].stage <= a1) NB_FAIL("N10b: the growth lost by the cut");
        /* N10c: a cut under the boot show (2 s, nothing seen): the next boot still says both */
        nb_board(); nb_found(4284, true);
        progression_set_age(&T, 0, 1.0f * H); progression_set_age(&T, 1, 11.0f * H);
        progression_save(&T); nb_mark(); sim_clock_shift(7 * H);
        h = nb_boot(4285, false); s = nb_frames(2, 4); nb_show("N10c 7 h night, a cut at 2 s (under the boot show)", h, s);
        if (s.stage) NB_FAIL("N10c: staging: a card under the boot show");
        h = nb_boot(4286, false); s = nb_frames(40, 4); nb_show("N10c the boot after the cut", h, s);
        if (!nb_stages_ok(s, 2)) NB_FAIL("N10c: cards never seen before the cut said %d times after it (chime %d), want 2", s.stage, s.stage_cues);
        /* N10d: a short night (5.9 h: no badge) still says the fish that grew in it */
        nb_board(); nb_found(4287, false);
        progression_set_age(&T, 0, 1.6f * H);        /* + 5.9 h x 0.25 = 3.1 h: a juvenile */
        progression_save(&T); nb_mark(); sim_clock_shift((int64_t)(5.9 * H));
        h = nb_boot(4288, true); s = nb_frames(30, 4); nb_show("N10d deep sleep 5.9 h: fish 0 grew up, no badge", h, s);
        if (nb_night() || s.night) NB_FAIL("N10d: 5.9 h earned or said the badge");
        if (!nb_stages_ok(s, 1) || s.stage_fish[0] != 1 || nb_grew() != 1) NB_FAIL("N10d: %d grew, %d cards (fish 0: %d, fish 1: %d), want fish 0 only", nb_grew(), s.stage, s.stage_fish[0], s.stage_fish[1]);
        /* N10e: no night at all (a reboot at once): no card, even for a fish whose stage came awake and was already said */
        progression_save(&T);
        h = nb_boot(4289, false); s = nb_frames(20, 0); nb_show("N10e a reboot at once", h, s);
        if (s.stage) NB_FAIL("N10e: a reboot with no night said a stage (%d)", s.stage);
        /* N10f: the keeper's reset right after a night that grew a fish: the fresh tank owes nothing */
        nb_board(); nb_found(4290, true); progression_set_age(&T, 0, 1.0f * H); progression_save(&T); sim_clock_shift(7 * H);
        nb_boot(4291, false);
        progression_reset(&T, 4292); notice_sync(&T);
        s = nb_frames(10, 0); nb_show("N10f the keeper's reset right after a night that grew a fish", 0, s);
        if (s.stage) NB_FAIL("N10f: the fresh tank after a reset said a stage (%d)", s.stage);
    }

    /* N9c (D-5's review): a night that earns the badge AND grows a fish: the badge first (4 s), the card after (11 s); a cut
       at 8 s, the badge seen, the card not: the badge never again, and kept. (The card is lost: the badge's save took the
       growth - never twice, at worst unsaid) */
    nb_board(); nb_found(4301, false);
    progression_set_age(&T, 0, 1.0f * H); progression_save(&T);
    sim_clock_shift(7 * H);
    h = nb_boot(4302, false); s = nb_frames(8, 4); nb_show("N9c 7 h night + a fish grown, a cut at 8 s (badge seen, card not)", h, s);
    if (s.night != 1 || s.stage) NB_FAIL("N9c: staging: badge %d, card %d before the cut (want the badge only)", s.night, s.stage);
    h = nb_boot(4303, false); s = nb_frames(30, 4); nb_show("N9c the boot after the cut", h, s);
    if (s.night) NB_FAIL("N9c: the badge said AGAIN after the cut (%d)", s.night);
    if (!nb_night()) NB_FAIL("N9c: the badge lost by the cut");
    if (s.stage > 1) NB_FAIL("N9c: the card said %d times", s.stage);

    /* N10g (D-5's review): a fish grows up AWAKE, its card seen; a cut before the coalesced save (30 s) and the tank off
       3 min: the boot lives those minutes from the older save and grows it again - its card must not come twice */
    nb_board(); nb_found(4304, true); nb_boot(4306, false);   /* (a reboot: the badges staged by hand adopted, nothing queued) */
    progression_set_age(&T, 0, 2.0f * H - 10); { stage_t a = T.fish[0].stage; progression_save(&T);
    s = nb_frames(16, 0); nb_show("N10g an awake stage-up, its card seen (16 s, no save since)", 0, s);
    if (T.fish[0].stage <= a || s.stage != 1) NB_FAIL("N10g: staging: stage %d -> %d, card %d (want one)", a, T.fish[0].stage, s.stage); }
    sim_clock_shift(180);
    h = nb_boot(4305, false); s = nb_frames(30, 4); nb_show("N10g the boot after a cut and 3 min off", h, s);
    if (s.stage) NB_FAIL("N10g: the awake card said AGAIN after a cut (%d)", s.stage);

    sim_clock_freeze(0);
    if (nb_bad) { printf("night-badge: %d FAIL(s)\n", nb_bad); return 1; }
    printf("selftest-night-badge: FIRST FULL NIGHT'S SLEEP is said once at the boot that ends the night (cold, deep sleep, OTA, a SWAP IN off a DORMANT shelf); a fish that grew up in the night gets its card (N10, D-5); old badges, a short night, a PAUSED shelf, a restore, a fresh tank and a reset say nothing; a power cut never says it twice (N9)\n");
    return 0;
}
