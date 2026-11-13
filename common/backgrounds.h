/* backgrounds.h - THE BACKGROUNDS (0.4.1; docs/backgrounds-0.4.1.md)
 *
 * What lies behind the water: Open Water (the tank as it always was, free) and seven a keeper can buy in the
 * shop, each drawn by code on every board. A background is (1) a WATER PALETTE - the haze every frond, piece and
 * critter is tinted toward (water_rgb) - and (2) far shapes BAKED into the scene, hazed toward that water. A still
 * background costs no frames: every frame is primed from the scene anyway. Its cost is the bake (boot, the light
 * double-tap, a page that changed something closing, the switch itself).
 *
 * ROW BY ROW: the background is drawn INSIDE render.c's water pass - for each row the water pass asks bg_row for
 * the row's colors (the water where nothing stands, its glow mixed in) and quantizes every pixel once. A second
 * pass over the frame (measured: up to ~200 ms on the square) is what this avoids. bg_row also says, a pixel at a
 * time, which LAYER the pixel is (0 the water; 1.. far to near) - the boot show builds the background up by it.
 *
 * Memory: nothing static but a few ints. The bake borrows one PSRAM block (bg_scratch_size) for its grids and
 * gives it back; without it a background bakes as its water palette alone. Internal RAM does not move.
 */
#ifndef BACKGROUNDS_H
#define BACKGROUNDS_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* the ids, as saved (tank_t.bg_now) - Strato's order of preference, 11-04 */
enum { BG_OPEN = 0, BG_RUINS, BG_KELP, BG_TWILIGHT, BG_LAGOON, BG_RIDGES, BG_REEF, BG_GROTTO,
       BG_SHIP, BG_VENT, BG_MANGROVE,                       /* 0.4.2 (docs/backgrounds-0.4.2.md) */
       BG_N };
#define BG_LAYERS_MAX 6                          /* the most layers any background has (the boot show's budget) */

const char *bg_name(int bg);                    /* the shop's name (Strato named them 11-05) */
void bg_words(int bg, const char **l1, const char **l2);   /* the shop modal's two lines (<= 25 chars each; approved by Strato 11-05) */

/* the water of row y (0 = the surface) under background bg: the palette water_rgb reads. Open Water's is the
 * tank's own gradient, to the bit. */
uint32_t bg_water_rgb(int bg, int y);

/* THE GLOW (Twilight, Reef Walls, Grotto): the water of pixel (x, y) is mixed toward rgb by
 * row[y] * col[x] >> 8 (0..255) - in the water pass, on rows bg_row leaves to it. */
typedef struct { uint32_t rgb; const uint8_t *row, *col; } bg_glow_t;

size_t bg_scratch_size(void);                   /* the bake's scratch: bytes to borrow (PSRAM) on this board */
size_t bg_scratch_need(int bg);                /* ... what a bake of bg borrows now (its own part; Reef Walls kept: rows only) */
int    bg_layers(int bg);                       /* how many layers bg_row names (0: none - the water alone) */

/* THE KEEP (0.4.1): a layout the light does not change (Reef Walls) is held between rebuilds while that background is
 * in use, from this lender (render_set_bake_scratch passes the bake's own; none set = malloc). bg_begin with another
 * background lets it go. bg_keep_drop: let it go now (tests); bg_kept: whose layout is held (BG_OPEN: none). */
void bg_set_keep_lender(void *(*alloc)(size_t), void (*release)(void *));
void bg_keep_drop(void);
int  bg_kept(void);

/* before the water pass: lay out bg in scratch (its grids, its tables) - or take its kept layout. Returns its glow, or NULL. */
const bg_glow_t *bg_begin(int bg, void *scratch);

/* during the water pass, row y: the row's colors (RGB888, TANK_W of them; the glow already in) and in *lay the
 * row's layers (0 = the water) - or NULL: nothing of the background on this row but the water and its glow. */
const uint32_t *bg_row(int bg, int y, void *scratch, const uint8_t **lay);
/* after bg_row: how far each of the row's pixels lights itself (0: not at all; 255: the night does not dim it - the
 * vent's mouths, 0.4.2), or NULL: none on this row */
const uint8_t *bg_row_lit(void *scratch);

/* THE DRIFT: specks in the water, drawn every frame by render.c (it marks them) - Twilight Deep's marine snow, and
 * (0.4.2) the Volcanic Vent's embers and the Mangroves' silt and leaves. Each is a function of the clock alone -
 * nothing kept between frames. *rgb: its color; *lit: it lights itself (an ember: the night does not dim it). */
int  bg_specks(int bg);
/* the drift's own time for this frame, from the tank's lifelong clock (a double): the T bg_speck takes. The 0.4.2
 * drifts take tank_anim's lap (tank.h: a float of the raw clock steps half a second on a tank weeks old - every
 * period of theirs divides the lap, so nothing jumps where it wraps). Twilight's snow still takes the raw clock as
 * a float, as 0.4.1 shipped it: its periods do not divide the lap, and its frames are held byte for byte. */
float bg_speck_clock(int bg, double clock);
void bg_speck(int bg, int i, float T, float *x, float *y, int *alpha, bool *big, uint32_t *rgb, bool *lit);
#define BG_SPECK_RGB 0xc8d8ffu

/* for the tests only: the Volcanic Vent's mouths (k from 0; false past the last) and, after a bg_begin(BG_VENT, scratch),
 * its tube worms' tips (i from 0; false past the last) */
bool bg_debug_vent_mouth(int k, int *x, int *y);
bool bg_debug_vent_worm(const void *scratch, int i, int *x, int *y0);

#endif
