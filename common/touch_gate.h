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
 * panel stamped as it opens spends taps for OPEN_WAIT_US (the shop's and the fish SELL's MSP_SELL_WAIT_S, 0.6 s), and
 * a hold on it counts only from a press that began after that.
 * A MASH (0.4 rc round three): a kid hammering one spot at ~5 Hz had taps 2 and 3 spent and tap 4, 600 ms after the
 * opening tap, act - NEW TANK bought, SWAP IN started. A spent tap restarts the wait: only a pause of OPEN_WAIT_US
 * lets the next tap act, however long the mash. */
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

#define OPEN_WAIT_US 600000
typedef struct { int64_t at; bool on; } open_wait_t;
static inline void open_wait_start(open_wait_t *w, int64_t now) { w->at = now; w->on = true; }
/* true = a tap (or a press beginning) at `now` is still the opening tap's double, or a mash after it: spent, and the
   wait starts again from it */
static inline bool open_wait_spent(open_wait_t *w, int64_t now) {
    if (!(w->on && now >= w->at && now - w->at < OPEN_WAIT_US)) return false;
    w->at = now; return true;
}

#endif
