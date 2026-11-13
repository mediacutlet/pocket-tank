/* selftest_xtclock.c - --selftest-xtclock (0.4.2 audit round 2, FW2-1): the experimental tanks' clocks over a long time awake.
 *
 * The jelly tank, the minnows and the piranha tank each kept a clock in a float that ran for as long as the board was
 * awake (zeroed only at a boot). A float of seconds loses its small change as it grows: the piranhas' frame step, taken
 * as clock - (float)last clock, was uneven by 5% at 18 h, 40% at 3 days; the jellies' and the crumbs' clock ran fast and
 * slow from a day and a half and stopped at 12.1 days (2^20 s), with the tentacles' ripple, the frills and the specks.
 * The fix (common/jelly.c, school.c, piranha_spike.c): the piranhas' last clock is a double; each float clock laps every
 * 20000 pi s, a whole number of turns of every fixed rate that reads it; the one rate that is not (a tentacle's own
 * ripple) carries the turns the laps left over (jelly_tank_t.laps); the piranhas' tap and rings are re-based at a lap.
 *   A  at 1 h, 18 h, 3 days, 13 days and 60 days awake: ten seconds of 40 ms frames move the jellies' and the crumbs'
 *      clock ten seconds (3%), and every piranha step is the frame's 40 ms (1%)
 *   B  below the first lap the clocks are the old float sums, bit for bit (nothing changed in a tank's first 17.5 h)
 *   C  the lap is a whole number of turns of every fixed rate (the list is the readers', by hand: keep it with them)
 *   D  the jellies across a lap: the frame is the frame of a clock that did not lap (the control, a lap that forgets
 *      the turns left over, is another frame)
 *   E  the piranhas across a lap: a short tap still feeds, a long press still does not, a ring on the surface keeps
 *      its age, the step is the frame's
 * Built alone too (the audit's harness): cc -DXTCLOCK_MAIN -I common sim/selftest_xtclock.c common/{jelly,school,piranha_spike}.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "jelly.h"
#include "school.h"
#include "piranha_spike.h"

float piranha_spike_test(piranha_spike_t *R, int what, float set);   /* piranha_spike.c, the sim's alone */

#define LAP 62831.853f
static int g_fail;
static void expect(bool ok, const char *what) { printf("selftest-xtclock: %s %s\n", what, ok ? "ok" : "FAIL"); g_fail += !ok; }

static jelly_tank_t J, J2;
static school_t S;
static uint16_t fa[TANK_W * TANK_H], fr[TANK_W * TANK_H], fk[TANK_W * TANK_H];

/* the clock a tank awake for `up` seconds holds: laps and the rest when the engine laps, the bare float when it does not */
static float at_uptime(double up, bool laps, int *n) {
    if (!laps) { *n = 0; return (float)up; }
    *n = (int)(up / (double)LAP); return (float)(up - *n * (double)LAP);
}
static long frame_diff(const uint16_t *a, const uint16_t *b) { long n = 0; for (int i = 0; i < TANK_W * TANK_H; i++) n += a[i] != b[i]; return n; }

int selftest_xtclock(void) {
    static const double up[] = { 3600, 18 * 3600.0, 3 * 86400.0, 13 * 86400.0, 60 * 86400.0 };
    static const char *const upn[] = { "1 h", "18 h", "3 days", "13 days", "60 days" };
    char msg[200];
    jelly_set_bg_cache(NULL);

    /* B. below the lap: the old float sums, bit for bit */
    {   static const float c0[] = { 0, 3600, 61000 };
        bool same = true;
        for (int k = 0; k < 3; k++) {
            jelly_theme_start(&J, 0); school_init(&S, 1, 64);
            float want = c0[k]; J.clock = c0[k]; S.t = c0[k];
            for (int i = 0; i < 250; i++) { jelly_tick(&J, 0.04f); school_tick(&S, 0.04f); want += 0.04f; }
            same = same && memcmp(&J.clock, &want, 4) == 0 && memcmp(&S.t, &want, 4) == 0 && J.laps == 0;
        }
        expect(same, "B: below the first lap (from 0, 1 h, 16.9 h) the jellies' and the crumbs' clocks are the old float sums, bit for bit");
    }

    /* does each engine lap? */
    jelly_theme_start(&J, 0); J.clock = LAP - 0.01f; jelly_tick(&J, 0.04f);
    school_init(&S, 1, 64); S.t = LAP - 0.01f; school_tick(&S, 0.04f);
    bool jlap = J.clock < 1 && J.laps == 1, slap = S.t < 1;
    expect(jlap, "the jellies' clock laps at 20000 pi s and counts the lap");
    expect(slap, "the crumbs' clock laps at 20000 pi s");

    /* A. ten seconds of frames at each time awake */
    piranha_spike_t *P = malloc(piranha_spike_size()); uint16_t *room = calloc(TANK_W * TANK_H, 2), *fb = calloc(TANK_W * TANK_H, 2);
    piranha_spike_build(P, room, TANK_W, TANK_H, 8);
    double clock = 0; piranha_spike_frame(P, fb, TANK_W, clock, false);
    for (unsigned k = 0; k < sizeof up / sizeof *up; k++) {
        int n; float c0 = at_uptime(up[k], jlap, &n);
        jelly_theme_start(&J, 0); J.clock = c0; J.laps = n;
        for (int i = 0; i < 250; i++) jelly_tick(&J, 0.04f);
        float dj = J.clock - c0 + (J.laps - n) * LAP;
        c0 = at_uptime(up[k], slap, &n);
        school_init(&S, 1, 64); S.t = c0;
        for (int i = 0; i < 250; i++) school_tick(&S, 0.04f);
        float ds = S.t - c0; if (ds < -1) ds += LAP;
        /* the piranhas: the caller's clock jumps to this time awake (one clamped frame), then 250 steady frames */
        clock = up[k]; piranha_spike_frame(P, fb, TANK_W, clock, false);
        float lo = 9, hi = -9, t0 = piranha_spike_test(P, 0, -1);
        for (int i = 0; i < 250; i++) {
            clock += 0.04; piranha_spike_frame(P, fb, TANK_W, clock, false);
            float t1 = piranha_spike_test(P, 0, -1), d = t1 - t0; t0 = t1;
            if (d < lo) lo = d;
            if (d > hi) hi = d;
        }
        bool ok = fabsf(dj - 10) < 0.3f && fabsf(ds - 10) < 0.3f && lo > 0.0396f && hi < 0.0404f;
        snprintf(msg, sizeof msg, "A: awake %s: 10 s of 40 ms frames move the jellies' clock %.3f s, the crumbs' %.3f s; a piranha step is %.2f .. %.2f ms",
                 upn[k], (double)dj, (double)ds, (double)lo * 1e3, (double)hi * 1e3);
        expect(ok, msg);
    }

    /* C. every fixed rate that reads a lapped clock: a whole number of turns a lap (within 0.02 rad, a float's lap) */
    {   static const float rate[] = { 2.0f, 1.7f, 1.6f, 1.9f, 2.2f, 2.5f, 2.8f, 3.1f, 3.4f, 3.7f,      /* jelly.c: the frills, the specks' 1.6 + 0.3 k */
                                      2.1f,                                                                /* school.c: the crumbs' sway */
                                      0.029f, 0.045f, 0.07f, 0.11f, 0.13f, 0.21f, 0.3f, 0.8f, 1.6f, 1.6f * 0.7f };   /* piranha_spike.c: the shoal's anchor, the meat, the motes, the orbit */
        double worst = 0;
        for (unsigned i = 0; i < sizeof rate / sizeof *rate; i++) {
            double m = fmod((double)LAP * (double)rate[i], 6.283185307179586); if (m > 3.14159265) m = 6.283185307179586 - m;
            if (m > worst) worst = m;
        }
        snprintf(msg, sizeof msg, "C: the lap is a whole number of turns of all %d fixed rates (the worst is %.4f rad off)", (int)(sizeof rate / sizeof *rate), worst);
        expect(worst < 0.02, msg);
    }

    /* D. the jellies across a lap: the frame of a clock that did not lap */
    if (jlap) {
        jelly_theme_start(&J, 0);
        for (int i = 0; i < 250; i++) jelly_tick(&J, 0.04f);
        J.clock = LAP - 0.02f; jelly_tick(&J, 0.04f);                 /* the lap */
        bool lapped = J.laps == 1 && J.clock < 0.03f;
        jelly_render(&J, fa, TANK_W);
        J2 = J; J2.laps = 0; J2.clock = LAP + J.clock; jelly_render(&J2, fr, TANK_W);   /* the same water, a clock that ran on */
        J2 = J; J2.laps = 0; jelly_render(&J2, fk, TANK_W);                             /* the control: the lap with the turns forgotten */
        J2 = J; jelly_render(&J2, fb, TANK_W);                                          /* (the same tank twice: the same frame) */
        long d = frame_diff(fa, fr), dk = frame_diff(fk, fr), d0 = frame_diff(fa, fb);
        snprintf(msg, sizeof msg, "D: the jellies' frame after a lap differs from the unlapped clock's in %ld px (the same tank twice: %ld; the control, turns forgotten: %ld)", d, d0, dk);
        expect(lapped && d0 == 0 && dk > 200 && d * 20 < dk, msg);
        /* two laps on, the count goes on carrying */
        J.clock = LAP - 0.02f; jelly_tick(&J, 0.04f); jelly_render(&J, fa, TANK_W);
        J2 = J; J2.laps = 1; J2.clock = LAP + J.clock; jelly_render(&J2, fr, TANK_W);
        d = frame_diff(fa, fr);
        snprintf(msg, sizeof msg, "D: the second lap: %ld px", d);
        expect(J.laps == 2 && d * 20 < dk, msg);
    }

    /* E. the piranhas across a lap */
    {   /* how long a drop takes to reach the surface (its ring) */
        int fall = 0;
        piranha_spike_feed(P, TANK_W * 0.3f);
        while (piranha_spike_test(P, 1, -1) < 0 && fall < 100) { clock += 0.04; piranha_spike_frame(P, fb, TANK_W, clock, false); fall++; }
        /* a ring born 0.4 s before the lap keeps its age through it */
        piranha_spike_test(P, 0, LAP - 0.4f - fall * 0.04f);
        piranha_spike_feed(P, TANK_W * 0.6f);
        for (int i = 0; i < fall - 1; i++) { clock += 0.04; piranha_spike_frame(P, fb, TANK_W, clock, false); }
        bool ring_ok = fall > 0 && fall < 100, step_ok = true, crossed = false; float age0 = -1, tb = piranha_spike_test(P, 0, -1);
        for (int i = 0; i < 30; i++) {
            clock += 0.04; piranha_spike_frame(P, fb, TANK_W, clock, false);
            float age = piranha_spike_test(P, 1, -1), tn = piranha_spike_test(P, 0, -1), d = tn - tb;
            if (d < -1) { d += LAP; crossed = true; }
            step_ok = step_ok && fabsf(d - 0.04f) < 0.005f; tb = tn;                      /* (a float near the lap splits a second 256 ways) */
            if (age0 >= 0 && age0 < 1.5f) ring_ok = ring_ok && fabsf(age - age0 - 0.04f) < 0.005f;
            if (i > 0 || age < 0.2f) age0 = age;
        }
        snprintf(msg, sizeof msg, "E: the piranhas' clock laps; the step across it is the frame's; a ring born before the lap keeps its age (%.2f s after 30 frames)", (double)age0);
        expect(crossed && step_ok && ring_ok && age0 > 1.0f && age0 < 1.4f, msg);
        /* a short tap across the lap feeds; a long press across it does not */
        piranha_spike_test(P, 0, LAP - 0.1f);
        piranha_spike_touch(P, 100, 100, true);
        for (int i = 0; i < 5; i++) { clock += 0.04; piranha_spike_frame(P, fb, TANK_W, clock, false); }
        piranha_spike_touch(P, 100, 100, false);
        bool tap = piranha_spike_test(P, 2, -1) == 1 && piranha_spike_test(P, 0, -1) < 1;
        clock += 0.04; piranha_spike_frame(P, fb, TANK_W, clock, false);
        piranha_spike_test(P, 0, LAP - 0.5f);
        piranha_spike_touch(P, 100, 100, true);
        for (int i = 0; i < 30; i++) { clock += 0.04; piranha_spike_frame(P, fb, TANK_W, clock, false); }
        piranha_spike_touch(P, 100, 100, false);
        bool press = piranha_spike_test(P, 2, -1) == 0 && piranha_spike_test(P, 0, -1) < 1;
        expect(tap, "E: a 0.2 s tap across the lap drops meat");
        expect(press, "E: a 1.2 s press across the lap drops none");
    }
    free(P); free(room); free(fb);
    printf("selftest-xtclock: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
#ifdef XTCLOCK_MAIN
int main(void) { return selftest_xtclock(); }
#endif
