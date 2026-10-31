/* notice.c - the announcement queue. See notice.h. */
#include "notice.h"
#include "sounds.h"
#include <string.h>

static uint32_t s_ms[N_FISH_MAX], s_tms;
static stage_t  s_stage[N_FISH_MAX];
static int      s_n_fish = -1;               /* -1 = never synced */
static bool     s_tip;                       /* the tank's light_tip_seen, last frame */
static notice_t s_q[NOTICE_QUEUE];
static int      s_qn;
static notice_t s_cur;
static bool     s_up;
static float    s_gap;                       /* seconds until the next may come up */
static int      s_cue = -1;
static bool     s_model_told;                /* NOTICE_MODEL_DAMAGED: once per start (a sync does not forget it) */

/* never announced: they belong to the birth of a tank or a fish */
#define MS_SILENT   (MS_ARRIVED | MS_INSPECTED | MS_RETIRED_6 | MS_RETIRED_7)   /* (MS_INSPECTED: no badge since 0.3.0) */
#define TMS_SILENT  (TMS_PAIR)
#define MS_STAGES   (MS_REACHED_JUV | MS_REACHED_ADULT | MS_REACHED_ELDER)

static void push(int kind, int fish, uint32_t bit) {
    if (s_qn >= NOTICE_QUEUE) return;        /* a burst beyond the queue: the page still shows the rings */
    s_q[s_qn++] = (notice_t){ kind, fish, bit, 0 };
}

void notice_sync(const tank_t *t) {
    for (int i = 0; i < N_FISH_MAX; i++) { s_ms[i] = t->fish[i].ms_bits; s_stage[i] = t->fish[i].stage; }
    s_tms = t->tank_ms_bits; s_n_fish = t->n_fish; s_tip = t->light_tip_seen;
    s_qn = 0; s_up = false; s_gap = 0; s_cue = -1;
}

void notice_tick(const tank_t *t, float dt, bool blocked) {
    if (s_n_fish < 0) { notice_sync(t); return; }
    if (t->n_fish < s_n_fish) {
        if ((t->tank_ms_bits & s_tms) != s_tms) { notice_sync(t); return; }   /* a reset: nothing to say */
        for (int i = 0; i < N_FISH_MAX; i++) { s_ms[i] = t->fish[i].ms_bits; s_stage[i] = t->fish[i].stage; }
        /* a sale (0.3.5): the fish after it moved down a slot - adopt them as
           they are; the tank's badges (the first sale's) still speak */
    }
    for (int i = s_n_fish; i < t->n_fish; i++) {            /* an arrival: adopt the fry quietly */
        s_ms[i] = t->fish[i].ms_bits; s_stage[i] = t->fish[i].stage;
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
    }
    uint32_t fresh = t->tank_ms_bits & ~s_tms & ~TMS_SILENT;
    for (uint32_t b = 1; b && fresh; b <<= 1)
        if (fresh & b) { push(NOTICE_TANK_MILESTONE, -1, b); fresh &= ~b; }
    s_tms = t->tank_ms_bits;
    if (t->light_tip_seen && !s_tip) push(NOTICE_LIGHTS_OUT, -1, 0);   /* the first double-tap that turned the light off */
    s_tip = t->light_tip_seen;

    if (s_up) {
        if (blocked) {                       /* something opened over it (2026-09-29: the birth flow over
                                                a fry's population badge): it steps aside and comes back
                                                after, whole and quiet - its cue has played */
            if (s_qn < NOTICE_QUEUE) {
                memmove(&s_q[1], &s_q[0], sizeof(notice_t) * (size_t)s_qn);
                s_q[0] = s_cur; s_q[0].age = -1; s_qn++;
            }
            s_up = false; s_gap = 0;
            return;
        }
        s_cur.age += dt;
        if (s_cur.age >= NOTICE_UP_S && s_cur.kind != NOTICE_MODEL_DAMAGED && s_cur.kind != NOTICE_WRONG_BOARD) { s_up = false; s_gap = NOTICE_GAP_S; }   /* (these two wait for a tap) */
        return;
    }
    if (s_gap > 0) { s_gap -= dt; return; }
    if (blocked || !s_qn) return;
    bool again = s_q[0].age < 0;             /* stepped aside earlier: no second chime */
    s_cur = s_q[0]; s_cur.age = 0;
    memmove(&s_q[0], &s_q[1], sizeof(notice_t) * (size_t)(s_qn - 1)); s_qn--;
    s_up = true;
    s_cue = again || s_cur.kind == NOTICE_LIGHTS_OUT ? -1 : s_cur.kind == NOTICE_STAGE ? SND_STAGE_UP : s_cur.kind == NOTICE_LOW_BATTERY || s_cur.kind == NOTICE_MODEL_DAMAGED || s_cur.kind == NOTICE_WRONG_BOARD ? SND_LOW_BATTERY : SND_MILESTONE;
}

const notice_t *notice_current(void) { return s_up ? &s_cur : NULL; }
bool notice_dismiss(void) {
    if (!s_up) return false;
    if (s_cur.kind == NOTICE_LIGHTS_OUT) { s_up = false; s_gap = NOTICE_GAP_S; return false; }   /* its tap goes on to the tank: the double-tap it asks for */
    if (s_cur.age < NOTICE_HOLD_S) return true;   /* the hold: a tap this soon is spent, the notice stays (notice.h) */
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
