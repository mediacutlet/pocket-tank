#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_boot_light.c - --selftest-boot-light (0.4.3, C1704 #help 11-13 "Lights out sound playing when booting"):
 * a tank whose light the keeper left off played the lights-out cue at every boot - a cold boot, the wake after the
 * 20 min standby's deep sleep, a MY TANKS swap (a restart too). tank_init leaves t->night false, the save restores the
 * double-tap's light_manual_off, and the first tank_tick saw false -> true: a switch. A restored light is a state, not
 * a switch (tank.h light_known). The frames run in firmware main.c's order (hold_light, tank_tick, progression_tick).
 *
 *   B1  a cold boot of a dark tank: dark from the first frame, no light cue, in 5 s of frames
 *   B2  the deep sleep's wake (progression_wake an hour on) of a dark tank: the same
 *   B3  a lit tank's boot: no light cue (the lights-on cue never played at boot - it stays so)
 *   B4  after a dark boot the keeper's double-tap still sounds: on = one LIGHTS ON cue, off again = one LIGHTS OUT
 *   B5  AUTO at 5 s: the boot is silent, the idle lights-out 5 s later is one LIGHTS OUT cue (a real one)
 *   B6  the other cues the first frames of a dark boot raise: none at all (a critter's, the bubbles', a notice's)
 * PT_BOOT_LIGHT_OLD=1: the control - the first tick treated as any other (light_known set before it, the code before
 * the fix): B1 and B2 must FAIL, and show the test sees the bug. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tank.h"
#include "tank_events.h"
#include "advisor.h"
#include "progression.h"
#include "notice.h"
#include "setup.h"
#include "collection.h"

const char *sim_shelf_dir(void);

static tank_t T;
static int bl_bad;
static int s_ev[TEV_COUNT], s_notice_cues;
#define BL_FAIL(...) do { printf("boot-light: FAIL: "); printf(__VA_ARGS__); printf("\n"); bl_bad++; } while (0)
#define DT 0.05f

static void bl_count(int ev, int fish, void *ud) { (void)fish; (void)ud; if (ev >= 0 && ev < TEV_COUNT) s_ev[ev]++; }
static void bl_zero(void) { memset(s_ev, 0, sizeof s_ev); s_notice_cues = 0; }
static int  bl_all(void) { int n = 0; for (int i = 0; i < TEV_COUNT; i++) n += s_ev[i]; return n + s_notice_cues; }

static void bl_board(void) {                     /* a scratch board of its own ($PT_SHELF_DIR/boot-light) */
    char cmd[800], d[600], p[700];
    snprintf(d, sizeof d, "%s/boot-light", sim_shelf_dir());
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", d, d); (void)system(cmd);
    snprintf(p, sizeof p, "%s/tank.sav", d); setenv("POCKET_TANK_SAVE", p, 1);
    snprintf(p, sizeof p, "%s/storage.bin", d); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", d); setenv("POCKET_TANK_FRAME", p, 1);
}
static void bl_old(void) { if (getenv("PT_BOOT_LIGHT_OLD")) T.light_known = true; }   /* the control: no first tick */
static void bl_boot(uint32_t seed) {             /* the power on: the tank as its save has it */
    setup_cancel(&T);
    memset(&T, 0, sizeof T); tank_init(&T, seed); progression_boot(&T); notice_sync(&T); bl_old();
}
static void bl_wake(uint32_t seed, int64_t now) { /* the deep sleep's wake: RAM gone, the save and the clock left */
    setup_cancel(&T);
    memset(&T, 0, sizeof T); tank_init(&T, seed); progression_wake(&T, now); progression_woke(&T); notice_sync(&T); bl_old();
}
static void bl_frame(void) {                     /* one frame, as the tank task runs it */
    T.hold_light = setup_active() && !setup_is_about(); T.ui_cover = T.hold_light;
    tank_tick(&T, DT, advisor_rules);
    progression_tick(&T, DT);
    notice_tick(&T, DT, T.ui_cover || setup_birth_due(&T));
    if (notice_take_cue() >= 0) s_notice_cues++;
}
/* secs of frames; the light must read `night` from the first one */
static void bl_run(float secs, bool night, const char *what) {
    for (float at = 0; at < secs; at += DT) {
        bl_frame();
        if (T.night != night) { BL_FAIL("%s: the light read %s at %.2f s", what, T.night ? "OFF" : "ON", (double)at); return; }
    }
}
static void bl_light(bool on) { T.light_auto = false; T.light_manual_off = !on; }   /* the keeper's double-tap */
static void bl_save(void) { if (!progression_save(&T)) BL_FAIL("the save failed"); }

int selftest_boot_light(void) {
    tank_events_set(bl_count, NULL);
    bl_board(); collection_reset();
    bl_boot(11); progression_setup_done(&T); progression_tick(&T, 0.1f);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    for (int i = 0; i < 20; i++) bl_frame();
    if (T.night) BL_FAIL("a fresh tank is dark");
    /* the keeper turns the light off and the tank is saved: one cue, as ever */
    bl_zero(); bl_light(false); bl_run(1.0f, true, "the double-tap off");
    if (s_ev[TEV_LIGHT_OFF] != 1) BL_FAIL("the keeper's lights-out raised %d cues, not 1", s_ev[TEV_LIGHT_OFF]);
    bl_save();

    /* B1 the cold boot of the dark tank */
    bl_zero(); bl_boot(12); bl_run(5.0f, true, "B1 the dark boot");
    printf("boot-light: B1 cold boot, light off: %d lights-out, %d lights-on cues\n", s_ev[TEV_LIGHT_OFF], s_ev[TEV_LIGHT_ON]);
    if (s_ev[TEV_LIGHT_OFF] || s_ev[TEV_LIGHT_ON]) BL_FAIL("B1: a dark tank's boot played the light cue (%d off, %d on)", s_ev[TEV_LIGHT_OFF], s_ev[TEV_LIGHT_ON]);
    /* B6 nothing else either in the first frames of the dark boot (counted from B1's boot) */
    printf("boot-light: B6 every cue in B1's 5 s: %d (tank events + notice chimes)\n", bl_all());
    if (bl_all()) {
        for (int i = 0; i < TEV_COUNT; i++) if (s_ev[i]) printf("boot-light:   %s x%d\n", TANK_EVENT_NAMES[i], s_ev[i]);
        BL_FAIL("B6: the dark boot raised %d cues", bl_all());
    }
    bl_save();

    /* B2 the wake from the standby's deep sleep, an hour on */
    bl_zero(); bl_wake(13, progression_loaded_unix() + 3600); bl_run(5.0f, true, "B2 the dark wake");
    printf("boot-light: B2 deep-sleep wake, light off: %d lights-out, %d lights-on cues\n", s_ev[TEV_LIGHT_OFF], s_ev[TEV_LIGHT_ON]);
    if (s_ev[TEV_LIGHT_OFF] || s_ev[TEV_LIGHT_ON]) BL_FAIL("B2: a dark tank's wake played the light cue (%d off, %d on)", s_ev[TEV_LIGHT_OFF], s_ev[TEV_LIGHT_ON]);

    /* B4 after the dark boot, the keeper's double-tap still sounds both ways */
    bl_zero(); bl_light(true); bl_run(1.0f, false, "B4 on");
    if (s_ev[TEV_LIGHT_ON] != 1 || s_ev[TEV_LIGHT_OFF]) BL_FAIL("B4: the light on after a dark boot: %d on, %d off cues (1, 0)", s_ev[TEV_LIGHT_ON], s_ev[TEV_LIGHT_OFF]);
    bl_zero(); bl_light(false); bl_run(1.0f, true, "B4 off");
    if (s_ev[TEV_LIGHT_OFF] != 1 || s_ev[TEV_LIGHT_ON]) BL_FAIL("B4: the light off again: %d off, %d on cues (1, 0)", s_ev[TEV_LIGHT_OFF], s_ev[TEV_LIGHT_ON]);
    printf("boot-light: B4 the double-tap after a dark boot: one cue each way\n");

    /* B3 a lit tank's boot */
    bl_light(true); bl_run(0.5f, false, "B3 lit before the save"); bl_save();
    bl_zero(); bl_boot(14); bl_run(5.0f, false, "B3 the lit boot");
    printf("boot-light: B3 cold boot, light on: %d lights-on, %d lights-out cues\n", s_ev[TEV_LIGHT_ON], s_ev[TEV_LIGHT_OFF]);
    if (s_ev[TEV_LIGHT_OFF] || s_ev[TEV_LIGHT_ON]) BL_FAIL("B3: a lit tank's boot played the light cue (%d on, %d off)", s_ev[TEV_LIGHT_ON], s_ev[TEV_LIGHT_OFF]);

    /* B5 AUTO at 5 s: a silent boot, then the idle lights-out is a real one */
    T.light_auto = true; T.light_idle_s = 5; T.light_manual_off = false; bl_save();
    bl_zero(); bl_boot(15);
    if (!T.light_auto || T.light_idle_s != 5) BL_FAIL("B5: AUTO at 5 s did not survive the save");
    bl_run(4.0f, false, "B5 the AUTO boot, still lit");
    if (s_ev[TEV_LIGHT_OFF] || s_ev[TEV_LIGHT_ON]) BL_FAIL("B5: the AUTO boot played the light cue (%d off, %d on)", s_ev[TEV_LIGHT_OFF], s_ev[TEV_LIGHT_ON]);
    for (int i = 0; i < 60 && !T.night; i++) bl_frame();
    if (!T.night || s_ev[TEV_LIGHT_OFF] != 1) BL_FAIL("B5: AUTO's idle lights-out: dark %d, %d cues (1)", T.night, s_ev[TEV_LIGHT_OFF]);
    else printf("boot-light: B5 AUTO: a silent boot, one lights-out cue when the idle turned it off\n");
    T.light_auto = false; T.light_manual_off = false; bl_save();

    tank_events_set(NULL, NULL);
    if (bl_bad) { printf("boot-light: %d FAIL%s\n", bl_bad, getenv("PT_BOOT_LIGHT_OLD") ? " (the control: as it must)" : ""); return 1; }
    printf("selftest-boot-light: PASS (B1-B6: a restored light is no switch; the keeper's and AUTO's are)\n");
    return 0;
}
