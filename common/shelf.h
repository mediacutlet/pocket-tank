/* shelf.h - MY TANKS: the tanks a board keeps (the tank switcher,
 * docs/tank-switcher.md "The shared data").
 *
 * The tank on the glass lives where it always has (the progression save,
 * NVS tank/save); its place on the shelf, its name and its shelf ID are the
 * frame's `now`. Every other tank is a 24 KB slot in the `storage` partition,
 * past its first 64 KB (the NVS rescue copy is at 0): a 4 KB sector holding
 * the header and the save, then its snapshot. There is no index: the slots
 * describe themselves and the shelf is read back from their headers at boot.
 *
 * A swap is journaled in the frame (`swap`) before anything moves, so a power
 * cut at any step is finished at the next boot (shelf_boot), never half-done:
 *   1. the tank on the glass -> a free slot (its header written last)
 *   2. the journal: from, to, the two slots
 *   3. the incoming save -> NVS (or no save: a fresh tank, born at the boot)
 *   4. frame `now` = the incoming tank
 *   5. its old slot erased, the journal deleted
 * A write the board refuses (NVS full or worn, saving off) never loses a tank:
 * before step 3 the swap stops there; at step 3 or 4 it is undone (the tank
 * that left goes back onto the glass from its slot). If even the undo is
 * refused, both tanks stay whole in their slots, the journal stands, and
 * nothing on the shelf is written until the boot that finishes it. A save
 * longer than SHELF_SAVE_MAX (a newer build's, after a rollback) never swaps.
 * What is shared across a swap (the wallet, the board's settings) goes with
 * it as the frame's `carry`, given to the incoming tank after it loads.
 *
 * THE TANK'S OWN BLOBS (0.4): the visitor collection, the KIN book and the
 * GIVE notes live beside the save (persist_port_blob_*: the glass's). On the
 * shelf they ride in the slot's last SHELF_EXTRA_MAX bytes, gathered into the
 * outgoing slot at step 1 and put back in NVS with the incoming save at step 3
 * (a slot without them - a fresh tank, a 0.3.9 shelf - brings none).
 *
 * THE VISITOR BOOK (0.4, Strato 11-03: the collection pages): the collection's first page (12) is the "visitors" blob
 * above, as it always was; pages 2..10 (108 more) are a BOOK, a slot of its own on the shelf's grid. A book belongs to a
 * tank by its shelf ID and the PRINT of its first page (a hash of those 12 fish IDs: once the page is full it never
 * changes, so a book finds its tank again after a rollback that wiped the save's IDs, and never a reset tank's). It
 * never moves in a swap - the tank on the glass and on the shelf read the same book. The slot is 6 sectors: three
 * GROUPS of 3 pages (36 records), each in two copies (A sector 2g, B 2g+1) with a sequence number and a checksum, the
 * newer good one read; a write erases ONE sector (the older copy). A slot is a book when group 0 (sector 0 or 1)
 * reads; its other groups need the same print. Older builds read a book's slot as a free one (a swap there writes a
 * tank over it: pages 2..10 lost, the first page kept). A book counts as a used slot, and one is made only with two
 * free (one stays free for swaps). A tank removed, a reset, or a book whose tank is gone (the boot) erases it.
 * Parked (the director's stash): its owner's top bit set, so neither the staged tank nor the sweep touches it. */
#define SHELF_BOOK_MAGIC   0x31565450u           /* "PTV1" */
#define SHELF_BOOK_GROUPS  3
#define SHELF_BOOK_RECS    36                    /* records a group: 3 pages of 12 */
#define SHELF_BOOK_HDR     32                    /* a sector's head: magic owner print seq | group count rec | 8 zero | crc */
#define SHELF_BOOK_PAYLOAD (4096 - SHELF_BOOK_HDR)
#define SHELF_BOOK_PARKED  0x80000000u           /* (a shelf ID never reaches it) */
#ifndef SHELF_H
#define SHELF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHELF_SLOT_SIZE   (24 * 1024)
#define SHELF_SAVE_OFF    256                     /* the save, in the header's sector */
#define SHELF_SAVE_MAX    (4096 - SHELF_SAVE_OFF)
#define SHELF_THUMB_OFF   4096
#define SHELF_EXTRA_MAX   2560                    /* the tank's own blobs, full: the collection 918 + KIN 262 + the gift notes 1296 (v2: 145-byte notes) + 42 = 2518 (shelf.c asserts it) */
#define SHELF_EXTRA_OFF   (SHELF_SLOT_SIZE - SHELF_EXTRA_MAX)
#define SHELF_THUMB_MAX   (SHELF_EXTRA_OFF - SHELF_THUMB_OFF)   /* 17920: the square's 92 x 96 is the biggest (17664) */
#define SHELF_SLOTS_MAX   120
#define SHELF_POS_MAX     120                     /* places on the shelves: 15 pages of 8 */
#define SHELF_NAME_MAX    15                     /* two plate lines of 7 and the break between them (0.3.9.3) */
#define SHELF_NEW_PRICE   300                     /* D4: another tank of a type you have */
enum { SHELF_TIME_DORMANT = 0, SHELF_TIME_PAUSED = 1 };

/* a tank on the shelf (or on the glass: shelf_now) - the slot header's own fields, appended only */
typedef struct {
    uint32_t magic;
    uint16_t layout;
    uint16_t save_len;                            /* 0 = a fresh tank: never on the glass yet */
    uint32_t seq;                                 /* one tank in two slots (a cut mid-rewrite): the higher wins */
    uint32_t id;                                  /* the shelf's own; 0.4's tank_uid is the save's */
    uint8_t  type, pos, time_mode, pad0;          /* type 0 = the fish tank */
    char     name[16];
    int64_t  shelved_unix;                        /* when it went on the shelf */
    int64_t  clock_unix;                          /* when its DORMANT clock last started */
    uint32_t banked_s;                            /* lived-to-be from earlier DORMANT stretches */
    uint32_t save_crc;
    uint16_t thumb_w, thumb_h;                    /* 0 = no snapshot yet (a fresh tank) */
    uint32_t thumb_crc;
    uint32_t pad1;
} shelf_tank_t;

/* the ports (persist_port_esp.c / persist_port_sim.c): the shelf's area of
 * `storage` (offsets from its start) and the NVS namespace `frame` */
size_t shelf_port_size(void);
bool   shelf_port_read(uint32_t off, void *buf, size_t n);
bool   shelf_port_write(uint32_t off, const void *buf, size_t n);
bool   shelf_port_erase(uint32_t off, size_t n); /* 4 KB aligned */
bool   shelf_port_frame_get(const char *key, void *buf, size_t max, size_t *got);
bool   shelf_port_frame_set(const char *key, const void *buf, size_t n);
bool   shelf_port_frame_del(const char *key);
bool   persist_port_forget(void);                 /* the save on the glass only (a fresh tank comes in) */

/* boot: read the slots, finish a journaled swap, drop leftover copies.
 * Before progression_boot (it moves the save the boot will load). */
void shelf_boot(int64_t now_unix);
const shelf_tank_t *shelf_now(void);              /* the tank on the glass */
int  shelf_count(void);                           /* the tanks on the shelf (not the one on the glass) */
const shelf_tank_t *shelf_get(int i);
int  shelf_at_pos(int pos);                       /* the shelf index at a place, -1 = the tank on the glass, -2 = empty */
int  shelf_free_places(void);                     /* tanks that can still be added */
bool shelf_thumb(int i, uint16_t *out, size_t max_px);   /* its snapshot; false = none (a fresh tank) */
int  shelf_count_type(int type);                  /* tanks of a type, the one on the glass included */

/* a fresh tank of `type` in the first empty place; its index, or -1 (full) */
int  shelf_new(int type, int64_t now_unix);
/* NEW TANK, BOUGHT (0.4.1): the slot and the wallet are two writes, and a power cut between them gave a tank for nothing
 * (slot first) or took the price for none (wallet first). A journal (`buy` in the frame: the price, the new tank's ID and
 * `mark`, a fresh marker) is written first, then the slot, then pay(ctx) takes the price and saves the wallet WITH the
 * marker (false: refused, both as they were), then the journal goes. Its index, or -1 (nothing bought, nothing paid).
 * shelf_buy_settle, at the boot, with the loaded save's NEW TANK marker: the journal's - paid, the tank stays; another -
 * not paid, the tank goes. Never the wallet's figure (round 2, "Marker now"). Returns SD to give back (paid and the tank
 * not there - by the order, never): the caller clears the save's marker with the refund, in one save */
int  shelf_buy(int type, int64_t now_unix, int price, uint32_t mark, bool (*pay)(void *ctx), void *ctx);
int  shelf_buy_settle(uint32_t save_mark, int32_t wallet);   /* (wallet: only for a release candidate's journal, R2) */
/* a new name (RENAME on its card): shelf tank i, or -1 the tank on the glass. "LINE 1\nLINE 2" or one line; each line
   trimmed; an empty name is "TANK <place>" */
bool shelf_rename(int i, const char *name);
/* REMOVE (D5, MORE on its card): what it sells back for - everything its save bought, at SD_SELL_PCT: its placeable
   pieces (each at its SELL price), its critters (snail, shrimp, urchin, seahorse, crab) and its backgrounds (0.4.1,
   audit P6: Strato, "give partial refund for critters and backgrounds"; the pieces alone until then). A fresh tank: 0;
   the tank's own price is never refunded. n (NULL: not wanted): how many pieces, critters and backgrounds, for the
   modal's words. Then the tank gone for good, its slot erased. Never the tank on the glass (it is not on the shelf).
   The caller pays the wallet */
int  shelf_refund_value(int i, int n[3]);
/* MOVE (0.3.9.3): tank i (-1 the tank on the glass) to place `pos`; a tank already there trades places with it. Two
   rewrites when they trade - a cut between leaves two at one place, which the boot sorts (the later finds a free one) */
bool shelf_move(int i, int pos);
bool shelf_remove(int i);
/* REMOVE, PAID (0.4.1): the slot's erase and the refund's save are two writes - erased first, a power cut between lost the
 * refund, and a refused erase brought the tank back at the boot with its refund paid. A journal (`rm` in the frame: the
 * tank, the refund, `mark`) is written first, then pay(ctx) adds the refund and saves it WITH the marker (false: refused,
 * both as they were - nothing removed), then the slot is erased and the journal goes. shelf_remove_settle, at the boot
 * with the loaded save's REMOVE marker: the journal's - paid, the tank is erased if it is still there; another - not paid,
 * the tank stays (round 2, "Marker now") */
bool shelf_remove_paid(int i, int refund, uint32_t mark, bool (*pay)(void *ctx), void *ctx);
void shelf_remove_settle(uint32_t save_mark, int32_t wallet);
/* DORMANT <-> PAUSED; banks what a DORMANT stretch lived (rewritten to a free slot, the old erased) */
bool shelf_set_time(int i, int mode, int64_t now_unix);
/* the tank on the glass onto the shelf (its snapshot `thumb`, w x h), shelf tank i onto the glass.
 * `carry` (what the board shares) is kept for the boot that follows: the caller restarts. */
bool shelf_swap(int i, const uint16_t *thumb, int w, int h, const void *carry, size_t carry_len, int64_t now_unix);
/* the carry, after the incoming tank has loaded (false = none). Read, not taken: progression gives it to the tank unless
   the loaded save already carries its marker (progression_carry_t.mark - applied, round 2), saves, and only once that save
   has landed calls shelf_carry_done (0.4.1 audit round 1, R-saves S1: it was deleted first, and a power cut before the save
   lost the wallet). A carry left behind (a cut, a refused delete) reads as applied by its marker at every later boot */
/* the work a refused write left (a journal or the carry not deleted, a slot not erased): cleanup only with the markers - tried
   before every save of the tank and before any MY TANKS action, which is refused while it is not clear. true = none left */
bool shelf_tidy(void);
bool shelf_peek_carry(void *buf, size_t len);
void shelf_carry_done(void);
/* the tank on the glass's name as it travels on its fish's passports (passport_t.home_name, 16 bytes): one line, or ""
   for a name the keeper never gave it - MY TANK, TANK <n> - so its HOME WATERS stand alone next door */
void shelf_keeper_name(char *out);
/* how long the tank on the glass lived on the shelf before this boot, for the log (-1 = not swapped in) */
float shelf_lived_h(void);

/* THE VISITOR BOOK (above). owner = a shelf ID (shelf_now()->id for the tank on the glass), print = its first page's.
 * read: group g's records (count returned, 0 = none or no book; *recs into the shelf's scratch, valid until the next
 * book call; *rec = the record size it was written with). write: fill shelf_book_buf() with count records of rec
 * bytes, then write group g (the book made if there is none: false = no room, or the flash refused). room: a book of
 * this tank's, or one can be made. forget: every book of an owner (its slot erased). rekey: books of `from` become
 * `to`'s (the director parks one with SHELF_BOOK_PARKED); the count rekeyed. */
int      shelf_book_read(uint32_t owner, uint32_t print, int group, const uint8_t **recs, int *rec);
uint8_t *shelf_book_buf(void);
bool     shelf_book_write(uint32_t owner, uint32_t print, int group, int count, int rec);
bool     shelf_book_room(uint32_t owner, uint32_t print);
void     shelf_book_forget(uint32_t owner);
int      shelf_book_rekey(uint32_t from, uint32_t to);
int      shelf_book_count(void);                  /* books on the board (the log, the director) */
#endif
