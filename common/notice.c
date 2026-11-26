/* notice.c - the announcement queue. See notice.h. */
#include "notice.h"
#include "progression.h"                 /* (progression_newest: a count notice's fish) */
#include "sounds.h"
#include "setup.h"
#include "render.h"                      /* (render_bowl_card: the travel bowl's card, 0.4.2) */
#include <string.h>

static uint32_t s_ms[N_FISH_MAX], s_tms;
static stage_t  s_stage[N_FISH_MAX];
static bool     s_evo_ready[N_FISH_MAX];     /* (0.4.3) THE PARAGON: each fish's three were rolled / it has evolved, last frame */
static uint8_t  s_evo[N_FISH_MAX];
static int      s_n_fish = -1;               /* -1 = never synced */
static bool     s_tip;                       /* the tank's light_tip_seen, last frame */
static notice_t s_q[NOTICE_QUEUE];
static int      s_qn;
static notice_t s_cur;
static const tank_t *s_tank;                 /* the tank the per-fish memory is of (notice_sync / notice_tick) */
static const tank_t *s_removing;             /* (notice_fish_removed_of) the tank a fish just left: another's never moves this memory */
static bool     s_up;
static bool s_came_back;                 /* the notice up stepped aside under a page and came back (notice_came_back) */
static bool s_after_page;                /* (0.4.3 audit r2, R2-4) the notice up came the tick the page over the tank went (notice_after_page) */
static bool s_was_blocked;               /* last tick's `blocked` */
static float    s_gap;                       /* seconds until the next may come up */
static int      s_cue = -1;
static uint32_t s_owed;                     /* (0.4.2, PR-2 + D-5) the tank badges the boot's wake earned: queued by the first notice_tick */
static bool     s_model_told;                /* NOTICE_MODEL_DAMAGED: once per start (a sync does not forget it) */
static unsigned s_gen;                       /* (0.4.2, OWED CARDS) the tank generation the last sync was for (progression_owed_take) */
_Static_assert(MS_FISH_COUNT <= 16, "OWED CARDS: a fish's badges are saved in 16 bits (notice_owed_t.ms, save_t.owed_ms)");

/* never announced: they belong to the birth of a tank or a fish */
#define MS_SILENT   (MS_ARRIVED | MS_INSPECTED | MS_RETIRED_6 | MS_RETIRED_7 | MS_CROSSBRED | MS_BOUGHT | MS_CAME_CHANGED)   /* (MS_INSPECTED: no badge since 0.3.0) */
#define TMS_SILENT  (TMS_PAIR | TMS_RETIRED_4)
#define MS_STAGES   (MS_REACHED_JUV | MS_REACHED_ADULT | MS_REACHED_ELDER)

/* (0.4.2, the dark holds notices) the ones a dark tank still shows: the notice that explains the dark, and the three
   that can't wait for morning */
static bool dark_says(int kind) {
    return kind == NOTICE_LIGHTS_OUT || kind == NOTICE_LOW_BATTERY || kind == NOTICE_MODEL_DAMAGED || kind == NOTICE_WRONG_BOARD;
}
static void push(int kind, int fish, uint32_t bit) {
    if (s_qn >= NOTICE_QUEUE) {              /* a burst beyond the queue: the page still shows the rings */
        if (!dark_says(kind)) return;
        int k = s_qn - 1;                    /* (0.4.2) ... but a dark night's held badges never crowd out LOW BATTERY: the
                                                newest held one gives it its place */
        while (k >= 0 && dark_says(s_q[k].kind)) k--;
        if (k < 0) return;
        memmove(&s_q[k], &s_q[k + 1], sizeof(notice_t) * (size_t)(s_qn - 1 - k)); s_qn--;
    }
    s_q[s_qn++] = (notice_t){ kind, fish, bit, 0, 0 };
}

static void drop_evolve(int fish) {               /* a READY TO EVOLVE card of this fish, queued or up: gone (it has evolved) */
    int n = 0;
    for (int i = 0; i < s_qn; i++) if (!(s_q[i].kind == NOTICE_EVOLVE && s_q[i].fish == fish)) s_q[n++] = s_q[i];
    s_qn = n;
    if (s_up && s_cur.kind == NOTICE_EVOLVE && s_cur.fish == fish) { s_up = false; s_gap = NOTICE_GAP_S; }
}
void notice_sync(const tank_t *t) {
    s_tank = t;
    for (int i = 0; i < N_FISH_MAX; i++) s_ms[i] = t->fish[i].ms_bits;
    progression_wake_grew_take(t, s_stage);  /* (0.4.2, D-5) every fish's stage, but a fish the boot's slept span grew up keeps the
                                                stage it had before the night: the first notice_tick says its card, as awake */
    uint32_t owed = progression_wake_badges_take() & t->tank_ms_bits & ~TMS_SILENT;   /* (0.4.2, audit PR-2) what the boot's own
                                                wake earned: FIRST FULL NIGHT'S SLEEP is earned across a boot on a board (a night's
                                                power-off, a deep sleep, a DORMANT shelf), and adopted silently it was never said - its
                                                ring came with no chime and no card. Kept for the first notice_tick (D-5's reviews): it
                                                queues them AFTER the boot's own cards (UPDATED, MODEL DAMAGED, WRONG BOARD, pushed
                                                after this sync - the badge's save stamps the release, so UPDATED must be seen first)
                                                and AHEAD of the night's stage cards (a card's save would put the badge on the flash
                                                unseen). It waits like any badge (the boot show, a fry's welcome, a page). Taken once */
    notice_owed_t was; progression_owed_take(t, &was, &s_gen);   /* (0.4.2, OWED CARDS) what the save this tank was loaded from still
                                                owed (a dark morning's held cards, a card earned in the dark, slept before the light came
                                                on): left unadopted, as the wake's own, so the first notice_tick says them - a stage card
                                                once (the wake's own and the save's are one card: it says the stage the fish is at) */
    owed |= was.tms & t->tank_ms_bits & ~TMS_SILENT;
    for (int i = 0; i < t->n_fish && i < N_FISH_MAX; i++) {
        s_ms[i] &= ~(was.ms[i] & t->fish[i].ms_bits & ~MS_SILENT & ~MS_STAGES);
        if ((was.stage >> i & 1) && t->fish[i].stage > STAGE_FRY && s_stage[i] >= t->fish[i].stage) s_stage[i] = (stage_t)(t->fish[i].stage - 1);
    }
    s_tms = t->tank_ms_bits;
    for (int i = 0; i < N_FISH_MAX; i++) { s_evo_ready[i] = tank_evo_ready(&t->fish[i]); s_evo[i] = t->fish[i].evo; }
    s_n_fish = t->n_fish; s_tip = t->light_tip_seen;
    s_qn = 0; s_up = false; s_gap = 0; s_cue = -1;
    s_owed = owed;
}

/* OWED CARDS (0.4.2, notice.h): what a save writes as still to be said - the queue (held in the dark, behind a page, waiting
   its turn; one that stepped aside too: it comes back whole) and the sync's owed badges not yet queued. Not the one up: it
   is on the glass. Not what the tank earned since the last notice_tick (a save in progression_tick runs before this frame's
   diff): the next tick queues it and the next save has it - a cut inside that one frame loses its card, as before 0.4.2 */
bool notice_owed_pack(const tank_t *t, unsigned gen, notice_owed_t *o) {
    memset(o, 0, sizeof *o);
    if (s_n_fish < 0 || gen != s_gen) return false;
    for (int k = 0; k < s_qn; k++) {
        const notice_t *q = &s_q[k];
        if (q->kind == NOTICE_TANK_MILESTONE) o->tms |= q->bit;
        else if (q->kind == NOTICE_MILESTONE && q->fish >= 0 && q->fish < N_FISH_MAX) o->ms[q->fish] |= (uint16_t)q->bit;
        else if (q->kind == NOTICE_STAGE && q->fish >= 0 && q->fish < N_FISH_MAX) o->stage |= (notice_stage_bits_t)(1u << q->fish);
    }
    o->tms |= s_owed & ~TMS_SILENT;
    return true;
}

/* ROOM FOR A CARD THAT COMES BY ITSELF (0.4.2, the travel bowl's IS WAITING): nothing else has the glass. ONE test, here
   in the common code, so the boards and the sim cannot drift (the boards' `blocked` left out the rename wheel and the
   placement pages, the reset prompt and a tool in hand - a card opened under those is neither drawn nor tappable, and
   its one offer of the start was spent). notice_tick asks it itself, whatever `blocked` the platform passes */
bool notice_card_room(const tank_t *t) {
    return !setup_active() && !setup_birth_due(t)              /* the first run, a birth flow, the rename wheel, a placement page, MORE's pages */
        && render_reset_page() == RESET_PG_NONE                /* the reset prompt */
        && t->tool == TOOL_HAND && !render_breed_card_up()     /* a tool in hand owns the glass (the BREEDING tool, the sponge, the scissors) */
        && !t->ui_cover                                        /* a page over the tank */
        && !render_card_over_tank(t);                          /* the GIVE modal, a gift offered to us */
}
/* (0.4.3 audit r2, R2-L11) a READY TO EVOLVE card whose fish is next door: held, never shown - its tap opens nothing there.
   It waits in the queue (the others go on past it) and comes when the fish is home */
static bool held_away(const tank_t *t, const notice_t *n) {
    return n->kind == NOTICE_EVOLVE && n->fish >= 0 && n->fish < t->n_fish && t->fish[n->fish].away;
}
void notice_tick(const tank_t *t, float dt, bool blocked) {
    s_tank = t;
    const bool was_blocked = s_was_blocked;  /* (R2-4) a page was over the tank last tick: what comes up now came as it went */
    s_was_blocked = blocked;
    if (s_n_fish < 0) { notice_sync(t); return; }
    { stage_t drop[N_FISH_MAX]; (void)progression_wake_grew_take(t, drop); }   /* (D-5: the same for the night's stages) */
    (void)progression_wake_badges_take();    /* (0.4.2) a wake this tick's diff already sees (no sync after it: the sim's in-place
                                                sleep) - a later sync must not owe it again */
    for (uint32_t b = 1; b && s_owed; b <<= 1)  /* the sync's owed badges: behind the boot's cards, ahead of this tick's stage cards */
        if (s_owed & b) { push(NOTICE_TANK_MILESTONE, (b & (TMS_TRIO | TMS_QUARTET | TMS_QUINTET)) ? progression_newest(t) : -1, b); s_owed &= ~b; }
    if (t->n_fish < s_n_fish) {
        if ((t->tank_ms_bits & s_tms) != s_tms) { notice_sync(t); return; }   /* a reset: nothing to say */
        for (int i = 0; i < N_FISH_MAX; i++) { s_ms[i] = t->fish[i].ms_bits; s_stage[i] = t->fish[i].stage; s_evo_ready[i] = tank_evo_ready(&t->fish[i]); s_evo[i] = t->fish[i].evo; }
        /* a sale (0.3.5): the fish after it moved down a slot - adopt them as
           they are; the tank's badges (the first sale's) still speak */
    }
    for (int i = s_n_fish; i < t->n_fish; i++) {            /* an arrival: adopt the fry quietly */
        s_ms[i] = t->fish[i].ms_bits; s_stage[i] = t->fish[i].stage;
        s_evo_ready[i] = tank_evo_ready(&t->fish[i]); s_evo[i] = t->fish[i].evo;   /* (a paragon that arrives says nothing) */
    }
    s_n_fish = t->n_fish;
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i];
        if (f->stage != s_stage[i]) {
            if (f->stage > s_stage[i]) push(NOTICE_STAGE, i, 0);
            s_stage[i] = f->stage;
        }
        uint32_t fresh = f->ms_bits & ~s_ms[i] & ~MS_SILENT & ~MS_STAGES;   /* the stage said it */
        for (uint32_t b = 1; b && fresh; b <<= 1)
            if (fresh & b) { push(NOTICE_MILESTONE, i, b); fresh &= ~b; }
        s_ms[i] = f->ms_bits;
        bool ready = tank_evo_ready(f);                          /* THE PARAGON (0.4.3) */
        if (ready && !s_evo_ready[i] && !f->away) push(NOTICE_EVOLVE, i, 0);
        if (f->evo && !s_evo[i]) { drop_evolve(i); push(NOTICE_PARAGON, i, 0); }   /* (its READY TO EVOLVE, still queued or up: said - SPS-4) */
        s_evo_ready[i] = ready; s_evo[i] = f->evo;
    }
    uint32_t fresh = t->tank_ms_bits & ~s_tms & ~TMS_SILENT;
    for (uint32_t b = 1; b && fresh; b <<= 1)
        if (fresh & b) {                     /* a count (A TRIO..A QUINTET) carries the fish that made it, as it is queued: whoever
                                                arrives before it comes up is not its picture (0.4.1 audit round 1, R-seam-keeper-sees S2) */
            push(NOTICE_TANK_MILESTONE, (b & (TMS_TRIO | TMS_QUARTET | TMS_QUINTET)) ? progression_newest(t) : -1, b); fresh &= ~b; }
    s_tms = t->tank_ms_bits;
    if (t->light_tip_seen && !s_tip) push(NOTICE_LIGHTS_OUT, -1, 0);   /* the first double-tap that turned the light off */
    s_tip = t->light_tip_seen;

    /* (0.4.2) the travel bowl's IS WAITING card holds the notices under it, as the GIVE modal does - but never LOW BATTERY
       or the others that can't wait (dark_says): one of those queued puts the card away (as NOT NOW would), and the card
       is owed again once the queue is empty */
    if (!s_up && render_give_modal_step(t) == RENDER_GM_BOWL)
        for (int i = 0; i < s_qn; i++) if (dark_says(s_q[i].kind)) { render_bowl_card_aside(); progression_bowl_reoffer(); break; }
    /* (0.4.2, the dark holds notices - Strato: "tank light off should be treated largely as a notification suppressor
       too") while the light is off nothing comes up and nothing chimes: all of the above still ran, so everything is
       queued in order and comes up, with its chime, once the light is on. dark_says' four are shown all the same. */
    const bool dark = tank_light_off(t);
    if (s_up) {
        const bool to_dark = !blocked && dark && !dark_says(s_cur.kind);
        if (to_dark && s_cur.age >= NOTICE_SEEN_S) { s_up = false; s_gap = 0; return; }   /* (0.4.2) read before the light went off: done */
        if (blocked || to_dark || held_away(t, &s_cur)) {   /* (R2-L11: its fish went next door - it steps aside as under a page) */
                                             /* something opened over it (2026-09-29: the birth flow over a fry's population
                                                badge), or the light went off (0.4.2): it steps aside and comes back after,
                                                quiet - its cue has played. From under a page it comes back whole; from the dark
                                                with the time it was shown kept (a tank on AUTO at 5 s goes dark before a card's
                                                6 s: back whole at every wake, the head never left and the queue never moved) */
            if (s_qn >= NOTICE_QUEUE) {      /* (0.4.2) a full queue: the card that was up is never the one lost - the newest
                                                held ordinary one gives it its place, as for push */
                int k = s_qn - 1;
                while (k >= 0 && dark_says(s_q[k].kind)) k--;
                if (k < 0) k = s_qn - 1;
                memmove(&s_q[k], &s_q[k + 1], sizeof(notice_t) * (size_t)(s_qn - 1 - k)); s_qn--;
            }
            memmove(&s_q[1], &s_q[0], sizeof(notice_t) * (size_t)s_qn);
            const bool away = !blocked && !to_dark;  /* (R2-L11: aside for its fish next door - age -2: back quiet, its time kept, no tap brought it) */
            s_q[0] = s_cur; s_q[0].age = away ? -2 : -1; s_q[0].since = to_dark || away ? s_cur.age : 0; s_qn++;
            s_up = false; s_gap = 0;
            return;
        }
        s_cur.age += dt; s_cur.since += dt;
        if (s_cur.age >= NOTICE_UP_S && s_cur.kind != NOTICE_MODEL_DAMAGED && s_cur.kind != NOTICE_WRONG_BOARD) { s_up = false; s_gap = NOTICE_GAP_S; }   /* (these two wait for a tap) */
        return;
    }
    if (s_gap > 0) { s_gap -= dt; return; }
    /* THE TRAVEL BOWL (0.4.2): a fish waiting in it is said once a start - <NAME> IS WAITING, LET IN | NOT NOW - after
       every card that was queued, never over a page (blocked) and never in the dark: it waits for the light as the
       others do. NOT NOW: not again by itself until the next start (the keeper page's row brings it back) */
    bool none = true;                        /* (R2-L11: a READY card held for a fish next door is no card waiting - the bowl's offer goes on) */
    for (int i = 0; i < s_qn && none; i++) none = held_away(t, &s_q[i]);
    if (!blocked && none && !tank_light_off(t) && notice_card_room(t) && progression_bowl_offer_due()) { progression_bowl_offered(); render_bowl_card(t); return; }
    if (blocked || !s_qn) return;
    int k = 0;                               /* the head - in the dark the first one the dark still shows, the others kept in order; never
                                                a READY card of a fish next door (R2-L11) */
    while (k < s_qn && ((dark && !dark_says(s_q[k].kind)) || held_away(t, &s_q[k]))) k++;
    if (k == s_qn) return;
    bool again = s_q[k].age < 0;             /* stepped aside earlier: no second chime */
    s_came_back = s_q[k].age == -1;          /* (from under a page or the dark; not back from a fish's visit, age -2) */
    s_after_page = was_blocked;              /* (R2-4) held under a page, or earned by the tap that closed it: the port gives it the
                                                same window as one that came back (the double's second half is no tap on it) */
    s_cur = s_q[k]; s_cur.since = 0;
    s_cur.age = again ? s_q[k].since : 0;    /* (back from the dark: the time it was shown; from under a page: 0) */
    memmove(&s_q[k], &s_q[k + 1], sizeof(notice_t) * (size_t)(s_qn - 1 - k)); s_qn--;
    s_up = true;
    s_cue = again || s_cur.kind == NOTICE_LIGHTS_OUT ? -1 : s_cur.kind == NOTICE_STAGE || s_cur.kind == NOTICE_EVOLVE || s_cur.kind == NOTICE_PARAGON ? SND_STAGE_UP : s_cur.kind == NOTICE_LOW_BATTERY || s_cur.kind == NOTICE_MODEL_DAMAGED || s_cur.kind == NOTICE_WRONG_BOARD ? SND_LOW_BATTERY : SND_MILESTONE;
}

/* a fish left and the slots shifted (progression: a sale, a gift): a notice about a fish follows it; about the one that
   left, it goes (the 11-02 sweep: queued, it would have shown another fish's badge) */
void notice_fish_removed_of(const tank_t *t, int idx) { s_removing = t; notice_fish_removed(idx); s_removing = NULL; }
void notice_fish_removed(int idx) {
    /* (0.4.3 audit r1, SLP-1 / SLP-6) what this fish's slot and those after it last said moves down with them, and the count
       goes one down: an arrival in the same call (a SWAP: out, then in) is adopted quietly by notice_tick's arrival loop, and
       a count drop no longer re-reads every fish (that dropped a card another fish earned the same frame) */
    if (s_n_fish > 0 && idx >= 0 && idx < s_n_fish && (!s_removing || s_removing == s_tank)) {
        for (int i = idx; i < N_FISH_MAX - 1; i++) { s_ms[i] = s_ms[i + 1]; s_stage[i] = s_stage[i + 1]; s_evo_ready[i] = s_evo_ready[i + 1]; s_evo[i] = s_evo[i + 1]; }
        s_n_fish--;
    }
    int n = 0;
    for (int i = 0; i < s_qn; i++) {
        notice_t q = s_q[i];
        if (q.fish >= 0 && q.fish == idx && q.kind == NOTICE_TANK_MILESTONE) q.fish = -1;   /* (a count stays: its fish gone, the newest) */
        else if (q.fish >= 0 && q.fish == idx) continue;
        if (q.fish > idx) q.fish--;
        s_q[n++] = q;
    }
    s_qn = n;
    if (s_up && s_cur.fish >= 0) {
        if (s_cur.fish == idx && s_cur.kind == NOTICE_TANK_MILESTONE) s_cur.fish = -1;
        else if (s_cur.fish == idx) { s_up = false; s_gap = NOTICE_GAP_S; }
        else if (s_cur.fish > idx) s_cur.fish--;
    }
}
const notice_t *notice_current(void) { return s_up ? &s_cur : NULL; }
bool notice_came_back(void) { return s_up && s_came_back; }
bool notice_after_page(void) { return s_up && s_after_page; }
bool notice_dismiss(void) { return notice_dismiss_held(0); }
bool notice_dismiss_held(float held_s) {
    if (!s_up) return false;
    if (s_cur.kind == NOTICE_LIGHTS_OUT) { s_up = false; s_gap = NOTICE_GAP_S; return false; }   /* its tap goes on to the tank: the double-tap it asks for */
    if (held_s >= s_cur.since) return true;   /* THE TOUCH RULE (touch_gate.h): a press begun before it came is spent, the notice stays (G1;
                                               it comes by itself: no window - the 2 s hold before 11-06 is gone) */
    s_up = false; s_gap = NOTICE_GAP_S;
    return true;
}
void notice_low_battery(void) {
    for (int i = 0; i < s_qn; i++) if (s_q[i].kind == NOTICE_LOW_BATTERY) return;
    if (s_up && s_cur.kind == NOTICE_LOW_BATTERY) return;
    push(NOTICE_LOW_BATTERY, -1, 0);
}
void notice_updated(void) { push(NOTICE_UPDATED, -1, 0); }
void notice_model_damaged(void) { if (s_model_told) return; s_model_told = true; push(NOTICE_MODEL_DAMAGED, -1, 0); }
void notice_wrong_board(void) { push(NOTICE_WRONG_BOARD, -1, 0); }
int notice_take_cue(void) { int c = s_cue; s_cue = -1; return c; }
int notice_pending(void) { return s_qn; }
