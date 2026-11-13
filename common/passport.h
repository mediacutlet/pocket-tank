/* passport.h — a fish, packed to cross to another tank and back (0.4, the
 * tank link: docs/tank-link-0.4.md).
 *
 * The passport is who the fish is and how it is right now: its IDs, name,
 * look and genes, drives, personality, stage, size, age, meals and badges.
 * NOT where it is or what it is doing (the tank it arrives in places it and
 * its advisor decides), and nothing that is the home tank's (sand dollars,
 * the slot, its spot by the reef).
 *
 * THE WIRE FORMAT IS APPEND-ONLY, like the save: version 1 is
 * PASSPORT_V1_BYTES, little-endian, its offsets fixed in passport.c. A later
 * version only adds fields before the CRC and raises `len`; an older reader
 * takes the prefix it knows. The CRC (CRC-16/CCITT) always sits in the last
 * two bytes of `len`.
 *
 * EVERYTHING READ IS CHECKED: a passport comes from another tank, which may
 * run other firmware or none of ours. passport_read refuses what it cannot
 * trust (magic, length, CRC, a zero ID, a stage that does not exist) and
 * clamps what it can (every number to its range, the name to letters and
 * digits, genes to known values). A refused passport is never drawn.
 *
 * Genes travel from version 1 on (genetics 0.4, docs/genetics-0.4.md): the
 * form it shows, the copy of each gene it carries, which gene mutated at its
 * birth and where each shown copy came from - in GENE_* order. The colors it
 * SHOWS travel as RGB (color / fin / accent), the gene copies as indexes into
 * LOOK_COLORS.
 *
 * Keeper-facing name: the POCKET TANK PASSPORT (Strato, 2026-10-04). */
#ifndef POCKET_TANK_PASSPORT_H
#define POCKET_TANK_PASSPORT_H

#include <stdbool.h>
#include <stdint.h>
#include "tank.h"

#define PASSPORT_MAGIC     0x5046u   /* "FP": a fish passport */
#define PASSPORT_VERSION   2
#define PASSPORT_V1_BYTES  94
#define PASSPORT_V2_BYTES  110       /* v2 (0.4, 2026-11-01): + its home tank's keeper-given name */
#define PASSPORT_TANK_NAME 16        /* that name's room: 15 characters and the end */
#define PASSPORT_MAX_BYTES 240       /* what a reader accepts from a later version (one ESP-NOW frame is 250) */
#define PASSPORT_KIND_FISH 0         /* passport_t.kind: a reef fish */
#define PASSPORT_GENES     5         /* shape, fins, pattern, body, accent (GENE_N, checked in passport.c) */

typedef struct {
    uint32_t uid, home_tank, parent_uid[2];
    char     name[FISH_NAME_MAX + 1];     /* lowercase letters and digits ('?' for anything else) */
    uint32_t color, fin, accent;          /* 0xRRGGBB */
    uint8_t  preset, stage;
    uint8_t  shape, fins, pattern;        /* the form it shows (SHAPE_* / FINS_* / PATTERN_*) */
    uint8_t  carried[PASSPORT_GENES];     /* the copies it carries and does not show (GENE_* order; colors as LOOK_COLORS indexes) */
    uint8_t  mutated, from_b, from_carried;
    uint8_t  kind;                        /* the creature's KIND (0.4.2, the travel bowl): 0 = a reef fish, the only kind there is
                                             yet. The wire's spare byte (OFF_PAD, always written 0 before); read as it comes,
                                             refused nowhere over the radio in this release - the travel bowl's LET IN checks it */
    float    hunger, energy, stress, curiosity, bored, trust;   /* 0..10 */
    float    sociable, bold, lazy, bold0, sociable0;           /* 0..1 */
    float    drift_acc;                   /* 0..655 */
    float    size;                        /* 0..4 */
    float    age_s;                       /* tended seconds (progression) */
    int32_t  eaten, eaten_player;         /* 0..65535 */
    uint32_t ms_bits, ms_seen;            /* MS_* (the retired bits never travel) */
    char     home_name[PASSPORT_TANK_NAME];   /* v2: what its keeper named its home tank, one line ("" = a default
                                                 name - MY TANK, TANK 2 - or a v1 passport: its HOME WATERS stand alone) */
} passport_t;

/* a fish of this tank into a passport. age_s is progression_age_s for a home
 * fish, the visitor's own clock for a visitor. */
void passport_from_fish(passport_t *p, const fish_t *f, uint32_t home_tank, float age_s);
/* a passport onto a fish_t: identity, look, drives, personality, stage, size,
 * meals, badges, and the preset's body. Position, heading, goal and the
 * tank-local fields are the caller's (the age too: it is not in fish_t). */
void passport_to_fish(const passport_t *p, fish_t *f);
/* HOME WATERS (2026-11-01, Strato): a tank's lasting name, two words from its
 * tank ID (64 x 64, kid-safe, 15 characters at most) - the same everywhere,
 * stored nowhere, never typed. The link's badge shape is who is next door
 * NOW; the waters are where a fish came from, for good. 0: FAR WATERS */
#define PASSPORT_WATERS_MAX 16
void passport_home_waters(uint32_t home_tank, char *out, int cap);
/* a tank's name made fit to travel and to draw: one line (a break is a space),
 * A-Z 0-9 and the rename keyboard's ' . - ! ? /, anything else '?' */
void passport_clean_tank_name(char *dst, const char *src);
/* bytes written (PASSPORT_V2_BYTES), or 0 if cap is too small */
int  passport_write(const passport_t *p, uint8_t *buf, int cap);
/* true = *p holds a checked, clamped passport; false = refused (*p undefined) */
bool passport_read(passport_t *p, const uint8_t *buf, int len);

#endif
