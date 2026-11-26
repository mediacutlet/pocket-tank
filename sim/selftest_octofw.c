#define _DEFAULT_SOURCE
/* selftest_octofw.c - --selftest-octo-core1 (0.4.3 audit round 1, FW-1; V4's --v4-fw1 adopted). THE OCTOPUS'S PICTURE ON THE SECOND
 * CORE against the portrait built on the first. The board's order (firmware main.c): render_tank, octopus_async_prep -> core 1
 * rasterizes the resting animal's next picture while core 0 goes on to the card over the tank (render_stats_card ->
 * octopus_portrait). Here a thread is core 1, and the test registers the board's wait (octopus_set_async_wait: main.c's ob_wait).
 *   mode 0  the control: no portrait
 *   mode 1  a portrait rebuilt (a new size) on core 0 every frame core 1 is rastering: every picture sent is taken up (it was
 *           ~1% before the fix: the portrait's set-aside threw core 1's picture away), and every picture taken is the fresh
 *           build at its own pose (the sack cache shared by two cores at once: none may be torn)
 *   mode 2  a portrait after core 1 is done (serial)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "tank.h"
#include "advisor.h"
#include "progression.h"
#include "render.h"
#include "octopus.h"

const char *sim_shelf_dir(void);
goal_t advisor_rules(const tank_t *t, int fish_idx, bool request);
int octopus_debug_sack_check(void);
int octopus_debug_async_takes(void);
extern void octopus_set_async_wait(void (*wait)(void)) __attribute__((weak));   /* (weak: the test also builds on the tree before the fix) */
static tank_t T;
enum { I_OCTO = 11 };
static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static volatile int g_go, g_done = 1, g_quit;
static void *core1(void *a) {
    (void)a;
    for (;;) {
        pthread_mutex_lock(&g_m); while (!g_go && !g_quit) pthread_cond_wait(&g_cv, &g_m);
        if (g_quit) { pthread_mutex_unlock(&g_m); return NULL; }
        g_go = 0; pthread_mutex_unlock(&g_m);
        octopus_async_build();
        pthread_mutex_lock(&g_m); g_done = 1; pthread_cond_broadcast(&g_cv); pthread_mutex_unlock(&g_m);
    }
}
static void core1_wait(void) { pthread_mutex_lock(&g_m); while (!g_done) pthread_cond_wait(&g_cv, &g_m); pthread_mutex_unlock(&g_m); }   /* (main.c's ob_wait) */
int selftest_octo_core1(void) {
    static uint16_t fb[TANK_W * TANK_H];
    int fails = 0;
    const int frames = getenv("PT_FW1_FRAMES") ? atoi(getenv("PT_FW1_FRAMES")) : 1500;
    char sav[600]; snprintf(sav, sizeof sav, "%s-octocore1-%dx%d.sav", sim_shelf_dir(), TANK_W, TANK_H); setenv("POCKET_TANK_SAVE", sav, 1); remove(sav);
    pthread_t th; pthread_create(&th, NULL, core1, NULL);
    if (octopus_set_async_wait && !getenv("PT_FW1_NOWAIT")) octopus_set_async_wait(core1_wait);   /* (PT_FW1_NOWAIT=1: the control, the board before the fix - must FAIL) */
    for (int mode = 0; mode < 3; mode++) {
        tank_init(&T, 5); progression_boot(&T); progression_setup_done(&T); T.sd_balance = 100000; progression_buy(&T, I_OCTO);
        T.light_override = true; T.light_on = true; T.oc_growth = 40000;
        for (int i = 0; i < 60; i++) { tank_tick(&T, 0.04f, advisor_rules); tank_handled(&T); }
        octopus_think(0); for (int i = 0; i < 100; i++) tank_tick(&T, 0.04f, advisor_rules);
        octopus_set_async(1); (void)octopus_debug_async_takes();
        int sent = 0, bad = 0, ports = 0, checked = 0, took = 0;
        for (int f = 0; f < frames; f++) {
            core1_wait();                                              /* (the tick's ob_wait) */
            tank_tick(&T, 0.04f, advisor_rules);
            took += octopus_debug_async_takes();
            render_tank(&T, fb, TANK_W);
            const int now = octopus_debug_async_takes(); took += now;
            if (now > 0) { checked++; if (octopus_debug_sack_check() > 0) bad++; }   /* (a picture just taken: its pose is the animal's) */
            if (octopus_async_prep()) {
                sent++;
                pthread_mutex_lock(&g_m); g_done = 0; g_go = 1; pthread_cond_broadcast(&g_cv); pthread_mutex_unlock(&g_m);
                if (mode == 1) { int mx, fy; (void)octopus_portrait(f % 256, &mx, &fy); ports++; }
                if (mode == 2) { core1_wait(); int mx, fy; (void)octopus_portrait(f % 256, &mx, &fy); ports++; }
            }
        }
        core1_wait(); tank_tick(&T, 0.04f, advisor_rules); render_tank(&T, fb, TANK_W); took += octopus_debug_async_takes();
        static const char *MN[3] = { "control (no portrait)", "a portrait on core 0 WHILE core 1 builds", "a portrait after core 1 is done" };
        printf("octo-core1 %-42s: %d frames, %d pictures sent to core 1, %d taken up (%d thrown away), %d portraits; of %d taken, %d not the fresh build\n",
               MN[mode], frames, sent, took, sent - took, ports, checked, bad);
        if (sent < frames / 10) { printf("FAIL: octo-core1 %s: only %d pictures went to core 1 (the run proves nothing)\n", MN[mode], sent); fails++; }
        if ((sent - took) * 50 > sent) { printf("FAIL: octo-core1 %s: %d of %d pictures core 1 finished were thrown away\n", MN[mode], sent - took, sent); fails++; }
        if (bad) { printf("FAIL: octo-core1 %s: %d pictures taken up differ from a fresh build at their pose (a torn sack)\n", MN[mode], bad); fails++; }
        octopus_set_async(0); tank_octopus_release(&T);
    }
    if (octopus_set_async_wait) octopus_set_async_wait(NULL);
    pthread_mutex_lock(&g_m); g_quit = 1; pthread_cond_broadcast(&g_cv); pthread_mutex_unlock(&g_m); pthread_join(th, NULL);
    remove(sav);
    printf("selftest-octo-core1: %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
