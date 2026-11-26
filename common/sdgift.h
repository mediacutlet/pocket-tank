/* sdgift.h — SAND DOLLAR GIFTS (0.4.2, docs/release-0.4.2.md): a keeper sends sand dollars to the tank next door from
 * the LINK HUD (the coin under an open door). This is the book that makes a gift count EXACTLY ONCE: pure, no radio, no
 * save - the caller saves the book and the balance TOGETHER (one save_t write) after every change, and undoes the change
 * when that save fails (a copy taken before the call).
 *
 * THE RULES (the selftest's promises):
 *   - the giver takes the money and writes its OUT note in one save, before the first frame goes; the note is its gift in
 *     flight (one at a time), sent again and again while the door to that tank is open - across a reboot, a sleep, a
 *     closed door - until the answer comes;
 *   - the receiver decides a gift ONCE: credited or refused, with its verdict in its SEEN book in the same save as the
 *     money, and answers that verdict to every copy of the same gift (a resend, a duplicate frame) - never a second credit;
 *   - the giver gets its money back ONLY on a refusal (the receiver kept nothing); on OK the note goes, the money stays gone.
 * Nonces count up per giving tank (sdg_book_t.seq, floored at the clock's seconds), so an old copy that comes late after
 * a newer gift is not a new gift.
 * The receiver remembers the last gift of SDG_SEEN_N tanks (the least recent one forgotten first). */
#ifndef POCKET_TANK_SDGIFT_H
#define POCKET_TANK_SDGIFT_H
#include <stdbool.h>
#include <stdint.h>
#include "tank.h"

#define SDG_MAX_GIFT     999        /* the dial's three digits */
#define SDG_BALANCE_CAP  99999      /* a gift that would take the receiver past this is refused (SDG_V_FULL) */
#define SDG_GIVE_BACK_H  24         /* a gift no tank has answered this many hours (by the clock) comes back (Strato 11-08) */
enum { SDG_V_NONE, SDG_V_OK, SDG_V_FULL, SDG_V_BAD, SDG_V_OLD };   /* a gift's verdict, as the frames carry it (NONE: no answer;
                                                          OLD: older than the last gift decided from that tank - a giver restored
                                                          from an older save: it comes back, the trade of the day rule) */
enum { SDG_SENT = 0, SDG_E_BUSY, SDG_E_FUNDS, SDG_E_BAD };   /* sdg_out_begin */

/* the giver: take `amount` from *bal and write the OUT note (to `peer`); *nonce = the gift's. SDG_E_BUSY: a gift is still
   in flight; SDG_E_FUNDS: more than *bal; SDG_E_BAD: 0, past SDG_MAX_GIFT, or no peer */
int  sdg_out_begin(sdg_book_t *b, int32_t *bal, uint32_t peer, int amount, uint32_t floor, uint32_t *nonce);
/* floor: the clock in seconds (0 = none): a nonce is never below it, so a tank brought back from an older save (the
   backup page) does not hand out numbers its neighbor has already decided - the count alone would repeat them */
bool sdg_out_pending(const sdg_book_t *b);
/* NOTHING IS LOST (Strato 11-08: "back after a day"): a gift in flight is stamped with the clock's hour (out.verdict +
   out.age: the hour's low 16 bits; 0 = not stamped yet - no clock when it left - and stamped at the first look that has
   one). Once SDG_GIVE_BACK_H have passed with no answer, the note goes and the money comes back: the return value
   (0 = nothing yet). The caller calls it only while the door to that tank is NOT open (open: the answer is ms away) and
   tells the engine to stop sending (link_sd_forget). The trade, chosen: if that tank DID take it as the link dropped
   and is never met again, both keep it - a rare double, never a loss */
int  sdg_out_expire(sdg_book_t *b, int32_t *bal, uint32_t now_unix);
/* THE CLOCK WAS MOVED under the gift (the first time sync: main.c net_clock_rebase - 0.4.2 audit r2, PW2-5): the hour it left
   is on the old clock, and the jump would read as "a day passed" (back at once, a minute after giving). The stamp is cleared:
   the next look that has a clock stamps it again, so the day starts over on the new clock - longer, never shorter.
   true = a stamp was cleared (the caller saves) */
bool sdg_out_restamp(sdg_book_t *b);
int  sdg_out_amount(const sdg_book_t *b);   /* the gift in flight's amount (0: none) - a reset or a REMOVE gives it back */
/* the giver, on the answer: the note goes. Returns the money given back (> 0: refused), 0 (taken), -1 (not the note's:
   ignore - a stale or stranger answer) */
int  sdg_out_settle(sdg_book_t *b, int32_t *bal, uint32_t peer, uint32_t nonce, int verdict);
/* the receiver: a gift from `peer`. Returns the verdict to answer (SDG_V_NONE: answer nothing - a zero id; SDG_V_OLD: older
   than the last one decided, nothing kept - a late copy's answer finds no note and is ignored, a restored giver's note comes back);
   *fresh = it was decided just now (the caller saves, then answers; a credit shows) */
int  sdg_in(sdg_book_t *b, int32_t *bal, uint32_t peer, uint32_t nonce, int amount, bool *fresh);

#endif
