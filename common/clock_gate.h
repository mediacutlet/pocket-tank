/* clock_gate.h - the boot's TIME CHECK gate (firmware main.c net_clock_boot), kept here, pure, so the sim's
 * --selftest-clock-gate drives the very lines the boards run through a host model of the boot (0.4.1).
 *
 * WHAT THE PAGE IS FOR. A chip-board's clock is the chip's; it lags real time only when a boot found the chip
 * stopped (no cell, the cable out) or unanswered, the internet did not answer, and the boot resumed the clock at
 * the save's stamp (main.c, the cold boot's seed): that stretch is OWED, and the first good sync lives it
 * through. Every other skip of the ask that 0.4 tried (rc rounds one and two) lost an owed night some way, so 0.4
 * went back to 0.3.9.6's gate: ask at every boot until a try works - up to 30 s of TIME CHECK at EVERY boot while
 * the saved network is gone (0.4.1 items: release-0.4.md 2g item 1).
 *
 * 0.4.1 keeps 0.3.9.6's rule - an owed stretch is asked for at every boot, never capped, until a try works - and
 * makes it cheaper two ways, each of which can only DELAY a payment, never drop it:
 *  - NVS "netlag" (the board's, never a backup's): 1 = the clock was resumed from a save since the last good sync
 *    (written at the seed, BEFORE the chip is), 0 = the last good sync set it and no seed since (written after the
 *    chip has it). Absent = not known (0.4.0 and older never wrote it): 0.3.9.6's gate exactly. A chip-board whose
 *    last try failed but owes nothing (lag 0: the chip ran since a good sync - a read that failed at one boot with
 *    the router down, H) does not ask; one that owes asks whatever netok says (a seed while no network was saved
 *    left netok 1 from before: model I1, the night skipped in 0.3.9.6 and 0.4.0).
 *  - THE QUICK LOOK: a chip-board whose last boot ask did not see the router at all (every try "not found";
 *    `missed` counts them, in the Wi-Fi namespace: saving a network clears it, any connect clears it) asks with
 *    two sweeps, a few seconds, not the 30 s; every CLOCK_FULL_EVERY-th check still looks in full (a router at the
 *    edge of range, found only by the long look, waits three boots at most). A router seen gets the whole 30 s
 *    (net_port_esp.c). The
 *    pendant (no chip) keeps its full morning check: its nights' power-off waits on that answer (0.3.9).
 * The first sync onto real time (netclk 0) keeps its NET_FIRST_TRIES cap, as since 0.3.9. */
#ifndef POCKET_TANK_CLOCK_GATE_H
#define POCKET_TANK_CLOCK_GATE_H

#include <stdbool.h>

#define NET_FIRST_TRIES 3
#define CLOCK_FULL_EVERY 4   /* while the router goes unseen, every 4th check is the whole 30 s: a weak router only a long look finds is not left waiting */

enum { CLOCK_LAG_UNKNOWN = -1, CLOCK_LAG_NONE = 0, CLOCK_LAG_OWED = 1 };
/* NVS "netlag" as read (nvs_u8 with 255 for absent): anything but 0 or 1 is not known */
static inline int clock_lag_of(int v) { return v == 0 ? CLOCK_LAG_NONE : v == 1 ? CLOCK_LAG_OWED : CLOCK_LAG_UNKNOWN; }

typedef enum {
    CLOCK_ASK,            /* the TIME CHECK page, the whole 30 s if need be */
    CLOCK_ASK_QUICK,      /* the page, two sweeps for a router not seen last time (the whole 30 s once it is seen) */
    CLOCK_SKIP_GOOD,      /* 0.3.9.6's: the last try worked, nothing seeded since */
    CLOCK_SKIP_RAN,       /* 0.4.1: the last try failed, but the chip ran since a good sync - nothing owed */
    CLOCK_SKIP_GAVE_UP,   /* the first sync onto real time, NET_FIRST_TRIES failed mornings: the chip keeps its own time */
} clock_gate_t;

typedef struct {
    bool rtc;        /* the board has a clock chip that answered (s_rtc) */
    bool real;       /* NVS "netclk": the clock has been put on real time once */
    bool set;        /* the clock is set at this boot (a stopped chip, one that did not answer, or a clockless board after a power-off: not) */
    bool ok_before;  /* NVS "netok": the last try worked */
    int lag;         /* clock_lag_of(NVS "netlag") */
    int gave;        /* NVS "netgave": failed first-sync mornings */
    int missed;      /* boot asks in a row that never saw the router (Wi-Fi namespace "miss"; 0.4.2, several saved networks: that saw NONE of them) */
} clock_gate_in_t;

static inline clock_gate_t clock_gate(const clock_gate_in_t *g) {
    if (g->real && g->set && g->lag != CLOCK_LAG_OWED) {
        if (g->ok_before) return CLOCK_SKIP_GOOD;                         /* a deep-sleep wake on a good clock, a chip that ran all night */
        if (g->rtc && g->lag == CLOCK_LAG_NONE) return CLOCK_SKIP_RAN;    /* the chip ran since the last good sync: nothing is owed */
    }
    if (g->rtc && g->set && !g->real)                                     /* THE STOPPED CLOCK's one-time move onto real time, a few tries */
        return g->gave >= NET_FIRST_TRIES ? CLOCK_SKIP_GAVE_UP : CLOCK_ASK;
    if (g->rtc && g->missed > 0 && g->missed % CLOCK_FULL_EVERY) return CLOCK_ASK_QUICK;   /* owed (or not known), the router not seen last time */
    return CLOCK_ASK;
}

/* the gate 0.3.9.6 and 0.4.0 run (the model's yardstick) */
static inline clock_gate_t clock_gate_040(const clock_gate_in_t *g) {
    if (g->real && g->set && g->ok_before) return CLOCK_SKIP_GOOD;
    if (g->rtc && g->set && !g->real && g->gave >= NET_FIRST_TRIES) return CLOCK_SKIP_GAVE_UP;
    return CLOCK_ASK;
}

static inline bool clock_gate_asks(clock_gate_t d) { return d == CLOCK_ASK || d == CLOCK_ASK_QUICK; }

#endif
