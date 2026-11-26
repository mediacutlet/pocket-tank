/* xt_notice.c - THE EXPERIMENTAL TANK'S THANK-YOU (0.4.3): xt_notice.h */
#include "xt_notice.h"
#include "shelf.h"
#include <string.h>

typedef struct { uint32_t magic, n, id[XT_SEEN_MAX]; } xt_seen_t;   /* (only magic, n and n ids are written) */

static uint32_t s_id;
static bool  s_armed, s_up, s_jelly;   /* s_jelly: a jelly tank's - the jelly tanks' mark is written with it */
static float s_wait, s_since;

static bool seen_read(xt_seen_t *r) {
    size_t got = 0;
    memset(r, 0, sizeof *r);
    if (!shelf_port_frame_get(XT_SEEN_KEY, r, sizeof *r, &got) || got < 8 || r->magic != XT_SEEN_MAGIC
        || r->n > XT_SEEN_MAX || got != 8 + 4 * (size_t)r->n) { memset(r, 0, sizeof *r); return false; }
    return true;
}
bool xt_notice_seen(uint32_t shelf_id) {
    xt_seen_t r; seen_read(&r);
    for (uint32_t k = 0; k < r.n; k++) if (r.id[k] == shelf_id) return true;
    return false;
}
static void seen_add(uint32_t shelf_id, uint32_t mark) {   /* mark: XT_SEEN_JELLY too, or 0 (in one write) */
    xt_seen_t r; seen_read(&r);
    const uint32_t want[2] = { shelf_id, mark };
    bool changed = false;
    for (int w = 0; w < 2; w++) {
        if (!want[w]) continue;
        bool have = false;
        for (uint32_t k = 0; k < r.n && !have; k++) have = r.id[k] == want[w];
        if (have) continue;
        if (r.n == XT_SEEN_MAX) {                                  /* full: the oldest goes - never the jelly tanks' mark (it stands for all three) */
            uint32_t o = r.id[0] == XT_SEEN_JELLY ? 1 : 0;
            memmove(r.id + o, r.id + o + 1, sizeof r.id[0] * (XT_SEEN_MAX - 1 - o)); r.n--; }
        r.id[r.n++] = want[w]; changed = true;
    }
    if (!changed) return;
    r.magic = XT_SEEN_MAGIC;
    shelf_port_frame_set(XT_SEEN_KEY, &r, 8 + 4 * (size_t)r.n);   /* (refused: it says thank you again next time - nothing else rides on it) */
}

void xt_notice_open(uint32_t shelf_id, bool experimental) {
    s_id = shelf_id; s_up = false; s_wait = 0; s_since = 0; s_jelly = false;
    s_armed = experimental && !xt_notice_seen(shelf_id);
}
void xt_notice_open_jelly(uint32_t shelf_id) {
    xt_notice_open(shelf_id, true);
    if (!s_armed) return;
    bool said = xt_notice_seen(XT_SEEN_JELLY);
    for (int i = 0; i < shelf_count() && !said; i++)                    /* (a jelly tank on the shelf that said it before the mark) */
        said = shelf_get(i)->type == SHELF_TYPE_JELLY && xt_notice_seen(shelf_get(i)->id);
    if (said && !xt_notice_seen(XT_SEEN_JELLY)) seen_add(XT_SEEN_JELLY, 0);   /* (seen before the mark: the mark now, so a REMOVE of that tank changes nothing) */
    s_armed = !said; s_jelly = true;
}
void xt_notice_tick(float dt, bool blocked) {
    if (!s_armed && !s_up) return;
    if (s_up) {
        if (blocked) { s_up = false; return; }                 /* MY TANKS over it (BOOT twice): it was seen */
        s_since += dt; return;
    }
    if (blocked) { s_wait = 0; return; }                       /* (the whole wait again with the tank bare) */
    if ((s_wait += dt) < XT_NOTICE_WAIT_S) return;
    s_armed = false; s_up = true; s_since = 0;
    seen_add(s_id, s_jelly ? XT_SEEN_JELLY : 0);               /* seen the frame it comes up: nothing shows it twice */
}
bool xt_notice_up(void) { return s_up; }
bool xt_notice_tap(float held_s) {
    if (!s_up) return false;
    if (held_s >= s_since) return true;                        /* THE TOUCH RULE (G1): a press from before it came - spent, it stays */
    s_up = false;
    return true;
}
