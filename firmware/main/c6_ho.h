/* c6_ho.h - THE HANDOVER as one pure state machine: every decision of the big screens' automatic C6 radio update
 * (0.4.3, the one install; rebuilt 11-24 after four rounds of case-by-case fixes, two of which added a serious bug).
 *
 *     int c6_ho_step(c6_ho_state_t *s, const c6_ho_ev_t *e)    ->  C6A_* (| C6A_SAVE)
 *
 * The firmware only gathers facts, calls this, saves the record when the answer carries C6A_SAVE (before it does
 * anything else) and performs the action: radio_hosted.c (the boot, the greeting, the check's task, the write) and
 * link_dev.c (the tank task: the minute's check, LINK, the cards, the restart). No I/O here, no IDF: the exhaustive
 * checker on the Mac (firmware/host_test/c6_ho_check.c) runs this very function against a model of the C6, and
 * tools/test_radio_ram.py forbids the decisions anywhere else.
 *
 * THE STATE. `r`, the record, is in the P4's NVS ("radio"/"c6rec", one per staged image: its SHA-256's first 8
 * bytes); `r.rep` is the radio report's (radio_hosted_report) and is never read here. The rest is this boot's RAM.
 *
 * THE RULES, per stage:
 *  (a) at most C6_TRIES_AUTO automatic writes (the second only after the first did not finish), C6_TRIES_ALL in all.
 *      A write is counted when it begins (phase WRITE, saved before the C6 is told), a refused beginning too.
 *  (b) never a write over the C6's way back: once the stage went in (`wrote`, saved before the C6's end, where it
 *      switches), a C6 that answers our hosted but not the relay may be the new image on probation, its other slot
 *      the only firmware known to work. It is written again only by a LINK tap, and only after the C6 was found so in
 *      boots that started it at least C6_DEAF_BOOTS times (see there; one count per P4 boot - the 11-24 review: LINK taps
 *      in one boot counted once each): by then its guard has turned an unconfirmed image back. After a write reaches
 *      the C6's end, the tank restarts whatever the end said.
 *  (c) no automatic write after the C6 was found back on older firmware once the stage went in (a turn-back).
 *  (d) TAP LINK TO TRY AGAIN only while a LINK tap would check with a write left (c6_ho_retry); with none, THE LINK
 *      UPDATE STOPPED / GET HELP AT; a C6 that answers nothing: THE LINK DID NOT START.
 *  (e) one automatic check per boot; none after C6_QUIET_BOOTS quiet ones (no answer, a refused start, a deaf relay,
 *      an image on probation whose radio did not send), none after a check that crashed the tank (phase CHECK left
 *      by a crash: no restart loop); the hold (M4) once per write; the restart only after a write reached the end.
 *  (f) a C6 that answers nothing at all (no SDIO, or no request answered - the #259 signature) is never written and
 *      no try is spent on it. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "now_relay.h"                         /* nr_state_t, nr_slots_ok: what the relay answered */

/* the handover's states, this boot (RAM): radio_hosted.h says what each means */
enum { HO_IDLE, HO_CHECKING, HO_READY, HO_WRITING, HO_WRITTEN, HO_FAIL, HO_NOT_NEEDED, HO_NO_IMAGE, HO_NO_ANSWER, HO_NOT_NOW, HO_REFUSED,
       HO_ESCAPE };   /* (ESCAPE: the escape chain - the tank saves and restarts itself, both chips: see C6A_ESCAPE) */

#define C6_TRIES_AUTO     2
#define C6_TRIES_ALL      4
#define C6_QUIET_BOOTS    3
/* (b) TWO LAYERS. 1. The relay itself (firmware-c6/main/guard.c __wrap_esp_ota_begin) refuses an update's begin while its
   image is unconfirmed and the slot the update would erase holds a valid image - its way back - answering
   C6_ERR_UNCONFIRMED (nothing erased: the try is given back). That covers every write while our relay runs.
   2. The P4's own count, for whatever it cannot ask: guard.c turns an unconfirmed image back at its GUARD_BOOTS'th start
   of any kind. The P4 counts only a start it drove itself and saw land: its bring-up resets the C6 (hosted 3.1.0
   CP_RESET_STRATEGY_ALWAYS, eh_host_bus_sdio.c ensure_slave_bus_ready) and hosted then answers - and the guard holds
   hosted until that start is saved in NVS (or, NVS refusing, turns the image back at once). One per P4 boot, at a deaf
   check. NOT a power-on: an EN reset (the RESET button, the USB-UART's auto-reset) reads POWERON too, and the C6 may
   not have restarted (review 5's H1); nor a start the bring-up's reset cut before its save landed. The C6's own
   restarts only add starts the guard counts. Once the P4 has counted GUARD_BOOTS, an unconfirmed image is gone. */
#define C6_GUARD_S        180
#define C6_GUARD_BOOTS    6                    /* = guard.c GUARD_BOOTS */
#define C6_DEAF_BOOTS     C6_GUARD_BOOTS
#define C6_REFUSALS       3                    /* refusals that give their try back (rule a, d) */
#define C6_INTERRUPTED    3                    /* checks in a row cut by a power-on: the automatic ones stop (a supply that drops at the radio's start) */
#define C6_SAME_PLACE_KB  64                   /* two failed writes this close inside the image, same answer: it never goes in */
#define C6_AUTO_AFTER_S   60                   /* the automatic check: not in the boot's first minute */
#define C6_ERR_INVALID_SIZE 0x104              /* ESP_ERR_INVALID_SIZE: hosted 2.x/3.x's answer for an image bigger than its slot */
#define C6_ERR_UNCONFIRMED  0x10D              /* ESP_ERR_NOT_ALLOWED: our relay on probation refuses a write over its way back */
#define C6_REC_VER        7                    /* (7: the escape chain's esc, esc_n, esc_b - 0.4.3, Strato 11-25 "no card option is best build it") */
#define C6_ESC_CHAINS     2                    /* escape chains a stage may start (a LINK tap each); after them: THE LINK DID NOT START, GET HELP */
#define C6_ESC_MISS       2                    /* boots of one chain in a row that counted no deaf start (a power cut, a crash, a bus
                                                  that would not come up): then GET HELP. Every other boot of it counts one (up to
                                                  C6_DEAF_BOOTS), and misses may fall between them: a chain is at most about
                                                  C6_DEAF_BOOTS + C6_ESC_MISS * (C6_DEAF_BOOTS + 1) boots, of which at most
                                                  C6_DEAF_BOOTS are restarts it asks for itself (review 9's L5) */

enum { C6PH_IDLE, C6PH_CHECK, C6PH_WRITE };   /* what the handover was doing when the record was last saved */
enum { C6W_NONE, C6W_INTERRUPTED, C6W_CHECK_CUT, C6W_NO_SDIO, C6W_DEAF, C6W_BEGIN_REFUSED, C6W_WRITE_FAILED, C6W_TOO_BIG,
       C6W_TURNED_BACK, C6W_LAYOUT, C6W_BUSY, C6W_WRITTEN, C6W_IN_C6, C6W_OUT_OF_TRIES, C6W_NOT_AGAIN, C6W_BUS_STUCK,
       C6W_PROBATION, C6W_OLD_RECORD, C6W_SAME_PLACE, C6W_NO_RELAY_ANSWER, C6W_N };

typedef struct __attribute__((packed)) {      /* the radio report's: written here, never read here */
    uint8_t  last, why;                        /* the last check's HO_* and C6W_* */
    uint8_t  hv_said, hv[3];                   /* the C6's hosted version, if it said it */
    uint8_t  heard, rv;                        /* a relay answered (rv: its version) */
    uint8_t  slots, run_sub, next_sub;         /* (relay v3) its slot report, now_relay.h NR_SLOT_* */
    uint16_t next_kb, checks;
} c6_rep_t;
typedef struct __attribute__((packed)) {
    uint8_t  sha[8];                           /* the stage this record is for */
    uint8_t  ver;                              /* C6_REC_VER */
    uint8_t  phase;                            /* C6PH_* */
    uint8_t  tries, auto_n;                    /* writes begun: in all, and by the tank itself */
    uint8_t  quiet;                            /* quiet checks (rule e), up to C6_QUIET_BOOTS */
    uint8_t  deaf;                             /* the C6's starts seen deaf since the stage went in (rule b), up to C6_DEAF_BOOTS */
    uint8_t  intr;                             /* checks in a row cut by a power-on (rule e), up to C6_INTERRUPTED */
    uint8_t  refused;                          /* writes our relay on probation refused (C6_ERR_UNCONFIRMED): the first C6_REFUSALS give
                                                  their try back, later ones spend it (review 6's M1: no endless TAP LINK) */
    uint8_t  backs;                            /* turn-backs seen (at most one per write) */
    uint8_t  never;                            /* it can never go into this C6 (too big; a layout with no way back; refused too often),
                                                  or it is in it, stuck on probation (no confirmation, no way back): no more checks, LINK as
                                                  always - a LINK start that fails (no answer, or a radio that never sends) says GET HELP */
    uint8_t  wrote;                            /* a write reached the C6's end: the next hearing says if the C6 kept it */
    uint8_t  seen;                             /* heard running in the C6, its guard not yet confirmed (a later older one: a turn-back) */
    uint8_t  cut;                              /* a check crashed the tank: none by itself again */
    uint8_t  held;                             /* (M4) the C6 held in reset once since this write */
    uint8_t  greet;                            /* greet the C6 at the next boot */
    uint8_t  ok;                               /* in the C6, confirmed: nothing more to do */
    uint16_t fail_kb;                          /* where the last failed write stopped inside the image, KB + 1 (0: none) */
    uint8_t  fail_code;                        /* ... and the C6's answer there */
    uint8_t  esc;                              /* THE ESCAPE CHAIN under way: a LINK tap found the stage in the C6 and the C6 deaf to our
                                                  relay (its guard still counting starts) - the tank restarts itself, both chips, until the
                                                  guard decides; every boot of it runs the keeper's check at once (cleared when it resolves) */
    uint8_t  esc_n;                            /* chains started for this stage, up to C6_ESC_CHAINS */
    uint8_t  esc_b;                            /* boots of the chain under way in a row that counted no deaf start, up to C6_ESC_MISS */
    c6_rep_t rep;
} c6_rec_t;
_Static_assert(sizeof(c6_rec_t) == 45, "c6_ho.h: the record's size (NVS blob)");

typedef struct {
    c6_rec_t r;                                /* NVS (saved on C6A_SAVE) */
    uint32_t size;                             /* the stage: its image's bytes (0: no stage of our relay) */
    uint8_t  rv;                               /* ... its relay version */
    uint8_t  ho;                               /* HO_* */
    uint8_t  power_on;                         /* this boot began at a power-on (else a software restart or a crash) */
    uint8_t  nosave;                           /* the record would not read: at the cap, never saved over */
    uint8_t  keeper;                           /* the check under way is a LINK tap's (or one joined it) */
    uint8_t  wait;                             /* LINK's STARTING card waits on it */
    uint8_t  upd;                              /* UPDATING YOUR DEVICE is up */
    uint8_t  go;                               /* READY, and the glass is bare: write */
    uint8_t  act;                              /* the C6's hosted switches at activate (2.6 and up), not at the end */
    uint8_t  deaf_done;                        /* this boot's deaf starts counted (rule b) */
    uint8_t  auto_done, greeting, restart;
    uint8_t  probe_old;                        /* the probe under way is of an older relay of ours on probation (review 7's L3) */
    uint8_t  elf8[8];                          /* the stage's build: its app_elf_sha256's first 8 bytes (now_relay.h elf8) */
    uint8_t  chain;                            /* this boot is a boot of the escape chain (1: its check not begun yet; 2: begun) */
    uint8_t  esc_out;                          /* the chain ran out of boots: THE LINK DID NOT START (GET HELP) at the first tick */
    uint8_t  esc_abort;                        /* ESC_ABORT's GET HELP is owed even if its record save fails (review 11's M1) */
} c6_ho_state_t;

enum { C6E_BOOT, C6E_TICK, C6E_LINK, C6E_LINK_FAILED, C6E_STAGE, C6E_HEARD, C6E_GREET_HEARD, C6E_PROBED, C6E_READY_POLL, C6E_SENT, C6E_RESULT,
       C6E_SAVE_FAILED, C6E_ESC_ABORT };       /* ESC_ABORT (review 10's M1): the tank's own save failed at the chain's restart - nothing restarts */                      /* SAVE_FAILED: the last answer's record would not save - the caller acts on this one's
                                                  instead (radio_hosted.c ho_step); nothing is decided from RAM alone after it */
enum { C6R_POWERON, C6R_SOFTWARE, C6R_CRASH };
enum { C6X_OK, C6X_BEGIN, C6X_READ, C6X_CHUNK, C6X_END, C6X_ACTIVATE };   /* where a write stopped */
typedef struct {
    uint8_t  type;                             /* C6E_* */
    uint8_t  reset, bench;                     /* BOOT: C6R_*; the console's `radio c6 update` left its greet mark */
    uint8_t  ok;                               /* STAGE: verified (2: no room to run the check - not the stage's fault). TICK: the glass is bare (no page but LINK's STARTING card, no flow, no prompt) */
    uint8_t  idle;                             /* TICK: nothing on the glass at all and no link (the automatic check may start) */
    uint8_t  flow;                             /* TICK: a flow on the glass the restart still waits for (its 30 s not up) */
    uint8_t  card;                             /* TICK: LINK's STARTING card is still up */
    uint8_t  late;                             /* READY_POLL: the glass stayed busy two minutes */
    uint8_t  up, silent, said, rpc;            /* HEARD: reached over SDIO; hosted said NO_ANSWER; it said its version; Wi-Fi's start answered */
    uint8_t  hv[3];                            /* HEARD: that version */
    uint8_t  at;                               /* RESULT: C6X_* */
    int32_t  code;                             /* RESULT: the C6's answer */
    uint32_t off;                              /* RESULT: where in the image (C6X_CHUNK) */
    uint32_t now_s;                            /* TICK: seconds since the boot */
    const nr_state_t *st;                      /* HEARD, GREET_HEARD, PROBED: our relay's answer (NULL: none) */
    const void *raw;                           /* BOOT: the record in NVS, n bytes (n 0: none); unreadable: there but it would not read */
    uint16_t n;
    uint8_t  unreadable;
    uint8_t  sha[8], stage_rv;                 /* BOOT: the stage (size 0: none) */
    uint8_t  elf8[8];                          /* BOOT: ... its build (its app description's app_elf_sha256) */
    uint32_t size;
} c6_ho_ev_t;

enum { C6A_NONE, C6A_GREET, C6A_CHECK, C6A_CHECK_WAIT, C6A_BRING_UP, C6A_PROBE, C6A_READY, C6A_WAIT, C6A_WRITE, C6A_END,
       C6A_END_ACTIVATE, C6A_HOLD, C6A_RESTART, C6A_DONE, C6A_CARD_WAIT, C6A_CARD_UPDATING, C6A_CARD_NOSTART, C6A_CARD_RETRY,
       C6A_CARD_OUT, C6A_LINK, C6A_CHECK_ESC, C6A_CARD_ESC, C6A_ESCAPE };
/* (the escape chain) CHECK_ESC: a boot of the chain - its card (STARTING LINK / YOUR DEVICE MAY RESTART SEVERAL TIMES / THIS IS
   NORMAL) at once, and the keeper's check. CARD_ESC: that card (a LINK tap during the chain: no second chain). ESCAPE: the chain
   card, the tank saved, held ~3 s to read, then both chips restart (radio_hosted_restart_clean: a C6 start the P4 drove) */
#define C6A_SAVE 0x80
#define C6A_OF(a) ((a) & 0x7f)

/* a hosted 2.6+ C6 switches only when told (activate); an older one (the factory 1.x) at the end by itself */
static inline bool c6_ho_activates(const uint8_t hv[3]) { return hv[0] > 2 || (hv[0] == 2 && hv[1] > 5); }
static inline int c6_tries_left(const c6_rec_t *r) { return r->never || r->tries >= C6_TRIES_ALL ? 0 : C6_TRIES_ALL - r->tries; }
/* would a LINK tap check? (the stage went in: a visit; else while a write is left) */
static inline bool c6_ho_gate(const c6_ho_state_t *s, bool keeper) {
    const c6_rec_t *r = &s->r;
    if (!s->size || s->nosave || r->ok || r->never) return false;   /* (never: LINK as always - review 7's M1: a relay that links, links) */
    if (r->wrote || r->seen) return keeper || (!r->cut && r->quiet < C6_QUIET_BOOTS);
    if (!c6_tries_left(r)) return false;
    return keeper || (!r->cut && r->quiet < C6_QUIET_BOOTS && !r->backs && r->auto_n < C6_TRIES_AUTO);
}
/* (d) TAP LINK TO TRY AGAIN is true: a LINK tap would check, with a write left */
static inline bool c6_ho_retry(const c6_ho_state_t *s) { return c6_ho_gate(s, true) && c6_tries_left(&s->r) > 0; }

/* the C6 runs the stage: this very build (its elf8), or a later relay version (review 7's L3: not the version alone - an earlier
   build of the same version is updated) */
static inline bool c6_is_stage(const c6_ho_state_t *s, const nr_state_t *st) { return st->version > s->rv || (st->version == s->rv && !memcmp(st->elf8, s->elf8, 8)); }
static inline void c6_q(uint8_t *v, int cap) { if (*v < cap) (*v)++; }
static inline int c6_done(c6_ho_state_t *s, int ho) { s->ho = (uint8_t)ho; s->r.rep.last = (uint8_t)ho; s->r.phase = C6PH_IDLE; s->r.intr = 0; s->r.esc = s->r.esc_b = 0; return C6A_DONE | C6A_SAVE; }   /* (a check's end: the chain resolved) */
#define C6_CHUNK 1400                           /* the write's chunk (radio_hosted.c c6_send) */
static inline void c6_confirmed(c6_rec_t *r) { r->ok = 1; r->wrote = r->seen = 0; r->quiet = 0; r->rep.why = C6W_IN_C6; }

#if defined(__GNUC__) && !defined(__clang__)
#define C6_HO_SMALL __attribute__((noinline, optimize("Os")))   /* (the 10.1's radio image is a few KB under its slot: no hot path here) */
#else
#define C6_HO_SMALL __attribute__((noinline))
#endif
static inline C6_HO_SMALL int c6_ho_step(c6_ho_state_t *s, const c6_ho_ev_t *e) {
    c6_rec_t *r = &s->r;
    const uint8_t abort_owed = s->esc_abort; s->esc_abort = 0;   /* (only the very next step, ESC_ABORT's own SAVE_FAILED, may owe its card) */
    switch (e->type) {
    case C6E_BOOT: {                           /* the record from NVS; what the last boot left behind */
        memset(s, 0, sizeof *s);
        s->power_on = e->reset == C6R_POWERON; s->size = e->size; s->rv = e->stage_rv;
        memcpy(r->sha, e->sha, 8); r->ver = C6_REC_VER; memcpy(s->elf8, e->elf8, 8);
        if (!e->size) return e->bench ? C6A_GREET : C6A_NONE;   /* (no stage of our relay: the console's greeting only) */
        const c6_rec_t *old = (const c6_rec_t *)e->raw;
        if (e->unreadable || (e->n >= 8 && !memcmp(e->raw, e->sha, 8) && (e->n != sizeof *r || old->ver != C6_REC_VER))) {
            /* this stage's record, unreadable or in another layout: at the cap (a layout change never hands tries back);
               unreadable: never saved over either */
            r->tries = C6_TRIES_ALL; r->auto_n = C6_TRIES_AUTO; r->rep.why = C6W_OLD_RECORD; s->nosave = e->unreadable;
            return C6A_NONE;
        }
        if (e->n == sizeof *r && !memcmp(e->raw, e->sha, 8)) memcpy(r, e->raw, sizeof *r);   /* (another stage's, or none: afresh) */
        const bool left = r->phase != C6PH_IDLE;
        if (r->phase == C6PH_CHECK) {
            if (!s->power_on) { r->rep.why = C6W_CHECK_CUT; r->cut = 1; }
            else { r->rep.why = C6W_INTERRUPTED; c6_q(&r->intr, C6_INTERRUPTED); if (r->intr >= C6_INTERRUPTED) r->cut = 1; }   /* (e) */
        }
        else if (left) r->rep.why = C6W_INTERRUPTED;
        r->phase = C6PH_IDLE;
        if (r->esc) {                          /* (the escape chain) this boot is one of it, however it came: counted, and bounded */
            if (r->esc_b >= C6_ESC_MISS) s->esc_out = 1;   /* (out of boots: GET HELP - the intent kept until it is shown, never a silent end) */
            else { r->esc_b++; s->chain = 1; }
            return C6A_SAVE;
        }
        if (!r->greet && !e->bench) return left ? C6A_SAVE : C6A_NONE;
        r->greet = 0; s->greeting = 1; r->phase = C6PH_CHECK;   /* (a greeting that crashes the tank is a cut check: no loop) */
        return C6A_GREET | C6A_SAVE; }

    case C6E_TICK:                             /* the tank task, every poll */
        if (s->esc_out) { s->esc_out = 0; r->esc = r->esc_b = 0; return C6A_CARD_NOSTART | C6A_SAVE; }
        if (s->chain == 1 && s->ho == HO_IDLE && !s->nosave) {   /* a boot of the chain: its card and the keeper's check at once (no first minute;
                                                  a record that would not save ends it: nothing restarts on a count it could not keep) */
        chain_go:
            s->chain = 2; s->auto_done = 1; s->ho = HO_CHECKING; s->keeper = 1; s->wait = 1; r->phase = C6PH_CHECK;
            return C6A_CHECK_ESC | C6A_SAVE;
        }
        if (s->ho == HO_ESCAPE && !s->restart && !s->nosave) { s->restart = 1; return C6A_ESCAPE; }   /* (the chain's next boot: the glue saves the tank first) */
        if (!s->auto_done && s->ho == HO_IDLE && e->idle && e->now_s >= C6_AUTO_AFTER_S && !s->greeting) {
            s->auto_done = 1;
            if (!s->size || r->ok) return C6A_NONE;
            if (!c6_ho_gate(s, false)) {
                s->ho = HO_NOT_NOW; r->rep.last = HO_NOT_NOW;
                if (!c6_tries_left(r) && !r->wrote && !r->seen && !r->never && !s->nosave) r->rep.why = C6W_OUT_OF_TRIES;
                return C6A_NONE;
            }
            s->ho = HO_CHECKING; s->keeper = 0; r->phase = C6PH_CHECK;   /* (marked now: a crash before the check's task runs is a cut check) */
            return C6A_CHECK | C6A_SAVE;
        }
        if (s->ho == HO_READY && !s->go && e->ok) { s->go = 1; s->wait = 0; s->upd = 1; return C6A_CARD_UPDATING; }
        if (s->ho == HO_WRITTEN && !s->restart && !e->flow) { s->restart = 1; return C6A_RESTART; }
        if (s->upd && s->ho == HO_FAIL) { s->upd = 0; return c6_ho_retry(s) ? C6A_CARD_RETRY : C6A_CARD_OUT; }
        if (s->wait && s->ho >= HO_FAIL && s->ho != HO_ESCAPE) {   /* a LINK tap's check is answered */
            s->wait = 0;
            if (!e->card) return C6A_NONE;     /* (the keeper closed the card meanwhile) */
            if (s->ho == HO_NO_ANSWER) return C6A_CARD_NOSTART;
            if (s->ho == HO_FAIL) return c6_ho_retry(s) ? C6A_CARD_RETRY : C6A_CARD_OUT;
            return C6A_LINK;                   /* nothing to write (or REFUSED: the relay it has links): LINK as always */
        }
        return C6A_NONE;

    case C6E_LINK:                             /* LINK tapped, the radio down */
        if (s->esc_out) { s->esc_out = 0; r->esc = r->esc_b = 0; return C6A_CARD_NOSTART | C6A_SAVE; }   /* (the chain ran out of boots: GET HELP first) */
        if (s->ho == HO_ESCAPE) return C6A_CARD_ESC;   /* (the chain restarts now: no second one) */
        if (s->chain == 1 && s->ho == HO_IDLE && !s->nosave) goto chain_go;   /* (a tap on a boot of the chain before its first tick: that check) */
        if (s->ho == HO_CHECKING || s->ho == HO_READY) { s->keeper = 1; s->wait = 1; return s->chain ? C6A_CARD_ESC : C6A_CARD_WAIT; }   /* (join it) */
        if (s->ho == HO_WRITING || s->ho == HO_WRITTEN) return C6A_CARD_UPDATING;
        if (s->ho == HO_NO_IMAGE || !c6_ho_gate(s, true)) return C6A_LINK;
        s->ho = HO_CHECKING; s->keeper = 1; s->wait = 1; return C6A_CHECK_WAIT;

    case C6E_LINK_FAILED:                      /* LINK's start failed: on a C6 the update could not reach, the card says so */
        if (r->ok && s->size && !s->nosave) { r->ok = 0; r->seen = 1; return C6A_SAVE; }   /* (review 7's L4) the stage was confirmed, yet LINK cannot
                                                  start: the C6 may have lost its mark (an NVS erase) and turned back - look again */
        return s->size && !r->ok && (s->ho == HO_NO_IMAGE || s->nosave || r->never || (!c6_tries_left(r) && !r->wrote && !r->seen)) ? C6A_CARD_OUT : C6A_NONE;   /* (a torn
                                                  stage can never go in either: GET HELP - c6_ho_check.c's (d)) */

    case C6E_STAGE:                            /* the check's task: the stage verified? may it still go on? */
        if (e->ok == 2) { s->ho = HO_NOT_NOW; r->phase = C6PH_IDLE; r->esc = r->esc_b = 0; return s->nosave ? C6A_DONE : C6A_DONE | C6A_SAVE; }   /* (no try, no quiet: next boot) */
        if (!e->ok) {                          /* a torn stage: nothing to hand over (quiet: not verified every boot for ever) */
            s->ho = HO_NO_IMAGE; r->phase = C6PH_IDLE; r->esc = r->esc_b = 0; if (!s->keeper) c6_q(&r->quiet, C6_QUIET_BOOTS);
            return s->nosave ? C6A_DONE : C6A_DONE | C6A_SAVE;
        }
        if (!c6_ho_gate(s, s->keeper)) return s->nosave ? (s->ho = HO_NOT_NOW, C6A_DONE) : c6_done(s, r->ok ? HO_NOT_NEEDED : HO_NOT_NOW);
        s->probe_old = 0; r->phase = C6PH_CHECK; if (r->rep.checks < 0xffff) r->rep.checks++;
        return C6A_BRING_UP | C6A_SAVE;

    case C6E_HEARD: {                          /* what the check heard */
        const nr_state_t *st = e->st;
        const bool in = r->wrote || r->seen;
        if (e->up) { r->rep.hv_said = e->said; memcpy(r->rep.hv, e->hv, 3); r->cut = 0; r->rep.heard = st != NULL; r->rep.rv = st ? st->version : 0;
                     s->act = c6_ho_activates(e->hv); }
        if (st && !st->guard_s) r->deaf = 0;   /* (a confirmed relay answers: no starts on probation to count) */
        if (st) { r->rep.slots = st->slots; r->rep.run_sub = st->run_sub; r->rep.next_sub = st->next_sub; r->rep.next_kb = st->next_kb; }
        if (!e->up || (!st && !e->said && !e->rpc)) {   /* (f) nothing over SDIO, or nothing answered: never written to */
            r->rep.why = e->up ? C6W_DEAF : C6W_NO_SDIO; c6_q(&r->quiet, C6_QUIET_BOOTS);
            if (in && !r->held && !e->up && e->silent && !s->power_on) { r->held = 1; r->rep.why = C6W_BUS_STUCK; c6_done(s, HO_NO_ANSWER); return C6A_HOLD | C6A_SAVE; }   /* (M4) */
            return c6_done(s, HO_NO_ANSWER);
        }
        /* (b) a relay that does not answer on our hosted (2.x/3.x - never the factory 1.x/0.x), or after the stage went in on
           one that says no version but answers Wi-Fi's start: maybe the new image, deaf; and (g) never written blind - its
           slot report unheard, a one-slot C6 would lose its running image (c6_ho_check.c found it: every PING lost) */
        const bool deaf = !st && (e->said ? e->hv[0] >= 2 : in && e->rpc);
        if (deaf && in && !s->deaf_done) { s->deaf_done = 1; c6_q(&r->deaf, C6_DEAF_BOOTS); r->esc_b = 0; }   /* (b) one start: this boot's bring-up reset it, and
                                                  hosted answering proves its guard counted that start (it holds hosted until saved) */
        if (deaf && in && s->keeper && r->deaf < C6_DEAF_BOOTS && !s->nosave && (r->esc || r->esc_n < C6_ESC_CHAINS)) {   /* THE ESCAPE CHAIN
                                                  (Strato 11-25): the stage in, the C6 deaf to it, its guard still counting - the tank restarts
                                                  itself, both chips, each a start the P4 drives and counts here, until the guard decides */
            if (!r->esc) { r->esc = 1; c6_q(&r->esc_n, C6_ESC_CHAINS); r->esc_b = 0; }
            r->rep.why = C6W_NO_RELAY_ANSWER; s->ho = HO_ESCAPE; r->rep.last = HO_ESCAPE; r->phase = C6PH_IDLE; r->intr = 0;
            return C6A_DONE | C6A_SAVE;
        }
        if (deaf && !(s->keeper && r->deaf >= C6_DEAF_BOOTS)) { r->rep.why = C6W_NO_RELAY_ANSWER; c6_q(&r->quiet, C6_QUIET_BOOTS); return c6_done(s, HO_NO_ANSWER); }
        if (st && c6_is_stage(s, st)) {        /* at or above the stage: it runs it (this very build, or a later version) */
            r->wrote = 0;
            if (st->guard_s == 0) { c6_confirmed(r); return c6_done(s, HO_NOT_NEEDED); }
            r->seen = 1; r->rep.why = C6W_PROBATION;
            if (!s->deaf_done) { s->deaf_done = 1; c6_q(&r->deaf, C6_DEAF_BOOTS); }   /* (a start of it counted, as a deaf one) */
            if (r->deaf >= C6_DEAF_BOOTS) { r->never = 1; return c6_done(s, HO_NOT_NOW); }   /* (review 6's M1) still on probation after the guard's
                                                  last start: it can never be confirmed or turned back here (the C6's NVS, no way back). LINK as
                                                  always: its start says GET HELP if the radio does not send (review 8's M2, link_port_hosted.c) */
            if (st->version >= 2) return C6A_PROBE | C6A_SAVE;   /* (phase CHECK stays: a probe that crashes the tank is a cut check) */
            c6_q(&r->quiet, C6_QUIET_BOOTS); return c6_done(s, HO_NOT_NOW);
        }
        if (in) {                              /* below the stage after it went in: turned back (or the deaf escape) */
            r->wrote = r->seen = 0;
            c6_q(&r->backs, 7);                /* (the escape too: its guard must have turned it back - no automatic write after either) */
            r->rep.why = deaf ? C6W_NO_RELAY_ANSWER : C6W_TURNED_BACK;
        }
        const int fit = st ? nr_slots_ok(st, s->size) : -1;   /* (-1: factory firmware or a relay before v3 cannot say) */
        if (fit == 0) { r->never = 1; r->rep.why = (uint32_t)st->next_kb * 1024u < s->size ? C6W_TOO_BIG : C6W_LAYOUT; return c6_done(s, HO_REFUSED); }
        if (!c6_tries_left(r)) { if (r->rep.why != C6W_TURNED_BACK) r->rep.why = C6W_OUT_OF_TRIES; return c6_done(s, HO_NOT_NOW); }
        if (!s->keeper && (r->backs || r->auto_n >= C6_TRIES_AUTO)) { if (r->rep.why != C6W_TURNED_BACK) r->rep.why = C6W_NOT_AGAIN; return c6_done(s, HO_NOT_NOW); }
        if (st && st->guard_s && st->version >= 2 && !s->probe_old) { s->probe_old = 1; return C6A_PROBE | C6A_SAVE; }   /* (review 7's L3) an older
                                                  relay of ours on probation: probed first - confirmed, it takes the write; else its begin refuses */
        s->ho = HO_READY; s->go = 0; r->rep.last = HO_READY; r->esc = r->esc_b = 0;   /* (phase CHECK stays: READY waits on the radio task) */
        return C6A_READY | C6A_SAVE; }

    case C6E_GREET_HEARD: {                    /* the greeting after a write heard st (NULL: no relay) */
        const nr_state_t *st = e->st;
        if (st && c6_is_stage(s, st) && s->size) {
            r->rep.heard = 1; r->rep.rv = st->version; r->rep.slots = st->slots; r->rep.run_sub = st->run_sub; r->rep.next_sub = st->next_sub; r->rep.next_kb = st->next_kb;
            if (st->guard_s == 0) c6_confirmed(r);
            else if (st->version >= 2) return C6A_PROBE;   /* (ESP-NOW up once, its probe out: the guard confirms on a real send) */
            else { r->seen = 1; r->rep.why = C6W_PROBATION; }
        } else if (!st && s->size && !r->held && !e->up && e->silent && !s->power_on) {   /* (M4) after a write, a software restart, no SDIO */
            r->held = 1; r->rep.why = C6W_BUS_STUCK; s->greeting = 0; r->phase = C6PH_IDLE; return C6A_HOLD | C6A_SAVE;
        }
        s->greeting = 0; r->phase = C6PH_IDLE; r->intr = 0;
        return s->size ? C6A_DONE | C6A_SAVE : C6A_DONE; }

    case C6E_PROBED:                           /* the probe (ESP-NOW up, its probe out, down): confirmed? */
        if (s->probe_old) {                    /* (an older relay: confirmed now, the write goes ahead; else not now, quiet) */
            if (e->st && !e->st->guard_s) { s->ho = HO_READY; s->go = 0; r->rep.last = HO_READY; r->esc = r->esc_b = 0; return C6A_READY | C6A_SAVE; }
            c6_q(&r->quiet, C6_QUIET_BOOTS); return c6_done(s, HO_NOT_NOW);
        }
        if (e->st && e->st->guard_s == 0) c6_confirmed(r);
        else { r->seen = 1; r->rep.why = C6W_PROBATION; if (!s->greeting) c6_q(&r->quiet, C6_QUIET_BOOTS); }
        if (s->greeting) { s->greeting = 0; r->phase = C6PH_IDLE; r->intr = 0; return C6A_DONE | C6A_SAVE; }
        return c6_done(s, r->ok ? HO_NOT_NEEDED : HO_NOT_NOW);

    case C6E_READY_POLL:                       /* the radio task waits for the tank task's go */
        if (s->go) {                           /* (a) counted now, before the C6 is told */
            r->tries++; if (!s->keeper) r->auto_n++;
            r->phase = C6PH_WRITE; r->held = 0; r->intr = 0; s->ho = HO_WRITING;
            return C6A_WRITE | C6A_SAVE;
        }
        if (!e->late) return C6A_WAIT;
        r->rep.why = C6W_BUSY; if (!s->keeper) c6_q(&r->quiet, C6_QUIET_BOOTS);   /* (a busy glass spends no try; it counts as quiet: no endless checks) */
        return c6_done(s, HO_NOT_NOW);

    case C6E_SENT:                             /* every chunk taken: the end is where the C6 switches - from here it may be in */
        r->wrote = 1; r->greet = 1; r->deaf = 0;     /* (the deaf count starts again only now: a write cut before its end leaves the C6
                                                   as it was, the escape still earned - c6_ho_check.c's (d)) */
        return (s->act ? C6A_END_ACTIVATE : C6A_END) | C6A_SAVE;

    case C6E_ESC_ABORT:                        /* (review 10's M1) the chain's restart called off: the tank would not save - GET HELP, no restart,
                                                  nothing busy; the chain's intent gone (its next boot would restart again) */
        if (s->ho != HO_ESCAPE) return C6A_NONE;
        s->restart = 0; s->wait = 0; r->esc = r->esc_b = 0; s->ho = HO_NO_ANSWER; r->rep.last = HO_NO_ANSWER;
        s->chain = 0; s->esc_abort = 1;        /* (review 11: a later tap's check is an ordinary one, its card the plain one) */
        return s->nosave ? C6A_CARD_NOSTART : C6A_CARD_NOSTART | C6A_SAVE;

    case C6E_SAVE_FAILED:                      /* NVS will not keep the record: nothing more this boot (the rules' counts would not
                                                  last - (a) and (c) without a limit), and LINK says GET HELP */
        s->nosave = 1; s->greeting = 0;
        if (s->ho == HO_CHECKING || s->ho == HO_READY) s->ho = HO_NOT_NOW;
        else if (s->ho == HO_ESCAPE) s->ho = HO_NO_ANSWER;   /* (review 9's L1: a chain whose decision did not save restarts nothing - GET HELP) */
        if (abort_owed) return C6A_CARD_NOSTART;   /* (review 11's M1: the abort's GET HELP shows, saved or not) */
        else if (s->ho == HO_WRITING) s->ho = HO_FAIL;   /* (no end follows; a RESULT that reached it left WRITTEN: the restart) */
        return C6A_DONE;

    case C6E_RESULT: {                         /* how the write ended */
        r->phase = C6PH_IDLE;
        if ((e->at == C6X_BEGIN || e->at == C6X_END || (e->at == C6X_CHUNK && !e->off)) && e->code == C6_ERR_UNCONFIRMED) {   /* our relay on probation refused it (layer 1):
                                                  at its begin nothing erased; at its end (a copy of its own image) wiped - nothing switched
                                                  either way. Not "the stage is in it": the image on probation may be an older relay (review 6's M2) */
            r->wrote = 0; r->greet = 0; c6_q(&r->refused, 255);
            if (e->at != C6X_BEGIN) r->seen = 1;   /* (a copy refused at its first chunk or end: the image on probation IS the stage) */
            if (e->at == C6X_BEGIN && r->refused <= C6_REFUSALS) { r->tries--; if (!s->keeper) r->auto_n--; }   /* (at its end the slot was
                                                  erased and written: that try stays spent - rule a) */
            if (r->refused > C6_REFUSALS || (r->deaf >= C6_DEAF_BOOTS && e->at != C6X_BEGIN)) r->never = 1;   /* (refused after the guard's last start, or too often: GET HELP) */
            c6_q(&r->quiet, C6_QUIET_BOOTS); r->rep.why = C6W_BEGIN_REFUSED;
            return c6_done(s, HO_FAIL);
        }
        if (e->at == C6X_CHUNK && e->off + C6_CHUNK >= s->size && !s->act) { r->wrote = 1; r->greet = 1; r->deaf = 0; }   /* the LAST chunk failed: the C6
                                                  may hold the whole image, and the next begin's closing end would switch a 1.x one (c6_ho_check.c) - as SENT */
        if (e->at == C6X_OK || r->wrote) {     /* the end reached: the tank restarts whatever it said (its next hearing says if the
                                                  C6 kept it) */
            r->rep.why = e->at == C6X_OK ? C6W_WRITTEN : C6W_WRITE_FAILED; r->quiet = 0; r->fail_kb = 0;
            s->ho = HO_WRITTEN; r->rep.last = HO_WRITTEN; return C6A_DONE | C6A_SAVE;
        }
        if (e->at == C6X_BEGIN) {       /* refused at the start: the try stays spent - the C6 may have erased the slot and lost
                                                  its answer (c6_ho_check.c: a try given back let the erases go on without a limit); quiet */
            c6_q(&r->quiet, C6_QUIET_BOOTS); r->rep.why = C6W_BEGIN_REFUSED;
        } else if (e->at == C6X_CHUNK && e->code == C6_ERR_INVALID_SIZE) { r->never = 1; r->rep.why = C6W_TOO_BIG; }
        else {
            r->rep.why = C6W_WRITE_FAILED;
            if (e->at == C6X_CHUNK && e->off) {    /* a failure inside the image has a place: twice there with one answer, never */
                const uint32_t kb = e->off / 1024 + 1;
                if (r->fail_kb && (kb > r->fail_kb ? kb - r->fail_kb : r->fail_kb - kb) <= C6_SAME_PLACE_KB && r->fail_code == (uint8_t)e->code) { r->never = 1; r->rep.why = C6W_SAME_PLACE; }
                r->fail_kb = (uint16_t)(kb > 0xffff ? 0xffff : kb); r->fail_code = (uint8_t)e->code;
            }
        }
        return c6_done(s, HO_FAIL); }          /* (UPDATING turns into THE LINK UPDATE STOPPED: TICK) */
    }
    return C6A_NONE;
}
