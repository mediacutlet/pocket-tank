/* sdgift.c — SAND DOLLAR GIFTS' book (sdgift.h has the rules) */
#include <string.h>
#include "sdgift.h"

bool sdg_out_pending(const sdg_book_t *b) { return b->out.nonce != 0; }

int sdg_out_begin(sdg_book_t *b, int32_t *bal, uint32_t peer, int amount, uint32_t floor, uint32_t *nonce) {
    if (sdg_out_pending(b)) return SDG_E_BUSY;
    if (!peer || amount < 1 || amount > SDG_MAX_GIFT) return SDG_E_BAD;
    if (*bal < amount) return SDG_E_FUNDS;
    uint32_t n = b->seq + 1;
    if (floor && (int32_t)(floor - n) > 0) n = floor;   /* (the clock ahead of the count: a restored older save, sdgift.h) */
    if (!n) n = 1;                            /* (never 0: 0 is "no note") */
    b->seq = n;
    memset(&b->out, 0, sizeof b->out);
    b->out.peer = peer; b->out.nonce = n; b->out.amount = (uint16_t)amount;
    if (floor) { uint16_t h = (uint16_t)(floor / 3600u); if (!h) h = 1; b->out.verdict = (uint8_t)h; b->out.age = (uint8_t)(h >> 8); }   /* (the hour it left) */
    *bal -= amount;
    if (nonce) *nonce = n;
    return SDG_SENT;
}

int sdg_out_settle(sdg_book_t *b, int32_t *bal, uint32_t peer, uint32_t nonce, int verdict) {
    if (!sdg_out_pending(b) || b->out.peer != peer || b->out.nonce != nonce || verdict == SDG_V_NONE) return -1;
    int back = verdict == SDG_V_OK ? 0 : b->out.amount;   /* refused: nothing was kept there - it comes back */
    *bal += back;
    memset(&b->out, 0, sizeof b->out);
    return back;
}

int sdg_in(sdg_book_t *b, int32_t *bal, uint32_t peer, uint32_t nonce, int amount, bool *fresh) {
    *fresh = false;
    if (!peer || !nonce) return SDG_V_NONE;
    sdg_entry_t *e = NULL;
    for (int i = 0; i < SDG_SEEN_N && !e; i++) if (b->seen[i].peer == peer) e = &b->seen[i];
    if (e) {
        if (e->nonce == nonce) return e->verdict;            /* a copy of a gift decided before: the same answer */
        if ((int32_t)(nonce - e->nonce) < 0) return SDG_V_OLD;    /* an older gift than the last one decided: nothing here (the review's 3) */
    } else {                                                 /* a tank not in the book: a free line, else the least recent */
        for (int i = 0; i < SDG_SEEN_N; i++)
            if (!e || (e->peer && (!b->seen[i].peer || b->seen[i].age > e->age))) e = &b->seen[i];
    }
    int v = amount < 1 || amount > SDG_MAX_GIFT ? SDG_V_BAD : *bal > SDG_BALANCE_CAP - amount ? SDG_V_FULL : SDG_V_OK;
    if (v == SDG_V_OK) *bal += amount;
    for (int i = 0; i < SDG_SEEN_N; i++) if (b->seen[i].peer && b->seen[i].age < 255) b->seen[i].age++;
    e->peer = peer; e->nonce = nonce; e->amount = (uint16_t)(amount < 0 ? 0 : amount > 65535 ? 65535 : amount); e->verdict = (uint8_t)v; e->age = 0;
    *fresh = true;
    return v;
}

bool sdg_out_restamp(sdg_book_t *b) {
    if (!sdg_out_pending(b) || !(b->out.verdict | b->out.age)) return false;
    b->out.verdict = 0; b->out.age = 0;       /* (0 = not stamped: sdg_out_expire stamps it at its next look) */
    return true;
}
int sdg_out_amount(const sdg_book_t *b) { return sdg_out_pending(b) ? b->out.amount : 0; }
int sdg_out_expire(sdg_book_t *b, int32_t *bal, uint32_t now_unix) {
    if (!sdg_out_pending(b) || !now_unix) return 0;
    uint16_t now_h = (uint16_t)(now_unix / 3600u); if (!now_h) now_h = 1;
    uint16_t at = (uint16_t)(b->out.verdict | b->out.age << 8);
    if (!at) { b->out.verdict = (uint8_t)now_h; b->out.age = (uint8_t)(now_h >> 8); return 0; }   /* (stamped now: the day starts here) */
    if ((uint16_t)(now_h - at) < SDG_GIVE_BACK_H) return 0;
    if ((uint16_t)(now_h - at) > 0x8000u) { b->out.verdict = (uint8_t)now_h; b->out.age = (uint8_t)(now_h >> 8); return 0; }   /* (the clock went back:
                                                                   the day starts again, never a give-back on a bad clock) */
    int back = b->out.amount;
    *bal += back;
    memset(&b->out, 0, sizeof b->out);
    return back;
}
