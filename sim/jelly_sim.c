/* jelly_sim.c - THE JELLY TANK in the simulator (2026-10-18, the render spike).
 *
 *   ./fishsim --jelly                 the jelly tank in a window
 *   ./fishsim --jelly-shot pfx [s]    headless: pfx_violet / _blue / _night / _jet .ppm after s seconds
 *   ./fishsim --jelly-frames pfx n [s]  headless: n frames s apart (0.2; pfx_000.ppm ...), the jet on, a tap a third in
 *   ./fishsim --jelly-bench           headless render cost per stage, 1 / 3 / 6 / 9 jellies, day and night
 *   ./fishsim --selftest-jelly        headless: the jet's lap time, the glass is never crossed, nothing
 *                                     drawn outside the glass, a jelly carried a full lap
 *
 * Window keys: J the jet (OFF / GENTLE / STEADY), D its direction, L the
 * light, C the light's color, N add a jelly (species and color step), X
 * take the last one away, F feed, P print the cost, D the jet's aim. Mouse: click = a
 * ripple (in the top band: brine shrimp), hold = a light they turn toward, drag = a
 * current, hold the nozzle = the jet's fan (slide to an arrow; the hub = the pace).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "jelly.h"
#include "lvgl/lvgl.h"
#include <SDL2/SDL.h>

static jelly_tank_t jt;
static uint16_t jbg[TANK_W * TANK_H];
static uint16_t jfb[TANK_W * TANK_H];

static int64_t now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* the starter mix for the spike: three moons, two nettles, a blubber */
static void seed_tank(int n) {
    static const int MIX[][2] = {
        { JSP_MOON, 0 }, { JSP_NETTLE, 6 }, { JSP_MOON, 1 }, { JSP_BLUBBER, 4 }, { JSP_MOON, 2 }, { JSP_NETTLE, 1 },
        { JSP_MOON, 5 }, { JSP_BLUBBER, 3 }, { JSP_MOON, 7 }, { JSP_NETTLE, 2 }, { JSP_MOON, 3 }, { JSP_BLUBBER, 6 } };
    for (int i = 0; i < n && i < (int)(sizeof MIX / sizeof MIX[0]); i++) jelly_add(&jt, MIX[i][0], MIX[i][1]);
}

static void write_ppm(const char *path, const uint16_t *fb) {
    FILE *f = fopen(path, "wb"); if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        uint16_t p = fb[i];
        unsigned char rgb[3] = { (unsigned char)(((p >> 11) & 31) << 3), (unsigned char)(((p >> 5) & 63) << 2), (unsigned char)((p & 31) << 3) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void run(float seconds) { for (int i = 0; i < (int)(seconds * 25); i++) jelly_tick(&jt, 1.0f / 25); }

static int jelly_shot(const char *pfx, float seconds) {
    char p[512];
    jelly_init(&jt, 1234);
    jelly_set_bg_cache(jbg);
    seed_tank(6);
    run(seconds);
    jelly_render(&jt, jfb, TANK_W); snprintf(p, sizeof p, "%s_violet.ppm", pfx); write_ppm(p, jfb);
    jt.light = JLIGHT_BLUE;
    jelly_render(&jt, jfb, TANK_W); snprintf(p, sizeof p, "%s_blue.ppm", pfx); write_ppm(p, jfb);
    jt.light_on = false;
    jelly_render(&jt, jfb, TANK_W); snprintf(p, sizeof p, "%s_night.ppm", pfx); write_ppm(p, jfb);
    jt.light_on = true; jt.light = JLIGHT_VIOLET; jt.jet = JET_GENTLE;
    run(40);
    jelly_render(&jt, jfb, TANK_W); snprintf(p, sizeof p, "%s_jet.ppm", pfx); write_ppm(p, jfb);
    printf("jelly-shot: %s_{violet,blue,night,jet}.ppm (%dx%d)\n", pfx, TANK_W, TANK_H);
    return 0;
}

/* --jelly-fan pfx: the jet's fan open on this glass (held on the nozzle, the finger slid to an arrow) */
static int jelly_fan(const char *pfx) {
    char p[512];
    jelly_init(&jt, 7); jelly_set_bg_cache(jbg); seed_tank(6);
    jt.jet = JET_GENTLE; run(30);
    float nx, ny, ax[JET_AIMS], ay[JET_AIMS], cr; jelly_jet_geom(&jt, &nx, &ny, ax, ay, &cr);
    double now = 100;
    jelly_touch(&jt, nx, ny, true, now);
    for (int i = 0; i < 12; i++) { now += 0.04; jelly_touch(&jt, nx, ny, true, now); jelly_tick(&jt, 0.04f); }
    int k = (jt.jet_aim + 2) % JET_AIMS;
    jelly_touch(&jt, ax[k], ay[k], true, now + 0.04);
    jelly_tick(&jt, 0.25f);
    jelly_render(&jt, jfb, TANK_W); snprintf(p, sizeof p, "%s_fan.ppm", pfx); write_ppm(p, jfb);
    printf("jelly-fan: %s_fan.ppm (nozzle %.0f,%.0f; the finger on arrow %d)\n", pfx, nx, ny, k);
    return 0;
}

static int jelly_frames(const char *pfx, int n, float step) {
    char p[512];
    jelly_init(&jt, 99);
    jelly_set_bg_cache(jbg);
    seed_tank(6);
    jt.jet = JET_GENTLE;
    run(20);
    for (int i = 0; i < n; i++) {
        if (i == n / 3) jelly_tap(&jt, TANK_W * 0.5f, TANK_H * 0.55f);
        run(step);
        jelly_render(&jt, jfb, TANK_W);
        snprintf(p, sizeof p, "%s_%03d.ppm", pfx, i); write_ppm(p, jfb);
    }
    printf("jelly-frames: %d frames %s_000..%03d.ppm\n", n, pfx, n - 1);
    return 0;
}

static void bench_line(const char *label) {
    const int N = 300;
    memset(jelly_prof_us, 0, sizeof jelly_prof_us);
    int64_t tot = 0;
    for (int i = 0; i < N; i++) {
        jelly_tick(&jt, 1.0f / 25);
        int64_t t0 = now_us();
        jelly_render(&jt, jfb, TANK_W);
        tot += now_us() - t0;
    }
    printf("%-26s total %6.0f us | bg %5.0f halos %5.0f tent+arms %5.0f bells %5.0f motes %5.0f\n", label, (double)tot / N,
           (double)jelly_prof_us[0] / N, (double)jelly_prof_us[1] / N, (double)jelly_prof_us[2] / N,
           (double)jelly_prof_us[3] / N, (double)jelly_prof_us[4] / N);
}
static int jelly_bench(void) {
    jelly_clock_us = now_us;
    static const int NS[] = { 1, 3, 6, 9 };
    printf("jelly-bench %dx%d (the Mac is ~10x the ESP32-S3; the stage RATIOS carry over)\n", TANK_W, TANK_H);
    for (int k = 0; k < 4; k++) {
        char l[64];
        jelly_init(&jt, 5);
        jelly_set_bg_cache(jbg);
        seed_tank(NS[k]);
        jt.jet = JET_GENTLE;
        run(10);
        snprintf(l, sizeof l, "%d jellies, light on", NS[k]); bench_line(l);
        jt.light_on = false;
        snprintf(l, sizeof l, "%d jellies, light off", NS[k]); bench_line(l);
    }
    return 0;
}

/* --selftest-jelly: what has to hold on every glass */
static int selftest_jelly(void) {
    int fail = 0;
    /* 1. the jet's lap: a mote that rides the water at 3/4 of the way out goes round in the lap time */
    jelly_init(&jt, 7);
    jt.jet = JET_GENTLE;
    run(30);                                            /* the water spins up */
    float cx = TANK_W * 0.5f, cy = TANK_H * 0.5f;
#ifdef TANK_ROUND
    cx = cy = TANK_RAD;
#endif
    float x = cx + TANK_W * 0.5f * 0.75f, y = cy, ang = 0, prev = atan2f((y - cy) / TANK_H, (x - cx) / TANK_W), tsec = 0;
    while (ang < 6.2831853f && tsec < 400) {
        float u, v; jelly_field_at(&jt, x, y, &u, &v);
        x += u / 25; y += v / 25; tsec += 1.0f / 25;
        float a = atan2f((y - cy) / TANK_H, (x - cx) / TANK_W), d = a - prev;
        if (d > 3.14159f) d -= 6.2831853f;
        if (d < -3.14159f) d += 6.2831853f;
        ang += d; prev = a;
    }
    float want = jelly_jet_lap_s(JET_GENTLE);
    bool lap_ok = fabsf(tsec - want) < want * 0.2f;
    printf("selftest-jelly: a lap of the GENTLE jet at 3/4 out took %.1f s (want %.0f +- 20%%) %s\n", tsec, want, lap_ok ? "ok" : "FAIL");
    fail += !lap_ok;

    /* 2. twenty minutes of six jellies through still water, both jet paces, taps and strokes:
     *    no bell's middle ever leaves the glass, nothing goes NaN; a jelly is carried a full lap */
    jelly_init(&jt, 11);
    jelly_set_bg_cache(jbg);
    seed_tank(6);
    uint32_t r = 3;
    int bad = 0, pivots = 0, ropes_bad = 0; long curl_n = 0, curl_spiral = 0, axis_n = 0, axis_off = 0; float worst = 1e9f, prev_head[JELLY_MAX];
    for (int i = 0; i < jt.n; i++) prev_head[i] = jt.j[i].heading;
    float lap_ang[JELLY_MAX] = {0}, lap_prev[JELLY_MAX];
    for (int i = 0; i < jt.n; i++) lap_prev[i] = atan2f((jt.j[i].y - cy) / TANK_H, (jt.j[i].x - cx) / TANK_W);
    float best_lap = 0;
    for (int s = 0; s < 20 * 60 * 25; s++) {
        int minute = s / (60 * 25);
        jt.jet = minute < 5 ? JET_OFF : minute < 12 ? JET_GENTLE : minute < 16 ? JET_STEADY : JET_OFF;
        if (s % 250 == 0) {
            r = r * 1103515245u + 12345u;
            float tx = TANK_W * (0.15f + 0.7f * ((r >> 8) & 1023) / 1023.0f), ty = TANK_H * (0.2f + 0.7f * ((r >> 18) & 1023) / 1023.0f);
            if ((r >> 4) & 1) jelly_tap(&jt, tx, ty);
            else for (int k = 0; k < 10; k++) jelly_swipe(&jt, tx + k * 6, ty, tx + (k + 1) * 6, ty, 1.0f / 25);
        }
        jelly_tick(&jt, 1.0f / 25);
        for (int i = 0; i < jt.n; i++) {
            const jelly_t *j = &jt.j[i];
            if (!isfinite(j->x) || !isfinite(j->y) || !isfinite(j->heading)) { bad++; continue; }
            float dh = j->heading - prev_head[i];                    /* no pivot without motion through the water */
            if (dh > 3.14159f) dh -= 6.2831853f;
            if (dh < -3.14159f) dh += 6.2831853f;
            if (fabsf(dh) > j->turn_max / 25 + 1e-5f) pivots++;
            for (int m = 0; m < jelly_arms(j) && j->arm_ready; m++) {           /* the arms' strings */
                float seg = jelly_arm_len(j, m) / (JELLY_ARM_N - 1);
                for (int q = 1; q < JELLY_ARM_N; q++) {
                    float dx = j->arm_x[m][q] - j->arm_x[m][q - 1], dy = j->arm_y[m][q] - j->arm_y[m][q - 1];
                    if (!isfinite(dx) || !isfinite(dy) || fabsf(sqrtf(dx * dx + dy * dy) - seg) > 0.05f * seg + 0.05f) { ropes_bad++; break; }
                }
            }
            for (int k = 0, np = jelly_rope_points(j); np > 1 && k < jelly_tentacles(j) && j->rope_ready; k++) {   /* the strings */
                float seg = jelly_rope_len(j, k) / (np - 1);
                for (int q = 1; q < np; q++) {
                    float dx = j->rope_x[k][q] - j->rope_x[k][q - 1], dy = j->rope_y[k][q] - j->rope_y[k][q - 1];
                    if (!isfinite(dx) || !isfinite(dy) || fabsf(sqrtf(dx * dx + dy * dy) - seg) > 0.05f * seg + 0.05f) { ropes_bad++; break; }
                }
                if (np >= 8) {                                       /* the long ones: never a spiral (Strato's eddies) */
                    float tot = 0;
                    for (int q = 2; q < np; q++) {
                        float ax = j->rope_x[k][q - 1] - j->rope_x[k][q - 2], ay = j->rope_y[k][q - 1] - j->rope_y[k][q - 2];
                        float bx = j->rope_x[k][q] - j->rope_x[k][q - 1], by = j->rope_y[k][q] - j->rope_y[k][q - 1];
                        tot += fabsf(atan2f(ax * by - ay * bx, ax * bx + ay * by));
                    }
                    curl_n++; if (tot > 3.14159f) curl_spiral++;
                    if (fabsf(j->heading) > 0.52f) {                /* a tilted bell: its tentacles trail its axis (Strato, 10-19) */
                        int mid = np / 2;
                        float ex = j->rope_x[k][mid] - j->rope_x[k][0], ey = j->rope_y[k][mid] - j->rope_y[k][0], em = sqrtf(ex * ex + ey * ey);
                        if (em > 1e-3f) { axis_n++; if ((-ex * sinf(j->heading) + ey * cosf(j->heading)) / em < 0.5f) axis_off++; }
                    }
                }
            }
            prev_head[i] = j->heading;
            float h = jelly_bell_a(j) * 0.6f;
            float d = jelly_glass_dist(j->x + sinf(j->heading) * h * 0.5f, j->y - cosf(j->heading) * h * 0.5f);
            if (d < worst) worst = d;
            if (d < 0) bad++;
            if (minute >= 5 && minute < 12) {
                float a = atan2f((j->y - cy) / TANK_H, (j->x - cx) / TANK_W), dd = a - lap_prev[i];
                if (dd > 3.14159f) dd -= 6.2831853f;
                if (dd < -3.14159f) dd += 6.2831853f;
                lap_ang[i] += dd; lap_prev[i] = a;
                if (fabsf(lap_ang[i]) > best_lap) best_lap = fabsf(lap_ang[i]);
            } else lap_prev[i] = atan2f((j->y - cy) / TANK_H, (j->x - cx) / TANK_W);
        }
    }
    printf("selftest-jelly: 20 min, 6 jellies: %d ticks with a bell out of the glass or NaN (want 0), closest to the glass %.1f px %s\n",
           bad, worst, bad == 0 ? "ok" : "FAIL");
    fail += bad != 0;
    printf("selftest-jelly: %d ticks where a bell turned faster than its motion through the water allows (want 0) %s\n",
           pivots, pivots == 0 ? "ok" : "FAIL");
    fail += pivots != 0;
    printf("selftest-jelly: %d tentacle checks off their length or not finite (want 0) %s\n", ropes_bad, ropes_bad == 0 ? "ok" : "FAIL");
    fail += ropes_bad != 0;
    float spiral_pct = curl_n ? 100.0f * curl_spiral / curl_n : 0;
    printf("selftest-jelly: long tentacles curled past half a turn %.2f%% of the time (want < 5%%) %s\n", spiral_pct, spiral_pct < 5 ? "ok" : "FAIL");
    fail += spiral_pct >= 5;
    float axis_pct = axis_n ? 100.0f * axis_off / axis_n : 0;
    printf("selftest-jelly: a tilted bell's long tentacles more than 60 degrees off its axis %.2f%% of the time (want < 3%%) %s\n", axis_pct, axis_pct < 3 ? "ok" : "FAIL");
    fail += axis_pct >= 3;
    bool lap = best_lap >= 6.2831853f;
    printf("selftest-jelly: in 7 min of the GENTLE jet the furthest-carried jelly went %.2f laps (want >= 1) %s\n", best_lap / 6.2831853f, lap ? "ok" : "FAIL");
    fail += !lap;

    /* 3. nothing is drawn outside the glass */
    jt.light_on = true;
    jelly_tap(&jt, TANK_W * 0.5f, TANK_H * 0.5f);
    jelly_hold(&jt, TANK_W * 0.9f, TANK_H * 0.1f, true);
    jelly_tick(&jt, 0.3f);
    jelly_render(&jt, jfb, TANK_W);
    int outside = 0;
    for (int yy = 0; yy < TANK_H; yy++)
        for (int xx = 0; xx < TANK_W; xx++)
            if (jelly_glass_dist(xx + 0.5f, yy + 0.5f) < -0.5f && jfb[yy * TANK_W + xx]) outside++;
    printf("selftest-jelly: %d lit pixels outside the glass (want 0) %s\n", outside, outside == 0 ? "ok" : "FAIL");
    fail += outside != 0;
    /* 4. the jet's fan: every arrow clear of the glass (Strato: the pendant's nozzle sat at the edge), a hold on
     *    the nozzle opens it, sliding to an arrow and letting go picks it, and the water eases to a stop
     *    before it turns: the old direction keeps pushing (less and less) until then */
    {
        jelly_init(&jt, 5); jt.jet = JET_GENTLE; run(30);
        float nx, ny, ax[JET_AIMS], ay[JET_AIMS], cr; jelly_jet_geom(&jt, &nx, &ny, ax, ay, &cr);
        float clear = jelly_glass_dist(nx, ny) - 5 * JELLY_S;
        for (int k = 0; k < JET_AIMS; k++) { float d = jelly_glass_dist(ax[k], ay[k]) - cr * 1.38f; if (d < clear) clear = d; }
        bool pad = clear >= 4;
        printf("selftest-jelly: the fan's nearest edge is %.1f px inside the glass (want >= 4) %s\n", clear, pad ? "ok" : "FAIL");
        fail += !pad;
        /* the opposite end of the fan: the tank's circling reverses */
        int k = jt.jet_aim == 0 ? JET_AIMS - 1 : 0;
        float fcx = TANK_W * 0.12f, fcy = cy;                          /* far side of the tank from the nozzle */
        float u0, v0; jelly_field_at(&jt, fcx, fcy, &u0, &v0);
        double now = 50; jelly_touch(&jt, nx, ny, true, now);
        for (int i = 0; i < 12; i++) { now += 0.04; jelly_touch(&jt, nx + 2, ny + 1, true, now); jelly_tick(&jt, 0.04f); }
        bool opened = jt.ui_open;
        jelly_touch(&jt, ax[k], ay[k], true, now + 0.04); jelly_touch(&jt, ax[k], ay[k], false, now + 0.08);
        jelly_tick(&jt, 0.04f);
        bool picked = jt.jet_aim == k && !jt.ui_open, at_once = jt.jet_aim_live == k;
        /* near the nozzle the water takes up the new push quickly; far off it coasts, slows, then turns */
        float ex = ax[k] - nx, ey = ay[k] - ny, em = sqrtf(ex * ex + ey * ey); ex /= em; ey /= em;
        float px = nx + ex * 0.16f * JELLY_SHORT, py = ny + ey * 0.16f * JELLY_SHORT;
        float t_near = -1, t_cross = -1, tt = 0, m0 = u0 * u0 + v0 * v0;
        while (tt < 20 && (t_near < 0 || t_cross < 0)) {
            jelly_tick(&jt, 0.04f); tt += 0.04f;
            float u, v; jelly_field_at(&jt, px, py, &u, &v);
            if (t_near < 0 && u * ex + v * ey > 2 * JELLY_S) t_near = tt;
            jelly_field_at(&jt, fcx, fcy, &u, &v);
            if (t_cross < 0 && m0 > 1e-4f && (u * u0 + v * v0) / m0 < 0) t_cross = tt;
        }
        bool ok = opened && picked && at_once && t_near >= 0 && t_near < 1.5f && t_cross > 1.5f && t_cross < 15;
        printf("selftest-jelly: fan opened %d, arrow %d picked %d, the jet on it at once %d; the water near the nozzle follows in %.1f s (want < 1.5), "
               "the far water reverses after %.1f s (want 1.5-15) %s\n", opened, k, picked, at_once, t_near, t_cross, ok ? "ok" : "FAIL");
        fail += !ok;
    }
    /* 5. the light: a double tap toggles it (the fish tank's gesture); the first tap still ripples at once,
     *    the second does not; two slow taps are two ripples and leave the light alone */
    {
        jelly_init(&jt, 9); run(2);
        float x = TANK_W * 0.4f, y = TANK_H * 0.6f; double now = 10;
        bool was = jt.light_on;
        jelly_touch(&jt, x, y, true, now); jelly_touch(&jt, x, y, false, now + 0.08);
        bool rippled_at_once = jt.tc_ripples == 1 && jt.light_on == was;
        jelly_touch(&jt, x + 6, y + 4, true, now + 0.25); jelly_touch(&jt, x + 6, y + 4, false, now + 0.32);
        bool toggled = jt.light_on != was && jt.tc_ripples == 1 && jt.tc_lights == 1;
        now += 5;
        jelly_touch(&jt, x, y, true, now); jelly_touch(&jt, x, y, false, now + 0.08);
        jelly_touch(&jt, x, y, true, now + 0.9); jelly_touch(&jt, x, y, false, now + 0.98);
        bool slow = jt.tc_ripples == 3 && jt.tc_lights == 1;
        now += 5;                                                     /* two quick taps far apart: two ripples */
        jelly_touch(&jt, TANK_W * 0.2f, y, true, now); jelly_touch(&jt, TANK_W * 0.2f, y, false, now + 0.08);
        jelly_touch(&jt, TANK_W * 0.8f, y, true, now + 0.25); jelly_touch(&jt, TANK_W * 0.8f, y, false, now + 0.32);
        bool apart = jt.tc_ripples == 5 && jt.tc_lights == 1;
        bool ok = rippled_at_once && toggled && slow && apart;
        printf("selftest-jelly: the first tap ripples at once %d, a double tap toggles the light %d, slow taps ripple %d, far-apart taps ripple %d %s\n",
               rippled_at_once, toggled, slow, apart, ok ? "ok" : "FAIL");
        fail += !ok;
    }
    printf("selftest-jelly: %s (%dx%d)\n", fail ? "FAILED" : "passed", TANK_W, TANK_H);
    return fail ? 1 : 0;
}

/* ---- the window ---------------------------------------------------------- */
static lv_draw_buf_t jdraw;
static lv_obj_t *jcanvas;
static uint32_t jlast;
static bool jprint;
static uint32_t tick_cb(void) { return SDL_GetTicks(); }
static void frame_cb(lv_timer_t *tm) {
    (void)tm;
    uint32_t now = SDL_GetTicks();
    float dt = (now - jlast) / 1000.0f; jlast = now;
    jelly_tick(&jt, dt);
    int64_t t0 = now_us();
    jelly_render(&jt, jfb, TANK_W);
    static int64_t acc; static int frames; static uint32_t since;
    acc += now_us() - t0; frames++;
    if (jprint && now - since > 2000) {
        printf("render %.2f ms/frame (Mac) | bg %.2f halos %.2f tent+arms %.2f bells %.2f motes %.2f | %d jellies\n",
               acc / 1000.0 / frames, jelly_prof_us[0] / 1000.0 / frames, jelly_prof_us[1] / 1000.0 / frames,
               jelly_prof_us[2] / 1000.0 / frames, jelly_prof_us[3] / 1000.0 / frames, jelly_prof_us[4] / 1000.0 / frames, jt.n);
        acc = 0; frames = 0; since = now; memset(jelly_prof_us, 0, sizeof jelly_prof_us);
    }
    lv_obj_invalidate(jcanvas);
}

static int jelly_window(void) {
    jelly_init(&jt, (uint32_t)time(NULL));
    jelly_set_bg_cache(jbg);
    jelly_clock_us = now_us;
    seed_tank(6);
    printf("THE JELLY TANK (spike): J jet, D direction, L light, C color, N add, X remove, F feed, P cost\n"
           "mouse: click = ripple (top band = feed), hold = a light, drag = a current\n");
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_sdl_window_create(TANK_W, TANK_H);
    lv_sdl_window_set_title(disp, "pocket-tank: the jelly tank (spike)");
    lv_draw_buf_init(&jdraw, TANK_W, TANK_H, LV_COLOR_FORMAT_RGB565, TANK_W * 2, jfb, sizeof(jfb));
    jcanvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_draw_buf(jcanvas, &jdraw);
    lv_obj_center(jcanvas);
    jlast = SDL_GetTicks();
    lv_timer_create(frame_cb, 16, NULL);

    bool was[SDL_NUM_SCANCODES] = {0};
    int add_k = 0;
    while (1) {
        uint32_t wait = lv_timer_handler();
        const Uint8 *k = SDL_GetKeyboardState(NULL);
#define EDGE(sc) (k[sc] && !was[sc])
        if (EDGE(SDL_SCANCODE_J)) { jt.jet = (jt.jet + 1) % 3; printf("jet: %s\n", jt.jet == JET_OFF ? "OFF" : jt.jet == JET_GENTLE ? "GENTLE" : "STEADY"); }
        if (EDGE(SDL_SCANCODE_D)) { jelly_jet_set(&jt, jt.jet == JET_OFF ? JET_GENTLE : jt.jet, (jt.jet_aim + 1) % JET_AIMS); printf("jet aim %d (the water eases to a stop, then turns)\n", jt.jet_aim); }
        if (EDGE(SDL_SCANCODE_L)) { jt.light_on = !jt.light_on; printf("light: %s\n", jt.light_on ? "ON" : "OFF"); }
        if (EDGE(SDL_SCANCODE_C)) { jt.light = (jt.light + 1) % JLIGHT_N; printf("light color: %s\n", JELLY_LIGHT_NAMES[jt.light]); }
        if (EDGE(SDL_SCANCODE_N)) { int sp = add_k % JSP_N, ne = (add_k * 3 + 1) % JELLY_NEON_N; add_k++;
                                    if (jelly_add(&jt, sp, ne) >= 0) printf("added a %s, %s (%d)\n", JELLY_SPECIES_NAMES[sp], JELLY_NEON_NAMES[ne], jt.n); }
        if (EDGE(SDL_SCANCODE_X)) { jelly_remove_last(&jt); printf("%d jellies\n", jt.n); }
        if (EDGE(SDL_SCANCODE_F)) jelly_feed(&jt, TANK_W * 0.5f);
        if (EDGE(SDL_SCANCODE_P)) { jprint = !jprint; memset(jelly_prof_us, 0, sizeof jelly_prof_us); }
        memcpy(was, k, sizeof was);
        int mx, my;
        bool m = SDL_GetMouseState(&mx, &my) & SDL_BUTTON(SDL_BUTTON_LEFT);
        jelly_touch(&jt, (float)mx, (float)my, m, SDL_GetTicks() / 1000.0);   /* tap, hold, stroke, the jet's fan: jelly.c */
        if (wait < 5) wait = 5;
        SDL_Delay(wait);
    }
    return 0;
}

/* a theme's tank after `seconds` (the jet off), drawn into out (TANK_W x TANK_H) - main.c's --jelly-crew-shot */
void jelly_sim_theme_frame(int theme, float seconds, int jet, uint16_t *out) {
    jelly_theme_start(&jt, theme);
    jelly_set_bg_cache(jbg);
    jt.jet = jet;
    run(seconds);
    jelly_render(&jt, out, TANK_W);
}

/* argv: the whole command line; returns -1 when it is not a jelly command */
int jelly_sim_main(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--jelly") == 0) return jelly_window();
        if (strcmp(argv[a], "--jelly-shot") == 0 && a + 1 < argc) return jelly_shot(argv[a + 1], a + 2 < argc ? (float)atof(argv[a + 2]) : 30);
        if (strcmp(argv[a], "--jelly-frames") == 0 && a + 1 < argc) return jelly_frames(argv[a + 1], a + 2 < argc ? atoi(argv[a + 2]) : 30, a + 3 < argc ? (float)atof(argv[a + 3]) : 0.2f);
        if (strcmp(argv[a], "--jelly-bench") == 0) return jelly_bench();
        if (strcmp(argv[a], "--jelly-fan") == 0 && a + 1 < argc) return jelly_fan(argv[a + 1]);
        if (strcmp(argv[a], "--selftest-jelly") == 0) return selftest_jelly();
    }
    return -1;
}
