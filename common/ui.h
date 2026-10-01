/* ui.h - the pages' measurements, taken from the 448 x 368 tank they were
 * designed on and scaled to the tank this build draws.
 *
 * UI(n) is a layout length - a position, a box, a pitch, a gap - designed at
 * 368 px of height; UI_TEXT(s) is a text scale designed at the same size.
 * On the Waveshare AMOLED (TANK_H 368) both are exactly n and s, so that
 * build draws what it always drew. On the 2.8" CYD (TANK_H 240) they are
 * about 0.65 of the design, rounded: the glass has 143 px per inch against
 * the AMOLED's 322, so a page at 0.65 of the pixels still stands a little
 * larger in the hand than the original did.
 *
 * Pixel art (the icons, the castle and coral glyphs) is NOT scaled: it is
 * drawn a pixel at a time and would fall apart - it keeps its size and the
 * layout around it shrinks. */
#ifndef POCKET_TANK_UI_H
#define POCKET_TANK_UI_H
#include "tank.h"

#define UI_DESIGN_H 368
#define UI(n)      (((n) * TANK_H + UI_DESIGN_H / 2) / UI_DESIGN_H)
#define UI_F       ((float)TANK_H / UI_DESIGN_H)                   /* the same, for float sizes */
/* A text scale: 2 -> 1, 3 -> 2, 4 -> 3, 6 -> 4 on the CYD; never below 1. */
#define UI_TEXT(s) (UI(s) < 1 ? 1 : UI(s))

/* A tank too short for the pages' pixel art at its drawn size: the stats
 * card goes to two columns, the milestones page to 24 px badges, and the
 * pages that are laid out for it rather than scaled pick their own numbers. */
#define UI_COMPACT (TANK_H < 300)
#endif
