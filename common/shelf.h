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
 * What is shared across a swap (the wallet, the board's settings) goes with
 * it as the frame's `carry`, given to the incoming tank after it loads. */
#ifndef SHELF_H
#define SHELF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHELF_SLOT_SIZE   (24 * 1024)
#define SHELF_SAVE_OFF    256                     /* the save, in the header's sector */
#define SHELF_SAVE_MAX    (4096 - SHELF_SAVE_OFF)
#define SHELF_THUMB_OFF   4096
#define SHELF_THUMB_MAX   (SHELF_SLOT_SIZE - SHELF_THUMB_OFF)
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
/* a new name (RENAME on its card): shelf tank i, or -1 the tank on the glass. "LINE 1\nLINE 2" or one line; each line
   trimmed; an empty name is "TANK <place>" */
bool shelf_rename(int i, const char *name);
/* REMOVE (D5, MORE on its card): what its pieces sell back for - every placeable piece its save owns, at its SELL
   price (a fresh tank: 0; the tank's own price is never refunded) - and the tank gone for good, its slot erased.
   Never the tank on the glass (it is not on the shelf). The caller pays the wallet */
int  shelf_pieces_value(int i);
/* MOVE (0.3.9.3): tank i (-1 the tank on the glass) to place `pos`; a tank already there trades places with it. Two
   rewrites when they trade - a cut between leaves two at one place, which the boot sorts (the later finds a free one) */
bool shelf_move(int i, int pos);
bool shelf_remove(int i);
/* DORMANT <-> PAUSED; banks what a DORMANT stretch lived (rewritten to a free slot, the old erased) */
bool shelf_set_time(int i, int mode, int64_t now_unix);
/* the tank on the glass onto the shelf (its snapshot `thumb`, w x h), shelf tank i onto the glass.
 * `carry` (what the board shares) is kept for the boot that follows: the caller restarts. */
bool shelf_swap(int i, const uint16_t *thumb, int w, int h, const void *carry, size_t carry_len, int64_t now_unix);
/* the carry, once, after the incoming tank has loaded (false = none) */
bool shelf_take_carry(void *buf, size_t len);
/* how long the tank on the glass lived on the shelf before this boot, for the log (-1 = not swapped in) */
float shelf_lived_h(void);
#endif
