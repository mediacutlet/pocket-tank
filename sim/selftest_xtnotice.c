#define _DEFAULT_SOURCE   /* setenv, mkdtemp under -std=c11 (glibc) */
/* selftest_xtnotice.c - THE EXPERIMENTAL TANK'S THANK-YOU (0.4.3, common/xt_notice.h)
 *
 *   ./fishsim --xt-notice-shot <prefix>   the card over a jelly tank, the piranhas and the minnows (.ppm)
 *   ./fishsim --selftest-xtnotice         headless:
 *     A  the first open of each experimental tank (a jelly tank, PIRANHAS, MINNOWS) says it, after its wait, not before
 *     B  the second open of each does not; nor after a "restart" (the module opened afresh, the record read back)
 *     C  never on a fish tank
 *     D  a press begun before it came up is spent (it stays); a tap after closes it
 *     E  MY TANKS over it (BOOT twice) closes it, and it is already seen; a page over it before it came holds it
 *     F  the record: one from a build with none (an update) says it once; a damaged one is no list; full, the oldest goes
 *     G  ERASE EVERYTHING lets the record go (the shelf's ids start again at 2)
 *     H  closed, the module holds nothing up and takes no tap (the port itself: --selftest-touch-port, xt_notice_port)
 * Its record lives in a fresh directory of its own (POCKET_TANK_FRAME): never another test's fixed path. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "xt_notice.h"
#include "shelf.h"
#include "render.h"
#include "jelly.h"
#include "school.h"
#include "piranha_spike.h"

void jelly_sim_theme_frame(int theme, float seconds, int jet, uint16_t *out);   /* jelly_sim.c */

static uint16_t s_fb[TANK_W * TANK_H];
static void ppm(const char *path, const uint16_t *fb) {
    FILE *f = fopen(path, "wb"); if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        uint16_t p = fb[i];
        unsigned char rgb[3] = { (unsigned char)(((p >> 11) & 31) << 3), (unsigned char)(((p >> 5) & 63) << 2), (unsigned char)((p & 31) << 3) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int xt_notice_shot(const char *prefix) {
    char p[600];
    static uint16_t base[TANK_W * TANK_H];
    jelly_sim_theme_frame(0, 20, 0, base);
    memcpy(s_fb, base, sizeof s_fb); render_xt_notice(s_fb, TANK_W);
    snprintf(p, sizeof p, "%s_jelly.ppm", prefix); ppm(p, s_fb);
    memcpy(s_fb, base, sizeof s_fb); snprintf(p, sizeof p, "%s_jelly_bare.ppm", prefix); ppm(p, s_fb);
    {   static uint16_t room[TANK_W * TANK_H];
        piranha_spike_t *pp = malloc(piranha_spike_size());
        piranha_spike_build(pp, room, TANK_W, TANK_H, 8);
        for (int f = 0; f < 150; f++) piranha_spike_frame(pp, s_fb, TANK_W, f * 0.04, false);
        free(pp);
        render_xt_notice(s_fb, TANK_W); snprintf(p, sizeof p, "%s_piranha.ppm", prefix); ppm(p, s_fb);
    }
    {   static school_t sc; school_init(&sc, 5, 448);
        for (int f = 0; f < 400; f++) school_tick(&sc, 0.04f);
        school_render(&sc, s_fb, TANK_W);
        render_xt_notice(s_fb, TANK_W); snprintf(p, sizeof p, "%s_minnows.ppm", prefix); ppm(p, s_fb);
    }
    printf("xt-notice-shot: %s_* (%dx%d)\n", prefix, TANK_W, TANK_H);
    return 0;
}

/* ---- the selftest ---- */
static int g_fail;
static void expect(bool ok, const char *what) { printf("selftest-xtnotice: %s %s\n", what, ok ? "ok" : "FAIL"); g_fail += !ok; }
static int frames_to_up(float limit_s) {        /* 40 ms frames, the tank bare, until it comes up: the frame count, or -1 */
    for (int i = 1; i <= (int)(limit_s * 25); i++) { xt_notice_tick(0.04f, false); if (xt_notice_up()) return i; }
    return -1;
}
static bool shows(uint32_t id, bool experimental) { xt_notice_open(id, experimental); return frames_to_up(10) > 0; }

int selftest_xtnotice(void) {
    char dir[] = "/tmp/pt-xtnotice-XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    setenv("POCKET_TANK_FRAME", dir, 1);
    char rec[700]; snprintf(rec, sizeof rec, "%s/%s", dir, XT_SEEN_KEY);
    enum { JELLY = 7, PIRANHA = 8, MINNOWS = 9, JELLY2 = 12, FISH = 1 };

    /* A. first opens */
    xt_notice_open(JELLY, true);
    int n = frames_to_up(10);
    expect(n == (int)(XT_NOTICE_WAIT_S * 25 + 0.5f) || n == (int)(XT_NOTICE_WAIT_S * 25 + 0.5f) + 1, "A: the jelly tank's first open says it after its wait (1.5 s of bare tank)");
    expect(xt_notice_seen(JELLY), "A: ... and is in the seen list the frame it comes up");
    expect(shows(PIRANHA, true), "A: PIRANHAS' first open says it");
    expect(shows(MINNOWS, true), "A: MINNOWS' first open says it");
    expect(shows(JELLY2, true), "A: a second jelly tank (its own shelf id) says it too");

    /* B. never twice */
    expect(!shows(JELLY, true) && !shows(PIRANHA, true) && !shows(MINNOWS, true) && !shows(JELLY2, true),
           "B: the second open of each says nothing (a restart, a sleep, a swap back: the module opened afresh, the list read back)");

    /* C. a fish tank */
    expect(!shows(FISH, false) && !shows(42, false), "C: never on a fish tank");
    expect(!xt_notice_seen(FISH) && !xt_notice_seen(42), "C: ... and a fish tank is never written into the list");

    /* D. THE TOUCH RULE */
    remove(rec);
    xt_notice_open(JELLY, true); frames_to_up(10);
    for (int i = 0; i < 10; i++) xt_notice_tick(0.04f, false);           /* up 0.4 s */
    expect(xt_notice_tap(0.6f) && xt_notice_up(), "D: a press begun before it came up (held 0.6 s, it has been up 0.4 s) is spent: it stays");
    expect(xt_notice_tap(0.45f) && xt_notice_up(), "D: ... one begun 0.05 s before it came is spent too");
    expect(xt_notice_tap(0.1f) && !xt_notice_up(), "D: a tap after it came closes it");
    expect(!xt_notice_tap(0.1f), "D: with it gone a tap goes on to the tank");
    for (int i = 0; i < 400; i++) xt_notice_tick(0.04f, false);
    expect(!xt_notice_up(), "D: closed, it does not come back in the same boot");
    xt_notice_open(PIRANHA, false);                                    /* (a reset of state between cases) */
    remove(rec);
    xt_notice_open(JELLY, true); frames_to_up(10);
    for (int i = 0; i < 2000; i++) xt_notice_tick(0.04f, false);
    expect(xt_notice_up(), "D: it waits for a tap (80 s up, still there: a kid who looked away still reads it)");

    /* E. BOOT twice, a page */
    xt_notice_tick(0.04f, true);
    expect(!xt_notice_up() && xt_notice_seen(JELLY), "E: MY TANKS over it (BOOT twice) closes it - it is already seen");
    for (int i = 0; i < 100; i++) xt_notice_tick(0.04f, false);
    expect(!xt_notice_up(), "E: ... and back from MY TANKS without a swap it does not come again");
    remove(rec);
    xt_notice_open(MINNOWS, true);
    for (int i = 0; i < 150; i++) xt_notice_tick(0.04f, i % 30 == 29);  /* a page every 1.2 s, 5.8 s bare in all: the wait begins again each time */
    expect(!xt_notice_up() && !xt_notice_seen(MINNOWS), "E: a page over the tank before it came holds it (5.8 s bare in all, never 1.5 s at once: the wait begins again, nothing written)");
    expect(frames_to_up(10) > 0, "E: ... and once the tank is bare for the wait it comes");

    /* F. the record */
    remove(rec);
    expect(shows(JELLY, true), "F: no record (a board that opened its jelly tank before 0.4.3): it says thank you once after the update");
    expect(!shows(JELLY, true), "F: ... once");
    { FILE *f = fopen(rec, "wb"); uint32_t bad[3] = { 0x12345678u, 1, JELLY }; fwrite(bad, 4, 3, f); fclose(f); }
    expect(shows(JELLY, true), "F: a record with another magic is no list (it says it, and writes a good one)");
    expect(!shows(JELLY, true), "F: ... then not again");
    { FILE *f = fopen(rec, "wb"); uint32_t bad[3] = { XT_SEEN_MAGIC, 5, JELLY }; fwrite(bad, 4, 3, f); fclose(f); }
    expect(shows(JELLY, true), "F: a record shorter than its count is no list");
    remove(rec);
    for (uint32_t id = 100; id < 100 + XT_SEEN_MAX; id++) shows(id, true);
    expect(xt_notice_seen(100) && xt_notice_seen(100 + XT_SEEN_MAX - 1), "F: the list holds XT_SEEN_MAX tanks");
    shows(500, true);
    expect(!xt_notice_seen(100) && xt_notice_seen(101) && xt_notice_seen(500), "F: one more lets the oldest go");

    /* G. ERASE EVERYTHING */
    {   char st[700], sv[700]; snprintf(st, sizeof st, "%s/storage.bin", dir); snprintf(sv, sizeof sv, "%s/tank.sav", dir);
        setenv("POCKET_TANK_STORAGE", st, 1); setenv("POCKET_TANK_SAVE", sv, 1);
        shelf_boot(1700000000);
        remove(rec); shows(2, true);
        expect(xt_notice_seen(2), "G: (a tank with shelf id 2 seen)");
        shelf_erase_all();
        expect(!xt_notice_seen(2), "G: ERASE EVERYTHING lets the list go: the next tank given id 2 says thank you");
        unsetenv("POCKET_TANK_STORAGE"); unsetenv("POCKET_TANK_SAVE");
        remove(st); remove(sv);
    }

    /* H. nothing while it is not up */
    xt_notice_open(JELLY, true); frames_to_up(10); xt_notice_tick(0.04f, false); xt_notice_tap(0);
    expect(!xt_notice_up() && !xt_notice_tap(0.1f), "H: closed: the module keeps nothing up, a tap goes on (the port's side: --selftest-touch-port)");

    remove(rec);
    char cmd[800]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir); if (system(cmd)) {}
    printf("selftest-xtnotice: %s (%d failed)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
