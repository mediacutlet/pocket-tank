/* collection.h — the VISITOR COLLECTION (tank link 0.4, Strato 2026-10-06):
 * fish from other tanks the keeper collected from a visitor's passport card
 * (COLLECT). Each entry is the fish's look and identity as it was when last
 * seen here, and its history with this tank: when it was first met, how many
 * visits, how long it has spent in this water, and the cross-tank fry it had
 * here. No special treatment in the tank - a keepsake.
 *
 * Its own save slot (persist_port_blob_*, key COLL_NVS_KEY in the tank's NVS
 * namespace), never the tank save: the save layout stays as it is. A reset
 * erases the namespace, the collection with it. The blob is versioned with a
 * record size, so a later build may append fields to a record. */
#ifndef POCKET_TANK_COLLECTION_H
#define POCKET_TANK_COLLECTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tank.h"

/* PAGES (0.4, Strato 11-03): up to COLL_PAGES pages of COLL_PAGE. The first page is the "visitors" blob as it always
 * was (an older build reads it, a MY TANKS slot carries it); pages 2..10 are the tank's VISITOR BOOK, a slot of its own
 * in storage (shelf.h), and in RAM a PSRAM block main.c hands over (collection_set_more): never internal RAM. No block,
 * no book (no shelf) - the collection stops at the first page, FULL. */
#define COLL_PAGE    12
#define COLL_PAGES   10
#define COLL_MAX     (COLL_PAGE * COLL_PAGES)
#define COLL_NVS_KEY "visitors"
#define COLL_BLOB_MAX (6 + COLL_PAGE * 76)  /* the first page's blob, full (collection.c COLL_HDR + COLL_REC): what a MY TANKS slot must hold */

typedef struct {
    uint32_t uid, home, parent[2];
    char     name[FISH_NAME_MAX + 1];
    uint32_t color, fin, accent;          /* 0xRRGGBB */
    uint8_t  preset, stage, shape, fins, pattern;
    int8_t   badge;                       /* its home tank's badge when last seen (-1 = not known) */
    uint32_t met_unix, seen_unix;         /* first met / last seen here (0 = this board had no clock) */
    uint32_t here_s;                      /* seconds it has spent in this tank, every visit */
    uint16_t visits;
    uint8_t  fry;                         /* cross-tank fry it had here */
    char     home_name[16];               /* its home tank's keeper-given name when last seen ("" = none: its HOME WATERS alone) */
} coll_entry_t;

void collection_load(void);                                /* at boot (the KIN book is read again when next asked) */
void collection_flush(void);                               /* the time here counted since the last save, saved (MY TANKS: before a swap) */
void collection_reset(void);                               /* the keeper's reset (the namespace is erased) */
int  collection_count(void);
size_t collection_more_size(void);                         /* the block for pages 2..10 (bytes) */
void collection_set_more(void *block);                     /* main.c's PSRAM block of collection_more_size() (zeroed), before the boot's load */
/* COLLECT can take no more: every page full, or no room for the book (no block, no free slot) - the passport says FULL */
bool collection_full(void);
int  collection_pages(void);                               /* pages to show: the full ones and the next with room (1..COLL_PAGES) */
/* staging (the director's coll fill, the sim): the collection made n made-up visitors (the book written), the count */
int  collection_stage(int n, uint32_t seed);
const coll_entry_t *collection_get(int i);                 /* newest last */
int  collection_find(uint32_t uid);                        /* its index, or -1 */
/* COLLECT on a visitor's passport: the visitor in visitor slot k; false = it
 * is in already, or the collection is full */
bool collection_add(const tank_t *t, int k);
/* the tank link's moments, for every visitor (the history before it was
 * collected counts too): it swam in, it is here (dt), it left, it had a fry */
void collection_visit(uint32_t uid);
void collection_tick(const tank_t *t, float dt);
void collection_left(const tank_t *t, uint32_t uid);
void collection_fry(uint32_t parent_uid);
void collection_fry_undo(uint32_t parent_uid);   /* ... a birth undone (progression_fry_kept: no room in either tank) */
/* for the passport card and the detail page: a visitor's time here and its
 * visits, collected or not (0 if never seen) */
uint32_t collection_here_s(uint32_t uid);
int      collection_visits(uint32_t uid);

/* KIN (2026-10-06): the names of fish from other tanks - every visitor that
 * swims in, and a cross-tank fry's other parent - so a family line reads
 * "BORN TO KIRU AND MIRA", not "? ". Its own small save slot (KIN_NVS_KEY),
 * written only when a fish is new to it or renamed, at most once a minute
 * (the changes between coalesced; every tank save, sleep and SWAP IN writes
 * what waits: kin_flush); the oldest makes room */
#define KIN_MAX     16
#define KIN_NVS_KEY "kin"
#define KIN_BLOB_MAX (6 + KIN_MAX * 16)     /* its blob, full (collection.c KIN_REC) */
typedef struct { uint32_t uid; char name[FISH_NAME_MAX + 1]; uint32_t color; float bold, sociable; } kin_t;
void kin_learn(uint32_t uid, const char *name, uint32_t color, float bold, float sociable);
const kin_t *kin_find(uint32_t uid);
void kin_set_keep(bool (*keep)(uint32_t uid));             /* who the book never makes room by (a parent a fry here names, 0.4.2) */
void kin_flush(void);                                      /* a change still waiting for its minute, written now */

#endif
