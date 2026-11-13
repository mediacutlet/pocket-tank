/* collection.c — the VISITOR COLLECTION (collection.h). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "collection.h"
#include "progression.h"
#include "passport.h"
#include "shelf.h"

/* the blob: a 6-byte header (magic, version, count, record size) and the
   records, little-endian. A later build may append fields to a record (a
   larger record size): we read the head of each, and keep our own size */
#define COLL_MAGIC   0x4356u   /* "VC" */
#define COLL_VERSION 1
#define COLL_REC     76        /* 64 until 2026-11-01: + the home tank's name (57..72) */
#define COLL_REC_V1  64
#define COLL_HDR     6
_Static_assert(COLL_HDR + COLL_PAGE * COLL_REC == COLL_BLOB_MAX, "COLL_BLOB_MAX (collection.h) is the first page's blob, full");
_Static_assert(COLL_MAX - COLL_PAGE == SHELF_BOOK_GROUPS * SHELF_BOOK_RECS, "pages 2..10 are the visitor book's three groups");
_Static_assert(SHELF_BOOK_RECS * COLL_REC <= SHELF_BOOK_PAYLOAD, "a book group's records fit its sector");

/* the first page: statics, as before pages (internal RAM unchanged); pages 2..10: the PSRAM block (collection_set_more) */
static coll_entry_t s_e[COLL_PAGE];
typedef struct { coll_entry_t e[COLL_MAX - COLL_PAGE]; float frac[COLL_MAX - COLL_PAGE]; } coll_more_t;
static coll_more_t *s_more;
static int s_n;

/* every visitor seen since boot (not saved): its history before it is
   collected - first met, visits, time here */
#define SEEN_MAX 8
static struct { uint32_t uid, met; uint32_t here_ms; uint16_t visits; } s_seen[SEEN_MAX];
static float s_frac[COLL_PAGE];   /* (the part of a second not yet counted into here_s) */
static float s_seen_frac[SEEN_MAX];
/* what waits for a save: bit 0 the first page (NVS), bit 1 + g the book's group g (its sector) */
static unsigned s_dirty;
static bool s_p1_unread;          /* the last load could not read the first page (no RAM for it): never written over until one does */
static coll_entry_t *E(int i) { return i < COLL_PAGE ? &s_e[i] : &s_more->e[i - COLL_PAGE]; }
static float *FRAC(int i) { return i < COLL_PAGE ? &s_frac[i] : &s_more->frac[i - COLL_PAGE]; }
static unsigned part_of(int i) { return i < COLL_PAGE ? 1u : 2u << ((i - COLL_PAGE) / SHELF_BOOK_RECS); }
static int cap(void) { return s_more ? COLL_MAX : COLL_PAGE; }
size_t collection_more_size(void) { return sizeof(coll_more_t); }
void collection_set_more(void *block) { s_more = block; if (s_more) memset(s_more, 0, sizeof *s_more); }
/* the book's owner and print (shelf.h): the tank on the glass, its first page's 12 IDs (once the page is full) */
static uint32_t book_owner(void) { return shelf_now()->id; }
static uint32_t book_print(void) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < COLL_PAGE && i < s_n; i++) for (int b = 0; b < 4; b++) { h ^= (s_e[i].uid >> (8 * b)) & 255; h *= 16777619u; }
    return h ? h : 1;
}

static uint32_t now_unix(void) { int64_t u = clock_port_now_unix(); return u > 0 ? (uint32_t)u : 0; }

/* ---- kin ---- */
#define KIN_MAGIC 0x4b4eu   /* "NK" */
#define KIN_REC   16
_Static_assert(6 + KIN_MAX * KIN_REC == KIN_BLOB_MAX, "KIN_BLOB_MAX (collection.h) is the full blob");
static kin_t s_kin[KIN_MAX];
static int s_kin_n, s_kin_next;
static bool s_kin_loaded;
/* the KIN book is written at most once a KIN_SAVE_GAP_S, its changes coalesced (the evening sweep: a modded board next door
   offering fresh uids made the tank write NVS many times a second), and on every tank save, sleep and SWAP IN (kin_flush) */
#define KIN_SAVE_GAP_S 60.0f
static bool s_kin_dirty;
static float s_kin_wait = KIN_SAVE_GAP_S;               /* seconds of running time since the last write */
static void kin_save(void);
static void put16(uint8_t *b, int o, uint32_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *b, int o, uint32_t v) { put16(b, o, v & 0xffff); put16(b, o + 2, v >> 16); }
static uint32_t get16(const uint8_t *b, int o) { return b[o] | (uint32_t)b[o + 1] << 8; }
static uint32_t get32(const uint8_t *b, int o) { return get16(b, o) | get16(b, o + 2) << 16; }

static void rec_write(const coll_entry_t *e, uint8_t *b) {
    memset(b, 0, COLL_REC);
    put32(b, 0, e->uid); put32(b, 4, e->home); put32(b, 8, e->parent[0]); put32(b, 12, e->parent[1]);
    memcpy(b + 16, e->name, FISH_NAME_MAX);                            /* 16..23: the name, unterminated */
    put32(b, 24, e->color & 0xffffff); put32(b, 28, e->fin & 0xffffff); put32(b, 32, e->accent & 0xffffff);
    b[36] = e->preset; b[37] = e->stage; b[38] = e->shape; b[39] = e->fins; b[40] = e->pattern; b[41] = (uint8_t)e->badge;
    put32(b, 42, e->met_unix); put32(b, 46, e->seen_unix); put32(b, 50, e->here_s);
    put16(b, 54, e->visits); b[56] = e->fry;
    memcpy(b + 57, e->home_name, 16);
}
static void rec_read(coll_entry_t *e, const uint8_t *b, int rec) {
    memset(e, 0, sizeof *e);
    e->uid = get32(b, 0); e->home = get32(b, 4); e->parent[0] = get32(b, 8); e->parent[1] = get32(b, 12);
    memcpy(e->name, b + 16, FISH_NAME_MAX); e->name[FISH_NAME_MAX] = 0;
    for (int i = 0; i < FISH_NAME_MAX && e->name[i]; i++) {           /* a damaged name reads as '?' */
        char c = e->name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) e->name[i] = '?';
    }
    e->color = get32(b, 24) & 0xffffff; e->fin = get32(b, 28) & 0xffffff; e->accent = get32(b, 32) & 0xffffff;
    e->preset = (uint8_t)(b[36] % tank_roster_count()); e->stage = b[37] & 3;
    e->shape = b[38] < SHAPE_N ? b[38] : 0; e->fins = b[39] < FINS_N ? b[39] : 0; e->pattern = b[40] < PATTERN_N ? b[40] : 0;
    e->badge = (int8_t)b[41]; if (e->badge >= 4) e->badge = -1;
    e->met_unix = get32(b, 42); e->seen_unix = get32(b, 46); e->here_s = get32(b, 50);
    e->visits = (uint16_t)get16(b, 54); e->fry = b[56];
    if (rec >= 73) { char raw[16]; memcpy(raw, b + 57, 16); raw[15] = 0; passport_clean_tank_name(e->home_name, raw); }   /* (a 64-byte record: none) */
}

/* the parts in `parts` (s_dirty's bits) written: the first page as the "visitors" blob it always was (never more than
   12: an older build reads it), each book group into its sector. False = a part the flash or the shelf refused - the first
   page's too (0.4 rc: a refused page 1 still said COLLECTED, and the visitor was gone at the next boot) */
static bool save_parts(unsigned parts) {
    bool ok = true;
    if (parts & 1) {
        int n = s_n < COLL_PAGE ? s_n : COLL_PAGE;
        uint8_t *blob = s_p1_unread ? NULL : malloc(COLL_BLOB_MAX);   /* (only while saving: the internal RAM is tight) */
        if (!blob) ok = false;                        /* (no RAM, or a page 1 this boot never read: the stored one stands) */
        else {
            put16(blob, 0, COLL_MAGIC); blob[2] = COLL_VERSION; blob[3] = (uint8_t)n; put16(blob, 4, COLL_REC);
            for (int i = 0; i < n; i++) rec_write(&s_e[i], blob + COLL_HDR + i * COLL_REC);
            if (!persist_port_blob_save(COLL_NVS_KEY, blob, (size_t)(COLL_HDR + n * COLL_REC))) { ok = false; printf("collection: the first page was not saved\n"); }
            free(blob);
        }
    }
    for (int g = 0; g < SHELF_BOOK_GROUPS; g++) {
        if (!(parts & (2u << g)) || !s_more) continue;
        int first = COLL_PAGE + g * SHELF_BOOK_RECS, n = s_n - first;
        if (n <= 0) continue;
        if (n > SHELF_BOOK_RECS) n = SHELF_BOOK_RECS;
        uint8_t *b = shelf_book_buf();
        if (!b) { ok = false; continue; }
        for (int j = 0; j < n; j++) rec_write(E(first + j), b + j * COLL_REC);   /* (straight into the shelf's PSRAM sector) */
        if (!shelf_book_write(book_owner(), book_print(), g, n, COLL_REC)) { ok = false; printf("collection: the visitor book's pages %d..%d were not saved\n", 2 + 3 * g, 4 + 3 * g); }
    }
    return ok;
}
static void save(unsigned more) { unsigned p = s_dirty | more; s_dirty = save_parts(p) ? 0 : p; }   /* (refused: it waits for the next save) */
void collection_load(void) {
    const size_t cap = COLL_HDR + COLL_MAX * 128;     /* (room for a later build's longer records) */
    uint8_t *blob = malloc(cap);
    size_t got = 0;
    s_n = 0; s_dirty = 0; memset(s_frac, 0, sizeof s_frac);
    if (s_more) memset(s_more->frac, 0, sizeof s_more->frac);
    s_kin_loaded = false; s_kin_dirty = false;        /* (a SWAP IN brought another tank's book: read it again when asked; the old book
                                                         was flushed before the swap) */
    s_p1_unread = !blob;                              /* (no room to read it: an empty page now must not be written over the stored one) */
    if (!blob) return;
    if (persist_port_blob_load(COLL_NVS_KEY, blob, cap, &got) && got >= COLL_HDR && get16(blob, 0) == COLL_MAGIC) {
        int n = blob[3], rec = (int)get16(blob, 4);
        if (n > COLL_PAGE) n = COLL_PAGE;
        if (rec >= COLL_REC_V1)                       /* (never shorter than the first: its fields would be missing; a 64-byte one has no home name) */
            for (int i = 0; i < n && COLL_HDR + (size_t)(i + 1) * rec <= got && COLL_HDR + (size_t)(i + 1) * rec <= cap; i++) {
                rec_read(&s_e[s_n], blob + COLL_HDR + i * rec, rec);
                if (s_e[s_n].uid) s_n++;
            }
    }
    free(blob);
    /* pages 2..10: the tank's book, when its first page is full (a book's groups in order; a record a damaged book
       repeats, or a fish already in, read once) */
    if (s_n == COLL_PAGE && s_more)
        for (int g = 0; g < SHELF_BOOK_GROUPS; g++) {
            const uint8_t *r; int rec = 0, n = shelf_book_read(book_owner(), book_print(), g, &r, &rec);
            for (int j = 0; j < n && s_n < COLL_MAX; j++) {
                coll_entry_t e; rec_read(&e, r + j * rec, rec);
                if (e.uid && collection_find(e.uid) < 0) *E(s_n++) = e;
            }
            if (n < SHELF_BOOK_RECS) break;           /* (a group not full: the last) */
        }
}
void collection_flush(void) { if (s_dirty) save(0); kin_flush(); }
static void coll_clear(void) {
    s_n = 0; memset(s_seen, 0, sizeof s_seen); s_dirty = 0; s_kin_n = 0; s_kin_next = 0; s_kin_loaded = true; s_kin_dirty = false;
    s_p1_unread = false;                              /* (an empty first page is the truth now) */
}
void collection_reset(void) {
    if (s_n > COLL_PAGE || shelf_book_count()) {     /* (the keeper's reset: its book too - and the one the director parked, as the
                                                         reset erases the parked save, persist_port_erase) */
        uint32_t o = book_owner();
        shelf_book_forget(o);
        if (o) shelf_book_forget(o | SHELF_BOOK_PARKED);
    }
    coll_clear();
}
int  collection_count(void) { return s_n; }
const coll_entry_t *collection_get(int i) { return i >= 0 && i < s_n ? E(i) : NULL; }
int  collection_find(uint32_t uid) { for (int i = 0; i < s_n; i++) if (uid && E(i)->uid == uid) return i; return -1; }
bool collection_full(void) {
    if (s_n >= cap()) return true;
    return s_n >= COLL_PAGE && !shelf_book_room(book_owner(), book_print());
}
int collection_pages(void) {
    int p = collection_full() ? (s_n + COLL_PAGE - 1) / COLL_PAGE : s_n / COLL_PAGE + 1;
    return p < 1 ? 1 : p > COLL_PAGES ? COLL_PAGES : p;
}

static int seen_slot(uint32_t uid, bool make) {
    int worst = 0;
    for (int i = 0; i < SEEN_MAX; i++) {
        if (s_seen[i].uid == uid && uid) return i;
        if (!s_seen[i].uid || s_seen[i].here_ms < s_seen[worst].here_ms) worst = i;
    }
    if (!make || !uid) return -1;
    memset(&s_seen[worst], 0, sizeof s_seen[worst]); s_seen_frac[worst] = 0;
    s_seen[worst].uid = uid; s_seen[worst].met = now_unix();
    return worst;
}

static void entry_from_fish(coll_entry_t *e, const tank_t *t, int k) {
    const fish_t *f = &t->fish[N_FISH_MAX + k];
    e->uid = f->uid; e->home = t->vis_home[k]; e->parent[0] = f->parent_uid[0]; e->parent[1] = f->parent_uid[1];
    memset(e->name, 0, sizeof e->name); memcpy(e->name, f->name, FISH_NAME_MAX);
    e->color = f->color & 0xffffff; e->fin = f->fin & 0xffffff; e->accent = f->accent & 0xffffff;
    e->preset = (uint8_t)f->preset; e->stage = (uint8_t)(f->stage & 3);
    e->shape = f->shape; e->fins = f->fins; e->pattern = f->pattern;
    e->badge = t->vis_badge[k];
    passport_clean_tank_name(e->home_name, t->vis_home_name[k]);
    e->seen_unix = now_unix();
}
bool collection_add(const tank_t *t, int k) {
    if (k < 0 || k >= N_VISIT_MAX || !t->vis_used[k]) return false;
    uint32_t uid = t->fish[N_FISH_MAX + k].uid;
    if (collection_find(uid) >= 0 || collection_full()) return false;
    coll_entry_t *e = E(s_n);
    memset(e, 0, sizeof *e);
    entry_from_fish(e, t, k);
    int s = seen_slot(uid, false);                    /* its history here so far */
    if (s >= 0) { e->met_unix = s_seen[s].met; e->here_s = s_seen[s].here_ms / 1000; e->visits = s_seen[s].visits; }
    else { e->met_unix = now_unix(); e->here_s = (uint32_t)(t->clock - t->vis_since[k] > 0 ? t->clock - t->vis_since[k] : 0); e->visits = 1; }
    if (!e->visits) e->visits = 1;
    *FRAC(s_n) = 0;
    s_n++;
    if (!save_parts(s_dirty | part_of(s_n - 1))) { s_n--; return false; }   /* (a book the flash would not take: not collected) */
    s_dirty = 0;
    return true;
}
void collection_visit(uint32_t uid) {
    int s = seen_slot(uid, true);
    if (s >= 0 && s_seen[s].visits < 65535) s_seen[s].visits++;
    int i = collection_find(uid);
    if (i >= 0 && E(i)->visits < 65535) { E(i)->visits++; s_dirty |= part_of(i); }
}
void collection_tick(const tank_t *t, float dt) {
    s_kin_wait += dt;
    if (s_kin_dirty && s_kin_wait >= KIN_SAVE_GAP_S) kin_save();
    for (int k = 0; k < N_VISIT_MAX; k++) {
        if (!t->vis_used[k]) continue;
        uint32_t uid = t->fish[N_FISH_MAX + k].uid;
        int s = seen_slot(uid, true);
        if (s >= 0) { s_seen_frac[s] += dt * 1000; uint32_t ms = (uint32_t)s_seen_frac[s]; s_seen[s].here_ms += ms; s_seen_frac[s] -= (float)ms; }
        int i = collection_find(uid);
        if (i >= 0) {
            float *fr = FRAC(i);
            *fr += dt;
            if (*fr >= 1) { uint32_t w = (uint32_t)*fr; E(i)->here_s += w; *fr -= (float)w; s_dirty |= part_of(i); }
        }
    }
}
void collection_left(const tank_t *t, uint32_t uid) {
    int i = collection_find(uid);
    if (i < 0) return;
    for (int k = 0; k < N_VISIT_MAX; k++)               /* the last we saw of it: its look as it grew here */
        if (t->vis_used[k] && t->fish[N_FISH_MAX + k].uid == uid) {
            coll_entry_t *e = E(i);
            uint32_t met = e->met_unix, here = e->here_s; uint16_t v = e->visits; uint8_t fry = e->fry;
            entry_from_fish(e, t, k);
            e->met_unix = met; e->here_s = here; e->visits = v; e->fry = fry;
        }
    E(i)->seen_unix = now_unix();
    save(part_of(i));                                  /* (a save per visit, never per frame: its own page's part) */
}
void collection_fry(uint32_t parent_uid) {
    int i = collection_find(parent_uid);
    if (i >= 0 && E(i)->fry < 255) { E(i)->fry++; save(part_of(i)); }
}
/* staging (collection.h): made-up visitors, each its own look, from far-off tanks */
int collection_stage(int n, uint32_t seed) {
    static const char CON[] = "bdfgklmnprstvz", VOW[] = "aeiou";   /* (names a keeper could say: consonant, vowel, twice, maybe an end) */
    static const uint32_t COL[9] = { 0xffd166, 0x6db9ff, 0xf25b65, 0x78d67d, 0xff9f43, 0x9b72ff, 0x38dcc7, 0xe85dad, 0xffffff };
    if (s_n > COLL_PAGE || shelf_book_count()) shelf_book_forget(book_owner());   /* (the staged tank's own book; a parked one stays) */
    coll_clear();
    if (n > cap()) n = cap();
    uint32_t r = seed ? seed : 1;
    for (int i = 0; i < n; i++) {
        coll_entry_t *e = E(s_n); memset(e, 0, sizeof *e);
        r = r * 1103515245u + 12345u; uint32_t q = r >> 8;
        e->uid = 0x5a000000u | (seed & 0xff) << 16 | (uint32_t)(i + 1); e->home = 0x7a000000u | (q & 0xffff);
        char nm[6] = { CON[q % 14], VOW[(q / 14) % 5], CON[(q / 70) % 14], VOW[(q / 980) % 5], (q >> 20) & 1 ? CON[(q >> 21) % 14] : 0, 0 };
        memcpy(e->name, nm, sizeof nm);
        e->color = COL[q % 9]; e->fin = COL[(q / 9) % 9]; e->accent = COL[(q / 81) % 9];
        if (e->fin == e->color) e->fin = COL[(q / 9 + 1) % 9];
        e->preset = (uint8_t)(q % tank_roster_count()); e->stage = (uint8_t)(1 + q % 3);
        e->shape = (uint8_t)((q >> 3) % SHAPE_N); e->fins = (uint8_t)((q >> 5) % FINS_N); e->pattern = (uint8_t)((q >> 7) % PATTERN_N);
        e->badge = (int8_t)(q % 4); e->met_unix = e->seen_unix = now_unix(); e->here_s = 60 + q % 3600; e->visits = (uint16_t)(1 + q % 9);
        *FRAC(s_n) = 0;
        s_n++;
        if (s_n == COLL_PAGE && n > COLL_PAGE && !shelf_book_room(book_owner(), book_print())) break;   /* (no room for a book) */
    }
    if (!save_parts(1u | 2u | 4u | 8u) && s_n > COLL_PAGE) s_n = COLL_PAGE;   /* (each part written once; a book refused: the first page) */
    s_dirty = 0;
    return s_n;
}
void collection_fry_undo(uint32_t parent_uid) {
    int i = collection_find(parent_uid);
    if (i >= 0 && E(i)->fry > 0) { E(i)->fry--; save(part_of(i)); }   /* (as collection_fry: the visitor may be on a book page) */
}
static void kin_save(void) {
    uint8_t *b = malloc(6 + KIN_MAX * KIN_REC);
    if (!b) return;
    put16(b, 0, KIN_MAGIC); b[2] = 1; b[3] = (uint8_t)s_kin_n; put16(b, 4, KIN_REC);
    for (int i = 0; i < s_kin_n; i++) {
        uint8_t *r = b + 6 + i * KIN_REC; memset(r, 0, KIN_REC);
        put32(r, 0, s_kin[i].uid); memcpy(r + 4, s_kin[i].name, FISH_NAME_MAX);
        r[11] = (uint8_t)(s_kin[i].color >> 16); r[12] = (uint8_t)(s_kin[i].color >> 8); r[13] = (uint8_t)s_kin[i].color;
        r[14] = (uint8_t)(s_kin[i].bold * 255 + 0.5f); r[15] = (uint8_t)(s_kin[i].sociable * 255 + 0.5f);
    }
    persist_port_blob_save(KIN_NVS_KEY, b, (size_t)(6 + s_kin_n * KIN_REC));
    free(b);
    s_kin_dirty = false; s_kin_wait = 0;
}
void kin_flush(void) { if (s_kin_dirty) kin_save(); }
static void kin_load(void) {
    s_kin_loaded = true; s_kin_n = 0; s_kin_next = 0;
    const size_t cap = 6 + KIN_MAX * 64;
    uint8_t *b = malloc(cap); size_t got = 0;
    if (!b) return;
    if (persist_port_blob_load(KIN_NVS_KEY, b, cap, &got) && got >= 6 && get16(b, 0) == KIN_MAGIC) {
        int n = b[3], rec = (int)get16(b, 4);
        if (n > KIN_MAX) n = KIN_MAX;
        if (rec >= KIN_REC)
            for (int i = 0; i < n && 6 + (size_t)(i + 1) * rec <= got; i++) {
                const uint8_t *r = b + 6 + i * rec; kin_t *k = &s_kin[s_kin_n];
                k->uid = get32(r, 0); memcpy(k->name, r + 4, FISH_NAME_MAX); k->name[FISH_NAME_MAX] = 0;
                for (int j = 0; k->name[j]; j++) { char c = k->name[j]; if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) k->name[j] = '?'; }
                k->color = (uint32_t)r[11] << 16 | (uint32_t)r[12] << 8 | r[13];
                k->bold = r[14] / 255.0f; k->sociable = r[15] / 255.0f;
                if (k->uid) s_kin_n++;
            }
        s_kin_next = s_kin_n % KIN_MAX;
    }
    free(b);
}
static bool (*s_kin_keep)(uint32_t uid);
void kin_set_keep(bool (*keep)(uint32_t uid)) { s_kin_keep = keep; }
void kin_learn(uint32_t uid, const char *name, uint32_t color, float bold, float sociable) {
    if (!uid || !name || !name[0]) return;
    if (!s_kin_loaded) kin_load();
    kin_t *k = NULL;
    for (int i = 0; i < s_kin_n && !k; i++) if (s_kin[i].uid == uid) k = &s_kin[i];
    bool changed = false;
    if (!k) {                                          /* new to us: the next place, the oldest when full */
        if (s_kin_n < KIN_MAX) k = &s_kin[s_kin_n++];
        else {                                         /* the oldest - past any a fry here still names (0.4.2: kin_set_keep), unless all are */
            int j = s_kin_next;
            for (int n = 0; n < KIN_MAX && s_kin_keep && s_kin_keep(s_kin[j].uid); n++) j = (j + 1) % KIN_MAX;
            if (s_kin_keep && s_kin_keep(s_kin[j].uid)) j = s_kin_next;
            k = &s_kin[j]; s_kin_next = (j + 1) % KIN_MAX;
        }
        memset(k, 0, sizeof *k); k->uid = uid; changed = true;
    }
    if (strncmp(k->name, name, FISH_NAME_MAX)) { memset(k->name, 0, sizeof k->name); strncpy(k->name, name, FISH_NAME_MAX); changed = true; }
    k->color = color & 0xffffff; k->bold = bold; k->sociable = sociable;   /* (the look and temper as last seen: not worth a save alone) */
    if (changed) { s_kin_dirty = true; if (s_kin_wait >= KIN_SAVE_GAP_S) kin_save(); }   /* (else at the gap's end, or the next save) */
}
const kin_t *kin_find(uint32_t uid) {
    if (!s_kin_loaded) kin_load();
    for (int i = 0; i < s_kin_n; i++) if (uid && s_kin[i].uid == uid) return &s_kin[i];
    return NULL;
}
uint32_t collection_here_s(uint32_t uid) {
    int i = collection_find(uid);
    if (i >= 0) return E(i)->here_s;
    int s = seen_slot(uid, false);
    return s >= 0 ? s_seen[s].here_ms / 1000 : 0;
}
int collection_visits(uint32_t uid) {
    int i = collection_find(uid);
    if (i >= 0) return E(i)->visits;
    int s = seen_slot(uid, false);
    return s >= 0 ? s_seen[s].visits : 0;
}
