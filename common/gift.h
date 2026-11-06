/* gift.h — GIVE's notes, the tank's half of a fish handed over for good
 * (tank link phase 6: link.h has the protocol and its three rules,
 * docs/tank-link-0.4.md the why and Strato's decisions).
 *
 * What a tank SAVES about a gift, in its own save slot (persist_port_blob_*,
 * key GIFT_NVS_KEY - never the tank save, whose layout stays as it is):
 *   - GIVEN: one of ours was accepted next door. Written, with the fish's
 *     passport, as the fish leaves (through the door, or the stamp over if
 *     it was visiting there) and only while that tank is linked and heard;
 *     the fish goes out in the same step, then GIVEN is sent. Until then the
 *     gift can still fall through (a reset, the link lost): the fish stays
 *     here and the other tank's TAKE is answered CANCEL. A boot that finds
 *     the fish and its GIVEN note removes the fish (the note wins). Dropped when the other
 *     tank says it has the fish alive; if it says it never had it, the fish
 *     lives here again from the note.
 *   - ARRIVING: our keeper said ACCEPT. Written BEFORE the yes goes out; the
 *     fish is not alive here until the giver says GIVEN.
 *   - RECEIVED: the last few gifts that came alive here, so a late GIVEN is
 *     answered "we have it" and never "we never had it".
 * The engine (link.c) keeps nothing across a boot: these notes answer for
 * the tank (gift_state = link_host_t.give_state).
 *
 * The book lives outside tank_t: on the boards tank_t is in internal RAM and
 * the book in PSRAM, and tank_init (the director's `fresh`, a reset) never
 * loses it. gift_attach pairs a tank with its book; a tank without one (most
 * selftests) cannot give or take. The rest of the book is the glass's
 * side, not saved: the keeper's taps, the question on our glass, the answer
 * shown on the card. */
#ifndef POCKET_TANK_GIFT_H
#define POCKET_TANK_GIFT_H

#include <stdbool.h>
#include <stdint.h>
#include "passport.h"
#include "link.h"

#define GIFT_NVS_KEY  "gifts"
#define GIFT_NOTES    (2 * LINK_GIVE_MAX)   /* GIVEN / ARRIVING notes each */
#define GIFT_RECEIVED 16
#define GIFT_BLOB_MAX (8 + 2 * GIFT_NOTES * 145 + GIFT_RECEIVED * 8)   /* the notes' blob, full (gift.c GIFT_REC): what a MY TANKS slot must hold */
#define GIFT_SAID_S   6.0f                  /* an answer stays on the fish card this long */
#define GIFT_LEAVE_S  20.0f                 /* a given fish swims out through the door; past this, bubbles carry it */
#define GIFT_WAIT_S   12.0f                 /* GIVE tapped on a fish in a doorway: it waits this long to be home or over there */
#define GIFT_HERE_S   2.5f                  /* given where it is (visiting the receiver): the GIVEN stamp shows this long, then it is theirs */
#define GIFT_HOLD_S   2.0f                  /* (no longer a tap guard since THE TOUCH RULE, touch_gate.h, 11-06; before: the card ignored taps this long (a kid tapping
                                               the glass as it appears must not answer it; the notices' NOTICE_HOLD_S, 2026-11-02) */
#define GIFT_HEARD_MS 3000                  /* the receiver silent this long while the gift swims out: it stays ours (its note is
                                               saved only as it leaves, 2026-11-02 - a gift lost with its link waited for a relink) */
#define GIFT_ARRIVING_TTL_S 1800.0f         /* an ARRIVING note lets go after this much of our running time with the giver not next door
                                               (the 11-02 sweep: it held a place for good if the two never met again - the shop said
                                               WELCOME THE NEW FRY, gifts were refused FULL). Never a second fish: the giver brings its
                                               fish back only on GIVEN_LOST, which is what we answer a late GIVEN once the note is gone */
#define GIFT_COMMIT_MS 1000                 /* ... and it leaves only with the receiver heard this lately (its TAKE comes every 0.5 s):
                                               at the door it waits a moment for the next one */

typedef struct { uint32_t uid, nonce, peer; passport_t pp; float held; bool back; } gift_note_t;   /* held: ARRIVING, seconds of this
                                                                                      tank's running time it has held a place (saved).
                                                                                      back: GIVEN, the fish lives HERE again (saved) */

/* the door a gift goes through (want_door): the keeper's pick in the GIVE modal, or GIFT_DOOR_ANY (`link give`
   without a side: the open door nearest the fish) - tank_link's gift_door, the one place the door is chosen */
enum { GIFT_DOOR_ANY = 0, GIFT_DOOR_LEFT = 1 + LINK_LEFT, GIFT_DOOR_RIGHT = 1 + LINK_RIGHT };

typedef struct gift_book {
    /* saved */
    gift_note_t given[GIFT_NOTES], arriving[GIFT_NOTES];
    uint8_t  n_given, n_arr, n_recv;
    struct { uint32_t uid, nonce; } recv[GIFT_RECEIVED];
    bool     persist;                       /* false: a selftest's book, kept in memory only */
    float    arr_unsaved; double arr_aged_at;      /* the ARRIVING notes' held time not yet on the flash; the clock it was last aged at */
    /* the glass's side (not saved) */
    uint32_t want_uid;                      /* GIVE confirmed in the GIVE modal (render.h): tank_link offers it */
    uint8_t  want_door; uint32_t want_peer; /* ... through this door (GIFT_DOOR_*) to this tank (0: whoever is there) */
    uint32_t wait_uid; double wait_since;    /* ... a fish in a doorway: offered once it is home or over there (the card says ASKING) */
    uint8_t  wait_door; uint32_t wait_peer;
    double   asking_since;                  /* offered at this clock (the modal's bar: the other keeper's LINK_GIVE_ASK_MS) */
    struct { uint32_t uid, nonce; } unsaved[LINK_GIVE_MAX];   /* gifts come alive whose save the flash refused: RECEIVED once one lands */
    double   resumed_at;                    /* the notes last handed to the engine (tank_link, once a second while linked) */
    uint32_t door_peer[LINK_EDGES];         /* who is at each door now (0: none) and its badge - the GIVE modal's names */
    int8_t   door_badge[LINK_EDGES];
    uint32_t cancel_uid;                    /* a tap on ASKING: tank_link takes it back */
    uint32_t asking_uid, asking_nonce;      /* offered, the other keeper deciding (the nonce: a NO for an older ask of it is not this one's) */
    uint32_t leaving_uid, leaving_nonce;    /* accepted: swimming out through the door, then gone */
    uint32_t leaving_peer;                  /* ... to this tank: GIVEN is noted and sent only while it is linked and heard */
    passport_t leaving_pp;                  /* ... given where it is: the host's latest of it (the engine's, at TAKEN) */
    int8_t   leaving_edge;
    bool     leaving_here;                  /* given where it is: visiting the receiver, it stays there (no swim out) */
    int8_t   peer_badge;                    /* the tank it is offered / going to: its badge (-1 = not known), for the card's words */
    double   leaving_since;
    uint32_t said_uid; uint8_t said_reason; double said_until;   /* the other tank's NO (LINK_GIVE_NO_*), on the card */
    struct {                                /* a gift offered to us: the question on our glass */
        bool on; uint32_t uid, nonce, peer; passport_t pp; int8_t badge; double since;
        int8_t answer;                      /* the keeper's tap: 0 = none yet, 1 = ACCEPT, -1 = NO THANKS */
        bool shown; double seen;            /* first drawn at this clock (render_gift): a press from before it is no answer (THE TOUCH RULE), not from the ask -
                                               a card that waited under a page is not half spent when it comes up (the 11-02 sweep) */
    } ask;
} gift_book_t;

void gift_attach(const void *tank, gift_book_t *b);   /* a tank_t and its book (the boards: once, at boot) */
bool gift_saved_received(uint32_t uid);   /* a fish this tank took in as a gift, by its RECEIVED note on the flash (the receipts fold) */
gift_book_t *gift_of(const void *tank);               /* NULL: this tank cannot give or take */
void gift_load(gift_book_t *b);                    /* at boot (a persisting book) */
void gift_clear(gift_book_t *b);                   /* the keeper's reset */
int  gift_state(const gift_book_t *b, uint32_t uid, uint32_t nonce, passport_t *pp);   /* LINK_GS_* */
bool gift_given_add(gift_book_t *b, uint32_t uid, uint32_t nonce, uint32_t peer, const passport_t *pp);   /* saved at once */
void gift_given_drop(gift_book_t *b, uint32_t uid, uint32_t nonce);
bool gift_arriving_add(gift_book_t *b, uint32_t uid, uint32_t nonce, uint32_t peer, const passport_t *pp);
void gift_arriving_drop(gift_book_t *b, uint32_t uid, uint32_t nonce);
bool gift_arriving_age(gift_book_t *b, float dt);   /* the ARRIVING notes held dt longer (saved every few minutes): true = one is past GIFT_ARRIVING_TTL_S */
void gift_received(gift_book_t *b, uint32_t uid, uint32_t nonce);   /* RECEIVED, its ARRIVING note dropped: one save */
bool gift_given_has(const gift_book_t *b, uint32_t uid);   /* a GIVEN note for this fish (any gift) */
/* A GIVEN FISH THAT COMES BACK (0.4.1): the fish is made here again (GIVEN_LOST, or a gift of it from a third tank) and
 * its GIVEN notes must go - but a power cut between the fish's save and the notes' drop left both on the flash, and the
 * boot (the note wins) took the fish out again: alive nowhere until those two tanks met. So the order is: every GIVEN
 * note of it marked BACK (with its latest passport) and saved, then the fish saved, then the notes dropped. A BACK note
 * is the fish: the boot keeps the fish it finds, or makes it from the note; the engine never hears of it; it counts no
 * place beside the fish. false = the mark is not on the flash (nothing changed): the fish must not be made yet */
bool gift_given_back(gift_book_t *b, uint32_t uid, const passport_t *pp);
bool gift_note_live(const gift_note_t *n);                 /* a GIVEN note still out (not BACK) */
bool gift_answer(gift_book_t *b, double press, double clock, bool yes);   /* the keeper's tap on the gift card, pressed at `press`, lifted
                                                           at `clock` (touch_gate.h READ AT THE PRESS): false = spent (a press from before the card, or no question) */

#endif
