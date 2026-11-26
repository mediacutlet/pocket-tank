/* swap.h — FISH SWAP (0.4.3, docs/fish-swap-0.4.3.md): one fish each way between two linked tanks, as one trade.
 *
 * The protocol lives here, pure: no fish_t, no drawing. The tank (tank_link.c) gives it a swap_host_t (is this fish
 * free to go, its passport, take it out, bring one in) and the link engine carries its frames (link_swap_send,
 * LEV_SWAP). The sim's --selftest-swap drives two of these over a hostile radio with crashes anywhere.
 *
 * Names: I starts the swap (its fish a), R is next door (its fish b).
 *
 * THE RULES (the selftest's promises):
 *   - nothing is saved before I's keeper's last SWAP: before it everything lives in RAM, and a reboot, a closed door,
 *     a NO THANKS or a CALL OFF leaves both tanks as they were;
 *   - I alone decides. It saves its NOTE (a's and b's passports, COMMITTED) before a leaves, and then a leaves (a
 *     place stays held for b: progression's fish_held counts the note);
 *   - R applies only a COMMIT for a pick it still holds in RAM, and saves its own NOTE (DONE) before b leaves and a
 *     comes in. Anything else is answered ABORT (presumed abort: R is never stuck waiting) - and a comes back to I;
 *   - every note change is saved BEFORE the fish change it stands for; a boot finishes what a note says was started;
 *   - after DONE, I brings b in (RECEIVING saved first), the note goes, CLOSED. R's note goes on CLOSED, so a late
 *     COMMIT gets DONE and a DONE I has no note of gets CLOSED (audit r1, LK-3) - or, its partner never back,
 *     SWAP_R_KEEP_S apart (a month), or for room after SWAP_R_SPARE_S apart (a week; audit r2, R2-1). Asked later about a
 *     trade it let go by time, a tank answers NOT_HERE (below);
 *   - the book holds SWAP_NOTES notes, each its tank's: a waiting note, or another tank's (MY TANKS), blocks nothing
 *     (LK-2 / LK-4R / LK-10). A note of a tank this board no longer holds goes at the boot (R2-L28);
 *   - the one case no single tank can settle (the link gone in the moment after R's DONE, the two never linked
 *     again): after a day apart I's a comes back (Strato 11-21: a rare double until they meet), and at the next meeting the
 *     trade finishes (Strato 11-23, GAVE-UP / TAKING);
 *   - a tank asked about a trade whose record it let go by time answers NOT_HERE (SWAP_ANS_NOT_HERE): the asker then
 *     keeps the fish it holds the only copy of and brings it in at a place (audit r3, R3-1). Each meeting restarts both
 *     records' days. So the trade finishes if the tanks meet within a month apart (I's GAVE-UP lasts SWAP_GAVEUP_S, 32
 *     days). Past that both records end by time and b is lost: in neither tank, a in both (audit r3b, R3b-1: Strato
 *     11-23 accepts it as the end). A tank remembers only the last SWAP_ENDED (8) trades it let go by time: 8 more
 *     ended on it before the partner is back, and the partner gets the plain answer and b is lost too (R3b-L2).
 *     THE DAY APART (audit r2,
 *     R2-2): a tank with a set clock counts the wall clock, sleep and power-off included; awake, only the tank's own
 *     running time (a clock set or a bad read mid-run counts nothing, PW-7); with no clock, its awake time (LK-4). It
 *     ends only SWAP_BOOT_GRACE_MS into a boot with the partner not linked (PW-6: a boot beside it links first). */
#ifndef POCKET_TANK_SWAP_H
#define POCKET_TANK_SWAP_H

#include <stdbool.h>
#include <stdint.h>
#include "passport.h"

struct link;
#define SWAP_NVS_KEY       "swap"
#define SWAP_NOTES         4        /* notes the book holds, any tanks' (0.4.3 audit r1: one slot blocked every swap while a note waited) */
#define SWAP_ASK_MS        90000    /* R's keeper: pick one and say SWAP (Strato 11-21: 90 s) */
#define SWAP_LAST_MS       60000    /* I's keeper: the last yes */
#define SWAP_PICK_TTL_MS   (SWAP_LAST_MS + SWAP_LEAVE_MS + 20000)   /* R holds a pick this long: past I's last chance to commit */
#define SWAP_RESEND_MS     500
#define SWAP_HEARD_MS      3000     /* the other tank silent this long while a swaps out: the swap is off, it turns back */
#define SWAP_COMMIT_MS     1000     /* ... and I commits only with it heard this lately */
#define SWAP_LEAVE_MS      25000    /* a swims to its door; past this, bubbles carry it */
#define SWAP_WAIT_S        (24 * 3600)       /* the hard case: I's COMMITTED note, a day apart (the day: see the top) */
#define SWAP_R_SPARE_S     (7 * 24 * 3600)   /* R's applied note gives its room to a new trade a week apart (audit r2, R2-1) */
#define SWAP_R_KEEP_S      (30 * 24 * 3600)  /* ... and, its partner never back, goes a month apart. Long: an I off or shelved is not
                                                counting its own day (review r2: a week let a boot beside R after 8 days off double a) */
#define SWAP_BOOT_GRACE_MS 60000             /* a note ends only this long into a boot with its partner not linked (PW-6) */
#define SWAP_SPAN_MAX_S    (SWAP_R_SPARE_S - 3600)   /* the most one boot's (or nap's) wall-clock span counts apart (audit r3, R3-L15) */
#define SWAP_GAVEUP_S      (SWAP_WAIT_S + SWAP_R_KEEP_S + 24 * 3600)   /* I's GAVE-UP note: past any R note's month, it goes (Q1) */
#define SWAP_SAID_S        6.0f

/* the frames' kinds (FR_SWAP's first byte) */
enum { SWK_OFFER = 1, SWK_WITHDRAW, SWK_PICK, SWK_NO, SWK_GO, SWK_COMMIT, SWK_DONE, SWK_ABORT, SWK_CLOSED, SWK_N };
/* why a swap ended (SWK_NO's reason, and what the glass says) */
enum { SWAP_NO_THANKS, SWAP_NO_ANSWER, SWAP_NO_CALLED_OFF, SWAP_NO_BUSY, SWAP_NO_CANNOT, SWAP_NO_FIRST, SWAP_NO_N };
/* what the glass shows when a swap ends (swap_book_t.said) */
enum { SWAP_SAID_NONE, SWAP_SAID_SWAPPED, SWAP_SAID_THANKS, SWAP_SAID_ANSWER, SWAP_SAID_CALLED_OFF, SWAP_SAID_BUSY,
       SWAP_SAID_NOT_HAPPENED, SWAP_SAID_FIRST, SWAP_SAID_NOT_FINISHED,
       SWAP_SAID_FULL };              /* (audit r3, R3-1: the trade waits for a place here - THE SWAP DID NOT FINISH / THIS TANK IS FULL) */
/* the note */
enum { SWN_NONE, SWN_COMMITTED, SWN_RECEIVING, SWN_BACK, SWN_DONE,
       SWN_GAVEUP,                  /* I: the hard case's day came, a is home - kept so R's DONE at the next meeting FINISHES the trade (Q1) */
       SWN_TAKING };                /* I: ... that DONE came: a goes, b comes in (saved first), then the note goes */
enum { SWAP_ROLE_I = 1, SWAP_ROLE_R = 2 };
#define SWAP_GAVE_ONLY     2         /* swap_note_t.gave: b's only copy (R3-1) */
/* the answer to a trade a tank has no note of (CLOSED's and ABORT's reason): PLAIN as before; NOT_HERE = "its record ended here by
   time" (swap_book_t.ended) - the asker keeps the fish it holds the only copy of, never drops it (audit r3, R3-1). A 0.4.3 tank
   before it reads PLAIN */
enum { SWAP_ANS_PLAIN = 0, SWAP_ANS_NOT_HERE = 1 };
/* I's side, in RAM */
enum { SO_OFF, SO_ASKING, SO_PICKED, SO_LEAVING, SO_COMMITTED };
/* R's side, in RAM */
enum { SI_OFF, SI_ASKED, SI_WAITING, SI_GOING };

typedef struct {
    uint8_t  st, role, applied;      /* applied: the fish change it stands for is saved (a boot does it again only if not) */
    uint8_t  gave;                   /* I BACK: the hard case's (a home, then GAVE-UP), not an ABORT's. SWAP_GAVE_ONLY (I GAVE-UP or R DONE): the
                                        other tank has no record of the trade and no b - this note holds b's only copy: b comes in here at a
                                        place, and the note never ends by time (audit r3, R3-1) */
    uint32_t sid, peer, tank;        /* tank: whose note (MY TANKS: a note of a tank not on the glass waits for it) */
    uint32_t wait_s;                 /* seconds apart: `peer` not linked (the day: see the top; saved every 10 minutes and at a sleep) */
    uint32_t wall_at;                /* the wall clock (unix s) wait_s was counted to (0: never a clock) - a boot counts the span since */
    passport_t out, in;              /* the fish that leaves here, the one that comes */
    uint32_t sent;                   /* (RAM) its last resend */
    uint32_t apart_ms;               /* (RAM) this boot, ms with `peer` not linked (the grace) */
    double   part;                   /* (RAM) the seconds not yet in wait_s */
    uint8_t  live;                   /* (RAM) counted this boot (0: its first tick counts the span since wall_at) */
    uint8_t  told;                   /* (RAM) THIS TANK IS FULL said this boot (once) */
} swap_note_t;

typedef struct {
    void *ctx;
    bool (*can_go)(void *ctx, uint32_t uid, uint32_t peer);     /* ours, alive, free to part (no sale, gift, bowl), and home or visiting
                                                                   `peer` (Strato 11-22: a fish visiting the partner can be swapped) -
                                                                   never one visiting a third tank */
    bool (*settled)(void *ctx, uint32_t uid, uint32_t peer);    /* ... and not in a doorway now: home, or a settled visit at `peer` (the
                                                                   trade's moment: I commits, R applies, only on this) */
    bool (*passport)(void *ctx, uint32_t uid, passport_t *pp);  /* that fish as it is now */
    bool (*out)(void *ctx, uint32_t uid, bool bubbles);         /* it leaves, saved (not here: true). false = not saved, nothing changed */
    bool (*in)(void *ctx, const passport_t *pp, int edge);      /* it comes alive here, saved (already here: true); edge -1 = no door */
    bool (*at_door)(void *ctx, uint32_t uid);                   /* I: a has swum through its door */
    bool (*near_door)(void *ctx, uint32_t uid);                 /* R: b waits at its door (it goes without bubbles) */
    uint32_t (*hour)(void *ctx);                                /* the clock in hours (0: no clock) */
    int64_t (*wall)(void *ctx);                                 /* the wall clock, unix s (0: not set). NULL: none */
    bool (*tank_gone)(void *ctx, uint32_t tank);                /* that tank is on no shelf of this board and not on the glass (sure). NULL: never */
    int  (*home_now)(void *ctx, uint32_t uid);                  /* 1 home and settled here, 0 not ours here (sold, given, gone), -1 ours but
                                                                   not home just now (next door, at a door, a fry not home yet) */
    bool (*room)(void *ctx);                                    /* a place for one more fish (held places counted) */
} swap_host_t;

typedef struct { uint32_t sid, peer; uint8_t role; } swap_ended_t;   /* a trade whose record this book let go by time (R3-1) */
#define SWAP_ENDED 8
typedef struct swap_book {
    /* saved */
    swap_note_t notes[SWAP_NOTES];   /* every open note, each its tank's (MY TANKS: another tank's waits for it, blocking nothing). An I note
                                        is dropped when its swap ends; an R note on CLOSED, or its partner gone a month, or a week for room (R2-1) */
    uint32_t seq;
    swap_ended_t ended[SWAP_ENDED];  /* the last trades whose record ended by time here (I's GAVE-UP days, R's month or a week's room),
                                        newest last - asked about one, this tank answers NOT_HERE: the asker keeps b's only copy. Any other
                                        trade it has no record of gets the plain answer (audit r3, R3-1). In the blob's spare list.
                                        Only 8: an older one falls off, and its partner's b is lost (R3b-L2) */
    uint8_t  n_ended;
    bool     persist;                /* false: a selftest's book - its "flash" is mem[] below */
    uint8_t  mem[1300]; int mem_len;  /* (a selftest's flash) */
    bool     fail_save;              /* (a selftest: the flash refuses) */
    /* I's side (RAM) */
    struct { uint8_t st; uint32_t sid, peer; passport_t a, b; uint32_t t0, sent; int8_t badge; } out;
    /* R's side (RAM) */
    struct { uint8_t st; uint32_t sid, from; passport_t a, b; uint32_t t0, sent; int8_t badge; bool shown; double seen; } in;
    /* the glass's taps (render.c sets them, swap_tick acts) */
    uint32_t want_uid, want_peer;    /* I: SWAP on the GIVE card */
    int8_t   i_answer;               /* I: the last yes (1) or NO THANKS (-1) */
    bool     i_cancel, r_cancel;     /* CALL OFF */
    int8_t   r_answer;               /* R: NO THANKS (-1) */
    uint32_t r_pick;                 /* R: SWAP on the confirm, with this fish */
    /* what the glass says when it ends */
    struct { uint8_t what; bool has_mine, has_theirs; passport_t mine, theirs; int8_t badge; double at; } said;
    uint32_t door_peer[2]; bool door_swap[2], door_full[2];   /* (tank_link, each tick) who is at each door, takes swaps, is full: the GIVE card's SWAP */
    uint32_t now;                    /* the last tick's ms (the glass reads the bars from it) */
    uint32_t now_last;               /* (RAM) the last tick's ms */
    double   clock_last;             /* (RAM) the last tick's tank clock: the wait counts it awake, never the link's ms */
    int8_t   peer_edge;              /* the door the swap goes through (I: to out.peer; R: to in.from), -1 none */
    uint32_t no_from, no_sid; uint8_t no_reason;   /* R's last NO (RAM): a repeat of that OFFER is answered it again */
    uint32_t away_peer[8];           /* (tank_link, each tick) the tank each of our fish is visiting, 0 = home: the cards' VISITING */
    uint32_t steer_uid;              /* (tank_link) the fish it is steering to the door now */
} swap_book_t;

void swap_attach(const void *tank, swap_book_t *b);
swap_book_t *swap_of(const void *tank);
bool swap_load(swap_book_t *b);                     /* the note from the flash (the boards: at boot) */
bool swap_save(swap_book_t *b);
void swap_park(swap_book_t *b);                     /* on the way to a sleep or a power-off: the notes' waits saved (R2-2) */
void swap_rebase(int64_t delta, int64_t now_unix);  /* the first time sync moved the clock: every note's stamp with it (nothing lived) */
/* a boot: finish what the note says was started, for this tank (my_tank); a note of another tank waits */
void swap_boot(swap_book_t *b, const swap_host_t *h, uint32_t my_tank);
/* the keeper's reset of the tank my_tank (its ID before the reset; 0 = ERASE EVERYTHING, any note): its note goes, the RAM sides end */
void swap_reset(swap_book_t *b, uint32_t my_tank);
void swap_tick(swap_book_t *b, struct link *L, const swap_host_t *h, uint32_t my_tank, uint32_t now_ms, double clock);
void swap_rx(swap_book_t *b, struct link *L, const swap_host_t *h, uint32_t my_tank, uint32_t from, int kind, uint32_t sid,
             uint32_t uid, int reason, const passport_t *pp, uint32_t now_ms, double clock);
/* the link to that tank went (a door closed, a sleep): RAM sides with it end; a note waits for the next link */
void swap_unlinked(swap_book_t *b, uint32_t peer, double clock);

bool swap_busy(const swap_book_t *b);                       /* a swap asked, picked or leaving, or no room for its note: no second one */
const swap_note_t *swap_note_of(const swap_book_t *b, uint32_t tank, int k);   /* that tank's k-th open note (NULL: none) - the tests */
bool swap_holds(const swap_book_t *b, uint32_t uid);        /* this fish is in a swap now (no SELL, no GIVE, no bowl) */
int  swap_held_places(const swap_book_t *b, uint32_t my_tank, bool (*here)(const void *t, uint32_t uid), const void *t);   /* places a note holds */
bool swap_noted(const swap_book_t *b, uint32_t uid, uint32_t tank);   /* a note of that tank will still move this fish (the bowl's LET IN waits) */
void swap_forget_tank(uint32_t tank);                       /* MY TANKS' REMOVE: that tank's notes go, in every attached book */
uint32_t swap_leaving_uid(const swap_book_t *b);            /* the fish swimming to its door for a swap (I's a, R's b), 0 none */
void swap_said(swap_book_t *b, int what, const passport_t *mine, const passport_t *theirs, double clock);

/* the selftest: a crash - RAM lost, the book back as its flash has it */
void swap_crash(swap_book_t *b);
/* the selftest's planted bugs, each must fail the soak: 1 = I takes a out before its note is saved; 2 = R drops its note
   at DONE instead of at CLOSED (a late COMMIT then gets ABORT: audit r1 LK-3); 3 = R's ABORT without first sending the
   partner's visiting fish to bubbles (selftest 3o: it swims in both for a moment) */
extern int swap_test_plant;

#endif
