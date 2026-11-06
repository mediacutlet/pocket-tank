/* gift.c — GIVE's notes (gift.h). */
#include <string.h>
#include <stdlib.h>
#include "gift.h"
#include "progression.h"
#include "touch_gate.h"

/* the blob: magic, version, the three counts, the record size; then the
   GIVEN notes, the ARRIVING notes (uid, nonce, peer, the passport's length
   and bytes in a fixed GIFT_REC), then the RECEIVED pairs */
#define GIFT_MAGIC 0x4647u                  /* "GF" */
#define GIFT_PP    128                      /* a passport's room in a record (v1 is 94 bytes) */
#define GIFT_REC_V1 (12 + 1 + GIFT_PP)
#define GIFT_REC   (GIFT_REC_V1 + 4)           /* v2 (11-02 sweep): + the ARRIVING note's held seconds - an older build reads past it */
#define GIFT_BACK  0x4b434142u                 /* "BACK" in that word of a GIVEN note (0.4.1: its held time was never used) */
_Static_assert(8 + 2 * GIFT_NOTES * GIFT_REC + GIFT_RECEIVED * 8 == GIFT_BLOB_MAX, "GIFT_BLOB_MAX (gift.h) is the full blob");

static struct { const void *t; gift_book_t *b; } s_att[4];
void gift_attach(const void *tank, gift_book_t *b) {
    int w = -1;
    for (int i = 0; i < 4; i++) { if (s_att[i].t == tank) { w = i; break; } if (w < 0 && !s_att[i].t) w = i; }
    if (w < 0) w = 0;
    s_att[w].t = b ? tank : NULL; s_att[w].b = b;
}
gift_book_t *gift_of(const void *tank) {
    for (int i = 0; i < 4; i++) if (s_att[i].t == tank && tank) return s_att[i].b;
    return NULL;
}
static void put32(uint8_t *b, int o, uint32_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); b[o + 2] = (uint8_t)(v >> 16); b[o + 3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *b, int o) { return b[o] | (uint32_t)b[o + 1] << 8 | (uint32_t)b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }

static bool save(const gift_book_t *b) {   /* false: not on the flash */
    if (!b->persist) return true;
    size_t len = 8 + (size_t)(b->n_given + b->n_arr) * GIFT_REC + (size_t)b->n_recv * 8;
    uint8_t *buf = malloc(len);
    if (!buf) return false;
    memset(buf, 0, len);
    buf[0] = GIFT_MAGIC & 0xff; buf[1] = GIFT_MAGIC >> 8; buf[2] = 2;
    buf[3] = b->n_given; buf[4] = b->n_arr; buf[5] = b->n_recv; buf[6] = GIFT_REC & 0xff; buf[7] = GIFT_REC >> 8;
    uint8_t *p = buf + 8;
    for (int list = 0; list < 2; list++) {
        const gift_note_t *n = list ? b->arriving : b->given; int k = list ? b->n_arr : b->n_given;
        for (int i = 0; i < k; i++, p += GIFT_REC) {
            put32(p, 0, n[i].uid); put32(p, 4, n[i].nonce); put32(p, 8, n[i].peer);
            int w = passport_write(&n[i].pp, p + 13, GIFT_PP); p[12] = (uint8_t)(w > 0 ? w : 0);
            put32(p, GIFT_REC_V1, list ? (uint32_t)(n[i].held > 0 ? n[i].held : 0) : n[i].back ? GIFT_BACK : 0);
        }
    }
    for (int i = 0; i < b->n_recv; i++, p += 8) { put32(p, 0, b->recv[i].uid); put32(p, 4, b->recv[i].nonce); }
    bool ok = persist_port_blob_save(GIFT_NVS_KEY, buf, len);
    free(buf);
    return ok;
}
void gift_load(gift_book_t *b) {
    if (!b) return;
    b->n_given = b->n_arr = b->n_recv = 0;
    if (!b->persist) return;
    const size_t cap = 8 + 2 * GIFT_NOTES * 256 + GIFT_RECEIVED * 8;
    uint8_t *buf = malloc(cap); size_t got = 0;
    if (!buf) return;
    if (persist_port_blob_load(GIFT_NVS_KEY, buf, cap, &got) && got >= 8 && (buf[0] | buf[1] << 8) == GIFT_MAGIC) {
        int ng = buf[3], na = buf[4], nr = buf[5], rec = buf[6] | buf[7] << 8;
        const uint8_t *p = buf + 8, *end = buf + got;
        if (rec >= GIFT_REC_V1) {                     /* (v1 records: no held time - held from this boot) */
            for (int list = 0; list < 2; list++) {
                int k = list ? na : ng;
                for (int i = 0; i < k && p + rec <= end; i++, p += rec) {
                    gift_note_t n; memset(&n, 0, sizeof n);
                    n.uid = get32(p, 0); n.nonce = get32(p, 4); n.peer = get32(p, 8);
                    int w = p[12] <= GIFT_PP ? p[12] : 0;
                    if (!n.uid || !w || !passport_read(&n.pp, p + 13, w)) continue;   /* (a note we cannot read: it was never there) */
                    if (rec >= GIFT_REC && list) n.held = (float)get32(p, GIFT_REC_V1);
                    if (rec >= GIFT_REC && !list) n.back = get32(p, GIFT_REC_V1) == GIFT_BACK;
                    if (list && b->n_arr < GIFT_NOTES) b->arriving[b->n_arr++] = n;
                    else if (!list && b->n_given < GIFT_NOTES) b->given[b->n_given++] = n;
                }
            }
            for (int i = 0; i < nr && p + 8 <= end && b->n_recv < GIFT_RECEIVED; i++, p += 8) {
                b->recv[b->n_recv].uid = get32(p, 0); b->recv[b->n_recv].nonce = get32(p, 4); b->n_recv++;
            }
        }
    }
    free(buf);
}
void gift_clear(gift_book_t *b) {
    if (!b) return;
    bool persist = b->persist;
    memset(b, 0, sizeof *b);
    b->persist = persist;
    save(b);
}
static int find(const gift_note_t *n, int k, uint32_t uid, uint32_t nonce) {
    for (int i = 0; i < k; i++) if (n[i].uid == uid && (!nonce || n[i].nonce == nonce)) return i;
    return -1;
}
int gift_state(const gift_book_t *b, uint32_t uid, uint32_t nonce, passport_t *pp) {
    if (!b) return LINK_GS_NONE;
    int i;
    if ((i = find(b->given, b->n_given, uid, nonce)) >= 0 && !b->given[i].back) { if (pp) *pp = b->given[i].pp; return LINK_GS_GIVEN; }   /* (BACK: the
                                                       fish is here - a TAKE for it is answered CANCEL, never GIVEN) */
    if ((i = find(b->arriving, b->n_arr, uid, nonce)) >= 0) { if (pp) *pp = b->arriving[i].pp; return LINK_GS_ARRIVING; }
    for (i = 0; i < b->n_recv; i++) if (b->recv[i].uid == uid && b->recv[i].nonce == nonce) return LINK_GS_ALIVE;
    return LINK_GS_NONE;
}
bool gift_given_has(const gift_book_t *b, uint32_t uid) { return b && find(b->given, b->n_given, uid, 0) >= 0; }
bool gift_note_live(const gift_note_t *n) { return n && !n->back; }
bool gift_given_back(gift_book_t *b, uint32_t uid, const passport_t *pp) {
    if (!b) return false;
    bool was[GIFT_NOTES], any = false;
    passport_t ppw[GIFT_NOTES];
    for (int i = 0; i < b->n_given; i++) {
        was[i] = b->given[i].back; ppw[i] = b->given[i].pp;
        if (b->given[i].uid == uid) { any = true; b->given[i].back = true; if (pp) b->given[i].pp = *pp; }
    }
    if (!any || save(b)) return true;
    for (int i = 0; i < b->n_given; i++) { b->given[i].back = was[i]; b->given[i].pp = ppw[i]; }   /* (not on the flash: as it was) */
    return false;
}
/* a note counts only once it is on the flash: GIVEN before the fish leaves, ARRIVING before the yes. A save that
   fails (the flash full, saving off after a bad load) takes the note back out, so nobody acts on it (2026-11-02) */
static bool add(gift_book_t *b, gift_note_t *n, uint8_t *k, uint32_t uid, uint32_t nonce, uint32_t peer, const passport_t *pp) {
    if (!b) return false;
    int i = find(n, *k, uid, nonce);
    const uint8_t k0 = *k;
    if (i < 0) { if (*k >= GIFT_NOTES) return false; i = (*k)++; n[i].held = 0; n[i].back = false; }   /* (a new note: held from now) */
    const gift_note_t was = n[i];
    n[i].uid = uid; n[i].nonce = nonce; n[i].peer = peer; n[i].pp = *pp; n[i].back = false;
    if (save(b)) return true;
    n[i] = was; *k = k0;
    return false;
}
static void drop(gift_book_t *b, gift_note_t *n, uint8_t *k, uint32_t uid, uint32_t nonce, bool write) {
    if (!b) return;
    int i = find(n, *k, uid, nonce);
    if (i < 0) return;
    n[i] = n[--*k];
    if (write) save(b);
}
bool gift_given_add(gift_book_t *b, uint32_t uid, uint32_t nonce, uint32_t peer, const passport_t *pp) { return b && add(b, b->given, &b->n_given, uid, nonce, peer, pp); }
void gift_given_drop(gift_book_t *b, uint32_t uid, uint32_t nonce) { if (b) drop(b, b->given, &b->n_given, uid, nonce, true); }
bool gift_arriving_add(gift_book_t *b, uint32_t uid, uint32_t nonce, uint32_t peer, const passport_t *pp) { return b && add(b, b->arriving, &b->n_arr, uid, nonce, peer, pp); }
void gift_arriving_drop(gift_book_t *b, uint32_t uid, uint32_t nonce) { if (b) drop(b, b->arriving, &b->n_arr, uid, nonce, true); }
bool gift_arriving_age(gift_book_t *b, float dt) {
    if (!b || !b->n_arr || dt <= 0) return false;
    bool past = false;
    for (int i = 0; i < b->n_arr; i++) { b->arriving[i].held += dt; past |= b->arriving[i].held >= GIFT_ARRIVING_TTL_S; }
    b->arr_unsaved += dt;
    if (b->arr_unsaved >= 300.0f) { b->arr_unsaved = 0; save(b); }   /* (a power cut forgets at most five minutes of it) */
    return past;
}
void gift_received(gift_book_t *b, uint32_t uid, uint32_t nonce) {
    if (!b) return;
    bool have = false;
    for (int i = 0; i < b->n_recv; i++) have |= b->recv[i].uid == uid && b->recv[i].nonce == nonce;
    if (!have) {
        if (b->n_recv == GIFT_RECEIVED) { memmove(b->recv, b->recv + 1, (GIFT_RECEIVED - 1) * sizeof b->recv[0]); b->n_recv--; }
        b->recv[b->n_recv].uid = uid; b->recv[b->n_recv].nonce = nonce; b->n_recv++;
    }
    drop(b, b->arriving, &b->n_arr, uid, nonce, false);
    save(b);
}
/* one of the gifts this tank took in, by its RECEIVED note on the flash - the last GIFT_RECEIVED kept, as 0.4.0 kept
   them: what a save from 0.4.0 has that tells a gift from a fish of its own (the receipts fold, round 2 review F1). Read
   from the flash, not a book: the fold runs at the load, before gift_boot */
bool gift_saved_received(uint32_t uid) {
    if (!uid) return false;
    const size_t cap = 8 + 2 * GIFT_NOTES * 256 + GIFT_RECEIVED * 8;
    uint8_t *buf = malloc(cap); size_t got = 0; bool hit = false;
    if (!buf) return false;
    if (persist_port_blob_load(GIFT_NVS_KEY, buf, cap, &got) && got >= 8 && (buf[0] | buf[1] << 8) == GIFT_MAGIC) {
        int ng = buf[3], na = buf[4], nr = buf[5], rec = buf[6] | buf[7] << 8;
        if (rec >= GIFT_REC_V1) {
            const uint8_t *p = buf + 8 + (size_t)(ng + na) * (size_t)rec, *end = buf + got;
            for (int i = 0; i < nr && p + 8 <= end; i++, p += 8) hit |= get32(p, 0) == uid;
        }
    }
    free(buf);
    return hit;
}
bool gift_answer(gift_book_t *b, double press, double clock, bool yes) {
    if (!b || !b->ask.on || b->ask.answer || !b->ask.shown) return false;   /* (not yet seen: no tap of its) */
    if (b->ask.seen > clock) b->ask.seen = clock;   /* (a clock gone back since it was first seen: as if it came now) */
    if (press <= b->ask.seen) return false;   /* THE TOUCH RULE (touch_gate.h; Strato 11-06: the card's answer is G1 only): a press begun
                                                before the card came (a finger resting on the water as a friend's GIVE popped it, TCH-2) is no
                                                answer - and one in the poll before the card's first draw stamped it (main.c polls before it draws: the same clock), hence <=.
                                                No hold, no restart (round 1's 2 s, restarted by every tap, is gone) */
    b->ask.answer = yes ? 1 : -1;
    return true;
}
