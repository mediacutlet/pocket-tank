/* ui.h - the pages' measurements, taken from the 448 x 368 page they were
 * designed on and scaled to the page this build draws.
 *
 * UI(n) is a layout length - a position, a box, a pitch, a gap - designed at
 * 368 px of page height; UI_TEXT(s) is a text scale designed at the same size.
 * Every Waveshare board (the 1.8, the round 1.75C, the watch) lays its pages
 * out on the same 448 x 368 PAGE (render.h), whatever the size of its glass,
 * so there both are exactly n and s, by definition - not by arithmetic that
 * happens to come out even: those builds compile what upstream wrote. Only on
 * a 320 x 240 board (CONFIG_POCKET_TANK_320X240: the CYD, the Touch-LCD-2) is the page
 * smaller - 292 x 240, the design at 240/368 - and there they are about 0.65
 * of the design, rounded:
 * the glass has 143 px per inch against the AMOLED's 322, so a page at 0.65
 * of the pixels still stands a little larger in the hand than the original
 * did. (2026-10-08: it was keyed on TANK_H, which upstream's round board and
 * watch made 466 and 502 - their pages would have drawn at 1.27x and 1.36x.)
 *
 * Pixel art (the icons, the castle and coral glyphs) is NOT scaled: it is
 * drawn a pixel at a time and would fall apart - it keeps its size and the
 * layout around it shrinks. Where that cannot work (the milestones page, the
 * shop, settings, the stats card) the 320 x 240 boards have layouts of their
 * own: render.h's CONFIG_POCKET_TANK_320X240 blocks. */
#ifndef POCKET_TANK_UI_H
#define POCKET_TANK_UI_H
#include "tank.h"

#define UI_DESIGN_H 368             /* the page height every layout was designed at */
#ifdef CONFIG_POCKET_TANK_320X240
#define UI_PAGE_H  240              /* the CYD's page height: render.h's PAGE_H there */
#define UI(n)      (((n) * UI_PAGE_H + UI_DESIGN_H / 2) / UI_DESIGN_H)
#define UI_F       ((float)UI_PAGE_H / UI_DESIGN_H)                /* the same, for float sizes */
/* A text scale: 2 -> 1, 3 -> 2, 4 -> 3, 6 -> 4 on the CYD; never below 1. */
#define UI_TEXT(s) (UI(s) < 1 ? 1 : UI(s))
#else
#define UI_PAGE_H  UI_DESIGN_H      /* the page is the design */
#define UI(n)      (n)
#define UI_F       1.0f
#define UI_TEXT(s) (s)
#endif
#endif
