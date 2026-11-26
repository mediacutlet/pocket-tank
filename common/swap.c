/* swap.c — FISH SWAP (swap.h): the book and the protocol. */
#include <string.h>
#include <stdlib.h>
#include "swap.h"
#include "link.h"

/* ---- the book on the flash: magic "SW", version 3, the notes' count, the notes, then the ended trades' count and each (sid 4, peer
 * 4, role 1 - R3-1; once version 1's finished list, so a version 3 reader before it skips them), the count ----
 * a note: st 1, role 1, applied 1, gave 1, sid 4, peer 4, tank 4, wait_s 4, wall_at 4, out len 1 + 128, in len 1 + 128 = 282
 * at most 4 + 4 * 282 + 1 + 8 * 9 + 4 = 1209 bytes. Read too: version 2 (11-22: hour 2, wait_min 2 - awake minutes - at 16, a 278-byte
 * note) and version 1 (11-21/22, never shipped: byte 3 bit 0 = a note, bit 1 = a second, then 8 finished entries of 9 bytes) */
#define SW_MAGIC 0x5753u
#define SW_PP    128
#define SW_HEAD  24
#define SW_HEAD2 20                                      /* (versions 1 and 2) */
#define SW_NOTE  (SW_HEAD + 2 * (1 + SW_PP))
#define SW_NOTE2 (SW_HEAD2 + 2 * (1 + SW_PP))
#define SW_BLOB_MAX (4 + SWAP_NOTES * SW_NOTE + 1 + 8 * 9 + 4)   /* (room to read a version 1 blob's finished list too) */
_Static_assert(SW_BLOB_MAX <= (int)sizeof(((swap_book_t *)0)->mem), "a selftest's flash holds the whole blob");
_Static_assert(PASSPORT_MAX_BYTES >= SW_PP, "a passport's room in the note");
_Static_assert(PASSPORT_V5_BYTES <= SW_PP, "the passport written today (v5) fits a note whole (audit r2, R2-L5) - grow SW_PP and the version first");

static struct { const void *t; swap_book_t *b; } s_att[4];
void swap_attach(const void *tank, swap_book_t *b) {
    int w = -1;
    for (int i = 0; i < 4; i++) { if (s_att[i].t == tank) { w = i; break; } if (w < 0 && !s_att[i].t) w = i; }
    if (w < 0) w = 0;
    s_att[w].t = b ? tank : NULL; s_att[w].b = b;
}
swap_book_t *swap_of(const void *tank) {
    for (int i = 0; i < 4; i++) if (s_att[i].t == tank && tank) return s_att[i].b;
    return NULL;
}
bool persist_port_blob_load(const char *key, void *buf, size_t max, size_t *got);   /* (progression.h's; not included: pure) */
bool persist_port_blob_save(const char *key, const void *buf, size_t len);

static void put32(uint8_t *b, int o, uint32_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); b[o + 2] = (uint8_t)(v >> 16); b[o + 3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *b, int o) { return b[o] | (uint32_t)b[o + 1] << 8 | (uint32_t)b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }

static bool note_put(uint8_t *buf, int p, const swap_note_t *n) {   /* false: a passport did not fit (never saved as a note without its fish) */
    buf[p] = n->st; buf[p + 1] = n->role; buf[p + 2] = n->applied; buf[p + 3] = n->gave;
    put32(buf, p + 4, n->sid); put32(buf, p + 8, n->peer); put32(buf, p + 12, n->tank);
    put32(buf, p + 16, n->wait_s); put32(buf, p + 20, n->wall_at);
    int w1 = passport_write(&n->out, buf + p + SW_HEAD + 1, SW_PP); buf[p + SW_HEAD] = (uint8_t)(w1 > 0 ? w1 : 0);
    int w2 = passport_write(&n->in, buf + p + SW_HEAD + 1 + SW_PP + 1, SW_PP); buf[p + SW_HEAD + 1 + SW_PP] = (uint8_t)(w2 > 0 ? w2 : 0);
    return w1 > 0 && w2 > 0;
}
static bool note_get(const uint8_t *buf, int p, int ver, swap_note_t *out) {   /* false: a note we cannot read - it was never there */
    swap_note_t n; memset(&n, 0, sizeof n);
    const int hd = ver >= 3 ? SW_HEAD : SW_HEAD2;
    n.st = buf[p]; n.role = buf[p + 1]; n.applied = buf[p + 2]; n.gave = ver >= 3 && buf[p + 3] <= SWAP_GAVE_ONLY ? buf[p + 3] : 0;
    n.sid = get32(buf, p + 4); n.peer = get32(buf, p + 8); n.tank = get32(buf, p + 12);
    if (ver >= 3) { n.wait_s = get32(buf, p + 16); n.wall_at = get32(buf, p + 20); }
    else if (ver == 2) n.wait_s = 60u * (uint32_t)(buf[p + 18] | buf[p + 19] << 8);   /* (version 2: awake minutes. Version 1's bytes counted
                                                       another thing: its wait starts over - review 11-22) */
    int w1 = buf[p + hd], w2 = buf[p + hd + 1 + SW_PP];
    bool ok = n.st > SWN_NONE && n.st <= SWN_TAKING && (n.role == SWAP_ROLE_I || n.role == SWAP_ROLE_R) && n.sid && n.peer
           && w1 > 0 && w1 <= SW_PP && passport_read(&n.out, buf + p + hd + 1, w1)
           && w2 > 0 && w2 <= SW_PP && passport_read(&n.in, buf + p + hd + 1 + SW_PP + 1, w2);
    if (ok) *out = n;
    return ok;
}
static int pack(const swap_book_t *b, uint8_t *buf) {   /* -1: a note would not fit (the save is refused, as the flash refusing) */
    memset(buf, 0, SW_BLOB_MAX);
    buf[0] = SW_MAGIC & 0xff; buf[1] = SW_MAGIC >> 8; buf[2] = 3;
    int p = 4, n = 0;
    for (int k = 0; k < SWAP_NOTES; k++) if (b->notes[k].st != SWN_NONE) { if (!note_put(buf, p, &b->notes[k])) return -1; p += SW_NOTE; n++; }
    buf[3] = (uint8_t)n;
    const int ne = b->n_ended <= SWAP_ENDED ? b->n_ended : SWAP_ENDED;
    buf[p++] = (uint8_t)ne;                              /* (version 3: the trades ended here by time, R3-1 - in version 1's finished list, 9 bytes
                                                            each: a 0.4.3 reader before it skips them) */
    for (int i = 0; i < ne; i++) { put32(buf, p, b->ended[i].sid); put32(buf, p + 4, b->ended[i].peer); buf[p + 8] = b->ended[i].role; p += 9; }
    put32(buf, p, b->seq); p += 4;
    return p;
}
static void unpack(swap_book_t *b, const uint8_t *buf, int len) {
    memset(b->notes, 0, sizeof b->notes); memset(b->ended, 0, sizeof b->ended); b->n_ended = 0;
    if (len < 5 || (buf[0] | buf[1] << 8) != SW_MAGIC) return;
    const int ver = buf[2], sz = ver >= 3 ? SW_NOTE : SW_NOTE2;
    int p = 4, n = ver >= 2 ? buf[3] : (buf[3] & 1) + (buf[3] >> 1 & 1), k = 0;
    for (int i = 0; i < n; i++) {
        if (len < p + sz) return;
        swap_note_t m;
        if (k < SWAP_NOTES && note_get(buf, p, ver, &m)) b->notes[k++] = m;   /* (a note we cannot read: it was never there) */
        p += sz;
    }
    if (p >= len) return;
    int nf = buf[p++];
    if (nf > SWAP_ENDED) nf = SWAP_ENDED;
    if (ver >= 3 && p + 9 * nf <= len)                   /* (R3-1's ended trades; version 1's finished list meant another thing: skipped) */
        for (int i = 0; i < nf; i++) {
            const uint32_t sid = get32(buf, p + 9 * i), peer = get32(buf, p + 9 * i + 4); const uint8_t role = buf[p + 9 * i + 8];
            if (sid && peer && (role == SWAP_ROLE_I || role == SWAP_ROLE_R)) { b->ended[b->n_ended].sid = sid; b->ended[b->n_ended].peer = peer; b->ended[b->n_ended].role = role; b->n_ended++; }
        }
    p += 9 * nf;
    if (p + 4 <= len) b->seq = get32(buf, p);
}
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define SW_BUF_RAM EXT_RAM_BSS_ATTR                      /* (on a board: PSRAM - internal RAM is tight, audit r2 R2-L19) */
#else
#define SW_BUF_RAM
#endif
static SW_BUF_RAM uint8_t s_buf[SW_BLOB_MAX];            /* the one buffer save and load share (never nested; one caller at a time - the
                                                            tank task's): off its stack, and 1.2 KB once, not twice (R2-L19) */
bool swap_save(swap_book_t *b) {
    if (!b || b->fail_save) return false;
    int n = pack(b, s_buf);
    if (n < 0) return false;
    if (!b->persist) { memcpy(b->mem, s_buf, (size_t)n); b->mem_len = n; return true; }
    return persist_port_blob_save(SWAP_NVS_KEY, s_buf, (size_t)n);
}
bool swap_load(swap_book_t *b) {
    if (!b) return false;
    if (!b->persist) { unpack(b, b->mem, b->mem_len); return true; }
    size_t got = 0;
    if (!persist_port_blob_load(SWAP_NVS_KEY, s_buf, sizeof s_buf, &got)) { memset(b->notes, 0, sizeof b->notes); return false; }
    unpack(b, s_buf, (int)got);
    return true;
}
void swap_rebase(int64_t delta, int64_t now_unix) {   /* (progression_clock_rebase, before the clock moves: a cut does it again - a stamp
                                                       never after now, so done twice it only counts less, never more) */
    if (delta <= 0) return;                              /* (the clock was ahead: left alone it only counts less - and a cut that repeats a
                                                            backward move would count more, review r2) */
    for (int i = 0; i < 4; i++) if (s_att[i].b) {
        swap_book_t *b = s_att[i].b; bool w = false;
        for (int k = 0; k < SWAP_NOTES; k++) {
            swap_note_t *n = &b->notes[k];
            if (n->st == SWN_NONE || !n->wall_at) continue;
            int64_t v = (int64_t)n->wall_at + delta;
            if (v > now_unix) v = now_unix;
            if (v < 1) v = 1;
            n->wall_at = (uint32_t)v; w = true;
        }
        if (w) swap_save(b);
    }
}
void swap_park(swap_book_t *b) {
    if (!b) return;
    for (int k = 0; k < SWAP_NOTES; k++) if (b->notes[k].st != SWN_NONE) { swap_save(b); return; }
}
static void ram_off(swap_book_t *b) {
    memset(&b->out, 0, sizeof b->out); memset(&b->in, 0, sizeof b->in);
    b->want_uid = b->want_peer = 0; b->i_answer = 0; b->i_cancel = b->r_cancel = false; b->r_answer = 0; b->r_pick = 0;
    b->peer_edge = -1;
}
void swap_crash(swap_book_t *b) {
    if (!b) return;
    ram_off(b); memset(&b->said, 0, sizeof b->said);
    swap_load(b);
}

/* ---- small things ---- */
int swap_test_plant;                                 /* the selftest's planted bugs (swap.h); 0 on the boards */
static swap_note_t *note_find(swap_book_t *b, int role, uint32_t sid, uint32_t peer, uint32_t tank) {   /* this tank's (the link speaks as it) */
    for (int k = 0; k < SWAP_NOTES; k++) {
        swap_note_t *n = &b->notes[k];
        if (n->st != SWN_NONE && n->role == role && n->sid == sid && n->peer == peer && n->tank == tank) return n;
    }
    return NULL;
}
static bool r_spare(const swap_note_t *n) {          /* a note that may give its room to a new trade: an applied R note a week apart
                                                       (R2-1: no BUSY for good; a week, so an I off a day or a weekend still finds it) */
    return n->st == SWN_DONE && n->role == SWAP_ROLE_R && n->applied && n->gave != SWAP_GAVE_ONLY && n->wait_s >= SWAP_R_SPARE_S
        && n->apart_ms >= SWAP_BOOT_GRACE_MS;            /* (this boot's grace too: an I booting beside it links first. A GAVE-UP note never
                                                            gives its room: R may still hold b - review r2) */
}
static bool linked(const struct link *L, uint32_t peer);
static swap_note_t *note_free(swap_book_t *b, const struct link *L) {   /* a free slot - else the R note longest apart that may give its room */
    swap_note_t *best = NULL;
    for (int k = 0; k < SWAP_NOTES; k++) {
        swap_note_t *n = &b->notes[k];
        if (n->st == SWN_NONE) return n;
        if (r_spare(n) && !linked(L, n->peer) && (!best || n->wait_s > best->wait_s)) best = n;
    }
    return best;                                         /* (not cleared: the caller writes over it only once its trade is sure, and puts it
                                                            back if its save is refused - review r2) */
}
const swap_note_t *swap_note_of(const swap_book_t *b, uint32_t tank, int k) {
    if (!b) return NULL;
    for (int i = 0; i < SWAP_NOTES; i++) if (b->notes[i].st != SWN_NONE && b->notes[i].tank == tank && !k--) return &b->notes[i];
    return NULL;
}
static bool since(uint32_t now, uint32_t t, uint32_t ms) { return (uint32_t)(now - t) >= ms; }   /* (wrap-safe: any clock value, 0 included) */
static bool linked(const struct link *L, uint32_t peer) { return L && peer && (link_neighbor(L, LINK_LEFT) == peer || link_neighbor(L, LINK_RIGHT) == peer); }
static int edge_to(const struct link *L, uint32_t peer) { return !L || !peer ? -1 : link_neighbor(L, LINK_LEFT) == peer ? LINK_LEFT : link_neighbor(L, LINK_RIGHT) == peer ? LINK_RIGHT : -1; }
static void send(struct link *L, uint32_t to, int kind, uint32_t sid, uint32_t uid, int reason, const passport_t *pp) {
    if (L && linked(L, to)) link_swap_send(L, to, kind, sid, uid, reason, pp);
}
void swap_said(swap_book_t *b, int what, const passport_t *mine, const passport_t *theirs, double clock) {
    memset(&b->said, 0, sizeof b->said);
    b->said.what = (uint8_t)what; b->said.at = clock;
    b->said.badge = (int8_t)(b->out.st != SO_OFF ? b->out.badge : b->in.st != SI_OFF ? b->in.badge : -1);
    if (mine) { b->said.mine = *mine; b->said.has_mine = true; }
    if (theirs) { b->said.theirs = *theirs; b->said.has_theirs = true; }
}
static int said_of(int reason) {
    static const uint8_t M[SWAP_NO_N] = { SWAP_SAID_THANKS, SWAP_SAID_ANSWER, SWAP_SAID_CALLED_OFF, SWAP_SAID_BUSY, SWAP_SAID_NOT_HAPPENED, SWAP_SAID_FIRST };
    return reason >= 0 && reason < SWAP_NO_N ? M[reason] : SWAP_SAID_NOT_HAPPENED;
}
static void said_no(swap_book_t *b, struct link *L, int reason) {   /* R's NO, remembered: a repeat of that OFFER gets it again */
    b->no_from = b->in.from; b->no_sid = b->in.sid; b->no_reason = (uint8_t)reason;
    send(L, b->in.from, SWK_NO, b->in.sid, 0, reason, NULL);
}
static uint32_t next_sid(swap_book_t *b, uint32_t floor) {
    if (b->seq < floor) b->seq = floor;
    if (++b->seq == 0) b->seq = 1;
    return b->seq;
}
bool swap_busy(const swap_book_t *b) {               /* one trade at a time, and room for its note (a note waiting for its partner,
                                                       or another tank's, blocks nothing else - audit r1, LK-4R / LK-10 / LK-2) */
    if (!b) return false;
    if (b->out.st != SO_OFF || b->in.st != SI_OFF) return true;
    for (int k = 0; k < SWAP_NOTES; k++) if (b->notes[k].st == SWN_NONE || r_spare(&b->notes[k])) return false;
    return true;
}
bool swap_holds(const swap_book_t *b, uint32_t uid) {
    if (!b || !uid) return false;
    for (int k = 0; k < SWAP_NOTES; k++) {
        const swap_note_t *n = &b->notes[k];
        if (n->st != SWN_NONE && !n->applied && n->out.uid == uid) return true;   /* (its note will take it out: not for sale, nor the bowl) */
        if (n->st == SWN_TAKING && n->out.uid == uid) return true;   /* (the finish takes it out: Q1. A GAVE-UP note's a is free - Strato: what
                                                       is done with it meanwhile is lost at the finish) */
    }
    if (b->out.st != SO_OFF && b->out.a.uid == uid) return true;
    if (b->in.st >= SI_WAITING && b->in.b.uid == uid) return true;
    if (b->r_pick == uid && b->in.st == SI_ASKED) return true;
    return false;
}
bool swap_noted(const swap_book_t *b, uint32_t uid, uint32_t tank) {   /* a note of THIS tank that will still move this fish (the bowl's
                                                       LET IN waits for it). An applied R note moves nothing more: it only answers (review 11-22) */
    if (!b || !uid) return false;
    for (int k = 0; k < SWAP_NOTES; k++) {
        const swap_note_t *n = &b->notes[k];
        if (n->st == SWN_NONE || n->st == SWN_GAVEUP || n->tank != tank || (n->role == SWAP_ROLE_R && n->applied)) continue;
        if (n->out.uid == uid || n->in.uid == uid) return true;
    }
    return false;
}
void swap_forget_tank(uint32_t tank) {               /* MY TANKS' REMOVE: that tank is gone, its notes with it (every attached book) */
    if (!tank) return;
    for (int i = 0; i < 4; i++) if (s_att[i].b) {
        swap_book_t *b = s_att[i].b; bool w = false;
        for (int k = 0; k < SWAP_NOTES; k++) if (b->notes[k].st != SWN_NONE && b->notes[k].tank == tank) { memset(&b->notes[k], 0, sizeof b->notes[k]); w = true; }
        if (w) swap_save(b);
    }
}
uint32_t swap_leaving_uid(const swap_book_t *b) {
    if (!b) return 0;
    if (b->out.st == SO_LEAVING) return b->out.a.uid;
    if (b->in.st == SI_GOING) return b->in.b.uid;
    return 0;
}
int swap_held_places(const swap_book_t *b, uint32_t my_tank, bool (*here)(const void *t, uint32_t uid), const void *t) {
    if (!b) return 0;
    int held = 0;
    for (int k = 0; k < SWAP_NOTES; k++) {
        const swap_note_t *n = &b->notes[k];
        if (n->st == SWN_NONE || n->tank != my_tank) continue;
        if (n->role == SWAP_ROLE_I && (n->st == SWN_COMMITTED || n->st == SWN_RECEIVING) && !here(t, n->in.uid)) held++;   /* b's place */
        else if (n->role == SWAP_ROLE_I && n->st == SWN_BACK && !here(t, n->out.uid)) held++;                            /* a's, coming back */
        else if (n->role == SWAP_ROLE_I && n->st == SWN_TAKING && !here(t, n->in.uid) && !here(t, n->out.uid)) held++;  /* b's, the finish (Q1) */
        else if (n->role == SWAP_ROLE_R && !n->applied && !here(t, n->in.uid) && !here(t, n->out.uid)) held++;   /* a's, b gone and a not in yet */
    }
    return held;
}

/* ---- the note's steps: each saved before the fish change it stands for ---- */
static void note_drop(swap_book_t *b, swap_note_t *n) { memset(n, 0, sizeof *n); swap_save(b); }
/* R3-1: the trades whose record ended here by time (saved with the notes, the caller saves) */
static void ended_add(swap_book_t *b, const swap_note_t *n) {
    if (b->n_ended >= SWAP_ENDED) { memmove(b->ended, b->ended + 1, sizeof b->ended[0] * (SWAP_ENDED - 1)); b->n_ended = SWAP_ENDED - 1; }
    b->ended[b->n_ended].sid = n->sid; b->ended[b->n_ended].peer = n->peer; b->ended[b->n_ended].role = n->role; b->n_ended++;
}
static bool ended_has(const swap_book_t *b, int role, uint32_t sid, uint32_t peer) {
    for (int i = 0; i < b->n_ended && i < SWAP_ENDED; i++) if (b->ended[i].sid == sid && b->ended[i].peer == peer && b->ended[i].role == role) return true;
    return false;
}
static bool room_for(const swap_host_t *h) { return h->room && h->room(h->ctx); }
static void tell_full(swap_book_t *b, swap_note_t *n, double clock) {
    if (n->told || b->out.st != SO_OFF || b->in.st != SI_OFF) return;   /* (never over a swap the keeper is in: after it - review r3) */
    n->told = 1;
    swap_said(b, SWAP_SAID_FULL, n->role == SWAP_ROLE_I ? &n->in : &n->out, NULL, clock);
}
/* I: DONE came - b comes in (RECEIVING first), then the note goes */
static void i_receive(swap_book_t *b, swap_note_t *n, struct link *L, const swap_host_t *h, const passport_t *bpp, double clock) {
    if (bpp && bpp->uid == n->in.uid) n->in = *bpp;      /* (b as it left R) */
    n->st = SWN_RECEIVING;
    if (!swap_save(b)) { n->st = SWN_COMMITTED; return; }   /* (not on the flash: the next DONE again) */
    if (!h->in(h->ctx, &n->in, edge_to(L, n->peer))) return;   /* (not saved: a boot brings it from the note) */
    passport_t a = n->out, bb = n->in; uint32_t peer = n->peer, sid = n->sid;
    note_drop(b, n);
    if (b->out.sid == sid && b->out.peer == peer) { swap_said(b, SWAP_SAID_SWAPPED, &a, &bb, clock); memset(&b->out, 0, sizeof b->out); }
    else swap_said(b, SWAP_SAID_SWAPPED, &a, &bb, clock);
    send(L, peer, SWK_CLOSED, sid, 0, 0, NULL);
}
/* I, Q1 (Strato 11-23: FINISH THE TRADE when the tanks meet again): the hard case's day brought a home, and R's DONE has come -
   the trade completes as both keepers asked. This a leaves (R has had its a all along; what was done with this copy since is
   lost), b comes in. a not home just now (next door, at a door): wait - R resends DONE. a gone (sold, given, swapped, the bowl):
   b only, if there is a place; none: wait. TAKING is saved first; the boot finishes it */
static void i_finish(swap_book_t *b, swap_note_t *n, struct link *L, const swap_host_t *h, const passport_t *bpp, double clock) {
    const int w = h->home_now ? h->home_now(h->ctx, n->out.uid) : -1;
    if (w < 0) return;
    if (w == 0 && !room_for(h)) { tell_full(b, n, clock); return; }   /* (a gone and no place: b waits at R - the keeper is told) */
    const passport_t was = n->in;
    if (bpp) n->in = *bpp;                               /* (b as it is at R's note) */
    n->st = SWN_TAKING;
    if (!swap_save(b)) { n->st = SWN_GAVEUP; n->in = was; return; }
    if (!h->out(h->ctx, n->out.uid, true)) return;       /* (not saved: the tick's note_finish again) */
    if (!h->in(h->ctx, &n->in, edge_to(L, n->peer))) return;
    passport_t a = n->out, bb = n->in; uint32_t peer = n->peer, sid = n->sid;
    note_drop(b, n);
    swap_said(b, SWAP_SAID_SWAPPED, &a, &bb, clock);    /* (the SWAPPED card: approved words) */
    send(L, peer, SWK_CLOSED, sid, 0, 0, NULL);
}
/* I: ABORT came (or the long wait is over) - a comes back (BACK first), then the note goes */
static void i_back(swap_book_t *b, swap_note_t *n, const swap_host_t *h, int said, double clock, uint8_t keep) {
    n->st = SWN_BACK; n->gave = keep;
    if (!swap_save(b)) { n->st = SWN_COMMITTED; n->gave = 0; return; }
    if (!h->in(h->ctx, &n->out, -1)) return;
    passport_t a = n->out, bb = n->in; uint32_t peer = n->peer, sid = n->sid;
    if (keep) { n->st = SWN_GAVEUP; n->applied = 1; swap_save(b); }   /* (a home: the note moves it no more) */      /* (the hard case: kept for R's DONE - Q1. Refused: the boot's BACK does it again) */
    else note_drop(b, n);
    swap_said(b, said, &a, &bb, clock);
    if (b->out.sid == sid && b->out.peer == peer) memset(&b->out, 0, sizeof b->out);
}
/* audit r3, R3-1: a note that holds b's only copy (SWAP_GAVE_ONLY) - the other tank answered "no record, nor the fish". Its fish
   comes in at a place; until then the note waits (THIS TANK IS FULL, once a boot) and never ends by time. Never past five */
static void i_bring(swap_book_t *b, swap_note_t *n, const swap_host_t *h, double clock) {   /* I GAVE-UP, only: b in (RECEIVING first) */
    if (!room_for(h)) { tell_full(b, n, clock); return; }
    n->st = SWN_RECEIVING;
    if (!swap_save(b)) { n->st = SWN_GAVEUP; return; }
    if (!h->in(h->ctx, &n->in, -1)) return;              /* (not saved: the boot's RECEIVING brings it) */
    passport_t a = n->out, bb = n->in;
    note_drop(b, n);
    swap_said(b, SWAP_SAID_SWAPPED, &a, &bb, clock);     /* (a lives in R too - the designed double) */
}
static void i_take(swap_book_t *b, swap_note_t *n, const swap_host_t *h, double clock) {   /* I: R's "not here" ABORT */
    if (n->st == SWN_COMMITTED) {                        /* (R's note ended before this tank's day: a comes home first) */
        i_back(b, n, h, SWAP_SAID_NOT_FINISHED, clock, SWAP_GAVE_ONLY);
        if (n->st != SWN_GAVEUP) return;                 /* (a not home yet: the BACK on the flash, the tick or the boot brings it) */
        n->told = 1;                                     /* (<a> CAME BACK stands this boot; THIS TANK IS FULL from the next - review r3) */
    } else if (n->gave != SWAP_GAVE_ONLY) {
        const uint8_t was = n->gave;
        n->gave = SWAP_GAVE_ONLY;
        if (!swap_save(b)) { n->gave = was; return; }    /* (not on the flash: R answers the same at the next ask) */
    }
    i_bring(b, n, h, clock);
}
static void r_home(swap_book_t *b, swap_note_t *n, const swap_host_t *h, double clock) {   /* R DONE, only: b home, a stays (the double) */
    if (!n->applied) return;                             /* (the trade's own change first: note_finish) */
    const bool here = h->home_now && h->home_now(h->ctx, n->out.uid) != 0;   /* (a cut after it came: only the note goes) */
    if (!here) {
        if (!room_for(h)) { tell_full(b, n, clock); return; }
        if (!h->in(h->ctx, &n->out, -1)) return;
    }
    passport_t bb = n->out, a = n->in;
    note_drop(b, n);
    swap_said(b, SWAP_SAID_NOT_HAPPENED, &bb, &a, clock);   /* (THE SWAP DID NOT HAPPEN / b IS STILL YOURS) */
}
/* R: a COMMIT for the pick it holds - the note (DONE) first, then b out and a in */
static bool r_apply(swap_book_t *b, struct link *L, const swap_host_t *h, uint32_t my_tank, uint32_t from, uint32_t sid, const passport_t *apa, double clock) {
    passport_t bpp;
    if (!h->can_go(h->ctx, b->in.b.uid, from) || !h->settled(h->ctx, b->in.b.uid, from) || !h->passport(h->ctx, b->in.b.uid, &bpp)) return false;
    swap_note_t *slot = note_free(b, L);
    if (!slot) return false;                             /* (no room for its note: no trade - ABORT, a home) */
    const swap_note_t was = *slot;
    swap_ended_t ek[SWAP_ENDED]; const uint8_t ne = b->n_ended; memcpy(ek, b->ended, sizeof ek);
    if (was.st != SWN_NONE) ended_add(b, &was);          /* (a week's room: remembered, R3-1 - if its I comes back, its record has b) */
    swap_note_t n; memset(&n, 0, sizeof n);
    n.st = SWN_DONE; n.role = SWAP_ROLE_R; n.sid = sid; n.peer = from; n.tank = my_tank;
    n.out = bpp; n.in = apa && apa->uid == b->in.a.uid ? *apa : b->in.a;
    *slot = n;
    if (!swap_save(b)) {                                 /* (not on the flash: all as it was) */
        *slot = was;
        memcpy(b->ended, ek, sizeof ek); b->n_ended = ne;
        return false;
    }
    bool quiet = h->near_door && h->near_door(h->ctx, n.out.uid);
    if (h->out(h->ctx, n.out.uid, !quiet) && h->in(h->ctx, &n.in, edge_to(L, from))) { slot->applied = 1; swap_save(b); swap_said(b, SWAP_SAID_SWAPPED, &n.out, &n.in, clock); }
    /* (not saved: the tick's note_finish shows the card when it lands - one card, review r3) */
    memset(&b->in, 0, sizeof b->in);
    send(L, from, SWK_DONE, sid, n.out.uid, 0, &slot->out);
    slot->sent = b->now;
    if (swap_test_plant == 2) note_drop(b, slot);        /* (planted: the note gone at DONE, not at CLOSED) */
    return true;
}
/* the fish change a note stands for, again (a boot, or a save the flash refused): idempotent - out of one not here is nothing,
   in of one here is nothing. Each ends with the card the live path shows (audit r3, R3-L10: one fish gone, another come, no word).
   clock < 0 (a boot): the tick dates the card (swap_tick) */
static void finish_said(swap_book_t *b, int what, const passport_t *mine, const passport_t *theirs, double clock) {
    if (b->out.st == SO_OFF && b->in.st == SI_OFF) swap_said(b, what, mine, theirs, clock);   /* (never over a swap the keeper is in) */
}
static void note_finish(swap_book_t *b, swap_note_t *n, const swap_host_t *h, double clock) {
    const passport_t a = n->out, bb = n->in;
    if (n->role == SWAP_ROLE_I) {
        if (n->st == SWN_COMMITTED && !n->applied) { if (h->out(h->ctx, n->out.uid, false)) { n->applied = 1; swap_save(b); } }
        else if (n->st == SWN_RECEIVING) { if (h->in(h->ctx, &n->in, -1)) { note_drop(b, n); finish_said(b, SWAP_SAID_SWAPPED, &a, &bb, clock); } }
        else if (n->st == SWN_BACK) {
            if (h->in(h->ctx, &n->out, -1)) {
                if (n->gave) { n->st = SWN_GAVEUP; n->applied = 1; swap_save(b); finish_said(b, SWAP_SAID_NOT_FINISHED, &a, &bb, clock); }
                else { note_drop(b, n); finish_said(b, SWAP_SAID_NOT_HAPPENED, &a, &bb, clock); }
            }
        }
        else if (n->st == SWN_TAKING) { if (h->out(h->ctx, n->out.uid, true) && h->in(h->ctx, &n->in, -1)) { note_drop(b, n); finish_said(b, SWAP_SAID_SWAPPED, &a, &bb, clock); } }   /* (R's next
                                                       DONE then finds no note: CLOSED) */
    } else if (n->role == SWAP_ROLE_R && n->st == SWN_DONE && !n->applied) {
        if (h->out(h->ctx, n->out.uid, false) && h->in(h->ctx, &n->in, -1)) { n->applied = 1; swap_save(b); finish_said(b, SWAP_SAID_SWAPPED, &a, &bb, clock); }
    }
}
void swap_boot(swap_book_t *b, const swap_host_t *h, uint32_t my_tank) {
    if (!b) return;
    ram_off(b); memset(&b->said, 0, sizeof b->said);
    swap_load(b);
    b->now_last = 0; b->clock_last = 0;
    bool gone = false;                                   /* a note of a tank this board no longer holds (REMOVE, RESET, ERASE cut before its
                                                            notes went - audit r2, R2-1 / R2-L28): nothing can ever finish it */
    for (int k = 0; k < SWAP_NOTES; k++) {
        swap_note_t *n = &b->notes[k];
        if (n->st != SWN_NONE && n->tank != my_tank && h->tank_gone && h->tank_gone(h->ctx, n->tank)) { memset(n, 0, sizeof *n); gone = true; }
    }
    if (gone) swap_save(b);
    for (int k = 0; k < SWAP_NOTES; k++) if (b->notes[k].st != SWN_NONE && b->notes[k].tank == my_tank) note_finish(b, &b->notes[k], h, -1.0);
}
void swap_reset(swap_book_t *b, uint32_t my_tank) {
    if (!b) return;
    ram_off(b); memset(&b->said, 0, sizeof b->said);
    bool w = false;
    for (int k = 0; k < SWAP_NOTES; k++)
        if (b->notes[k].st != SWN_NONE && (!my_tank || b->notes[k].tank == my_tank)) { memset(&b->notes[k], 0, sizeof b->notes[k]); w = true; }
    if (w) swap_save(b);
}
void swap_unlinked(swap_book_t *b, uint32_t peer, double clock) {
    if (!b || !peer) return;
    if (b->out.st == SO_COMMITTED && b->out.peer == peer) memset(&b->out, 0, sizeof b->out);   /* (its note waits for the next link) */
    if (b->out.st >= SO_ASKING && b->out.st <= SO_LEAVING && b->out.peer == peer) { swap_said(b, SWAP_SAID_NOT_HAPPENED, &b->out.a, b->out.st >= SO_PICKED ? &b->out.b : NULL, clock); memset(&b->out, 0, sizeof b->out); }
    if (b->in.st != SI_OFF && b->in.from == peer) { if (b->in.st >= SI_WAITING) swap_said(b, SWAP_SAID_NOT_HAPPENED, &b->in.b, &b->in.a, clock); memset(&b->in, 0, sizeof b->in); b->r_pick = 0; b->r_answer = 0; }
}

void swap_tick(swap_book_t *b, struct link *L, const swap_host_t *h, uint32_t my_tank, uint32_t now, double clock) {
    if (!b) return;
    b->now = now;
    /* the wait's time: the tank's own running clock, never the link engine's (review 11-22: with the partner gone the board
       turns its radio off and stops ticking the engine, so its ms stand still - the day never came) */
    double dc = b->clock_last > 0 && clock > b->clock_last ? clock - b->clock_last : 0;
    const bool gap = dc > 5.0;                           /* (a jump: a nap, a slept span lived at once - the wall clock says how long) */
    if (gap) dc = 0;
    b->clock_last = clock;
    if (b->said.what != SWAP_SAID_NONE && b->said.at < 0) b->said.at = clock;   /* (a boot's card: dated by the first tick, R3-L10) */
    const int64_t wall = h->wall ? h->wall(h->ctx) : 0;
    bool save = false;
    for (int k = 0; k < SWAP_NOTES; k++) {
        swap_note_t *n = &b->notes[k];
        if (n->st == SWN_NONE) continue;
        const bool near = linked(L, n->peer);
        if (n->tank != my_tank) continue;                /* (another tank's note waits for that tank on the glass: the link speaks as the tank
                                                            on the glass - a shelved tank's note answering under its name was let go by the
                                                            wrong CLOSED, review 11-22. Its span counts when that tank is back: wall_at) */
        /* THE DAY APART (audit r2, R2-2; doc 2.4): counted while `peer` is not linked. Across a boot, a nap, a tank's time on the
           shelf: the wall clock's span since the note last counted (sleep and power-off are apart too - Strato: "after a day, the
           fish comes home"). Awake: the tank's own running seconds, never the wall clock's (a clock set, an NTP step or a bad read
           mid-run counts nothing: PW-7). No clock: awake seconds only (LK-4), saved at a sleep (swap_park) */
        const uint32_t w0 = n->wait_s, wa0 = n->wall_at;
        if (near || gap) n->apart_ms = 0;               /* (a nap: its link closed - the grace again, as at a boot. Review r2) */
        if (near) {
            if (wall <= 0 && n->wall_at && n->live) { n->part += dc; const uint32_t s = (uint32_t)n->part; n->part -= s; n->wall_at += s; }   /* (linked
                                                       time never counts as apart at a later boot - review r2) */
        }
        else if ((!n->live || gap) && wall > 0 && n->wall_at && wall > (int64_t)n->wall_at) {
            uint64_t span = (uint64_t)(wall - (int64_t)n->wall_at);
            if (span > SWAP_SPAN_MAX_S) span = SWAP_SPAN_MAX_S;   /* (R3-L15: one boot or nap counts under a week - a clock wrongly far ahead
                                                       never by itself ends a month's note, a GAVE-UP or gives a week's room) */
            const uint64_t w = (uint64_t)n->wait_s + span;
            n->wait_s = w > 0xffffffffu ? 0xffffffffu : (uint32_t)w;
        } else if (n->live) {
            n->part += dc;
            const uint32_t s = (uint32_t)n->part; n->part -= s;
            n->wait_s = n->wait_s + s < n->wait_s ? 0xffffffffu : n->wait_s + s;
            if (wall <= 0 && n->wall_at) n->wall_at += s;   /* (no clock just now: the stamp keeps step, so the span a later boot counts
                                                       never counts these seconds twice) */
            const uint32_t add = (uint32_t)(dc * 1000.0 + 0.5);
            n->apart_ms = n->apart_ms + add < n->apart_ms ? 0xffffffffu : n->apart_ms + add;
        }
        if (wall > 0 && !n->wall_at) save = true;        /* (the first stamp: a power cut before the next save still counts from it) */
        if (wall > 0) n->wall_at = (uint32_t)wall;
        n->live = 1;
        if (n->wait_s / 600 != w0 / 600) save = true;    /* (every 10 minutes, and swap_park at a sleep) */
        if (near && n->wall_at / 600 != wa0 / 600) save = true;   /* (linked: the stamp on the flash kept within ten minutes - review r2) */
        const bool over = !near && n->apart_ms >= SWAP_BOOT_GRACE_MS;   /* (PW-6: a boot beside the partner links before its first minute) */
        if (n->role == SWAP_ROLE_R && n->st == SWN_DONE) {   /* R: it answers until CLOSED - or, its partner never back, a week apart */
            if (!n->applied) note_finish(b, n, h, clock);
            if (n->gave == SWAP_GAVE_ONLY) { r_home(b, n, h, clock); continue; }   /* (R3-1: b's only copy - home at a place, never by time) */
            if (near && since(now, n->sent, SWAP_RESEND_MS * 2)) { send(L, n->peer, SWK_DONE, n->sid, n->out.uid, 0, &n->out); n->sent = now; }
            if (over && n->applied && n->wait_s >= SWAP_R_KEEP_S) { ended_add(b, n); memset(n, 0, sizeof *n); save = true; }   /* (R2-1: every I's day long
                                                       over. Remembered: if I comes back, its record has b - NOT_HERE, R3-1) */
            continue;
        }
        if (n->st == SWN_GAVEUP) {                       /* Q1: asks again whenever linked - R's note answers DONE (the finish), no note ABORT */
            if (n->gave == SWAP_GAVE_ONLY) { i_bring(b, n, h, clock); continue; }   /* (R3-1: b's only copy - in at a place, never by time) */
            if (near && since(now, n->sent, SWAP_RESEND_MS * 4)) { send(L, n->peer, SWK_COMMIT, n->sid, n->in.uid, 0, &n->out); n->sent = now; }
            if (over && n->wait_s >= SWAP_GAVEUP_S) { ended_add(b, n); memset(n, 0, sizeof *n); save = true; }   /* (past R's month - remembered:
                                                       if R's note still holds b, its DONE gets NOT_HERE and b goes home there, R3-1) */
            continue;
        }
        if (n->st != SWN_COMMITTED) { note_finish(b, n, h, clock); continue; }   /* (RECEIVING / BACK / TAKING whose save was refused: again) */
        if (!n->applied) note_finish(b, n, h, clock);
        if (near) {
            if (since(now, n->sent, SWAP_RESEND_MS)) { send(L, n->peer, SWK_COMMIT, n->sid, n->in.uid, 0, &n->out); n->sent = now; }
            continue;
        }
        /* THE HARD CASE (doc 2.4): R silent since the trade - a day apart, a comes home */
        if (over && n->wait_s >= SWAP_WAIT_S && n->applied) { if (save) swap_save(b); save = false; i_back(b, n, h, SWAP_SAID_NOT_FINISHED, clock, 1); }
    }
    if (save) swap_save(b);
    /* I: the keeper's SWAP on the GIVE card */
    if (b->want_uid) {
        uint32_t uid = b->want_uid, peer = b->want_peer; b->want_uid = b->want_peer = 0;
        passport_t a;
        bool have = h->passport(h->ctx, uid, &a);
        if (swap_busy(b)) swap_said(b, SWAP_SAID_BUSY, have ? &a : NULL, NULL, clock);
        else if (!linked(L, peer) || !link_peer_swap(L, peer) || !have || !h->can_go(h->ctx, uid, peer)) swap_said(b, SWAP_SAID_NOT_HAPPENED, have ? &a : NULL, NULL, clock);
        else {
            memset(&b->out, 0, sizeof b->out);
            b->out.st = SO_ASKING; b->out.sid = next_sid(b, h->hour ? h->hour(h->ctx) * 3600u : 0); b->out.peer = peer; b->out.a = a;
            b->out.t0 = now; b->out.sent = now; b->out.badge = (int8_t)link_peer_badge(L, peer); b->peer_edge = (int8_t)edge_to(L, peer);
            swap_save(b);                                    /* (the count, so a reboot never repeats a number) */
            memset(&b->said, 0, sizeof b->said);
            send(L, peer, SWK_OFFER, b->out.sid, uid, 0, &a);
        }
    }
    /* I's side */
    if (b->out.st >= SO_ASKING && b->out.st <= SO_LEAVING) {
        uint32_t peer = b->out.peer;
        if (!linked(L, peer)) { swap_unlinked(b, peer, clock); }
        else if (b->i_cancel && b->out.st != SO_LEAVING) {
            send(L, peer, SWK_WITHDRAW, b->out.sid, 0, SWAP_NO_CALLED_OFF, NULL); memset(&b->out, 0, sizeof b->out); memset(&b->said, 0, sizeof b->said);
        } else if (!h->can_go(h->ctx, b->out.a.uid, peer) && b->out.st != SO_LEAVING) {
            send(L, peer, SWK_WITHDRAW, b->out.sid, 0, SWAP_NO_CANNOT, NULL);
            swap_said(b, SWAP_SAID_NOT_HAPPENED, &b->out.a, NULL, clock); memset(&b->out, 0, sizeof b->out);
        } else if (b->out.st == SO_ASKING) {
            if (since(now, b->out.t0, SWAP_ASK_MS + 5000)) {
                send(L, peer, SWK_WITHDRAW, b->out.sid, 0, SWAP_NO_ANSWER, NULL);
                swap_said(b, SWAP_SAID_ANSWER, &b->out.a, NULL, clock); memset(&b->out, 0, sizeof b->out);
            } else if (since(now, b->out.sent, SWAP_RESEND_MS)) { send(L, peer, SWK_OFFER, b->out.sid, b->out.a.uid, 0, &b->out.a); b->out.sent = now; }
        } else if (b->out.st == SO_PICKED) {
            if (b->i_answer < 0 || since(now, b->out.t0, SWAP_LAST_MS)) {
                send(L, peer, SWK_WITHDRAW, b->out.sid, 0, b->i_answer < 0 ? SWAP_NO_THANKS : SWAP_NO_ANSWER, NULL);
                memset(&b->out, 0, sizeof b->out); memset(&b->said, 0, sizeof b->said);
            } else if (b->i_answer > 0) {
                b->out.st = SO_LEAVING; b->out.t0 = now; b->out.sent = now;
                send(L, peer, SWK_GO, b->out.sid, 0, 0, NULL);
            }
        } else if (b->out.st == SO_LEAVING) {
            bool heard = link_peer_heard(L, peer, SWAP_HEARD_MS), fresh = link_peer_heard(L, peer, SWAP_COMMIT_MS);
            if (!heard || since(now, b->out.t0, SWAP_LEAVE_MS + 10000) || !h->can_go(h->ctx, b->out.a.uid, peer)) {
                send(L, peer, SWK_WITHDRAW, b->out.sid, 0, SWAP_NO_CANNOT, NULL);
                swap_said(b, SWAP_SAID_NOT_HAPPENED, &b->out.a, &b->out.b, clock); memset(&b->out, 0, sizeof b->out);
            } else {
                if (since(now, b->out.sent, SWAP_RESEND_MS * 2)) { send(L, peer, SWK_GO, b->out.sid, 0, 0, NULL); b->out.sent = now; }
                bool through = h->at_door(h->ctx, b->out.a.uid), late = since(now, b->out.t0, SWAP_LEAVE_MS);
                if ((through || late) && fresh && h->settled(h->ctx, b->out.a.uid, peer)) {   /* THE COMMIT (a home or settled next door, never in a
                                                     doorway): the note on the flash first, then a goes */
                    passport_t a;
                    if (!h->passport(h->ctx, b->out.a.uid, &a)) a = b->out.a;
                    swap_note_t m; memset(&m, 0, sizeof m);
                    m.st = SWN_COMMITTED; m.role = SWAP_ROLE_I; m.sid = b->out.sid; m.peer = peer; m.tank = my_tank;
                    m.out = a; m.in = b->out.b;
                    if (swap_test_plant == 1) h->out(h->ctx, a.uid, !through);   /* (planted: a out before its note) */
                    swap_note_t *slot = note_free(b, L); /* (swap_busy kept one free since the ask) */
                    swap_note_t was; swap_ended_t ek[SWAP_ENDED]; const uint8_t ne = b->n_ended; memcpy(ek, b->ended, sizeof ek);
                    if (slot) { was = *slot; if (was.st != SWN_NONE) ended_add(b, &was); *slot = m; }   /* (a week's room taken here too: remembered,
                                                       R3-1 review r3 - if its I comes back, its record has b) */
                    if (!slot || !swap_save(b)) {
                        if (slot) { *slot = was; memcpy(b->ended, ek, sizeof ek); b->n_ended = ne; }
                        send(L, peer, SWK_WITHDRAW, b->out.sid, 0, SWAP_NO_CANNOT, NULL);
                        swap_said(b, SWAP_SAID_NOT_HAPPENED, &b->out.a, &b->out.b, clock); memset(&b->out, 0, sizeof b->out);
                    } else {
                        if (h->out(h->ctx, a.uid, !through)) { slot->applied = 1; swap_save(b); }
                        b->out.st = SO_COMMITTED; b->out.sent = now; slot->sent = now;
                        send(L, peer, SWK_COMMIT, m.sid, m.in.uid, 0, &slot->out);
                    }
                }
            }
        }
        b->i_cancel = false; b->i_answer = 0;
    } else { b->i_cancel = false; if (b->out.st != SO_COMMITTED) b->i_answer = 0; }
    if (b->out.st == SO_COMMITTED && !note_find(b, SWAP_ROLE_I, b->out.sid, b->out.peer, my_tank)) memset(&b->out, 0, sizeof b->out);   /* (settled: the note is gone) */
    /* R's side */
    if (b->in.st != SI_OFF) {
        uint32_t from = b->in.from;
        if (!linked(L, from)) swap_unlinked(b, from, clock);
        else if (b->in.st == SI_ASKED) {
            if (b->r_answer < 0) { said_no(b, L, SWAP_NO_THANKS); memset(&b->in, 0, sizeof b->in); b->r_pick = 0; }
            else if (since(now, b->in.t0, SWAP_ASK_MS)) { said_no(b, L, SWAP_NO_ANSWER); memset(&b->in, 0, sizeof b->in); b->r_pick = 0; }
            else if (b->r_pick) {
                passport_t bp; uint32_t u = b->r_pick; b->r_pick = 0;
                if (h->can_go(h->ctx, u, from) && h->passport(h->ctx, u, &bp)) {
                    b->in.b = bp; b->in.st = SI_WAITING; b->in.t0 = now; b->in.sent = now;
                    send(L, from, SWK_PICK, b->in.sid, u, 0, &bp);
                }
            }
        } else if (b->in.st == SI_WAITING || b->in.st == SI_GOING) {
            if (b->r_cancel && b->in.st == SI_WAITING) { said_no(b, L, SWAP_NO_CALLED_OFF); memset(&b->in, 0, sizeof b->in); memset(&b->said, 0, sizeof b->said); }
            else if (!h->can_go(h->ctx, b->in.b.uid, from)) {
                said_no(b, L, SWAP_NO_CANNOT);
                swap_said(b, SWAP_SAID_NOT_HAPPENED, &b->in.b, &b->in.a, clock); memset(&b->in, 0, sizeof b->in);
            } else if (since(now, b->in.t0, (b->in.st == SI_GOING ? SWAP_LEAVE_MS + 15000 : SWAP_PICK_TTL_MS))) {
                swap_said(b, b->in.st == SI_GOING ? SWAP_SAID_NOT_HAPPENED : SWAP_SAID_ANSWER, &b->in.b, &b->in.a, clock); memset(&b->in, 0, sizeof b->in);
            } else if (b->in.st == SI_WAITING && since(now, b->in.sent, SWAP_RESEND_MS)) { send(L, from, SWK_PICK, b->in.sid, b->in.b.uid, 0, &b->in.b); b->in.sent = now; }
        }
        b->r_cancel = false; b->r_answer = 0;
    } else { b->r_cancel = false; b->r_answer = 0; b->r_pick = 0; }
}

void swap_rx(swap_book_t *b, struct link *L, const swap_host_t *h, uint32_t my_tank, uint32_t from, int kind, uint32_t sid,
             uint32_t uid, int reason, const passport_t *pp, uint32_t now, double clock) {
    if (!b || !from || !sid) return;
    switch (kind) {
    case SWK_OFFER: {                                   /* R: a swap offered to us */
        if (b->in.st != SI_OFF && b->in.from == from && b->in.sid == sid) return;   /* (a resend) */
        if (b->no_from == from && b->no_sid == sid) { send(L, from, SWK_NO, sid, 0, b->no_reason, NULL); return; }   /* (a resend after our NO: NO again,
                                                       never the card back - its NO may have been lost) */
        if (!pp || pp->uid != uid || !linked(L, from)) { send(L, from, SWK_NO, sid, 0, SWAP_NO_CANNOT, NULL); return; }
        if (b->out.st == SO_ASKING && b->out.peer == from) {   /* both asked at once: the lower tank's ask stands */
            if (my_tank < from) { send(L, from, SWK_NO, sid, 0, SWAP_NO_FIRST, NULL); return; }
            send(L, from, SWK_WITHDRAW, b->out.sid, 0, SWAP_NO_FIRST, NULL);
            swap_said(b, SWAP_SAID_FIRST, &b->out.a, NULL, clock); memset(&b->out, 0, sizeof b->out);
        }
        if (swap_busy(b)) { send(L, from, SWK_NO, sid, 0, SWAP_NO_BUSY, NULL); return; }
        memset(&b->in, 0, sizeof b->in);
        b->in.st = SI_ASKED; b->in.sid = sid; b->in.from = from; b->in.a = *pp; b->in.t0 = now; b->in.sent = now;
        b->in.badge = (int8_t)link_peer_badge(L, from); b->peer_edge = (int8_t)edge_to(L, from);
        b->r_answer = 0; b->r_pick = 0;
        if (b->said.what != SWAP_SAID_FIRST) memset(&b->said, 0, sizeof b->said);
        return;
    }
    case SWK_WITHDRAW: case SWK_NO:                     /* WITHDRAW: the sender's own offer (ours to answer); NO: our offer (its answer) */
        if (kind == SWK_WITHDRAW && b->in.st != SI_OFF && b->in.from == from && b->in.sid == sid) {   /* R: I took it back */
            if (b->in.st >= SI_WAITING || reason == SWAP_NO_FIRST) swap_said(b, reason == SWAP_NO_FIRST ? SWAP_SAID_NONE : said_of(reason), &b->in.b, &b->in.a, clock);
            memset(&b->in, 0, sizeof b->in); b->r_pick = 0;
        }
        if (kind == SWK_NO && b->out.st >= SO_ASKING && b->out.st <= SO_LEAVING && b->out.peer == from && b->out.sid == sid) {   /* I: R said no */
            swap_said(b, said_of(reason), &b->out.a, b->out.st >= SO_PICKED ? &b->out.b : NULL, clock);
            memset(&b->out, 0, sizeof b->out);
        }
        return;
    case SWK_PICK:                                      /* I: their pick */
        if (b->out.st == SO_ASKING && b->out.peer == from && b->out.sid == sid && pp && pp->uid == uid) {
            b->out.b = *pp; b->out.st = SO_PICKED; b->out.t0 = now; b->i_answer = 0;
        } else if (!(b->out.st >= SO_PICKED && b->out.sid == sid && b->out.peer == from) && !note_find(b, SWAP_ROLE_I, sid, from, my_tank))
            send(L, from, SWK_WITHDRAW, sid, 0, SWAP_NO_CANNOT, NULL);   /* (we ask nothing of it now: it lets go) */
        return;
    case SWK_GO:                                        /* R: the last yes - b swims to its door */
        if (b->in.st == SI_WAITING && b->in.from == from && b->in.sid == sid) { b->in.st = SI_GOING; b->in.t0 = now; }
        return;
    case SWK_COMMIT: {                                  /* R: the trade */
        swap_note_t *n = note_find(b, SWAP_ROLE_R, sid, from, my_tank);
        if (n) {
            if (n->gave == SWAP_GAVE_ONLY) return;       /* (b comes home here: nothing more to say) */
            if (n->wait_s) { n->wait_s = 0; n->part = 0; swap_save(b); }   /* (R3-1: the partner asked - its month starts again from here) */
            send(L, from, SWK_DONE, sid, n->out.uid, 0, &n->out); return;
        }
        if (b->in.st >= SI_WAITING && b->in.from == from && b->in.sid == sid && uid == b->in.b.uid
            && r_apply(b, L, h, my_tank, from, sid, pp, clock)) return;
        /* no note, no pick: never applied (an R note lives until CLOSED, and I closes only after b is in) - ABORT is the truth.
           Or its note ended here by time (its month, a week's room: R2-1) and b with it - I's note holds b's only copy: NOT_HERE tells
           I to keep it (R3-1) */
        const bool picked = b->in.st != SI_OFF && b->in.from == from && b->in.sid == sid;
        const bool ended = !picked && ended_has(b, SWAP_ROLE_R, sid, from);
        if (picked) { swap_said(b, SWAP_SAID_NOT_HAPPENED, &b->in.b, &b->in.a, clock); memset(&b->in, 0, sizeof b->in); }
        if (L && pp && pp->uid && swap_test_plant != 3) link_swap_poof(L, pp->uid, from);   /* (a was swapped where it visits us: its home let go of it and brings it
                                                       back on this ABORT - it must not swim here a moment longer. Review 11-22) */
        send(L, from, SWK_ABORT, sid, 0, ended ? SWAP_ANS_NOT_HERE : SWAP_ANS_PLAIN, NULL);
        return; }
    case SWK_DONE: {                                    /* I: R did it */
        swap_note_t *n = note_find(b, SWAP_ROLE_I, sid, from, my_tank);
        if (!n) {                                        /* no note of this tank: finished here long ago (b is here) - R may let go. Or the GAVE-UP
                                                            record ended here by time and b never came: NOT_HERE - R keeps b (R3-1) */
            send(L, from, SWK_CLOSED, sid, 0, ended_has(b, SWAP_ROLE_I, sid, from) ? SWAP_ANS_NOT_HERE : SWAP_ANS_PLAIN, NULL);
            return;
        }
        if (n->st == SWN_COMMITTED) {
            if (!n->applied) return;                     /* (a not out yet on the flash: the tick tries again first) */
            i_receive(b, n, L, h, pp && pp->uid == n->in.uid ? pp : NULL, clock);
        } else if (n->st == SWN_GAVEUP) {
            if (n->wait_s) { n->wait_s = 0; n->part = 0; swap_save(b); }   /* (R3-1: R answered - this record's days start again from here) */
            i_finish(b, n, L, h, pp && pp->uid == n->in.uid ? pp : NULL, clock);
        }
        return; }
    case SWK_ABORT: {                                   /* I: R could not - a comes back */
        swap_note_t *n = note_find(b, SWAP_ROLE_I, sid, from, my_tank);
        if (n && reason == SWAP_ANS_NOT_HERE && (n->st == SWN_COMMITTED || n->st == SWN_GAVEUP)) i_take(b, n, h, clock);   /* (R3-1: R's note
                                                       ended and took b with it - ours is b's only copy: a home, b in at a place) */
        else if (n && n->st == SWN_COMMITTED) i_back(b, n, h, SWAP_SAID_NOT_HAPPENED, clock, 0);
        else if (n && n->st == SWN_GAVEUP && n->gave != SWAP_GAVE_ONLY) note_drop(b, n);   /* (a GAVE_ONLY record: R said its note ended -
                                                       a stale plain ABORT never drops b's only copy) */   /* (R never applied, or its note has gone: a stays home, nothing to finish) */
        return; }
    case SWK_CLOSED: {                                  /* R: I has b - our note goes (once its own fish change is on the flash) */
        swap_note_t *n = note_find(b, SWAP_ROLE_R, sid, from, my_tank);
        if (n && reason == SWAP_ANS_NOT_HERE) {          /* (R3-1: I has no record and no b - ours is b's only copy: home at a place, the tick) */
            if (n->gave != SWAP_GAVE_ONLY) { n->gave = SWAP_GAVE_ONLY; if (!swap_save(b)) n->gave = 0; }
        } else if (n && n->applied && n->gave != SWAP_GAVE_ONLY) note_drop(b, n);   /* (a GAVE_ONLY note: I said its record ended - a stale
                                                       plain CLOSED never drops b's only copy) */
        return; }
    }
}
