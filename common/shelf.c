/* shelf.c - MY TANKS: the tanks a board keeps. See shelf.h. */
#include "shelf.h"
#include "progression.h"
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
typedef struct { uint8_t a[SHELF_SAVE_MAX], b[SHELF_SAVE_MAX]; uint16_t thumb[SHELF_THUMB_MAX / 2]; } scratch_t;
static scratch_t *s_x;
static bool buffers(void) {
    if (!s_t) s_t = calloc(SHELF_SLOTS_MAX, sizeof *s_t);
    if (!s_x) s_x = malloc(sizeof *s_x);
    return s_t && s_x;
}
static int s_n, s_slots, s_cursor;
static uint32_t s_next;
static float s_lived_h = -1;

static uint32_t crc32(const void *p, size_t n) {
    const uint8_t *b = p; uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) { c ^= b[i]; for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xedb88320u : c >> 1; }
    return ~c;
}
static uint32_t slot_off(int k) { return (uint32_t)k * SHELF_SLOT_SIZE; }
static void save_now(void) { shelf_port_frame_set("now", &s_now, sizeof s_now); }
static void save_next(void) { shelf_port_frame_set("next", &s_next, sizeof s_next); }

/* a slot's header, checked (the save's checksum too: a header is written last, a cut before it = no tank) */
static bool read_hdr(int k, shelf_tank_t *h, uint8_t *save) {
    if (!shelf_port_read(slot_off(k), h, sizeof *h)) return false;
    if (h->magic != SHELF_MAGIC || h->layout != SHELF_LAYOUT || h->save_len > SHELF_SAVE_MAX) return false;
    if (h->save_len) {
        uint8_t *b = save ? save : s_x->b;               /* (b: scratch only the caller never passes) */
        if (!shelf_port_read(slot_off(k) + SHELF_SAVE_OFF, b, h->save_len) || crc32(b, h->save_len) != h->save_crc) return false;
    }
    return true;
}
static bool slot_used(int k) { for (int i = 0; i < s_n; i++) if (s_t[i].slot == k) return true; return false; }
static int free_slot(int not_this) {                 /* round the area from the last one written: no slot wears first */
    for (int j = 1; j <= s_slots; j++) {
        int k = (s_cursor + j) % s_slots;
        if (k != not_this && !slot_used(k)) return k;
    }
    return -1;
}
static int free_slots(void) { int n = 0; for (int k = 0; k < s_slots; k++) n += !slot_used(k); return n; }
/* a whole slot: erased, the snapshot, the save, the header LAST */
static bool write_slot(int k, shelf_tank_t *h, const uint8_t *save, const uint16_t *thumb) {
    h->magic = SHELF_MAGIC; h->layout = SHELF_LAYOUT;
    h->save_crc = h->save_len ? crc32(save, h->save_len) : 0;
    size_t tb = (size_t)h->thumb_w * h->thumb_h * 2;
    if (tb > SHELF_THUMB_MAX) { h->thumb_w = h->thumb_h = 0; tb = 0; }
    h->thumb_crc = tb ? crc32(thumb, tb) : 0;
    if (!shelf_port_erase(slot_off(k), SHELF_SLOT_SIZE)) return false;
    if (tb && !shelf_port_write(slot_off(k) + SHELF_THUMB_OFF, thumb, tb)) return false;
    if (h->save_len && !shelf_port_write(slot_off(k) + SHELF_SAVE_OFF, save, h->save_len)) return false;
    if (!shelf_port_write(slot_off(k), h, sizeof *h)) return false;
    s_cursor = k;
    return true;
}
static void forget_slot(int k) { shelf_port_erase(slot_off(k), 4096); }   /* the header's sector: the slot is free */
static void drop(int i) { s_t[i] = s_t[--s_n]; }
static void scan(void) {
    s_n = 0;
    for (int k = 0; k < s_slots; k++) {
        shelf_tank_t h;
        if (!read_hdr(k, &h, NULL)) continue;
        s_t[s_n].h = h; s_t[s_n].slot = (int16_t)k; s_n++;
        if (h.id >= s_next) s_next = h.id + 1;
    }
}
static int find_id(uint32_t id) { for (int i = 0; i < s_n; i++) if (s_t[i].h.id == id) return i; return -1; }

static bool rewrite(int i, const shelf_tank_t *nh);    /* (below: a tank's header written again, to a free slot) */
void shelf_boot(int64_t now_unix) {
    size_t area = shelf_port_size(), got;
    s_n = 0;
    if (!buffers()) { s_slots = 0; printf("shelf: no memory for the shelf - only the tank on the glass\n"); }
    else {
    s_slots = (int)(area / SHELF_SLOT_SIZE); if (s_slots > SHELF_SLOTS_MAX) s_slots = SHELF_SLOTS_MAX;
    }
    if (!shelf_port_frame_get("next", &s_next, sizeof s_next, &got) || got != sizeof s_next || !s_next) s_next = 2;
    if (!shelf_port_frame_get("now", &s_now, sizeof s_now, &got) || got != sizeof s_now || s_now.magic != NOW_MAGIC) {
        memset(&s_now, 0, sizeof s_now);          /* the first boot with shelves: the tank on the glass is tank 1 */
        s_now.magic = NOW_MAGIC; s_now.layout = SHELF_LAYOUT; s_now.id = 1; s_now.type = 0; s_now.pos = 0;
        snprintf(s_now.name, sizeof s_now.name, "MY TANK");
        save_now();
    }
    scan();
    /* a swap cut short: the tank that left is safe in its slot (written before the journal), so finish it */
    swap_t j;
    if (shelf_port_frame_get("swap", &j, sizeof j, &got) && got == sizeof j && j.magic == SWAP_MAGIC) {
        int in = find_id(j.to_id);
        if (in >= 0 && s_t[in].slot == j.in_slot) {   /* its save not yet erased from the shelf: (re)do steps 3-5 */
            uint8_t *save = s_x->a;
            shelf_tank_t h = s_t[in].h;
            read_hdr(s_t[in].slot, &h, save);
            int64_t lived = (int64_t)h.banked_s + (h.time_mode == SHELF_TIME_DORMANT && now_unix > h.clock_unix ? now_unix - h.clock_unix : 0);
            if (h.save_len) { progression_blob_set_saved_unix(save, h.save_len, now_unix - lived); persist_port_save(save, h.save_len); }
            else persist_port_forget();
            s_now = h; s_now.magic = NOW_MAGIC; save_now();
            forget_slot(s_t[in].slot); drop(in);
            s_lived_h = j.lived_h;
            printf("shelf: a swap was cut short - finished: %s on the glass\n", s_now.name);
        }
        shelf_port_frame_del("swap");
    }
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
    if (shelf_free_places() < 1) return -1;
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
    if (!write_slot(k, &h, NULL, NULL)) return -1;
    s_t[s_n].h = h; s_t[s_n].slot = (int16_t)k;
    return s_n++;
}

/* a tank's whole slot rewritten to a free one with seq + 1 (a header change never erases the only copy) */
static bool rewrite(int i, const shelf_tank_t *nh) {
    uint8_t *save = s_x->a; uint16_t *thumb = s_x->thumb;
    int k = free_slot(s_t[i].slot);
    if (k < 0) return false;
    shelf_tank_t h = *nh;
    if (h.save_len && !read_hdr(s_t[i].slot, &(shelf_tank_t){0}, save)) return false;
    size_t tb = (size_t)h.thumb_w * h.thumb_h * 2;
    if (tb && !shelf_port_read(slot_off(s_t[i].slot) + SHELF_THUMB_OFF, thumb, tb)) return false;
    h.seq = s_t[i].h.seq + 1;
    if (!write_slot(k, &h, save, thumb)) return false;
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
    if (i < 0 || i >= s_n) return false;
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
    else snprintf(nm, sizeof nm, "TANK %u", (unsigned)h.pos + 1);
    (void)nl;
    if (!memcmp(nm, h.name, sizeof nm)) return true;
    memcpy(h.name, nm, sizeof h.name);
    if (i == -1) { s_now = h; save_now(); return true; }
    return rewrite(i, &h);
}

bool shelf_swap(int i, const uint16_t *thumb, int w, int h_px, const void *carry, size_t carry_len, int64_t now_unix) {
    if (i < 0 || i >= s_n) return false;
    uint8_t *out_save = s_x->a, *in_save = s_x->b;
    size_t out_len = 0;
    if (!persist_port_load(out_save, SHELF_SAVE_MAX, &out_len)) out_len = 0;   /* nothing saved yet: it goes up fresh */
    /* 1. the tank on the glass -> a free slot */
    int k = free_slot(s_t[i].slot);
    if (k < 0) return false;
    shelf_tank_t out = s_now;
    out.seq = 1; out.save_len = (uint16_t)out_len; out.shelved_unix = out.clock_unix = now_unix; out.banked_s = 0;
    out.thumb_w = (uint16_t)w; out.thumb_h = (uint16_t)h_px;
    if (!write_slot(k, &out, out_save, thumb)) return false;
    /* the incoming tank, read before anything else changes */
    shelf_tank_t in = s_t[i].h;
    if (!read_hdr(s_t[i].slot, &in, in_save)) { forget_slot(k); return false; }
    int64_t lived = (int64_t)in.banked_s + (in.time_mode == SHELF_TIME_DORMANT && now_unix > in.clock_unix ? now_unix - in.clock_unix : 0);
    /* 2. the journal (and what the board carries across) */
    if (carry && carry_len) shelf_port_frame_set("carry", carry, carry_len);
    swap_t j = { SWAP_MAGIC, s_now.id, in.id, (int16_t)k, s_t[i].slot, 0, lived / 3600.0f };
    if (!shelf_port_frame_set("swap", &j, sizeof j)) { forget_slot(k); return false; }
    /* 3. its save into NVS: the wake lives `lived` from it (capped, D12); a fresh tank: no save, born at the boot */
    if (in.save_len) { progression_blob_set_saved_unix(in_save, in.save_len, now_unix - lived); persist_port_save(in_save, in.save_len); }
    else persist_port_forget();
    /* 4. it is the tank on the glass; 5. its slot freed, the journal closed */
    s_now = in; s_now.magic = NOW_MAGIC; save_now();
    forget_slot(s_t[i].slot);
    shelf_port_frame_del("swap");
    s_lived_h = j.lived_h;
    s_t[i].h = out; s_t[i].slot = (int16_t)k;
    return true;
}
bool shelf_take_carry(void *buf, size_t len) {
    size_t got;
    bool ok = shelf_port_frame_get("carry", buf, len, &got) && got == len;
    shelf_port_frame_del("carry");
    return ok;
}
float shelf_lived_h(void) { return s_lived_h; }
