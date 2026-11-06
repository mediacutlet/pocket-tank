/* tank_link.h — the tank's side of the tank link (0.4, docs/tank-link-0.4.md):
 * the link engine (link.h) knows passports and frames, the tank knows fish
 * and glass; this joins them. The platform owns the radio and the engine (it
 * may be NULL: the radio is down) and calls, every frame after tank_tick:
 *
 *     tank_link_tick(t, L)                       - doors, offers, fish through the glass
 *     while (link_poll(L, &e)) tank_link_event(t, L, &e);
 *
 * and gives the engine tank_link_host(t) as its host (room, visitors). */
#ifndef POCKET_TANK_TANK_LINK_H
#define POCKET_TANK_TANK_LINK_H

#include "tank.h"
#include "link.h"

/* the room during a visit (Strato 2026-11-05, replacing his 10-05 "5 fish a tank, visitors counted": two full tanks linked
   and no fish ever visited): a tank always has TANK_LINK_VISIT_PLACES places for visitors, whatever its own count - "5 local
   fish and 2 visitor slots ... 7 fish cap during visitations". POP_CAP bounds our own fish (owned, away or home), the places
   bound the visitors, so a fish of ours coming home never needs room: it always has its place, and the sum holds at 7.
   A fry born here for a visitor's home swims home as a visitor: it needs one of the places too (progression do_arrival) */
#define TANK_LINK_VISIT_PLACES 2
#define TANK_LINK_SWIM_MAX (POP_CAP + TANK_LINK_VISIT_PLACES)   /* fish swimming in one tank at once, home + visitors: 7 */
#if TANK_LINK_VISIT_PLACES > N_VISIT_MAX
#error "the visitor places live in tank.h's visitor slots: N_VISIT_MAX must hold them"
#endif
#define TANK_LINK_CURIOUS  2.0f    /* curiosity a door opening gives every fish here ("something new over there") */
#define TANK_LINK_REFUSED_S 10.0f  /* a door that refused a fish is glass for it this long ... */
#define TANK_LINK_FULL_S    60.0f  /* ... and this long when the tank next door was full */
/* a cross-tank fry's place at its home (2026-11-03, the five-fish cap): the host asks every FRY_ASK_S while the pair
   courts, yes or no; a yes is a place the home HOLDS (tank_t.fry_hold) until FRY_HOLD_S after the host's last ask; the
   host counts a yes only FRY_FRESH_S after it heard it. A fry born on a fresh yes reaches its home (the hand-off,
   LINK_CROSS_MS at most) while its place is still held: */
#define TANK_LINK_FRY_ASK_S   30
#define TANK_LINK_FRY_FRESH_S 75
#define TANK_LINK_FRY_HOLD_S  120
#if TANK_LINK_FRY_FRESH_S + LINK_CROSS_MS / 1000 >= TANK_LINK_FRY_HOLD_S || TANK_LINK_FRY_ASK_S >= TANK_LINK_FRY_FRESH_S
#error "a held fry place must outlast a fresh yes and its hand-off, and the host must ask again before its yes goes stale"
#endif

link_host_t tank_link_host(tank_t *t, void (*send)(void *ctx, uint32_t to, const uint8_t *buf, int len));
int  tank_link_room(void *t);                                     /* link_host_t.room: visitor places free now */
/* a place free for a fry born here to swim home in as a visitor: the places the tank sees free, less the ones the engine
   gave and the tank has not drawn yet (tank_t.vis_coming) */
bool tank_link_fry_place(const tank_t *t);
/* the ONE check for a fry that belongs next door (0.4.1 audit round 2, R2-4 / R2L-1; Strato 11-05: it "waits quietly while the
   tank it would be born in has no visitor place: no courting, no dance, nothing held at its home; it comes once a place
   opens"). no_place: the fry partner is a visitor here and no place is free - places TAKEN, not promised (vis_coming: a
   promise that never came cut a dance and dropped the home's hold, the review's F2; the birth itself still checks
   tank_link_fry_place). tank_link_tick then only PEEKS at its home's room (nothing held there). waits: no place, unless its
   home said NO - a home with room (or not yet heard): the fry belongs next door; a full home's fry is ours, born here as
   before (the review's F1).
   progression: nobody courts for a fry that waits */
bool tank_link_cross_no_place(const tank_t *t);
bool tank_link_cross_waits(const tank_t *t);
/* THE SWIM LIMIT's guard (0.4.1): a tank with more than TANK_LINK_VISIT_PLACES visitors, or more than TANK_LINK_SWIM_MAX
   swimming, calls this (the sim's selftests fail, the device logs once); NULL = no guard */
extern void (*tank_link_swim_hook)(const tank_t *t, int swim, int visitors, const char *path);
bool tank_link_visitor(void *t, uint32_t uid, passport_t *pp);    /* link_host_t.visitor */
void tank_link_tick(tank_t *t, link_t *L);
/* one engine event: true = it was about a fish of this tank or a visitor here */
bool tank_link_event(tank_t *t, link_t *L, const link_ev_t *e);
/* no engine any more (the radio went down with a fish's trip half done):
 * every door shuts; a fish half way through the glass turns back */
void tank_link_doors_shut(tank_t *t);
/* the keeper's DEPORT on a visitor's passport: home it swims, briskly; the
 * door refuses it for LINK_DEPORT_MS */
void tank_link_deport(tank_t *t, int vis_slot);

#endif
