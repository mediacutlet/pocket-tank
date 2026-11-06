#define _DEFAULT_SOURCE   /* setenv under -std=c11 */
/* selftest_seam.c - --selftest-seam (0.4.1 audit, phase 2, seam reviewer "link x progression").
 *
 * Two REAL tanks side by side, each with its OWN progression (the per-tank modules' statics - progression, notice,
 * setup, collection, shelf - live in two sections of their own, seam_sect.h, and are swapped per tank: a reboot is
 * those bytes put back to the image's), the real link engine between them over a radio that delivers next frame.
 * A seeded random "week": the fish visit on their own and are sent, called home, DEPORTed; keepers GIVE (home, and
 * where it is next door), say yes or no; births here, cross-tank births kept or sent home (a fry born next door),
 * sales, shop fry, reboots (saved, and power cuts), nights and days. After EVERY frame each tank's books are checked:
 *   count marks (POP_TMS over the fish here, not a fry still next door), MS_ARRIVED, the ledger (each stage / trust
 *   paid once per fish across both tanks), age and starve clocks (follow their fish, never backwards), MS_ARRIVED,
 *   the welcome (exactly once for every fish of ours that is new, none for a gift), notices (a badge said once per
 *   fish, a tank badge once per tank, FIRST FISH SOLD only after a sale, FIRST MUTATION only after a mutated birth
 *   here), roster_gen on every arrival and departure, born_away only on a fish next door, the cap, bonds and
 *   courting naming a fish that left, the fry wait after a sale or GIVE. Each failure prints once per kind. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef __APPLE__
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#endif
#include "tank_link.h"
#include "progression.h"
#include "collection.h"
#include "gift.h"
#include "shelf.h"
#include "notice.h"

#ifdef __APPLE__
float   progression_dbg_starve(int i);
int32_t progression_dbg_sale_need(void);

/* ---- two progressions: the sections swapped ---- */
static uint8_t *SB, *SD; static unsigned long NB, ND;
static uint8_t *PB, *PD;                      /* the image's bytes (a cold boot) */
static uint8_t *CB[2], *CD[2]; static int cur = -1;
static char dirs[2][300];
static void use(int k) {
    if (cur == k) return;
    if (cur >= 0) { memcpy(CB[cur], SB, NB); memcpy(CD[cur], SD, ND); }
    memcpy(SB, CB[k], NB); memcpy(SD, CD[k], ND); cur = k;
    char p[400];
    snprintf(p, sizeof p, "%s/tank.sav", dirs[k]); setenv("POCKET_TANK_SAVE", p, 1);
    snprintf(p, sizeof p, "%s/storage.bin", dirs[k]); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", dirs[k]); setenv("POCKET_TANK_FRAME", p, 1);
}
static void cold(int k) { use(k); memcpy(SB, PB, NB); memcpy(SD, PD, ND); }   /* the RAM of tank k as at power-on */

/* ---- rng ---- */
static uint32_t rs = 1;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static float rndf(void) { return (rnd() & 0xffffff) / 16777216.0f; }

/* ---- the tanks and the radio ---- */
static tank_t T[2]; static link_t *L[2]; static link_host_t H[2]; static gift_book_t G[2];
typedef struct { int to, len; uint8_t b[LINK_FRAME_MAX]; } qf_t;
static qf_t Q[512]; static int qn;
static uint32_t now = 1000;
static uint32_t now_ms(void) { return now; }
static uint32_t ent(void) { return rnd() | 1; }   /* the device's esp_random for IDs (the sim's own repeat after a power cut) */
static bool deaf[2];
static void q_send(void *ctx, uint32_t to, const uint8_t *buf, int len) {
    int from = ctx == &T[0] ? 0 : 1;
    if (qn < 512 && (!to || to == T[1 - from].uid)) { Q[qn].to = 1 - from; Q[qn].len = len; memcpy(Q[qn].b, buf, (size_t)len); qn++; }
}

/* ---- the books' ledger of the run ---- */
#define UMAX 8192
typedef struct {
    uint32_t uid;
    int  home;                 /* the tank whose fish it is now (-1 none) */
    bool ever_home[2];         /* it has been a fish of that tank */
    bool gift;                 /* it came to its tank as a gift (no welcome owed) */
    int  welcomes, owed;       /* welcomes shown / owed */
    int  paid[4];              /* stage JUV, ADULT, ELDER, trust: paid how many times, both tanks */
    float age;                 /* last age seen (home, not away) */
    int  ms_said[32];          /* a fish badge's notice: how many times */
    int  stage_said[4];
    bool born_here_mut;
    /* round 2 */
    bool sold;                 /* it left by a sale (gone for good) */
    double gone_since;         /* left a roster not by a sale: in flight since (0 = in a roster) */
    int  leave_stage; uint32_t leave_ms, leave_paid; float leave_age; bool leave_valid, leave_away;   /* as it LEFT its last tank */
    double away_since; double max_away;   /* one of ours next door: since when, the longest stretch */
    char log[600];
} ub_t;
static ub_t U[UMAX]; static int un;
static ub_t *ub(uint32_t uid) {
    for (int i = 0; i < un; i++) if (U[i].uid == uid) return &U[i];
    if (un >= UMAX) { printf("selftest-seam: FAIL: the uid book is full (UMAX)\n"); exit(2); }
    memset(&U[un], 0, sizeof U[un]); U[un].uid = uid; U[un].home = -1; U[un].age = -1;
    return &U[un++];
}
static void ulog(ub_t *u, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static uint32_t now_ms(void);
static void ulog(ub_t *u, const char *fmt, ...) {
    size_t n = strlen(u->log); if (n > sizeof u->log - 60) return;
    char b[120]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    snprintf(u->log + n, sizeof u->log - n, " | %.0f %s", now_ms() / 1000.0, b);
}
static struct { bool booted; int sold, given_out, mut_here, tms_said[32]; int wel_up, wel_frames; int nb_slot; uint32_t nb_uid;
                uint32_t count_uid[32]; bool staged_wrong; } K[2];
typedef struct { int n; uint32_t uid[N_FISH_MAX]; uint32_t paid[N_FISH_MAX]; float age[N_FISH_MAX], starve[N_FISH_MAX]; bool away[N_FISH_MAX], born_away[N_FISH_MAX];
                 uint32_t tms; uint8_t gen; int32_t earned, feedings, sale_need;
                 /* round 2 */
                 uint8_t shift; int8_t rslot; int nvis, nvis_pending, swim; bool spawning; float danced; int give_k; bool staged;
                 int8_t grow_met, grow_cf, change_met, change_cf; bool all, all_cf;   /* GROW / CHANGE met (-1 no row) as the keeper sees, and with every fish of ours HERE */
                 uint32_t ms[N_FISH_MAX]; uint8_t stage[N_FISH_MAX], mutated[N_FISH_MAX]; } snap_t;
static void gates(const tank_t *t, int8_t *grow, int8_t *change, bool *staged, bool *all) {
    fry_req_t r[FRY_REQ_MAX]; bool st_ = false;
    int n = progression_next_fry(t, r, &st_);
    *grow = *change = -1; *all = n > 0;
    for (int i = 0; i < n; i++) { if (r[i].kind == FRY_REQ_GROW) *grow = r[i].met; if (r[i].kind == FRY_REQ_CHANGE) *change = r[i].met; *all &= r[i].met; }
    if (staged) *staged = st_;
}
static bool g_gates = true;                       /* (the R2-2 counterfactual each frame) */
static void snap(int k, snap_t *s) {
    tank_t *t = &T[k]; s->n = t->n_fish;
    bool any_away = false;
    for (int i = 0; i < t->n_fish; i++) { s->uid[i] = t->fish[i].uid; s->paid[i] = t->sd_paid_fish[i]; s->age[i] = progression_age_s(t, i);
        s->starve[i] = progression_dbg_starve(i); s->away[i] = t->fish[i].away; s->born_away[i] = t->fish[i].born_away;
        s->ms[i] = t->fish[i].ms_bits; s->stage[i] = (uint8_t)t->fish[i].stage; s->mutated[i] = t->fish[i].mutated; any_away |= t->fish[i].away; }
    s->tms = t->tank_ms_bits; s->gen = t->roster_gen; s->earned = t->sd_earned; s->feedings = t->player_feedings; s->sale_need = progression_dbg_sale_need();
    s->shift = t->roster_shift; s->rslot = t->removed_slot; s->nvis = s->nvis_pending = s->swim = 0;
    for (int j = 0; j < N_VISIT_MAX; j++) { s->nvis += t->vis_used[j]; s->nvis_pending += t->vis_pending[j]; }
    for (int i = 0; i < N_FISH_SLOTS; i++) s->swim += tank_swims(t, i);
    s->spawning = t->spawning; s->danced = t->spawn_danced; s->give_k = t->fry_give_k;
    s->grow_met = s->grow_cf = s->change_met = s->change_cf = -1; s->staged = false; s->all = s->all_cf = false;
    if (g_gates && t->n_fish >= 2) {
        gates(t, &s->grow_met, &s->change_met, &s->staged, &s->all);
        s->grow_cf = s->grow_met; s->change_cf = s->change_met; s->all_cf = s->all;
        if (any_away) {                               /* the counterfactual: every fish of ours counted, next door or not (R2-2) */
            bool aw[N_FISH_MAX]; for (int i = 0; i < t->n_fish; i++) { aw[i] = t->fish[i].away; t->fish[i].away = false; }
            gates(t, &s->grow_cf, &s->change_cf, NULL, &s->all_cf);
            for (int i = 0; i < t->n_fish; i++) t->fish[i].away = aw[i];
        }
    }
}
static int s_find(const snap_t *s, uint32_t uid) { for (int i = 0; i < s->n; i++) if (s->uid[i] == uid) return i; return -1; }

/* ---- failures: each kind printed a few times, all counted ---- */
#define KINDS 64
static const char *kname[KINDS]; static int kcount[KINDS], nk;
static int fails;
static double tsec(void) { return now / 1000.0; }
#define BOOK(cond, kind, ...) do { if (!(cond)) { int _k = 0; while (_k < nk && strcmp(kname[_k], kind)) _k++; \
    if (_k == nk && nk < KINDS) kname[nk++] = kind; kcount[_k]++; fails++; \
    if (kcount[_k] <= 3) { printf("BOOK t=%.1f [%s] ", tsec(), kind); printf(__VA_ARGS__); printf("\n"); } } } while (0)
static int caps;
static void cap_hook(const tank_t *t, int held, const char *file, int line, const char *path) {
    caps++; if (caps <= 3) printf("CAP t=%.1f tank %c: %d held (%s:%d %s)\n", tsec(), t == &T[0] ? 'A' : 'B', held, file, line, path);
}
static struct { int visits, homes, deports, gives, gives_away, gift_yes, gift_no, births, cross_try, cross_home, cross_kept, born_next_door, sales, buys,
                reboots, cuts, nights, days, churn, notice_churn, reboot_fry_away, arrivals_gift, fry_home, stale_mate, stall_frames, given_away_born, welcomes;
                /* round 2 */
                int frames_two_vis, max_vis, max_swim, home_into_two, swim7_frames, fry_home_into_two, mut_fry_home;
                int count_notices, count_pic_checked, gift_left_checked, gives_cut, give_forced_born_away, give_forced_refused;
                long frames, frames_r22_decisive, frames_away_any, frames_r22_grow, frames_r22_change; int r22_staged, r22_births, r22_staged_events;
                int dances_done, dances_fry, dances_nofry_coming, dances_nofry_two_vis, dances_nofry_other, nofry_two_vis_cross_forced; double longest_two_vis_loop;
                int removals, shift_checked; long stall_mate_away; double max_away; } st;
static int swim_hooks;
static void swim_hook(const tank_t *t, int swim, int visitors, const char *path) {
    swim_hooks++; if (swim_hooks <= 3) printf("SWIM t=%.1f tank %c: %d swimming, %d visitors (%s)\n", now / 1000.0, t == &T[0] ? 'A' : 'B', swim, visitors, path);
}

static const uint32_t POPB[6] = { 0, 0, TMS_PAIR, TMS_TRIO, TMS_QUARTET, TMS_QUINTET };

/* ---- one tank's frame, and its books after it ---- */
static void keeper(int k, float dt) {
    tank_t *t = &T[k];
    int nb = progression_newborn();               /* the birth flow: shown, read, done (setup.c's pages, about 3 s) */
    if (nb >= 0) {
        if (K[k].nb_uid != t->fish[nb].uid) {
            K[k].nb_slot = nb; K[k].nb_uid = t->fish[nb].uid; K[k].wel_frames = 0;
            ub_t *u = ub(t->fish[nb].uid); u->welcomes++; st.welcomes++;
            BOOK(!t->fish[nb].away, "welcome-while-away", "tank %c: a welcome for %08x while it is next door", 'A' + k, u->uid);
            BOOK(!u->gift, "welcome-for-gift", "tank %c: a welcome for the gift %08x", 'A' + k, u->uid);
            ulog(u, "%c welcome s%d/%d", 'A' + k, nb, t->n_fish);
            if (getenv("SEAM_TRACE")) printf("  trace t=%.2f %c welcome slot %d uid %08x n %d\n", tsec(), 'A' + k, nb, u->uid, t->n_fish);
            BOOK(u->welcomes <= 1, "welcome-twice", "tank %c: a SECOND welcome for %08x (%d), age %.0f s:%s", 'A' + k, u->uid, u->welcomes, progression_age_s(t, nb), u->log);
        }
        if (++K[k].wel_frames > 90) { if (getenv("SEAM_TRACE")) printf("  trace t=%.2f %c done slot %d\n", tsec(), 'A' + k, nb); progression_newborn_done(t); K[k].nb_slot = -1; K[k].nb_uid = 0; }
    } else { K[k].nb_slot = -1; K[k].nb_uid = 0; }
    {   /* what notice_tick sees between two of its ticks: the same count, other fish = it cannot tell (a removal and an arrival) */
        static uint32_t last[2][N_FISH_MAX]; static int ln[2] = { -1, -1 };
        if (ln[k] == t->n_fish) { bool diff = false; for (int i = 0; i < t->n_fish; i++) diff |= last[k][i] != t->fish[i].uid;
            if (diff) { st.notice_churn++; if (getenv("SEAM_TRACE")) printf("  trace t=%.2f %c notice churn (%d fish, other ones)\n", tsec(), 'A' + k, t->n_fish);
                for (int i = 0; i < t->n_fish; i++) ulog(ub(t->fish[i].uid), "%c NOTICE-CHURN", 'A' + k); } }
        ln[k] = t->n_fish; for (int i = 0; i < t->n_fish; i++) last[k][i] = t->fish[i].uid;
        if (K[k].booted) { ln[k] = -1; K[k].booted = false; }
    }
    notice_tick(t, dt, nb >= 0);
    const notice_t *c = notice_current();
    int cue = notice_take_cue();                   /* (-1 for a notice back after stepping aside: no second chime, not said again) */
    if (c && c->age == 0 && cue >= 0) {
        if (c->kind == NOTICE_TANK_MILESTONE) {
            int b = __builtin_ctz(c->bit);
            K[k].tms_said[b]++;
            BOOK(K[k].tms_said[b] <= 1, "tank-badge-twice", "tank %c: the tank badge bit %d said %d times", 'A' + k, b, K[k].tms_said[b]);
            if (c->bit == TMS_FIRST_SALE) BOOK(K[k].sold > 0, "first-sale-no-sale", "tank %c: FIRST FISH SOLD with no sale (%d given)", 'A' + k, K[k].given_out);
            if (c->bit == TMS_FIRST_MUTATION) BOOK(K[k].mut_here > 0, "first-mutation-not-here", "tank %c: FIRST MUTATION with no mutated birth here", 'A' + k);
            if (c->bit & (TMS_TRIO | TMS_QUARTET | TMS_QUINTET)) {   /* round 2 (B5): a count pictures the fish that made it */
                st.count_notices++;
                uint32_t want = K[k].count_uid[b]; int ws = -1;
                for (int i = 0; i < t->n_fish; i++) if (want && t->fish[i].uid == want) ws = i;
                if (want) { st.count_pic_checked++;
                    BOOK(c->fish == ws, "count-picture", "tank %c: count bit %d pictures slot %d (%08x), the fish that made it is slot %d (%08x)", 'A' + k, b,
                         c->fish, c->fish >= 0 && c->fish < t->n_fish ? t->fish[c->fish].uid : 0, ws, want); }
            }
        } else if (c->kind == NOTICE_MILESTONE && c->fish >= 0 && c->fish < t->n_fish) {
            ub_t *u = ub(t->fish[c->fish].uid); int b = __builtin_ctz(c->bit);
            u->ms_said[b]++; ulog(u, "%c says badge %d", 'A' + k, b);
            BOOK(u->ms_said[b] <= 1, "fish-badge-twice", "tank %c: fish %08x's badge bit %d said %d times (gift %d):%s", 'A' + k, u->uid, b, u->ms_said[b], u->gift, u->log);
        } else if (c->kind == NOTICE_STAGE && c->fish >= 0 && c->fish < t->n_fish) {
            ub_t *u = ub(t->fish[c->fish].uid); int sgi = t->fish[c->fish].stage & 3;
            u->stage_said[sgi]++; ulog(u, "%c says stage %d", 'A' + k, sgi);
            if (getenv("SEAM_TRACE")) printf("  trace t=%.2f %c STAGE notice slot %d %08x stage %d age %.0f\n", tsec(), 'A' + k, c->fish, u->uid, sgi, progression_age_s(t, c->fish));
            BOOK(u->stage_said[sgi] <= 1, "stage-said-twice", "tank %c: fish %08x's stage %d said %d times (gift %d):%s", 'A' + k, u->uid, sgi, u->stage_said[sgi], u->gift, u->log);
        }
    }
    if (c && c->age > NOTICE_HOLD_S + 0.2f) notice_dismiss();
}
/* ---- round 2: the round-1 fixes' books (c04f87e) ---- */
static bool in_frame;                              /* books() from a frame (not a keeper action) */
static struct { int run; double run_t0; } D[2];   /* a run of dances with no fry */
#define MS_STAGE_BITS (MS_REACHED_JUV | MS_REACHED_ADULT | MS_REACHED_ELDER)
static void books2(int k, const snap_t *a, const snap_t *b, bool left, bool came) {
    tank_t *t = &T[k];
    /* 9f12374: no bond or courting pair names a fish of ours that is no longer ours (here or next door) */
    { bool inr = false; for (int i = 0; i < t->n_fish; i++) inr |= t->fish[i].uid == t->fry_mate;
      if (t->fry_mate) { BOOK(inr, "fry-mate-gone", "tank %c: fry_mate %08x is no fish of ours (partner %08x)", 'A' + k, t->fry_mate, t->fry_partner);
          if (inr && t->fry_partner) { bool here = false; for (int i = 0; i < t->n_fish; i++) here |= t->fish[i].uid == t->fry_mate && !t->fish[i].away; if (!here && in_frame) st.stall_mate_away++; } }
      for (int j = 0; j < 6; j++) if (t->bond[j].vis && t->bond[j].mate) { bool m = false; for (int i = 0; i < t->n_fish; i++) m |= t->fish[i].uid == t->bond[j].mate;
          BOOK(m, "bond-mate-gone", "tank %c: bond %d (visitor %08x) names %08x, no fish of ours", 'A' + k, j, t->bond[j].vis, t->bond[j].mate); } }
    /* the visitor rule (efe7414): two places, 7 swimming at most, ours coming home always has its place */
    BOOK(b->nvis + b->nvis_pending <= TANK_LINK_VISIT_PLACES, "visitor-places", "tank %c: %d visitors + %d waiting fry", 'A' + k, b->nvis, b->nvis_pending);
    BOOK(b->swim <= TANK_LINK_SWIM_MAX || t->cap_over, "swim-over-7", "tank %c: %d swimming (%d own, %d visitors)", 'A' + k, b->swim, b->n, b->nvis);
    if (b->nvis > st.max_vis) st.max_vis = b->nvis;
    if (b->swim > st.max_swim) st.max_swim = b->swim;
    if (in_frame) { st.frames++; if (b->nvis >= 2) st.frames_two_vis++; if (b->swim == TANK_LINK_SWIM_MAX) st.swim7_frames++; }
    int nleft = 0, jleft = -1;
    for (int i = 0; i < a->n; i++) if (s_find(b, a->uid[i]) < 0) {
        nleft++; jleft = i;
        ub_t *u = ub(a->uid[i]);
        u->leave_valid = true; u->leave_stage = a->stage[i]; u->leave_ms = a->ms[i]; u->leave_paid = a->paid[i]; u->leave_age = a->age[i]; u->leave_away = a->away[i];
        if (!u->sold) u->gone_since = tsec();
        u->away_since = 0;
        BOOK(!a->born_away[i], "born-away-given", "tank %c: a fry of ours born next door (%08x) left the roster before it came home", 'A' + k, a->uid[i]);
    }
    if (getenv("SEAM_TRACE") && (nleft || came)) { printf("  trace t=%.2f %c roster%s:", tsec(), 'A' + k, in_frame ? "" : " (action)");
        for (int i = 0; i < a->n; i++) printf(" %08x%s", a->uid[i], a->away[i] ? "*" : ""); printf(" ->");
        for (int i = 0; i < b->n; i++) printf(" %08x%s", b->uid[i], b->away[i] ? "*" : ""); printf("\n"); }
    /* roster_shift (B4 89c1dae): ticks once per fish that left, never for an arrival; removed_slot names the slot */
    st.removals += nleft;
    BOOK((uint8_t)(b->shift - a->shift) == nleft, "roster-shift", "tank %c: %d left, roster_shift %u -> %u", 'A' + k, nleft, a->shift, b->shift);
    if (nleft == 1) { st.shift_checked++; BOOK(b->rslot == jleft, "removed-slot", "tank %c: slot %d left, removed_slot %d", 'A' + k, jleft, b->rslot); }
    if (nleft == 0) BOOK(b->rslot == a->rslot, "removed-slot-arrival", "tank %c: removed_slot %d -> %d with nothing removed", 'A' + k, a->rslot, b->rslot);
    /* B1 (bf7ad9b): the slot a fish leaves keeps nothing */
    if (nleft && !came && b->give_k == a->give_k)
        for (int s = b->n; s < a->n; s++) {
            static const fish_t zero; bool clean = !memcmp(&t->fish[s], &zero, sizeof zero);
            BOOK(clean, "vacated-slot-dirty", "tank %c: slot %d after a removal holds %08x (born_away %d)", 'A' + k, s, t->fish[s].uid, t->fish[s].born_away);
        }
    /* arrivals and homecomings */
    int n_new = 0; uint32_t new_uid = 0, home_uid = 0; int n_home = 0;
    for (int i = 0; i < b->n; i++) {
        uint32_t x = b->uid[i]; int j = s_find(a, x); ub_t *u = ub(x); fish_t *f = &t->fish[i];
        if (j >= 0 && b->away[i] != a->away[j]) ulog(u, "%c %s age %.0f st %d ms %x", 'A' + k, b->away[i] ? "goes" : "home", b->age[i], b->stage[i], b->ms[i]);
        if (j >= 0 && b->stage[i] != a->stage[j] && b->away[i]) ulog(u, "%c GREW AWAY %d->%d", 'A' + k, a->stage[j], b->stage[i]);
        if (b->away[i]) { if (!u->away_since) u->away_since = tsec(); double d = tsec() - u->away_since; if (d > u->max_away) u->max_away = d; if (d > st.max_away) st.max_away = d; }
        else u->away_since = 0;
        if (j < 0) {
            n_new++; new_uid = x;
            u->gone_since = 0;
            if (u->gift && u->leave_valid && !u->leave_away) {   /* B3 (611aa01): a GIVE's passport is the fish as it LEFT */
                st.gift_left_checked++;
                BOOK((int)f->stage == u->leave_stage, "gift-stage-not-as-left", "tank %c: gift %08x arrived stage %d, it left stage %d:%s", 'A' + k, x, f->stage, u->leave_stage, u->log);
                BOOK(!((u->leave_ms ^ f->ms_bits) & MS_STAGE_BITS), "gift-ms-not-as-left", "tank %c: gift %08x stage bits %x, it left with %x", 'A' + k, x, f->ms_bits & MS_STAGE_BITS, u->leave_ms & MS_STAGE_BITS);
                BOOK(fabsf(b->age[i] - u->leave_age) < 5, "gift-age-not-as-left", "tank %c: gift %08x arrived %.0f s old, it left %.0f s", 'A' + k, x, b->age[i], u->leave_age);
            }
            u->leave_valid = false;
        } else if (a->away[j] && !b->away[i]) {       /* home from next door */
            if (a->nvis + a->nvis_pending >= 2 || b->nvis >= 2) { st.home_into_two++; if (a->born_away[j]) st.fry_home_into_two++; }
            if (a->born_away[j]) { n_home++; home_uid = x;
                if (f->mutated) { st.mut_fry_home++;   /* 51ba947: FIRST MUTATION at the homecoming */
                    BOOK(b->tms & TMS_FIRST_MUTATION, "first-mutation-not-at-home", "tank %c: mutated fry %08x home, no FIRST MUTATION", 'A' + k, x); } }
        }
    }
    /* which fish made a count newly set (B5): the one that arrived, or the fry that came home */
    for (int n = 3; n <= 5; n++) if ((b->tms & POPB[n]) && !(a->tms & POPB[n])) {
        int bit = __builtin_ctz(POPB[n]);
        K[k].count_uid[bit] = (n_new + n_home == 1) ? (n_new ? new_uid : home_uid) : 0;
    }
    /* R2-2: GROW / CHANGE as the keeper sees them vs with the fish next door counted */
    if (in_frame && b->n >= 2) {
        bool wrong = (b->grow_met == 1 && b->grow_cf == 0) || (b->change_met == 1 && b->change_cf == 0);
        if (b->grow_cf != b->grow_met) st.frames_away_any++;
        if (b->grow_met == 1 && b->grow_cf == 0) st.frames_r22_grow++;
        if (b->change_met == 1 && b->change_cf == 0) st.frames_r22_change++;
        if (b->all && !b->all_cf) st.frames_r22_decisive++;
        bool wrong_a = (a->grow_met == 1 && a->grow_cf == 0) || (a->change_met == 1 && a->change_cf == 0);
        if (!a->staged && b->staged && (wrong || wrong_a)) { st.r22_staged_events++; K[k].staged_wrong = true;
            if (getenv("SEAM_TRACE")) printf("  trace t=%.2f %c R2-2: staged while GROW/CHANGE met only because the youngest is next door\n", tsec(), 'A' + k); }
        if (!b->staged) K[k].staged_wrong = false;
        if (came && !left) for (int i = 0; i < b->n; i++) if (s_find(a, b->uid[i]) < 0 && !t->fish[i].born_away && !(t->fish[i].ms_bits & MS_BOUGHT) && !ub(b->uid[i])->gift && a->staged && K[k].staged_wrong)
            st.r22_births++;
    }
    /* R2-4 and every dance: a dance done brings a fry (ours, or one for its home), or says why not */
    if (a->spawning && !b->spawning && a->danced >= SPAWN_DANCE_S - 0.1f && !left) {   /* (a fish leaving mid-dance calls the fry off: a sale's rule) */
        st.dances_done++;
        bool fry = (came && b->n > a->n) || (b->give_k >= 0 && a->give_k < 0);
        if (fry) { st.dances_fry++; if (D[k].run) { double d = tsec() - D[k].run_t0; if (d > st.longest_two_vis_loop) st.longest_two_vis_loop = d; } D[k].run = 0; }
        else {
            if (!D[k].run) D[k].run_t0 = tsec();
            D[k].run++;
            if (a->nvis + a->nvis_pending >= 2 || !tank_link_fry_place(t)) { st.dances_nofry_two_vis++; if (a->nvis + a->nvis_pending < 2) st.dances_nofry_coming++; }
            else { st.dances_nofry_other++;
                BOOK(0, "dance-no-fry", "tank %c: a dance done, no fry: %d own (%d held), %d visitors (+%d pending, %d coming), fry place %d, home room %d (%.0f s ago), doors %d%d, partner %08x mate %08x, staged %d",
                     'A' + k, b->n, progression_fish_held(t), a->nvis, a->nvis_pending, t->vis_coming, tank_link_fry_place(t), t->fry_home_room, t->clock - t->fry_room_at,
                     t->door_open[0], t->door_open[1], t->fry_partner, t->fry_mate, b->staged); }
        }
    }
    if (!b->spawning && !t->courting && D[k].run) { double d = tsec() - D[k].run_t0; if (d > st.longest_two_vis_loop) st.longest_two_vis_loop = d; D[k].run = 0; }
}
static void books(int k, const snap_t *a, const snap_t *b) {
    tank_t *t = &T[k];
    for (int i = 0; i < b->n; i++) { int j = s_find(a, b->uid[i]);   /* (51ba947: a mutated fry of ours home from next door earns FIRST MUTATION) */
        if (j >= 0 && a->born_away[j] && !b->born_away[i] && t->fish[i].mutated) K[k].mut_here++; }
    BOOK(caps == 0, "cap", "tank %c over the cap", 'A' + k);
    BOOK(progression_fish_held(t) <= POP_CAP || t->cap_over, "held-over-cap", "tank %c holds %d", 'A' + k, progression_fish_held(t));
    int counted = 0;
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        counted += !(f->born_away && f->away);   /* (the keeper's view: a fry still next door does not count; one HERE does) */
        BOOK(!f->born_away || f->away, "born-away-at-home", "tank %c: fish %d (%08x) at home with born_away set - its next trip home welcomes it again", 'A' + k, i, f->uid);
    }
    for (int n = 2; n <= counted && n <= 5; n++)
        BOOK(t->tank_ms_bits & POPB[n], "count-mark-missing", "tank %c: %d fish here and no count mark %d", 'A' + k, counted, n);
    for (int n = 2; n <= 5; n++)
        if ((t->tank_ms_bits & POPB[n]) && !(a->tms & POPB[n])) BOOK(counted >= n, "count-mark-early", "tank %c: count mark %d with %d fish counted", 'A' + k, n, counted);
    if (!(a->tms & TMS_FIRST_SALE) && (t->tank_ms_bits & TMS_FIRST_SALE)) BOOK(K[k].sold > 0, "first-sale-bit-no-sale", "tank %c: FIRST FISH SOLD set with no sale (%d given)", 'A' + k, K[k].given_out);
    /* arrivals and departures */
    bool changed = a->n != b->n;
    for (int i = 0; i < b->n; i++) if (s_find(a, b->uid[i]) < 0) changed = true;
    if (changed) BOOK(b->gen != a->gen, "roster-gen", "tank %c: the roster changed (%d -> %d) and roster_gen did not tick", 'A' + k, a->n, b->n);
    bool left = false, came = false;
    for (int i = 0; i < a->n; i++) if (s_find(b, a->uid[i]) < 0) {
        left = true;
        uint32_t x = a->uid[i];
        ub_t *u = ub(x); if (u->home == k) u->home = -1; ulog(u, "%c left%s", 'A' + k, a->away[i] ? " (away)" : "");
        for (int j = 0; j < 6; j++) if (t->bond[j].vis && t->bond[j].mate == x) { st.stale_mate++; BOOK(0, "bond-mate-left", "tank %c: bond %d (visitor %08x) still names %08x, gone", 'A' + k, j, t->bond[j].vis, x); }
        if (t->fry_mate == x) { st.stale_mate++; BOOK(0, "fry-mate-left", "tank %c: fry_mate still names %08x, gone (partner %08x)", 'A' + k, x, t->fry_partner); }
        if (a->born_away[i]) st.given_away_born++;
    }
    for (int i = 0; i < b->n; i++) {
        uint32_t x = b->uid[i]; int j = s_find(a, x);
        ub_t *u = ub(x);
        fish_t *f = &t->fish[i];
        if (j < 0) {                                   /* new here */
            came = true;
            ulog(u, "%c in%s%s ms %x paid %x", 'A' + k, f->born_away ? " born-away" : "", f->away ? " away" : "", f->ms_bits, b->paid[i]);
            bool was_fish = u->ever_home[0] || u->ever_home[1];
            if (was_fish) { u->gift = true; st.arrivals_gift++;
                uint32_t want = SD_PAID_TRUST | (f->ms_bits & MS_REACHED_JUV ? SD_PAID_JUV : 0) | (f->ms_bits & MS_REACHED_ADULT ? SD_PAID_ADULT : 0) | (f->ms_bits & MS_REACHED_ELDER ? SD_PAID_ELDER : 0);
                BOOK((b->paid[i] & want) == want, "gift-ledger", "tank %c: gift %08x paid %x, want %x", 'A' + k, x, b->paid[i], want);
                if (u->age >= 0) BOOK(fabsf(b->age[i] - u->age) < 600 || b->away[i] || u->leave_away, "gift-age", "tank %c: gift %08x arrived %.0f s old, it was %.0f s:%s", 'A' + k, x, b->age[i], u->age, u->log);
            } else {
                u->owed = 1; u->gift = false;
                BOOK(b->paid[i] == 0 || (f->ms_bits & MS_BOUGHT), "new-ledger", "tank %c: new fish %08x paid %x", 'A' + k, x, b->paid[i]);
                if (f->born_away) { st.born_next_door++; BOOK(b->earned - a->earned < SD_BIRTH, "fry-away-paid", "tank %c: the fry still next door paid %d", 'A' + k, b->earned - a->earned); }
                else if (f->mutated && !(f->ms_bits & MS_BOUGHT)) K[k].mut_here++;
                BOOK(b->age[i] < 1, "new-age", "tank %c: new fish %08x is %.0f s old", 'A' + k, x, b->age[i]);
            }
            BOOK(b->starve[i] == 0, "new-starve", "tank %c: new fish %08x starts %.1f s starving", 'A' + k, x, b->starve[i]);
            u->home = k; u->ever_home[k] = true;
        } else {
            if (!b->away[i] && !a->away[j]) {
                BOOK(b->age[i] >= a->age[j] - 0.01f, "age-backwards", "tank %c: %08x %.1f -> %.1f s", 'A' + k, x, a->age[j], b->age[i]);
                BOOK(b->starve[i] == 0 || (b->starve[i] >= a->starve[j] - 0.01f && b->starve[i] <= a->starve[j] + 1.0f), "starve-jump", "tank %c: %08x starve %.1f -> %.1f (slot %d -> %d)", 'A' + k, x, a->starve[j], b->starve[i], j, i);
            }
            uint32_t nw = b->paid[i] & ~a->paid[j];
            static const uint32_t PB4[4] = { SD_PAID_JUV, SD_PAID_ADULT, SD_PAID_ELDER, SD_PAID_TRUST };
            for (int q = 0; q < 4; q++) if (nw & PB4[q]) { u->paid[q]++; ulog(u, "%c pays %d", 'A' + k, q); BOOK(u->paid[q] <= 1, "paid-twice", "tank %c: %08x paid bit %d a %d time:%s", 'A' + k, x, q, u->paid[q], u->log); }
            BOOK((a->paid[j] & ~b->paid[i]) == 0, "ledger-lost", "tank %c: %08x's paid bits %x -> %x", 'A' + k, x, a->paid[j], b->paid[i]);
            if (a->born_away[j] && !b->born_away[i]) { st.fry_home++;
                BOOK(b->earned - a->earned >= SD_BIRTH, "fry-home-unpaid", "tank %c: fry %08x home, earned +%d", 'A' + k, x, b->earned - a->earned); }
        }
        if (!b->away[i]) u->age = b->age[i];
        u->ever_home[k] = true; u->home = k;
    }
    if (!(a->tms & TMS_FIRST_MUTATION) && (t->tank_ms_bits & TMS_FIRST_MUTATION)) {
        BOOK(K[k].mut_here > 0, "first-mutation-bit", "tank %c: FIRST MUTATION set with no mutated birth here", 'A' + k);
        if (K[k].mut_here <= 0) for (int i = 0; i < t->n_fish; i++) printf("    fish %d %08x mutated %d ms %x away %d born_away %d new %d:%s\n", i, t->fish[i].uid, t->fish[i].mutated, t->fish[i].ms_bits,
            t->fish[i].away, t->fish[i].born_away, s_find(a, t->fish[i].uid) < 0, ub(t->fish[i].uid)->log);
    }
    if (left && came) { st.churn++; for (int i = 0; i < t->n_fish; i++) ulog(ub(t->fish[i].uid), "%c CHURN (a fish left and one came in one frame) slot %d", 'A' + k, i);
        if (getenv("SEAM_TRACE")) printf("  trace t=%.2f %c churn %d -> %d fish\n", tsec(), 'A' + k, a->n, b->n); }
    for (int i = 0; i < b->n; i++) if (s_find(a, b->uid[i]) < 0 && !(t->fish[i].ms_bits & MS_ARRIVED)) BOOK(0, "no-ms-arrived", "tank %c: %08x arrived without MS_ARRIVED (ms %x)", 'A' + k, b->uid[i], t->fish[i].ms_bits);
    if (left && !came) BOOK(b->sale_need == b->feedings + SELL_FRY_MEALS, "fry-wait", "tank %c: a fish left, the fry wait %d (feedings %d)", 'A' + k, b->sale_need, b->feedings);
    if (came && !left) BOOK(b->sale_need == a->sale_need, "fry-wait-arrival", "tank %c: an arrival moved the fry wait %d -> %d", 'A' + k, a->sale_need, b->sale_need);
    /* the courting pair names a fish not here (the visitor is): it can never court - count the frames */
    if (t->fry_partner && t->fry_mate) {
        bool here = false; for (int i = 0; i < t->n_fish; i++) here |= t->fish[i].uid == t->fry_mate && !t->fish[i].away;
        if (!here) st.stall_frames++;
    }
    books2(k, a, b, left, came);
}
void sim_clock_freeze(int on); void sim_clock_shift(int64_t s);
static void frame(void) {
    const float dt = 0.033f;
    now += 33;
    if (now / 1000 != (now - 33) / 1000) sim_clock_shift(1);   /* (the wall clock follows the run's: a seed replays exactly) */
    int m = qn; static qf_t q[512]; memcpy(q, Q, sizeof(qf_t) * (size_t)m); qn = 0;
    for (int i = 0; i < m; i++) if (!deaf[q[i].to]) { use(q[i].to); link_rx(L[q[i].to], q[i].b, q[i].len, -30, now); }
    for (int k = 0; k < 2; k++) {
        use(k);
        snap_t a, b; snap(k, &a);
        tank_tick(&T[k], dt, NULL);
        progression_tick(&T[k], dt);
        link_tick(L[k], now);
        tank_link_tick(&T[k], L[k]);
        link_ev_t e; while (link_poll(L[k], &e)) tank_link_event(&T[k], L[k], &e);
        keeper(k, dt);
        snap(k, &b);
        in_frame = true; books(k, &a, &b); in_frame = false;
        BOOK(link_events_lost(L[k]) == 0, "events-lost", "tank %c", 'A' + k);
    }
    /* never two: one fish, one tank */
    for (int k = 0; k < 2; k++) for (int i = 0; i < T[k].n_fish; i++) {
        uint32_t x = T[k].fish[i].uid;
        for (int j = 0; j < T[1 - k].n_fish; j++) BOOK(T[1 - k].fish[j].uid != x, "fish-in-two-tanks", "%08x is a fish of both tanks", x);
    }
}
static void run_s(float s) { for (int n = 0; n < (int)(s * 30); n++) frame(); }

/* ---- the tanks' lives ---- */
static void boot_tank(int k, uint32_t seed, bool first) {
    cold(k);
    memset(&T[k], 0, sizeof T[k]);
    tank_init(&T[k], seed);
    progression_cap_hook = cap_hook;
    memset(&G[k], 0, sizeof G[k]); G[k].persist = true; gift_attach(&T[k], &G[k]);
    shelf_boot(0);
    progression_boot(&T[k]);
    if (first) { progression_setup_done(&T[k]); shelf_rename(-1, k ? "BEA'S\nBAY" : "SAM'S\nREEF"); }
    notice_sync(&T[k]);
    K[k].nb_slot = -1; K[k].nb_uid = 0;
    H[k] = tank_link_host(&T[k], q_send);
    if (!L[k]) L[k] = calloc(1, (size_t)link_size());
    link_init(L[k], T[k].uid, &H[k], rnd(), now);
    link_mode(L[k], true);
}
static void relink(void) {
    use(0); link_mode(L[0], true); link_arm(L[0], LINK_RIGHT);
    run_s(0.2f);
    use(1); link_mode(L[1], true); link_arm(L[1], LINK_LEFT);
    run_s(2);
}
static int home_ok(int k, bool givable) {      /* a fish of k home, clear of the doors (or -1) */
    int n = T[k].n_fish, s = (int)(rnd() % (unsigned)(n ? n : 1));
    for (int q = 0; q < n; q++) { int i = (s + q) % n; fish_t *f = &T[k].fish[i];
        if (!f->away && f->door == DOOR_NONE && (!givable || progression_fish_givable(&T[k], i))) return i; }
    return -1;
}
static int vis_of(int k) { for (int j = 0; j < N_VISIT_MAX; j++) if (T[k].vis_used[j] && !T[k].fish[N_FISH_MAX + j].homeward) return N_FISH_MAX + j; return -1; }

static snap_t ea, eb;
#define ACT_BEGIN(k) do { use(k); snap(k, &ea); } while (0)
#define ACT_END(k)   do { use(k); snap(k, &eb); books(k, &ea, &eb); } while (0)
static void seed_books(int k) {                   /* every fish here now is this tank's (a boot, the start) */
    use(k); for (int i = 0; i < T[k].n_fish; i++) { ub_t *u = ub(T[k].fish[i].uid); u->ever_home[k] = true; u->home = k; u->gone_since = 0; if (!T[k].fish[i].away) u->age = progression_age_s(&T[k], i); }
}
static void ev_visit(int k) {
    use(k); int i = home_ok(k, false); if (i < 0) return;
    fish_t *f = &T[k].fish[i]; f->goal.id = GOAL_EXPLORE; f->door = DOOR_HEADING; f->door_edge = (int8_t)(k ? LINK_LEFT : LINK_RIGHT); f->door_refused = 0;
    st.visits++;
}
static void ev_home(int k) { use(k); int v = vis_of(k); if (v >= 0) { T[k].fish[v].homeward = true; st.homes++; } }
static void ev_deport(int k) { use(k); int v = vis_of(k); if (v >= 0) { tank_link_deport(&T[k], v); st.deports++; } }
static void ev_give(int k, bool away) {
    use(k); int o = 1 - k;
    if (G[k].want_uid || G[k].wait_uid || G[k].asking_uid || G[k].leaving_uid || G[o].ask.on) return;
    int i = -1;
    if (away) { for (int q = 0; q < T[k].n_fish; q++) if (T[k].fish[q].away && progression_fish_givable(&T[k], q) && (i < 0 || T[k].fish[q].born_away)) i = q; }
    else i = home_ok(k, true);
    if (i < 0) return;
    G[k].want_uid = T[k].fish[i].uid; G[k].want_door = GIFT_DOOR_ANY; G[k].want_peer = 0;
    if (away) st.gives_away++; else st.gives++;
    bool yes = rndf() < 0.75f;
    for (int n = 0; n < 30 * 8; n++) { frame(); if (G[o].ask.on) break; }
    if (!G[o].ask.on) return;
    run_s(1 + rndf() * 4);                       /* the other keeper reads the card */
    use(o); if (G[o].ask.on) { G[o].ask.answer = yes ? 1 : -1; if (yes) st.gift_yes++; else st.gift_no++; }
    run_s(2);
}
static void ev_reboot_cut(int k);
/* B2 (44b80ea): the keeper's GIVE asked for a fry of ours still next door - straight into the book, past any modal */
static void ev_give_force(int k, uint32_t uid) {
    use(k); int o = 1 - k;
    if (G[k].want_uid || G[k].wait_uid || G[k].asking_uid || G[k].leaving_uid || G[o].ask.on) return;
    st.give_forced_born_away++;
    G[k].want_uid = uid; G[k].want_door = GIFT_DOOR_ANY; G[k].want_peer = 0;
    bool asked = false;
    for (int n = 0; n < 30 * 4; n++) { frame(); if (G[o].ask.on && G[o].ask.uid == uid) asked = true; }
    use(o); if (G[o].ask.on && G[o].ask.uid == uid) { G[o].ask.answer = 1; run_s(3); }
    if (!asked) st.give_forced_refused++;
    else BOOK(0, "born-away-asked", "tank %c: a GIVE of the fry %08x still next door reached the other keeper", 'A' + k, uid);
}
/* a GIVE with a power cut in it: the receiver or the giver dies at a random moment after the yes (GIVEN_LOST, the
   ARRIVING note's resume, GIVE_RESTORE from the GIVEN note - B3's restore half) */
static void ev_give_cut(int k) {
    use(k); int o = 1 - k;
    if (G[k].want_uid || G[k].wait_uid || G[k].asking_uid || G[k].leaving_uid || G[o].ask.on) return;
    int i = home_ok(k, true); if (i < 0) return;
    G[k].want_uid = T[k].fish[i].uid; G[k].want_door = GIFT_DOOR_ANY; G[k].want_peer = 0;
    for (int n = 0; n < 30 * 8; n++) { frame(); if (G[o].ask.on) break; }
    if (!G[o].ask.on) return;
    run_s(1 + rndf() * 3);
    use(o); if (!G[o].ask.on) return;
    G[o].ask.answer = 1; st.gives_cut++;
    float w = rndf() * 6;                        /* (0-6 s: before the swim out, during, around the commit) */
    run_s(w);
    int who = rndf() < 0.6f ? o : k;
    use(who); ev_reboot_cut(who);
    run_s(5);
}
static void ev_birth(int k) {
    use(k); if (progression_fish_held(&T[k]) >= POP_CAP || progression_newborn() >= 0) return;
    if (rndf() < 0.3f) T[k].mutate_next = true;
    ACT_BEGIN(k); progression_force_arrival(&T[k]); st.births++; ACT_END(k);
}
static void ev_reboot(int k);
static void ev_reboot_cut(int k);
static void ev_give(int k, bool away);
static void ev_cross(int k) {                    /* a visitor here courts our best: the fry ours, or its home's */
    use(k); int v = vis_of(k); if (v < 0 || T[k].fry_partner || progression_newborn() >= 0) return;
    uint32_t par = T[k].fish[v].uid;
    T[k].fry_partner = par; T[k].fry_asked = false; T[k].fry_home_room = -1;
    for (int n = 0; n < 60; n++) { use(k); int w = -1; for (int j = 0; j < N_VISIT_MAX; j++) if (T[k].vis_used[j] && T[k].fish[N_FISH_MAX + j].uid == par) w = N_FISH_MAX + j;
        if (w < 0) return; T[k].fish[w].homeward = false; T[k].vis_since[w - N_FISH_MAX] = T[k].clock; frame(); }
    use(k);
    if (T[k].fry_partner != par) return;
    T[k].fry_force = rndf() < 0.6f ? 1 : 0; if (rndf() < 0.3f) T[k].mutate_next = true;
    int n0 = T[k].n_fish; st.cross_try++;
    ACT_BEGIN(k); progression_force_arrival(&T[k]); ACT_END(k);
    if (T[k].fry_give_k >= 0) st.cross_home++; else if (T[k].n_fish > n0) st.cross_kept++;
    T[k].fry_force = -1;
    if (T[k].fry_give_k >= 0) {                  /* its home: while the fry is still next door, now and then a reboot (P5) or a GIVE of it */
        int o = 1 - k, got = -1;
        for (int n = 0; n < 30 * 5 && got < 0; n++) { frame(); use(o); for (int i = 0; i < T[o].n_fish; i++) if (T[o].fish[i].born_away && T[o].fish[i].away) got = i; }
        float r = rndf();
        if (got >= 0 && r < 0.35f) ev_reboot(o);
        else if (got >= 0 && r < 0.55f) ev_give(o, true);
        else if (got >= 0 && r < 0.62f) ev_give_force(o, T[o].fish[got].uid);   /* (B2: the gate itself, whatever the modal shows) */
        else if (got >= 0 && r < 0.75f) { use(o); int j = home_ok(o, false); if (j >= 0 && progression_fish_sellable(&T[o], j)) { ub_t *u = ub(T[o].fish[j].uid); u->sold = true; ACT_BEGIN(o); K[o].sold++; if (!progression_sell_fish(&T[o], j)) { K[o].sold--; u->sold = false; } else st.sales++; ACT_END(o); } }
    }
}
static void ev_sell(int k) {
    use(k); int i = home_ok(k, false); if (i < 0 || !progression_fish_sellable(&T[k], i)) return;
    ub_t *u = ub(T[k].fish[i].uid); u->sold = true;
    ACT_BEGIN(k); K[k].sold++;
    if (progression_sell_fish(&T[k], i)) st.sales++; else { K[k].sold--; u->sold = false; }
    ACT_END(k);
}
static void ev_buy(int k) {
    use(k); if (!progression_fry_for_sale(&T[k])) return;
    T[k].sd_balance += SD_PRICE_FRY;
    for (int q = 0; q < FRY_OFFER_N; q++) { ACT_BEGIN(k); bool ok = progression_buy_fry(&T[k], q); ACT_END(k); if (ok) { st.buys++; return; } }
    T[k].sd_balance -= SD_PRICE_FRY; progression_fry_roll(&T[k]);
}
static void ev_reboot2(int k, int force_cut);
static void ev_reboot(int k) { ev_reboot2(k, -1); }
static void ev_reboot_cut(int k) { ev_reboot2(k, 1); }
static void ev_reboot2(int k, int force_cut) {
    use(k);
    bool cut = force_cut >= 0 ? force_cut == 1 : rndf() < 0.4f;   /* a power cut: what the last 30 s did not save is lost */
    bool saved_ok = !cut && progression_save(&T[k]);
    if (getenv("SEAM_TRACE")) for (int i = 0; i < T[k].n_fish; i++) printf("  trace t=%.2f %c reboot%s (saved %d): slot %d %08x away %d born_away %d newborn %d\n", tsec(), 'A' + k, cut ? " CUT" : "", saved_ok, i, T[k].fish[i].uid, T[k].fish[i].away, T[k].fish[i].born_away, progression_newborn());
    uint32_t uid = T[k].uid;
    for (int i = 0; i < T[k].n_fish; i++) st.reboot_fry_away += T[k].fish[i].born_away && T[k].fish[i].away;
    K[k].booted = true;
    deaf[k] = true; run_s(0.5f);                 /* (dead: hears nothing; the radio drops what it sent) */
    deaf[k] = false;
    boot_tank(k, rnd() | 1, false);
    if (getenv("SEAM_TRACE")) for (int i = 0; i < T[k].n_fish; i++) printf("  trace t=%.2f %c booted: slot %d %08x away %d born_away %d newborn %d\n", tsec(), 'A' + k, i, T[k].fish[i].uid, T[k].fish[i].away, T[k].fish[i].born_away, progression_newborn());
    BOOK(T[k].uid == uid, "reboot-uid", "tank %c came back as %08x", 'A' + k, T[k].uid);
    seed_books(k);
    for (int i = 0; i < T[k].n_fish; i++) {         /* what a power cut rolled back is owed (and may be said, paid) again */
        ub_t *u = ub(T[k].fish[i].uid); fish_t *f = &T[k].fish[i];
        for (int b = 0; b < 32; b++) if (!(f->ms_bits >> b & 1)) u->ms_said[b] = 0;
        for (int sg = (f->stage & 3) + 1; sg < 4; sg++) u->stage_said[sg] = 0;
        static const uint32_t PB4[4] = { SD_PAID_JUV, SD_PAID_ADULT, SD_PAID_ELDER, SD_PAID_TRUST };
        for (int q = 0; q < 4; q++) if (!(T[k].sd_paid_fish[i] & PB4[q])) u->paid[q] = 0;
    }
    for (int b = 0; b < 32; b++) if (!(T[k].tank_ms_bits >> b & 1)) K[k].tms_said[b] = 0;
    if (progression_newborn() >= 0) ub(T[k].fish[progression_newborn()].uid)->welcomes = 0;
    st.reboots++; st.cuts += cut;
    for (int i = 0; i < T[k].n_fish; i++) ulog(ub(T[k].fish[i].uid), "%c reboot%s", 'A' + k, cut ? " CUT" : "");
    relink();
}
static bool gifts_busy(void) { for (int k = 0; k < 2; k++) if (G[k].want_uid || G[k].wait_uid || G[k].asking_uid || G[k].leaving_uid || G[k].n_given || G[k].n_arr || G[k].ask.on) return true; return false; }
static void ev_night(void) {                     /* everyone home, a night asleep, a day's growth */
    for (int n = 0; n < 30 * 180 && gifts_busy(); n++) frame();   /* (a gift in flight settles first: the device's sleep keeps it - link_sleep) */
    if (gifts_busy()) return;
    for (int k = 0; k < 2; k++) { use(k); for (int j = 0; j < N_VISIT_MAX; j++) if (T[k].vis_used[j]) T[k].fish[N_FISH_MAX + j].homeward = true; }
    for (int n = 0; n < 30 * 120; n++) { frame(); bool any = false; for (int k = 0; k < 2; k++) for (int i = 0; i < T[k].n_fish; i++) any |= T[k].fish[i].away; if (!any) break;
        for (int k = 0; k < 2; k++) { use(k); for (int j = 0; j < N_VISIT_MAX; j++) if (T[k].vis_used[j]) T[k].fish[N_FISH_MAX + j].homeward = true; } }
    for (int n = 0; n < 30 * 20; n++) {          /* (a real sleep shuts the doors: no OFFER outstanding across the night's growth - its
                                                    passport was taken at the ask, and a fish crossing after the jump took hours-old books) */
        bool ask = false; for (int k = 0; k < 2; k++) for (int i = 0; i < T[k].n_fish; i++) ask |= T[k].fish[i].door != DOOR_NONE;
        if (!ask) break;
        for (int k = 0; k < 2; k++) for (int i = 0; i < T[k].n_fish; i++) if (T[k].fish[i].door == DOOR_HEADING) T[k].fish[i].door = DOOR_NONE;
        frame();
    }
    for (int k = 0; k < 2; k++) {
        use(k);
        bool any = false; for (int i = 0; i < T[k].n_fish; i++) any |= T[k].fish[i].away || T[k].fish[i].door != DOOR_NONE;
        if (any) continue;                       /* (someone still out: no time jump this night) */
        for (int i = 0; i < T[k].n_fish; i++) progression_set_age(&T[k], i, progression_age_s(&T[k], i) + 14 * 3600 * rndf());
        if (getenv("SEAM_TRACE")) printf("  trace t=%.2f %c NIGHT\n", tsec(), 'A' + k);
        progression_slept(&T[k], 9 * 3600);
        for (int i = 0; i < T[k].n_fish; i++) { ub_t *u = ub(T[k].fish[i].uid); u->age = progression_age_s(&T[k], i); }
        for (int i = 0; i < T[k].n_fish; i++) T[k].fish[i].trust = fminf(10, T[k].fish[i].trust + rndf() * 3);
    }
    st.nights++;
    relink();
}

/* ---- two cases told on their own (one tank, no radio) ---- */
static passport_t fry_pp(uint32_t seed, const tank_t *home) {   /* one of ours born next door: its passport as the host sends it */
    static tank_t there; tank_init(&there, seed); tank_new_population(&there); tank_ensure_ids(&there);
    fish_t fry = there.fish[0]; fry.stage = STAGE_FRY; fry.size = 0.4f; fry.ms_bits = MS_CROSSBRED; fry.mutated = 0;
    fry.parent_uid[0] = home->fish[0].uid; fry.parent_uid[1] = 0xc0ffee77u;
    passport_t pp; passport_from_fish(&pp, &fry, home->uid, 0); return pp;
}
static void drain_welcomes(tank_t *t) { while (progression_newborn() >= 0) progression_newborn_done(t); }
/* S1: a fry of ours born next door, a sale while it is still out, then a birth here: the new fry inherits born_away */
static int case_stale_born_away(void) {
    int bad = 0;
    cold(0); tank_t *t = &T[0]; (void)remove(getenv("POCKET_TANK_SAVE"));
    memset(t, 0, sizeof *t); tank_init(t, 4242); progression_cap_hook = cap_hook;
    memset(&G[0], 0, sizeof G[0]); gift_attach(t, &G[0]);
    progression_boot(t); progression_setup_done(t); notice_sync(t);
    for (int i = 0; i < t->n_fish; i++) progression_set_age(t, i, STAGE_ADULT_AGE + 60);
    { passport_t g = fry_pp(901, t); g.uid ^= 0x5a5a5a5au; g.stage = STAGE_ADULT; g.age_s = STAGE_ADULT_AGE + 99; g.ms_bits = MS_ARRIVED | MS_REACHED_JUV | MS_REACHED_ADULT;
      progression_gift_adopt(t, &g); }           /* three fish (a gift): one may be sold and two stay */
    passport_t pp = fry_pp(902, t);
    int fs = progression_fry_adopt(t, &pp, LINK_RIGHT);          /* FRY_IN: ours, next door, swimming home */
    printf("  S1: three fish and a fry of ours born next door (slot %d, born_away %d)\n", fs, fs >= 0 && t->fish[fs].born_away);
    if (fs != 3) { printf("  S1: FAIL staging (slot %d)\n", fs); return 1; }
    int32_t bal;
    if (!progression_sell_fish(t, 0)) { printf("  S1: FAIL the sale was refused\n"); return 1; }   /* the keeper sells one while the fry is out */
    printf("  S1: sold fish 0 while the fry was next door: the fry moves to slot 2, n_fish %d; the slot it left (3) keeps born_away=%d\n", t->n_fish, t->fish[3].born_away);
    t->fish[2].away = false; progression_fry_home(t, 2);                     /* home (FISH_BACK) */
    drain_welcomes(t);
    for (int k = 0; k < 10; k++) progression_tick(t, 0.1f);
    t->tank_ms_bits &= ~TMS_QUARTET; t->tank_ms_seen &= ~TMS_QUARTET;        /* (never four before: the count from here) */
    bal = t->sd_balance;
    progression_force_arrival(t);                                            /* a birth here, into slot 3 */
    int nb = progression_newborn();
    if (nb != 3) { printf("  S1: FAIL no birth in slot 3 (%d)\n", nb); return 1; }
    uint32_t uid = t->fish[nb].uid;
    printf("  S1: a birth here, slot 3 (%08x): away %d, born_away %d; +%d SD; QUARTET with %d fish here: %s\n", uid, t->fish[nb].away, t->fish[nb].born_away,
           (int)(t->sd_balance - bal), t->n_fish, t->tank_ms_bits & TMS_QUARTET ? "yes" : "NO");
    if (t->fish[nb].born_away) { printf("  S1: FINDING: a fish born HERE carries born_away (tank_make_fish never clears it)\n"); bad++; }
    if (!(t->tank_ms_bits & TMS_QUARTET)) { printf("  S1: FINDING: four fish here and no QUARTET mark (pop_marks skips the fry it thinks is next door)\n"); bad++; }
    drain_welcomes(t);
    /* its first visit next door and back */
    t->fish[nb].away = true; progression_save(t);
    bal = t->sd_balance;
    t->fish[nb].away = false; progression_fry_home(t, nb);                   /* LEV_FISH_BACK */
    int again = progression_newborn();
    printf("  S1: it visits next door and swims home: welcome owed again %s, +%d SD (SD_BIRTH %d)\n", again == nb ? "YES" : "no", (int)(t->sd_balance - bal), SD_BIRTH);
    if (again == nb) { printf("  S1: FINDING: a second birth flow for a fish already welcomed, and the birth's sand dollars paid again\n"); bad++; }
    drain_welcomes(t);
    return bad;
}
/* S2: a visitor that has spent its first minute beside one of ours keeps that fish as its mate for good: sold, given or
   gone, its time together never grows again and it never courts here (bond[].mate is never let go) */
static int bond_run(bool sell, float *s_out, uint32_t *partner) {
    cold(0); tank_t *t = &T[0]; (void)remove(getenv("POCKET_TANK_SAVE"));
    memset(t, 0, sizeof *t); tank_init(t, 5151); progression_cap_hook = cap_hook;
    memset(&G[0], 0, sizeof G[0]); gift_attach(t, &G[0]);
    progression_boot(t); progression_setup_done(t); notice_sync(t);
    { passport_t g = fry_pp(911, t); g.uid ^= 0x13579bdfu; g.stage = STAGE_ADULT; g.age_s = STAGE_ADULT_AGE + 99; g.ms_bits = MS_ARRIVED | MS_REACHED_JUV | MS_REACHED_ADULT;
      progression_gift_adopt(t, &g); }
    for (int i = 0; i < t->n_fish; i++) { progression_set_age(t, i, STAGE_ADULT_AGE + 60); t->fish[i].x = 100 + 150 * i; t->fish[i].y = 200; }
    fish_t *v = &t->fish[N_FISH_MAX]; *v = t->fish[0]; v->uid = 0x0b0dfaceu; v->away = false; v->homeward = false; v->stage = STAGE_ADULT;
    t->vis_used[0] = true; t->vis_since[0] = t->clock; t->vis_home[0] = 0x77777777u;
    uint32_t m = t->fish[2].uid;
    for (int k = 0; k < 900; k++) { v->x = t->fish[2].x + 10; v->y = t->fish[2].y; progression_tick(t, 0.1f); }   /* 90 s beside fish 2 */
    float s0 = progression_bond(t, v->uid, NULL); uint32_t mate = 0; progression_bond(t, v->uid, &mate);
    if (sell) progression_sell_fish(t, 2);                                    /* the keeper sells it (or GIVEs it next door) */
    for (int k = 0; k < 12000; k++) {                                         /* 20 min beside fish 0, the light on, the glass clean */
        v->x = t->fish[0].x + 10; v->y = t->fish[0].y; t->vis_since[0] = t->clock - 120; progression_tick(t, 0.1f);
        if (t->fry_partner) break;
    }
    *s_out = progression_bond(t, v->uid, NULL); *partner = t->fry_partner;
    printf("  S2 %s: after 90 s beside %08x the bond is %.2f (mate %08x)%s; 20 min beside fish 0 later: bond %.2f, courting partner %08x\n",
           sell ? "sold" : "kept", m, s0, mate, sell ? ", then that fish is SOLD" : "", *s_out, *partner);
    return 0;
}
static int case_bond_stall(void) {
    float s_keep, s_sold; uint32_t p_keep, p_sold;
    bond_run(false, &s_keep, &p_keep); bond_run(true, &s_sold, &p_sold);
    if (p_keep && !p_sold) { printf("  S2: FINDING: a visitor whose mate was sold never courts again (bond stays at %.2f)\n", s_sold); return 1; }
    return 0;
}
/* S3: GIVE through the door: the gift comes to life next door as its passport was when the keeper ASKED, not as it left.
   A stage reached while the other keeper reads the card: paid here, paid again there, said in both */
static int case_gift_passport(void) {
    for (int k = 0; k < 2; k++) boot_tank(k, 0x2000u + (uint32_t)k * 31, true);
    for (int k = 0; k < 2; k++) { use(k); for (int i = 0; i < T[k].n_fish; i++) { progression_set_age(&T[k], i, STAGE_ADULT_AGE + 60); T[k].fish[i].door_seen = T[k].clock + 3600; } }
    use(0); { passport_t g = fry_pp(931, &T[0]); g.uid ^= 0x2468ace0u; g.stage = STAGE_JUV; g.age_s = STAGE_ADULT_AGE - 20; g.ms_bits = MS_ARRIVED | MS_REACHED_JUV;
      progression_gift_adopt(&T[0], &g); }    /* A's third fish: a juvenile 20 s from grown */
    int gi = T[0].n_fish - 1; uint32_t gu = T[0].fish[gi].uid; T[0].fish[gi].door_seen = T[0].clock + 3600;
    relink(); run_s(10);
    use(0); gi = -1; for (int i = 0; i < T[0].n_fish; i++) if (T[0].fish[i].uid == gu) gi = i;
    if (gi < 0 || !T[0].door_open[LINK_RIGHT]) { printf("  S3: FAIL staging (fish %d, door %d)\n", gi, T[0].door_open[LINK_RIGHT]); return 1; }
    progression_set_age(&T[0], gi, STAGE_ADULT_AGE - 20); T[0].fish[gi].stage = STAGE_JUV; T[0].fish[gi].ms_bits &= ~MS_REACHED_ADULT; T[0].sd_paid_fish[gi] &= ~SD_PAID_ADULT;
    G[0].want_uid = gu; G[0].want_door = GIFT_DOOR_ANY; G[0].want_peer = 0;
    for (int n = 0; n < 30 * 8 && !G[1].ask.on; n++) frame();
    if (!G[1].ask.on) { printf("  S3: FAIL no question reached B\n"); return 1; }
    use(0); int32_t eA = T[0].sd_earned;
    run_s(30);                                   /* B's keeper reads the card; the fish grows up in A meanwhile */
    use(0); int sA = -1; for (int i = 0; i < T[0].n_fish; i++) if (T[0].fish[i].uid == gu) sA = T[0].fish[i].stage;
    int32_t paidA = T[0].sd_earned - eA;
    use(1); G[1].ask.answer = 1; int32_t eB = T[1].sd_earned;
    int bi = -1; for (int n = 0; n < 30 * 90 && bi < 0; n++) { frame(); use(1); for (int i = 0; i < T[1].n_fish; i++) if (T[1].fish[i].uid == gu) bi = i; }
    if (bi < 0) { printf("  S3: FAIL the gift never arrived\n"); return 1; }
    use(1); int stB0 = T[1].fish[bi].stage; float ageB0 = progression_age_s(&T[1], bi); uint32_t msB0 = T[1].fish[bi].ms_bits, pdB0 = T[1].sd_paid_fish[bi];
    run_s(30);
    use(1); bi = -1; for (int i = 0; i < T[1].n_fish; i++) if (T[1].fish[i].uid == gu) bi = i;
    int32_t paidB = T[1].sd_earned - eB;
    printf("  S3: asked as a juvenile; grown up in A while B read the card (A stage %d, +%d SD in A);\n"
           "      arrived in B as stage %d, %.0f s old, ms %x, paid %x; 30 s later stage %d, B earned +%d SD (SD_STAGE_ADULT %d)\n",
           sA, (int)paidA, stB0, ageB0, msB0, pdB0, bi >= 0 ? T[1].fish[bi].stage : -1, (int)paidB, SD_STAGE_ADULT);
    int bad = 0;
    if (sA == STAGE_ADULT && stB0 < STAGE_ADULT) { printf("  S3: FINDING: the gift arrived younger than it left (the ask's passport)\n"); bad++; }
    if (paidA >= SD_STAGE_ADULT && paidB >= SD_STAGE_ADULT) { printf("  S3: FINDING: its grown-up stage paid in both tanks\n"); bad++; }
    return bad;
}

/* ---- round 2 told cases (c04f87e) ---- */
static void add_adult_gift(int k, uint32_t seed, uint32_t mix) {   /* one more grown fish of k's (a gift, as S1 makes them) */
    use(k); passport_t g = fry_pp(seed, &T[k]); g.uid ^= mix; g.stage = STAGE_ADULT; g.age_s = STAGE_ADULT_AGE + 99; g.ms_bits = MS_ARRIVED | MS_REACHED_JUV | MS_REACHED_ADULT;
    progression_gift_adopt(&T[k], &g);
}
static int vis_count(int k) { int n = 0; for (int j = 0; j < N_VISIT_MAX; j++) n += T[k].vis_used[j]; return n; }
static int swim_count(int k) { int n = 0; for (int i = 0; i < N_FISH_SLOTS; i++) n += tank_swims(&T[k], i); return n; }
static void hold_visitors(void) {               /* visitors stay (not drawn home), as a keeper's tanks often hold them */
    for (int k = 0; k < 2; k++) for (int j = 0; j < N_VISIT_MAX; j++) if (T[k].vis_used[j]) { T[k].fish[N_FISH_MAX + j].homeward = false; T[k].vis_since[j] = T[k].clock; }
}
static bool send_over(int k, int i, float max_s) {   /* fish i of k through its door to the other tank (true: it is next door) */
    uint32_t uid = T[k].fish[i].uid;
    for (int n = 0; n < (int)(max_s * 30); n++) {
        use(k); int s = -1; for (int q = 0; q < T[k].n_fish; q++) if (T[k].fish[q].uid == uid) s = q;
        if (s < 0) return false;
        fish_t *f = &T[k].fish[s];
        if (f->away) return true;
        if (f->door == DOOR_NONE) { f->goal.id = GOAL_EXPLORE; f->door = DOOR_HEADING; f->door_edge = (int8_t)(k ? LINK_LEFT : LINK_RIGHT); f->door_refused = 0; f->door_sent = false; }
        hold_visitors(); frame();
    }
    return false;
}
static void wipe_dirs(void) {
    for (int k = 0; k < 2; k++) { char cmd[700]; snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", dirs[k], dirs[k]); (void)system(cmd); }
    for (int k = 0; k < 2; k++) { memset(&G[k], 0, sizeof G[k]); if (L[k]) memset(L[k], 0, (size_t)link_size()); }
    qn = 0;
    memset(K, 0, sizeof K); un = 0; memset(D, 0, sizeof D);
}
static void two_full_tanks(uint32_t base) {
    wipe_dirs();
    for (int k = 0; k < 2; k++) boot_tank(k, base + (uint32_t)k * 31, true);
    for (int k = 0; k < 2; k++) for (int q = 0; q < 3; q++) add_adult_gift(k, base + 100 + (uint32_t)(k * 7 + q), 0x1111u * (uint32_t)(k * 3 + q + 1));
    for (int k = 0; k < 2; k++) { use(k); for (int i = 0; i < T[k].n_fish; i++) { progression_set_age(&T[k], i, STAGE_ADULT_AGE + 60); T[k].fish[i].trust = 7; T[k].fish[i].door_seen = T[k].clock + 1e5; } }
    relink(); run_s(10);
}
/* S4: the visitor rule (efe7414): two full tanks; A's fish next door, B's two in A - A hosts 2 with 4 of its own: 6
   swimming. A's fish comes home: 7, never refused (ours always has its place); a third visitor is refused */
static int case_visitor_rule(void) {
    int bad = 0;
    two_full_tanks(0x4100u);
    printf("  S4: A %d fish, B %d fish\n", T[0].n_fish, T[1].n_fish);
    use(0); uint32_t ours = T[0].fish[2].uid;
    if (!send_over(0, 2, 60)) { printf("  S4: FAIL A's fish never crossed\n"); return 1; }
    if (!send_over(1, 1, 60) || !send_over(1, 3, 60)) { printf("  S4: FAIL B's two never crossed (A visitors %d)\n", vis_count(0)); return 1; }
    run_s(3); hold_visitors();
    printf("  S4: A hosts %d visitors, %d swimming (5 own, one next door); B hosts %d\n", vis_count(0), swim_count(0), vis_count(1));
    use(1); int v = -1; for (int j = 0; j < N_VISIT_MAX; j++) if (T[1].vis_used[j] && T[1].fish[N_FISH_MAX + j].uid == ours) v = j;
    if (v < 0) { printf("  S4: FAIL A's fish is not in B\n"); return 1; }
    T[1].fish[N_FISH_MAX + v].homeward = true;
    double t0 = tsec(); bool back = false;
    for (int n = 0; n < 30 * 90 && !back; n++) {
        for (int k = 0; k < 2; k++) for (int j = 0; j < N_VISIT_MAX; j++) if (T[k].vis_used[j] && T[k].fish[N_FISH_MAX + j].uid != ours) { T[k].fish[N_FISH_MAX + j].homeward = false; T[k].vis_since[j] = T[k].clock; }
        frame(); use(0); for (int i = 0; i < T[0].n_fish; i++) if (T[0].fish[i].uid == ours && !T[0].fish[i].away) back = true;
    }
    use(0);
    printf("  S4: called home: %s after %.0f s; A now %d swimming, %d visitors; swim hook %d\n", back ? "HOME" : "NOT HOME", tsec() - t0, swim_count(0), vis_count(0), swim_hooks);
    if (!back) { printf("  S4: FINDING: a fish of ours could not come home to a tank hosting two\n"); bad++; }
    if (back && swim_count(0) != 7) { printf("  S4: FINDING: %d swimming, not 7\n", swim_count(0)); bad++; }
    /* a third visitor for A: refused (full) */
    use(1); int third = -1; for (int i = 0; i < T[1].n_fish; i++) if (!T[1].fish[i].away) third = i;
    bool crossed = third >= 0 && send_over(1, third, 25);
    printf("  S4: a third visitor for A: %s (A visitors %d)\n", crossed ? "CROSSED" : "refused", vis_count(0));
    if (crossed) { printf("  S4: FINDING: a third visitor came in\n"); bad++; }
    return bad;
}
/* S5 (R2-4, efe7414): a pair of B's and a visitor from A courts in B (time together done); A has room and says yes; a
   second visitor from A is in B, so the fry would be B's third visitor: the dance runs, no fry, and again */
static int case_dance_loop(void) {
    wipe_dirs();
    for (int k = 0; k < 2; k++) boot_tank(k, 0x5200u + (uint32_t)k * 31, true);
    add_adult_gift(0, 0x5301, 0x2222u); add_adult_gift(1, 0x5302, 0x3333u);     /* A 3, B 3 */
    for (int k = 0; k < 2; k++) { use(k); for (int i = 0; i < T[k].n_fish; i++) { progression_set_age(&T[k], i, STAGE_ADULT_AGE + 60); T[k].fish[i].trust = 7; T[k].fish[i].door_seen = T[k].clock + 1e5; } }
    relink(); run_s(10);
    if (!send_over(0, 1, 60) || !send_over(0, 2, 60)) { printf("  S5: FAIL A's two never crossed\n"); return 1; }
    run_s(2); hold_visitors();
    use(1); int vp = -1; for (int j = 0; j < N_VISIT_MAX; j++) if (T[1].vis_used[j]) { vp = j; break; }
    uint32_t partner = T[1].fish[N_FISH_MAX + vp].uid;
    T[1].fry_partner = partner; T[1].fry_mate = T[1].fish[0].uid; T[1].fry_home_room = -1; T[1].fry_asked = false;
    /* (round 2, review F3: the partner swims free - pinned beside fish 0 every frame it never reached the court site, so
       the tank never really danced; the visitors are only kept from going home. Judged on the dance while two are in, and
       on the fry once the second is home) */
    int dances = 0, fry = 0, spawn_two = 0, held_two = 0; bool was = false; double t_first = -1, t_fry = -1, t_rel = -1; int vis_at_fry = -1;
    for (int n = 0; n < 30 * 480; n++) {          /* 8 minutes; the second visitor goes home after 4 */
        bool release = n > 30 * 240;
        if (release && t_rel < 0) t_rel = tsec();
        for (int j = 0; j < N_VISIT_MAX; j++) if (T[1].vis_used[j]) {
            fish_t *f = &T[1].fish[N_FISH_MAX + j];
            if (f->uid == partner) { f->homeward = false; T[1].vis_since[j] = T[1].clock; }
            else if (!release) { f->homeward = false; T[1].vis_since[j] = T[1].clock; } else f->homeward = true;
        }
        int n0 = T[1].n_fish, gk0 = T[1].fry_give_k;
        frame(); use(1);
        if (vis_count(1) == 2) { spawn_two += T[1].spawning; use(0); held_two += progression_fish_held(&T[0]) > T[0].n_fish; use(1); }
        if (was && !T[1].spawning) { dances++; if (t_first < 0) t_first = tsec(); }
        if (T[1].n_fish > n0 || (T[1].fry_give_k >= 0 && gk0 < 0)) { fry++; t_fry = tsec(); vis_at_fry = vis_count(1); break; }
        was = T[1].spawning;
    }
    printf("  S5: B: partner %08x, home room %d; with two visitors in: %d frames dancing, %d frames A held a place; dances ended %d; the second home at %.0f s, fry at %.0f s (%d visitors then)\n",
           partner, T[1].fry_home_room, spawn_two, held_two, dances, t_rel, t_fry, vis_at_fry);
    if (spawn_two || held_two) { printf("  S5: FINDING (R2-4): with two visitors in the pair danced (%d frames) or the home held a place (%d frames)\n", spawn_two, held_two); return 1; }
    if (!fry) { printf("  S5: FINDING: the second visitor home, the fry never came\n"); return 1; }
    return 0;
}
/* S6 (R2-2, 4c0055d): three fish, the youngest a fry born here; every other gate met. The fry swims next door: GROW and
   CHANGE read an adult, the fry is staged, and it is born after the fry comes home still a fry */
static int case_youngest_away(void) {
    cold(0); tank_t *t = &T[0]; (void)remove(getenv("POCKET_TANK_SAVE"));
    memset(t, 0, sizeof *t); tank_init(t, 6161); progression_cap_hook = cap_hook;
    memset(&G[0], 0, sizeof G[0]); gift_attach(t, &G[0]);
    progression_boot(t); progression_setup_done(t); notice_sync(t);
    for (int i = 0; i < t->n_fish; i++) { progression_set_age(t, i, STAGE_ADULT_AGE + 60); t->fish[i].trust = 9; t->fish[i].drift_acc = 99; }
    progression_force_arrival(t); drain_welcomes(t);
    int fry = t->n_fish - 1; t->fish[fry].trust = 9; t->fish[fry].drift_acc = 0;
    t->player_feedings = 400; t->hold_approaches = 5;
    int8_t g0, c0, g1, c1; bool st0, st1, all0, all1;
    for (int k = 0; k < 5; k++) progression_tick(t, 0.1f);
    gates(t, &g0, &c0, &st0, &all0);
    t->fish[fry].away = true;                     /* (its visit next door) */
    for (int k = 0; k < 5; k++) progression_tick(t, 0.1f);
    gates(t, &g1, &c1, &st1, &all1);
    t->fish[fry].away = false;                    /* home again, still a fry */
    int n0 = t->n_fish; float waited = 0;
    for (int k = 0; k < 10 * 120 && t->n_fish == n0; k++) { progression_tick(t, 0.1f); tank_tick(t, 0.1f, NULL); waited += 0.1f; }
    printf("  S6: fry here: GROW %d CHANGE %d staged %d; fry next door: GROW %d CHANGE %d staged %d; home still a fry (stage %d): %s after %.0f s\n",
           g0, c0, st0, g1, c1, st1, t->fish[fry].stage, t->n_fish > n0 ? "A FRY IS BORN" : "no birth", waited);
    if (!g0 && g1 && st1) { printf("  S6: FINDING (R2-2): the youngest's visit met GROW and staged a fry%s\n", t->n_fish > n0 ? ", born while the youngest is still a fry" : ""); return 1; }
    return 0;
}
/* S2b: GIVE (progression_give_out) lets go of the visitor's time together too (9f12374's other path) */
static int case_bond_give(void) {
    cold(0); tank_t *t = &T[0]; (void)remove(getenv("POCKET_TANK_SAVE"));
    memset(t, 0, sizeof *t); tank_init(t, 5252); progression_cap_hook = cap_hook;
    memset(&G[0], 0, sizeof G[0]); gift_attach(t, &G[0]);
    progression_boot(t); progression_setup_done(t); notice_sync(t);
    { passport_t g = fry_pp(912, t); g.uid ^= 0x2468u; g.stage = STAGE_ADULT; g.age_s = STAGE_ADULT_AGE + 99; g.ms_bits = MS_ARRIVED | MS_REACHED_JUV | MS_REACHED_ADULT; progression_gift_adopt(t, &g); }
    for (int i = 0; i < t->n_fish; i++) { progression_set_age(t, i, STAGE_ADULT_AGE + 60); t->fish[i].x = 100 + 150 * i; t->fish[i].y = 200; }
    fish_t *v = &t->fish[N_FISH_MAX]; *v = t->fish[0]; v->uid = 0x0b0dfac2u; v->away = false; v->homeward = false; v->stage = STAGE_ADULT;
    t->vis_used[0] = true; t->vis_since[0] = t->clock; t->vis_home[0] = 0x77777777u;
    for (int k = 0; k < 900; k++) { v->x = t->fish[2].x + 10; v->y = t->fish[2].y; progression_tick(t, 0.1f); }
    float s0 = progression_bond(t, v->uid, NULL);
    uint32_t gone = t->fish[2].uid;
    progression_give_out(t, 2);
    uint32_t mate = 1; float s1 = progression_bond(t, v->uid, &mate);
    printf("  S2b: bond %.2f beside %08x; GIVEN: bond %.2f, mate %08x\n", s0, gone, s1, mate);
    if (mate == gone) { printf("  S2b: FINDING: GIVE kept the visitor's mate\n"); return 1; }
    return 0;
}

/* S7 (sibling of 50e32f8 #7): a fry of ours born next door comes home (FISH_BACK) and is welcomed; a power cut inside the
   save gap. The homecoming was never saved: the boot finds it next door, brings it home from bubbles, and its birth flow
   (the welcome, the +20, its count) comes again */
static int case_fry_home_cut(void) {
    cold(0); tank_t *t = &T[0]; (void)remove(getenv("POCKET_TANK_SAVE"));
    memset(t, 0, sizeof *t); tank_init(t, 7171); progression_cap_hook = cap_hook;
    memset(&G[0], 0, sizeof G[0]); gift_attach(t, &G[0]);
    progression_boot(t); progression_setup_done(t); notice_sync(t);
    for (int i = 0; i < t->n_fish; i++) progression_set_age(t, i, STAGE_ADULT_AGE + 60);
    passport_t pp = fry_pp(971, t);
    int fs = progression_fry_adopt(t, &pp, LINK_RIGHT);          /* FRY_IN: saved at once (born_away, away) */
    uint32_t uid = t->fish[fs].uid;
    for (int k = 0; k < 20; k++) progression_tick(t, 0.1f);
    t->fish[fs].away = false; progression_fry_home(t, fs);      /* LEV_FISH_BACK: welcomed, +20 */
    int w1 = progression_newborn() == fs; int32_t bal1 = t->sd_balance;
    for (int k = 0; k < 50; k++) progression_tick(t, 0.1f);     /* 5 s: the birth flow up (the keeper reading it), inside SAVE_MIN_GAP_S */
    /* the cut: RAM gone, the flash as it is */
    cold(0); memset(t, 0, sizeof *t); tank_init(t, 7172); gift_attach(t, &G[0]); progression_boot(t); notice_sync(t);
    int s2 = -1; for (int i = 0; i < t->n_fish; i++) if (t->fish[i].uid == uid) s2 = i;
    int ba = s2 >= 0 ? t->fish[s2].born_away : -1, aw = s2 >= 0 ? t->fish[s2].away : -1;
    t->clock += 600; tank_link_tick(t, NULL);                    /* (link_restore_at: home from bubbles) */
    int w2 = s2 >= 0 && progression_newborn() == s2;
    printf("  S7: home and welcomed (%d), +20 -> %d SD; a cut 5 s into its birth flow: the boot has it born_away %d away %d; restored: welcome owed again %s, %d SD\n",
           w1, (int)bal1, ba, aw, w2 ? "YES" : "no", (int)t->sd_balance);
    drain_welcomes(t);
    /* (round 2, fix N1: the homecoming is saved at once. What N1 named is a SECOND homecoming - the boot finding the fry
       next door again, from bubbles, its count and +20 again. A welcome the keeper had not finished stays owed across a
       cut, as for a birth here (audit-r1 case 7): that is w2, and not a finding) */
    if (ba || aw || t->sd_balance != bal1) { printf("  S7: FINDING: a cut inside the save gap after a fry's homecoming gives it a second homecoming (next door again %d/%d, %d SD, want %d)\n",
                                                   ba, aw, (int)t->sd_balance, (int)bal1); return 1; }
    printf("  S7: home at the boot, +20 once; its unfinished welcome owed once more (%s), as for a birth here\n", w2 ? "yes" : "no");
    return 0;
}
int selftest_seam(void) {
    const char *seed_s = getenv("SEAM_SEED"); rs = seed_s ? (uint32_t)strtoul(seed_s, NULL, 0) : 0x5ea11u;
    const char *ev_s = getenv("SEAM_EVENTS"); int events = ev_s ? atoi(ev_s) : 1400;
    const char *base = getenv("PT_SHELF_DIR"); if (!base || !*base) base = "/tmp";
    SB = getsectiondata(&_mh_execute_header, "__DATA", "__ptseamb", &NB);
    SD = getsectiondata(&_mh_execute_header, "__DATA", "__ptseamd", &ND);
    if (!SB || !NB) { printf("selftest-seam: FAIL: no swap sections (build with seam_sect.h)\n"); return 1; }
    if (!SD) ND = 0;
    PB = malloc(NB); PD = malloc(ND ? ND : 1); memcpy(PB, SB, NB); if (ND) memcpy(PD, SD, ND);
    for (int k = 0; k < 2; k++) { CB[k] = malloc(NB); CD[k] = malloc(ND ? ND : 1); memcpy(CB[k], PB, NB); if (ND) memcpy(CD[k], PD, ND);
        snprintf(dirs[k], sizeof dirs[k], "%s/seam-%c", base, 'a' + k);
        char cmd[700]; snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", dirs[k], dirs[k]); (void)system(cmd); }
    tank_entropy = ent;
    sim_clock_freeze(1);
    tank_link_swim_hook = swim_hook;             /* (efe7414's guard: more than 2 visitors or 7 swimming) */
    if (getenv("SEAM_NO_GATES")) g_gates = false;
    printf("selftest-seam: two progressions, %lu + %lu bytes swapped per tank\n", NB, ND);
    int found = 0;
    if (!getenv("SEAM_NO_CASES")) { found += case_stale_born_away(); found += case_bond_stall();
        found += case_gift_passport();
        found += case_bond_give();
        found += case_youngest_away();
        found += case_visitor_rule();
        found += case_dance_loop();
        found += case_fry_home_cut();
        printf("selftest-seam: the told cases: %d finding(s)\n", found); caps = 0; un = 0; memset(K, 0, sizeof K); memset(&st, 0, sizeof st); nk = 0; fails = 0; memset(kcount, 0, sizeof kcount);
        for (int k = 0; k < 2; k++) { char cmd[700]; snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", dirs[k], dirs[k]); (void)system(cmd); }
        cold(0); cur = -1; memcpy(CB[0], PB, NB); if (ND) memcpy(CD[0], PD, ND); }
    if (getenv("SEAM_CASES_ONLY") || !getenv("SEAM_EVENTS")) {   /* (make check: the told cases; the soak with SEAM_EVENTS, soak.sh) */
        printf("selftest-seam: %s (the told cases)\n", found ? "FAILED" : "OK");
        return found ? 1 : 0; }
    for (int k = 0; k < 2; k++) { boot_tank(k, 0x1000u + (uint32_t)k * 77, true); seed_books(k); }
    use(0); T[0].autofeed_off = true;            /* (A: AUTO FEED off - the starve clock runs, and must follow its fish) */
    {   /* the swap works: an age set in A is not B's */
        use(0); progression_set_age(&T[0], 0, 777); use(1); progression_set_age(&T[1], 0, 3);
        use(0); BOOK(fabsf(progression_age_s(&T[0], 0) - 777) < 1, "swap", "A's age %.0f", progression_age_s(&T[0], 0));
        use(1); BOOK(fabsf(progression_age_s(&T[1], 0) - 3) < 1, "swap", "B's age %.0f", progression_age_s(&T[1], 0)); }
    for (int k = 0; k < 2; k++) { use(k); for (int i = 0; i < T[k].n_fish; i++) { progression_set_age(&T[k], i, STAGE_ADULT_AGE + 60); T[k].fish[i].trust = 7; } }
    relink(); run_s(15);
    use(0); BOOK(T[0].door_open[LINK_RIGHT], "link", "the doors did not open");
    for (int e = 0; e < events; e++) {
        int k = (int)(rnd() & 1);
        float r = rndf();
        if      (r < 0.20f) ev_visit(k);
        else if (r < 0.27f) ev_home(k);
        else if (r < 0.30f) ev_deport(k);
        else if (r < 0.42f) ev_give(k, false);
        else if (r < 0.46f) ev_give(k, true);
        else if (r < 0.54f) ev_birth(k);
        else if (r < 0.66f) ev_cross(k);
        else if (r < 0.74f) ev_sell(k);
        else if (r < 0.78f) ev_buy(k);
        else if (r < 0.81f) ev_reboot(k);
        else if (r < 0.83f) ev_night();
        else if (r < 0.85f) ev_give_cut(k);
        else { use(k); tank_feed(&T[k], TANK_W * rndf(), 3); }
        if (getenv("SEAM_CARE")) for (int q = 0; q < 2; q++) {   /* (a keeper who has done every other gate: meals, trust, a hold) */
            use(q); T[q].player_feedings += 60; T[q].hold_approaches += 1; for (int i = 0; i < T[q].n_fish; i++) if (T[q].fish[i].trust < 9) T[q].fish[i].trust = 9; }
        run_s(2 + rndf() * 25);
        if (e % 200 == 0) { printf("  event %d t=%.0f s: A %d fish, B %d fish; %d book failures so far\n", e, tsec(), T[0].n_fish, T[1].n_fish, fails); fflush(stdout); }
    }
    /* the end: everyone home, every welcome said */
    for (int k = 0; k < 2; k++) { use(k); for (int j = 0; j < N_VISIT_MAX; j++) if (T[k].vis_used[j]) T[k].fish[N_FISH_MAX + j].homeward = true; }
    run_s(120);
    for (int i = 0; i < un; i++) if (U[i].owed && U[i].home >= 0) {
        int k = U[i].home; bool away = false; for (int q = 0; q < T[k].n_fish; q++) if (T[k].fish[q].uid == U[i].uid) away = T[k].fish[q].away;
        if (!away) BOOK(U[i].welcomes == 1, "welcome-count", "%08x (tank %c) welcomed %d times:%s", U[i].uid, 'A' + k, U[i].welcomes, U[i].log);
    }
    for (int i = 0; i < un; i++) if (U[i].gone_since > 0 && !U[i].sold) {   /* every fish that left a roster, not sold, is in one again */
        bool any = false; for (int k = 0; k < 2; k++) for (int q = 0; q < T[k].n_fish; q++) any |= T[k].fish[q].uid == U[i].uid;
        for (int k = 0; k < 2; k++) for (int j = 0; j < N_VISIT_MAX; j++) any |= (T[k].vis_used[j] || T[k].vis_pending[j]) && T[k].fish[N_FISH_MAX + j].uid == U[i].uid;
        BOOK(any, "fish-lost", "%08x left a roster at %.0f s and lives nowhere:%s", U[i].uid, U[i].gone_since, U[i].log);
    }
    printf("selftest-seam R2: frames %ld; two visitors %d frames, 7 swimming %d frames, max visitors %d, max swimming %d; ours home into a tank hosting 2: %d (%d fry born next door)\n"
           "  removals %d (removed_slot checked %d); gifts checked as left %d; GIVE with a cut %d; forced GIVE of a fry next door %d (%d never asked); mutated fry home %d; count notices %d (%d pictures checked)\n"
           "  R2-2: frames GROW met only by skipping a fish next door %ld, CHANGE %ld, every gate met only so %ld; stagings so %d; births of that staging %d\n"
           "  dances done %d: fry %d, no fry for want of a place %d (R2-4; %d of them one visitor in + one coming), no fry otherwise %d; longest run of fry-less dances %.0f s; longest a fish of ours stayed next door %.0f s; swim hook %d\n",
           st.frames, st.frames_two_vis, st.swim7_frames, st.max_vis, st.max_swim, st.home_into_two, st.fry_home_into_two,
           st.removals, st.shift_checked, st.gift_left_checked, st.gives_cut, st.give_forced_born_away, st.give_forced_refused, st.mut_fry_home, st.count_notices, st.count_pic_checked,
           st.frames_r22_grow, st.frames_r22_change, st.frames_r22_decisive, st.r22_staged_events, st.r22_births,
           st.dances_done, st.dances_fry, st.dances_nofry_two_vis, st.dances_nofry_coming, st.dances_nofry_other, st.longest_two_vis_loop, st.max_away, swim_hooks);
#define RS(x) printf("R2STAT %s %.0f\n", #x, (double)(st.x))
    RS(frames); RS(frames_two_vis); RS(swim7_frames); RS(max_vis); RS(max_swim); RS(home_into_two); RS(fry_home_into_two); RS(removals); RS(shift_checked);
    RS(gift_left_checked); RS(gives_cut); RS(give_forced_born_away); RS(give_forced_refused); RS(mut_fry_home); RS(count_notices); RS(count_pic_checked);
    RS(frames_r22_grow); RS(frames_r22_change); RS(frames_r22_decisive); RS(r22_staged_events); RS(r22_births); RS(dances_done); RS(dances_fry);
    RS(dances_nofry_two_vis); RS(dances_nofry_coming); RS(dances_nofry_other); RS(longest_two_vis_loop); RS(max_away); RS(visits); RS(gives); RS(gives_away);
    RS(gift_yes); RS(gift_no); RS(arrivals_gift); RS(births); RS(cross_try); RS(cross_home); RS(cross_kept); RS(born_next_door); RS(fry_home); RS(sales); RS(buys);
    RS(reboots); RS(cuts); RS(nights); RS(welcomes); RS(stale_mate); RS(stall_frames); RS(reboot_fry_away); RS(homes); RS(deports); RS(stall_mate_away); RS(notice_churn); RS(churn);
    printf("R2STAT link_hours %.2f\n", tsec() / 3600);
    printf("selftest-seam: %.1f h of link time, %d nights; visits %d, home %d, deports %d; GIVE %d (+%d next door): %d yes %d no, %d gifts arrived;\n"
           "  births %d, cross %d (home %d, kept %d), born next door %d (home %d), sales %d, shop fry %d, reboots %d (%d cuts), welcomes %d;\n"
           "  stale mates %d, frames a courting pair's mate was gone %d, born-away fish given away %d, churn frames %d, notice churn %d, reboots with a fry next door %d\n",
           tsec() / 3600, st.nights, st.visits, st.homes, st.deports, st.gives, st.gives_away, st.gift_yes, st.gift_no, st.arrivals_gift,
           st.births, st.cross_try, st.cross_home, st.cross_kept, st.born_next_door, st.fry_home, st.sales, st.buys, st.reboots, st.cuts, st.welcomes,
           st.stale_mate, st.stall_frames, st.given_away_born, st.churn, st.notice_churn, st.reboot_fry_away);
    for (int q = 0; q < nk; q++) printf("  [%s] x%d\n", kname[q], kcount[q]);
    printf("selftest-seam: %s (%d book failures, %d cap, %d swim, %d told-case findings)\n", fails || caps || swim_hooks || found ? "FAILED" : "PASS", fails, caps, swim_hooks, found);
    return fails || caps || swim_hooks || found ? 1 : 0;
}
#else
/* (Linux / ELF: no Mach-O section to swap - the two-progression harness runs on macOS only; CI skips it) */
int selftest_seam(void) { printf("selftest-seam: skipped (the section swap is Mach-O only)\n"); return 0; }
#endif
