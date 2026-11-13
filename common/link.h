/* link.h — tanks side by side, fish that visit (0.4: docs/tank-link-0.4.md).
 *
 * The protocol engine, and nothing else: no radio, no drawing, no fish_t.
 * It hears frames through link_rx, sends through the host's `send`, keeps
 * time from the caller's milliseconds, and tells the tank what to do through
 * an event queue (link_poll). The tank tells it what the keeper and the fish
 * did (link_arm, link_close, link_moving, link_offer, link_cross, ...).
 *
 * THE TWO PROMISES (selftest-link checks them after every step):
 *   - never two copies: a fish is simulated by its home tank OR by one host,
 *     never both, whatever the radio drops, delays, repeats or cuts;
 *   - never zero: home never deletes a fish because of the radio. A fish
 *     whose host goes quiet comes home from a bubble cloud.
 * A boot that finds fish away in its save (the tank died with them next
 * door) restores them itself, LINK_RESTORE_MS after the boot - not at once:
 * the host may still hold them on their last lease.
 * How (the leases): a visitor lives on its host only on a lease that home
 * renews, once a second, only while it hears that fish's VISIT reports. The
 * host lets a visitor go LINK_LEASE_LIFE_MS after its last renewal; home
 * restores LINK_RESTORE_MS after the fish's last VISIT. Renewals stop at most
 * LINK_LEASE_HEARD_MS after the last VISIT, so the host lets go at most
 * HEARD + LIFE (+ the radio's delay) after it - before home restores.
 *
 * Edges are LEFT and RIGHT (a row of tanks). A link joins one tank's RIGHT to
 * the next one's LEFT, made by the keeper's holds on both arrows (link_arm).
 * One hop: a visitor goes home through the door it came in by; a host never
 * passes a visitor on.
 *
 * Addresses are tank IDs (tank_t.uid); the platform's send maps them to radio
 * addresses (0 = everyone). Every frame read is checked: a frame from a
 * stranger's tank can be ignored, never crash. */
#ifndef POCKET_TANK_LINK_H
#define POCKET_TANK_LINK_H

#include <stdbool.h>
#include <stdint.h>
#include "passport.h"

#define LINK_PROTO        1
#define LINK_FRAME_MAX    250        /* one ESP-NOW frame */
#define LINK_PEERS_MAX    6          /* tanks heard and remembered */
#define LINK_VISIT_MAX    N_VISIT_MAX  /* visitors the engine can hold at once: tank.h's slots. The room rule (how many a tank
                                          takes: 2, tank_link.h TANK_LINK_VISIT_PLACES) is the host's room() */
#define LINK_AWAY_MAX     N_FISH_MAX /* this tank's fish out at once (at most all of them) */
#define LINK_EVQ          32
#define LINK_BADGES       4          /* circle, triangle, square, diamond - a shape AND a color */

/* timings, ms */
#define LINK_BEACON_MODE_MS    500   /* link mode: 2 a second */
#define LINK_BEACON_LINKED_MS  1000
#define LINK_PEER_FRESH_MS     1500  /* heard this recently: in the NEARBY row, a candidate */
#define LINK_PEER_GONE_MS      10000 /* a linked tank silent this long: the edge closes (LOST) */
#define LINK_ARM_MS            5000  /* an arrow held stays asking this long after it is let go */
#define LINK_JOIN_RESEND_MS    200
#define LINK_MISMATCH_MS       3000  /* linked here but not there this long: the edge closes */
#define LINK_OFFER_MS          500   /* no answer to an OFFER: the door is glass for that fish */
#define LINK_RESEND_MS         100
#define LINK_PENDING_MS        8000  /* the host holds an accepted place this long for the GO */
#define LINK_GO_SLOW_MS        500   /* home's GO resends past that, until VISIT or DROP */
#define LINK_CROSS_MS          6000  /* the fish must pass the edge this soon after CROSS_OK (2026-10-05: 2 s was too short
                                      * for a fish hovering at the door facing in - it turns first) */
#define LINK_VISIT_MS          1000  /* a host reports each visitor this often */
#define LINK_LEASE_MS          1000  /* home renews each away fish this often ... */
#define LINK_LEASE_HEARD_MS    2000  /* ... while its last VISIT is this fresh */
#define LINK_LEASE_LIFE_MS     6000  /* a visitor lives this long past its last renewal */
#define LINK_RESTORE_MS        10000 /* home restores this long after the last VISIT */
#define LINK_RESTORE_NOVISIT_MS (LINK_PENDING_MS + LINK_LEASE_LIFE_MS + LINK_LEASE_HEARD_MS)
                                     /* ... or this long after the GO when no VISIT ever came: a GO landing at the end of
                                      * the host's held place starts a lease nobody renews (16 s; 2026-10-05, 7c) */
#define LINK_RETURN_RESEND_MS  200
#define LINK_NUDGE_MS          1000  /* motion shorter than this is a nudge: nothing happens */
#define LINK_SETTLE_MS         10000 /* after a door opens, moving is settling the tanks into place, not drifting
                                      * (2026-10-05: every link on the glass was followed by a DRIFTED notice) */
#define LINK_STILL_MS          2000  /* still this long: drifting is over */
#define LINK_DRIFT_GRACE_MS    30000 /* drifting this long: the visitors head home */
#define LINK_HOMEWARD_MS       20000 /* ... and this long later the edge closes (the rest poof) */
#define LINK_MODE_IDLE_MS      120000/* link mode closes itself after this long with nothing happening */

typedef enum { LINK_LEFT, LINK_RIGHT, LINK_EDGES } link_edge_t;

enum { LINK_NO_ANSWER, LINK_NO_FULL, LINK_NO_PAUSED, LINK_NO_OTHER, LINK_NO_DEPORTED };
#define LINK_DEPORT_MS 300000   /* a DEPORTed visitor is refused at our door this long (the keeper's button, 2026-10-06) */   /* why a crossing was refused (LEV_CROSS_NO.reason) */
typedef enum {   /* why an edge closed (LEV_UNLINKED.reason) */
    LINK_WHY_KEEPER, LINK_WHY_LOST, LINK_WHY_SLEEP, LINK_WHY_DRIFT, LINK_WHY_MISMATCH
} link_why_t;

typedef enum {
    LEV_NONE,
    LEV_NEARBY,          /* the NEARBY row changed (link_peers) */
    LEV_ASKING,          /* edge: another tank asks to join here (peer = it, or 0 = two at once: wait) */
    LEV_LINKED,          /* edge + peer: the door opens */
    LEV_UNLINKED,        /* edge + peer + reason: the door closes */
    LEV_CROSS_OK,        /* uid: swim on through (call link_cross as it passes the edge) */
    LEV_CROSS_NO,        /* uid: the door is glass for this fish for now (reason: LINK_NO_*) */
    LEV_VISITOR_IN,      /* pp + edge + depth/heading/speed: a visitor swims in at this edge */
    LEV_VISITOR_POOF,    /* uid: a visitor turns to bubbles here and is gone */
    LEV_VISITOR_HOMEWARD,/* uid: steer this visitor home through its door (link_leaving as it passes) */
    LEV_FISH_BACK,       /* pp + edge + depth/heading/speed: one of ours swims back in at this edge */
    LEV_FISH_RESTORE,    /* pp: one of ours comes back from a bubble cloud */
    LEV_DRIFT,           /* the DRIFTED notice: on (ms_left of the grace) */
    LEV_STEADY,          /* ... off */
    /* cross-tank fry (phase 5, docs/tank-link-0.4.md): a host fish and a visitor spawn */
    LEV_FRY_ROOM,        /* host: uid = the visiting parent; reason 1 = its home has room for a fry, 0 = not */
    LEV_FRY_IN,          /* home: pp + edge + peer: a fry of ours, born next door - let go of its held place, add it,
                          * away there, on its way home */
    LEV_FRY_OUT,         /* host: uid + pp + edge + peer: its home took it - it swims home through that door now */
    LEV_FRY_KEEP,        /* host: pp: its home said no - the fry is this tank's after all */
    LEV_FRY_LOST,        /* host: uid: no answer at all - no fry here (its home restores it if it took it) */
    /* GIVE (phase 6): one of our fish becomes the neighbor's for good - see link_give below */
    LEV_GIVE_ASK,        /* receiver: pp + uid + peer: a keeper next door offers us this fish - ask ours (link_give_answer) */
    LEV_GIVE_TAKEN,      /* giver: uid + peer + nonce + pp: it was accepted - save a GIVEN note (nonce + pp), take the fish out,
                          * save, THEN link_give_done (a boot that finds a fish and a GIVEN note for it removes the fish) */
    LEV_GIVE_NO,         /* giver: uid + reason (LINK_GIVE_NO_*): it stays ours */
    LEV_GIVE_ALIVE,      /* receiver: pp + uid + peer + nonce: it is ours - add it, note it RECEIVED (uid + nonce), drop the
                          * ARRIVING note, save, THEN link_give_alive */
    LEV_GIVE_CANCEL,     /* receiver: uid + peer + nonce: the giver kept it - drop the ARRIVING note (or close the question) */
    LEV_GIVE_CLOSED,     /* giver: uid + nonce: the neighbor has it alive - the GIVEN note can go */
    LEV_GIVE_RESTORE,    /* giver: uid + nonce + pp: the neighbor never had it - it lives here again from the GIVEN note */
    /* SAND DOLLAR GIFTS (0.4.2, sdgift.h): money, not fish - see link_sd_send below */
    LEV_SD_IN,           /* receiver: peer + edge + nonce + amount: a gift from that tank - decide it (sdg_in), save, link_sd_answer */
    LEV_SD_DONE,         /* giver: peer + edge + nonce + amount + reason (SDG_V_*): the answer to our gift - settle the OUT note */
    LEV_COUNT
} link_ev_type_t;

/* a fish known by name in another tank: a cross-tank fry's other parent
   travels with it (the family line reads its name, 2026-10-06) */
typedef struct { uint32_t uid; char name[FISH_NAME_MAX + 1]; uint32_t color; uint8_t bold, sociable; } link_kin_t;

typedef struct {
    link_ev_type_t type;
    link_edge_t    edge;
    uint8_t        reason;
    uint32_t       peer, uid, ms_left;
    uint32_t       nonce;                     /* GIVE: which gift (the notes keep it); SD: which sand dollar gift */
    int32_t        amount;                    /* SD: sand dollars */
    float          depth, heading, speed;     /* depth: 0 = the surface .. 1 = the floor */
    passport_t     pp;
    link_kin_t     kin;                       /* LEV_FRY_IN: its parent in the tank it was born in (uid 0 = not sent) */
} link_ev_t;

typedef struct {
    void *ctx;
    void (*send)(void *ctx, uint32_t to, const uint8_t *buf, int len);  /* to 0 = every tank */
    int  (*room)(void *ctx);                                            /* visitors this tank can take now */
    bool (*visitor)(void *ctx, uint32_t uid, passport_t *pp);           /* a visitor's state now (false = not here) */
    int  (*fry_room)(void *ctx);                                        /* home: room for a new fry of ours (> 0 = yes) */
    /* GIVE: what this tank has SAVED about a fish given or taken (LINK_GS_*; pp = the note's copy). The engine
       keeps nothing across a boot - after one, this is how it answers a neighbor still mid-way. NULL = nothing */
    int  (*give_state)(void *ctx, uint32_t uid, uint32_t nonce, passport_t *pp);
    /* home: hold (on) a place for a fry of ours - `parent`, courting at the tank `peer` - or let it go (off). on: > 0 =
       held (asked again: still held, its time renewed). The engine asks it for FRY_ASK and FRY; the tank lets go when
       the fry comes (LEV_FRY_IN). NULL = fry_room alone, nothing held (2026-11-03, the five-fish cap) */
    int  (*fry_hold)(void *ctx, uint32_t peer, uint32_t parent, bool on);
    /* GIVE: this fish lives here as one of OURS (home, or next door on a trip - never a visitor here). A late GIVEN for
       it is answered "we have it" whatever RECEIVED says: that list keeps the last 16, the fish stays (0.4.2, LK-2: the
       giver's GIVEN_LOST brought it back there, two copies). NULL = RECEIVED alone */
    bool (*ours)(void *ctx, uint32_t uid);
} link_host_t;

/* what the NEARBY row and the arrows draw */
typedef struct { uint32_t tank; uint8_t badge; bool link_mode, drifting; int rssi; } link_peer_info_t;

typedef struct link link_t;           /* ~5 KB: the firmware puts it in PSRAM */
int     link_size(void);
/* trip_seed: any number that differs between boots (the clock, the RNG) */
void    link_init(link_t *L, uint32_t my_tank, const link_host_t *host, uint32_t trip_seed, uint32_t now_ms);
void    link_tick(link_t *L, uint32_t now_ms);
void    link_rx(link_t *L, const uint8_t *buf, int len, int rssi, uint32_t now_ms);
bool    link_poll(link_t *L, link_ev_t *ev);

/* the keeper */
void    link_mode(link_t *L, bool on);              /* the LINK page: beacons, NEARBY, arrows */
bool    link_in_mode(const link_t *L);
void    link_arm(link_t *L, link_edge_t e);         /* the hold on an arrow completed */
void    link_close(link_t *L, link_edge_t e);       /* the keeper closes this door */
void    link_moving(link_t *L, bool moving);        /* the IMU, every tick (never on a worn watch) */
void    link_sleep(link_t *L);                      /* going to sleep: everyone home, every door closed */

/* the fish */
bool    link_door_open(const link_t *L, link_edge_t e);   /* linked, not paused: a fish may head out */
uint32_t link_neighbor(const link_t *L, link_edge_t e);   /* the tank at that edge, 0 = glass */
void    link_offer(link_t *L, link_edge_t e, const passport_t *pp, float depth, float heading, float speed);
bool    link_cross(link_t *L, uint32_t uid);              /* home: it passed the edge after CROSS_OK. true = it is next
                                                           * door now (stop simulating it); false = too late, keep it */
void    link_leaving(link_t *L, uint32_t uid, const passport_t *pp, float depth, float heading, float speed);
                                                          /* host: a visitor passed its door home (stop simulating it) */
/* state, for the tank, the director and the tests */
/* cross-tank fry. host: link_fry_ask (does the visitor's home have room? A yes is a place it HOLDS while we keep
 * asking - ask again every TANK_LINK_FRY_ASK_S), then link_fry_give (the fry is its home's: hand it over; false =
 * cannot, keep it); link_fry_off: the pair is off, its home lets go of the place */
/* the keeper DEPORTed a visitor: it swims home, and an OFFER of it is refused
 * (LINK_NO_DEPORTED) for ms */
void    link_deny(link_t *L, uint32_t uid, uint32_t ms);
void    link_fry_ask(link_t *L, uint32_t parent_uid);
void    link_fry_off(link_t *L, uint32_t parent_uid);
/* ... link_fry_peek: room or not, and the home holds nothing (0.4.1 round 2: the pair waits for a visitor place here, and only
 * a fry whose home HAS room waits - a full home's fry is ours). Answered as an ask is (LEV_FRY_ROOM) */
void    link_fry_peek(link_t *L, uint32_t parent_uid);
bool    link_fry_give(link_t *L, uint32_t parent_uid, const passport_t *fry, const link_kin_t *other);   /* other: our parent of it (may be NULL) */
bool    link_accepted(const link_t *L, uint32_t uid, link_edge_t e);   /* home: a yes for it at that door, still in time */
bool    link_is_away(const link_t *L, uint32_t uid);      /* one of ours, next door (not simulated here) */
bool    link_hosting(const link_t *L, uint32_t uid);      /* a visitor simulated here */
int     link_visitors_coming(const link_t *L);            /* places given that host.room cannot see yet (accepted, not drawn) */
int     link_badge(const link_t *L);
int     link_peer_badge(const link_t *L, uint32_t tank);  /* a tank heard: its badge (-1 = not heard) */
int     link_peers(const link_t *L, link_peer_info_t *out, int max);   /* the NEARBY row: fresh tanks in link mode */
bool    link_drifting(const link_t *L);                   /* this tank's doors are paused */
uint32_t link_events_lost(const link_t *L);               /* events dropped on a full queue (a test fails on any) */

/* GIVE (phase 6, docs/tank-link-0.4.md "Giving a fish"): one of our fish becomes the neighbor's
 * for good - both keepers agree on their own glass. Never two, never zero, across a crash at any
 * point, on three rules:
 *   - the receiver brings it to life ONLY on GIVEN, and holds it until then as ARRIVING (saved,
 *     not alive), asking TAKE until the giver answers GIVEN or CANCEL;
 *   - the giver sends GIVEN only after it has saved a GIVEN note (with the fish's passport) and
 *     taken the fish out; it keeps sending until GIVEN_OK, or GIVEN_LOST (then the note brings
 *     the fish back here);
 *   - before that the fish is still home, alive, and the giver may cancel at any time - a crash,
 *     a timeout, the keeper - and answers any later TAKE with CANCEL.
 * Nothing about a gift lives in the engine across a boot: the tank's notes do (give_state), and
 * after a LINKED the tank hands back what it was in the middle of (link_give_resume_*). */
#define LINK_GIVE_MAX       2       /* gifts in flight each way */
#define LINK_GIVE_ASK_MS    60000   /* the receiving keeper's time to answer */
#define LINK_GIVE_RESEND_MS 500
enum { LINK_GIVE_NO_REFUSED, LINK_GIVE_NO_FULL, LINK_GIVE_NO_ANSWER, LINK_GIVE_NO_CANCEL, LINK_GIVE_NO_OTHER };
enum { LINK_GS_NONE, LINK_GS_GIVEN, LINK_GS_ARRIVING, LINK_GS_ALIVE };   /* give_state: a GIVEN note / an ARRIVING note / RECEIVED */
bool    link_give_refuse(link_t *L, uint32_t uid, uint32_t nonce, int reason);   /* receiver: no, and why (LINK_GIVE_NO_*) - never the keeper's NO THANKS unless it was */
void    link_give_forget(link_t *L, uint32_t uid, uint32_t nonce);   /* receiver: let go of a yes the giver never settled (its note expired) */
bool    link_give(link_t *L, link_edge_t e, const passport_t *pp);   /* giver: offer this fish (home, alive) to the neighbor there;
                                                                       * false = the door is shut, it is busy, or no room for it */
/* every answer names the gift - the fish AND the event's nonce: an answer to a question that has
   closed must not land on a newer offer of the same fish (2026-10-20, selftest-give's soak) */
void    link_give_cancel(link_t *L, uint32_t uid);                    /* giver: the keeper takes it back (only while asking) */
bool    link_give_answer(link_t *L, uint32_t uid, uint32_t nonce, bool yes);   /* receiver: yes ONLY after the ARRIVING note
                                                                       * is saved; false = that question had closed (drop the note again) */
void    link_give_done(link_t *L, uint32_t uid, uint32_t nonce, const passport_t *pp);   /* giver: after LEV_GIVE_TAKEN's note and save - pp the
                                                                       * fish as it left (the GIVEN note's): GIVEN carries it (0.4.1) */
bool    link_give_keep(link_t *L, uint32_t uid, uint32_t nonce);      /* giver: a TAKEN gift that never left (the tank next door went
                                                                       * quiet, its door closed): ours after all - safe until link_give_done,
                                                                       * as the receiver brings a fish to life only on GIVEN (2026-11-02).
                                                                       * false = too late */
bool    link_peer_heard(const link_t *L, uint32_t tank, uint32_t within_ms);   /* a frame from that tank this recently */
void    link_give_alive(link_t *L, uint32_t uid, uint32_t nonce);     /* receiver: after LEV_GIVE_ALIVE's save */
void    link_give_resume_out(link_t *L, uint32_t peer, uint32_t uid, uint32_t nonce, const passport_t *pp);   /* a GIVEN note */
void    link_give_resume_in(link_t *L, uint32_t from, uint32_t uid, uint32_t nonce, const passport_t *pp);    /* an ARRIVING note */
bool    link_give_busy(const link_t *L, uint32_t uid);                /* being given away (not to be offered through a door) */
uint32_t link_give_nonce(const link_t *L, uint32_t uid);              /* ... that gift's nonce (0: none) - which ask a LEV_GIVE_NO answers */
int     link_away_edge(const link_t *L, uint32_t uid);                /* one of ours visiting a linked neighbor: that edge (-1: home, or not there) -
                                                                         GIVE offers it to the tank it is visiting, where it is (2026-11-01) */

/* SAND DOLLAR GIFTS (0.4.2, sdgift.h). A tank of this build says so in its beacon (an older one's beacon does not, and
 * drops the frames: its door shows no coin). The engine keeps one gift of ours in flight and sends it while the door to
 * its tank is open, until that tank answers (LEV_SD_DONE) - nothing times out: the tank's saved OUT note is the truth,
 * and after a boot or a closed door the tank hands it back at that tank's LINKED (link_sd_send again). A gift to us comes
 * as LEV_SD_IN, once per frame heard (copies too); the tank decides and answers every one. */
bool    link_peer_sd(const link_t *L, uint32_t tank);      /* that tank takes sand dollar gifts (its beacon says so) */
bool    link_sd_send(link_t *L, uint32_t peer, uint32_t nonce, int amount);   /* giver: after the OUT note is saved; false = not linked
                                                                                 to that tank now (the note waits for its LINKED) */
void    link_sd_answer(link_t *L, uint32_t peer, uint32_t nonce, int verdict); /* receiver: after the save */
bool    link_sd_busy(const link_t *L);                     /* a gift of ours in flight in the engine */
void    link_sd_forget(link_t *L, uint32_t nonce);         /* stop sending that gift (it came back to us after a day) */
void    link_test_as_041(link_t *L, bool on);              /* the selftest: this engine acts as a 0.4.1 tank's (no gifts) */

/* what link mode's page draws (render_link_mode), read off the engine */
enum { LV_FREE, LV_ARMED, LV_ASKED, LV_TWO, LV_OPEN };   /* an edge: glass / our arrow asking / one tank asks here / two do / the door is open */
typedef struct {
    uint8_t badge;                                       /* ours */
    struct { uint8_t state, badge, badge2, sd; } edge[LINK_EDGES];   /* badge: the asker or the neighbor; badge2: the second asker;
                                                                         sd: an open door's neighbor takes sand dollar gifts (0.4.2) */
    uint8_t n_near, near_badge[LINK_PEERS_MAX];          /* tanks heard in link mode */
    bool    drifting; uint32_t drift_ms_left;             /* the DRIFTED notice, and its phase's time left */
    bool    drift_homeward;                               /* ... past the grace: the visitors swim home, then the door closes */
} link_view_t;
void    link_view(const link_t *L, link_view_t *v);

#endif
