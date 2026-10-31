/* tank_events.h - moments the tank reports as they happen (2026-09-15).
 *
 * Sounds are feedback, never behavior (docs/AUDIO.md): tank.c emits an
 * event at the instant something audible happens and carries on; whoever
 * registered a listener (the device: the audio port; the sim: SDL audio)
 * turns it into a cue. No listener = nothing happens, so tests and QEMU
 * need not care. Emitting is cheap and synchronous - a listener must only
 * enqueue, never render (the tank task owns the frame rate). */
#ifndef TANK_EVENTS_H
#define TANK_EVENTS_H

enum {
    TEV_TAP,          /* a tap on the glass that was not a feed */
    TEV_FEED,         /* the keeper dropped pellets (tank_feed) */
    TEV_LIGHT_ON,     /* the light toggle, and where it landed */
    TEV_LIGHT_OFF,
    TEV_WIPE,         /* a stroke became a wipe (once per stroke) */
    TEV_SNIP,         /* a segment of a slash cut at least one frond */
    TEV_EAT,          /* fish took a pellet (fish = who) */
    TEV_SPOOK,        /* the 3-tap flee engaged (once per episode) */
    TEV_INVESTIGATE,  /* a hold-approach was credited (fish = who) */
    TEV_BUBBLES,      /* a fish reached the bubble column to play (fish = who) */
    TEV_WELCOME,      /* the first-run setup opened (a fresh install / a reset) */
    TEV_WHEEL_TICK,   /* the letter wheel moved one detent */
    TEV_CONFIRM,      /* BEGIN / DONE: a setup or birth flow was completed */
    TEV_CRAB_FIND,    /* the crab's dig came up with a sand dollar (0.3.9.1: a cue for the keeper to look) */
    TEV_CRAB_COIN,    /* the keeper took the sand dollar the crab held up (0.3.9.3: Strato's coin sound) */
    TEV_COUNT
};
extern const char *const TANK_EVENT_NAMES[TEV_COUNT];

typedef void (*tank_event_fn)(int ev, int fish, void *ud);
void tank_events_set(tank_event_fn fn, void *ud);   /* one listener; NULL = none */
void tank_emit(int ev, int fish);                   /* fish = -1 when it is nobody's */

#endif
