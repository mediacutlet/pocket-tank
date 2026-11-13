#define _DEFAULT_SOURCE   /* setenv under -std=c11 */
/* selftest_link.c — the tank link's two promises under a hostile radio
 * (common/link.h; docs/tank-link-0.4.md).
 *
 * 2 to 4 pretend tanks, each with its fish, run the real link engine over a
 * pretend radio that drops, delays, reorders and repeats frames and cuts out
 * one way or both. Tanks die and come back, go to sleep, drift, and keepers
 * close doors; fish come and go at random. After every 10 ms step:
 *   - NEVER TWO: no fish is simulated by two tanks (home, or one host);
 *   - NEVER ZERO FOR LONG: a fish frozen at a living home is simulated
 *     somewhere again within LINK_RESTORE_MS (+ the radio's delay);
 *   - the tank's view and the engine's agree (a visitor here = link_hosting);
 *   - a fish's meals never go backwards across a trip.
 * At the end the radio heals and every fish must be home, with no visitor
 * anywhere. Then 200k frames of noise and mutated real frames into a spare
 * engine: nothing may crash. No tank_t here: the engine is tested alone, the
 * tank's half comes with the doors (phase 4). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "link.h"

#define NT_MAX   4
#define NF       3                 /* fish per tank */
#define STEP_MS  10
#define QCAP     8192

static uint32_t rs = 1;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static float    rndf(void) { return (rnd() & 0xffffff) / 16777216.0f; }
static uint32_t rndi(uint32_t lo, uint32_t hi) { return lo + rnd() % (hi - lo + 1); }

typedef enum { F_HOME, F_FROZEN } fst_t;
typedef struct {
    uint32_t uid; fst_t st; passport_t pp;
    bool offered; uint32_t cross_at;           /* CROSS_OK: when it reaches the edge (0 = not crossing) */
    uint32_t restore_at;                       /* a boot found it away: the tank's own restore */
    uint32_t limbo_since;                      /* frozen, home alive, nobody simulating it */
    int32_t  eaten_out;                        /* its meals when it left */
} mfish_t;
typedef struct { bool used; uint32_t uid, home; passport_t pp; uint32_t leave_at; link_edge_t edge; } mvis_t;
typedef struct {
    uint32_t uid; link_t *L; bool alive, radio; uint32_t back_at, radio_at;
    mfish_t f[NF];
    mvis_t v[LINK_VISIT_MAX + 1];
    bool moving; uint32_t move_until;
    uint32_t arm_at[LINK_EDGES];
} mtank_t;
typedef struct { int from, to; uint32_t at; int len; uint8_t b[LINK_FRAME_MAX]; } frame_t;

static mtank_t T[NT_MAX];
static int nt;
static uint32_t now;
static frame_t Q[QCAP]; static int qn;
static float drop_p[NT_MAX][NT_MAX]; static uint32_t cut_until[NT_MAX][NT_MAX];
static bool chaos;
static int fails;
static struct { int drift_notice, drift_unlink, ambiguous, cross, back, restore, poof, homeward, linked, unlinked, refused, frames, dropped, boot_restore, deaths, sleeps, drifts, closes, max_limbo; } st;
static uint8_t pool[64][LINK_FRAME_MAX]; static int pool_len[64], pool_n;
static int evc[NT_MAX][LEV_COUNT]; static uint32_t evt[NT_MAX][LEV_COUNT];   /* scenarios: how many of each event, and the last one's time */
static uint32_t stay_ms;                       /* scenarios: how long a visitor stays (0 = at random) */
static uint32_t max_delay_ms = 300;            /* the radio's worst delay (LINK_DELAY_MS to try others) */
static int cross_ms = -1;                      /* scenarios: CROSS_OK to the edge (-1 = at random, sometimes too late) */
static int drop_type; static uint32_t drop_type_until;   /* scenarios: lose every frame of this type until then */
static int drop_type2; static uint32_t drop_type2_until;

#define LCHECK(cond, ...) do { if (!(cond)) { if (fails < 12) { printf("FAIL t=%u: ", now); printf(__VA_ARGS__); printf("\n"); } fails++; } } while (0)

static mvis_t *vis_find(mtank_t *t, uint32_t uid) { for (int i = 0; i <= LINK_VISIT_MAX; i++) if (t->v[i].used && t->v[i].uid == uid) return &t->v[i]; return NULL; }
static int vis_n(mtank_t *t) { int n = 0; for (int i = 0; i <= LINK_VISIT_MAX; i++) n += t->v[i].used; return n; }

/* ---- the pretend radio ---- */
static void enqueue(int from, int to, const uint8_t *b, int len) {
    st.frames++;
    if (drop_type && len > 3 && b[3] == drop_type && (int32_t)(drop_type_until - now) > 0) { st.dropped++; return; }
    if (drop_type2 && len > 3 && b[3] == drop_type2 && (int32_t)(drop_type2_until - now) > 0) { st.dropped++; return; }
    if (chaos && (rndf() < drop_p[from][to] || (int32_t)(cut_until[from][to] - now) > 0)) { st.dropped++; return; }
    int copies = chaos && rndf() < 0.03f ? 2 : 1;      /* a repeat now and then */
    for (int c = 0; c < copies && qn < QCAP; c++) {
        frame_t *f = &Q[qn++];
        f->from = from; f->to = to; f->len = len; memcpy(f->b, b, (size_t)len);
        f->at = now + (chaos ? rndi(1, max_delay_ms) : rndi(1, 20));   /* jitter reorders */
    }
    if (pool_n < 64 && rndf() < 0.01f) { memcpy(pool[pool_n], b, (size_t)len); pool_len[pool_n++] = len; }
}
static void h_send(void *ctx, uint32_t to, const uint8_t *buf, int len) {
    mtank_t *t = ctx; int from = (int)(t - T);
    if (!t->alive || !t->radio) return;
    for (int i = 0; i < nt; i++) if (i != from && (!to || T[i].uid == to)) enqueue(from, i, buf, len);
}
static int h_room(void *ctx) { mtank_t *t = ctx; return LINK_VISIT_MAX - vis_n(t); }
static bool h_visitor(void *ctx, uint32_t uid, passport_t *pp) {
    mvis_t *v = vis_find(ctx, uid); if (!v) return false; *pp = v->pp; return true;
}
static void deliver(void) {
    for (int i = 0; i < qn; ) {
        frame_t *f = &Q[i];
        if ((int32_t)(f->at - now) > 0) { i++; continue; }
        mtank_t *t = &T[f->to];
        if (t->alive && t->radio) link_rx(t->L, f->b, f->len, -40 - (int)(rnd() % 30), now);
        Q[i] = Q[--qn];
    }
}

/* ---- the pretend tanks ---- */
static void boot(mtank_t *t, bool first) {
    link_host_t h = { t, h_send, h_room, h_visitor, NULL, NULL, NULL, NULL };
    link_init(t->L, t->uid, &h, rnd(), now);
    t->alive = true; t->radio = true; t->moving = false; t->arm_at[0] = t->arm_at[1] = 0;
    memset(t->v, 0, sizeof t->v);
    for (int k = 0; k < NF; k++) {
        mfish_t *f = &t->f[k];
        f->offered = false; f->cross_at = 0;
        if (!first && f->st == F_FROZEN) { f->restore_at = now + LINK_RESTORE_MS; st.boot_restore++; }   /* the save said: away */
    }
    link_mode(t->L, true);
}
static mfish_t *fish_of(mtank_t *t, uint32_t uid) { for (int k = 0; k < NF; k++) if (t->f[k].uid == uid) return &t->f[k]; return NULL; }
static void fish_home(mtank_t *t, uint32_t uid, const passport_t *pp, const char *how) {
    mfish_t *f = fish_of(t, uid);
    LCHECK(f, "tank %d: %s for a fish that is not its own", (int)(t - T), how);
    if (!f) return;
    LCHECK(f->st == F_FROZEN, "tank %d: %s for fish %08x that was already home", (int)(t - T), how, uid);
    if (pp) { LCHECK(pp->eaten >= f->eaten_out, "fish %08x came back with fewer meals (%d < %d)", uid, pp->eaten, f->eaten_out); f->pp.eaten = pp->eaten; }
    f->st = F_HOME; f->restore_at = 0; f->offered = false; f->cross_at = 0;
}
static void run_events(mtank_t *t) {
    link_ev_t e;
    while (link_poll(t->L, &e)) {
        evc[t - T][e.type]++; evt[t - T][e.type] = now;
        switch (e.type) {
        case LEV_CROSS_OK: { mfish_t *f = fish_of(t, e.uid); if (f && f->offered) f->cross_at = now + (cross_ms >= 0 ? (uint32_t)cross_ms : rndi(0, 2600)); break; }   /* sometimes too late */
        case LEV_CROSS_NO: { mfish_t *f = fish_of(t, e.uid); if (f) { f->offered = false; f->cross_at = 0; } st.refused++; break; }
        case LEV_VISITOR_IN: {
            LCHECK(!vis_find(t, e.uid), "tank %d: a visitor arrived twice", (int)(t - T));
            mvis_t *v = NULL; for (int i = 0; i <= LINK_VISIT_MAX && !v; i++) if (!t->v[i].used) v = &t->v[i];
            LCHECK(v && vis_n(t) < LINK_VISIT_MAX, "tank %d: more visitors than LINK_VISIT_MAX", (int)(t - T));
            if (v) { v->used = true; v->uid = e.uid; v->home = e.peer; v->pp = e.pp; v->edge = e.edge; v->leave_at = now + (stay_ms ? stay_ms : rndi(1500, 30000)); }
            st.cross++; break;
        }
        case LEV_VISITOR_POOF: { mvis_t *v = vis_find(t, e.uid); if (v) v->used = false; st.poof++; break; }
        case LEV_VISITOR_HOMEWARD: { mvis_t *v = vis_find(t, e.uid); if (v) { uint32_t soon = now + rndi(0, 5000); if ((int32_t)(v->leave_at - soon) > 0) v->leave_at = soon; } st.homeward++; break; }
        case LEV_FISH_BACK: fish_home(t, e.uid, &e.pp, "FISH_BACK"); st.back++; break;
        case LEV_FISH_RESTORE: fish_home(t, e.uid, &e.pp, "FISH_RESTORE"); st.restore++; break;
        case LEV_LINKED: st.linked++; break;
        case LEV_UNLINKED: st.unlinked++; st.drift_unlink += e.reason == LINK_WHY_DRIFT; break;
        case LEV_DRIFT: st.drift_notice++; break;
        case LEV_ASKING: st.ambiguous += e.reason >= 2; break;
        default: break;
        }
    }
}
static void tank_step(mtank_t *t) {
    if (!t->alive) { if ((int32_t)(now - t->back_at) >= 0) boot(t, false); else return; }
    if (!t->radio && (int32_t)(now - t->radio_at) >= 0) { t->radio = true; link_mode(t->L, true); }
    if (t->moving && (int32_t)(now - t->move_until) >= 0) t->moving = false;
    link_moving(t->L, t->moving);
    for (int e = 0; e < LINK_EDGES; e++) if (t->arm_at[e] && (int32_t)(now - t->arm_at[e]) >= 0) { link_mode(t->L, true); link_arm(t->L, (link_edge_t)e); t->arm_at[e] = 0; }
    link_tick(t->L, now);
    run_events(t);
    for (int k = 0; k < NF; k++) {
        mfish_t *f = &t->f[k];
        if (f->restore_at && (int32_t)(now - f->restore_at) >= 0) { fish_home(t, f->uid, NULL, "the boot's restore"); st.restore++; }
        if (f->st == F_HOME && f->cross_at && (int32_t)(now - f->cross_at) >= 0) {
            f->cross_at = 0; f->offered = false;
            if (link_cross(t->L, f->uid)) { f->st = F_FROZEN; f->eaten_out = f->pp.eaten; }
        }
        if (chaos && f->st == F_HOME && !f->offered && rndf() < 0.004f) {
            link_edge_t e = (link_edge_t)(rnd() % 2);
            if (link_door_open(t->L, e)) { f->offered = true; link_offer(t->L, e, &f->pp, rndf(), rndf() * 6 - 3, 40); }
        }
    }
    for (int i = 0; i <= LINK_VISIT_MAX; i++) {
        mvis_t *v = &t->v[i];
        if (!v->used) continue;
        if (rndf() < 0.002f) v->pp.eaten++;                 /* the host's keeper feeds it */
        if ((int32_t)(now - v->leave_at) >= 0) { link_leaving(t->L, v->uid, &v->pp, rndf(), 0, 40); v->used = false; }
    }
    run_events(t);
}

/* ---- the troubles ---- */
static void troubles(void) {
    if (rndf() < 0.0004f) { int a = (int)(rnd() % nt), b = (int)(rnd() % nt); if (a != b) {   /* a cut: one way, or both */
        uint32_t until = now + rndi(1000, 25000); cut_until[a][b] = until; if (rnd() & 1) cut_until[b][a] = until; } }
    if (rndf() < 0.0005f) { int a = (int)(rnd() % nt), b = (int)(rnd() % nt); if (a != b) drop_p[a][b] = rndf() * 0.45f; }
    if (rndf() < 0.00008f) { mtank_t *t = &T[rnd() % nt]; if (t->alive) {   /* a dead battery */
        t->alive = false; t->back_at = now + rndi(2000, 40000); memset(t->v, 0, sizeof t->v); st.deaths++; } }
    if (rndf() < 0.00008f) { mtank_t *t = &T[rnd() % nt]; if (t->alive && t->radio) {   /* the PWR key */
        link_sleep(t->L); run_events(t); t->radio = false; t->radio_at = now + rndi(3000, 30000); st.sleeps++; } }
    if (rndf() < 0.0002f) { mtank_t *t = &T[rnd() % nt]; if (t->alive && !t->moving) {   /* bumped, or carried off */
        t->moving = true; t->move_until = now + (rnd() & 1 ? rndi(100, 900) : rndi(1500, 60000)); st.drifts++; } }
    if (rndf() < 0.00008f) { mtank_t *t = &T[rnd() % nt]; if (t->alive) { link_close(t->L, (link_edge_t)(rnd() % 2)); run_events(t); st.closes++; } }
}
/* keepers join tanks: two arrows, held a few seconds apart; now and then a third at once (two asking: nothing joins) */
static void keepers(void) {
    if (rndf() > 0.002f) return;
    int a = (int)(rnd() % nt), b = (int)(rnd() % nt);
    if (a == b || !T[a].alive || !T[b].alive) return;
    link_edge_t ea = (link_edge_t)(rnd() % 2);
    if (link_neighbor(T[a].L, ea) || link_neighbor(T[b].L, ea == LINK_LEFT ? LINK_RIGHT : LINK_LEFT)) return;
    T[a].arm_at[ea] = now + 1; T[b].arm_at[ea == LINK_LEFT ? LINK_RIGHT : LINK_LEFT] = now + rndi(1, 3500);
    if (nt > 2 && rndf() < 0.15f) { int c = (int)(rnd() % nt); if (c != a && c != b) T[c].arm_at[ea == LINK_LEFT ? LINK_RIGHT : LINK_LEFT] = now + rndi(1, 3500); }
}

/* ---- the promises ---- */
static void check(void) {
    for (int i = 0; i < nt; i++) {
        if (!T[i].alive) continue;
        LCHECK(link_events_lost(T[i].L) == 0, "tank %d lost events on a full queue", i);
        for (int j = 0; j <= LINK_VISIT_MAX; j++) if (T[i].v[j].used) LCHECK(link_hosting(T[i].L, T[i].v[j].uid), "tank %d draws a visitor the engine let go", i);
        for (int k = 0; k < nt; k++) for (int f = 0; f < NF; f++) {
            uint32_t u = T[k].f[f].uid;
            if (link_hosting(T[i].L, u)) LCHECK(vis_find(&T[i], u), "tank %d's engine hosts a fish the tank does not draw", i);
        }
    }
    for (int k = 0; k < nt; k++) for (int f = 0; f < NF; f++) {
        mfish_t *m = &T[k].f[f];
        int sims = (T[k].alive && m->st == F_HOME) ? 1 : 0;
        for (int i = 0; i < nt; i++) if (i != k && T[i].alive && vis_find(&T[i], m->uid)) sims++;
        LCHECK(sims <= 1, "TWO COPIES: fish %08x of tank %d is simulated %d times", m->uid, k, sims);
        if (m->st == F_FROZEN && T[k].alive && !sims) {
            if (!m->limbo_since) m->limbo_since = now;
            int limbo = (int)(now - m->limbo_since);
            if (limbo > st.max_limbo) st.max_limbo = limbo;
            LCHECK(limbo <= LINK_RESTORE_NOVISIT_MS + 1500, "fish %08x of tank %d in limbo %d ms", m->uid, k, limbo);   /* (a host that never reported: 16 s) */
        } else m->limbo_since = 0;
    }
}

static int run(int n, uint32_t seed, uint32_t chaos_ms, bool quiet) {
    nt = n; rs = seed; now = 1000; qn = 0; chaos = true; pool_n = 0;
    memset(&st, 0, sizeof st); memset(drop_p, 0, sizeof drop_p); memset(cut_until, 0, sizeof cut_until);
    int f0 = fails;
    for (int i = 0; i < nt; i++) {
        mtank_t *t = &T[i];
        link_t *keep = t->L; memset(t, 0, sizeof *t); t->L = keep;
        t->uid = rnd() | 1;
        for (int k = 0; k < NF; k++) {
            mfish_t *f = &t->f[k]; f->uid = rnd() | 1; f->st = F_HOME;
            memset(&f->pp, 0, sizeof f->pp);
            f->pp.uid = f->uid; f->pp.home_tank = t->uid; snprintf(f->pp.name, sizeof f->pp.name, "t%df%d", i, k);
            f->pp.stage = STAGE_ADULT; f->pp.size = 1; f->pp.hunger = 4; f->pp.bold = 0.5f; f->pp.sociable = 0.5f;
        }
        boot(t, true);
    }
    for (int i = 0; i + 1 < nt; i++) { T[i].arm_at[LINK_RIGHT] = now + 10; T[i + 1].arm_at[LINK_LEFT] = now + 600; }   /* a row, to start */
    uint32_t end = now + chaos_ms;
    while ((int32_t)(now - end) < 0) {
        troubles(); keepers();
        deliver();
        for (int i = 0; i < nt; i++) tank_step(&T[i]);
        check();
        now += STEP_MS;
    }
    /* the end: the radio heals, everyone wakes and stays still, no fish heads out; within a minute all are home */
    chaos = false;
    for (int i = 0; i < nt; i++) { T[i].moving = false; for (int k = 0; k < NF; k++) T[i].f[k].offered = true; }
    for (uint32_t t_end = now + 60000; (int32_t)(now - t_end) < 0; now += STEP_MS) {
        deliver();
        for (int i = 0; i < nt; i++) { if (!T[i].alive) T[i].back_at = now; tank_step(&T[i]); }
        check();
    }
    int away = 0, visiting = 0;
    for (int i = 0; i < nt; i++) {
        visiting += vis_n(&T[i]);
        for (int k = 0; k < NF; k++) { away += T[i].f[k].st != F_HOME; LCHECK(!link_is_away(T[i].L, T[i].f[k].uid), "fish %08x still away in the engine", T[i].f[k].uid); }
        for (int k = 0; k < nt; k++) for (int f = 0; f < NF; f++) LCHECK(!link_hosting(T[i].L, T[k].f[f].uid), "tank %d still hosts a fish", i);
    }
    LCHECK(!away && !visiting, "after the radio healed: %d fish not home, %d visitors", away, visiting);
    if (!quiet || fails > f0)
        printf("selftest-link: %d tanks, seed %u, %u min: %d crossings, %d swam home, %d came back from bubbles (%d after a reboot), %d poofs, %d homeward; %d links made, %d closed; %d deaths, %d sleeps, %d drifts (%d notices, %d doors closed by drifting), %d doors closed; %d times two asked at once; %d of %d frames lost; longest limbo %.1f s\n",
               nt, seed, chaos_ms / 60000, st.cross, st.back, st.restore, st.boot_restore, st.poof, st.homeward, st.linked, st.unlinked,
               st.deaths, st.sleeps, st.drifts, st.drift_notice, st.drift_unlink, st.closes, st.ambiguous, st.dropped, st.frames, st.max_limbo / 1000.0);
    return st.cross;
}

static void fuzz(link_t *L) {
    static uint8_t b[LINK_FRAME_MAX + 8];
    link_host_t h = { &T[0], h_send, h_room, h_visitor, NULL, NULL, NULL, NULL };
    T[0].alive = false;                                   /* its sends go nowhere */
    link_init(L, 0x12345679u, &h, 7, 1000);
    link_mode(L, true);
    for (int k = 0; k < 200000; k++) {
        int len;
        if (pool_n && (k & 1)) {                          /* a real frame, damaged */
            int i = (int)(rnd() % (uint32_t)pool_n); len = pool_len[i]; memcpy(b, pool[i], (size_t)len);
            for (int m = (int)rndi(1, 4); m--; ) b[rnd() % (uint32_t)len] ^= (uint8_t)rnd();
            if (rnd() & 1) { b[8] = 0x79; b[9] = 0x56; b[10] = 0x34; b[11] = 0x12; }   /* addressed to it */
            if (rnd() % 4 == 0) len = (int)(rnd() % (LINK_FRAME_MAX + 8));
        } else { len = (int)(rnd() % (LINK_FRAME_MAX + 8)); for (int i = 0; i < len; i++) b[i] = (uint8_t)rnd(); if (len > 2) { b[0] = 0x4c; b[1] = 0x4b; } }
        link_rx(L, b, len, -50, 1000 + (uint32_t)k);
        if ((k & 63) == 0) link_tick(L, 1000 + (uint32_t)k);
        link_ev_t e; while (link_poll(L, &e)) {}
    }
}

/* ---- the scripted stories: a clean radio, exact timings ---- */
static void step_ms(uint32_t ms) {
    for (uint32_t end = now + ms; (int32_t)(now - end) < 0; now += STEP_MS) { deliver(); for (int i = 0; i < nt; i++) tank_step(&T[i]); check(); }
}
static void scene(int n) {
    nt = n; rs = 77; now = 1000; qn = 0; chaos = false; stay_ms = 600000; drop_type = 0; cross_ms = 50;
    memset(&st, 0, sizeof st); memset(evc, 0, sizeof evc); memset(evt, 0, sizeof evt);
    for (int i = 0; i < nt; i++) {
        mtank_t *t = &T[i]; link_t *keep = t->L; memset(t, 0, sizeof *t); t->L = keep; t->uid = 0x100u + (uint32_t)i;
        for (int k = 0; k < NF; k++) {
            mfish_t *f = &t->f[k]; f->uid = 0x1000u * (uint32_t)(i + 1) + (uint32_t)k; f->st = F_HOME;
            memset(&f->pp, 0, sizeof f->pp); f->pp.uid = f->uid; f->pp.home_tank = t->uid; snprintf(f->pp.name, sizeof f->pp.name, "s%d", k);
            f->pp.stage = STAGE_ADULT; f->pp.size = 1;
        }
        boot(t, true);
    }
}
static bool join(int a, link_edge_t ea, int b, uint32_t gap_ms) {      /* two holds, gap_ms apart */
    link_edge_t eb = ea == LINK_LEFT ? LINK_RIGHT : LINK_LEFT;
    T[a].arm_at[ea] = now + 10; T[b].arm_at[eb] = now + 10 + gap_ms;
    step_ms(gap_ms + 1500);
    return link_neighbor(T[a].L, ea) == T[b].uid && link_neighbor(T[b].L, eb) == T[a].uid;
}
static bool send_fish_over(int a, int k, link_edge_t e) {             /* a's fish k swims out at e (crossing at once) */
    mfish_t *f = &T[a].f[k];
    f->offered = true; link_offer(T[a].L, e, &f->pp, 0.4f, 0.1f, 40);
    step_ms(300);
    return f->st == F_FROZEN;
}
static bool hosted(int b, uint32_t uid) { return vis_find(&T[b], uid) && link_hosting(T[b].L, uid); }

static void scenarios(void) {
    /* 1. a visit and the swim home */
    scene(2);
    LCHECK(join(0, LINK_RIGHT, 1, 2000), "1: two holds 2 s apart did not join A's right to B's left");
    { int c = link_badge(T[0].L) == 0 ? 0 : 1, o = 1 - c, n0 = evc[o][LEV_NEARBY];   /* the circle's neighbor watches it leave link mode */
      LCHECK(link_badge(T[c].L) == 0, "1: no tank wears the circle");
      link_mode(T[c].L, false); step_ms(LINK_MODE_IDLE_MS / 4);
      LCHECK(evc[o][LEV_NEARBY] - n0 <= 1, "1: %d NEARBY events for one tank leaving link mode", evc[o][LEV_NEARBY] - n0); link_mode(T[c].L, true); }
    LCHECK(evc[0][LEV_LINKED] == 1 && evc[1][LEV_LINKED] == 1, "1: LINKED %d / %d", evc[0][LEV_LINKED], evc[1][LEV_LINKED]);
    uint32_t t0 = now;
    for (int k = 0; k < NF; k++) T[0].f[k].cross_at = 0;
    LCHECK(send_fish_over(0, 0, LINK_RIGHT), "1: the fish did not leave");
    LCHECK(hosted(1, T[0].f[0].uid) && evt[1][LEV_VISITOR_IN] - t0 < 300, "1: no visitor at B");
    step_ms(5000);
    mvis_t *v = vis_find(&T[1], T[0].f[0].uid);
    if (v) { v->pp.eaten = 3; v->leave_at = now; }
    step_ms(300);
    LCHECK(T[0].f[0].st == F_HOME && evc[0][LEV_FISH_BACK] == 1 && T[0].f[0].pp.eaten == 3, "1: the swim home: back %d, meals %d", evc[0][LEV_FISH_BACK], T[0].f[0].pp.eaten);
    printf("selftest-link: 1. joined by two holds 2 s apart; a fish crossed in %u ms, swam home with the 3 meals it ate next door\n", evt[1][LEV_VISITOR_IN] - t0);

    /* 1b. settling the tanks into place right after the door opens is not drifting */
    scene(2); LCHECK(join(0, LINK_RIGHT, 1, 500), "1b: no join");
    T[1].moving = true; T[1].move_until = now + 4000; step_ms(6000);
    LCHECK(!evc[0][LEV_DRIFT] && !evc[1][LEV_DRIFT], "1b: moving the tanks into place right after linking raised the DRIFTED notice");
    printf("selftest-link: 1b. 4 s of moving right after the door opened: settling, no notice\n");
    scene(2); join(0, LINK_RIGHT, 1, 2000);
    LCHECK(send_fish_over(0, 0, LINK_RIGHT), "1b: the fish did not leave"); step_ms(LINK_SETTLE_MS);   /* (scenario 2 from here, past the settling time) */

    /* 2. a nudge is nothing; a drift with the tanks set back still is forgiven */
    LCHECK(send_fish_over(0, 1, LINK_RIGHT), "2: the fish did not leave");
    { mvis_t *v0 = vis_find(&T[1], T[0].f[0].uid); if (v0) v0->leave_at = now + 600000; }
    T[1].moving = true; T[1].move_until = now + 600; step_ms(3000);
    LCHECK(!evc[0][LEV_DRIFT] && !evc[1][LEV_DRIFT], "2: a 0.6 s nudge raised the DRIFTED notice");
    T[1].moving = true; T[1].move_until = now + 8000; step_ms(4000);
    LCHECK(evc[1][LEV_DRIFT] == 1 && evc[0][LEV_DRIFT] == 1 && link_drifting(T[0].L) && !link_door_open(T[0].L, LINK_RIGHT), "2: drifting: notices %d / %d, doors still open", evc[0][LEV_DRIFT], evc[1][LEV_DRIFT]);
    step_ms(8000);
    LCHECK(evc[1][LEV_STEADY] == 1 && evc[0][LEV_STEADY] == 1 && link_door_open(T[0].L, LINK_RIGHT) && hosted(1, T[0].f[1].uid), "2: still again: the door did not reopen with the visitor still there");
    printf("selftest-link: 2. a 0.6 s nudge: nothing; 8 s of drifting: the notice on both tanks, doors paused, then still again: doors open, the visitor never left\n");

    /* 3. drifting past the grace: the visitor heads home, the door closes */
    T[1].moving = true; T[1].move_until = now + 120000; t0 = now;
    step_ms(LINK_NUDGE_MS + LINK_DRIFT_GRACE_MS + 500);
    LCHECK(evc[1][LEV_VISITOR_HOMEWARD] == 2, "3: %d HOMEWARD after the grace, want 2 (both visitors)", evc[1][LEV_VISITOR_HOMEWARD]);
    step_ms(LINK_HOMEWARD_MS + 1000);
    LCHECK(T[0].f[0].st == F_HOME && T[0].f[1].st == F_HOME && !link_neighbor(T[0].L, LINK_RIGHT) && !link_neighbor(T[1].L, LINK_LEFT), "3: after the homeward time the fish is %s and the door %s",
           T[0].f[1].st == F_HOME ? "home" : "away", link_neighbor(T[0].L, LINK_RIGHT) ? "open" : "closed");
    printf("selftest-link: 3. carried off for good: the visitors head home %.1f s in, %s, the door closes once they are home (%.0f s at most)\n",
           (evt[1][LEV_VISITOR_HOMEWARD] - t0) / 1000.0, evc[0][LEV_FISH_BACK] >= 2 ? "both swim back through it" : "they come back from bubbles", LINK_HOMEWARD_MS / 1000.0);
    T[1].moving = false;

    /* 3b. carried off with nobody visiting: the door closes as the grace ends - no empty homeward wait */
    scene(2); join(0, LINK_RIGHT, 1, 500); step_ms(LINK_SETTLE_MS);
    T[1].moving = true; T[1].move_until = now + 120000; t0 = now;
    step_ms(LINK_NUDGE_MS + LINK_DRIFT_GRACE_MS + 1000);
    LCHECK(!link_neighbor(T[0].L, LINK_RIGHT) && !link_neighbor(T[1].L, LINK_LEFT) && evc[0][LEV_STEADY] == 0,
           "3b: nobody visiting - the door was still open a second after the grace (or a STEADY came with the doors shut)");
    printf("selftest-link: 3b. carried off with nobody visiting: the door closes %.1f s in, as the grace ends\n", (evt[0][LEV_UNLINKED] - t0) / 1000.0);
    T[1].moving = false;

    /* 4. the radio cut while a fish is next door: a poof there, bubbles at home, never both */
    scene(2); join(0, LINK_RIGHT, 1, 500);
    send_fish_over(0, 0, LINK_RIGHT); step_ms(3000);
    chaos = true; cut_until[0][1] = cut_until[1][0] = now + 60000; t0 = now;
    step_ms(15000);
    LCHECK(evc[1][LEV_VISITOR_POOF] == 1 && evc[0][LEV_FISH_RESTORE] == 1 && T[0].f[0].st == F_HOME, "4: the cut: poof %d, restore %d", evc[1][LEV_VISITOR_POOF], evc[0][LEV_FISH_RESTORE]);
    printf("selftest-link: 4. the radio cut: the visitor poofs next door at %.1f s, comes home from bubbles at %.1f s - never two\n",
           (evt[1][LEV_VISITOR_POOF] - t0) / 1000.0, (evt[0][LEV_FISH_RESTORE] - t0) / 1000.0);
    chaos = false;

    /* 5. home goes to sleep with a fish next door: it comes home at once */
    scene(2); join(0, LINK_RIGHT, 1, 500);
    send_fish_over(0, 0, LINK_RIGHT); step_ms(2000); t0 = now;
    link_sleep(T[0].L); step_ms(1000);
    LCHECK(T[0].f[0].st == F_HOME && evc[1][LEV_VISITOR_POOF] == 1 && evc[0][LEV_FISH_RESTORE] == 1 && evt[0][LEV_FISH_RESTORE] - t0 < 500, "5: the sleep's recall");
    printf("selftest-link: 5. home sleeps with a fish next door: it is home from bubbles in %u ms\n", evt[0][LEV_FISH_RESTORE] - t0);

    /* 6. two tanks ask at once: nothing joins until one lets go */
    scene(3);
    T[0].arm_at[LINK_RIGHT] = now + 10; T[1].arm_at[LINK_LEFT] = now + 10; T[2].arm_at[LINK_LEFT] = now + 10;
    step_ms(3000);
    LCHECK(!link_neighbor(T[0].L, LINK_RIGHT) && !link_neighbor(T[1].L, LINK_LEFT) && !link_neighbor(T[2].L, LINK_LEFT), "6: two asking at once - something joined");
    bool two = false; for (int i = 0; i < 3; i++) two |= evc[i][LEV_ASKING] > 0;
    LCHECK(two, "6: no ASKING event");
    step_ms(LINK_ARM_MS);
    LCHECK(join(0, LINK_RIGHT, 2, 1000), "6: after the asks ran out, two holds did not join");
    printf("selftest-link: 6. two tanks asking at once: nothing joins; once they run out, two holds join as ever\n");

    /* 7. every GO lost for longer than the host holds the place: the host never had it, and says so - home before LINK_RESTORE_MS */
    scene(2); join(0, LINK_RIGHT, 1, 500);
    drop_type = 8 /* FR_GO */; drop_type_until = now + LINK_PENDING_MS + 1000; t0 = now;
    send_fish_over(0, 0, LINK_RIGHT);
    step_ms(LINK_PENDING_MS + 3000);
    LCHECK(T[0].f[0].st == F_HOME && evc[0][LEV_FISH_RESTORE] == 1 && evt[0][LEV_FISH_RESTORE] - t0 < LINK_RESTORE_MS && !evc[1][LEV_VISITOR_IN],
           "7: lost GOs: %d restores, the last %d ms in", evc[0][LEV_FISH_RESTORE], evc[0][LEV_FISH_RESTORE] ? (int)(evt[0][LEV_FISH_RESTORE] - t0) : -1);
    printf("selftest-link: 7. every GO lost past the held place: the host never had the fish; home in %.1f s\n", (evt[0][LEV_FISH_RESTORE] - t0) / 1000.0);
    /* 7b. GOs lost for 4 s: the host still holds the place - the fish arrives late, never twice */
    scene(2); join(0, LINK_RIGHT, 1, 500);
    drop_type = 8; drop_type_until = now + 4000; t0 = now;
    send_fish_over(0, 0, LINK_RIGHT);
    step_ms(6000);
    LCHECK(evc[1][LEV_VISITOR_IN] == 1 && hosted(1, T[0].f[0].uid) && T[0].f[0].st == F_FROZEN && !evc[0][LEV_FISH_RESTORE], "7b: a late GO did not land the fish next door");
    printf("selftest-link: 7b. GOs lost for 4 s: the fish arrives next door %.1f s late\n", (evt[1][LEV_VISITOR_IN] - t0) / 1000.0);
    /* 7c. a late GO (7 s) and then every VISIT lost: the host holds it on its first lease, home must not bring it back meanwhile */
    scene(2); join(0, LINK_RIGHT, 1, 500);
    drop_type = 8; drop_type_until = now + 7000; drop_type2 = 9 /* FR_VISIT */; drop_type2_until = now + 60000; t0 = now;
    send_fish_over(0, 0, LINK_RIGHT);
    step_ms(25000);                                           /* (check() fails on two copies at any step) */
    LCHECK(T[0].f[0].st == F_HOME && !hosted(1, T[0].f[0].uid), "7c: the fish did not end up home");
    printf("selftest-link: 7c. a late GO, every VISIT lost: never two - home from bubbles %.1f s after it left\n", (evt[0][LEV_FISH_RESTORE] - t0) / 1000.0);
    drop_type2 = 0;
    drop_type = 0; stay_ms = 0; cross_ms = -1;

    /* 8. a third tank opens link mode beside two linked tanks out of link mode: three badges, all different */
    scene(3);
    LCHECK(join(0, LINK_RIGHT, 1, 500), "8: the first pair did not join");
    link_mode(T[0].L, false); link_mode(T[1].L, false); step_ms(3000);
    { link_host_t h = { &T[2], h_send, h_room, h_visitor, NULL, NULL, NULL, NULL };            /* as on the glass: its radio was off, it has heard nobody, */
      link_init(T[2].L, T[2].uid, &h, 9, now); link_mode(T[2].L, true); }   /* so it picks before the first beacon arrives */
    step_ms(3000);
    int b0 = link_badge(T[0].L), b1 = link_badge(T[1].L), b2 = link_badge(T[2].L);
    LCHECK(b0 != b1 && b0 != b2 && b1 != b2, "8: badges %d %d %d - not all different", b0, b1, b2);
    printf("selftest-link: 8. a third tank opening link mode beside a linked pair takes a badge of its own (%d %d %d)\n", b0, b1, b2);
    /* 9. the middle of three (the 11-02 sweep): the right neighbor drifting pauses only the door it is at - the left one
       still takes a fish - and reopens when it is still again */
    scene(3);
    LCHECK(join(0, LINK_RIGHT, 1, 500) && join(1, LINK_RIGHT, 2, 500), "9: the row of three did not join");
    step_ms(10000);                                   /* (past the settle after a join: a moved tank then counts) */
    T[2].moving = true; T[2].move_until = now + 8000; step_ms(4000);
    LCHECK(link_drifting(T[1].L) && !link_door_open(T[1].L, LINK_RIGHT) && link_door_open(T[1].L, LINK_LEFT),
           "9: the middle with its right neighbor drifting: right door open %d, left door open %d (want 0, 1)", link_door_open(T[1].L, LINK_RIGHT), link_door_open(T[1].L, LINK_LEFT));
    LCHECK(send_fish_over(1, 0, LINK_LEFT) && hosted(0, T[1].f[0].uid), "9: the left door took no fish while the right neighbor drifted");
    step_ms(8000);
    LCHECK(link_door_open(T[1].L, LINK_RIGHT) && link_door_open(T[1].L, LINK_LEFT), "9: still again, the right door did not reopen");
    printf("selftest-link: 9. the middle of three, its right neighbor drifting: only that door paused, the left one took a fish; both open once still\n");
    drop_type = 0; stay_ms = 0; cross_ms = -1;      /* (the chaos runs draw their own) */
}

int selftest_link(void) {
    fails = 0;
    link_t *L[NT_MAX + 1];
    for (int i = 0; i <= NT_MAX; i++) { L[i] = calloc(1, (size_t)link_size()); if (i < NT_MAX) T[i].L = L[i]; }
    printf("selftest-link: one engine is %d bytes\n", link_size());
    scenarios();
    int crossings = 0;
    static const struct { int n; uint32_t seed, ms; } RUNS[] = {
        { 2, 11, 20 * 60000 }, { 2, 12, 20 * 60000 }, { 3, 21, 20 * 60000 }, { 3, 22, 20 * 60000 }, { 4, 31, 20 * 60000 }, { 4, 32, 20 * 60000 },
    };
    for (size_t r = 0; r < sizeof RUNS / sizeof RUNS[0]; r++) crossings += run(RUNS[r].n, RUNS[r].seed, RUNS[r].ms, false);
    if (getenv("LINK_DELAY_MS")) max_delay_ms = (uint32_t)atoi(getenv("LINK_DELAY_MS"));
    int quiet_runs = getenv("LINK_SOAK") ? atoi(getenv("LINK_SOAK")) : 40;   /* LINK_SOAK=2000: a long soak */
    for (int r = 0; r < quiet_runs; r++) crossings += run(2 + r % 3, 1000 + (uint32_t)r * 7919u, 5 * 60000, true);
    printf("selftest-link: + %d more runs of 5 min; %d crossings in all, every one checked after every 10 ms step\n", quiet_runs, crossings);
    LCHECK(crossings > 1000, "only %d crossings: the test is not exercising the doors", crossings);
    fuzz(L[NT_MAX]);
    printf("selftest-link: 200000 frames of noise and damaged real frames: no crash\n");
    for (int i = 0; i <= NT_MAX; i++) free(L[i]);
    printf("selftest-link: %s\n", fails ? "FAILED" : "OK");
    return fails ? 1 : 0;
}

/* ---- the doors (phase 4): two real tanks, real fish, the real engines ----
 * Not the mock tanks above: tank_t with its physics, tank_link.c between the
 * tank and the engine, a radio that delivers next step. No advisor (the
 * fish's goal is set by hand) and no progression (one per process). */
#include <math.h>
#include "tank_link.h"
#include "progression.h"
#include "collection.h"
#include "gift.h"
#include "shelf.h"
#include "notice.h"
#include "sounds.h"
#include "render.h"
typedef struct { int to, len; uint8_t b[LINK_FRAME_MAX]; } dframe_t;
static dframe_t dq[256]; static int dqn;
static tank_t DA, DB; static link_t *LA, *LB;
static uint32_t dnow;
static int d_drop_type, d_drop_to; static uint32_t d_drop_until;   /* (a test: frames of this type to tank d_drop_to (0 A, 1 B) lost until then) */
static void d_send(void *ctx, uint32_t to, const uint8_t *buf, int len) {
    int from = ctx == &DA ? 0 : 1;
    if (d_drop_type && len > 3 && buf[3] == d_drop_type && 1 - from == d_drop_to && (int32_t)(d_drop_until - dnow) > 0) return;
    if (dqn < 256 && (!to || to == (from ? DA.uid : DB.uid))) { dq[dqn].to = 1 - from; dq[dqn].len = len; memcpy(dq[dqn].b, buf, (size_t)len); dqn++; }
}
static bool d_deaf[2];                         /* 0 = A, 1 = B: that tank hears nothing (the other went quiet) */
static int d_fails;
#define DCHECK(cond, ...) do { if (!(cond)) { if (d_fails < 10) { printf("FAIL t=%.1f s: ", dnow / 1000.0); printf(__VA_ARGS__); printf("\n"); } d_fails++; } } while (0)
static int d_vis_of(const tank_t *t, uint32_t uid) { for (int k = 0; k < N_VISIT_MAX; k++) if (t->vis_used[k] && t->fish[N_FISH_MAX + k].uid == uid) return N_FISH_MAX + k; return -1; }
static void d_step(int frames) {
    for (int n = 0; n < frames; n++) {
        dnow += 33;
        int m = dqn; dframe_t q[256]; memcpy(q, dq, sizeof(dframe_t) * (size_t)m); dqn = 0;
        for (int i = 0; i < m; i++) if (!d_deaf[q[i].to]) link_rx(q[i].to ? LB : LA, q[i].b, q[i].len, -30, dnow);
        tank_t *T2[2] = { &DA, &DB }; link_t *L2[2] = { LA, LB };
        for (int k = 0; k < 2; k++) {
            tank_tick(T2[k], 0.033f, NULL);
            link_tick(L2[k], dnow);
            tank_link_tick(T2[k], L2[k]);
            link_ev_t e; while (link_poll(L2[k], &e)) { if (getenv("EV_TRACE")) printf("  ev t %.2f %c type %d uid %08x reason %d\n", dnow / 1000.0, 'A' + k, e.type, e.uid, e.reason); tank_link_event(T2[k], L2[k], &e); }
        }
        { static uint32_t wu_; static int wn_; if (!wu_ && getenv("WATCH_UID")) wu_ = (uint32_t)strtoul(getenv("WATCH_UID"), NULL, 16);
          if (wu_ && ++wn_ % 10 == 0) for (int k = 0; k < 2; k++) { tank_t *tt = k ? &DB : &DA; for (int i = 0; i < N_FISH_SLOTS; i++) if (tt->fish[i].uid == wu_ && (i < tt->n_fish || (i >= N_FISH_MAX && tt->vis_used[i - N_FISH_MAX])))
              printf("  watch t %.2f %c slot %d door %d sent %d away %d x %.0f\n", dnow / 1000.0, 'A' + k, i, tt->fish[i].door, tt->fish[i].door_sent, tt->fish[i].away, tt->fish[i].x); } }
        /* never two: a fish of A is simulated by A or as B's visitor, never both */
        for (int i = 0; i < DA.n_fish; i++) DCHECK(!(tank_swims(&DA, i) && d_vis_of(&DB, DA.fish[i].uid) >= 0), "A's fish %d is in both tanks", i);
        for (int i = 0; i < DB.n_fish; i++) DCHECK(!(tank_swims(&DB, i) && d_vis_of(&DA, DB.fish[i].uid) >= 0), "B's fish %d is in both tanks", i);
    }
}
static void d_head_for(tank_t *t, int i, int edge) {
    fish_t *f = &t->fish[i];
    f->goal.id = GOAL_EXPLORE; f->door = DOOR_HEADING; f->door_edge = (int8_t)edge; f->door_refused = 0;
}
/* as on the boards: hovering at the door, FACING IN, when the neighbor says yes */
static void d_at_door_facing_in(tank_t *t, int i, int edge) {
    fish_t *f = &t->fish[i];
    float gx = edge == LINK_LEFT ? tank_glass_x0(DOOR_Y) : tank_glass_x1(DOOR_Y);
    f->x = gx + (edge == LINK_LEFT ? 26 : -26); f->y = DOOR_Y;
    f->heading = edge == LINK_LEFT ? 0 : 3.14159265f; tank_fish_face(f); f->speed = f->target_speed = 4;
    d_head_for(t, i, edge);
}
/* wait until that fish is home and clear of the doors, then keep it from a visit of its own a while (a gift test's start) */
/* THE CARDS BAKED (0.4): the GIVE modal (gift = false) or the gift card over the tank, drawn whole, then from its bake -
   painted, then copied again with nothing changed: the same frame pixel for pixel. The pixels that differ */
static int d_bake_diff(tank_t *t, bool gift) {
    static uint16_t bk_give[GIFT_CARD_W * GIFT_CARD_H], bk_gift[GIFT_CARD_W * GIFT_CARD_H], whole[TANK_W * TANK_H], baked[TANK_W * TANK_H];
    int n = 0;
    for (int pass = 0; pass < 3; pass++) {              /* 0: whole; 1: the bake painted; 2: the bake copied */
        if (pass < 2) render_set_modal_cache(pass ? bk_give : NULL, pass ? bk_gift : NULL);
        uint16_t *o = pass ? baked : whole;
        render_tank(t, o, TANK_W); render_tank(t, o, TANK_W);
        if (gift) render_gift(t, o, TANK_W); else render_give_modal(t, o, TANK_W);
        if (pass) for (int q = 0; q < TANK_W * TANK_H; q++) n += baked[q] != whole[q];
    }
    render_set_modal_cache(NULL, NULL);
    return n;
}
/* THE CARDS LENT (0.4.2): the device takes a card's bake when the card is drawn and gives it back ~3 s after. With an
   allocator set: drawn from a lent bake = drawn whole, pixel for pixel (painted, then copied); given back once the card is
   not drawn for BAKE_IDLE_FRAMES; taken again and still the same; with no memory, drawn whole and asked again only after
   BAKE_RETRY_FRAMES. Returns the failures (each printed) */
static int dl_allocs, dl_frees, dl_fail;
static void *dl_alloc(size_t n) { dl_allocs++; return dl_fail ? NULL : malloc(n); }
static void dl_free(void *p) { dl_frees++; free(p); }
static int d_bake_lent(tank_t *t) {
    static uint16_t whole[TANK_W * TANK_H], lent[TANK_W * TANK_H];
    int bad = 0, held;
    #define DL_DRAW(o) do { render_frame_begin(); render_tank(t, o, TANK_W); render_tank(t, o, TANK_W); render_give_modal(t, o, TANK_W); } while (0)
    #define DL_SAME(what) do { int nd = 0; for (int q = 0; q < TANK_W * TANK_H; q++) nd += lent[q] != whole[q]; \
                               if (nd) { printf("FAIL: doors: 7k lent: %s - %d pixels differ from the card drawn whole\n", what, nd); bad++; } } while (0)
    render_set_modal_cache(NULL, NULL); DL_DRAW(whole);                       /* the card drawn whole */
    dl_allocs = dl_frees = dl_fail = 0; render_set_modal_alloc(dl_alloc, dl_free);
    DL_DRAW(lent); DL_SAME("painted into a lent bake");
    DL_DRAW(lent); DL_SAME("copied from the lent bake");
    if (dl_allocs != 1 || !render_modal_bakes_held(&held) || held != 1) { printf("FAIL: doors: 7k lent: %d takes, %d held after two frames (want 1, 1)\n", dl_allocs, held); bad++; }
    for (int q = 0; q < 70; q++) render_frame_begin();                         /* closed under 3 s: still held */
    if (dl_frees) { printf("FAIL: doors: 7k lent: given back after 70 frames (want held to 75)\n"); bad++; }
    for (int q = 0; q < 10; q++) render_frame_begin();
    if (dl_frees != 1 || render_modal_bakes_held(NULL)) { printf("FAIL: doors: 7k lent: %d given back after 80 frames unseen (want 1)\n", dl_frees); bad++; }
    DL_DRAW(lent); DL_SAME("taken again");
    if (dl_allocs != 2) { printf("FAIL: doors: 7k lent: %d takes after it came back (want 2)\n", dl_allocs); bad++; }
    for (int q = 0; q < 80; q++) render_frame_begin();
    dl_fail = 1; dl_allocs = 0;
    for (int f = 0; f < 10; f++) { DL_DRAW(lent); DL_SAME("no memory: drawn whole"); }
    if (dl_allocs != 1) { printf("FAIL: doors: 7k lent: no memory asked %d times in 10 frames (want 1, then a wait)\n", dl_allocs); bad++; }
    for (int f = 0; f < 20; f++) DL_DRAW(lent);
    if (dl_allocs != 2) { printf("FAIL: doors: 7k lent: no memory asked %d times in 30 frames (want 2)\n", dl_allocs); bad++; }
    dl_fail = 0; for (int f = 0; f < 30; f++) DL_DRAW(lent);
    DL_SAME("memory back: the bake again"); 
    if (!render_modal_bakes_held(NULL)) { printf("FAIL: doors: 7k lent: memory back but no bake taken\n"); bad++; }
    for (int q = 0; q < 80; q++) render_frame_begin();
    render_set_modal_alloc(NULL, NULL); render_set_modal_cache(NULL, NULL);
    if (dl_frees != 3) { printf("FAIL: doors: 7k lent: %d given back in all (want 3, one a take: nothing leaks)\n", dl_frees); bad++; }
    #undef DL_DRAW
    #undef DL_SAME
    return bad;
}
/* GIVE_SHOT=<path prefix>: a card the doors cases draw, saved as <prefix>_<name>.ppm (for looking at the words) */
static void d_shot(const uint16_t *fb, const char *name) {
    const char *shot = getenv("GIVE_SHOT");
    if (!shot) return;
    char pth[512]; snprintf(pth, sizeof pth, "%s_%s.ppm", shot, name);
    FILE *pf = fopen(pth, "wb");
    if (!pf) return;
    fprintf(pf, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int q = 0; q < TANK_W * TANK_H; q++) { uint16_t v = fb[q];
        unsigned char rgb[3] = { (unsigned char)((v >> 11) << 3), (unsigned char)(((v >> 5) & 63) << 2), (unsigned char)((v & 31) << 3) }; fwrite(rgb, 1, 3, pf); }
    fclose(pf);
}
static int d_wait_home(tank_t *t, uint32_t uid) {
    for (int q = 0; q < 5400; q++) {
        for (int i = 0; i < t->n_fish; i++) if (t->fish[i].uid == uid && !t->fish[i].away && t->fish[i].door == DOOR_NONE) { t->fish[i].door_refused = t->clock + 60; return i; }
        tank_t *o = t == &DA ? &DB : &DA; int v = d_vis_of(o, uid);
        if (v >= 0) o->fish[v].homeward = o->fish[v].bolt_home = true;   /* (visiting: called home, briskly - the watch's floor is long) */
        d_step(1);
    }
    return -1;
}
/* 8. a fish that leaves (sold or given) lets go of every visitor's time together with it (0.4.1 audit round 1, R-link F1 =
   R-progression F5; the link reviewer's L1a/L1b). L1a: a visitor 90 s beside A2, A2 sold - its time together starts over
   with a fish still here. L1b: the pair formed (B1 and A2), A2 sold - the pair is let go (fry_mate never the sold fish),
   and B1's home stops holding a place for a fry that cannot come */
static void dm_hold_doors(tank_t *t, double s) { for (int i = 0; i < t->n_fish; i++) t->fish[i].door_seen = t->clock + s; }
static int dm_visit(tank_t *home, int i, int edge, tank_t *host) {
    uint32_t u = home->fish[i].uid; d_head_for(home, i, edge);
    for (int n = 0; n < 1800 * ((TANK_W + 447) / 448); n++) { d_step(1); if (d_vis_of(host, u) >= 0) return n; }
    return -1;
}
static void dm_pin(tank_t *host, uint32_t vis, int beside) {
    int v = d_vis_of(host, vis); if (v < 0) return;
    fish_t *g = &host->fish[v], *m = &host->fish[beside];
    for (int i = 0; i < host->n_fish; i++) if (i != beside) { host->fish[i].x = m->x < TANK_W / 2 ? TANK_W - 40 : 40; host->fish[i].y = TANK_BOT - 80; }
    g->x = m->x + 30; g->y = m->y; g->homeward = false; host->vis_since[v - N_FISH_MAX] = host->clock;
}
static int dm_holds(const tank_t *t) { int n = 0; for (int h = 0; h < TANK_FRY_HOLDS; h++) n += t->fry_hold[h].peer != 0; return n; }
static void doors_mate_left(void) {
    dnow = 1000; dqn = 0; d_deaf[0] = d_deaf[1] = false;
    tank_init(&DA, 61); progression_fresh(&DA); tank_ensure_ids(&DA);
    tank_init(&DB, 62); tank_new_population(&DB); tank_ensure_ids(&DB);
    while (DA.n_fish < 3) tank_add_fish(&DA, 0, 1);
    while (progression_newborn() >= 0) progression_newborn_done(&DA);
    while (DB.n_fish < 4) { tank_make_fish(&DB, DB.n_fish, DB.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); DB.n_fish++; } tank_ensure_ids(&DB);
    for (int i = 0; i < DA.n_fish; i++) { progression_set_age(&DA, i, STAGE_ADULT_AGE + 60); DA.fish[i].stage = STAGE_ADULT; }
    for (int i = 0; i < DB.n_fish; i++) DB.fish[i].stage = STAGE_ADULT;
    LA = calloc(1, (size_t)link_size()); LB = calloc(1, (size_t)link_size());
    link_host_t ha = tank_link_host(&DA, d_send), hb = tank_link_host(&DB, d_send);
    link_init(LA, DA.uid, &ha, 1, dnow); link_init(LB, DB.uid, &hb, 2, dnow);
    link_mode(LA, true); link_mode(LB, true); d_step(10);
    link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400);
    DCHECK(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT], "8: the doors did not open");
    dm_hold_doors(&DA, 1e6); dm_hold_doors(&DB, 1e6);
    /* L1a */
    { uint32_t par = DB.fish[0].uid;
      DCHECK(dm_visit(&DB, 0, LINK_LEFT, &DA) >= 0, "8 L1a: B0 never visited");
      for (int n = 0; n < 90; n++) { dm_pin(&DA, par, 2); d_step(1); progression_tick(&DA, 1.0f); }
      uint32_t mate = 0, sold = DA.fish[2].uid; float b0 = progression_bond(&DA, par, &mate);
      DCHECK(mate == sold && b0 > 0, "8 L1a: staging - no time together beside A2 (bond %.2f)", b0);
      DCHECK(progression_sell_fish(&DA, 2), "8 L1a: the sale failed");
      (void)progression_bond(&DA, par, &mate);
      DCHECK(mate != sold, "8 L1a: after A2 was sold the visitor's time together is still with it");
      for (int n = 0; n < 300; n++) { dm_pin(&DA, par, 0); d_step(1); progression_tick(&DA, 1.0f); }
      float b1 = progression_bond(&DA, par, &mate);
      DCHECK(mate && mate != sold && b1 > 0, "8 L1a: 5 min beside A0 after the sale: bond %.2f, mate %s", b1, mate == sold ? "the sold fish" : mate ? "another" : "none");
      printf("selftest-doors: 8 L1a. a visitor's time together with a fish sold starts over with one still here (bond %.2f -> %.2f)\n", b0, b1); }
    /* L1b */
    { while (DA.n_fish < 3) tank_add_fish(&DA, 0, 1);
      while (progression_newborn() >= 0) progression_newborn_done(&DA);
      for (int i = 0; i < DA.n_fish; i++) { progression_set_age(&DA, i, STAGE_ADULT_AGE + 60); DA.fish[i].stage = STAGE_ADULT; }
      dm_hold_doors(&DA, 1e6);
      for (int q = 0; q < 2400; q++) {                      /* every visitor home */
          int left = 0;
          for (int k = 0; k < N_VISIT_MAX; k++) {
              if (DA.vis_used[k]) { DA.fish[N_FISH_MAX + k].homeward = DA.fish[N_FISH_MAX + k].bolt_home = true; left++; }
              if (DB.vis_used[k]) { DB.fish[N_FISH_MAX + k].homeward = DB.fish[N_FISH_MAX + k].bolt_home = true; left++; } }
          for (int i = 0; i < DA.n_fish; i++) left += DA.fish[i].away;
          for (int i = 0; i < DB.n_fish; i++) left += DB.fish[i].away;
          if (!left) break;
          d_step(1);
      }
      uint32_t p2 = DB.fish[1].uid;
      DCHECK(dm_visit(&DB, 1, LINK_LEFT, &DA) >= 0, "8 L1b: B1 never visited");
      int t_set = -1;
      for (int n = 0; n < 900 && t_set < 0; n++) { dm_pin(&DA, p2, 2); d_step(1); progression_tick(&DA, 1.0f); if (DA.fry_partner == p2) t_set = n; }
      uint32_t m2 = DA.fish[2].uid;
      if (t_set < 0 || DA.fry_mate != m2) printf("selftest-doors: 8 L1b. (the pair did not form in 900 s here - nothing to let go)\n");
      else {
          DCHECK(progression_sell_fish(&DA, 2), "8 L1b: the sale failed");
          DCHECK(DA.fry_mate != m2 && DA.fry_partner != p2, "8 L1b: after A2 was sold the pair stands (partner B1 %d, fry_mate the sold fish %d)", DA.fry_partner == p2, DA.fry_mate == m2);
          for (int n = 0; n < 120; n++) { dm_pin(&DA, p2, 0); d_step(1); progression_tick(&DA, 1.0f); }
          DCHECK(DA.fry_mate != m2, "8 L1b: 2 min later fry_mate is the sold fish again");
          DCHECK(dm_holds(&DB) == 0, "8 L1b: B1's home still holds %d place(s) for a fry that cannot come (fry room %d)", dm_holds(&DB), progression_fry_room(&DB));
          printf("selftest-doors: 8 L1b. the pair formed after %d s; its fish sold - the pair let go, and B1's home holds no place for it\n", t_set);
      }
    }
    free(LA); free(LB); LA = LB = NULL;
}
/* 9. a GIVE brings the fish to life as it LEFT, not as it was when the keeper asked (0.4.1 audit round 1,
   R-seam-link-progression F3, its told case S3): A asks to give a juvenile; while B's keeper reads the card it grows up
   in A. B must get an adult that has REACHED ADULT (its stage paid once, in A) - and a gift restored from bubbles comes
   back from its GIVEN note's own passport, never the engine's older copy */
static int dg_home(const tank_t *t, uint32_t u) { for (int i = 0; i < t->n_fish; i++) if (t->fish[i].uid == u) return i; return -1; }
static void doors_give_fresh(void) {
    dnow = 1000; dqn = 0; d_deaf[0] = d_deaf[1] = false;
    static gift_book_t ga, gb; memset(&ga, 0, sizeof ga); memset(&gb, 0, sizeof gb);
    tank_init(&DA, 71); progression_fresh(&DA); tank_ensure_ids(&DA);
    tank_init(&DB, 72); tank_new_population(&DB); tank_ensure_ids(&DB);
    gift_attach(&DA, &ga); gift_attach(&DB, &gb);
    while (DA.n_fish < 3) tank_add_fish(&DA, 0, 1);
    while (progression_newborn() >= 0) progression_newborn_done(&DA);
    for (int i = 0; i < 2; i++) { progression_set_age(&DA, i, STAGE_ADULT_AGE + 60); DA.fish[i].stage = STAGE_ADULT; DB.fish[i].stage = STAGE_ADULT; }
    progression_set_age(&DA, 2, STAGE_JUV_AGE + 60); DA.fish[2].stage = STAGE_JUV;
    DA.fish[2].ms_bits |= MS_REACHED_JUV; DA.fish[2].ms_bits &= ~MS_REACHED_ADULT;
    LA = calloc(1, (size_t)link_size()); LB = calloc(1, (size_t)link_size());
    link_host_t ha = tank_link_host(&DA, d_send), hb = tank_link_host(&DB, d_send);
    link_init(LA, DA.uid, &ha, 1, dnow); link_init(LB, DB.uid, &hb, 2, dnow);
    link_mode(LA, true); link_mode(LB, true); d_step(10);
    link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400);
    DCHECK(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT], "9: the doors did not open");
    for (int i = 0; i < DA.n_fish; i++) DA.fish[i].door_seen = DA.clock + 1e6;
    uint32_t gu = DA.fish[2].uid;
    ga.want_uid = gu; d_step(10);
    DCHECK(gb.ask.on && gb.ask.uid == gu, "9: the question never reached B");
    {   int i = dg_home(&DA, gu);                                /* B's keeper reads the card: the fish grows up in A meanwhile */
        progression_set_age(&DA, i, STAGE_ADULT_AGE + 60); progression_tick(&DA, 0.1f);
        DCHECK(DA.fish[i].stage == STAGE_ADULT && (DA.fish[i].ms_bits & MS_REACHED_ADULT), "9: staging - it did not grow up in A"); }
    gb.ask.answer = 1;
    int n = 0; for (; n < 1800 && dg_home(&DB, gu) < 0; n++) d_step(1);
    int j = dg_home(&DB, gu);
    DCHECK(j >= 0, "9: the gift never reached B");
    if (j >= 0) {
        DCHECK(DB.fish[j].stage == STAGE_ADULT && (DB.fish[j].ms_bits & MS_REACHED_ADULT), "9: it left A an adult and arrived in B a %s (REACHED ADULT %d) - the ask's passport",
               DB.fish[j].stage == STAGE_JUV ? "juvenile" : "fish of another stage", !!(DB.fish[j].ms_bits & MS_REACHED_ADULT));
        DCHECK(DB.sd_paid_fish[j] & SD_PAID_ADULT, "9: B will pay its adult stage again (paid %x)", DB.sd_paid_fish[j]);
        if (DB.fish[j].stage == STAGE_ADULT) printf("selftest-doors: 9. GIVE: it grew up while B's keeper read the card - it arrived in B an adult, its stage paid once\n");
    }
    for (n = 0; n < 600 && (ga.n_given || gb.n_arr); n++) d_step(1);
    free(LA); free(LB); LA = LB = NULL;
    /* the restore: a GIVEN note (the fish as it left: an adult) and GIVEN_LOST carrying an older copy (a juvenile) */
    {   int i = 0; passport_t left, asked;
        passport_from_fish(&left, &DA.fish[i], DA.uid, STAGE_ADULT_AGE + 60); asked = left;
        asked.stage = STAGE_JUV; asked.ms_bits &= ~MS_REACHED_ADULT; asked.age_s = STAGE_JUV_AGE + 60;
        uint32_t u = DA.fish[i].uid;
        DCHECK(gift_given_add(&ga, u, 0x77u, DB.uid, &left), "9: staging the GIVEN note");
        progression_give_out(&DA, i);
        link_ev_t ev; memset(&ev, 0, sizeof ev); ev.type = LEV_GIVE_RESTORE; ev.uid = u; ev.peer = DB.uid; ev.nonce = 0x77u; ev.pp = asked;
        tank_link_event(&DA, NULL, &ev);
        int k = dg_home(&DA, u);
        DCHECK(k >= 0 && DA.fish[k].stage == STAGE_ADULT && (DA.fish[k].ms_bits & MS_REACHED_ADULT),
               "9: restored from bubbles a %s, not as its GIVEN note has it", k < 0 ? "nothing" : DA.fish[k].stage == STAGE_JUV ? "juvenile" : "fish of another stage");
        if (k >= 0 && DA.fish[k].stage == STAGE_ADULT) printf("selftest-doors: 9. a gift restored from bubbles comes back from its GIVEN note's own passport\n");
    }
    gift_attach(&DA, NULL); gift_attach(&DB, NULL);
}
int selftest_doors(void) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-doors.sav", 1);   /* (the births and welcomes save: never into the sim's own tank) */
    d_fails = 0; dnow = 1000; dqn = 0;
    tank_init(&DA, 31); progression_fresh(&DA); tank_ensure_ids(&DA);   /* (progression booted for A: scenario 6 runs its tick) */
    tank_init(&DB, 32); tank_new_population(&DB); tank_ensure_ids(&DB);
    for (int i = 0; i < 2; i++) { progression_set_age(&DA, i, STAGE_ADULT_AGE + 60); DA.fish[i].stage = STAGE_ADULT; DB.fish[i].stage = STAGE_ADULT; }
    /* the room: two visitor places whatever our own count, 7 swimming at most (Strato 2026-11-05; was 10-05's "5 fish a tank,
       visitors counted": two full tanks linked and nobody ever visited) */
    { static tank_t R; memset(&R, 0, sizeof R); tank_init(&R, 33); tank_new_population(&R); int n0 = R.n_fish;
      for (int n = 2; n <= N_FISH_MAX; n++) {
          R.n_fish = n;
          int want = n <= POP_CAP ? TANK_LINK_VISIT_PLACES : TANK_LINK_SWIM_MAX - n;   /* (past the cap: an older build's save, cap_over) */
          DCHECK(tank_link_room(&R) == want, "room with %d fish at home: %d, want %d", n, tank_link_room(&R), want);
      }
      R.n_fish = 4; R.fish[0].away = R.fish[1].away = true;           /* fish away make no room: their places wait for them */
      DCHECK(tank_link_room(&R) == 2, "two of four away: room %d, want 2", tank_link_room(&R));
      R.fish[0].away = R.fish[1].away = false; R.n_fish = POP_CAP;
      R.vis_used[0] = true; DCHECK(tank_link_room(&R) == 1, "five fish + a visitor: room %d, want 1", tank_link_room(&R));
      R.vis_pending[1] = true; DCHECK(tank_link_room(&R) == 0, "five + a visitor + a fry waiting for its home: room %d, want 0", tank_link_room(&R));
      DCHECK(!tank_link_fry_place(&R), "a fry place with both places taken");
      R.vis_pending[1] = false; R.vis_coming = 1;
      DCHECK(tank_link_room(&R) == 1 && !tank_link_fry_place(&R), "a visitor let in, not drawn yet: the fry took its place (room %d)", tank_link_room(&R));
      R.vis_used[0] = false; R.vis_coming = 0; R.n_fish = n0;
      printf("selftest-doors: room = 2 visitor places whatever the tank holds (2..5 fish: 2), 7 swimming at most; fish away make no room; a fry for its home takes a place\n"); }
    LA = calloc(1, (size_t)link_size()); LB = calloc(1, (size_t)link_size());
    (void)system("rm -rf /tmp/pt-doors-shelf && mkdir -p /tmp/pt-doors-shelf/frame");   /* (the keeper's name for A, on its passports) */
    setenv("POCKET_TANK_STORAGE", "/tmp/pt-doors-shelf/storage.bin", 1); setenv("POCKET_TANK_FRAME", "/tmp/pt-doors-shelf/frame", 1);
    shelf_boot(0); shelf_rename(-1, "SAM'S\nREEF");
    link_host_t ha = tank_link_host(&DA, d_send), hb = tank_link_host(&DB, d_send);
    link_init(LA, DA.uid, &ha, 1, dnow); link_init(LB, DB.uid, &hb, 2, dnow);
    link_mode(LA, true); link_mode(LB, true); d_step(10);
    link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30);
    DCHECK(link_neighbor(LA, LINK_RIGHT) == DB.uid && DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT], "the doors did not open");
    d_step(400);                                                            /* past the settling time; the fish swim */

    /* 1. A's fish 0 heads out of the right door */
    uint32_t uid = DA.fish[0].uid; float y_out = 0;
    d_head_for(&DA, 0, LINK_RIGHT);
    int f_away = -1, f_in = -1;
    for (int n = 0; n < 1800 && f_in < 0; n++) {                           /* up to a minute */
        d_step(1);
        if (getenv("DOORS_TRACE") && n % 15 == 0) printf("  t %4.1f door %d x %5.1f y %5.1f speed %4.1f goal %d away %d\n", n / 30.0, DA.fish[0].door, DA.fish[0].x, DA.fish[0].y, DA.fish[0].speed, DA.fish[0].goal.id, DA.fish[0].away);
        if (f_away < 0 && DA.fish[0].away) { f_away = n; y_out = DA.fish[0].y; }
        if (d_vis_of(&DB, uid) >= 0) f_in = n;
    }
    DCHECK(f_away >= 0 && f_in >= 0, "fish 0 never crossed (door %d, away %d, x %.0f y %.0f)", DA.fish[0].door, DA.fish[0].away, DA.fish[0].x, DA.fish[0].y);
    int v = d_vis_of(&DB, uid);
    if (v >= 0) {
        fish_t *g = &DB.fish[v];
        DCHECK(g->x < tank_glass_x0(DOOR_Y) + 30 && cosf(g->heading) > 0.5f, "the visitor did not come in at B's left door, swimming in (x %.0f heading %.2f)", g->x, g->heading);
        DCHECK(!strcmp(g->name, DA.fish[0].name) && g->color == DA.fish[0].color && g->shape == DA.fish[0].shape, "the visitor is not A's fish (name, color, form)");
        DCHECK(fabsf(g->y / TANK_BOT - y_out / TANK_BOT) < 0.25f, "it arrived at another depth (%.2f vs %.2f)", g->y / TANK_BOT, y_out / TANK_BOT);
        DCHECK(!strcmp(DB.vis_home_name[v - N_FISH_MAX], "SAM'S REEF"), "its home's name came as '%s' (want SAM'S REEF, one line)", DB.vis_home_name[v - N_FISH_MAX]);
        d_step(300);
        DCHECK(tank_swims(&DB, v) && g->x > tank_glass_x0(DOOR_Y) + 40 && g->door == DOOR_NONE, "the visitor did not swim on into B (x %.0f door %d)", g->x, g->door);
        printf("selftest-doors: A's fish went through the right door in %.1f s and swam into B at its left, the same depth\n", f_in / 30.0);

        /* 2. home: told to go, it swims back through the door */
        g->homeward = true;
        int back = -1;
        for (int n = 0; n < 1800 && back < 0; n++) { d_step(1); if (!DA.fish[0].away && d_vis_of(&DB, uid) < 0) back = n; }
        DCHECK(back >= 0, "the visitor never went home");
        DCHECK(DA.fish[0].door == DOOR_ENTERING && DA.fish[0].x > tank_glass_x1(DOOR_Y) - 30, "it came home somewhere else than its door (x %.0f door %d)", DA.fish[0].x, DA.fish[0].door);
        d_step(200);
        DCHECK(tank_swims(&DA, 0) && DA.fish[0].x < tank_glass_x1(DOOR_Y) - 40, "home, it did not swim on in (x %.0f)", DA.fish[0].x);
        printf("selftest-doors: told to go home, it swam back through the door in %.1f s and on into A\n", back / 30.0);
    }

    /* 3. the keeper closes the door with a fish next door: a poof there, bubbles at home */
    d_step(300);
    d_head_for(&DA, 1, LINK_RIGHT); uid = DA.fish[1].uid;
    for (int n = 0; n < 1800 && d_vis_of(&DB, uid) < 0; n++) d_step(1);
    DCHECK(d_vis_of(&DB, uid) >= 0, "fish 1 never crossed");
    d_step(60);
    link_close(LA, LINK_RIGHT);
    d_step(30);
    DCHECK(d_vis_of(&DB, uid) < 0 && !DA.fish[1].away && tank_swims(&DA, 1), "closing the door did not bring fish 1 home (away %d, at B %d)", DA.fish[1].away, d_vis_of(&DB, uid));
    DCHECK(!DA.door_open[LINK_RIGHT] && !DB.door_open[LINK_LEFT], "the doors stayed open");
    printf("selftest-doors: the door closed with a fish next door - it poofed there and came home from bubbles\n");

    /* 3c. the model changes its mind between the ask and the yes, then explore picks the door again: the yes still counts */
    link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400);
    d_at_door_facing_in(&DA, 0, LINK_RIGHT); uid = DA.fish[0].uid;
    for (int n = 0; n < 300 && DA.fish[0].door != DOOR_ASKED; n++) d_step(1);
    if (DA.fish[0].door == DOOR_ASKED) {                    /* the ask is out: the answer lands next step - meanwhile it heads for the door again */
        tank_link_tick(&DA, LA);                            /* (the OFFER goes out now) */
        DA.fish[0].door = DOOR_HEADING;
        DA.fish[0].x -= 50;                                 /* as on the board: drifted back from the glass (kelp: 30-55 px) */
    }
    int got3 = -1;
    for (int n = 0; n < 300 && got3 < 0; n++) { d_step(1); if (d_vis_of(&DB, uid) >= 0) got3 = n; }
    DCHECK(got3 >= 0, "3c: a yes that found the fish heading back to the door was lost (door %d, x %.0f)", DA.fish[0].door, DA.fish[0].x);
    printf("selftest-doors: the model wavered between the ask and the yes: the yes still counts - through in %.1f s\n", got3 / 30.0);
    if (d_vis_of(&DB, uid) >= 0) { DB.fish[d_vis_of(&DB, uid)].homeward = true; for (int n = 0; n < 1800 && DA.fish[0].away; n++) d_step(1); }
    link_close(LA, LINK_RIGHT); d_step(60);

    /* 3d. the model drops the door after the ask, the yes lands while it is elsewhere, then explore picks
       the door again: back at the glass it asks no more (its offer is out) - the yes it already has counts
       (2026-10-05, pip on the 1.8: "on through the left door", then ASKED at x 33 until the window closed) */
    link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400);
    d_at_door_facing_in(&DA, 0, LINK_RIGHT); uid = DA.fish[0].uid;
    for (int n = 0; n < 300 && DA.fish[0].door != DOOR_ASKED; n++) d_step(1);
    if (DA.fish[0].door == DOOR_ASKED) {
        tank_link_tick(&DA, LA);                            /* (the OFFER goes out now) */
        DA.fish[0].door = DOOR_NONE;                        /* a new goal: no visit */
        d_step(3);                                          /* the yes lands while it is elsewhere */
        d_head_for(&DA, 0, LINK_RIGHT);                     /* explore chooses the door again */
    }
    int got4 = -1;
    for (int n = 0; n < 300 && got4 < 0; n++) { d_step(1); if (d_vis_of(&DB, uid) >= 0) got4 = n; }
    DCHECK(got4 >= 0, "3d: back at the glass with a yes in hand it never went (door %d, x %.0f)", DA.fish[0].door, DA.fish[0].x);
    if (got4 >= 0) printf("selftest-doors: the model dropped the door and came back to it: the yes it had still counts - through in %.1f s\n", got4 / 30.0);
    if (d_vis_of(&DB, uid) >= 0) { DB.fish[d_vis_of(&DB, uid)].homeward = true; for (int n = 0; n < 1800 && DA.fish[0].away; n++) d_step(1); }
    link_close(LA, LINK_RIGHT); d_step(60);

    /* 3f. once through the glass is decided it stays decided: explore must not pick a door again mid-doorway
       (2026-10-05, the boards: 9 of 25 accepted fish ended "elsewhere" at the glass, facing in - explore's
       door pick ran under a PASSING fish, turned it round for a door, often the other one) */
    link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400);
    d_at_door_facing_in(&DA, 0, LINK_RIGHT); uid = DA.fish[0].uid;
    DA.fish[0].curiosity = 10; DA.fish[0].door_seen = DA.clock - 600; DA.fish[0].explore_set = false;
    int left_passing = 0, got5 = -1; bool was_passing = false;
    for (int n = 0; n < 600 && got5 < 0; n++) {
        DA.fish[0].explore_set = false; DA.fish[0].door_seen = DA.clock - 600;   /* every frame explore would pick again */
        d_step(1);
        if (was_passing && !DA.fish[0].away && DA.fish[0].door != DOOR_PASSING) left_passing++;
        was_passing = DA.fish[0].door == DOOR_PASSING;
        if (d_vis_of(&DB, uid) >= 0) got5 = n;
    }
    DCHECK(left_passing == 0, "3f: a fish passing through the door was turned for a door again (%d times)", left_passing);
    DCHECK(got5 >= 0, "3f: it never crossed (door %d x %.0f)", DA.fish[0].door, DA.fish[0].x);
    if (!left_passing && got5 >= 0) printf("selftest-doors: explore picking doors every frame: the passing fish kept its line - through in %.1f s\n", got5 / 30.0);
    if (d_vis_of(&DB, uid) >= 0) { DB.fish[d_vis_of(&DB, uid)].homeward = true; for (int n = 0; n < 1800 && DA.fish[0].away; n++) d_step(1); }
    link_close(LA, LINK_RIGHT); d_step(60);

    /* 3b. hovering at the door facing in when the neighbor says yes: it turns and goes (the boards' case) */
    link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400);
    DCHECK(DA.door_open[LINK_RIGHT], "3b: the door did not open again");
    d_at_door_facing_in(&DA, 0, LINK_RIGHT); uid = DA.fish[0].uid;
    { int j = 1 - 0;                                        /* and everything that steers a fish elsewhere, at once */
      DA.fish[0].hesitate = 3; DA.fish[0].goal.runner_up = GOAL_REST; DA.greet_timer = 6; DA.fish[0].trust = 9;
      DA.spawning = true; DA.court_a = 0; DA.court_b = (int8_t)j;
      DA.hold_active = true; DA.hold_time = 5; DA.hold_x = TANK_W * 0.3f; DA.hold_y = TANK_BOT * 0.5f; }
    int got = -1;
    for (int n = 0; n < 600 && got < 0; n++) { d_step(1); if (d_vis_of(&DB, uid) >= 0) got = n; }
    DCHECK(got >= 0 && got < 6 * 30, "3b: a fish at the door facing in did not get through (door %d, x %.0f, facing %d)", DA.fish[0].door, DA.fish[0].x, DA.fish[0].facing);
    printf("selftest-doors: hovering at the door facing in, courting, hesitating, a finger held: it turned and was through in %.1f s\n", got / 30.0);
    DA.spawning = false; DA.court_a = DA.court_b = -1; DA.greet_timer = 0;
    DB.fish[d_vis_of(&DB, uid) >= 0 ? d_vis_of(&DB, uid) : N_FISH_MAX].homeward = true;
    for (int n = 0; n < 1800 && DA.fish[0].away; n++) d_step(1);

    /* 3e. the keeper triple-taps a visitor: it high-tails it home through its door (Strato 2026-10-05) */
    d_step(60);
    d_head_for(&DA, 1, LINK_RIGHT); uid = DA.fish[1].uid;
    for (int n = 0; n < 1800 && d_vis_of(&DB, uid) < 0; n++) d_step(1);
    DCHECK(d_vis_of(&DB, uid) >= 0, "3e: fish 1 never crossed");
    if (d_vis_of(&DB, uid) >= 0) {
        fish_t *g = &DB.fish[d_vis_of(&DB, uid)];
        d_step(60);
        g->x = tank_glass_x1(DOOR_Y) - 60; g->y = DOOR_Y + 20;          /* across the tank from its door */
        float y_tap = g->y + 30;
        for (int k = 0; k < 3; k++) tank_touch_tap(&DB, g->x + 20, y_tap);
        DCHECK(g->bolt_home && g->homeward, "3e: the spooked visitor did not turn for home");
        int home = -1; float top = 0;
        for (int n = 0; n < 600 && home < 0; n++) {
            d_step(1);
            if (d_vis_of(&DB, uid) >= 0 && g->speed > top) top = g->speed;
            if (getenv("BOLT_TRACE") && n % 15 == 0 && d_vis_of(&DB, uid) >= 0) printf("  t %4.1f door %d x %5.1f y %5.1f speed %4.1f facing %d heading %.2f behind %.2f stage %d\n", n / 30.0, g->door, g->x, g->y, g->speed, g->facing, g->heading, g->behind, g->stage);
            if (!DA.fish[1].away && d_vis_of(&DB, uid) < 0) home = n;
        }
        DCHECK(home >= 0 && home < 300, "3e: the spooked visitor was not home within 10 s (%d frames, door %d x %.0f)", home, g->door, g->x);
        DCHECK(top > 60, "3e: it did not bolt (top speed %.0f)", top);
        if (home >= 0) printf("selftest-doors: triple-tapped, the visitor bolted home across the tank (top %.0f px/s) and through its door in %.1f s\n", top, home / 30.0);
    }
    /* 4b. no parent here (every fish of A next door): a forced or a wake's birth waits - never a fry of absent fish */
    { int n0 = DA.n_fish; bool aw[N_FISH_MAX];
      for (int i = 0; i < DA.n_fish; i++) { aw[i] = DA.fish[i].away; DA.fish[i].away = true; }
      progression_force_arrival(&DA);
      DCHECK(DA.n_fish == n0, "4b: a fry was born with every parent away (%d -> %d fish)", n0, DA.n_fish);
      for (int i = 0; i < DA.n_fish; i++) DA.fish[i].away = aw[i];
      while (DA.n_fish > n0) tank_remove_fish(&DA, DA.n_fish - 1);
      if (DA.n_fish == n0) printf("selftest-doors: 4b. every fish of A next door: a forced birth waits - no fry of absent parents\n"); }
    /* 5. cross-tank fry (phase 5, Strato 2026-10-05): B's fish visits A and courts A's best */
    { link_close(LA, LINK_RIGHT); d_step(60);
      int n_fry_ok = 0;
      for (int sc = 0; sc < 4; sc++) {                     /* 5a home wins, 5b ours wins, 5c home full, 5d the door closes mid-scurry */
        link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400);
        int nA = DA.n_fish, nB0 = DB.n_fish;
        for (int i = 0; i < DA.n_fish; i++) DA.fish[i].door_seen = DA.clock + 3600;   /* only B's fish 0 wanders: the scene's one visitor */
        for (int i = 1; i < DB.n_fish; i++) DB.fish[i].door_seen = DB.clock + 3600;
        int nB = DB.n_fish;
        d_head_for(&DB, 0, LINK_LEFT); uint32_t par = DB.fish[0].uid;   /* B's fish 0 over to A */
        for (int n = 0; n < 1800 && d_vis_of(&DA, par) < 0; n++) d_step(1);
        DCHECK(d_vis_of(&DA, par) >= 0, "5%c: B's fish never visited (door %d away %d x %.0f, A room %d, A swims %d, B fish %d)", 'a' + sc, DB.fish[0].door, DB.fish[0].away, DB.fish[0].x, tank_link_room(&DA), DA.n_fish, DB.n_fish);
        if (d_vis_of(&DA, par) < 0) { printf("  A slots:"); for (int k = 0; k < N_VISIT_MAX; k++) printf(" [%d used %d pend %d uid %08x]", k, DA.vis_used[k], DA.vis_pending[k], DA.fish[N_FISH_MAX + k].uid); printf(" give %d\n", DA.fry_give_k);
            link_close(LA, LINK_RIGHT); d_step(60); while (DB.n_fish > nB0) tank_remove_fish(&DB, DB.n_fish - 1); continue; }
        if (sc == 2) {                                     /* B fills up while its fish is over here (new fish with no wanderlust yet) */
            while (DB.n_fish < POP_CAP && DB.n_fish < N_FISH_MAX) { int j = tank_add_fish(&DB, 0, 1); if (j >= 0) DB.fish[j].door_seen = DB.clock + 600; }
            nB = DB.n_fish;
        }
        for (int q = 0, quiet = 0; q < 3600 && quiet < 90; q++) {   /* the scene's one visitor: another that wandered over goes home and none
                                                                       sets out (0.4.1: a fry for its home needs a visitor place - two in, it waits) */
            int other = 0;
            for (int k = 0; k < N_VISIT_MAX; k++) {
                if (DA.vis_used[k] && DA.fish[N_FISH_MAX + k].uid != par) { DA.fish[N_FISH_MAX + k].homeward = DA.fish[N_FISH_MAX + k].bolt_home = true; other++; }
                if (DB.vis_used[k]) { DB.fish[N_FISH_MAX + k].homeward = DB.fish[N_FISH_MAX + k].bolt_home = true; other++; }
            }
            for (int t2 = 0; t2 < 2; t2++) { tank_t *h = t2 ? &DB : &DA;
                for (int i = 0; i < h->n_fish; i++) { fish_t *f = &h->fish[i]; if (f->uid == par) continue;
                    f->door_seen = h->clock + 3600;
                    if (f->away) other++; else if (f->door == DOOR_HEADING) f->door = DOOR_NONE; else if (f->door != DOOR_NONE) other++; } }
            int pv = d_vis_of(&DA, par); if (pv >= 0) { DA.fish[pv].homeward = false; DA.vis_since[pv - N_FISH_MAX] = DA.clock; }
            quiet = other ? 0 : quiet + 1;
            d_step(1);
        }
        DA.fry_partner = par; DA.fry_asked = false; DA.fry_home_room = -1;
        for (int n = 0; n < 60 && DA.fry_home_room < 0; n++) d_step(1);
        DCHECK(DA.fry_home_room == (sc == 2 ? 0 : 1), "5%c: its home's room came back %d", 'a' + sc, DA.fry_home_room);
        DA.fry_force = sc == 1 ? 0 : 1;
        uint32_t hostpar = 0;
        { int a = -1; for (int i = 0; i < DA.n_fish; i++) if (a < 0 || DA.fish[i].trust > DA.fish[a].trust) a = i; (void)a; }
        progression_force_arrival(&DA);
        if (sc == 0 || sc == 3) {                          /* its home's: it waits for the yes, then swims home - its parent after it */
            DCHECK(DA.n_fish == nA && DA.fry_give_k >= 0, "5%c: the fry was not held for its home (A %d fish, give %d)", 'a' + sc, DA.n_fish, DA.fry_give_k);
            int k = DA.fry_give_k; uint32_t fry = k >= 0 ? DA.fish[N_FISH_MAX + k].uid : 0;
            for (int n = 0; n < 30 && DA.fry_give_k >= 0; n++) d_step(1);
            int bs = -1; for (int i = 0; i < DB.n_fish; i++) if (DB.fish[i].uid == fry) bs = i;
            DCHECK(bs >= 0 && DB.n_fish == nB + 1 && DB.fish[bs].away, "5%c: B did not take its fry (slot %d, %d fish)", 'a' + sc, bs, DB.n_fish);
            DCHECK(bs >= 0 && (DB.fish[bs].ms_bits & MS_CROSSBRED) && progression_fish_value(&DB, bs) == 2 * SD_FISH_FRY,
                   "5%c: its home's fry is not marked cross-tank / worth double (value %d)", 'a' + sc, bs >= 0 ? progression_fish_value(&DB, bs) : -1);
            int fv = d_vis_of(&DA, fry);
            DCHECK(fv >= 0 && DA.fish[fv].bolt_home, "5%c: the fry is not swimming home from A", 'a' + sc);
            if (bs >= 0) { hostpar = DB.fish[bs].parent_uid[0] == par ? DB.fish[bs].parent_uid[1] : DB.fish[bs].parent_uid[0];
                DCHECK((DB.fish[bs].parent_uid[0] == par || DB.fish[bs].parent_uid[1] == par) && hostpar && d_vis_of(&DA, hostpar) < 0,
                       "5%c: the fry does not name both parents (%08x %08x)", 'a' + sc, DB.fish[bs].parent_uid[0], DB.fish[bs].parent_uid[1]);
                /* its home knows the other parent by name: it came with the fry (it never visited B) */
                const kin_t *kn = kin_find(hostpar); const char *want = NULL;
                for (int i = 0; i < DA.n_fish; i++) if (DA.fish[i].uid == hostpar) want = DA.fish[i].name;
                DCHECK(kn && want && !strcmp(kn->name, want), "5%c: its home does not know its other parent's name (%s)", 'a' + sc, kn ? kn->name : "none"); }
            int pv = d_vis_of(&DA, par);
            DCHECK(pv >= 0 && DA.fish[pv].homeward, "5%c: the visiting parent does not follow it home", 'a' + sc);
            if (sc == 3) {                                  /* the door closes as it swims: bubbles there, bubbles here, one fry */
                d_step(15); link_close(LA, LINK_RIGHT); d_step(60);
                DCHECK(bs >= 0 && !DB.fish[bs].away && d_vis_of(&DA, fry) < 0, "5d: the door closed and the fry is not home (away %d, at A %d)", bs >= 0 ? DB.fish[bs].away : -1, d_vis_of(&DA, fry));
            } else {
                int home = -1; for (int n = 0; n < 600 && home < 0; n++) { d_step(1); if (bs >= 0 && !DB.fish[bs].away) home = n; }
                DCHECK(home >= 0 && home < 360, "5a: the fry was not home in 12 s (%d)", home);
                DCHECK(bs >= 0 && progression_newborn() == bs, "5a: its birth flow is not waiting at home (newborn %d, slot %d)", progression_newborn(), bs);
                {   /* two owed at once (the boards, 2026-10-05): the second waits its turn, never lost */
                    int other = bs == 0 ? 1 : 0;
                    DB.fish[other].born_away = true; progression_fry_home(&DB, other);
                    DCHECK(progression_newborn() == bs, "5a: a second arrival took the first one's welcome");
                    progression_newborn_done(&DB);
                    DCHECK(progression_newborn() == other, "5a: the second welcome was lost when the first was done (%d)", progression_newborn());
                    progression_newborn_done(&DB);
                    DCHECK(progression_newborn() < 0, "5a: a welcome owed twice");
                }
                DCHECK(d_vis_of(&DA, par) >= 0, "5a: the parent was home before its fry - it should follow, not lead");
                int back = -1; for (int n = 0; n < 1800 && back < 0; n++) { d_step(1); if (d_vis_of(&DA, par) < 0 && !DB.fish[0].away) back = n; }
                DCHECK(back >= 0 && back < 150, "5a: the parent did not follow it home within 5 s (%d)", back);
                if (home >= 0 && back >= 0) { printf("selftest-doors: 5a. a fry for the visitor's home: born in A's grass, home through the door in %.1f s, its parent %.1f s later\n", home / 30.0, back / 30.0); n_fry_ok++; }
            }
            if (sc == 3 && bs >= 0 && !DB.fish[bs].away) { printf("selftest-doors: 5d. the door closed as the fry swam: bubbles in A, the fry home in B from bubbles - one fry\n"); n_fry_ok++; }
        } else {                                            /* ours: born here, both parents named */
            DCHECK(DA.n_fish == nA + 1 && DB.n_fish == nB, "5%c: the fry is not A's (A %d -> %d, B %d -> %d)", 'a' + sc, nA, DA.n_fish, nB, DB.n_fish);
            const fish_t *f = &DA.fish[DA.n_fish - 1];
            DCHECK(f->parent_uid[0] == par || f->parent_uid[1] == par, "5%c: A's fry does not name the visitor as a parent", 'a' + sc);
            DCHECK((f->ms_bits & MS_CROSSBRED) && progression_fish_value(&DA, DA.n_fish - 1) == 2 * SD_FISH_FRY, "5%c: A's cross-tank fry is not worth double", 'a' + sc);
            { static const int worth[4] = { SD_FISH_FRY, SD_FISH_JUV, SD_FISH_ADULT, SD_FISH_ELDER };
              DCHECK(!(DA.fish[0].ms_bits & MS_CROSSBRED) && progression_fish_value(&DA, 0) == worth[DA.fish[0].stage & 3],
                     "5%c: a home-bred fish's value changed (%d, stage %d, bits %x)", 'a' + sc, progression_fish_value(&DA, 0), DA.fish[0].stage, DA.fish[0].ms_bits); }
            DCHECK(f->parent_a != N_FISH_MAX + 0 && f->parent_b < N_FISH_MAX, "5%c: a visitor's slot kept as a parent slot", 'a' + sc);
            if (DA.n_fish == nA + 1) { printf("selftest-doors: 5%c. %s: the fry is A's, born in its grass, both parents named\n", 'a' + sc, sc == 1 ? "the coin said ours" : "its home is full"); n_fry_ok++; }
            tank_remove_fish(&DA, DA.n_fish - 1);
        }
        /* home again, back to two each */
        for (int i = 0; i < N_VISIT_MAX; i++) if (DA.vis_used[i]) DA.fish[N_FISH_MAX + i].homeward = true;
        for (int n = 0; n < 1800 && (DB.fish[0].away || d_vis_of(&DA, par) >= 0); n++) d_step(1);
        link_close(LA, LINK_RIGHT); d_step(60);
        while (DB.n_fish > nB0) tank_remove_fish(&DB, DB.n_fish - 1);
        while (DA.n_fish > nA) tank_remove_fish(&DA, DA.n_fish - 1);
      }
      DCHECK(n_fry_ok == 4, "5: %d of 4 fry scenes", n_fry_ok); }

    /* 6. time together (Strato 2026-10-06): no care gates met in A - a visitor that swims beside one of A's
       fish for its goal (5-10 min, random) courts it, and a fry comes */
    { link_close(LA, LINK_RIGHT); d_step(60);
      link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400);
      int nA = DA.n_fish, nB = DB.n_fish;
      for (int i = 0; i < DA.n_fish; i++) DA.fish[i].door_seen = DA.clock + 3600;
      for (int i = 1; i < DB.n_fish; i++) DB.fish[i].door_seen = DB.clock + 3600;
      d_head_for(&DB, 0, LINK_LEFT); uint32_t par = DB.fish[0].uid;
      for (int n = 0; n < 1800 * ((TANK_W + 447) / 448) && d_vis_of(&DA, par) < 0; n++) d_step(1);   /* (a minute per 1.8 floor: the home tank's fish can start 765 px from its door) */
      DCHECK(d_vis_of(&DA, par) >= 0, "6: B's fish never visited");
      fry_req_t rq[FRY_REQ_MAX]; bool staged; int nr = progression_next_fry(&DA, rq, &staged), met = 0;
      for (int i = 0; i < nr; i++) met += rq[i].met;
      DCHECK(met < nr, "6: A's own checklist is complete - the test proves nothing");
      int v = d_vis_of(&DA, par); double t0 = DA.clock; int born = -1, n_steps = 0;
      if (v >= 0) for (int n = 0; n < 30 * 900 && born < 0; n++, n_steps++) {   /* up to 15 minutes, beside A's fish 0 */
          int vv = d_vis_of(&DA, par); if (vv < 0) break;
          fish_t *g = &DA.fish[vv];
          uint32_t mate = 0; (void)progression_bond(&DA, par, &mate); int mi = 0;   /* (beside the fish it grew close to - fish 0 unless another */
          for (int i = 0; i < DA.n_fish; i++) if (mate && DA.fish[i].uid == mate) mi = i;   /*  was nearer as it came in: on the watch, 0.4) */
          if (!DA.courting) { g->x = DA.fish[mi].x + 30; g->y = DA.fish[mi].y; g->homeward = false; DA.vis_since[vv - N_FISH_MAX] = DA.clock; }   /* (kept beside it; its home's pull held off) */
          DA.fry_force = 1;
          DA.fish[mi].hunger = g->hunger = 2;                   /* (a tended tank: 15+ minutes unfed and a parent begs at the surface
                                                                     before it dances - on the home tank's long floor that ran the fry
                                                                     to 41 min, 2026-10-20) */
          d_step(1); progression_tick(&DA, 0.033f);
          if (DA.n_fish != nA || DA.fry_give_k >= 0 || DB.n_fish != nB) born = n;
      }
      float took = (float)(DA.clock - t0) / 60.0f;
      DCHECK(born >= 0, "6: 15 minutes together and no fry (bond %.2f, partner %08x, courting %d)", progression_bond(&DA, par, NULL), DA.fry_partner, DA.courting);
      DCHECK(born < 0 || (took >= 4.9f && took <= 11.5f), "6: the fry came after %.1f min - the goal is 5-10", took);
      for (int n = 0; n < 900 && (DA.fry_give_k >= 0 || (DB.n_fish > nB && DB.fish[DB.n_fish - 1].away)); n++) d_step(1);
      bool home = DB.n_fish == nB + 1 && !DB.fish[DB.n_fish - 1].away && (DB.fish[DB.n_fish - 1].ms_bits & MS_CROSSBRED);
      DCHECK(home, "6: the fry did not reach B (B %d -> %d fish)", nB, DB.n_fish);
      DCHECK(progression_bond(&DA, par, NULL) < 0.05f, "6: the pair's time together did not start over (%.2f)", progression_bond(&DA, par, NULL));
      if (born >= 0 && home) printf("selftest-doors: 6. time together: a visitor beside one of A's fish (A's checklist not met) - a fry after %.1f min, home in B\n", took);
      for (int i = 0; i < N_VISIT_MAX; i++) if (DA.vis_used[i]) DA.fish[N_FISH_MAX + i].homeward = true;
      for (int n = 0; n < 1800 && DB.fish[0].away; n++) d_step(1);
      while (DB.n_fish > nB) tank_remove_fish(&DB, DB.n_fish - 1);
      while (DA.n_fish > nA) tank_remove_fish(&DA, DA.n_fish - 1);
      link_close(LA, LINK_RIGHT); d_step(60);
      link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400); }

    /* 7. GIVE (phase 6): a fish handed over for good - both keepers agree on their own glass,
       tank_link.c and the notes between the tanks and the engines (gift.h) */
    { static gift_book_t GA, GB; memset(&GA, 0, sizeof GA); memset(&GB, 0, sizeof GB);
      gift_attach(&DA, &GA); gift_attach(&DB, &GB);
      #define D_HOME(t, u) ({ int _h = -1; for (int _i = 0; _i < (t)->n_fish; _i++) if ((t)->fish[_i].uid == (u)) _h = _i; _h; })
      while (DB.n_fish < 3) { tank_make_fish(&DB, DB.n_fish, DB.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); DB.n_fish++; }
      tank_ensure_ids(&DB);
      for (int i = 0; i < DB.n_fish; i++) DB.fish[i].door = DOOR_NONE;
      int nA = DA.n_fish, nB = DB.n_fish; uint32_t gu = DB.fish[nB - 1].uid;
      /* 7a. NO THANKS: it stays, and the giver's card says why */
      while (progression_newborn() >= 0) progression_newborn_done(&DA);   /* (scenario 6's fry: its welcome is not this test's) */
      if (!DA.door_open[LINK_RIGHT]) { link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); d_step(60);
          link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400); }
      DCHECK(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT], "7: the doors did not open");
      {   /* the gift: one of B's fish that is home, not in a doorway (on the watch's narrow floor the relink found the last one
             crossing, then visiting - a fish is given once it is home, 2026-11-01) */
          int gi = -1;
          for (int n = 0; n < 1800 && gi < 0; n++) {
              for (int i = DB.n_fish - 1; i >= 0 && gi < 0; i--)   /* (home AND clear of the doors: with B five fish in 5c, not
                                                                    six, the round's run reached 7a with it in a doorway - the
                                                                    gift waited past the test's 10 frames, 2026-11-03) */
                  if (progression_fish_givable(&DB, i) && !DB.fish[i].away && DB.fish[i].door == DOOR_NONE) gi = i;
              if (gi < 0) d_step(1);
          }
          DCHECK(gi >= 0, "7a: no fish of B's came home to be given");
          if (gi >= 0) gu = DB.fish[gi].uid;
      }
      GB.want_uid = gu; d_step(10);
      DCHECK(GB.asking_uid == gu && GA.ask.on && GA.ask.uid == gu, "7a: the question never reached A (asking %08x, ask %d)", GB.asking_uid, GA.ask.on);
      GA.ask.answer = -1; d_step(10);
      DCHECK(!GA.ask.on && !GB.asking_uid && GB.said_uid == gu && GB.said_reason == LINK_GIVE_NO_REFUSED && DB.n_fish == nB && D_HOME(&DB, gu) >= 0,
             "7a: a NO did not leave the fish where it was (said %08x/%d, B %d fish)", GB.said_uid, GB.said_reason, DB.n_fish);
      printf("selftest-doors: 7a. GIVE, NO THANKS: the fish stays, the card says why\n");
      /* 7b. the giver taps ASKING: it takes the fish back, the question closes over there */
      GB.said_uid = 0; GB.want_uid = gu; d_step(10);
      DCHECK(GA.ask.on, "7b: no question");
      GB.cancel_uid = gu; d_step(10);
      DCHECK(!GA.ask.on && !GB.asking_uid && DB.n_fish == nB && GA.n_arr == 0, "7b: backing out left something (ask %d, asking %08x)", GA.ask.on, GB.asking_uid);
      printf("selftest-doors: 7b. GIVE, the giver backs out while asking: the question closes next door\n");
      /* 7c. ACCEPT, by the keepers' taps: B's card shows RENAME | GIVE | SELL while linked; GIVE opens the GIVE modal
         (the keeper card's path), its GIVE - not in its first GIFT_HOLD_S - asks A; A's card answers ACCEPT and the
         modal goes. Then out of B's left door, in at A's right, never in both - and never in neither without its note */
      { static uint16_t pfb[TANK_W * TANK_H]; const char *shot = getenv("GIVE_SHOT");
        #define D_SHOT(tk, name) d_shot(pfb, name)
        int k = D_HOME(&DB, gu), gx, gy;
        render_milestones_show_fish(&DB, k); render_milestones(&DB, pfb, TANK_W); D_SHOT(&DB, "1_card");
        if (shot) { render_tank(&DB, pfb, TANK_W); render_tank(&DB, pfb, TANK_W); render_stats_card(&DB, k, pfb, TANK_W); D_SHOT(&DB, "1s_stat"); }
        DCHECK(render_milestones_give(&DB, &gx, &gy), "7c: no GIVE on a linked tank's card");
        render_milestones_tap(&DB, (float)gx, (float)gy); render_milestones(&DB, pfb, TANK_W); D_SHOT(&DB, "2_modal");
        DCHECK(render_give_modal_step(&DB) == RENDER_GM_CONFIRM && !strcmp(render_give_modal_line(&DB), "IT WOULD LIVE THERE") && !GB.want_uid,
               "7c: the keeper card's GIVE did not open the modal (step %d)", render_give_modal_step(&DB));
        int mx = 0, my = 0;
        DCHECK(render_give_modal_button(&DB, RENDER_GM_TAP_GIVE, &mx, &my), "7c: no GIVE in the modal");
        DCHECK(render_milestones_tap(&DB, (float)mx, (float)my) == MS_TAP_KEPT && !GB.want_uid, "7c: the modal's GIVE took a tap the moment it came up");
        d_step((int)(GIFT_HOLD_S * 30) + 2);
        render_milestones_tap(&DB, (float)mx, (float)my);
        DCHECK(GB.want_uid == gu && GB.want_door == GIFT_DOOR_LEFT && GB.want_peer == DA.uid, "7c: the modal's GIVE did not ask through B's left door (want %08x door %d peer %08x)", GB.want_uid, GB.want_door, GB.want_peer);
        d_step(10);
        render_milestones(&DB, pfb, TANK_W); D_SHOT(&DB, "3_asking");
        DCHECK(render_give_modal_step(&DB) == RENDER_GM_ASKING && GB.asking_uid == gu, "7c: the modal did not say ASKING (step %d)", render_give_modal_step(&DB));
        DCHECK(render_milestones_tap(&DB, 2, 2) == MS_TAP_KEPT && GB.asking_uid == gu && render_give_modal_step(&DB) == RENDER_GM_ASKING,
               "7c: a tap off the modal did something while the gift was asked");   /* (no RENAME, no page: the modal has the glass) */
        DCHECK(GA.ask.on && GA.ask.uid == gu, "7c: A was not asked");
        render_tank(&DA, pfb, TANK_W); render_gift(&DA, pfb, TANK_W); D_SHOT(&DA, "4_gift");
        DCHECK(!d_bake_diff(&DA, true), "7c: the gift card from its bake is not the card drawn whole");
        int ax = (TANK_W - GIFT_CARD_W) / 2 + 12 + GIFT_BTN_W / 2, ay = (TANK_H - GIFT_CARD_H) / 2 + GIFT_BTN_Y + GIFT_BTN_H / 2;
        int nx = (TANK_W + GIFT_CARD_W) / 2 - 12 - GIFT_BTN_W / 2;
        DCHECK(render_gift_hit((float)ax, (float)ay) == GIFT_HIT_ACCEPT && render_gift_hit((float)nx, (float)ay) == GIFT_HIT_NO, "7c: the gift card's buttons are not where they are drawn");
        /* THE TOUCH RULE (touch_gate.h, 11-06): a press begun before the card came is no answer; there is no hold (2 s before) */
        DCHECK(!gift_answer(&GA, GA.ask.seen - 0.05, DA.clock, true) && !gift_answer(&GA, GA.ask.seen - 0.05, DA.clock, false) && GA.ask.on && !GA.ask.answer, "7c: a press from before the gift card answered it");
        d_step((int)(GIFT_HOLD_S * 30) + 2);
        DCHECK(gift_answer(&GA, DA.clock, DA.clock, render_gift_hit((float)ax, (float)ay) == GIFT_HIT_ACCEPT) && GA.ask.answer == 1, "7c: ACCEPT did not answer after the hold");   /* (the touch port's ACCEPT) */
        DCHECK(!gift_answer(&GA, DA.clock, DA.clock, false) && GA.ask.answer == 1, "7c: a second tap changed the answer");
        d_step(20); render_milestones(&DB, pfb, TANK_W); D_SHOT(&DB, "5_leaving");
        DCHECK(render_give_modal_step(&DB) == RENDER_GM_OFF, "7c: the modal stayed up once the gift was taken");
        if (shot && D_HOME(&DB, gu) >= 0) { render_tank(&DB, pfb, TANK_W); render_stats_card(&DB, D_HOME(&DB, gu), pfb, TANK_W); D_SHOT(&DB, "5s_stat"); }
        render_milestones_leave();
        #undef D_SHOT
      }
      int gone = -1, in = -1, n = 0;
      for (; n < 1800 && (gone < 0 || in < 0 || GB.n_given || GA.n_arr); n++) {
          d_step(1);
          bool inA = D_HOME(&DA, gu) >= 0, inB = D_HOME(&DB, gu) >= 0;
          DCHECK(!(inA && inB), "7c: the gift lives in both tanks");
          DCHECK(inA || inB || gift_given_has(&GB, gu), "7c: the gift lives nowhere and no note holds it");
          if (gone < 0 && !inB) gone = n;
          if (in < 0 && inA) in = n;
      }
      DCHECK(gone >= 0 && in >= 0 && !GB.n_given && !GA.n_arr && DA.n_fish == nA + 1 && DB.n_fish == nB - 1,
             "7c: the gift did not settle (gone %d in %d, notes %d/%d, A %d B %d)", gone, in, GB.n_given, GA.n_arr, DA.n_fish, DB.n_fish);
      {   /* the slot the gift left: the next fish made there must not inherit its trip through the door (it never asked to cross) */
          static tank_t S; S = DB; int k = S.n_fish;
          tank_make_fish(&S, k, 0, 0.5f, 0.5f, STAGE_ADULT);
          DCHECK(!S.fish[k].door_sent && !S.fish[k].deported && !S.fish[k].bolt_home, "7c: a fish made in the given fish's slot inherited its door (sent %d deported %d)", S.fish[k].door_sent, S.fish[k].deported); }
      { int i = D_HOME(&DA, gu);
        DCHECK(i < 0 || (DA.fish[i].x > tank_glass_x1(DA.fish[i].y) - 120 || DA.fish[i].door == DOOR_ENTERING || n < in + 60), "7c: the gift did not come in at the door");
        if (gone >= 0 && in >= 0) printf("selftest-doors: 7c. GIVE, ACCEPT: out through B's door in %.1f s, in at A's %.1f s later, both notes gone\n", gone / 30.0, (in - gone) / 30.0); }
      /* 7d. the giver dies the moment its gift is taken (the fish still swimming out): nothing about it was saved
         yet - its GIVEN note is written only as it leaves (2026-11-02) - so the boot keeps the fish, and B's next TAKE
         is answered CANCEL: B's ARRIVING note goes, the fish lives once, in A. (It used to be removed by the boot and
         live nowhere until the two tanks linked again.) */
      int nA2 = DA.n_fish, nB2 = DB.n_fish;
      for (int q = 0; q < 600 && D_HOME(&DA, gu) >= 0 && DA.fish[D_HOME(&DA, gu)].door != DOOR_NONE; q++) d_step(1);   /* (in through the door first) */
      DCHECK(progression_fish_givable(&DA, D_HOME(&DA, gu)), "7d: the new fish cannot be given on");
      GA.want_uid = gu; d_step(10); GB.ask.answer = 1;
      int w = 0; while (w < 300 && GA.leaving_uid != gu) { d_step(1); w++; }
      DCHECK(GA.leaving_uid == gu && !gift_given_has(&GA, gu) && GB.n_arr == 1, "7d: the gift was never taken (or its note came before it left)");
      { gift_book_t keep = GA; memset(&GA, 0, sizeof GA);                       /* the crash: only what was saved */
        memcpy(GA.given, keep.given, sizeof GA.given); GA.n_given = keep.n_given;
        memcpy(GA.arriving, keep.arriving, sizeof GA.arriving); GA.n_arr = keep.n_arr;
        memcpy(GA.recv, keep.recv, sizeof GA.recv); GA.n_recv = keep.n_recv;
        for (int k = 0; k < GA.n_given; k++) { int i = D_HOME(&DA, GA.given[k].uid); if (i >= 0) progression_give_out(&DA, i); }   /* the boot: a note wins */
        for (int v = 0; v < N_VISIT_MAX; v++) DA.vis_used[v] = false;     /* ... and no visitor survives it (visitors are never saved: B's lease brings
                                                                             them home - on the watch's floor one of B's was next door, 2026-11-01) */
        for (int i = 0; i < DA.n_fish; i++) { DA.fish[i].door = DOOR_NONE; DA.fish[i].deported = false; }   /* (a boot's fish are where the save left them) */
        for (int i = 0; i < DA.n_fish; i++) if (DA.fish[i].away) DA.link_restore_at = DA.clock + LINK_BOOT_RESTORE_S;   /* (a fish next door: home once
                                                                             B's lease is surely over, as progression_load does) */
        link_init(LA, DA.uid, &ha, 9, dnow); dqn = 0; }
      DCHECK(D_HOME(&DA, gu) >= 0 && !gift_given_has(&GA, gu), "7d: the boot lost the fish that never left");
      int settled = -1; bool relinked = false;
      for (n = 0; n < 1800 && (settled < 0 || GA.n_given || GB.n_arr); n++) {
          if (n == 150) { link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); relinked = true; }
          d_step(1);
          bool inA = D_HOME(&DA, gu) >= 0, inB = D_HOME(&DB, gu) >= 0;
          DCHECK(!(inA && inB), "7d: the gift lives in both tanks");
          DCHECK(inA || inB || gift_given_has(&GA, gu), "7d: the gift lives nowhere and no note holds it");
          if (settled < 0 && !GB.n_arr) settled = n;
      }
      DCHECK(settled >= 0 && !GA.n_given && !GB.n_arr && D_HOME(&DA, gu) >= 0 && DA.n_fish == nA2 && DB.n_fish == nB2,
             "7d: the gift did not settle after the crash (settled %d, notes %d/%d, A %d B %d)", settled, GA.n_given, GB.n_arr, DA.n_fish, DB.n_fish);
      if (settled >= 0) printf("selftest-doors: 7d. GIVE, the giver died as its gift swam out: the boot kept the fish (no note yet), B's next TAKE got CANCEL %.1f s after the crash%s - one fish, in A\n", settled / 30.0, settled < 150 ? ", before any relink" : "");
      (void)relinked;
      /* 7h. the receiver goes QUIET while the gift swims out (unplugged, a dead battery, out of range - A cannot tell
         which for 10 s): A keeps it once B has been silent GIFT_HEARD_MS, before it reaches the door; when B is heard
         again its TAKE gets CANCEL. Never in neither, never in both. */
      { if (!(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT])) { link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); d_step(60);   /* (7d's crash shut A's door) */
            link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400); }
        int gi = d_wait_home(&DA, gu);
        DCHECK(gi >= 0, "7h: the fish never came home");
        if (gi >= 0) { DA.fish[gi].x = tank_glass_x0(DOOR_Y) + 30; DA.fish[gi].y = DOOR_Y; DA.fish[gi].door_refused = DA.clock + 60; }   /* (at the far glass: a swim of several seconds) */
        GA.want_uid = gu; d_step(10); GB.ask.answer = 1;
        int w2 = 0; while (w2 < 300 && GA.leaving_uid != gu) { d_step(1); w2++; }
        DCHECK(GA.leaving_uid == gu, "7h: the gift was never taken (A home %d givable %d n %d, asking %08x wait %08x said %d/%08x, B ask %d n_arr %d, doors %d/%d, newborn %d)", D_HOME(&DA, gu), D_HOME(&DA, gu) >= 0 && progression_fish_givable(&DA, D_HOME(&DA, gu)), DA.n_fish, GA.asking_uid, GA.wait_uid, GA.said_reason, GA.said_uid, GB.ask.on, GB.n_arr, DA.door_open[1], DB.door_open[0], progression_newborn());
        d_deaf[0] = true;                                                   /* B goes quiet: A hears nothing */
        int kept = -1;
        for (int q = 0; q < 300 && kept < 0; q++) {
            d_step(1);
            DCHECK(D_HOME(&DA, gu) >= 0 || gift_given_has(&GA, gu), "7h: the gift lives nowhere and no note holds it");
            if (!GA.leaving_uid) kept = q;
        }
        DCHECK(kept >= 0 && D_HOME(&DA, gu) >= 0 && !gift_given_has(&GA, gu) && D_HOME(&DB, gu) < 0, "7h: A did not keep its gift when B went quiet (kept %d, A %d, note %d)", kept, D_HOME(&DA, gu), gift_given_has(&GA, gu));
        { int h = D_HOME(&DA, gu);                  /* the giver's card says why (GIVE's one "no", Strato's words 11-02) */
          if (h >= 0) { static uint16_t kfb[TANK_W * TANK_H];
              render_milestones_show_fish(&DA, h); render_milestones(&DA, kfb, TANK_W);
              DCHECK(!strcmp(render_milestones_line(), "CANNOT BE GIVEN RIGHT NOW"), "7h: the kept gift's card said \"%s\"", render_milestones_line());
              d_shot(kfb, "7_kept");
              render_milestones_leave(); } }
        d_deaf[0] = false;
        int cleared = -1;
        for (int q = 0; q < 300 && cleared < 0; q++) { d_step(1); if (!GB.n_arr) cleared = q; DCHECK(D_HOME(&DB, gu) < 0, "7h: the kept gift came alive in B"); }
        DCHECK(cleared >= 0 && D_HOME(&DA, gu) >= 0 && D_HOME(&DB, gu) < 0, "7h: B's ARRIVING note never cleared");
        if (kept >= 0 && cleared >= 0) printf("selftest-doors: 7h. the receiver went quiet as the gift swam out: kept by A %.1f s later (B silent %.1f s), B's note gone %.1f s after it was heard again\n",
                                              kept / 30.0, GIFT_HEARD_MS / 1000.0, cleared / 30.0); }
      /* 7h2. the receiver goes quiet just as the gift reaches the door: it leaves only with B heard in the last
         GIFT_COMMIT_MS - it waits beyond the glass, and once B has been silent GIFT_HEARD_MS it is kept */
      { int gi = d_wait_home(&DA, gu);
        DCHECK(gi >= 0, "7h2: the fish never came home");
        if (gi >= 0) { DA.fish[gi].x = tank_glass_x1(DOOR_Y) - 70; DA.fish[gi].y = DOOR_Y; DA.fish[gi].door_refused = DA.clock + 60; }   /* (no visit of its own meanwhile) */
        GA.want_uid = gu; d_step(10); GB.ask.answer = 1;
        int w5 = 0; while (w5 < 300 && GA.leaving_uid != gu) { d_step(1); w5++; }
        DCHECK(GA.leaving_uid == gu, "7h2: the gift was never taken");
        int far = -1;                                 /* B quiet once the fish is a body from the glass */
        for (int q = 0; q < 300 && far < 0 && GA.leaving_uid; q++) { d_step(1); int h = D_HOME(&DA, gu); if (h >= 0 && DA.fish[h].x > tank_glass_x1(DOOR_Y) - 30) far = q;
            if (getenv("GIVE_TRACE") && h >= 0 && q % 5 == 0) printf("  7h2 t %.2f x %.0f glass %.0f door %d leaving %08x away %d open %d/%d speed %.1f\n", q / 30.0, DA.fish[h].x, tank_glass_x1(DOOR_Y), DA.fish[h].door, GA.leaving_uid, DA.fish[h].away, DA.door_open[1], DB.door_open[0], DA.fish[h].speed); }
        if (getenv("GIVE_TRACE")) printf("  7h2 w5 %d leaving %08x A %d note %d\n", w5, GA.leaving_uid, D_HOME(&DA, gu), gift_given_has(&GA, gu));
        d_deaf[0] = true;
        int kept = -1;
        for (int q = 0; q < 300 && kept < 0; q++) {
            d_step(1);
            DCHECK(D_HOME(&DA, gu) >= 0 && !gift_given_has(&GA, gu), "7h2: the gift left with B silent (A %d, note %d)", D_HOME(&DA, gu), gift_given_has(&GA, gu));
            if (!GA.leaving_uid) kept = q;
        }
        d_deaf[0] = false;
        for (int q = 0; q < 300 && GB.n_arr; q++) d_step(1);
        DCHECK(far >= 0 && kept >= 0 && D_HOME(&DA, gu) >= 0 && D_HOME(&DB, gu) < 0 && !GB.n_arr, "7h2: not kept at the door (far %d kept %d, A %d, B arr %d)", far, kept, D_HOME(&DA, gu), GB.n_arr);
        if (kept >= 0) printf("selftest-doors: 7h2. the receiver went quiet as the gift reached the door: it waited, never left, kept %.1f s later\n", kept / 30.0); }
      /* 7i. the receiver SLEEPS (its PWR key: the goodbye closes the door) while the gift swims out: kept at once; the
         next link clears B's note */
      { int gi = d_wait_home(&DA, gu);
        DCHECK(gi >= 0, "7i: the fish never came home");
        if (gi >= 0) { DA.fish[gi].x = TANK_W * 0.3f; DA.fish[gi].y = DOOR_Y; DA.fish[gi].door_refused = DA.clock + 60; }
        GA.want_uid = gu; d_step(10); GB.ask.answer = 1;
        int w3 = 0; while (w3 < 300 && GA.leaving_uid != gu) { d_step(1); w3++; }
        DCHECK(GA.leaving_uid == gu, "7i: the gift was never taken");
        link_sleep(LB); d_step(3);
        DCHECK(!GA.leaving_uid && D_HOME(&DA, gu) >= 0 && !gift_given_has(&GA, gu) && D_HOME(&DB, gu) < 0, "7i: the gift left for a tank going to sleep (leaving %08x, A %d)", GA.leaving_uid, D_HOME(&DA, gu));
        d_step(60);
        link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT);
        int cleared = -1;
        for (int q = 0; q < 600 && cleared < 0; q++) { d_step(1); if (!GB.n_arr) cleared = q; DCHECK(D_HOME(&DB, gu) < 0, "7i: the kept gift came alive in B"); }
        DCHECK(cleared >= 0 && D_HOME(&DA, gu) >= 0 && DA.door_open[LINK_RIGHT], "7i: B's ARRIVING note did not clear at the relink (doors %d)", DA.door_open[LINK_RIGHT]);
        if (cleared >= 0) printf("selftest-doors: 7i. the receiver went to sleep as the gift swam out: A kept it at once; at the relink B's note went - one fish, in A\n"); }
      /* 7j. given where it is, and the receiver sleeps during the GIVEN stamp: B keeps its fish - the sleep poofs it
         over there and it comes home from bubbles; no note anywhere once they link again */
      { while (DB.n_fish < 4) { tank_make_fish(&DB, DB.n_fish, DB.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); tank_ensure_ids(&DB); DB.n_fish++; }
        while (DA.n_fish > 3) tank_remove_fish(&DA, DA.n_fish - 1);
        if (!(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT])) { link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); d_step(60);
            link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400); }
        for (int q = 0, quiet = 0; q < 3600 && quiet < 90; q++) {   /* (A's places free first, and B's other fish stay home: two of them
                                                                       wandering over filled A's two places - 0.4.1) */
            int in = 0; for (int k = 0; k < N_VISIT_MAX; k++) if (DA.vis_used[k]) { DA.fish[N_FISH_MAX + k].homeward = DA.fish[N_FISH_MAX + k].bolt_home = true; in++; }
            for (int i = 0; i < DB.n_fish; i++) { fish_t *f = &DB.fish[i]; f->door_seen = DB.clock + 3600;
                if (f->away) in++; else if (f->door == DOOR_HEADING) f->door = DOOR_NONE; else if (f->door != DOOR_NONE) in++; }
            quiet = in ? 0 : quiet + 1;
            d_step(1);
        }
        int vi = -1; uint32_t vu = 0;
        for (int i = DB.n_fish - 1; i >= 0 && vi < 0; i--) if (!DB.fish[i].away && DB.fish[i].door == DOOR_NONE && progression_fish_givable(&DB, i)) vi = i;
        if (vi >= 0) { vu = DB.fish[vi].uid; d_head_for(&DB, vi, LINK_LEFT); }
        for (int q = 0; q < 1800 && vu && d_vis_of(&DA, vu) < 0; q++) { d_step(1); int h = D_HOME(&DB, vu); if (h >= 0 && !DB.fish[h].away && DB.fish[h].door == DOOR_NONE) d_head_for(&DB, h, LINK_LEFT); }
        DCHECK(vu && d_vis_of(&DA, vu) >= 0, "7j: B's fish never reached A (A room %d, %d visitors, %d swimming; B's fish door %d refused %.0f s)", tank_link_room(&DA),
               DA.vis_used[0] + DA.vis_used[1] + DA.vis_used[2] + DA.vis_used[3], DA.vis_used[0] + DA.vis_used[1] + DA.vis_used[2] + DA.vis_used[3] + DA.n_fish,
               vi >= 0 ? DB.fish[vi].door : -9, vi >= 0 ? DB.fish[vi].door_refused - DB.clock : 0);
        { int h = D_HOME(&DB, vu);   /* next door: not for sale (it swims on over there), but it can be given where it is */
          DCHECK(h >= 0 && DB.fish[h].away && !progression_fish_sellable(&DB, h) && progression_fish_givable(&DB, h), "7j: a fish next door was for sale (or could not be given)");
          /* its card: SELL dim, and the line a tap on it brings (every world: the words) */
          if (h >= 0) { static uint16_t sfb[TANK_W * TANK_H]; int sx, sy;
              render_milestones_show_fish(&DB, h); render_milestones(&DB, sfb, TANK_W);
              if (render_milestones_card(&DB, NULL, NULL, &sx, &sy)) { render_milestones_tap(&DB, (float)sx, (float)sy); render_milestones(&DB, sfb, TANK_W); }
              DCHECK(!strcmp(render_milestones_line(), "VISITING NEXT DOOR"), "7j: a tap on the dim SELL of a fish next door said \"%s\"", render_milestones_line());
              d_shot(sfb, "6_away_sell");
              render_milestones_leave(); } }
        for (int q = 0; q < 60; q++) d_step(1);
        GB.want_uid = vu; d_step(10); GA.ask.answer = 1;
        int w4 = 0; while (w4 < 120 && GB.leaving_uid != vu) { d_step(1); w4++; }
        DCHECK(GB.leaving_uid == vu && GB.leaving_here, "7j: the gift of a visiting fish was not taken (leaving %08x here %d)", GB.leaving_uid, GB.leaving_here);
        d_step(20);                                                          /* (into the stamp) */
        link_sleep(LA);
        int home = -1;
        for (int q = 0; q < 900 && home < 0; q++) {
            d_step(1);
            int hb = D_HOME(&DB, vu);
            DCHECK(!(hb >= 0 && !DB.fish[hb].away && (D_HOME(&DA, vu) >= 0 || d_vis_of(&DA, vu) >= 0)), "7j: the fish swims in both tanks");
            DCHECK(hb >= 0 || D_HOME(&DA, vu) >= 0 || gift_given_has(&GB, vu), "7j: the fish lives nowhere and no note holds it");
            if (hb >= 0 && !DB.fish[hb].away) home = q;
        }
        DCHECK(home >= 0 && D_HOME(&DA, vu) < 0 && d_vis_of(&DA, vu) < 0 && !gift_given_has(&GB, vu), "7j: B did not keep its fish (home %d, A %d)", home, D_HOME(&DA, vu));
        d_step(60);
        link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT);
        int cleared = -1;
        for (int q = 0; q < 600 && cleared < 0; q++) { d_step(1); if (!GA.n_arr && !GB.n_given) cleared = q; }
        DCHECK(cleared >= 0 && D_HOME(&DB, vu) >= 0 && D_HOME(&DA, vu) < 0, "7j: the notes did not settle (A arriving %d, B given %d)", GA.n_arr, GB.n_given);
        if (home >= 0 && cleared >= 0) printf("selftest-doors: 7j. given where it is, the receiver slept during the stamp: B kept it - home from bubbles %.1f s later, A's note gone at the relink\n", home / 30.0); }
      /* 7e. GIVEN WHERE IT IS (2026-11-01, Strato: a fish that swam off on its own must not stop the gift): one of B's
         visiting A, given to A - it stays just where it swims, A's from then on, never in both and never in neither */
      { while (DB.n_fish < 4) { tank_make_fish(&DB, DB.n_fish, DB.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); tank_ensure_ids(&DB); DB.n_fish++; }
        while (DA.n_fish > 3) tank_remove_fish(&DA, DA.n_fish - 1);
        if (!(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT])) { link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); d_step(60);
            link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400); }
        DCHECK(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT], "7e: the doors did not open");
        int vi = -1; uint32_t vu = 0;
        for (int i = DB.n_fish - 1; i >= 0 && vi < 0; i--) if (!DB.fish[i].away && DB.fish[i].door == DOOR_NONE) vi = i;
        DCHECK(vi >= 0, "7e: no fish of B's at home to send next door");
        if (getenv("GIVE_TRACE")) printf("DBG 7e: doors A %d/%d B %d/%d, A n %d vis %d %d %d, B n %d, room A %d, vi %d\n", DA.door_open[0], DA.door_open[1], DB.door_open[0], DB.door_open[1], DA.n_fish, DA.vis_used[0], DA.vis_used[1], DA.vis_used[2], DB.n_fish, tank_link_room(&DA), vi);
        if (vi >= 0) { vu = DB.fish[vi].uid; d_head_for(&DB, vi, LINK_LEFT); }
        for (int q = 0; q < 1800 && vu && d_vis_of(&DA, vu) < 0; q++) { d_step(1); int h = D_HOME(&DB, vu); if (h >= 0 && !DB.fish[h].away && DB.fish[h].door == DOOR_NONE) d_head_for(&DB, h, LINK_LEFT); }
        DCHECK(vu && d_vis_of(&DA, vu) >= 0, "7e: B's fish never reached A");
        for (int q = 0; q < 60; q++) d_step(1);                              /* (in, past the doorway) */
        GB.want_uid = vu; d_step(10);
        DCHECK(GB.asking_uid == vu && GA.ask.on && GA.ask.uid == vu, "7e: the gift of a fish next door was not asked (asking %08x, ask %d)", GB.asking_uid, GA.ask.on);
        GA.ask.answer = 1;
        float vx = 0, vy = 0; int took = -1;
        for (int q = 0; q < 300 && took < 0; q++) {
            int s0 = d_vis_of(&DA, vu); if (s0 >= 0) { vx = DA.fish[s0].x; vy = DA.fish[s0].y; }
            d_step(1);
            int r = D_HOME(&DA, vu);
            DCHECK(!(r >= 0 && d_vis_of(&DA, vu) >= 0), "7e: the gift is A's and A's visitor at once");
            if (r >= 0) { took = q; DCHECK(fabsf(DA.fish[r].x - vx) < 20 && fabsf(DA.fish[r].y - vy) < 20, "7e: the gift moved (%.0f,%.0f from %.0f,%.0f)", DA.fish[r].x, DA.fish[r].y, vx, vy); }
        }
        DCHECK(took >= 0, "7e: the visitor never became A's");
        int gone = -1;
        for (int q = 0; q < 600 && (gone < 0 || GA.n_arr || GB.n_given); q++) {
            d_step(1);
            bool inA = D_HOME(&DA, vu) >= 0 || d_vis_of(&DA, vu) >= 0;
            int hb = D_HOME(&DB, vu); bool inB = hb >= 0 && !DB.fish[hb].away;
            DCHECK(!(inA && inB), "7e: the gift swims in both tanks");
            DCHECK(inA || inB || gift_given_has(&GB, vu), "7e: the gift lives nowhere and no note holds it");
            if (gone < 0 && hb < 0) gone = q;
        }
        DCHECK(gone >= 0 && D_HOME(&DA, vu) >= 0 && d_vis_of(&DA, vu) < 0 && !GA.n_arr && !GB.n_given, "7e: the gift did not settle (B %d, A home %d, notes %d/%d)", D_HOME(&DB, vu), D_HOME(&DA, vu), GA.n_arr, GB.n_given);
        if (took >= 0 && gone >= 0) printf("selftest-doors: 7e. GIVEN WHERE IT IS: one of B's visiting A became A's on the spot %.1f s after ACCEPT; gone from B's roster %.1f s later, notes settled\n", took / 30.0, gone / 30.0);
      }
      /* 7f. GIVE tapped while the fish crosses a doorway: no refusal - it waits, and asks once the fish is home or over there */
      { int wi = -1; uint32_t wu = 0;
        for (int i = DB.n_fish - 1; i >= 0 && wi < 0; i--) if (!DB.fish[i].away && DB.fish[i].door == DOOR_NONE && progression_fish_givable(&DB, i)) wi = i;
        DCHECK(wi >= 0, "7f: no fish of B's to give");
        if (wi >= 0) {
            wu = DB.fish[wi].uid; d_at_door_facing_in(&DB, wi, LINK_LEFT);
            int at_door = -1;
            for (int q = 0; q < 300 && at_door < 0; q++) { d_step(1); int h = D_HOME(&DB, wu); if (h >= 0 && (DB.fish[h].door == DOOR_ASKED || DB.fish[h].door == DOOR_PASSING || DB.fish[h].away)) at_door = q; }
            DCHECK(at_door >= 0, "7f: the fish never reached the doorway");
            GB.want_uid = wu; d_step(2);
            DCHECK(GB.wait_uid == wu || GB.asking_uid == wu, "7f: GIVE in a doorway was not held (wait %08x, asking %08x)", GB.wait_uid, GB.asking_uid);
            int asked = -1;
            for (int q = 0; q < (int)(GIFT_WAIT_S * 30) && asked < 0; q++) { d_step(1); if (GA.ask.on && GA.ask.uid == wu) asked = q;
                if (getenv("GIVE_TRACE") && q % 15 == 0) { int h = D_HOME(&DB, wu); if (h >= 0) printf("  7f t %.1f door %d away %d x %.0f vis-in-A %d ask %d/%08x\n", q / 30.0, DB.fish[h].door, DB.fish[h].away, DB.fish[h].x, d_vis_of(&DA, wu), GA.ask.on, GA.ask.uid); } }
            DCHECK(asked >= 0, "7f: the gift waited and never asked (wait %08x asking %08x leaving %08x said %d/%08x, A ask %d %08x, A n %d arr %d, room %d, doors %d/%d)", GB.wait_uid, GB.asking_uid, GB.leaving_uid, GB.said_reason, GB.said_uid, GA.ask.on, GA.ask.uid, DA.n_fish, GA.n_arr, progression_fry_room(&DA), DA.door_open[1], DB.door_open[0]);
            GA.ask.answer = -1; d_step(15);
            int h = D_HOME(&DB, wu);
            DCHECK(h >= 0 && !GB.wait_uid && !GB.asking_uid, "7f: NO THANKS did not leave the fish B's");
            if (asked >= 0) printf("selftest-doors: 7f. GIVE tapped in a doorway: held, asked %.1f s later once it was %s; NO THANKS kept it B's\n", asked / 30.0, h >= 0 && DB.fish[h].away ? "over there" : "home");
        }
      }
      /* 7g. GIVE from the stat card (0.4): the box under the toolbox (or right of the lock) opens the GIVE modal (the quick
         path) - its GIVE after the hold asks next door; NO THANKS stays on it until OK (past the card line's 6 s); CALL OFF
         takes a second ask back. The door: B's pick is its left (A); a pick of a door with no tank, or of a tank that is not
         the one there, cannot go - and says so */
      { int si = -1;
        while (progression_newborn() >= 0) progression_newborn_done(&DA);   /* (a welcome owed holds every gift: not this test's) */
        if (DB.n_fish < N_FISH_MAX) {                                    /* a fish of B's at home for its card (on the watch's narrow floor the others
                                                                            were often next door or in a doorway by now) */
            si = DB.n_fish; tank_make_fish(&DB, si, si % 3, 0.5f, 0.5f, STAGE_ADULT); DB.n_fish++; tank_ensure_ids(&DB);
            DB.fish[si].x = TANK_W * 0.5f; DB.fish[si].y = TANK_BOT * 0.5f; DB.fish[si].door = DOOR_NONE; d_step(2);
            if (!(progression_fish_givable(&DB, si) && !DB.fish[si].away && (DB.fish[si].door == DOOR_NONE || DB.fish[si].door == DOOR_HEADING))) {
                printf("7g: B's new fish: givable %d away %d door %d\n", progression_fish_givable(&DB, si), DB.fish[si].away, DB.fish[si].door); si = -1; }
        }
        DCHECK(si >= 0, "7g: no fish of B's to give from its card (%d fish, newborn %d)", DB.n_fish, progression_newborn());
        if (si >= 0) {
            uint32_t su = DB.fish[si].uid; int gx = -100, gy = -100, gw = 0, gh = 0; render_tool_tile(&DB, si, RENDER_TOOLS_GIVE, &gx, &gy, &gw, &gh);
            const float bx = gx + gw / 2.0f, by = gy + gh / 2.0f;   /* GIVE's tile in the toolbox (0.4.2) */
            DB.fish[si].door_refused = DB.clock + 120;                      /* (home for this test: no visit of its own) */
            DCHECK(gw > 0 && render_give_hit(&DB, si, bx, by) && render_tools_hit(&DB, si, bx, by) < 0, "7g: no GIVE tile in a linked tank's toolbox (or a tool answered it)");
            static uint16_t gfb[TANK_W * TANK_H];
            render_give_tap(&DB, si);
            DCHECK(render_give_modal_step(&DB) == RENDER_GM_CONFIRM, "7g: the stat card's GIVE did not open the modal (step %d)", render_give_modal_step(&DB));
            render_tank(&DB, gfb, TANK_W); render_tank(&DB, gfb, TANK_W); render_stats_card(&DB, si, gfb, TANK_W); render_give_modal(&DB, gfb, TANK_W); d_shot(gfb, "g1_confirm_stat");
            int mx = 0, my = 0, ox = 0, oy = 0;
            DCHECK(render_give_modal_button(&DB, RENDER_GM_TAP_GIVE, &mx, &my), "7g: no GIVE in the modal");
            DCHECK(render_give_modal_tap(&DB, (float)mx, (float)my) == RENDER_GM_TAP_HELD && !GB.want_uid, "7g: the modal's GIVE took a tap the moment it came up");
            d_step((int)(GIFT_HOLD_S * 30) + 2);
            DCHECK(!GB.asking_uid && !GB.wait_uid && !GB.want_uid, "7g: the box's one tap gave the fish (the modal asks first)");
            DCHECK(render_give_modal_tap(&DB, (float)mx, (float)my) == RENDER_GM_TAP_GIVE, "7g: the modal's GIVE did not take its tap after the hold");
            d_step(10);
            DCHECK(GB.asking_uid == su && GA.ask.on && GA.ask.uid == su && render_give_modal_step(&DB) == RENDER_GM_ASKING, "7g: the modal's GIVE did not ask next door");
            render_tank(&DB, gfb, TANK_W); render_tank(&DB, gfb, TANK_W); render_stats_card(&DB, D_HOME(&DB, su), gfb, TANK_W); render_give_modal(&DB, gfb, TANK_W); d_shot(gfb, "g4_asking");
            DCHECK(!d_bake_diff(&DB, false), "7g: ASKING (its bar live) from its bake is not it drawn whole");
            GA.ask.answer = -1; d_step(15);
            DCHECK(D_HOME(&DB, su) >= 0 && !GB.asking_uid, "7g: NO THANKS did not keep it B's");
            DCHECK(render_give_modal_step(&DB) == RENDER_GM_SAID && !strcmp(render_give_modal_line(&DB), "THEY SAID NO THANKS"), "7g: a NO was silent (step %d, \"%s\")", render_give_modal_step(&DB), render_give_modal_line(&DB));
            render_tank(&DB, gfb, TANK_W); render_tank(&DB, gfb, TANK_W); render_give_modal(&DB, gfb, TANK_W); d_shot(gfb, "g5_no_thanks");
            d_step((int)(GIFT_SAID_S * 30) + 30);
            DCHECK(render_give_modal_step(&DB) == RENDER_GM_SAID, "7g: the NO went before OK");
            DCHECK(render_give_modal_button(&DB, RENDER_GM_TAP_OK, &ox, &oy) && render_give_modal_tap(&DB, (float)ox, (float)oy) == RENDER_GM_TAP_OK
                   && render_give_modal_step(&DB) == RENDER_GM_OFF, "7g: OK did not close the modal");
            /* CALL OFF: asked again, taken back */
            d_step(30); render_give_tap(&DB, D_HOME(&DB, su)); d_step((int)(GIFT_HOLD_S * 30) + 2);
            render_give_modal_button(&DB, RENDER_GM_TAP_GIVE, &mx, &my); render_give_modal_tap(&DB, (float)mx, (float)my); d_step(10);
            DCHECK(GB.asking_uid == su && GA.ask.on, "7g: the second ask did not go");
            DCHECK(render_give_modal_button(&DB, RENDER_GM_TAP_CALLOFF, &ox, &oy) && render_give_modal_tap(&DB, (float)ox, (float)oy) == RENDER_GM_TAP_CALLOFF
                   && render_give_modal_step(&DB) == RENDER_GM_OFF, "7g: CALL OFF 0.3 s after ASKING came up did not close the modal (THE TOUCH RULE: no hold)");
            d_step(15);
            DCHECK(!GB.asking_uid && !GA.ask.on && D_HOME(&DB, su) >= 0, "7g: CALL OFF did not take the gift back (asking %08x, A asked %d)", GB.asking_uid, GA.ask.on);
            /* the door is the keeper's pick: no tank at B's right, and a tank at its left that is not the one picked - it cannot go */
            d_step(30);
            GB.want_uid = su; GB.want_door = GIFT_DOOR_RIGHT; GB.want_peer = 0; d_step(5);
            DCHECK(!GB.asking_uid && !GA.ask.on && GB.said_uid == su && GB.said_reason == LINK_GIVE_NO_OTHER, "7g: a gift through a door with no tank went (asking %08x said %08x/%d)", GB.asking_uid, GB.said_uid, GB.said_reason);
            d_step(5);
            GB.want_uid = su; GB.want_door = GIFT_DOOR_LEFT; GB.want_peer = DA.uid ^ 1u; d_step(5);
            DCHECK(!GB.asking_uid && !GA.ask.on && GB.said_uid == su, "7g: a gift to a tank that is not there went");
            d_step(5);
            GB.want_uid = su; GB.want_door = GIFT_DOOR_LEFT; GB.want_peer = DA.uid; d_step(10);
            DCHECK(GB.asking_uid == su && GA.ask.on, "7g: the pick of B's left door, A, did not ask A");
            GB.cancel_uid = su; d_step(15);
            static tank_t U; memset(&U, 0, sizeof U); tank_init(&U, 41); tank_new_population(&U);   /* (unlinked: no box) */
            DCHECK(!render_give_hit(&U, 0, bx, by), "7g: a GIVE box on an unlinked tank's card");
            printf("selftest-doors: 7g. GIVE from the stat card's box: the modal, its hold, asked next door; NO stays until OK; CALL OFF; the door picked or none; no box unlinked\n");
        }
      }
      /* 7k. the GIVE modal on its own (a tank between two doors, faked: the harness links two): both doors open - the
         pick (each row its tank, the hold, a closed door's row dim and no tap, the pick is the yes through that door to
         that tank); one door - CONFIRM to it, and its door closing turns it to CANNOT BE GIVEN RIGHT NOW; both rows
         closing too; the tank the keeper picked answered FULL - until OK; a dim GIVE (the last two) says why, from the
         keeper card's path as well */
      { static tank_t U; static gift_book_t UG; static uint16_t ufb[TANK_W * TANK_H];
        memset(&U, 0, sizeof U); memset(&UG, 0, sizeof UG); tank_init(&U, 77); tank_new_population(&U);
        while (U.n_fish < 4) { tank_make_fish(&U, U.n_fish, U.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); U.n_fish++; }
        tank_ensure_ids(&U); gift_attach(&U, &UG);
        while (progression_newborn() >= 0) progression_newborn_done(&U);
        const uint32_t PL = 0x49439648u, PR = 0xf63b8f6bu;
        U.door_open[LINK_LEFT] = U.door_open[LINK_RIGHT] = true; UG.door_peer[LINK_LEFT] = PL; UG.door_peer[LINK_RIGHT] = PR;
        UG.door_badge[LINK_LEFT] = 1; UG.door_badge[LINK_RIGHT] = 2;
        int f0 = 0; for (int i = 0; i < U.n_fish; i++) if (progression_fish_givable(&U, i)) { f0 = i; break; }
        uint32_t uu = U.fish[f0].uid; int x = 0, y = 0;
        { int gx = -100, gy = -100, gw = 0, gh = 0; render_tool_tile(&U, f0, RENDER_TOOLS_GIVE, &gx, &gy, &gw, &gh);
          DCHECK(gw > 0 && render_give_hit(&U, f0, gx + gw / 2.0f, gy + gh / 2.0f), "7k: no GIVE tile between two doors"); }
        render_give_tap(&U, f0);
        DCHECK(render_give_modal_step(&U) == RENDER_GM_PICK, "7k: both doors open and no pick (step %d)", render_give_modal_step(&U));
        render_tank(&U, ufb, TANK_W); render_tank(&U, ufb, TANK_W); render_stats_card(&U, f0, ufb, TANK_W); render_give_modal(&U, ufb, TANK_W); d_shot(ufb, "g3_pick");
        DCHECK(!d_bake_diff(&U, false), "7k: the pick from its bake is not the pick drawn whole");
        DCHECK(render_give_modal_button(&U, RENDER_GM_TAP_LEFT, &x, &y) && render_give_modal_tap(&U, (float)x, (float)y) == RENDER_GM_TAP_HELD && !UG.want_uid,
               "7k: the pick took a tap the moment it came up");
        U.clock += GIFT_HOLD_S + 0.1f;
        U.door_open[LINK_RIGHT] = false;                                       /* the right tank goes while the pick is up */
        render_tank(&U, ufb, TANK_W); render_tank(&U, ufb, TANK_W); render_give_modal(&U, ufb, TANK_W); d_shot(ufb, "g8_pick_closed");
        DCHECK(!d_bake_diff(&U, false), "7k: the pick with a closed row from its bake is not it drawn whole");
        DCHECK(render_give_modal_button(&U, RENDER_GM_TAP_RIGHT, &x, &y) && render_give_modal_tap(&U, (float)x, (float)y) == RENDER_GM_TAP_CARD && !UG.want_uid
               && render_give_modal_step(&U) == RENDER_GM_PICK, "7k: a closed door's row took the gift");
        U.door_open[LINK_RIGHT] = true; UG.door_peer[LINK_RIGHT] = PR ^ 7u;     /* ... and a different tank there now: still not the one shown */
        DCHECK(render_give_modal_tap(&U, (float)x, (float)y) == RENDER_GM_TAP_CARD && !UG.want_uid, "7k: a row went to a tank that is not the one it named");
        render_give_modal_button(&U, RENDER_GM_TAP_LEFT, &x, &y);
        DCHECK(render_give_modal_tap(&U, (float)x, (float)y) == RENDER_GM_TAP_LEFT && UG.want_uid == uu && UG.want_door == GIFT_DOOR_LEFT && UG.want_peer == PL
               && render_give_modal_step(&U) == RENDER_GM_ASKING, "7k: the left row did not give through the left door to its tank (want %08x door %d peer %08x)", UG.want_uid, UG.want_door, UG.want_peer);
        UG.want_uid = 0; U.clock += 0.5f; UG.said_uid = uu; UG.said_reason = LINK_GIVE_NO_FULL; UG.said_until = U.clock + GIFT_SAID_S;   /* (the engine's answer) */
        DCHECK(render_give_modal_step(&U) == RENDER_GM_SAID && !strcmp(render_give_modal_line(&U), "THEIR TANK IS FULL"), "7k: FULL was silent (\"%s\")", render_give_modal_line(&U));
        render_tank(&U, ufb, TANK_W); render_tank(&U, ufb, TANK_W); render_give_modal(&U, ufb, TANK_W); d_shot(ufb, "g6_full");
        DCHECK(!d_bake_diff(&U, false), "7k: FULL from its bake is not FULL drawn whole");
        U.clock += GIFT_SAID_S + 1;
        DCHECK(render_give_modal_step(&U) == RENDER_GM_SAID && render_give_modal_button(&U, RENDER_GM_TAP_OK, &x, &y) && render_give_modal_tap(&U, (float)x, (float)y) == RENDER_GM_TAP_OK
               && render_give_modal_step(&U) == RENDER_GM_OFF, "7k: FULL did not stay until OK");
        /* both rows closing under the pick: it cannot go */
        UG.door_peer[LINK_RIGHT] = PR; render_give_tap(&U, f0);
        U.door_open[LINK_LEFT] = U.door_open[LINK_RIGHT] = false;
        DCHECK(render_give_modal_step(&U) == RENDER_GM_SAID && !strcmp(render_give_modal_line(&U), "CANNOT BE GIVEN RIGHT NOW"), "7k: both doors closed under the pick, and it did not say so");
        U.clock += GIFT_HOLD_S + 0.1f; render_give_modal_button(&U, RENDER_GM_TAP_OK, &x, &y); render_give_modal_tap(&U, (float)x, (float)y);
        /* one door: CONFIRM to it; the door closes while it is up */
        U.door_open[LINK_RIGHT] = true; render_give_tap(&U, f0);
        DCHECK(render_give_modal_step(&U) == RENDER_GM_CONFIRM, "7k: one door open and no CONFIRM (step %d)", render_give_modal_step(&U));
        U.clock += GIFT_HOLD_S + 0.1f; U.door_open[LINK_RIGHT] = false;
        DCHECK(render_give_modal_step(&U) == RENDER_GM_SAID && !strcmp(render_give_modal_line(&U), "CANNOT BE GIVEN RIGHT NOW") && !UG.want_uid,
               "7k: its door closed under CONFIRM, and it did not say so");
        render_tap_press(U.clock - 0.05);                    /* (THE TOUCH RULE: the words came by themselves - only a press from before them is spent) */
        DCHECK(render_give_modal_button(&U, RENDER_GM_TAP_OK, &x, &y) && render_give_modal_tap(&U, (float)x, (float)y) == RENDER_GM_TAP_HELD, "7k: OK took a press that began before the words came up");
        render_tap_press_end();
        render_tank(&U, ufb, TANK_W); render_tank(&U, ufb, TANK_W); render_give_modal(&U, ufb, TANK_W); d_shot(ufb, "g9_cannot");
        DCHECK(!d_bake_diff(&U, false), "7k: CANNOT from its bake is not CANNOT drawn whole");
        U.clock += GIFT_HOLD_S + 0.1f; render_give_modal_tap(&U, (float)x, (float)y);
        DCHECK(render_give_modal_step(&U) == RENDER_GM_OFF, "7k: OK did not close CANNOT");
        /* a dim GIVE (the last two): why - from the keeper card's GIVE too */
        U.door_open[LINK_RIGHT] = true;
        while (U.n_fish > FISH_KEEP_MIN) tank_remove_fish(&U, U.n_fish - 1);
        render_milestones_show_fish(&U, 0); render_milestones(&U, ufb, TANK_W);
        int gx = 0, gy = 0;
        DCHECK(render_milestones_give(&U, &gx, &gy), "7k: no GIVE on the card of one of the last two");
        render_milestones_tap(&U, (float)gx, (float)gy); render_milestones(&U, ufb, TANK_W); d_shot(ufb, "g7_why");
        DCHECK(!d_bake_diff(&U, false), "7k: WHY from its bake is not WHY drawn whole");
        DCHECK(render_give_modal_step(&U) == RENDER_GM_WHY && !strcmp(render_give_modal_line(&U), "KEEP AT LEAST TWO FISH"), "7k: a dim GIVE did not say why (step %d, \"%s\")", render_give_modal_step(&U), render_give_modal_line(&U));
        U.clock += GIFT_HOLD_S + 0.1f; render_give_modal_button(&U, RENDER_GM_TAP_OK, &x, &y); render_milestones_tap(&U, (float)x, (float)y);
        DCHECK(render_give_modal_step(&U) == RENDER_GM_OFF, "7k: OK did not close WHY");
        render_milestones_leave();
        /* the keeper card's path to the modal, rendered (one door) */
        while (U.n_fish < 4) { tank_make_fish(&U, U.n_fish, U.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); U.n_fish++; } tank_ensure_ids(&U);
        render_milestones_show_fish(&U, f0); render_milestones(&U, ufb, TANK_W);
        render_milestones_give(&U, &gx, &gy); render_milestones_tap(&U, (float)gx, (float)gy); render_milestones(&U, ufb, TANK_W); d_shot(ufb, "g2_confirm_page");
        DCHECK(!d_bake_diff(&U, false), "7k: CONFIRM from its bake is not CONFIRM drawn whole");
        DCHECK(!d_bake_lent(&U), "7k: CONFIRM from a lent bake (THE CARDS LENT, 0.4.2)");
        DCHECK(render_give_modal_step(&U) == RENDER_GM_CONFIRM, "7k: the keeper card's GIVE did not open CONFIRM");
        render_give_modal_button(&U, RENDER_GM_TAP_BACK, &x, &y);
        DCHECK(render_milestones_tap(&U, (float)x, (float)y) == MS_TAP_KEPT && render_give_modal_step(&U) == RENDER_GM_OFF, "7k: BACK (in the hold, too) did not close it");
        render_milestones_leave(); gift_attach(&U, NULL);
        printf("selftest-doors: 7k. the GIVE modal: the pick (hold, a closed door's row dim, a row's tank changed), FULL until OK, doors closing -> CANNOT, a dim GIVE says why, BACK always\n"); }
      /* 7l. KEEP-TWO WHILE A GIFT IS OUT (the 11-02 sweep): B has 3 fish. F asked next door, then accepted and swimming
         out: neither of the other two may be sold or given meanwhile (it was: one SELL and ONE fish left). Then a fish lost
         while one is asked (forced here): the accept falls through, it stays - the last pair never goes */
      { while (progression_newborn() >= 0) progression_newborn_done(&DB);
        while (DB.n_fish > 3) tank_remove_fish(&DB, DB.n_fish - 1);
        while (DB.n_fish < 3) { tank_make_fish(&DB, DB.n_fish, DB.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); DB.n_fish++; }
        tank_ensure_ids(&DB);
        if (!(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT])) { link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); d_step(60);
            link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400); }
        uint32_t fu = DB.fish[0].uid;
        int fi = d_wait_home(&DB, fu);
        for (int k = 0; k < DB.n_fish; k++) d_wait_home(&DB, DB.fish[k].uid);
        fi = D_HOME(&DB, fu);
        DCHECK(fi >= 0 && DB.n_fish == 3, "7l: B is not three fish at home (%d, F %d)", DB.n_fish, fi);
        if (fi >= 0) {
            DB.fish[fi].x = tank_glass_x1(DOOR_Y) - 30; DB.fish[fi].y = DOOR_Y;   /* (at the far glass: a swim of several seconds) */
            int gi = fi == 0 ? 1 : 0;
            DCHECK(progression_fish_givable(&DB, gi) && progression_fish_sellable(&DB, gi), "7l: before the gift the others could not go");
            GB.want_uid = fu; GB.want_door = GIFT_DOOR_LEFT; GB.want_peer = DA.uid; d_step(10);
            DCHECK(GB.asking_uid == fu && GA.ask.on, "7l: F was not asked (asking %08x)", GB.asking_uid);
            for (int k = 0; k < DB.n_fish; k++) if (DB.fish[k].uid != fu)
                DCHECK(!progression_fish_givable(&DB, k) && !progression_fish_sellable(&DB, k), "7l: F asked, and %s could still be sold or given", DB.fish[k].name);
            GA.ask.answer = 1;
            int w = 0; while (w < 120 && GB.leaving_uid != fu) { d_step(1); w++; }
            DCHECK(GB.leaving_uid == fu, "7l: F was not taken");
            for (int k = 0; k < DB.n_fish; k++) if (DB.fish[k].uid != fu)
                DCHECK(!progression_fish_givable(&DB, k) && !progression_fish_sellable(&DB, k), "7l: F swimming out, and %s could still be sold or given", DB.fish[k].name);
            for (int q = 0; q < 1200 && (D_HOME(&DB, fu) >= 0 || GB.n_given); q++) d_step(1);
            DCHECK(D_HOME(&DB, fu) < 0 && D_HOME(&DA, fu) >= 0 && DB.n_fish == 2, "7l: F did not arrive (B %d fish)", DB.n_fish);
            /* a fish lost while one is asked: the accept must not take the last pair below two */
            while (!progression_fry_room(&DA) && DA.n_fish > 2)   /* (A's room for a second gift - not this test's; with A at five, the cap,
                                                                     A said FULL: the test counted on a sixth place, 2026-11-03) */
                tank_remove_fish(&DA, DA.fish[DA.n_fish - 1].uid == fu ? DA.n_fish - 2 : DA.n_fish - 1);
            tank_make_fish(&DB, DB.n_fish, 1, 0.5f, 0.5f, STAGE_ADULT); DB.n_fish++; tank_ensure_ids(&DB);
            uint32_t f2 = DB.fish[DB.n_fish - 1].uid;
            int f2i = d_wait_home(&DB, f2);
            if (f2i >= 0) { DB.fish[f2i].x = tank_glass_x1(DOOR_Y) - 30; DB.fish[f2i].y = DOOR_Y; }
            GB.want_uid = f2; GB.want_door = GIFT_DOOR_LEFT; GB.want_peer = DA.uid; d_step(10);
            DCHECK(GB.asking_uid == f2 && GA.ask.on, "7l: the second gift was not asked");
            int lose = D_HOME(&DB, f2) == 0 ? 1 : 0;
            tank_remove_fish(&DB, lose);                                    /* (forced: B is down to two while f2 is asked) */
            GA.ask.answer = 1;
            for (int q = 0; q < 900; q++) d_step(1);
            DCHECK(D_HOME(&DB, f2) >= 0 && D_HOME(&DA, f2) < 0 && DB.n_fish == 2 && !gift_given_has(&GB, f2) && !GA.n_arr,
                   "7l: the accept took B below two (B %d fish, f2 in A %d, note %d, A arriving %d)", DB.n_fish, D_HOME(&DA, f2) >= 0, gift_given_has(&GB, f2), GA.n_arr);
            printf("selftest-doors: 7l. keep-two while a gift is out: the other two could not go while F was asked or swimming; a fish lost mid-ask made the accept fall through\n");
        }
      }
      /* 7m. THE HIDDEN DISCONNECT (the 11-02 sweep): the DRIFTED notice's DISCONNECT answers only where the notice is seen -
         not under a page, a milestone notice, the GIVE modal or the gift card (its hit ran under all of them) */
      { static tank_t U; static gift_book_t UG;
        memset(&U, 0, sizeof U); memset(&UG, 0, sizeof UG); tank_init(&U, 78); tank_new_population(&U);
        while (U.n_fish < 4) { tank_make_fish(&U, U.n_fish, U.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); U.n_fish++; }
        tank_ensure_ids(&U); gift_attach(&U, &UG);
        U.door_open[LINK_LEFT] = true; UG.door_peer[LINK_LEFT] = 0x49439648u;
        while (notice_current()) notice_tick(&U, 1.0f, false);
        DCHECK(render_drift_live(&U, false), "7m: the bare tank: the notice not live");
        DCHECK(!render_drift_live(&U, true), "7m: a page up: the DISCONNECT still answered");
        UG.ask.on = true; DCHECK(!render_drift_live(&U, false), "7m: the gift card up: the DISCONNECT still answered"); UG.ask.on = false;
        render_give_tap(&U, 0); DCHECK(render_give_modal_step(&U) != RENDER_GM_OFF && !render_drift_live(&U, false), "7m: the GIVE modal up: the DISCONNECT still answered");
        render_give_modal_close();
        notice_sync(&U); notice_low_battery(); for (int q = 0; q < 60 && !notice_current(); q++) notice_tick(&U, 0.1f, false);
        DCHECK(notice_current() && !render_drift_live(&U, false), "7m: a notice up: the DISCONNECT still answered");
        while (notice_current()) notice_tick(&U, 1.0f, false);
        DCHECK(render_drift_live(&U, false), "7m: all gone: the notice not live again");
        gift_attach(&U, NULL);
        printf("selftest-doors: 7m. the DRIFTED notice's DISCONNECT answers only on the bare tank: not under a page, a notice, the GIVE modal or the gift card\n"); }
      /* 7n. THE GIFT CARD UNDER PAGES (the 11-02 sweep): its 2 s hold counts from when it is first SEEN, not from the ask
         (a card that waited under a page came up half spent); it waits while not allowed (the tank's own setup, the reset
         prompt, ADVANCED) and its cue comes once, as it is first seen; over the keeper page the page draws it. The GIVE
         modal back from under a page (a birth flow): a fresh hold */
      { static tank_t U; static gift_book_t UG; static uint16_t nfb[TANK_W * TANK_H];
        memset(&U, 0, sizeof U); memset(&UG, 0, sizeof UG); tank_init(&U, 79); tank_new_population(&U);
        while (U.n_fish < 4) { tank_make_fish(&U, U.n_fish, U.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); U.n_fish++; }
        tank_ensure_ids(&U); gift_attach(&U, &UG);
        U.door_open[LINK_LEFT] = true; UG.door_peer[LINK_LEFT] = 0x49439648u; UG.door_badge[LINK_LEFT] = 1;
        while (render_gift_take_cue() >= 0) {}
        UG.ask.on = true; UG.ask.uid = 0x77u; passport_from_fish(&UG.ask.pp, &U.fish[0], 0x49439648u, 3600); UG.ask.pp.uid = 0x77u;
        UG.ask.since = U.clock; UG.ask.shown = false;
        U.clock += 5;                                                       /* (5 s under a page: never drawn) */
        DCHECK(!gift_answer(&UG, U.clock, U.clock, true), "7n: a card never seen took a tap");
        render_gift_allow(false); render_tank(&U, nfb, TANK_W); render_gift(&U, nfb, TANK_W);
        DCHECK(!UG.ask.shown && !render_gift_up(&U) && render_gift_take_cue() < 0, "7n: the card came up while it must wait (shown %d)", UG.ask.shown);
        render_gift_allow(true);
        render_milestones_show_fish(&U, 1); render_milestones(&U, nfb, TANK_W);   /* over the keeper page: the page draws it */
        DCHECK(UG.ask.shown && UG.ask.seen == U.clock, "7n: over the keeper page the card was not drawn");
        DCHECK(render_gift_take_cue() == SND_ARRIVAL && render_gift_take_cue() < 0, "7n: its cue not once as it was first seen");
        d_shot(nfb, "n1_gift_over_page");
        render_milestones_leave();
        { link_view_t lv; memset(&lv, 0, sizeof lv); lv.badge = 1; lv.n_near = 1; lv.near_badge[0] = 2;   /* over the LINK page */
          lv.edge[LINK_RIGHT].state = LV_OPEN; lv.edge[LINK_RIGHT].badge = 2;
          render_tank(&U, nfb, TANK_W); render_tank(&U, nfb, TANK_W); render_link_mode(&lv, nfb, TANK_W, 1.0f); render_gift(&U, nfb, TANK_W);
          render_link_leave(); d_shot(nfb, "n2_gift_over_link"); }
        render_milestones_show_fish(&U, 1);
        DCHECK(!gift_answer(&UG, UG.ask.seen - 0.05, U.clock + 1.0f, true), "7n: a press from before the card was seen answered it");   /* (THE TOUCH RULE: G1, no hold) */
        DCHECK(gift_answer(&UG, U.clock + 1.0f, U.clock + 1.0f, true) && UG.ask.answer == 1, "7n: ACCEPT 1 s after it was seen did not answer");
        render_milestones_leave(); UG.ask.on = false; UG.ask.answer = 0;
        /* the GIVE modal back from under a page: a fresh hold */
        render_give_tap(&U, 0); render_frame_begin(); render_tank(&U, nfb, TANK_W); render_give_modal(&U, nfb, TANK_W);
        U.clock += GIFT_HOLD_S + 0.5f;
        for (int q = 0; q < 5; q++) render_frame_begin();                  /* (frames under a birth flow: not drawn) */
        render_frame_begin(); render_tank(&U, nfb, TANK_W); render_give_modal(&U, nfb, TANK_W);
        int mx = 0, my = 0; render_give_modal_button(&U, RENDER_GM_TAP_GIVE, &mx, &my);
        DCHECK(render_give_modal_tap(&U, (float)mx, (float)my) == RENDER_GM_TAP_HELD && !UG.want_uid, "7n: the GIVE modal back from under a page took a tap at once");
        render_give_modal_close(); gift_attach(&U, NULL);
        printf("selftest-doors: 7n. the gift card: its hold from when it is first seen (it waited 5 s), waits while not allowed, its cue once, drawn over the keeper page; the GIVE modal back from under a page: a fresh hold\n"); }
      /* 7o. AN ARRIVING NOTE THAT NEVER SETTLES (the 11-02 sweep): B gives F, A says yes, then A hears nothing more - B's fish
         leaves on its GIVEN note but GIVEN never arrives. A's ARRIVING note holds a place only until GIFT_ARRIVING_TTL_S of
         its time with B gone; then it lets go (the place is free again). When the two meet again B's GIVEN is answered
         GIVEN_LOST and B's own note brings F back: ONE fish, in B, no notes left anywhere */
      { while (progression_newborn() >= 0) progression_newborn_done(&DB);
        while (DB.n_fish < 4) { tank_make_fish(&DB, DB.n_fish, DB.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); DB.n_fish++; } tank_ensure_ids(&DB);
        while (DA.n_fish > POP_CAP - 1) tank_remove_fish(&DA, DA.n_fish - 1);   /* (A one place short of full: the note takes the last) */
        while (DA.n_fish < POP_CAP - 1) { tank_make_fish(&DA, DA.n_fish, DA.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); DA.n_fish++; } tank_ensure_ids(&DA);
        if (!(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT])) { link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); d_step(60);
            link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400); }
        uint32_t fu = DB.fish[DB.n_fish - 1].uid; int fi = d_wait_home(&DB, fu);
        DCHECK(fi >= 0, "7o: B's fish never came home");
        if (fi >= 0) {
            int room0 = progression_fry_room(&DA);
            GB.want_uid = fu; GB.want_door = GIFT_DOOR_LEFT; GB.want_peer = DA.uid; d_step(10);
            DCHECK(GA.ask.on && GA.ask.uid == fu, "7o: A was not asked");
            GA.ask.answer = 1;
            int w = 0; while (w < 120 && GB.leaving_uid != fu) { d_step(1); w++; }
            DCHECK(GB.leaving_uid == fu && GA.n_arr == 1, "7o: not taken (leaving %08x, A arriving %d)", GB.leaving_uid, GA.n_arr);
            d_deaf[0] = true;                                               /* A hears nothing more: GIVEN never arrives */
            for (int q = 0; q < 1500 && !gift_given_has(&GB, fu); q++) d_step(1);
            DCHECK(gift_given_has(&GB, fu) && D_HOME(&DB, fu) < 0 && D_HOME(&DA, fu) < 0 && GA.n_arr == 1, "7o: B's fish did not leave on its note (note %d, B %d, A %d, A arriving %d)",
                   gift_given_has(&GB, fu), D_HOME(&DB, fu), D_HOME(&DA, fu), GA.n_arr);
            for (int q = 0; q < 900 && link_neighbor(LA, LINK_RIGHT) == DB.uid; q++) d_step(1);   /* (A gives up on the silent link) */
            DCHECK(link_neighbor(LA, LINK_RIGHT) != DB.uid, "7o: A still linked to a silent B");
            DCHECK(GA.n_arr == 1 && room0 && !progression_fry_room(&DA), "7o: the note did not hold the last place while young (arriving %d, room %d, before %d)", GA.n_arr, progression_fry_room(&DA), room0);
            GA.arriving[0].held = GIFT_ARRIVING_TTL_S - 0.5f; d_step(60);
            DCHECK(!GA.n_arr && progression_fry_room(&DA) == room0, "7o: the note held its place past its time (arriving %d, room %d of %d)", GA.n_arr, progression_fry_room(&DA), room0);
            d_deaf[0] = false;                                              /* ... and they meet again */
            link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); d_step(60);
            link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30);
            for (int q = 0; q < 1500 && (gift_given_has(&GB, fu) || D_HOME(&DB, fu) < 0); q++) d_step(1);
            DCHECK(D_HOME(&DB, fu) >= 0 && D_HOME(&DA, fu) < 0 && d_vis_of(&DA, fu) < 0 && !gift_given_has(&GB, fu) && !GA.n_arr,
                   "7o: after the meeting: B %d, A %d, visiting A %d, B's note %d, A arriving %d (want F in B only, no notes)",
                   D_HOME(&DB, fu), D_HOME(&DA, fu), d_vis_of(&DA, fu), gift_given_has(&GB, fu), GA.n_arr);
            printf("selftest-doors: 7o. an ARRIVING note that never settled let go after its time (the place free again); at the next meeting B's note brought the fish back - one fish, no notes\n");
        }
      }
      /* 7p. NO FALSE "NO THANKS" (the 11-02 sweep): a gift offered while another gift card is up is answered CANNOT (not the
         keeper's NO THANKS); a yes the receiver can no longer keep (its room gone meanwhile) is answered FULL */
      { if (!(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT])) { link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); d_step(60);
            link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400); }
        while (DA.n_fish > POP_CAP - 2) tank_remove_fish(&DA, DA.n_fish - 1);
        uint32_t fu = DB.fish[0].uid; int fi = d_wait_home(&DB, fu);
        if (fi >= 0) {
            GA.ask.on = true; GA.ask.uid = 0x99u; GA.ask.nonce = 1; GA.ask.answer = 0;   /* (another gift's card is up on A) */
            GB.want_uid = fu; GB.want_door = GIFT_DOOR_LEFT; GB.want_peer = DA.uid; GB.said_uid = 0;
            for (int q = 0; q < 60 && GB.said_uid != fu; q++) d_step(1);
            DCHECK(GB.said_uid == fu && GB.said_reason == LINK_GIVE_NO_OTHER, "7p: a gift while A's card was up said reason %d (want CANNOT %d, not NO THANKS %d)", GB.said_reason, LINK_GIVE_NO_OTHER, LINK_GIVE_NO_REFUSED);
            GA.ask.on = false;
            d_step(30); fi = d_wait_home(&DB, fu);
            GB.want_uid = fu; GB.want_door = GIFT_DOOR_LEFT; GB.want_peer = DA.uid; GB.said_uid = 0; d_step(10);
            DCHECK(GA.ask.on && GA.ask.uid == fu, "7p: A was not asked the second time");
            while (DA.n_fish < POP_CAP - 1) { tank_make_fish(&DA, DA.n_fish, 1, 0.5f, 0.5f, STAGE_ADULT); DA.n_fish++; }   /* (A fills up while its keeper decides) */
            tank_make_fish(&DA, DA.n_fish, 1, 0.5f, 0.5f, STAGE_ADULT); DA.n_fish++; tank_ensure_ids(&DA);
            GA.ask.answer = 1;
            for (int q = 0; q < 60 && GB.said_uid != fu; q++) d_step(1);
            DCHECK(GB.said_uid == fu && GB.said_reason == LINK_GIVE_NO_FULL && D_HOME(&DB, fu) >= 0 && !GA.n_arr, "7p: a yes A could not keep said reason %d (want FULL %d), B has it %d, A arriving %d",
                   GB.said_reason, LINK_GIVE_NO_FULL, D_HOME(&DB, fu) >= 0, GA.n_arr);
            while (DA.n_fish > POP_CAP - 2) tank_remove_fish(&DA, DA.n_fish - 1);
            printf("selftest-doors: 7p. a gift while another card was up: CANNOT, not NO THANKS; a yes A could no longer keep: FULL\n");
        }
      }
      /* 7q. A HELD PLACE IS A PLACE (the 11-02 sweep): with one place left and a gift's ARRIVING note holding it, no fry is
         staged into it (births counted only the fish here: a sixth fish, then the gift stalled in GI_ALIVE) */
      { while (DA.n_fish > POP_CAP - 1) tank_remove_fish(&DA, DA.n_fish - 1);
        while (DA.n_fish < POP_CAP - 1) { tank_make_fish(&DA, DA.n_fish, 1, 0.5f, 0.5f, STAGE_ADULT); DA.n_fish++; } tank_ensure_ids(&DA);
        fry_req_t req[FRY_REQ_MAX]; bool staged;
        int free_n = progression_next_fry(&DA, req, &staged);
        int arr0 = GA.n_arr; GA.arriving[arr0].uid = 0x5151u; GA.arriving[arr0].nonce = 1; GA.arriving[arr0].peer = DB.uid; GA.n_arr = (uint8_t)(arr0 + 1);
        int held_n = progression_next_fry(&DA, req, &staged);
        DCHECK(free_n > 0 && held_n == 0 && !progression_fry_room(&DA), "7q: one place left: the fry gates %d free, %d with it held (room %d)", free_n, held_n, progression_fry_room(&DA));
        GA.n_arr = (uint8_t)arr0;
        printf("selftest-doors: 7q. one place left and a gift holding it: no fry staged into it\n"); }
      /* 7r. A RETURNED GIFT HOLDS ITS PLACE (11-03 rc): 7o, but the GIVER refills while its GIVEN note is out - births and
         buys counted only the fish here, B went back to five, and the note's GIVEN_LOST brought F back as a sixth. Now the
         note holds F's place (fish_held) until the gift settles: B refills to four, F comes home the fifth. And at six (an
         older build's restore, cap_over) the restore finds no slot: the note is KEPT, nobody dropped - the next meeting
         with a place free brings F back */
      for (int six = 0; six < 2; six++) {
        while (progression_newborn() >= 0) progression_newborn_done(&DB);
        while (DB.n_fish > 4) tank_remove_fish(&DB, DB.n_fish - 1);
        while (DB.n_fish < 4) { tank_make_fish(&DB, DB.n_fish, DB.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); DB.n_fish++; } tank_ensure_ids(&DB);
        while (DA.n_fish > 3) tank_remove_fish(&DA, DA.n_fish - 1);
        GB.said_uid = 0;
        if (!(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT])) { link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); d_step(60);
            link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400); }
        uint32_t fu = DB.fish[DB.n_fish - 1].uid; int fi = d_wait_home(&DB, fu);
        DCHECK(fi >= 0, "7r: B's fish never came home");
        if (fi < 0) break;
        GB.want_uid = fu; GB.want_door = GIFT_DOOR_LEFT; GB.want_peer = DA.uid; d_step(10);
        DCHECK(GA.ask.on && GA.ask.uid == fu, "7r: A was not asked");
        GA.ask.answer = 1;
        int w = 0; while (w < 120 && GB.leaving_uid != fu) { d_step(1); w++; }
        d_deaf[0] = true;                                               /* A hears nothing more: GIVEN never arrives */
        for (int q = 0; q < 1500 && !gift_given_has(&GB, fu); q++) d_step(1);
        DCHECK(gift_given_has(&GB, fu) && D_HOME(&DB, fu) < 0 && DB.n_fish == 3 && progression_fish_held(&DB) == 4,
               "7r: F did not leave on its note holding its place (note %d, B %d fish, held %d)", gift_given_has(&GB, fu), DB.n_fish, progression_fish_held(&DB));
        for (int q = 0; q < 900 && link_neighbor(LA, LINK_RIGHT) == DB.uid; q++) d_step(1);
        int added = 0;                                                  /* B's keeper buys fry, a birth: while the game says there is room */
        while (progression_fry_room(&DB)) { int j = tank_add_fish(&DB, 0, 1); if (j < 0) break; DB.fish[j].door_seen = DB.clock + 3600; added++; }
        DCHECK(added == 1 && DB.n_fish == POP_CAP - 1 && progression_fish_held(&DB) == POP_CAP, "7r: B refilled %d over the note's place (B %d fish, held %d)", added, DB.n_fish, progression_fish_held(&DB));
        if (six) {                                                      /* (six already: the note must wait, never drop F) */
            DB.cap_over = N_FISH_MAX + 1;
            while (DB.n_fish < N_FISH_MAX) { tank_make_fish(&DB, DB.n_fish, 1, 0.5f, 0.5f, STAGE_ADULT); DB.fish[DB.n_fish].door_seen = DB.clock + 3600; DB.n_fish++; }
            tank_ensure_ids(&DB);
        }
        GA.arriving[0].held = GIFT_ARRIVING_TTL_S - 0.5f; d_step(60);
        DCHECK(!GA.n_arr, "7r: A's note did not let go");
        d_deaf[0] = false;                                              /* ... and they meet again */
        link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); d_step(60);
        link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30);
        if (!six) {
            for (int q = 0; q < 1500 && gift_given_has(&GB, fu); q++) d_step(1);
            DCHECK(DB.n_fish == POP_CAP && D_HOME(&DB, fu) >= 0 && D_HOME(&DA, fu) < 0 && d_vis_of(&DA, fu) < 0 && !gift_given_has(&GB, fu) && !GA.n_arr,
                   "7r: after the meeting: B %d fish (want %d), F in B %d, in A %d, visiting A %d, B's note %d, A arriving %d",
                   DB.n_fish, POP_CAP, D_HOME(&DB, fu) >= 0, D_HOME(&DA, fu) >= 0, d_vis_of(&DA, fu) >= 0, gift_given_has(&GB, fu), GA.n_arr);
            printf("selftest-doors: 7r. a GIVEN note out holds F's place: B refilled to %d, F came home the %dth - no sixth fish\n", POP_CAP - 1, POP_CAP);
        } else {
            for (int q = 0; q < 900; q++) d_step(1);
            DCHECK(DB.n_fish == N_FISH_MAX && gift_given_has(&GB, fu) && D_HOME(&DB, fu) < 0 && D_HOME(&DA, fu) < 0 && d_vis_of(&DA, fu) < 0 && !GA.n_arr,
                   "7r six: the restore with no slot: B %d fish, B's note %d (want kept), F in B %d, in A %d, A arriving %d",
                   DB.n_fish, gift_given_has(&GB, fu), D_HOME(&DB, fu) >= 0, D_HOME(&DA, fu) >= 0, GA.n_arr);
            while (DB.n_fish > POP_CAP - 1) tank_remove_fish(&DB, DB.n_fish - 1);   /* (a place free: the next meeting brings F back) */
            DB.cap_over = 0;
            link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); d_step(60);
            link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30);
            for (int q = 0; q < 1500 && gift_given_has(&GB, fu); q++) d_step(1);
            DCHECK(DB.n_fish == POP_CAP && D_HOME(&DB, fu) >= 0 && D_HOME(&DA, fu) < 0 && !gift_given_has(&GB, fu),
                   "7r six: the kept note did not bring F back (B %d fish, F in B %d, note %d)", DB.n_fish, D_HOME(&DB, fu) >= 0, gift_given_has(&GB, fu));
            printf("selftest-doors: 7r. ... and at six the restore found no slot: the note was kept (F held by nobody else, never dropped); a place free, the next meeting brought F home\n");
        }
      }
      /* 7s. ONE GIFT ASKED AT A TIME (11-03 rc): a second GIVE while the first is asking took over asking_uid - the first
         sellable while its question was still up next door, and its CALL OFF dead. Now the second is CANNOT BE GIVEN RIGHT
         NOW, the first stays unsellable, CALL OFF closes it. A stale ask (an event lost) lets go after the engine's time */
      { while (progression_newborn() >= 0) progression_newborn_done(&DB);
        GA.ask.on = false; GA.ask.answer = 0; GB.said_uid = 0;
        while (DB.n_fish < 5) { tank_make_fish(&DB, DB.n_fish, DB.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); DB.n_fish++; } tank_ensure_ids(&DB);
        while (DA.n_fish > 2) tank_remove_fish(&DA, DA.n_fish - 1);
        if (!(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT])) { link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); d_step(60);
            link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400); }
        uint32_t f1 = DB.fish[DB.n_fish - 1].uid, f2 = DB.fish[DB.n_fish - 2].uid; d_wait_home(&DB, f1); d_wait_home(&DB, f2);
        GB.want_uid = f1; GB.want_door = GIFT_DOOR_LEFT; GB.want_peer = DA.uid; d_step(10);
        DCHECK(GB.asking_uid == f1 && GA.ask.on && GA.ask.uid == f1, "7s: f1 was not asked (asking %08x)", GB.asking_uid);
        DCHECK(progression_fish_givable(&DB, D_HOME(&DB, f2)), "7s: f2 not givable on its own (the test needs it)");
        GB.want_uid = f2; GB.want_door = GIFT_DOOR_LEFT; GB.want_peer = DA.uid; d_step(10);
        DCHECK(GB.asking_uid == f1 && !link_give_busy(LB, f2) && link_give_busy(LB, f1) && GB.said_uid == f2 && GB.said_reason == LINK_GIVE_NO_OTHER,
               "7s: a GIVE while one asked: asking %08x (f1 %08x), engine f1 %d f2 %d, said %08x reason %d (want CANNOT %d)",
               GB.asking_uid, f1, link_give_busy(LB, f1), link_give_busy(LB, f2), GB.said_uid, GB.said_reason, LINK_GIVE_NO_OTHER);
        DCHECK(!progression_fish_sellable(&DB, D_HOME(&DB, f1)), "7s: f1 sellable while its question is up next door");
        GB.cancel_uid = f1; d_step(10);
        DCHECK(!link_give_busy(LB, f1) && !GA.ask.on && !GB.asking_uid, "7s: CALL OFF f1: engine %d, A's card %d, asking %08x", link_give_busy(LB, f1), GA.ask.on, GB.asking_uid);
        /* a stale ask: held until past the engine's own time, then let go - the next GIVE asks */
        GB.asking_uid = 0x5157u; GB.asking_since = DB.clock; GB.said_uid = 0;
        GB.want_uid = f2; GB.want_door = GIFT_DOOR_LEFT; GB.want_peer = DA.uid; d_step(10);
        DCHECK(GB.asking_uid == 0x5157u && GB.said_uid == f2 && !link_give_busy(LB, f2), "7s: a fresh ask did not hold the next GIVE");
        GB.asking_since = DB.clock - (LINK_GIVE_ASK_MS + 6000) / 1000.0; d_step(1);
        DCHECK(!GB.asking_uid, "7s: a stale ask held on (asking %08x)", GB.asking_uid);
        GB.said_uid = 0; GB.want_uid = f2; GB.want_door = GIFT_DOOR_LEFT; GB.want_peer = DA.uid; d_step(10);
        DCHECK(GB.asking_uid == f2 && GA.ask.on && GA.ask.uid == f2, "7s: after the stale ask let go, f2 was not asked (asking %08x)", GB.asking_uid);
        GB.cancel_uid = f2; d_step(10);
        DCHECK(!GA.ask.on && !GB.asking_uid, "7s: CALL OFF f2 left the card up");
        while (DB.n_fish > 3) tank_remove_fish(&DB, DB.n_fish - 1);
        printf("selftest-doors: 7s. a GIVE while one asked: CANNOT BE GIVEN RIGHT NOW, the first unsellable, its CALL OFF closed it; a stale ask let go after the engine's time\n"); }
      /* 7t. NO DEMOTION, NO MINT (11-03 rc): an elder grown under 0.3.4's shorter stage times (30 h, past the 0.4 adult
         line only) came home from a visit, or arrived as a gift, an ADULT - the passport's age ran through the director's
         set_age, which demotes. And a fish given at trust 9.6 and tapped to 10 in the new tank paid SD_TRUST every trip */
      { while (progression_newborn() >= 0) progression_newborn_done(&DA);
        while (DA.n_fish > 2) tank_remove_fish(&DA, DA.n_fish - 1);
        const float old_elder = 30 * 3600.0f;
        fish_t el = DB.fish[0]; el.stage = STAGE_ELDER; el.ms_bits |= MS_REACHED_JUV | MS_REACHED_ADULT | MS_REACHED_ELDER;
        passport_t pp; passport_from_fish(&pp, &el, DB.uid, old_elder);
        int i = progression_gift_adopt(&DA, &pp);
        DCHECK(i >= 0 && DA.fish[i].stage == STAGE_ELDER, "7t: a 30 h elder given: stage %d (want ELDER %d)", i >= 0 ? DA.fish[i].stage : -1, STAGE_ELDER);
        if (i >= 0) tank_remove_fish(&DA, i);
        int k = DA.n_fish - 1; fish_t keep = DA.fish[k];                 /* home from a visit (welcome_home): one of A's, an old elder */
        DA.fish[k].stage = STAGE_ELDER; DA.fish[k].ms_bits |= MS_REACHED_JUV | MS_REACHED_ADULT | MS_REACHED_ELDER;
        DA.fish[k].away = true; DA.fish[k].door_edge = LINK_RIGHT;
        link_ev_t ev; memset(&ev, 0, sizeof ev); ev.type = LEV_FISH_RESTORE; ev.uid = DA.fish[k].uid; ev.peer = DB.uid;
        passport_from_fish(&ev.pp, &DA.fish[k], DA.uid, old_elder);
        tank_link_event(&DA, LA, &ev);
        DCHECK(!DA.fish[k].away && DA.fish[k].stage == STAGE_ELDER, "7t: a 30 h elder home from a visit: stage %d away %d (want ELDER)", DA.fish[k].stage, DA.fish[k].away);
        DA.fish[k] = keep; progression_set_age(&DA, k, STAGE_ADULT_AGE + 60);
        int got = 0, n = 0;
        for (int r = 0; r < 3; r++) {                                   /* three trips: given at 9.6, tapped to 10 here */
            passport_from_fish(&pp, &el, DB.uid, old_elder); pp.uid ^= 0x1000u * (uint32_t)(r + 1); pp.trust = 9.6f;
            i = progression_gift_adopt(&DA, &pp); if (i < 0) break;
            int b0 = DA.sd_balance; DA.fish[i].trust = 10.0f; progression_tick(&DA, 0.033f); got += DA.sd_balance - b0; n++;
            tank_remove_fish(&DA, i);
        }
        DCHECK(n == 3 && got == 0, "7t: three gifts at trust 9.6, raised to 10 here, paid +%d SD over %d trips (want +0)", got, n);
        printf("selftest-doors: 7t. a 30 h elder stays an elder home from a visit and as a gift; three trips at trust 9.6 -> 10 paid +0\n"); }
      /* 7u. MORE NOTES THAN THE ENGINE HAS SLOTS (0.4.1, release-0.4.md 2g's deferred list): a book holds GIFT_NOTES (4) GIVEN
         and ARRIVING notes, the engine LINK_GIVE_MAX (2) gifts each way, and a note was handed to the engine only at LINKED -
         a third waited for the next unlink and relink: its fish in neither tank (GIVEN) or a place held (ARRIVING). Now the
         notes are handed over again once a second while linked: all three settle in one meeting. And slots held for a tank
         that walked off (GIVEN sent, its OK never heard) left none for a gift to the tank next door: they go with the link */
      { while (progression_newborn() >= 0) progression_newborn_done(&DA);
        while (progression_newborn() >= 0) progression_newborn_done(&DB);
        GA.ask.on = false; GA.ask.answer = 0; GB.said_uid = 0; GB.asking_uid = 0; GA.n_given = GA.n_arr = 0; GB.n_given = GB.n_arr = 0;
        while (DA.n_fish > 2) tank_remove_fish(&DA, DA.n_fish - 1);
        while (DB.n_fish > 3) tank_remove_fish(&DB, DB.n_fish - 1);
        while (DB.n_fish < 3) { tank_make_fish(&DB, DB.n_fish, DB.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); DB.n_fish++; } tank_ensure_ids(&DB);
        if (!(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT])) { link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); d_step(60);
            link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400); }
        /* (a) B gave two fish to a tank C that walked off before its GIVEN_OK: the engine held both slots (GO_GIVEN) */
        const uint32_t C = 0xc3c3c3c3u;
        passport_t pc; passport_from_fish(&pc, &DB.fish[0], DB.uid, 3600);
        for (int k = 0; k < 2; k++) { pc.uid = 0x7c000001u + (uint32_t)k; DCHECK(gift_given_add(&GB, pc.uid, 0x51u + (uint32_t)k, C, &pc), "7u: B's note to C %d", k);
                                      link_give_resume_out(LB, C, pc.uid, 0x51u + (uint32_t)k, &pc); }
        d_step(2);
        uint32_t f1 = DB.fish[DB.n_fish - 1].uid; d_wait_home(&DB, f1);
        GB.want_uid = f1; GB.want_door = GIFT_DOOR_LEFT; GB.want_peer = DA.uid; d_step(10);
        DCHECK(GB.asking_uid == f1 && GA.ask.on && GA.ask.uid == f1, "7u: with two GIVEN to a tank gone, B could not give next door (asking %08x, said %08x/%d)",
               GB.asking_uid, GB.said_uid, GB.said_reason);
        GB.cancel_uid = f1; d_step(10);
        DCHECK(!GA.ask.on && !GB.asking_uid && GB.n_given == 2, "7u: CALL OFF (A's card %d, asking %08x, B's notes %d)", GA.ask.on, GB.asking_uid, GB.n_given);
        GB.n_given = 0;                                                     /* (C's notes: not this case's - C never comes back here) */
        while (DB.n_fish > 2) tank_remove_fish(&DB, DB.n_fish - 1);
        /* (b) three GIVEN notes in B whose fish A never got (A's ARRIVING notes ran out): A answers GIVEN_LOST, each comes home
           to B; three ARRIVING notes in A for gifts B called off (B answers CANCEL): each lets go. Unlinked while they are made */
        link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); d_step(60);
        DCHECK(link_neighbor(LA, LINK_RIGHT) != DB.uid, "7u: still linked");
        passport_t pg; passport_from_fish(&pg, &DB.fish[0], DB.uid, 3600);
        uint32_t back[3], gone[3];
        for (int k = 0; k < 3; k++) {
            pg.uid = back[k] = 0x7b000001u + (uint32_t)k; DCHECK(gift_given_add(&GB, pg.uid, 0x61u + (uint32_t)k, DA.uid, &pg), "7u: B's note %d", k);
            pg.uid = gone[k] = 0x7a000001u + (uint32_t)k; DCHECK(gift_arriving_add(&GA, pg.uid, 0x71u + (uint32_t)k, DB.uid, &pg), "7u: A's note %d", k);
        }
        DCHECK(progression_fish_held(&DA) == POP_CAP && progression_fish_held(&DB) == POP_CAP, "7u: held A %d B %d (want %d each)", progression_fish_held(&DA), progression_fish_held(&DB), POP_CAP);
        link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT);
        int done = -1, linked = -1;
        for (int q = 0; q < 600 && done < 0; q++) {
            d_step(1);
            if (linked < 0 && link_neighbor(LA, LINK_RIGHT) == DB.uid && link_neighbor(LB, LINK_LEFT) == DA.uid) linked = q;
            if (linked < 0) continue;
            for (int k = 0; k < 3; k++) {
                bool inB = D_HOME(&DB, back[k]) >= 0;
                DCHECK(!(inB && D_HOME(&DA, back[k]) >= 0), "7u: %08x in both tanks", back[k]);
                DCHECK(inB || gift_given_has(&GB, back[k]), "7u: %08x lives nowhere and no note holds it", back[k]);
            }
            DCHECK(link_neighbor(LA, LINK_RIGHT) == DB.uid, "7u: the link dropped mid-meeting");
            if (!GB.n_given && !GA.n_arr) done = q;
        }
        int home = 0; for (int k = 0; k < 3; k++) home += D_HOME(&DB, back[k]) >= 0 && D_HOME(&DA, gone[k]) < 0 && D_HOME(&DB, gone[k]) < 0;
        DCHECK(done >= 0 && home == 3 && DB.n_fish == POP_CAP && DA.n_fish == 2,
               "7u: one meeting settled %d of 3 each way (B notes %d, A notes %d, home in B %d, B %d fish, A %d)", done < 0 ? 3 - (GB.n_given > GA.n_arr ? GB.n_given : GA.n_arr) : 3,
               GB.n_given, GA.n_arr, home, DB.n_fish, DA.n_fish);
        if (done >= 0) printf("selftest-doors: 7u. three GIVEN notes and three ARRIVING notes, one meeting: all settled %.1f s after the link (two slots each way); two slots held for a tank gone let a gift go next door\n", (done - linked) / 30.0);
        while (DB.n_fish > 3) tank_remove_fish(&DB, DB.n_fish - 1); }
      gift_attach(&DA, NULL); gift_attach(&DB, NULL);
      #undef D_HOME
    }

    link_close(LA, LINK_RIGHT); d_step(30);                 /* (3b reopened it) */
    /* 4. never through glass: a closed door is a wall */
    d_head_for(&DA, 0, LINK_RIGHT);
    d_step(600);
    DCHECK(!DA.fish[0].away && DA.fish[0].x < tank_glass_x1(DA.fish[0].y), "a fish went through a closed door");
    free(LA); free(LB);
    doors_mate_left();
    doors_give_fresh();
    printf("selftest-doors: %s\n", d_fails ? "FAILED" : "OK");
    return d_fails ? 1 : 0;
}

/* ---- THE FIVE-FISH CAP ACROSS THE DOORS (2026-11-03, Strato: a keeper's tank holds at most POP_CAP fish, never more,
 * on every path). A cross-tank fry born in the host for the visitor's home: the home said yes once, filled up while the
 * pair courted (5-10 min), said no at the birth, and the host kept the fry as a SIXTH fish - or, at six, dropped it
 * without a word. Now the home HOLDS the place it said yes to (fish_held counts it) while the host keeps asking, and a
 * host that cannot hand the fry over and has no room undoes the birth (the fry was never on the glass; the pair courts
 * again). Two real tanks, the doors' harness: B's fish 0 visits A and courts there.
 *   a. home full by the birth: B says yes, then fills the way the game lets it (while progression_fry_room says so), A
 *      full too, the coin forced to home: the fry reaches B, nobody above POP_CAP, nothing lost;
 *   b. both full at the ask: no birth; a place frees in B and the host's next ask hears it (a 'no' is not for good);
 *   c. the hold ends: the pair off frees it at once; a host that stops asking (an older 0.4 build asks once), in its time;
 *   d. the last resort: the radio goes down between the birth and the offer, A full: no sixth fish, the pair stays. */
static int fc_over;
static void fc_step(int frames, uint32_t par) {
    for (int n = 0; n < frames; n++) {
        int v = d_vis_of(&DA, par);                    /* (the visitor stays: its home's pull held off) */
        if (v >= 0) { DA.fish[v].homeward = false; DA.vis_since[v - N_FISH_MAX] = DA.clock; }
        d_step(1);
        static bool was;                               /* (said once as it goes over, not every frame) */
        bool over = DA.n_fish > POP_CAP || DB.n_fish > POP_CAP;
        if (over && !was) { fc_over++; DCHECK(0, "over the cap: A %d, B %d fish (POP_CAP %d)", DA.n_fish, DB.n_fish, POP_CAP); }
        was = over;
    }
}
static int fc_fill(tank_t *t) {                        /* fish arrive the way the game lets them: while there is room */
    int n = 0;
    while (progression_fry_room(t)) { int j = tank_add_fish(t, 0, 1); if (j < 0) break; t->fish[j].door_seen = t->clock + 3600; n++; }
    return n;
}
static void fc_trim(tank_t *t, int n) { while (t->n_fish > n) tank_remove_fish(t, t->n_fish - 1); }
static int fc_count(const tank_t *t, uint32_t uid) {   /* a fish here: at home, a visitor, or a fry waiting for its home */
    int n = 0;
    for (int i = 0; i < t->n_fish; i++) n += t->fish[i].uid == uid;
    for (int k = 0; k < N_VISIT_MAX; k++) n += (t->vis_used[k] || t->vis_pending[k]) && t->fish[N_FISH_MAX + k].uid == uid;
    return n;
}
int selftest_fry_cap(void) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-fry-cap.sav", 1);
    d_fails = 0; fc_over = 0; dnow = 1000; dqn = 0; d_deaf[0] = d_deaf[1] = false;
    tank_init(&DA, 41); progression_fresh(&DA); tank_ensure_ids(&DA);
    tank_init(&DB, 42); tank_new_population(&DB); tank_ensure_ids(&DB);
    for (int i = 0; i < 2; i++) { progression_set_age(&DA, i, STAGE_ADULT_AGE + 60); DA.fish[i].stage = STAGE_ADULT; DB.fish[i].stage = STAGE_ADULT; }
    LA = calloc(1, (size_t)link_size()); LB = calloc(1, (size_t)link_size());
    link_host_t ha = tank_link_host(&DA, d_send), hb = tank_link_host(&DB, d_send);
    link_init(LA, DA.uid, &ha, 1, dnow); link_init(LB, DB.uid, &hb, 2, dnow);
    link_mode(LA, true); link_mode(LB, true); d_step(10);
    link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30); d_step(400);
    DCHECK(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT], "the doors did not open");
    const int nA0 = DA.n_fish, nB0 = DB.n_fish;
    int passed = 0;
    for (int sc = 0; sc < 4; sc++) {
        fc_trim(&DA, nA0); fc_trim(&DB, nB0);
        while (progression_newborn() >= 0) progression_newborn_done(&DA);
        for (int i = 0; i < DA.n_fish; i++) DA.fish[i].door_seen = DA.clock + 3600;   /* only B's fish 0 goes visiting */
        for (int i = 1; i < DB.n_fish; i++) DB.fish[i].door_seen = DB.clock + 3600;
        uint32_t par = DB.fish[0].uid;
        for (int q = 0, quiet = 0; q < 3600 && quiet < 90; q++) {   /* the scene's one visitor: any other that wandered over before the holds
                                                            goes home, and none sets out (0.4.1: a fry for its home needs a visitor place free -
                                                            two in and it waits). Quiet for 3 s, the holds afresh each frame (a welcome home clears them) */
            int other = 0;
            for (int k = 0; k < N_VISIT_MAX; k++) {
                if (DA.vis_used[k] && DA.fish[N_FISH_MAX + k].uid != par) { DA.fish[N_FISH_MAX + k].homeward = DA.fish[N_FISH_MAX + k].bolt_home = true; other++; }
                if (DB.vis_used[k]) { DB.fish[N_FISH_MAX + k].homeward = DB.fish[N_FISH_MAX + k].bolt_home = true; other++; }
            }
            for (int t2 = 0; t2 < 2; t2++) { tank_t *h = t2 ? &DB : &DA;
                for (int i = 0; i < h->n_fish; i++) { fish_t *f = &h->fish[i]; if (f->uid == par) continue;
                    f->door_seen = h->clock + 3600;
                    if (f->away) other++; else if (f->door == DOOR_HEADING) f->door = DOOR_NONE; else if (f->door != DOOR_NONE) other++; } }
            quiet = other ? 0 : quiet + 1;
            d_step(1);
        }
        if (d_vis_of(&DA, par) < 0) { d_head_for(&DB, 0, LINK_LEFT); for (int n = 0; n < 1800 * ((TANK_W + 447) / 448) && d_vis_of(&DA, par) < 0; n++) d_step(1); }
        DCHECK(d_vis_of(&DA, par) >= 0, "%c: B's fish never visited (door %d, away %d)", 'a' + sc, DB.fish[0].door, DB.fish[0].away);
        if (d_vis_of(&DA, par) < 0) break;
        const int f0 = fc_over;
        if (sc == 1) fc_fill(&DB);                       /* b: B full before it is asked */
        DA.fry_partner = par; DA.fry_asked = false; DA.fry_home_room = -1;
        fc_step(60, par);
        DCHECK(DA.fry_home_room == (sc == 1 ? 0 : 1), "%c: its home's room came back %d", 'a' + sc, DA.fry_home_room);
        if (sc == 0) {
            int got = fc_fill(&DB), nB = DB.n_fish;     /* B fills while the pair courts: its own births, gifts, shop fry */
            fc_fill(&DA); int nA = DA.n_fish;
            fc_step(30 * 40, par);                       /* (the courting: well past a host's first ask) */
            DCHECK(DA.fry_home_room == 1, "a: the yes did not last the courting (room %d)", DA.fry_home_room);
            DA.fry_force = 1; progression_force_arrival(&DA);
            int k = DA.fry_give_k; uint32_t fry = k >= 0 ? DA.fish[N_FISH_MAX + k].uid : 0;
            DCHECK(k >= 0 && DA.n_fish == nA, "a: the fry was not born for its home (give %d, A %d -> %d)", k, nA, DA.n_fish);
            fc_step(60, par);
            int inA = fry ? fc_count(&DA, fry) : 0, inB = fry ? fc_count(&DB, fry) : 0;
            DCHECK(fry && inB == 1 && DB.n_fish == nB + 1 && DA.n_fish == nA, "a: the fry lives in A %d, B %d times (A %d -> %d, B %d -> %d fish: want it in B)",
                   inA, inB, nA, DA.n_fish, nB, DB.n_fish);
            if (fry && inB == 1 && DA.n_fish == nA && fc_over == f0) { printf("selftest-fry-cap: a. B said yes, then %d fish arrived there while the pair courted: its held place took the fry - A %d, B %d fish\n", got, DA.n_fish, DB.n_fish); passed++; }
        } else if (sc == 1) {
            fc_fill(&DA); int nA = DA.n_fish, nB = DB.n_fish;
            DA.fry_force = 1; progression_force_arrival(&DA);
            DCHECK(DA.n_fish == nA && DB.n_fish == nB && DA.fry_give_k < 0, "b: both full and a fry was born (A %d -> %d, B %d -> %d, give %d)", nA, DA.n_fish, nB, DB.n_fish, DA.fry_give_k);
            fc_trim(&DB, nB - 1);                        /* a place frees in B: the host's next ask hears it */
            int heard = -1; for (int n = 0; n < 30 * 120 && heard < 0; n++) { fc_step(1, par); if (DA.fry_home_room == 1) heard = n; }
            DCHECK(heard >= 0, "b: a place freed in B and the host never heard (room %d)", DA.fry_home_room);
            DA.fry_force = 1; progression_force_arrival(&DA);
            int k = DA.fry_give_k; uint32_t fry = k >= 0 ? DA.fish[N_FISH_MAX + k].uid : 0;
            fc_step(60, par);
            bool ok = fry && fc_count(&DB, fry) == 1 && DB.n_fish == nB && DA.n_fish == nA;
            DCHECK(ok, "b: the fry did not take B's freed place (A %d, B %d fish)", DA.n_fish, DB.n_fish);
            if (heard >= 0 && ok && fc_over == f0) { printf("selftest-fry-cap: b. both full: no birth; a place freed in B, heard %.0f s on, and the fry took it\n", heard / 30.0); passed++; }
        } else if (sc == 2) {
            fc_fill(&DB); int nB = DB.n_fish;
            DCHECK(!progression_fry_room(&DB), "c: B is not full");
            DA.fry_partner = 0; fc_step(10, par);         /* the pair is off (here by hand; a birth of ours, or the visitor left) */
            bool off = progression_fry_room(&DB) && DB.n_fish == nB;
            DCHECK(off, "c: the pair is off and B still holds a place for its fry");
            fc_trim(&DB, nB0); fc_fill(&DB); fc_trim(&DB, DB.n_fish - 1);   /* one place free again */
            DA.fry_partner = par; DA.fry_asked = false; DA.fry_home_room = -1; fc_step(30, par);
            DCHECK(DA.fry_home_room == 1 && !progression_fry_room(&DB), "c: B did not hold its last place for the fry (room %d, B room %d)", DA.fry_home_room, progression_fry_room(&DB));
            DA.fry_ask_at = DA.clock + 1e6;              /* the host asks no more, the link up (an older 0.4 host asks once) */
            int freed = -1; for (int n = 0; n < 30 * 300 && freed < 0; n++) { fc_step(1, par); if (progression_fry_room(&DB)) freed = n; }
            DCHECK(freed >= 30 * 60, "c: B let go of the held place after %.0f s (want a timeout well past the asks)", freed / 30.0);
            DCHECK(freed >= 0, "c: B held the place for good after the asks stopped");
            if (off && freed >= 30 * 60 && fc_over == f0) { printf("selftest-fry-cap: c. the pair off: B let go at once; the asks stopped: B let go %.0f s on\n", freed / 30.0); passed++; }
            DA.fry_ask_at = DA.clock;
        } else {
            fc_fill(&DA); int nA = DA.n_fish;
            DA.fry_force = 1; progression_force_arrival(&DA);
            DCHECK(DA.fry_give_k >= 0, "d: the fry was not born for its home (give %d)", DA.fry_give_k);
            tank_link_tick(&DA, NULL);                   /* the radio goes down before the offer */
            int pend = 0; for (int q = 0; q < N_VISIT_MAX; q++) pend += DA.vis_pending[q];
            DCHECK(DA.n_fish == nA && !pend && DA.fry_give_k < 0, "d: A full, the radio down: A %d -> %d fish (cap %d), %d waiting", nA, DA.n_fish, POP_CAP, pend);
            DCHECK(DA.fry_partner == par, "d: the pair's courting was lost with the undone birth (partner %08x)", DA.fry_partner);
            fc_step(30, par);
            if (DA.n_fish == nA && !pend && DA.fry_partner == par && fc_over == f0) { printf("selftest-fry-cap: d. A full, the radio down before the offer: the birth undone (never on the glass), no sixth fish, the pair still courts\n"); passed++; }
        }
        DA.fry_partner = 0; DA.fry_force = -1; fc_step(10, par);
    }
    DCHECK(passed == 4, "%d of 4 scenes", passed);
    link_close(LA, LINK_RIGHT); d_step(60);
    free(LA); free(LB); LA = LB = NULL;
    printf("selftest-fry-cap: %s\n", d_fails ? "FAILED" : "OK");
    return d_fails ? 1 : 0;
}

/* ---- GIVE (phase 6): fish handed over for good, two tanks, the same hostile radio ----
 * Each tank keeps what a real one SAVES (its fish, its GIVEN / ARRIVING notes, the RECEIVED
 * ring) apart from what it is in the middle of doing (the work between an engine event and the
 * call that answers it), and a crash loses only the second. Each step after an event is split
 * as a real tank's two saves would be, so a crash lands between them too. After every 10 ms:
 *   - NEVER TWO: no fish lives in both tanks (a dead tank's save counts);
 *   - NEVER ZERO: a fish alive in neither has a GIVEN note in one (its passport, to bring it back).
 * At the end the radio heals and every gift must settle: no note left anywhere, every fish
 * alive exactly once, none lost and none made. */
#define GV_FISH 12
typedef struct { uint32_t uid, nonce, peer; passport_t pp; } gnote_t;
enum { GJ_ANSWER, GJ_TAKEN_NOTE, GJ_TAKEN_OUT, GJ_ALIVE_ADD, GJ_ALIVE_DONE };
typedef struct { int type; uint32_t at, uid, nonce, peer; passport_t pp; bool yes; } gjob_t;
typedef struct {
    passport_t f[GV_FISH]; bool giving[GV_FISH]; int nf;          /* saved (giving: in memory) */
    gnote_t given[8]; int ngiven;                                   /* saved */
    gnote_t arriving[8]; int narr;                                  /* saved */
    struct { uint32_t uid, nonce; } recv[32]; int nrecv;            /* saved: the last 32 received */
    gjob_t job[24]; int njob;                                       /* in memory: lost on a crash */
} gtank_t;
static gtank_t GT[2];
static struct { int asked, yes, no, taken, kept, closed, restored, cancelled, refused, crashes, mid_crash, lost_notes; } gs;

static int gt_of(void *ctx) { return (int)((mtank_t *)ctx - T); }
static int gf_find(gtank_t *g, uint32_t uid) { for (int i = 0; i < g->nf; i++) if (g->f[i].uid == uid) return i; return -1; }
static int gn_find(gnote_t *n, int k, uint32_t uid, uint32_t nonce) { for (int i = 0; i < k; i++) if (n[i].uid == uid && (!nonce || n[i].nonce == nonce)) return i; return -1; }
static void gn_drop(gnote_t *n, int *k, int i) { if (i >= 0) n[i] = n[--*k]; }
static int g_recv_cap = 32;                       /* the RECEIVED ring's length (0.4.2, LK-2: 1 in selftest-give's short-ring runs) */
static bool g_recv_has(gtank_t *g, uint32_t uid, uint32_t nonce) { for (int i = 0; i < g->nrecv; i++) if (g->recv[i].uid == uid && g->recv[i].nonce == nonce) return true; return false; }
static void g_recv_add(gtank_t *g, uint32_t uid, uint32_t nonce) {
    if (g_recv_has(g, uid, nonce)) return;
    if (g->nrecv >= g_recv_cap) { memmove(g->recv, g->recv + 1, (size_t)(g_recv_cap - 1) * sizeof g->recv[0]); g->nrecv = g_recv_cap - 1; }
    g->recv[g->nrecv].uid = uid; g->recv[g->nrecv].nonce = nonce; g->nrecv++;
}
static void gf_remove(gtank_t *g, uint32_t uid) { int i = gf_find(g, uid); if (i >= 0) { g->f[i] = g->f[g->nf - 1]; g->giving[i] = g->giving[g->nf - 1]; g->nf--; } }
static void gf_add(gtank_t *g, const passport_t *pp) {
    LCHECK(g->nf < GV_FISH, "a gift arrived in a full tank");
    if (g->nf < GV_FISH && gf_find(g, pp->uid) < 0) { g->f[g->nf] = *pp; g->giving[g->nf] = false; g->nf++; }
}
static int g_give_state(void *ctx, uint32_t uid, uint32_t nonce, passport_t *pp) {
    gtank_t *g = &GT[gt_of(ctx)]; int i;
    if ((i = gn_find(g->given, g->ngiven, uid, nonce)) >= 0) { if (pp) *pp = g->given[i].pp; return LINK_GS_GIVEN; }
    if ((i = gn_find(g->arriving, g->narr, uid, nonce)) >= 0) { if (pp) *pp = g->arriving[i].pp; return LINK_GS_ARRIVING; }
    if (g_recv_has(g, uid, nonce)) return LINK_GS_ALIVE;
    return LINK_GS_NONE;
}
static bool g_ours(void *ctx, uint32_t uid) { return gf_find(&GT[gt_of(ctx)], uid) >= 0; }   /* (link_host_t.ours: alive here, saved) */
static int g_room(void *ctx) { gtank_t *g = &GT[gt_of(ctx)]; return GV_FISH - g->nf - g->narr; }
static void g_job(gtank_t *g, int type, uint32_t at, const link_ev_t *e, bool yes) {
    LCHECK(g->njob < 24, "too much work queued");
    if (g->njob >= 24) return;
    gjob_t *j = &g->job[g->njob++]; j->type = type; j->at = at; j->uid = e->uid; j->nonce = e->nonce; j->peer = e->peer; j->pp = e->pp; j->yes = yes;
}
static void g_boot(int i) {                       /* a boot: the engine knows nothing; the saved notes settle what they can */
    mtank_t *t = &T[i]; gtank_t *g = &GT[i];
    link_host_t h = { t, h_send, g_room, NULL, g_room, g_give_state, NULL, g_ours };
    link_init(t->L, t->uid, &h, rnd(), now);
    t->alive = true; t->radio = true;
    g->njob = 0; for (int k = 0; k < g->nf; k++) g->giving[k] = false;
    for (int k = 0; k < g->ngiven; k++) gf_remove(g, g->given[k].uid);                  /* a GIVEN note wins over the fish */
    for (int k = 0; k < g->narr; ) {                                                     /* an ARRIVING one already alive: received */
        if (gf_find(g, g->arriving[k].uid) >= 0) { g_recv_add(g, g->arriving[k].uid, g->arriving[k].nonce); gn_drop(g->arriving, &g->narr, k); }
        else k++;
    }
    link_mode(t->L, true);
}
static void g_events(int i) {
    mtank_t *t = &T[i]; gtank_t *g = &GT[i]; link_ev_t e;
    while (link_poll(t->L, &e)) {
        switch (e.type) {
        case LEV_GIVE_ASK: gs.asked++; g_job(g, GJ_ANSWER, now + (rndf() < 0.1f ? rndi(55000, 70000) : rndi(300, 20000)), &e, rndf() < 0.8f); break;
        case LEV_GIVE_TAKEN: gs.taken++; g_job(g, GJ_TAKEN_NOTE, now + (rnd() & 1 ? rndi(0, 300) : rndi(1000, 6000)), &e, true); break;   /* (the swim out to the door: a crash then loses it) */
        case LEV_GIVE_NO: { int k = gf_find(g, e.uid); if (k >= 0) g->giving[k] = false; gs.refused++; break; }
        case LEV_GIVE_ALIVE: g_job(g, GJ_ALIVE_ADD, now + rndi(0, 300), &e, true); break;
        case LEV_GIVE_CANCEL: gn_drop(g->arriving, &g->narr, gn_find(g->arriving, g->narr, e.uid, e.nonce)); gs.cancelled++; break;
        case LEV_GIVE_CLOSED: gn_drop(g->given, &g->ngiven, gn_find(g->given, g->ngiven, e.uid, e.nonce)); gs.closed++; break;
        case LEV_GIVE_RESTORE: {
            int k = gn_find(g->given, g->ngiven, e.uid, e.nonce);
            LCHECK(k >= 0, "tank %d: RESTORE without a GIVEN note", i);
            if (k >= 0) { gf_add(g, &g->given[k].pp); gn_drop(g->given, &g->ngiven, k); }
            gs.restored++; break;
        }
        case LEV_LINKED:                          /* what this tank was in the middle of, with that tank */
            for (int k = 0; k < g->ngiven; k++) if (g->given[k].peer == e.peer) link_give_resume_out(t->L, e.peer, g->given[k].uid, g->given[k].nonce, &g->given[k].pp);
            for (int k = 0; k < g->narr; k++) if (g->arriving[k].peer == e.peer) link_give_resume_in(t->L, e.peer, g->arriving[k].uid, g->arriving[k].nonce, &g->arriving[k].pp);
            break;
        default: break;
        }
    }
}
static void g_work(int i) {
    mtank_t *t = &T[i]; gtank_t *g = &GT[i];
    for (int k = 0; k < g->njob; ) {
        gjob_t j = g->job[k];
        if ((int32_t)(now - j.at) < 0) { k++; continue; }
        g->job[k] = g->job[--g->njob];
        switch (j.type) {
        case GJ_ANSWER:
            if (j.yes && g_room(t) > 0 && g->narr < 8) {          /* the ARRIVING note first, then the yes */
                gnote_t *n = &g->arriving[g->narr++]; n->uid = j.uid; n->nonce = j.nonce; n->peer = j.peer; n->pp = j.pp;
                if (link_give_answer(t->L, j.uid, j.nonce, true)) gs.yes++;
                else gn_drop(g->arriving, &g->narr, g->narr - 1);   /* (too late: the question had closed) */
            } else { link_give_answer(t->L, j.uid, j.nonce, false); gs.no++; }
            break;
        case GJ_TAKEN_NOTE: {                                     /* save 1: the GIVEN note, with its passport */
            int f = gf_find(g, j.uid);
            LCHECK(f >= 0, "tank %d: TAKEN for a fish it does not have", i);
            if (f < 0) break;
            if (rndf() < 0.2f || !link_neighbor(t->L, i == 0 ? LINK_RIGHT : LINK_LEFT)) {   /* the swim out failed (the other tank went quiet, its
                                                                     door closed - tank_link's gift_keep, 2026-11-02): it stays, nothing saved */
                if (link_give_keep(t->L, j.uid, j.nonce)) { g->giving[f] = false; gs.kept++; break; }
            }
            if (g->ngiven < 8) { gnote_t *n = &g->given[g->ngiven++]; n->uid = j.uid; n->nonce = j.nonce; n->peer = j.peer; n->pp = g->f[f]; }
            j.type = GJ_TAKEN_OUT; j.at = now + rndi(0, 200); g->job[g->njob++] = j;
            break;
        }
        case GJ_TAKEN_OUT: gf_remove(g, j.uid); link_give_done(t->L, j.uid, j.nonce, NULL); break;   /* save 2: the fish out */
        case GJ_ALIVE_ADD:                                        /* save 1: the fish in */
            gf_add(g, &j.pp); g_recv_add(g, j.uid, j.nonce);
            j.type = GJ_ALIVE_DONE; j.at = now + rndi(0, 200); g->job[g->njob++] = j;
            break;
        case GJ_ALIVE_DONE:                                       /* save 2: the note gone */
            gn_drop(g->arriving, &g->narr, gn_find(g->arriving, g->narr, j.uid, j.nonce));
            link_give_alive(t->L, j.uid, j.nonce);
            break;
        }
    }
}
static uint32_t g_all[2 * GV_FISH]; static int g_all_n;
static void g_check(void) {
    for (int u = 0; u < g_all_n; u++) {
        int alive = 0, notes = 0;
        for (int i = 0; i < 2; i++) { alive += gf_find(&GT[i], g_all[u]) >= 0; notes += gn_find(GT[i].given, GT[i].ngiven, g_all[u], 0) >= 0; }
        LCHECK(alive <= 1, "NEVER TWO: fish %08x lives in both tanks", g_all[u]);
        LCHECK(alive + notes >= 1, "NEVER ZERO: fish %08x lives nowhere and no GIVEN note holds it", g_all[u]);
    }
}
static int give_run(uint32_t seed, uint32_t chaos_ms, bool quiet) {
    nt = 2; rs = seed; now = 1000; qn = 0; chaos = true; pool_n = 0;
    memset(drop_p, 0, sizeof drop_p); memset(cut_until, 0, sizeof cut_until); memset(&gs, 0, sizeof gs);
    int f0 = fails; g_all_n = 0;
    for (int i = 0; i < 2; i++) {
        mtank_t *t = &T[i]; link_t *keep = t->L; memset(t, 0, sizeof *t); t->L = keep; t->uid = rnd() | 1;
        gtank_t *g = &GT[i]; memset(g, 0, sizeof *g);
        for (int k = 0; k < 5; k++) {
            passport_t *p = &g->f[g->nf++]; memset(p, 0, sizeof *p);
            p->uid = rnd() | 1; p->home_tank = t->uid; snprintf(p->name, sizeof p->name, "g%d%d", i, k);
            p->stage = STAGE_ADULT; p->size = 1; p->bold = p->sociable = 0.5f;
            g_all[g_all_n++] = p->uid;
        }
        g_boot(i);
    }
    T[0].arm_at[LINK_RIGHT] = now + 10; T[1].arm_at[LINK_LEFT] = now + 300;
    uint32_t end = now + chaos_ms, settle_end = end + 10 * 60000;
    while ((int32_t)(now - settle_end) < 0) {
        bool calm = (int32_t)(now - end) >= 0;
        if (calm && chaos) { chaos = false; memset(drop_p, 0, sizeof drop_p); memset(cut_until, 0, sizeof cut_until);
                             for (int i = 0; i < 2; i++) if (!T[i].alive) g_boot(i); }
        if (!calm) {                              /* the troubles: cuts, a bad radio, a dead battery */
            if (rndf() < 0.0004f) { int a = (int)(rnd() % 2); cut_until[a][!a] = now + rndi(1000, 20000); if (rnd() & 1) cut_until[!a][a] = cut_until[a][!a]; }
            if (rndf() < 0.0005f) { int a = (int)(rnd() % 2); drop_p[a][!a] = rndf() * 0.45f; }
            if (rndf() < 0.0002f) { int a = (int)(rnd() % 2); if (T[a].alive) { T[a].alive = false; T[a].back_at = now + rndi(1000, 20000); gs.crashes++; gs.mid_crash += GT[a].njob > 0;
                if (rndf() < 0.15f) { gs.lost_notes += GT[a].narr; GT[a].narr = 0; } } }   /* ... its ARRIVING notes lost with it now and then (a damaged blob): the giver's GIVEN note must bring the fish back */
            for (int i = 0; i < 2; i++) {         /* a keeper gives a fish (the tank keeps two) */
                gtank_t *g = &GT[i]; mtank_t *t = &T[i];
                if (!t->alive || g->nf <= 2 || rndf() > 0.002f) continue;
                int k = (int)(rnd() % g->nf); if (g->giving[k]) continue;
                if (link_give(t->L, i == 0 ? LINK_RIGHT : LINK_LEFT, &g->f[k])) g->giving[k] = true;
            }
            for (int i = 0; i < 2; i++) {         /* ... and now and then thinks better of it */
                gtank_t *g = &GT[i];
                if (T[i].alive && rndf() < 0.0003f) for (int k = 0; k < g->nf; k++) if (g->giving[k]) { link_give_cancel(T[i].L, g->f[k].uid); break; }
            }
        }
        for (int i = 0; i < 2; i++) {             /* boots, and the keepers' holds when the door is shut */
            mtank_t *t = &T[i];
            if (!t->alive) { if ((int32_t)(now - t->back_at) >= 0) g_boot(i); else continue; }
            for (int e = 0; e < LINK_EDGES; e++) if (t->arm_at[e] && (int32_t)(now - t->arm_at[e]) >= 0) { link_mode(t->L, true); link_arm(t->L, (link_edge_t)e); t->arm_at[e] = 0; }
        }
        if (T[0].alive && T[1].alive && !link_neighbor(T[0].L, LINK_RIGHT) && !T[0].arm_at[LINK_RIGHT] && rndf() < 0.01f) {
            T[0].arm_at[LINK_RIGHT] = now + 10; T[1].arm_at[LINK_LEFT] = now + 300;
        }
        deliver();
        for (int i = 0; i < 2; i++) if (T[i].alive) { link_tick(T[i].L, now); g_events(i); g_work(i); g_events(i); }
        g_check();
        if (calm) {                               /* settled: no note, no work, every fish alive once */
            bool done = true;
            for (int i = 0; i < 2; i++) done &= GT[i].ngiven == 0 && GT[i].narr == 0 && GT[i].njob == 0;
            if (done) break;
        }
        now += STEP_MS;
    }
    int total = 0;
    for (int i = 0; i < 2; i++) {
        LCHECK(GT[i].ngiven == 0, "tank %d: %d GIVEN notes never settled", i, GT[i].ngiven);
        LCHECK(GT[i].narr == 0, "tank %d: %d ARRIVING notes never settled", i, GT[i].narr);
        total += GT[i].nf;
    }
    for (int u = 0; u < g_all_n; u++) {
        int alive = 0; for (int i = 0; i < 2; i++) alive += gf_find(&GT[i], g_all[u]) >= 0;
        LCHECK(alive == 1, "fish %08x alive %d times at the end", g_all[u], alive);
    }
    LCHECK(total == g_all_n, "%d fish at the end, %d at the start", total, g_all_n);
    if (!quiet || fails != f0)
        printf("selftest-give: seed %u, %u min: %d asked (%d yes, %d no), %d taken (%d kept as they swam out), %d delivered, %d came back from a GIVEN note, %d cancelled, %d refused; %d crashes (%d mid-work); %d+%d fish at the end\n",
               seed, chaos_ms / 60000, gs.asked, gs.yes, gs.no, gs.taken, gs.kept, gs.closed, gs.restored, gs.cancelled, gs.refused, gs.crashes, gs.mid_crash, GT[0].nf, GT[1].nf);
    return gs.closed;
}
/* ---- GIVE WHERE IT IS (2026-11-01): visits and gifts at once, the same hostile radio ----
 * Fish come and go between two tanks through the doors (leases, returns, restores) while keepers
 * give them away - home, or next door to the tank they are visiting, where they are. Each tank keeps
 * what a real one SAVES (its fish and whether each is away, its notes) apart from what it is doing
 * (visitors are never saved, work between an event and its answer is lost in a crash). After every
 * 10 ms step:
 *   - NEVER TWO SWIMMING: no fish swims in two tanks (at home, or as a visitor);
 *   - NEVER TWO OWNERS: no fish is on two tanks' rosters (home or away);
 *   - NEVER LOST: a fish on no roster has a GIVEN note (its passport) in one.
 * At the end the radio heals: every fish on exactly one roster, home, no visitor, no note. */
#define VG_FISH 8
typedef struct { uint32_t uid; int st; passport_t pp; bool offered, giving; uint32_t cross_at, restore_at; } vgf_t;   /* st 0 free, 1 home, 2 away */
typedef struct { bool used; uint32_t uid, home; passport_t pp; uint32_t leave_at; link_edge_t edge; } vgv_t;
typedef struct {
    vgf_t f[VG_FISH]; vgv_t v[LINK_VISIT_MAX + 1];
    gnote_t given[8], arriving[8]; int ngiven, narr; struct { uint32_t uid, nonce; } recv[32]; int nrecv;
    gjob_t job[24]; int njob;
} vgtank_t;
static vgtank_t VG[2];
static struct { int asked, here, taken, closed, restored, visits, crashes; } vs_;
static vgf_t *vg_f(vgtank_t *g, uint32_t uid) { for (int i = 0; i < VG_FISH; i++) if (g->f[i].st && g->f[i].uid == uid) return &g->f[i]; return NULL; }
static vgv_t *vg_v(vgtank_t *g, uint32_t uid) { for (int i = 0; i <= LINK_VISIT_MAX; i++) if (g->v[i].used && g->v[i].uid == uid) return &g->v[i]; return NULL; }
static int vg_n(vgtank_t *g) { int n = 0; for (int i = 0; i < VG_FISH; i++) n += g->f[i].st != 0; return n; }
static void vg_add(vgtank_t *g, const passport_t *pp) {
    if (vg_f(g, pp->uid)) return;
    for (int i = 0; i < VG_FISH; i++) if (!g->f[i].st) { memset(&g->f[i], 0, sizeof g->f[i]); g->f[i].uid = pp->uid; g->f[i].st = 1; g->f[i].pp = *pp; return; }
    LCHECK(0, "a gift arrived in a full tank");
}
static bool vg_recv_has(vgtank_t *g, uint32_t uid, uint32_t nonce) { for (int i = 0; i < g->nrecv; i++) if (g->recv[i].uid == uid && g->recv[i].nonce == nonce) return true; return false; }
static void vg_recv_add(vgtank_t *g, uint32_t uid, uint32_t nonce) {
    if (vg_recv_has(g, uid, nonce)) return;
    if (g->nrecv >= g_recv_cap) { memmove(g->recv, g->recv + 1, (size_t)(g_recv_cap - 1) * sizeof g->recv[0]); g->nrecv = g_recv_cap - 1; }
    g->recv[g->nrecv].uid = uid; g->recv[g->nrecv].nonce = nonce; g->nrecv++;
}
static int vg_of(void *ctx) { return (int)((mtank_t *)ctx - T); }
static void vg_job(vgtank_t *g, int type, uint32_t at, const link_ev_t *e, bool yes) {
    LCHECK(g->njob < 24, "too much work queued");
    if (g->njob >= 24) return;
    gjob_t *j = &g->job[g->njob++]; j->type = type; j->at = at; j->uid = e->uid; j->nonce = e->nonce; j->peer = e->peer; j->pp = e->pp; j->yes = yes;
}
static int vg_state(void *ctx, uint32_t uid, uint32_t nonce, passport_t *pp) {
    vgtank_t *g = &VG[vg_of(ctx)]; int i;
    if ((i = gn_find(g->given, g->ngiven, uid, nonce)) >= 0) { if (pp) *pp = g->given[i].pp; return LINK_GS_GIVEN; }
    if ((i = gn_find(g->arriving, g->narr, uid, nonce)) >= 0) { if (pp) *pp = g->arriving[i].pp; return LINK_GS_ARRIVING; }
    return vg_recv_has(g, uid, nonce) ? LINK_GS_ALIVE : LINK_GS_NONE;
}
static int vg_room_v(void *ctx) { vgtank_t *g = &VG[vg_of(ctx)]; int n = 0; for (int i = 0; i <= LINK_VISIT_MAX; i++) n += g->v[i].used; return LINK_VISIT_MAX - n; }
static int vg_room_f(void *ctx) { vgtank_t *g = &VG[vg_of(ctx)]; return VG_FISH - vg_n(g) - g->narr; }
static bool vg_ours(void *ctx, uint32_t uid) { return vg_f(&VG[vg_of(ctx)], uid) != NULL; }   /* (on our roster, home or away: never a visitor) */
static bool vg_visitor(void *ctx, uint32_t uid, passport_t *pp) { vgv_t *v = vg_v(&VG[vg_of(ctx)], uid); if (!v) return false; *pp = v->pp; return true; }
static void vg_boot(int i) {
    mtank_t *t = &T[i]; vgtank_t *g = &VG[i];
    link_host_t h = { t, h_send, vg_room_v, vg_visitor, vg_room_f, vg_state, NULL, vg_ours };
    link_init(t->L, t->uid, &h, rnd(), now);
    t->alive = true; t->radio = true;
    g->njob = 0; memset(g->v, 0, sizeof g->v);                           /* visitors are never saved */
    for (int k = 0; k < VG_FISH; k++) { vgf_t *f = &g->f[k]; f->offered = f->giving = false; f->cross_at = 0; if (f->st == 2) f->restore_at = now + LINK_RESTORE_MS; }
    for (int k = 0; k < g->ngiven; k++) { vgf_t *f = vg_f(g, g->given[k].uid); if (f) f->st = 0; }              /* a GIVEN note wins */
    for (int k = 0; k < g->narr; ) { if (vg_f(g, g->arriving[k].uid)) { vg_recv_add(g, g->arriving[k].uid, g->arriving[k].nonce); gn_drop(g->arriving, &g->narr, k); } else k++; }
    link_mode(t->L, true);
}
static void vg_events(int i) {
    mtank_t *t = &T[i]; vgtank_t *g = &VG[i]; link_ev_t e;
    while (link_poll(t->L, &e)) {
        vgf_t *f = vg_f(g, e.uid);
        switch (e.type) {
        case LEV_CROSS_OK: if (f && f->offered) f->cross_at = now + rndi(0, 2600); break;
        case LEV_CROSS_NO: if (f) { f->offered = false; f->cross_at = 0; } break;
        case LEV_VISITOR_IN: {
            LCHECK(!vg_v(g, e.uid), "tank %d: a visitor arrived twice", i);
            for (int k = 0; k <= LINK_VISIT_MAX; k++) if (!g->v[k].used) { g->v[k].used = true; g->v[k].uid = e.uid; g->v[k].home = e.peer; g->v[k].pp = e.pp; g->v[k].edge = e.edge; g->v[k].leave_at = now + rndi(2000, 40000); vs_.visits++; break; }
            break; }
        case LEV_VISITOR_POOF: { vgv_t *v = vg_v(g, e.uid); if (v) v->used = false; break; }
        case LEV_VISITOR_HOMEWARD: { vgv_t *v = vg_v(g, e.uid); if (v) v->leave_at = now + rndi(200, 3000); break; }
        case LEV_FISH_BACK: case LEV_FISH_RESTORE:
            if (f && f->st == 2) { f->st = 1; f->restore_at = 0; if (e.pp.uid == f->uid) f->pp = e.pp; }
            break;
        case LEV_GIVE_ASK: vs_.asked++; vg_job(g, GJ_ANSWER, now + rndi(300, 15000), &e, rndf() < 0.8f); break;
        case LEV_GIVE_TAKEN: vs_.taken++; vg_job(g, GJ_TAKEN_NOTE, now + rndi(0, 300), &e, true); break;
        case LEV_GIVE_NO: if (f) f->giving = false; break;
        case LEV_GIVE_ALIVE: vg_job(g, GJ_ALIVE_ADD, now + rndi(0, 300), &e, true); break;
        case LEV_GIVE_CANCEL: gn_drop(g->arriving, &g->narr, gn_find(g->arriving, g->narr, e.uid, e.nonce)); break;
        case LEV_GIVE_CLOSED: gn_drop(g->given, &g->ngiven, gn_find(g->given, g->ngiven, e.uid, e.nonce)); vs_.closed++; break;
        case LEV_GIVE_RESTORE: {
            int k = gn_find(g->given, g->ngiven, e.uid, e.nonce);
            if (k >= 0) { vg_add(g, &g->given[k].pp); gn_drop(g->given, &g->ngiven, k); }
            vs_.restored++; break; }
        case LEV_LINKED:
            for (int k = 0; k < g->ngiven; k++) if (g->given[k].peer == e.peer) link_give_resume_out(t->L, e.peer, g->given[k].uid, g->given[k].nonce, &g->given[k].pp);
            for (int k = 0; k < g->narr; k++) if (g->arriving[k].peer == e.peer) link_give_resume_in(t->L, e.peer, g->arriving[k].uid, g->arriving[k].nonce, &g->arriving[k].pp);
            break;
        default: break;
        }
    }
}
static void vg_work(int i) {
    mtank_t *t = &T[i]; vgtank_t *g = &VG[i];
    for (int k = 0; k < g->njob; ) {
        gjob_t j = g->job[k];
        if ((int32_t)(now - j.at) < 0) { k++; continue; }
        g->job[k] = g->job[--g->njob];
        switch (j.type) {
        case GJ_ANSWER:
            if (j.yes && vg_room_f(t) > 0 && g->narr < 8) {
                gnote_t *n = &g->arriving[g->narr++]; n->uid = j.uid; n->nonce = j.nonce; n->peer = j.peer; n->pp = j.pp;
                if (!link_give_answer(t->L, j.uid, j.nonce, true)) gn_drop(g->arriving, &g->narr, g->narr - 1);
            } else link_give_answer(t->L, j.uid, j.nonce, false);
            break;
        case GJ_TAKEN_NOTE: {                       /* save 1: the note (the host's latest when it is next door: the event's) */
            vgf_t *f = vg_f(g, j.uid);
            LCHECK(f, "tank %d: TAKEN for a fish not on its roster", i);
            if (!f) break;
            if (g->ngiven < 8) { gnote_t *n = &g->given[g->ngiven++]; n->uid = j.uid; n->nonce = j.nonce; n->peer = j.peer; n->pp = f->st == 2 ? j.pp : f->pp; }
            if (f->st == 2) vs_.here++;
            j.type = GJ_TAKEN_OUT; j.at = now + rndi(0, 2600); g->job[g->njob++] = j;   /* (next door: the GIVEN stamp's 2.5 s) */
            break; }
        case GJ_TAKEN_OUT: { vgf_t *f = vg_f(g, j.uid); if (f) f->st = 0; link_give_done(t->L, j.uid, j.nonce, NULL); break; }
        case GJ_ALIVE_ADD: {                        /* save 1: the fish in - a visitor of it becomes ours where it swims */
            vgv_t *v = vg_v(g, j.uid);
            passport_t pp = v ? v->pp : j.pp;
            if (v) v->used = false;
            vg_add(g, &pp); vg_recv_add(g, j.uid, j.nonce);
            j.type = GJ_ALIVE_DONE; j.at = now + rndi(0, 200); g->job[g->njob++] = j;
            break; }
        case GJ_ALIVE_DONE:
            gn_drop(g->arriving, &g->narr, gn_find(g->arriving, g->narr, j.uid, j.nonce));
            link_give_alive(t->L, j.uid, j.nonce);
            break;
        }
    }
}
static uint32_t vg_all[2 * VG_FISH]; static int vg_all_n;
static void vg_check(void) {
    for (int u = 0; u < vg_all_n; u++) {
        int swim = 0, own = 0, notes = 0;
        for (int i = 0; i < 2; i++) {
            vgf_t *f = vg_f(&VG[i], vg_all[u]);
            own += f != NULL; swim += f && f->st == 1;
            swim += T[i].alive && vg_v(&VG[i], vg_all[u]) != NULL;
            notes += gn_find(VG[i].given, VG[i].ngiven, vg_all[u], 0) >= 0;
        }
        LCHECK(swim <= 1, "NEVER TWO: fish %08x swims in both tanks", vg_all[u]);
        LCHECK(own <= 1, "NEVER TWO OWNERS: fish %08x is on both rosters", vg_all[u]);
        LCHECK(own + notes >= 1, "NEVER LOST: fish %08x is on no roster and no GIVEN note holds it", vg_all[u]);
    }
}
static int give_here_run(uint32_t seed, uint32_t chaos_ms, bool quiet) {
    nt = 2; rs = seed; now = 1000; qn = 0; chaos = true; pool_n = 0;
    memset(drop_p, 0, sizeof drop_p); memset(cut_until, 0, sizeof cut_until); memset(&vs_, 0, sizeof vs_);
    int f0 = fails; vg_all_n = 0;
    for (int i = 0; i < 2; i++) {
        mtank_t *t = &T[i]; link_t *keep = t->L; memset(t, 0, sizeof *t); t->L = keep; t->uid = rnd() | 1;
        vgtank_t *g = &VG[i]; memset(g, 0, sizeof *g);
        for (int k = 0; k < 4; k++) {
            passport_t p; memset(&p, 0, sizeof p); p.uid = rnd() | 1; p.home_tank = t->uid; snprintf(p.name, sizeof p.name, "v%d%d", i, k);
            p.stage = STAGE_ADULT; p.size = 1; p.bold = p.sociable = 0.5f;
            vg_add(g, &p); vg_all[vg_all_n++] = p.uid;
        }
        vg_boot(i);
    }
    T[0].arm_at[LINK_RIGHT] = now + 10; T[1].arm_at[LINK_LEFT] = now + 300;
    uint32_t end = now + chaos_ms, settle_end = end + 15 * 60000;
    while ((int32_t)(now - settle_end) < 0) {
        bool calm = (int32_t)(now - end) >= 0;
        if (calm && chaos) { chaos = false; memset(drop_p, 0, sizeof drop_p); memset(cut_until, 0, sizeof cut_until); for (int i = 0; i < 2; i++) if (!T[i].alive) vg_boot(i); }
        if (!calm) {
            if (rndf() < 0.0004f) { int a = (int)(rnd() % 2); cut_until[a][!a] = now + rndi(1000, 20000); if (rnd() & 1) cut_until[!a][a] = cut_until[a][!a]; }
            if (rndf() < 0.0005f) { int a = (int)(rnd() % 2); drop_p[a][!a] = rndf() * 0.45f; }
            if (rndf() < 0.0002f) { int a = (int)(rnd() % 2); if (T[a].alive) { T[a].alive = false; T[a].back_at = now + rndi(1000, 20000); vs_.crashes++; } }
        }
        for (int i = 0; i < 2; i++) {
            mtank_t *t = &T[i]; vgtank_t *g = &VG[i];
            if (!t->alive) { if ((int32_t)(now - t->back_at) >= 0) vg_boot(i); else continue; }
            for (int e = 0; e < LINK_EDGES; e++) if (t->arm_at[e] && (int32_t)(now - t->arm_at[e]) >= 0) { link_mode(t->L, true); link_arm(t->L, (link_edge_t)e); t->arm_at[e] = 0; }
            link_edge_t door = i == 0 ? LINK_RIGHT : LINK_LEFT;
            for (int k = 0; k < VG_FISH; k++) {
                vgf_t *f = &g->f[k];
                if (f->st == 1 && !f->offered && !f->giving && !calm && rndf() < 0.0008f) { f->offered = true; link_offer(t->L, door, &f->pp, 0.5f, 0, 30); }
                if (f->st == 1 && f->cross_at && (int32_t)(now - f->cross_at) >= 0) { f->cross_at = 0; f->offered = false; if (link_cross(t->L, f->uid)) f->st = 2; }
                if (f->st == 2 && f->restore_at && (int32_t)(now - f->restore_at) >= 0) { f->restore_at = 0; if (!link_is_away(t->L, f->uid)) f->st = 1; }
            }
            for (int k = 0; k <= LINK_VISIT_MAX; k++) {      /* visitors swim home through the door when their time is up (or at the calm) */
                vgv_t *v = &g->v[k];
                if (v->used && (calm || (int32_t)(now - v->leave_at) >= 0)) { link_leaving(t->L, v->uid, &v->pp, 0.5f, 0, 30); v->used = false; }
            }
            if (!calm && rndf() < 0.003f && vg_n(g) > 2) {   /* a keeper gives one - home, or next door where it is */
                int k = (int)(rnd() % VG_FISH); vgf_t *f = &g->f[k];
                if (f->st && !f->giving && !f->offered && !f->cross_at) {
                    int e = f->st == 2 ? link_away_edge(t->L, f->uid) : (int)door;
                    if (e >= 0 && link_give(t->L, (link_edge_t)e, &f->pp)) f->giving = true;
                }
            }
            if (!calm && rndf() < 0.0003f) for (int k = 0; k < VG_FISH; k++) if (g->f[k].st && g->f[k].giving) { link_give_cancel(t->L, g->f[k].uid); break; }
        }
        if (T[0].alive && T[1].alive && !link_neighbor(T[0].L, LINK_RIGHT) && !T[0].arm_at[LINK_RIGHT] && rndf() < 0.01f) { T[0].arm_at[LINK_RIGHT] = now + 10; T[1].arm_at[LINK_LEFT] = now + 300; }
        deliver();
        for (int i = 0; i < 2; i++) if (T[i].alive) { link_tick(T[i].L, now); vg_events(i); vg_work(i); vg_events(i); }
        vg_check();
        if (calm) {
            bool done = true;
            for (int i = 0; i < 2; i++) { done &= VG[i].ngiven == 0 && VG[i].narr == 0 && VG[i].njob == 0; for (int k = 0; k < VG_FISH; k++) done &= VG[i].f[k].st != 2; for (int k = 0; k <= LINK_VISIT_MAX; k++) done &= !VG[i].v[k].used; }
            if (done) break;
        }
        now += STEP_MS;
    }
    for (int i = 0; i < 2; i++) {
        LCHECK(VG[i].ngiven == 0 && VG[i].narr == 0, "tank %d: notes never settled (%d given, %d arriving)", i, VG[i].ngiven, VG[i].narr);
        for (int k = 0; k < VG_FISH; k++) LCHECK(VG[i].f[k].st != 2, "tank %d: fish %08x still away at the end", i, VG[i].f[k].uid);
    }
    for (int u = 0; u < vg_all_n; u++) { int own = 0; for (int i = 0; i < 2; i++) own += vg_f(&VG[i], vg_all[u]) != NULL; LCHECK(own == 1, "fish %08x on %d rosters at the end", vg_all[u], own); }
    if (!quiet || fails != f0)
        printf("selftest-give: WHERE IT IS, seed %u, %u min: %d visits, %d asked, %d taken (%d given next door, where they were), %d delivered, %d back from a note; %d crashes; %d+%d fish at the end\n",
               seed, chaos_ms / 60000, vs_.visits, vs_.asked, vs_.taken, vs_.here, vs_.closed, vs_.restored, vs_.crashes, vg_n(&VG[0]), vg_n(&VG[1]));
    return vs_.here;
}
int selftest_give(void) {
    fails = 0;
    {   /* the notes' save slot (gift.c): written, read back into a new book, the same */
        setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-gift.sav", 1);
        static gift_book_t a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); a.persist = b.persist = true;
        gift_clear(&a);
        passport_t pp; memset(&pp, 0, sizeof pp); pp.uid = 0x1234567u; pp.home_tank = 0xabcdu; snprintf(pp.name, sizeof pp.name, "kelp");
        pp.stage = STAGE_ADULT; pp.size = 1; pp.bold = 0.25f; pp.sociable = 0.75f; pp.shape = 1; pp.fins = 2; pp.pattern = 3;
        gift_given_add(&a, pp.uid, 0x11u, 0x22u, &pp);
        passport_t p2 = pp; p2.uid = 0x7654321u; snprintf(p2.name, sizeof p2.name, "nori");
        gift_arriving_add(&a, p2.uid, 0x33u, 0x44u, &p2);
        gift_arriving_add(&a, 0x999u, 0x55u, 0x44u, &p2);                 /* (one arriving that then comes alive) */
        p2.uid = 0x999u; gift_arriving_add(&a, 0x999u, 0x55u, 0x44u, &p2); gift_received(&a, 0x999u, 0x55u);
        gift_load(&b);
        passport_t got;
        LCHECK(b.n_given == 1 && b.n_arr == 1 && b.n_recv == 1, "gift notes: read back %d/%d/%d, want 1/1/1", b.n_given, b.n_arr, b.n_recv);
        LCHECK(gift_state(&b, pp.uid, 0x11u, &got) == LINK_GS_GIVEN && !strcmp(got.name, "kelp") && got.shape == 1 && got.pattern == 3 && b.given[0].peer == 0x22u,
               "gift notes: the GIVEN note did not come back whole");
        LCHECK(gift_state(&b, 0x7654321u, 0x33u, &got) == LINK_GS_ARRIVING && !strcmp(got.name, "nori"), "gift notes: the ARRIVING note did not come back");
        LCHECK(gift_state(&b, 0x999u, 0x55u, NULL) == LINK_GS_ALIVE && gift_state(&b, 0x999u, 0x56u, NULL) == LINK_GS_NONE, "gift notes: RECEIVED did not come back");
        gift_clear(&a); gift_load(&b);
        LCHECK(b.n_given == 0 && b.n_arr == 0 && b.n_recv == 0, "gift notes: a cleared book read back non-empty");
        printf("selftest-give: the notes' save slot: GIVEN, ARRIVING and RECEIVED read back whole; a cleared book stays empty\n");
        /* a save that fails takes the note back out: a fish never leaves on a GIVEN note that is not on the flash,
           a yes never goes out on an ARRIVING note that is not (2026-11-02) */
        setenv("POCKET_TANK_SAVE", "/dev/null/no-such-dir/gift.sav", 1);
        LCHECK(!gift_given_add(&a, pp.uid, 0x11u, 0x22u, &pp) && !gift_given_has(&a, pp.uid) && a.n_given == 0, "gift notes: a GIVEN note the flash refused still counted");
        LCHECK(!gift_arriving_add(&a, pp.uid, 0x11u, 0x22u, &pp) && a.n_arr == 0 && gift_state(&a, pp.uid, 0x11u, NULL) == LINK_GS_NONE, "gift notes: an ARRIVING note the flash refused still counted");
        setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-gift.sav", 1);
        LCHECK(gift_given_add(&a, pp.uid, 0x11u, 0x22u, &pp) && a.n_given == 1, "gift notes: a note did not save once the flash took it");
        gift_clear(&a);
        printf("selftest-give: a note the flash refuses is taken back out (no fish leaves on it, no yes goes out on it)\n");
    }
    {   /* GIVEN BACK (0.4 rc round two): A gives F to B, B's GIVEN_OK is lost (A keeps GIVEN(F)); F comes back to A from a
           third tank C, B -> C -> A. The stale note read as a gift cut short at A's next boot (gift_boot) and took F out -
           and fish_held counted F twice. Through the door (GIVE_ALIVE adopts it) and given where it is (a visitor here) */
        setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-givenback.sav", 1);
        static tank_t A; static gift_book_t GA2;
        const uint32_t B = 0xB0B0B0B0u, C = 0xC0C0C0C0u; const int f0 = fails;
        for (int where = 0; where < 2; where++) {
            remove("/tmp/pocket-tank-selftest-givenback.sav"); remove("/tmp/pocket-tank-selftest-givenback.sav.gifts");
            tank_init(&A, 77); memset(&GA2, 0, sizeof GA2); GA2.persist = true; gift_attach(&A, &GA2); progression_boot(&A);
            while (A.n_fish < 4) tank_add_fish(&A, 0, 1);
            tank_ensure_ids(&A); for (int i = 0; i < A.n_fish; i++) A.fish[i].stage = STAGE_ADULT;
            progression_save(&A);
            const uint32_t F = A.fish[3].uid; passport_t pp; passport_from_fish(&pp, &A.fish[3], A.uid, 3600);
            gift_given_add(&GA2, F, 11, B, &pp); progression_give_out(&A, 3);   /* 1. A gave F to B (its note, F out): B's GIVEN_OK lost */
            gift_arriving_add(&GA2, F, 99, C, &pp);                           /* 2. C offers F, A's keeper says ACCEPT ... */
            if (where) {                                                      /* ... (given where it is: F visiting A from C) */
                const int k = 0; A.fish[N_FISH_MAX + k] = A.fish[0]; A.fish[N_FISH_MAX + k].uid = F; A.fish[N_FISH_MAX + k].away = false;
                A.vis_used[k] = true; A.vis_home[k] = C; A.vis_age[k] = 3600;
            }
            link_ev_t ev; memset(&ev, 0, sizeof ev); ev.type = LEV_GIVE_ALIVE; ev.uid = F; ev.peer = C; ev.nonce = 99; ev.pp = pp;
            tank_link_event(&A, NULL, &ev);                                   /* ... and C's GIVEN: F is A's again */
            int n = 0; for (int i = 0; i < A.n_fish; i++) n += A.fish[i].uid == F;
            LCHECK(n == 1 && !gift_given_has(&GA2, F) && progression_fish_held(&A) == A.n_fish,
                   "given back (%s): F here %dx, its old GIVEN note %s, held %d for %d fish", where ? "where it is" : "the door", n,
                   gift_given_has(&GA2, F) ? "STILL THERE" : "gone", progression_fish_held(&A), A.n_fish);
            progression_save(&A);
            tank_init(&A, 77); memset(&GA2, 0, sizeof GA2); GA2.persist = true; gift_attach(&A, &GA2); progression_boot(&A);   /* 3. the next boot */
            n = 0; for (int i = 0; i < A.n_fish; i++) n += A.fish[i].uid == F;
            LCHECK(n == 1 && A.n_fish == 4, "given back (%s): after the next boot F is %s (%d fish, want 4)", where ? "where it is" : "the door", n ? "here" : "GONE", A.n_fish);
            gift_clear(&GA2); gift_attach(&A, NULL);
        }
        if (fails == f0) printf("selftest-give: given back through a third tank (A -> B -> C -> A, B's GIVEN_OK lost): A's old note goes, the fish counts once and stays at the next boot (the door, where it is)\n");
    }
    link_t *L[2]; for (int i = 0; i < 2; i++) { L[i] = calloc(1, (size_t)link_size()); T[i].L = L[i]; }
    max_delay_ms = 300;
    int closed = 0, restored = 0;
    for (uint32_t s = 1; s <= 4; s++) { closed += give_run(100 + s, 30 * 60000, false); restored += gs.restored; }
    int soak = getenv("LINK_SOAK") ? atoi(getenv("LINK_SOAK")) : 30;
    for (int r = 0; r < soak; r++) { closed += give_run(5000 + (uint32_t)r * 7919u, 10 * 60000, true); restored += gs.restored; }
    printf("selftest-give: + %d more runs of 10 min; %d gifts delivered in all, %d brought back by a GIVEN note (the receiver's note lost), checked after every 10 ms step\n", soak, closed, restored);
    LCHECK(restored > 0, "no gift ever came back from a GIVEN note: that path is not tested");
    LCHECK(closed > 200, "only %d gifts delivered: the test is not giving", closed);
    {   /* GIVE WHERE IT IS: visits and gifts together */
        int here = 0;
        for (uint32_t s2 = 1; s2 <= 2; s2++) here += give_here_run(700 + s2, 30 * 60000, false);
        int soak2 = getenv("LINK_SOAK") ? atoi(getenv("LINK_SOAK")) : 20;
        for (int r = 0; r < soak2; r++) here += give_here_run(9000 + (uint32_t)r * 7919u, 10 * 60000, true);
        printf("selftest-give: WHERE IT IS: + %d more runs of 10 min; %d fish given next door where they were, checked after every 10 ms step\n", soak2, here);
        LCHECK(here > 20, "only %d fish given next door: the path is not tested", here);
    }
    {   /* A RING OF ONE (0.4.2, LK-2): RECEIVED keeps one gift, so nearly every lost GIVEN_OK is answered past it - by the
           receiver's own fish alone (link_host_t.ours). The same promises, after every 10 ms step */
        g_recv_cap = 1;
        int c1 = 0, h1 = 0;
        for (int r = 0; r < 8; r++) c1 += give_run(31000 + (uint32_t)r * 7919u, 10 * 60000, true);
        for (int r = 0; r < 6; r++) h1 += give_here_run(37000 + (uint32_t)r * 7919u, 10 * 60000, true);
        g_recv_cap = 32;
        printf("selftest-give: RECEIVED a ring of one: %d gifts through the door and %d where they were, never two, never zero (LK-2)\n", c1, h1);
        LCHECK(c1 > 40 && h1 > 5, "the ring-of-one runs gave too little (%d, %d)", c1, h1);
    }
    for (int i = 0; i < 2; i++) free(L[i]);
    printf("selftest-give: %s\n", fails ? "FAILED" : "OK");
    return fails ? 1 : 0;
}

/* --selftest-clock's visit (0.4): two tanks whose clocks read day0, linked; A's fish goes through the door and, never
   called home, comes back on its own (its pull home grows with its time next door: DOOR_HOME_PULL_S, then certain within
   DOOR_HOME_FULL_S). 0 = it did. */
int selftest_clock_doors(double day0) {
    d_fails = 0; dnow = 1000; dqn = 0; d_deaf[0] = d_deaf[1] = false;
    tank_init(&DA, 41); tank_new_population(&DA); tank_ensure_ids(&DA);
    tank_init(&DB, 42); tank_new_population(&DB); tank_ensure_ids(&DB);
    for (int i = 0; i < 2; i++) { DA.fish[i].stage = DB.fish[i].stage = STAGE_ADULT; }
    if (!LA) LA = calloc(1, (size_t)link_size());
    if (!LB) LB = calloc(1, (size_t)link_size());
    memset(LA, 0, (size_t)link_size()); memset(LB, 0, (size_t)link_size());
    link_host_t ha = tank_link_host(&DA, d_send), hb = tank_link_host(&DB, d_send);
    link_init(LA, DA.uid, &ha, 1, dnow); link_init(LB, DB.uid, &hb, 2, dnow);
    DA.clock = DB.clock = day0;
    link_mode(LA, true); link_mode(LB, true); d_step(10);
    link_arm(LA, LINK_RIGHT); d_step(5); link_arm(LB, LINK_LEFT); d_step(30);
    DCHECK(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT], "clock: the doors did not open");
    d_step(100);
    uint32_t uid = DA.fish[0].uid; int in = -1, home = -1;
    d_head_for(&DA, 0, LINK_RIGHT);
    for (int n = 0; n < 1800 && in < 0; n++) { d_step(1); if (d_vis_of(&DB, uid) >= 0) in = n; }
    DCHECK(in >= 0, "clock: fish 0 never went through the door");
    for (int n = 0; in >= 0 && n < 30 * 600 && home < 0; n++) { d_step(1); if (d_vis_of(&DB, uid) < 0 && !DA.fish[0].away) home = n; }
    DCHECK(in < 0 || home >= 0, "clock: the visitor was still next door 10 min on (%.0f s of its clock there)",
           d_vis_of(&DB, uid) >= 0 ? (double)DB.clock - (double)DB.vis_since[d_vis_of(&DB, uid) - N_FISH_MAX] : -1.0);
    if (!d_fails) printf("selftest-clock: day %.0f - through the door in %.1f s, home on its own %.0f s later\n", day0 / 86400, in / 30.0, home / 30.0);
    link_mode(LA, false); link_mode(LB, false);
    return d_fails;
}

/* ---- --selftest-visitors (0.4.1): the room during a visit, Strato 2026-11-05: "5 local fish and 2 visitor slots so
   that a tank with 5 fish can still get a visitor or two. 7 fish cap during visitations". Two real tanks:
   V1 two full tanks linked: visitors both ways, 2 each, the third refused FULL;
   V2 a fish of ours home to a tank hosting 2: always let in, nobody sent away - 7 swimming;
   V3 two (and three) offers at once: one place, one in (the audit's F2a: both came in);
   V4 fish trading places one at a time (the audit's F2c: 8 swam);
   V5 a fry for its home with two visitors in: it waits for a place, then swims home (never a third visitor);
   V6 doors left to chance for 20 minutes: crossings both ways.
   Through all of it no tank swims more than 7 or hosts more than 2 (the swim guard, sim/main.c, fails at once too),
   and none owns more than 5. 0 = all held ---- */
static int vt_max_swim[2], vt_max_vis[2], vt_max_held[2];
static int vt_swim(const tank_t *t) { int n = 0; for (int i = 0; i < N_FISH_SLOTS; i++) n += tank_swims(t, i); return n; }
static int vt_vis(const tank_t *t) { int n = 0; for (int k = 0; k < N_VISIT_MAX; k++) n += t->vis_used[k]; return n; }
static void vt_step(int frames) {
    for (int n = 0; n < frames; n++) {
        d_step(1);
        tank_t *T2[2] = { &DA, &DB };
        for (int k = 0; k < 2; k++) {
            int s = vt_swim(T2[k]), v = vt_vis(T2[k]), h = progression_fish_held(T2[k]);
            if (s > vt_max_swim[k]) vt_max_swim[k] = s;
            if (v > vt_max_vis[k]) vt_max_vis[k] = v;
            if (h > vt_max_held[k]) vt_max_held[k] = h;
        }
    }
}
static void vt_hold_doors(tank_t *t) { for (int i = 0; i < t->n_fish; i++) t->fish[i].door_seen = t->clock + 1e6; }
static void vt_keep_visitors(tank_t *t) {             /* the visitors here stay (no pull home) */
    for (int k = 0; k < N_VISIT_MAX; k++) if (t->vis_used[k]) { t->fish[N_FISH_MAX + k].homeward = false; t->vis_since[k] = t->clock; }
}
/* home's fish i to host by its door: frames until it swims there, or -1 (refused, or not there in time) */
static int vt_visit(tank_t *home, int i, int edge, tank_t *host) {
    uint32_t u = home->fish[i].uid; d_head_for(home, i, edge);
    for (int n = 0; n < 1800 * ((TANK_W + 447) / 448); n++) {
        vt_keep_visitors(host);
        vt_step(1);
        if (d_vis_of(host, u) >= 0) return n;
        if (home->fish[i].door_refused > home->clock) return -1;
    }
    return -1;
}
static void vt_home_all(void) {                        /* every visitor both ways home */
    for (int q = 0; q < 3600; q++) {
        int left = 0;
        tank_t *T2[2] = { &DA, &DB };
        for (int t = 0; t < 2; t++) {
            for (int k = 0; k < N_VISIT_MAX; k++) if (T2[t]->vis_used[k]) { T2[t]->fish[N_FISH_MAX + k].homeward = T2[t]->fish[N_FISH_MAX + k].bolt_home = true; left++; }
            for (int i = 0; i < T2[t]->n_fish; i++) left += T2[t]->fish[i].away;
        }
        if (!left) break;
        vt_step(1);
    }
    for (int i = 0; i < DA.n_fish; i++) DA.fish[i].door_refused = 0;
    for (int i = 0; i < DB.n_fish; i++) DB.fish[i].door_refused = 0;
    vt_hold_doors(&DA); vt_hold_doors(&DB);
}
static void vt_fill(tank_t *t, int n) {
    while (t->n_fish < n) { tank_make_fish(t, t->n_fish, t->n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); t->n_fish++; }
    while (t->n_fish > n) tank_remove_fish(t, t->n_fish - 1);
    tank_ensure_ids(t);
    if (t == &DA) for (int i = 0; i < t->n_fish; i++) progression_set_age(t, i, STAGE_ADULT_AGE + 60);
    for (int i = 0; i < t->n_fish; i++) t->fish[i].stage = STAGE_ADULT;
    vt_hold_doors(t);
}
/* ---- V7-V9 (0.4.1 audit round 2: R2L-1, R2-4, R2L-2; Strato 11-05: "a fry that belongs next door waits quietly while
   the tank it would be born in has no visitor place: no courting, no dance, nothing held at its home; it comes once a
   place opens"). Each its own pair of tanks: A 3 fish, B's fish visiting it ---- */
static int vt_holds(const tank_t *t) { int n = 0; for (int h = 0; h < TANK_FRY_HOLDS; h++) n += t->fry_hold[h].peer != 0; return n; }
static void vt_link_pair(uint32_t sa, uint32_t sb, int na, int nb) {
    dnow = 1000; dqn = 0; d_deaf[0] = d_deaf[1] = false;
    tank_init(&DA, sa); progression_fresh(&DA); tank_ensure_ids(&DA);
    tank_init(&DB, sb); tank_new_population(&DB); tank_ensure_ids(&DB);
    vt_fill(&DA, na); vt_fill(&DB, nb);
    if (!LA) LA = calloc(1, (size_t)link_size());
    if (!LB) LB = calloc(1, (size_t)link_size());
    memset(LA, 0, (size_t)link_size()); memset(LB, 0, (size_t)link_size());
    link_host_t ha = tank_link_host(&DA, d_send), hb = tank_link_host(&DB, d_send);
    link_init(LA, DA.uid, &ha, 1, dnow); link_init(LB, DB.uid, &hb, 2, dnow);
    link_mode(LA, true); link_mode(LB, true); vt_step(10);
    link_arm(LA, LINK_RIGHT); vt_step(5); link_arm(LB, LINK_LEFT); vt_step(30); vt_step(400);
    vt_hold_doors(&DA); vt_hold_doors(&DB);
    while (progression_newborn() >= 0) progression_newborn_done(&DA);
}
/* V7 (R2L-1, from the round-2 link reviewer's --selftest-r2staged): our fry staged by the checklist, the coin given to one
   of two visitors. Pass 0: no courting, no dance, nothing held at its home. Pass 1, the other visitor home: it comes */
static void vt_v7(void) {
    vt_link_pair(61, 62, 3, 3);
    uint32_t par = DB.fish[0].uid, oth = DB.fish[1].uid;
    int in = (vt_visit(&DB, 0, LINK_LEFT, &DA) >= 0) + (vt_visit(&DB, 1, LINK_LEFT, &DA) >= 0);
    DCHECK(in == 2, "V7: B's two did not visit (%d)", in);
    int nB = DB.n_fish, said[2] = { 0, 0 };
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0) { DA.fry_partner = par; DA.fry_mate = 0; DA.fry_home_room = -1; DA.fry_asked = false; progression_stage_arrival(&DA); }
        int dances = 0, court = 0, born = 0, nA = DA.n_fish, held_b = 0, holds_b = 0; bool was = false; double t0 = DA.clock;
        for (int n = 0; n < 240 * 30 && !born; n++) {
            vt_keep_visitors(&DA);
            if (pass == 1) { int ov = d_vis_of(&DA, oth); if (ov >= 0) DA.fish[ov].homeward = DA.fish[ov].bolt_home = true; }
            vt_step(1); progression_tick(&DA, 0.033f);
            if (DA.spawning && !was) dances++;
            was = DA.spawning; court += DA.courting;
            if (progression_fish_held(&DB) > held_b) held_b = progression_fish_held(&DB);
            if (vt_holds(&DB) > holds_b) holds_b = vt_holds(&DB);
            if (DA.fry_give_k >= 0 || DA.n_fish != nA) born = DA.fry_give_k >= 0 ? 2 : 1;
        }
        if (pass == 0) {
            DCHECK(!court && !dances && !born, "V7: two visitors in, the coin's visitor courted anyway (%d courting frames, %d dances, born %d)", court, dances, born);
            DCHECK(!holds_b && held_b == nB, "V7: B held a place for a fry that cannot come (%d held, %d fish)", held_b, nB);
            DCHECK(DA.fry_partner == par && progression_arrival_pending(), "V7: the pair or the staged fry let go while waiting (partner %08x, staged %d)", DA.fry_partner, progression_arrival_pending());
            said[0] = !court && !dances && !born && !holds_b;
        } else {
            DCHECK(born && dances >= 1, "V7: a place free, the fry never came (%d dances, %.0f s)", dances, DA.clock - t0);
            said[1] = born;
            if (born) printf("selftest-visitors: V7. our staged fry, the coin's visitor partner, two visitors in: no courting, no dance, nothing held at B for 240 s;"
                             " the other visitor home: %s after %.0f s\n", born == 2 ? "born for B's home" : "born here", DA.clock - t0);
        }
    }
    while (progression_newborn() >= 0) progression_newborn_done(&DA);
    vt_home_all();
    link_mode(LA, false); link_mode(LB, false);
    (void)said;
}
/* V7c (the sibling): two visitors in, neither our fry's partner - our own pair courts and the fry is born here */
static void vt_v7c(void) {
    vt_link_pair(63, 64, 3, 3);
    int in = (vt_visit(&DB, 0, LINK_LEFT, &DA) >= 0) + (vt_visit(&DB, 1, LINK_LEFT, &DA) >= 0);
    DCHECK(in == 2, "V7c: B's two did not visit (%d)", in);
    DA.fry_partner = 0; DA.fry_mate = 0;
    progression_stage_arrival(&DA);
    int nA = DA.n_fish, court = 0; double t0 = DA.clock;
    for (int n = 0; n < 240 * 30 && DA.n_fish == nA; n++) { vt_keep_visitors(&DA); vt_step(1); progression_tick(&DA, 0.033f); court += DA.courting; }
    bool ours = DA.n_fish == nA + 1;
    if (ours) { const fish_t *f = &DA.fish[DA.n_fish - 1]; for (int p = 0; p < 2; p++) for (int k = 0; k < N_VISIT_MAX; k++) ours &= !(DA.vis_used[k] && DA.fish[N_FISH_MAX + k].uid == f->parent_uid[p]); }
    DCHECK(ours && court, "V7c: two visitors in and no partner among them: our own pair's fry did not come (A %d -> %d fish, %d courting frames)", nA, DA.n_fish, court);
    if (ours) printf("selftest-visitors: V7c. two visitors in, neither the partner: our own pair courted and its fry was born here after %.0f s\n", DA.clock - t0);
    while (progression_newborn() >= 0) progression_newborn_done(&DA);
    vt_home_all();
    link_mode(LA, false); link_mode(LB, false);
}
/* V8 (R2-4): the time-together pair (fry_mate set) with one visitor in: its home holds the place. A second visitor comes:
   the pair stops courting and the home lets go (a 4-fish home is not FULL for a fry that cannot come). The second one
   home: asked again, the dance, the fry - for its home (fry_force), through the place */
static void vt_v8(void) {
    vt_link_pair(65, 66, 3, 4);
    uint32_t par = DB.fish[0].uid;
    DCHECK(vt_visit(&DB, 0, LINK_LEFT, &DA) >= 0, "V8: B's partner did not visit");
    DA.fry_partner = par; DA.fry_mate = DA.fish[0].uid; DA.fry_home_room = -1; DA.fry_asked = false;
    DA.ui_cover = true;                                       /* (a page up: the pair courts, the dance waits - the hold seen first) */
    for (int n = 0; n < 90; n++) { vt_keep_visitors(&DA); vt_step(1); progression_tick(&DA, 0.033f); }
    DCHECK(DA.courting && DA.fry_home_room == 1 && vt_holds(&DB) == 1 && progression_fish_held(&DB) == POP_CAP,
           "V8: one visitor in: the pair courts and B holds the place (courting %d, room %d, holds %d, B held %d)", DA.courting, DA.fry_home_room, vt_holds(&DB), progression_fish_held(&DB));
    DCHECK(vt_visit(&DB, 1, LINK_LEFT, &DA) >= 0, "V8: B's second fish did not visit");
    DA.ui_cover = false;
    int dances = 0, court = 0, holds = 0, full = 0; bool was = false;
    for (int n = 0; n < 150 * 30; n++) {
        vt_keep_visitors(&DA); vt_step(1); progression_tick(&DA, 0.033f);
        if (n < 60) continue;                                 /* (two seconds for the call-off to reach B) */
        if (DA.spawning && !was) dances++;
        was = DA.spawning; court += DA.courting; holds += vt_holds(&DB) > 0; full += progression_fish_held(&DB) >= POP_CAP;
    }
    DCHECK(!court && !dances && !holds && !full && DA.fry_give_k < 0, "V8: two visitors in, the pair still courted or B still held (%d courting frames, %d dances, %d frames held, %d FULL)", court, dances, holds, full);
    DCHECK(DA.fry_partner == par && DA.fry_mate, "V8: the pair let go while waiting");
    int oth = d_vis_of(&DA, DB.fish[1].uid);
    int nB = DB.n_fish, born = 0; double t0 = DA.clock;
    DA.fry_force = 1;
    for (int n = 0; n < 240 * 30 && !born; n++) {
        if (oth >= 0 && d_vis_of(&DA, DB.fish[1].uid) >= 0) DA.fish[oth].homeward = DA.fish[oth].bolt_home = true;
        int pv = d_vis_of(&DA, par); if (pv >= 0) { DA.fish[pv].homeward = false; DA.vis_since[pv - N_FISH_MAX] = DA.clock; }
        vt_step(1); progression_tick(&DA, 0.033f);
        if (DB.n_fish > nB) born = 1;
    }
    DCHECK(born, "V8: the second visitor home, the fry for B never came (B %d fish, give %d, room %d)", DB.n_fish, DA.fry_give_k, DA.fry_home_room);
    if (born) printf("selftest-visitors: V8. the time-together pair with two visitors in: no courting, B let go of its place (not FULL) for 150 s; the other home: the fry came to B %.0f s later\n", DA.clock - t0);
    while (progression_newborn() >= 0) progression_newborn_done(&DA);
    vt_home_all();
    link_mode(LA, false); link_mode(LB, false);
}
/* V9 (R2L-2): THE SWIM LIMIT's guard counts a fry waiting in a visitor's place as a place (tank_link_room does) */
static int vt_guard_calls, vt_guard_vis;
static void vt_guard(const tank_t *t, int swim, int visitors, const char *path) { (void)t; (void)swim; (void)path; vt_guard_calls++; vt_guard_vis = visitors; }
static void vt_v9(void) {
    static tank_t R; memset(&R, 0, sizeof R); tank_init(&R, 67); tank_new_population(&R); tank_ensure_ids(&R);
    void (*was)(const tank_t *, int, int, const char *) = tank_link_swim_hook;
    tank_link_swim_hook = vt_guard; vt_guard_calls = 0;
    for (int k = 0; k < 2; k++) { R.fish[N_FISH_MAX + k] = R.fish[k]; R.fish[N_FISH_MAX + k].uid ^= 0x5a5a0000u + (uint32_t)k; R.vis_used[k] = true; }
    tank_link_tick(&R, NULL);
    int two = vt_guard_calls;                                 /* two visitors: within the rule */
    R.fish[N_FISH_MAX + 2] = R.fish[0]; R.fish[N_FISH_MAX + 2].uid ^= 0x0f0f0000u; R.vis_pending[2] = true;
    tank_link_tick(&R, NULL);
    tank_link_swim_hook = was;
    DCHECK(!two && vt_guard_calls == 1 && vt_guard_vis == 3, "V9: the guard: two visitors called it %d times; two + a fry waiting: %d calls, %d places counted (want 1 call, 3)", two, vt_guard_calls - two, vt_guard_vis);
    if (!two && vt_guard_calls == 1) printf("selftest-visitors: V9. the swim guard: two visitors are within the rule; two + a fry waiting in a visitor's place is three places\n");
}
/* V10 (the review's F1): a FULL home (B at 5 held) - its fry can only be ours. Two of B's fish in A, A's fry staged, the
   coin's visitor partner: the pair courts and the fry is born HERE, in base's time (27-35 s in the review's probe). V10b:
   the same on the time-together path (fry_mate set, nothing staged) */
static void vt_v10(void) {
    for (int path = 0; path < 2; path++) {
        vt_link_pair(71 + path, 73 + path, 3, POP_CAP);
        uint32_t par = DB.fish[0].uid;
        int in = (vt_visit(&DB, 0, LINK_LEFT, &DA) >= 0) + (vt_visit(&DB, 1, LINK_LEFT, &DA) >= 0);
        DCHECK(in == 2, "V10%s: B's two did not visit (%d)", path ? "b" : "", in);
        DA.fry_partner = par; DA.fry_mate = path ? DA.fish[0].uid : 0; DA.fry_home_room = -1; DA.fry_asked = false;
        if (!path) progression_stage_arrival(&DA);
        int nA = DA.n_fish, court = 0, born = 0, said = -1; double t0 = DA.clock;
        for (int n = 0; n < 120 * 30 && !born; n++) {
            vt_keep_visitors(&DA); vt_step(1);
            if (DA.fry_home_room >= 0) said = DA.fry_home_room;   /* (B's answer, before the birth clears it: fry_partner_clear) */
            progression_tick(&DA, 0.033f); court += DA.courting;
            if (DA.n_fish > nA) born = 1; else if (DA.fry_give_k >= 0) born = 2;
        }
        DCHECK(said == 0, "V10%s: B full: its answer was %d, want 0 (no room) - the fry was ours for another reason", path ? "b" : "", said);
        DCHECK(born == 1 && court && DA.clock - t0 < 60, "V10%s: B full, two visitors in: the fry is ours - not born here in 60 s (born %d, %d courting frames, %.0f s, B said %d)",
               path ? "b" : "", born, court, DA.clock - t0, said);
        if (born == 1 && said == 0) printf("selftest-visitors: V10%s. B full, two of its fish in A, %s: B said no room; courted, born HERE after %.0f s\n",
                              path ? "b" : "", path ? "the time-together pair" : "our staged fry with the coin's visitor", DA.clock - t0);
        while (progression_newborn() >= 0) progression_newborn_done(&DA);
        vt_home_all(); link_mode(LA, false); link_mode(LB, false);
    }
}
/* V11 (the review's F4): the coin rolls ON ITS OWN (visitors here a minute, vis_since not reset) while two are in. B with
   room: a visitor that wins it waits - nobody courts, nothing held at B - and the fry comes once the other leaves. B full:
   born here. A few tanks are tried until the coin falls to a visitor */
static void vt_v11(void) {
    for (int full = 0; full < 2; full++) {
        int won = 0;
        for (int tryn = 0; tryn < 8 && !won; tryn++) {
            vt_link_pair(81 + (uint32_t)tryn * 2 + (uint32_t)full * 40, 82 + (uint32_t)tryn * 2 + (uint32_t)full * 40, 3, full ? POP_CAP : 3);
            int in = (vt_visit(&DB, 0, LINK_LEFT, &DA) >= 0) + (vt_visit(&DB, 1, LINK_LEFT, &DA) >= 0);
            if (in != 2) { DCHECK(0, "V11: B's two did not visit (%d)", in); break; }
            DA.fry_partner = 0; DA.fry_mate = 0;
            for (int n = 0; n < 65 * 30; n++) {                  /* (their minute here first: the coin rolls only for a visitor here 60 s,
                                                                    and only while our fry is staged - else our own pair is quicker) */
                for (int k = 0; k < N_VISIT_MAX; k++) if (DA.vis_used[k]) DA.fish[N_FISH_MAX + k].homeward = false;
                vt_step(1); progression_tick(&DA, 0.033f);
            }
            progression_stage_arrival(&DA);
            int nA = DA.n_fish, born = 0, court_wait = 0, holds = 0; uint32_t par = 0; double t_won = -1;
            for (int n = 0; n < 240 * 30 && !born; n++) {
                for (int k = 0; k < N_VISIT_MAX; k++) if (DA.vis_used[k]) DA.fish[N_FISH_MAX + k].homeward = false;   /* (here, but their minute runs) */
                vt_step(1); progression_tick(&DA, 0.033f);
                if (!par && DA.fry_partner && d_vis_of(&DA, DA.fry_partner) >= 0) { par = DA.fry_partner; t_won = DA.clock; }
                if (par && vt_vis(&DA) == 2 && !full) { court_wait += DA.courting; holds += vt_holds(&DB) > 0; }
                if (par && !full && DA.clock - t_won > 60 && vt_vis(&DA) == 2) {   /* a minute of waiting seen: the other one home */
                    for (int k = 0; k < N_VISIT_MAX; k++) if (DA.vis_used[k] && DA.fish[N_FISH_MAX + k].uid != par) DA.fish[N_FISH_MAX + k].homeward = DA.fish[N_FISH_MAX + k].bolt_home = true;
                }
                if (DA.n_fish > nA) born = 1; else if (DA.fry_give_k >= 0) born = 2;
            }
            if (!par) { while (progression_newborn() >= 0) progression_newborn_done(&DA); vt_home_all(); link_mode(LA, false); link_mode(LB, false); continue; }
            won = 1;
            if (full) DCHECK(born == 1 && DA.clock - t_won < 60, "V11: B full, the coin's visitor: not born here within 60 s of the coin (born %d, %.0f s)", born, DA.clock - t_won);
            else {
                DCHECK(!court_wait && !holds, "V11: B with room, the coin's visitor, two in: courted %d frames, B held %d frames", court_wait, holds);
                DCHECK(born, "V11: B with room: the other visitor home, the fry never came");
            }
            if ((full && born == 1) || (!full && born && !court_wait && !holds))
                printf("selftest-visitors: V11. the coin on its own, two in, B %s: a visitor won it (try %d); %s %.0f s after the coin\n",
                       full ? "full" : "with room", tryn + 1, full ? "born here" : "it waited quietly, then came", DA.clock - t_won);
            while (progression_newborn() >= 0) progression_newborn_done(&DA);
            vt_home_all(); link_mode(LA, false); link_mode(LB, false);
        }
        DCHECK(won, "V11: in 8 tanks the coin never fell to a visitor (B %s)", full ? "full" : "with room");
    }
}
/* V2c (the review's F2): a place only PROMISED (an OFFER accepted, its GO never coming: B never hears the ACCEPT) while the
   time-together pair dances with one visitor in: the dance goes on, the home keeps its place, the fry comes */
static void vt_v2c(void) {
    vt_link_pair(91, 92, 3, 3);
    uint32_t par = DB.fish[0].uid;
    DCHECK(vt_visit(&DB, 0, LINK_LEFT, &DA) >= 0, "V2c: B's partner did not visit");
    DA.fry_partner = par; DA.fry_mate = DA.fish[0].uid; DA.fry_home_room = -1; DA.fry_asked = false;
    int nA = DA.n_fish, born = 0, blip = 0, cut = 0, called_off = 0; bool armed = false;
    for (int n = 0; n < 150 * 30 && !born; n++) {
        vt_keep_visitors(&DA);
        if (!armed && DA.spawning) {                          /* the dance under way: B's fish 1 at the door, its ACCEPT lost */
            armed = true; d_drop_type = 6 /* FR_ACCEPT */; d_drop_to = 1; d_drop_until = dnow + 6000;
            d_at_door_facing_in(&DB, 1, LINK_LEFT);
        }
        bool sp = DA.spawning; uint32_t ask = DA.fry_ask_uid;
        vt_step(1); progression_tick(&DA, 0.033f);
        if (DA.vis_coming > 0) { blip++; if (sp && !DA.spawning && DA.n_fish == nA && DA.fry_give_k < 0) cut++; if (ask && !DA.fry_ask_uid) called_off++; }
        if (DA.n_fish > nA || DA.fry_give_k >= 0) born = 1;
    }
    d_drop_type = 0;
    DCHECK(blip > 0, "V2c: no promised place seen (the ACCEPT drop did not hold one)");
    DCHECK(!cut && !called_off && born, "V2c: a promised place cut the dance %d times, called the home off %d times; fry %d", cut, called_off, born);
    if (blip && !cut && !called_off && born) printf("selftest-visitors: V2c. a place promised for %.1f s mid-dance (its GO never came): the dance went on, the home kept its place, the fry came\n", blip / 30.0);
    while (progression_newborn() >= 0) progression_newborn_done(&DA);
    vt_home_all(); link_mode(LA, false); link_mode(LB, false);
}
int selftest_visitors(void) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-visitors.sav", 1);
    d_fails = 0; dnow = 1000; dqn = 0; d_deaf[0] = d_deaf[1] = false;
    memset(vt_max_swim, 0, sizeof vt_max_swim); memset(vt_max_vis, 0, sizeof vt_max_vis); memset(vt_max_held, 0, sizeof vt_max_held);
    tank_init(&DA, 71); progression_fresh(&DA); tank_ensure_ids(&DA);
    tank_init(&DB, 72); tank_new_population(&DB); tank_ensure_ids(&DB);
    vt_fill(&DA, POP_CAP); vt_fill(&DB, POP_CAP);
    if (!LA) LA = calloc(1, (size_t)link_size());
    if (!LB) LB = calloc(1, (size_t)link_size());
    memset(LA, 0, (size_t)link_size()); memset(LB, 0, (size_t)link_size());
    link_host_t ha = tank_link_host(&DA, d_send), hb = tank_link_host(&DB, d_send);
    link_init(LA, DA.uid, &ha, 1, dnow); link_init(LB, DB.uid, &hb, 2, dnow);
    link_mode(LA, true); link_mode(LB, true); vt_step(10);
    link_arm(LA, LINK_RIGHT); vt_step(5); link_arm(LB, LINK_LEFT); vt_step(30); vt_step(400);
    DCHECK(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT], "the doors did not open");
    vt_hold_doors(&DA); vt_hold_doors(&DB);

    /* V1. two full tanks linked: 2 visitors each way, the third refused as FULL */
    DCHECK(tank_link_room(&DA) == 2 && tank_link_room(&DB) == 2, "V1: a full tank's room %d / %d, want 2", tank_link_room(&DA), tank_link_room(&DB));
    int a_in = 0, b_in = 0;
    a_in += vt_visit(&DA, 0, LINK_RIGHT, &DB) >= 0; a_in += vt_visit(&DA, 1, LINK_RIGHT, &DB) >= 0;
    b_in += vt_visit(&DB, 0, LINK_LEFT, &DA) >= 0;  b_in += vt_visit(&DB, 1, LINK_LEFT, &DA) >= 0;
    DCHECK(a_in == 2 && b_in == 2, "V1: two full tanks: %d of A's and %d of B's got through (want 2 and 2)", a_in, b_in);
    DCHECK(tank_link_room(&DA) == 0 && tank_link_room(&DB) == 0, "V1: two visitors in, room %d / %d, want 0", tank_link_room(&DA), tank_link_room(&DB));
    int a3 = vt_visit(&DA, 2, LINK_RIGHT, &DB); double a3_for = DA.fish[2].door_refused - DA.clock;
    int b3 = vt_visit(&DB, 2, LINK_LEFT, &DA);
    DCHECK(a3 < 0 && b3 < 0 && vt_vis(&DA) == 2 && vt_vis(&DB) == 2, "V1: a third visitor got in (A's %d, B's %d; visitors %d / %d)", a3, b3, vt_vis(&DA), vt_vis(&DB));
    DCHECK(a3_for > TANK_LINK_FULL_S - 2, "V1: the third was not refused as FULL (refused for %.0f s)", a3_for);
    if (!d_fails) printf("selftest-visitors: V1. two full tanks linked: 2 of A's in B, 2 of B's in A, the third refused FULL each way; each swims %d (3 of its own + 2)\n", vt_swim(&DA));

    /* V2. A's two come home while A hosts B's two: let in, nobody sent away - 5 + 2 = 7 */
    {   int mx0 = vt_max_swim[0]; (void)mx0; vt_max_swim[0] = 0;
        int home = -1;
        for (int q = 0; q < 3600 && home < 0; q++) {
            for (int k = 0; k < N_VISIT_MAX; k++) if (DB.vis_used[k]) DB.fish[N_FISH_MAX + k].homeward = DB.fish[N_FISH_MAX + k].bolt_home = true;
            vt_keep_visitors(&DA);
            vt_step(1);
            if (!DA.fish[0].away && !DA.fish[1].away && DA.fish[0].door == DOOR_NONE && DA.fish[1].door == DOOR_NONE) home = q;
        }
        bool stay = vt_vis(&DA) == 2; for (int k = 0; k < N_VISIT_MAX; k++) if (DA.vis_used[k]) stay &= !DA.fish[N_FISH_MAX + k].homeward;
        DCHECK(home >= 0, "V2: A's two did not come home (away %d %d)", DA.fish[0].away, DA.fish[1].away);
        DCHECK(stay, "V2: a visitor was sent away to make room (%d visitors)", vt_vis(&DA));
        DCHECK(vt_swim(&DA) == TANK_LINK_SWIM_MAX && vt_max_swim[0] == TANK_LINK_SWIM_MAX, "V2: A swims %d (most %d), want %d", vt_swim(&DA), vt_max_swim[0], TANK_LINK_SWIM_MAX);
        DCHECK(tank_link_room(&DA) == 0, "V2: A room %d at 7 swimming", tank_link_room(&DA));
        if (home >= 0 && stay) printf("selftest-visitors: V2. A's two came home %.1f s after the call while A hosted two of B's: all let in, nobody sent away - A swam %d at most\n", home / 30.0, vt_max_swim[0]);
        vt_home_all();
    }
    /* V3. two offers at once: A holds 5 and hosts one of B's (one place left); two of B's reach the door together */
    {   int in1 = vt_visit(&DB, 0, LINK_LEFT, &DA) >= 0;
        DCHECK(in1 && tank_link_room(&DA) == 1, "V3: the first visitor (in %d), room %d, want 1", in1, tank_link_room(&DA));
        d_at_door_facing_in(&DB, 1, LINK_LEFT); d_at_door_facing_in(&DB, 2, LINK_LEFT); DB.fish[2].y += 20;
        int both_asked = -1, offers = 0;
        for (int n = 0; n < 600; n++) {
            vt_keep_visitors(&DA); vt_step(1);
            if (both_asked < 0 && DB.fish[1].door_sent && DB.fish[2].door_sent) both_asked = n;
            offers = (d_vis_of(&DA, DB.fish[1].uid) >= 0) + (d_vis_of(&DA, DB.fish[2].uid) >= 0);
        }
        DCHECK(both_asked >= 0, "V3: the two never asked together");
        DCHECK(offers == 1 && vt_vis(&DA) == 2, "V3: two offers at once with one place: %d came in (%d visitors)", offers, vt_vis(&DA));
        int refused = (DB.fish[1].door_refused > DB.clock) + (DB.fish[2].door_refused > DB.clock);
        DCHECK(refused == 1, "V3: the other was not refused (%d refused)", refused);
        if (offers == 1) printf("selftest-visitors: V3. one place left, two of B's at the door together (asked %.1f s apart at most): one in, the other refused - A hosts %d\n", both_asked / 30.0, vt_vis(&DA));
        vt_home_all();
        /* ... and three at once into two places */
        for (int i = 0; i < 3; i++) d_at_door_facing_in(&DB, i, LINK_LEFT);
        DB.fish[1].y += 20; DB.fish[2].y -= 20;
        for (int n = 0; n < 600; n++) { vt_keep_visitors(&DA); vt_step(1); }
        int in3 = 0; for (int i = 0; i < 3; i++) in3 += d_vis_of(&DA, DB.fish[i].uid) >= 0;
        DCHECK(in3 == 2, "V3: three at once into two places: %d came in", in3);
        if (in3 == 2) printf("selftest-visitors: V3. three of B's at the door together, two places: two in\n");
        vt_home_all();
    }
    /* V4. fish trading places one at a time (A 5, B 3), every crossing with room, then all of A's home */
    {   vt_fill(&DB, 3); vt_max_swim[0] = 0;
        int ok = 0;
        ok += vt_visit(&DA, 0, LINK_RIGHT, &DB) >= 0; ok += vt_visit(&DA, 1, LINK_RIGHT, &DB) >= 0;   /* B 3 + 2 */
        ok += vt_visit(&DB, 0, LINK_LEFT, &DA) >= 0;  ok += vt_visit(&DB, 1, LINK_LEFT, &DA) >= 0;    /* A 3 + 2 */
        ok += vt_visit(&DA, 2, LINK_RIGHT, &DB) >= 0;                                                 /* B full of visitors: refused */
        ok += vt_visit(&DB, 2, LINK_LEFT, &DA) >= 0;                                                  /* A too */
        DCHECK(ok == 4, "V4: %d of the six crossings made (want 4: the two past the places refused)", ok);
        for (int q = 0; q < 3600; q++) {                  /* A's home; B's stay at A */
            for (int k = 0; k < N_VISIT_MAX; k++) if (DB.vis_used[k]) DB.fish[N_FISH_MAX + k].homeward = DB.fish[N_FISH_MAX + k].bolt_home = true;
            vt_keep_visitors(&DA); vt_step(1);
            int away = 0; for (int i = 0; i < DA.n_fish; i++) away += DA.fish[i].away;
            if (!away && q > 60) break;
        }
        DCHECK(vt_max_swim[0] <= TANK_LINK_SWIM_MAX && vt_vis(&DA) == 2, "V4: A swam %d at most (%d visitors)", vt_max_swim[0], vt_vis(&DA));
        printf("selftest-visitors: V4. fish trading places (A 5, B 3): %d crossings, the two past the places refused; all of A's home: A swam %d at most (the audit's run: 8)\n", ok, vt_max_swim[0]);
        vt_home_all(); vt_fill(&DB, POP_CAP - 1);
    }
    /* V5. a fry for its home with two visitors in: it waits for a place (the coin stays fair: not ours because the tank is
       crowded), then is born and swims home - the visitors here never more than 2 */
    {   vt_fill(&DA, POP_CAP - 1);
        uint32_t par = DB.fish[0].uid, oth = DB.fish[1].uid;
        int in = (vt_visit(&DB, 0, LINK_LEFT, &DA) >= 0) + (vt_visit(&DB, 1, LINK_LEFT, &DA) >= 0);
        DCHECK(in == 2, "V5: B's two did not visit (%d)", in);
        DA.fry_partner = par; DA.fry_asked = false; DA.fry_home_room = -1;
        for (int n = 0; n < 60; n++) { vt_keep_visitors(&DA); vt_step(1); }
        /* (round 2, Strato 11-05: it waits QUIETLY - with no place its home is only peeked at: its room known, nothing held there) */
        DCHECK(DA.fry_home_room == 1 && !vt_holds(&DB), "V5: two visitors in: B's room not known, or B held a place (room %d, holds %d)", DA.fry_home_room, vt_holds(&DB));
        int nA = DA.n_fish;
        DA.fry_force = 1; progression_force_arrival(&DA);
        DCHECK(DA.n_fish == nA && DA.fry_give_k < 0 && vt_vis(&DA) == 2, "V5: a fry was born with both places taken (A %d -> %d, give %d)", nA, DA.n_fish, DA.fry_give_k);
        int ov = d_vis_of(&DA, oth); if (ov >= 0) DA.fish[ov].homeward = DA.fish[ov].bolt_home = true;
        for (int n = 0; n < 900 && d_vis_of(&DA, oth) >= 0; n++) { int p = d_vis_of(&DA, par); if (p >= 0) { DA.fish[p].homeward = false; DA.vis_since[p - N_FISH_MAX] = DA.clock; } vt_step(1); }
        DCHECK(d_vis_of(&DA, oth) < 0, "V5: the other visitor never went home");
        for (int n = 0; n < 60 && !vt_holds(&DB); n++) { int p = d_vis_of(&DA, par); if (p >= 0) { DA.fish[p].homeward = false; DA.vis_since[p - N_FISH_MAX] = DA.clock; } vt_step(1); }
        DCHECK(DA.fry_home_room == 1 && vt_holds(&DB) == 1, "V5: a place free: B not asked, or not holding (room %d, holds %d)", DA.fry_home_room, vt_holds(&DB));
        DA.fry_force = 1; progression_force_arrival(&DA);
        DCHECK(DA.fry_give_k >= 0, "V5: a place free, the fry for its home was not born (give %d, A %d fish)", DA.fry_give_k, DA.n_fish);
        uint32_t fry = DA.fry_give_k >= 0 ? DA.fish[N_FISH_MAX + DA.fry_give_k].uid : 0;
        int nB = DB.n_fish, got = -1;
        for (int n = 0; n < 1800 && got < 0; n++) { vt_step(1); for (int i = 0; i < DB.n_fish; i++) if (DB.fish[i].uid == fry && !DB.fish[i].away && d_vis_of(&DA, par) < 0) got = n; }
        DCHECK(got >= 0 && DB.n_fish == nB + 1, "V5: the fry and its parent were not home (%d fish, was %d)", DB.n_fish, nB);
        if (got >= 0) printf("selftest-visitors: V5. a fry for its home with two visitors in waited; the other visitor gone, it was born and swam home with its parent (%.1f s) - A hosted %d at most\n", got / 30.0, vt_max_vis[0]);
        vt_home_all(); vt_fill(&DA, POP_CAP); vt_fill(&DB, POP_CAP);
    }
    /* V6. 20 minutes of doors left to chance: every few seconds a fish of either tank heads for the door, a visitor heads home */
    {   int cross[2] = { 0, 0 }; bool was[2][N_VISIT_MAX] = { { 0 } };
        rs = 0x5157u;
        for (int n = 0; n < 20 * 60 * 30; n++) {
            if (n % 45 == 0) {                            /* a fish of ours, home and clear of the doors, for the door */
                tank_t *h = rnd() & 1 ? &DA : &DB; int i = (int)(rnd() % (uint32_t)h->n_fish);
                if (!h->fish[i].away && h->fish[i].door == DOOR_NONE) d_head_for(h, i, h == &DA ? LINK_RIGHT : LINK_LEFT);
            }
            if (n % 70 == 0) {                            /* a visitor's time is up */
                tank_t *h = rnd() & 1 ? &DA : &DB; int k = (int)(rnd() % N_VISIT_MAX);
                if (h->vis_used[k]) h->fish[N_FISH_MAX + k].homeward = true;
            }
            vt_step(1);
            for (int t = 0; t < 2; t++) { tank_t *h = t ? &DB : &DA; for (int k = 0; k < N_VISIT_MAX; k++) { if (h->vis_used[k] && !was[t][k]) cross[t]++; was[t][k] = h->vis_used[k]; } }
        }
        DCHECK(cross[0] >= 10 && cross[1] >= 10, "V6: 20 min of open doors: %d visits into A, %d into B", cross[0], cross[1]);
        printf("selftest-visitors: V6. 20 min of doors left to chance, two full tanks: %d visits into A, %d into B\n", cross[0], cross[1]);
        vt_home_all();
    }
    link_mode(LA, false); link_mode(LB, false);
    vt_v7(); vt_v7c(); vt_v8(); vt_v9(); vt_v10(); vt_v11(); vt_v2c();                     /* (round 2: each its own pair of tanks; the maxima below cover them too) */
    for (int k = 0; k < 2; k++) {
        DCHECK(vt_max_swim[k] <= TANK_LINK_SWIM_MAX && vt_max_vis[k] <= TANK_LINK_VISIT_PLACES && vt_max_held[k] <= POP_CAP,
               "%c: %d swam, %d visited, %d owned at most (limits %d, %d, %d)", 'A' + k, vt_max_swim[k], vt_max_vis[k], vt_max_held[k], TANK_LINK_SWIM_MAX, TANK_LINK_VISIT_PLACES, POP_CAP);
        printf("selftest-visitors: %c at most %d swimming, %d visiting, %d owned\n", 'A' + k, vt_max_swim[k], vt_max_vis[k], vt_max_held[k]);
    }
    link_mode(LA, false); link_mode(LB, false);
    printf("selftest-visitors: %s\n", d_fails ? "FAILED" : "OK");
    return d_fails ? 1 : 0;
}

/* ---- 0.4.1 audit ROUND 2, reviewer "link": a soak of the visitor places with everything else on - births and
   cross-tank fry on A (progression_tick), a forced time-together pair, sales, GIVEs both ways, DEPORTs, a deaf radio.
   Every frame: no tank past 2 places (drawn + a fry waiting), 7 swimming, 5 held; no fish of B unseen for long
   (neither home nor drawn at A: an invisible visitor). Prints what it saw; FAIL lines count ---- */
static int r2_places(const tank_t *t) { int n = 0; for (int k = 0; k < N_VISIT_MAX; k++) n += t->vis_used[k] || t->vis_pending[k]; return n; }
int selftest_r2link_soak(void) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-r2soak.sav", 1);
    int seeds = getenv("R2_SEEDS") ? atoi(getenv("R2_SEEDS")) : 3, mins = getenv("R2_MINS") ? atoi(getenv("R2_MINS")) : 30;   /* (make check: a short gate; the audit ran 8-12 x 60) */
    int fails0 = 0, home_fry_all = 0, probes_all = 0, refused_all = 0;
    for (int sd = 0; sd < seeds; sd++) {
        d_fails = 0; dnow = 1000; dqn = 0; d_deaf[0] = d_deaf[1] = false;
        memset(vt_max_swim, 0, sizeof vt_max_swim); memset(vt_max_vis, 0, sizeof vt_max_vis); memset(vt_max_held, 0, sizeof vt_max_held);
        tank_init(&DA, 81 + sd); progression_fresh(&DA); tank_ensure_ids(&DA);
        tank_init(&DB, 91 + sd); tank_new_population(&DB); tank_ensure_ids(&DB);
        int na = 3 + sd % 3, nb = 5 - sd % 3;
        vt_fill(&DA, na); vt_fill(&DB, nb);
        static gift_book_t GA, GB; memset(&GA, 0, sizeof GA); memset(&GB, 0, sizeof GB);
        gift_attach(&DA, &GA); gift_attach(&DB, &GB);
        if (!LA) LA = calloc(1, (size_t)link_size());
        if (!LB) LB = calloc(1, (size_t)link_size());
        memset(LA, 0, (size_t)link_size()); memset(LB, 0, (size_t)link_size());
        link_host_t ha = tank_link_host(&DA, d_send), hb = tank_link_host(&DB, d_send);
        link_init(LA, DA.uid, &ha, 1, dnow); link_init(LB, DB.uid, &hb, 2, dnow);
        link_mode(LA, true); link_mode(LB, true); vt_step(10);
        link_arm(LA, LINK_RIGHT); vt_step(5); link_arm(LB, LINK_LEFT); vt_step(30); vt_step(400);
        DCHECK(DA.door_open[LINK_RIGHT] && DB.door_open[LINK_LEFT], "soak %d: the doors did not open", sd);
        rs = 0x9e37u + (uint32_t)sd * 7919u;
        int unseen[N_FISH_MAX] = { 0 }; uint32_t unseen_uid[N_FISH_MAX] = { 0 };
        int births = 0, cross_home = 0, kept = 0, sold = 0, gifts = 0, deports = 0, dances = 0, max_unseen = 0, deaf_n = 0, place_bad = 0;
        int max_places[2] = { 0, 0 }, deaf_left = 0, deaf_who = 0; bool was_sp = false; int nA_prev = DA.n_fish; uint32_t spell_uid = 0; int relinks = 0; uint32_t pend_probe_uid = 0; int pend_probes = 0, pend_refused = 0;
        for (int n = 0; n < mins * 60 * 30; n++) {
            /* (round 2, review F4: a quiet spell of 3 min every 10 - no fish sent to the doors, every visitor but A's partner
               home - so a cross fry for B's home can come and the to-home path runs: vis_pending, fry_give_k, LEV_FRY_OUT) */
            const bool quiet = n % (30 * 600) >= 30 * 420;
            if (quiet && spell_uid && !DA.fry_partner && n % 15 == 0)   /* (on its way until it is over: a new goal clears a door, tank.c) */
                for (int i = 0; i < DB.n_fish; i++) if (DB.fish[i].uid == spell_uid && !DB.fish[i].away && DB.fish[i].door == DOOR_NONE) {
                    DB.fish[i].hunger = 1; DB.fish[i].energy = 8;   /* (B runs no keeper: its fish starve, and the rules' SEEK FOOD clears a door) */
                    d_at_door_facing_in(&DB, i, LINK_LEFT); }
            if (getenv("R2_TRACE") && quiet && n % 150 == 0) printf("  spell sd %d t=%.0f: partner %08x mate %08x vis %d courting %d spawning %d room %d B held %d A held %d coming %d\n",
                sd, n / 30.0, DA.fry_partner, DA.fry_mate, vt_vis(&DA), DA.courting, DA.spawning, DA.fry_home_room, progression_fish_held(&DB), progression_fish_held(&DA), DA.vis_coming);
            if (n % (30 * 600) == 30 * 420 - 30 * 20 && !deaf_left && (!DA.door_open[LINK_RIGHT] || !DB.door_open[LINK_LEFT])) {   /* (a deaf spell of
                                                                    over 10 s unlinked them, and nothing linked them again: the doors
                                                                    stayed shut for the rest of the seed) */
                link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); link_arm(LB, LINK_LEFT); relinks++;
            }
            if (n % (30 * 600) == 30 * 420) {                     /* the spell begins: B with room (a sale there, if full), one of B's sent over */
                if (progression_fish_held(&DB) >= POP_CAP) for (int i = DB.n_fish - 1; i >= 0; i--) if (!DB.fish[i].away && DB.fish[i].door == DOOR_NONE) { tank_remove_fish(&DB, i); break; }
                for (int i = 0; i < DB.n_fish; i++) if (!DB.fish[i].away && DB.fish[i].door == DOOR_NONE) { d_head_for(&DB, i, LINK_LEFT); spell_uid = DB.fish[i].uid; break; }
            }
            if (quiet && !DA.fry_partner && DA.fry_give_k < 0 && DA.n_fish) {   /* the spell: a pair with one of B's here, the coin its home's */
                int pk = -1; for (int k = 0; k < N_VISIT_MAX && pk < 0; k++) if (DA.vis_used[k] && !DA.fish[N_FISH_MAX + k].homeward && DA.fish[N_FISH_MAX + k].stage >= STAGE_JUV) pk = k;
                int m = (int)(rnd() % (uint32_t)DA.n_fish);
                if (pk >= 0 && !DA.fish[m].away) { DA.fry_partner = DA.fish[N_FISH_MAX + pk].uid; DA.fry_mate = DA.fish[m].uid; DA.fry_home_room = -1; DA.fry_asked = false; DA.fry_force = 1; }
            }
            if (quiet && n % 30 == 0) {                         /* (through the spell: B keeps a place - gifts and its fish home refill it - and
                                                                    any pair here has the coin its home's) */
                if (progression_fish_held(&DB) >= POP_CAP) for (int i = DB.n_fish - 1; i >= 0; i--) if (!DB.fish[i].away && DB.fish[i].door == DOOR_NONE && DB.fish[i].uid != spell_uid) { tank_remove_fish(&DB, i); break; }
                if (DA.fry_partner) DA.fry_force = 1;
            }
            if (quiet) for (int k = 0; k < N_VISIT_MAX; k++) {
                if (DA.vis_used[k] && DA.fish[N_FISH_MAX + k].uid != DA.fry_partner) DA.fish[N_FISH_MAX + k].homeward = true;
                if (DA.vis_used[k] && DA.fish[N_FISH_MAX + k].uid == DA.fry_partner && !DA.fish[N_FISH_MAX + k].bolt_home) { DA.fish[N_FISH_MAX + k].homeward = false; DA.vis_since[k] = DA.clock; }
                if (DB.vis_used[k]) DB.fish[N_FISH_MAX + k].homeward = true;
            }
            if (!quiet && n % 45 == 0) { tank_t *h = rnd() & 1 ? &DA : &DB; if (h->n_fish) { int i = (int)(rnd() % (uint32_t)h->n_fish);
                if (!h->fish[i].away && h->fish[i].door == DOOR_NONE) d_head_for(h, i, h == &DA ? LINK_RIGHT : LINK_LEFT); } }
            if (n % 90 == 0) { tank_t *h = rnd() & 1 ? &DA : &DB; int k = (int)(rnd() % N_VISIT_MAX);
                if (h->vis_used[k] && rnd() % 3 == 0) h->fish[N_FISH_MAX + k].homeward = true; }
            if (n % 600 == 300 && !DA.fry_partner && DA.fry_give_k < 0) {   /* a time-together pair at A with one of B's here */
                for (int k = 0; k < N_VISIT_MAX; k++) if (DA.vis_used[k] && !DA.fish[N_FISH_MAX + k].homeward && DA.fish[N_FISH_MAX + k].stage >= STAGE_JUV && DA.n_fish) {
                    int m = (int)(rnd() % (uint32_t)DA.n_fish); if (DA.fish[m].away) break;
                    DA.fry_partner = DA.fish[N_FISH_MAX + k].uid; DA.fry_mate = DA.fish[m].uid; DA.fry_home_room = -1; DA.fry_asked = false; break; }
            }
            if (n % 1500 == 700 && rnd() % 2) { DA.fry_force = (int8_t)(rnd() % 2); progression_force_arrival(&DA); }
            if (n % 2700 == 1300 && DA.n_fish > 3) { int i = (int)(rnd() % (uint32_t)DA.n_fish); if (progression_fish_sellable(&DA, i) && progression_sell_fish(&DA, i)) sold++; }
            if (n % 2100 == 1000) {                              /* a GIVE, one way or the other */
                tank_t *g = rnd() & 1 ? &DA : &DB; gift_book_t *gb = g == &DA ? &GA : &GB;
                if (!gb->want_uid && !gb->asking_uid && !gb->leaving_uid && g->n_fish > 2) { int i = (int)(rnd() % (uint32_t)g->n_fish);
                    if (progression_fish_givable(g, i)) { gb->want_uid = g->fish[i].uid; gb->want_door = GIFT_DOOR_ANY; gb->want_peer = 0; gifts++; } }
            }
            if (GA.ask.on && !GA.ask.answer && rnd() % 60 == 0) GA.ask.answer = rnd() % 4 ? 1 : -1;
            if (GB.ask.on && !GB.ask.answer && rnd() % 60 == 0) GB.ask.answer = rnd() % 4 ? 1 : -1;
            if (n % 3300 == 2000) { tank_t *h = rnd() & 1 ? &DA : &DB; int k = (int)(rnd() % N_VISIT_MAX); if (h->vis_used[k]) { tank_link_deport(h, N_FISH_MAX + k); deports++; } }
            if (!deaf_left && n % 5000 == 4000 && rnd() % 2) { deaf_who = (int)(rnd() % 2); deaf_left = (int)rndi(90, 900); d_deaf[deaf_who] = true; deaf_n++; }
            if (deaf_left && !--deaf_left) d_deaf[deaf_who] = false;
            /* B tops itself up when low (a fish bought; outside progression: B runs only the link) */
            if (n % 3000 == 0 && progression_fish_held(&DB) < 4 && DB.n_fish < N_FISH_MAX) {
                tank_make_fish(&DB, DB.n_fish, DB.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); DB.n_fish++; tank_ensure_ids(&DB); }
            if (n % 3000 == 1500 && progression_fish_held(&DA) < 3 && DA.n_fish < N_FISH_MAX) {
                tank_make_fish(&DA, DA.n_fish, DA.n_fish % 3, 0.5f, 0.5f, STAGE_ADULT); DA.n_fish++; tank_ensure_ids(&DA); progression_set_age(&DA, DA.n_fish - 1, STAGE_ADULT_AGE + 60); }
            int fgk = DA.fry_give_k;
            vt_step(1);
            progression_tick(&DA, 0.033f);
            while (progression_newborn() >= 0) progression_newborn_done(&DA);
            if (DA.spawning && !was_sp) dances++;
            was_sp = DA.spawning;
            if (DA.fry_give_k >= 0 && fgk < 0) {
                cross_home++;
                /* (round 2, the review's G5) the fry waits in a visitor's place for its home's OK - its FRY lost 3 s (the hand-off
                   waits LINK_CROSS_MS) - and meanwhile one of B's fish asks at our door: the partner and the waiting fry take both
                   places, so it is refused. A room that forgot the waiting fry lets it in: three places, caught below */
                d_drop_type = 18 /* FR_FRY */; d_drop_to = 1; d_drop_until = dnow + 3000;
                for (int i = 0; i < DB.n_fish; i++) if (!DB.fish[i].away && DB.fish[i].door == DOOR_NONE) {
                    DB.fish[i].hunger = 1; DB.fish[i].energy = 8; d_at_door_facing_in(&DB, i, LINK_LEFT); pend_probe_uid = DB.fish[i].uid; pend_probes++; break; }
            }
            if (pend_probe_uid && DA.fry_give_k >= 0) for (int i = 0; i < DB.n_fish; i++)   /* (kept at the door while the fry waits) */
                if (DB.fish[i].uid == pend_probe_uid && !DB.fish[i].away && DB.fish[i].door == DOOR_NONE && DB.fish[i].door_refused <= DB.clock) d_at_door_facing_in(&DB, i, LINK_LEFT);
            if (pend_probe_uid && DA.fry_give_k < 0) {
                for (int i = 0; i < DB.n_fish; i++) if (DB.fish[i].uid == pend_probe_uid && DB.fish[i].door_refused > DB.clock) pend_refused++;
                pend_probe_uid = 0;
            }
            if (DA.n_fish > nA_prev) births += DA.n_fish - nA_prev;
            nA_prev = DA.n_fish;
            tank_t *T2[2] = { &DA, &DB };
            for (int k = 0; k < 2; k++) { int p = r2_places(T2[k]); if (p > max_places[k]) max_places[k] = p;
                if (p > TANK_LINK_VISIT_PLACES && place_bad++ < 3) { printf("FAIL soak %d t=%.1f: %c holds %d places (drawn + a fry waiting)\n", sd, n / 30.0, 'A' + k, p); d_fails++; } }
            /* B's fish: home, or drawn at A. Unseen too long = an invisible visitor */
            for (int i = 0; i < DB.n_fish; i++) {
                if (unseen_uid[i] != DB.fish[i].uid) { unseen_uid[i] = DB.fish[i].uid; unseen[i] = 0; }
                bool seen = tank_swims(&DB, i) || d_vis_of(&DA, DB.fish[i].uid) >= 0;
                unseen[i] = seen ? 0 : unseen[i] + 1;
                if (unseen[i] > max_unseen) max_unseen = unseen[i];
                if (unseen[i] == 30 * 120 && !d_deaf[0] && !d_deaf[1]) { printf("FAIL soak %d t=%.1f: B's fish %08x seen nowhere for 120 s (away %d)\n", sd, n / 30.0, DB.fish[i].uid, DB.fish[i].away); d_fails++; }
            }
        }
        (void)kept; home_fry_all += cross_home; probes_all += pend_probes; refused_all += pend_refused; d_drop_type = 0; if (relinks) printf("selftest-r2soak %d: linked again %d times (a deaf spell unlinked them)\n", sd, relinks);
        printf("selftest-r2soak %d: A %d->%d fish, B %d->%d; %d births on A, %d cross fry for B's home, %d dances, %d sold, %d GIVEs asked, %d deports, %d deaf spells; "
               "most swimming A %d B %d, visitors A %d B %d, places A %d B %d, held A %d B %d; longest a fish of B unseen %.1f s\n",
               sd, na, DA.n_fish, nb, DB.n_fish, births, cross_home, dances, sold, gifts, deports, deaf_n, vt_max_swim[0], vt_max_swim[1], vt_max_vis[0], vt_max_vis[1],
               max_places[0], max_places[1], vt_max_held[0], vt_max_held[1], max_unseen / 30.0);
        DCHECK(vt_max_swim[0] <= TANK_LINK_SWIM_MAX && vt_max_swim[1] <= TANK_LINK_SWIM_MAX && vt_max_held[0] <= POP_CAP && vt_max_held[1] <= POP_CAP, "soak %d: past a limit", sd);
        vt_home_all();
        link_mode(LA, false); link_mode(LB, false);
        gift_attach(&DA, NULL); gift_attach(&DB, NULL);
        fails0 += d_fails;
    }
    if (!home_fry_all) { printf("FAIL r2soak: no cross fry for B's home in %d seeds: the to-home path never ran\n", seeds); fails0++; }
    if (!refused_all) { printf("FAIL r2soak: no fish of B refused at the door while a fry waited in a visitor's place (%d tries)\n", probes_all); fails0++; }
    printf("selftest-r2soak: %s (%d; %d cross fry for B's home; %d times one of B's asked at the door while it waited, %d refused)\n",
           fails0 ? "FAILED" : "OK", fails0, home_fry_all, probes_all, refused_all);
    return fails0 ? 1 : 0;
}

/* --selftest-link-lk (0.4.2 audit, LK-2 + LK-1; from the audit's harness, r-link/linkfuzz.inc cases D and F): ONE real
   tank A (tank_t, progression, a gift book, tank_link.c, the engine) and a neighbor B whose every frame is written here
   by hand - an honest tank's (LK-2: one lost GIVEN_OK) or a modified one's (LK-1: a fry nobody asked for, an elder's
   passport on a fish coming home). 0 = every check held */
static tank_t KA; static link_t *KL; static gift_book_t KG;
static uint32_t know = 5000; static int k_fails; static char k_sav[400];
static const uint32_t KB = 0xB0B0B0B1u;
#define KCHECK(cond, ...) do { if (!(cond)) { if (k_fails < 12) { printf("FAIL selftest-link-lk: "); printf(__VA_ARGS__); printf("\n"); } k_fails++; } } while (0)
enum { K_BEACON = 1, K_JOIN = 2, K_LINKED = 3, K_OFFER = 5, K_ACCEPT = 6, K_GO = 8, K_VISIT = 9, K_RETURN = 11, K_FRY_ASK = 16,
       K_FRY = 18, K_FRY_OK = 19, K_FRY_NO = 20, K_GIVE = 21, K_GIVEN = 25, K_GIVEN_OK = 26, K_GIVEN_LOST = 27 };   /* (link.c's FR_* order) */
typedef struct { int len; uint8_t b[LINK_FRAME_MAX]; } kfr_t;
static kfr_t kout[512]; static int koutn;                 /* what A sent since the last look */
static void k_send(void *ctx, uint32_t to, const uint8_t *buf, int len) {
    (void)ctx; (void)to;
    if (koutn < 512 && len <= LINK_FRAME_MAX) { kout[koutn].len = len; memcpy(kout[koutn].b, buf, (size_t)len); koutn++; }
}
static int k_sent(int type) { int n = 0; for (int q = 0; q < koutn; q++) n += kout[q].b[3] == type; return n; }
static uint32_t k_u16(const kfr_t *f, int o) { return f->b[o] | (uint32_t)f->b[o + 1] << 8; }
typedef struct { uint8_t b[LINK_FRAME_MAX]; int n; } kw_t;
static void k8(kw_t *w, uint32_t v) { if (w->n < LINK_FRAME_MAX) w->b[w->n++] = (uint8_t)v; }
static void k16(kw_t *w, uint32_t v) { k8(w, v); k8(w, v >> 8); }
static void k32(kw_t *w, uint32_t v) { k16(w, v & 0xffff); k16(w, v >> 16); }
static void khdr(kw_t *w, int type) { w->n = 0; k16(w, 0x4b4c); k8(w, LINK_PROTO); k8(w, (uint32_t)type); k32(w, KB); k32(w, KA.uid); }
static void kpp(kw_t *w, const passport_t *pp) { uint8_t b[PASSPORT_MAX_BYTES]; int n = passport_write(pp, b, sizeof b); k8(w, (uint32_t)n); for (int i = 0; i < n; i++) k8(w, b[i]); }
static void krx(kw_t *w) { link_rx(KL, w->b, w->n, -40, know); }
static void k_beacon(void) {                              /* B: link mode, linked LEFT to A once it is */
    bool linked = link_neighbor(KL, LINK_RIGHT) == KB;
    kw_t w; khdr(&w, K_BEACON); w.b[8] = w.b[9] = w.b[10] = w.b[11] = 0;   /* (to everyone) */
    k8(&w, 1); k8(&w, 1); k8(&w, linked ? 0 : 1); k8(&w, 2); k8(&w, 0); k32(&w, linked ? KA.uid : 0); k32(&w, 0); krx(&w);
}
static void k_fish(int type, uint32_t uid, uint32_t trip) { kw_t w; khdr(&w, type); k32(&w, uid); k16(&w, trip); krx(&w); }
static void k_visit(uint32_t uid, uint32_t trip, const passport_t *pp) { kw_t w; khdr(&w, K_VISIT); k32(&w, uid); k16(&w, trip); kpp(&w, pp); krx(&w); }
static void k_return(uint32_t uid, uint32_t trip, const passport_t *pp) {   /* home through the door, swimming */
    kw_t w; khdr(&w, K_RETURN); k32(&w, uid); k16(&w, trip); k8(&w, 0); k8(&w, LINK_LEFT); k16(&w, 32000); k16(&w, 0); k16(&w, 300); kpp(&w, pp); krx(&w);
}
static void k_step(void) {
    know += 33;
    tank_tick(&KA, 0.033f, NULL);
    progression_tick(&KA, 0.033f);
    link_tick(KL, know);
    tank_link_tick(&KA, KL);
    link_ev_t e; while (link_poll(KL, &e)) tank_link_event(&KA, KL, &e);
    if (know % 990 < 33) k_beacon();
    if (progression_newborn() >= 0) progression_newborn_done(&KA);
}
static void k_steps(int n) { for (int k = 0; k < n; k++) k_step(); }
static bool k_setup(uint32_t seed) {
    (void)remove(k_sav);                                   /* (a fresh tank: progression_boot reads the save) */
    collection_reset();
    tank_init(&KA, seed); progression_boot(&KA); progression_setup_done(&KA); progression_tick(&KA, 0.1f); notice_sync(&KA);
    tank_ensure_ids(&KA);
    memset(&KG, 0, sizeof KG); gift_attach(&KA, &KG);
    if (!KL) KL = calloc(1, (size_t)link_size());
    link_host_t h = tank_link_host(&KA, k_send);
    link_init(KL, KA.uid, &h, seed, know);
    link_mode(KL, true); link_arm(KL, LINK_RIGHT);        /* A's RIGHT to B's LEFT */
    for (int k = 0; k < 40 && link_neighbor(KL, LINK_RIGHT) != KB; k++) {
        k_beacon(); koutn = 0; k_step();
        if (k_sent(K_JOIN)) { kw_t w; khdr(&w, K_LINKED); k8(&w, LINK_LEFT); k8(&w, 0); krx(&w); }
    }
    link_mode(KL, false); k_steps(30); koutn = 0;
    return link_neighbor(KL, LINK_RIGHT) == KB && KA.door_open[LINK_RIGHT];
}
static int k_slot(uint32_t uid) { for (int i = 0; i < KA.n_fish; i++) if (KA.fish[i].uid == uid) return i; return -1; }
static int k_cross(int i, uint32_t *trip) {               /* A's fish i across to B: OFFER answered ACCEPT, GO; the trip */
    fish_t *f = &KA.fish[i];
    f->door = DOOR_ASKED; f->door_edge = LINK_RIGHT; f->door_sent = false; f->door_refused = 0;
    koutn = 0; k_step();
    int t = -1;
    for (int q = 0; q < koutn; q++) if (kout[q].b[3] == K_OFFER) t = (int)k_u16(&kout[q], 16);
    if (t < 0) return -1;
    k_fish(K_ACCEPT, f->uid, (uint32_t)t); k_step();
    for (int k = 0; k < 30 && !f->away; k++) { f->door = DOOR_PASSING; f->door_edge = LINK_RIGHT; f->x = TANK_W + 200; k_step(); }
    if (!f->away) return -2;
    *trip = (uint32_t)t;
    return 0;
}
static passport_t k_stranger(uint32_t seed) {             /* a fish of another tank */
    static tank_t fr; tank_init(&fr, seed); tank_new_population(&fr); tank_ensure_ids(&fr);
    passport_t pp; passport_from_fish(&pp, &fr.fish[0], KB, 3600);
    return pp;
}
/* B gives A one fish through the engine, A's keeper says yes; GIVEN; A's slot of it or -1. A's GIVEN_OK goes nowhere */
static int k_gift_in(uint32_t uid, uint32_t nonce, uint32_t seed) {
    passport_t pp = k_stranger(seed); pp.uid = uid;
    kw_t w; khdr(&w, K_GIVE); k32(&w, uid); k32(&w, nonce); kpp(&w, &pp); krx(&w);
    for (int s = 0; s < 60; s++) { k_step(); if (KG.ask.on && !KG.ask.answer) { KG.ask.shown = true; KG.ask.answer = 1; } }
    khdr(&w, K_GIVEN); k32(&w, uid); k32(&w, nonce); kpp(&w, &pp); koutn = 0; krx(&w);
    k_steps(30);
    return k_slot(uid);
}
static int k_late_given(uint32_t uid, uint32_t nonce) {   /* B's GIVEN again: +1 = GIVEN_OK, -1 = GIVEN_LOST, 0 = neither */
    kw_t w; khdr(&w, K_GIVEN); k32(&w, uid); k32(&w, nonce); koutn = 0; krx(&w);
    return k_sent(K_GIVEN_OK) ? 1 : k_sent(K_GIVEN_LOST) ? -1 : 0;
}

int selftest_link_lk(void) {
    const char *d = getenv("PT_SHELF_DIR");
    snprintf(k_sav, sizeof k_sav, "%s/pocket-tank-selftest-link-lk.sav", d && *d ? d : "/tmp");
    setenv("POCKET_TANK_SAVE", k_sav, 1);
    k_fails = 0;

    /* ---- LK-2: B gives Z to A; A's GIVEN_OK is lost; A takes 16 more gifts (each sold); B's resent GIVEN(Z) ---- */
    if (!k_setup(4205)) { KCHECK(0, "LK-2: no link"); return 1; }
    const uint32_t Z = 0x2a2a0001u, ZN = 0x77770001u;
    KCHECK(k_gift_in(Z, ZN, 501) >= 0, "LK-2: Z never came");
    KCHECK(k_late_given(Z, ZN) == 1, "LK-2: a resent GIVEN for Z just taken in was not answered GIVEN_OK");
    int more = 0;
    for (int k = 0; k < GIFT_RECEIVED; k++) {
        int i = k_gift_in(0x2b2b0000u + (uint32_t)k + 1, 0x88880000u + (uint32_t)k + 1, 600 + (uint32_t)k);
        if (i < 0) continue;
        more++;
        KCHECK(progression_sell_fish(&KA, i), "LK-2: could not sell gift %d", k);
        k_steps(10);
    }
    KCHECK(more == GIFT_RECEIVED && k_slot(Z) >= 0 && gift_state(&KG, Z, ZN, NULL) == LINK_GS_NONE,
           "LK-2: the setup did not roll RECEIVED past Z (%d gifts, Z %s, its RECEIVED %s)", more, k_slot(Z) >= 0 ? "here" : "GONE",
           gift_state(&KG, Z, ZN, NULL) == LINK_GS_NONE ? "gone" : "still kept");
    int z = k_late_given(Z, ZN);
    KCHECK(z == 1, "LK-2: Z swims here, B's late GIVEN was answered %s (B's note brings Z back there: two Zs)", z < 0 ? "GIVEN_LOST" : "nothing");
    printf("selftest-link-lk: LK-2: Z alive here, RECEIVED rolled past it by %d more gifts: B's late GIVEN answered %s\n", more,
           z == 1 ? "GIVEN_OK (one Z)" : z < 0 ? "GIVEN_LOST (TWO Zs)" : "nothing");
    /* ... and what must still be GIVEN_LOST (never "we have it" for a fish that is not ours here: the giver's note is the
       only copy - zero): never given, a visitor of B's here, one of OURS given to B whose OK we lost (given A -> B -> A) */
    KCHECK(k_late_given(0x2c2c0001u, 0x99990001u) == -1, "LK-2: a GIVEN for a fish never seen here was not answered GIVEN_LOST");
    {   passport_t vp = k_stranger(777); vp.uid = 0x2d2d0001u;   /* B's fish V visits A (OFFER, GO): a visitor here */
        kw_t w; khdr(&w, K_OFFER); k32(&w, vp.uid); k16(&w, 77); k8(&w, LINK_LEFT); k16(&w, 32000); k16(&w, 0); k16(&w, 300); kpp(&w, &vp); krx(&w);
        k_step(); k_fish(K_GO, vp.uid, 77); k_steps(5);
        KCHECK(link_hosting(KL, vp.uid), "LK-2: B's visitor V never came");
        KCHECK(k_late_given(vp.uid, 0x99990002u) == -1, "LK-2: a GIVEN for B's visitor V (no gift asked) was not answered GIVEN_LOST");
        int w0 = -1; for (int i = 0; i < KA.n_fish && w0 < 0; i++) if (!KA.fish[i].away && KA.fish[i].uid != Z) w0 = i;
        uint32_t W = KA.fish[w0].uid; passport_t wp; passport_from_fish(&wp, &KA.fish[w0], KA.uid, progression_age_s(&KA, w0));
        KCHECK(gift_given_add(&KG, W, 0x55550001u, KB, &wp) && progression_give_out(&KA, w0), "LK-2: could not give W out");
        KCHECK(k_late_given(W, 0x55550002u) == -1, "LK-2: W, given from here (our note out), came back late as B's gift: not GIVEN_LOST (W nowhere)");
        gift_given_drop(&KG, W, 0x55550001u);
    }
    printf("selftest-link-lk: LK-2: still GIVEN_LOST: a fish never seen here, B's visitor here, one of ours given out whose gift back came late\n");

    /* ---- LK-1: a modified B pushes FRY nobody asked for (even rounds: it sends a FRY_ASK first, as a modified tank can),
       with an elder's passport, and brings each home through the door with that passport ---- */
    if (!k_setup(4201)) { KCHECK(0, "LK-1: no link"); return 1; }
    uint32_t trip; int r = k_cross(0, &trip);
    KCHECK(!r, "LK-1: fish 0 did not cross (%d)", r);
    if (r) return 1;
    const uint32_t X = KA.fish[0].uid;
    passport_t xp; passport_from_fish(&xp, &KA.fish[0], KA.uid, 100);
    const int32_t sd0 = KA.sd_balance; int made = 0, rounds = 0;
    for (int round = 0; round < 6 && KA.n_fish < POP_CAP; round++) {
        k_visit(X, trip, &xp);                             /* (X is still there: its lease) */
        if (round % 2 == 0) { kw_t w; khdr(&w, K_FRY_ASK); k32(&w, X); k16(&w, trip); k8(&w, 0); krx(&w); }
        passport_t fp = k_stranger(4300 + (uint32_t)round);
        fp.uid = 0xF0000000u + (uint32_t)round * 7919u + 1; fp.home_tank = KA.uid; fp.parent_uid[0] = X; fp.parent_uid[1] = 0x1234;
        fp.stage = STAGE_ELDER; fp.size = 4.0f; fp.age_s = 4.0e9f; fp.trust = 10; fp.eaten = 65535; fp.eaten_player = 60;
        fp.ms_bits = MS_REACHED_JUV | MS_REACHED_ADULT | MS_REACHED_ELDER | MS_CROSSBRED | MS_BOUGHT | MS_FIRST_MEAL_FROM_YOU;
        kw_t w; khdr(&w, K_FRY); k32(&w, X); k8(&w, LINK_LEFT); kpp(&w, &fp); koutn = 0; krx(&w);
        k_step();
        int ftrip = -1; for (int q = 0; q < koutn; q++) if (kout[q].b[3] == K_FRY_OK) ftrip = (int)k_u16(&kout[q], 16);
        rounds++;
        if (ftrip < 0) { k_steps(30); continue; }
        int s = k_slot(fp.uid);
        if (s >= 0) {
            const fish_t *f = &KA.fish[s];
            KCHECK(f->stage == STAGE_FRY && f->trust <= 4.0f && f->eaten == 0 && !(f->ms_bits & (MS_REACHED_JUV | MS_REACHED_ADULT | MS_REACHED_ELDER | MS_BOUGHT))
                   && progression_age_s(&KA, s) == 0, "LK-1: fry %d taken in as its passport said: stage %d trust %.1f eaten %d bits %04x age %.0f",
                   round, f->stage, f->trust, f->eaten, (unsigned)f->ms_bits, progression_age_s(&KA, s));
        }
        for (int k = 0; k < 3; k++) { k_visit(fp.uid, (uint32_t)ftrip, &fp); k_visit(X, trip, &xp); k_steps(30); }
        k_return(fp.uid, (uint32_t)ftrip, &fp);         /* home through the door, an elder's passport */
        k_steps(60);
        if ((s = k_slot(fp.uid)) < 0) continue;
        made++;
        const fish_t *f = &KA.fish[s];
        KCHECK(!f->away && f->stage == STAGE_FRY && progression_age_s(&KA, s) < STAGE_JUV_AGE
               && !(f->ms_bits & (MS_REACHED_JUV | MS_REACHED_ADULT | MS_REACHED_ELDER | MS_BOUGHT)) && (f->ms_bits & MS_CROSSBRED),
               "LK-1: fry %d came home as its passport said: away %d stage %d age %.0f bits %04x", round, f->away, f->stage, progression_age_s(&KA, s), (unsigned)f->ms_bits);
    }
    k_steps(60);
    const int32_t paid = KA.sd_balance - sd0;
    KCHECK(paid <= made * SD_BIRTH, "LK-1: %d fry from B paid %d sand dollars (a birth each is %d)", made, paid, made * SD_BIRTH);
    printf("selftest-link-lk: LK-1: B pushed %d FRY with elder passports (%d after a FRY_ASK of its own): %d came in, each a newborn; +%d sand dollars (births only)\n",
           rounds, (rounds + 1) / 2, made, paid);

    /* ---- LK-1's sibling: one of ours home from a visit (welcome_home). A modified host's passport: 4e9 s old, every stage,
       CROSSBRED and BOUGHT - it comes home as old as it has lived, its stages its own; an honest host's: the stage it
       reached next door counts ---- */
    if (!k_setup(4206)) { KCHECK(0, "LK-1 visit: no link"); return 1; }
    int a = KA.n_fish - 1;
    progression_set_age(&KA, a, STAGE_ADULT_AGE + 60);
    const uint32_t Y = KA.fish[a].uid; const uint32_t ybits = KA.fish[a].ms_bits;
    r = k_cross(a, &trip); KCHECK(!r, "LK-1 visit: Y did not cross (%d)", r);
    if (!r) {
        passport_t yp; passport_from_fish(&yp, &KA.fish[k_slot(Y)], KA.uid, 4.0e9f);
        yp.ms_bits |= MS_REACHED_ELDER | MS_CROSSBRED | MS_BOUGHT | MS_FIRST_DART;
        const int32_t s0 = KA.sd_balance;
        k_visit(Y, trip, &yp); k_steps(30); k_return(Y, trip, &yp); k_steps(60);
        int s = k_slot(Y); const fish_t *f = &KA.fish[s];
        KCHECK(!f->away && f->stage == STAGE_ADULT && progression_age_s(&KA, s) < STAGE_ADULT_AGE + 60 + 200
               && !(f->ms_bits & (MS_REACHED_ELDER | MS_CROSSBRED | MS_BOUGHT)) && (f->ms_bits & MS_FIRST_DART) && KA.sd_balance == s0,
               "LK-1 visit: Y came home as the passport said: away %d stage %d age %.0f bits %04x (left %04x) sand dollars +%d",
               f->away, f->stage, progression_age_s(&KA, s), (unsigned)f->ms_bits, (unsigned)ybits, KA.sd_balance - s0);
    }
    int j = 0;                                            /* honest: J leaves 10 s short of JUV, comes home 15 s past it */
    for (int i = 0; i < KA.n_fish; i++) if (!KA.fish[i].away && KA.fish[i].uid != Y) j = i;
    progression_set_age(&KA, j, STAGE_JUV_AGE - 10); KA.fish[j].stage = STAGE_FRY; KA.fish[j].ms_bits &= ~(uint32_t)(MS_REACHED_JUV | MS_REACHED_ADULT | MS_REACHED_ELDER);
    const uint32_t J = KA.fish[j].uid;
    r = k_cross(j, &trip); KCHECK(!r, "LK-1 visit: J did not cross (%d)", r);
    if (!r) {
        passport_t jp; passport_from_fish(&jp, &KA.fish[k_slot(J)], KA.uid, STAGE_JUV_AGE + 15);
        for (int k = 0; k < 30; k++) { k_visit(J, trip, &jp); k_steps(30); }   /* (30 s next door) */
        k_return(J, trip, &jp); k_steps(60);
        int s = k_slot(J); const fish_t *f = &KA.fish[s];
        KCHECK(!f->away && f->stage == STAGE_JUV && (f->ms_bits & MS_REACHED_JUV) && progression_age_s(&KA, s) >= STAGE_JUV_AGE + 15,
               "LK-1 visit: an honest host's age was not taken: away %d stage %d age %.0f bits %04x", f->away, f->stage, progression_age_s(&KA, s), (unsigned)f->ms_bits);
    }
    printf("selftest-link-lk: LK-1: home from a visit, a modified host's passport (4e9 s, every stage, CROSSBRED, BOUGHT): as old as it lived, its own stages, no pay; an honest host's JUV counts\n");

    gift_attach(&KA, NULL); (void)remove(k_sav);
    printf("selftest-link-lk: %s\n", k_fails ? "FAILED" : "OK");
    return k_fails ? 1 : 0;
}
