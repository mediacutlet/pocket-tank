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

#define TANK_LINK_SWIM_MAX 5       /* fish swimming in one tank at once, home + visitors: the room a tank offers (Strato 2026-10-05: a 5-fish
                                      limit per tank, not a fixed visitor cap - 2 fish host 3, 4 host 1, 5 host none until one of theirs is away) */
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
int  tank_link_room(void *t);                                     /* link_host_t.room */
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
