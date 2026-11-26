#include <stdio.h>
/* THE OCTOPUS, a sim prototype (docs/octopus.md). Look A, the ring of eight, from baby to adult (twice the baby).
 * A port of working-assets/octopus/octo_motion.py (Octo2): arms are POSED curves, never loose strings; the crawl is
 * two sets of four that take turns and a planted tip never slides; the jet is squat, squeeze, glide, squeeze, glide,
 * then a sink onto open arms. Nothing of it is in tank_t and nothing is saved: it is one static animal behind
 * octopus_proto_set(), so the save and every other test stay byte for byte as they were.
 * SIM ONLY as it stands: the grids below are static (.bss), which the firmware cannot spare (internal RAM); a
 * firmware octopus takes them from the decor scratch in PSRAM. */
#include <math.h>
#include <string.h>
#include "octopus.h"

#define PI 3.14159265f
#define DOWN (PI * 0.5f)
#define STANCE 0.70f
enum { M_REST, M_CRAWL, M_GATHER, M_JET };

typedef struct {
    int far, idx, rank;
    float L0, Lmax, tip[2], curl, pho, ang[OCTO_NMAX - 1], freev, flen;
    int mv; float mp0[2], mp1[2], mt, mdur, mlift;
    int sw; float swf[2], swu; int first; float up;     /* up: 0..1 raised off the sand (it holds something up) */      /* sliding forward: from where (px from the body), how far through; its first slide of this walk */
} arm_t;
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define OCTO_BIG_ST EXT_RAM_BSS_ATTR
#define OCTO_BIG EXT_RAM_BSS_ATTR                    /* on a board: 27.6 KB of grids in PSRAM, never internal RAM (about 23 KB free there) */
#else
#define OCTO_BIG
#define OCTO_BIG_ST
#endif
static OCTO_BIG_ST struct {
    int on, growth, N;
    octo_look_t lk; float stride, crawl_v, drop;
    float x, y, vx, vy, sp, theta, om, squeeze, puff, tuck, droop, blink, t, mode_t, gait, queue_t, sink, spread, td, jk;
    int mode, dir, jdir;
    float stream;                                  /* 0..1: drawn out long to flee */
    /* ITS DEN (docs/octopus.md section 29): an opening it lives in - the castle's arch (item 2) or the volcano's cave (9) */
    int fav, den_item, home, going_home;           /* the one it favors; the one it has now (-1 none); wanting to be inside; walking there */
    float den_x, den_floor, base_floor, inn;       /* where its opening is and the floor inside; the sand's line; 0..1 how far in it is */
    /* ITS LANE on the sand. The back lane is its own line, behind every piece IN FRONT. A den in a piece IN FRONT is entered
       from BEFORE that piece, so for it the octopus takes the front lane (4 px nearer, drawn over the pieces IN FRONT) - and a
       lane changes only where no piece IN FRONT stands over it, so the change is never seen (the crab's rule). Strato 11-21,
       at the first den clip: at a volcano IN FRONT "the octopus popped into view" - it had walked up hidden behind the rock */
    int den_front, lane, clear, detour, hide, behind; float den_half, sand_floor;
    /* CAMOUFLAGE (docs/octopus.md sections 3, 18, 30; approved on the prototype 11-21). Still for a while, by day, it takes the colours
       of what it sits at: camo runs 0 -> 1 over 3 s (render spreads it from the arm tips inward and up the sack); the moment it
       moves it drops in 0.4 s. Its eyes never change. */
    float camo, still_t; int night, camo_now;
    int after_home;                             /* backed off from the crab's door: home next, once it has stopped */
    int leaving;                                /* (audit SOC-2) its den is to change: it comes out of the one it is in, and the tank keeps that one its den until it is out */
    float fs_clear;                             /* the nearest sand clear of every piece IN FRONT (the tank's; out of range: none) */
    float rival_x, rival_gap; int visiting;                   /* the crab out on the sand (-1: none, or in its den) and the gap it keeps from it */
    float den_d;                                /* the den's shade on it, 0..1: as the crab's cr_inn - up over 0.6 s once it is in (drawn in the den), down at once out */
    float gath, gath_k, den_open; int front_shelter; float fs_x, fs_half;   /* a shelter IN FRONT: the nearest one's middle and half width */                       /* gath: 0..1 its arms drawn in under it to pass a den's door; den_open: the opening's half width */
    /* A FIND (docs/octopus.md 17, 20, 31): like the crab's sand dollar. Due every 2.5 to 3.5 h of its time, with the light on and the
       keeper here and no coin up in the crab's claws: it digs (a second and a half of scrabbling), then holds the coin up in an arm
       tip and waves it for 3 minutes. A tap takes it (5). Untaken it is buried again and comes back sooner. */
    float find, find_cool, dig, wave; int found, present, crab_coin, find_now, cue; float raise;   /* find: s left held up; dig: s left digging; raise: 0..1 the arm up */   /* behind: no clear sand to come round on, so it goes in from behind the piece */
    float floor, xlo, xhi;
    arm_t arm[8];
    octo_pose_t pose;
    /* its little brain */
    float think, goal_x, blink_in; uint32_t rs; int want_jet;
    /* the picture */
    octo_grid_t g; float built_t; int dirty, builds; int base_x0, base_y0; float built_x, built_y;   /* (where the picture was laid out, to carry it along between two layouts) */
} O;
static OCTO_BIG uint8_t g_tone[OCTO_GW * OCTO_GH] __attribute__((aligned(4))); static OCTO_BIG uint8_t g_part[OCTO_GW * OCTO_GH] __attribute__((aligned(4)));   /* word aligned: the draw looks at four cells at a time */
static OCTO_BIG uint8_t g_btone[OCTO_GW * OCTO_GH] __attribute__((aligned(4))); static OCTO_BIG uint8_t g_bpart[OCTO_GW * OCTO_GH] __attribute__((aligned(4)));
static OCTO_BIG octo_grid_t g_bg; static OCTO_BIG octo_pose_t g_bpose; static OCTO_BIG octo_look_t g_blk;

static const float FAN[8]   = { -94, -86, -40, -24, 24, 40, 86, 94 };
static const float REACH[8] = { 0.90f, 0.74f, 0.56f, 0.30f, -0.30f, -0.56f, -0.74f, -0.90f };
static const int   BACK[8]  = { 1, 0, 1, 0, 0, 1, 0, 1 };
static const float BEAT[8]  = { 0.38f, 0.63f, 0.88f, 0.13f, 0.51f, 0.26f, 0.00f, 0.76f };   /* where each arm is in its own turn: never two together */
static const float SWAY[8]  = { 0.0f, 2.1f, 4.4f, 1.2f, 3.3f, 5.6f, 0.7f, 2.9f };            /* each arm sways on its own beat */
#define SLIDE_S 1.1f                               /* an arm takes this long to slide forward: slow off, slow on, never a snap */

static float wrap(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
static float lerp_ang(float a, float b, float t) { return a + wrap(b - a) * t; }
static float ease(float t) { if (t < 0) t = 0; if (t > 1) t = 1; return t * t * (3 - 2 * t); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float mn1(float v) { return v > 1 ? 1 : v; }
static float hashf(int i, int j, int k) {
    uint32_t h = (uint32_t)i * 374761393u + (uint32_t)j * 668265263u + (uint32_t)k * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}
static float rnd(void) { O.rs = O.rs * 1664525u + 1013904223u; return (float)(O.rs >> 8) / 16777216.0f; }
static float stage3(float s, float a, float b, float c) { return s < 1.5f ? a + (b - a) * (s - 1.0f) * 2.0f : b + (c - b) * (s - 1.5f) * 2.0f; }

/* the look at this size: not a blow-up - the sack and arms outgrow the eyes and the head (octo_grow.py grown()) */
static void set_look(int growth) {
    const float s = 1.0f + clampf((float)growth / 255.0f, 0, 1), hs = powf(s, 0.92f);
    octo_look_t *k = &O.lk;
    const float arm = stage3(s, 1.00f, 1.12f, 1.25f), eye = stage3(s, 1.00f, 0.76f, 0.66f);
    k->sz = s; k->m_len = 16.0f * s; k->m_b = 9.5f * s * (1.0f + 0.04f * (s - 1)); k->m_droop = 0;
    k->head[0] = 8.0f * hs; k->head[1] = 5.0f * hs; k->rest_h = 7.5f * hs;
    k->L = 18.0f * s * arm; k->w0 = 2.7f * powf(s, 0.9f); k->w1 = 1.0f * powf(s, 0.7f); k->taper = 1.3f; k->curl = 0.55f;
    k->eye_r = 2.9f * s * eye; k->eye_at[0] = 4.3f * hs; k->eye_at[1] = -1.0f * hs; k->far_eye[0] = -4.3f * hs; k->far_eye[1] = -1.0f * hs;
    k->mottle = stage3(s, 0.10f, 0.14f, 0.18f);
    k->N = s < 1.25f ? 9 : s < 1.75f ? 10 : 11;
    for (int i = 0; i < 8; i++) k->reach[i] = REACH[i];
    octo_look_init(k);
    O.stride = stage3(s, 8.0f, 11.0f, 14.0f); O.crawl_v = stage3(s, 8.0f, 10.0f, 11.5f); O.drop = 0.20f;
    for (int i = 0; i < 8; i++) {
        arm_t *a = &O.arm[i]; a->idx = i; a->far = BACK[i];
        const float r = fabsf(REACH[i]);
        a->L0 = k->L * (a->far ? 0.90f : 1.0f) * (0.94f + 0.10f * hashf(i, 7, a->far ? 3 : 5)) * clampf(r * 1.02f + 0.22f, 0.5f, 1.0f);
        a->Lmax = k->L * 1.3f; if (a->Lmax > a->L0 * 1.8f) a->Lmax = a->L0 * 1.8f;
    }
}
static float arm_wd(const arm_t *a, float t) { const float w = O.lk.wtab[(int)(t * OCTO_WT + 0.5f)]; return a->far ? w * 0.86f : w; }
static float floor_of(const arm_t *a) { return O.floor - arm_wd(a, 1.0f); }
static float neutral(const arm_t *a, float k) {
    const float h = O.lk.rest_h + 1.5f; float q = a->L0 * a->L0 - h * h; if (q < 1) q = 1;
    const float far_as = sqrtf(q) * 0.90f;
    return clampf(O.lk.reach[a->idx] * O.lk.L, -far_as, far_as) * k;
}
static float rest_theta(void) { return 1.5f * PI; }
static int g_why_on, g_why;                          /* (tests: octopus_debug_why) */
int octopus_debug_why(int on) { const int w = g_why; g_why_on = on; g_why = 0; return w; }
static int g_glide;                                  /* (an A/B: the arms laid out every OTHER frame at a crawl, the 11-22 cost cut that jittered) */
void octopus_debug_glide(int on) { g_glide = on; }
static int g_no_gather;                              /* (tests: in at the door with its arms out, as before 11-22 - the control of 'through the door') */
void octopus_debug_no_gather(int on) { g_no_gather = on; }
/* how far out its arms rest: all the way on the sand, drawn in under it at a den's door (gath) and inside (inn) */
static float arm_out(void) { const float a = 1.0f - 0.5f * O.inn, b = 1.0f - O.gath * (1.0f - O.gath_k); return a < b ? a : b; }
static void arm_root(const arm_t *a, float *rx, float *ry, float *base) {
    const float fan = FAN[a->idx] * (PI / 180.0f) * (1.0f - 0.72f * O.tuck), ca = O.theta + PI;
    const float side = -sinf(fan) * O.lk.head[0] * (a->far ? 0.62f : 0.50f), down = O.lk.head[1] * (a->far ? 0.30f : 0.45f);
    *rx = O.x + sinf(ca) * side + cosf(ca) * down; *ry = O.y - cosf(ca) * side + sinf(ca) * down; *base = ca + fan;
}
static void start_mv(arm_t *a, float tx, float ty, float dur, float lift) {
    a->mv = 1; a->mp0[0] = a->tip[0]; a->mp0[1] = a->tip[1]; a->mp1[0] = tx; a->mp1[1] = ty; a->mt = 0; a->mdur = dur; a->mlift = lift;
}
static void arms_pose(void);
static void set_mode(int m, int dir) {
    O.mode = m; O.mode_t = 0;
    if (m == M_CRAWL) {
        O.dir = dir; O.gait = 0;
        for (int i = 0; i < 8; i++) { arm_t *a = &O.arm[i]; a->mv = 0; a->sw = 0; a->first = 1; }
    }
    if (m == M_REST) {
        for (int i = 0; i < 8; i++) { arm_t *a = &O.arm[i]; if (a->sw) { start_mv(a, O.x + neutral(a, 1), floor_of(a), 0.55f, 1.0f * O.lk.sz); a->sw = 0; } }
        O.queue_t = 0.25f;
    }
    if (m == M_GATHER) { O.jdir = dir; for (int i = 0; i < 8; i++) { arm_t *a = &O.arm[i]; start_mv(a, O.x + neutral(a, 0.50f), floor_of(a), 0.42f, 0); } }
    if (m == M_JET) {
        arms_pose();                                                  /* (it leaves from the pose it is in) */
        O.jdir = dir; O.sink = 0;
        const int N = O.lk.N;
        for (int i = 0; i < 8; i++) {
            arm_t *a = &O.arm[i]; const float *P = O.pose.arm[i].P; float len = 0;
            for (int j = 0; j < N - 1; j++) { const float dx = P[2 * j + 2] - P[2 * j], dy = P[2 * j + 3] - P[2 * j + 1]; a->ang[j] = atan2f(dy, dx); len += sqrtf(dx * dx + dy * dy); }
            a->flen = len; a->freev = 1.0f; a->mv = 0;
        }
    }
}
static void settle(float dt, float y_to, float k, float c) { O.vy += ((y_to - O.y) * k - O.vy * c) * dt; O.y += O.vy * dt; }
static void turn(float want, float dt, float freer) {
    const float sp = sqrtf(O.vx * O.vx + O.vy * O.vy), cap = sp / (9.0f * O.lk.sz) + freer;   /* it turns only as fast as it moves: never a pivot on the spot */
    O.om += (wrap(want - O.theta) * 14.0f - O.om * 5.0f) * dt;
    O.om = clampf(O.om, -cap, cap); O.theta += O.om * dt;
}
static void m_rest(float dt) {
    const octo_look_t *lk = &O.lk;
    O.sp *= expf(-dt * 6);
    if (O.sink == 0) O.vx = O.dir * O.sp; else O.vx *= expf(-dt * 6);
    O.x += O.vx * dt;
    const float digdip = O.dig > 0 ? 1.6f * lk->sz * (0.5f + 0.5f * sinf(O.dig * 18.5f)) : -2.0f * lk->sz * O.raise;   /* digging it dips, beat by beat; holding the coin up it stands taller */
    settle(dt, O.floor - lk->rest_h + 0.35f * sinf(O.t * 1.9f) + digdip, 120.0f, 16.0f);
    if (O.y > O.floor - lk->rest_h + 1.6f + (digdip > 0 ? digdip : 0)) O.y = O.floor - lk->rest_h + 1.6f + (digdip > 0 ? digdip : 0);
    turn(rest_theta(), dt, 0.8f);
    O.squeeze += (0 - O.squeeze) * mn1(dt * 4); O.puff += (0 - O.puff) * mn1(dt * 3.5f);
    O.tuck += ((0.5f * O.inn > 0.6f * O.gath ? 0.5f * O.inn : 0.6f * O.gath) - O.tuck) * mn1(dt * 3); O.droop += (1 - O.droop) * mn1(dt * 3);        /* in its den it sits drawn in */
    for (int i = 0; i < 8; i++) if (!O.arm[i].mv) O.arm[i].tip[1] = floor_of(&O.arm[i]);              /* (the floor under it changes as it goes in) */
    O.queue_t -= dt;
    if (O.queue_t <= 0) {                                           /* one arm at a time finds its place */
        arm_t *best = NULL; float err = 2.0f * lk->sz;
        for (int i = 0; i < 8; i++) { arm_t *a = &O.arm[i]; const float e = fabsf(a->tip[0] - (O.x + neutral(a, 1) * arm_out())); if (!a->mv && e > err) { best = a; err = e; } }
        if (best) { start_mv(best, O.x + neutral(best, 1) * arm_out(), floor_of(best), 0.36f, 1.5f * lk->sz); O.queue_t = 0.12f; }
    }
    for (int i = 0; i < 8; i++) O.arm[i].curl += (1 - O.arm[i].curl) * mn1(dt * 2.5f);
}
/* THE WALK (Strato 11-21: "octopus generally have their heads moving at relatively constant velocity while the legs
   negotiate the path", and no arm that snaps). The head goes at one steady pace. Each arm holds its place on the sand
   while the body goes by, then SLIDES forward low over about a second, slow off and slow on, and holds again. The eight
   are spread through that turn (BEAT), so no two move together and some arm is always moving. */
static void m_crawl(float dt) {
    const octo_look_t *lk = &O.lk; const float sz = lk->sz, amp = 6.0f * sz;
    O.sp += (O.crawl_v - O.sp) * mn1(dt * 2.2f);
    O.vx = O.dir * O.sp; O.x += O.vx * dt;
    O.gait += O.sp * dt / O.stride;
    settle(dt, O.floor - lk->rest_h - 2.0f * sz + 0.25f * sinf(O.t * 1.9f), 60.0f, 14.0f);    /* up on its arms a little */
    turn(rest_theta() + 6 * (PI / 180.0f) * O.dir * (O.sp / O.crawl_v), dt, 0.5f);              /* the sack trails */
    O.droop += (1.15f - O.droop) * mn1(dt * 3);
    for (int i = 0; i < 8; i++) {
        arm_t *a = &O.arm[i];
        const float nx = lk->reach[i] * lk->L * 0.80f, fl = floor_of(a);
        if (!a->sw) {
            a->tip[1] = fl; a->curl += (0.9f - a->curl) * mn1(dt * 5);
            const float behind = (nx - (a->tip[0] - O.x)) * O.dir;                              /* how far the body has gone by it */
            if (behind >= (a->first ? amp * (0.35f + 1.3f * BEAT[i]) : amp)) { a->sw = 1; a->first = 0; a->swu = 0; a->swf[0] = a->tip[0] - O.x; }
        }
        if (a->sw) {
            a->swu += dt / SLIDE_S;
            const float u = a->swu >= 1 ? 1.0f : a->swu, e = 0.5f - 0.5f * cosf(PI * u), to = nx + O.dir * amp;
            a->tip[0] = O.x + a->swf[0] + (to - a->swf[0]) * e; a->tip[1] = fl - 1.0f * sz * sinf(PI * u);   /* low: it slides, it does not step */
            a->curl += (0.45f - a->curl) * mn1(dt * 6);
            if (a->swu >= 1) a->sw = 0;
        }
    }
}
static void m_gather(float dt) {
    O.sp *= expf(-dt * 8);
    O.tuck += (1 - O.tuck) * mn1(dt * 5); O.puff += (1 - O.puff) * mn1(dt * 5);
    settle(dt, O.floor - O.lk.rest_h + 1.0f, 120.0f, 16.0f);
    turn(rest_theta() + 8 * (PI / 180.0f) * O.jdir, dt, 0.8f);                                 /* it leans the way it will go */
}
static void m_jet(float dt) {
    static const float PULSE[2][3] = { { 0.0f, 20.0f, 1.70f }, { 0.65f, 6.0f, 1.10f } };       /* start, elevation, strength: low and LONG. Strato 11-21, at the sim: "the movement is too short
                                                                                                  of a distance for the torpedo motion ... it doesnt look right" (it went ~50 px). A flee crosses a
                                                                                                  good part of the tank: about 170 px for the adult, half that for the baby */
    const octo_look_t *lk = &O.lk; const float t = O.mode_t, sz = lk->sz;                       /* (a bigger animal: the same move, scaled) */
    int thrust = 0; float last_end = 0;
    for (int i = 0; i < 2; i++) {
        const float t0 = PULSE[i][0], dur = 0.28f; if (t0 + dur > last_end) last_end = t0 + dur;
        if (t0 <= t && t < t0 + dur) {
            const float a = 215.0f * sz * O.jk * PULSE[i][2] * sinf(PI * (t - t0) / dur), e = PULSE[i][1] * (PI / 180.0f);   /* a push that builds and fades, never a kick */
            O.vx += O.jdir * cosf(e) * a * dt; O.vy -= sinf(e) * a * dt; thrust = 1;
        }
    }
    const int sinking = t > last_end + 0.45f;
    O.sink += ((sinking ? 1.0f : 0.0f) - O.sink) * mn1(dt * 3.0f);
    float cdx = 0.85f + 1.45f * O.sink; const float cdy = 1.4f + 1.3f * O.sink;                /* drawn out long it slips through the water; open arms catch it */
    { const float room = O.vx < 0 ? O.x - O.xlo : O.xhi - O.x; if (room < 40.0f * sz) cdx += 3.0f * (1 - room / (40.0f * sz)); }   /* near the glass it eases up: never a dead stop on it */
    O.vx *= expf(-dt * cdx); O.vy = O.vy * expf(-dt * cdy) + 30.0f * sz * dt;                   /* it has weight in the water */
    O.x += O.vx * dt; O.y += O.vy * dt;
    if (O.x < O.xlo) { O.x = O.xlo; if (O.vx < 0) O.vx = 0; }
    if (O.x > O.xhi) { O.x = O.xhi; if (O.vx > 0) O.vx = 0; }
    const int away = O.sink < 0.5f;                                                             /* fleeing: a torpedo. The sack first, long and narrow, every arm in one bundle behind (Strato's photos, 11-21) */
    O.squeeze += ((away ? 2.3f : 0.0f) - O.squeeze) * mn1(dt * (away ? 10 : 4));
    O.stream += ((away ? 1.0f : 0.0f) - O.stream) * mn1(dt * (away ? 9 : 4));
    O.puff += ((thrust ? 0.0f : 0.8f * (1 - O.sink)) - O.puff) * mn1(dt * (thrust ? 14 : 2.5f));
    O.tuck += ((1 - 0.85f * O.sink) - O.tuck) * mn1(dt * 5);
    O.droop += (0.2f - O.droop) * mn1(dt * 6);
    const float sp = sqrtf(O.vx * O.vx + O.vy * O.vy), up = -DOWN;
    float want = rest_theta();
    if (sp > 4 * sz && O.sink < 0.5f) want = atan2f(O.vy - 0.12f * sp, O.vx);                    /* the sack leads, all the way round: it turns AS it goes (turn() never pivots on the spot) */
    (void)up;
    turn(want, dt, 0.6f * O.sink);
    if (sp > 3 * sz) O.td = lerp_ang(O.td, atan2f(-O.vy, -O.vx), mn1(dt * 7));
    O.spread += (0.035f * (1 - O.sink) + 0.60f * O.sink - O.spread) * mn1(dt * (O.sink < 0.5f ? 12 : 4));   /* one tapering bundle, then open to sink */
    for (int i = 0; i < 8; i++) O.arm[i].curl += ((O.sink < 0.5f ? 0.0f : 0.5f) - O.arm[i].curl) * mn1(dt * 6);
    { const float skim = O.floor - lk->m_b * 0.66f; if (O.y > skim) { O.y = skim; if (O.vy > 0) O.vy = 0; } }   /* drawn out long it skims the sand, it does not plough it */
    if (O.y >= O.floor - lk->rest_h && t > 0.5f && (O.sink > 0.55f || sp < 10.0f * sz)) {       /* down, once it has slowed and opened: the arms take the weight
                                                                                                  (the first cut landed it the moment it was low: half a second, 50 px) */
        arms_pose();                                                                            /* (its arms land where they are) */
        O.mode = M_REST; O.mode_t = 0; O.puff = 1.1f; O.queue_t = 0.35f; O.sp = 0;
        const int N = lk->N;
        for (int i = 0; i < 8; i++) {
            arm_t *a = &O.arm[i]; const float h = lk->rest_h + 1.5f; float q = a->L0 * a->L0 - h * h; if (q < 1) q = 1;
            const float reach = sqrtf(q);
            a->tip[0] = clampf(O.pose.arm[i].P[2 * (N - 1)], O.x - reach, O.x + reach); a->tip[1] = floor_of(a);
        }
    }
}
static float g_jt[OCTO_NMAX][5]; static int g_jt_n; static float g_jt_dt;   /* the trailing joints' table (arms_state) */
static void trail(const arm_t *a, float *P, float rx, float ry) {      /* the trailing pose: the chain laid out from its (lagging) joint angles */
    const int N = O.lk.N; const float seg = a->flen / (float)(N - 1);
    P[0] = rx; P[1] = ry;
    for (int j = 0; j < N - 1; j++) {
        const float c = cosf(a->ang[j]);
        float x = P[2 * j] + c * seg, y = P[2 * j + 1] + sinf(a->ang[j]) * seg;
        const float fl = O.floor - arm_wd(a, (float)(j + 1) / (float)(N - 1));
        if (y > fl) { const float dyy = fl - P[2 * j + 1]; float q = seg * seg - dyy * dyy; if (q < 0) q = 0; y = fl; x = P[2 * j] + (c >= 0 ? 1 : -1) * sqrtf(q); }
        P[2 * j + 2] = x; P[2 * j + 3] = y;
    }
}
/* THE ARMS, in two halves (the cost pass, docs/octopus.md section 28). arms_state runs every step of the clock and is
   cheap: where each tip is, the trailing joints' angles. arms_pose lays the eight curves out and runs ONCE A FRAME, when
   the picture is rebuilt (it is the costly half: eight fitted curves and the sway). The first cut did both every step:
   four times a frame on a board. */
static void arms_state(float dt) {
    const octo_look_t *lk = &O.lk; const int N = lk->N;
    for (int i = 0; i < 8; i++) {
        arm_t *a = &O.arm[i];
        float rx, ry, base; arm_root(a, &rx, &ry, &base);
        if (a->mv) {
            a->mt += dt / a->mdur;
            if (a->mt >= 1) { a->tip[0] = a->mp1[0]; a->tip[1] = a->mp1[1]; a->mv = 0; }
            else { const float e = ease(a->mt); a->tip[0] = a->mp0[0] + (a->mp1[0] - a->mp0[0]) * e; a->tip[1] = a->mp0[1] + (a->mp1[1] - a->mp0[1]) * e - a->mlift * sinf(PI * a->mt); }
        }
        if (a->freev > 0) {
            /* (the jet's 3 ms a tick on the 1.8, 11-22: sqrtf, sinf and expf for every joint of every arm, four steps a tick - software on
               the S3. The joint's place along the arm, its t^1.5, its follow rate for this step and the wave's sine and cosine of
               5.2 t are a table now (for this N and step), and the wave's sine comes from one sinf/cosf per arm by the sum rule) */
            if (g_jt_n != N || g_jt_dt != dt) {
                g_jt_n = N; g_jt_dt = dt;
                for (int j = 0; j < N - 1; j++) {
                    const float tj = (float)(j + 1) / (float)(N - 1);
                    g_jt[j][0] = tj; g_jt[j][1] = tj * sqrtf(tj); g_jt[j][2] = 1 - expf(-dt * (24 - 17 * tj)); g_jt[j][3] = sinf(5.2f * tj); g_jt[j][4] = cosf(5.2f * tj);
                }
            }
            const float c0 = lerp_ang(O.td, DOWN, O.sink), bi = c0 + FAN[i] * (PI / 180.0f) * O.spread;
            const float ph = SWAY[i] - O.t * 13.0f, sp_ = sinf(ph), cp_ = cosf(ph), kk = 0.40f - 0.22f * O.sink, w0 = 0.10f * O.stream, d_ = wrap(O.td - bi);
            for (int j = 0; j < N - 1; j++) {
                const float *J = g_jt[j];
                const float want = bi + d_ * kk * J[1];
                const float wave = w0 * J[0] * (J[3] * cp_ + J[4] * sp_);                       /* each arm its own slow wave running tip-ward: never a straight rod */
                a->ang[j] += wrap(want + wave - a->ang[j]) * J[2];                                /* the tip follows last */
            }
            const float full = a->L0 + ((a->L0 > lk->L * 0.9f ? a->L0 : lk->L * 0.9f) - a->L0) * (1 - O.sink);
            a->flen += (full - a->flen) * mn1(dt * 5);
        }
        if (O.mode != M_JET) {
            const float dx = a->tip[0] - rx, dy = a->tip[1] - ry;                                /* a hold can't be further than the arm */
            if (dx * dx + dy * dy > a->Lmax * a->Lmax && !a->mv && !a->sw) { float q = a->Lmax * a->Lmax - dy * dy; if (q < 0) q = 0; a->tip[0] = rx + (dx >= 0 ? 1 : -1) * sqrtf(q); }
            if (a->freev > 0) { a->freev -= dt / 0.35f; if (a->freev < 0) a->freev = 0; }
        }
    }
}
static void arms_pose(void) {
    const octo_look_t *lk = &O.lk; const int N = lk->N;
    for (int i = 0; i < 8; i++) {
        arm_t *a = &O.arm[i]; octo_arm_t *pa = &O.pose.arm[i];
        float rx, ry, base; arm_root(a, &rx, &ry, &base);
        float Pf[OCTO_NMAX * 2]; const int have_f = a->freev > 0 || O.mode == M_JET;
        if (have_f) trail(a, Pf, rx, ry);
        if (O.mode == M_JET) memcpy(pa->P, Pf, sizeof(float) * 2 * (size_t)N);
        else {
            const float a0 = lerp_ang(base, DOWN, O.drop + (a->far ? 0.22f : 0.0f));
            octo_bezier(lk, pa->P, N, rx, ry, cosf(a0), sinf(a0), a->tip[0], a->tip[1], a->L0, a->Lmax, O.floor, a->far);
            if (have_f) { const float b = ease(a->freev); for (int j = 0; j < 2 * N; j++) pa->P[j] = pa->P[j] * (1 - b) + Pf[j] * b; }
            if (a->up > 0.001f) {                                                              /* held up: out of the body, then UP to its tip (the prototype's raised arm), waving */
                const float e = ease(a->up), sg = lk->reach[i] >= 0 ? 1.0f : -1.0f;
                const float tx = O.x + sg * lk->L * (0.61f + 0.13f * sinf(O.wave * 6.2832f)), ty = O.floor - arm_wd(a, 1.0f) - lk->L * (0.98f + 0.04f * sinf(O.wave * 6.2832f + 1.3f));
                const float ex = tx - rx, ey = ty - ry, Ln = sqrtf(ex * ex + ey * ey), ca = 0.616f * sg, sa = -0.788f;     /* (it leaves the body 52 degrees up) */
                const float c1x = rx + ca * Ln * 0.55f, c1y = ry + sa * Ln * 0.55f, c2x = tx - 0.10f * sg * Ln, c2y = ty + 0.45f * Ln;
                for (int j = 0; j < N; j++) {
                    const float u = (float)j / (float)(N - 1), v = 1 - u, b0 = v * v * v, b1 = 3 * v * v * u, b2 = 3 * v * u * u, b3 = u * u * u;
                    const float qx = b0 * rx + b1 * c1x + b2 * c2x + b3 * tx, qy = b0 * ry + b1 * c1y + b2 * c2y + b3 * ty;
                    pa->P[2 * j] = pa->P[2 * j] * (1 - e) + qx * e; pa->P[2 * j + 1] = pa->P[2 * j + 1] * (1 - e) + qy * e;
                }
            }
            /* its arms are never quite still: a small sway between the body and a tip that holds. One sine and one cosine
               an arm, turned along it (the first cut called sin twice a point) */
            const float sw = lk->sz * 0.41f, c1 = cosf(O.t * 1.7f + SWAY[i]), s1 = sinf(O.t * 1.7f + SWAY[i]), c2 = cosf(O.t * 2.3f + SWAY[i] * 1.3f), s2 = sinf(O.t * 2.3f + SWAY[i] * 1.3f);
            const float du1 = 2.0f / (float)(N - 1), du2 = 2.6f / (float)(N - 1), ca1 = cosf(du1), sa1 = sinf(du1), ca2 = cosf(du2), sa2 = sinf(du2);
            float x1 = s1, y1 = c1, x2 = s2, y2 = c2;                                           /* sin and cos of the running angle, stepped */
            for (int j = 1; j < N - 1; j++) {
                float t_ = x1 * ca1 + y1 * sa1; y1 = y1 * ca1 - x1 * sa1; x1 = t_;
                t_ = x2 * ca2 + y2 * sa2; y2 = y2 * ca2 - x2 * sa2; x2 = t_;
                const float u = (float)j / (float)(N - 1), env = 4 * u * (1 - u) * sw;
                pa->P[2 * j] += env * x1; pa->P[2 * j + 1] += 0.8f * env * x2;
                const float lim = O.floor - arm_wd(a, u); if (pa->P[2 * j + 1] > lim) pa->P[2 * j + 1] = lim;
            }
        }
        float len = 0; for (int j = 0; j < N - 1; j++) { const float ex = pa->P[2 * j + 2] - pa->P[2 * j], ey = pa->P[2 * j + 3] - pa->P[2 * j + 1]; len += sqrtf(ex * ex + ey * ey); }
        pa->seg = len / (float)(N - 1); if (pa->seg < 0.6f) pa->seg = 0.6f;
        pa->far = a->far; pa->curl = a->curl;
    }
}
static void place(float x) {
    O.x = x; O.y = O.floor - O.lk.rest_h; O.theta = rest_theta(); O.om = 0; O.vx = O.vy = O.sp = 0; O.dir = 1; O.jdir = -1;
    O.squeeze = O.puff = O.tuck = O.blink = 0; O.droop = 1; O.mode = M_REST; O.mode_t = 0; O.gait = 0; O.queue_t = 0; O.sink = 0; O.spread = 0.2f; O.td = DOWN; O.jk = 1.0f;
    for (int i = 0; i < 8; i++) { arm_t *a = &O.arm[i]; a->mv = a->sw = 0; a->freev = 0; a->flen = a->L0; a->curl = 1; a->tip[0] = O.x + neutral(a, 1); a->tip[1] = floor_of(a); memset(a->ang, 0, sizeof a->ang); }
    arms_state(1.0f / 60.0f); arms_pose();
}

/* ---- what the tank sees ---- */
static int g_async;                                   /* the raster on core 1 (the board) */
static void (*g_async_wait)(void);                    /* (audit FW-1) core 0's wait for core 1's raster (firmware main.c ob_wait), or NULL */
void octopus_set_async_wait(void (*wait)(void)) { g_async_wait = wait; }
static volatile int g_ab_ready, g_ab_owed;            /* a picture built behind, ready to take up; one asked of core 1 */
static int g_ab_want;                                 /* this frame's draw found a new picture due */
static unsigned g_ab_gen, g_b_gen;                   /* the animal's generation (octopus_proto_set): a picture core 1 finished for an animal since replaced is never taken up */
static int g_ab_moving;                               /* (tests: pictures sent to core 1 while it moved) */
static int g_ab_n;                                    /* (tests: pictures taken up from core 1) */
static int g_port_growth = -1;
void octopus_proto_set(int on, int growth, float x, float floor, float xlo, float xhi) {
    memset(&O, 0, sizeof O); g_port_growth = -1; g_ab_ready = 0; g_ab_want = 0; g_ab_gen++;   /* (a picture in flight on core 1 is the old animal's: its generation no longer matches) */                       /* (the grids are the body's again: a kept portrait is gone) */
    O.on = on; if (!on) return;
    O.growth = growth < 0 ? 0 : growth > 255 ? 255 : growth; O.floor = floor; O.xlo = xlo; O.xhi = xhi; O.rs = 0x0c70u + (uint32_t)growth * 2654435761u;
    if (g_bg.tone == g_tone) { O.g.tone = g_btone; O.g.part = g_bpart; } else { O.g.tone = g_tone; O.g.part = g_part; }   /* (the picture core 1 may be writing is never the one drawn: the two buffers swap) */
    O.base_floor = O.sand_floor = floor; O.clear = 1; O.den_item = -1; O.fav = (O.rs >> 9) & 1 ? 9 : 2;                    /* it favors the castle or the volcano, by its seed */
    set_look(O.growth); place(clampf(x, xlo, xhi));
    O.think = 2.0f; O.blink_in = 3.0f; O.goal_x = O.x; O.dirty = 1; O.built_t = -1;
    O.present = 1; O.find_cool = 2700.0f + rnd() * 2700.0f; O.rival_x = -1;                                     /* its first find: 45 to 90 minutes in, as the crab's */
}
static float g_hz;
void octopus_set_hz(float hz) { g_hz = hz; }
int octopus_proto_on(void) { return O.on; }
int octopus_fav(void) { return O.fav; }
void octopus_set_fav(int item) { O.fav = item == 9 ? 9 : 2; }      /* (a stage picks it) */
int octopus_den_item(void) { return O.on ? O.den_item : -1; }
void octopus_den_leave(void) { if (O.on && O.inn > 0.01f) { O.leaving = 1; O.home = 0; O.going_home = 0; O.detour = 0; } }   /* (audit SOC-2) the tank: its den is to change - out of this one first */
float octopus_gath(void) { return O.gath; }
float octopus_den_depth(void) { return O.on ? O.den_d : 0.0f; }
float octopus_span(void) { float m = 0; for (int i = 0; i < 8; i++) { const float d = fabsf(O.arm[i].tip[0] - O.x); if (d > m) m = d; } return m; }   /* its widest arm tip from its middle */
float octopus_inn(void) { return O.on ? O.inn : 0.0f; }
float octopus_y(void) { return O.y; }
/* the tank says, every tick, which opening is its den (-1: none it can have), where, and the floor inside it */
/* the tank sends it somewhere on the sand (a look at the crab's door), and off again: a step or two back, startled (a blink, its
   body drawn in a moment), then on with its own life - home, if it has one */
void octopus_debug_put(float x, int lane) { if (!O.on) return; O.lane = lane; O.base_floor = O.sand_floor + 4.0f * lane; O.floor = O.base_floor; place(clampf(x, O.xlo, O.xhi)); }   /* (tests: set down there, in that lane) */
void octopus_debug_state(char *b, int n) { snprintf(b, n, "gh %d ah %d det %d beh %d clear %d fs %d fsx %.0f fsclr %.0f goal %.0f think %.1f vis %d rival %.0f", O.going_home, O.after_home, O.detour, O.behind, O.clear, O.front_shelter, (double)O.fs_x, (double)O.fs_clear, (double)O.goal_x, (double)O.think, O.visiting, (double)O.rival_x); }
int  octopus_bound(void) { return O.on && (O.going_home || O.after_home || O.visiting || O.home || O.detour); }   /* on its way somewhere it must go (home, a look at the door) */
int  octopus_idle(void) { return O.on && O.mode == M_REST && O.inn < 0.01f && !O.going_home && !O.home && O.dig <= 0 && O.find <= 0 && O.raise < 0.02f && !O.find_now; }
void octopus_goto(float x) {
    if (!octopus_idle()) return;
    O.goal_x = clampf(x, O.xlo, O.xhi);
    if (fabsf(O.goal_x - O.x) > 2.0f) { set_mode(M_CRAWL, O.goal_x > O.x ? 1 : -1); if (O.think < 1e8f) O.think = 40.0f; }
}
void octopus_backoff(int dir, float far) {
    if (!O.on || O.inn > 0.01f || O.mode == M_JET || O.mode == M_GATHER) return;
    O.blink = 1.0f; O.puff = 0.6f; O.going_home = 0; O.detour = 0;
    O.goal_x = clampf(O.x + dir * far, O.xlo, O.xhi);
    if (O.mode == M_CRAWL) set_mode(M_REST, 0);
    set_mode(M_CRAWL, dir);
    if (O.think < 1e8f) O.think = 5.0f;
    O.after_home = O.den_item >= 0;                                    /* ... then to its own den */
}
void octopus_rival(float x, float gap, int visiting) { if (O.on) { O.rival_x = x; O.rival_gap = gap; O.visiting = visiting; } }   /* the tank, each tick */
void octopus_front_shelter(int any, float x, float half, float clear_x) { if (O.on) { O.front_shelter = any; O.fs_x = x; O.fs_half = half; O.fs_clear = clear_x; } }   /* the tank, each tick: a castle or a volcano stands IN FRONT */
void octopus_den(int item, float x, float floor_y, int front, float half, int floor_clear) {
    if (!O.on) return;
    if (O.leaving) { if (O.inn < 0.01f) O.leaving = 0; else { O.home = 0; O.going_home = 0; O.detour = 0; } }   /* (out of the piece it is in first: never back in) */
    if (item != O.den_item && O.den_item >= 0 && O.inn > 0.01f) {   /* (audit SOC-2) its den went from under it (shelved, sold): no piece left to come out of - it is
                                                                       out on the sand at once, where it was, never drawn cut to another piece's opening */
        O.inn = 0; O.den_d = 0; O.gath = 0;
    }
    if (item != O.den_item && (O.home || O.going_home)) { O.home = 0; O.going_home = 0; O.detour = 0; }   /* its den went (moved, shelved, taken): out it comes */
    if (item >= 0 && item == O.den_item && O.inn > 0.01f) {          /* (audit SOC-2) ITS DEN MOVED WITH IT INSIDE (the placement page's MOVE, a re-rank's new spot):
                                                                       it moves with the piece, still in it - never left behind at the old door, cut to the new one */
        const float dx = clampf(x, O.xlo, O.xhi) - O.den_x;
        if (fabsf(dx) > 0.001f) {
            O.x += dx; O.goal_x += dx;
            for (int i = 0; i < 8; i++) { arm_t *a = &O.arm[i]; a->tip[0] += dx; a->mp0[0] += dx; a->mp1[0] += dx; }
        }
    }
    O.den_item = item; O.clear = floor_clear;
    if (item >= 0) { O.den_x = clampf(x, O.xlo, O.xhi); O.den_floor = floor_y; O.den_front = front; O.den_half = half; O.den_open = item == 2 ? 26.0f : 26.0f; }   /* (the castle's arch and the volcano's cave: 26 px either side of the middle) */
}
int octopus_lane_front(void) { return O.on && O.lane; }
/* A FIND: the tank says each tick whether the keeper is here and whether the crab has a coin up; a tap on it or its coin takes it */
void octopus_presence(int here, int crab_coin) { O.present = here; O.crab_coin = crab_coin; }
int octopus_found(void) { return O.found; }
/* it grew (the tank's saved growth, 0..255 here): its look rebuilt at the new size where it stands - nothing else of it changes */
void octopus_grow(int growth) {
    growth = growth < 0 ? 0 : growth > 255 ? 255 : growth;
    if (!O.on || growth == O.growth) return;
    if (O.mode != M_REST || O.inn > 0.01f || O.going_home || O.find > 0 || O.dig > 0 || O.raise > 0.01f) return;   /* (only at rest on the sand: the next call finds it so) */
    const int n0 = O.lk.N;
    O.growth = growth; set_look(growth);
    if (O.lk.N != n0) place(O.x);                                     /* (an arm gains a joint, twice in its life: laid out afresh where it rests) */
    else for (int i = 0; i < 8; i++) O.arm[i].flen = O.arm[i].L0;
    O.dirty = 1; O.built_t = -1;
}
/* the tank slept: its find's wait ran too, and one it held up is due again at the wake (the crab's rule) */
void octopus_sleep(float seconds) {
    if (!O.on) return;
    if (O.find > 0 || O.dig > 0) { O.find = 0; O.dig = 0; O.raise = 0; O.find_cool = 0; }
    else O.find_cool = O.find_cool > seconds ? O.find_cool - seconds : 0;
}
/* what the keeper should HEAR, once each: 1 the coin came up, 2 the coin was taken. The tank raises the crab's own two events for them
   (Strato 11-21: "the same sound... those two experiences should be very similar"), so every board plays the crab's sounds */
/* ITS PORTRAIT (the card, the shelf's jar, the shop): its picture and the point on the sand under its middle. The live body's own
   picture when it is on; else one built at rest at that size, the body left off after it */
static OCTO_BIG uint8_t g_ptone[OCTO_GW * OCTO_GH] __attribute__((aligned(4))); static OCTO_BIG uint8_t g_ppart[OCTO_GW * OCTO_GH] __attribute__((aligned(4)));
static OCTO_BIG uint8_t g_obak[sizeof O];
const octo_grid_t *octopus_portrait(int growth, int *mx, int *floor_y) {
    /* A STILL (Strato 11-22, the 1.8: "we dont need to show a live animated view of what its doing.. a still image is enough like you
       have with the crab"): one composed pose - at rest on the sand, every arm out and down - built ONCE for a size into a picture of
       its own, the live body untouched (set aside, the pose built, put back). Never what it is doing, never redrawn while a card is up */
    static octo_grid_t pg; static int pmx, pfl;
    growth = growth < 0 ? 0 : growth > 255 ? 255 : growth;
    if (g_port_growth != growth) {
        /* (audit FW-1) core 1 may be rastering the live animal's next picture this very frame (the card is drawn after the tank's draw sent
           it): wait for it first - its finished picture is then kept below, never thrown away by the set-aside, and the two cores never build
           at once (the sack cache is not theirs to share). Only on a frame the portrait is rebuilt: a card opened, a size step */
        if (g_async && g_async_wait) g_async_wait();
        const unsigned keep_gen = g_ab_gen; const int keep_ready = g_ab_ready, keep_want = g_ab_want;   /* (the live animal's picture in flight is kept: the review) */
        memcpy(g_obak, &O, sizeof O);
        octopus_proto_set(1, growth, 200.0f, 200.0f, 60.0f, 340.0f);
        O.g.tone = g_ptone; O.g.part = g_ppart; O.blink_in = 1e9f; O.think = 1e9f; O.find_cool = 1e9f;
        for (int i = 0; i < 40; i++) octopus_tick(0.04f);
        O.built_t = -1; (void)octopus_grid();
        pg = O.g; pmx = (int)O.x; pfl = (int)O.floor;
        memcpy(&O, g_obak, sizeof O);
        g_ab_gen = keep_gen; g_ab_ready = keep_ready; g_ab_want = keep_want;
        g_port_growth = growth;
    }
    *mx = pmx; *floor_y = pfl;
    return &pg;
}
int octopus_cue(void) { const int c = O.cue; O.cue = 0; return c; }
float octopus_find_left(void) { return O.on ? O.find : 0.0f; }
float octopus_digging(void) { return O.on ? O.dig : 0.0f; }
int octopus_coin(float *x, float *y) {                              /* where its coin is, held up (0: none up yet) */
    if (!O.on || O.find <= 0 || O.raise < 0.6f) return 0;
    const int N = O.lk.N; const float *P = O.pose.arm[1].P;
    *x = P[2 * (N - 1)] + 1.0f; *y = P[2 * (N - 1) + 1] - 9.0f; return 1;
}
/* a tap lands on it (its body's circle, reaching up as the crab's does: a floor tap reads high on the 1.8) */
int octopus_hit(float x, float y) {
    if (!O.on || O.hide) return 0;
    const float bx = x - O.x, by = y - (O.y - 28.0f * 0.5f * O.lk.sz);
    return bx * bx + by * by <= 34.0f * 34.0f * O.lk.sz;
}
/* its next find's wait, for the tank to keep across a shelving or a swap: 0 while it digs or holds one up (due again at once) */
float octopus_find_wait(void) { return !O.on ? -1.0f : (O.find > 0 || O.dig > 0 || O.find_now) ? 0.0f : O.find_cool; }
void octopus_set_find_wait(float s) { if (O.on) O.find_cool = s < 0 ? 0 : s; }
/* its sand dollar taken from its card (COLLECT): wherever the finger is */
int octopus_take(void) {
    if (!O.on || O.find <= 0) return 0;
    O.find = 0; O.found++; O.cue |= 2; O.find_cool = 9000.0f + rnd() * 3600.0f;
    return 5;
}
int octopus_tap(float x, float y) {
    float cx, cy;
    if (!O.on || O.find <= 0) return 0;
    const int on_coin = octopus_coin(&cx, &cy) && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= 26.0f * 26.0f;
    if (!on_coin && !octopus_hit(x, y)) return 0;                     /* (the one circle its card's tap uses: what tank_octopus_find_hit lets through, this takes) */
    O.find = 0; O.found++; O.cue |= 2; O.find_cool = 9000.0f + rnd() * 3600.0f;   /* taken: the next in 2.5 to 3.5 hours */
    return 5;                                                         /* sand dollars (the prototype only counts them: nothing is paid or saved yet) */
}
float octopus_camo(void) { return O.on ? O.camo : 0.0f; }            /* 0 its own colours .. 1 matched */
void octopus_night(int night) { O.night = night; }                  /* (the light is off: nothing to match, and it is out and about) */
float octopus_reach(void) { return O.lk.L * 1.1f; }
float octopus_mlen(void) { return O.lk.m_len; }
float octopus_floor(void) { return O.floor; }
int octopus_from_behind(void) { return O.on && O.behind; }           /* it went into a den IN FRONT from behind the piece: inside, it comes forward out of the dark */
void octopus_hide(int hide) { O.hide = hide; }                      /* (a test's: not drawn, living on) */
float octopus_x(void) { return O.x; }
float octopus_size(void) { return O.lk.sz; }
int octopus_builds(void) { return O.builds; }
static unsigned g_serial;                            /* every picture ever built, never reset (a new animal's first picture is never an old one's: the draw's replay key) */
unsigned octopus_picture_serial(void) { return g_serial; }
long octo_perf_us[6];
int octopus_mode(void) { return O.mode; }
void octopus_cmd(int what, int dir) {                                  /* the sim's keys and its stages: 1 crawl, 2 jet, 0 rest */
    if (!O.on || O.mode == M_JET || O.mode == M_GATHER) return;
    if (what == 0) { if (O.mode == M_CRAWL) set_mode(M_REST, 0); O.think = 4.0f; }
    if (what == 1) { O.goal_x = dir > 0 ? O.xhi : O.xlo; set_mode(M_CRAWL, dir > 0 ? 1 : -1); O.think = 60.0f; }
    if (what == 2 && O.inn < 0.05f) { if (O.mode == M_CRAWL) set_mode(M_REST, 0); set_mode(M_GATHER, dir > 0 ? 1 : -1); }
    if (what == 3 && O.den_item >= 0 && !O.leaving) { if (O.mode == M_CRAWL) set_mode(M_REST, 0); O.going_home = 1; O.detour = 0; O.think = 1e9f; }   /* go home (step() walks it there, by the lane the den needs) */
    if (what == 4) { O.home = 0; O.going_home = 0; O.detour = 0; O.think = 1e9f; }
    if (what == 5) O.camo_now = 1;
    if (what == 6) O.find_now = 1;                                                                                      /* (a stage's: a find is due now) */                                                                                      /* (a stage's: match now, not after 20 s still) */                                                    /* come out */
}
static void step(float dt);
void octopus_think(int on) { O.think = on ? 2.0f : 1e9f; }
void octopus_tick(float dt) {
    if (!O.on) return;
    if (dt > 0.05f) dt = 0.05f;
    const int sub = (int)(dt / 0.0125f) + 1;                          /* (its springs were tuned on short steps) */
    dt /= (float)sub;
    for (int k = 0; k < sub; k++) step(dt);
}
static void step(float dt) {
    O.t += dt; O.mode_t += dt;
    /* the brain: sit, walk somewhere, now and then jet back the long way */
    if (O.mode == M_REST) {
        O.think -= dt;
        if (O.think <= 0 && O.home) { O.home = 0; O.think = 2.5f; }                              /* time to go out: it comes out of the door first */
        else if (O.think <= 0 && O.inn > 0.02f) O.think = 0.3f;
        else if (O.rival_x >= 0 && O.inn < 0.01f && !O.going_home && !O.after_home && !O.visiting && O.dig <= 0 && O.find <= 0 && O.raise < 0.02f && O.think < 1e8f
                 && fabsf(O.rival_x - O.x) < O.rival_gap && O.mode_t > 0.35f) {   /* the crab came near it at rest: it gives ground */
            const float away = O.x >= O.rival_x ? 1.0f : -1.0f; float gx = clampf(O.x + away * O.rival_gap * 1.2f, O.xlo, O.xhi);
            if (O.front_shelter && !O.lane && (gx - O.fs_x) * (O.x - O.fs_x) <= 0 ? 1 : (O.front_shelter && !O.lane && fabsf(gx - O.fs_x) < O.fs_half))
                gx = O.fs_x + (O.x >= O.fs_x ? 1.0f : -1.0f) * (O.fs_half + 4.0f);         /* (in its back lane: never in under a shelter IN FRONT - to its edge at most) */
            if ((gx - O.x) * away <= 0) gx = O.x;                                                  /* (nowhere to give: it holds its ground) */
            if (fabsf(gx - O.x) > 8.0f) { O.goal_x = gx; set_mode(M_CRAWL, gx > O.x ? 1 : -1); O.think = 4.0f + rnd() * 3.0f; }
            else O.mode_t = 0;                                                                     /* (against the glass: it holds its ground) */
        }
        else if (O.think <= 0 && O.den_item >= 0 && rnd() < 0.45f) { octopus_cmd(3, 0); O.think = 9.0f + rnd() * 10.0f; }   /* home for a while */
        else if (O.think <= 0) {
            const float room_l = O.x - O.xlo, room_r = O.xhi - O.x, need = 75.0f * O.lk.sz;
            if (rnd() < 0.28f && (room_l > need || room_r > need)) set_mode(M_GATHER, room_l > room_r ? -1 : 1);
            else {
                /* KEEP APART (Strato 11-22: "the crab and octopus should generally keep their distance when exploring ... soft rules"):
                   of three places on the sand it goes to the one furthest from the crab that it can reach without passing it - a
                   preference, never a wall: with no such place it goes where it would have */
                float best = O.xlo + rnd() * (O.xhi - O.xlo), bd = -1;
                for (int k = 0; k < 3 && O.rival_x >= 0; k++) {
                    const float c = k ? O.xlo + rnd() * (O.xhi - O.xlo) : best;
                    const int passes = (c - O.rival_x) * (O.x - O.rival_x) < 0;
                    const float d = passes ? -1 : fabsf(c - O.rival_x);
                    if (d > bd) { bd = d; best = c; }
                }
                O.goal_x = best;
                if (fabsf(O.goal_x - O.x) < 20) O.goal_x = O.x < (O.xlo + O.xhi) * 0.5f ? O.x + 40 : O.x - 40;
                set_mode(M_CRAWL, O.goal_x > O.x ? 1 : -1);
            }
            O.think = 3.0f + rnd() * 5.0f;
        }
    } else if (O.mode == M_CRAWL) {
        if ((O.goal_x - O.x) * O.dir <= 0 || (O.x <= O.xlo && O.dir < 0) || (O.x >= O.xhi && O.dir > 0)) { O.x = clampf(O.x, O.xlo, O.xhi); set_mode(M_REST, 0); }   /* (at a wall only when walking INTO it: at the right wall it could never set off left, 11-22) */
        else if (O.front_shelter && !O.lane && !O.going_home && !O.behind && (O.fs_x - O.x) * O.dir > 0 && fabsf(O.fs_x - O.x) < O.fs_half - 2.0f)
            { set_mode(M_REST, 0); }   /* in its back lane, walking in under a shelter IN FRONT: it stops at its edge - its front lane first (where the sand is clear) */
        else if (O.rival_x >= 0 && !O.going_home && !O.after_home && !O.visiting && O.think < 1e8f && (O.rival_x - O.x) * O.dir > 0 && fabsf(O.rival_x - O.x) < O.rival_gap)
            { set_mode(M_REST, 0); O.think = 1.0f + rnd() * 1.5f; }   /* walking up to the crab: it stops short, and thinks again soon (away, then) */
    } else if (O.mode == M_GATHER && O.mode_t >= 0.5f) set_mode(M_JET, O.jdir);
    O.blink_in -= dt;
    if (O.blink_in <= 0) { O.blink = 1.0f; O.blink_in = 4.0f + rnd() * 5.0f; }
    if (O.blink > 0) { O.blink -= dt / 0.14f; if (O.blink < 0) O.blink = 0; }
    {   /* its lane, and the way home */
        /* ITS LANE. A shelter IN FRONT (the castle, the volcano) is not a thing to walk THROUGH: its back lane runs through the castle's
           gate, and since 0.4.3 that gate is a den with a back wall and, often, the crab in it (Strato 11-22, the 1.8: "the octopus
           walking sometimes in front of the crab when its in the castle, and its not completely on the ground.. floating a bit" - it
           was passing through the den, on the back lane's floor, 8 px above the den's own). So while a shelter stands IN FRONT it
           keeps to its FRONT lane, before every piece, and passes the whole castle in front. It takes the lane up where it is clear */
        /* (11-22, the 1.8, Strato: "the bottom base of the volcano, stones, briefly drew over the top of the octopus" - going home to a
           volcano BEHIND in its FRONT lane, it was drawn before every piece until halfway in, then behind the volcano and cut by the
           cave's mouth: 355 px of it, then 170, in one frame. The front lane is for passing a shelter IN FRONT, so it keeps it only near
           one; a den BEHIND it goes to from its back lane, and changes lane on clear sand before the door) */
        const int near_fs = O.front_shelter && fabsf(O.x - O.fs_x) < O.fs_half + 44.0f * O.lk.sz + 16.0f;
        /* (11-22, the big screens: sent back from the crab's door it backed off onto clear sand beyond the castle, took its back lane there for
           its volcano BEHIND, and walked home under the castle IN FRONT - through the crab's den. The back lane is for the last stretch: while
           a shelter IN FRONT still stands between it and its door it keeps its front lane, and changes on the clear sand after it) */
        const int fs_between = O.front_shelter && O.den_item >= 0 && (O.fs_x - O.x) * (O.den_x - O.fs_x) > 0.0f && fabsf(O.den_x - O.fs_x) > O.fs_half;
        const int home_back = O.den_item >= 0 && !O.den_front && (O.going_home || O.home || O.inn > 0.01f) && !(fs_between && O.inn < 0.01f && !O.home);
        const int want = (near_fs && !home_back) || (O.den_item >= 0 && O.den_front && (O.going_home || O.home || O.inn > 0.01f));
        if (!O.going_home && !O.home && O.inn < 0.01f) O.behind = 0;
        if (O.lane != want && O.clear && !O.behind) O.lane = want;
        if (want && !O.lane && !O.clear && O.front_shelter && !O.behind && !O.going_home && !O.home && O.inn < 0.01f && O.mode != M_JET && O.mode != M_GATHER && O.dig <= 0 && O.find <= 0) {
            /* under a shelter IN FRONT in its back lane (the piece was set down over it, or it was put there): out from under it to the
               nearest clear sand, where it takes its front lane - never on through the gate */
            /* (11-22, the 1.8: it stood crawling on the spot at the castle's edge - the spot it worked out for itself was not clear by
               the tank's own measure (a coral IN FRONT beside the castle), so it was sent there again every tick. The tank finds the
               spot now: the nearest sand clear of EVERY piece IN FRONT) */
            const float gx = O.fs_clear;
            if (gx >= O.xlo && gx <= O.xhi && fabsf(gx - O.x) > 3.0f && (O.mode == M_REST || (O.mode == M_CRAWL && (O.goal_x - O.x) * (gx - O.x) < 0))) { O.goal_x = gx; set_mode(M_CRAWL, gx > O.x ? 1 : -1); O.think = O.think < 1e8f ? 6.0f : O.think; }
        }                                         /* (only where no piece IN FRONT is over it: never seen) */
        const float bf = O.sand_floor + 4.0f * O.lane;
        O.base_floor += (bf - O.base_floor) * mn1(dt * 3.0f);
        if (O.after_home && O.mode == M_REST && O.mode_t > 1.2f) { O.after_home = 0; if (O.den_item >= 0 && O.inn < 0.01f) O.going_home = 1; }
        if (O.going_home && O.den_item < 0) { O.going_home = 0; O.detour = 0; }
        if (O.going_home && O.mode == M_REST) {
            const int lane_ok = O.den_front ? (O.lane || O.behind) : (!O.lane || O.behind);   /* (a den BEHIND: from its back lane) */
            if (O.detour && lane_ok) O.detour = 0;
            if (!lane_ok && !O.detour) {                                                      /* under the piece in the back lane: out to where it is clear, then round to its front */
                const float out = O.den_half + 40.0f * O.lk.sz + 5.0f; float gx = O.den_x + (O.x >= O.den_x ? out : -out);
                if (gx > O.xhi || gx < O.xlo) gx = O.den_x + (O.x >= O.den_x ? -out : out);
                if (O.front_shelter && O.fs_clear >= O.xlo && O.fs_clear <= O.xhi) gx = O.fs_clear;   /* (the tank's nearest sand clear of every piece IN FRONT: where its lane may change) */
                if (gx > O.xhi || gx < O.xlo) O.behind = 1;                                   /* (no clear sand either side: it goes in from behind the piece, and comes forward out of the dark) */
                else { O.detour = 1; O.goal_x = gx; if (fabsf(gx - O.x) > 1.5f) set_mode(M_CRAWL, gx > O.x ? 1 : -1); }
            } else if (!lane_ok && O.detour && fabsf(O.goal_x - O.x) < 3.0f) { O.detour = 0; O.behind = 1; }   /* (it got there and it is still not clear: from behind, then) */
            else if (lane_ok && fabsf(O.den_x - O.x) > 1.5f) { O.goal_x = O.den_x; set_mode(M_CRAWL, O.den_x > O.x ? 1 : -1); }
            else if (lane_ok) { O.x = O.den_x; O.home = 1; O.going_home = 0; }
        }
    }
    {   /* going in and coming out: at the door only, at rest. Inside, the floor is the opening's and it sits further back.
           THROUGH THE DOOR (Strato 11-22, the 1.8: it "popped behind awkwardly not using the arch gate, then phased through the castle" -
           it went in with its arms spread wider than the opening, drawn before the wall one frame and cut by it the next): at the door it
           first DRAWS ITS ARMS IN under it until all of it stands within the opening, only then goes in, and coming out it stays
           drawn in until it is out; then its arms find their places again, one at a time */
        const int at_door = O.den_item >= 0 && O.mode == M_REST && fabsf(O.x - O.den_x) < 3.0f;
        const int through = !g_no_gather && ((O.home && at_door) || O.inn > 0.01f);
        if (through) {                                                                     /* how far in its arms must come for this opening, at its size */
            float far_as = 1.0f; for (int i = 0; i < 8; i++) { const float n = fabsf(neutral(&O.arm[i], 1)); if (n > far_as) far_as = n; }
            const float room = (O.den_open > 6.0f ? O.den_open : 24.0f) - 12.5f - 1.5f * O.lk.w1;   /* (an arm rests within 2 sz of its place: the room allows for it, and for the 3 px the door's middle may be off) */
            O.gath_k = clampf(room / far_as, 0.15f, 1.0f);
        }
        O.gath += ((through ? 1.0f : 0.0f) - O.gath) * mn1(dt * (through ? 3.0f : 2.0f));
        int fits = O.gath > 0.9f || g_no_gather;
        for (int i = 0; i < 8 && fits && !g_no_gather; i++) fits = !O.arm[i].mv && fabsf(O.arm[i].tip[0] - O.den_x) <= (O.den_open > 6.0f ? O.den_open : 24.0f) - 1.0f;
        if (O.home && at_door && !fits && O.inn < 0.02f) O.inn = 0;                        /* (not yet: it waits at the door, gathering) */
        else
        O.inn += (((O.home && at_door) ? 1.0f : 0.0f) - O.inn) * mn1(dt * 2.2f);
        if (!O.home && O.inn < 0.01f) O.inn = 0;
        if (O.inn >= 0.5f) { O.den_d += dt / 0.6f; if (O.den_d > 1) O.den_d = 1; } else O.den_d = 0;   /* (the crab's CR_INN_S, 0.4.3) */
        /* up onto the den's floor in the FIRST half of the way in, while it is still drawn whole on the sand: at the half it is drawn in
           the den, cut to the opening, and the cave's floor is 12 px over the sand - halfway up, its lower 6 px were cut there, the
           volcano's foot stones over it for a moment (Strato 11-22). The way out the same, the other way round */
        const float e = ease(O.inn * 2.0f), fl = O.base_floor + ((O.den_item >= 0 ? O.den_floor : O.base_floor) - O.base_floor) * e;
        O.y += fl - O.floor; O.floor = fl;
    }
    {   /* A FIND */
        const octo_look_t *lk = &O.lk;
        if (O.find <= 0 && O.dig <= 0 && O.find_cool > 0) O.find_cool -= dt;
        const int can = O.mode == M_REST && O.inn < 0.01f && !O.going_home && !O.night && O.present && !O.crab_coin;
        if (!can) O.find_now = 0;                                                               /* (a stage's ask, when it cannot: dropped, not kept for later) */
        if (O.find <= 0 && O.dig <= 0 && can && (O.find_cool <= 0 || O.find_now)) { O.dig = 1.7f; O.find_now = 0; O.think = O.think > 1e8f ? O.think : 200.0f; }
        if (O.dig > 0) {
            if (O.mode != M_REST) O.dig = 0;                                                    /* (called away mid dig: nothing found) */
            else {
                const float before = O.dig; O.dig -= dt;
                const int beat0 = (int)(before / 0.34f), beat1 = (int)(O.dig / 0.34f);
                if (beat1 != beat0 || before == 1.7f) {                                         /* a scrabble: the two front arms in turn */
                    arm_t *a = &O.arm[(beat1 & 1) ? 3 : 4];
                    start_mv(a, O.x + lk->reach[a->idx] * lk->L * ((beat1 & 2) ? 0.55f : 1.25f), floor_of(a), 0.30f, 1.2f * lk->sz);
                }
                if (O.dig <= 0) { O.dig = 0; O.find = 180.0f; O.wave = 0; O.cue |= 1; }   /* (the crab's own cue: its find sound) */
            }
        }
        if (O.find > 0) {
            O.find -= dt; O.wave += dt;
            if (O.mode != M_REST || O.night) O.find = 0;                                        /* (it has to go: the coin is dropped and buried again) */
            if (O.find <= 0) { O.find = 0; O.find_cool = 3600.0f + rnd() * 1800.0f; }           /* untaken: back in 60 to 90 minutes */
        }
        O.raise += ((O.find > 0 ? 1.0f : 0.0f) - O.raise) * mn1(dt * (O.find > 0 ? 2.6f : 5.0f));
        O.arm[1].up = O.raise;
    }
    {   /* still, by day: after 20 s it matches. Any move - a walk, a flee, going in or out of its den - and it is itself again at once */
        int moving = O.mode != M_REST || O.going_home || (O.inn > 0.02f && O.inn < 0.98f) || fabsf(O.vx) > 0.5f || O.dig > 0 || O.raise > 0.02f;
        for (int i = 0; i < 8 && !moving; i++) moving = O.arm[i].sw || O.arm[i].freev > 0;
        if (moving) O.still_t = 0; else O.still_t += dt;
        if (O.mode != M_REST) O.camo_now = 0;                                                      /* (a stage's ask holds through its settling, and ends with its next walk) */
        const int want = !moving && !O.night && O.inn < 0.5f && (O.still_t > 20.0f || O.camo_now);   /* (never in its den: it is in the dark there, and matching costs a board time every frame) */
        O.camo += want ? dt / 3.0f : -dt / 0.4f; O.camo = clampf(O.camo, 0, 1);
    }
    switch (O.mode) { case M_REST: m_rest(dt); break; case M_CRAWL: m_crawl(dt); break; case M_GATHER: m_gather(dt); break; default: m_jet(dt); break; }
    arms_state(dt);
}
/* the grid as it stands: rebuilt 12 times a second while it moves, 4 while it only breathes (a held frame is a blit) */
static float g_b_t, g_b_x, g_b_y;
void octopus_set_async(int on) { g_async = on; g_ab_ready = 0; g_ab_owed = 0; g_ab_want = 0; }
static void octopus_async_take(void) {                /* (core 0, in the draw) the picture core 1 finished becomes the one drawn */
    __sync_synchronize();                                             /* (its bytes after the flag: the other side of core 1's barrier) */
    uint8_t *t_ = O.g.tone, *p_ = O.g.part;
    O.g = g_bg; g_bg.tone = t_; g_bg.part = p_;
    O.built_t = g_b_t; O.builds++; g_serial++; O.base_x0 = O.g.x0; O.base_y0 = O.g.y0; O.built_x = g_b_x; O.built_y = g_b_y;
    g_ab_ready = 0; g_ab_n++;
}
/* (the sack-replay study, 11-22: the sack's inputs at this picture, quantized: frac x and y to 1/qf px, the breath to 1/qb of its swing,
   theta to qt steps a turn, squeeze/puff/droop to 1/qs. Returns a hash of them - equal hashes would replay one sack) */
unsigned octopus_debug_sack_key(int qf, int qb, int qt, int qs) {
    const octo_pose_t *p = &O.pose;
    const int fx = (int)((p->x - floorf(p->x)) * qf), fy = (int)((p->y - floorf(p->y)) * qf);
    const int br = (int)((sinf(p->t * 2.4f) + 1.0f) * 0.5f * qb + 0.5f), th = (int)(p->theta / (2 * PI) * qt + 0.5f);
    const int sq = (int)(p->squeeze * qs + 0.5f), pf = (int)(p->puff * qs + 0.5f), dr = (int)(p->droop * qs + 0.5f);
    unsigned h = 2166136261u; const int v[7] = { fx, fy, br, th, sq, pf, dr };
    for (int i = 0; i < 7; i++) { h ^= (unsigned)v[i]; h *= 16777619u; }
    return h;
}
/* (tests, octo-den 26: the picture drawn now, built again at the same pose with the sack drawn fresh: the cells that differ) */
int octopus_debug_sack_check(void) {
    extern void octo_debug_sack_bypass(int on);
    static OCTO_BIG uint8_t t2[OCTO_GW * OCTO_GH], p2[OCTO_GW * OCTO_GH];
    if (!O.on) return 0;
    octo_grid_t g2 = O.g; g2.tone = t2; g2.part = p2;
    octo_debug_sack_bypass(1); octo_build(&g2, &O.lk, &O.pose); octo_debug_sack_bypass(0);
    int d = 0;
    for (int y = 0; y < O.g.h; y++) for (int x = 0; x < O.g.w; x++) { const int i = y * OCTO_GW + x; d += O.g.tone[i] != t2[i] || O.g.part[i] != p2[i]; }
    return d;
}
int octopus_debug_async_moving(void) { const int n = g_ab_moving; g_ab_moving = 0; return n; }
int octopus_debug_async_takes(void) { const int n = g_ab_n; g_ab_n = 0; return n; }
/* (core 0, after this frame's draw, before the frame is sent) the next picture's pose, laid out here - cheap; its raster asked of
   core 1. 1: a build is owed (call octopus_async_build there, then wait for it before the next tick) */
int octopus_async_prep(void) {
    if (!g_async || !O.on || O.hide || !g_ab_want || g_ab_owed || g_ab_ready) return 0;
    g_ab_want = 0;
    octo_pose_t *p = &O.pose;
    p->x = O.x; p->y = O.y; p->theta = O.theta; p->rest_theta = rest_theta(); p->squeeze = O.squeeze; p->puff = O.puff; p->droop = O.droop; p->t = O.t; p->blink = O.blink; p->floor = O.floor;
    arms_pose();
    p->stream = O.stream;
    {   const float rot = wrap(O.theta - rest_theta()), tp = clampf((fabsf(rot) - 0.25f) / 0.85f, 0, 1), cr = cosf(-rot), sr = sinf(-rot);
        const float sy = -0.30f * O.lk.head[1], ex = -sy * sr, ey = sy * cr;
        p->eye_set = 1;
        p->eye_at[0] = O.lk.eye_at[0] * (1 - tp) + ex * tp; p->eye_at[1] = O.lk.eye_at[1] * (1 - tp) + ey * tp;
        p->far_eye[0] = O.lk.far_eye[0] * (1 - tp) + ex * tp; p->far_eye[1] = O.lk.far_eye[1] * (1 - tp) + ey * tp; }
    g_bpose = *p; g_blk = O.lk;                                       /* (core 1 reads only these copies: nothing of O while core 0 runs on) */
    if (!g_bg.tone) { g_bg.tone = g_btone; g_bg.part = g_bpart; }
    g_b_t = O.t; g_b_x = O.x; g_b_y = O.y; g_b_gen = g_ab_gen;
    if (O.mode != M_REST || O.dig > 0 || O.raise > 0.01f) g_ab_moving++;   /* (tests: none may go to core 1 while it moves) */
    g_ab_owed = 1;
    return 1;
}
void octopus_async_build(void) {                      /* (core 1) the raster, into the picture behind */
    if (!g_ab_owed) return;
    uint8_t *t_ = g_bg.tone, *p_ = g_bg.part;
    octo_build(&g_bg, &g_blk, &g_bpose);
    g_bg.tone = t_; g_bg.part = p_;
    __sync_synchronize();                                             /* (the picture's bytes out before the flag that says so: the other core reads the flag) */
    g_ab_owed = 0; g_ab_ready = 1;
}
const octo_grid_t *octopus_grid(void) {
    if (!O.on || O.hide) return NULL;
    int moving = O.mode != M_REST || fabsf(O.vy) > 1.0f || O.blink > 0;
    for (int i = 0; i < 8 && !moving; i++) moving = O.arm[i].mv || O.arm[i].freev > 0;
    for (int i = 0; i < 8 && !moving; i++) moving = O.arm[i].sw;
    /* Strato 11-21: "the frame rate of the wave is pretty choppy": digging, and the arm up with the coin, are MOVING (the coin is drawn
       every frame at the arm's tip, and the arm under it was held at the resting 6 a second) */
    if (O.dig > 0 || O.raise > 0.01f) moving = 1;
    if ((O.inn > 0.001f && O.inn < 0.999f) || (O.gath > 0.001f && O.gath < 0.999f)) moving = 1;
    if (g_why_on) {                                                   /* (tests: what kept it moving this ask) */
        int w = 0;
        if (O.mode != M_REST) { w |= 1; } if (fabsf(O.vy) > 1.0f) { w |= 2; } if (O.blink > 0) { w |= 4; }
        for (int i = 0; i < 8; i++) { if (O.arm[i].mv) w |= 8; if (O.arm[i].freev > 0) w |= 16; if (O.arm[i].sw) w |= 32; }
        if (O.dig > 0 || O.raise > 0.01f) w |= 64;
        if (O.inn > 0.001f && O.inn < 0.999f) { w |= 128; } if (O.gath > 0.001f && O.gath < 0.999f) { w |= 256; }
        g_why |= w;
    }   /* (going in, coming out, drawing its arms in: at the den's mouth a picture 1/6 s old stood lower than it, and the cave's floor cut its arms, 11-22) */
    /* Strato 11-21, at the sim: "the frame rate of the octopus seemed slower than the fish, grass, everything else". It was:
       12 a second by design, and 8 in fact (a 0.04 s frame never lands on 1/12). So while it MOVES it is rebuilt every
       frame, like everything round it. At rest its sway is slow and under a pixel a frame: 6 a second. g_hz caps the
       moving rate for an A/B (0 = every frame). On a board this is the cost to watch: docs/octopus.md section 27. */
    /* THE BOARD'S FRAME (11-22, measured on the 1.8: a walking adult rebuilt every frame cost 2 to 4 ms of a 42.7 ms frame, and the
       frame rate may not drop). Crawling, its ARMS are laid out every other frame, and between two layouts the whole picture
       still moves with it every frame (the grid's corner follows its body by whole pixels: at a crawl that is half a pixel a
       frame, so a planted tip is never more than a pixel off the sand it holds). Jetting, digging and waving a coin it is still
       rebuilt every frame: those move far in a frame. */
    /* (11-22, Strato on the 1.8: "the baby octopus looks a bit jittery as its moving ... the animation could be smoother". It was this:
       the half-rate layout carried along between solves made its middle step BACK on one frame in seven while it walked forward. The
       arms are laid out every frame again; g_glide is only the A/B) */
    const int glide = g_glide && O.mode == M_CRAWL && O.dig <= 0 && O.raise <= 0.01f && O.blink <= 0;
    const float every = moving ? (g_hz > 0 ? 1.0f / g_hz - 0.004f : glide ? 0.06f : 0.0f) : 1.0f / 6.0f;
    if (O.built_t >= 0 && O.t - O.built_t < every && moving) {         /* not rebuilt this frame: carried along */
        O.g.x0 = O.base_x0 + ((int)floorf(O.x) - (int)floorf(O.built_x));
        O.g.y0 = O.base_y0 + ((int)floorf(O.y) - (int)floorf(O.built_y));
    }
    /* (11-22, the 1.8 a slide show: with every == 0 (moving) this rebuilt the WHOLE picture on every CALL, and octo_covers called it for
       every pixel of the castle's arch shade - up to a second a frame. A picture is built at most once a tick, whoever asks) */
    /* THE BUILD ON THE SECOND CORE (0.4.3, the 1.8: the adult walking cost a frame, its picture's raster 2.5 to 4 ms of it). With
       g_async (the board), the raster is done by octopus_async_build on core 1 while core 0 sends the frame, from the pose
       octopus_async_prep took after this frame's draw; the next draw takes it up here (one frame behind the body: the same for
       every frame, so smooth). At a den's mouth, digging or with the coin up it is built here, now: a picture a frame old stood
       below the cave's floor there once (11-22) */
    /* (the coordinator's call on the 1.8's numbers, 11-22: walking, its raster on core 1 cost every model decision ~300 ms past the 4.2 s
       limit. Only a resting picture - 6 a second, ~1 ms a frame - goes to core 1; moving, it is built here and costs the frame, never the decision) */
    const int sync_now = !g_async || moving || O.built_t < 0 || (O.inn > 0.001f && O.inn < 0.999f) || (O.gath > 0.001f && O.gath < 0.999f) || O.dig > 0 || O.raise > 0.01f;
    int stale = 0;
    if (!sync_now) {
        if (g_ab_ready && g_b_gen == g_ab_gen && g_b_t >= O.built_t && O.t - g_b_t <= 0.09f) octopus_async_take();   /* (never one older than a picture built here since, nor one left
                                                                         from before a page, a sleep, the loading: the review, 11-22) */
        else if (g_ab_ready) g_ab_ready = 0;
        stale = O.t - O.built_t > every + 0.09f;                      /* (the picture drawn is from before such a gap: built here, now, fresh) */
        if (!stale) {
            g_ab_want = O.t - O.built_t >= every && O.t != O.built_t && O.t != g_b_t;   /* (prep lays the next one out after this draw, if one is due: once a tick) */
            return &O.g;
        }
    }
    g_ab_want = 0;
    if (O.built_t < 0 || (O.t - O.built_t >= every && O.t != O.built_t)) {
        octo_pose_t *p = &O.pose;
        p->x = O.x; p->y = O.y; p->theta = O.theta; p->rest_theta = rest_theta(); p->squeeze = O.squeeze; p->puff = O.puff; p->droop = O.droop; p->t = O.t; p->blink = O.blink; p->floor = O.floor;
        arms_pose();                                                  /* (the curves are laid out here, once a picture) */
        p->stream = O.stream;
        {   /* side on as it turns to flee: ONE eye, on the upper side of the head; the far one slides behind the near one */
            const float rot = wrap(O.theta - rest_theta()), tp = clampf((fabsf(rot) - 0.25f) / 0.85f, 0, 1), cr = cosf(-rot), sr = sinf(-rot);
            const float sy = -0.30f * O.lk.head[1], ex = -sy * sr, ey = sy * cr;
            p->eye_set = 1;
            p->eye_at[0] = O.lk.eye_at[0] * (1 - tp) + ex * tp; p->eye_at[1] = O.lk.eye_at[1] * (1 - tp) + ey * tp;
            p->far_eye[0] = O.lk.far_eye[0] * (1 - tp) + ex * tp; p->far_eye[1] = O.lk.far_eye[1] * (1 - tp) + ey * tp;
        }
        octo_build(&O.g, &O.lk, p); O.built_t = O.t; O.builds++; g_serial++;
        O.base_x0 = O.g.x0; O.base_y0 = O.g.y0; O.built_x = O.x; O.built_y = O.y;
    }
    return &O.g;
}
