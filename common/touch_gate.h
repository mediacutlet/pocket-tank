/* touch_gate.h — two small rules of the boards' touch ports, kept here so the
 * sim's selftests can drive them (the touch ports themselves are not in the
 * sim; --selftest-under-card).
 *
 * THE FINGER UNDER A CARD (0.4, the 11-02 evening sweep): the incoming gift
 * card draws over any page, but MY TANKS's holds (REMOVE's HOLD TO SELL, the
 * RENAME wheel, MOVE's 0.7 s lift) read the finger itself - holding the
 * card's NO THANKS for 2 s sold a shelved tank. A press that met a card (or a
 * prompt) is hidden from the page under it until it lifts: the card coming
 * mid-press reads as a lift, the card going mid-press is no new press.
 *
 * THE WATCH'S REST (the FT3168, da89534): a read the chip fails times out at
 * 20 ms and the chip is then left alone FT_REST_US. 20 + 50 ms is more than
 * the 60 ms of silence that lifts a press, so a drag, the RENAME wheel or a
 * hold broke into a tap and a new press. The first rest inside a silence is
 * bridged: the silence is counted from its end. A second failed read in the
 * same silence is not (a chip gone quiet for good still lifts, ~130 ms).
 *
 * A PANEL'S FIRST 600 MS (0.4 rc, kids press firmly and fast): the touch layer hands over BOTH taps of a double tap (the
 * light's double tap needs them), so a tap that opens a panel has its second half land on whatever stands there. In
 * MY TANKS an empty place opens THE LIBRARY, whose NEW TANK row covers the places under it (300 SD gone on a double
 * tap), and a tank's place opens its card, whose SWAP IN and MORE cover places 4-5; tap, tap, hold sold a tank. A
 * panel stamped as it opens spends the double's second half: a press within OPEN_WAIT_US (THE TOUCH RULE's 0.2 s,
 * below; 0.6 s and restarted by every tap inside it before 11-06), or one that began before it opened. Never restarted.
 *
 * 0.4.1 (the touch items release-0.4.md deferred): A FIRM PRESS, A PAGE FED THE FINGER ITSELF, THE RELEASE THAT IS
 * SPENT and THE TOUCH RULE (11-06: it replaced the audit's P8 restart), below; the firmware's touch port drives them, and
 * --selftest-touch-port runs that port on the host. */
#ifndef POCKET_TANK_TOUCH_GATE_H
#define POCKET_TANK_TOUCH_GATE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct { bool hidden; } finger_gate_t;
/* the finger the page under a card sees this poll: `covered` = a card or a prompt is over it */
static inline bool finger_gate(finger_gate_t *g, bool touched, bool covered) {
    if (!touched) g->hidden = false;
    else if (covered) g->hidden = true;
    return touched && !g->hidden;
}

typedef struct { int64_t seen_us, rest_until, bridge_to; bool down; } press_rest_t;
static inline bool press_rest_may_read(const press_rest_t *p, int64_t now) { return now >= p->rest_until; }
/* a read failed (the bus timed out), `now` taken after it: the chip rests `rest_us`; a press bridges the first rest of a silence */
static inline void press_rest_failed(press_rest_t *p, int64_t now, int64_t rest_us) {
    p->rest_until = now + rest_us;
    if (p->down && !p->bridge_to) p->bridge_to = p->rest_until;
}
/* the poll, `now` taken before its read: `answered` = the chip gave a report, `finger` = with a finger in it; true while down */
static inline bool press_rest_step(press_rest_t *p, int64_t now, bool answered, bool finger, int64_t lift_us) {
    if (finger) { p->seen_us = now; p->bridge_to = 0; p->down = true; }
    else if (p->down) {
        int64_t from = p->bridge_to > p->seen_us ? p->bridge_to : p->seen_us;
        if (answered || now - from > lift_us) { p->down = false; p->bridge_to = 0; }   /* "no finger", said plainly, is a lift */
    }
    return p->down;
}

/* THE TOUCH RULE (Strato 11-06: "its just quick double taps we should be guarding against without disrupting folks that are
 * comfortable with the UI and move quickly"; "the countdown should not restart with every tap"; FIX-give.md):
 *  - G1: a press that BEGAN before the page / step / card / button appeared is no tap on it (the finger already down);
 *  - W: where a tap brought it up, its stray second half lands within TAP_W of it appearing - a FIXED window from the
 *    stamp, never moved by a tap or a redraw (a burst of taps never delays the tap that counts);
 *  - nothing else: a deliberate tap acts at once. Protection for what is hard to undo is a hold, Strato's call each.
 * (Rounds 1-2 had a 0.6 s wait restarted by every tap inside it, and 2 s holds on the gift card, GIVE and DEPORT: a keeper
 * tapping once a second never got through - "the delay is WAY too long".) */
#define TAP_W_US 200000
#define TAP_W_S  0.2f
#define OPEN_WAIT_US TAP_W_US
typedef struct { int64_t at; bool on; } open_wait_t;
static inline void open_wait_start(open_wait_t *w, int64_t now) { w->at = now; w->on = true; }
/* true = no tap: the press began before the stamp (G1) or within `len` of it (W). The stamp is never moved by a tap. One in
   the future (a clock gone back: a reset, a new tank) spends nothing */
static inline bool hold_spent_us_at(int64_t *at, int64_t press, int64_t now, int64_t len) {
    if (*at > now) return false;
    return press < *at || press - *at < len;
}
static inline bool hold_spent_us(int64_t *at, int64_t now, int64_t len) { return hold_spent_us_at(at, now, now, len); }
/* true = a tap whose press began at `press` is the opening tap's double (W) or a press from before it opened (G1) */
static inline bool open_wait_spent_at(open_wait_t *w, int64_t press, int64_t now) { return w->on && hold_spent_us_at(&w->at, press, now, OPEN_WAIT_US); }
static inline bool open_wait_spent(open_wait_t *w, int64_t now) { return open_wait_spent_at(w, now, now); }

/* A FIRM PRESS (0.4.1, kids press firmly and slowly): a press that lifts within TAP_STILL_PX of where it landed is a tap
 * on whatever BUTTON it landed on, however long it stayed - the settings page always took it so, the others wanted it
 * under TAP_QUICK_US and a slow press did nothing. Only the water keeps the quick rule: a finger resting on it is the
 * HOLD (the fish come round it, a piece's page at 0.7 s), so its lift is no tap. On the bare tank a press that lands on
 * a button (a card's, the pill, the tool chip) is no hold: it calls no fish and acts at its lift, at any length. */
#define TAP_QUICK_US 350000
#define TAP_STILL_PX 24
static inline bool tap_still(float dx, float dy) { return dx * dx + dy * dy < TAP_STILL_PX * TAP_STILL_PX; }
static inline bool tap_quick(int64_t held_us) { return held_us < TAP_QUICK_US; }

/* A PAGE FED THE FINGER ITSELF (settings, UPDATES, link mode: their own press-to-lift rules) opened or brought back by a
 * tap (0.4.1, the watch: UPDATES' CLOSE double tapped - the second tap landed on settings' SCREEN under it and turned the
 * picture): a press beginning within OPEN_WAIT_US of the opening (or before it) is hidden from the page until it lifts. Nothing
 * restarts the window (THE TOUCH RULE). `pressed` = the finger landed this poll. True = the page sees it. */
typedef struct { open_wait_t w; bool hidden; } page_gate_t;
static inline void page_gate_open(page_gate_t *g, int64_t now) { open_wait_start(&g->w, now); }
static inline bool page_gate(page_gate_t *g, bool touched, bool pressed, int64_t now) {
    if (!touched) { g->hidden = false; return false; }
    if (pressed) g->hidden = open_wait_spent(&g->w, now);
    return !g->hidden;
}

/* THE RELEASE THAT IS SPENT (s_back): a page's own button (settings' CLOSE, UPDATES' CLOSE, link mode's DONE, the
 * DRIFTED notice's DISCONNECT) acted on this press; its lift must not land on the page that comes back under it. Taken by
 * the NEXT LIFT, whatever kind of lift it is (0.4.1: a slow CLOSE, held past TAP_QUICK_US, left it set, and the keeper's
 * next tap was eaten instead). */
typedef struct { bool on; } spent_lift_t;
static inline void spent_lift_set(spent_lift_t *s) { s->on = true; }
static inline bool spent_lift_take(spent_lift_t *s) { bool on = s->on; s->on = false; return on; }

/* hold_spent: THE TOUCH RULE (above) on any clock - `*at` the stamp (the thing appeared), `len` its window (W, or 0 for G1
 * alone); true = spent. The stamp is never moved by a tap (the audit's P8 restart, 0.4.1 round 1, is gone since 11-06). A
 * stamp in the future (a clock gone back) spends nothing.
 * READ AT THE PRESS (0.4.1 audit round 1, TCH-1/TCH-9): a tap is measured by when its press BEGAN, not when it lifted -
 * a firm second press (tap, press again 100 ms on, hold 500 ms) lifted past the window and sold the fish. The _at forms
 * take the press; the bare forms are a tap with no length (press = lift). */
static inline bool hold_spent_at(double *at, double press, double now, double len) {   /* (hold_spent_us_at above: the board's microseconds) */
    if (*at > now) return false;               /* (a clock gone back) */
    return press < *at || press - *at < len;   /* (THE TOUCH RULE above: no restart) */
}
static inline bool hold_spent(double *at, double now, double len) { return hold_spent_at(at, now, now, len); }

#endif
