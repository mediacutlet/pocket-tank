/* link_note.h - LINK's two cards on the big screens, and the radio's start behind them (0.4.3; the 11-24 radio audit).
 *
 * The big screens' radio is a chip next door (the C6) that can take seconds to answer, so LINK hands the start to the
 * port's radio task (link_port_begin) and shows a card until it knows:
 *   WAIT  STARTING THE LINK - the start is under way (render_link_wait)
 *   FAIL  THE LINK DID NOT START - it could not start (render_link_nostart)
 * OK or a tap outside closes either card: a press is classified when it lands and answered when it lifts (the page
 * gate), and a press begun under something else is forgotten (ln_cancel - the review's B). Closing the WAIT card does
 * not stop the start: when it answers with nobody wanting the radio (no page, no `link on`) it is taken down again.
 * Plain C on purpose: link_dev.c keeps only the wiring, and firmware/host_test/link_note_host.c runs every path on the
 * Mac (tools/test_radio_ram.py) - the audit found this logic had no test at all. */
#pragma once
#include <stdbool.h>

enum { LN_NONE, LN_WAIT, LN_FAIL, LN_UPD, LN_UPD_FAIL, LN_UPD_OUT };   /* the card on LINK's page (UPD: the one install's handover, below) */
enum { LN_BEGIN_WAIT = 1, LN_BEGIN_UP = 2, LN_BEGIN_FAIL = 3 };   /* = LP_BEGIN_WAIT / UP / FAIL (link_port_espnow.h) */
enum { LN_TAP_NONE, LN_TAP_OK, LN_TAP_OUTSIDE }; /* = LNOTE_TAP_* (render.h) */
enum { LN_MODE = 1, LN_DIR = 2, LN_STOP = 4 };   /* what the caller does: link mode's page up / the director's `link on` / the radio down */

typedef struct {
    int  card;                                   /* LN_NONE / LN_WAIT / LN_FAIL */
    int  down;                                   /* LN_TAP_* under the press, -1 = no press */
    bool pending;                                /* a start is with the radio task */
    bool want_dir;                               /* ... and the director's `link on` waits for it */
} link_note_t;

static inline void ln_init(link_note_t *n) { n->card = LN_NONE; n->down = -1; n->pending = false; n->want_dir = false; }

/* LINK tapped with the radio down: r is link_port_begin()'s answer. LN_MODE: up now, open link mode's page; else the
 * page opens as a card (WAIT or FAIL). A card left by an earlier `link on` + `link off` is not this page: it goes. */
static inline int ln_open(link_note_t *n, int r) {
    n->down = -1;
    if (r == LN_BEGIN_UP) { n->card = LN_NONE; return LN_MODE; }
    if (r == LN_BEGIN_WAIT) { n->card = LN_WAIT; n->pending = true; return 0; }
    n->card = LN_FAIL; return 0;
}
/* the director's `link on` with the radio down: LN_DIR = up now; WAIT = it finishes when the answer comes */
static inline int ln_dir(link_note_t *n, int r) {
    if (r == LN_BEGIN_UP) return LN_DIR;
    if (r == LN_BEGIN_WAIT) { n->pending = true; n->want_dir = true; }
    return 0;
}
/* the start answered (link_port_begin_poll() said UP or FAIL); page_up: LINK's page is still up */
static inline int ln_result(link_note_t *n, bool page_up, int r) {
    const bool page = page_up && n->card == LN_WAIT, dir = n->want_dir;
    n->pending = false; n->want_dir = false;
    if (r == LN_BEGIN_UP) {
        if (page) { n->card = LN_NONE; n->down = -1; }
        return (page ? LN_MODE : 0) | (dir ? LN_DIR : 0) | (!page && !dir ? LN_STOP : 0);   /* nobody wants it now: down again */
    }
    if (page) { n->card = LN_FAIL; n->down = -1; }
    return 0;
}
/* the radio died under link mode (link_port_alive() went false): LINK's page, if up, becomes the FAIL card */
static inline void ln_died(link_note_t *n, bool page_up) { n->want_dir = false; if (page_up) { n->card = LN_FAIL; n->down = -1; } }
/* a press or a lift on a card (down: the page gate's), hit = LN_TAP_* where it is: true = the card closes */
static inline bool ln_touch(link_note_t *n, bool down, int hit) {
    if (n->card == LN_NONE || n->card == LN_UPD) { n->down = -1; return false; }   /* (UPDATING has no button: nothing closes it) */
    if (down && n->down < 0) n->down = hit;
    if (!down && n->down >= 0) {
        const int r = n->down; n->down = -1;
        return r == LN_TAP_OK || r == LN_TAP_OUTSIDE;
    }
    return false;
}
static inline bool ln_card(const link_note_t *n, bool page_up) { return page_up && n->card != LN_NONE; }
static inline void ln_close(link_note_t *n) { if (n->card != LN_UPD) n->card = LN_NONE; n->down = -1; }   /* the card closed (a start under way goes on; UPDATING stays) */
/* THE HANDOVER (the one install, 0.4.3): the staged relay going into the C6. UPDATING takes the glass while it writes
   (no button); if the write stops, THE LINK UPDATE STOPPED (OK closes it) - with TAP LINK TO TRY AGAIN only while the
   stage has tries left (retry), else with GET HELP AT (LN_UPD_OUT: the audit's H1, the card promised a retry that did
   not exist). OUT is also LINK's answer when its start fails on a C6 the update could not reach. Success needs no
   card: the tank restarts. */
static inline void ln_updating(link_note_t *n) { n->card = LN_UPD; n->down = -1; }
static inline void ln_update_failed(link_note_t *n, bool retry) { n->card = retry ? LN_UPD_FAIL : LN_UPD_OUT; n->down = -1; }
static inline void ln_cancel(link_note_t *n, bool page_up) { n->down = -1; if (!page_up && n->card != LN_UPD) n->card = LN_NONE; }   /* the page not fed this poll */
static inline void ln_off(link_note_t *n) { n->want_dir = false; }                       /* `link off` / `link die` */
static inline void ln_sleep(link_note_t *n) { if (n->card != LN_UPD) n->card = LN_NONE; n->down = -1; n->want_dir = false; }   /* (UPDATING stays: the tank does not sleep through it - link_dev_busy) */
