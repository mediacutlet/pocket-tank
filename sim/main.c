/* main.c — PC simulator entry: LVGL v9 + SDL window, 448x368 to match the
 * ESP32 AMOLED (landscape). This is the only platform-specific file; tank.c,
 * advisor.c and render.c compile unchanged for firmware.
 *
 *   ./fishsim              run the tank (keys: F feed at the mouse x,
 *                          N light (manual override), A back to the idle rule,
 *                          H handle the tank (a pick-up; the mouse moving over
 *                          the window counts too - still 15 s = lights out, once
 *                          settings has LIGHTS OUT on AUTO; MANUAL is the default),
 *                          L brain, U overlays, M milestones view,
 *                          X reset prompt (device: hold BOOT + tap the glass),
 *                          S the first-run setup flow (welcome / names / colours),
 *                          R force an arrival (the birth flow opens: announce /
 *                          name / family; S drops it), A auto-light, Q quit,
 *                          V volume (off / quiet / normal), B the low-battery notice,
 *                          P the cable in / out (a pretend battery: the pill
 *                          shows for a few seconds; with a card up it is
 *                          always there - click it for the battery page),
 *                          4 ($) the shop page, D +50 sand dollars,
 *                          T (the watch build) wear it the other way round;
 *                          click fish = stats; tap the water surface = feed;
 *                          drag down from the top = feed; hold >= 3 s = finger
 *                          on glass (trusting fish visit); swipe sideways
 *                          through a canopy = trim that bed; drag = wipe algae;
 *                          3 quick taps = startle; 2 taps = the light, in
 *                          settings' MANUAL mode, the default)
 *   ./fishsim --fresh      ignore the save (new tank: random pair)
 *   ./fishsim --fast N     tended time runs N x faster (stages, drift)
 *   ./fishsim --battery N  the pretend battery starts at N% (default 72)
 *   ./fishsim --greedy     greedy decoding instead of sampling
 *   ./fishsim --selftest   headless reflex-layer check, no window
 *   ./fishsim --selftest-llm [min]   headless LLM path (real-time if min > 0)
 *   ./fishsim --selftest-pop         headless population/arrival/save check
 *   ./fishsim --selftest-card [pfx]  headless fish card: RENAME and SELL (pfx: also writes its pages as PPMs)
 *   ./fishsim --selftest-sleep       headless sleep metabolism + ravenous begging
 *   ./fishsim --selftest-tend        headless canopy/algae/hold-attract check
 *   ./fishsim --selftest-hunger      headless hunger economy (untended tank never ravenous)
 *   ./fishsim --selftest-shop        headless sand dollars: awards, the shop, the plant, the snail, the save
 *   ./fishsim --selftest-battery     headless battery page: stretches, sleep, learned rates, estimates, the pill
 *   ./fishsim --selftest-saves       headless update promise: every save in testdata/saves, whole and cut to older builds' lengths
 *   ./fishsim --bench                headless render-cost profile (veg, card)
 *   (key Z: jump through 7 h of device-style sleep; key G: grow the canopy +
 *    algae now to try the chores - press again to cycle; key Y: the urchin)
 */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE   /* glibc hides setenv / strdup / truncate under -std=c11; an undeclared
                             strdup returns a truncated int and --selftest-saves crashed in CI */
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include "tank.h"
#include "advisor.h"
#include "advisor_core.h"
#include "render.h"
#include "progression.h"
#include "version.h"
#include "setup.h"
#include "audio.h"
#include "notice.h"
#include "tank_events.h"
#include "update.h"
void net_sim_advance(float dt);    /* net_port_sim.c: the pretend radio's clock */
bool net_sim_radio_on(void);

static tank_t tank;

static void print_roster(const tank_t *t) {
    printf("tank: %d fish\n", t->n_fish);
    for (int i = 0; i < t->n_fish; i++)
        printf("  %-5s (model token %-4s) %s bold %.2f social %.2f trust %.1f\n",
               t->fish[i].name, t->fish[i].model_name, STAGE_NAMES[t->fish[i].stage],
               t->fish[i].bold, t->fish[i].sociable, t->fish[i].trust);
}

/* ---------- the selftests' geometry (2026-10-01) ----------
 * Three boards run these tests - the rectangle, the round bowl (TANK_ROUND),
 * the portrait watch (TANK_WATCH) - so a test never carries one board's
 * pixels: it asks the code. The pages are laid out in PAGE space (render.h)
 * and sit centered on each glass, and a tap is the frame's: PG_X / PG_Y put a
 * page point on this board's glass; the spots under them are where a finger
 * aims, from the layouts' own numbers (render.h MSP_ / SHP_ / SET_, setup.h,
 * update.h). The few bare numbers left are a page's own line heights that
 * render.c never named, and how far off a button a test presses on purpose.
 * (This fork: the CYD's page is the design scaled - ui.h, 240 / 368 - so
 * those bare numbers are UI(n) where the page they press is scaled: exactly n
 * on every other board.) */
#define PG_X(x) (PAGE_X + (x))
#define PG_Y(y) (PAGE_Y + (y))
/* the milestones page, in page coordinates: row i's top (the NEW FRY row is
 * row n_fish), then inside a row the name / strip cluster, badge k's center */
#define MS_ROW(i)      (MSP_ROW_Y0 + (i) * MSP_ROW_H)
#define MS_NAME_X      ((MSP_FISH_X + MSP_BADGE_X0) / 2)
#define MS_NAME_DY     (MSP_ROW_MID / 2)
#define MS_BADGE_X(k)  (MSP_BADGE_X0 + (k) * MSP_BADGE_DX + MSP_ICON / 2)
#define MS_BADGE_DY    (MSP_ROW_BADGE + MSP_ICON / 2)
#define MS_TANK_Y      (MSP_TANK_Y + 4 + MSP_ICON / 2)             /* the TANK row's badges (and the coin) */
#define MS_FOOT_Y      (MSP_CLOSE_Y + 10)                          /* the foot's buttons */
/* its detail modal: the arrows at the top corners, a spot on it that is no
 * button (any tap there closes it - and with no modal up it is the last fish
 * row, empty in these tests), and a gate's HOW? button at the foot of the
 * taller panel (two sentence lines and the progress line: render.c's 20 + 24,
 * the button 14 under them and 10 off the edge) */
#define MS_ARROW_L_X   (MSP_MODAL_X + MSP_ARROW_IN + MSP_ARROW_W / 2)
#define MS_ARROW_R_X   (MSP_MODAL_X + MSP_MODAL_W - MSP_ARROW_IN - MSP_ARROW_W / 2)
#define MS_ARROW_Y     (MSP_MODAL_Y + MSP_ARROW_IN + MSP_ARROW_H / 2)
#define MS_OFF_X       (MSP_MODAL_X + MSP_MODAL_W / 2 - 24)
#define MS_OFF_Y       (MS_ROW(N_FISH_MAX - 1) + MSP_ROW_H - 12)
#define MS_HOW_X       (MSP_MODAL_X + (MSP_MODAL_W - MSP_HOW_W) / 2)
#define MS_HOW_Y       (MSP_FRY_MODAL_Y + MSP_MODAL_H + 20 + 24 + MSP_HOW_H + 14 - 10 - MSP_HOW_H)
#ifdef CONFIG_POCKET_TANK_320X240 /* (this fork) the CYD's modals are the design scaled (ui.h), these line heights with them (render.c) */
#undef  MS_OFF_X
#define MS_OFF_X       (MSP_MODAL_X + MSP_MODAL_W / 2 - UI(24))
#undef  MS_HOW_Y
#define MS_HOW_Y       (MSP_FRY_MODAL_Y + MSP_MODAL_H + UI(20) + UI(24) + MSP_HOW_H + UI(14) - UI(10) - MSP_HOW_H)
#endif
/* every fish wants the reef (the reef badge's test) */
static goal_t advisor_inspect(const tank_t *t, int fish_idx, bool request) {
    (void)t; (void)fish_idx; (void)request;
    goal_t g = { 0 }; g.id = GOAL_INSPECT_REEF; g.urgency = 3; g.confidence = 1; g.runner_up = GOAL_COUNT;
    return g;
}
static int ms_tap(float px, float py) { return render_milestones_tap(&tank, PG_X(px), PG_Y(py)); }
/* the shop: row r's band, the modal's one button (UNLOCK) or two (MOVE, SELL), the header's arrows */
#define SHOP_ROW_X     100                                         /* on a row's name (the band runs the page's width) */
#define SHOP_ROW_Y(r)  (SHP_ROW_Y0 + (r) * SHP_ROW_DY + 20)
#define SHOP_BTN_Y     (SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H / 2)
#define SHOP_BTN_X     (SHP_MODAL_X + SHP_MODAL_W / 2)
#define SHOP_MOVE_X    (SHP_TWO_X0 + MSP_HOW_W / 2)
#define SHOP_SELL_X    (SHP_TWO_X1 + MSP_HOW_W / 2)
#define SHOP_ARROW_Y   (SHP_ARROW_Y + SHP_ARROW_H / 2)
#define SHOP_PREV_X    (SHP_ARROW_X0 + SHP_ARROW_W / 2)
#define SHOP_NEXT_X    (SHP_ARROW_X1 + SHP_ARROW_W / 2)
static int shop_tap(float px, float py) { return render_shop_tap(&tank, PG_X(px), PG_Y(py)); }
/* the settings page: segment i of a row's buttons, the LIGHTS OUT row's two arrows */
#define SETP_SEG_X(i)  (SET_SEG_X + (i) * SET_SEG_DX + SET_SEG_W / 2)
#define SETP_PREV_X    (SET_SEG_X + SET_ARW_W / 2)
#define SETP_NEXT_X    (SET_SEG_X + SET_SPAN_W - SET_ARW_W / 2)
/* the setup / birth / placement pages (setup.h's numbers are the page's) */
static int  pg_hit(float px, float py) { return setup_hit(PG_X(px), PG_Y(py)); }
static void pg_touch(float px, float py, bool down) { setup_touch(&tank, PG_X(px), PG_Y(py), down); }
/* the garden: a height (1 = the surface) for a y on the glass, as the scissors
 * read a stroke; and the lowest ceiling a frond at x may have - VEG_CAP_LO, or
 * what the glass over it leaves (the bowl's dome, the watch's upper corners) */
#define VEG_FLOOR_Y    (TANK_BOT - 16)
/* (this fork) a height over the sand - a stroke in the grass, a finger held
 * just over it - given as the 1.8's y: on the CYD the same height, 128 px
 * higher (its sand line is y 224, not 352; the 1.8's y 250 is under the CYD's
 * glass, its 200 down in the grass). Exactly y elsewhere. */
#ifdef CONFIG_POCKET_TANK_320X240
#define GRASS_Y(y)     ((y) - (368 - TANK_BOT))
#else
#define GRASS_Y(y)     (y)
#endif
/* (this fork) a spot on the floor given as the 1.8's x: on the CYD the same
 * share of its floor (tank.h's TANK_FLOOR_X) - the 1.8's x 300 is past where
 * the CYD's margins let the plant stand (51..269). Exactly x elsewhere. */
#ifdef CONFIG_POCKET_TANK_320X240
#define FLOOR_X(x)     TANK_FLOOR_X(x)
#else
#define FLOOR_X(x)     (x)
#endif
static float veg_height_at(float y) { return (VEG_FLOOR_Y - y) / ((VEG_SEGS_FULL - 1) * VEG_SEG_PX); }
static float veg_cap_floor(float x) {
    float room = (VEG_FLOOR_Y - tank_glass_top(x) - VEG_GLASS_GAP) / (VEG_SEGS_FULL * VEG_SEG_PX);
    return room < VEG_CAP_LO ? room : VEG_CAP_LO;
}
/* a fish put out of the way for a render or a leg: open water up in the left
 * shoulder (60, 60), brought inside the glass where the glass is not the frame */
static void park_fish(fish_t *f) { f->x = 60; f->y = 60; tank_glass_clamp(&f->x, &f->y, 40); }
/* film on just over `share` of this board's glass, grown the tank's own way -
 * so only on cells the glass has (the bowl's circle, the watch's corners) */
static void film_just_over(tank_t *t, float share) {
    static tank_t grow; tank_init(&grow, 7);
    for (int i = 0; i < 100000 && tank_algae_cover(&grow) <= share; i++) tank_grow_algae(&grow, 1);
    memcpy(t->algae, grow.algae, sizeof t->algae);
}

/* ---------- headless selftest ---------- */
static int selftest(void) {
    tank_init(&tank, 1234);
    tank_new_population(&tank);
    print_roster(&tank);
    if (tank.n_fish != 2 || fabsf(tank.fish[0].bold - tank.fish[1].bold) < 0.45f) {
        printf("FAIL: starting pair not contrasting\n"); return 1;
    }
    /* grow the population to the max through the tank API */
    while (tank_add_fish(&tank, 0, 1) >= 0) {}
    if (tank.n_fish != N_FISH_MAX) { printf("FAIL: population %d != %d\n", tank.n_fish, N_FISH_MAX); return 1; }
    int goal_seen[GOAL_COUNT] = {0};
    for (int i = 0; i < 7200; i++) {              /* 2 simulated minutes, the tank in hand (lit) */
        if (i % 60 == 0) tank_handled(&tank);
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        if (i == 600) {   /* peckish first (2026-09-29): with nobody hungry, a meal was a
                             fish blundering into a pellet - 3 seeds in 16 ate neither feeding */
            for (int fi = 0; fi < tank.n_fish; fi++) tank.fish[fi].hunger = 7.5f;
            tank_feed(&tank, 200, 3);
        }
        if (i == 1200) tank_touch_tap(&tank, 300, tank_glass_top(300) + 10);   /* surface tap = feed (the surface is the glass over x) */
        for (int fi = 0; fi < tank.n_fish; fi++) {
            const fish_t *f = &tank.fish[fi];
            if (!(f->x >= tank_glass_x0(f->y) && f->x <= tank_glass_x1(f->y) && f->y >= tank_glass_top(f->x) && f->y <= TANK_BOT) ||   /* in the glass: the frame in the rectangle */
                f->x != f->x || f->y != f->y) {   /* NaN check */
                printf("FAIL: %s out of bounds at tick %d (%.1f,%.1f)\n",
                       f->name, i, f->x, f->y);
                return 1;
            }
            goal_seen[f->goal.id]++;
        }
    }
    static uint16_t fb[TANK_W * TANK_H], scene[TANK_W * TANK_H];
    render_set_scene_cache(scene);
    render_tank(&tank, fb, TANK_W);               /* renderer must not crash */
    render_stats_card(&tank, 0, fb, TANK_W);
    render_milestones(&tank, fb, TANK_W);
    render_confirm_reset(fb, TANK_W, 0.5f);
    setup_begin(&tank);
    for (int pg = 0; pg < SETUP_PG_N; pg++) { render_setup(&tank, fb, TANK_W, 1.0f); if (pg + 1 < SETUP_PG_N) setup_activate(&tank, SETUP_HIT_NEXT); }
    setup_cancel(&tank);
    int eaten = 0, distinct = 0;
    for (int i = 0; i < tank.n_fish; i++) eaten += tank.fish[i].eaten;
    printf("selftest: 7200 ticks ok, %u advisor asks, %d player feedings, feed spot %.0f\n",
           tank.advisor_asks, tank.player_feedings, tank.feed_spot_x);
    for (int g = 0; g < GOAL_COUNT; g++) {
        if (goal_seen[g]) distinct++;
        printf("  %-14s %5d fish-ticks\n", GOAL_NAMES[g], goal_seen[g]);
    }
    printf("  pellets eaten: %d, distinct goals used: %d/8\n", eaten, distinct);
    for (int i = 0; i < tank.n_fish; i++)
        printf("  %-5s (%.0f,%.0f) hunger %.1f energy %.1f goal %s\n",
               tank.fish[i].name, tank.fish[i].x, tank.fish[i].y,
               tank.fish[i].hunger, tank.fish[i].energy,
               GOAL_NAMES[tank.fish[i].goal.id]);
    /* meals (2026-09-14): a feeding counts once a fish eats from it - two gestures, at least one eaten from */
    if (tank.player_feedings < 1 || tank.player_feedings > 2) { printf("FAIL: meals %d from 2 feedings\n", tank.player_feedings); return 1; }
    {   /* a feeding nobody eats from is not a meal: pellets that vanish uneaten count nothing */
        int meals = tank.player_feedings;
        tank_feed(&tank, 220, 3);
        for (int i = 0; i < MAX_FOOD; i++) tank.food[i].alive = false;
        for (int i = 0; i < 600; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        if (tank.player_feedings != meals) { printf("FAIL: an uneaten feeding counted as a meal\n"); return 1; }
        tank.feed_open = false;
        /* and one that IS eaten from counts once, however many pellets it drops */
        for (int i = 0; i < tank.n_fish; i++) tank.fish[i].hunger = 9.0f;
        tank_feed(&tank, tank.fish[0].x, 3);
        for (int i = 0; i < 1800 && tank.player_feedings == meals; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        for (int i = 0; i < 600; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        if (tank.player_feedings != meals + 1) { printf("FAIL: an eaten feeding counted %d meals\n", tank.player_feedings - meals); return 1; }
        printf("selftest: meals: an uneaten feeding counted nothing, an eaten one counted once\n");
    }
    return distinct >= 5 ? 0 : 1;                 /* a live tank uses most goals */
}

/* the spawning (2026-09-24, Strato: "new fry comes at next light on" felt
 * unintuitive): a staged fry is born on its own - a few seconds after its
 * last gate the parents swim down into the nursery grass and court, then the
 * fry comes, between them. The light has no say (leg A runs in the dark); a
 * page over the tank holds it (B); a tank put to sleep first has it at the
 * wake - a deep sleep (C) or the grace's quick wake (D). Runs on the test
 * save the setup leg left: a pair, nothing staged. */
/* Every leg boots THAT save, put back first (2026-10-01): a leg's staging
 * rides in the file once a coalesced save lands mid-leg and the next boot
 * delivers it, so the legs used to hand each other a fish or not by timing -
 * and on the watch, where the grass is a longer swim, all three did and the
 * last leg found the tank full. */
static uint8_t s_spawn_sav[4096]; static size_t s_spawn_sav_n;
static int spawn_prep(uint32_t seed) {
    if (s_spawn_sav_n) persist_port_save(s_spawn_sav, s_spawn_sav_n);
    tank_init(&tank, seed); progression_boot(&tank);
    for (int i = 0; i < tank.n_fish; i++) { tank.fish[i].hunger = 2; tank.fish[i].energy = 8; }
    tank_veg_set(&tank, 0, 0.6f);                 /* a nursery, whatever the save grew */
    progression_stage_arrival(&tank);
    return tank.n_fish;
}
static int selftest_spawn(void) {
    const float DT = 1.0f / 60.0f;
    if (!persist_port_load(s_spawn_sav, sizeof s_spawn_sav, &s_spawn_sav_n)) { printf("FAIL: the setup leg left no save for the spawning legs\n"); return 1; }
    /* A: in the dark, nobody touching it */
    int n0 = spawn_prep(40);
    tank.light_auto = false; tank.light_manual_off = true;   /* the keeper turned the light off */
    float t_spawn = -1, t_in = -1, t_born = -1; int pa = -1, pb = -1;
    for (int i = 0; i < 60 * 120 && t_born < 0; i++) {
        tank_tick(&tank, DT, advisor_rules); progression_tick(&tank, DT);
        if (!tank.night) { printf("FAIL: the light came on by itself\n"); return 1; }
        if (tank.spawning && t_spawn < 0) { t_spawn = i * DT; pa = tank.court_a; pb = tank.court_b; }
        if (tank.spawning && tank.spawn_danced > 0 && t_in < 0) t_in = i * DT;
        if (tank.n_fish != n0) t_born = i * DT;
    }
    printf("selftest-pop: spawning in the dark: the pair set off at %.1f s, in the grass at %.1f s, the fry at %.1f s\n", t_spawn, t_in, t_born);
    if (t_born < 0) { printf("FAIL: no fry within 2 min of the staging\n"); return 1; }
    if (t_spawn < SPAWN_WAIT_MIN_S - DT || t_spawn > SPAWN_WAIT_MAX_S + DT) { printf("FAIL: the courtship started at %.1f s, not %.0f..%.0f\n", t_spawn, SPAWN_WAIT_MIN_S, SPAWN_WAIT_MAX_S); return 1; }
    if (t_in < 0 || t_in > 30) { printf("FAIL: the pair was not courting in the grass within 30 s (%.1f)\n", t_in); return 1; }
#ifdef CONFIG_POCKET_TANK_320X240
    const float dance_slack = 1.5f * DT;          /* (this fork) a frame for the sighting, and half a frame for the float times: on
                                                     the CYD the fry lands 599 frames in, which i * DT puts one ulp under 599 * DT */
#else
    const float dance_slack = DT;
#endif
    if (t_born - t_in < SPAWN_DANCE_S - dance_slack) { printf("FAIL: the fry came %.1f s into the dance, before SPAWN_DANCE_S\n", t_born - t_in); return 1; }
    {
        const fish_t *f = &tank.fish[tank.n_fish - 1];
        int b = tank_nursery_bed(&tank); float x0, x1; tank_veg_bed(&tank, b, &x0, &x1, NULL, NULL);
        bool parents = (f->parent_a == pa && f->parent_b == pb) || (f->parent_a == pb && f->parent_b == pa);
        if (!parents) { printf("FAIL: the fry's parents %d/%d are not the courting pair %d/%d\n", f->parent_a, f->parent_b, pa, pb); return 1; }
        if (f->x < x0 || f->x > x1) { printf("FAIL: the fry was born at x %.0f, outside the nursery %.0f..%.0f\n", f->x, x0, x1); return 1; }
        if (progression_newborn() != tank.n_fish - 1 || progression_arrival_pending() || tank.spawning) { printf("FAIL: the birth left the debt / staging wrong\n"); return 1; }
    }
    /* B: a page over the tank holds it, even mid-dance; the page gone, it resumes */
    n0 = spawn_prep(41);
    tank.ui_cover = true;
    for (int i = 0; i < 60 * 60; i++) { tank_tick(&tank, DT, advisor_rules); progression_tick(&tank, DT); }
    if (tank.n_fish != n0 || tank.spawning) { printf("FAIL: the fry came (or the pair courted) under a page\n"); return 1; }
    tank.ui_cover = false;
    int i = 0;
    for (; i < 60 * 120 && !(tank.spawning && tank.spawn_danced > 2); i++) { tank_tick(&tank, DT, advisor_rules); progression_tick(&tank, DT); }
    tank.ui_cover = true;                          /* the keeper opens the milestones mid-dance */
    tank_tick(&tank, DT, advisor_rules); progression_tick(&tank, DT);
    if (tank.spawning || tank.n_fish != n0) { printf("FAIL: the dance went on under a page\n"); return 1; }
    tank.ui_cover = false;
    for (i = 0; i < 60 * 120 && tank.n_fish == n0; i++) { tank_tick(&tank, DT, advisor_rules); progression_tick(&tank, DT); }
    printf("selftest-pop: a page held the spawning 60 s and cut a dance short; closed, the fry came %.1f s later\n", i * DT);
    if (tank.n_fish != n0 + 1) { printf("FAIL: no fry after the page closed\n"); return 1; }
    /* C: the tank goes to sleep mid-dance; the deep-sleep wake has the fry at once */
    n0 = spawn_prep(42);
    for (i = 0; i < 60 * 120 && !tank.spawning; i++) { tank_tick(&tank, DT, advisor_rules); progression_tick(&tank, DT); }
    if (!tank.spawning) { printf("FAIL: no courtship to interrupt\n"); return 1; }
    progression_save(&tank);                       /* the sleep key saves */
    tank_init(&tank, 43);
    progression_wake(&tank, clock_port_now_unix() + 2 * 3600);
    if (tank.n_fish != n0 + 1 || progression_newborn() != n0 || progression_arrival_pending()) { printf("FAIL: the wake did not deliver the fry (%d fish, owed %d)\n", tank.n_fish, progression_newborn()); return 1; }
    /* D: the grace's quick wake (resume in place) delivers it too */
    n0 = spawn_prep(44);
    for (i = 0; i < 60 * 3; i++) { tank_tick(&tank, DT, advisor_rules); progression_tick(&tank, DT); }
    tank_tick_sleep(&tank, 45);
    progression_woke(&tank);
    if (tank.n_fish != n0 + 1 || progression_arrival_pending()) { printf("FAIL: the quick wake did not deliver the fry\n"); return 1; }
    printf("selftest-pop: slept before the birth: the fry is born at the wake (deep sleep and the quick wake)\n");
    return 0;
}

/* population + progression + persistence, headless and fast */
static int selftest_pop(void) {
    setenv("POCKET_TANK_SAVE", "/dev/null/pocket-tank-selftest.sav", 1);
    tank_init(&tank, 98);
    if (progression_save(&tank)) { printf("FAIL: an unwritable save reported success\n"); return 1; }
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest/nested/tank.sav", 1);   /* also checks parent creation */
    char cmd[600]; snprintf(cmd, sizeof cmd, "rm -f /tmp/pocket-tank-selftest/nested/tank.sav"); (void)system(cmd);
    tank_init(&tank, 99);
    progression_boot(&tank);                      /* no save -> new random pair */
    print_roster(&tank);
    if (tank.n_fish != 2) { printf("FAIL: new tank should start with 2\n"); return 1; }
    if (!progression_setup_pending()) { printf("FAIL: a new tank should owe the first-run setup\n"); return 1; }
    /* the new-fry checklist (2026-09-14) reads the same gates: a fresh pair
       owes trust, meals and a hold, and has the default garden's nursery */
    {
        fry_req_t req[FRY_REQ_MAX]; bool staged;
        int n = progression_next_fry(&tank, req, &staged);
        if (n != 5 || staged) { printf("FAIL: a new pair should list 5 fry gates, none staged (%d, %d)\n", n, staged); return 1; }
        int met = 0; for (int i = 0; i < n; i++) met += req[i].met;
        if (req[0].kind != FRY_REQ_TRUST || req[1].kind != FRY_REQ_FEED || req[2].kind != FRY_REQ_HOLD || req[3].kind != FRY_REQ_GLASS || req[4].kind != FRY_REQ_GRASS
            || met != 2 || !req[3].met || !req[4].met) { printf("FAIL: a fresh pair's gates: %d met, glass %d, grass %d\n", met, req[3].met, req[4].met); return 1; }
        printf("selftest-pop: fry checklist: %d gates (%s / %s / %s / %s / %s)\n", n, req[0].progress, req[1].progress, req[2].progress, req[3].progress, req[4].progress);
        tank.player_feedings = 12; progression_next_fry(&tank, req, &staged);
        if (!req[1].met || strcmp(req[1].progress, "DONE") || req[1].frac != 1.0f) { printf("FAIL: 12 feedings should meet the MEALS gate (%s)\n", req[1].progress); return 1; }
        tank.player_feedings = 3; progression_next_fry(&tank, req, &staged);
        if (req[1].met || req[1].frac < 0.24f || req[1].frac > 0.26f) { printf("FAIL: 3 of 12 feedings should be a quarter (%.2f)\n", req[1].frac); return 1; }
        tank.player_feedings = 0;
        float g0 = tank.veg_growth[0], g1 = tank.veg_growth[1], g2 = tank.veg_growth[2];
        tank_veg_set(&tank, 0, VEG_NUB); tank_veg_set(&tank, 1, VEG_NUB); tank_veg_set(&tank, 2, VEG_NUB);
        progression_next_fry(&tank, req, &staged);
        if (req[4].met || req[4].frac >= 1.0f) { printf("FAIL: scalped beds should leave the GRASS gate owed (%s)\n", req[4].progress); return 1; }
        tank_veg_set(&tank, 0, g0); tank_veg_set(&tank, 1, g1); tank_veg_set(&tank, 2, g2);
        /* the GLASS gate (2026-09-16): a dirty tank - film on more than
           ALGAE_DIRTY of the glass - holds the fry; wiping it back under
           frees it. The bar fills as the film comes off. */
        film_just_over(&tank, ALGAE_DIRTY);                                 /* just over the line */
        progression_next_fry(&tank, req, &staged);
#ifdef CONFIG_POCKET_TANK_320X240
        const float nearly_full = 1.0f - 1.0f / ALGAE_CELLS / ALGAE_DIRTY - 1e-4f;   /* (this fork) one cell over: one of the CYD's
                                                                                       300 is 2.2% of the bar (one of 644, 1%) */
#else
        const float nearly_full = 0.98f;
#endif
        if (req[3].met || req[3].frac < nearly_full) { printf("FAIL: film just over ALGAE_DIRTY should leave GLASS owed, bar nearly full (%d, %.2f)\n", req[3].met, req[3].frac); return 1; }
        int met_dirty = 0; for (int i = 0; i < n; i++) met_dirty += req[i].met;
        if (met_dirty != 1) { printf("FAIL: a dirty tank should meet only GRASS (%d met)\n", met_dirty); return 1; }
        for (int i = 0; i < ALGAE_CELLS; i++) tank.algae[i] = 0;
        tank_grow_algae(&tank, 2000);                                       /* an untended tank: up to the growth cap */
        progression_next_fry(&tank, req, &staged);
        printf("selftest-pop: fry checklist GLASS at the cap: %s / %s (bar %.2f)\n", req[3].words, req[3].progress, req[3].frac);
        if (req[3].met || req[3].frac > 0.05f || strcmp(req[3].title, "GLASS")) { printf("FAIL: a tank at the cap should have GLASS owed with an empty bar (%d, %.2f)\n", req[3].met, req[3].frac); return 1; }
        for (int i = 0; i < ALGAE_CELLS; i++) tank.algae[i] = 0;
        tank.algae[0] = tank.algae[1] = 200;                                /* a little film is fine */
        progression_next_fry(&tank, req, &staged);
        if (!req[3].met || req[3].frac != 1.0f || strcmp(req[3].progress, "CURRENTLY CLEAN ENOUGH")) { printf("FAIL: two filmed cells should pass GLASS (%s)\n", req[3].progress); return 1; }
        for (int i = 0; i < ALGAE_CELLS; i++) tank.algae[i] = 0;
    }
    progression_time_scale = 600;                 /* 10 minutes of tended time per second */
    int arrivals = 0, last_n = tank.n_fish;
    bool saw_court = false;                       /* the tell fires before the fry */
    bool saw_spawn = false;                       /* ... and the fry comes out of the spawning */
    float hold_x = 0, hold_y = 0;
    /* up to 20 sim-minutes: the gates close in a few and the fry is born
       seconds later, after the pair courts in the grass (the spawning,
       2026-09-24 - it used to wait for the next light-on). The keeper here
       tends for 30 s and leaves it alone for 30 s, lights-out opted in, so
       the birth may land lit or dark */
    tank.light_auto = true;                       /* this keeper opted into lights-out (the default is MANUAL) */
    for (int i = 0; i < 60 * 60 * 20 && arrivals == 0; i++) {
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        /* an attentive keeper: feeds often, rests a finger by a fish */
        bool tending = i % 3600 < 1800;
        if (tending && i % 300 == 0) tank_feed(&tank, 150 + (i % 900) / 3, 2);
        if (tending && i % 600 == 0) { hold_y = tank.fish[0].y;       /* across the tank, 80 px in from the glass at the fish's height */
                                           hold_x = tank.fish[0].x < TANK_W / 2 ? tank_glass_x1(hold_y) - 80 : tank_glass_x0(hold_y) + 80; }
        if (tending && i % 600 < 480) tank_touch_hold(&tank, hold_x, hold_y);   /* 8 s across the tank, 2 s off */
        progression_tick(&tank, 1.0f / 60.0f);
        saw_court |= (tank.courting && arrivals == 0);
        saw_spawn |= (tank.spawning && arrivals == 0);
        if (tank.n_fish != last_n) {
            printf("  t=%.0fs arrival: %s (%s) bold %.2f social %.2f -> %d fish\n", tank.clock,
                   tank.fish[tank.n_fish - 1].name, STAGE_NAMES[tank.fish[tank.n_fish - 1].stage],
                   tank.fish[tank.n_fish - 1].bold, tank.fish[tank.n_fish - 1].sociable, tank.n_fish);
            last_n = tank.n_fish; arrivals++;
        }
    }
    printf("selftest-pop: %d arrivals, %d fish, feedings %d, hold-approaches %d, pending %d, courted %d\n",
           arrivals, tank.n_fish, tank.player_feedings, tank.hold_approaches, progression_arrival_pending(), saw_court);
    if (arrivals > 0 && !saw_court) { printf("FAIL: no courtship tell before the first arrival\n"); return 1; }
    if (arrivals > 0 && !saw_spawn) { printf("FAIL: the first fry was born without the spawning\n"); return 1; }
    for (int i = 0; i < tank.n_fish; i++)
        printf("  %-5s %s age %.0fh size %.2f ms 0x%03x trust %.1f\n", tank.fish[i].name,
               STAGE_NAMES[tank.fish[i].stage], progression_age_s(&tank, i) / 3600, tank.fish[i].size,
               tank.fish[i].ms_bits, tank.fish[i].trust);
    printf("  tank ms 0x%03x\n", tank.tank_ms_bits);
    if (arrivals < 1) { printf("FAIL: no arrival earned by an attentive keeper\n"); return 1; }
    {   /* the checklist follows the population: a full tank lists nothing,
           a growing one lists the next arrival's gates + grass */
        fry_req_t req[FRY_REQ_MAX];
        int n = progression_next_fry(&tank, req, NULL);
        if (tank.n_fish >= POP_CAP ? n != 0 : (n < 4 || req[n - 2].kind != FRY_REQ_GLASS || req[n - 1].kind != FRY_REQ_GRASS)) {
            printf("FAIL: checklist lists %d gates at %d fish\n", n, tank.n_fish); return 1; }
        if (n > 0) {   /* and the page draws it: the row, the name's tally, the first gate's modal */
            static uint16_t fb[TANK_W * TANK_H];
            int top = MS_ROW(tank.n_fish);
            const int mid_y = MSP_FRY_MODAL_Y + 90;            /* the middle of a gate's panel (and of its tip page): no button there */
            render_milestones(&tank, fb, TANK_W);
            if (ms_tap(MS_NAME_X, top + MS_NAME_DY) != MS_TAP_KEPT) { printf("FAIL: the NEW FRY name did not open its modal\n"); return 1; }
            render_milestones(&tank, fb, TANK_W);
            if (ms_tap(MS_NAME_X, top + MS_NAME_DY) != MS_TAP_KEPT) { printf("FAIL: a tap did not close the modal\n"); return 1; }
            if (ms_tap(MS_BADGE_X(0), top + MS_BADGE_DY) != MS_TAP_KEPT) { printf("FAIL: the first gate did not open its modal\n"); return 1; }
            render_milestones(&tank, fb, TANK_W);
            /* HOW? (bottom right of the gate's panel) flips to the tip page; the next tap closes it */
            if (ms_tap(MS_HOW_X + MSP_HOW_W / 2, MS_HOW_Y + MSP_HOW_H / 2) != MS_TAP_KEPT) { printf("FAIL: HOW? did not open the tip\n"); return 1; }
            render_milestones(&tank, fb, TANK_W);
            if (ms_tap(MS_OFF_X, mid_y) != MS_TAP_KEPT) { printf("FAIL: a tap did not close the tip page\n"); return 1; }
            /* a low, wide press - 26 px under the button's foot, 30 px past its side - still opens it (the slop) */
            if (ms_tap(MS_BADGE_X(0), top + MS_BADGE_DY) != MS_TAP_KEPT) { printf("FAIL: the gate did not reopen\n"); return 1; }
            if (ms_tap(MS_HOW_X + MSP_HOW_W + UI(30), MS_HOW_Y + MSP_HOW_H + UI(26)) != MS_TAP_KEPT) { printf("FAIL: a low wide press missed HOW?\n"); return 1; }
            render_milestones(&tank, fb, TANK_W);
            if (ms_tap(MS_OFF_X, mid_y) != MS_TAP_KEPT) { printf("FAIL: a tap did not close the tip page\n"); return 1; }
            if (ms_tap(MS_BADGE_X(0), top + MS_BADGE_DY) != MS_TAP_KEPT) { printf("FAIL: the gate did not reopen\n"); return 1; }
            if (ms_tap(MS_OFF_X, mid_y) != MS_TAP_KEPT) { printf("FAIL: a tap off HOW? did not close the modal\n"); return 1; }
            if (ms_tap(MS_OFF_X, MS_OFF_Y) != MS_TAP_NONE) { printf("FAIL: the modal is still up\n"); return 1; }   /* an empty row */
            render_milestones_leave();
            if (ms_tap(MS_BADGE_X(n), top + MS_BADGE_DY) != MS_TAP_NONE) { printf("FAIL: an empty gate cell opened a modal\n"); return 1; }
            {   /* the modal's arrows (2026-09-16): a step shows something else, six steps come back round, the left
                   arrow from the first badge is the last; a fish's name steps through the fish; TANK's tally has none */
                uint32_t h[8];
                render_milestones_leave();
                if (ms_tap(MS_BADGE_X(0), MS_ROW(0) + MS_BADGE_DY) != MS_TAP_KEPT) { printf("FAIL: fish 0's first badge did not open its modal\n"); return 1; }
                for (int i = 0; i < 7; i++) {
                    render_milestones(&tank, fb, TANK_W);
                    h[i] = 2166136261u; for (int q = 0; q < TANK_W * TANK_H; q++) h[i] = (h[i] ^ fb[q]) * 16777619u;
                    if (ms_tap(MS_ARROW_R_X, MS_ARROW_Y) != MS_TAP_KEPT) { printf("FAIL: the right arrow dropped the modal\n"); return 1; }
                }
                for (int i = 1; i < 6; i++) for (int j = 0; j < i; j++) if (h[i] == h[j]) { printf("FAIL: arrow step %d showed step %d's modal again\n", i, j); return 1; }
                if (h[6] != h[0]) { printf("FAIL: six right steps did not come back round\n"); return 1; }
                for (int i = 0; i < 2; i++) {      /* seven rights sit on badge 1: a left is badge 0, another wraps to the last */
                    if (ms_tap(MS_ARROW_L_X, MS_ARROW_Y) != MS_TAP_KEPT) { printf("FAIL: the left arrow dropped the modal\n"); return 1; }
                    render_milestones(&tank, fb, TANK_W);
                    uint32_t hl = 2166136261u; for (int q = 0; q < TANK_W * TANK_H; q++) hl = (hl ^ fb[q]) * 16777619u;
                    if (hl != h[i ? 5 : 0]) { printf("FAIL: the left arrow did not step back to badge %d\n", i ? 5 : 0); return 1; }
                }
                if (ms_tap(MS_OFF_X, MS_OFF_Y) != MS_TAP_KEPT) { printf("FAIL: a tap off the arrows did not close the modal\n"); return 1; }
                if (ms_tap(MS_NAME_X, MS_ROW(0) + MS_NAME_DY) != MS_TAP_KEPT) { printf("FAIL: fish 0's name did not open its modal\n"); return 1; }
                for (int i = 0; i <= tank.n_fish; i++) {
                    render_milestones(&tank, fb, TANK_W);
                    h[i] = 2166136261u; for (int q = 0; q < TANK_W * TANK_H; q++) h[i] = (h[i] ^ fb[q]) * 16777619u;
                    int ax, ay;                        /* a fish's card stands higher than a badge's modal (2026-10-01): ask where its arrow is */
                    if (!render_milestones_arrow(&tank, true, &ax, &ay) || render_milestones_tap(&tank, (float)ax, (float)ay) != MS_TAP_KEPT) { printf("FAIL: the right arrow dropped the name modal\n"); return 1; }
                }
                if (h[1] == h[0] || h[tank.n_fish] != h[0]) { printf("FAIL: a fish's name did not step through the %d fish and back\n", tank.n_fish); return 1; }
                render_milestones_leave();
                if (ms_tap(MS_NAME_X, MSP_TANK_Y + 10) != MS_TAP_KEPT) { printf("FAIL: TANK's tally did not open\n"); return 1; }
                ms_tap(MS_ARROW_R_X, MS_ARROW_Y);                               /* no arrows on a group of one: this closes it */
                if (ms_tap(MS_OFF_X, MS_OFF_Y) != MS_TAP_NONE) { printf("FAIL: TANK's tally grew arrows\n"); return 1; }
                printf("selftest-pop: the modal's arrows cycle the badges, the fish and back round; the tally has none\n");
            }
            printf("selftest-pop: NEW FRY row at %d fish: %d gates, first %s / %s / %s\n", tank.n_fish, n, req[0].title, req[0].words, req[0].progress);
            if (tank.n_fish == 3) {   /* CHANGE (2026-09-15) is the fry's own drift pressure: owed at birth, earned on a clamp */
                int ci = -1; for (int i = 0; i < n; i++) if (req[i].kind == FRY_REQ_CHANGE) ci = i;
                if (n != 5 || ci < 0 || req[ci].met || req[ci].frac > 0.01f) { printf("FAIL: at 3 fish CHANGE should be owed by the fry (n %d, met %d, frac %.2f)\n", n, ci >= 0 && req[ci].met, ci >= 0 ? req[ci].frac : -1.f); return 1; }
                fish_t *fry = &tank.fish[2];
                fry->bold = fry->bold0 = 0.95f; fry->sociable = fry->sociable0 = 0.05f;   /* born on both clamps */
                tank.night = false;
                for (int i = 0; i < 60 * 3; i++) {               /* 3 s x 600 = 30 lit minutes, fed and calm */
                    fry->hunger = 1; fry->stress = 0; fry->goal.id = GOAL_REST;
                    progression_tick(&tank, 1.0f / 60.0f);
                }
                progression_next_fry(&tank, req, NULL);
                printf("selftest-pop: fry on both clamps, 30 lit min fed+calm: CHANGE %s (%s), traits %.2f/%.2f\n",
                       req[ci].met ? "met" : "owed", req[ci].progress, fry->bold, fry->sociable);
                if (!req[ci].met) { printf("FAIL: CHANGE soft-locked on a clamped fry\n"); return 1; }
            }
        }
    }
    /* the newest fry is owed its welcome (the birth flow) and wears the
       family's colours: the body of one parent, the markings of the other */
    int nb = progression_newborn();
    if (nb != tank.n_fish - 1) { printf("FAIL: the last arrival (%d) is not owed the birth flow (%d)\n", tank.n_fish - 1, nb); return 1; }
    {
        const fish_t *f = &tank.fish[nb];
        if (f->parent_a < 0 || f->parent_b < 0 || f->parent_a >= nb || f->parent_b >= nb) { printf("FAIL: newborn parents %d/%d\n", f->parent_a, f->parent_b); return 1; }
        const fish_t *pa = &tank.fish[f->parent_a], *pb = &tank.fish[f->parent_b];
        if (f->color != pa->color || (f->accent != pb->accent && pb->accent != pa->color)) {   /* (markings step aside from a matching body) */
            printf("FAIL: newborn look %06x/%06x is not %s's body + %s's markings (%06x/%06x)\n", f->color, f->accent, pa->name, pb->name, pa->color, pb->accent); return 1;
        }
        printf("  newborn %s: body from %s, markings from %s, bold %.2f (%.2f/%.2f) social %.2f (%.2f/%.2f)\n", f->name, pa->name, pb->name,
               f->bold, pa->bold, pb->bold, f->sociable, pa->sociable, pb->sociable);
    }
    /* round-trip the save */
    bool pending_at_save = progression_arrival_pending();
    progression_save(&tank);
    tank_t saved = tank;
    tank_init(&tank, 5); progression_boot(&tank);
    /* an arrival earned but not yet born is delivered at boot (the wake) */
    int expect = saved.n_fish + (pending_at_save ? 1 : 0);
    if (tank.n_fish != expect) { printf("FAIL: restore n_fish %d != %d\n", tank.n_fish, expect); return 1; }
    for (int i = 0; i < saved.n_fish; i++)
        if (tank.fish[i].preset != saved.fish[i].preset || tank.fish[i].ms_bits != saved.fish[i].ms_bits ||
            fabsf(tank.fish[i].bold - saved.fish[i].bold) > 1e-4f ||
            tank.fish[i].parent_a != saved.fish[i].parent_a || tank.fish[i].parent_b != saved.fish[i].parent_b) {
            printf("FAIL: restore mismatch on fish %d\n", i); return 1;
        }
    /* the debt came back too (or moved to the fry delivered at boot) */
    if (progression_newborn() != tank.n_fish - 1) { printf("FAIL: the birth flow owed is %d after the reload, not %d\n", progression_newborn(), tank.n_fish - 1); return 1; }
    printf("  save/restore ok (%d fish, tank ms 0x%03x, fry %d owed its welcome)\n", tank.n_fish, tank.tank_ms_bits, progression_newborn());
    /* the birth flow (setup.c): the platforms poll it open; announce ->
       name (the wheel) -> family (BACK / DONE); DONE pays the debt and
       saves the name */
    {
        static uint16_t fb[TANK_W * TANK_H];
        nb = progression_newborn();
        if (setup_poll_birth(&tank) != nb || !setup_active() || !setup_is_birth() || setup_page() != SETUP_PG_BORN || setup_fish() != nb) {
            printf("FAIL: the birth flow did not open on the announcement (%d)\n", setup_page()); return 1;
        }
        if (setup_poll_birth(&tank) != -1) { printf("FAIL: the poll re-opened a flow already up\n"); return 1; }
        if (pg_hit(SETUP_MID_X + UI(20), SETUP_BTN_Y + UI(20)) != SETUP_HIT_NEXT || pg_hit(SETUP_TOP_NEXT_X + UI(20), SETUP_TOP_BTN_Y + UI(20)) != 0) { printf("FAIL: announcement hit-test\n"); return 1; }
        render_setup(&tank, fb, TANK_W, 1.0f);
        setup_activate(&tank, SETUP_HIT_NEXT);
        setup_touch(&tank, 0, 0, false);
        if (setup_page() != SETUP_PG_NAME_NEW || setup_fish() != nb || tank.stage_fish != nb) { printf("FAIL: MEET IT did not reach the fry's name page (page %d, stage %d)\n", setup_page(), tank.stage_fish); return 1; }
        char was[FISH_NAME_MAX + 1]; strcpy(was, tank.fish[nb].name);
        setup_activate(&tank, SETUP_HIT_SLOT0 + 0); setup_activate(&tank, SETUP_HIT_UP);   /* the first letter, one step on */
        if (!strcmp(tank.fish[nb].name, was) || setup_fish() != nb) { printf("FAIL: the wheel did not turn the fry's name\n"); return 1; }
        render_setup(&tank, fb, TANK_W, 1.0f);
        setup_activate(&tank, SETUP_HIT_NEXT);
        char named[FISH_NAME_MAX + 1]; strcpy(named, tank.fish[nb].name);
        if (setup_page() != SETUP_PG_FAMILY || tank.stage_fish != -1) { printf("FAIL: NEXT did not reach the family page\n"); return 1; }
        if (pg_hit(SETUP_NEXT_X + UI(20), SETUP_BTN_Y + UI(20)) != SETUP_HIT_NEXT || pg_hit(SETUP_BACK_X + UI(20), SETUP_BTN_Y + UI(20)) != SETUP_HIT_BACK ||
            pg_hit(SETUP_MID_X + UI(55), SETUP_FAM_ROW_Y) != 0) { printf("FAIL: family page hit-test\n"); return 1; }
        render_setup(&tank, fb, TANK_W, 1.0f);
        setup_activate(&tank, SETUP_HIT_BACK);
        if (setup_page() != SETUP_PG_NAME_NEW || strcmp(tank.fish[nb].name, named)) { printf("FAIL: BACK from the family page lost the name\n"); return 1; }
        setup_activate(&tank, SETUP_HIT_BACK);
        if (setup_page() != SETUP_PG_BORN) { printf("FAIL: BACK did not return to the announcement\n"); return 1; }
        setup_activate(&tank, SETUP_HIT_BACK);
        if (setup_page() != SETUP_PG_BORN) { printf("FAIL: BACK left the flow\n"); return 1; }
        setup_activate(&tank, SETUP_HIT_NEXT); setup_activate(&tank, SETUP_HIT_NEXT); setup_activate(&tank, SETUP_HIT_NEXT);   /* DONE */
        if (setup_active() || progression_newborn() != -1 || tank.stage_fish != -1) { printf("FAIL: DONE did not pay the birth debt\n"); return 1; }
        if (setup_poll_birth(&tank) != -1) { printf("FAIL: the poll opened a flow with nothing owed\n"); return 1; }
        tank_init(&tank, 6); progression_boot(&tank);
        if (progression_newborn() != -1 || strcmp(tank.fish[nb].name, named)) { printf("FAIL: the fry's name ('%s') or the paid debt (%d) did not survive a reboot\n", tank.fish[nb].name, progression_newborn()); return 1; }
        printf("  birth flow ok: %s (was %s), named through the wheel, saved and reloaded, nothing owed\n", named, was);
    }
    /* the keeper's reset: every save gone, two fry with nothing tended, and
       the fresh pair already saved so a reboot lands on them */
    progression_reset(&tank, 11);
    if (tank.n_fish != 2 || tank.fish[0].stage != STAGE_FRY || tank.fish[1].stage != STAGE_FRY ||
        tank.tank_ms_bits != TMS_PAIR || tank.player_feedings != 0 || progression_age_s(&tank, 0) != 0) {
        printf("FAIL: reset did not give a fresh pair\n"); return 1;
    }
    tank_t fresh = tank;
    tank_init(&tank, 12); progression_boot(&tank);
    if (tank.n_fish != 2 || tank.fish[0].preset != fresh.fish[0].preset ||
        tank.fish[1].preset != fresh.fish[1].preset || tank.tank_ms_bits != TMS_PAIR) {
        printf("FAIL: the reset tank did not come back from its save\n"); return 1;
    }
    if (render_confirm_hit(PG_X(RENDER_CONFIRM_NO_X + 10), PG_Y(RENDER_CONFIRM_BTN_Y + 10)) != -1 ||
        render_confirm_hit(PG_X(RENDER_CONFIRM_YES_X + 10), PG_Y(RENDER_CONFIRM_BTN_Y + 10)) != 1 ||
        render_confirm_hit(PG_X(RENDER_CONFIRM_X + 5), PG_Y(RENDER_CONFIRM_Y + 5)) != 0 || render_confirm_hit(5, 5) != 0) {
        printf("FAIL: confirm buttons hit-test\n"); return 1;
    }
    printf("  reset ok: %s + %s, both fry, saved and reloaded\n", tank.fish[0].name, tank.fish[1].name);
    /* the first-run setup (setup.c): the reset tank owes it; walk it the way
       a finger would - hit-test where a finger lands, activate what came
       back, drag the letter wheel through setup_touch - name the first fish
       BUB, give it the blue body and white accent, empty the second name
       (the preset's returns), BEGIN; then reload and find it all kept */
    if (!progression_setup_pending()) { printf("FAIL: the reset tank should owe the setup\n"); return 1; }
    setup_begin(&tank);
    if (!setup_active() || setup_page() != SETUP_PG_WELCOME) { printf("FAIL: setup did not open on the welcome page\n"); return 1; }
    if (pg_hit(SETUP_MID_X + UI(20), SETUP_BTN_Y + UI(20)) != SETUP_HIT_NEXT || setup_hit(5, 5) != 0) { printf("FAIL: welcome NEXT hit-test\n"); return 1; }
    setup_activate(&tank, pg_hit(SETUP_MID_X + UI(20), SETUP_BTN_Y + UI(20)));
    if (setup_page() != SETUP_PG_BUBBLES) { printf("FAIL: NEXT did not reach the bubbles page\n"); return 1; }
    /* the bubble column: a press on the water brings it there, a drag moves
       it, the rising bubbles come along, the reef and the grass push it off;
       the release is no tap */
    {
        const float bx = TANK_FLOOR_X(300.0f), by = PG_Y(200);      /* on this board's floor; in the water under the page's buttons */
        setup_touch(&tank, bx, by, true);
        tank_t probe = tank; tank_set_bubble_x(&probe, bx);
        if (fabsf(tank.bubble_x - probe.bubble_x) > 0.5f) { printf("FAIL: press put the column at %.0f, not %.0f\n", tank.bubble_x, probe.bubble_x); return 1; }
        for (int k = 1; k <= 20; k++) setup_touch(&tank, bx + k * 2, by, true);
        probe = tank; tank_set_bubble_x(&probe, bx + 40);
        if (fabsf(tank.bubble_x - probe.bubble_x) > 0.5f) { printf("FAIL: drag left the column at %.0f\n", tank.bubble_x); return 1; }
        for (int i = 0; i < MAX_BUBBLE; i++)
            if (tank.bubble[i].column && fabsf(tank.bubble[i].x - tank.bubble_x) > 12) { printf("FAIL: a column bubble stayed behind\n"); return 1; }
        setup_touch(&tank, bx + 40, by, false);
        if (setup_page() != SETUP_PG_BUBBLES) { printf("FAIL: the drag's release acted as a tap\n"); return 1; }
        tank_set_bubble_x(&tank, 5);
        if (tank.bubble_x < tank.reef_x + 50) { printf("FAIL: the column sits on the reef (%.0f)\n", tank.bubble_x); return 1; }
        tank_set_bubble_x(&tank, 900);
        if (tank.bubble_x > TANK_FX1 - 30) { printf("FAIL: the column sits in the glass (%.0f)\n", tank.bubble_x); return 1; }   /* (the floor's end: the glass itself in the rectangle) */
        tank_set_bubble_x(&tank, bx + 40);
    }
    float bubble_x_set = tank.bubble_x;
    setup_activate(&tank, SETUP_HIT_NEXT);
    if (setup_page() != SETUP_PG_NAME_A) { printf("FAIL: NEXT did not reach the name page\n"); return 1; }
    /* the fish being named takes the stage - the clear spot above the letters */
    setup_touch(&tank, 0, 0, false);
    if (tank.stage_fish != 0) { printf("FAIL: fish 0 not staged\n"); return 1; }
    tank.fish[0].x = 60; tank.fish[0].y = 300;
    for (int i = 0; i < 900; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
    if (tank_dist(tank.fish[0].x, tank.fish[0].y, SETUP_STAGE_X, SETUP_STAGE_NAME_Y) > 45) {
        printf("FAIL: staged fish at (%.0f,%.0f), stage (%d,%d)\n", tank.fish[0].x, tank.fish[0].y, SETUP_STAGE_X, SETUP_STAGE_NAME_Y); return 1;
    }
    const char *preset0 = tank_roster_name(tank.fish[0].preset);
    /* the wheel's bands: the row picks a slot, above / below the active one are the chevrons */
    if (pg_hit(SETUP_SLOT_X + 2 * SETUP_SLOT_PX + UI(10), SETUP_SLOT_Y + UI(20)) != SETUP_HIT_SLOT0 + 2 ||
        pg_hit(SETUP_SLOT_X + UI(15), SETUP_SLOT_Y - UI(50)) != SETUP_HIT_UP ||
        pg_hit(SETUP_SLOT_X + UI(15), SETUP_SLOT_Y + SETUP_SLOT_H + UI(60)) != SETUP_HIT_DOWN ||
        pg_hit(SETUP_TOP_NEXT_X + UI(20), SETUP_TOP_BTN_Y + UI(20)) != SETUP_HIT_NEXT) { printf("FAIL: name page hit-test\n"); return 1; }
    for (int i = 0; i < 3; i++) {                     /* B U B by spinning three slots */
        setup_activate(&tank, SETUP_HIT_SLOT0 + i);
        if (setup_slot() != i) { printf("FAIL: slot %d not picked\n", i); return 1; }
        int guard = 30;
        while (guard-- && toupper((unsigned char)tank.fish[0].name[i]) != "BUB"[i]) setup_activate(&tank, SETUP_HIT_UP);
    }
    while ((int)strlen(tank.fish[0].name) > 3) {       /* a longer preset: blank the tail */
        setup_activate(&tank, SETUP_HIT_SLOT0 + 3);
        int guard = 30;
        while (guard-- && (int)strlen(tank.fish[0].name) > 3) setup_activate(&tank, SETUP_HIT_DOWN);
    }
    if (strcmp(tank.fish[0].name, "bub")) { printf("FAIL: spun name is '%s'\n", tank.fish[0].name); return 1; }
    /* the drag: press on slot 2, pull up three steps (B -> E), no tap fires on
       release; pull back down three: B again */
    {
        float sx = PG_X(SETUP_SLOT_X + 2 * SETUP_SLOT_PX + 15), sy = PG_Y(SETUP_SLOT_Y + 20);
        setup_touch(&tank, sx, sy, true);
        for (int k = 1; k <= 30; k++) setup_touch(&tank, sx, sy - k * 3 * SETUP_SPIN_PX / 30.0f - 0.5f, true);
        if (strcmp(tank.fish[0].name, "bue")) { printf("FAIL: drag up gave '%s'\n", tank.fish[0].name); return 1; }
        setup_touch(&tank, sx, sy - 3 * SETUP_SPIN_PX, false);
        if (strcmp(tank.fish[0].name, "bue") || setup_slot() != 2) { printf("FAIL: the drag's release acted as a tap\n"); return 1; }
        setup_touch(&tank, sx, sy, true);
        for (int k = 1; k <= 30; k++) setup_touch(&tank, sx, sy + k * 3 * SETUP_SPIN_PX / 30.0f + 0.5f, true);
        setup_touch(&tank, sx, sy, false);
        if (strcmp(tank.fish[0].name, "bub")) { printf("FAIL: drag down gave '%s'\n", tank.fish[0].name); return 1; }
    }
    pg_touch(SETUP_TOP_NEXT_X + UI(20), SETUP_TOP_BTN_Y + UI(20), true);     /* a tap through setup_touch */
    pg_touch(SETUP_TOP_NEXT_X + UI(22), SETUP_TOP_BTN_Y + UI(24), false);
    if (setup_page() != SETUP_PG_LOOK_A) { printf("FAIL: NEXT did not reach the look page\n"); return 1; }
    /* the swatch row takes a finger 30 px under it (fingers land low), never the "?" */
    if (pg_hit(SETUP_SW_X + 6 * SETUP_SW_PX + UI(20), SETUP_SW_Y + SETUP_SW_H + UI(30)) != SETUP_HIT_BODY0 + 6 ||
        pg_hit(SETUP_SW_X + UI(20), SETUP_ACC_Y + UI(30)) != 0) { printf("FAIL: swatch row hit-test\n"); return 1; }
    setup_activate(&tank, pg_hit(SETUP_SW_X + 6 * SETUP_SW_PX + UI(20), SETUP_SW_Y + UI(25)));
    uint32_t accent0 = tank.fish[0].accent;
    if (tank.fish[0].color != LOOK_BODY[6] || accent0 == LOOK_BODY[6]) {
        printf("FAIL: swatch gave body %06x accent %06x\n", tank.fish[0].color, tank.fish[0].accent); return 1;
    }
    /* a body the same as the accent: the accent moves (stripes must show) */
    tank_set_look(&tank, 0, accent0, 0);
    if (tank.fish[0].accent == accent0) { printf("FAIL: accent did not step aside for a matching body\n"); return 1; }
    tank_set_look(&tank, 0, LOOK_BODY[6], accent0);
    setup_activate(&tank, SETUP_HIT_BACK);            /* back to the name page and forward again: name kept */
    if (setup_page() != SETUP_PG_NAME_A || strcmp(tank.fish[0].name, "bub")) { printf("FAIL: BACK lost the name\n"); return 1; }
    setup_activate(&tank, SETUP_HIT_NEXT); setup_activate(&tank, SETUP_HIT_NEXT);
    if (setup_page() != SETUP_PG_NAME_B) { printf("FAIL: not on the second name page\n"); return 1; }
    const char *preset1 = tank_roster_name(tank.fish[1].preset);
    for (int i = (int)strlen(tank.fish[1].name) - 1; i >= 0; i--) {   /* blank it from the end... */
        setup_activate(&tank, SETUP_HIT_SLOT0 + i);
        int guard = 30;
        while (guard-- && (int)strlen(tank.fish[1].name) > i) setup_activate(&tank, SETUP_HIT_DOWN);
    }
    if (tank.fish[1].name[0]) { printf("FAIL: could not blank the name ('%s')\n", tank.fish[1].name); return 1; }
    setup_activate(&tank, SETUP_HIT_NEXT);                                  /* ...and leave it empty: the preset's returns */
    if (strcmp(tank.fish[1].name, preset1)) { printf("FAIL: empty name did not fall back ('%s')\n", tank.fish[1].name); return 1; }
    setup_activate(&tank, SETUP_HIT_NEXT);
    if (setup_page() != SETUP_PG_CARE) { printf("FAIL: not on the care page (%d)\n", setup_page()); return 1; }
    setup_activate(&tank, SETUP_HIT_NEXT);            /* BEGIN */
    if (setup_active() || progression_setup_pending() || tank.stage_fish != -1) { printf("FAIL: BEGIN did not finish the setup\n"); return 1; }
    tank_init(&tank, 13); progression_boot(&tank);
    if (fabsf(tank.bubble_x - bubble_x_set) > 0.5f) { printf("FAIL: the bubble column did not come back from the save (%.0f)\n", tank.bubble_x); return 1; }
    if (progression_setup_pending() || strcmp(tank.fish[0].name, "bub") || strcmp(tank.fish[1].name, preset1) ||
        tank.fish[0].color != LOOK_BODY[6] || tank.fish[0].accent != accent0 || strcmp(preset0, tank_roster_name(tank.fish[0].preset))) {
        printf("FAIL: names/looks did not come back from the save ('%s' %06x/%06x, pending %d)\n",
               tank.fish[0].name, tank.fish[0].color, tank.fish[0].accent, progression_setup_pending()); return 1;
    }
    printf("  setup ok: %s (blue) + %s, bubbles at x %.0f, saved and reloaded, nothing owed\n", tank.fish[0].name, tank.fish[1].name, tank.bubble_x);
    /* notices wait for a fry's welcome (2026-09-29, Strato: the milestone and
       the welcome overlapped). The frame order both platforms keep: the tank
       ticks (the fry is born, its badge set), the notices tick, THEN the
       welcome opens - so the badge came up the frame before it. */
    {
        const float fdt = 1.0f / 60;
#define NOTICE_FRAME() notice_tick(&tank, fdt, setup_active() || setup_birth_due())
        notice_sync(&tank);
        progression_force_arrival(&tank);                 /* born: owed its welcome */
        NOTICE_FRAME();
        if (notice_current()) { printf("FAIL: a notice came up before the welcome opened\n"); return 1; }
        if (setup_poll_birth(&tank) < 0 || !setup_is_birth()) { printf("FAIL: the welcome did not open\n"); return 1; }
        int held = notice_pending();
        for (int i = 0; i < 120; i++) { NOTICE_FRAME(); if (notice_current()) { printf("FAIL: a notice over the welcome\n"); return 1; } }
        for (int guard = 0; guard < 12 && setup_active(); guard++) setup_activate(&tank, SETUP_HIT_NEXT);   /* MEET IT ... DONE */
        if (setup_active() || progression_newborn() >= 0) { printf("FAIL: could not finish the welcome\n"); return 1; }
        int up_at = -1;
        for (int i = 0; i < 180 && up_at < 0; i++) { NOTICE_FRAME(); if (notice_current()) up_at = i; }
        if (held > 0 && up_at < 0) { printf("FAIL: the %d held notice(s) never came after the welcome\n", held); return 1; }
        notice_dismiss();
        for (int i = 0; i < 600 && (notice_current() || notice_pending()); i++) { NOTICE_FRAME(); if (notice_current()) notice_dismiss(); }
        /* the safety net: a notice already up when something opens steps aside, and returns quietly */
        notice_low_battery();
        for (int i = 0; i < 180 && !notice_current(); i++) NOTICE_FRAME();
        if (!notice_current() || notice_take_cue() < 0) { printf("FAIL: the test notice did not come up with its cue\n"); return 1; }
        notice_tick(&tank, fdt, true);
        if (notice_current() || notice_pending() != 1) { printf("FAIL: a covered notice did not step aside (pending %d)\n", notice_pending()); return 1; }
        notice_tick(&tank, fdt, false);
        if (!notice_current() || notice_take_cue() >= 0) { printf("FAIL: the stepped-aside notice did not return quietly\n"); return 1; }
        notice_dismiss();
#undef NOTICE_FRAME
        printf("  notices wait for the welcome: %d held through it, up %d frames after DONE; a covered one steps aside and returns quietly\n", held, up_at);
    }
    if (selftest_spawn()) return 1;
    (void)system(cmd);                            /* leave no test save behind */
    return 0;
}

/* sleep metabolism + ravenous begging, headless (the device drowse path):
 * a long dark gap starves everyone -> they beg at the surface, the trickle
 * holds off, and the keeper's first pellets end the wait. */
/* the night shift (2026-10-02, Strato: the snail and the urchin "shouldn't
 * remove all the maintenance duties, but it should be perceptible that having
 * them in the tank helps keep both the algae and grass at bay even when the
 * tank is in deep sleep"). From a wiped glass and grass trimmed to VEG_START:
 * a night with both wakes visibly cleaner and lower than the same night
 * without, yet with film and growth still there; a week away settles under
 * the alone tank's cap; naps add up to the same night as one span; the
 * sword plant and the keep line are the urchin's limits. */
static float ns_grass(const tank_t *t) { float g = 0; for (int b = 0; b < VEG_BEDS; b++) g += t->veg_growth[b]; return g / VEG_BEDS; }
static void ns_tank(tank_t *t, bool helpers) {
    tank_init(t, 1234); tank_new_population(t);
    t->sd_unlocks = SD_ITEM_PLANT | (helpers ? SD_ITEM_SNAIL | SD_ITEM_URCHIN : 0);
    tank_plant_place(t);
    if (helpers) { tank_snail_place(t); tank_urchin_place(t); }
    memset(t->algae, 0, sizeof t->algae);
    for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(t, b, VEG_START);
}
static int night_shift_check(void) {
    static tank_t alone, both;
    float hours[] = { 7, 7 * 24 };
    for (int k = 0; k < 2; k++) {
        ns_tank(&alone, false); ns_tank(&both, true);
        tank_tick_sleep(&alone, hours[k] * 3600); tank_tick_sleep(&both, hours[k] * 3600);
        float fa = tank_algae_cover(&alone), fb = tank_algae_cover(&both), ga = ns_grass(&alone), gb = ns_grass(&both);
        printf("selftest-sleep: night shift, %3.0f h: film %.3f alone, %.3f with the snail; grass %.2f alone, %.2f with the urchin; sword plant %.2f / %.2f\n",
               hours[k], fa, fb, ga, gb, alone.veg_growth[3], both.veg_growth[3]);
        if (fb > fa * 0.80f || fb < fa * 0.35f) { printf("FAIL: the snail's night shift is not a perceptible share (%.3f vs %.3f)\n", fb, fa); return 1; }
        if (ga - gb < 0.08f || gb < VEG_START + (k ? 0.15f : 0.10f)) { printf("FAIL: the urchin's night shift: grass %.2f vs %.2f\n", gb, ga); return 1; }
        if (fabsf(both.veg_growth[3] - alone.veg_growth[3]) > 1e-4f) { printf("FAIL: the urchin ate the sword plant\n"); return 1; }
        for (int b = 0; b < VEG_BEDS; b++) for (int i = 0; i < VEG_FRONDS_MAX; i++)
            if (both.veg_h[b][i] > 0 && both.veg_h[b][i] < URCHIN_KEEP - 1e-4f && alone.veg_h[b][i] >= URCHIN_KEEP) { printf("FAIL: the urchin took bed %d frond %d under the keep line (%.3f)\n", b, i, both.veg_h[b][i]); return 1; }
        if (both.urchin_grazed_px <= 0 || both.snail_grazed <= 0) { printf("FAIL: the tallies did not count the night\n"); return 1; }
    }
    /* naps: the 20-minute graces of a night add up to one span (a few cells of chance either way) */
    static tank_t naps; ns_tank(&naps, true); ns_tank(&both, true);
    for (int i = 0; i < 21; i++) tank_tick_sleep(&naps, 1200);
    tank_tick_sleep(&both, 7 * 3600);
    printf("selftest-sleep: night shift in 21 naps: film %.3f grass %.2f (one span %.3f / %.2f)\n", tank_algae_cover(&naps), ns_grass(&naps), tank_algae_cover(&both), ns_grass(&both));
    if (fabsf(tank_algae_cover(&naps) - tank_algae_cover(&both)) > 0.03f || fabsf(ns_grass(&naps) - ns_grass(&both)) > 0.01f) { printf("FAIL: naps and one span disagree\n"); return 1; }
    return 0;
}

#ifdef CONFIG_POCKET_TANK_320X240
/* (this fork) which SLEEP segment the settings page drew lit, read off the
   frame rather than the tap test, so a row that taps right but draws wrong
   still fails. The pixel 3 down inside each segment (under its double
   outline, over its lettering) is the lit fill in one segment and the unlit
   fill in the other two. -1: no row there - all three are the page's
   background, the pixel just left of the segments. -2: anything else. */
static int cyd_sleep_lit(const uint16_t *fb) {
    const int y = PG_Y(SET_SEG_Y(SET_ROW6_Y) + 3);
    uint16_t px[3];
    for (int k = 0; k < 3; k++) px[k] = fb[y * TANK_W + PG_X(SETP_SEG_X(k))];
    uint16_t background = fb[y * TANK_W + PG_X(SET_SEG_X - 4)];
    if (px[0] == background && px[1] == background && px[2] == background) return -1;
    for (int k = 0; k < 3; k++) {
        int other1 = (k + 1) % 3, other2 = (k + 2) % 3;
        if (px[other1] == px[other2] && px[other1] != background && px[k] != px[other1]) return k;
    }
    return -2;
}
#endif

static int selftest_sleep(void) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest.sav", 1);
    char cmd[600]; snprintf(cmd, sizeof cmd, "rm -f /tmp/pocket-tank-selftest.sav"); (void)system(cmd);
    tank_init(&tank, 4242);
    progression_boot(&tank);
    for (int i = 0; i < 600; i++) { tank_tick(&tank, 1.0f / 60.0f, advisor_rules); progression_tick(&tank, 1.0f / 60.0f); }
    tank_tick_sleep(&tank, 12 * 3600);            /* a long night away */
    for (int i = 0; i < tank.n_fish; i++) {
        const fish_t *f = &tank.fish[i];
        if (f->hunger < 8.5f || f->energy < 9.9f) {
            printf("FAIL: after 12h sleep %s hunger %.1f energy %.1f\n", f->name, f->hunger, f->energy);
            return 1;
        }
    }
    for (int i = 0; i < MAX_FOOD; i++)
        if (tank.food[i].alive) { printf("FAIL: pellet survived the night\n"); return 1; }
    /* wake: begging engages, everyone rises to the surface, no self-serve */
    float avg_y0 = 0;
    for (int i = 0; i < tank.n_fish; i++) avg_y0 += tank.fish[i].y / tank.n_fish;
    for (int step = 1; step <= 600; step++) {      /* 10 s awake */
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        progression_tick(&tank, 1.0f / 60.0f);
        if (step == 120 && !tank.ravenous) { printf("FAIL: not ravenous after starving sleep\n"); return 1; }
    }
    float avg_y = 0; int food_n = 0;
    for (int i = 0; i < tank.n_fish; i++) avg_y += tank.fish[i].y / tank.n_fish;
    for (int i = 0; i < MAX_FOOD; i++) food_n += tank.food[i].alive;
    printf("selftest-sleep: begging avg y %.0f (was %.0f), trickle held (%d pellets)\n", avg_y, avg_y0, food_n);
    if (avg_y > 100) { printf("FAIL: fish not waiting at the surface\n"); return 1; }
    if (food_n) { printf("FAIL: trickle fed a begging tank\n"); return 1; }
    /* the keeper arrives: starving fish DASH for the fresh pellets (the
     * frenzy presentation), and feeding everyone ends the state */
    /* the pellets land 90 px to one side of where the school begs: the test
       measures the DASH, and a fish that happens to be sitting under the
       drop (a matter of begging phase) has nothing to dash for */
    tank_feed(&tank, TANK_W * 0.5f + 90, 4);
    int fed_at = -1; float dash_speed = 0;
    for (int step = 1; step <= 2400 && fed_at < 0; step++) {   /* 40 s: one fish may gobble
                                                                     every pellet; the trickle
                                                                     then serves the other */
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        progression_tick(&tank, 1.0f / 60.0f);
        if (step <= 30)                               /* within 0.5 s of the drop */
            for (int i = 0; i < tank.n_fish; i++)
                if (tank.fish[i].hunger > 6.5f && tank.fish[i].target_speed > dash_speed)
                    dash_speed = tank.fish[i].target_speed;
        if (!tank.ravenous) fed_at = step;
    }
    if (fed_at < 0) {
        printf("FAIL: feeding did not end the begging\n");
        for (int i = 0; i < tank.n_fish; i++)
            printf("  DEBUG %s hunger %.1f at (%.0f,%.0f) speed %.0f goal %s size %.2f\n", tank.fish[i].name, tank.fish[i].hunger,
                   tank.fish[i].x, tank.fish[i].y, tank.fish[i].speed, GOAL_NAMES[tank.fish[i].goal.id], tank.fish[i].size);
        for (int i = 0; i < MAX_FOOD; i++) if (tank.food[i].alive) printf("  DEBUG pellet (%.0f,%.0f) age %.0f\n", tank.food[i].x, tank.food[i].y, tank.food[i].age);
        printf("  DEBUG ravenous %d feed_spot %.0f\n", tank.ravenous, tank.feed_spot_x);
        return 1;
    }
    /* the device's deep-sleep wake (2026-09-14): save, forget everything,
       come back "8 hours later" - the night is lived through in one step,
       NOT the cold-boot ravenous rule (which would pin everyone at 9.6) */
    {
        for (int i = 0; i < tank.n_fish; i++) tank.fish[i].hunger = 2.0f;
        tank_veg_set(&tank, 1, 0.30f);                   /* the 12 h above grew it to the ceiling */
        progression_save(&tank);
        float veg0 = tank.veg_growth[1], age0 = progression_age_s(&tank, 0);
        tank_init(&tank, 8);
        if (tank.tank_ms_bits & TMS_FIRST_FULL_NIGHT) { printf("FAIL: full-night badge before any night\n"); return 1; }
        float h = progression_wake(&tank, clock_port_now_unix() + 8 * 3600);
        if (h < 7.99f || h > 8.01f) { printf("FAIL: wake simulated %.2f h, not 8\n", h); return 1; }
        /* and they grew through it, slowly: 8 h asleep = 2 h of growth (SLEEP_GROWTH_FRAC) */
        float grew = progression_age_s(&tank, 0) - age0, hunger_wake = tank.fish[0].hunger;
        if (fabsf(grew - 8 * 3600 * SLEEP_GROWTH_FRAC) > 1.0f) { printf("FAIL: 8 h asleep grew %.0f s, want %.0f\n", grew, 8 * 3600 * SLEEP_GROWTH_FRAC); return 1; }
        for (int i = 0; i < tank.n_fish; i++)
            if (fabsf(tank.fish[i].hunger - 8.4f) > 0.15f) {     /* 2.0 + 8 h x 0.8/h */
                printf("FAIL: after an 8 h wake %s hunger %.1f (want 8.4)\n", tank.fish[i].name, tank.fish[i].hunger); return 1; }
        if (tank.veg_growth[1] <= veg0) { printf("FAIL: the grass did not grow through the night\n"); return 1; }
        /* the light is the idle detector's (2026-09-15): the wake lit it; left
           alone it goes out after LIGHT_IDLE_S; a touch lights it again; and
           awake, the dark no longer pauses growth (2026-09-14): 60 s of dark
           ticks age 60 s */
        {
            if (tank.night) { printf("FAIL: the wake did not light the tank\n"); return 1; }
            for (int i = 0; i < 30 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
            if (tank.night) { printf("FAIL: MANUAL (the default) went dark on its own\n"); return 1; }
            tank.light_auto = true; tank_handled(&tank);                /* the keeper opts in */
            for (int i = 0; i < 14 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
            if (tank.night) { printf("FAIL: lights out after 14 s still (idle %.1f)\n", tank.idle_s); return 1; }
            for (int i = 0; i < 2 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
            if (!tank.night) { printf("FAIL: still lit after 16 s still (idle %.1f)\n", tank.idle_s); return 1; }
            float a0 = progression_age_s(&tank, 0);
            for (int i = 0; i < 60 * 60; i++) { tank_tick(&tank, 1.0f / 60.0f, advisor_rules); progression_tick(&tank, 1.0f / 60.0f); }
            float dark = progression_age_s(&tank, 0) - a0;
            if (!tank.night || dark < 59.0f || dark > 61.0f) { printf("FAIL: 60 s with the light off aged %.0f s (night %d)\n", dark, tank.night); return 1; }
            tank_touch_tap(&tank, 200, 200); tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
            if (tank.night) { printf("FAIL: a tap did not light the tank\n"); return 1; }
            for (int i = 0; i < 20 * 60; i++) { tank_tick(&tank, 1.0f / 60.0f, advisor_rules); if (i % 60 == 0) tank_handled(&tank); }
            if (tank.night) { printf("FAIL: a tank handled every second went dark\n"); return 1; }
            tank.hold_light = true;                     /* a setup page holds the light however still */
            for (int i = 0; i < 20 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
            if (tank.night) { printf("FAIL: hold_light did not hold the light\n"); return 1; }
            tank.hold_light = false; tank.light_auto = false;
        }
        /* the settings page (2026-09-15; one LIGHTS OUT row since 0.3.2):
           DOUBLE-TAP (MANUAL) keeps the light on however still; a step to
           the right is AUTO, the idle rule, after 5 SEC .. 30 MIN; the row's
           left half steps back. Then AUTO FEED and ROTATION (SCREEN on the watch) */
        {
            static uint16_t sfb[TANK_W * TANK_H];
            int v = 0, r;
            #define SET_TOUCH(X, Y, DOWN) render_settings_touch(&tank, PG_X(X), PG_Y(Y), (DOWN), &v)   /* page coordinates in */
            #define SET_TAP_AT(X, Y) (SET_TOUCH(X, Y, true), SET_TOUCH(X, Y, false))
            const int light_y = SET_ROW3_Y + 10;                             /* on the LIGHTS OUT segments */
            if (tank.light_idle_s != LIGHT_IDLE_S || tank.light_auto) { printf("FAIL: light settings not at the default (%d s, auto %d)\n", tank.light_idle_s, tank.light_auto); return 1; }
            r = SET_TAP_AT(SETP_PREV_X, light_y);                             /* LIGHTS OUT: already at the first choice, the double-tap */
            if (r != SET_TAP_NONE || tank.light_auto || tank_light_choice(&tank) != 0) { printf("FAIL: back from the first LIGHTS OUT choice -> %d, auto %d\n", r, tank.light_auto); return 1; }
            for (int i = 0; i < 30 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
            if (tank.night) { printf("FAIL: lights went out in MANUAL\n"); return 1; }
            render_settings(&tank, sfb, TANK_W, 60, 2);
            /* in MANUAL a double-tap on the glass flips the light, and the flip rides in the save */
            notice_sync(&tank);
            tank_touch_tap(&tank, 200, 200); tank_touch_tap(&tank, 200, 200);
            for (int i = 0; i < 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
            if (!tank.night || !tank.light_manual_off) { printf("FAIL: a double-tap in MANUAL did not turn the light off\n"); return 1; }
            /* the first time, a notice says what happened (2026-10-03): once, no chime, and its tap reaches the tank */
            if (!tank.light_tip_seen) { printf("FAIL: the first lights-out did not mark its notice\n"); return 1; }
            for (int i = 0; i < 180 && !notice_current(); i++) notice_tick(&tank, 1.0f / 60.0f, false);
            if (!notice_current() || notice_current()->kind != NOTICE_LIGHTS_OUT || notice_take_cue() >= 0) { printf("FAIL: no quiet lights-out notice after the first double-tap\n"); return 1; }
            if (notice_dismiss()) { printf("FAIL: the lights-out notice swallowed its tap\n"); return 1; }
            progression_save(&tank); { int keep = tank.light_idle_s; tank_init(&tank, 8); progression_boot(&tank); tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
              if (!tank.light_manual_off || !tank.night || tank.light_auto || tank.light_idle_s != keep) { printf("FAIL: the manual light-off did not survive the save\n"); return 1; }
              if (!tank.light_tip_seen) { printf("FAIL: the lights-out notice's mark did not survive the save\n"); return 1; }
              notice_sync(&tank); }
            tank_touch_tap(&tank, 200, 200); tank_touch_tap(&tank, 200, 200);
            for (int i = 0; i < 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
            if (tank.night) { printf("FAIL: a second double-tap did not turn the light back on\n"); return 1; }
            tank_touch_tap(&tank, 200, 200); tank_touch_tap(&tank, 200, 200);   /* off again: no second notice */
            for (int i = 0; i < 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
            for (int i = 0; i < 180; i++) notice_tick(&tank, 1.0f / 60.0f, false);
            if (!tank.night || notice_current() || notice_pending()) { printf("FAIL: the lights-out notice came up a second time\n"); return 1; }
            tank_touch_tap(&tank, 200, 200); tank_touch_tap(&tank, 200, 200);   /* and on, for what follows */
            for (int i = 0; i < 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
            if (tank.night) { printf("FAIL: the light did not come back on\n"); return 1; }
            for (int i = 0; i < 3; i++) tank_touch_tap(&tank, 200, 200);         /* three taps = a startle, not a toggle */
            for (int i = 0; i < 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
            if (tank.night) { printf("FAIL: a triple tap toggled the light\n"); return 1; }
            r = SET_TAP_AT(SETP_NEXT_X, light_y);                             /* one step on: AUTO, after the shortest time */
            if (r != SET_TAP_LIGHT || v != 1 || !tank.light_auto || tank.light_manual_off || tank.light_idle_s != 5) { printf("FAIL: LIGHTS OUT's first step -> %d/%d, %d s\n", r, v, tank.light_idle_s); return 1; }
            tank_touch_tap(&tank, 200, 200); tank_touch_tap(&tank, 200, 200);   /* in AUTO a double-tap is nothing */
            for (int i = 0; i < 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
            if (tank.night || tank.light_manual_off) { printf("FAIL: a double-tap in AUTO touched the light\n"); return 1; }
            r = SET_TAP_AT(SETP_NEXT_X, light_y);                             /* 5 -> 15 */
            if (r != SET_TAP_IDLE || v != 15 || tank.light_idle_s != 15) { printf("FAIL: the next choice -> %d, %d s\n", r, tank.light_idle_s); return 1; }
            { static const int want[] = { 30, 60, 180, 300, 600, 1800 };   /* Strato's list, to its end */
              for (int k = 0; k < 6; k++) { r = SET_TAP_AT(SETP_SEG_X(2), light_y);   /* (anywhere on the row's right half) */
                  if (r != SET_TAP_IDLE || tank.light_idle_s != want[k]) { printf("FAIL: LIGHTS OUT choice %d -> %d, %d s (want %d)\n", k + 3, r, tank.light_idle_s, want[k]); return 1; } } }
            r = SET_TAP_AT(SETP_NEXT_X, light_y);                             /* the end of the list holds */
            if (r != SET_TAP_NONE || tank.light_idle_s != 1800) { printf("FAIL: past the last LIGHTS OUT choice -> %d, %d s\n", r, tank.light_idle_s); return 1; }
            render_settings(&tank, sfb, TANK_W, 60, 2);
            for (int k = 0; k < 5; k++) r = SET_TAP_AT(SETP_PREV_X, light_y);  /* back to 30 SEC */
            if (r != SET_TAP_IDLE || tank.light_idle_s != 30) { printf("FAIL: back five choices -> %d, %d s\n", r, tank.light_idle_s); return 1; }
            tank_handled(&tank);
            for (int i = 0; i < 28 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
            if (tank.night) { printf("FAIL: dark at 28 s with 30 s set\n"); return 1; }
            for (int i = 0; i < 3 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
            if (!tank.night) { printf("FAIL: lit at 31 s with 30 s set\n"); return 1; }
            tank.light_idle_s = 20;                                            /* a save from the wheel's days: honoured, shown as the nearest choice */
            if (tank_light_choice(&tank) != 2) { printf("FAIL: 20 s reads as choice %d (want 15 SEC)\n", tank_light_choice(&tank)); return 1; }
            r = SET_TAP_AT(SETP_NEXT_X, light_y);
            if (r != SET_TAP_IDLE || tank.light_idle_s != 30) { printf("FAIL: a step from a wheel-era 20 s -> %d s\n", tank.light_idle_s); return 1; }
            for (int k = 0; k < 3; k++) r = SET_TAP_AT(SETP_PREV_X, light_y);  /* 15, 5, and back to the double-tap */
            if (r != SET_TAP_LIGHT || v != 0 || tank.light_auto) { printf("FAIL: back to the double-tap -> %d/%d\n", r, v); return 1; }
            tank_light_choice_set(&tank, 3);                                   /* 30 SEC, for the save below */
            /* AUTO FEED (0.3.2): ON by default, OFF and back */
            {
                const int feed_y = SET_ROW4_Y + 10;
                if (tank.autofeed_off) { printf("FAIL: AUTO FEED not ON at the start\n"); return 1; }
                r = SET_TAP_AT(SETP_SEG_X(1), feed_y);
                if (r != SET_TAP_FEED || v != 0 || !tank.autofeed_off) { printf("FAIL: AUTO FEED OFF -> %d/%d\n", r, v); return 1; }
                r = SET_TAP_AT(SETP_SEG_X(0), feed_y);
                if (r != SET_TAP_FEED || v != 1 || tank.autofeed_off) { printf("FAIL: AUTO FEED ON -> %d/%d\n", r, v); return 1; }
                r = SET_TAP_AT(SETP_SEG_X(1), feed_y);                               /* OFF, for the save below */
                render_settings(&tank, sfb, TANK_W, 60, 2);
            }
#if TANK_WORN
            /* SCREEN (the watch, 2026-10-02): NORMAL by default, the toggle
               turns the picture and back, and the choice rides in the save */
            {
                const int scr_y = SET_ROW5_Y + 10;
                if (tank_screen_turned(&tank)) { printf("FAIL: SCREEN not NORMAL at the start\n"); return 1; }
                r = SET_TAP_AT(SETP_SEG_X(1), scr_y);                                /* TURNED */
                if (r != SET_TAP_SCREEN || v != 1 || !tank_screen_turned(&tank)) { printf("FAIL: SCREEN TURNED -> %d/%d\n", r, v); return 1; }
                r = SET_TAP_AT(SETP_SEG_X(0), scr_y);                                /* NORMAL */
                if (r != SET_TAP_SCREEN || v != 0 || tank_screen_turned(&tank)) { printf("FAIL: SCREEN NORMAL -> %d/%d\n", r, v); return 1; }
                r = SET_TAP_AT(SETP_SEG_X(1), scr_y);
                render_settings(&tank, sfb, TANK_W, 60, 2);
                progression_save(&tank); tank_init(&tank, 8); progression_boot(&tank);
                if (!tank_screen_turned(&tank)) { printf("FAIL: SCREEN TURNED did not survive the save\n"); return 1; }
                tank_screen_set(&tank, false);
                printf("selftest-sleep: SCREEN: NORMAL by default, TURNED and back, saved\n");
            }
#else
#ifdef CONFIG_POCKET_TANK_320X240
            /* (this fork) the CYD's row where the others have ROTATION is chosen
               at run time: SCREEN with no IMU (the platform applies it); with
               one, ROTATION - tested below. Under it, IMU or not, SLEEP:
               NEVER / SCREEN / LIGHT, the segment for the platform to apply;
               a deepsleep build has no row there (render_settings_set_sleep(-1)) */
            {
                const int row5_y = SET_ROW5_Y + 10, row6_y = SET_ROW6_Y + 10;
                render_settings_set_sleep(SET_SLEEP_LIGHT);
                render_settings_set_imu(false);
                r = SET_TAP_AT(SETP_SEG_X(1), row5_y);
                if (r != SET_TAP_FLIP || v != 1) { printf("FAIL: SCREEN FLIPPED (no IMU) -> %d/%d\n", r, v); return 1; }
                r = SET_TAP_AT(SETP_SEG_X(0), row5_y);
                if (r != SET_TAP_FLIP || v != 0) { printf("FAIL: SCREEN UPRIGHT (no IMU) -> %d/%d\n", r, v); return 1; }
                for (int k = 0; k < 3; k++) {
                    r = SET_TAP_AT(SETP_SEG_X(k), row6_y);
                    if (r != SET_TAP_SLEEP || v != k) { printf("FAIL: SLEEP segment %d (no IMU) -> %d/%d\n", k, r, v); return 1; }
                }
                /* ... and drawn: the platform's choice is the segment lit */
                for (int k = 0; k < 3; k++) {
                    render_settings_set_sleep(k);
                    render_settings(&tank, sfb, TANK_W, 60, 2);
                    if (cyd_sleep_lit(sfb) != k) { printf("FAIL: SLEEP %d (no IMU) drawn with %d lit\n", k, cyd_sleep_lit(sfb)); return 1; }
                }
                render_settings_set_imu(true);
                for (int k = 0; k < 3; k++) {
                    r = SET_TAP_AT(SETP_SEG_X(k), row6_y);
                    if (r != SET_TAP_SLEEP || v != k) { printf("FAIL: SLEEP segment %d (an IMU) -> %d/%d\n", k, r, v); return 1; }
                }
                render_settings(&tank, sfb, TANK_W, 60, 2);
                if (cyd_sleep_lit(sfb) != SET_SLEEP_LIGHT) { printf("FAIL: SLEEP LIGHT (an IMU) drawn with %d lit\n", cyd_sleep_lit(sfb)); return 1; }
                render_settings_set_sleep(-1);                                   /* a deepsleep build: no row */
                r = SET_TAP_AT(SETP_SEG_X(0), row6_y);
                if (r != SET_TAP_NONE) { printf("FAIL: a SLEEP row on a deepsleep build -> %d\n", r); return 1; }
                render_settings(&tank, sfb, TANK_W, 60, 2);
                if (cyd_sleep_lit(sfb) != -1) { printf("FAIL: a SLEEP row drawn on a deepsleep build (%d)\n", cyd_sleep_lit(sfb)); return 1; }
                render_settings_set_sleep(SET_SLEEP_LIGHT);
                printf("selftest-sleep: the CYD's rows: SCREEN with no IMU, ROTATION with one; SLEEP NEVER / SCREEN / LIGHT under either, tapped and drawn, none on a deepsleep build\n");
            }
#endif
            tank_screen_set(&tank, true);
            if (tank_screen_turned(&tank)) { printf("FAIL: a desk tank's screen turned by the setting\n"); return 1; }
            /* ROTATION (0.3.2): the picture follows the tank's flip until the
               keeper locks it - then it keeps the way up it had, through a
               save, until it is unlocked */
            {
                const int rot_y = SET_ROW5_Y + 10;
                if (tank.orient_lock || tank_orient(&tank, false) || !tank_orient(&tank, true)) { printf("FAIL: the picture does not follow the tank by default\n"); return 1; }
                r = SET_TAP_AT(SETP_SEG_X(0), rot_y);                                /* locked, while turned over */
                if (r != SET_TAP_ROTATE || v != 1 || !tank.orient_lock) { printf("FAIL: ROTATION lock -> %d/%d\n", r, v); return 1; }
                if (!tank_orient(&tank, false) || !tank_orient(&tank, true)) { printf("FAIL: a locked picture turned with the tank\n"); return 1; }
                render_settings(&tank, sfb, TANK_W, 60, 2);
                progression_save(&tank); tank_init(&tank, 8); progression_boot(&tank);
                if (!tank.orient_lock || !tank_orient(&tank, false)) { printf("FAIL: the locked way up did not survive the save\n"); return 1; }
                r = SET_TAP_AT(SET_ROT_WORD_X + 20, rot_y);                          /* its word is the button too: unlocked */
                if (r != SET_TAP_ROTATE || v != 0 || tank.orient_lock || tank_orient(&tank, false)) { printf("FAIL: ROTATION unlock -> %d/%d\n", r, v); return 1; }
                r = SET_TAP_AT(SETP_SEG_X(0), rot_y);                                /* locked upright this time */
                if (!tank.orient_lock || tank_orient(&tank, true)) { printf("FAIL: a picture locked upright turned over\n"); return 1; }
                r = SET_TAP_AT(SETP_SEG_X(0), rot_y);
                printf("selftest-sleep: ROTATION: follows the tank by default, locks the way up it has (either way), saved, unlocks\n");
            }
#ifdef CONFIG_POCKET_TANK_320X240
            render_settings_set_imu(false);
#endif
#endif
            r = SET_TAP_AT(SET_CLOSE_X + 40, SET_FOOT_Y + 10);
            if (r != SET_TAP_CLOSE) { printf("FAIL: CLOSE -> %d\n", r); return 1; }
            progression_save(&tank); tank_init(&tank, 8); progression_boot(&tank);
            if (tank.light_idle_s != 30 || !tank.light_auto) { printf("FAIL: light settings did not survive the save (%d s)\n", tank.light_idle_s); return 1; }
            if (!tank.autofeed_off) { printf("FAIL: AUTO FEED OFF did not survive the save\n"); return 1; }
            tank.light_idle_s = LIGHT_IDLE_S; tank.light_auto = false; tank.autofeed_off = false;
            printf("selftest-sleep: settings: LIGHTS OUT one row (the double-tap, saved / 5 SEC .. 30 MIN, both ends hold, a wheel-era 20 s), 30 s honoured, AUTO FEED, saved\n");
            #undef SET_TAP_AT
            #undef SET_TOUCH
        }
        /* the full-night badge: one stretch of device sleep as long as a night */
        if (!(tank.tank_ms_bits & TMS_FIRST_FULL_NIGHT)) { printf("FAIL: the 8 h wake did not earn the full-night badge\n"); return 1; }
        printf("selftest-sleep: deep-sleep wake lived through %.0f h (hunger 2.0 -> %.1f, bed 1 %.2f -> %.2f); grew %.0f s asleep, 60 s in the dark\n",
               h, hunger_wake, veg0, tank.veg_growth[1], grew);
    }
    /* an older build's save is shorter - whatever length that build's struct
       had, padding included (the seen-masks build wrote 1440, the family
       build looked for 1436 and replaced a live tank with two fry on
       2026-09-14) - and must still come back: same fish, same name */
    {
        tank_set_name(&tank, 0, "Fez");
        progression_save(&tank);
        const char *sav = getenv("POCKET_TANK_SAVE");
        static const long older[] = { 1480, 1440, 1436, 1408, 1304, 1112, 448 };   /* 1480 = the light-settings build, before the shop */
        for (size_t k = 0; k < sizeof older / sizeof *older; k++) {
            if (truncate(sav, older[k])) { printf("FAIL: could not truncate the save to %ld\n", older[k]); return 1; }
            tank_init(&tank, 8);
            progression_boot(&tank);
            if (tank.n_fish != 2 || strcmp(tank.fish[0].name, older[k] >= 1304 + 4 + 8 ? "fez" : tank_roster_name(tank.fish[0].preset))) {
                printf("FAIL: a %ld-byte save came back as %d fish, %s\n", older[k], tank.n_fish, tank.n_fish ? tank.fish[0].name : "-"); return 1; }
        }
        if (truncate(sav, 100)) return 1;
        tank_init(&tank, 8); progression_boot(&tank);
        if (!progression_setup_pending()) { printf("FAIL: a 100-byte save is not ours\n"); return 1; }
        printf("selftest-sleep: older saves (1480 .. 448 bytes) load; a 100-byte one starts fresh\n");
        /* the browser installer's first builds (public 2026-09-13 .. 09-14) wrote
           1432 bytes with the seen masks at 1404, where bubble_x was inserted the
           next day: such a save must come back with its badges still seen and the
           bubble column at the default, not at "mask bits as a float" (= the left edge) */
        tank_init(&tank, 8); progression_boot(&tank);    /* a fresh pair (setup pending) */
        tank_set_name(&tank, 0, "Fez");
        tank.fish[0].ms_bits |= MS_ARRIVED | MS_FIRST_MEAL_FROM_YOU; tank.fish[0].ms_seen = tank.fish[0].ms_bits;
        tank.tank_ms_bits |= TMS_FIRST_FULL_NIGHT; tank.tank_ms_seen = tank.tank_ms_bits;
        uint32_t want_seen = tank.fish[0].ms_seen, want_tseen = tank.tank_ms_seen;
        float bx_default = tank.bubble_x;
        progression_save(&tank);
        {
            unsigned char buf[4096]; FILE *f = fopen(sav, "rb"); if (!f) return 1;
            size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
            if (n < 1436) { printf("FAIL: the save is only %zu bytes\n", n); return 1; }
            memmove(buf + 1404, buf + 1408, 28);         /* drop bubble_x: masks back to the old spot */
            f = fopen(sav, "wb"); if (!f) return 1;
            fwrite(buf, 1, 1432, f); fclose(f);
        }
        tank_init(&tank, 8); progression_boot(&tank);
        if (tank.n_fish != 2 || strcmp(tank.fish[0].name, "fez") || tank.fish[0].ms_seen != want_seen ||
            tank.tank_ms_seen != want_tseen || tank.bubble_x != bx_default) {
            printf("FAIL: the 1432-byte pre-bubble save came back as %d fish, %s, seen %08x/%08x (want %08x/%08x), bubble %.0f (want %.0f)\n",
                   tank.n_fish, tank.n_fish ? tank.fish[0].name : "-", tank.fish[0].ms_seen, tank.tank_ms_seen,
                   want_seen, want_tseen, tank.bubble_x, bx_default); return 1; }
        printf("selftest-sleep: a 1432-byte save from the first public installer builds keeps its badges seen and its bubble column\n");
    }
    printf("selftest-sleep: dash %.0f px/s at the drop; fed and calmed %.1f s after pellets\n",
           dash_speed, fed_at / 60.0f);
    if (dash_speed < 80) { printf("FAIL: no feeding-frenzy dash (%.0f px/s)\n", dash_speed); return 1; }
    (void)system(cmd);
    return night_shift_check();
}

/* --selftest-saves (2026-09-29): the update promise - a browser update never
 * loses the tank. Every save in testdata/saves (frozen: the board's, the
 * sim's, one with every tail set; see the README there) loads, whole and cut
 * to every older build's length (the 1432 cut in the first public
 * installer's pre-bubble layout). What comes back must match the file's own
 * bytes read at the offsets of progression.c's SAVE LAYOUT LOCK, a cut-off
 * tail reading as the defaults - and again after a save and a reload. */
#ifndef _MSC_VER
#include <dirent.h>
static const size_t SAVE_CUTS[] = { 448, 1112, 1304, 1408, 1432, 1440, 1456, 1480, 1608, 1616, 1624, 1640, 1656, 1664, 1672, 1680, 1688 };
#define SAVE_NOW 1688                    /* today's sizeof(save_t): the urchin, 2026-10-02 (its x and its tally) */
static uint32_t sv_u32(const uint8_t *e, size_t off) { uint32_t v; memcpy(&v, e + off, 4); return v; }
static float    sv_f32(const uint8_t *e, size_t off) { float v; memcpy(&v, e + off, 4); return v; }
static int name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* the loaded tank vs e (the save's bytes in today's layout, zeros past the cut) */
static int saves_check(const char *what, const uint8_t *e, float bubble_default) {
    #define SV_FAIL(...) do { printf("FAIL: %s: ", what); printf(__VA_ARGS__); printf("\n"); return 1; } while (0)
    int n = e[23];
    if (tank.n_fish != n && !(e[22] && tank.n_fish == n + 1)) SV_FAIL("%d fish, want %d", tank.n_fish, n);
    if (progression_setup_pending() != (e[1304] != 0)) SV_FAIL("setup pending %d", progression_setup_pending());
    for (int i = 0; i < n; i++) {
        const fish_t *f = &tank.fish[i]; const uint8_t *fs = e + 40 + 68 * i;
        uint32_t ms = sv_u32(fs, 64) & ~MS_RETIRED_MASK;
        char want[FISH_NAME_MAX + 1] = { 0 }; const char *nm = (const char *)e + 1308 + (FISH_NAME_MAX + 1) * i;
        for (int k = 0; k < FISH_NAME_MAX && nm[k]; k++) want[k] = (char)tolower((unsigned char)nm[k]);
        if (!want[0]) snprintf(want, sizeof want, "%s", tank_roster_name(fs[0] % tank_roster_count()));
        if (f->preset != fs[0] % tank_roster_count()) SV_FAIL("fish %d preset %d, want %d", i, f->preset, fs[0]);
        if (strcmp(f->name, want)) SV_FAIL("fish %d is %s, want %s", i, f->name, want);
        if (f->trust != sv_f32(fs, 8) || progression_age_s(&tank, i) != sv_f32(fs, 44))
            SV_FAIL("fish %d trust %.2f age %.0f s, want %.2f / %.0f", i, f->trust, progression_age_s(&tank, i), sv_f32(fs, 8), sv_f32(fs, 44));
        if (f->eaten != (int32_t)sv_u32(fs, 56) || f->eaten_player != (int32_t)sv_u32(fs, 60)) SV_FAIL("fish %d meals %d/%d", i, f->eaten, f->eaten_player);
        if (f->ms_bits != ms || f->ms_seen != (sv_u32(e, 1408 + 4 * i) & ms))
            SV_FAIL("fish %d badges %03x seen %03x, want %03x / %03x", i, f->ms_bits, f->ms_seen, ms, sv_u32(e, 1408 + 4 * i) & ms);
        if (sv_u32(e, 1356 + 4 * i) && f->color != sv_u32(e, 1356 + 4 * i)) SV_FAIL("fish %d color %06x", i, f->color);
        int pa = e[1437 + 2 * i] - 1; if (pa >= n) pa = -1;
        if (f->parent_a != pa) SV_FAIL("fish %d parent %d, want %d", i, f->parent_a, pa);
    }
    int nb = e[1436] && e[1436] <= n ? e[1436] - 1 : -1;
    if (progression_newborn() != nb) SV_FAIL("newborn %d, want %d", progression_newborn(), nb);
    if (tank.player_feedings != (int32_t)sv_u32(e, 28) || tank.tank_ms_bits != sv_u32(e, 36) ||
        tank.tank_ms_seen != (sv_u32(e, 1432) & sv_u32(e, 36))) SV_FAIL("feedings %d, tank badges %03x seen %03x", tank.player_feedings, tank.tank_ms_bits, tank.tank_ms_seen);
    if (memcmp(tank.algae, e + 460, ALGAE_CELLS) || tank.trims != (int32_t)sv_u32(e, 1104) || tank.cells_cleaned != (int32_t)sv_u32(e, 1108))
        SV_FAIL("the glass / the chore counters differ");
    float bx = bubble_default;
    if (sv_f32(e, 1404) > 0) {                           /* the file's column, where this board's floor lets it stand (the fixtures are
                                                            the rectangle's: its 358 is past the end of the watch's floor) */
        static tank_t probe; tank_init(&probe, 8); tank_set_bubble_x(&probe, sv_f32(e, 1404)); bx = probe.bubble_x;
    }
    if (tank.bubble_x != bx) SV_FAIL("bubble column %.0f, want %.0f", tank.bubble_x, bx);
    int idle = e[1476] | e[1477] << 8;
    if (tank.light_tip_seen != (e[1684] != 0)) SV_FAIL("the lights-out notice's mark %d", tank.light_tip_seen);
    if (tank.light_idle_s != (idle ? idle : LIGHT_IDLE_S) || tank.light_auto != (e[1478] != 0)) SV_FAIL("light settings %d s auto %d", tank.light_idle_s, tank.light_auto);
    if (tank.sd_balance != (int32_t)sv_u32(e, 1480) || tank.sd_earned != (int32_t)sv_u32(e, 1484) ||
        tank.sd_unlocks != (sv_u32(e, 1488) & ((1u << SD_ITEM_COUNT) - 1))) SV_FAIL("sand dollars %d (earned %d), unlocks %02x", tank.sd_balance, tank.sd_earned, tank.sd_unlocks);
    if (tank.snail_grazed != (int32_t)sv_u32(e, 1612)) SV_FAIL("the snail's tally %d", (int)tank.snail_grazed);
    float castle_x = sv_f32(e, 1616);
#ifdef CONFIG_POCKET_TANK_320X240
    if (castle_x > 0) {                                  /* (this fork) where the CYD's floor lets it stand, as the bubble column:
                                                            the fixtures' 250 is past the end of its 320 px floor. The CYD's
                                                            alone: on the other boards a saved castle that moved is a failure */
        static tank_t probe; tank_init(&probe, 8); tank_decor_set(&probe, 2, castle_x, DECOR_Z_FRONT); castle_x = probe.castle_x;
    }
#endif
    if (sv_f32(e, 1616) > 0 && (tank.castle_x != castle_x || tank.castle_z != (e[1620] == DECOR_Z_BACK + 1 ? DECOR_Z_BACK : DECOR_Z_FRONT)))
        SV_FAIL("castle at %.0f depth %d", tank.castle_x, tank.castle_z);
    if (sv_u32(e, 1632) && tank.coral_rgb != (sv_u32(e, 1632) & 0xffffff)) SV_FAIL("coral color %06x", tank.coral_rgb);
    if (tank.coral_growth != fmaxf(sv_f32(e, 1636), 0) || tank.cluster_growth != fmaxf(sv_f32(e, 1648), 0)) SV_FAIL("coral / cluster growth %.2f / %.2f", tank.coral_growth, tank.cluster_growth);
    if (tank.cluster_scheme != (e[1645] < CLUSTER_SCHEME_N ? e[1645] : CLUSTER_SCHEME_N - 1)) SV_FAIL("cluster look %d", tank.cluster_scheme);
    if (tank.sd_unlocks & SD_ITEM_SHRIMP) {             /* the shrimp tail (09-29): the school, its count, its cooldown */
        float cool = sv_f32(e, 1656);
        if (tank.shrimp_n != (e[1652] >= SHRIMP_START ? e[1652] : SHRIMP_START) || tank.shrimp_food != (e[1653] <= SHRIMP_PER_JOIN ? e[1653] : SHRIMP_PER_JOIN)
            || tank.shrimp_cool != (cool > 0 && cool <= SHRIMP_COOLDOWN_S ? cool : 0) || tank.shrimp_eaten != (int32_t)sv_u32(e, 1660))
            SV_FAIL("shrimp %d count %d cooldown %.0f eaten %d", tank.shrimp_n, tank.shrimp_food, tank.shrimp_cool, (int)tank.shrimp_eaten);
    } else if (tank.shrimp_n) SV_FAIL("%d shrimp without the unlock", tank.shrimp_n);
    if (tank.sd_unlocks & SD_ITEM_URCHIN) {             /* the urchin tail (10-02): its spot (0 = by the reef bed) and its tally */
        if ((sv_f32(e, 1676) > 0 && tank.urchin_x != sv_f32(e, 1676)) || tank.urchin_x < 0
            || tank.urchin_grazed_px != fmaxf(sv_f32(e, 1680), 0)) SV_FAIL("urchin at %.0f, %.0f px grazed", tank.urchin_x, tank.urchin_grazed_px);
    } else if (tank.urchin_x >= 0 || tank.urchin_grazed_px) SV_FAIL("an urchin without the unlock (%.0f)", tank.urchin_x);
    return 0;
    #undef SV_FAIL
}

/* write len bytes as the save, boot it, check it; then save, reload, check again */
static int saves_load(const char *what, const uint8_t *bytes, size_t len, const uint8_t *e) {
    const char *sav = getenv("POCKET_TANK_SAVE");
    FILE *f = fopen(sav, "wb"); if (!f || fwrite(bytes, 1, len, f) != len) { printf("FAIL: could not write %s\n", sav); return 1; }
    fclose(f);
    tank_init(&tank, 8); float bx0 = tank.bubble_x;
    progression_wake(&tank, 0);                          /* no clock: a plain restore, nothing lived through */
    if (saves_check(what, e, bx0)) return 1;
    if (progression_loaded_release() != sv_u32(e, 1664)) { printf("FAIL: %s: the release stamp read %06x, the file says %06x\n", what, progression_loaded_release(), sv_u32(e, 1664)); return 1; }
    progression_save(&tank);
    tank_init(&tank, 9); progression_wake(&tank, 0);
    char again[300]; snprintf(again, sizeof again, "%s, saved and reloaded", what);
    if (progression_loaded_release() != PT_RELEASE_NUM) { printf("FAIL: %s: re-saved, the stamp is %06x, not this release's %06x\n", again, progression_loaded_release(), PT_RELEASE_NUM); return 1; }
    return saves_check(again, e, bx0);
}

static int selftest_saves(void) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-saves.sav", 1);   /* never touch the real save */
    if (strcmp(SAVE_NVS_NS, "tank") || strcmp(SAVE_NVS_KEY, "save")) {
        printf("FAIL: the device's save moved to %s/%s - every keeper's tank stays behind in tank/save\n", SAVE_NVS_NS, SAVE_NVS_KEY); return 1; }
    const char *dir = "testdata/saves";
    DIR *d = opendir(dir); if (!d) d = opendir(dir = "sim/testdata/saves");
    if (!d) { printf("FAIL: no testdata/saves (run from sim/ or the repo root)\n"); return 1; }
    char *names[64]; int nf = 0; struct dirent *de;
    while ((de = readdir(d)) && nf < 64) { size_t l = strlen(de->d_name); if (l > 4 && !strcmp(de->d_name + l - 4, ".sav")) names[nf++] = strdup(de->d_name); }
    closedir(d);
    qsort(names, nf, sizeof *names, name_cmp);
    if (nf == 0) { printf("FAIL: %s holds no .sav fixtures\n", dir); return 1; }
    int loads = 0;
    static uint8_t file[4096], cur[4096], e[4096], cut[4096];
    for (int k = 0; k < nf; k++) {
        char path[600]; snprintf(path, sizeof path, "%s/%s", dir, names[k]);
        FILE *f = fopen(path, "rb"); if (!f) { printf("FAIL: %s\n", path); return 1; }
        size_t len = fread(file, 1, sizeof file, f); fclose(f);
        if (len < 448 || sv_u32(file, 0) != 0x50544b32u) { printf("FAIL: %s is not a PTK2 save (%zu bytes)\n", names[k], len); return 1; }
        /* (this fork) a *-cyd.sav is a save the CYD wrote before v0.3.3, in its
           old layout (300 algae cells, the tail 344 bytes early): only the CYD
           reads it (progression.c's load_save moves the tail) */
        bool cyd_layout = strstr(names[k], "-cyd.sav") != NULL;
#ifndef CONFIG_POCKET_TANK_320X240
        if (cyd_layout) { printf("selftest-saves: %s: the CYD's old layout - the CYD's own (make 320X240=1)\n", names[k]); continue; }
#endif
        /* today's layout: a 1432-byte save is the pre-bubble one - bubble_x (4 zero bytes) goes back in at 1404 */
        memset(cur, 0, sizeof cur); size_t curlen = len;
        if (len == 1432) { memcpy(cur, file, 1404); memcpy(cur + 1408, file + 1404, 28); curlen = 1436; }
#ifdef CONFIG_POCKET_TANK_320X240
        else if (cyd_layout) { memcpy(cur, file, 760); memcpy(cur + 1104, file + 760, len - 760); curlen = len + 344; }   /* the tail back past the 644 cells */
#endif
        else memcpy(cur, file, len);
        size_t ats[sizeof SAVE_CUTS / sizeof *SAVE_CUTS + 1]; int na = 0; bool whole = false;   /* every older length it holds, then the file as it
                                                                                                    is (sized by SAVE_CUTS: a 16 overflowed when the urchin made it 17) */
        for (size_t c = 0; c < sizeof SAVE_CUTS / sizeof *SAVE_CUTS; c++)
            if (SAVE_CUTS[c] <= curlen) { ats[na++] = SAVE_CUTS[c]; whole |= SAVE_CUTS[c] == len; }
        if (!whole) ats[na++] = len;
        char cuts[400] = ""; size_t cl = 0;
        for (int c = 0; c < na; c++) {
            size_t at = ats[c];
            memset(e, 0, sizeof e);
            if (at == 1432) {                                    /* the first public installer's layout */
                memcpy(e, cur, 1436); memset(e + 1404, 0, 4);
                memcpy(cut, cur, 1404); memcpy(cut + 1404, cur + 1408, 28);
            }
#ifdef CONFIG_POCKET_TANK_320X240
            else if (cyd_layout && at == len) { memcpy(e, cur, curlen); memcpy(cut, file, len); }   /* the file as the CYD wrote it */
#endif
            else { memcpy(e, cur, at); memcpy(cut, cur, at); }
            char what[300]; snprintf(what, sizeof what, "%s cut to %zu bytes", names[k], at);
            if (saves_load(what, cut, at, e)) return 1;
            loads++;
            cl += snprintf(cuts + cl, sizeof cuts - cl, "%s%zu", cl ? " " : "", at);
        }
        char who[120] = ""; size_t wl = 0;
        for (int i = 0; i < tank.n_fish && wl < sizeof who - 12; i++) wl += snprintf(who + wl, sizeof who - wl, "%s%s", i ? " " : "", tank.fish[i].name);
        printf("selftest-saves: %s: %d fish (%s), %d sand dollars - loads at %s, and again after a save\n",
               names[k], tank.n_fish, who, (int)tank.sd_balance, cuts);
    }
    /* a rollback: a NEWER build's save (today's layout + 200 bytes of tail
     * this build never heard of) loads its head, and the next save writes
     * today's length. Before 2026-09-29 it started a fresh tank over it. */
    {
        memset(e, 0, sizeof e); memcpy(e, cur, SAVE_NOW);
        memcpy(cut, cur, SAVE_NOW); memset(cut + SAVE_NOW, 0xa5, 200);
        if (saves_load("a newer build's save, 200 bytes longer (a rollback)", cut, SAVE_NOW + 200, e)) return 1;
        FILE *f = fopen(getenv("POCKET_TANK_SAVE"), "rb"); fseek(f, 0, SEEK_END); long l = ftell(f); fclose(f);
        if (l != SAVE_NOW) { printf("FAIL: after a rollback the save is %ld bytes, want %d\n", l, SAVE_NOW); return 1; }
        loads++;
        printf("selftest-saves: a rollback: a newer build's %d-byte save loads its first %d and saves back at %d\n", SAVE_NOW + 200, SAVE_NOW, SAVE_NOW);
    }
    /* not ours: shorter than the smallest PTK2, or another magic -> a fresh tank (setup owed) */
    {
        FILE *f = fopen(getenv("POCKET_TANK_SAVE"), "wb"); fwrite(cur, 1, 447, f); fclose(f);
        tank_init(&tank, 8); progression_wake(&tank, 0);
        if (!progression_setup_pending() || tank.n_fish != 2) { printf("FAIL: a 447-byte save loaded\n"); return 1; }
        memcpy(cut, cur, SAVE_NOW); cut[0] ^= 1;
        f = fopen(getenv("POCKET_TANK_SAVE"), "wb"); fwrite(cut, 1, SAVE_NOW, f); fclose(f);
        tank_init(&tank, 8); progression_wake(&tank, 0);
        if (!progression_setup_pending() || tank.n_fish != 2) { printf("FAIL: a save with another magic loaded\n"); return 1; }
    }
    remove(getenv("POCKET_TANK_SAVE"));
    for (int k = 0; k < nf; k++) free(names[k]);
    printf("selftest-saves ok (%d fixtures, %d loads; a 447-byte save and a foreign magic start fresh; NVS %s/%s)\n", nf, loads, SAVE_NVS_NS, SAVE_NVS_KEY);
    return 0;
}
#else
static int selftest_saves(void) { printf("selftest-saves: not in the MSVC build (no dirent; its save layout asserts are off)\n"); return 1; }
#endif

/* upkeep chores + the settled-hold gate, headless: sleep grows the canopy
 * and algae; taps trim a bed to nubs (never bare); a drag wipes the glass;
 * the canopy comfort band moves stress both ways; a hold only draws fish in
 * after ~3 s, and never a starving one. */
/* --selftest-update (2026-09-30): the UPDATES page's taps, then the whole
 * wizard over the pretend radio - scan, the list, the password keyboard with
 * every mode (letters, caps, digits, symbols), JOIN, the check, the offer,
 * the download, the restart - then the saved network's short path, a wrong
 * password (the store is dropped), no signal, up to date, needs the cable,
 * the battery gate, CANCEL mid-download, NOT NOW. */
static int selftest_update(void) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-update.sav", 1);
    setenv("POCKET_TANK_WIFI", "/tmp/pocket-tank-selftest-wifi.txt", 1);
    unsetenv("POCKET_TANK_FAKE_UPDATE"); net_port_creds_forget();
    static uint16_t fb[TANK_W * TANK_H];
    tank_init(&tank, 5); progression_reset(&tank, 5);
    char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
    /* page coordinates in (update.h's layout): a tap, the element under a point, a keyboard cell's center */
    #define TAP(X, Y) (update_touch(PG_X(X), PG_Y(Y), true), update_touch(PG_X(X), PG_Y(Y), false))
    #define HIT(X, Y) update_hit(PG_X(X), PG_Y(Y))
    #define KEY(cell) TAP(UPD_KB_X + ((cell) % UPD_KB_COLS) * UPD_KB_PX + UPD_KB_W / 2, UPD_KB_Y + ((cell) / UPD_KB_COLS) * UPD_KB_PY + UPD_KB_H / 2)
    #define ROWY(r)   (UPD_ROW_Y0 + (r) * UPD_ROW_H + 16)              /* network r of the list's page */
    const int bl = UPD_BTN_L_X + UPD_BTN_W / 2, br = UPD_BTN_R_X + UPD_BTN_W / 2, bm = UPD_BTN_MID_X + UPD_BTN_W / 2;   /* the buttons: left, right, one on its own */
    const int yb = UPD_BTN_Y + UPD_BTN_H / 2, ya = yb - 50;             /* the foot's row, and a message's first pair above it */
    const int lx = PAGE_W / 4, ax = UPD_ARROW_X + 6;                    /* on a network's name; the list's page arrows */
    enum { K_MORE = 14, K_DEL, K_SPACE, K_CAPS, K_MODE, K_BACK, K_JOIN };
    #define ADV(secs) do { net_sim_advance(secs); update_tick(secs); } while (0)
    #define EXPECT(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); return 1; } } while (0)
    /* the settings page's UPDATES button, and the UPDATES page's taps */
    { int v = 0, r;
      render_settings_touch(&tank, PG_X(SET_UPD_X + 40), PG_Y(SET_FOOT_Y + 12), true, &v); r = render_settings_touch(&tank, PG_X(SET_UPD_X + 40), PG_Y(SET_FOOT_Y + 12), false, &v);
#ifdef CONFIG_POCKET_TANK_320X240 /* (this fork) a 320x240 board has no update channel, so no UPDATES button: its spot is the version line */
      EXPECT(r != SET_TAP_UPDATES, "settings: the CYD has an UPDATES button (%d)", r);
#else
      EXPECT(r == SET_TAP_UPDATES, "settings: the UPDATES button -> %d", r);
#endif
      render_settings_touch(&tank, PG_X(SETP_SEG_X(0)), PG_Y(SET_ROW5_Y + 10), true, &v); r = render_settings_touch(&tank, PG_X(SETP_SEG_X(0)), PG_Y(SET_ROW5_Y + 10), false, &v);
      EXPECT(r != SET_TAP_UPDATES && r != SET_TAP_CLOSE, "settings: the last row read as a foot button (%d)", r);
      render_settings_touch(&tank, PG_X(SETP_SEG_X(0)), PG_Y(SET_ROW5_Y + 10), true, &v); render_settings_touch(&tank, PG_X(SETP_SEG_X(0)), PG_Y(SET_ROW5_Y + 10), false, &v);   /* (and back) */
      EXPECT(updates_page_tap(PG_X(UPD_CLOSE_X + 40), PG_Y(UPD_CLOSE_Y + 12)) == UPD_TAP_CLOSE, "updates page: CLOSE");
      EXPECT(updates_page_tap(PG_X(UPD_CHECK_X + UPD_CHECK_W / 2), PG_Y(UPD_CHECK_Y + 20)) == UPD_TAP_CHECK, "updates page: CHECK");
      const float fx = PG_X(UPD_FORGET_X + UPD_FORGET_W / 2), fy = PG_Y(UPD_FORGET_Y + 16);
      EXPECT(updates_page_tap(fx, fy) == UPD_TAP_NONE, "updates page: FORGET with no network");
      net_port_creds_set("Fishbowl", "abc");
      EXPECT(updates_page_tap(fx, fy) == UPD_TAP_FORGET, "updates page: FORGET with a network");
      updates_page_touch(fx, fy, true); r = updates_page_touch(fx, fy, false);
      EXPECT(r == UPD_TAP_FORGET && !net_port_creds_get(ssid, pass), "updates page: FORGET did not drop the store");
      render_updates_page(fb, TANK_W); }
    /* the wizard, end to end */
    update_begin(50, false);
    EXPECT(update_page() == UPD_PG_BUSY, "no network: not scanning (page %d)", update_page());
    render_update(fb, TANK_W, 0.1f);
    ADV(0.5f); EXPECT(update_page() == UPD_PG_BUSY, "the scan finished too early");
    ADV(1.0f); EXPECT(update_page() == UPD_PG_SCAN, "no list after the scan (page %d)", update_page());
    render_update(fb, TANK_W, 0.1f);
    EXPECT(HIT(lx, ROWY(0)) == 200 && HIT(lx, ROWY(5)) == 205, "list rows: %d / %d", HIT(lx, ROWY(0)), HIT(lx, ROWY(5)));
    EXPECT(HIT(ax, UPD_ROW_Y0 + 12) == 9, "no page-up arrow on a two-page list (%d)", HIT(ax, UPD_ROW_Y0 + 12));
    TAP(ax, UPD_ROW_Y0 + UPD_ROWS_PER * UPD_ROW_H - 20);              /* page down */
    EXPECT(HIT(lx, ROWY(0)) == 206, "page 2's first row -> %d", HIT(lx, ROWY(0)));
    TAP(ax, UPD_ROW_Y0 + 12);                                         /* page up */
    TAP(lx, ROWY(1));                                                 /* "Strato's Wi-Fi", secured */
    EXPECT(update_page() == UPD_PG_PASSWORD, "no keyboard after a secured network (page %d)", update_page());
    /* type h u n t e r 2 ! (caps: H): letters, CAPS, MORE, 123, symbols */
    KEY(K_CAPS); KEY(7);                                              /* CAPS, h -> H */
    KEY(K_CAPS);                                                      /* CAPS off */
    KEY(K_MORE); KEY(7);                                              /* MORE -> n-z page; u = index 7 */
    KEY(0);                                                           /* n */
    KEY(6);                                                           /* t */
    KEY(K_MORE);                                                      /* MORE -> a-m */
    KEY(4);                                                           /* e */
    KEY(K_MORE); KEY(4);                                              /* r */
    KEY(K_MODE); KEY(2);                                              /* 123, "2" */
    KEY(K_MORE); KEY(0);                                              /* symbols page 1: "!" */
    KEY(K_SPACE);                                                      /* SPACE */
    KEY(K_DEL);                                                      /* DEL */
    EXPECT(!strcmp(update_typed(), "Hunter2!"), "typed '%s', wanted 'Hunter2!'", update_typed());
    KEY(K_MORE); KEY(0); KEY(K_MORE); KEY(0); KEY(K_DEL); KEY(K_DEL);   /* symbols page 2 '\"', page 0 '0', deleted */
    EXPECT(!strcmp(update_typed(), "Hunter2!"), "after the symbol pages: '%s'", update_typed());
    render_update(fb, TANK_W, 0.1f);
    KEY(K_JOIN);                                                      /* JOIN */
    EXPECT(update_page() == UPD_PG_BUSY, "JOIN did not connect (page %d)", update_page());
    ADV(2); EXPECT(update_page() == UPD_PG_BUSY && net_port_creds_get(ssid, pass) && !strcmp(ssid, "Strato's Wi-Fi") && !strcmp(pass, "Hunter2!"),
                   "connected: the network was not saved (%s / %s)", ssid, pass);
    ADV(2); EXPECT(update_page() == UPD_PG_OFFER, "no offer after the check (page %d)", update_page());
    render_update(fb, TANK_W, 0.1f);
    TAP(bl, yb);                                                      /* UPDATE */
    EXPECT(update_page() == UPD_PG_BUSY, "UPDATE did not start the download");
    ADV(3); render_update(fb, TANK_W, 0.1f);
    ADV(4); EXPECT(update_outcome() == UPD_RUNNING, "left before the restart page");
    ADV(2); EXPECT(update_outcome() == UPD_RESTART, "no restart after the install (outcome %d)", update_outcome());
    net_port_off();
    /* the saved network: straight to CONNECTING, then NOT NOW */
    update_begin(50, false);
    EXPECT(update_page() == UPD_PG_BUSY, "saved network: not connecting");
    ADV(2); ADV(2); EXPECT(update_page() == UPD_PG_OFFER, "saved network: no offer");
    TAP(br, yb);                                                      /* NOT NOW */
    EXPECT(update_outcome() == UPD_BACK, "NOT NOW did not leave");
    /* CANCEL mid-download: a message, nothing changed */
    update_begin(50, false); ADV(2); ADV(2); TAP(bl, yb); ADV(2);
    TAP(bm, yb);                                                      /* CANCEL */
    EXPECT(update_page() == UPD_PG_MESSAGE && update_outcome() == UPD_RUNNING, "CANCEL: no message page");
    TAP(bm, yb);                                                      /* BACK TO TANK */
    EXPECT(update_outcome() == UPD_BACK, "CANCEL's BACK TO TANK");
    /* a wrong password drops the store and offers the keyboard */
    net_port_creds_set("Strato's Wi-Fi", "wrong-now"); update_begin(50, false); ADV(2);
    EXPECT(update_page() == UPD_PG_MESSAGE && net_port_creds_get(ssid, pass), "wrong password: the store must stay (routers refuse first tries)");
    render_update(fb, TANK_W, 0.1f);
    TAP(bl, ya);                                                      /* TRY AGAIN: the same network once more */
    EXPECT(update_page() == UPD_PG_BUSY, "TRY AGAIN: no connect");
    ADV(2); EXPECT(update_page() == UPD_PG_MESSAGE, "TRY AGAIN with the wrong password: no message");
    EXPECT(HIT(bl, yb) == 8 && HIT(br, yb) == 1, "wrong password: OTHER NETWORK / BACK on row B (%d / %d)", HIT(bl, yb), HIT(br, yb));
    TAP(br, ya);                                                      /* TYPE IT AGAIN: the text is KEPT (a long passphrase, one wrong letter) */
    EXPECT(update_page() == UPD_PG_PASSWORD && !strcmp(update_typed(), "wrong-now"), "TYPE IT AGAIN: the text was lost ('%s')", update_typed());
    KEY(K_BACK); ADV(2);                                              /* a stray BACK: the list; the same network keeps the text */
    TAP(lx, ROWY(1));
    EXPECT(update_page() == UPD_PG_PASSWORD && !strcmp(update_typed(), "wrong-now"), "BACK then the same network: the text was lost ('%s')", update_typed());
    KEY(K_BACK); TAP(lx, ROWY(0));                                    /* another network: fresh */
    EXPECT(update_page() == UPD_PG_PASSWORD && !update_typed()[0], "another network: the old text stayed ('%s')", update_typed());
    KEY(K_BACK);                                                      /* the list again (no scan: it is known) */
    EXPECT(update_page() == UPD_PG_SCAN, "BACK with a known list: no list (page %d)", update_page());
    KEY(K_BACK);                                                      /* BACK -> no list yet (the saved network's path): a scan */
    EXPECT(update_page() == UPD_PG_BUSY, "keyboard BACK without a list: no scan");
    ADV(2); EXPECT(update_page() == UPD_PG_SCAN, "keyboard BACK: no list");
    TAP(lx, ROWY(2));                                                 /* "CoffeeShop Guest": open, connects at once */
    EXPECT(update_page() == UPD_PG_BUSY, "open network: no connect");
    ADV(2); ADV(2); EXPECT(update_page() == UPD_PG_OFFER && net_port_creds_get(ssid, pass) && !pass[0], "open network: not saved without a password");
    TAP(br, yb);
    /* no signal, up to date, needs the cable, the download stopping */
    net_port_creds_set("Ghost", "x"); update_begin(50, false); ADV(2);
    EXPECT(update_page() == UPD_PG_MESSAGE && net_port_creds_get(ssid, pass), "no signal: the store must stay");
    TAP(br, ya);                                                      /* OTHER NETWORK */
    EXPECT(update_page() == UPD_PG_BUSY, "OTHER NETWORK: no scan");
    TAP(bm, yb);                                                      /* CANCEL the scan = back */
    EXPECT(update_outcome() == UPD_BACK, "CANCEL on the scan");
    net_port_creds_set("Fishbowl", "abcdefgh");
    setenv("POCKET_TANK_FAKE_UPDATE", "none", 1); update_begin(50, false); ADV(2); ADV(2);
    EXPECT(update_page() == UPD_PG_MESSAGE, "up to date: no message");
    ADV(UPD_UP_TO_DATE_S + 1); EXPECT(update_outcome() == UPD_BACK, "up to date: did not go back by itself");
    setenv("POCKET_TANK_FAKE_UPDATE", "cable", 1); update_begin(50, false); ADV(2); ADV(2);
    EXPECT(update_page() == UPD_PG_MESSAGE && HIT(bl, ya) == 0 && HIT(bm, ya) == 0, "needs the cable: a TRY AGAIN showed");
    setenv("POCKET_TANK_FAKE_UPDATE", "downloadfail", 1); update_begin(50, false); ADV(2); ADV(2); TAP(bl, yb); ADV(3);
    EXPECT(update_page() == UPD_PG_MESSAGE, "download stopped: no message");
    EXPECT(HIT(bm, ya) != 0, "a lone TRY AGAIN is not centred over BACK TO TANK");   /* (2026-10-03) */
    TAP(bm, ya);                                                      /* TRY AGAIN */
    EXPECT(update_page() == UPD_PG_BUSY, "TRY AGAIN: no download");
    setenv("POCKET_TANK_FAKE_UPDATE", "fail", 1); update_begin(50, false); ADV(2); ADV(2);
    EXPECT(update_page() == UPD_PG_MESSAGE, "server down: no message");
    /* three boards, one release (2026-10-02): a manifest for another board is never offered, and an
       image whose board marker is another board's stops at its first sector - no TRY AGAIN either way */
    setenv("POCKET_TANK_FAKE_UPDATE", "otherboard", 1); update_begin(50, false); ADV(2); ADV(2);
    EXPECT(update_page() == UPD_PG_MESSAGE && !strcmp(update_message_title(), "WRONG BOARD") && HIT(bl, ya) == 0 && HIT(bm, ya) == 0,
           "another board's manifest: page %d, %s", update_page(), update_message_title());
    setenv("POCKET_TANK_FAKE_UPDATE", "boardimage", 1); update_begin(50, false); ADV(2); ADV(2);
    EXPECT(update_page() == UPD_PG_OFFER, "a right-board manifest was not offered (page %d)", update_page());
    TAP(bl, yb); ADV(1); ADV(1);
    EXPECT(update_page() == UPD_PG_MESSAGE && !strcmp(update_message_title(), "WRONG BOARD") && HIT(bl, ya) == 0 && HIT(bm, ya) == 0,
           "another board's image: page %d, %s", update_page(), update_message_title());
    printf("selftest-update: board %s: another board's manifest -> WRONG BOARD, never offered; another board's image -> WRONG BOARD at its first sector\n", PT_BOARD);
    unsetenv("POCKET_TANK_FAKE_UPDATE");
    /* the battery gate */
    update_begin(12, false); EXPECT(update_page() == UPD_PG_POWER, "12%% on battery: no PLUG IN page");
    TAP(bm, yb); EXPECT(update_outcome() == UPD_BACK, "PLUG IN's BACK");   /* (a button on its own: centered, 2026-10-01) */
    update_begin(12, true); EXPECT(update_page() == UPD_PG_BUSY, "12%% on the cable: refused");
    update_begin(-1, false); EXPECT(update_page() == UPD_PG_BUSY, "no gauge: refused");
    net_port_off(); net_port_creds_forget();
    /* the case-sensitive font: every printable character has a glyph, widths match the tank's font */
    { char all[96]; for (int i = 0; i < 95; i++) all[i] = (char)(32 + i); all[95] = 0;
      update_text(fb, TANK_W, 0, 0, 1, 0xffffff, all);
      EXPECT(update_text_w("abc", 2) == render_text_w("ABC", 2), "font widths differ"); }
    #undef TAP
    #undef HIT
    #undef KEY
    #undef ROWY
    #undef ADV
    #undef EXPECT
    printf("selftest-update: OK\n");
    return 0;
}
static int selftest_tend(void) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest.sav", 1);
    char cmd[600]; snprintf(cmd, sizeof cmd, "rm -f /tmp/pocket-tank-selftest.sav"); (void)system(cmd);
    tank_init(&tank, 777);
    progression_boot(&tank);
    int film0 = 0;
    for (int i = 0; i < ALGAE_CELLS; i++) film0 += tank.algae[i] > 0;
    if (film0) { printf("FAIL: fresh glass not clean (%d cells)\n", film0); return 1; }
    float g0 = tank.veg_growth[1];
    /* a night of drowse lets the garden get away */
    tank_tick_sleep(&tank, 7 * 3600);
    int film = 0;
    for (int i = 0; i < ALGAE_CELLS; i++) film += tank.algae[i] > 0;
    printf("selftest-tend: after 7 h sleep, canopy %.2f -> %.2f, %d algae cells\n",
           g0, tank.veg_growth[1], film);
    if (tank.veg_growth[1] <= g0 + 0.2f) { printf("FAIL: canopy barely grew in sleep\n"); return 1; }
    if (tank.veg_growth[1] > 0.80f) { printf("FAIL: one night should not put the bed near the surface (2026-09-23: it did, every time)\n"); return 1; }
    if (film < 10) { printf("FAIL: algae did not film the glass\n"); return 1; }
    if (film > (int)(ALGAE_CELLS * 0.30f) - 20) {           /* tank.c ALGAE_COVER_CAP */ printf("FAIL: one night filmed the glass to the cap\n"); return 1; }
    /* the ceilings (2026-09-23): however long the tank sleeps, every frond
     * stops at its own height in VEG_CAP_LO..VEG_CAP_HI and the skyline
     * stays ragged - a grown bed is not a wall at the surface */
    {
        tank_t grown; tank_init(&grown, 777); progression_boot(&grown);
        tank_tick_sleep(&grown, 4 * 3600);
        float nap = grown.veg_growth[1];
        tank_tick_sleep(&grown, 96 * 3600);
        float lo = 1, hi = 0;
        for (int i = 0; i < VEG_FRONDS_MAX; i++) {
            float h = grown.veg_h[1][i], cap = tank_veg_cap(1, i), fx;
            tank_veg_frond(&grown, 1, i, &fx);
            if (h > cap + 1e-4f || cap < veg_cap_floor(fx) - 1e-4f || cap > VEG_CAP_HI + 1e-4f) { printf("FAIL: frond %d at %.3f past its ceiling %.3f\n", i, h, cap); return 1; }
            if (h < cap - 1e-3f) { printf("FAIL: frond %d never reached its ceiling (%.3f < %.3f) in 100 h\n", i, h, cap); return 1; }
            if (h < lo) lo = h; if (h > hi) hi = h;
        }
        printf("selftest-tend: a 4 h nap: bed 1 %.2f -> %.2f; 100 h asleep: fronds %.2f..%.2f (mean %.2f), never the surface\n", g0, nap, lo, hi, grown.veg_growth[1]);
        if (nap > 0.60f) { printf("FAIL: a 4 h nap overgrew the bed\n"); return 1; }
        if (hi - lo < 0.10f) { printf("FAIL: the grown skyline is flat\n"); return 1; }
        if (grown.veg_growth[1] >= VEG_CAP_HI) { printf("FAIL: the grown bed reads as full\n"); return 1; }
        /* a staged frond above its ceiling is left alone, never pulled down */
        grown.veg_h[1][0] = 1.0f; tank_veg_sync(&grown);
        tank_tick_sleep(&grown, 3600);
        if (grown.veg_h[1][0] < 1.0f - 1e-4f) { printf("FAIL: growth pulled a staged frond down\n"); return 1; }
        /* the skyline is ragged, not a slope (2026-10-05: the ceilings came
           from a plain multiply and stepped down frond by frond - every
           grown bed a ramp): neighbors' ceilings turn up and down */
        for (int b = 0; b < VEG_BEDS; b++) {
            float x0, x1; int n, turns = 0;
            tank_veg_bed(&grown, b, &x0, &x1, NULL, &n);
            for (int i = 2; i < n; i++) {
                float d0 = tank_veg_cap(b, i - 1) - tank_veg_cap(b, i - 2), d1 = tank_veg_cap(b, i) - tank_veg_cap(b, i - 1);
                turns += (d0 > 0) != (d1 > 0);
            }
            if (turns < n / 4) { printf("FAIL: bed %d's ceilings are a slope (%d turns in %d fronds)\n", b, turns, n); return 1; }
        }
        /* a bed cut flat comes back ragged: every frond its own pace
           (VEG_PACE_SPREAD) - it never regrows as a hedge */
        {
            tank_t cut; tank_init(&cut, 777); progression_boot(&cut);
#ifdef CONFIG_POCKET_TANK_320X240
            const int cb = 2;                              /* (this fork) the CYD's floor leaves bed 1 three fronds, too few
                                                              to show a spread (0.55..0.57): bed 2's eight */
#else
            const int cb = 1;
#endif
            tank_veg_set(&cut, cb, 0.20f);
            tank_tick_sleep(&cut, 8 * 3600);
            float clo = 1, chi = 0, x0, x1; int n;
            tank_veg_bed(&cut, cb, &x0, &x1, NULL, &n);
            for (int i = 0; i < n; i++) {                  /* still growing: the bowl's glass stops the outer ones early */
                float h = cut.veg_h[cb][i];
                if (h >= tank_veg_cap(cb, i) - 1e-3f) continue;
                if (h < clo) clo = h; if (h > chi) chi = h;
            }
            printf("selftest-tend: bed %d cut flat at 0.20, 8 h asleep: fronds %.2f..%.2f\n", cb, clo, chi);
            if (chi - clo < 0.03f) { printf("FAIL: a flat cut regrew flat\n"); return 1; }
            if (chi - clo > 0.25f) { printf("FAIL: a flat cut regrew spiky\n"); return 1; }
        }
        /* and the untended grown tank still reads as smothered: two beds at
           their ceilings (the trim-it-back signal, relative to the ceilings now) */
        fish_t *gf = &grown.fish[0]; gf->stress = 0;
        for (int i = 0; i < 60 * 60; i++) tank_tick(&grown, 1.0f / 60.0f, advisor_rules);
        printf("selftest-tend: every bed at its ceiling for an hour: stress %.1f\n", gf->stress);
        if (gf->stress < 1.5f) { printf("FAIL: a tank grown to its ceilings should smother\n"); return 1; }
    }
    /* days asleep bank no film (2026-09-29): the sleep cap's overflow used to
       stay in the accumulator and the awake tick paid it out a step per frame,
       the glass refilling under the keeper's wipe */
    {
        tank_t away; tank_init(&away, 777); progression_boot(&away);
        tank_tick_sleep(&away, 4 * 24 * 3600);
        for (int i = 0; i < ALGAE_CELLS; i++) away.algae[i] = 0;          /* the keeper wipes it all */
        for (int i = 0; i < 30 * 60; i++) tank_tick(&away, 1.0f / 60.0f, advisor_rules);
        int back = 0;
        for (int i = 0; i < ALGAE_CELLS; i++) back += away.algae[i] > 0;
        printf("selftest-tend: 4 days asleep, glass wiped, 30 s awake: %d cells back\n", back);
        if (back) { printf("FAIL: film refilled behind the wipe - a banked backlog\n"); return 1; }
    }
    /* the device drowses in 30 s slices (firmware DROWSE_TICK_US): the same
     * night delivered that way must film the glass just as much. Before
     * 2026-09-04 every slice truncated to 0 film steps and the glass stayed
     * clean forever on the hardware. */
    {
        tank_t sliced; tank_init(&sliced, 777); progression_boot(&sliced);
        for (int i = 0; i < 7 * 120; i++) tank_tick_sleep(&sliced, 30.0f);
        int f2 = 0;
        for (int i = 0; i < ALGAE_CELLS; i++) f2 += sliced.algae[i] > 0;
        printf("selftest-tend: the same night in 30 s drowse slices: %d algae cells\n", f2);
        if (f2 < film / 2) { printf("FAIL: drowse slices do not grow algae\n"); return 1; }
    }
    /* height is growth: a full bed's tallest frond touches the ceiling */
    {
        float bx0, bx1, ty; int ms;
        for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(&tank, b, 1.0f);
        tank_veg_bed(&tank, 2, &bx0, &bx1, &ty, NULL);
        ms = tank_veg_frond(&tank, 2, 0, NULL);
        float glass = fmaxf(tank_glass_top(bx0), tank_glass_top(bx1));   /* the ceiling over the bed: y 0 in the rectangle; the bowl's
                                                                            dome comes down over its outer frond (its crown stands
                                                                            higher than a full frond: the bowl kept the 3.2 px pitch) */
        printf("selftest-tend: full bed 2: %d segments, canopy top y %.0f\n", ms, ty);
        if (ty - glass > 24) { printf("FAIL: a full bed should reach the ceiling (top %.0f, the glass over it %.0f)\n", ty, glass); return 1; }
        tank_tick_sleep(&tank, 7 * 3600);          /* restore the grown state for the trims */
    }
    /* a TAP in the canopy must NOT trim (that was the accidental-cut bug:
     * missed pokes at a fish were shearing the garden) */
    {
        float x0, x1, ty;
        tank_veg_bed(&tank, 1, &x0, &x1, &ty, NULL);
        float g_before = tank.veg_growth[1];
        tank_touch_tap(&tank, (x0 + x1) * 0.5f, (ty + TANK_BOT) * 0.5f);
        if (tank.veg_growth[1] != g_before) { printf("FAIL: a tap trimmed the canopy\n"); return 1; }
        tank.tap_count = 0; tank.tap_burst_t = 99;   /* don't leak into later gestures */
    }
    /* an algae scrub that starts mid-glass and sweeps across the whole floor
     * must NOT cut anything (the accidental mow-the-garden bug): a slash only
     * arms the bed the stroke STARTED on */
    {
        float g0[VEG_BEDS]; memcpy(g0, tank.veg_growth, sizeof g0);
        const float sy = TANK_BOT - 30.0f;               /* one full-width stroke, glass to glass */
#ifdef CONFIG_POCKET_TANK_320X240
        /* (this fork) right to left: the CYD's reef bed stands 18 px from the left glass, so a stroke begun there starts
           beside its first frond (SLASH_START_SIDE_PX) - aimed at the bed, not the mid-glass start this is about */
        for (float sx = tank_glass_x1(sy) - 2; sx > tank_glass_x0(sy) + 2; sx -= 6)
#else
        for (float sx = tank_glass_x0(sy) + 2; sx < tank_glass_x1(sy) - 2; sx += 6)
#endif
            tank_touch_drag(&tank, sx, sy);
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        for (int b = 0; b < VEG_BEDS; b++)
            if (tank.veg_growth[b] < g0[b]) {
                printf("FAIL: a mid-glass scrub cut bed %d\n", b); return 1;
            }
    }
    /* trimming (2026-09-04, per frond): a sideways stroke that starts on a
     * bed cuts exactly the fronds it crosses, at the height it crosses them.
     * A cleaning scrub that starts over a bed but not beside a frond (bed 1:
     * frond 0 at the ceiling, the rest short; the finger lands mid-glass in
     * the short fronds' column, above their tips) and zigzags down to the
     * floor must cut NOTHING (2026-09-04: the old bed-box arming sheared the
     * garden whenever one frond was tall) */
    {
        tank_veg_set(&tank, 1, 0.3f); tank.veg_h[1][0] = 1.0f; tank_veg_sync(&tank);
        float before[VEG_FRONDS_MAX]; memcpy(before, tank.veg_h[1], sizeof before);
        float fx2; tank_veg_frond(&tank, 1, 2, &fx2);
#ifdef CONFIG_POCKET_TANK_320X240
        const float scrub_y0 = 102;                     /* (this fork) the CYD's sand line is y 224, so the short fronds' tips stand at
                                                           ~122 and the 1.8's 150 is in among them: 20 px over the tips instead, on the
                                                           zigzag's right-hand step as there (its left one is beside the tall frond 0) */
#else
        const float scrub_y0 = 150;
#endif
        for (float y = scrub_y0; y <= TANK_BOT - 10; y += 6)
            tank_touch_drag(&tank, fx2 + ((int)(y / 6) & 1 ? 14 : -14), y);
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules); tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        int n; tank_veg_bed(&tank, 1, NULL, NULL, NULL, &n);
        for (int i = 0; i < n; i++)
            if (tank.veg_h[1][i] < before[i] - 0.001f) { printf("FAIL: scrub from mid-glass cut frond %d (%.2f -> %.2f)\n", i, before[i], tank.veg_h[1][i]); return 1; }
        printf("selftest-tend: a scrub begun mid-glass over the bed cut nothing\n");
        tank_veg_set(&tank, 1, 1.0f);
    }
    /* First the precision: a short flick across fronds 1 and 2 of bed 1 at
     * mid-height takes those two to that height and touches nothing else */
    {
        float fx1, fx2; tank_veg_frond(&tank, 1, 1, &fx1); tank_veg_frond(&tank, 1, 2, &fx2);
        float before[VEG_FRONDS_MAX]; memcpy(before, tank.veg_h[1], sizeof before);
        float sy = 200;                            /* lands and lifts a third of a pitch off the two spines:
                                                      the pad's reach (SLASH_REACH_PX) must not take 0 or 3 */
        for (float sx = fx1 - 3; sx <= fx2 + 3; sx += 2) tank_touch_drag(&tank, sx, sy);
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules); tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        float want = veg_height_at(sy);
        int n; tank_veg_bed(&tank, 1, NULL, NULL, NULL, &n);
        printf("selftest-tend: flick at y %.0f: bed 1 fronds 1,2 %.2f/%.2f -> %.2f/%.2f (want %.2f); frond 0 %.2f -> %.2f\n",
               sy, before[1], before[2], tank.veg_h[1][1], tank.veg_h[1][2], want, before[0], tank.veg_h[1][0]);
        for (int i = 0; i < n; i++) {
            bool cut = i == 1 || i == 2;
            float h = tank.veg_h[1][i];
            if (cut && fabsf(h - want) > 0.02f) { printf("FAIL: frond %d not cut to the finger's height (%.2f vs %.2f)\n", i, h, want); return 1; }
            if (!cut && h < before[i]) { printf("FAIL: frond %d cut by a flick that never crossed it\n", i); return 1; }
        }
    }
    /* The outer blade by the glass (Strato, 2026-09-14: "the last blade in the
     * row never trips"): bed 1's last frond stands 24 px from the glass and
     * the finger's reported centre stops short of its spine. A sweep along
     * the floor that ends 6 px before that spine must still take it... */
    {
        int n; float x0; tank_veg_bed(&tank, 1, &x0, NULL, NULL, &n);
        float fl; tank_veg_frond(&tank, 1, n - 1, &fl);
        tank_veg_set(&tank, 1, 1.0f);
        for (float sx = x0 + 2; sx <= fl - 6; sx += 4) tank_touch_drag(&tank, sx, TANK_BOT - 8.0f);
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules); tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        printf("selftest-tend: sweep stopping 6 px short of bed 1's outer frond (spine %.0f, glass %d): %.2f\n", fl, TANK_FX1, tank.veg_h[1][n - 1]);
        if (tank.veg_h[1][n - 1] > VEG_NUB + 1e-3f) { printf("FAIL: the outer frond stood after a sweep that stopped just short of it\n"); return 1; }
        /* ...and a stroke that BEGINS between that frond and the glass, 14 px
         * out, heading in, arms and takes it (it used to arm nothing) */
        tank_veg_set(&tank, 1, 1.0f);
        for (float sx = fl + 14; sx >= x0; sx -= 4) tank_touch_drag(&tank, sx, TANK_BOT - 8.0f);
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules); tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        if (fabsf(tank.veg_growth[1] - VEG_NUB) > 1e-3f) { printf("FAIL: a sweep begun at the glass left bed 1 at %.2f\n", tank.veg_growth[1]); return 1; }
        printf("selftest-tend: a sweep begun 14 px outside the outer frond mowed the bed\n");
        tank_veg_set(&tank, 1, 1.0f);
    }
    /* The WALL frond left standing alone (Strato, 2026-10-01: its neighbors
     * mown, 73 tries at it): a finger cannot land on a frond that close to
     * the glass - the pad meets the bezel, the strokes start 30..100 px
     * inside and lift 20..45 px short of the wall. A stroke that starts on
     * no frond but on the wall frond's open side, heading for the wall, takes
     * it at the finger's height even though it lifts short of the spine;
     * the same stroke heading AWAY cuts nothing; and a stroke that starts ON
     * a tall frond keeps the precise reach (no running on into the wall). */
    {
        int n; tank_veg_bed(&tank, 1, NULL, NULL, NULL, &n);
        float fl; tank_veg_frond(&tank, 1, n - 1, &fl);
        float want = veg_height_at(GRASS_Y(200.0f));
        tank_veg_set(&tank, 1, 0.11f); tank.veg_h[1][n - 1] = 0.90f; tank_veg_sync(&tank);
        for (float sx = fl - 30; sx >= fl - 70; sx -= 4) tank_touch_drag(&tank, sx, GRASS_Y(200.0f));     /* away from the wall */
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules); tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        if (tank.veg_h[1][n - 1] < 0.90f) { printf("FAIL: a stroke heading away from the wall cut the wall frond\n"); return 1; }
        for (float sx = fl - 70; sx <= fl - 19; sx += 4) tank_touch_drag(&tank, sx, GRASS_Y(200.0f));     /* to the wall, lifting 19+ px short of the spine */
#ifdef CONFIG_POCKET_TANK_320X240
        /* (this fork) the CYD's outer frond stands 27 px from the glass, not 24, so those 4 px steps lift 22 px short of it -
           49 px from the glass, past the 20..45 px short of the wall a real stroke lifts (above): a last step at 45 */
        tank_touch_drag(&tank, TANK_FX1 - 45, GRASS_Y(200.0f));
#endif
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules); tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        printf("selftest-tend: the lone wall frond (spine %.0f), a stroke from open water lifting 19 px short: 0.90 -> %.2f (want %.2f)\n", fl, tank.veg_h[1][n - 1], want);
        if (fabsf(tank.veg_h[1][n - 1] - want) > 0.02f) { printf("FAIL: the lone wall frond stood\n"); return 1; }
        tank_veg_set(&tank, 1, 1.0f);
#ifdef CONFIG_POCKET_TANK_320X240
        const float on_bed = fl - 35;                     /* (this fork) the CYD's bed 1 is three fronds, so fl - 48 is in bed 2: 11 px
                                                             left of bed 1's first frond, and still the 16 px a stroke needs to cut */
#else
        const float on_bed = fl - 48;
#endif
        for (float sx = on_bed; sx <= fl - 19; sx += 4) tank_touch_drag(&tank, sx, GRASS_Y(200.0f));     /* begun ON the bed's fronds */
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules); tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        if (tank.veg_h[1][n - 1] < 1.0f) { printf("FAIL: a stroke begun on a frond ran on into the wall\n"); return 1; }
        tank_veg_set(&tank, 1, 1.0f);
    }
    /* the TOOLBOX (2026-10-01): the SPONGE - the floor sweep that mows a bed
     * only wipes the glass; the SCISSORS - a stroke begun in open water, on
     * no frond, cuts every frond it crosses at its height and wipes nothing;
     * a tool left alone goes back in the box; the box's tap test */
    {
        float x0, x1, ty; tank_veg_bed(&tank, 1, &x0, &x1, &ty, NULL);
        for (int i = 0; i < ALGAE_CELLS; i++) tank.algae[i] = 200;   /* a fouled glass, every cell */
        int32_t cleaned = tank.cells_cleaned;
        tank_set_tool(&tank, TOOL_SPONGE);
        for (float sx = x0 + 2; sx <= x1; sx += 4) tank_touch_drag(&tank, sx, TANK_BOT - 8.0f);
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules); tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        if (tank.veg_growth[1] < 0.999f) { printf("FAIL: the sponge cut bed 1 (%.2f)\n", tank.veg_growth[1]); return 1; }
        if (tank.cells_cleaned == cleaned) { printf("FAIL: the sponge wiped nothing\n"); return 1; }
        printf("selftest-tend: the SPONGE's floor sweep wiped %d cells and cut nothing\n", (int)(tank.cells_cleaned - cleaned));
        tank_set_tool(&tank, TOOL_SCISSORS);
        for (int i = 0; i < ALGAE_CELLS; i++) tank.algae[i] = 200;
        uint8_t film[ALGAE_CELLS]; memcpy(film, tank.algae, sizeof film);
        float want = veg_height_at(GRASS_Y(250.0f));
        for (float sx = x1 + 60; sx >= x0 - 40; sx -= 4) tank_touch_drag(&tank, sx, GRASS_Y(250.0f));   /* begun 60 px out in open water */
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules); tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        int n; tank_veg_bed(&tank, 1, NULL, NULL, NULL, &n);
        for (int i = 0; i < n; i++)
            if (fabsf(tank.veg_h[1][i] - want) > 0.02f) { printf("FAIL: the scissors left frond %d at %.2f (want %.2f)\n", i, tank.veg_h[1][i], want); return 1; }
        for (int i = 0; i < ALGAE_CELLS; i++)
            if (tank.algae[i] < film[i]) { printf("FAIL: the scissors wiped the glass\n"); return 1; }
        printf("selftest-tend: the SCISSORS, begun in open water, cut all %d fronds to %.2f and wiped nothing\n", n, want);
        /* a tool in hand owns the glass (2026-10-04): a tap feeds nothing, two taps leave the light, a hold draws no fish */
        { int food0 = 0, food1 = 0; for (int i = 0; i < MAX_FOOD; i++) food0 += tank.food[i].alive;
          bool light0 = tank.light_manual_off;
          tank_touch_tap(&tank, 220, tank_glass_top(220) + 6);                       /* the surface: would feed */
          tank_touch_tap(&tank, 200, 200); tank_touch_tap(&tank, 200, 200);         /* would toggle the light */
          tank_touch_hold(&tank, 200, 200);
          bool held = tank.hold_active;
          for (int i = 0; i < MAX_FOOD; i++) food1 += tank.food[i].alive;       /* before the ticks: the tank drops food of its own */
          for (int i = 0; i < 90; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
          if (food1 > food0 || tank.light_manual_off != light0 || held || tank.tap_count) {
              printf("FAIL: with the scissors in hand a tap still reached the tank (food %d -> %d, light %d -> %d, hold %d, taps %d)\n",
                     food0, food1, light0, tank.light_manual_off, held, tank.tap_count); return 1; }
          printf("selftest-tend: a tool in hand - a surface tap, a double tap and a hold did nothing\n"); }
        for (int i = 0; i < (int)(TOOL_IDLE_S * 60) + 120; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        if (tank.tool != TOOL_HAND) { printf("FAIL: the scissors stayed in hand after %.0f s untouched\n", TOOL_IDLE_S); return 1; }
        const int by = RENDER_TOOLS_Y + RENDER_TOOLS_H / 2;
        if (render_tools_hit(RENDER_TOOLS_X + 20, by) != TOOL_SPONGE || render_tools_hit(RENDER_TOOLS_X + RENDER_TOOLS_W - 20, by) != TOOL_SCISSORS ||
            render_tools_hit(RENDER_TOOLS_X + 20, RENDER_CARD_Y + RENDER_CARD_H - 4) >= 0 || !RENDER_CARD_HIT(RENDER_TOOLS_X + 20, RENDER_TOOLS_Y - 2)) {
            printf("FAIL: the toolbox's tap test\n"); return 1; }
        for (int i = 0; i < ALGAE_CELLS; i++) tank.algae[i] = 0;
        tank_veg_set(&tank, 1, 1.0f);
    }
    /* ...then the mow: one sweep along the floor takes every frond of a bed
     * to nubs, never bare */
    for (int b = 0; b < VEG_BEDS; b++) {
        float x0, x1, ty;
        tank_veg_bed(&tank, b, &x0, &x1, &ty, NULL);
        for (float sx = x0 + 2; sx <= x1; sx += 4) tank_touch_drag(&tank, sx, TANK_BOT - 8.0f);
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);   /* consume the stroke... */
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);   /* ...and reset for the next */
        if (fabsf(tank.veg_growth[b] - VEG_NUB) > 1e-3f) {   /* it keeps growing a hair per tick */
            printf("FAIL: bed %d not mowed to nubs (%.2f)\n", b, tank.veg_growth[b]); return 1;
        }
    }
    if (tank.startled) { printf("FAIL: trimming spooked the tank\n"); return 1; }
    /* the canopy comfort band, all three regimes */
    fish_t *cf = &tank.fish[0];
    for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(&tank, b, 1.0f);      /* jungle */
    cf->stress = 0;
    for (int i = 0; i < 60 * 30; i++) {
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
    }
    float s_jungle = cf->stress;
    for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(&tank, b, VEG_NUB);   /* scalped bare */
    cf->stress = 0;
    for (int i = 0; i < 60 * 60; i++) {
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
    }
    float s_bare = cf->stress;
    for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(&tank, b, 0.40f);     /* comfortable */
    cf->stress = 5;
    for (int i = 0; i < 60 * 30; i++) {
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
    }
    float s_comfy = cf->stress;
    /* the smother threshold (2026-09-04): TWO beds at 90% is smothered ... */
    tank_veg_set(&tank, 0, 0.9f); tank_veg_set(&tank, 1, 0.9f); tank_veg_set(&tank, 2, 0.3f);
    cf->stress = 0;
    for (int i = 0; i < 60 * 60; i++) {
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
    }
    float s_two = cf->stress;
    /* ... but ONE bed at the ceiling with the others tall (under 85% of their
     * own ceilings, ~0.83: 0.62 is three quarters of the way) is just a lot
     * of good cover: stress must fall, faster than in open water */
    tank_veg_set(&tank, 0, 1.0f); tank_veg_set(&tank, 1, 0.62f); tank_veg_set(&tank, 2, 0.62f);
    cf->stress = 5; cf->x = TANK_W * 0.5f; cf->y = 60;   /* open water, not hidden */
    for (int i = 0; i < 60 * 8; i++) {
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        cf->x = TANK_W * 0.5f; cf->y = 60; cf->goal.id = GOAL_EXPLORE;   /* not resting: base decay only */
    }
    float s_one = cf->stress;
    for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(&tank, b, VEG_BARE);   /* bare-ish: base decay only */
    cf->stress = 5;
    for (int i = 0; i < 60 * 8; i++) {
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        cf->x = TANK_W * 0.5f; cf->y = 60; cf->goal.id = GOAL_EXPLORE;
    }
    float s_open = cf->stress;
    printf("selftest-tend: stress jungle %.1f, bare %.1f (mild), comfy %.1f | two beds 90%% %.1f, one full bed %.1f (open water 8 s from 5: cover %.1f vs none %.1f)\n",
           s_jungle, s_bare, s_comfy, s_two, s_one, s_one, s_open);
    if (s_jungle < 4.0f) { printf("FAIL: overgrown tank not stressful\n"); return 1; }
    if (s_bare < 0.5f || s_bare > 3.5f) { printf("FAIL: bare tank should be mildly uneasy\n"); return 1; }
    if (s_comfy > 1.5f) { printf("FAIL: comfortable canopy should let stress decay\n"); return 1; }
    if (s_two < 1.5f) { printf("FAIL: two beds past 85%% should smother\n"); return 1; }
    if (s_one >= s_open) { printf("FAIL: one tall bed should calm, not stress\n"); return 1; }
    /* cleaning: squeegee strokes across every row of the glass */
    for (int y = 8; y < ALGAE_ROWS * ALGAE_CELL; y += ALGAE_CELL) {   /* (the film grid: the frame in the rectangle, down to the bowl's floor) */
        for (int x = 0; x <= TANK_W; x += 8) tank_touch_drag(&tank, (float)x, (float)y);
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);   /* consume: stroke ends */
        progression_tick(&tank, 1.0f / 60.0f);
    }
    film = 0;
    for (int i = 0; i < ALGAE_CELLS; i++) film += tank.algae[i] > 0;
    if (film) { printf("FAIL: %d algae cells survived the wipe\n", film); return 1; }
    if (!(tank.tank_ms_bits & TMS_FIRST_TRIM) || !(tank.tank_ms_bits & TMS_FIRST_CLEANING)) {
        printf("FAIL: upkeep milestones not detected (ms 0x%03x)\n", tank.tank_ms_bits); return 1;
    }
    printf("selftest-tend: trimmed + wiped clean (trims %d, cells %d, tank ms 0x%03x)\n",
           tank.trims, tank.cells_cleaned, tank.tank_ms_bits);
    /* upkeep survives a save round-trip */
    tank_veg_set(&tank, 0, 0.62f); tank_veg_set(&tank, 1, VEG_NUB); tank_veg_set(&tank, 2, 0.9f);
    tank_grow_algae(&tank, 40);
    progression_save(&tank);
    tank_t before = tank;
    tank_init(&tank, 9); progression_boot(&tank);
    /* the boot lives the seconds since the save (the stamp is whole seconds:
       crossing one grows every frond a hair - 1/72000 - which used to make
       this compare flaky, 2026-09-23), so the heights get a tolerance */
    bool veg_same = true;
    for (int b = 0; b < VEG_BEDS_MAX && veg_same; b++) {
        if (fabsf(tank.veg_growth[b] - before.veg_growth[b]) > 1e-3f) veg_same = false;
        for (int i = 0; i < VEG_FRONDS_MAX; i++) if (fabsf(tank.veg_h[b][i] - before.veg_h[b][i]) > 1e-3f) veg_same = false;
    }
    if (memcmp(tank.algae, before.algae, ALGAE_CELLS) != 0 || !veg_same ||
        tank.trims != before.trims || tank.cells_cleaned != before.cells_cleaned) {
        printf("FAIL: upkeep state lost in save round-trip\n"); return 1;
    }
    printf("selftest-tend: save round-trip ok\n");
    for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(&tank, b, 0.40f);  /* comfy for the hold legs */
    /* the settled hold: fish[0] made calm + trusting, finger 90 px away.
     * Holds are reported ~0.3 s after contact (platform), so hold_time here
     * maps 1:1 to reported time; the gate is 2.7 s of hold_time. */
    fish_t *f = &tank.fish[0];
    f->trust = 9; f->hunger = 1; f->energy = 10; f->stress = 0;
    f->goal.id = GOAL_EXPLORE; f->goal.urgency = 2;
    tank.ravenous = false;              /* isolate the hold reflex (no progression_tick here) */
#ifdef CONFIG_POCKET_TANK_320X240
    const float hdx = -40;              /* (this fork) the CYD's glass ends at x 320 and its grass stands far higher (GRASS_Y): the
                                           finger just over the grass, as on the 1.8, and 70 px from the right glass, not 30 */
#else
    const float hdx = 0;
#endif
    f->x = 200 + hdx; f->y = GRASS_Y(200); float hx = 290 + hdx, hy = GRASS_Y(200);
    float d_early = -1, d_late = -1;
    for (int step = 1; step <= 60 * 8; step++) {
        tank_touch_hold(&tank, hx, hy);
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        f->hunger = 1;                                  /* keep the veto out of this leg */
        if (step == 60 * 2) d_early = tank_dist(f->x, f->y, hx, hy);
        if (step == 60 * 8) d_late = tank_dist(f->x, f->y, hx, hy);
    }
    printf("selftest-tend: hold dist at 2 s %.0f px, at 8 s %.0f px\n", d_early, d_late);
    if (d_early < 45) { printf("FAIL: fish approached before the 3 s gate\n"); return 1; }
    if (d_late > 45) { printf("FAIL: settled hold never drew the fish in\n"); return 1; }
    /* a starving fish ignores the finger */
    f->x = 200 + hdx; f->y = GRASS_Y(200); f->hunger = 9.4f;
    for (int i = 0; i < MAX_FOOD; i++) tank.food[i].alive = false;   /* nothing to chase */
    for (int step = 1; step <= 60 * 8; step++) {
        tank_touch_hold(&tank, hx, hy);
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        f->hunger = 9.4f;
        for (int i = 0; i < MAX_FOOD; i++) tank.food[i].alive = false;
        /* the others parked far from the finger (2026-09-29): a trusting
           friend drawn to it led the starving fish there by follow_friend */
        for (int i = 1; i < tank.n_fish; i++) { tank.fish[i].x = 40; tank.fish[i].y = 60; tank_glass_clamp(&tank.fish[i].x, &tank.fish[i].y, 30); }
    }
    float d_hungry = tank_dist(f->x, f->y, hx, hy);
    printf("selftest-tend: starving fish dist after 8 s hold %.0f px\n", d_hungry);
    if (d_hungry < 40) { printf("FAIL: a starving fish came to the finger\n"); return 1; }
    /* the milestone is per fish (2026-09-15): two trusting fish drawn in by
     * ONE hold both earn first hold-approach, and the hold counts once */
    if (tank.n_fish < 2) { printf("FAIL: tend test needs two fish\n"); return 1; }
    fish_t *g = &tank.fish[1];
    tank.hold_active = false; tank_tick(&tank, 1.0f / 60.0f, advisor_rules);   /* end the hold */
    f->ms_bits &= ~MS_FIRST_HOLD_APPROACH; g->ms_bits &= ~MS_FIRST_HOLD_APPROACH;
    int holds_before = tank.hold_approaches;
    f->trust = 10; f->hunger = 1; f->stress = 0; f->x = 200 + hdx; f->y = GRASS_Y(200);   /* the fast one, closer */
    f->heading = 3.14159f; tank_fish_face(f);   /* swimming AWAY until the draw: its wander must not
                                                  carry it inside HOLD_APPROACH_FROM first (2026-09-29) */
    g->trust = 6;  g->hunger = 1; g->stress = 0; g->x = 120 + hdx; g->y = GRASS_Y(200); g->energy = 10;
    g->goal.id = GOAL_EXPLORE; g->goal.urgency = 2;
    for (int step = 1; step <= 60 * 14; step++) {
        tank_touch_hold(&tank, hx, hy);
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        f->hunger = 1; g->hunger = 1;
    }
    printf("selftest-tend: two-fish hold: fish0 ms %s, fish1 ms %s (dist %.0f px), holds +%d\n",
           f->ms_bits & MS_FIRST_HOLD_APPROACH ? "yes" : "no", g->ms_bits & MS_FIRST_HOLD_APPROACH ? "yes" : "no",
           tank_dist(g->x, g->y, hx, hy), tank.hold_approaches - holds_before);
    if (!(f->ms_bits & MS_FIRST_HOLD_APPROACH) || !(g->ms_bits & MS_FIRST_HOLD_APPROACH)) {
        printf("FAIL: the second fish to reach the finger was not credited\n"); return 1;
    }
    if (tank.hold_approaches - holds_before != 1) { printf("FAIL: one hold counted %d times\n", tank.hold_approaches - holds_before); return 1; }
    /* a fish already sitting under the finger never approached: no credit */
    tank.hold_active = false; tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
    f->ms_bits &= ~MS_FIRST_HOLD_APPROACH; g->ms_bits &= ~MS_FIRST_HOLD_APPROACH;
    holds_before = tank.hold_approaches;
    f->x = hx + 8; f->y = hy; g->x = 40; g->y = 40; g->trust = 0;        /* g stays away */
    for (int step = 1; step <= 60 * 8; step++) {
        tank_touch_hold(&tank, hx, hy);
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        f->x = hx + 8; f->y = hy; f->hunger = 1; g->trust = 0;
    }
    printf("selftest-tend: fish parked under the finger 8 s: ms %s, holds +%d\n",
           f->ms_bits & MS_FIRST_HOLD_APPROACH ? "yes" : "no", tank.hold_approaches - holds_before);
    if ((f->ms_bits & MS_FIRST_HOLD_APPROACH) || tank.hold_approaches != holds_before) {
        printf("FAIL: a fish that never approached was credited with a hold-approach\n"); return 1;
    }
    (void)system(cmd);
    return 0;
}

/* ---------- LVGL + SDL ---------- */
#include "lvgl/lvgl.h"
#include <SDL2/SDL.h>

static lv_draw_buf_t draw_buf;
static uint16_t canvas_buf[TANK_W * TANK_H];
static uint16_t scene_buf[TANK_W * TANK_H];
static uint8_t  vig_buf[TANK_W * TANK_H];     /* vignette LUT, as on the device */
static uint16_t card_buf[RENDER_CARD_W * RENDER_CARD_H];   /* stats card cache, as on the device */
static uint32_t dirty_buf[RENDER_DIRTY_WORDS];
static lv_obj_t *canvas;
static uint32_t last_ms;
static bool llm_available = false;
static bool llm_active = false;
static int  selected_fish = -1;      /* click a fish for its stat card */
static bool ui_visible = true;       /* U toggles all overlays */
static bool milestones_view = false; /* M toggles the milestones screen */
static bool confirm_view = false;    /* X: the reset prompt (YES wipes the save) */
static bool settings_view = false;   /* the settings page (from the milestones page's SETTINGS button) */
static bool shop_view = false;       /* the shop (the sand dollar on the milestones page's TANK row; $ key) */
static bool updates_view = false;    /* the UPDATES page (the settings page's UPDATES button; no radio) */
static bool update_mode = false;     /* update mode (2026-09-30): what the device runs at boot, before the tank - here the
                                        tank pauses and the pages run over a pretend radio (net_port_sim.c); E enters it */
static float update_clock;
/* a worn tank (TANK_WORN, the watch, 2026-10-02): the pretend wearer. T puts
 * the watch on the other way around (buttons toward the elbow instead of the
 * hand); the window then shows the glass as that wearer sees it - upside down
 * until settings SCREEN is TURNED - and the mouse lands where their finger would. */
static bool sim_worn_turned = false;
static bool sim_view_turned(void) { return (!update_mode && tank_screen_turned(&tank)) != sim_worn_turned; }   /* update mode: no tank, no turn (the device's) */
static void sim_flip_canvas(void);
static bool s_canvas_flipped;
static bool ms_back = false;         /* the settings page's CLOSE just brought the milestones page back (2026-09-16,
                                        Strato: a submenu's CLOSE returns to the menu, not the tank): this release is spent */
static int  sim_bright = 100;        /* the settings page's brightness (device setting; cosmetic here) */
static uint32_t confirm_ms;          /* when it opened; it gives up after CONFIRM_MS */
#define CONFIRM_MS 20000
/* the battery (2026-09-24): the sim has no gauge, so a pretend cell drains
 * and charges at the device's default rates and P moves the cable. The pill
 * shows with a card, when low, and for BAT_POPUP_S after P plugs in; a
 * click on it opens the battery page (any click closes it), as on the glass. */
static bat_t sim_bat;
static float sim_bat_pct = 72.0f;    /* --battery <pct> sets it */
static int   sim_bat_state = BAT_ON_BATTERY;
static bool  battery_view = false;   /* the battery page (a click on the pill) */
static uint32_t battery_view_ms, bat_popup_ms;
#define BATTERY_VIEW_MS 30000
static int sim_bat_gauge(void) { return (int)(sim_bat_pct + 0.5f); }
static bool sim_pill_up(void) {
    return !milestones_view && !settings_view && !shop_view && !battery_view && !confirm_view && !setup_active() &&
           ((ui_visible && selected_fish >= 0) || (!BAT_ON_POWER(sim_bat_state) && sim_bat_gauge() <= 10) ||
            (bat_popup_ms && SDL_GetTicks() - bat_popup_ms < BAT_POPUP_S * 1000));
}

static uint32_t tick_cb(void) { return SDL_GetTicks(); }

/* ---- sound (docs/AUDIO.md): the mixer in common/audio.c fed by an SDL
 * callback; the tank's events and the notice queue become cues here, the
 * same way the device's audio port does it ---- */
static SDL_AudioDeviceID s_adev;
static int16_t *s_bank;
static bool s_loop_on;               /* bubbles_loop running (the setup's bubble page) */
static int  s_prev_sel = -1;
static void audio_cb(void *ud, Uint8 *stream, int len) { (void)ud; audio_render((int16_t *)stream, len / 2); }
static void snd(int cue, int pitch_q8) {
    if (!s_adev) return;
    SDL_LockAudioDevice(s_adev); audio_play(cue, pitch_q8, SDL_GetTicks()); SDL_UnlockAudioDevice(s_adev);
}
static int stage_pitch(int fish) {   /* fry high, elder low */
    if (fish < 0 || fish >= tank.n_fish) return AUDIO_PITCH_ONE;
    static const int p[4] = { 320, 282, 256, 230 };
    return p[tank.fish[fish].stage & 3];
}
static void on_tank_event(int ev, int fish, void *ud) {
    (void)ud;
    switch (ev) {
    case TEV_TAP:         snd(SND_TAP, AUDIO_PITCH_ONE); break;
    case TEV_FEED:        snd(SND_FEED, AUDIO_PITCH_ONE); break;
    case TEV_LIGHT_ON:    snd(SND_LIGHT_ON, AUDIO_PITCH_ONE); break;
    case TEV_LIGHT_OFF:   snd(SND_LIGHT_OFF, AUDIO_PITCH_ONE); break;
    case TEV_WIPE:        snd(SND_WIPE, AUDIO_PITCH_ONE); break;
    case TEV_SNIP:        snd(SND_SNIP, AUDIO_PITCH_ONE); break;
    case TEV_EAT:         snd(SND_EAT, stage_pitch(fish)); break;
    case TEV_SPOOK:       snd(SND_SPOOK, AUDIO_PITCH_ONE); break;
    case TEV_INVESTIGATE: snd(SND_INVESTIGATE, stage_pitch(fish)); break;
    case TEV_BUBBLES:     snd(SND_BUBBLES, AUDIO_PITCH_ONE); break;
    case TEV_WELCOME:     snd(SND_WELCOME, AUDIO_PITCH_ONE); break;
    case TEV_WHEEL_TICK:  snd(SND_WHEEL_TICK, AUDIO_PITCH_ONE); break;
    case TEV_CONFIRM:     snd(SND_CONFIRM, AUDIO_PITCH_ONE); break;
    default: break;
    }
}
static void sound_init(void) {
    FILE *f = fopen(SOUNDS_BIN, "rb");
    if (!f) { printf("sound: %s not found (tools/make_sounds.py build) - silent\n", SOUNDS_BIN); return; }
    s_bank = malloc(SND_BANK_BYTES);
    size_t got = s_bank ? fread(s_bank, 1, SND_BANK_BYTES, f) : 0; fclose(f);
    if (got != SND_BANK_BYTES) { printf("sound: bank is %zu bytes, sounds.h says %u - rebuild (tools/make_sounds.py build)\n", got, (unsigned)SND_BANK_BYTES); free(s_bank); s_bank = NULL; return; }
    audio_init(s_bank, SND_BANK_SAMPLES);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { printf("sound: SDL audio init failed: %s\n", SDL_GetError()); return; }   /* LVGL brings up video later */
    SDL_AudioSpec want = { 0 }, have;
    want.freq = SND_RATE; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 256; want.callback = audio_cb;
    s_adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!s_adev) { printf("sound: SDL audio failed: %s\n", SDL_GetError()); return; }
    SDL_PauseAudioDevice(s_adev, 0);
    tank_events_set(on_tank_event, NULL);
    printf("sound: %u cues, %u KB bank, %d Hz (V cycles the volume)\n", (unsigned)SND_COUNT, (unsigned)(SND_BANK_BYTES / 1024), have.freq);
}
/* per frame: the notice queue, the bubble loop, the card cue, night */
static void sound_frame(uint32_t now, float dt) {
    notice_tick(&tank, dt, setup_active() || setup_birth_due() || confirm_view || milestones_view || settings_view || shop_view || battery_view);
    int cue = notice_take_cue();
    if (cue >= 0) snd(cue, AUDIO_PITCH_ONE);
    bool loop = setup_active() && !setup_is_birth() && setup_page() == SETUP_PG_BUBBLES;
    if (loop != s_loop_on && s_adev) {
        SDL_LockAudioDevice(s_adev);
        if (loop) audio_play(SND_BUBBLES_LOOP, AUDIO_PITCH_ONE, now); else audio_stop(SND_BUBBLES_LOOP);
        SDL_UnlockAudioDevice(s_adev);
        s_loop_on = loop;
    }
    if (selected_fish >= 0 && s_prev_sel < 0) snd(SND_CARD_OPEN, AUDIO_PITCH_ONE);
    if (selected_fish < 0 && s_prev_sel >= 0) snd(SND_CARD_CLOSE, AUDIO_PITCH_ONE);
    s_prev_sel = selected_fish;
    if (s_adev) { SDL_LockAudioDevice(s_adev); audio_set_night(tank.night); SDL_UnlockAudioDevice(s_adev); }
}

/* brain indicator, top-right: teal square = rules, amber = LLM */
static void draw_brain_dot(void) {
    uint16_t col = llm_active ? 0xFDC0 /*amber*/ : 0x3E98 /*teal*/;
    for (int y = 4; y < 10; y++)
        for (int x = TANK_W - 10; x < TANK_W - 4; x++)
            canvas_buf[y * TANK_W + x] = col;
}

/* the round build: the panel has no pixels outside its circle - the window shows the same */
static void round_mask(void) {
#ifdef TANK_ROUND
    for (int y = 0; y < TANK_H; y++) {
        float d = y + 0.5f - TANK_RAD, r2 = TANK_RAD * TANK_RAD - d * d, half = r2 > 0 ? sqrtf(r2) : 0;
        int x0 = (int)(TANK_RAD - half), x1 = (int)(TANK_RAD + half);
        for (int x = 0; x < x0; x++) canvas_buf[y * TANK_W + x] = 0;
        for (int x = x1 + 1; x < TANK_W; x++) canvas_buf[y * TANK_W + x] = 0;
    }
#elif defined(TANK_CORNER_R)                    /* the watch: its glass's round corners (tank_glass_x0 / x1 are the arcs) */
    for (int y = 0; y < TANK_H; y++) {
        int x0 = (int)tank_glass_x0(y + 0.5f), x1 = (int)tank_glass_x1(y + 0.5f);
        for (int x = 0; x < x0; x++) canvas_buf[y * TANK_W + x] = 0;
        for (int x = x1 + 1; x < TANK_W; x++) canvas_buf[y * TANK_W + x] = 0;
    }
#endif
}

/* the window's view of the glass turned over (sim_view_turned): the canvas is
 * reversed in place after the frame is drawn and put back before the next one
 * draws (the renderer's caches expect last frame's pixels where it left them) */
static void sim_flip_canvas(void) {
    for (int i = 0, j = TANK_W * TANK_H - 1; i < j; i++, j--) { uint16_t c = canvas_buf[i]; canvas_buf[i] = canvas_buf[j]; canvas_buf[j] = c; }
    s_canvas_flipped = !s_canvas_flipped;
}

static void frame_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_canvas_flipped) sim_flip_canvas();
    uint32_t now = SDL_GetTicks();
    float dt = (now - last_ms) / 1000.0f;
    last_ms = now;
    if (dt > 0.1f) dt = 0.1f;                     /* window drag pause */
    if (update_mode) {                            /* the tank is "rebooting": only the update pages run */
        update_clock += dt;
        net_sim_advance(dt); update_tick(dt);
        render_update(canvas_buf, TANK_W, update_clock);
        int o = update_outcome();
        if (o) {
            update_mode = false; net_port_off();
            printf("update mode: %s (radio %s)\n", o == UPD_RESTART ? "installed - the device would restart into the new image now" : "back to the tank", net_sim_radio_on() ? "ON?!" : "off");
            if (o == UPD_RESTART) notice_updated();   /* what the first boot of the new release shows */
        }
        lv_obj_invalidate(canvas);
        return;
    }
    tank.hold_light = setup_active() || confirm_view;   /* no lights-out mid-name */
    tank.ui_cover = tank.hold_light || milestones_view || settings_view || shop_view || battery_view || updates_view;   /* a fry's spawning waits */
    tank_tick(&tank, dt, llm_active ? advisor_llm : advisor_rules);
    {   /* the pretend cell, and the battery page's history fed once a second, as the device feeds it */
        if (sim_bat_state == BAT_ON_BATTERY) sim_bat_pct = fmaxf(0, sim_bat_pct - BAT_DRAIN_DEFAULT / 3600 * dt);
        else if (sim_bat_state == BAT_CHARGING && (sim_bat_pct += BAT_CHARGE_DEFAULT / 3600 * dt) >= 100) { sim_bat_pct = 100; sim_bat_state = BAT_FULL; }
        static float acc; acc += dt;
        if (acc >= 1) {
            if (battery_tick(&sim_bat, clock_port_now_unix(), acc, sim_bat_gauge(), sim_bat_state) > 0) bat_popup_ms = now;
            acc = 0;
        }
        if (battery_view && now - battery_view_ms > BATTERY_VIEW_MS) battery_view = false;
    }
    progression_tick(&tank, dt);
    if (!confirm_view) {                          /* an arrival owed its welcome: the birth flow (setup.c) */
        int nb = setup_poll_birth(&tank);
        if (nb >= 0) { selected_fish = -1; milestones_view = false; snd(SND_ARRIVAL, AUDIO_PITCH_ONE);
                       printf("a new fry, %s: the birth flow is up (announce / name / family; S drops it)\n", tank.fish[nb].name); }
    }
    { int rf = setup_take_renamed();              /* a rename closed: back to the milestones page, the fish's card up */
      if (rf >= 0) { milestones_view = true; render_milestones_show_fish(&tank, rf); printf("milestones: back on %s's card\n", tank.fish[rf].name); } }
    sound_frame(now, dt);
    if (milestones_view) render_milestones(&tank, canvas_buf, TANK_W);
    else if (settings_view) render_settings(&tank, canvas_buf, TANK_W, sim_bright, audio_volume());
    else if (updates_view) render_updates_page(canvas_buf, TANK_W);
    else if (shop_view) render_shop(&tank, canvas_buf, TANK_W);
    else {
        render_tank(&tank, canvas_buf, TANK_W);
        render_sd_toast(&tank, canvas_buf, TANK_W);      /* "+N" sand dollars, as they are earned */
        if (ui_visible) {
            draw_brain_dot();
            if (selected_fish >= 0)
                render_stats_card(&tank, selected_fish, canvas_buf, TANK_W);
            if (!battery_view) render_tool_chip(&tank, selected_fish, canvas_buf, TANK_W);   /* a tool in hand: the chip (2026-10-01) */
        }
        if (battery_view) {                              /* the battery page (a click on the pill) */
            bat_info_t bi;
            battery_info(&sim_bat, clock_port_now_unix(), sim_bat_gauge(), 3500 + (int)(sim_bat_pct * 7), sim_bat_state, &bi);
            render_battery_info(canvas_buf, TANK_W, &bi, tank.clock);
        } else if (sim_pill_up()) render_battery(canvas_buf, TANK_W, sim_bat_gauge() / 100.0f, sim_bat_state, tank.clock);
        const notice_t *nt = notice_current();
        if (nt) render_notice(&tank, canvas_buf, TANK_W, nt->kind, nt->fish, nt->bit, 1.0f - nt->age / NOTICE_UP_S);
    }
    if (setup_active()) render_setup(&tank, canvas_buf, TANK_W, tank.clock);
    if (confirm_view) render_confirm_reset(canvas_buf, TANK_W, 1.0f - (SDL_GetTicks() - confirm_ms) / (float)CONFIRM_MS);
    round_mask();
    if (sim_view_turned()) sim_flip_canvas();
    lv_obj_invalidate(canvas);
}

/* headless check of the LLM advisor path: encode → infer → goals applied.
 * minutes > 0 runs REAL-TIME pacing for that long (the honest measurement of
 * survival-reflex overrides); minutes == 0 is the fast smoke test. */
static int selftest_llm(int minutes) {
    if (!advisor_llm_init("../model/out/model_q4.bin", "../model/out/tokenizer.bin")) {
        printf("FAIL: model.bin/tokenizer.bin not found under ../model/out/\n");
        return 1;
    }
    tank_init(&tank, 4321);
    tank_new_population(&tank);
    while (tank.n_fish < 4) tank_add_fish(&tank, 0, 1);   /* the 4-fish tank the soak numbers refer to */
    for (int i = 2; i < 4; i++) tank.fish[i].stage = STAGE_ADULT;
    if (getenv("POCKET_CURIOUS")) {               /* reproduce a state: POCKET_CURIOUS=9 = the
                                                     device after days of the old economy */
        for (int i = 0; i < tank.n_fish; i++) { tank.fish[i].curiosity = (float)atof(getenv("POCKET_CURIOUS")); tank.fish[i].hunger = 3; }
        printf("start state: curiosity %s, hunger 3\n", getenv("POCKET_CURIOUS"));
    }
    print_roster(&tank);
    printf("warm-up (pages in the mmap'd weights):\n");
    advisor_llm_debug(&tank, 0);
    advisor_llm_debug(&tank, 1);
    int changes = 0, torn = 0;
    int ticks = minutes > 0 ? minutes * 3600 : 3600;
    int delay = minutes > 0 ? 16 : 2;             /* 16ms = real-time 60fps */
    goal_id_t last[N_FISH_MAX];
    for (int i = 0; i < tank.n_fish; i++) last[i] = tank.fish[i].goal.id;
    /* census (2026-09-01, Strato: "all four fish overlapping at the bubble
     * column almost all the time"): goal shares, time near the column, how
     * often 3+ fish crowd it, and the drives the model is reading */
    long goal_ticks[GOAL_COUNT] = {0}, near_ticks = 0, crowd_ticks = 0, cluster_ticks = 0; double cur_sum = 0, hun_sum = 0, en_sum = 0;
    /* exploration census (2026-09-14, the boredom pass): distinct zones a fish
     * passes through per minute, the longest stretch on one goal, mean boredom */
    uint8_t zmask[N_FISH_MAX] = {0}; long zones_sum = 0, zone_windows = 0;
    float streak[N_FISH_MAX] = {0}, longest_streak = 0; double bored_sum = 0;
    for (int i = 0; i < ticks; i++) {
        tank_tick(&tank, 1.0f / 60.0f, advisor_llm);
        progression_tick(&tank, 1.0f / 60.0f);
        SDL_Delay(delay);
        int near_b = 0, near_r = 0, cluster = 0;
        for (int fi = 0; fi < tank.n_fish; fi++) {
            const fish_t *f = &tank.fish[fi];
            goal_ticks[f->goal.id]++;
            cur_sum += f->curiosity; hun_sum += f->hunger; en_sum += f->energy;
            if (tank_dist(f->x, f->y, tank.bubble_x, tank.bubble_y - 74) < 55) near_b++;
            if (tank_dist(f->x, f->y, tank.reef_x, tank.reef_y - 35) < 55) near_r++;
            bored_sum += f->bored;
            zmask[fi] |= (uint8_t)(1u << (int)(f->zone_last < 0 ? 0 : f->zone_last));
            streak[fi] += 1.0f / 60.0f; if (streak[fi] > longest_streak) longest_streak = streak[fi];
            int others = 0;                        /* the visual complaint: bodies overlapping */
            for (int fj = 0; fj < tank.n_fish; fj++)
                if (fj != fi && tank_dist(f->x, f->y, tank.fish[fj].x, tank.fish[fj].y) < 45) others++;
            if (others >= 2) cluster = 1;
        }
        near_ticks += near_b + near_r; if (near_b >= 3 || near_r >= 3) crowd_ticks++; cluster_ticks += cluster;
        if ((i + 1) % 3600 == 0)
            for (int fi = 0; fi < tank.n_fish; fi++) {
                zones_sum += __builtin_popcount(zmask[fi]); zone_windows++; zmask[fi] = 0;
            }
        if (i % (ticks / 4) == 0) {
            printf("  tick %5d goals:", i);
            for (int fi = 0; fi < tank.n_fish; fi++) printf(" %s", GOAL_NAMES[tank.fish[fi].goal.id]);
            printf(" | asks %u overrides %d\n", tank.advisor_asks, tank_reflex_overrides);
        }
        for (int fi = 0; fi < tank.n_fish; fi++)
            if (tank.fish[fi].goal.id != last[fi]) {
                last[fi] = tank.fish[fi].goal.id;
                streak[fi] = 0;
                changes++;
                if (tank.fish[fi].goal.confidence < 0.6f) torn++;
                if (changes <= 8)
                    printf("  t=%.1fs %-5s -> %s (urgency %.0f, p=%.2f%s)\n",
                           tank.clock, tank.fish[fi].name,
                           GOAL_NAMES[tank.fish[fi].goal.id],
                           tank.fish[fi].goal.urgency, tank.fish[fi].goal.confidence,
                           tank.fish[fi].hesitate > 0 ? ", hesitating" : "");
            }
    }
    printf("selftest-llm: %d goal changes (%d torn), %u asks, %d survival overrides in %d sim-seconds\n",
           changes, torn, tank.advisor_asks, tank_reflex_overrides, ticks / 60);
    printf("census: goal share");
    for (int g = 0; g < GOAL_COUNT; g++) printf(" %s %.0f%%", GOAL_NAMES[g], 100.0 * goal_ticks[g] / (ticks * tank.n_fish));
    printf("\ncensus: fish-time at a landmark (bubbles/reef, 55 px) %.0f%% | 3+ fish crowding one %.0f%% of the time | "
           "a 3-fish cluster anywhere %.0f%% | mean curiosity %.1f hunger %.1f energy %.1f\n",
           100.0 * near_ticks / (ticks * tank.n_fish), 100.0 * crowd_ticks / ticks, 100.0 * cluster_ticks / ticks,
           cur_sum / (ticks * tank.n_fish), hun_sum / (ticks * tank.n_fish), en_sum / (ticks * tank.n_fish));
    printf("census: explore - %.1f distinct zones per fish-minute | longest one-goal stretch %.0f s | mean bored %.1f\n",
           zone_windows ? (double)zones_sum / zone_windows : 0.0, longest_streak, bored_sum / (ticks * tank.n_fish));
    return changes >= 4 ? 0 : 1;                  /* a live brain redirects fish */
}

/* --snapshot <prefix> [seconds]: run headless (rules brain, all 6 fish, fast
 * progression) and write <prefix>_tank.ppm, _card.ppm, _milestones.ppm -
 * a look at the renderer without a window (docs, review, CI). */
static void write_ppm(const char *path, const uint16_t *fb) {
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        uint16_t p = fb[i];
        unsigned char rgb[3] = { (unsigned char)(((p >> 11) & 31) << 3), (unsigned char)(((p >> 5) & 63) << 2),
                                 (unsigned char)((p & 31) << 3) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
/* --hero <prefix>: pocketank.com's glamor shot (2026-09-29, Strato: the site's
 * frames caught fish "mid-flip and they look flat"). The fish are posed
 * side-on (tank_fish_face: no turn in progress) and held there. Three
 * grown fish, the castle, the coral and the reef cluster in full bloom, tidy
 * grass, clean glass; <prefix>_hero.ppm and <prefix>_hero_card.ppm (the stats card). */
typedef struct { int preset; stage_t stage; float x, y, heading; } hero_fish_t;
static int hero_shot(const char *prefix) {
    static const hero_fish_t CAST[] = {
        { 0, STAGE_ELDER, 244, 150,  3.14159f - 0.06f },   /* mira: teal, the logo's fish, center stage */
        { 3, STAGE_ADULT, 104, 122,  0.10f },              /* nori: violet, upper left, heading in */
        { 1, STAGE_ADULT, 330, 323,  3.14159f },           /* bolt: coral red, through the castle arch */
    };
    const int n = (int)(sizeof CAST / sizeof CAST[0]);
    tank_init(&tank, 2024);
    tank_new_population(&tank);
    for (int i = 0; i < n; i++) tank_make_fish(&tank, i, CAST[i].preset, 0.6f, 0.6f, CAST[i].stage);
    tank.n_fish = n;
    for (int i = 0; i < n; i++) {
        tank.fish[i].eaten = 60;                          /* well fed: the size bonus */
        progression_set_age(&tank, i, CAST[i].stage == STAGE_ELDER ? STAGE_ELDER_AGE + 1 : STAGE_ADULT_AGE + 1);
        tank.fish[i].hunger = 2; tank.fish[i].stress = 1; tank.fish[i].energy = 8;
    }
    /* the card's fish is an old friend: trusting, and its traits seen (render.c
       reveals a slider once its behaviour has been: the dart, the follow, its landmark) */
    tank.fish[0].ms_bits |= MS_FIRST_DART | MS_FIRST_FOLLOW | MS_INSPECTED | MS_FIRST_GRASS;
    tank.fish[0].trust = 8.5f; tank.fish[0].bold = 0.72f; tank.fish[0].sociable = 0.4f; tank.fish[0].curiosity = 7.5f;
    tank.sd_unlocks |= SD_ITEM_CASTLE | SD_ITEM_CORAL | SD_ITEM_CLUSTER;
    tank_castle_place(&tank); tank_coral_place(&tank); tank_cluster_place(&tank);
    tank_decor_set(&tank, 2, 336, DECOR_Z_FRONT);                        /* the castle, right */
    tank_decor_set(&tank, 3, 208, DECOR_Z_FRONT);                        /* the coral in gold (the logo's sand dollar) */
    tank_coral_set_rgb(&tank, CORAL_PAL[7]); tank.coral_growth = CORAL_FULL;
    tank_decor_set(&tank, 4, 92, DECOR_Z_FRONT);                         /* the reef cluster in bloom, left */
    tank_cluster_set_scheme(&tank, 0); tank.cluster_growth = CLUSTER_FULL;
    for (int i = 0; i < 12 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);   /* bubbles up the column */
    tank_veg_set(&tank, 0, 0.62f); tank_veg_set(&tank, 1, 0.5f); tank_veg_set(&tank, 2, 0.7f);
    memset(tank.algae, 0, sizeof tank.algae);
    tank.night = false; for (int i = 0; i < MAX_FOOD; i++) tank.food[i].alive = false;
    static uint16_t fb[TANK_W * TANK_H], scene[TANK_W * TANK_H];
    render_set_scene_cache(scene);
    render_set_vignette_cache(vig_buf);
    render_set_card_cache(card_buf);
    render_set_dirty_mask(dirty_buf);
    for (int k = 0; k < 90; k++) {                       /* the pose held for the frames the scene draws */
        for (int i = 0; i < n; i++) {
            fish_t *f = &tank.fish[i];
            f->x = CAST[i].x; f->y = CAST[i].y; f->heading = CAST[i].heading; tank_fish_face(f);
            f->speed = f->target_speed = 38; f->hesitate = 0;
        }
        tank.clock += 1.0f / 60.0f;
        render_tank(&tank, fb, TANK_W);
    }
    char path[512];
    snprintf(path, sizeof path, "%s_hero.ppm", prefix); write_ppm(path, fb);
    render_tank(&tank, fb, TANK_W); render_stats_card(&tank, 0, fb, TANK_W);
    snprintf(path, sizeof path, "%s_hero_card.ppm", prefix); write_ppm(path, fb);
    printf("hero: wrote %s_hero.ppm, %s_hero_card.ppm\n", prefix, prefix);
    return 0;
}

/* --clip <prefix> [seconds]: the hero's tank, alive (2026-10-03, the newsletter:
 * Strato wanted moving pictures). The same grown fish and decor, placed by the
 * glass's own width so the bowl and the watch get the same scene, plus the
 * urchin and a shrimp school; the fish swim free under the rules brain and
 * every 4th frame (15 a second) is written as <prefix>_NNN.ppm. */
static int clip(const char *prefix, int seconds) {
    static const struct { int preset; stage_t stage; } CAST[] = {
        { 0, STAGE_ELDER }, { 3, STAGE_ADULT }, { 1, STAGE_ADULT }, { 2, STAGE_JUV } };
    const int n = (int)(sizeof CAST / sizeof CAST[0]);
    const float sx = TANK_W / 448.0f;
    tank_init(&tank, 2024);
    tank_new_population(&tank);
    for (int i = 0; i < n; i++) tank_make_fish(&tank, i, CAST[i].preset, 0.6f, 0.6f, CAST[i].stage);
    tank.n_fish = n;
    for (int i = 0; i < n; i++) {
        tank.fish[i].eaten = 60;
        progression_set_age(&tank, i, CAST[i].stage == STAGE_ELDER ? STAGE_ELDER_AGE + 1
                                    : CAST[i].stage == STAGE_ADULT ? STAGE_ADULT_AGE + 1 : STAGE_JUV_AGE + 1);
        tank.fish[i].hunger = 2; tank.fish[i].stress = 1; tank.fish[i].energy = 8;
    }
    tank.sd_unlocks |= SD_ITEM_CASTLE | SD_ITEM_CORAL | SD_ITEM_CLUSTER | SD_ITEM_URCHIN | SD_ITEM_SHRIMP;
    tank_castle_place(&tank); tank_coral_place(&tank); tank_cluster_place(&tank);
    tank_urchin_place(&tank); tank_shrimp_place(&tank, 6);
    tank_decor_set(&tank, 2, 336 * sx, DECOR_Z_FRONT);
    tank_decor_set(&tank, 3, 208 * sx, DECOR_Z_FRONT);
    tank_coral_set_rgb(&tank, CORAL_PAL[7]); tank.coral_growth = CORAL_FULL;
    tank_decor_set(&tank, 4, 92 * sx, DECOR_Z_FRONT);
    tank_cluster_set_scheme(&tank, 0); tank.cluster_growth = CLUSTER_FULL;
    tank_veg_set(&tank, 0, 0.62f); tank_veg_set(&tank, 1, 0.5f); tank_veg_set(&tank, 2, 0.7f);
    for (int i = 0; i < 20 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);   /* settle: bubbles up, fish spread */
    memset(tank.algae, 0, sizeof tank.algae);
    tank.night = false; for (int i = 0; i < MAX_FOOD; i++) tank.food[i].alive = false;
    static uint16_t fb[TANK_W * TANK_H], scene[TANK_W * TANK_H];
    render_set_scene_cache(scene);
    render_set_vignette_cache(vig_buf);
    render_set_card_cache(card_buf);
    render_set_dirty_mask(dirty_buf);
    char path[512]; int shot = 0;
    for (int k = 0; k < seconds * 60; k++) {
        for (int i = 0; i < n; i++) { tank.fish[i].hunger = 2; tank.fish[i].stress = 1; }
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        render_tank(&tank, fb, TANK_W);
        if (k % 4 == 0) { snprintf(path, sizeof path, "%s_%03d.ppm", prefix, shot++); write_ppm(path, fb); }
    }
    printf("clip: wrote %d frames, %s_000.ppm ..\n", shot, prefix);
    return 0;
}

static int snapshot(const char *prefix, int seconds) {
    tank_init(&tank, 2024);
    tank_new_population(&tank);
    while (tank.n_fish < N_FISH_MAX) tank_add_fish(&tank, 0, 1);
    for (int i = 2; i < tank.n_fish; i++) tank.fish[i].stage = (stage_t)(i % 4);
    tank.fish[0].stage = STAGE_ELDER; tank.fish[0].ms_bits = 0xfff; tank.fish[1].ms_bits = 0x1c7;
    tank.tank_ms_bits = 0x1a7;
    for (int i = 0; i < seconds * 60; i++) {
        tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        if (i % 400 == 0) tank_feed(&tank, 220, 3);
    }
    /* show the upkeep systems: one overgrown bed, one mid, one trimmed to
     * nubs, and a patchy algae film; park a fish inside the reef canopy so
     * the front/back frond weave is visible */
    tank_veg_set(&tank, 0, 0.9f); tank_veg_set(&tank, 1, 0.5f); tank_veg_set(&tank, 2, VEG_NUB);
    tank.veg_h[0][4] = tank.veg_h[0][5] = 0.35f;   /* two fronds flick-trimmed at mid height */
    tank_grow_algae(&tank, 90);
    tank.fish[1].x = tank.reef_x + 16; tank.fish[1].y = TANK_BOT - 60;
    static uint16_t fb[TANK_W * TANK_H], scene[TANK_W * TANK_H];
    char path[512];
    render_set_scene_cache(scene);
    render_set_vignette_cache(vig_buf);
    render_set_card_cache(card_buf);
    render_set_dirty_mask(dirty_buf);
    render_tank(&tank, fb, TANK_W);
    snprintf(path, sizeof path, "%s_tank.ppm", prefix); write_ppm(path, fb);
    /* _grown: the same tank left asleep for days - every frond at its own
       ceiling (tank_veg_cap), the film where a long absence leaves it */
    {
        tank_t grown = tank;
        for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(&grown, b, VEG_START);
        for (int i = 0; i < ALGAE_CELLS; i++) grown.algae[i] = 0;
        tank_tick_sleep(&grown, 100 * 3600);
        render_set_scene_cache(scene);            /* invalidated: its own scene */
        render_tank(&grown, fb, TANK_W);
        snprintf(path, sizeof path, "%s_grown.ppm", prefix); write_ppm(path, fb);
        render_set_scene_cache(scene);
        /* _regrow: every bed cut flat by the scissors, then a night - each
           frond's own pace (VEG_PACE_SPREAD) brings it back ragged */
        grown = tank;
        for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(&grown, b, 0.20f);
        for (int i = 0; i < ALGAE_CELLS; i++) grown.algae[i] = 0;
        tank_tick_sleep(&grown, 8 * 3600);
        render_set_scene_cache(scene);
        render_tank(&grown, fb, TANK_W);
        snprintf(path, sizeof path, "%s_regrow.ppm", prefix); write_ppm(path, fb);
        render_set_scene_cache(scene);
    }
    render_tank(&tank, fb, TANK_W); render_stats_card(&tank, 0, fb, TANK_W);
    snprintf(path, sizeof path, "%s_card.ppm", prefix); write_ppm(path, fb);
    render_tank(&tank, fb, TANK_W); render_stats_card(&tank, 1, fb, TANK_W);
    snprintf(path, sizeof path, "%s_card1.ppm", prefix); write_ppm(path, fb);   /* partly unrevealed */
    /* the toolbox (2026-10-01): the card with the scissors in hand, then the chip on the open tank */
    tank_set_tool(&tank, TOOL_SCISSORS);
    render_tank(&tank, fb, TANK_W); render_stats_card(&tank, 0, fb, TANK_W);
    snprintf(path, sizeof path, "%s_tools.ppm", prefix); write_ppm(path, fb);
    render_tank(&tank, fb, TANK_W); render_tool_chip(&tank, -1, fb, TANK_W);
    snprintf(path, sizeof path, "%s_tool_chip.ppm", prefix); write_ppm(path, fb);
    tank_set_tool(&tank, TOOL_HAND);
    /* the milestones page: everything seen but one fish badge and one tank
       badge (they wear the "new" ring), and a tapped badge's caption */
    for (int i = 0; i < tank.n_fish; i++) tank.fish[i].ms_seen = tank.fish[i].ms_bits;
    tank.tank_ms_seen = tank.tank_ms_bits;
    tank.fish[1].ms_seen &= ~MS_FIRST_MEAL_FROM_YOU; tank.tank_ms_seen &= ~TMS_FIRST_FULL_NIGHT;
    render_milestones(&tank, fb, TANK_W);
    snprintf(path, sizeof path, "%s_milestones.ppm", prefix); write_ppm(path, fb);
#ifdef CONFIG_POCKET_TANK_320X240
    render_milestones_tap(&tank, PG_X(MSP_NAME_X + 8), PG_Y(MS_TANK_Y));              /* (this fork) the CYD's TANK name: its own column */
#else
    render_milestones_tap(&tank, PG_X(100), PG_Y(MS_TANK_Y));                          /* TANK's name: the tally, the school drawn over it */
#endif
    render_milestones(&tank, fb, TANK_W);
    snprintf(path, sizeof path, "%s_milestones_tank.ppm", prefix); write_ppm(path, fb);
    { int n_keep = tank.n_fish; tank.n_fish = 3;                                       /* three of six: the open places dim */
#ifdef CONFIG_POCKET_TANK_320X240
      render_milestones_leave(); render_milestones_tap(&tank, PG_X(MSP_NAME_X + 8), PG_Y(MS_TANK_Y));
#else
      render_milestones_leave(); render_milestones_tap(&tank, PG_X(100), PG_Y(MS_TANK_Y));
#endif
      render_milestones(&tank, fb, TANK_W);
      snprintf(path, sizeof path, "%s_milestones_tank3.ppm", prefix); write_ppm(path, fb);
      tank.n_fish = n_keep; }
    render_milestones_leave();
    {   /* with a shrimp school (2026-09-30): the TANK row's arrow (ringed: the full
           school's badge, earned and not seen, waits on page 2), then page 2 */
        uint32_t unl = tank.sd_unlocks, tms = tank.tank_ms_bits, seen = tank.tank_ms_seen;
        tank.sd_unlocks |= SD_ITEM_SHRIMP; tank.tank_ms_bits |= TMS_FULL_SCHOOL; tank.tank_ms_seen = tank.tank_ms_bits & ~TMS_FULL_SCHOOL;
        render_milestones(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_milestones_shrimp.ppm", prefix); write_ppm(path, fb);
#ifdef CONFIG_POCKET_TANK_320X240
        render_milestones_tap(&tank, PG_X(MSP_TPG_X + 12), PG_Y(MS_TANK_Y));             /* (this fork) the CYD's page arrow */
#else
        render_milestones_tap(&tank, PAGE_X + 426, PAGE_Y + 254 + 20);
#endif
        render_milestones(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_milestones_shrimp2.ppm", prefix); write_ppm(path, fb);
        render_milestones_leave();
        tank.sd_unlocks = unl; tank.tank_ms_bits = tms; tank.tank_ms_seen = seen;
    }
    render_settings(&tank, fb, TANK_W, 60, 2);                     /* the defaults: the double-tap, AUTO FEED on, the picture free to turn */
    snprintf(path, sizeof path, "%s_settings.ppm", prefix); write_ppm(path, fb);
    tank_light_choice_set(&tank, 5); tank.autofeed_off = true; tank_orient_lock(&tank, true);
    render_settings(&tank, fb, TANK_W, 60, 2);                     /* AUTO after 3 MIN, AUTO FEED off, the way up locked */
    snprintf(path, sizeof path, "%s_settings_auto.ppm", prefix); write_ppm(path, fb);
    tank_light_choice_set(&tank, 0); tank.autofeed_off = false; tank_orient_lock(&tank, false);
#ifdef CONFIG_POCKET_TANK_320X240
    /* (this fork) the CYD once an IMU answers: ROTATION where SCREEN was, and
       the SLEEP row on SCREEN, the middle segment - then back as it was */
    render_settings_set_imu(true); render_settings_set_sleep(SET_SLEEP_SCREEN);
    render_settings(&tank, fb, TANK_W, 60, 2);
    snprintf(path, sizeof path, "%s_settings_imu.ppm", prefix); write_ppm(path, fb);
    render_settings_set_imu(false); render_settings_set_sleep(SET_SLEEP_LIGHT);
#endif
    {   /* the UPDATES page and update mode's pages (2026-09-30), over the pretend radio */
        setenv("POCKET_TANK_WIFI", "/tmp/pocket-tank-snapshot-wifi.txt", 1); net_port_creds_forget();
        render_updates_page(fb, TANK_W);
        snprintf(path, sizeof path, "%s_updates_nonet.ppm", prefix); write_ppm(path, fb);
        net_port_creds_set("Strato's Wi-Fi", "hunter2!"); render_updates_page(fb, TANK_W);
        snprintf(path, sizeof path, "%s_updates.ppm", prefix); write_ppm(path, fb);
        net_port_creds_forget(); unsetenv("POCKET_TANK_FAKE_UPDATE");
        update_begin(50, false); render_update(fb, TANK_W, 0.3f);
        snprintf(path, sizeof path, "%s_update_scanning.ppm", prefix); write_ppm(path, fb);
        net_sim_advance(2); update_tick(2); render_update(fb, TANK_W, 0.3f);
        snprintf(path, sizeof path, "%s_update_networks.ppm", prefix); write_ppm(path, fb);
        update_touch(100, 58 + 38 + 16, true); update_touch(100, 58 + 38 + 16, false);   /* the second row: a secured network */
        {   /* type "Hunter2!" the way a finger would: CAPS h, the n-z page, 123, the symbols */
            #define SKEY(cell) (update_touch(37 + ((cell) % 7) * 54 + 25, 96 + ((cell) / 7) * 78 + 36, true), update_touch(37 + ((cell) % 7) * 54 + 25, 96 + ((cell) / 7) * 78 + 36, false))
            SKEY(17); SKEY(7); SKEY(17); SKEY(14); SKEY(7); SKEY(0); SKEY(6); SKEY(14); SKEY(4); SKEY(14); SKEY(4); SKEY(18); SKEY(2); SKEY(14); SKEY(0);
            #undef SKEY
        }
        render_update(fb, TANK_W, 0.3f);
        snprintf(path, sizeof path, "%s_update_password.ppm", prefix); write_ppm(path, fb);
        update_touch(37 + 6 * 54 + 25, 96 + 2 * 78 + 36, true); update_touch(37 + 6 * 54 + 25, 96 + 2 * 78 + 36, false);   /* JOIN */
        render_update(fb, TANK_W, 0.3f);
        snprintf(path, sizeof path, "%s_update_connecting.ppm", prefix); write_ppm(path, fb);
        net_sim_advance(2); update_tick(2); net_sim_advance(2); update_tick(2); render_update(fb, TANK_W, 0.3f);
        snprintf(path, sizeof path, "%s_update_offer.ppm", prefix); write_ppm(path, fb);
        update_touch(32 + 88, 296 + 19, true); update_touch(32 + 88, 296 + 19, false);   /* UPDATE */
        net_sim_advance(2.5f); update_tick(2.5f); render_update(fb, TANK_W, 0.3f);
        snprintf(path, sizeof path, "%s_update_downloading.ppm", prefix); write_ppm(path, fb);
        net_sim_advance(4); update_tick(4); render_update(fb, TANK_W, 0.3f);
        snprintf(path, sizeof path, "%s_update_installed.ppm", prefix); write_ppm(path, fb);
        net_sim_advance(2); update_tick(2);
        setenv("POCKET_TANK_FAKE_UPDATE", "none", 1); update_begin(50, false);
        for (int i = 0; i < 4; i++) { net_sim_advance(1); update_tick(1); }
        render_update(fb, TANK_W, 0.3f);
        snprintf(path, sizeof path, "%s_update_uptodate.ppm", prefix); write_ppm(path, fb);
        net_port_creds_set("Fishbowl", "abcdefgh");                /* a message with ONE action: TRY AGAIN centred over BACK TO TANK (2026-10-03) */
        setenv("POCKET_TANK_FAKE_UPDATE", "downloadfail", 1); update_begin(50, false);
        for (int i = 0; i < 4; i++) { net_sim_advance(1); update_tick(1); }
        update_touch(PAGE_X + UPD_BTN_L_X + UPD_BTN_W / 2, PAGE_Y + UPD_BTN_Y + UPD_BTN_H / 2, true);
        update_touch(PAGE_X + UPD_BTN_L_X + UPD_BTN_W / 2, PAGE_Y + UPD_BTN_Y + UPD_BTN_H / 2, false);   /* UPDATE */
        for (int i = 0; i < 4; i++) { net_sim_advance(1); update_tick(1); }
        render_update(fb, TANK_W, 0.3f);
        snprintf(path, sizeof path, "%s_update_one_action.ppm", prefix); write_ppm(path, fb);
        setenv("POCKET_TANK_FAKE_UPDATE", "otherboard", 1); update_begin(50, false);
        for (int i = 0; i < 4; i++) { net_sim_advance(1); update_tick(1); }
        render_update(fb, TANK_W, 0.3f);
        snprintf(path, sizeof path, "%s_update_wrong_board.ppm", prefix); write_ppm(path, fb);
        net_port_creds_set("Strato's Wi-Fi", "wrong-now"); update_begin(50, false);
        for (int i = 0; i < 3; i++) { net_sim_advance(1); update_tick(1); }
        render_update(fb, TANK_W, 0.3f);
        snprintf(path, sizeof path, "%s_update_badpassword.ppm", prefix); write_ppm(path, fb);
        update_begin(12, false); render_update(fb, TANK_W, 0.3f);
        snprintf(path, sizeof path, "%s_update_plugin.ppm", prefix); write_ppm(path, fb);
        net_port_off(); net_port_creds_forget(); unsetenv("POCKET_TANK_FAKE_UPDATE"); unsetenv("POCKET_TANK_WIFI");
        render_notice(&tank, fb, TANK_W, NOTICE_UPDATED, -1, 0, 0.8f);
        snprintf(path, sizeof path, "%s_updated_notice.ppm", prefix); write_ppm(path, fb);
    }
    /* the shop (2026-09-15): broke, rich, an item's modal, the HOW TO EARN
       modal, then the tank with both purchases in it and the toast */
    render_shop(&tank, fb, TANK_W);
    snprintf(path, sizeof path, "%s_shop.ppm", prefix); write_ppm(path, fb);
    tank.sd_balance = 95; render_shop(&tank, fb, TANK_W);
    snprintf(path, sizeof path, "%s_shop_rich.ppm", prefix); write_ppm(path, fb);
#ifdef CONFIG_POCKET_TANK_320X240
    render_shop_tap(&tank, PG_X(SHOP_ROW_X), PG_Y(SHOP_ROW_Y(1))); render_shop(&tank, fb, TANK_W);   /* (this fork) the CYD's shop rows */
#else
    render_shop_tap(&tank, PAGE_X + 100, PAGE_Y + 98 + 56 + 20); render_shop(&tank, fb, TANK_W);      /* the snail's row -> its modal */
#endif
    snprintf(path, sizeof path, "%s_shop_modal.ppm", prefix); write_ppm(path, fb);
    render_shop_leave();
#ifdef CONFIG_POCKET_TANK_320X240
    render_shop_tap(&tank, PG_X(SHOP_ROW_X), PG_Y(SHOP_ROW_Y(0))); render_shop(&tank, fb, TANK_W);
#else
    render_shop_tap(&tank, PAGE_X + 100, PAGE_Y + 98 + 20); render_shop(&tank, fb, TANK_W);           /* the plant's row -> its modal (the long second line) */
#endif
    snprintf(path, sizeof path, "%s_shop_plant.ppm", prefix); write_ppm(path, fb);
    render_shop_leave();
#ifdef CONFIG_POCKET_TANK_320X240
    render_shop_tap(&tank, PG_X(SHP_EARN_X + 28), PG_Y(MSP_CLOSE_Y + 8)); render_shop(&tank, fb, TANK_W);
#else
    render_shop_tap(&tank, PAGE_X + 60, PAGE_Y + 320); render_shop(&tank, fb, TANK_W);                /* HOW TO EARN */
#endif
    snprintf(path, sizeof path, "%s_shop_earn.ppm", prefix); write_ppm(path, fb);
    render_shop_leave();
    tank.sd_unlocks = SD_ITEM_PLANT | SD_ITEM_SNAIL; tank_plant_place(&tank); tank_snail_place(&tank);
    tank_veg_set(&tank, 3, 0.45f);
    tank.snail_x = 120; tank.snail_y = 140;
    tank.sd_balance = 12; render_shop(&tank, fb, TANK_W);
    snprintf(path, sizeof path, "%s_shop_owned.ppm", prefix); write_ppm(path, fb);
    for (int i = 0; i < 30; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
    progression_sd_grant(&tank, 5);
    render_tank(&tank, fb, TANK_W); render_sd_toast(&tank, fb, TANK_W);
    snprintf(path, sizeof path, "%s_tank_shop.ppm", prefix); write_ppm(path, fb);   /* the snail on the glass, after film */
    { tank_decor_set(&tank, 0, 300, DECOR_Z_FRONT); setup_begin_place(&tank, 0);   /* the placement page: the plant dragged right, in FRONT */
      render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, 1.0f);
      snprintf(path, sizeof path, "%s_place.ppm", prefix); write_ppm(path, fb);
      setup_cancel(&tank); tank_decor_set(&tank, 0, PLANT_X_DEFAULT, DECOR_Z_MIDDLE); }
    uint8_t film[ALGAE_CELLS]; memcpy(film, tank.algae, sizeof film);
    { memset(tank.algae, 0, sizeof tank.algae);
      tank.snail_cell = -1; tank.snail_x = 300; tank.snail_y = SNAIL_FLOOR_Y; tank.snail_heading = 3.14159f;
      for (int i = 0; i < 30; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
      render_tank(&tank, fb, TANK_W);
      snprintf(path, sizeof path, "%s_tank_snail_floor.ppm", prefix); write_ppm(path, fb);   /* upright on the floor, walking left */
      { float sx = tank.snail_x; tank.snail_x = tank.reef_x + 20;                        /* in the left bed's grass: no frond root under its sole */
        render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_tank_snail_grass.ppm", prefix); write_ppm(path, fb);
        tank.snail_x = sx; }
      tank.snail_grazed = 128; render_tank(&tank, fb, TANK_W); render_stats_card(&tank, RENDER_CARD_SNAIL, fb, TANK_W);
      snprintf(path, sizeof path, "%s_snail_card.ppm", prefix); write_ppm(path, fb);         /* its card (2026-09-16) */
      memcpy(tank.algae, film, sizeof film); }
    { memset(tank.algae, 0, sizeof tank.algae); tank.snail_cell = -1; tank.snail_x = 200; tank.snail_y = 250;
      tank.algae[2 * ALGAE_COLS + 12] = 150;                    /* film straight above: the glass snail climbs, head first */
      for (int i = 0; i < 4; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
      render_tank(&tank, fb, TANK_W);
      snprintf(path, sizeof path, "%s_tank_snail_up.ppm", prefix); write_ppm(path, fb);
      memcpy(tank.algae, film, sizeof film); }
    { tank.sd_unlocks |= SD_ITEM_URCHIN; tank_urchin_place(&tank);   /* the urchin (2026-10-02): at the foot of the right bed, chewing; its card */
      for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(&tank, b, 0.6f);
      tank.veg_h[1][2] = 0.85f; tank_veg_sync(&tank);
      float fx; tank_veg_frond(&tank, 1, 2, &fx);
      tank.urchin_x = fx; tank.urchin_appetite = 0.05f; tank.urchin_rest = 0;
      for (int i = 0; i < 120; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
      render_tank(&tank, fb, TANK_W);
      snprintf(path, sizeof path, "%s_tank_urchin.ppm", prefix); write_ppm(path, fb);
      tank.urchin_x = PLANT_X_DEFAULT + 40; tank.urchin_bed = tank.urchin_frond = -1; tank.urchin_rest = 60;   /* out on the sand */
      render_tank(&tank, fb, TANK_W);
      snprintf(path, sizeof path, "%s_tank_urchin_sand.ppm", prefix); write_ppm(path, fb);
      tank.urchin_grazed_px = 1234; render_stats_card(&tank, RENDER_CARD_URCHIN, fb, TANK_W);
      snprintf(path, sizeof path, "%s_urchin_card.ppm", prefix); write_ppm(path, fb); }
    tank.sd_unlocks = 0; tank.sd_balance = 0;
#ifdef CONFIG_POCKET_TANK_320X240
    render_milestones_tap(&tank, PG_X(MS_BADGE_X(0)), PG_Y(MS_ROW(1) + MS_BADGE_DY));   /* (this fork) the CYD's rows */
#else
    render_milestones_tap(&tank, PAGE_X + 176 + 16, PAGE_Y + 4 + 40 + 20);           /* fish 1's first badge -> the detail modal */
#endif
    render_milestones(&tank, fb, TANK_W);
    snprintf(path, sizeof path, "%s_milestone_modal.ppm", prefix); write_ppm(path, fb);
    render_milestones_leave();
    /* the announcements (notice.h): a fish milestone, a tank milestone, a
       stage reached, low battery - each over the live tank */
    render_tank(&tank, fb, TANK_W); render_notice(&tank, fb, TANK_W, 0, 1, MS_FIRST_BUBBLES, 0.6f);
    snprintf(path, sizeof path, "%s_notice_fish.ppm", prefix); write_ppm(path, fb);
    render_tank(&tank, fb, TANK_W); render_notice(&tank, fb, TANK_W, 1, -1, TMS_FIRST_TRIM, 0.6f);
    snprintf(path, sizeof path, "%s_notice_tank.ppm", prefix); write_ppm(path, fb);
    render_tank(&tank, fb, TANK_W); render_notice(&tank, fb, TANK_W, 2, 2, 0, 0.6f);
    snprintf(path, sizeof path, "%s_notice_stage.ppm", prefix); write_ppm(path, fb);
    render_tank(&tank, fb, TANK_W); render_notice(&tank, fb, TANK_W, 3, -1, 0, 0.6f); render_battery(fb, TANK_W, 0.08f, BAT_ON_BATTERY, 0);
    snprintf(path, sizeof path, "%s_notice_battery.ppm", prefix); write_ppm(path, fb);
    { bool mo = tank.light_manual_off, la = tank.light_auto, ni = tank.night;      /* lights out: over the dark tank */
      tank.light_auto = false; tank.light_manual_off = true; tank.night = true;
      render_tank(&tank, fb, TANK_W); render_notice(&tank, fb, TANK_W, NOTICE_LIGHTS_OUT, -1, 0, 0.6f);
      snprintf(path, sizeof path, "%s_notice_lights.ppm", prefix); write_ppm(path, fb);
      tank.light_manual_off = mo; tank.light_auto = la; tank.night = ni; }
    /* the battery (2026-09-24): the pill with a card in each state (clock
       0.65 = the charging sweep mid-fill), then the page on battery, charging,
       full - its numbers from a staged history */
    { static const struct { const char *name; float frac; int state; } PILL[] = {
          { "on_battery", 0.63f, BAT_ON_BATTERY }, { "charging", 0.63f, BAT_CHARGING }, { "full", 1.0f, BAT_FULL },
          { "plugged", 0.82f, BAT_PLUGGED }, { "low", 0.08f, BAT_ON_BATTERY }, { "low_charging", 0.08f, BAT_CHARGING } };
      for (size_t i = 0; i < sizeof PILL / sizeof PILL[0]; i++) {
          render_tank(&tank, fb, TANK_W); render_stats_card(&tank, 0, fb, TANK_W);
          render_battery(fb, TANK_W, PILL[i].frac, PILL[i].state, 0.65f);
          snprintf(path, sizeof path, "%s_battery_pill_%s.ppm", prefix, PILL[i].name); write_ppm(path, fb);
      }
      const int64_t now = 1790000000;
      bat_t b; battery_init(&b, NULL);
      b.h.since_pct = 100; b.h.on_power = 0; b.h.since_unix = now - (3 * 3600 + 20 * 60);   /* unplugged 3 h 20 m ago ... */
      b.h.awake_s = 70 * 60; b.h.drop_pct = 52; b.h.drain_x10 = 430;                        /* ... 1 h 10 m of it awake */
      bat_info_t bi;
      battery_info(&b, now, 48, 3790, BAT_ON_BATTERY, &bi);
      render_tank(&tank, fb, TANK_W); render_battery_info(fb, TANK_W, &bi, 0.65f);
      snprintf(path, sizeof path, "%s_battery_page.ppm", prefix); write_ppm(path, fb);
      b.h.on_power = 1; b.h.since_unix = now - 25 * 60; b.h.awake_s = 25 * 60; b.h.drop_pct = 0;
      battery_info(&b, now, 71, 4040, BAT_CHARGING, &bi);
      render_tank(&tank, fb, TANK_W); render_battery_info(fb, TANK_W, &bi, 0.65f);
      snprintf(path, sizeof path, "%s_battery_page_charging.ppm", prefix); write_ppm(path, fb);
      b.h.since_unix = now - (2 * 3600 + 40 * 60);
      battery_info(&b, now, 100, 4180, BAT_FULL, &bi);
      render_tank(&tank, fb, TANK_W); render_battery_info(fb, TANK_W, &bi, 0.65f);
      snprintf(path, sizeof path, "%s_battery_page_full.ppm", prefix); write_ppm(path, fb); }
    render_tank(&tank, fb, TANK_W); render_confirm_reset(fb, TANK_W, 0.7f);
    snprintf(path, sizeof path, "%s_confirm.ppm", prefix); write_ppm(path, fb);
    /* the first-run setup, page by page (never BEGIN: that would save this
       staged tank over the real one) */
    setup_begin(&tank);
    static const char *const pg_name[SETUP_PG_N] = { "welcome", "bubbles", "name", "look", "name2", "look2", "care" };
    for (int pg = 0; pg < SETUP_PG_N; pg++) {
        if (pg == SETUP_PG_BUBBLES) { setup_touch(&tank, 300, 200, true); setup_touch(&tank, 300, 200, false); }
        if (pg == SETUP_PG_NAME_A) { tank_set_name(&tank, 0, "BUB"); setup_activate(&tank, SETUP_HIT_SLOT0 + 1); }
        if (pg == SETUP_PG_LOOK_A) setup_activate(&tank, SETUP_HIT_BODY0 + 6);
        setup_touch(&tank, 0, 0, false);                   /* the stage is set; let the fish get there */
        for (int i = 0; i < 300; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, 1.0f);
        snprintf(path, sizeof path, "%s_setup_%s.ppm", prefix, pg_name[pg]); write_ppm(path, fb);
        if (pg == SETUP_PG_NAME_A) {                       /* the two rejected keyboards (setup.h), typed through their own hit tests */
            for (int kb = SETUP_KBD_GRID; kb <= SETUP_KBD_PAGES; kb++) {
                setup_set_keyboard(kb);
                float kx = kb == SETUP_KBD_GRID ? 32 + 17 + 3 * 50 + 23 : 60 + 3 * 72 + 33, ky = kb == SETUP_KBD_GRID ? 16 + 116 + 17 : 16 + 76 + 37;   /* the fourth key of the top row: D */
                setup_touch(&tank, kx, ky, true); setup_touch(&tank, kx, ky, false);
                if (strcmp(tank.fish[0].name, "d")) printf("WARNING: keyboard %d typed '%s', not 'd'\n", kb, tank.fish[0].name);
                render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, 1.0f);
                snprintf(path, sizeof path, "%s_setup_kbd_%s.ppm", prefix, kb == SETUP_KBD_GRID ? "grid" : "pages"); write_ppm(path, fb);
            }
            setup_set_keyboard(SETUP_KBD_WHEEL); tank_set_name(&tank, 0, "BUB");
        }
        if (pg + 1 < SETUP_PG_N) setup_activate(&tank, SETUP_HIT_NEXT);
    }
    setup_cancel(&tank);
    /* the birth flow, page by page (never DONE: it saves): the last fish as
       a fry just hatched in the reef bed's grass, born to fish 0 and 1
       (tank_add_fish above gave it their colours) */
    {
        int nb = tank.n_fish - 1;
        tank.fish[nb].stage = STAGE_FRY; tank.fish[nb].size = tank.fish[nb].base_size * 0.55f;
        tank.fish[nb].x = tank.reef_x + 70; tank.fish[nb].y = TANK_BOT - 34;
        setup_begin_birth(&tank, nb);
        static const char *const bpg_name[SETUP_BIRTH_PAGES] = { "born", "name_new", "family" };
        for (int pg = 0; pg < SETUP_BIRTH_PAGES; pg++) {
            setup_touch(&tank, 0, 0, false);
            if (setup_page() == SETUP_PG_NAME_NEW) for (int i = 0; i < 300; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);   /* let the fry reach the stage */
            render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, 1.0f);
            snprintf(path, sizeof path, "%s_setup_%s.ppm", prefix, bpg_name[pg]); write_ppm(path, fb);
            if (pg + 1 < SETUP_BIRTH_PAGES) setup_activate(&tank, SETUP_HIT_NEXT);
        }
        setup_cancel(&tank);
    }
    /* the NEW FRY row (2026-09-14): a young pair part way to its first
       arrival - the page, then the TRUST gate's modal */
    {
        tank_init(&tank, 2024); tank_new_population(&tank);
        tank.fish[0].trust = 4.1f; tank.fish[1].trust = 5.2f;
        tank.player_feedings = 7; tank.hold_approaches = 1;
        tank_veg_set(&tank, 0, 0.5f); tank_veg_set(&tank, 1, 0.2f); tank_veg_set(&tank, 2, VEG_NUB);
        for (int i = 0; i < tank.n_fish; i++) tank.fish[i].ms_seen = tank.fish[i].ms_bits;
        tank.tank_ms_seen = tank.tank_ms_bits;
        render_milestones(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_milestones_fry.ppm", prefix); write_ppm(path, fb);
#ifdef CONFIG_POCKET_TANK_320X240
        render_milestones_tap(&tank, PG_X(MS_BADGE_X(0)), PG_Y(MS_ROW(2) + MS_BADGE_DY));   /* (this fork) the CYD's rows */
#else
        render_milestones_tap(&tank, PAGE_X + 176 + 16, PAGE_Y + 4 + 2 * 40 + 20);        /* the TRUST gate -> its modal */
#endif
        render_milestones(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_fry_modal.ppm", prefix); write_ppm(path, fb);
#ifdef CONFIG_POCKET_TANK_320X240
        render_milestones_tap(&tank, PG_X(MS_HOW_X + MSP_HOW_W / 2), PG_Y(MS_HOW_Y + MSP_HOW_H / 2));   /* (this fork) the scaled modal's HOW? */
#else
        render_milestones_tap(&tank, PAGE_X + 56 + 336 / 2, PAGE_Y + 60 + 156 + 20 + 24 + 32 + 14 - 10 - 16);   /* HOW? -> the tip page */
#endif
        render_milestones(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_fry_tip.ppm", prefix); write_ppm(path, fb);
        render_milestones_leave();
#ifdef CONFIG_POCKET_TANK_320X240
        render_milestones_tap(&tank, PG_X(MSP_NAME_X + 8), PG_Y(MS_ROW(2) + MS_NAME_DY));   /* (this fork) the CYD's rows */
#else
        render_milestones_tap(&tank, PAGE_X + 100, PAGE_Y + 4 + 2 * 40 + 10);              /* the name -> the tally */
#endif
        render_milestones(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_fry_tally.ppm", prefix); write_ppm(path, fb);
        render_milestones_leave();
    }
    /* the castle (2026-09-16): bought, IN FRONT of the grass at x 300 with
       beds 1 and 2 growing (the grass stops at its walls) - one fish in the
       arch, one behind the gate wall, one over the towers; the night; then
       BEHIND (a backdrop: the grass and the fish over it); the placement page */
    {
        tank_init(&tank, 2024); tank_new_population(&tank);
        while (tank.n_fish < 4) tank_add_fish(&tank, 0, 1);
        tank.tank_ms_bits = 0x1a7;
        tank_veg_set(&tank, 0, 0.6f); tank_veg_set(&tank, 1, 0.5f); tank_veg_set(&tank, 2, 0.45f);
        for (int i = 0; i < 20 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        tank.sd_unlocks |= SD_ITEM_CASTLE; tank_castle_place(&tank); tank_decor_set(&tank, 2, 300, DECOR_Z_FRONT);
        tank.fish[0].x = 300; tank.fish[0].y = TANK_BOT - 16 - 24; tank.fish[0].heading = 0; tank_fish_face(&tank.fish[0]);
        tank.fish[1].x = 348; tank.fish[1].y = TANK_BOT - 16 - 30; tank.fish[1].heading = 3.14f; tank_fish_face(&tank.fish[1]);
        tank.fish[2].x = 250; tank.fish[2].y = TANK_BOT - 16 - 120; tank.fish[2].heading = 0.3f; tank_fish_face(&tank.fish[2]);
        tank.fish[3].x = 120; tank.fish[3].y = 150; tank.fish[3].heading = 0.1f; tank_fish_face(&tank.fish[3]);
        for (int i = 0; i < 30; i++) render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_castle.ppm", prefix); write_ppm(path, fb);
        tank.night = true; render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_castle_night.ppm", prefix); write_ppm(path, fb); tank.night = false;
        tank_decor_set(&tank, 2, 300, DECOR_Z_BACK); render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_castle_behind.ppm", prefix); write_ppm(path, fb);
        tank_decor_set(&tank, 2, 300, DECOR_Z_FRONT); setup_begin_place(&tank, 2);
        render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, 1.0f);
        snprintf(path, sizeof path, "%s_place_castle.ppm", prefix); write_ppm(path, fb);
        setup_cancel(&tank);
    }
    /* the coral (2026-09-23): bought, at its default spot AMONG the reef
       bed's grass in the reference orange, a fish in front; then IN FRONT in
       violet; then BEHIND; then its placement page with the COLOR row */
    {
        tank.sd_unlocks &= ~SD_ITEM_CASTLE;
        tank.sd_unlocks |= SD_ITEM_CORAL; tank_coral_place(&tank); tank_coral_set_rgb(&tank, CORAL_PAL[0]);
        tank_veg_set(&tank, 0, 0.55f);
        tank.fish[0].x = 150; tank.fish[0].y = TANK_BOT - 16 - 40; tank.fish[0].heading = 0; tank_fish_face(&tank.fish[0]);
        tank.fish[3].x = 190; tank.fish[3].y = TANK_BOT - 16 - 90; tank.fish[3].heading = 3.0f; tank_fish_face(&tank.fish[3]);
        for (int i = 0; i < 30; i++) render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_coral_young.ppm", prefix); write_ppm(path, fb);      /* just bought: CORAL_START */
        tank.coral_growth = 0.72f; render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_coral_half.ppm", prefix); write_ppm(path, fb);       /* a week in */
        tank.coral_growth = 1.0f; render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_coral_fan.ppm", prefix); write_ppm(path, fb);        /* the fan complete, no crown yet */
        tank.coral_growth = CORAL_FULL; render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_coral.ppm", prefix); write_ppm(path, fb);            /* the crown out */
        tank_decor_set(&tank, 3, 150, DECOR_Z_FRONT); tank_coral_set_rgb(&tank, CORAL_PAL[3]);
        render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_coral_front.ppm", prefix); write_ppm(path, fb);
        tank_decor_set(&tank, 3, 150, DECOR_Z_BACK); tank_coral_set_rgb(&tank, CORAL_PAL[1]);
        render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_coral_behind.ppm", prefix); write_ppm(path, fb);
        tank_decor_set(&tank, 3, 150, DECOR_Z_FRONT); tank_coral_set_rgb(&tank, CORAL_PAL[0]); setup_begin_place(&tank, 3);
        render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, 1.0f);
        snprintf(path, sizeof path, "%s_place_coral.ppm", prefix); write_ppm(path, fb);
        setup_cancel(&tank);
    }
    /* the reef cluster (2026-09-24): the day it is bought (REEF, AMONG, the
       coral gone), full size, in bloom; LAGOON in front; DUSK behind; its
       page with the LOOK row; the shop's second page */
    {
        tank.sd_unlocks &= ~SD_ITEM_CORAL;
        tank.sd_unlocks |= SD_ITEM_CLUSTER; tank_cluster_place(&tank); tank_cluster_set_scheme(&tank, 0);
        tank_veg_set(&tank, 1, 0.5f); tank_veg_set(&tank, 2, 0.45f);
        tank.fish[0].x = 290; tank.fish[0].y = TANK_BOT - 16 - 50; tank.fish[0].heading = 0; tank_fish_face(&tank.fish[0]);
        tank.fish[3].x = 370; tank.fish[3].y = TANK_BOT - 16 - 100; tank.fish[3].heading = 3.0f; tank_fish_face(&tank.fish[3]);
        for (int i = 0; i < 30; i++) render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_cluster_young.ppm", prefix); write_ppm(path, fb);
        tank.cluster_growth = 1.0f; render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_cluster_full.ppm", prefix); write_ppm(path, fb);
        tank.cluster_growth = CLUSTER_FULL; render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_cluster.ppm", prefix); write_ppm(path, fb);
        tank_decor_set(&tank, 4, 330, DECOR_Z_FRONT); tank_cluster_set_scheme(&tank, 1); render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_cluster_front.ppm", prefix); write_ppm(path, fb);
        tank_decor_set(&tank, 4, 330, DECOR_Z_BACK); tank_cluster_set_scheme(&tank, 2); render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_cluster_behind.ppm", prefix); write_ppm(path, fb);
        {   /* the snail and the cluster (0.3.2): walking the floor past it, and on the glass over it */
            uint32_t unl = tank.sd_unlocks; float sx = tank.snail_x, sy = tank.snail_y, sh = tank.snail_heading; int sc = tank.snail_cell;
            tank.sd_unlocks |= SD_ITEM_SNAIL; tank.snail_cell = -1; tank.snail_heading = 0;
            for (int z = 0; z < 2; z++) {
                tank_decor_set(&tank, 4, 330, z ? DECOR_Z_FRONT : DECOR_Z_BACK);
                tank.snail_x = 336; tank.snail_y = SNAIL_FLOOR_Y; tank.snail_front = false;   /* at its foot, in its back lane */
                render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
                snprintf(path, sizeof path, "%s_cluster_snail_%s.ppm", prefix, z ? "front" : "behind"); write_ppm(path, fb);
                tank.snail_front = true;                                             /* ... and in its front lane: over a piece IN FRONT */
                render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
                snprintf(path, sizeof path, "%s_cluster_snail_%s_lane.ppm", prefix, z ? "front" : "behind"); write_ppm(path, fb);
                tank.snail_x = 330; tank.snail_y = SNAIL_FLOOR_Y - 40;
                render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
                snprintf(path, sizeof path, "%s_cluster_snail_glass_%s.ppm", prefix, z ? "front" : "behind"); write_ppm(path, fb);
            }
            tank.sd_unlocks = unl; tank.snail_x = sx; tank.snail_y = sy; tank.snail_heading = sh; tank.snail_cell = (int16_t)sc;
        }
        tank_decor_set(&tank, 4, 330, DECOR_Z_FRONT); tank_cluster_set_scheme(&tank, 0); setup_begin_place(&tank, 4);
        render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, 1.0f);
        snprintf(path, sizeof path, "%s_place_cluster.ppm", prefix); write_ppm(path, fb);
        setup_cancel(&tank);
        tank.sd_unlocks |= SD_ITEM_CASTLE; tank_castle_place(&tank);
#ifdef CONFIG_POCKET_TANK_320X240
        render_shop_leave(); render_shop(&tank, fb, TANK_W); render_shop_tap(&tank, PG_X(SHOP_ROW_X), PG_Y(SHOP_ROW_Y(2)));
        render_shop(&tank, fb, TANK_W); render_shop_tap(&tank, PG_X(SHOP_SELL_X), PG_Y(SHOP_BTN_Y));   /* (this fork) SELL, armed, in the CYD's modal */
#else
        render_shop_leave(); render_shop(&tank, fb, TANK_W); render_shop_tap(&tank, PAGE_X + 100, PAGE_Y + 98 + 2 * 56 + 20);
        render_shop(&tank, fb, TANK_W); render_shop_tap(&tank, PAGE_X + 48 + (352 - 216) / 2 + 116 + 50, PAGE_Y + 48 + 244 - 12 - 16);   /* SELL, armed */
#endif
        render_shop(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_shop_sell.ppm", prefix); write_ppm(path, fb);
        render_shop_leave();
        setup_begin_place(&tank, 2); setup_activate(&tank, SETUP_HIT_SELL);   /* the page, SELL armed */
        render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, 1.0f);
        snprintf(path, sizeof path, "%s_place_sell.ppm", prefix); write_ppm(path, fb);
        setup_cancel(&tank); tank.sd_unlocks &= ~SD_ITEM_CASTLE;
        render_shop_leave(); render_shop(&tank, fb, TANK_W);
#ifdef CONFIG_POCKET_TANK_320X240
        render_shop_tap(&tank, PG_X(SHOP_NEXT_X), PG_Y(SHOP_ARROW_Y));                 /* (this fork) the CYD's header arrow */
#else
        render_shop_tap(&tank, PAGE_X + 400, PAGE_Y + 30);                              /* the header's right arrow: page 2 */
#endif
        render_shop(&tank, fb, TANK_W);
        snprintf(path, sizeof path, "%s_shop2.ppm", prefix); write_ppm(path, fb);
        render_shop_leave();
    }
    printf("snapshot: %d fish, wrote %s_{tank,card,card1,milestones,milestones_shrimp{,2},milestones_fry,confirm,setup_*}.ppm\n", tank.n_fish, prefix);
    return 0;
}


/* --selftest-battery (2026-09-24): the battery page's arithmetic - a fresh
 * history, a stretch on battery (screen-on time, the gauge's drop, the
 * estimate), a gauge that bounces, a night asleep that is not screen time,
 * the saves, the cable in (the drain learned) and a charge (the charge rate
 * learned), an edge that happened while the tank was off, a foreign blob,
 * the durations' words, and the pill: its hit box, the bolt on the cable only. */
static int selftest_battery(void) {
    int fails = 0;
#define BCHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
    bat_t b; battery_init(&b, NULL); bat_info_t bi;
    int64_t t = 1790000000;
    BCHECK(battery_tick(&b, t, 0, 90, BAT_ON_BATTERY) == 0, "a first boot on battery is no cable edge");
    BCHECK(battery_take_save(&b), "the first stretch is saved at once");
    float pct = 90;                                          /* 40 awake min at 45 %/h */
    for (int s = 0; s < 40 * 60; s++) { t++; pct -= 45.0f / 3600; battery_tick(&b, t, 1.0f, (int)(pct + 0.5f), BAT_ON_BATTERY); }
    battery_info(&b, t, (int)(pct + 0.5f), 3800, BAT_ON_BATTERY, &bi);
    BCHECK(bi.awake_min == 40 && bi.since_min == 40, "screen on %d / since %d min, want 40 / 40", bi.awake_min, bi.since_min);
    BCHECK(bi.measured && abs(bi.left_min - 80) <= 10, "60%% at ~45 %%/h: %d min left (measured %d), want ~80", bi.left_min, bi.measured);
    BCHECK(abs(bi.life_min - 133) <= 15, "a full charge %d min, want ~133", bi.life_min);
    printf("selftest-battery: 40 min on battery 90 -> %d%%: screen on %d min, ~%d min left, a full charge ~%d min\n", (int)(pct + 0.5f), bi.awake_min, bi.left_min, bi.life_min);
    int d0 = b.h.drop_pct;                                   /* the gauge bounces 60/59/60/59: 1%% */
    static const int BOUNCE[] = { 60, 59, 60, 59, 60, 59 };
    for (int i = 0; i < 6; i++) battery_tick(&b, ++t, 1.0f, BOUNCE[i], BAT_ON_BATTERY);
    BCHECK(b.h.drop_pct - d0 == 1, "a bouncing gauge counted %d%%, want 1", b.h.drop_pct - d0);
    uint32_t aw = b.h.awake_s; int d1 = b.h.drop_pct;        /* 8 h asleep, 2%% lost: not screen time, not screen drain */
    battery_woke(&b); t += 8 * 3600;
    battery_tick(&b, t, 0, 57, BAT_ON_BATTERY);
    battery_info(&b, t, 57, 3780, BAT_ON_BATTERY, &bi);
    BCHECK(b.h.awake_s == aw && b.h.drop_pct == d1, "the night counted: awake +%u s, drop +%d", b.h.awake_s - aw, b.h.drop_pct - d1);
    BCHECK(bi.since_min > 8 * 60 && bi.awake_min < 60, "after the night: since %d, screen on %d", bi.since_min, bi.awake_min);
    battery_take_save(&b);                                   /* the saves: every 10 awake minutes (20 more min at 45 %/h) */
    int saves = 0; pct = 57;
    for (int s = 0; s < 1200; s++) { pct -= 45.0f / 3600; battery_tick(&b, ++t, 1.0f, (int)(pct + 0.5f), BAT_ON_BATTERY); saves += battery_take_save(&b); }
    BCHECK(saves == 2, "20 awake min asked for %d saves, want 2", saves);
    int plug = (int)(pct + 0.5f);
    BCHECK(battery_tick(&b, ++t, 1.0f, plug, BAT_CHARGING) == 1, "the cable in is +1");
    BCHECK(b.h.drain_x10 >= 400 && b.h.drain_x10 <= 500, "the drain learned %.1f %%/h, want ~45", b.h.drain_x10 / 10.0);
    BCHECK(battery_take_save(&b), "a cable edge is saved");
    float cp = plug;                                         /* charging at 80 %/h */
    for (int s = 0; s < 15 * 60; s++) { t++; cp += 80.0f / 3600; battery_tick(&b, t, 1.0f, (int)(cp + 0.5f), BAT_CHARGING); }
    battery_info(&b, t, (int)(cp + 0.5f), 4050, BAT_CHARGING, &bi);
    int want = (int)((100 - cp) / 80 * 60);
    BCHECK(bi.since_min == 15 && abs(bi.left_min - want) <= 6, "charging at %d%%: since %d, full in %d, want ~%d", (int)(cp + 0.5f), bi.since_min, bi.left_min, want);
    BCHECK(bi.awake_min < 0, "no screen-on row on the cable");
    printf("selftest-battery: plugged in at %d%%: the drain learned %.1f %%/h; 15 min later %d%%, full in ~%d min\n", plug, b.h.drain_x10 / 10.0, (int)(cp + 0.5f), bi.left_min);
    while (cp < 100) { t++; cp += 80.0f / 3600; battery_tick(&b, t, 1.0f, cp >= 99.5f ? 100 : (int)(cp + 0.5f), BAT_CHARGING); }
    BCHECK(battery_tick(&b, ++t, 1.0f, 100, BAT_FULL) == 0, "full is no cable edge");
    BCHECK(b.h.charge_x10 >= 720 && b.h.charge_x10 <= 880, "the charge learned %.1f %%/h, want ~80", b.h.charge_x10 / 10.0);
    battery_info(&b, t, 100, 4180, BAT_FULL, &bi);
    BCHECK(bi.left_min < 0 && bi.life_min > 0, "full: no countdown, a life");
    BCHECK(battery_tick(&b, ++t, 1.0f, 100, BAT_ON_BATTERY) == -1, "unplugged is -1");
    bat_hist_t saved = b.h; bat_t b2;                        /* saved on battery, the cable went in while the tank was off */
    battery_init(&b2, &saved);
    BCHECK(battery_tick(&b2, t + 3600, 0, 64, BAT_CHARGING) == 1, "a plug-in while off is +1 at the boot");
    BCHECK(b2.h.drain_x10 == saved.drain_x10, "a stretch with no screen time taught nothing");
    bat_hist_t junk; memset(&junk, 0x5a, sizeof junk); bat_t b3;
    battery_init(&b3, &junk);
    BCHECK(b3.h.since_pct == -1 && b3.h.drain_x10 == 0, "a blob that is not ours starts fresh");
    char d[16];
    static const struct { int min; const char *want; } DUR[] = { { 0, "0M" }, { 45, "45M" }, { 120, "2H" }, { 135, "2H 15M" }, { 1500, "1D 1H" }, { 2880, "2D" } };
    for (size_t i = 0; i < sizeof DUR / sizeof DUR[0]; i++) { battery_fmt_dur(d, sizeof d, DUR[i].min); BCHECK(!strcmp(d, DUR[i].want), "%d min reads %s, want %s", DUR[i].min, d, DUR[i].want); }
    BCHECK(RENDER_BAT_HIT(RENDER_BAT_X + 10, RENDER_BAT_Y + 5) && RENDER_BAT_HIT(RENDER_BAT_X - 30, RENDER_BAT_Y + 45), "the pill and a fingertip below-left of it");
    BCHECK(!RENDER_BAT_HIT(TANK_W / 2, TANK_H / 2) && !RENDER_BAT_HIT(RENDER_CARD_X + 60, 20) && !RENDER_BAT_HIT(RENDER_BAT_X, RENDER_BAT_Y + RENDER_BAT_H + 67), "not the water, the card or below the slop");
    static uint16_t fb[TANK_W * TANK_H];                     /* the bolt: left of the pill on the cable, never on battery */
    int bolt_px[4];
    for (int st = 0; st < 4; st++) {
        for (int i = 0; i < TANK_W * TANK_H; i++) fb[i] = 0x4208;
        render_battery(fb, TANK_W, 0.5f, st, 0.65f);
        bolt_px[st] = 0;
        for (int y = 0; y < RENDER_BAT_Y + RENDER_BAT_H + 17; y++) for (int x = RENDER_BAT_X - 30; x < RENDER_BAT_X - 1; x++) bolt_px[st] += fb[y * TANK_W + x] != 0x4208;   /* the pill's rows and a bolt's overhang */
    }
    BCHECK(bolt_px[BAT_ON_BATTERY] == 0 && bolt_px[BAT_CHARGING] > 60 && bolt_px[BAT_FULL] > 60 && bolt_px[BAT_PLUGGED] > 60,
           "bolt pixels on battery %d, charging %d, full %d, resting %d", bolt_px[0], bolt_px[1], bolt_px[2], bolt_px[3]);
    uint16_t a[RENDER_BAT_W], z[RENDER_BAT_W]; int moved = 0;   /* the sweep moves while charging, only then */
    for (int st = 0; st < 3; st += 2) {
        for (int i = 0; i < TANK_W * TANK_H; i++) fb[i] = 0x4208;
        render_battery(fb, TANK_W, 0.9f, st ? BAT_FULL : BAT_CHARGING, 0.3f); memcpy(a, fb + (RENDER_BAT_Y + 7) * TANK_W + RENDER_BAT_X, sizeof a);
        for (int i = 0; i < TANK_W * TANK_H; i++) fb[i] = 0x4208;
        render_battery(fb, TANK_W, 0.9f, st ? BAT_FULL : BAT_CHARGING, 0.9f); memcpy(z, fb + (RENDER_BAT_Y + 7) * TANK_W + RENDER_BAT_X, sizeof z);
        if (memcmp(a, z, sizeof a)) moved |= st ? 2 : 1;
    }
    BCHECK(moved == 1, "the sweep: charging %s, full %s", moved & 1 ? "moves" : "STILL", moved & 2 ? "MOVES" : "still");
    printf("selftest-battery: charged in ~%.0f min (learned %.1f %%/h); the bolt %d px on the cable, none on battery; the sweep only while charging\n",
           (100 - plug) / (b.h.charge_x10 / 10.0) * 60, b.h.charge_x10 / 10.0, bolt_px[BAT_CHARGING]);
#undef BCHECK
    if (!fails) printf("selftest-battery: ok\n");
    return fails ? 1 : 0;
}

/* --selftest-shop (2026-09-15): the sand dollars. A fresh tank has none; a
 * feeding pays only once somebody eats from it; stages, a birth and full
 * trust pay once each, and a save round-trip never pays again; the two chore
 * counters (colonies wiped, grass cut) pay every hundred / 250 cm; the shop refuses
 * a short balance, the plant becomes bed 3 (the slash cuts it, the comfort
 * band counts it), the snail grazes without touching the keeper's counts;
 * the page's taps; the save carries all of it; a pre-shop save back-pays. */
static int selftest_shop(void) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest.sav", 1);
    char cmd[600]; snprintf(cmd, sizeof cmd, "rm -f /tmp/pocket-tank-selftest.sav"); (void)system(cmd);
    tank_init(&tank, 4242);
    progression_boot(&tank);
    progression_setup_done(&tank);
    tank.trickle_off = true;
    int want = 0;
#define SHOP_TICK(n) for (int i_ = 0; i_ < (n); i_++) { tank_tick(&tank, 1.0f / 60.0f, advisor_rules); progression_tick(&tank, 1.0f / 60.0f); }
#define SHOP_WANT(msg) do { if (tank.sd_balance != want) { printf("FAIL: %s: balance %d, wanted %d\n", msg, tank.sd_balance, want); return 1; } } while (0)
    SHOP_TICK(2); SHOP_WANT("a fresh tank");
    /* a feeding nobody eats from pays nothing; the first bite makes it a meal */
    for (int i = 0; i < tank.n_fish; i++) { tank.fish[i].hunger = 0.5f; tank.fish[i].x = 380; tank.fish[i].y = 300; }
    tank_feed(&tank, 60, 3);
    SHOP_TICK(60); SHOP_WANT("an uneaten feeding");
    for (int i = 0; i < MAX_FOOD; i++) tank.food[i].alive = false;
    tank_feed(&tank, 220, 3);
    tank.fish[0].hunger = 9.5f; tank.fish[0].x = 220; tank.fish[0].y = 14;
    SHOP_TICK(60 * 20);
    if (tank.player_feedings != 1) { printf("FAIL: the eaten feeding did not count as a meal (%d)\n", tank.player_feedings); return 1; }
    want += SD_MEAL; SHOP_WANT("a meal");
    printf("selftest-shop: an uneaten feeding paid nothing, the eaten one paid %d\n", SD_MEAL);
    /* stages: paid once each, in order, and never again after a save round-trip */
    progression_set_age(&tank, 0, STAGE_JUV_AGE + 1);   SHOP_TICK(2); want += SD_STAGE_JUV;   SHOP_WANT("juvenile");
    progression_set_age(&tank, 0, STAGE_ADULT_AGE + 1); SHOP_TICK(2); want += SD_STAGE_ADULT; SHOP_WANT("adult");
    progression_set_age(&tank, 0, STAGE_ELDER_AGE + 1); SHOP_TICK(2); want += SD_STAGE_ELDER; SHOP_WANT("elder");
    progression_set_age(&tank, 1, STAGE_ADULT_AGE + 1); SHOP_TICK(2); want += SD_STAGE_JUV + SD_STAGE_ADULT; SHOP_WANT("fish 1 adult");
    progression_save(&tank);
    tank_init(&tank, 4242); progression_boot(&tank); tank.trickle_off = true;
    SHOP_TICK(2); SHOP_WANT("stages after a save round-trip");
    if (tank.sd_earned != want) { printf("FAIL: earned %d, wanted %d\n", tank.sd_earned, want); return 1; }
    /* a birth */
    tank_veg_set(&tank, 0, 0.5f);
    progression_force_arrival(&tank);
    SHOP_TICK(2); want += SD_BIRTH; SHOP_WANT("a birth");
    if (tank.n_fish != 3) { printf("FAIL: no fry arrived\n"); return 1; }
    /* full trust, once */
    tank.fish[1].trust = 10.0f; SHOP_TICK(2); want += SD_TRUST; SHOP_WANT("full trust");
    tank.fish[1].trust = 6.0f; SHOP_TICK(2); tank.fish[1].trust = 10.0f; SHOP_TICK(2); SHOP_WANT("full trust again");
    printf("selftest-shop: stages %d/%d/%d, a birth %d, full trust %d - each once, %d after the reload\n",
           SD_STAGE_JUV, SD_STAGE_ADULT, SD_STAGE_ELDER, SD_BIRTH, SD_TRUST, tank.sd_balance);
    /* colonies: three 2 x 2 patches, one stroke through each */
    {
        memset(tank.algae, 0, sizeof tank.algae);
#ifdef CONFIG_POCKET_TANK_320X240
        int c0[3] = { 3, 9, 15 };                 /* (this fork) the CYD's glass is 20 cells wide: column 21 is off it (and
                                                     wrapped onto the next row, it joined patch 0) */
#else
        int c0[3] = { 3, 12, 21 };
#endif
        for (int k = 0; k < 3; k++) for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) tank.algae[(5 + dy) * ALGAE_COLS + c0[k] + dx] = 120;
        int before = tank.algae_colonies;
        for (int k = 0; k < 3; k++) {
            float cx = (c0[k] + 1) * ALGAE_CELL, cy = 6 * ALGAE_CELL;
            for (float sx = cx - 30; sx <= cx + 30; sx += 4) tank_touch_drag(&tank, sx, cy);
            SHOP_TICK(2);
            if (tank.algae_colonies != before + k + 1) { printf("FAIL: patch %d wiped, colonies %d (wanted %d)\n", k, tank.algae_colonies, before + k + 1); return 1; }
        }
        /* half a patch is no colony */
        for (int dy = 0; dy < 4; dy++) tank.algae[(8 + dy) * ALGAE_COLS + 10] = 120;
        for (float sy = 8 * ALGAE_CELL - 20; sy <= 9 * ALGAE_CELL + 8; sy += 4) tank_touch_drag(&tank, 10 * ALGAE_CELL + 8, sy);
        SHOP_TICK(2);
        if (tank.algae_colonies != before + 3) { printf("FAIL: a half-wiped patch counted (%d)\n", tank.algae_colonies); return 1; }
        memset(tank.algae, 0, sizeof tank.algae);
        /* the hundredth pays */
        tank.algae_colonies = SD_CHORE_EVERY - 1; tank.sd_colonies_paid = 0;
        tank.algae[7 * ALGAE_COLS + 14] = 100;
        for (float sx = 14 * ALGAE_CELL - 24; sx <= 14 * ALGAE_CELL + 32; sx += 4) tank_touch_drag(&tank, sx, 7 * ALGAE_CELL + 8);
        SHOP_TICK(2); want += SD_CHORE; SHOP_WANT("100 colonies");
        if (tank.sd_colonies_paid != 1) { printf("FAIL: colonies paid count %d\n", tank.sd_colonies_paid); return 1; }
        printf("selftest-shop: 3 patches = 3 colonies, a half patch none, the 100th paid %d\n", SD_CHORE);
    }
    /* the grass: a full bed mowed to nubs is (1 - nub) x the frond height, per frond */
    {
        tank_veg_set(&tank, 1, 1.0f);
        int n; float x0, x1; tank_veg_bed(&tank, 1, &x0, &x1, NULL, &n);
        tank.trim_px = (SD_TRIM_CM - 100) * PX_PER_CM; tank.sd_inches_paid = 0;
        float px0 = tank.trim_px;
#ifdef CONFIG_POCKET_TANK_320X240
        /* (this fork) on the CYD's floor bed 2's last fronds stand among bed 1's three: at nubs for the sweep, so it mows
           bed 1 alone, and back after */
        float bed2[VEG_FRONDS_MAX]; memcpy(bed2, tank.veg_h[2], sizeof bed2); tank_veg_set(&tank, 2, VEG_NUB);
#endif
        for (float sx = x0 + 2; sx <= x1; sx += 4) tank_touch_drag(&tank, sx, TANK_BOT - 8.0f);
        SHOP_TICK(2);
#ifdef CONFIG_POCKET_TANK_320X240
        memcpy(tank.veg_h[2], bed2, sizeof bed2); tank_veg_sync(&tank);
#endif
        float cut = tank.trim_px - px0, expect = n * (1.0f - VEG_NUB) * (VEG_SEGS_FULL - 1) * VEG_PAY_PX;   /* paid at the 1.8's pitch on every board */
        printf("selftest-shop: mowing bed 1 (%d fronds) cut %.0f px = %.0f cm (expected ~%.0f px); the %dth cm paid %d\n", n, cut, cut / PX_PER_CM, expect, SD_TRIM_CM, SD_CHORE);
        if (fabsf(cut - expect) > expect * 0.1f) { printf("FAIL: the cut is off (%.0f vs %.0f)\n", cut, expect); return 1; }
        want += SD_CHORE; SHOP_WANT("250 cm");
        if (tank.sd_inches_paid != 1) { printf("FAIL: grass paid count %d\n", tank.sd_inches_paid); return 1; }
    }
    /* the shop: a short balance is refused; the plant is bed 3; the snail grazes */
    {
        tank.sd_balance = SD_PRICE_PLANT - 1; want = tank.sd_balance;
        if (progression_buy(&tank, 0)) { printf("FAIL: bought the plant short by one\n"); return 1; }
        if (tank_veg_beds(&tank) != VEG_BEDS) { printf("FAIL: bed count %d before the plant\n", tank_veg_beds(&tank)); return 1; }
        progression_sd_grant(&tank, 1); want += 1;
        if (!progression_buy(&tank, 0)) { printf("FAIL: could not buy the plant at the price\n"); return 1; }
        want -= SD_PRICE_PLANT; SHOP_WANT("after the plant");
        if (progression_buy(&tank, 0)) { printf("FAIL: bought the plant twice\n"); return 1; }
        if (tank_veg_beds(&tank) != VEG_BEDS_MAX || tank_veg_kind(&tank, 3) != VEG_KIND_SWORD) { printf("FAIL: the plant is not bed 3\n"); return 1; }
        if (fabsf(tank.veg_growth[3] - VEG_START) > 0.01f) { printf("FAIL: the plant did not start at VEG_START (%.2f)\n", tank.veg_growth[3]); return 1; }
        int n; float x0, x1; tank_veg_bed(&tank, 3, &x0, &x1, NULL, &n);
        float trim0 = tank.trim_px;
        for (float sx = x0 + 2; sx <= x1; sx += 4) tank_touch_drag(&tank, sx, TANK_BOT - 8.0f);
        SHOP_TICK(2);
        if (fabsf(tank.veg_growth[3] - VEG_NUB) > 1e-3f || tank.trim_px <= trim0) { printf("FAIL: the slash did not mow the plant (%.2f)\n", tank.veg_growth[3]); return 1; }
        /* the comfort band counts it: with every grass bed bare, the plant alone is cover */
        for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(&tank, b, VEG_NUB);
        tank_veg_set(&tank, 3, 0.5f);
        tank.fish[0].stress = 5; tank.fish[0].hunger = 2; tank.fish[0].goal.id = GOAL_EXPLORE;
        SHOP_TICK(60 * 20);
        float s_plant = tank.fish[0].stress;
        tank_veg_set(&tank, 3, VEG_NUB); tank.fish[0].stress = 5;
        SHOP_TICK(60 * 20);
        printf("selftest-shop: plant bought (%d fronds at x %.0f..%.0f), mowed by a sweep; stress with the plant alone %.2f, scalped %.2f\n", n, x0, x1, s_plant, tank.fish[0].stress);
        if (s_plant >= tank.fish[0].stress) { printf("FAIL: the plant gave no cover\n"); return 1; }
        /* the snail */
        progression_sd_grant(&tank, SD_PRICE_SNAIL); want = tank.sd_balance;
        if (!progression_buy(&tank, 1)) { printf("FAIL: could not buy the snail\n"); return 1; }
        want -= SD_PRICE_SNAIL; SHOP_WANT("after the snail");
        if (!(tank.sd_unlocks & SD_ITEM_SNAIL) || tank.snail_x < 0) { printf("FAIL: no snail on the glass\n"); return 1; }
        memset(tank.algae, 0, sizeof tank.algae);
#ifdef CONFIG_POCKET_TANK_320X240
        const int patch_row = 10;                  /* (this fork) the CYD's glass is 15 cells tall: row 18 is under it */
#else
        const int patch_row = 18;
#endif
        for (int dy = 0; dy < 3; dy++) for (int dx = 0; dx < 3; dx++) tank.algae[(patch_row + dy) * ALGAE_COLS + 4 + dx] = 200;
        int cells0 = 0, film0 = 0; for (int i = 0; i < ALGAE_CELLS; i++) { cells0 += tank.algae[i] > 0; film0 += tank.algae[i]; }
        int cleaned0 = tank.cells_cleaned, col0 = tank.algae_colonies;
        SHOP_TICK(60 * 90);
        int cells1 = 0, film1 = 0; for (int i = 0; i < ALGAE_CELLS; i++) { cells1 += tank.algae[i] > 0; film1 += tank.algae[i]; }
        printf("selftest-shop: snail: a 9-cell patch (%d film) -> %d cells (%d film) after 90 s, at %.0f,%.0f; keeper's counts %d/%d unchanged\n",
               film0, cells1, film1, tank.snail_x, tank.snail_y, tank.cells_cleaned - cleaned0, tank.algae_colonies - col0);
        if (cells1 >= cells0 || film1 >= film0 * 0.8f) { printf("FAIL: the snail did not graze\n"); return 1; }
        if (tank.cells_cleaned != cleaned0 || tank.algae_colonies != col0) { printf("FAIL: the snail's grazing counted as the keeper's\n"); return 1; }
        /* its own tally (2026-09-16, the snail's card): cells eaten clean */
        if (tank.snail_grazed < 1 || tank.snail_grazed > 9) { printf("FAIL: the snail's tally is %d after a 9-cell patch\n", (int)tank.snail_grazed); return 1; }
        {   /* a tap on it opens the card; the sprite's centre and a fingertip off both hit, 70 px off does not */
            if (!tank_snail_hit(&tank, tank.snail_x, tank.snail_y) || !tank_snail_hit(&tank, tank.snail_x + 24, tank.snail_y - 12)
                || tank_snail_hit(&tank, tank.snail_x + 70, tank.snail_y)) { printf("FAIL: the snail's hit test\n"); return 1; }
            static uint16_t cfb[TANK_W * TANK_H];
            render_tank(&tank, cfb, TANK_W); render_stats_card(&tank, RENDER_CARD_SNAIL, cfb, TANK_W);
            const int cx0 = (TANK_W - SNAIL_CARD_W) / 2, cy0 = (TANK_H - SNAIL_CARD_H) / 2;      /* the card: centered on the glass */
            int lit = 0; for (int y = cy0; y < cy0 + SNAIL_CARD_H; y += 4) for (int x = cx0; x < cx0 + SNAIL_CARD_W; x += 4) lit += cfb[y * TANK_W + x] == 0xffff;   /* white in RGB565 */
            if (lit < 20) { printf("FAIL: the snail's card drew no white text (%d)\n", lit); return 1; }
            printf("selftest-shop: snail card: %d spots grazed, the tap hits, the card draws\n", (int)tank.snail_grazed);
        }
        /* the night shift: the same night with and without the snail (sleep
           grows film too, so the twin is the yardstick) */
        tank_grow_algae(&tank, 40);
        int cn1 = 0; for (int i = 0; i < ALGAE_CELLS; i++) cn1 += tank.algae[i] > 0;
        static tank_t twin; twin = tank; twin.sd_unlocks &= ~SD_ITEM_SNAIL;
        tank_tick_sleep(&tank, 3600); tank_tick_sleep(&twin, 3600);
        int cn2 = 0, cn3 = 0; for (int i = 0; i < ALGAE_CELLS; i++) { cn2 += tank.algae[i] > 0; cn3 += twin.algae[i] > 0; }
        printf("selftest-shop: snail asleep: %d cells -> %d after an hour's sleep with it, %d without\n", cn1, cn2, cn3);
        if (cn2 >= cn3) { printf("FAIL: the snail slept through the night shift\n"); return 1; }
        /* the two poses: with the glass clean it comes down to the floor and
           walks it upright, turning at the ends; a target puts it back on the glass */
        memset(tank.algae, 0, sizeof tank.algae); tank.snail_cell = -1; tank.snail_x = 200; tank.snail_y = SNAIL_FLOOR_Y - 224; tank.snail_heading = 0;   /* 224 px up the glass: 56 s of crawl to the floor, however tall the tank */
        SHOP_TICK(60 * 5);
        if (tank_snail_upright(&tank)) { printf("FAIL: upright while still coming down the glass (y %.0f)\n", tank.snail_y); return 1; }
        SHOP_TICK(60 * 60);
        float fx = tank.snail_x;
        if (!tank_snail_upright(&tank) || fabsf(tank.snail_y - SNAIL_FLOOR_Y) > 0.5f) { printf("FAIL: not on the floor after a minute (y %.0f)\n", tank.snail_y); return 1; }
        SHOP_TICK(60 * 120);
        if (tank.snail_x == fx) { printf("FAIL: the floor walk did not move\n"); return 1; }
        tank.algae[3 * ALGAE_COLS + 20] = 150;
        SHOP_TICK(30);
        if (tank_snail_upright(&tank) || tank.snail_y >= SNAIL_FLOOR_Y - 1) { printf("FAIL: film on the glass did not lift it off the floor (y %.0f)\n", tank.snail_y); return 1; }
        printf("selftest-shop: snail poses: down the glass flat, then upright along the floor (x %.0f -> %.0f), back onto the glass for film\n", fx, tank.snail_x);
        /* the urchin (2026-10-02): the seventh item at its price, on the floor;
           a bite's appetite takes it to the tallest grass frond, which comes
           down a bite - not the sword plant, not the keeper's trim counts;
           its tally, its tap, its card; asleep, the grass over the keep line */
        tank.sd_balance = SD_PRICE_URCHIN - 1;
        if (progression_buy(&tank, 6)) { printf("FAIL: the urchin sold short\n"); return 1; }
        tank.sd_balance = SD_PRICE_URCHIN; want = 0;
        if (SD_ITEMS[6].bit != SD_ITEM_URCHIN || !progression_buy(&tank, 6)) { printf("FAIL: could not buy the urchin\n"); return 1; }
        SHOP_WANT("after the urchin");
        if (!(tank.sd_unlocks & SD_ITEM_URCHIN) || tank.urchin_x < 0) { printf("FAIL: no urchin on the floor\n"); return 1; }
        if (progression_sell(&tank, 6)) { printf("FAIL: the urchin sold back\n"); return 1; }
        float veg_was[VEG_BEDS_MAX][VEG_FRONDS_MAX]; memcpy(veg_was, tank.veg_h, sizeof veg_was);   /* put back after: the tests below place the plant among the beds */
        for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(&tank, b, 0.45f);
#ifdef CONFIG_POCKET_TANK_320X240
        const int tf = 2;                                     /* (this fork) the CYD's bed 1 has three fronds: slot 3 is not one */
#else
        const int tf = 3;
#endif
        tank_veg_set(&tank, 3, 0.70f);                        /* the sword plant tall: not its food */
        tank.veg_h[1][tf] = 0.80f; tank_veg_sync(&tank);
        float tf0, sword0 = tank.veg_h[3][1]; tank_veg_frond(&tank, 1, tf, &tf0);
        int trims0 = tank.trims; float tpx0 = tank.trim_px, ux0 = tank.urchin_x;
        tank.urchin_appetite = 0.05f; tank.urchin_rest = 0;
        SHOP_TICK(60 * 300);
        float h13 = tank.veg_h[1][tf];
        printf("selftest-shop: urchin: from x %.0f to the tall frond at %.0f (now x %.0f): 0.80 -> %.3f, %.0f px eaten; the sword plant %.3f -> %.3f\n",
               ux0, tf0, tank.urchin_x, h13, tank.urchin_grazed_px, sword0, tank.veg_h[3][1]);
        if (h13 > 0.78f || h13 < 0.74f) { printf("FAIL: the urchin did not eat a bite of the tallest frond\n"); return 1; }
        if (tank.veg_h[3][1] < sword0 - 1e-4f) { printf("FAIL: the urchin ate the sword plant\n"); return 1; }
        if (tank.trims != trims0 || tank.trim_px != tpx0) { printf("FAIL: the urchin's grazing counted as the keeper's trimming\n"); return 1; }
        if (tank.urchin_grazed_px < 10) { printf("FAIL: the urchin's tally %.0f px\n", tank.urchin_grazed_px); return 1; }
        for (int b = 0; b < VEG_BEDS; b++) for (int i = 0; i < VEG_FRONDS_MAX; i++)
            if (tank.veg_h[b][i] > 0 && tank.veg_h[b][i] < URCHIN_KEEP - 1e-4f) { printf("FAIL: bed %d frond %d under the keep line\n", b, i); return 1; }
        {   /* the tap and the card */
            if (!tank_urchin_hit(&tank, tank.urchin_x, URCHIN_FLOOR_Y) || !tank_urchin_hit(&tank, tank.urchin_x + 20, URCHIN_FLOOR_Y - 20)
                || tank_urchin_hit(&tank, tank.urchin_x + 70, URCHIN_FLOOR_Y)) { printf("FAIL: the urchin's hit test\n"); return 1; }
            static uint16_t cfb[TANK_W * TANK_H];
            render_tank(&tank, cfb, TANK_W); render_stats_card(&tank, RENDER_CARD_URCHIN, cfb, TANK_W);
            const int cx0 = (TANK_W - URCHIN_CARD_W) / 2, cy0 = (TANK_H - URCHIN_CARD_H) / 2;
            int lit = 0; for (int y = cy0; y < cy0 + URCHIN_CARD_H; y += 4) for (int x = cx0; x < cx0 + URCHIN_CARD_W; x += 4) lit += cfb[y * TANK_W + x] == 0xffff;
            if (lit < 20) { printf("FAIL: the urchin's card drew no white text (%d)\n", lit); return 1; }
            int purple = 0;                                   /* the urchin itself, drawn in the tank */
            render_tank(&tank, cfb, TANK_W);
            for (int y = (int)URCHIN_FLOOR_Y - 14; y <= (int)URCHIN_FLOOR_Y + 6; y++) for (int x = (int)tank.urchin_x - 14; x <= (int)tank.urchin_x + 14; x++) {
                uint16_t v = cfb[y * TANK_W + x]; int r = v >> 11, g = (v >> 5) & 63, b = v & 31;
                purple += r > 8 && b > 8 && g < r * 2; }
            if (purple < 60) { printf("FAIL: no urchin drawn at %.0f (%d px)\n", tank.urchin_x, purple); return 1; }
            printf("selftest-shop: urchin card: %.0f cm of grass, the tap hits, the card draws; %d urchin px in the tank\n", tank.urchin_grazed_px / PX_PER_CM, purple);
        }
        memcpy(tank.veg_h, veg_was, sizeof veg_was); tank_veg_sync(&tank);
    }
    /* the pages: the sand dollar on the milestones page, a row's modal, UNLOCK, CLOSE */
    {
        static uint16_t fb[TANK_W * TANK_H];
        render_milestones(&tank, fb, TANK_W);
        if (ms_tap(MSP_SD_X + MSP_ICON / 2, MS_TANK_Y) != MS_TAP_SHOP) { printf("FAIL: the sand dollar did not open the shop\n"); return 1; }
        if (ms_tap(MSP_UPG_X + MSP_UPG_W / 2, MS_FOOT_Y) != MS_TAP_SHOP) { printf("FAIL: UPGRADES did not open the shop\n"); return 1; }
        if (ms_tap(MSP_SET_X + MSP_SET_W / 2, MSP_SET_Y + 10) != MS_TAP_SETTINGS || ms_tap(MSP_CLOSE_X + MSP_CLOSE_W / 2, MS_FOOT_Y) != MS_TAP_CLOSE) { printf("FAIL: SETTINGS / CLOSE moved\n"); return 1; }
        render_milestones_leave();
        tank.sd_unlocks = 0; tank.sd_balance = SD_PRICE_PLANT + 5; want = tank.sd_balance;
        render_shop(&tank, fb, TANK_W);
        if (shop_tap(SHOP_ROW_X, SHOP_ROW_Y(0)) != SHOP_TAP_KEPT) { printf("FAIL: the plant's row did not open its modal\n"); return 1; }
        render_shop(&tank, fb, TANK_W);
        int r = shop_tap(SHOP_BTN_X, SHOP_BTN_Y);
        if (r != SHOP_TAP_BUY + 0) { printf("FAIL: UNLOCK in the modal returned %d\n", r); return 1; }
        if (!progression_buy(&tank, 0)) { printf("FAIL: the page's UNLOCK did not buy\n"); return 1; }
        want -= SD_PRICE_PLANT; SHOP_WANT("bought from the page");
        /* the placement page (2026-09-16): the plant lands at the default spot,
           MIDDLE; a drag on the water takes it along (clamped inside the
           window), a layer button sets the depth, DONE saves both; the render
           honours the layer (a fish on the leaf shows through only from BEHIND) */
        if (fabsf(tank_decor_x(&tank, 0) - PLANT_X_DEFAULT) > 0.01f || tank_decor_z(&tank, 0) != DECOR_Z_MIDDLE) { printf("FAIL: the plant did not land at the default spot\n"); return 1; }
        setup_begin_place(&tank, 0);
        if (!setup_active() || !setup_is_place() || setup_item() != 0 || setup_page() != SETUP_PG_PLACE || setup_fish() != -1) { printf("FAIL: the placement page did not open\n"); return 1; }
        const float wy = PG_Y(UI(250));                                     /* in the water under the page's DEPTH bar: the drag zone */
        setup_touch(&tank, FLOOR_X(120), wy, true);
        for (int k = 1; k <= 20; k++) setup_touch(&tank, FLOOR_X(120 + k * 9), wy, true);
        setup_touch(&tank, FLOOR_X(300), wy, false);
        if (fabsf(tank_decor_x(&tank, 0) - FLOOR_X(300)) > 0.01f || !setup_active()) { printf("FAIL: the drag did not carry the plant (x %.0f)\n", tank_decor_x(&tank, 0)); return 1; }
        { float l0, l3; tank_veg_frond(&tank, 3, 0, &l0); tank_veg_frond(&tank, 3, 3, &l3);
          if (fabsf((l0 + l3) * 0.5f - FLOOR_X(300)) > 0.01f) { printf("FAIL: bed 3 did not follow the plant (leaves %.0f..%.0f)\n", l0, l3); return 1; } }
        setup_touch(&tank, 2, wy, true); setup_touch(&tank, 2, wy, false);
        if (tank_decor_x(&tank, 0) != TANK_FX0 + DECOR_MARGIN + PLANT_HALF_W) { printf("FAIL: the plant was not kept inside the window (x %.0f)\n", tank_decor_x(&tank, 0)); return 1; }
        setup_touch(&tank, TANK_W - 8, wy, true); setup_touch(&tank, TANK_W - 8, wy, false);
        if (tank_decor_x(&tank, 0) != TANK_FX1 - DECOR_MARGIN - PLANT_HALF_W) { printf("FAIL: the plant went through the right glass (x %.0f)\n", tank_decor_x(&tank, 0)); return 1; }
        setup_touch(&tank, FLOOR_X(300), wy, true); setup_touch(&tank, FLOOR_X(300), wy, false);
        if (pg_hit(SETUP_DEPTH_X + UI(10), SETUP_DEPTH_Y + UI(10)) != SETUP_HIT_Z0 + DECOR_Z_BACK || pg_hit(SETUP_DEPTH_X + 2 * SETUP_DEPTH_SEG_W + UI(100), SETUP_DEPTH_Y + SETUP_DEPTH_H + UI(4)) != SETUP_HIT_Z0 + DECOR_Z_FRONT
            || pg_hit(SETUP_TOP_NEXT_X + UI(20), SETUP_TOP_BTN_Y + UI(20)) != SETUP_HIT_NEXT || pg_hit(SETUP_TOP_BACK_X + UI(20), SETUP_TOP_BTN_Y + UI(20)) != SETUP_HIT_SELL) { printf("FAIL: the placement page's buttons moved\n"); return 1; }   /* SELL took BACK's corner (2026-09-24) */
        pg_touch(SETUP_DEPTH_X + 2 * SETUP_DEPTH_SEG_W + UI(50), SETUP_DEPTH_Y + UI(18), true);
        pg_touch(SETUP_DEPTH_X + 2 * SETUP_DEPTH_SEG_W + UI(52), SETUP_DEPTH_Y + UI(20), false);
        if (tank_decor_z(&tank, 0) != DECOR_Z_FRONT || fabsf(tank_decor_x(&tank, 0) - FLOOR_X(300)) > 0.01f) { printf("FAIL: FRONT did not set the layer (z %d, x %.0f)\n", tank_decor_z(&tank, 0), tank_decor_x(&tank, 0)); return 1; }
        /* the render: a fish sitting on a leaf's spine, mid-height - leaf 0
           (even: the back half of a MIDDLE weave) and leaf 1 (odd: the front
           half); FRONT hides the fish behind both, BACK shows it over both */
        { tank_veg_set(&tank, 3, 0.5f);
          float top; tank_veg_bed(&tank, 3, NULL, NULL, &top, NULL);
          int sy = (int)((top + TANK_BOT - 16) * 0.5f);
          fish_t *f = &tank.fish[0]; float fx0 = f->x, fy0 = f->y;
          const int zs[3] = { DECOR_Z_FRONT, DECOR_Z_MIDDLE, DECOR_Z_BACK }; bool shows[3][2];
          for (int k = 0; k < 3; k++) for (int leaf = 0; leaf < 2; leaf++) {
              float lx; tank_veg_frond(&tank, 3, leaf, &lx); int sx = (int)lx;
              tank_decor_set(&tank, 0, FLOOR_X(300), zs[k]);
              park_fish(f); render_tank(&tank, fb, TANK_W); uint16_t bare = fb[sy * TANK_W + sx];
              f->x = (float)sx; f->y = (float)sy; render_tank(&tank, fb, TANK_W); uint16_t over = fb[sy * TANK_W + sx];
              shows[k][leaf] = over != bare;               /* the fish shows on the leaf's pixel */
          }
          f->x = fx0; f->y = fy0;
          printf("selftest-shop: placement: dragged to x 300 (clamped %d..%d), FRONT; a fish on leaves 0/1 shows through FRONT %d/%d, MIDDLE %d/%d, BACK %d/%d\n",
                 TANK_FX0 + DECOR_MARGIN + PLANT_HALF_W, TANK_FX1 - DECOR_MARGIN - PLANT_HALF_W, shows[0][0], shows[0][1], shows[1][0], shows[1][1], shows[2][0], shows[2][1]);
          if (shows[0][0] || shows[0][1] || !shows[1][0] || shows[1][1] || !shows[2][0] || !shows[2][1]) { printf("FAIL: the layer did not order the leaves and the fish\n"); return 1; }
          tank_decor_set(&tank, 0, FLOOR_X(300), DECOR_Z_FRONT); }
        pg_touch(SETUP_TOP_NEXT_X + UI(20), SETUP_TOP_BTN_Y + UI(20), true);
        pg_touch(SETUP_TOP_NEXT_X + UI(22), SETUP_TOP_BTN_Y + UI(24), false);
        if (setup_active()) { printf("FAIL: DONE did not close the placement page\n"); return 1; }
        { int nf = tank.n_fish; tank_init(&tank, 4242); progression_boot(&tank); tank.trickle_off = true;
          if (tank.n_fish != nf) { printf("FAIL: the save did not come back after the placement\n"); return 1; }
          if (fabsf(tank_decor_x(&tank, 0) - FLOOR_X(300)) > 0.01f || tank_decor_z(&tank, 0) != DECOR_Z_FRONT) { printf("FAIL: the placement was not saved (x %.0f, z %d)\n", tank_decor_x(&tank, 0), tank_decor_z(&tank, 0)); return 1; }
          printf("selftest-shop: DONE saved the placement; the reload put the plant back at x 300, FRONT\n"); }
        /* the castle (2026-09-16): the third item, at its price; placeable with
           TWO depths (BEHIND / IN FRONT - no AMONG: MIDDLE is taken as FRONT);
           its page's bar has two segments; IN FRONT a fish in the arch shows and
           one behind the gate wall does not, and the grass never draws over the
           walls; BEHIND the fish and the grass pass in front of it; the spot and
           the depth survive a save */
        {
            if (SD_ITEM_COUNT != 7 || SD_ITEMS[2].bit != SD_ITEM_CASTLE || SD_ITEMS[2].price != SD_PRICE_CASTLE) { printf("FAIL: the castle is not the third item\n"); return 1; }
            if (!tank_decor_placeable(2) || tank_decor_z_count(2) != 2 || tank_decor_z_at(2, 0) != DECOR_Z_BACK || tank_decor_z_at(2, 1) != DECOR_Z_FRONT
                || tank_decor_z_index(2, DECOR_Z_FRONT) != 1 || tank_decor_z_index(2, DECOR_Z_BACK) != 0) { printf("FAIL: the castle's depths\n"); return 1; }
            tank.sd_balance = SD_PRICE_CASTLE - 1;
            if (progression_buy(&tank, 2)) { printf("FAIL: the castle sold short\n"); return 1; }
            tank.sd_balance = SD_PRICE_CASTLE;
            if (!progression_buy(&tank, 2) || tank.sd_balance != 0 || !(tank.sd_unlocks & SD_ITEM_CASTLE)) { printf("FAIL: the castle did not sell at %d\n", SD_PRICE_CASTLE); return 1; }
            if (fabsf(tank_decor_x(&tank, 2) - CASTLE_X_DEFAULT) > 0.01f || tank_decor_z(&tank, 2) != DECOR_Z_FRONT) { printf("FAIL: the castle did not land at the default spot, IN FRONT\n"); return 1; }
            tank_decor_set(&tank, 2, FLOOR_X(300), DECOR_Z_MIDDLE);
            if (tank_decor_z(&tank, 2) != DECOR_Z_FRONT) { printf("FAIL: the castle took AMONG\n"); return 1; }
            tank_decor_set(&tank, 2, 10, DECOR_Z_BACK);
            if (tank_decor_x(&tank, 2) != TANK_FX0 + DECOR_MARGIN + CASTLE_HALF_W || tank_decor_z(&tank, 2) != DECOR_Z_BACK) { printf("FAIL: the castle's clamp (x %.0f)\n", tank_decor_x(&tank, 2)); return 1; }
            tank_decor_set(&tank, 2, FLOOR_X(300), DECOR_Z_FRONT);
            setup_begin_place(&tank, 2);
            if (!setup_is_place() || setup_item() != 2) { printf("FAIL: the castle's placement page did not open\n"); return 1; }
            int bx = (PAGE_W - 2 * SETUP_DEPTH_SEG_W) / 2;               /* the two segments, centered on the page */
            if (pg_hit(bx + UI(10), SETUP_DEPTH_Y + UI(10)) != SETUP_HIT_Z0 + DECOR_Z_BACK || pg_hit(bx + SETUP_DEPTH_SEG_W + UI(10), SETUP_DEPTH_Y + UI(10)) != SETUP_HIT_Z0 + DECOR_Z_FRONT
                || pg_hit(bx - UI(30), SETUP_DEPTH_Y + UI(10)) != 0) { printf("FAIL: the castle's two-segment DEPTH bar\n"); return 1; }
            pg_touch(bx + UI(10), SETUP_DEPTH_Y + UI(18), true); pg_touch(bx + UI(12), SETUP_DEPTH_Y + UI(20), false);
            if (tank_decor_z(&tank, 2) != DECOR_Z_BACK) { printf("FAIL: BEHIND did not set the castle's depth\n"); return 1; }
            render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, 1.0f);   /* the page draws (the castle live, BEHIND) */
            pg_touch(bx + SETUP_DEPTH_SEG_W + UI(10), SETUP_DEPTH_Y + UI(18), true); pg_touch(bx + SETUP_DEPTH_SEG_W + UI(12), SETUP_DEPTH_Y + UI(20), false);
            if (tank_decor_z(&tank, 2) != DECOR_Z_FRONT) { printf("FAIL: IN FRONT did not set the castle's depth\n"); return 1; }
            setup_touch(&tank, FLOOR_X(120), PG_Y(UI(250)), true); setup_touch(&tank, FLOOR_X(250), PG_Y(UI(250)), true); setup_touch(&tank, FLOOR_X(250), PG_Y(UI(250)), false);
            if (fabsf(tank_decor_x(&tank, 2) - FLOOR_X(250)) > 0.01f) { printf("FAIL: the drag did not carry the castle (x %.0f)\n", tank_decor_x(&tank, 2)); return 1; }
            render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, 1.0f);
            tank_decor_set(&tank, 2, FLOOR_X(300), DECOR_Z_FRONT);
            pg_touch(SETUP_TOP_NEXT_X + UI(20), SETUP_TOP_BTN_Y + UI(20), true); pg_touch(SETUP_TOP_NEXT_X + UI(22), SETUP_TOP_BTN_Y + UI(24), false);
            if (setup_active()) { printf("FAIL: DONE did not close the castle's page\n"); return 1; }
            /* the render */
            fish_t *f = &tank.fish[0]; float fx0 = f->x, fy0 = f->y, fh0 = f->heading;
            const int cx = (int)tank_decor_x(&tank, 2);                 /* where the castle stands: 300, or as near as this board's floor lets it */
            const int FY = TANK_BOT - 16, ax = cx, ay = FY - 20, wx = cx + 45, wy = FY - 30;   /* in the opening; on the gate wall right of it */
            f->heading = 0; tank_fish_face(f);
            park_fish(f); for (int i = 0; i < 3; i++) render_tank(&tank, fb, TANK_W);
            uint16_t bare_a = fb[ay * TANK_W + ax], bare_w = fb[wy * TANK_W + wx];
            f->x = ax; f->y = ay; render_tank(&tank, fb, TANK_W); bool in_arch = fb[ay * TANK_W + ax] != bare_a;
            f->x = wx; f->y = wy; render_tank(&tank, fb, TANK_W); bool on_wall = fb[wy * TANK_W + wx] != bare_w;
            tank_decor_set(&tank, 2, FLOOR_X(300), DECOR_Z_BACK);
            park_fish(f); for (int i = 0; i < 3; i++) render_tank(&tank, fb, TANK_W); uint16_t bare_wb = fb[wy * TANK_W + wx];
            f->x = wx; f->y = wy; render_tank(&tank, fb, TANK_W); bool on_wall_behind = fb[wy * TANK_W + wx] != bare_wb;
            park_fish(f);
            /* the grass: bed 2's fronds (x 284..368 in the rectangle) cross the walls; grown vs
               stubble changes the wall band only when the castle is BEHIND */
            int diff[2];
            for (int k = 0; k < 2; k++) {
                tank_decor_set(&tank, 2, FLOOR_X(300), k ? DECOR_Z_FRONT : DECOR_Z_BACK);
                static uint16_t fa[TANK_W * TANK_H];
                tank_veg_set(&tank, 2, VEG_NUB); render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W); memcpy(fa, fb, sizeof fa);
                tank_veg_set(&tank, 2, 0.6f);    render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
                diff[k] = 0;
                for (int y = FY - 50; y <= FY - 16; y++) for (int x = cx - 30; x <= cx + 50; x++) diff[k] += fa[y * TANK_W + x] != fb[y * TANK_W + x];
            }
            f->x = fx0; f->y = fy0; f->heading = fh0; tank_fish_face(f);
            printf("selftest-shop: castle: IN FRONT a fish in the arch shows %d, behind the gate wall %d; BEHIND on the wall %d; grass over the walls BEHIND %d px, IN FRONT %d px\n",
                   in_arch, on_wall, on_wall_behind, diff[0], diff[1]);
            if (!in_arch || on_wall || !on_wall_behind || diff[0] == 0 || diff[1] != 0) { printf("FAIL: the castle's depths did not order the fish and the grass\n"); return 1; }
            tank_decor_set(&tank, 2, FLOOR_X(260), DECOR_Z_BACK); progression_save(&tank);
            { int nf = tank.n_fish; tank_init(&tank, 4242); progression_boot(&tank); tank.trickle_off = true;
              if (tank.n_fish != nf || !(tank.sd_unlocks & SD_ITEM_CASTLE) || fabsf(tank_decor_x(&tank, 2) - FLOOR_X(260)) > 0.01f || tank_decor_z(&tank, 2) != DECOR_Z_BACK) {
                  printf("FAIL: the castle's placement was not saved (x %.0f, z %d)\n", tank_decor_x(&tank, 2), tank_decor_z(&tank, 2)); return 1; } }
            tank_decor_set(&tank, 2, FLOOR_X(300), DECOR_Z_FRONT);
            printf("selftest-shop: castle bought at %d, no AMONG, two-segment bar, dragged, DONE; the reload put it back at x 260, BEHIND\n", SD_PRICE_CASTLE);
        }
        /* the shop's MOVE: the owned plant's modal re-opens the page */
        render_shop(&tank, fb, TANK_W);
        if (shop_tap(SHOP_ROW_X, SHOP_ROW_Y(0)) != SHOP_TAP_KEPT) { printf("FAIL: the owned plant's row did not open its modal\n"); return 1; }
        render_shop(&tank, fb, TANK_W);
        if (shop_tap(SHOP_MOVE_X, SHOP_BTN_Y) != SHOP_TAP_MOVE + 0) { printf("FAIL: MOVE in the owned modal\n"); return 1; }   /* MOVE is the left of two buttons since SELL (2026-09-24) */
        tank.sd_balance = 5; want = tank.sd_balance;
        render_shop(&tank, fb, TANK_W);
        if (shop_tap(SHOP_ROW_X, SHOP_ROW_Y(1)) != SHOP_TAP_KEPT) { printf("FAIL: the snail's row did not open its modal\n"); return 1; }
        r = shop_tap(SHOP_BTN_X, SHOP_BTN_Y);     /* short by 75: the dim button buys nothing */
        if (r != SHOP_TAP_KEPT) { printf("FAIL: a dim UNLOCK returned %d\n", r); return 1; }
        if (shop_tap(SHP_EARN_X + 28, MSP_CLOSE_Y + 8) != SHOP_TAP_KEPT) { printf("FAIL: HOW TO EARN did not open\n"); return 1; }
        if (shop_tap(SHOP_BTN_X, SHP_EARN_MODAL_Y + SHP_EARN_MODAL_H / 2) != SHOP_TAP_KEPT) { printf("FAIL: the earn modal did not close\n"); return 1; }
        if (shop_tap(SHP_CLOSE_X + 40, MS_FOOT_Y) != SHOP_TAP_CLOSE) { printf("FAIL: CLOSE\n"); return 1; }
        render_shop_leave();
        printf("selftest-shop: pages: the sand dollar and UPGRADES open the shop; a row -> modal -> UNLOCK buys; a dim UNLOCK does not; HOW TO EARN; CLOSE\n");
    }
    /* the coral (2026-09-23): the fourth item at 100, placeable with all three
       depths (AMONG by default, in the reef bed's grass), its colour picked on
       the placement page's COLOR row and kept by the save; four rows fit the
       shop page, so there is one page (the arrows appear with a fifth item) */
    {
        static uint16_t fb[TANK_W * TANK_H];
        if (SD_ITEMS[3].bit != SD_ITEM_CORAL || SD_ITEMS[3].price != SD_PRICE_CORAL || SD_PRICE_CORAL != 100) { printf("FAIL: the coral is not the fourth item at 100\n"); return 1; }
        if (!tank_decor_placeable(3) || tank_decor_z_count(3) != 2 || tank_decor_z_at(3, 1) != DECOR_Z_FRONT || tank_decor_z_at(3, 0) != DECOR_Z_BACK || tank_decor_half_w(3) != CORAL_HALF_W) { printf("FAIL: the coral's depths (BEHIND / IN FRONT only)\n"); return 1; }
        tank.sd_unlocks &= ~SD_ITEM_CORAL; tank.sd_balance = SD_PRICE_CORAL - 1;
        if (progression_buy(&tank, 3)) { printf("FAIL: the coral sold short\n"); return 1; }
        tank.sd_balance = SD_PRICE_CORAL;
        if (!progression_buy(&tank, 3) || tank.sd_balance != 0 || !(tank.sd_unlocks & SD_ITEM_CORAL)) { printf("FAIL: the coral did not sell at %d\n", SD_PRICE_CORAL); return 1; }
        if (fabsf(tank_decor_x(&tank, 3) - CORAL_X_DEFAULT) > 0.01f || tank_decor_z(&tank, 3) != DECOR_Z_FRONT || tank_coral_rgb(&tank) != CORAL_PAL[0]) { printf("FAIL: the coral did not land at the default spot, IN FRONT, in the first colour\n"); return 1; }
        tank_decor_set(&tank, 3, FLOOR_X(150), DECOR_Z_MIDDLE);
        if (tank_decor_z(&tank, 3) != DECOR_Z_FRONT) { printf("FAIL: the coral took AMONG\n"); return 1; }
        tank_decor_set(&tank, 3, FLOOR_X(150), DECOR_Z_BACK);
        /* the shop page: the fourth row opens its modal, MOVE in it */
        render_shop_leave(); render_shop(&tank, fb, TANK_W);
        if (shop_tap(SHOP_ROW_X, SHOP_ROW_Y(3)) != SHOP_TAP_KEPT) { printf("FAIL: the coral's row did not open its modal\n"); return 1; }
        render_shop(&tank, fb, TANK_W);
        if (shop_tap(SHOP_MOVE_X, SHOP_BTN_Y) != SHOP_TAP_MOVE + 3) { printf("FAIL: MOVE in the coral's modal\n"); return 1; }
        render_shop_leave();
        /* its placement page: the COLOR row above the water, a swatch sets the colour, the drag carries it, DONE saves all three */
        setup_begin_place(&tank, 3);
        if (!setup_is_place() || setup_item() != 3) { printf("FAIL: the coral's placement page did not open\n"); return 1; }
        int sx = SETUP_COL_X + 3 * SETUP_COL_PX + SETUP_COL_W / 2;
        if (pg_hit(sx, SETUP_COL_Y + SETUP_COL_H / 2) != SETUP_HIT_COLOR0 + 3) { printf("FAIL: the fourth swatch's hit (%d)\n", pg_hit(sx, SETUP_COL_Y + SETUP_COL_H / 2)); return 1; }
        if (pg_hit(SETUP_COL_X + UI(4), SETUP_COL_Y + SETUP_COL_H + UI(8)) != SETUP_HIT_COLOR0) { printf("FAIL: a low finger under the first swatch\n"); return 1; }
        pg_touch(sx, SETUP_COL_Y + UI(10), true); pg_touch(sx + UI(1), SETUP_COL_Y + UI(12), false);
        if (tank_coral_rgb(&tank) != CORAL_PAL[3]) { printf("FAIL: the swatch did not colour the coral (%06x)\n", (unsigned)tank_coral_rgb(&tank)); return 1; }
        render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, 1.0f);        /* the page draws, the coral in the new colour */
        int bx = (PAGE_W - 2 * SETUP_DEPTH_SEG_W) / 2;
        pg_touch(bx + SETUP_DEPTH_SEG_W + UI(10), SETUP_DEPTH_Y + UI(18), true); pg_touch(bx + SETUP_DEPTH_SEG_W + UI(12), SETUP_DEPTH_Y + UI(20), false);
        if (tank_decor_z(&tank, 3) != DECOR_Z_FRONT) { printf("FAIL: IN FRONT did not set the coral's depth\n"); return 1; }
        setup_touch(&tank, FLOOR_X(120), PG_Y(SETUP_PLACE_CORAL_Y + UI(20)), true); setup_touch(&tank, FLOOR_X(320), PG_Y(SETUP_PLACE_CORAL_Y + UI(20)), true); setup_touch(&tank, FLOOR_X(320), PG_Y(SETUP_PLACE_CORAL_Y + UI(20)), false);
        if (fabsf(tank_decor_x(&tank, 3) - FLOOR_X(320)) > 0.01f) { printf("FAIL: the drag did not carry the coral (x %.0f)\n", tank_decor_x(&tank, 3)); return 1; }
        pg_touch(SETUP_COL_X + SETUP_COL_PX + UI(32), SETUP_COL_Y + SETUP_COL_H + UI(4), true); pg_touch(SETUP_COL_X + SETUP_COL_PX + UI(33), SETUP_COL_Y + SETUP_COL_H + UI(4), false);   /* a low press under the row is a swatch (the second), never a drag */
        if (fabsf(tank_decor_x(&tank, 3) - FLOOR_X(320)) > 0.01f || tank_coral_rgb(&tank) != CORAL_PAL[1]) { printf("FAIL: a press under the COLOR row (x %.0f, %06x)\n", tank_decor_x(&tank, 3), (unsigned)tank_coral_rgb(&tank)); return 1; }
        setup_activate(&tank, SETUP_HIT_NEXT);                                         /* DONE: saved */
        if (setup_active()) { printf("FAIL: DONE did not close the coral's page\n"); return 1; }
        render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);              /* IN FRONT, drawn over the fish: no crash, no rebake loop */
        tank_decor_set(&tank, 3, FLOOR_X(320), DECOR_Z_BACK); render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);   /* BEHIND: baked */
        progression_save(&tank);
        tank_init(&tank, 4243); progression_boot(&tank);
        if (!(tank.sd_unlocks & SD_ITEM_CORAL) || fabsf(tank_decor_x(&tank, 3) - FLOOR_X(320)) > 0.01f || tank_decor_z(&tank, 3) != DECOR_Z_BACK || tank_coral_rgb(&tank) != CORAL_PAL[1]) {
            printf("FAIL: the save lost the coral (x %.0f z %d colour %06x)\n", tank_decor_x(&tank, 3), tank_decor_z(&tank, 3), (unsigned)tank_coral_rgb(&tank)); return 1; }
        printf("selftest-shop: the coral: fourth row at %d, MOVE, the COLOR row (swatch 3 -> %06x, a low press = swatch 1), IN FRONT, dragged to 320, DONE saved; the reload kept the spot, the depth and the colour\n", SD_PRICE_CORAL, (unsigned)CORAL_PAL[3]);
        /* it grows, slowly, awake or asleep: a bought coral starts as a stub,
           a day adds ~0.03, a month makes the fan, a week more the crown; the
           sprite has more of it at every stage; the growth rides the save */
        if (fabsf(tank_coral_growth(&tank) - CORAL_START) > 1e-4f) { printf("FAIL: the reloaded coral is not young (%.2f)\n", tank_coral_growth(&tank)); return 1; }
        float g0 = tank_coral_growth(&tank);
        tank_tick_sleep(&tank, 86400);
        float g1 = tank_coral_growth(&tank);
        for (int i = 0; i < 61 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);   /* a minute awake grows it the same pace: one 60 s chunk lands (61 s: 3600 float sixtieths fall a hair short of 60) */
        float g2 = tank_coral_growth(&tank);
        if (g1 - g0 < 0.030f || g1 - g0 > 0.036f || g2 - g1 < 2.0e-5f || g2 - g1 > 2.7e-5f) { printf("FAIL: the coral's pace (day +%.4f, minute +%.6f)\n", g1 - g0, g2 - g1); return 1; }
        int c_young = render_coral_cells(CORAL_START), c_half = render_coral_cells(0.72f), c_fan = render_coral_cells(1.0f);
        if (!(c_young < c_half && c_half < c_fan) || c_young < 150 || c_fan < 300) { printf("FAIL: the sprite does not grow (%d, %d, %d cells)\n", c_young, c_half, c_fan); return 1; }
        tank_tick_sleep(&tank, 40 * 86400);
        if (tank_coral_growth(&tank) != CORAL_FULL) { printf("FAIL: 40 days did not finish the coral (%.2f)\n", tank_coral_growth(&tank)); return 1; }
        /* the crown: at CORAL_FULL there are pixels above the fan's top that the fan alone never touches */
        tank_decor_set(&tank, 3, FLOOR_X(224), DECOR_Z_FRONT);
        const int kx = (int)tank_decor_x(&tank, 3);
        tank.coral_growth = 1.0f; render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
        static uint16_t fb2[TANK_W * TANK_H]; memcpy(fb2, fb, sizeof fb2);
        tank.coral_growth = CORAL_FULL; render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
        int crown = 0, ytop = TANK_BOT - 14 - 92;
        for (int y = ytop - 12; y < ytop + 16; y++) for (int x = kx - 40; x < kx + 40; x++) crown += fb[y * TANK_W + x] != fb2[y * TANK_W + x];
        if (crown < 30) { printf("FAIL: no crown above the grown coral (%d px differ)\n", crown); return 1; }
        progression_save(&tank); tank_init(&tank, 4244); progression_boot(&tank);
        if (tank_coral_growth(&tank) != CORAL_FULL) { printf("FAIL: the save lost the coral's growth\n"); return 1; }
        printf("selftest-shop: the coral grows: a day +%.3f, a minute awake +%.6f; %d / %d / %d cells young / half / fan; 40 days = %.2f with a crown of %d px; saved\n", g1 - g0, g2 - g1, c_young, c_half, c_fan, CORAL_FULL, crown);
        tank_veg_set(&tank, 3, VEG_START);
    }
    /* the reef cluster (2026-09-24): the fifth item at 240, on the shop's
       SECOND page (the header's arrows: the right one flips, the left one
       back; a row on page 2 opens the cluster's modal); it arrives at 80% of
       its full size and fills out in two weeks, then blooms tentacle by
       tentacle over two more; its LOOK row picks one of three schemes; the
       save keeps spot, depth, look and growth */
    {
        static uint16_t fb[TANK_W * TANK_H];
        if (SD_ITEMS[4].bit != SD_ITEM_CLUSTER || SD_ITEMS[4].price != SD_PRICE_CLUSTER || SD_PRICE_CLUSTER != 240) { printf("FAIL: the cluster is not the fifth item at 240\n"); return 1; }
        if (!tank_decor_placeable(4) || tank_decor_z_count(4) != 2 || tank_decor_half_w(4) != CLUSTER_HALF_W) { printf("FAIL: the cluster's depths (BEHIND / IN FRONT only)\n"); return 1; }
        render_shop_leave(); render_shop(&tank, fb, TANK_W);
        if (shop_tap(SHOP_ROW_X, SHOP_ROW_Y(0)) != SHOP_TAP_KEPT) { printf("FAIL: page 1 row 0\n"); return 1; }
        render_shop_leave(); render_shop(&tank, fb, TANK_W);
        if (shop_tap(SHOP_NEXT_X, SHOP_ARROW_Y) != SHOP_TAP_KEPT) { printf("FAIL: the right arrow\n"); return 1; }
        render_shop(&tank, fb, TANK_W);                                /* page 2: row 0 is the cluster */
        if (shop_tap(SHOP_ROW_X, SHOP_ROW_Y(0)) != SHOP_TAP_KEPT) { printf("FAIL: page 2 row 0 did not open a modal\n"); return 1; }
        render_shop(&tank, fb, TANK_W);
        tank.sd_unlocks &= ~SD_ITEM_CLUSTER; tank.sd_balance = SD_PRICE_CLUSTER;
        int r = shop_tap(SHOP_BTN_X, SHOP_BTN_Y);
        if (r != SHOP_TAP_BUY + 4) { printf("FAIL: UNLOCK in the cluster's modal returned %d\n", r); return 1; }
        if (shop_tap(SHOP_ROW_X, SHOP_ROW_Y(3)) != SHOP_TAP_NONE) { printf("FAIL: page 2 has a fourth row (the cluster, the shrimp and the urchin only)\n"); return 1; }
        if (shop_tap(SHOP_ROW_X, SHOP_ROW_Y(2)) != SHOP_TAP_KEPT) { printf("FAIL: page 2 row 2 (the urchin) did not open a modal\n"); return 1; }
        render_shop(&tank, fb, TANK_W);
        shop_tap(SHOP_ROW_X, SHOP_ROW_Y(0));                           /* (any tap closes its modal) */
        if (shop_tap(SHOP_PREV_X, SHOP_ARROW_Y) != SHOP_TAP_KEPT) { printf("FAIL: the left arrow\n"); return 1; }
        render_shop(&tank, fb, TANK_W);
        if (shop_tap(SHOP_ROW_X, SHOP_ROW_Y(3)) != SHOP_TAP_KEPT) { printf("FAIL: back on page 1, row 3 (the coral)\n"); return 1; }
        render_shop_leave();
        tank.sd_balance = SD_PRICE_CLUSTER - 1;
        if (progression_buy(&tank, 4)) { printf("FAIL: the cluster sold short\n"); return 1; }
        tank.sd_balance = SD_PRICE_CLUSTER;
        if (!progression_buy(&tank, 4) || tank.sd_balance != 0 || !(tank.sd_unlocks & SD_ITEM_CLUSTER)) { printf("FAIL: the cluster did not sell at %d\n", SD_PRICE_CLUSTER); return 1; }
        if (fabsf(tank_decor_x(&tank, 4) - CLUSTER_X_DEFAULT) > 0.01f || tank_decor_z(&tank, 4) != DECOR_Z_FRONT || tank_cluster_growth(&tank) > 0.01f) { printf("FAIL: the cluster's arrival (x %.0f z %d g %.3f)\n", tank_decor_x(&tank, 4), tank_decor_z(&tank, 4), tank_cluster_growth(&tank)); return 1; }
        int c_young = render_cluster_cells(0), c_full = render_cluster_cells(1.0f);
        if (!(c_young < c_full) || c_young < 1000 || c_young * 100 / c_full < 60) { printf("FAIL: the cluster's size phase (%d young, %d full cells)\n", c_young, c_full); return 1; }
        float g0 = tank_cluster_growth(&tank);
        tank_tick_sleep(&tank, 86400);
        float g1 = tank_cluster_growth(&tank);
        for (int i = 0; i < 61 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        float g2 = tank_cluster_growth(&tank);
        if (g1 - g0 < 0.070f || g1 - g0 > 0.073f || g2 - g1 < 4.5e-5f || g2 - g1 > 5.5e-5f) { printf("FAIL: the cluster's pace (day +%.4f, minute +%.6f)\n", g1 - g0, g2 - g1); return 1; }
        tank_tick_sleep(&tank, 40 * 86400);
        if (tank_cluster_growth(&tank) != CLUSTER_FULL) { printf("FAIL: 41 days did not finish the cluster (%.2f)\n", tank_cluster_growth(&tank)); return 1; }
        /* the page: the LOOK row, DUSK, then the drag; DONE saves */
        setup_begin_place(&tank, 4);
        if (!setup_is_place() || setup_item() != 4) { printf("FAIL: the cluster's placement page did not open\n"); return 1; }
        int lx = SETUP_LOOK_X + 2 * SETUP_LOOK_PX + SETUP_LOOK_W / 2;
        if (pg_hit(lx, SETUP_LOOK_Y + UI(10)) != SETUP_HIT_COLOR0 + 2) { printf("FAIL: the third look's hit\n"); return 1; }
        pg_touch(lx, SETUP_LOOK_Y + UI(10), true); pg_touch(lx + UI(1), SETUP_LOOK_Y + UI(12), false);
        if (tank_cluster_scheme(&tank) != 2) { printf("FAIL: the tile did not set the look\n"); return 1; }
        render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, 1.0f);
        setup_touch(&tank, FLOOR_X(120), PG_Y(SETUP_PLACE_CLUSTER_Y + UI(20)), true); setup_touch(&tank, FLOOR_X(250), PG_Y(SETUP_PLACE_CLUSTER_Y + UI(20)), true); setup_touch(&tank, FLOOR_X(250), PG_Y(SETUP_PLACE_CLUSTER_Y + UI(20)), false);
        if (fabsf(tank_decor_x(&tank, 4) - FLOOR_X(250)) > 0.01f) { printf("FAIL: the drag did not carry the cluster (x %.0f)\n", tank_decor_x(&tank, 4)); return 1; }
        setup_activate(&tank, SETUP_HIT_NEXT);
        /* the bloom: at CLUSTER_FULL pixels differ from full size without it, above the rock */
        tank_decor_set(&tank, 4, FLOOR_X(250), DECOR_Z_FRONT);
        tank.cluster_growth = 1.0f; render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
        static uint16_t fb2[TANK_W * TANK_H]; memcpy(fb2, fb, sizeof fb2);
        tank.cluster_growth = CLUSTER_FULL; render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);
        int bloom = 0;
        for (int y = TANK_BOT - 14 - 140; y < TANK_BOT - 14 - 20; y++) for (int x = (int)FLOOR_X(250) - 80; x < (int)FLOOR_X(250) + 80; x++) bloom += fb[y * TANK_W + x] != fb2[y * TANK_W + x];
        if (bloom < 150) { printf("FAIL: no bloom on the grown cluster (%d px differ)\n", bloom); return 1; }
        tank_decor_set(&tank, 4, FLOOR_X(250), DECOR_Z_BACK); render_tank(&tank, fb, TANK_W); render_tank(&tank, fb, TANK_W);   /* BEHIND: baked, no crash */
        progression_save(&tank); tank_init(&tank, 4245); progression_boot(&tank);
        if (!(tank.sd_unlocks & SD_ITEM_CLUSTER) || fabsf(tank_decor_x(&tank, 4) - FLOOR_X(250)) > 0.01f || tank_decor_z(&tank, 4) != DECOR_Z_BACK || tank_cluster_scheme(&tank) != 2 || tank_cluster_growth(&tank) != CLUSTER_FULL) {
            printf("FAIL: the save lost the cluster (x %.0f z %d look %d g %.2f)\n", tank_decor_x(&tank, 4), tank_decor_z(&tank, 4), tank_cluster_scheme(&tank), tank_cluster_growth(&tank)); return 1; }
        printf("selftest-shop: the reef cluster: page 2 by the arrows, UNLOCK at %d, arrives at %d%% (%d / %d cells), a day +%.3f, LOOK -> DUSK, dragged to 250, a bloom of %d px, saved\n", SD_PRICE_CLUSTER, c_young * 100 / c_full, c_young, c_full, g1 - g0, bloom);
        tank_veg_set(&tank, 3, VEG_START);
    }
    /* the shrimp school (2026-09-29): the sixth item at 180, a resident (no
       SELL), page 2's second row; pellets rest FOOD_FLOOR_S on the floor; the
       school pecks one clean and it counts; ten bring a newcomer, then the
       cooldown - the count waits at ten, never a burst; over half the glass
       fouled they won't eat; a tap scatters them; the save keeps the school */
    {
        static uint16_t fb[TANK_W * TANK_H];
        if (SD_ITEMS[5].bit != SD_ITEM_SHRIMP || SD_ITEMS[5].price != SD_PRICE_SHRIMP || SD_PRICE_SHRIMP != 180) { printf("FAIL: the shrimp are not the sixth item at 180\n"); return 1; }
        if (tank_decor_placeable(5)) { printf("FAIL: the shrimp are placeable\n"); return 1; }
        render_shop_leave(); render_shop(&tank, fb, TANK_W);
        if (shop_tap(SHOP_NEXT_X, SHOP_ARROW_Y) != SHOP_TAP_KEPT) { printf("FAIL: the right arrow (to the shrimp)\n"); return 1; }
        render_shop(&tank, fb, TANK_W);
        if (shop_tap(SHOP_ROW_X, SHOP_ROW_Y(1)) != SHOP_TAP_KEPT) { printf("FAIL: page 2 row 1 (the shrimp) did not open a modal\n"); return 1; }
        render_shop(&tank, fb, TANK_W);
        render_shop_leave();
        tank.sd_unlocks &= ~SD_ITEM_SHRIMP; tank.sd_balance = SD_PRICE_SHRIMP - 1;
        if (progression_buy(&tank, 5)) { printf("FAIL: the shrimp sold short\n"); return 1; }
        tank.sd_balance = SD_PRICE_SHRIMP;
        if (!progression_buy(&tank, 5) || tank.sd_balance != 0 || tank.shrimp_n != SHRIMP_START) { printf("FAIL: the shrimp did not sell at %d (%d shrimp)\n", SD_PRICE_SHRIMP, tank.shrimp_n); return 1; }
        if (progression_sell(&tank, 5)) { printf("FAIL: the shrimp sold back\n"); return 1; }
        tank.trickle_off = true;
        memset(tank.algae, 0, ALGAE_CELLS);
        /* the fish out of the way (they would eat the test's pellets) */
#define SHRIMP_TICK(n) for (int i_ = 0; i_ < (n); i_++) { tank_tick(&tank, 1.0f / 60.0f, advisor_rules); progression_tick(&tank, 1.0f / 60.0f); \
            for (int f_ = 0; f_ < tank.n_fish; f_++) { tank.fish[f_].x = TANK_W - 40; tank.fish[f_].y = 50; tank_glass_clamp(&tank.fish[f_].x, &tank.fish[f_].y, 30); } }
#define PUT_PELLET(k, px, py, page) do { for (k = 0; k < MAX_FOOD && tank.food[k].alive; k++) {} \
            if (k == MAX_FOOD) { printf("FAIL: no free pellet slot\n"); return 1; } \
            food_t *p_ = &tank.food[k]; memset(p_, 0, sizeof *p_); p_->alive = true; p_->x = (px); p_->y = (py); p_->age = (page); } while (0)
        for (int k = 0; k < MAX_FOOD; k++) tank.food[k].alive = false;
        /* the floor rest, far from the school: gone at 45 s in the water, 15 s after landing on the floor.
           The school is put off its food for it, by its own rule - a fouled glass: it swims the bowl's
           floor and the watch's end to end inside the 15 s and ate the pellet (the rectangle's far end
           was a second or two out of its reach) */
        memset(tank.algae, 200, ALGAE_CELLS);
        int k;
        float far_x = tank.shrimp[0].x < TANK_W / 2 ? TANK_FX1 - 30 : TANK_FX0 + 30;   /* the floor's far end */
        PUT_PELLET(k, far_x, FOOD_FLOOR_Y, 43);
        int kw; PUT_PELLET(kw, far_x, 200, 44.5f);
        SHRIMP_TICK(60);
        if (tank.food[kw].alive || !tank.food[k].alive) { printf("FAIL: a pellet in the water outlived %.0f s, or the floor pellet went early\n", FOOD_LIFE_S); return 1; }
        SHRIMP_TICK(60 * 13);
        if (!tank.food[k].alive) { printf("FAIL: the floor pellet went before %.0f s on the floor (%.1f)\n", FOOD_FLOOR_S, tank.food[k].floor_s); return 1; }
        SHRIMP_TICK(60 * 2);
        if (tank.food[k].alive) { printf("FAIL: the floor pellet outlived %.0f s on the floor\n", FOOD_FLOOR_S); return 1; }
        memset(tank.algae, 0, ALGAE_CELLS);
        /* a pellet by the school: they swarm it and peck it clean - one toward the next shrimp */
        float sx = 0; for (int i = 0; i < tank.shrimp_n; i++) sx += tank.shrimp[i].x; sx /= tank.shrimp_n;
        PUT_PELLET(k, sx + 20, FOOD_FLOOR_Y, 43);
        int t_eat = 0;
        while (tank.food[k].alive && t_eat < 60 * 15) { SHRIMP_TICK(1); t_eat++; }
        if (tank.food[k].alive || tank.shrimp_food != 1) { printf("FAIL: the school did not eat a floor pellet in 15 s (count %d)\n", tank.shrimp_food); return 1; }
        int t_first = t_eat; float mid_top = -1, mid_y = -1;
        /* below the grass's top a pellet is theirs while it still sinks; above it, not yet */
        {
            for (int b = 0; b < tank_veg_beds(&tank); b++) tank_veg_set(&tank, b, 0.5f);   /* a grown canopy to be under */
            sx = 0; for (int i = 0; i < tank.shrimp_n; i++) sx += tank.shrimp[i].x; sx /= tank.shrimp_n;
            float top = 0, sum = 0; bool over = false;
            for (int b = 0; b < tank_veg_beds(&tank); b++) { float x0, x1, tp; tank_veg_bed(&tank, b, &x0, &x1, &tp, NULL); sum += tp; if (!over && sx >= x0 && sx <= x1) { top = tp; over = true; } }
            if (!over) top = sum / tank_veg_beds(&tank);
            if (top + 40 < FOOD_FLOOR_Y - 30 && top - 40 > 20) {
                int ka; PUT_PELLET(ka, sx, top - 40, 10);             /* above the canopy: left alone for now */
                SHRIMP_TICK(60 * 2);
                if (tank.food[ka].nibbled > 0) { printf("FAIL: a pellet above the grass's top was pecked\n"); return 1; }
                tank.food[ka].alive = false;
                PUT_PELLET(k, sx, top + 10, 10);                     /* just under it, still sinking: taken on the way down */
                int t_mid = 0;
                while (tank.food[k].alive && t_mid < 60 * 30) { mid_y = tank.food[k].y; SHRIMP_TICK(1); t_mid++; }
                mid_top = top;
                if (tank.food[k].alive || tank.food[k].floor_s > 0) { printf("FAIL: a sinking pellet below the grass's top was not taken before the floor (floor %.1f s)\n", tank.food[k].floor_s); return 1; }
            }
            if (tank.shrimp_food != 2 && tank.shrimp_food != 1) { printf("FAIL: the count after the sinking pellet %d\n", tank.shrimp_food); return 1; }
        }
        /* the tenth brings a newcomer and starts the cooldown */
        tank.shrimp_food = SHRIMP_PER_JOIN; SHRIMP_TICK(1);
        if (tank.shrimp_n != SHRIMP_START + 1 || tank.shrimp_food != 0 || tank.shrimp_cool < SHRIMP_COOLDOWN_S - 1) { printf("FAIL: ten pellets brought %d shrimp (count %d, cooldown %.0f)\n", tank.shrimp_n, tank.shrimp_food, tank.shrimp_cool); return 1; }
        /* during the cooldown the count waits at ten, and nobody joins */
        tank.shrimp_food = SHRIMP_PER_JOIN;
        sx = 0; for (int i = 0; i < tank.shrimp_n; i++) sx += tank.shrimp[i].x; sx /= tank.shrimp_n;
        PUT_PELLET(k, sx, FOOD_FLOOR_Y, 43);
        t_eat = 0; while (tank.food[k].alive && t_eat < 60 * 15) { SHRIMP_TICK(1); t_eat++; }
        if (tank.food[k].alive || tank.shrimp_food != SHRIMP_PER_JOIN || tank.shrimp_n != SHRIMP_START + 1) { printf("FAIL: the cooldown: count %d, %d shrimp\n", tank.shrimp_food, tank.shrimp_n); return 1; }
        tank_tick_sleep(&tank, SHRIMP_COOLDOWN_S);                     /* the cooldown runs on lived time, asleep too */
        SHRIMP_TICK(1);
        if (tank.shrimp_n != SHRIMP_START + 2 || tank.shrimp_cool < SHRIMP_COOLDOWN_S - 1) { printf("FAIL: after the cooldown %d shrimp (cooldown %.0f)\n", tank.shrimp_n, tank.shrimp_cool); return 1; }
        /* over half the glass fouled: they refuse food - the pellet lies there, the count stays */
        memset(tank.algae, 200, ALGAE_CELLS);
        if (!tank_shrimp_refusing(&tank)) { printf("FAIL: a fouled glass does not put them off food\n"); return 1; }
        tank.shrimp_cool = 0; int before = tank.shrimp_food = 3;
        sx = 0; for (int i = 0; i < tank.shrimp_n; i++) sx += tank.shrimp[i].x; sx /= tank.shrimp_n;
        PUT_PELLET(k, sx, FOOD_FLOOR_Y, 43);
        SHRIMP_TICK(60 * 10);
        if (tank.shrimp_food != before || tank.food[k].nibbled > 0) { printf("FAIL: they ate on a fouled glass (count %d, nibbled %.1f)\n", tank.shrimp_food, tank.food[k].nibbled); return 1; }
        memset(tank.algae, 0, ALGAE_CELLS);
        /* taps (2026-09-29): one on the school is its card, not a scare; the
           third quick tap on it scares them - the water's triple tap, fish and
           all - and so does a triple tap on the water nearby */
        const shrimp_t *qa = &tank.shrimp[0];
        if (!tank_shrimp_hit(&tank, qa->x + 5, qa->y - 5) || tank_shrimp_hit(&tank, 10, 10)) { printf("FAIL: the school's hit test\n"); return 1; }
        int darting = 0;
        tank.shrimp_tap_t = 99; tank.startled = false;
        if (tank_shrimp_tap(&tank, qa->x, qa->y) != 1) { printf("FAIL: a first tap on the school is not 1\n"); return 1; }
        for (int i = 0; i < tank.shrimp_n; i++) darting += tank.shrimp[i].dart > 0;
        if (darting || tank.startled || tank.tap_count) { printf("FAIL: one tap on the school scared them (or counted toward the light)\n"); return 1; }
        SHRIMP_TICK(6);
        if (tank_shrimp_tap(&tank, qa->x, qa->y) != 2) { printf("FAIL: a second quick tap is not 2\n"); return 1; }
        SHRIMP_TICK(6);
        if (tank_shrimp_tap(&tank, qa->x, qa->y) != 3 || !tank.startled) { printf("FAIL: the third quick tap did not startle\n"); return 1; }
        for (int i = 0; i < tank.shrimp_n; i++) darting += tank.shrimp[i].dart > 0;
        if (darting < 2) { printf("FAIL: the triple tap on the school scattered %d\n", darting); return 1; }
        SHRIMP_TICK(60 * 8);                                          /* settle; the startle wears off */
        tank.startled = false; tank.shrimp_tap_t = 99; tank.tap_count = 0; tank.tap_burst_t = 99;
        sx = 0; float sy = 0; for (int i = 0; i < tank.shrimp_n; i++) { sx += tank.shrimp[i].x; sy += tank.shrimp[i].y; } sx /= tank.shrimp_n; sy /= tank.shrimp_n;
        tank_touch_tap(&tank, sx, sy - 40);
        int w1 = 0; for (int i = 0; i < tank.shrimp_n; i++) w1 += tank.shrimp[i].dart > 0;
        if (w1) { printf("FAIL: one tap on the water scattered them\n"); return 1; }
        tank_touch_tap(&tank, sx, sy - 40); tank_touch_tap(&tank, sx, sy - 40);
        int w3 = 0; for (int i = 0; i < tank.shrimp_n; i++) w3 += tank.shrimp[i].dart > 0;
        if (w3 < 2) { printf("FAIL: a triple tap on the water by them scattered %d\n", w3); return 1; }
        tank.startled = false; tank.tap_count = 0; tank.tap_burst_t = 99;
        /* the card: a pip per pellet toward the next shrimp */
        tank.shrimp_food = 7; tank.shrimp_cool = 0;
        render_tank(&tank, fb, TANK_W); render_stats_card(&tank, RENDER_CARD_SHRIMP, fb, TANK_W);
        int pip_px = 0;
        for (int y = (TANK_H - SHRIMP_CARD_H) / 2 + UI(122); y <= (TANK_H - SHRIMP_CARD_H) / 2 + UI(142); y++)   /* the card's pip row */
            for (int x = (TANK_W - SHRIMP_CARD_W) / 2; x < (TANK_W + SHRIMP_CARD_W) / 2; x++) { uint16_t v = fb[y * TANK_W + x]; pip_px += (v >> 11) > 26 && ((v >> 5) & 63) > 40 && (v & 31) < 16; }
        if (pip_px < 7 * 60 * UI_F * UI_F || pip_px > 7 * 200 * UI_F * UI_F) { printf("FAIL: the card's pips (%d pellet pixels for 7)\n", pip_px); return 1; }   /* (this fork: the CYD's card is scaled, UI()) */
        /* drawn: cherry pixels at every shrimp (the decor off for it - an IN FRONT
           castle hides the school behind it, as it does the fish) */
        uint32_t unl = tank.sd_unlocks;
        tank.sd_unlocks &= SD_ITEM_SHRIMP | SD_ITEM_SNAIL;
        for (int b = 0; b < tank_veg_beds(&tank); b++) tank_veg_set(&tank, b, VEG_NUB);   /* and the grass cut: it covers them too */
        render_tank(&tank, fb, TANK_W);
        tank.sd_unlocks = unl;
        int red = 0;                                   /* cherry pixels in each shrimp's box (a front frond may cross it: the grass is their cover) */
        for (int i = 0; i < tank.shrimp_n; i++) {
            int qx = (int)tank.shrimp[i].x, qy = (int)tank.shrimp[i].y;
            for (int y = qy - 4; y <= qy + 4; y++) for (int x = qx - 9; x <= qx + 9; x++) {
                if (x < 0 || y < 0 || x >= TANK_W || y >= TANK_H) continue;
                uint16_t pv = fb[y * TANK_W + x]; int pr = (pv >> 11) << 3, pg = ((pv >> 5) & 63) << 2;
                red += pr >= 60 && pr > 2 * pg;           /* cherry, its shadow and its light (the floor's vignette darkens them) */
            }
        }
        if (red < 25 * tank.shrimp_n) { printf("FAIL: %d cherry pixels for %d shrimp\n", red, tank.shrimp_n); return 1; }
        /* the save keeps the school, its count and its cooldown */
        int keep_n = tank.shrimp_n; tank.shrimp_food = 7; tank.shrimp_cool = 321;
        progression_save(&tank);
        tank_init(&tank, 4243); progression_boot(&tank);
        if (!(tank.sd_unlocks & SD_ITEM_SHRIMP) || tank.shrimp_n != keep_n || tank.shrimp_food != 7 || fabsf(tank.shrimp_cool - 321) > 1) {
            printf("FAIL: the save lost the shrimp (%d of %d, count %d, cooldown %.0f)\n", tank.shrimp_n, keep_n, tank.shrimp_food, tank.shrimp_cool); return 1; }
        /* the full school's badge (2026-09-30): a seventh tank badge, shown only
           with shrimp in the tank, on the TANK row's second page behind the
           arrow at its right end (a tap, or a sideways swipe along the row) */
        {
            const float TY = MS_TANK_Y, ARROW_X = MSP_TPG_X + 14, CELL0 = MS_BADGE_X(0), CELL1 = MS_BADGE_X(1);   /* page coordinates */
            const float SWIPE_X = PG_X(300), SWIPE_Y = PG_Y(TY), FISH_ROW_Y = PG_Y(MS_ROW(2) + MS_BADGE_DY);        /* a swipe is the frame's */
            #define MS_HASH(h) do { render_milestones(&tank, fb, TANK_W); h = 2166136261u; \
                for (int q_ = 0; q_ < TANK_W * TANK_H; q_++) h = (h ^ fb[q_]) * 16777619u; } while (0)
            uint32_t h0, h1, hx;
            uint32_t had_cluster = tank.sd_unlocks & SD_ITEM_CLUSTER;                /* (the reef cluster brings a badge of its own to page 2:
                                                                                        out of the tank while the school's is counted) */
            tank.sd_unlocks &= ~SD_ITEM_CLUSTER;
            if (tank.tank_ms_bits & TMS_FULL_SCHOOL) { printf("FAIL: the full-school badge with %d shrimp\n", tank.shrimp_n); return 1; }
            uint32_t unl = tank.sd_unlocks; tank.sd_unlocks &= ~SD_ITEM_SHRIMP;      /* no shrimp: one page, no arrow */
            render_milestones_leave(); MS_HASH(h0);
            if (ms_tap(ARROW_X, TY) != MS_TAP_NONE || render_milestones_swipe(&tank, SWIPE_X, SWIPE_Y, -60)) { printf("FAIL: the tank row pages without shrimp\n"); return 1; }
            MS_HASH(hx); if (hx != h0) { printf("FAIL: a tap past the badges changed the page without shrimp\n"); return 1; }
            tank.sd_unlocks = unl;
            MS_HASH(h0);
            if (ms_tap(ARROW_X, TY) != MS_TAP_KEPT) { printf("FAIL: the tank row's arrow did not turn the page\n"); return 1; }
            MS_HASH(h1);
            if (h1 == h0) { printf("FAIL: page 2 of the tank row looks like page 1\n"); return 1; }
            if (ms_tap(CELL1, TY) != MS_TAP_NONE) { printf("FAIL: an empty cell on page 2 opened a modal\n"); return 1; }
            if (ms_tap(CELL0, TY) != MS_TAP_KEPT) { printf("FAIL: the full-school badge did not open its modal\n"); return 1; }
            ms_tap(MS_OFF_X, MS_OFF_Y);                              /* close it: page 2 stays */
            MS_HASH(hx); if (hx != h1) { printf("FAIL: closing the modal dropped the tank row's page\n"); return 1; }
            if (ms_tap(ARROW_X, TY) != MS_TAP_KEPT) { printf("FAIL: the arrow did not come back\n"); return 1; }
            MS_HASH(hx); if (hx != h0) { printf("FAIL: the arrow did not come back to page 1\n"); return 1; }
            if (!render_milestones_swipe(&tank, SWIPE_X, SWIPE_Y, -60)) { printf("FAIL: a swipe along the tank row did not page\n"); return 1; }
            MS_HASH(hx); if (hx != h1) { printf("FAIL: a left swipe did not show page 2\n"); return 1; }
            render_milestones_swipe(&tank, SWIPE_X, SWIPE_Y, -60);                       /* no wrap on a swipe */
            MS_HASH(hx); if (hx != h1) { printf("FAIL: a swipe past the last page moved\n"); return 1; }
            if (render_milestones_swipe(&tank, SWIPE_X, FISH_ROW_Y, 60)) { printf("FAIL: a swipe on a fish row paged the tank row\n"); return 1; }
            render_milestones_swipe(&tank, SWIPE_X, SWIPE_Y, 60);
            MS_HASH(hx); if (hx != h0) { printf("FAIL: a right swipe did not come back to page 1\n"); return 1; }
            /* the modal's arrows cross the pages: the sixth badge, one right = the seventh, and the row follows */
            if (ms_tap(MS_BADGE_X(5), TY) != MS_TAP_KEPT) { printf("FAIL: the sixth tank badge did not open\n"); return 1; }
            ms_tap(MS_ARROW_R_X, MS_ARROW_Y);
            ms_tap(MS_OFF_X, MS_OFF_Y);
            MS_HASH(hx); if (hx != h1) { printf("FAIL: the modal's arrow to the seventh badge left the row on page 1\n"); return 1; }
            render_milestones_leave();
            MS_HASH(hx); if (hx != h0) { printf("FAIL: leaving the page kept the tank row's page\n"); return 1; }
            /* ten shrimp earn it */
            int n_keep = tank.shrimp_n; tank.shrimp_n = SHRIMP_MAX;
            progression_tick(&tank, 1.0f / 60.0f);
            if (!(tank.tank_ms_bits & TMS_FULL_SCHOOL)) { printf("FAIL: %d shrimp did not earn the full-school badge\n", SHRIMP_MAX); return 1; }
            tank.shrimp_n = n_keep;
            tank.sd_unlocks |= had_cluster;
            #undef MS_HASH
            printf("selftest-shop: the shrimp: the full-school badge - hidden without shrimp, page 2 of the tank row (arrow, swipe, modal arrows), earned at %d\n", SHRIMP_MAX);
        }
        /* the reef and the seagrass (2026-10-02): "first reef" left the fish's
           badges - the "reef" was only the grass corner - for the TANK row, shown
           once the reef cluster is bought and earned when a fish goes to look at
           it (inspect_reef circles the cluster from then on, and the model's
           `reef` sighting points at it); the fish's badge in its place is its
           first rest inside a seagrass canopy */
        {
            uint32_t unl = tank.sd_unlocks, tms = tank.tank_ms_bits; fish_t keep0 = tank.fish[0]; float cl_x = tank.cluster_x;
            float bed0[VEG_FRONDS_MAX]; memcpy(bed0, tank.veg_h[0], sizeof bed0);
            fish_t *f = &tank.fish[0]; float rx, ry;
            tank.sd_unlocks &= ~(SD_ITEM_CLUSTER | SD_ITEM_SHRIMP); tank.tank_ms_bits &= ~(TMS_FIRST_REEF | TMS_FULL_SCHOOL);
            render_milestones_leave();
            if (ms_tap(MSP_TPG_X + 14, MS_TANK_Y) != MS_TAP_NONE) { printf("FAIL: the tank row pages with no cluster and no shrimp\n"); return 1; }
            tank_reef_spot(&tank, &rx, &ry);
            if (rx != tank.reef_x || ry != tank.reef_y) { printf("FAIL: no cluster, and the reef spot is not the grass corner's (%.0f,%.0f)\n", rx, ry); return 1; }
            f->ms_bits &= ~(MS_INSPECTED | MS_FIRST_GRASS); f->stress = 0;
            f->goal.id = GOAL_INSPECT_REEF; f->goal_age = 5; f->x = rx; f->y = ry - REEF_ORBIT_UP;
            progression_tick(&tank, 1.0f / 60.0f);
            if (!(f->ms_bits & MS_INSPECTED)) { printf("FAIL: a fish at its landmark did not get the quiet inspected bit (the curiosity slider's)\n"); return 1; }
            if (tank.tank_ms_bits & TMS_FIRST_REEF) { printf("FAIL: the reef badge with no reef cluster in the tank\n"); return 1; }
            /* the cluster bought: the spot is the cluster, the old corner earns nothing */
            tank.sd_unlocks |= SD_ITEM_CLUSTER; tank_decor_set(&tank, 4, CLUSTER_X_DEFAULT, DECOR_Z_FRONT);
            tank_reef_spot(&tank, &rx, &ry);
            if (fabsf(rx - tank_decor_x(&tank, 4)) > 0.5f) { printf("FAIL: the reef spot (%.0f) is not the cluster's (%.0f)\n", rx, tank_decor_x(&tank, 4)); return 1; }
            progression_tick(&tank, 1.0f / 60.0f);
            if (tank.tank_ms_bits & TMS_FIRST_REEF) { printf("FAIL: a fish at the old grass corner earned the reef badge\n"); return 1; }
            if (ms_tap(MSP_TPG_X + 14, MS_TANK_Y) != MS_TAP_KEPT) { printf("FAIL: with the cluster bought the tank row has no second page\n"); return 1; }
            if (ms_tap(MS_BADGE_X(0), MS_TANK_Y) != MS_TAP_KEPT) { printf("FAIL: the reef badge did not open its modal\n"); return 1; }
            render_milestones_leave();
            /* a fish that chooses inspect_reef swims to the cluster on its own and earns it */
            bool earned = false; float best = 1e9f; int took = 0;
            for (int k = 0; k < tank.n_fish; k++) { tank.fish[k].x = tank.reef_x + k * 6; tank.fish[k].y = tank.reef_y; }   /* all from the old corner */
            for (; took < 60 * 90 && !earned; took++) {
                tank_tick(&tank, 1.0f / 60.0f, advisor_inspect); progression_tick(&tank, 1.0f / 60.0f);
                for (int k = 0; k < tank.n_fish; k++) { float d = tank_dist(tank.fish[k].x, tank.fish[k].y, rx, ry); if (d < best) best = d; }
                earned = (tank.tank_ms_bits & TMS_FIRST_REEF) != 0;
            }
            if (!earned) { printf("FAIL: 90 s of inspect_reef did not bring a fish to the cluster (closest %.0f px)\n", best); return 1; }
            tank.sd_unlocks &= ~SD_ITEM_CLUSTER;                              /* sold: an earned badge stays on the row */
            if (ms_tap(MSP_TPG_X + 14, MS_TANK_Y) != MS_TAP_KEPT) { printf("FAIL: the earned reef badge left the row with the cluster\n"); return 1; }
            render_milestones_leave();
            /* the seagrass: a rest inside the left bed's canopy earns it; cut to nubs there is no cover to rest in */
            tank_veg_set(&tank, 0, VEG_NUB);
            f = &tank.fish[0]; f->ms_bits &= ~MS_FIRST_GRASS; f->stress = 0;
            f->goal.id = GOAL_REST; f->goal_age = 5; f->x = tank.reef_x + 20; f->y = TANK_BOT - 30;
            progression_tick(&tank, 1.0f / 60.0f);
            if (f->ms_bits & MS_FIRST_GRASS) { printf("FAIL: a rest over stubble earned the seagrass badge\n"); return 1; }
            tank_veg_set(&tank, 0, 0.5f);
            f->goal.id = GOAL_EXPLORE; progression_tick(&tank, 1.0f / 60.0f);
            if (f->ms_bits & MS_FIRST_GRASS) { printf("FAIL: swimming through the grass earned the seagrass badge\n"); return 1; }
            f->goal.id = GOAL_REST; f->goal_age = 5; f->y = TANK_BOT - 300;      /* resting, but above the canopy */
            progression_tick(&tank, 1.0f / 60.0f);
            if (f->ms_bits & MS_FIRST_GRASS) { printf("FAIL: a rest above the canopy earned the seagrass badge\n"); return 1; }
            f->y = TANK_BOT - 30; progression_tick(&tank, 1.0f / 60.0f);
            if (!(f->ms_bits & MS_FIRST_GRASS)) { printf("FAIL: a rest inside the canopy did not earn the seagrass badge\n"); return 1; }
            f->ms_bits &= ~MS_FIRST_GRASS; f->goal.id = GOAL_EXPLORE; f->stress = 7;   /* rattled and tucked in: hiding counts */
            progression_tick(&tank, 1.0f / 60.0f);
            if (!(f->ms_bits & MS_FIRST_GRASS)) { printf("FAIL: hiding in the canopy did not earn the seagrass badge\n"); return 1; }
            memcpy(tank.veg_h[0], bed0, sizeof bed0); tank_veg_sync(&tank);
            tank.fish[0] = keep0; tank.sd_unlocks = unl; tank.tank_ms_bits = tms; tank.cluster_x = cl_x;
            printf("selftest-shop: the reef: no badge without the cluster, the fish leave the old corner for the cluster and earn it in %.0f s, it stays once earned; the seagrass badge wants a rest or a hide inside real cover\n", took / 60.0f);
        }
        tank.trickle_off = false;
#undef SHRIMP_TICK
#undef PUT_PELLET
        printf("selftest-shop: the shrimp: sixth item at %d, no SELL; a pellet rests %.0f s on the floor; pecked clean in %.1f s; "
               "ten bring one (%d -> %d), the cooldown holds the count at ten; a fouled glass puts them off; one tap = the card, the third quick tap scatters %d; saved (%d)\n",
               SD_PRICE_SHRIMP, FOOD_FLOOR_S, t_first / 60.0f, SHRIMP_START, SHRIMP_START + 2, darting, keep_n);
        if (mid_top >= 0) printf("selftest-shop: the shrimp: grass top y %.0f - one above it left alone; one below it taken mid-fall at y %.0f (the floor is %d)\n", mid_top, mid_y, (int)FOOD_FLOOR_Y);
        else printf("selftest-shop: the shrimp: (the mid-fall leg skipped: the grass too short or too tall here)\n");
    }
    /* selling back (2026-09-24): an owned placeable piece's modal has MOVE and
       SELL; SELL arms on the first tap and sells on the second - 20% of the
       price back to the balance (not earnings), the piece gone and reset, the
       row for sale again at full price; the snail's modal has no SELL; the
       placement page's SELL (top left) does the same in two taps; a press on
       a piece finds it (tank_decor_hit: the hold's hit test) */
    {
        static uint16_t fb[TANK_W * TANK_H];
        tank.sd_unlocks |= SD_ITEM_CASTLE | SD_ITEM_SNAIL; tank_castle_place(&tank); tank.sd_balance = 10; int earned = tank.sd_earned;
        if (progression_sell_value(2) != 30 || progression_sell_value(4) != 48 || progression_sell_value(0) != 8) { printf("FAIL: the sale values\n"); return 1; }
        if (progression_sell(&tank, 1)) { printf("FAIL: the snail sold\n"); return 1; }
        render_shop_leave(); render_shop(&tank, fb, TANK_W);
        if (shop_tap(SHOP_ROW_X, SHOP_ROW_Y(2)) != SHOP_TAP_KEPT) { printf("FAIL: the castle's row\n"); return 1; }
        render_shop(&tank, fb, TANK_W);
        const int by = SHOP_BTN_Y, xmove = SHOP_MOVE_X, xsell = SHOP_SELL_X;
        if (shop_tap(xsell, by) != SHOP_TAP_KEPT) { printf("FAIL: the first SELL tap did not arm\n"); return 1; }
        render_shop(&tank, fb, TANK_W);                                   /* armed: +30 OK? */
        int r = shop_tap(xsell, by);
        if (r != SHOP_TAP_SELL + 2) { printf("FAIL: the second SELL tap returned %d\n", r); return 1; }
        if (!progression_sell(&tank, 2) || tank.sd_balance != 40 || tank.sd_earned != earned || (tank.sd_unlocks & SD_ITEM_CASTLE)) { printf("FAIL: the castle's sale (balance %d)\n", tank.sd_balance); return 1; }
        if (progression_sd_take_award() != 30) { printf("FAIL: the sale's toast\n"); return 1; }
        render_shop(&tank, fb, TANK_W); shop_tap(SHOP_ROW_X, SHOP_ROW_Y(2)); render_shop(&tank, fb, TANK_W);
        tank.sd_balance = SD_PRICE_CASTLE;
        if (shop_tap(SHOP_BTN_X, by) != SHOP_TAP_BUY + 2) { printf("FAIL: the sold castle is not for sale again\n"); return 1; }
        if (!progression_buy(&tank, 2) || tank.sd_balance != 0) { printf("FAIL: buying the castle back at full price\n"); return 1; }
        /* MOVE still works beside SELL, and an armed SELL stands down on any other tap */
        render_shop(&tank, fb, TANK_W); shop_tap(SHOP_ROW_X, SHOP_ROW_Y(2)); render_shop(&tank, fb, TANK_W);
        shop_tap(xsell, by); render_shop(&tank, fb, TANK_W);
        if (shop_tap(xmove, by) != SHOP_TAP_MOVE + 2) { printf("FAIL: MOVE beside an armed SELL\n"); return 1; }
        if (tank.sd_unlocks & SD_ITEM_CASTLE) {} else { printf("FAIL: MOVE sold the castle\n"); return 1; }
        render_shop_leave();
        /* the snail's modal: no SELL */
        render_shop(&tank, fb, TANK_W); shop_tap(SHOP_ROW_X, SHOP_ROW_Y(1)); render_shop(&tank, fb, TANK_W);
        if (shop_tap(xsell, by) != SHOP_TAP_KEPT || !(tank.sd_unlocks & SD_ITEM_SNAIL)) { printf("FAIL: the snail's modal sold something\n"); return 1; }
        render_shop_leave();
        /* the hit test: a press on the castle finds it, one on empty water does
           not, and the smaller coral wins where it overlaps the castle */
        tank.sd_unlocks = SD_ITEM_CASTLE | SD_ITEM_SNAIL; tank_decor_set(&tank, 2, 300, DECOR_Z_FRONT);
        const float hx = tank_decor_x(&tank, 2), hy = TANK_BOT - 60, ex = TANK_FX0 + 60;   /* on the castle (where this floor lets it stand); empty floor left of it */
        if (tank_decor_hit(&tank, hx, hy) != 2 || tank_decor_hit(&tank, hx, 40) != -1 || tank_decor_hit(&tank, ex, hy) != -1) {
            printf("FAIL: the decoration hit test (%d %d %d)\n", tank_decor_hit(&tank, hx, hy), tank_decor_hit(&tank, hx, 40), tank_decor_hit(&tank, ex, hy)); return 1; }
        tank.sd_unlocks |= SD_ITEM_CORAL; tank_coral_place(&tank); tank_decor_set(&tank, 3, hx, DECOR_Z_FRONT);
        if (tank_decor_hit(&tank, hx, hy) != 3) { printf("FAIL: the coral over the castle\n"); return 1; }
        tank.sd_unlocks &= ~SD_ITEM_CORAL;
        /* the page's SELL: arm, then sell; the page closes and the castle is gone */
        setup_begin_place(&tank, 2);
        if (pg_hit(SETUP_TOP_BACK_X + UI(20), SETUP_TOP_BTN_Y + UI(20)) != SETUP_HIT_SELL) { printf("FAIL: the page's SELL hit\n"); return 1; }
        setup_activate(&tank, SETUP_HIT_SELL);
        if (!setup_active() || (tank.sd_unlocks & SD_ITEM_CASTLE) == 0) { printf("FAIL: the first page SELL sold\n"); return 1; }
        render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, 1.0f);   /* armed: +30 OK? */
        setup_activate(&tank, SETUP_HIT_SELL);
        if (setup_active() || (tank.sd_unlocks & SD_ITEM_CASTLE) || tank.sd_balance != 30) { printf("FAIL: the page's second SELL (balance %d)\n", tank.sd_balance); return 1; }
        progression_sd_take_award();
        printf("selftest-shop: selling back: the castle for 30 (armed, then sold; earnings untouched), bought again at %d, MOVE beside SELL, the snail unsellable, the hold's hit test, the page's SELL\n", SD_PRICE_CASTLE);
    }
    /* the save carries it all */
    {
        tank.sd_unlocks = SD_ITEM_PLANT | SD_ITEM_SNAIL; tank_veg_set(&tank, 3, 0.62f); tank.snail_x = 123; tank.snail_y = 77; tank.snail_grazed = 321;
        tank.sd_balance = 37; int earned = tank.sd_earned, colonies = tank.algae_colonies; float trim = tank.trim_px;
        progression_save(&tank);
        tank_init(&tank, 4242); progression_boot(&tank); tank.trickle_off = true;
        SHOP_TICK(2);
        if (tank.sd_balance != 37 || tank.sd_earned != earned || tank.sd_unlocks != (SD_ITEM_PLANT | SD_ITEM_SNAIL)
            || tank.algae_colonies != colonies || fabsf(tank.trim_px - trim) > 1 || fabsf(tank.veg_growth[3] - 0.62f) > 0.01f
            || fabsf(tank.snail_x - 123) > 1 || fabsf(tank.snail_y - 77) > 1 || tank_veg_beds(&tank) != VEG_BEDS_MAX) {   /* (it crawls a hair in two ticks) */
            printf("FAIL: the save lost something: balance %d unlocks %x earned %d (was %d) colonies %d (was %d) trim %.0f (was %.0f) bed3 %.2f snail %.0f,%.0f beds %d\n",
                   tank.sd_balance, tank.sd_unlocks, tank.sd_earned, earned, tank.algae_colonies, colonies, tank.trim_px, trim, tank.veg_growth[3], tank.snail_x, tank.snail_y, tank_veg_beds(&tank)); return 1; }
        if (tank.snail_grazed != 321) { printf("FAIL: the save lost the snail's tally (%d)\n", (int)tank.snail_grazed); return 1; }
        printf("selftest-shop: the save round-trip kept the balance, the unlocks, the counters, the plant's height and the snail's spot + tally\n");
        /* a save from before the shop (1480 bytes): no dollars, then the back
           pay - the stages and the trust it already has, once */
        const char *sav = getenv("POCKET_TANK_SAVE");
        if (truncate(sav, 1480)) { printf("FAIL: could not truncate the save to 1480\n"); return 1; }
        tank_init(&tank, 4242); progression_boot(&tank); tank.trickle_off = true;
        if (tank.sd_balance != 0 || tank.sd_unlocks != 0 || tank_veg_beds(&tank) != VEG_BEDS) { printf("FAIL: a pre-shop save came back with dollars / unlocks\n"); return 1; }
        int back = 0;
        for (int i = 0; i < tank.n_fish; i++) {
            const fish_t *f = &tank.fish[i];
            if (f->ms_bits & MS_REACHED_JUV) back += SD_STAGE_JUV;
            if (f->ms_bits & MS_REACHED_ADULT) back += SD_STAGE_ADULT;
            if (f->ms_bits & MS_REACHED_ELDER) back += SD_STAGE_ELDER;
            if (f->trust >= 10.0f) back += SD_TRUST;
        }
        SHOP_TICK(2);
        if (tank.sd_balance != back || tank.sd_earned != back) { printf("FAIL: back pay %d, wanted %d\n", tank.sd_balance, back); return 1; }
        int toast = progression_sd_take_award();
        if (toast != back) { printf("FAIL: the toast got %d, wanted %d\n", toast, back); return 1; }
        SHOP_TICK(60);
        if (tank.sd_balance != back) { printf("FAIL: back pay paid twice (%d)\n", tank.sd_balance); return 1; }
        printf("selftest-shop: a 1480-byte (pre-shop) save loads with 0 dollars, then back-pays %d once (%d fish); the toast took it\n", back, tank.n_fish);
    }
#undef SHOP_TICK
#undef SHOP_WANT
    (void)system(cmd);
    printf("selftest-shop ok\n");
    return 0;
}

/* --selftest-hunger: the hunger economy (tank.c, 2026-09-01). An untended
 * 4-fish tank for 40 minutes of awake time under the rules brain: the
 * hunger-gated trickle must keep the school between "just fed" and
 * "peckish" - never ravenous (that is the after-sleep event), never the
 * old surface-hovering famine - and a keeper's feeding must make a fish
 * properly full. */
static int selftest_hunger(void) {
    tank_init(&tank, 99);
    tank_new_population(&tank);
    while (tank.n_fish < 4) tank_add_fish(&tank, 0, 1);
    for (int i = 0; i < tank.n_fish; i++) tank.fish[i].stage = STAGE_ADULT;
    const float dt = 1.0f / 25.0f;                 /* the device's frame rate */
    int ticks = (int)(40 * 60 / dt), rav_ticks = 0, peckish_ticks = 0, top_ticks = 0;
    float hmax = 0, hsum = 0; int pellets = 0, live_prev = 0;
    for (int i = 0; i < ticks; i++) {
        tank_tick(&tank, dt, advisor_rules);
        progression_tick(&tank, dt);
        int live = 0; for (int k = 0; k < MAX_FOOD; k++) live += tank.food[k].alive;
        if (live > live_prev) pellets += live - live_prev;
        live_prev = live;
        if (tank.ravenous) rav_ticks++;
        for (int k = 0; k < tank.n_fish; k++) {
            float h = tank.fish[k].hunger;
            if (h > hmax) hmax = h;
            hsum += h;
            if (h >= 7) peckish_ticks++;
            if (tank.fish[k].y < 45) top_ticks++;
        }
    }
    float mean = hsum / (ticks * tank.n_fish);
    float peck = 100.0f * peckish_ticks / (ticks * tank.n_fish);
    float top  = 100.0f * top_ticks / (ticks * tank.n_fish);
    printf("hunger: 40 min untended, 4 fish @25 fps: mean %.1f max %.1f | hungry(>=7) %.0f%% of fish-time | "
           "under the surface %.0f%% | trickle pellets %d | ravenous ticks %d\n",
           mean, hmax, peck, top, pellets, rav_ticks);
    if (rav_ticks > 0)  { printf("FAIL: an untended awake tank went ravenous\n"); return 1; }
    if (hmax > 8.6f)    { printf("FAIL: hunger reached %.1f (the trickle didn't keep up)\n", hmax); return 1; }
    if (mean > 7.0f)    { printf("FAIL: mean hunger %.1f - the school lives hungry\n", mean); return 1; }
    if (mean < 2.5f)    { printf("FAIL: mean hunger %.1f - the trickle feeds them for you\n", mean); return 1; }
    if (peck > 40.0f)   { printf("FAIL: fish are hungry %.0f%% of the time\n", peck); return 1; }
    /* the keeper feeds a HUNGRY fish: pellets land, it goes and eats (4.3
       hunger per pellet) and is comfortably fed a minute later. Judged on
       that fish, not the school's mean: the well-fed rest only ever met a
       pellet by wandering into one, which depended on how often the rule
       stub re-rolled their goals - and flipped when the idle re-ask ceiling
       went 9 -> 25 s in the battery pass (a full fish not eating is right). */
    tank.fish[0].hunger = 8.0f;
    float before = tank.fish[0].hunger; int eaten0 = tank.fish[0].eaten;
    tank_feed(&tank, 220, 3); tank_feed(&tank, 260, 3);
    for (int i = 0; i < (int)(60 / dt); i++) { tank_tick(&tank, dt, advisor_rules); progression_tick(&tank, dt); }
    float after = tank.fish[0].hunger, hmin = 10;
    for (int k = 0; k < tank.n_fish; k++) if (tank.fish[k].hunger < hmin) hmin = tank.fish[k].hunger;
    printf("hunger: keeper drops 6 pellets for hungry %s: %d eaten within a minute, hunger %.1f -> %.1f (fullest fish %.1f)\n",
           tank.fish[0].name, tank.fish[0].eaten - eaten0, before, after, hmin);
    if (tank.fish[0].eaten - eaten0 < 1 || after >= 5.0f) { printf("FAIL: the keeper's feeding didn't fill the hungry fish\n"); return 1; }
    /* a meal should LAST: the fullest fish stays under 7 for at least 4 minutes */
    int idx = 0; for (int k = 0; k < tank.n_fish; k++) if (tank.fish[k].hunger < tank.fish[idx].hunger) idx = k;
    for (int i = 0; i < (int)(4 * 60 / dt); i++) { tank_tick(&tank, dt, advisor_rules); progression_tick(&tank, dt); }
    if (tank.fish[idx].hunger >= 7) { printf("FAIL: %s hungry again (%.1f) 4 min after a meal\n", tank.fish[idx].name, tank.fish[idx].hunger); return 1; }
    printf("hunger: %s still %.1f four minutes on\n", tank.fish[idx].name, tank.fish[idx].hunger);
    /* AUTO FEED off (0.3.2): the tank drops nothing, ever. The school goes
       hungry and begs; nobody comes, so they give up begging (no pellets
       fall for it) and go about hungry; a starving fish in a lit tank
       loses trust, slowly, to a floor - none in the dark - and the
       keeper's pellets stop the loss. Back ON, the trickle is back. */
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-hunger.sav", 1); remove(getenv("POCKET_TANK_SAVE"));   /* never the real save */
    tank_init(&tank, 99); progression_boot(&tank);                                 /* progression runs the begging and the trust: a fresh pair */
    tank.autofeed_off = true;
    for (int k = 0; k < tank.n_fish; k++) tank.fish[k].trust = 6.0f;
    int fell = 0, rav = 0, rav_last = 0;
    live_prev = 0; for (int k = 0; k < MAX_FOOD; k++) live_prev += tank.food[k].alive;   /* (a new tank's first two pellets are not the trickle's) */
    for (int i = 0; i < (int)(60 * 60 / dt); i++) {
        tank_tick(&tank, dt, advisor_rules); progression_tick(&tank, dt);
        int live = 0; for (int k = 0; k < MAX_FOOD; k++) live += tank.food[k].alive;
        if (live > live_prev) fell += live - live_prev;
        live_prev = live;
        if (tank.ravenous) { rav++; rav_last = i; }
    }
    float tmax = 0, hlow = 10;
    for (int k = 0; k < tank.n_fish; k++) { if (tank.fish[k].trust > tmax) tmax = tank.fish[k].trust; if (tank.fish[k].hunger < hlow) hlow = tank.fish[k].hunger; }
    printf("hunger: AUTO FEED off, an hour lit: %d pellets fell, least hungry %.1f, begged %.0f s (last at %.0f min), most trust left %.2f\n",
           fell, hlow, rav * dt, rav_last * dt / 60, tmax);
    if (fell) { printf("FAIL: the tank fed itself with AUTO FEED off\n"); return 1; }
    if (hlow < 8.5f) { printf("FAIL: an unfed school is not starving after an hour (%.1f)\n", hlow); return 1; }
    if (!rav || tank.ravenous) { printf("FAIL: with AUTO FEED off the school %s\n", rav ? "never stops begging" : "never begged"); return 1; }
    if (tmax >= 5.9f) { printf("FAIL: starving cost no trust (%.2f)\n", tmax); return 1; }
    for (int i = 0; i < (int)(3 * 60 * 60 / dt); i++) { tank_tick(&tank, dt, advisor_rules); progression_tick(&tank, dt); }
    for (int k = 0; k < tank.n_fish; k++)
        if (tank.fish[k].trust < 2.0f - 0.001f || tank.fish[k].trust > 2.0f + 0.001f) { printf("FAIL: %s's trust %.2f after hours of starving (the floor is 2.0)\n", tank.fish[k].name, tank.fish[k].trust); return 1; }
    for (int k = 0; k < tank.n_fish; k++) tank.fish[k].trust = 6.0f;
    tank_toggle_light(&tank);                                                      /* dark (the first toggle of a lit tank): the fish rest, nothing is lost */
    for (int i = 0; i < (int)(30 * 60 / dt); i++) { tank_tick(&tank, dt, advisor_rules); progression_tick(&tank, dt); }
    for (int k = 0; k < tank.n_fish; k++) if (tank.fish[k].trust < 5.999f) { printf("FAIL: trust lost in the dark (%.2f)\n", tank.fish[k].trust); return 1; }
    tank_light_auto(&tank);
    for (int n = 0; n < 6; n++) {                                                   /* the keeper feeds, generously */
        tank_feed(&tank, 120 + n * 40, 3);
        for (int i = 0; i < (int)(40 / dt); i++) { tank_tick(&tank, dt, advisor_rules); progression_tick(&tank, dt); }
    }
    float t0[N_FISH_MAX]; int fed = 0;
    for (int k = 0; k < tank.n_fish; k++) { t0[k] = tank.fish[k].trust; fed += tank.fish[k].hunger < 8.5f; }
    for (int i = 0; i < (int)(60 / dt); i++) { tank_tick(&tank, dt, advisor_rules); progression_tick(&tank, dt); }
    for (int k = 0; k < tank.n_fish; k++)
        if (tank.fish[k].hunger < 8.0f && tank.fish[k].trust < t0[k] - 0.001f) { printf("FAIL: a fed fish still loses trust\n"); return 1; }
    if (!fed) { printf("FAIL: the keeper's pellets fed nobody\n"); return 1; }
    tank.autofeed_off = false; fell = 0; live_prev = 0;
    for (int k = 0; k < tank.n_fish; k++) tank.fish[k].hunger = 8.0f;
    for (int i = 0; i < (int)(5 * 60 / dt); i++) {
        tank_tick(&tank, dt, advisor_rules); progression_tick(&tank, dt);
        int live = 0; for (int k = 0; k < MAX_FOOD; k++) live += tank.food[k].alive;
        if (live > live_prev) fell += live - live_prev;
        live_prev = live;
    }
    if (!fell) { printf("FAIL: AUTO FEED back on, and nothing fell for a hungry school\n"); return 1; }
    remove(getenv("POCKET_TANK_SAVE"));
    printf("hunger: AUTO FEED off: nothing falls, they beg then give up, trust wears to its floor (lit only), the keeper's pellets stop it; back on, %d fell. selftest-hunger ok\n", fell);
    return 0;
}

/* --bench: headless render-cost profile (per-stage microseconds, averaged
 * over 300 frames) for the scenes that decide the device's frame budget:
 * 4 fish with the canopy at nubs vs fully grown, a fouled glass, and the
 * stats card. The Mac is ~10x the ESP32-S3, but the stage RATIOS carry
 * over - this is how the vegetation and card costs were measured. */
#include <time.h>
static int64_t bench_clock_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
static void bench_scene(const char *label, bool card) {
    static uint16_t fb[TANK_W * TANK_H];
    const int N = 300;
    memset(render_prof_us, 0, sizeof render_prof_us);
    int64_t card_us = 0, total_us = 0;
    for (int i = 0; i < N; i++) {
        tank_tick(&tank, 1.0f / 25.0f, advisor_rules);
        tank.night = false;                        /* day palette, shafts on */
        int64_t t0 = bench_clock_us();
        render_tank(&tank, fb, TANK_W);
        int64_t t1 = bench_clock_us();
        if (card) render_stats_card(&tank, 0, fb, TANK_W);
        int64_t t2 = bench_clock_us();
        card_us += t2 - t1; total_us += t2 - t0;
    }
    printf("%-28s total %6.0f us | scene %5.0f shafts %5.0f veg %5.0f fd/bub %5.0f fish %5.0f vig %5.0f algae %5.0f | card %5.0f\n",
           label, (double)total_us / N, (double)render_prof_us[0] / N, (double)render_prof_us[1] / N,
           (double)render_prof_us[2] / N, (double)render_prof_us[3] / N, (double)render_prof_us[4] / N,
           (double)render_prof_us[5] / N, (double)render_prof_us[6] / N, (double)card_us / N);
}
static int bench(void) {
    static uint16_t scene[TANK_W * TANK_H];
    tank_init(&tank, 77);
    tank_new_population(&tank);
    while (tank.n_fish < 4) tank_add_fish(&tank, 0, 1);
    for (int i = 0; i < tank.n_fish; i++) tank.fish[i].stage = STAGE_ADULT;
    tank.tank_ms_bits = 0x1a7;
    render_set_scene_cache(scene);
    render_set_vignette_cache(vig_buf);
    render_set_card_cache(card_buf);
    render_set_dirty_mask(dirty_buf);
    render_clock_us = bench_clock_us;
    for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(&tank, b, VEG_NUB);
    bench_scene("4 fish, canopy at nubs", false);
    for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(&tank, b, 1.0f);
    bench_scene("4 fish, canopy full", false);
    bench_scene("4 fish, canopy full + card", true);
    tank_grow_algae(&tank, 400);
    bench_scene("... + fouled glass", false);
    return 0;
}

/* --selftest-card [prefix] (2026-10-01): the milestones page's fish card -
 * RENAME (the letter wheel, CANCEL / DONE, the way back to the card) and SELL
 * (two taps, the doubled tap refused, the price by stage, the slots moving
 * down, the last pair kept, a welcome owed holding every sale, the next fry
 * waiting for SELL_FRY_MEALS more meals, all of it through a save).
 * Written for EVERY board: each tap asks the page where its targets are
 * (render_milestones_row / render_milestones_card, the setup page's own
 * geometry through PAGE_X / PAGE_Y), so this passes unchanged on the
 * rectangle, the bowl and the watch. With a prefix it also writes the card,
 * the armed card and the rename page as PPMs - the same three pictures per
 * board, to be looked at side by side before anything is flashed. */
#define CARD_FAIL(...) do { printf("FAIL: " __VA_ARGS__); printf("\n"); return 1; } while (0)
static int selftest_card(const char *prefix) {
    const float DT = 1.0f / 60.0f;
    static uint16_t fb[TANK_W * TANK_H];
    char path[300];
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-card.sav", 1);
    (void)system("rm -f /tmp/pocket-tank-selftest-card.sav");
    tank_init(&tank, 77);
    progression_boot(&tank);                                  /* no save: the founding pair */
    progression_setup_done(&tank);
    if (tank.n_fish != 2) CARD_FAIL("a new tank should start with 2 fish (%d)", tank.n_fish);
    int nx, ny, fish, rx, sx, by;

    /* the last pair is not for sale: SELL is dim, two taps on it sell nothing - the first brings the reason up */
    render_milestones(&tank, fb, TANK_W);
    render_milestones_row(0, &nx, &ny);
    if (render_milestones_tap(&tank, (float)nx, (float)ny) != MS_TAP_KEPT) CARD_FAIL("fish 0's name did not open its card");
    render_milestones(&tank, fb, TANK_W);
    if (!render_milestones_card(&tank, &fish, &rx, &sx, &by) || fish != 0) CARD_FAIL("no card up for fish 0");
    if (progression_fish_sellable(&tank, 0) || progression_sell_fish(&tank, 0)) CARD_FAIL("one of the last pair was for sale");
    if (prefix) { snprintf(path, sizeof path, "%s_card_pair.ppm", prefix); write_ppm(path, fb); }
    uint32_t hq = 2166136261u; for (int q = 0; q < TANK_W * TANK_H; q++) hq = (hq ^ fb[q]) * 16777619u;
    if (render_milestones_tap(&tank, (float)sx, (float)by) != MS_TAP_KEPT) CARD_FAIL("SELL on the last pair did something");
    render_milestones(&tank, fb, TANK_W);                     /* the reason shows only once the dim SELL is tapped */
    if (prefix) { snprintf(path, sizeof path, "%s_card_pair_asked.ppm", prefix); write_ppm(path, fb); }
    uint32_t ha = 2166136261u; for (int q = 0; q < TANK_W * TANK_H; q++) ha = (ha ^ fb[q]) * 16777619u;
    if (ha == hq) CARD_FAIL("the dim SELL's tap did not bring its reason up");
    { int by2 = -1; if (!render_milestones_card(&tank, NULL, NULL, NULL, &by2) || by2 != by) CARD_FAIL("the reason moved the buttons (%d -> %d)", by, by2); }
    tank.clock += 2;
    if (render_milestones_tap(&tank, (float)sx, (float)by) != MS_TAP_KEPT || tank.n_fish != 2) CARD_FAIL("a second tap on the dim SELL did something");
    if (!render_milestones_card(&tank, NULL, NULL, NULL, NULL)) CARD_FAIL("the dim SELL closed the card");
    render_milestones_leave();

    /* five fish: an elder, an adult, a juvenile, an adult and a fry */
    for (int i = 0; i < 3; i++) {
        progression_force_arrival(&tank);
        if (i == 2 && progression_fish_sellable(&tank, 0)) CARD_FAIL("a sale was open while a fry is owed its welcome");
        progression_newborn_done(&tank);
    }
    if (tank.n_fish != 5) CARD_FAIL("three arrivals should make 5 fish (%d)", tank.n_fish);
    static const char *const NM[5] = { "ada", "bo", "cy", "dee", "eli" };
    static const float AGE[5] = { STAGE_ELDER_AGE + 60, STAGE_ADULT_AGE + 60, STAGE_JUV_AGE + 60, STAGE_ADULT_AGE + 120, 60 };
    static const int WORTH[5] = { SD_FISH_ELDER, SD_FISH_ADULT, SD_FISH_JUV, SD_FISH_ADULT, SD_FISH_FRY };
    for (int i = 0; i < 5; i++) {
        tank_set_name(&tank, i, NM[i]); progression_set_age(&tank, i, AGE[i]);
        tank.fish[i].trust = 9; tank.fish[i].hunger = 2; tank.sd_paid_fish[i] = 0x100u << i;
        if (progression_fish_value(&tank, i) != WORTH[i]) CARD_FAIL("%s is worth %d, want %d", NM[i], progression_fish_value(&tank, i), WORTH[i]);
    }
    if (!(SD_FISH_FRY < SD_FISH_JUV && SD_FISH_JUV < SD_FISH_ADULT && SD_FISH_ADULT < SD_FISH_ELDER)) CARD_FAIL("an older fish must be worth more");
    tank.fish[4].parent_a = 1; tank.fish[4].parent_b = 3;     /* eli: bo's and dee's */
    tank.fish[3].parent_a = 0; tank.fish[3].parent_b = 1;
    tank.player_feedings = 150;
    memset(tank.algae, 0, sizeof tank.algae);
    progression_save(&tank);

    /* RENAME: the card's left button -> the wheel over the live tank */
    render_milestones(&tank, fb, TANK_W);
    render_milestones_row(2, &nx, &ny);
    if (render_milestones_tap(&tank, (float)nx, (float)ny) != MS_TAP_KEPT) CARD_FAIL("cy's name did not open its card");
    render_milestones(&tank, fb, TANK_W);
    if (prefix) { snprintf(path, sizeof path, "%s_card.ppm", prefix); write_ppm(path, fb); }
    if (!render_milestones_card(&tank, &fish, &rx, &sx, &by) || fish != 2) CARD_FAIL("no card up for cy");
    if (rx < 0 || sx >= TANK_W || by < 0 || by >= TANK_H) CARD_FAIL("the card's buttons are off the glass (%d %d %d)", rx, sx, by);
    int r = render_milestones_tap(&tank, (float)rx, (float)by);
    if (r != MS_TAP_RENAME + 2) CARD_FAIL("RENAME returned %d", r);
    render_milestones_leave();
    setup_begin_rename(&tank, 2);
    setup_touch(&tank, 0, 0, false);
    if (!setup_active() || !setup_is_rename() || setup_page() != SETUP_PG_RENAME || setup_fish() != 2 || tank.stage_fish != 2)
        CARD_FAIL("the rename page did not open on cy (active %d, page %d, fish %d, stage %d)", setup_active(), setup_page(), setup_fish(), tank.stage_fish);
    for (int i = 0; i < 60 * 12; i++) { tank_tick(&tank, DT, advisor_rules); setup_touch(&tank, 0, 0, false); }   /* cy swims to its stage */
    if (tank_dist(tank.fish[2].x, tank.fish[2].y, tank.stage_x, tank.stage_y) > 60) CARD_FAIL("cy is not on its stage after 12 s (%.0f px off)", tank_dist(tank.fish[2].x, tank.fish[2].y, tank.stage_x, tank.stage_y));
    setup_activate(&tank, SETUP_HIT_UP);                      /* c -> d */
    if (strcmp(tank.fish[2].name, "dy")) CARD_FAIL("the wheel made '%s' of cy", tank.fish[2].name);
    render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, tank.clock);
    if (prefix) { snprintf(path, sizeof path, "%s_rename.ppm", prefix); write_ppm(path, fb); }
    /* CANCEL (a tap through the touch path, top left): the old name back, the card owed */
    setup_touch(&tank, PAGE_X + SETUP_TOP_BACK_X + 20, PAGE_Y + SETUP_TOP_BTN_Y + 20, true);
    setup_touch(&tank, PAGE_X + SETUP_TOP_BACK_X + 22, PAGE_Y + SETUP_TOP_BTN_Y + 22, false);
    if (setup_active() || strcmp(tank.fish[2].name, "cy") || tank.stage_fish != -1) CARD_FAIL("CANCEL left '%s' (active %d)", tank.fish[2].name, setup_active());
    if (setup_take_renamed() != 2 || setup_take_renamed() != -1) CARD_FAIL("CANCEL did not hand the card back, once");
    /* DONE (top right) keeps it and saves it; an emptied name is the preset's again */
    setup_begin_rename(&tank, 2);
    setup_activate(&tank, SETUP_HIT_UP);
    setup_touch(&tank, PAGE_X + SETUP_TOP_NEXT_X + 20, PAGE_Y + SETUP_TOP_BTN_Y + 20, true);
    setup_touch(&tank, PAGE_X + SETUP_TOP_NEXT_X + 22, PAGE_Y + SETUP_TOP_BTN_Y + 22, false);
    if (setup_active() || strcmp(tank.fish[2].name, "dy")) CARD_FAIL("DONE left '%s' (active %d)", tank.fish[2].name, setup_active());
    if (setup_take_renamed() != 2) CARD_FAIL("DONE did not hand the card back");
    render_milestones_show_fish(&tank, 2);
    if (!render_milestones_card(&tank, &fish, NULL, NULL, NULL) || fish != 2) CARD_FAIL("the card did not come back after the rename");
    render_milestones_leave();
    tank_init(&tank, 78); progression_boot(&tank);
    if (tank.n_fish != 5 || strcmp(tank.fish[2].name, "dy")) CARD_FAIL("the reload lost the new name ('%s', %d fish)", tank.fish[2].name, tank.n_fish);
    setup_begin_rename(&tank, 2);
    setup_activate(&tank, SETUP_HIT_DOWN); setup_activate(&tank, SETUP_HIT_DOWN); setup_activate(&tank, SETUP_HIT_DOWN); setup_activate(&tank, SETUP_HIT_DOWN);   /* d c b a blank */
    setup_activate(&tank, SETUP_HIT_SLOT0 + 1);
    for (int i = 0; i < 25; i++) setup_activate(&tank, SETUP_HIT_DOWN);                /* y ... blank */
    setup_activate(&tank, SETUP_HIT_NEXT);
    if (strcmp(tank.fish[2].name, tank_roster_name(tank.fish[2].preset))) CARD_FAIL("an emptied name became '%s', not the preset's", tank.fish[2].name);
    (void)setup_take_renamed();
    setup_begin_rename(&tank, 4); setup_activate(&tank, SETUP_HIT_UP); setup_cancel(&tank);   /* dropped from outside: the old name, no card owed */
    if (setup_active() || strcmp(tank.fish[4].name, "eli") || setup_take_renamed() != -1) CARD_FAIL("a dropped rename left '%s'", tank.fish[4].name);
    tank_set_name(&tank, 2, "cy");
    printf("selftest-card: RENAME: the card's button, the wheel on the staged fish, CANCEL restores, DONE saves (reloaded), an empty name = the preset's\n");

    /* SELL: bo, the adult in slot 1 - two taps, and not a doubled one */
    for (int i = 0; i < 5; i++) tank.sd_paid_fish[i] = 0x100u << i;
    tank.fish[4].parent_a = 1; tank.fish[4].parent_b = 3; tank.fish[3].parent_a = 0; tank.fish[3].parent_b = 1;
    float age2 = progression_age_s(&tank, 2), rest3 = tank.fish[3].rest_dx;
    int bal0 = tank.sd_balance, earned0 = tank.sd_earned; uint8_t gen0 = tank.roster_gen;
    (void)progression_sd_take_award();
    progression_stage_arrival(&tank);                         /* (a sixth fish staged: the sim's cap is 6) */
    render_milestones(&tank, fb, TANK_W);
    render_milestones_row(1, &nx, &ny);
    if (render_milestones_tap(&tank, (float)nx, (float)ny) != MS_TAP_KEPT) CARD_FAIL("bo's name did not open its card");
    render_milestones(&tank, fb, TANK_W);
    if (!render_milestones_card(&tank, &fish, &rx, &sx, &by) || fish != 1) CARD_FAIL("no card up for bo");
    if (render_milestones_tap(&tank, (float)sx, (float)by) != MS_TAP_KEPT || tank.n_fish != 5) CARD_FAIL("the first SELL tap did more than arm");
    render_milestones(&tank, fb, TANK_W);
    if (prefix) { snprintf(path, sizeof path, "%s_card_armed.ppm", prefix); write_ppm(path, fb); }
    if (render_milestones_tap(&tank, (float)sx, (float)by) != MS_TAP_KEPT) CARD_FAIL("a doubled tap sold the fish");
    tank.clock += 1.0f;
    r = render_milestones_tap(&tank, (float)sx, (float)by);
    if (r != MS_TAP_SELL + 1) CARD_FAIL("the second SELL tap returned %d", r);
    if (render_milestones_card(&tank, NULL, NULL, NULL, NULL)) CARD_FAIL("the card stayed up after the sale's tap");
    if (!progression_sell_fish(&tank, 1)) CARD_FAIL("the sale was refused");
    if (tank.n_fish != 4 || strcmp(tank.fish[0].name, "ada") || strcmp(tank.fish[1].name, "cy") || strcmp(tank.fish[2].name, "dee") || strcmp(tank.fish[3].name, "eli"))
        CARD_FAIL("after the sale: %d fish, %s %s %s %s", tank.n_fish, tank.fish[0].name, tank.fish[1].name, tank.fish[2].name, tank.fish[3].name);
    if (tank.sd_balance != bal0 + SD_FISH_ADULT || tank.sd_earned != earned0 + SD_FISH_ADULT || progression_sd_take_award() != SD_FISH_ADULT)
        CARD_FAIL("the sale paid %d (earned %d), want %d", tank.sd_balance - bal0, tank.sd_earned - earned0, SD_FISH_ADULT);
    if (progression_age_s(&tank, 1) != age2 || tank.fish[1].stage != STAGE_JUV) CARD_FAIL("cy's clock did not move down with it");
    if (tank.sd_paid_fish[0] != 0x100u || tank.sd_paid_fish[1] != 0x400u || tank.sd_paid_fish[2] != 0x800u || tank.sd_paid_fish[3] != 0x1000u || tank.sd_paid_fish[4] != 0)
        CARD_FAIL("the paid ledger did not move down");
    if (tank.fish[3].parent_a != -1 || tank.fish[3].parent_b != 2 || tank.fish[2].parent_a != 0 || tank.fish[2].parent_b != -1)
        CARD_FAIL("the parents' slots: eli %d/%d, dee %d/%d", tank.fish[3].parent_a, tank.fish[3].parent_b, tank.fish[2].parent_a, tank.fish[2].parent_b);
    if (fabsf(tank.fish[2].rest_dx - (rest3 - 24)) > 1e-3f) CARD_FAIL("dee's spot by the reef did not move a slot nearer");
    if (tank.roster_gen == gen0) CARD_FAIL("the roster's generation did not tick");
    if (progression_arrival_pending() || tank.spawning) CARD_FAIL("the staged fry was not called off by the sale");
    printf("selftest-card: SELL: armed by one tap, a doubled tap refused, the second sold bo for %d; slots, clocks, ledger and parents moved down\n", SD_FISH_ADULT);

    /* the slot is earned again: every other gate met, the fry still waits for SELL_FRY_MEALS meals */
    progression_set_age(&tank, 3, STAGE_ADULT_AGE + 60);
    for (int i = 0; i < tank.n_fish; i++) { tank.fish[i].trust = 9; tank.fish[i].hunger = 2; }
    memset(tank.algae, 0, sizeof tank.algae);
    {
        fry_req_t req[FRY_REQ_MAX]; bool staged; int n = progression_next_fry(&tank, req, &staged), feed = -1, met = 0;
        for (int i = 0; i < n; i++) { if (req[i].kind == FRY_REQ_FEED) feed = i; met += req[i].met; }
        if (feed < 0 || req[feed].met || met != n - 1 || staged) CARD_FAIL("after a sale only MEALS should be owed (%d of %d met, staged %d)", met, n, staged);
        char want[28]; snprintf(want, sizeof want, "%d OF %d SO FAR", 150, 150 + SELL_FRY_MEALS);
        if (strcmp(req[feed].progress, want)) CARD_FAIL("the MEALS gate reads '%s', want '%s'", req[feed].progress, want);
    }
    for (int i = 0; i < 60 * 90; i++) {
        tank_tick(&tank, DT, advisor_rules); progression_tick(&tank, DT);
        for (int k = 0; k < tank.n_fish; k++) { tank.fish[k].hunger = 2; tank.fish[k].trust = 9; }
        memset(tank.algae, 0, sizeof tank.algae);
    }
    if (tank.n_fish != 4 || progression_arrival_pending()) CARD_FAIL("a fry came back within 90 s of the sale (%d fish, pending %d)", tank.n_fish, progression_arrival_pending());
    tank_init(&tank, 79); progression_boot(&tank);            /* ... and through a save */
    if (tank.n_fish != 4 || strcmp(tank.fish[1].name, "cy") || strcmp(tank.fish[3].name, "eli")) CARD_FAIL("the reload after the sale: %d fish, %s .. %s", tank.n_fish, tank.fish[1].name, tank.fish[3].name);
    if (tank.fish[3].parent_a != -1 || tank.fish[3].parent_b != 2) CARD_FAIL("the reload lost the moved parents (%d/%d)", tank.fish[3].parent_a, tank.fish[3].parent_b);
    for (int i = 0; i < tank.n_fish; i++) { tank.fish[i].trust = 9; tank.fish[i].hunger = 2; }
    memset(tank.algae, 0, sizeof tank.algae); for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(&tank, b, 0.8f);
    tank.player_feedings = 150 + SELL_FRY_MEALS - 1;
    progression_tick(&tank, DT);
    if (progression_arrival_pending()) CARD_FAIL("the meals owed since the sale did not survive the save");
    tank.player_feedings = 150 + SELL_FRY_MEALS;
    progression_tick(&tank, DT);
    if (!progression_arrival_pending()) CARD_FAIL("%d meals after the sale the fry was still not staged", SELL_FRY_MEALS);
    progression_force_arrival(&tank); progression_newborn_done(&tank);
    if (tank.n_fish != 5 || tank.fish[4].stage != STAGE_FRY || progression_age_s(&tank, 4) != 0) CARD_FAIL("the new fry: %d fish, stage %d", tank.n_fish, tank.fish[4].stage);
    printf("selftest-card: the next fry waited for %d more meals (saved with the tank), then came as fish %d\n", SELL_FRY_MEALS, tank.n_fish);

    /* down to the pair, never past it */
    while (tank.n_fish > FISH_KEEP_MIN) if (!progression_sell_fish(&tank, tank.n_fish - 1)) CARD_FAIL("a sale above the pair was refused at %d fish", tank.n_fish);
    if (progression_sell_fish(&tank, 0) || progression_sell_fish(&tank, 1) || tank.n_fish != FISH_KEEP_MIN) CARD_FAIL("the pair was sold");
    for (int i = 0; i < 600; i++) { tank_tick(&tank, DT, advisor_rules); progression_tick(&tank, DT); notice_tick(&tank, DT, false); }
    render_tank(&tank, fb, TANK_W); render_milestones(&tank, fb, TANK_W);
    printf("selftest-card: sold down to the pair, which stays; the tank runs on\nselftest-card: OK\n");
    return 0;
}

int main(int argc, char **argv) {
#ifdef CONFIG_POCKET_TANK_320X240
    render_settings_set_sleep(SET_SLEEP_LIGHT);   /* (this fork) a 320x240 board's SLEEP row starts where its build does (sdkconfig.defaults.cyd and .wst) */
#endif
    for (int a = 1; a < argc; a++)
        if (strcmp(argv[a], "--greedy") == 0) advisor_core_sample = false;
    for (int a = 1; a < argc; a++) {                 /* mode flags may sit anywhere */
        if (strcmp(argv[a], "--hero") == 0 && a + 1 < argc) return hero_shot(argv[a + 1]);
        if (strcmp(argv[a], "--clip") == 0 && a + 1 < argc)
            return clip(argv[a + 1], a + 2 < argc ? atoi(argv[a + 2]) : 6);
        if (strcmp(argv[a], "--snapshot") == 0 && a + 1 < argc)
            return snapshot(argv[a + 1], a + 2 < argc ? atoi(argv[a + 2]) : 20);
        if (strcmp(argv[a], "--selftest") == 0) return selftest();
        if (strcmp(argv[a], "--bench") == 0) return bench();
        if (strcmp(argv[a], "--selftest-hunger") == 0) return selftest_hunger();
        if (strcmp(argv[a], "--selftest-shop") == 0) return selftest_shop();
        if (strcmp(argv[a], "--selftest-battery") == 0) return selftest_battery();
        if (strcmp(argv[a], "--selftest-saves") == 0) return selftest_saves();
        if (strcmp(argv[a], "--selftest-pop") == 0) return selftest_pop();
        if (strcmp(argv[a], "--selftest-card") == 0) return selftest_card(a + 1 < argc ? argv[a + 1] : NULL);
        if (strcmp(argv[a], "--selftest-sleep") == 0) return selftest_sleep();
        if (strcmp(argv[a], "--selftest-tend") == 0) return selftest_tend();
        if (strcmp(argv[a], "--selftest-update") == 0) return selftest_update();
        if (strcmp(argv[a], "--selftest-llm") == 0)
            return selftest_llm(a + 1 < argc ? atoi(argv[a + 1]) : 0);
    }

    tank_init(&tank, (uint32_t)SDL_GetTicks() + 7);
    battery_init(&sim_bat, NULL);
    bool fresh = false;
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--fresh") == 0) fresh = true;
        if (strcmp(argv[a], "--battery") == 0 && a + 1 < argc) sim_bat_pct = fminf(100, fmaxf(0, (float)atof(argv[++a])));
        if (strcmp(argv[a], "--fast") == 0 && a + 1 < argc) progression_time_scale = (float)atof(argv[++a]);
    }
    if (fresh) {
        char cmd[600]; snprintf(cmd, sizeof cmd, "rm -f '%s/.cache/pocket-tank/tank.sav'", getenv("HOME") ? getenv("HOME") : ".");
        (void)system(cmd);
    }
    progression_boot(&tank);               /* restore, or a new random pair */
    { uint32_t r = progression_loaded_release();
      printf("pocket-tank v%s %s (build %s); the save was written by %s", PT_RELEASE, PT_RELEASE_STAGE, version_port_string(), r ? "" : "a build before release numbers (or there was none)\n");
      if (r) printf("v%d.%d.%d\n", (int)(r >> 16), (int)(r >> 8 & 255), (int)(r & 255)); }
    print_roster(&tank);
    notice_sync(&tank);                    /* nothing old gets announced */
    sound_init();
    if (progression_setup_pending()) { setup_begin(&tank); printf("first-run setup: welcome, names, colours (S re-opens it)\n"); }
    for (int a = 1; a < argc; a++)
        if (strcmp(argv[a], "--narrate") == 0) {
            advisor_llm_narrate = true;   /* film the tank + this terminal */
            llm_active = true;
        }
    llm_available = advisor_llm_init("../model/out/model_q4.bin",
                                     "../model/out/tokenizer.bin");
    printf(llm_available
           ? "LLM advisor loaded (press L to toggle rule/LLM brain)\n"
           : "model.bin/tokenizer.bin not found; rule brain only\n");
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_sdl_window_create(TANK_W, TANK_H);
    lv_sdl_window_set_title(disp, "pocket-tank sim 448x368");

    lv_draw_buf_init(&draw_buf, TANK_W, TANK_H, LV_COLOR_FORMAT_RGB565,
                     TANK_W * 2, canvas_buf, sizeof(canvas_buf));
    canvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_draw_buf(canvas, &draw_buf);
    lv_obj_center(canvas);
    render_set_scene_cache(scene_buf);
    render_set_vignette_cache(vig_buf);
    render_set_card_cache(card_buf);
    render_set_dirty_mask(dirty_buf);

    last_ms = SDL_GetTicks();
    lv_timer_create(frame_cb, 16, NULL);

    bool fdown = false, ndown = false, ldown = false;
    bool udown = false, mdown = false, mkdown = false, rdown = false, zdown = false, gdown = false, xdown = false, sdown = false, vdown = false, bdown = false, fourdown = false, ddown = false;
    bool cdown = false;             /* C: the castle prototype */
    bool pdown = false;             /* P: the pretend battery's cable */
    bool kdown = false;             /* K: the coral (colours cycle) */
    bool jdown = false;             /* J: the coral's growth, a step */
    bool idown = false, odown = false;   /* I: the reef cluster (looks cycle); O: its growth */
    bool wdown = false;                  /* W: the shrimp school (grant; again +1; past the last takes it away) */
    bool ydown = false;                  /* Y: the urchin (grant; again takes it away) */
    bool held_page = false;              /* this press opened a piece's page by holding on it */
    uint32_t press_ms = 0; int press_x = 0, press_y = 0;
    float press_fx[N_FISH_MAX] = {0}, press_fy[N_FISH_MAX] = {0};
    while (1) {
        uint32_t wait = lv_timer_handler();
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        int mx, my;
        bool mpress = SDL_GetMouseState(&mx, &my) & SDL_BUTTON(SDL_BUTTON_LEFT);
        if (sim_view_turned()) { mx = TANK_W - 1 - mx; my = TANK_H - 1 - my; }   /* the window shows the glass turned: the finger is too */
        { static int lmx = -1, lmy = -1;            /* a hand near the tank: the mouse moving over the window */
          if (mx != lmx || my != lmy || mpress || k[SDL_SCANCODE_H]) tank_handled(&tank);
          lmx = mx; lmy = my; }
        uint32_t now_ms = SDL_GetTicks();
        /* mouse -> touch gestures (device: FT3168 does the same job)
         *   press+release < 350 ms, little movement: TAP (on a fish = select its card;
         *                                              card up + empty glass = dismiss;
         *                                              on the surface = feed)
         *   drag down >= 40 px starting near the top: FEED at that x
         *   held > 300 ms: HOLD (finger resting on the glass) */
        if (mpress && !mdown) {
            press_ms = now_ms; press_x = mx; press_y = my;
            for (int i = 0; i < tank.n_fish; i++) { press_fx[i] = tank.fish[i].x; press_fy[i] = tank.fish[i].y; }
        }
        bool setup_up = setup_active();          /* before the touch: BEGIN's release must not become a tank tap */
        if (update_mode) {                       /* update mode owns the glass; nothing else sees the finger */
            update_touch((float)mx, (float)my, mpress);
            mdown = mpress;
            if (wait < 5) wait = 5;
            SDL_Delay(wait);
            continue;
        }
        if (setup_up) {
            bool birth = setup_is_birth(), rename = setup_is_rename(); int who = setup_fish(), place = setup_item();
            setup_touch(&tank, (float)mx, (float)my, mpress);   /* taps and the letter wheel, classified in setup.c */
            if (!setup_active()) {
                if (rename) printf("rename closed: the fish is %s\n", who >= 0 && who < tank.n_fish ? tank.fish[who].name : "?");
                else if (place >= 0) printf("placed: %s at x %.0f, %s layer, saved\n", SD_ITEMS[place].name, tank_decor_x(&tank, place),
                                       tank_decor_z(&tank, place) == DECOR_Z_BACK ? "BEHIND" : tank_decor_z(&tank, place) == DECOR_Z_FRONT ? "IN FRONT" : "AMONG");
                else { printf(birth ? "birth flow done: %s named and saved\n" : "setup done\n", who >= 0 ? tank.fish[who].name : "?"); print_roster(&tank); }
            }
        } else if (settings_view && !confirm_view) {             /* the settings page: segments, the seconds wheel, CLOSE */
            int v = 0, r = render_settings_touch(&tank, (float)mx, (float)my, mpress, &v);
            if (r == SET_TAP_CLOSE) { settings_view = false; milestones_view = true; ms_back = true; }   /* back to the milestones page */
            else if (r == SET_TAP_UPDATES) { settings_view = false; updates_view = true; ms_back = true; printf("updates page\n"); }
            else if (r == SET_TAP_BRIGHT) sim_bright = v;
            else if (r == SET_TAP_VOLUME) { if (s_adev) { SDL_LockAudioDevice(s_adev); audio_set_volume(v); SDL_UnlockAudioDevice(s_adev); } if (v) snd(SND_CONFIRM, AUDIO_PITCH_ONE);
                                            printf("volume: %s\n", v == 0 ? "off" : v == 1 ? "quiet" : "normal"); }
            else if (r == SET_TAP_LIGHT) printf("lights out: %s\n", v ? "AUTO (the idle rule)" : "MANUAL (double-tap the glass, the default)");
            else if (r == SET_TAP_SCREEN) printf("screen: %s\n", v ? "TURNED" : "NORMAL");
            else if (r == SET_TAP_IDLE) printf("lights out after %d s still\n", v);
            else if (r == SET_TAP_FEED) printf("auto feed: %s\n", v ? "ON" : "OFF");
            else if (r == SET_TAP_ROTATE) printf("rotation: %s\n", v ? "LOCKED" : "unlocked");
#ifdef CONFIG_POCKET_TANK_320X240
            else if (r == SET_TAP_SLEEP) {                     /* (this fork) a 320x240 board's SLEEP row: the firmware keeps it in NVS */
                render_settings_set_sleep(v);
                const char *name = "LIGHT";
                if (v == SET_SLEEP_NEVER) name = "NEVER";
                else if (v == SET_SLEEP_SCREEN) name = "SCREEN";
                printf("sleep: %s\n", name);
            }
#endif
        } else if (updates_view && !confirm_view) {              /* the UPDATES page: CHECK, FORGET, CLOSE */
            int r = updates_page_touch((float)mx, (float)my, mpress);
            if (r == UPD_TAP_CLOSE) { updates_view = false; settings_view = true; ms_back = true; }   /* back to the settings page */
            else if (r == UPD_TAP_FORGET) printf("updates: network forgotten\n");
            else if (r == UPD_TAP_CHECK) {                     /* the device saves and restarts here */
                progression_save(&tank); updates_view = false; update_mode = true; update_clock = 0;
                update_begin(sim_bat_gauge(), sim_bat_state != BAT_ON_BATTERY);
                printf("update mode: the tank would save and restart now (pretend radio; POCKET_TANK_FAKE_UPDATE steers the check)\n");
            }
        }
        bool modal = confirm_view || setup_up || settings_view || shop_view || battery_view || updates_view;
        if (mpress && !modal) tank_touch_drag(&tank, (float)mx, (float)my);   /* stroke -> wipe/slash */
        if (mpress && !modal && !held_page && tank.tool == TOOL_HAND && now_ms - press_ms > 700 && abs(mx - press_x) < 24 && abs(my - press_y) < 24) {   /* tap-and-hold on a piece: its page (2026-09-24); not with a tool in hand */
            int it = tank_decor_hit(&tank, (float)press_x, (float)press_y);
            if (it >= 0) { setup_begin_place(&tank, it); held_page = true; selected_fish = -1; printf("held on the %s: placement page up (MOVE / DEPTH / SELL)\n", SD_ITEMS[it].name); }
        }
        if (!mpress) held_page = false;
        if (mpress && !modal && !held_page && now_ms - press_ms > 300 && abs(my - press_y) < 30) tank_touch_hold(&tank, (float)mx, (float)my);
        if (!mpress && mdown) {
            int dx = mx - press_x, dy = my - press_y;
            if (confirm_view) {                    /* the prompt owns the glass: press AND release on one button */
                int h = press_ms > confirm_ms ? render_confirm_hit((float)press_x, (float)press_y) : 0;
                if (h && h == render_confirm_hit((float)mx, (float)my)) {
                    confirm_view = false;
                    if (h > 0) { progression_reset(&tank, SDL_GetTicks() + 7); selected_fish = -1; notice_sync(&tank);
                                 printf("RESET: a fresh tank\n"); print_roster(&tank); setup_begin(&tank); }
                    else printf("reset prompt: NO, tank kept\n");
                }
            }
            else if (setup_up) { /* the setup owns the glass: setup_touch took it */ }
            else if (notice_current() && notice_dismiss()) { }   /* an announcement up: the tap closes it (the lights-out notice lets it through: notice.h) */
            else if (battery_view) battery_view = false;      /* the battery page: any click closes it */
            else if (settings_view || updates_view || ms_back) ms_back = false;   /* the page owns the glass: render_settings_touch took it
                                                                    (and its CLOSE already brought the milestones page back) */
            else if (shop_view) {                       /* the shop: a row's modal, UNLOCK, HOW TO EARN, CLOSE */
                int r = render_shop_tap(&tank, (float)press_x, (float)press_y);
                if (r == SHOP_TAP_CLOSE) { shop_view = false; render_shop_leave(); milestones_view = true; }   /* back to the milestones page */
                else if (r >= SHOP_TAP_SELL) {              /* sold back (the second tap on SELL) */
                    int item = r - SHOP_TAP_SELL;
                    if (progression_sell(&tank, item)) { snd(SND_CONFIRM, AUDIO_PITCH_ONE); printf("shop: %s sold back for %d, balance %d\n", SD_ITEMS[item].name, progression_sell_value(item), tank.sd_balance); }
                }
                else if (r >= SHOP_TAP_MOVE) {              /* a piece already in the tank: place it again */
                    int item = r - SHOP_TAP_MOVE;
                    shop_view = false; render_shop_leave(); setup_begin_place(&tank, item);
                    printf("shop: MOVE %s - placement page up (drag, DEPTH, DONE)\n", SD_ITEMS[item].name);
                }
                else if (r >= SHOP_TAP_BUY) {
                    int item = r - SHOP_TAP_BUY;
                    if (progression_buy(&tank, item)) { snd(SND_CONFIRM, AUDIO_PITCH_ONE); printf("shop: %s unlocked, %d sand dollars left\n", SD_ITEMS[item].name, tank.sd_balance);
                        if (tank_decor_placeable(item)) { shop_view = false; render_shop_leave(); setup_begin_place(&tank, item);
                                                          printf("shop: placement page up for the %s\n", SD_ITEMS[item].name); } }
                    else printf("shop: %s refused (balance %d, price %d)\n", SD_ITEMS[item].name, tank.sd_balance, SD_ITEMS[item].price);
                }
            }
            else if (milestones_view && abs(dx) >= 40 && abs(dx) > 2 * abs(dy) && render_milestones_swipe(&tank, (float)press_x, (float)press_y, (float)dx)) {
                /* a sideways drag along the TANK row turned its page (2026-09-30), as the device's touch port does */
            }
            else if (milestones_view) {
                int r = render_milestones_tap(&tank, (float)press_x, (float)press_y);
                if (r >= MS_TAP_SELL) {                     /* a fish's card: SELL, confirmed - the page stays, a row shorter */
                    int fi = r - MS_TAP_SELL, worth = progression_fish_value(&tank, fi);
                    char nm[FISH_NAME_MAX + 1]; snprintf(nm, sizeof nm, "%s", fi < tank.n_fish ? tank.fish[fi].name : "?");
                    if (progression_sell_fish(&tank, fi)) { selected_fish = -1; printf("milestones: %s sold for %d, %d fish left, balance %d\n", nm, worth, tank.n_fish, tank.sd_balance); }
                }
                else if (r >= MS_TAP_RENAME) {              /* ... RENAME: the letter wheel over the live tank */
                    milestones_view = false; selected_fish = -1;
                    progression_ack_milestones(&tank); render_milestones_leave();
                    setup_begin_rename(&tank, r - MS_TAP_RENAME);
                    printf("milestones: renaming %s (the wheel; CANCEL / DONE)\n", tank.fish[r - MS_TAP_RENAME].name);
                }
                else if (r == MS_TAP_CLOSE || r == MS_TAP_SETTINGS || r == MS_TAP_SHOP) {
                    milestones_view = false; settings_view = r == MS_TAP_SETTINGS; shop_view = r == MS_TAP_SHOP;
                    progression_ack_milestones(&tank); render_milestones_leave(); }
                /* MS_TAP_KEPT: a badge / name opened the detail modal, or the modal closed; anything else: nothing */
            }
            else if (now_ms - press_ms < 350 && dx * dx + dy * dy < 24 * 24) {
                /* same hit test as the device: 38 px against the press-time
                   fish snapshot AND the current position, whichever is closer */
                int best = -1; float bd = 38 * 38;
                for (int i = 0; i < tank.n_fish; i++) {
                    float ax = press_fx[i] - press_x, ay = press_fy[i] - press_y;
                    float bx = tank.fish[i].x - press_x, by = tank.fish[i].y - press_y;
                    float d2a = ax * ax + ay * ay, d2b = bx * bx + by * by;
                    float d2 = d2a < d2b ? d2a : d2b;
                    if (d2 < bd) { bd = d2; best = i; }
                }
                /* a click ON the open card (its MORE button, or any of it): the
                   milestones page, as the device's touch port does (2026-09-16) */
                if (tank.tool != TOOL_HAND) {   /* a tool in hand owns the glass (2026-10-04): only its chip's DONE answers a click */
                    selected_fish = -1;
                    if (render_tool_chip_hit(&tank, (float)press_x, (float)press_y)) { tank_set_tool(&tank, TOOL_HAND); printf("toolbox: DONE - back to bare hands\n"); }
                    else printf("toolbox: click ignored - a tool is in hand (DONE first)\n");
                }
                else if (sim_pill_up() && RENDER_BAT_HIT(press_x, press_y)) {   /* the battery pill, while it shows: its page (2026-09-24) */
                    battery_view = true; battery_view_ms = now_ms; selected_fish = -1;
                    printf("battery page: %d%%, %s\n", sim_bat_gauge(), sim_bat_state == BAT_CHARGING ? "charging" : sim_bat_state == BAT_FULL ? "full" : "on battery");
                }
                else if (selected_fish >= 0 && selected_fish < tank.n_fish && render_tools_hit((float)press_x, (float)press_y) >= 0) {
                    /* the toolbox under the card (2026-10-01): pick a tool up (or put the one in hand back), the card goes */
                    int tl = render_tools_hit((float)press_x, (float)press_y);
                    tank_set_tool(&tank, tank.tool == tl ? TOOL_HAND : tl); selected_fish = -1;
                    printf("toolbox: %s\n", tank.tool == TOOL_SPONGE ? "the SPONGE - strokes only wipe" : tank.tool == TOOL_SCISSORS ? "the SCISSORS - strokes only cut" : "back to bare hands");
                }
                else if (selected_fish >= 0 && selected_fish != RENDER_CARD_SNAIL && selected_fish != RENDER_CARD_SHRIMP && selected_fish != RENDER_CARD_URCHIN
                         && RENDER_CARD_HIT(press_x, press_y))
                    milestones_view = true;
                else if ((selected_fish < 0 || selected_fish >= tank.n_fish) && render_tool_chip_hit(&tank, (float)press_x, (float)press_y)) {
                    tank_set_tool(&tank, TOOL_HAND); printf("toolbox: DONE - back to bare hands\n");
                }
                else if (best >= 0) selected_fish = (best == selected_fish) ? -1 : best;
                else if (tank_snail_hit(&tank, (float)press_x, (float)press_y))   /* the snail: its card (2026-09-16) */
                    selected_fish = selected_fish == RENDER_CARD_SNAIL ? -1 : RENDER_CARD_SNAIL;
                else if (tank_urchin_hit(&tank, (float)press_x, (float)press_y)) {  /* the urchin: its card (2026-10-02) */
                    selected_fish = selected_fish == RENDER_CARD_URCHIN ? -1 : RENDER_CARD_URCHIN;
                    printf("urchin tapped: card %s (%.0f cm of grass grazed)\n", selected_fish >= 0 ? "up" : "down", tank.urchin_grazed_px / PX_PER_CM);
                }
                else if (tank_shrimp_hit(&tank, (float)press_x, (float)press_y)) {   /* the shrimp: a tap its card, the third quick tap scares them (2026-09-29) */
                    int taps = tank_shrimp_tap(&tank, (float)press_x, (float)press_y);
                    if (taps == 1) selected_fish = selected_fish == RENDER_CARD_SHRIMP ? -1 : RENDER_CARD_SHRIMP;
                    else if (taps >= 3) { selected_fish = -1; printf("shrimp: scared off (triple tap)\n"); }
                }
                else if (selected_fish >= 0) selected_fish = -1;   /* card up: empty-glass tap dismisses, nothing else */
                else tank_touch_tap(&tank, (float)press_x, (float)press_y);
            } else if (tank.tool == TOOL_HAND && press_y - tank_glass_top(press_x) < 60 && dy >= 40) tank_feed(&tank, (float)mx, 3);
        }
        mdown = mpress;
        if (TANK_WORN) { static bool tdown; if (k[SDL_SCANCODE_T] && !tdown) {
            sim_worn_turned = !sim_worn_turned;
            printf("the watch is worn %s (settings SCREEN is %s)\n", sim_worn_turned ? "the OTHER way around - buttons toward the elbow" : "the usual way",
                   tank.screen_turned ? "TURNED" : "NORMAL");
          } tdown = k[SDL_SCANCODE_T]; }
        if (confirm_view && now_ms - confirm_ms > CONFIRM_MS) { confirm_view = false; printf("reset prompt: timed out, tank kept\n"); }
        if (k[SDL_SCANCODE_X] && !xdown && !confirm_view) {   /* the keeper's reset prompt (device: hold BOOT + tap) */
            confirm_view = true; confirm_ms = now_ms; selected_fish = -1; milestones_view = false; settings_view = false; shop_view = false; render_shop_leave();
            printf("reset prompt: click YES or NO (it gives up after %d s)\n", CONFIRM_MS / 1000);
        }
        xdown = k[SDL_SCANCODE_X];
        { static bool edown; if (k[SDL_SCANCODE_E] && !edown && !update_mode) {   /* E: update mode, as CHECK FOR UPDATES enters it */
            progression_save(&tank); milestones_view = settings_view = updates_view = shop_view = false; selected_fish = -1;
            update_mode = true; update_clock = 0; update_begin(sim_bat_gauge(), sim_bat_state != BAT_ON_BATTERY);
            printf("update mode (E): the pages over a pretend radio; POCKET_TANK_FAKE_UPDATE=none|cable|fail|downloadfail|otherboard|boardimage|<x.y.z> steers the check\n"); }
          edown = k[SDL_SCANCODE_E]; }
        if (k[SDL_SCANCODE_S] && !sdown) {                    /* the first-run flow, on cue */
            if (setup_active()) { setup_cancel(&tank); printf("setup: closed (still owed if it was pending)\n"); }
            else { setup_begin(&tank); selected_fish = -1; milestones_view = false; printf("setup: welcome page (click through; BEGIN saves)\n"); }
        }
        sdown = k[SDL_SCANCODE_S];
        if (k[SDL_SCANCODE_V] && !vdown) { int v = (audio_volume() + 1) % 3; if (s_adev) { SDL_LockAudioDevice(s_adev); audio_set_volume(v); SDL_UnlockAudioDevice(s_adev); }
                                           printf("volume: %s\n", v == 0 ? "off" : v == 1 ? "quiet" : "normal"); }
        vdown = k[SDL_SCANCODE_V];
        if (k[SDL_SCANCODE_B] && !bdown) { notice_low_battery(); printf("low battery notice queued\n"); }
        bdown = k[SDL_SCANCODE_B];
        if (k[SDL_SCANCODE_P] && !pdown) {                    /* the cable, in or out (the pretend cell) */
            sim_bat_state = BAT_ON_POWER(sim_bat_state) ? BAT_ON_BATTERY : sim_bat_gauge() >= 100 ? BAT_FULL : BAT_CHARGING;
            printf("battery: %s at %d%% (the pill shows; click it for the battery page)\n",
                   sim_bat_state == BAT_ON_BATTERY ? "unplugged" : "cable in", sim_bat_gauge());
        }
        pdown = k[SDL_SCANCODE_P];
        if (k[SDL_SCANCODE_4] && !fourdown && !confirm_view && !setup_up) {   /* $: the shop page */
            shop_view = !shop_view; if (!shop_view) render_shop_leave(); milestones_view = false; settings_view = false; selected_fish = -1;
            printf("shop: %s (%d sand dollars)\n", shop_view ? "up" : "closed", tank.sd_balance);
        }
        fourdown = k[SDL_SCANCODE_4];
        if (k[SDL_SCANCODE_C] && !cdown) {         /* the castle (2026-09-16): granted for a look, free; again takes it away */
            if (tank.sd_unlocks & SD_ITEM_CASTLE) tank.sd_unlocks &= ~SD_ITEM_CASTLE;
            else { tank.sd_unlocks |= SD_ITEM_CASTLE; tank_castle_place(&tank); }
            printf("castle: %s (free; the shop sells it at %d, MOVE in its modal places it - BEHIND or IN FRONT of the grass)\n",
                   (tank.sd_unlocks & SD_ITEM_CASTLE) ? "in the tank" : "gone", SD_PRICE_CASTLE);
        }
        cdown = k[SDL_SCANCODE_C];
        if (k[SDL_SCANCODE_K] && !kdown) {         /* the coral (2026-09-23): granted free; again = the next colour; past the last takes it away */
            if (!(tank.sd_unlocks & SD_ITEM_CORAL)) { tank.sd_unlocks |= SD_ITEM_CORAL; tank_coral_place(&tank); tank_coral_set_rgb(&tank, CORAL_PAL[0]); }
            else { int i = 0; while (i < CORAL_N && CORAL_PAL[i] != tank_coral_rgb(&tank)) i++;
                   if (i + 1 < CORAL_N) tank_coral_set_rgb(&tank, CORAL_PAL[i + 1]); else tank.sd_unlocks &= ~SD_ITEM_CORAL; }
            printf("coral: %s (free; the shop sells it at %d, MOVE in its modal places and colours it)\n",
                   (tank.sd_unlocks & SD_ITEM_CORAL) ? "in the tank" : "gone", SD_PRICE_CORAL);
        }
        kdown = k[SDL_SCANCODE_K];
        if (k[SDL_SCANCODE_J] && !jdown && (tank.sd_unlocks & SD_ITEM_CORAL)) {   /* the coral's growth, a step at a time (a month in ~8 taps) */
            tank.coral_growth = tank_coral_growth(&tank) + 0.15f > CORAL_FULL ? CORAL_START : tank_coral_growth(&tank) + 0.15f;
            printf("coral growth %.2f (1.0 = the fan, %.2f = the crown)\n", tank.coral_growth, CORAL_FULL);
        }
        jdown = k[SDL_SCANCODE_J];
        if (k[SDL_SCANCODE_I] && !idown) {         /* the reef cluster (2026-09-24): granted free; again = the next look; past the last takes it away */
            if (!(tank.sd_unlocks & SD_ITEM_CLUSTER)) { tank.sd_unlocks |= SD_ITEM_CLUSTER; tank_cluster_place(&tank); tank_cluster_set_scheme(&tank, 0); }
            else if (tank_cluster_scheme(&tank) + 1 < CLUSTER_SCHEME_N) tank_cluster_set_scheme(&tank, tank_cluster_scheme(&tank) + 1);
            else tank.sd_unlocks &= ~SD_ITEM_CLUSTER;
            printf("cluster: %s (free; the shop sells it at %d on page 2; O steps its growth)\n",
                   (tank.sd_unlocks & SD_ITEM_CLUSTER) ? CLUSTER_SCHEMES[tank_cluster_scheme(&tank)].name : "gone", SD_PRICE_CLUSTER);
        }
        idown = k[SDL_SCANCODE_I];
        if (k[SDL_SCANCODE_O] && !odown && (tank.sd_unlocks & SD_ITEM_CLUSTER)) {
            tank.cluster_growth = tank_cluster_growth(&tank) + 0.25f > CLUSTER_FULL ? CLUSTER_START + 1e-4f : tank_cluster_growth(&tank) + 0.25f;
            printf("cluster growth %.2f (1.0 = full size, %.2f = every tentacle)\n", tank.cluster_growth, CLUSTER_FULL);
        }
        odown = k[SDL_SCANCODE_O];
        if (k[SDL_SCANCODE_W] && !wdown) {         /* the shrimp school (2026-09-29): granted free; again = one more; past SHRIMP_MAX takes them away */
            if (!(tank.sd_unlocks & SD_ITEM_SHRIMP)) { tank.sd_unlocks |= SD_ITEM_SHRIMP; tank_shrimp_place(&tank, SHRIMP_START); }
            else if (tank.shrimp_n < SHRIMP_MAX) tank_shrimp_place(&tank, tank.shrimp_n + 1);
            else { tank.sd_unlocks &= ~SD_ITEM_SHRIMP; tank.shrimp_n = 0; }
            printf("shrimp: %d (free; the shop sells the school at %d on page 2; F feeds - pellets on the floor bring more; tap near them to scatter)\n",
                   (tank.sd_unlocks & SD_ITEM_SHRIMP) ? tank.shrimp_n : 0, SD_PRICE_SHRIMP);
        }
        wdown = k[SDL_SCANCODE_W];
        if (k[SDL_SCANCODE_Y] && !ydown) {         /* the urchin (2026-10-02): granted free; again takes it away */
            if (!(tank.sd_unlocks & SD_ITEM_URCHIN)) { tank.sd_unlocks |= SD_ITEM_URCHIN; tank_urchin_place(&tank); }
            else { tank.sd_unlocks &= ~SD_ITEM_URCHIN; tank.urchin_x = -1; if (selected_fish == RENDER_CARD_URCHIN) selected_fish = -1; }
            printf("urchin: %s (free; the shop sells it at %d on page 2; G grows the grass for it, Z sleeps 7 h)\n",
                   (tank.sd_unlocks & SD_ITEM_URCHIN) ? "in the tank" : "gone", SD_PRICE_URCHIN);
        }
        ydown = k[SDL_SCANCODE_Y];
        if (k[SDL_SCANCODE_D] && !ddown) { progression_sd_grant(&tank, 50); printf("+50 sand dollars (%d)\n", tank.sd_balance); }
        ddown = k[SDL_SCANCODE_D];
        if (k[SDL_SCANCODE_U] && !udown) { ui_visible = !ui_visible; }
        udown = k[SDL_SCANCODE_U];
        if (k[SDL_SCANCODE_M] && !mkdown) {
            milestones_view = !milestones_view;
            if (!milestones_view) { progression_ack_milestones(&tank); render_milestones_leave(); }
        }
        mkdown = k[SDL_SCANCODE_M];
        if (k[SDL_SCANCODE_R] && !rdown) { progression_force_arrival(&tank); print_roster(&tank); }
        rdown = k[SDL_SCANCODE_R];
        if (k[SDL_SCANCODE_Z] && !zdown) {         /* jump through a night of device sleep */
            tank_tick_sleep(&tank, 7 * 3600);
            progression_woke(&tank);               /* a fry on its way is born at the wake */
            printf("slept 7 h: hunger now");
            for (int i = 0; i < tank.n_fish; i++) printf(" %.1f", tank.fish[i].hunger);
            printf("\n");
        }
        zdown = k[SDL_SCANCODE_Z];
        if (k[SDL_SCANCODE_G] && !gdown) {         /* demo the upkeep chores at once */
            for (int b = 0; b < VEG_BEDS; b++)
                tank_veg_set(&tank, b, tank.veg_growth[b] > 0.99f ? VEG_NUB
                                   : fminf(1, tank.veg_growth[b] + 0.30f));
            tank_grow_algae(&tank, 80);
            printf("grew: canopy %.2f/%.2f/%.2f + algae (swipe sideways through a canopy to trim; drag to wipe; G cycles)\n",
                   tank.veg_growth[0], tank.veg_growth[1], tank.veg_growth[2]);
        }
        gdown = k[SDL_SCANCODE_G];
        if (k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_ESCAPE]) { progression_save(&tank); break; }
        if (k[SDL_SCANCODE_F] && !fdown) tank_feed(&tank, (float)mx, 3);
        if (k[SDL_SCANCODE_N] && !ndown) tank_toggle_light(&tank);
        if (k[SDL_SCANCODE_A]) tank_light_auto(&tank);
        if (k[SDL_SCANCODE_L] && !ldown && llm_available) {
            llm_active = !llm_active;
            printf("brain: %s\n", llm_active ? "LLM (14M student)" : "rules");
        }
        fdown = k[SDL_SCANCODE_F]; ndown = k[SDL_SCANCODE_N];
        ldown = k[SDL_SCANCODE_L];
        SDL_Delay(wait < 5 ? 5 : (wait > 16 ? 16 : wait));
    }
    return 0;
}
