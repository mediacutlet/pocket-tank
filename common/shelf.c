/* shelf.c - MY TANKS: the tanks a board keeps. See shelf.h. */
#include "shelf.h"
#include "progression.h"
#include "collection.h"
#include "gift.h"
#include "passport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHELF_MAGIC  0x31535450u                  /* "PTS1" */
#define SHELF_LAYOUT 1
#define NOW_MAGIC    0x574f4e50u                  /* "PNOW" */
#define SWAP_MAGIC   0x50415753u                  /* "SWAP" */
_Static_assert(sizeof(shelf_tank_t) == 80, "SHELF LAYOUT LOCK: the slot header only ever grows (and then its layout says so)");
_Static_assert(sizeof(shelf_tank_t) <= SHELF_SAVE_OFF, "the header must end before the save");

typedef struct { uint32_t magic, from_id, to_id; int16_t out_slot, in_slot; uint32_t pad; float lived_h; } swap_t;   /* the journal */

static shelf_tank_t s_now;
/* the list and the buffers come from the heap, never statics: on the device a
 * block over 8 KB is PSRAM, and internal RAM has ~23 KB to spare (as statics
 * these were +46 KB of .bss - a black tank, firmware-internal-ram-margin) */
typedef struct { shelf_tank_t h; int16_t slot; } entry_t;
static entry_t *s_t;
typedef struct { uint8_t a[SHELF_SAVE_MAX], b[SHELF_SAVE_MAX]; uint16_t thumb[SHELF_THUMB_MAX / 2]; uint8_t x[SHELF_EXTRA_MAX];
                 uint8_t sec[4096]; } scratch_t;   /* sec: a visitor book's sector */
static scratch_t *s_x;
/* the visitor books (shelf.h): their slots and what the boot read of them - in the shelf list's block (the heap's,
   past 8 KB: PSRAM on the device), never a static */
typedef struct { int16_t slot; int8_t cur[SHELF_BOOK_GROUPS]; uint32_t owner, print, seq; } book_t;
static book_t *s_bk;
static int s_nb;
static bool buffers(void) {
    if (!s_t) { s_t = calloc(1, SHELF_SLOTS_MAX * (sizeof *s_t + sizeof *s_bk)); if (s_t) s_bk = (book_t *)(void *)(s_t + SHELF_SLOTS_MAX); }
    if (!s_x) s_x = malloc(sizeof *s_x);
    return s_t && s_x;
}
static int s_n, s_slots, s_cursor;
static uint32_t s_next;
static float s_lived_h = -1;

static uint32_t crc_run(uint32_t c, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { c ^= b[i]; for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xedb88320u : c >> 1; }
    return c;
}
static uint32_t crc32(const void *p, size_t n) { return ~crc_run(0xffffffffu, p, n); }
static uint32_t slot_off(int k) { return (uint32_t)k * SHELF_SLOT_SIZE; }

/* THE TANK'S OWN BLOBS (shelf.h): [magic][payload length u16][blobs u16][payload crc] and, for each, its key (8 bytes,
   zero-padded) + length u16 + bytes. Gathered from NVS, put back, or none (0) */
#define EXTRA_MAGIC 0x31585450u                   /* "PTX1" */
#define EXTRA_HDR   12
static const char *const TANK_BLOBS[3] = { COLL_NVS_KEY, KIN_NVS_KEY, GIFT_NVS_KEY };
static void put16(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *b, uint32_t v) { put16(b, v & 0xffff); put16(b + 2, v >> 16); }
static uint32_t get16(const uint8_t *b) { return b[0] | (uint32_t)b[1] << 8; }
static uint32_t get32(const uint8_t *b) { return get16(b) | get16(b + 2) << 16; }
_Static_assert(EXTRA_HDR + 3 * 10 + COLL_BLOB_MAX + KIN_BLOB_MAX + GIFT_BLOB_MAX <= SHELF_EXTRA_MAX, "a tank's own blobs, all full, must fit its slot");
static size_t extras_gather(uint8_t *x) {         /* the glass's, for its slot */
    collection_flush();                           /* (the time its visitors spent here since the last save) */
    size_t off = EXTRA_HDR; int n = 0;
    for (int k = 0; k < 3; k++) {
        size_t got = 0;
        if (off + 10 >= SHELF_EXTRA_MAX) break;   /* (the room left: never wraps) */
        if (!persist_port_blob_load(TANK_BLOBS[k], x + off + 10, SHELF_EXTRA_MAX - off - 10, &got) || !got) continue;
        memset(x + off, 0, 8); memcpy(x + off, TANK_BLOBS[k], strlen(TANK_BLOBS[k]) < 8 ? strlen(TANK_BLOBS[k]) : 8);
        put16(x + off + 8, (uint32_t)got); off += 10 + got; n++;
    }
    if (!n) return 0;
    put32(x, EXTRA_MAGIC); put16(x + 4, (uint32_t)(off - EXTRA_HDR)); put16(x + 6, (uint32_t)n); put32(x + 8, crc32(x + EXTRA_HDR, off - EXTRA_HDR));
    return off;
}
static size_t extras_read(int k, uint8_t *x) {    /* a slot's (checked), 0 = none */
    if (!shelf_port_read(slot_off(k) + SHELF_EXTRA_OFF, x, EXTRA_HDR) || get32(x) != EXTRA_MAGIC) return 0;
    size_t len = get16(x + 4);
    if (len > SHELF_EXTRA_MAX - EXTRA_HDR || !shelf_port_read(slot_off(k) + SHELF_EXTRA_OFF + EXTRA_HDR, x + EXTRA_HDR, len)
        || crc32(x + EXTRA_HDR, len) != get32(x + 8)) return 0;
    return EXTRA_HDR + len;
}
static bool extras_put(const uint8_t *x, size_t len, bool *touched) {   /* into NVS for the tank coming onto the glass: each of its own, the rest
                                                                          forgotten; false = a write failed (*touched: one went through first) */
    for (int k = 0; k < 3; k++) {
        const uint8_t *p = x + EXTRA_HDR, *end = x + len; bool put = false, ok = true;
        for (; len && p + 10 <= end && p + 10 + get16(p + 8) <= end; p += 10 + get16(p + 8))
            if (!strncmp((const char *)p, TANK_BLOBS[k], 8)) { ok = persist_port_blob_save(TANK_BLOBS[k], p + 10, get16(p + 8)); put = true; break; }
        if (!put) ok = persist_port_blob_forget(TANK_BLOBS[k]);
        if (!ok) return false;
        *touched = true;
    }
    return true;
}
static bool save_now(void) { return shelf_port_frame_set("now", &s_now, sizeof s_now); }
static void save_next(void) { shelf_port_frame_set("next", &s_next, sizeof s_next); }

/* a slot's header, checked (the save's checksum too: a header is written last, a cut before it = no tank) */
static bool read_hdr(int k, shelf_tank_t *h, uint8_t *save) {
    if (!shelf_port_read(slot_off(k), h, sizeof *h)) return false;
    if (h->magic != SHELF_MAGIC || h->layout != SHELF_LAYOUT || h->save_len > SHELF_SAVE_MAX) return false;
    if (h->save_len) {
        uint8_t *b = save ? save : s_x->b;               /* (b: scratch only the caller never passes) */
        if (!shelf_port_read(slot_off(k) + SHELF_SAVE_OFF, b, h->save_len) || crc32(b, h->save_len) != h->save_crc) return false;
    }
    if ((size_t)h->thumb_w * h->thumb_h * 2 > SHELF_THUMB_MAX) h->thumb_w = h->thumb_h = 0;   /* (no checksum guards the header: a
                                                                                       hand-edited one's snapshot is none, never a read past the buffer) */
    return true;
}
static bool slot_used(int k) {
    for (int i = 0; i < s_n; i++) if (s_t[i].slot == k) return true;
    for (int b = 0; b < s_nb; b++) if (s_bk[b].slot == k) return true;   /* (a visitor book is a used slot) */
    return false;
}
static int free_slot(int not_this) {                 /* round the area from the last one written: no slot wears first */
    for (int j = 1; j <= s_slots; j++) {
        int k = (s_cursor + j) % s_slots;
        if (k != not_this && !slot_used(k)) return k;
    }
    return -1;
}
static int free_slots(void) { int n = 0; for (int k = 0; k < s_slots; k++) n += !slot_used(k); return n; }
/* a whole slot: erased, the snapshot, the save, the header LAST */
static bool write_slot(int k, shelf_tank_t *h, const uint8_t *save, const uint16_t *thumb, const uint8_t *extra, size_t extra_len) {
    h->magic = SHELF_MAGIC; h->layout = SHELF_LAYOUT;
    h->save_crc = h->save_len ? crc32(save, h->save_len) : 0;
    size_t tb = (size_t)h->thumb_w * h->thumb_h * 2;
    if (tb > SHELF_THUMB_MAX) { h->thumb_w = h->thumb_h = 0; tb = 0; }
    h->thumb_crc = tb ? crc32(thumb, tb) : 0;
    if (!shelf_port_erase(slot_off(k), SHELF_SLOT_SIZE)) return false;
    if (tb && !shelf_port_write(slot_off(k) + SHELF_THUMB_OFF, thumb, tb)) return false;
    if (extra_len && !shelf_port_write(slot_off(k) + SHELF_EXTRA_OFF, extra, extra_len)) return false;
    if (h->save_len && !shelf_port_write(slot_off(k) + SHELF_SAVE_OFF, save, h->save_len)) return false;
    if (!shelf_port_write(slot_off(k), h, sizeof *h)) return false;
    s_cursor = k;
    return true;
}
static void forget_slot(int k) { shelf_port_erase(slot_off(k), 4096); }   /* the header's sector: the slot is free */
static void drop(int i) { s_t[i] = s_t[--s_n]; }
/* a swap that could be neither finished nor undone (the flash refusing writes): both tanks are whole in their slots and
   the journal stands, so the next boot tries again - until then nothing on the shelf is written (a free-looking slot may
   hold the tank that left) */
static bool s_stuck;
/* a tank onto the glass (a swap's steps 3-4): its save into NVS (len 0: none - a fresh tank), its own blobs from its slot
   `slot`, and - h not NULL - the frame's `now`. False = a write failed: `now` as it was, *touched = NVS had already
   changed (the save on the glass may no longer be the old tank's) */
static bool glass_put(const shelf_tank_t *h, const uint8_t *save, size_t len, int slot, bool *touched) {
    *touched = false;
    if (!(len ? persist_port_save(save, len) : persist_port_forget())) return false;
    *touched = true;
    if (!extras_put(s_x->x, extras_read(slot, s_x->x), touched)) return false;
    if (!h) return true;
    shelf_tank_t was = s_now;
    s_now = *h; s_now.magic = NOW_MAGIC;
    if (!save_now()) { s_now = was; return false; }
    return true;
}
static void book_scan(int k);                       /* (the visitor books, below) */
static void scan(void) {
    s_n = 0; s_nb = 0;
    for (int k = 0; k < s_slots; k++) {
        shelf_tank_t h;
        if (!read_hdr(k, &h, NULL)) { book_scan(k); continue; }
        s_t[s_n].h = h; s_t[s_n].slot = (int16_t)k; s_n++;
        if (h.id >= s_next) s_next = h.id + 1;
    }
}
static bool name_worn(const char *nm, uint32_t not_id) {   /* another tank already wears this name */
    if (s_now.id != not_id && !strcmp(s_now.name, nm)) return true;
    for (int i = 0; i < s_n; i++) if (s_t[i].h.id != not_id && !strcmp(s_t[i].h.name, nm)) return true;
    return false;
}
static int find_id(uint32_t id) { for (int i = 0; i < s_n; i++) if (s_t[i].h.id == id) return i; return -1; }

static bool rewrite(int i, const shelf_tank_t *nh);    /* (below: a tank's header written again, to a free slot) */
void shelf_boot(int64_t now_unix) {
    size_t area = shelf_port_size(), got;
    s_n = 0; s_stuck = false;
    if (!buffers()) { s_slots = 0; printf("shelf: no memory for the shelf - only the tank on the glass\n"); }
    else {
    s_slots = (int)(area / SHELF_SLOT_SIZE); if (s_slots > SHELF_SLOTS_MAX) s_slots = SHELF_SLOTS_MAX;
    }
    if (!shelf_port_frame_get("next", &s_next, sizeof s_next, &got) || got != sizeof s_next || !s_next) s_next = 2;
    bool have_now = shelf_port_frame_get("now", &s_now, sizeof s_now, &got) && got == sizeof s_now && s_now.magic == NOW_MAGIC;
    scan();
    if (!have_now) {
        /* the first boot with shelves: the tank on the glass is tank 1. Slots but no `now` (a frame that would not read):
           an ID no shelved tank wears and a free place - the leftover pass below erases a slot wearing the glass's ID */
        memset(&s_now, 0, sizeof s_now);
        s_now.magic = NOW_MAGIC; s_now.layout = SHELF_LAYOUT; s_now.id = s_n ? s_next++ : 1; s_now.type = 0;
        s_now.pos = 0;
        for (int p = 0; p < SHELF_POS_MAX; p++) { bool used = false; for (int i = 0; i < s_n && !used; i++) used = s_t[i].h.pos == p; if (!used) { s_now.pos = (uint8_t)p; break; } }
        snprintf(s_now.name, sizeof s_now.name, "MY TANK");
        for (unsigned n = 2; name_worn(s_now.name, s_now.id) && n < 1024; n++) snprintf(s_now.name, sizeof s_now.name, "TANK %u", n);
        if (s_n) printf("shelf: no record of the tank on the glass - it is tank %u, %s\n", (unsigned)s_now.id, s_now.name);
        save_now();
    }
    /* a swap cut short: the tank that left is safe in its slot (written before the journal), so finish it */
    swap_t j;
    if (shelf_port_frame_get("swap", &j, sizeof j, &got) && got == sizeof j && j.magic == SWAP_MAGIC) {
        int in = find_id(j.to_id);
        if (in >= 0 && s_t[in].slot == j.in_slot) {   /* its save not yet erased from the shelf: (re)do steps 3-5 */
            uint8_t *save = s_x->a;
            shelf_tank_t h = s_t[in].h;
            bool touched, done = read_hdr(s_t[in].slot, &h, save);
            if (done) {
                int64_t lived = (int64_t)h.banked_s + (h.time_mode == SHELF_TIME_DORMANT && now_unix > h.clock_unix ? now_unix - h.clock_unix : 0);
                if (h.save_len) progression_blob_set_saved_unix(save, h.save_len, now_unix - lived);
                done = glass_put(&h, save, h.save_len, s_t[in].slot, &touched);
            }
            if (done) {
                forget_slot(s_t[in].slot); drop(in);
                shelf_port_frame_del("swap");
                s_lived_h = j.lived_h;
                printf("shelf: a swap was cut short - finished: %s on the glass\n", s_now.name);
            } else {                                  /* it would not go onto the glass: the tank that left goes back (whole in its slot) */
                int out = find_id(j.from_id); shelf_tank_t oh;
                bool back = out >= 0 && s_t[out].slot == j.out_slot && read_hdr(j.out_slot, &oh, save) && glass_put(&oh, save, oh.save_len, j.out_slot, &touched);
                if (back && shelf_port_frame_del("swap")) {
                    shelf_port_frame_del("carry");
                    forget_slot(s_t[out].slot); drop(out);
                    printf("shelf: a swap was cut short and could not be finished - undone: %s on the glass\n", s_now.name);
                } else {
                    s_stuck = true;
                    printf("shelf: a swap could not be finished or undone (the flash refused) - both tanks kept, tried again at the next boot\n");
                }
            }
        } else shelf_port_frame_del("swap");
    }
    if (s_stuck) { if (s_now.id >= s_next) s_next = s_now.id + 1; return; }   /* (nothing erased: a slot that looks like a leftover may be the only copy) */
    /* leftovers: the tank on the glass in a slot too (a cut before the journal: the glass wins), one tank in two slots (the higher seq wins) */
    for (int i = s_n - 1; i >= 0; i--)
        if (s_t[i].h.id == s_now.id) { printf("shelf: slot %d held a copy of the tank on the glass - erased\n", s_t[i].slot); forget_slot(s_t[i].slot); drop(i); }
    for (int i = s_n - 1; i >= 0; i--)
        for (int k = 0; k < i; k++)
            if (s_t[k].h.id == s_t[i].h.id) {
                bool keep_i = s_t[i].h.seq > s_t[k].h.seq;
                printf("shelf: tank %u was in two slots - the older erased\n", (unsigned)s_t[i].h.id);
                forget_slot(keep_i ? s_t[k].slot : s_t[i].slot);
                if (keep_i) s_t[k] = s_t[i];      /* the newer takes k's place in the list */
                drop(i); break;
            }
    /* two tanks at one place (a MOVE that traded places, cut between its two writes): the later one to a free place */
    for (int i = 0; i < s_n; i++) {
        bool clash = s_t[i].h.pos == s_now.pos;
        for (int k = 0; k < i && !clash; k++) clash = s_t[k].h.pos == s_t[i].h.pos;
        if (!clash) continue;
        for (int p = 0; p < SHELF_POS_MAX; p++) if (shelf_at_pos(p) == -2) {
            printf("shelf: tank %u shared place %d - moved to %d\n", (unsigned)s_t[i].h.id, s_t[i].h.pos, p);
            shelf_tank_t h = s_t[i].h; h.pos = (uint8_t)p; rewrite(i, &h); break;
        }
    }
    /* visitor books whose tank is gone (removed by an older build, a restore that left one): erased. Parked ones stay */
    for (int b = s_nb - 1; b >= 0; b--) {
        uint32_t o = s_bk[b].owner;
        if (!have_now || (o & SHELF_BOOK_PARKED) || o == s_now.id || find_id(o) >= 0) continue;   /* (no `now` read: the glass's own ID unknown) */
        printf("shelf: slot %d held the visitor book of tank %u, gone - erased\n", s_bk[b].slot, (unsigned)o);
        shelf_port_erase(slot_off(s_bk[b].slot), SHELF_SLOT_SIZE);
        s_bk[b] = s_bk[--s_nb];
    }
    if (s_now.id >= s_next) s_next = s_now.id + 1;
    save_next();
}
const shelf_tank_t *shelf_now(void) { return &s_now; }
int  shelf_count(void) { return s_n; }
const shelf_tank_t *shelf_get(int i) { return i >= 0 && i < s_n ? &s_t[i].h : NULL; }
int  shelf_at_pos(int pos) {
    if (pos == s_now.pos) return -1;
    for (int i = 0; i < s_n; i++) if (s_t[i].h.pos == pos) return i;
    return -2;
}
int  shelf_free_places(void) { int f = free_slots() - 1; return f < 0 ? 0 : f; }   /* one slot stays free for swaps */
int  shelf_count_type(int type) { int n = s_now.type == type; for (int i = 0; i < s_n; i++) n += s_t[i].h.type == type; return n; }
bool shelf_thumb(int i, uint16_t *out, size_t max_px) {
    if (i < 0 || i >= s_n) return false;
    const shelf_tank_t *h = &s_t[i].h;
    size_t px = (size_t)h->thumb_w * h->thumb_h;
    if (!px || px > max_px || !shelf_port_read(slot_off(s_t[i].slot) + SHELF_THUMB_OFF, out, px * 2)) return false;
    return crc32(out, px * 2) == h->thumb_crc;
}

int shelf_new(int type, int64_t now_unix) {
    if (s_stuck || shelf_free_places() < 1) return -1;
    int pos = 0;
    while (pos < SHELF_POS_MAX && shelf_at_pos(pos) != -2) pos++;
    if (pos >= SHELF_POS_MAX) return -1;
    int k = free_slot(-1);
    if (k < 0) return -1;
    shelf_tank_t h; memset(&h, 0, sizeof h);
    h.id = s_next++; save_next();
    h.seq = 1; h.type = (uint8_t)type; h.pos = (uint8_t)pos; h.time_mode = SHELF_TIME_DORMANT;
    h.shelved_unix = h.clock_unix = now_unix;
    /* the lowest TANK n no tank wears (a count repeats a name once a tank is removed) */
    for (unsigned n = 2; n < 1024; n++) {
        snprintf(h.name, sizeof h.name, "TANK %u", n);
        bool used = !strcmp(s_now.name, h.name);
        for (int i = 0; i < s_n && !used; i++) used = !strcmp(s_t[i].h.name, h.name);
        if (!used) break;
    }
    if (!write_slot(k, &h, NULL, NULL, NULL, 0)) return -1;
    s_t[s_n].h = h; s_t[s_n].slot = (int16_t)k;
    return s_n++;
}

/* a tank's whole slot rewritten to a free one with seq + 1 (a header change never erases the only copy) */
static bool rewrite(int i, const shelf_tank_t *nh) {
    uint8_t *save = s_x->a; uint16_t *thumb = s_x->thumb;
    int k = s_stuck ? -1 : free_slot(s_t[i].slot);
    if (k < 0) return false;
    shelf_tank_t h = *nh;
    if (h.save_len && !read_hdr(s_t[i].slot, &(shelf_tank_t){0}, save)) return false;
    size_t tb = (size_t)h.thumb_w * h.thumb_h * 2;
    if (tb > SHELF_THUMB_MAX) { h.thumb_w = h.thumb_h = 0; tb = 0; }   /* (as write_slot: never past the buffer) */
    if (tb && !shelf_port_read(slot_off(s_t[i].slot) + SHELF_THUMB_OFF, thumb, tb)) return false;
    h.seq = s_t[i].h.seq + 1;
    size_t xl = extras_read(s_t[i].slot, s_x->x);   /* (its visitors, KIN and gift notes go with it) */
    if (!write_slot(k, &h, save, thumb, s_x->x, xl)) return false;
    forget_slot(s_t[i].slot);
    s_t[i].h = h; s_t[i].slot = (int16_t)k;
    return true;
}
static bool set_pos(int i, int pos) {                   /* (i -1: the tank on the glass) */
    if (i == -1) { s_now.pos = (uint8_t)pos; save_now(); return true; }
    shelf_tank_t h = s_t[i].h; h.pos = (uint8_t)pos;
    return rewrite(i, &h);
}
bool shelf_move(int i, int pos) {
    if ((i != -1 && (i < 0 || i >= s_n)) || pos < 0 || pos >= SHELF_POS_MAX) return false;
    int from = i == -1 ? s_now.pos : s_t[i].h.pos, there = shelf_at_pos(pos);
    if (pos == from) return true;
    if (!set_pos(i, pos)) return false;
    if (there != -2 && !set_pos(there, from)) return false;   /* (a tank there: it takes the other's old place) */
    return true;
}
int shelf_pieces_value(int i) {
    if (i < 0 || i >= s_n || !s_t[i].h.save_len) return 0;
    shelf_tank_t h;
    if (!read_hdr(s_t[i].slot, &h, s_x->a)) return 0;
    uint32_t u = progression_blob_unlocks(s_x->a, h.save_len);
    int v = 0;
    for (int item = 0; item < SD_ITEM_COUNT; item++) if ((u & SD_ITEMS[item].bit) && tank_decor_placeable(item)) v += progression_sell_value(item);
    return v;
}
bool shelf_remove(int i) {
    if (s_stuck || i < 0 || i >= s_n) return false;
    shelf_book_forget(s_t[i].h.id);                   /* (its visitor book with it) */
    forget_slot(s_t[i].slot);
    drop(i);
    return true;
}

bool shelf_set_time(int i, int mode, int64_t now_unix) {
    if (i < 0 || i >= s_n || s_t[i].h.time_mode == mode) return i >= 0 && i < s_n;
    shelf_tank_t h = s_t[i].h;
    if (mode == SHELF_TIME_PAUSED) {             /* the DORMANT stretch so far is kept for the swap-in */
        int64_t d = now_unix > h.clock_unix ? now_unix - h.clock_unix : 0;
        int64_t b = (int64_t)h.banked_s + d;
        h.banked_s = (uint32_t)(b > PROGRESSION_SLEEP_CAP_S ? PROGRESSION_SLEEP_CAP_S : b);   /* the wake caps it anyway (D12) */
    } else h.clock_unix = now_unix;
    h.time_mode = (uint8_t)mode;
    return rewrite(i, &h);
}

bool shelf_rename(int i, const char *name) {
    if (i != -1 && (i < 0 || i >= s_n)) return false;
    shelf_tank_t h = i == -1 ? s_now : s_t[i].h;
    char ln[2][SHELF_NAME_MAX + 1]; int nl = 0;
    for (int row = 0; row < 2; row++) {                     /* each line trimmed at both ends */
        const char *e = row ? "" : strchr(name, '\n');
        const char *p = row ? (strchr(name, '\n') ? strchr(name, '\n') + 1 : "") : name;
        int len = row ? (int)strlen(p) : (e ? (int)(e - p) : (int)strlen(p));
        while (len > 0 && *p == ' ') { p++; len--; }
        while (len > 0 && p[len - 1] == ' ') len--;
        if (len > SHELF_NAME_MAX) len = SHELF_NAME_MAX;
        memcpy(ln[row], p, (size_t)len); ln[row][len] = 0;
    }
    char nm[sizeof h.name]; memset(nm, 0, sizeof nm);
    if (ln[0][0] && ln[1][0]) {                             /* two lines: 7 each (a plate line), the break between */
        size_t a = strlen(ln[0]), b = strlen(ln[1]); if (a > 7) a = 7; if (b > 7) b = 7;
        memcpy(nm, ln[0], a); nm[a] = '\n'; memcpy(nm + a + 1, ln[1], b);
    } else if (ln[0][0] || ln[1][0]) memcpy(nm, ln[0][0] ? ln[0] : ln[1], strlen(ln[0][0] ? ln[0] : ln[1]));   /* (one line: up to 15, an old name) */
    else {                                                  /* its place's TANK n - or, when a MOVE left that on another tank, the lowest n none wears */
        snprintf(nm, sizeof nm, "TANK %u", (unsigned)h.pos + 1);
        for (unsigned n = 2; name_worn(nm, h.id) && n < 1024; n++) snprintf(nm, sizeof nm, "TANK %u", n);
    }
    (void)nl;
    if (!memcmp(nm, h.name, sizeof nm)) return true;
    memcpy(h.name, nm, sizeof h.name);
    if (i == -1) { s_now = h; save_now(); return true; }
    return rewrite(i, &h);
}

bool shelf_swap(int i, const uint16_t *thumb, int w, int h_px, const void *carry, size_t carry_len, int64_t now_unix) {
    if (s_stuck || i < 0 || i >= s_n) return false;
    uint8_t *out_save = s_x->a, *in_save = s_x->b;
    size_t out_len = 0;
    if (!persist_port_load(out_save, SHELF_SAVE_MAX, &out_len)) out_len = 0;   /* nothing saved yet: it goes up fresh */
    if (out_len > SHELF_SAVE_MAX) {               /* (a newer build's longer save, after a rollback: it would not fit its slot) */
        printf("shelf: the save on the glass is %u bytes, a slot holds %u - no swap\n", (unsigned)out_len, (unsigned)SHELF_SAVE_MAX);
        return false;
    }
    /* 1. the tank on the glass -> a free slot */
    int k = free_slot(s_t[i].slot);
    if (k < 0) return false;
    shelf_tank_t out = s_now;
    out.seq = 1; out.save_len = (uint16_t)out_len; out.shelved_unix = out.clock_unix = now_unix; out.banked_s = 0;
    out.thumb_w = (uint16_t)w; out.thumb_h = (uint16_t)h_px;
    if (!write_slot(k, &out, out_save, thumb, s_x->x, extras_gather(s_x->x))) return false;   /* (its own blobs with it) */
    /* the incoming tank, read before anything else changes */
    shelf_tank_t in = s_t[i].h;
    if (!read_hdr(s_t[i].slot, &in, in_save)) { forget_slot(k); return false; }
    int64_t lived = (int64_t)in.banked_s + (in.time_mode == SHELF_TIME_DORMANT && now_unix > in.clock_unix ? now_unix - in.clock_unix : 0);
    /* 2. the journal (and what the board carries across: without it the wallet would stay behind) */
    if (carry && carry_len && !shelf_port_frame_set("carry", carry, carry_len)) { forget_slot(k); return false; }
    swap_t j = { SWAP_MAGIC, s_now.id, in.id, (int16_t)k, s_t[i].slot, 0, lived / 3600.0f };
    if (!shelf_port_frame_set("swap", &j, sizeof j)) { shelf_port_frame_del("carry"); forget_slot(k); return false; }   /* (a carry left
                                                                                       behind would set the wallet back at a later boot) */
    /* 3. its save into NVS: the wake lives `lived` from it (capped, D12); a fresh tank: no save, born at the boot - and its
       visitors, KIN and gift notes (none: a fresh tank's NVS has none); 4. it is the tank on the glass */
    if (in.save_len) progression_blob_set_saved_unix(in_save, in.save_len, now_unix - lived);
    bool touched;
    if (!glass_put(&in, in_save, in.save_len, s_t[i].slot, &touched)) {
        /* a write refused (NVS full or worn, saving off): undone - the tank that left goes back from its slot k, where it is
           whole; the journal goes before slot k (a journal left over would finish the swap at the boot, from that slot) */
        bool back = !touched || glass_put(NULL, out_save, out_len, k, &touched);
        if (back && shelf_port_frame_del("swap")) {
            shelf_port_frame_del("carry"); forget_slot(k);
            printf("shelf: the swap could not write the tank coming in - undone, %s stays on the glass\n", s_now.name);
        } else {
            s_stuck = true;
            printf("shelf: the swap could not write the tank coming in, nor undo - both tanks kept, finished at the next boot\n");
        }
        return false;
    }
    /* 5. its slot freed, the journal closed */
    forget_slot(s_t[i].slot);
    shelf_port_frame_del("swap");
    s_lived_h = j.lived_h;
    s_t[i].h = out; s_t[i].slot = (int16_t)k;
    return true;
}
bool shelf_take_carry(void *buf, size_t len) {
    if (s_stuck) return false;                    /* (a swap still to finish: its carry is for the boot that finishes it) */
    size_t got;
    bool ok = shelf_port_frame_get("carry", buf, len, &got) && got == len;
    shelf_port_frame_del("carry");
    return ok;
}
float shelf_lived_h(void) { return s_lived_h; }
void shelf_keeper_name(char *out) {
    unsigned n; char more;
    if (!s_now.name[0] || !strcmp(s_now.name, "MY TANK") || sscanf(s_now.name, "TANK %u%c", &n, &more) == 1) { out[0] = 0; return; }
    passport_clean_tank_name(out, s_now.name);
}

/* ---- THE VISITOR BOOK (shelf.h) ---- */
static uint32_t book_crc(const uint8_t *sec, size_t len) { return ~crc_run(crc_run(0xffffffffu, sec, 28), sec + SHELF_BOOK_HDR, len); }
/* sector s of slot k into s_x->sec, checked: false = not a book's sector */
static bool book_sector(int k, int s) {
    uint8_t *h = s_x->sec;
    if (!shelf_port_read(slot_off(k) + (uint32_t)s * 4096, h, SHELF_BOOK_HDR) || get32(h) != SHELF_BOOK_MAGIC) return false;
    size_t len = (size_t)h[17] * get16(h + 18);
    if (h[16] != s / 2 || h[17] > SHELF_BOOK_RECS || get16(h + 18) < 64 || len > SHELF_BOOK_PAYLOAD) return false;
    if (len && !shelf_port_read(slot_off(k) + (uint32_t)s * 4096 + SHELF_BOOK_HDR, h + SHELF_BOOK_HDR, len)) return false;
    return book_crc(h, len) == get32(h + 28);
}
static void book_scan(int k) {                     /* a slot that is no tank: a book's? (its group 0, then the other two) */
    if (!s_bk || s_nb >= SHELF_SLOTS_MAX) return;
    book_t b; memset(&b, 0, sizeof b); b.slot = (int16_t)k;
    uint32_t best[SHELF_BOOK_GROUPS] = { 0 };
    for (int g = 0; g < SHELF_BOOK_GROUPS; g++) b.cur[g] = -1;
    for (int s = 0; s < 2 * SHELF_BOOK_GROUPS; s++) {
        if (s >= 2 && b.cur[0] < 0) return;       /* (no group 0: not a book - a free slot reads two heads, no more) */
        if (!book_sector(k, s)) continue;
        const uint8_t *h = s_x->sec; uint32_t seq = get32(h + 12); int g = s / 2;
        if (b.cur[g] >= 0 && seq <= best[g]) continue;
        if (g == 0) { b.owner = get32(h + 4); b.print = get32(h + 8); }
        else if (get32(h + 8) != b.print) continue;
        b.cur[g] = (int8_t)(s & 1); best[g] = seq;
        if (seq > b.seq) b.seq = seq;
    }
    if (b.cur[0] < 0) return;
    for (int i = 0; i < s_nb; i++)                 /* one tank's book in two slots (never made so: a hand-made board): the newer kept */
        if (s_bk[i].owner == b.owner && s_bk[i].print == b.print) {
            int gone = s_bk[i].seq >= b.seq ? k : s_bk[i].slot;
            printf("shelf: tank %u's visitor book was in two slots - the older erased\n", (unsigned)b.owner);
            shelf_port_erase(slot_off(gone), SHELF_SLOT_SIZE);
            if (gone != k) s_bk[i] = b;
            return;
        }
    s_bk[s_nb++] = b;
}
static int book_find(uint32_t owner, uint32_t print) {
    for (int b = 0; b < s_nb; b++) if (s_bk[b].owner == owner && s_bk[b].print == print) return b;
    return -1;
}
int shelf_book_count(void) { return s_nb; }
uint8_t *shelf_book_buf(void) { return s_x ? s_x->sec + SHELF_BOOK_HDR : NULL; }
int shelf_book_read(uint32_t owner, uint32_t print, int group, const uint8_t **recs, int *rec) {
    int b = owner && s_x ? book_find(owner, print) : -1;
    if (b < 0 || group < 0 || group >= SHELF_BOOK_GROUPS || s_bk[b].cur[group] < 0) return 0;
    if (!book_sector(s_bk[b].slot, 2 * group + s_bk[b].cur[group])) return 0;
    *recs = s_x->sec + SHELF_BOOK_HDR; *rec = (int)get16(s_x->sec + 18);
    return s_x->sec[17];
}
static int book_stale(uint32_t owner) {            /* a book of this tank's with another first page (a reset under an older build) */
    for (int b = 0; b < s_nb; b++) if (s_bk[b].owner == owner) return b;
    return -1;
}
bool shelf_book_room(uint32_t owner, uint32_t print) {
    if (!owner || (owner & SHELF_BOOK_PARKED) || s_stuck || !s_x || !s_bk) return false;
    return book_find(owner, print) >= 0 || book_stale(owner) >= 0 || free_slots() >= 2;   /* (one slot stays free for swaps) */
}
static int book_make(uint32_t owner, uint32_t print) {
    int b = book_stale(owner);
    if (b >= 0) printf("shelf: tank %u's old visitor book (another first page) written over\n", (unsigned)owner);
    else {
        if (s_nb >= SHELF_SLOTS_MAX || free_slots() < 2) return -1;
        int k = free_slot(-1);
        if (k < 0) return -1;
        b = s_nb++; s_bk[b].slot = (int16_t)k;
    }
    if (!shelf_port_erase(slot_off(s_bk[b].slot), SHELF_SLOT_SIZE)) { s_bk[b] = s_bk[--s_nb]; return -1; }   /* (no other book's sector left) */
    s_bk[b].owner = owner; s_bk[b].print = print; s_bk[b].seq = 0;
    for (int g = 0; g < SHELF_BOOK_GROUPS; g++) s_bk[b].cur[g] = -1;
    s_cursor = s_bk[b].slot;
    return b;
}
/* group g of book b from s_x->sec's payload (count x rec), as `owner`'s: into its older copy, the head written last */
static bool book_put(int b, int group, uint32_t owner, int count, int rec) {
    uint8_t *h = s_x->sec; size_t len = (size_t)count * rec;
    int copy = s_bk[b].cur[group] == 0 ? 1 : 0;
    uint32_t off = slot_off(s_bk[b].slot) + (uint32_t)(2 * group + copy) * 4096;
    memset(h, 0, SHELF_BOOK_HDR);
    put32(h, SHELF_BOOK_MAGIC); put32(h + 4, owner); put32(h + 8, s_bk[b].print); put32(h + 12, s_bk[b].seq + 1);
    h[16] = (uint8_t)group; h[17] = (uint8_t)count; put16(h + 18, (uint32_t)rec);
    put32(h + 28, book_crc(h, len));
    if (!shelf_port_erase(off, 4096)) return false;
    if (len && !shelf_port_write(off + SHELF_BOOK_HDR, h + SHELF_BOOK_HDR, len)) return false;
    if (!shelf_port_write(off, h, SHELF_BOOK_HDR)) return false;
    s_bk[b].seq++; s_bk[b].cur[group] = (int8_t)copy;
    return true;
}
bool shelf_book_write(uint32_t owner, uint32_t print, int group, int count, int rec) {
    if (!owner || (owner & SHELF_BOOK_PARKED) || s_stuck || !s_x || !s_bk || group < 0 || group >= SHELF_BOOK_GROUPS
        || count < 0 || count > SHELF_BOOK_RECS || rec < 64 || (size_t)count * rec > SHELF_BOOK_PAYLOAD) return false;
    int b = book_find(owner, print);
    if (b < 0 && !count) return true;             /* (nothing to keep and no book: none made) */
    if (b < 0 && (group || (b = book_make(owner, print)) < 0)) return false;   /* (a book starts at its group 0: groups fill in order) */
    return book_put(b, group, owner, count, rec);   /* (book_make erased flash, never s_x->sec: the payload is as the caller left it) */
}
void shelf_book_forget(uint32_t owner) {
    if (!owner || s_stuck) return;
    for (int b = s_nb - 1; b >= 0; b--)
        if (s_bk[b].owner == owner) { shelf_port_erase(slot_off(s_bk[b].slot), SHELF_SLOT_SIZE); s_bk[b] = s_bk[--s_nb]; }
}
int shelf_book_rekey(uint32_t from, uint32_t to) {
    int n = 0;
    if (!from || !to || from == to || s_stuck || !s_x) return 0;
    for (int b = 0; b < s_nb; b++) {
        if (s_bk[b].owner != from || s_bk[b].cur[0] < 0 || book_find(to, s_bk[b].print) >= 0) continue;
        if (!book_sector(s_bk[b].slot, 2 * 0 + s_bk[b].cur[0])) continue;   /* group 0 read back, written again as the new owner's (it names the book) */
        if (book_put(b, 0, to, s_x->sec[17], (int)get16(s_x->sec + 18))) { s_bk[b].owner = to; n++; }
    }
    return n;
}
