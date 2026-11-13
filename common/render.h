/* render.h — software renderer: draws the tank into a raw RGB565 framebuffer.
 * No LVGL/SDL dependency; the same code runs on the ESP32 (the display port
 * just decides where the buffer goes). */
#ifndef RENDER_H
#define RENDER_H

#include <stddef.h>
#include "tank.h"
#include "battery.h"

/* fb is TANK_W x TANK_H, RGB565, stride in PIXELS (usually TANK_W). */
/* PAGE space (2026-10-01): the full-screen pages (milestones, shop, settings,
 * updates, the setup flow), the prompts and the notices are laid out on the
 * rectangle's 448 x 368. On a bigger frame (the round bowl's 466 x 466) that
 * layout sits centred: page (0,0) is frame (PAGE_X, PAGE_Y). The drawing
 * helpers below (render_text, render_rect, ...) take PAGE coordinates; the
 * tap tests take the frame's, as the touch ports report them. In the
 * rectangle the two are the same thing. */
#define PAGE_W 448
#define PAGE_H 368
#define PAGE_X ((TANK_W - PAGE_W) / 2)
#define PAGE_Y ((TANK_H - PAGE_H) / 2)
#ifdef TANK_ROUND               /* the bowl's circle cuts the page's corners: a few buttons are drawn in from the glass */
#define PAGE_BOWL 1
#else
#define PAGE_BOWL 0
#endif
/* the watch (410 x 502, portrait): the glass is 38 px NARROWER than the page.
 * The page still sits centred - PAGE_X is -19, so the glass shows page x
 * 19..428 - in the middle of the glass's height (PAGE_Y 67, where the round
 * corners no longer reach it). The layouts keep their margins of 24..32 px,
 * so most of every page is inside that window as it stands; PAGE_NARROW
 * marks the few columns that had to come in. */
#define PAGE_NARROW (TANK_W < PAGE_W)
void render_tank(const tank_t *t, uint16_t *fb, int stride);

/* Optional frame profiling: set a microsecond clock and render_tank fills
 * render_prof_us per stage (0 scene-copy, 1 shafts, 2 veg (back), 3 food+
 * bubbles, 4 fish + front veg, 5 vignette sweep, 6 algae film). Accumulates
 * until the caller zeroes it. NULL = off. */
extern int64_t (*render_clock_us)(void);
/* THE FRAMEBUFFER'S BYTE ORDER (2026-10-23, the frame rate on the big glasses): with PT_FB_SWAP the frame (and the
 * scene it is primed from) holds every pixel as the panel's wire wants it - RGB565 high byte first, i.e. byte-swapped
 * as a little-endian uint16 - so the SPI DMA can take the frame straight from PSRAM, and the flush's CPU fill (19 ms
 * a frame on the square: the read out of PSRAM, the swap, the stripes) goes away. Every store into a frame or the
 * scene goes through FB_SWAP16, every load back; rgb565() and the colour math stay native. The sim builds with it
 * off (its pixel tests read the frame); `make OUT=fishsim-swap BUILD=build-arm-swap EXTRA_CFLAGS=-DPT_FB_SWAP=1`
 * builds it on, and its frames must match the plain build's byte for byte (write_ppm unswaps). */
#ifndef PT_FB_SWAP
#define PT_FB_SWAP 0
#endif
#if PT_FB_SWAP
#define FB_SWAP16(v) ((uint16_t)((((uint16_t)(v)) << 8) | (((uint16_t)(v)) >> 8)))
#else
#define FB_SWAP16(v) ((uint16_t)(v))
#endif
extern int64_t render_prof_us[14];   /* 0..6 the stages; 7..13 the fish stage split (2026-10-23): the fish, the snail + urchin,
                                      * the pieces IN FRONT restored, the front grass, the front overlays, the front lanes, the arch shade */

/* Optional static-scene cache (TANK_W*TANK_H uint16): the water gradient,
 * pebbles and backdrop decor are rendered once per lighting state and copied each
 * frame instead of recomputed - keeps core 0's frame budget flat as the scene
 * gets lusher. NULL disables. */
void render_set_scene_cache(uint16_t *buf);

/* Optional vignette alpha cache (TANK_W*TANK_H bytes): with a scene cache the
 * porthole vignette is baked into the scene and re-applied per frame only on
 * dynamic pixels; this LUT removes the per-pixel float math. NULL = compute. */
void render_set_vignette_cache(uint8_t *buf);   /* render_vignette_cache_bytes() of them: a quarter of the tank (0.4.2) */
size_t render_vignette_cache_bytes(void);
/* THE VIGNETTE ON | OFF (0.4.2, the GRAPHICS page): a DEVICE setting like GRAPHICS (the boards: NVS "vigoff", absent or 0 =
 * ON; the sim: its device file). render_set_vignette is the setting - a byte; the render takes it up at its next tank frame
 * (the scene and the grass loop bake again). OFF: no porthole vignette anywhere. ON, the default: every frame as ever. The
 * jelly tank draws none either way. */
void render_set_vignette(bool on);
bool render_vignette(void);          /* the setting: what the platform saves and the page shows */
bool render_vignette_live(void);     /* ... as the last tank frame drew it (the tests) */
int  render_vignette_alpha_at(int x, int y);   /* ... and the alpha the frames use at a pixel: 0..255 ON, 0 OFF (the tests) */
int render_vignette_check(void);   /* (the sim's check: pixels whose cached alpha differs; -1 not filled yet) */

/* Scene prefetch support: render_scene_buf returns the built scene cache (or
 * NULL) and its epoch (bumped on lighting rebuilds). A platform that copies
 * the scene into fb ahead of time (e.g. by DMA) calls render_fb_primed; the
 * next render_tank into that fb at that epoch skips its own scene restore. */
const uint16_t *render_scene_buf(unsigned *epoch);

/* Dirty mask (RENDER_DIRTY_WORDS uint32): required with the scene cache -
 * marks the pixels drawn each frame so the vignette re-apply reads the mask,
 * not the scene. Without it the renderer falls back to full redraws. */
#define RENDER_DIRTY_WORDS (TANK_H * ((TANK_W + 31) / 32))
void render_set_dirty_mask(uint32_t *buf);
void render_fb_primed(const uint16_t *fb, unsigned epoch);

/* Stats overlay for one selected fish: selection ring + a small card of
 * visual bars (drives + personality) and stage pips. No text, no digits —
 * the progression design's "simple and visual" stats view. Personality bars
 * are revealed only after the fish has shown that side of itself (milestone
 * bits): you learn your fish by watching. A MORE button sits at the foot
 * of the card (2026-09-16, Strato: "how do I get to the milestones and
 * settings screen? it's not very obvious") - the label is the only text on
 * it; a tap anywhere on the card, button or not, opens the milestones page.
 * fish_idx == RENDER_CARD_SNAIL (2026-09-16): the SNAIL's card instead - a
 * ring on the snail and a small centred card: the upright sprite at 2x and
 * how much algae it has grazed so far (tank_t.snail_grazed). The platforms
 * keep it in the same selection slot as a fish (a tap on the snail opens it,
 * a tap anywhere else dismisses it, no card cache). */
#define RENDER_CARD_SNAIL 99
/* fish_idx == RENDER_CARD_SHRIMP (2026-09-29): the shrimp school's card, the
 * snail's way - a ring round the school and a centred card: a shrimp at 4x,
 * how many, the pellets they have eaten, ten pips toward the next shrimp and
 * where that stands (N more / arrives in N min / full / too much algae). */
#define RENDER_CARD_SHRIMP 98
/* fish_idx == RENDER_CARD_URCHIN (2026-10-02): the urchin's card, the
 * snail's way - a ring on it and a centred card: the urchin at 3x, the grass
 * it has eaten (cm) and what it is up to (chewing / off to the tall grass /
 * resting). */
#define RENDER_CARD_URCHIN 97
/* fish_idx == RENDER_CARD_SEAHORSE (0.3.6): the seahorse's card - a ring on
 * it and a centred card: the seahorse at 2x, its days with the keeper, its
 * hitches, the trust it passed on and what it is doing. */
#define RENDER_CARD_SEAHORSE 96
/* fish_idx == RENDER_CARD_CRAB (0.3.9): the strawberry crab's card - a ring on
 * it and a centred card: the crab facing the glass at 2x, the pellets it has
 * found and what it is doing. */
#define RENDER_CARD_CRAB 95
/* the critter cards' arrows (0.3.9.3, Strato: "cycle through all the
 * critters from this UI"): two small boxes inside a critter card's top
 * corners, in the NEXT FISH arrow's dress - the left one steps back, the
 * right one on, through the critters this tank has (snail, shrimp, urchin,
 * seahorse, crab, wrapping round; the fish keep their own arrow). Dim with
 * only one, where there is nowhere to go. render_critter_nav_hit is the tap
 * test while critter card `card` is up: -1 the left box, +1 the right, 0
 * neither (the boxes plus slop); render_critter_step the card it steps to
 * (the same card with one critter, or for a card that is not a critter's). */
#define RENDER_CRIT_NAV_W  34     /* the NEXT FISH arrow's box */
#define RENDER_CRIT_NAV_IN 8      /* in from the card's corner */
int  render_critter_nav_hit(int card, float x, float y);
int  render_critter_step(const tank_t *t, int card, int dir);
bool render_critter_here(const tank_t *t, int card);   /* that critter is in the tank (its card would draw) */
/* THE CRITTERS' NAMES (0.4.2): the snail's, urchin's, crab's and seahorse's cards (not the shrimp school's) carry a
 * button at the foot, NAME (no name yet) or RENAME, in their last CRIT_NAME_ROW_H; a named critter's card is titled with
 * its name. The press opens the fish's letter wheel for it (setup_begin_crit_rename) from the platform's touch loop, as
 * a fish card's RENAME does; render_critter_name_hit is its tap test while critter card `card` is up (the box plus slop,
 * the frame's coordinates). render_crit_name_of_card: the card's CRIT_NAME_* (-1 = none: the shrimp, a fish), and back */
#define RENDER_CRIT_NAME_W 132
#define RENDER_CRIT_FOOT_GAP 16   /* NAME | SHELVE side by side at the foot (0.4.2) */
bool render_critter_name_hit(const tank_t *t, int card, float x, float y);
bool render_crit_name_box(int card, int *x, int *y, int *w, int *h);
int  render_crit_name_of_card(int card);
/* THE CRITTER SHELF on a critter's card (0.4.2, docs/critter-shelf.md): SHELVE at the foot, right of NAME (alone on the
 * shrimp school's card). Asked twice (kids): render_critter_shelve_tap answers CRIT_SHELVE_ARMED for the first tap (the
 * button reads SURE? and a line over the foot says who waits on the shelf) and for a doubled one, CRIT_SHELVE_GO for the
 * second tap a moment later - the platform then calls progression_shelve(t, render_crit_item_of_card(card)) and closes the
 * card - and CRIT_SHELVE_NONE off the button. An armed button lapses by itself in a few seconds. */
enum { CRIT_SHELVE_NONE, CRIT_SHELVE_ARMED, CRIT_SHELVE_GO };
bool render_crit_shelve_box(int card, int *x, int *y, int *w, int *h);
bool render_critter_shelve_hit(const tank_t *t, int card, float x, float y);
int  render_critter_shelve_tap(const tank_t *t, int card, float x, float y);
void render_critter_shelve_disarm(void);
int  render_crit_item_of_card(int card);   /* the card's critter as an SD_ITEMS index (-1: a fish's) */
int  render_card_of_crit_name(int k);
bool render_crit_spot(const tank_t *t, int k, float *x, float *y, float *r);   /* its ring on the card: the tank's coordinates */
void render_crit_portrait(uint16_t *fb, int stride, const tank_t *t, int k, int cx, int cy);   /* its card's art at 1x, centred (PAGE) */
void render_stats_card(const tank_t *t, int fish_idx, uint16_t *fb, int stride);
/* Optional card cache (RENDER_CARD_W x RENDER_CARD_H uint16): with the scene
 * cache live, the card is redrawn at most 4x/s and blitted otherwise (~7 ms
 * -> ~1 ms per frame on the device). NULL = draw every frame. */
#ifdef TANK_ROUND               /* the bowl: where the circle is tall enough for the card; 0.4.2: 78 -> 113, the toolbox's column on its left */
#define RENDER_CARD_X 113
#define RENDER_CARD_Y 62
#elif defined(TANK_WATCH)       /* the watch: in from its round corner */
#define RENDER_CARD_X 36
#define RENDER_CARD_Y 36
#elif defined(TANK_SQUARE)      /* the square: its bezel's corner clipped the card at the 1.8's 14 / 8 (Strato, 2026-10-12) */
#define RENDER_CARD_X 26
#define RENDER_CARD_Y 22
#else
#define RENDER_CARD_X 14
#define RENDER_CARD_Y 8
#endif
#define RENDER_CARD_W 124
#define RENDER_CARD_H 258       /* 228 + the MORE button strip (2026-09-16) */
/* the card's tap hit box (touch ports): the card itself plus slop, most of
 * it BELOW the MORE button - fingers aiming at a button by the foot land
 * low and wide (Strato, 2026-09-16: "I'm not tapping it reliably"). Since
 * the toolbox (2026-10-01) the slop below is the gap down to it: 56 px of
 * it was for the panel's stretched report, which the calibration (09-30)
 * took out. RENDER_CARD_HIT(x, y) is the test. */
#define RENDER_TOOLS_GAP 14
#define RENDER_CARD_HIT_BELOW RENDER_TOOLS_GAP
#define RENDER_CARD_HIT_SIDE  12
#define RENDER_CARD_HIT(x, y) ((x) >= RENDER_CARD_X - RENDER_CARD_HIT_SIDE && (x) < RENDER_CARD_X + RENDER_CARD_W + RENDER_CARD_HIT_SIDE && \
                               (y) >= RENDER_CARD_Y && (y) < RENDER_CARD_Y + RENDER_CARD_H + RENDER_CARD_HIT_BELOW)
void render_set_card_cache(uint16_t *buf);
void render_set_modal_cache(uint16_t *give, uint16_t *gift);
void render_modal_cache_on(bool on);   /* false: drawn whole (the bakes kept) - measuring */   /* the GIVE modal's and the gift card's bakes (GIFT_CARD_W x GIFT_CARD_H each; NULL: drawn whole) */
void render_set_modal_alloc(void *(*alloc)(size_t), void (*release)(void *));   /* THE CARDS LENT (0.4.2): the two bakes taken when a card is
                                                   drawn and given back ~3 s after it is gone (the device; render_set_modal_cache: fixed buffers) */
bool render_modal_bakes_held(int *n);                /* (how many of the two are held now: the director's and the selftest's look) */
void render_crab_cache(bool on);        /* the crab's pose grids kept (on, the default) or built every pose change: the selftest's A/B (0.3.9.1) */
int  render_crab_builds(void);          /* the crab's grid builds so far */
/* The TOOLBOX (2026-10-01; tiles since 0.4.2): with a fish's card, a box in the card's dress holding a tile a tool -
 * the SPONGE, the SCISSORS (tank.h TOOL_*), the BREEDING tool once SELECTIVE BREEDING is owned - and GIVE last while the
 * tank is linked. The 1.8: a row under the card; the watch / square: two a row under it; the pendant: a column on the
 * card's left. The one in hand is lit (a white double edge on a lighter fill - brightness, not hue); GIVE is gold.
 * render_tools_hit is the tap test while a fish's card is up: the tool (TOOL_*) or -1 (GIVE's tile is render_give_hit's);
 * render_tool_tile gives a tile's box (RENDER_TOOLS_GIVE for GIVE's, -1 for the whole toolbox; false: not shown). */
#define RENDER_TOOLS_GIVE 10
int  render_tools_hit(const tank_t *t, int fish, float x, float y);
bool render_tool_tile(const tank_t *t, int fish, int tool, int *x, int *y, int *w, int *h);
/* SELECTIVE BREEDING (0.4.2): the BREEDING tool in hand. render_breed_tool_up as it is picked up (a pair already set shows
   at once); render_breed_tap takes EVERY tap while it is in hand - the PAIR card's buttons (THE TOUCH RULE), the chip's
   CANCEL, a fish of ours marked (the second opens the card), a visitor's line - and answers RENDER_BT_*. PAIR THEM pays
   (progression_breed_pair); UNPAIR refunds; CANCEL / DONE put the tool back. render_breed_card draws the card (after the chip) */
enum { RENDER_BT_NONE, RENDER_BT_HELD, RENDER_BT_FIRST, RENDER_BT_UNMARKED, RENDER_BT_SECOND, RENDER_BT_VISITOR, RENDER_BT_PAIRED,
       RENDER_BT_REFUSED, RENDER_BT_UNPAIRED, RENDER_BT_CANCEL, RENDER_BT_DONE };
void render_breed_tool_up(const tank_t *t);
int  render_breed_tap(tank_t *t, float x, float y);
bool render_breed_card_up(void);
bool render_breed_button_at(const tank_t *t, float x, float y);   /* the touch port's firm-press test while the tool is in hand */
void render_breed_pick(const tank_t *t, int fish);
void render_breed_show_pair(const tank_t *t, int a, int b);
void render_breed_close(void);
void render_breed_card(const tank_t *t, uint16_t *fb, int stride);
#define RENDER_SIDE_GAP 6          /* the NEXT arrow off the card (0.3.9) */
/* GIVE on the stat card (0.4, Strato 2026-11-01; a toolbox tile since 0.4.2): while the tank is linked, GIVE -> the GIVE
 * modal (CONFIRM / PICK -> ASKING, a tap takes it back -> GIVEN). render_give_hit is its tile's tap test while a fish's
 * card is up (false when it is not shown) */
bool render_give_hit(const tank_t *t, int fish_idx, float x, float y);
void render_give_tap(const tank_t *t, int fish_idx);   /* a tap on it (or the keeper card's GIVE): the GIVE modal */
/* THE GIVE MODAL (0.4, Strato 2026-11-02): both GIVE buttons open one card over the live tank or the keeper page -
 * CONFIRM (the tank it goes to, GIVE | BACK), PICK (both doors open: a row a tank, BACK; picking is the yes), ASKING
 * (the other keeper's time, CALL OFF), SAID (a NO, or it cannot go now: until OK), WHY (a dim GIVE: why, OK). Every
 * button but BACK ignores a press that began before its step came up, and, where a tap brought the step, that tap's
 * double (THE TOUCH RULE, touch_gate.h). While it is up it takes every tap (a tap off it:
 * nothing). render_give_modal draws it over the live tank; the keeper page draws its own. */
enum { RENDER_GM_OFF, RENDER_GM_CONFIRM, RENDER_GM_PICK, RENDER_GM_ASKING, RENDER_GM_SAID, RENDER_GM_WHY,
       /* THE TRAVEL BOWL's cards (0.4.2, docs/move-fish-0.4.2.md) are this modal's too - one card over the tank or the keeper
          page, so every rule of the GIVE modal (its taps, the notices under it, the pages over it) holds for them:
          MOVE: PUT <NAME> IN THE TRAVEL BOWL? MOVE | BACK; MOVE_WHY: why it stays, OK; BOWL: <NAME> IS WAITING, LET IN | NOT NOW */
       RENDER_GM_MOVE, RENDER_GM_MOVE_WHY, RENDER_GM_BOWL,
       RENDER_GM_DONE };   /* (_DONE: MOVE / LET IN just done - nothing drawn, every tap spent for a double tap's length, then OFF) */
enum { RENDER_GM_TAP_NONE, RENDER_GM_TAP_CARD, RENDER_GM_TAP_HELD, RENDER_GM_TAP_GIVE, RENDER_GM_TAP_LEFT, RENDER_GM_TAP_RIGHT,
       RENDER_GM_TAP_BACK, RENDER_GM_TAP_CALLOFF, RENDER_GM_TAP_OK, RENDER_GM_TAP_MOVE, RENDER_GM_TAP_LETIN };   /* (BACK is NOT NOW's too) */
int  render_give_modal_step(const tank_t *t);            /* RENDER_GM_OFF: not up */
void render_give_modal_close(void);                     /* as BACK (the director) */
const char *render_fry_first_line(const tank_t *t);     /* why SELL/GIVE wait on a fry not yet named: dark, the light first (0.4.2) */
const char *render_give_modal_line(const tank_t *t);     /* its words as drawn ("IT WOULD LIVE THERE", "THEIR TANK IS FULL", ...) */
void render_give_modal(const tank_t *t, uint16_t *fb, int stride);
/* A TAP'S PRESS (0.4.1 audit round 1, TCH-1, touch_gate.h READ AT THE PRESS): the touch port names the tank clock its
   press began at before the release's taps, and ends it after; the waits in the taps below (the keeper card's SELL, the
   shop's modal wait, its SELL and its rows, the GIVE modal) test that against their stamps; nothing restarts them.
   With none named (a direct caller, the sim's own mouse) the press is the lift, as before. */
void render_tap_press(double clock);
void render_tap_press_end(void);
int  render_give_modal_tap(const tank_t *t, float x, float y);   /* RENDER_GM_TAP_*: what the tap did */
/* THE TRAVEL BOWL (0.4.2): MOVE on a fish's card (the keeper page; MOVE shows once the board has a second tank, or the
   bowl a fish) opens the MOVE card, or why the fish stays; the IS WAITING card comes by itself once a start (notice.c)
   and from the keeper page's waiting row. For tests and the director: the taps' places */
void render_move_tap(const tank_t *t, int fish_idx);          /* MOVE on its card: the card, or why not */
void render_bowl_card(const tank_t *t);                       /* <NAME> IS WAITING (nothing if the bowl is empty) */
void render_bowl_card_aside(void);                            /* ... put away as NOT NOW does (notice.c: a notice that can't wait) */
#define RENDER_BOWL_CARD_S 30.0f                              /* ... and by itself, unanswered this long (the waiting row still says it) */
bool render_milestones_move(const tank_t *t, int *x, int *y); /* the card's MOVE button (false: not shown) */
bool render_bowl_row(int *x, int *y, int *w, int *h);         /* the keeper page's waiting row (false: the bowl is empty) */
void render_bowl_icon(uint16_t *fb, int stride, int x, int y, bool lit);   /* the bowl's little picture, 30 x 30 (frame coordinates) */
bool render_give_modal_button(const tank_t *t, int which, int *x, int *y);   /* a button's middle (tests, the director); false: not shown */
/* the NEXT FISH arrow (0.3.9, Bananna on Discord: "sometimes fish hide
 * behind the stat screen"): a small box just outside the card's top right,
 * in the lock's dress, one arrow; a tap steps the card on to the next fish
 * (wrapping round). Dim with a single fish, where there is nowhere to go.
 * render_next_hit is its tap test while a fish's card is up - the box plus
 * slop, most of it right and below; the platforms test it BEFORE the card's
 * own (RENDER_CARD_HIT's side slop reaches past the gap). A tap on it never
 * reaches the water, dim or not. render_next_fish is the fish it steps to
 * (the same one with a single fish). */
#define RENDER_NEXT_W   34
#define RENDER_NEXT_H   34
#define RENDER_NEXT_X   (RENDER_CARD_X + RENDER_CARD_W + RENDER_SIDE_GAP)
#define RENDER_NEXT_Y   RENDER_CARD_Y
bool render_next_hit(float x, float y);
int  render_next_fish(const tank_t *t, int fish_idx);
/* With a tool in hand and no fish card up, a chip at the top left says so:
 * the tool and DONE. A tap on it (render_tool_chip_hit) puts the tool back
 * in the box. render_tool_chip draws nothing with TOOL_HAND, when
 * fish_idx is a fish (its card shows the box instead), or while a stroke is
 * on the glass (0.3.5: the film under it shows as it is wiped). */
void render_tool_chip(const tank_t *t, int fish_idx, uint16_t *fb, int stride);
bool render_tool_chip_hit(const tank_t *t, float x, float y);

/* Device battery pill (top-right), drawn with the stats card on hardware,
 * while the battery is low, and for BAT_POPUP_S after the cable goes in:
 * frac 0..1, state = BAT_* (battery.h), clock (s) runs the sweep. Redrawn
 * 2026-09-24 for a color-blind keeper: on the cable a lightning bolt stands
 * left of the pill (gray while the charger rests), and while charge flows a
 * bright band sweeps the fill - shape and motion, hue only as a repeat.
 * A tap in RENDER_BAT_HIT (the pill plus a fingertip's slop, most of it
 * below and to the left) opens the battery page - while the pill shows. */
#define RENDER_BAT_W 32
#define RENDER_BAT_H 15
#ifdef TANK_ROUND               /* the bowl: in the NEXT arrow's lane, to the right - its right end 48 px in from the circle at its top
                                   line (the glass is at x 400 there), the bolt left of it (0.3.9.1, Strato's mockup: a keeper tapping
                                   the arrow to step through the fish opened the battery page - the pill sat top centre and its slop
                                   reached over the arrow, and the pill's tap is tested first). Its slop now starts 18 px right of the
                                   arrow's. 0.4.2: the card moved 35 px right (the toolbox's column), the arrow with it - the pill 320 -> 338,
                                   its slop clear of the arrow's again, its right end 31 px in from the glass */
#define RENDER_BAT_X 338
#define RENDER_BAT_Y (RENDER_NEXT_Y + (RENDER_NEXT_H - RENDER_BAT_H) / 2)
#elif defined(TANK_WATCH)       /* the watch: the top right, in from its round corner */
#define RENDER_BAT_X (TANK_W - RENDER_BAT_W - 56)
#define RENDER_BAT_Y 22
#elif defined(TANK_SQUARE)      /* the square: in from its bezel's corner, like the card */
#define RENDER_BAT_X (TANK_W - RENDER_BAT_W - 44)
#define RENDER_BAT_Y 22
#else
#define RENDER_BAT_X (TANK_W - RENDER_BAT_W - 28)     /* clear of the curved bezel; the bolt sits left of it */
#define RENDER_BAT_Y 8
#endif
#define RENDER_BAT_HIT(x, y) ((x) >= RENDER_BAT_X - 44 && (y) < RENDER_BAT_Y + RENDER_BAT_H + 40)
void render_battery(uint16_t *fb, int stride, float frac, int state, double clock);
/* the battery page (2026-09-24): a panel over the live tank - the battery
 * large with its percent, the state in words, time left / time to full,
 * then since the cable moved, screen-on time on this charge, what a full
 * charge lasts (battery_info fills bi) and the voltage, dim. Any tap closes
 * it (the platforms); it closes itself after a while. */
void render_battery_info(uint16_t *fb, int stride, const bat_info_t *bi, double clock);
/* an announcement over the live tank (notice.h: a milestone the moment it
 * is earned, a stage reached, low battery, lights out), in the milestones page's modal
 * style; frac_left (1 -> 0) is its remaining time, drawn as a thin bar */
void render_notice(const tank_t *t, uint16_t *fb, int stride, int kind, int fish, uint32_t bit, float frac_left);

/* Milestones page (separate screen, never on the tank; 2026-09-13 redesign
 * on Strato's pixel-art badges): one row per fish - its sprite at its real
 * size, its name, a growth strip fry -> elder - and six event badges; the
 * tank's row below with the population strip and the tank badges - six a
 * page; the shrimp school's seventh (once there are shrimp) turns the row
 * into pages behind a small arrow at its right end (2026-09-30). A locked
 * badge is the same picture as a grey silhouette; one earned since the
 * keeper last closed the page wears a ring. While the tank can still grow,
 * a NEW FRY row sits under the last fish (2026-09-14): the fry-to-be as a
 * silhouette, a tick per gate, and the next arrival's gates as badges
 * (progression_next_fry) - lit once met, a filling bar under each still
 * owed; a tap on a gate says what to do and where it stands, with a HOW?
 * button that flips to a tip page (progression_fry_tip: how the keeper
 * moves that gate); a tap on the name gives the tally. render_milestones_tap maps a tap: a badge, a name
 * or a strip opens a small detail modal (the art at 2x, a title, the
 * words). Arrow buttons at the modal's top corners (2026-09-16) step to the
 * previous / next of its group without leaving it - a fish's six badges,
 * the tank's (across its pages), the fry checklist's gates, or, from a fish's name, the
 * fish themselves (wrapping; a group of one shows none). Any other tap
 * closes the modal. A CLOSE button at the
 * bottom right leaves the page; a SETTINGS button at the bottom left
 * leaves it for the settings page; both the settings page's and the shop's
 * CLOSE bring the milestones page BACK (the platforms do that). */
void render_milestones(const tank_t *t, uint16_t *fb, int stride);
/* THE PAGE KEPT (0.4, render.c): a framebuffer that holds the keeper page from the last time it was drawn into, nothing
 * drawn into it since, keeps it - only the moving fish are drawn again. "Since" needs the frame's beginning: call
 * render_frame_begin once a frame (without it the page is always drawn whole). The counts: frames kept / drawn whole. */
void render_frame_begin(void);
enum { MS_WHY_NEW, MS_WHY_ZONE, MS_WHY_CHANGED, MS_WHY_OVER, MS_WHY_GAP, MS_WHY_OUT, MS_WHY_N };   /* drawn whole: not this buffer's page, a box
                                                                         out of its zone then, the page changed, drawn over since, a frame without it, a box out now */
void render_milestones_counts(int *kept, int *whole, int why[MS_WHY_N]);   /* since boot (why may be NULL) */
void render_debug_page_whole(bool on);   /* (the selftest's reference: the page drawn whole, nothing kept) */
/* a tap on the page (2026-09-13, Strato: with this much to tap, a stray tap
 * must not drop the whole page): MS_TAP_CLOSE = the CLOSE button, bottom
 * right - the ONLY way out by touch (caller closes the page, then
 * progression_ack_milestones + render_milestones_leave); MS_TAP_KEPT = a
 * badge / name / strip opened the detail modal, or the modal was up and
 * this tap closed it; MS_TAP_NONE = nothing here (the caller may try the
 * brightness row). */
enum { MS_TAP_NONE = 0, MS_TAP_KEPT = 1, MS_TAP_CLOSE = 2, MS_TAP_SETTINGS = 3,   /* SETTINGS: the button bottom left (2026-09-15) opens the settings page */
       MS_TAP_SHOP = 4,                                                            /* the foot's sand dollar (UPGRADES; on the TANK row until 0.4) opens the shop */
       MS_TAP_VISITORS = 5,                                                        /* the foot's plane: the VISITOR COLLECTION (2026-10-06; the icon foot, 0.4) */
       MS_TAP_TANKS = 6,                                                           /* the foot's shelf: MY TANKS (0.4; on the settings page in 0.3.9.3) */
       MS_TAP_RENAME = 16, MS_TAP_SELL = 32,                                       /* a fish's card (2026-10-01): + the fish's index */
       MS_TAP_ABOUT = 48 };                                                        /* ... its MORE (0.4.2): + the fish's index */
/* MS_TAP_RENAME + fish: the card's RENAME - the caller leaves the page (ack +
 * leave, as for CLOSE) and opens setup_begin_rename; when that flow ends
 * (setup_take_renamed) it brings the page back with render_milestones_show_fish.
 * MS_TAP_SELL + fish: the card's SELL, confirmed (its second tap) - the
 * caller calls progression_sell_fish; the page stays up, a row shorter. */
uint32_t render_milestones_tank_shown(void);   /* the tank badges the keeper page has drawn since it opened (seen at its close) */
uint32_t render_tank_badge_bits(void);         /* every tank badge's bit (the rest have no badge: no ring to keep) */
int  render_milestones_tap(const tank_t *t, float x, float y);
void render_milestones_show_fish(const tank_t *t, int fish);   /* put a fish's card up (the page must be showing) */
/* where things are, in the FRAME's coordinates as a tap arrives - for the
 * tests and the director, so neither copies a board's layout: a fish's card
 * (false = none up) with the centres of RENAME and SELL; a point on a row's name */
bool render_milestones_card(const tank_t *t, int *fish, int *rename_x, int *sell_x, int *btn_y);
bool render_milestones_give(const tank_t *t, int *x, int *y);   /* the card's GIVE (linked only: false when it is not shown) */
bool render_milestones_more(const tank_t *t, int *x, int *y);   /* the card's MORE (0.4.2: false with no card, or GIVEN) */
/* MORE's pages (setup.c, setup_is_about) over the keeper page halved: the page drawn into a buffer once, then held - the
   panel alone drawn again each frame. In place of render_milestones AND render_setup while setup_is_about(). */
void render_keeper_about(const tank_t *t, uint16_t *fb, int stride, double clock);
void render_keeper_about_counts(int *kept, int *whole);
void render_keeper_about_panel_counts(int *band, int *panel);   /* THE PANEL KEPT: frames that drew the band alone / the panel whole */
const char *render_milestones_line(void);   /* the line under the card's caption as last drawn ("" = none) */
const char *render_milestones_caption(int line);   /* the modal's caption, line 0 or 1, as last opened ("" = none) */
bool render_milestones_arrow(const tank_t *t, bool right, int *x, int *y);   /* the modal's left / right arrow (false = no modal up) */
void render_milestones_row(int row, int *name_x, int *y);
/* a sideways swipe on the milestones page (release - press dx): along the
 * TANK row, with more badges than one row holds, it turns the row's page
 * (2026-09-30). True = it did (or was a page swipe at the row's end);
 * false = not a page swipe. */
bool render_milestones_swipe(const tank_t *t, float x, float y, float dx);
void render_milestones_leave(void);
void render_fish_removed(int idx);   /* a fish left, the slots shifted: the keeper card and its armed SELL follow their fish (or close) */

/* The shop (2026-09-15): the sand dollar page. The balance at the top, one
 * row per item (SD_ITEMS: the art, the name, the price, UNLOCK / IN TANK), a
 * HOW TO EARN button bottom left (a modal listing the sources) and CLOSE
 * bottom right. A tap on a row opens the item's modal - the art at 2x, the
 * words, the price, an UNLOCK button; render_shop_tap returns SHOP_TAP_BUY +
 * item when that button is tapped (the caller calls progression_buy; a short
 * balance was already a dim button), SHOP_TAP_CLOSE for the way out (back
 * to the milestones page, 2026-09-16),
 * SHOP_TAP_KEPT when a modal opened or closed. Page state is render-local;
 * render_shop_leave clears it when the page closes. */
enum { SHOP_TAP_NONE = 0, SHOP_TAP_KEPT = 1, SHOP_TAP_CLOSE = 2, SHOP_TAP_BUY = 16, SHOP_TAP_MOVE = 32, SHOP_TAP_SELL = 64,
       SHOP_TAP_FRY = 128, SHOP_TAP_SHELVE = 256, SHOP_TAP_PLACE = 512, SHOP_TAP_BG_BUY = 1024, SHOP_TAP_BG_USE = 2048 };
/* SHOP_TAP_BG_BUY / SHOP_TAP_BG_USE + bg (0.4.1, THE BACKGROUNDS): a background's modal UNLOCK (progression_bg_buy) or USE
 * (progression_bg_use); the platform calls them - the shop stays up, the tank rebuilds when it closes */   /* BUY / MOVE / SELL / SHELVE / PLACE + item index; FRY + the fry's place in the offer (0.3.5) */
/* SHOP_TAP_SHELVE / SHOP_TAP_PLACE (0.3.9, the decoration shelf): a piece in
 * the tank has MOVE | SHELVE in its modal (SELL moved to the placement page);
 * SHELVE puts it away (progression_shelve) and the shop stays up - its row
 * now says ON THE SHELF with a PUT BACK button, and its modal PLACE | SELL.
 * PUT BACK / PLACE return SHOP_TAP_PLACE + item: the platform unshelves it
 * and opens the placement page with the kept spot in place. */
/* THE FRY (0.3.5, progression_fry_*): once a fish has been sold the shop's
 * first row is FRY - the first fry still for sale at its left, the price,
 * CHOOSE (SOLD OUT with all three bought, FULL with no slot). Its modal
 * shows the three side by side, a bought one a blank; a tap picks one (lit:
 * a white double edge), BUY returns SHOP_TAP_FRY + its place. Under them the
 * price, why BUY is dim (no room, too few dollars) and when a fresh three
 * comes. */
/* SHOP_TAP_SELL (2026-09-24): an owned placeable piece's modal has SELL next
 * to MOVE; the first tap arms it ("+30 OK?"), the second returns SELL + item
 * and the platform calls progression_sell. Test SELL before MOVE before BUY. */
/* SHOP_TAP_MOVE (2026-09-16): an owned, placeable item's modal carries a MOVE
 * button - the platform closes the shop and opens setup.c's placement page
 * (setup_begin_place), the same page a purchase opens. */
/* THE BOOT SHOW (0.4.1): while set, every bake hands hook each row's background layers (0 the water; 1.. far to near) - the
 * boot show builds the background up by them. NULL: off. */
void render_boot_bg_layers(void (*hook)(int y, const uint8_t *lay));
int      render_bg_current(void);                 /* the background the last tank was drawn on */
uint16_t render_boot_water_px(int x, int y);      /* the water alone at (x, y), as baked at full light: a background pixel not yet landed */
void render_debug_specks_off(bool off);   /* (tests: Twilight's drift left out of the frame, to compare against) */
void render_debug_force_bg(int bg);   /* THE BACKGROUNDS (0.4.1): the sim draws every tank on background bg (-1: each tank its own) */
void render_shop(const tank_t *t, uint16_t *fb, int stride);
int  render_shop_tap(const tank_t *t, float x, float y);
bool render_shop_modal_up(void);   /* a row's modal or HOW TO EARN is up */
void render_shop_leave(void);
/* THE SHOP'S AREAS (0.4.1): the foot's squares and what each lists. A row's code is an SD_ITEMS index, SHOP_ROW_FRY for the
 * fry, or SHOP_ROW_BG + a background. render_shop_find says where a row is (the tests walk there with the foot and arrows). */
enum { SHP_AREA_DECOR, SHP_AREA_CRITTERS, SHP_AREA_BG, SHP_AREAS };
enum { SHF_EARN, SHF_DECOR, SHF_CRIT, SHF_BG, SHF_CLOSE, SHF_N };
#define SHOP_ROW_FRY (-1)
#define SHOP_ROW_BG  100
#define SHOP_ROW_BREED 90          /* SELECTIVE BREEDING's row (0.4.2): CRITTERS' last; its UNLOCK answers SHOP_TAP_BUY + SHOP_ITEM_BREED */
#define SHOP_ITEM_BREED SD_ITEM_COUNT   /* (past the SD_ITEMS rows: progression_breed_buy, never progression_buy) */
bool render_shop_find(const tank_t *t, int code, int *area, int *page, int *row);
int  render_shop_area_rows(const tank_t *t, int area);
void render_shop_foot_box(int b, int *x, int *y);   /* the shop foot's square b (0 ?, 1 DECORATIONS, 2 CRITTERS, 3 BACKGROUNDS, 4 X), page coordinates */
/* the tests: piece `item` painted pixel (x, y) in the last render_tank (its
 * recorded silhouette; the castle's static one) */
bool render_decor_covers(int item, int x, int y);
int  render_coral_cells(float growth);   /* the coral sprite's filled cells at a growth (the sim's selftest) */
int  render_cluster_cells(float growth); /* the reef cluster's, likewise */
/* the decor scratch (2026-09-24): the coral's and the cluster's tables in one
 * block the platform provides - PSRAM on the board (internal RAM is spoken
 * for); the sim leaves it unset and render calloc's it */
size_t render_decor_scratch_size(void);
/* the pumpkin lantern (0.3.6, SD_ITEM_PUMPKIN): its sprite's pixels, and
 * in *glow the ones redrawn every frame (the face + the flame) */
int    render_pumpkin_px(int *glow);   /* its pixels; *glow = the ones redrawn every frame */
/* the touched mask and the restore (2026-10-23; render.c "The TOUCHED mask"): the firmware keeps a mask per framebuffer
 * (RENDER_DIRTY_WORDS words), hands the frame's over before render_tank, says when a frame is a whole page
 * (render_touch_all), and once the frame has left the wire puts the scene back under it (render_restore_touched: the
 * epoch it returns primes the buffer, render_fb_primed) */
void     render_set_touched_mask(uint32_t *buf);
/* the grass cache (2026-10-24, render.c "THE GRASS CACHE"): a scene-sized buffer and a RENDER_DIRTY_WORDS mask, then on/off and
 * its period; the prof gives the refreshes' time and count since the last call */
void     render_set_grass_cache(uint16_t *buf, uint32_t *mask);
void     render_set_grass_cache_on(bool on, int period_ms);
/* THE GRASS LOOP (0.3.9.3, render.c): the back grass baked into a loop of bands of its sway (up to 16), and eight
 * fine moments a band kept as their difference from it; a frame is primed with the scene above the grass's band
 * and the band of its moment below, its fine moment laid over that. The platform hands over an allocator
 * (64-byte aligned; PSRAM on the board) and a memory budget, then turns it on. render_grass_loop_parts is the
 * prefetch's: the scene and the band for the frame at `clock`, and the epoch to hand render_fb_primed (false: no
 * band ready - copy render_scene_buf as before). render_grass_loop_n and _ready count fine moments.
 * render_grass_loop_lock(m): the loop plans m moments (0: as many as the budget holds) and STEPS one a frame - the
 * grass moves with the frames, its sway's period m frames x 2 (0: the moment is the clock's). */
/* THE BACKGROUNDS' bake scratch (0.4.1): bg_scratch_size() bytes borrowed for each bake and given back at once. Not the grass
 * loop's lender: its headroom is for memory it keeps (on the boards the loop leaves ~1.25 MB of PSRAM free). Unset: malloc. */
void     render_set_bake_scratch(void *(*alloc)(size_t), void (*release)(void *));
void     render_set_grass_loop(void *(*alloc)(size_t), void (*release)(void *), size_t budget);
void     render_grass_loop_on(bool on);
bool     render_grass_loop_is_on(void);
bool     render_grass_loop_parts(double clock, const uint16_t **scene, size_t *top_bytes, const uint16_t **band, size_t *band_off, size_t *band_bytes, unsigned *ep);
void     render_grass_loop_prof(int *n, int *bakes, int64_t *us, int *rows);   /* moments, bakes and their time since the last call, the band's rows */
void     render_grass_loop_bypass(bool on);   /* (the sim's tests: the loop kept but not used - the old drawing) */
void     render_force_veg_clock(float c);     /* (... and the sway pinned to a moment; -1 = the frame's) */
float    render_sway_moment(const tank_t *t); /* (selftest-clock: the sway's moment the grass is drawn at now, 0..its period in s) */
float    render_fish_beat(const tank_t *t, int i);   /* (selftest-clock: the tail beat's phase fish i is drawn at now, radians, any lap) */
int      render_grass_loop_n(void);
int      render_grass_loop_ready(void);       /* moments baked and current */
void     render_grass_loop_lock(int moments);
int      render_grass_loop_moment(void);      /* the last frame's moment (stepping), -1 before one */
void     render_grass_loop_arena(int *used_kb, int *cap_kb);
int      render_grass_loop_nomem(int *why);
void     render_grass_loop_top(int *bed, int *segs, int *beds);   /* (the log) the tallest back frond the band is sized for: its bed, its segments, the beds counted */   /* rows the plan gave up on (0: it did not) and where (1 the tables, 2 the bake's buffers, 3 fewer than four bands or no arena) */   /* the fine moments' differences: KB used of KB held */
bool     render_grass_loop_patching(void);   /* THE PATCH (0.3.9.4): the loop stale inside boxes only - served with them repainted, rebaked inside them */
int      render_grass_loop_boxes(int box[][4]);   /* ... the boxes (x0, y0, x1, y1 each; up to 6) */
bool     render_grass_loop_dump(int j, uint16_t *px, uint32_t *mask, int *y0, int *rows);   /* (the tests) moment j as the loop holds it: its rows and its grass mask */
int      render_grass_loop_planned(void);   /* the moments the loop is PLANNED for (GRAPHICS LOW: HIGH's plan it holds; else render_grass_loop_n) */
void     render_set_grass_loop_task(bool on);   /* THE SECOND CORE: the platform bakes the slices itself, between frames (render_grass_loop_bake_slice under its lock) */
int      render_grass_loop_bake_slice(const tank_t *t);
bool     render_grass_loop_shrunk(int *rows0, int *rows1, int *m0, int *m1);   /* THE BAND SHRINKS: a shrink's replan, once (the band's rows and moments before and after) */
int      render_grass_loop_plans(void);       /* (the tests) plans made so far */
/* (the tests, GRAPHICS MEDIUM's night picture: render.c THE TWO BANKS) */
bool     render_grass_loop_night(void);        /* the night picture is the loop in use */
int      render_grass_loop_night_held(size_t *bytes);   /* its moments held (0: no room for it), and its memory */
int      render_grass_loop_swaps(void);        /* bank switches so far */
void     render_grass_loop_bank_bakes(int *day, int *night);   /* moments each bank has baked (since its memory was taken) */
uint32_t render_grass_loop_day_digest(bool pixels);   /* the day's loop: its memory's places, plan, look and moments baked (pixels: its bands' and arena's bytes too) */
void     render_debug_gl_night_refuse(bool on);       /* the night picture's room refused: MEDIUM's night as HIGH's */
bool     render_grass_loop_night_short(void);  /* the night picture in use is short of room (taken again at the next frame) */
void     render_grass_loop_bytes(size_t *day, size_t *night);   /* each bank's memory against the loop's budget */
void     render_debug_gl_restore_refuse(bool on);     /* ... and the bands traded for it not given back */
void     render_debug_gl_arena_refuse(bool on);       /* the next MEDIUM plan's arena refused with the night picture held (the bands traded for it put back) */
void     render_grass_loop_day_moments(int *planned, int *shape, int *ready);   /* the DAY's loop whichever bank is live: its plan's moments, its shape's in use, and baked
                                                                                   (the night picture in use: the day's last, ready 0 - main.c's planned-up-again skips the night) */
void     render_debug_gl_night_div(int div);          /* its arena sized a band over div (0: as built) - short of room, taken again */
void     render_debug_gl_shrink_old(bool on);   /* (the tests) the control: the band never shorter while the loop holds the grass, as before 0.4.2 */
void     render_grass_loop_probe(int out[5], int *odd, int *bands);   /* THE GRASS PROBE, since the last call: loop frames, live frames, switches between them, THROUGH
                                                 frames with the odd fronds, THROUGH drawn BEHIND for want of the odd masks; the odd masks held (of bands + 1) and the bands */
void     render_grass_loop_debug_budget(int us);   /* (the tests) a frame's budget for the bake's slices (0: as built) */
void     render_grass_loop_debug_abort(void);   /* (the tests) THE PATCH's fallback forced: the ring refuses the next splice, the fine moments bake whole */
bool     render_grass_cache_on(void);
int      render_grass_cache_ms(void);
void     render_grass_cache_prof(int64_t *us, int *n);
void     render_set_veg_step(int s);   /* the grass in row pairs (2): about half the spans, 2 px steps on its edges; 1 = row by row */
int      render_veg_step(void);
void     render_veg_bench(const tank_t *t, uint16_t *fb, int bed, int64_t (*clock_us)(void), int64_t *full_us, int64_t dry_us[3], int *spans);   /* director `vegbench`: full, no stores, no stores + no vignette, no span_final at all */
void     render_touch_all(void);
unsigned render_restore_touched(uint16_t *fb, const uint32_t *touched);
unsigned render_scene_epoch(void);
void   render_set_pumpkin_hidden(bool hidden);   /* the frame-rate A/B (director `pumpkin off`): not drawn, no light */
/* the bubble volcano (0.3.9, SD_ITEM_VOLCANO) */
void   render_set_volcano_style(int style);   /* 0 stacked boulders (the tank's), 1 smooth rock (the sim's look, PT_VOLCANO_SMOOTH) */
int    render_volcano_px(int *glow);   /* its pixels; *glow = the lava's, redrawn every frame */
bool   render_pumpkin_hidden(void);
void   render_set_decor_scratch(void *buf);
/* the sand dollar toast: dollars awarded during play (progression_sd_take_award)
 * show as a small pill top centre of the live tank, "+N" beside the coin,
 * for a few seconds; amounts that land while it is up add on. Call every
 * frame the live tank is showing (never over a page). */
void render_sd_toast(const tank_t *t, uint16_t *fb, int stride);

/* Reset confirm (2026-09-11): a modal panel over the live tank - "RESET
 * TANK?", what it costs, a NO and a YES button, and a bar draining toward
 * the timeout (frac 1 -> 0). The first text the renderer draws (a 5x7
 * pixel font, upper case). Drawn last, over the card / milestones page.
 * render_confirm_hit maps a tap in tank coordinates to a button (+1 YES,
 * -1 NO, 0 neither) so the device's touch port and the sim's mouse share
 * the geometry. */
#define RENDER_CONFIRM_X     56
#define RENDER_CONFIRM_Y     66                   /* (0.4.2: 20 px taller for a third line - what a reset KEEPS) */
#define RENDER_CONFIRM_W     336
#define RENDER_CONFIRM_H     236
#define RENDER_CONFIRM_BTN_W 132
#define RENDER_CONFIRM_BTN_H 56
#define RENDER_CONFIRM_BTN_Y (RENDER_CONFIRM_Y + 124)
#define RENDER_CONFIRM_NO_X  (RENDER_CONFIRM_X + 24)
#define RENDER_CONFIRM_YES_X (RENDER_CONFIRM_X + RENDER_CONFIRM_W - 24 - RENDER_CONFIRM_BTN_W)
void render_confirm_reset(uint16_t *fb, int stride, float frac);
int  render_confirm_hit(float x, float y);

/* THE RESET PROMPT'S PAGES (0.4.2, docs/release-0.4.2.md "ERASE EVERYTHING"): the chord (BOOT 5 s + a tap) opens a
 * CHOICE of two named buttons and a way out, never a reset at once.
 *   RESET_PG_CHOOSE  START OVER?  [RESET THIS TANK] [ERASE EVERYTHING] [BACK]
 *   RESET_PG_TANK    RESET THIS TANK?  NO / YES - render_confirm_reset, the reset as it always was
 *   RESET_PG_ERASE   ERASE EVERYTHING?  the ALSO FORGET WI-FI box (unticked)  BACK / NEXT
 *   RESET_PG_HOLD    ARE YOU SURE?  HOLD TO ERASE, held RESET_HOLD_S with a bar filling; let go and it empties.  NO
 * Every page's difference is in its WORDS and where things are, never a color alone. A tap never erases: the only way
 * to RESET_ANS_ERASE is render_reset_hold's full hold on the last page. NO / BACK is one tap on every page.
 * The state is here (one page, the box, the hold); the port (touch_port_ft3168.c, the sim's twin in main.c) owns the
 * clock: the give-up timeout, and which presses count (one begun before the page came never does).
 * All in tank coordinates, as render_confirm_hit. */
enum { RESET_PG_NONE = 0, RESET_PG_CHOOSE, RESET_PG_TANK, RESET_PG_ERASE, RESET_PG_HOLD,
       RESET_PG_REFUSED,
       RESET_PG_ERASING };                        /* ERASING / DO NOT UNPLUG / THE TANK WILL RESTART: no button, no timeout - shown before the erase begins */                        /* COULD NOT ERASE / NOTHING WAS CHANGED, OK: the board refused the erase's first write */
enum { RESET_BTN_NONE = 0, RESET_BTN_BACK, RESET_BTN_TANK, RESET_BTN_ERASE, RESET_BTN_YES, RESET_BTN_WIFI, RESET_BTN_NEXT, RESET_BTN_HOLD };
enum { RESET_ANS_NO = -1, RESET_ANS_NONE = 0, RESET_ANS_TANK = 1, RESET_ANS_ERASE = 2, RESET_ANS_ERASE_WIFI = 3,
       RESET_EV_PAGE = 4 };                       /* (a tap that turned the page or ticked the box: the port restarts its timeout) */
#define RESET_HOLD_S 3.0f
#define RESET_HOLD_STEP_S 0.1f                    /* the most one poll adds to the hold */
#define RESET_PANEL_X 56                          /* the three tall pages' panel (the TANK page keeps RENDER_CONFIRM_*) */
#define RESET_PANEL_Y 40
#define RESET_PANEL_W 336
#define RESET_PANEL_H 288
void  render_reset_open(int page);                /* RESET_PG_CHOOSE from the chord; any page for the director and the pictures */
void  render_reset_close(void);
int   render_reset_page(void);
bool  render_reset_wifi(void);                    /* the box as it stands (it stays readable after the prompt closes) */
void  render_reset_set_wifi(bool on);             /* (the director, the pictures) */
float render_reset_hold_frac(void);               /* the hold so far, 0..1 */
void  render_reset_set_hold(float frac);          /* (the pictures) */
bool  render_reset_holding(void);               /* a finger is on HOLD TO ERASE now (the port's timeout waits for it) */
int   render_reset_hit(float x, float y);         /* RESET_BTN_* under a point on the page that is up */
/* a tap: pressed at (px, py), lifted at (lx, ly). The same button under both, or nothing. RESET_ANS_NO (the prompt
   closed, everything kept), RESET_ANS_TANK (YES: the caller resets this tank), RESET_EV_PAGE, or RESET_ANS_NONE */
int   render_reset_tap(float px, float py, float lx, float ly);
/* every poll the prompt is up: the finger (down = on the glass in a press that counts), where it pressed, where it is,
   and dt_s, the time since the last poll. Only on RESET_PG_HOLD, only while press and finger are both on HOLD TO ERASE:
   each such poll adds dt_s (RESET_HOLD_STEP_S at most - a stalled task and one stale reading never make a hold); at
   RESET_HOLD_S it answers RESET_ANS_ERASE or RESET_ANS_ERASE_WIFI and closes. Off the button, or lifted: the bar empties,
   and that press is done (sliding back on does not go on) */
int   render_reset_hold(bool down, float px, float py, float x, float y, float dt_s);
void  render_reset_prompt(uint16_t *fb, int stride, float frac);   /* the page that is up (frac: the timeout's bar, 1 -> 0) */
/* THE ERASE, ANNOUNCED: the erase blocks the board for a while and ends in a restart, so the glass says so first. The
   platform, on the hold's answer, calls render_reset_erasing(answer) (the prompt stays up, on RESET_PG_ERASING) and does
   NOT erase; after each frame has been drawn and handed to the panel it asks render_reset_erase_ready(): RESET_ANS_NONE
   until the page has been drawn RESET_ERASING_FRAMES times, then the answer, once - then the platform waits for the
   panel to take the frame and erases. While render_reset_erase_armed() the platform closes nothing and starts no sleep,
   power-off or restart of its own: render_reset_open / _close disarm (no arm outlives its page).
   The page with no erase armed (staged by name) is only a picture: a tap closes it, and the port's give-up applies */
#define RESET_ERASING_FRAMES 2
void  render_reset_erasing(int ans);
int   render_reset_erase_ready(void);
bool  render_reset_erase_armed(void);
bool  render_reset_bake_held(void);               /* the prompt's page is kept as a picture just now (THE PROMPT BAKED, render.c) */
bool  render_reset_text_fits(void);               /* every line of every page inside its panel or button (the selftest) */

/* Settings page (2026-09-15; the brightness row left the milestones page
 * for it): BRIGHTNESS, a slider since 0.3.9 (a keeper sensitive to light
 * found 30 %, the lowest of the three buttons it had, too bright): a knob
 * carrying the percent, 5 .. 100 % in steps of 5, dragged along the row or
 * tapped where wanted; VOLUME the same slider since 0.4 (Strato: "turn the
 * volume setting into a slider like the brightness"), OFF (0) .. 100 % in
 * steps of 10, its knob an outline reading OFF at the left end - then LIGHTS OUT, one row since
 * 0.3.2 (Strato: "takes up too much real estate ... condense it and give a
 * few options"): a value between two arrows, DOUBLE-TAP (MANUAL, the default:
 * the keeper's double-tap on the glass) and then AUTO after 5 SEC .. 30 MIN
 * still (LIGHT_IDLE_CHOICES); a tap on the row's left half steps back, on its
 * right half forward. Under it AUTO FEED ON / OFF (tank_t.autofeed_off) and,
 * on a tank that turns its picture over by itself, ROTATION: one button, an
 * open padlock in a turning arrow while the picture follows the tank, a shut
 * one once the keeper locks the way up (tank_orient_lock). A CLOSE button
 * bottom right (back to the milestones page, 2026-09-16 - the platform's job).
 * The platform feeds render_settings_touch EVERY FRAME while the page is up
 * (x, y, finger down), as it feeds setup_touch: it classifies the taps,
 * applies the tank's own settings to the tank itself (and marks the save),
 * and returns what happened: SET_TAP_BRIGHT with *value = the percent
 * (the finger lifted off the slider: keep it), SET_TAP_BRIGHT_LIVE (the
 * knob moved under a finger still down: show it, do not save it yet),
 * SET_TAP_VOLUME with *value = the percent (0 = OFF; the finger lifted off
 * the slider: keep it) and SET_TAP_VOLUME_LIVE (as BRIGHT_LIVE: let the
 * keeper hear it, do not save it yet) (the platform's to apply),
 * SET_TAP_LIGHT (the row stepped to or from MANUAL: *value 1 = AUTO),
 * SET_TAP_IDLE (AUTO's time stepped: *value = the seconds now set),
 * SET_TAP_FEED (*value 1 = AUTO FEED on), SET_TAP_ROTATE (*value 1 =
 * locked), SET_TAP_GFX (*value = the GRAPHICS level now set), SET_TAP_CLOSE,
 * or nothing.
 * A worn tank (TANK_WORN, the watch) has SCREEN NORMAL / TURNED
 * (tank_screen_*) where the others have ROTATION, applied here like the light's.
 * render_settings_tap is the bare hit test (tests); render_settings_box says
 * where a foot button or a tile is drawn (tests and tools ask it, never a
 * literal pixel). */
enum { SET_TAP_NONE = 0, SET_TAP_CLOSE = 1, SET_TAP_BRIGHT = 2, SET_TAP_VOLUME = 3, SET_TAP_LIGHT = 4, SET_TAP_IDLE = 5,
       SET_TAP_UPDATES = 6,     /* the UPDATES button, bottom left (2026-09-30): the platform opens the updates page (update.h) */
       SET_TAP_SCREEN = 7,      /* a worn tank's SCREEN row (2026-10-02): *value 1 = TURNED, already applied and marked
                                   for the save - the platform only logs it (the picture turns on the next frame) */
       SET_TAP_FEED = 8,        /* AUTO FEED (0.3.2): *value 1 = ON; applied and marked for the save */
       SET_TAP_ROTATE = 9,      /* ROTATION (0.3.2): *value 1 = locked; applied and marked for the save */
       SET_TAP_BRIGHT_LIVE = 10,   /* the BRIGHTNESS slider under a finger (0.3.9): *value = the percent, the panel's to
                                      show now; SET_TAP_BRIGHT follows with the last one when the finger lifts */
       SET_TAP_TANKS = 11,         /* MY TANKS (0.3.9.3): the platform opens the tank switcher (docs/tank-switcher.md) */
       SET_TAP_LINK = 12,          /* LINK (0.4, the tank link): the platform closes settings and opens link mode on the live tank */
       SET_TAP_VOLUME_LIVE = 13,   /* the VOLUME slider under a finger (0.4): *value = the percent, the mixer's to use now;
                                      SET_TAP_VOLUME follows with the last one when the finger lifts */
       SET_TAP_VIG = 15,           /* the GRAPHICS page's VIGNETTE row (0.4.2): *value 1 = ON, 0 = OFF, the setting already changed
                                      (render_vignette) - the platform saves it as a DEVICE setting ("vigoff" = !value) */
       SET_TAP_GFX_PAGE = 16,      /* the GRAPHICS tile opened its page (*value 1) or the page's BACK came back (*value 0): the
                                      settings page is still the one up - nothing for the platform to do but say so */
       SET_TAP_GFX = 14 };         /* the GRAPHICS page's QUALITY row (0.4.2; the tile itself until the page): the setting already changed (render_gfx_level;
                                      the drawing takes it up at the next frame); *value = the level now - the platform saves it as a DEVICE setting */
/* the settings page's foot buttons and tiles, where they are drawn (PAGE coordinates) */
enum { SET_BOX_UPDATES = 0, SET_BOX_LINK, SET_BOX_CLOSE,   /* the foot, left to right */
       SET_BOX_TILE,                                       /* ROTATION (the watch: SCREEN) */
       SET_BOX_GFX, SET_BOX_N };                           /* GRAPHICS */
void render_settings_box(int which, int *x, int *y, int *w, int *h);
/* GRAPHICS (0.4.2): a DEVICE setting, beside the brightness and the volume - not a tank's (the boards keep it in NVS
 * "gfx", the sim in its device file). Stored 1 LOW, 2 MEDIUM, 3 HIGH; HIGH by default, for a device that never saved one,
 * and for anything else. Drawing only (render.c GRAPHICS): HIGH as ever; MEDIUM about half the loop's moments by day and by
 * night THE NIGHT PICTURE (a small loop of its own memory, the day's kept whole beside it: no rebake at a light switch), only
 * the bubble column's bubbles; LOW no sway, one still band day and night, only the column's bubbles. The setting is a byte;
 * the render takes it up at its next frame (which the boards run under their render lock) */
#define RENDER_GFX_LOW    1
#define RENDER_GFX_MEDIUM 2
#define RENDER_GFX_HIGH   3
int  render_gfx_level(void);          /* RENDER_GFX_LOW / _MEDIUM / _HIGH: what the platform saves */
void render_set_gfx_level(int level); /* a saved value (anything outside 1..3: HIGH) */
bool render_gfx_low(void);
const char *render_gfx_name(void);    /* "HIGH", "MEDIUM", "LOW" */
void render_settings(const tank_t *t, uint16_t *fb, int stride, int bright_pct, int volume);   /* volume: the percent, 0 = OFF */
/* THE GRAPHICS PAGE (0.4.2): the settings page's other face - its GRAPHICS tile opens it, its BACK returns (both inside
 * render_settings / render_settings_touch: the platform's settings page is up all the while). QUALITY LOW | MEDIUM | HIGH
 * (SET_TAP_GFX) and VIGNETTE ON | OFF (SET_TAP_VIG). render_settings_home: the settings page itself again - the platform
 * calls it whenever it opens the settings page. render_gfx_settle: the platform calls it each frame the settings page is
 * up, BEFORE render_settings, with the buffer the page then covers (t NULL in a jelly tank): a change made on the page is
 * baked in behind it (tank frames and loop moments drawn into fb, hidden) while the page says ONE MOMENT; idle, one test. */
enum { GFXP_BOX_BACK = 0, GFXP_BOX_Q_LOW, GFXP_BOX_Q_MEDIUM, GFXP_BOX_Q_HIGH, GFXP_BOX_V_ON, GFXP_BOX_V_OFF, GFXP_BOX_N };
void render_gfx_page_box(int which, int *x, int *y, int *w, int *h);   /* where each is drawn (PAGE coordinates) */
int  render_gfx_page_tap(float x, float y, int *value);                 /* the bare hit test (FRAME px): SET_TAP_GFX / _VIG / _GFX_PAGE (BACK) / _NONE */
bool render_gfx_page(void);                                             /* the GRAPHICS face is the one up */
void render_settings_home(void);
/* BACK FROM UPDATES (0.4.2, Strato): the UPDATES page's BACK stands in the middle of the settings foot's band, over LINK
 * (and the near ends of UPDATES and CLOSE). The platform calls this as that BACK brings the settings page back; for
 * SET_UPD_BACK_S from then a NEW press that lands where that BACK was (its whole reach) is spent - no tap, nothing
 * restarted, nothing queued, nothing drawn. Every other control on the page answers as ever, and after the window the
 * whole page does. THIS ONE CASE ONLY, by Strato's word: keepers move fast through the other pages and no other BACK
 * (the GRAPHICS page's is the same box) gets a wait; UPDATES is seldom opened. */
#define SET_UPD_BACK_S 0.5
void render_settings_from_updates(const tank_t *t);
void render_settings_from_updates_off(void);                           /* (a test's control: the guard not set) */
void render_gfx_page_show(bool on);                                     /* (the director, the tests) that face up, or the settings page's own, with no tap */
bool render_gfx_busy(void);                                             /* a bake is owed: true from a change until that bake is done, behind the page or under the tank */
bool render_gfx_waiting(void);                                          /* ... and the page says ONE MOMENT (not after it gave up) */
bool render_gfx_settle(tank_t *t, uint16_t *fb, int stride);            /* (t: the platform's own tank - its light is put down and back for the day's loop baked in the dark) */
void render_gfx_owe(const tank_t *t);                                   /* a change made with the page up owes its bake: ONE MOMENT from the next page frame (the page's tap; the director) */
void render_debug_gfx_idle(void);                                       /* (the tests: nothing owed) */
void render_debug_gl_no_room(bool on);                                  /* (the tests: the grass loop's arena reads full for every fine moment - each stays on its band) */
/* THE ADVANCED PAGE's pictures and hit tests (0.3.9.2; the page itself is
 * common/advanced.c, hidden behind a BOOT chord on the settings page). The
 * hit tests take FRAME px, like render_settings_tap.
 * render_advanced: TOUCH (CALIBRATE) and BATTERY (the size picked between
 * two arrows; a sign on it when it charges faster than the board's own;
 * SET <size> while the pick is not the size in use), BACK.
 * render_battery_warning: the sign, WARNING, the risk in plain words, the
 * box to tick ("MY BATTERY IS <size> OR BIGGER"), CANCEL, CONTINUE (an
 * outline with dim words until the box is ticked: brightness and fill, not
 * hue). The touch pages: the first one (START; RESET TOUCH while the
 * keeper's own map is in use; msg = a line of news, or NULL), a cross (i of
 * n, its centre in FRAME px - as the panel reports), and the check that
 * keeps the new map: one more cross before secs_left runs out. */
enum { ADV_HIT_NONE = 0, ADV_HIT_BACK, ADV_HIT_CALIBRATE, ADV_HIT_PREV, ADV_HIT_NEXT, ADV_HIT_SET,
       ADV_HIT_BOX, ADV_HIT_CANCEL, ADV_HIT_CONTINUE, ADV_HIT_START, ADV_HIT_RESET, ADV_HIT_SKIP, ADV_HIT_GO, ADV_HIT_OK };
void render_advanced(uint16_t *fb, int stride, int pick_mah, int pick_ma, int set_mah, bool stock, bool fast);
int  render_advanced_tap(float x, float y);
void render_battery_warning(uint16_t *fb, int stride, int cell_mah, int charge_ma, bool checked);
int  render_battery_warning_tap(float x, float y);
void render_touch_intro(uint16_t *fb, int stride, bool own_in_use, const char *msg);
int  render_touch_intro_tap(float x, float y, bool own_in_use);
void render_touch_cross(uint16_t *fb, int stride, int i, int n, int tx, int ty);
void render_touch_check(uint16_t *fb, int stride, int tx, int ty, float secs_left, float secs_total);
/* TURN IT OVER (0.4.2): the tank, an arrow, the tank upside down (the sand on top: shape, not color), what to do, SKIP; the
   watch adds GO (it can't feel the turn: the page is drawn the other way up, advanced.h) */
void render_touch_turn(uint16_t *fb, int stride);
int  render_touch_turn_tap(float x, float y);   /* ADV_HIT_SKIP, ADV_HIT_GO (the watch), ADV_HIT_NONE */
/* DONE (0.4.2): a big check mark (its shape says it), DONE, TOUCH CALIBRATION SAVED., OK */
void render_touch_done(uint16_t *fb, int stride);
int  render_touch_done_tap(float x, float y);   /* ADV_HIT_OK, ADV_HIT_NONE */
int  render_settings_tap(float x, float y, int *value);
int  render_settings_touch(tank_t *t, float x, float y, bool down, int *value);
int  render_settings_cut(int *value);           /* the port: a press cut (the page not fed) - forgotten; a slider in hand: its last
                                                   shown step returned as the setting to save (SET_TAP_BRIGHT / _VOLUME), else NONE */
void render_settings_cancel(void);              /* forget the press in hand (the BOOT chord took that finger to the ADVANCED page) */

/* UI primitives (2026-09-13) for panels built outside this file (the first-
 * run setup in common/setup.c): the confirm prompt's pixel font, flat rects
 * and buttons, and a fish drawn on its own for a preview. All ignore the
 * night dim, like the card, and draw AFTER render_tank (nothing re-vignettes
 * them). Text is upper case + digits + a little punctuation; `scale` is the
 * pixel size of one font dot (2 = caption, 3 = button). */
int  render_text_w(const char *s, int scale);
void render_text(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const char *s);
void render_rect(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb);
void render_rect_blend(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb, int alpha);   /* alpha 0..255 */
void render_rect_edge(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb);
void render_ring(uint16_t *fb, int stride, float cx, float cy, float r, uint32_t rgb);   /* the card's selection ring */
void render_button(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t fill, uint32_t edge, const char *label, int scale);
/* one 5x7 glyph from any table (rows: 5 bits, high bit left) at scale - the case-sensitive font in update.c draws with it */
void render_glyph(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const uint8_t *rows);
/* an adult fish facing right at (x,y), body length ~42 x size px, tail
 * swimming on `clock` - the setup's live preview of a colour choice. `form`
 * lends its genes (body shape, fins, pattern; NULL = CLASSIC). A page's fish
 * fits the room a CLASSIC fish takes, times `room` (0.4): a bigger form is
 * drawn smaller, centred the same, so no page's layout changes; a classic is
 * exact. */
#define FISH_ROOM_ROW  1.0f            /* a classic's own box: the milestones rows, badges, small glyphs */
#define FISH_ROOM_FAM  1.3f            /* the birth flow's family portrait */
#define FISH_ROOM_CARD 1.5f            /* a modal: the fish card, the notices */
void render_fish_preview(uint16_t *fb, int stride, float x, float y, float size,
                         uint32_t body, uint32_t fin, uint32_t accent, const fish_t *form, float room, double clock);
/* the same, but AS THE FISH IS: its own stage (a fry shows no markings yet,
 * an elder its long tail), calm and fed - the birth flow's portrait */
void render_fish_portrait(uint16_t *fb, int stride, float x, float y, float size, const fish_t *who, float room, double clock);
void render_hearts(uint16_t *fb, int stride, int x, int y, int lit, int scale);
void render_stamp(uint16_t *fb, int stride, int cx, int cy, float deg, uint32_t rgb, const char *l1, const char *l2, int scale);   /* a turned stamp (0.4.2), page coords */   /* five hearts, `lit` filled, the rest hollow (11 x 10 px at scale 1) */
/* the portrait at its true size, no fit (the sim's --genes boards compare the forms) */
void render_fish_sample(uint16_t *fb, int stride, float x, float y, float size, const fish_t *who, double clock);

/* ---- the pages' layouts (2026-10-01). These lived in render.c; they are here
 * so the sim's selftests tap where the layout says on every board instead of
 * carrying the rectangle's pixels. PAGE coordinates unless a line says the
 * frame's; render.c has the words that go with each page. ---- */
/* the snail's and the shrimp school's cards: centered on the glass (the frame's coordinates) */
#define CRIT_NAME_ROW_H 44     /* a named critter card's foot: RENAME (0.4.2) */
#define SNAIL_CARD_W 336
#define SNAIL_CARD_H (176 + CRIT_NAME_ROW_H)
#define SHRIMP_CARD_W 336
#define SHRIMP_CARD_H (190 + CRIT_NAME_ROW_H)   /* (+ the SHELVE row, 0.4.2) */
#define URCHIN_CARD_W 336
#define URCHIN_CARD_H (194 + CRIT_NAME_ROW_H)
/* a visitor's card: the Pocket Tank Passport (tank link, 2026-10-05) */
#define PASSPORT_CARD_W 336
#define PASSPORT_CARD_H 252
/* its buttons along the foot: DEPORT | COLLECT (card coordinates from its top left) */
#define PASSPORT_BTN_Y  214
#define PASSPORT_BTN_H  30
#define PASSPORT_BTN_W  150
enum { PASSPORT_HIT_NONE = 0, PASSPORT_HIT_DEPORT, PASSPORT_HIT_COLLECT, PASSPORT_HIT_CARD };
int render_passport_hit(float x, float y);   /* a tap while a visitor's passport is up */
/* a GIFT offered to us (tank link phase 6, gift.h): its card over the live tank until the keeper
   answers - A GIFT FOR YOUR TANK, the fish's photo and fields in the passport's dress, ACCEPT /
   NO THANKS, the time left as a bar. Real size on every glass (Strato 2026-10-20). Nothing drawn
   when no gift is asked. */
#define GIFT_CARD_W 336
#define GIFT_CARD_H 252
#define GIFT_BTN_Y  206                     /* its buttons (card coordinates from its top left) */
#define GIFT_BTN_H  34
#define GIFT_BTN_W  140
enum { GIFT_HIT_NONE = 0, GIFT_HIT_ACCEPT, GIFT_HIT_NO, GIFT_HIT_CARD };
void render_gift(const tank_t *t, uint16_t *fb, int stride);
void render_gift_allow(bool ok);
bool render_gift_back_window(const tank_t *t, double press);   /* the card just back from under a page a tap closed: that tap's double (W) */             /* main, each frame: false under the tank's own setup, the reset prompt, ADVANCED (it waits) */
bool render_gift_up(const tank_t *t);        /* a card to show now: it has the glass first, over any page */
bool render_card_over_tank(const tank_t *t); /* the GIVE modal or a gift card up: the live tank's own holds and swipes stop (the evening sweep) */
int  render_gift_take_cue(void);             /* SND_* the frame it is first seen, else -1 */
int  render_gift_hit(float x, float y);
/* the VISITOR COLLECTION page (2026-10-06): a grid of the collected, a tap
 * on one its passport with its history here; CLOSE goes back to the
 * milestones page. render_collection_tap: COLL_TAP_* */
enum { COLL_TAP_NONE = 0, COLL_TAP_KEPT, COLL_TAP_CLOSE };
void render_collection(const tank_t *t, uint16_t *fb, int stride);
int  render_collection_tap(const tank_t *t, float x, float y);
void render_collection_leave(void);
/* its pages (0.4): the page up (0-based; the leave goes back to the first), one set (clamped), an arrow's middle (frame
   coordinates; false = one page, no arrows) */
int  render_collection_page(void);
void render_collection_set_page(int page);
bool render_collection_arrow(bool right, int *x, int *y);
/* THE ICON FOOT (0.4, Strato 2026-11-01): UPGRADES (the sand dollar over the balance), SETTINGS (a cog; the bowl's in
 * the cap), MY TANKS (two tanks on a shelf), VISITORS (a plane), CLOSE (an X) - squares MSP_FOOT_SQ on a side */
enum { MSF_UPG, MSF_SET, MSF_TANKS, MSF_VIS, MSF_CLOSE, MSF_N };
void render_ms_foot_box(int which, int *x, int *y);   /* a foot square's top left, page coordinates */
#define CRAB_CARD_W 336          /* the strawberry crab's (0.3.9): the urchin's size */
#define CRAB_CARD_H (194 + CRIT_NAME_ROW_H)
bool render_crab_collect_hit(const tank_t *t, float x, float y);   /* the crab card's COLLECT (a sand dollar held up, 0.3.9.4) */
#define SEAHORSE_CARD_W 336
#define SEAHORSE_CARD_H (188 + CRIT_NAME_ROW_H)
/* the milestones page (the bowl has its own rows and buttons: render.c) */
#ifdef TANK_ROUND
#define MSP_ROW_Y0    56
#define MSP_ROW_H     34
#define MSP_TANK_Y    264
#define MSP_ROW_MID   17                /* in a row: the portrait's centre ... */
#define MSP_ROW_STRIP 27                /* ... the growth strip / the fry's ticks ... */
#define MSP_ROW_BADGE 1                 /* ... the badges' top ... */
#define MSP_ROW_BAR   33                /* ... the bar under a gate still owed */
#else
#define MSP_ROW_Y0    4
#define MSP_ROW_H     40
#define MSP_TANK_Y    254
#define MSP_ROW_MID   20
#define MSP_ROW_STRIP 30
#define MSP_ROW_BADGE 4
#define MSP_ROW_BAR   38
#endif
#define MSP_BADGE_X0  (PAGE_NARROW || PAGE_BOWL ? 172 : 176)   /* (the watch and the bowl: a 38 px pitch, the tank row's arrow inside the glass) */
#define MSP_BADGE_DX  (PAGE_NARROW || PAGE_BOWL ? 38 : 40)
#define MSP_ICON      32
#define MSP_FISH_X    (PAGE_NARROW ? 56 : 52)     /* a row's portrait (the watch: an elder's tail inside the glass) */
#define MSP_CLOSE_W   92
#define MSP_CLOSE_H   30
#define MSP_SET_W     116
#ifdef TANK_ROUND                       /* (page coordinates: the cap above the page is y < 0) */
#define MSP_CLOSE_X   242               /* UPGRADES 114..230 and CLOSE 242..334: the pair centred */
#define MSP_CLOSE_Y   318
#define MSP_SET_X     ((PAGE_W - MSP_SET_W) / 2)
#define MSP_SET_Y     (-PAGE_Y + 18)    /* SETTINGS: top dead centre */
#define SHP_CLOSE_X   259               /* the shop: HOW TO EARN 97..247 beside CLOSE */
#define SET_CLOSE_X   296               /* settings: UPDATES and CLOSE drawn in from the glass */
#else
#define MSP_CLOSE_X   324               /* the CLOSE button, bottom right, inside the bezel curve; clear of the brightness row's number */
#define MSP_CLOSE_Y   312
#define MSP_SET_X     178               /* the SETTINGS button, between UPGRADES and CLOSE (Strato, 2026-10-24: the two swapped places; it was bottom left) */
#define MSP_SET_Y     MSP_CLOSE_Y
#define SHP_CLOSE_X   MSP_CLOSE_X
#define SET_CLOSE_X   MSP_CLOSE_X
#endif
/* THE KEEPER PAGE'S FOOT (0.3.9.1; Strato 2026-10-25 on the square: the row pinned to the bottom of the frame on every
 * board, as low as taps stay reliable - the square and the watch had a band of glass under it). Its last row of pixels
 * sits MSP_FOOT_GAP above the glass's bottom where the bottom is flat (the watch's settings foot has stood 13 px off its
 * glass since 10-02), and where a curve takes the corners (the watch's 100 px arcs, the bowl's circle) as low as each
 * button's lower corners stay MSP_FOOT_IN inside the glass - the watch's three also close in on the middle. Frame y =
 * page y + PAGE_Y. --selftest-shop checks every board: inside the glass, and one px lower would not be. The shop and
 * the settings keep their own feet (MSP_CLOSE_Y, SET_FOOT_Y). */
#define MSP_FOOT_GAP  12
#define MSP_FOOT_IN   6
#ifdef TANK_ROUND
#define MSP_FOOT_Y    353               /* the circle: UPGRADES' and CLOSE's outer corners (frame x 123, 342) at 6 px in; 35 px off the bottom at the middle */
#elif defined(TANK_WATCH)
#define MSP_FOOT_Y    371               /* the arcs: the outer corners (frame x 33, 376) at 6 px in; 35 px off the bottom at the middle */
#elif defined(TANK_SQUARE)
#define MSP_FOOT_Y    382               /* flat: the last row at frame y 467, 12 above the glass's 480 (the 40 px corners end before the buttons) */
#else
#define MSP_FOOT_Y    326               /* flat: the last row at y 355, 12 above the glass's 368 (was 312) */
#endif
/* the foot's tap strip reaches HALF WAY up the gap to the TANK row (Strato 2026-10-25 on the square: a quick tap just
 * over the buttons, the way a child taps, opened a tank milestone): from the row's last drawn line (the balance under
 * the coin, MSP_TANK_Y + 48; its page pips since 0.4) to the buttons' top, the lower half is the buttons' */
#define MSP_TANK_BOT     (MSP_TANK_Y + 42)   /* (its page pips; the balance under the coin, +48, went to the icon foot in 0.4) */
#define MSP_FOOT_SQ      44                 /* THE ICON FOOT (0.4): squares a little taller than the text buttons, their */
#define MSP_FOOT_TOP     (MSP_FOOT_Y + MSP_CLOSE_H - MSP_FOOT_SQ)   /* bottoms where the text foot's were (the same corners on the glass) */
#define MSP_FOOT_HIT_Y   ((MSP_TANK_BOT + MSP_FOOT_TOP) / 2)
#if TANK_WORN
#define MSP_FOOT_SLOP    5              /* the strip's split between two buttons: half their gap */
#define MSP_FOOT_UPG_X   52             /* the watch's three, 10 px apart and centred: frame x 33..149 | 159..275 | 285..377 */
#define MSP_FOOT_SET_X   178
#define MSP_FOOT_CLOSE_X 304
#else
#define MSP_FOOT_SLOP    8
#define MSP_FOOT_UPG_X   MSP_UPG_X
#define MSP_FOOT_SET_X   MSP_SET_X
#define MSP_FOOT_CLOSE_X MSP_CLOSE_X
#endif
#ifdef TANK_ROUND
#define MSP_UPG_X     114
#else
#define MSP_UPG_X     32                /* the UPGRADES button, bottom left where the brightness row was: the shop (centred between the others until 2026-10-24) */
#endif
#define MSP_UPG_W     116
#define MSP_MODAL_X   56
#define MSP_MODAL_Y   100
#define MSP_MODAL_W   336
#define MSP_MODAL_H   156
#define MSP_PER_ROW   6
#define MSP_TANK_PER_ROW 7          /* the TANK row: seven badges a page (0.4.1) - slid left into the room its sand dollar left */
#define MSP_TSLIDE    (PAGE_NARROW || PAGE_BOWL ? 48 : 64)   /* (the watch, the bowl: 48 - TANK stays ~25 px inside the glass) */
                                     /* (0.4 moved the balance to the shop; Strato 11-05: "slide TANK to the left as well as all the
                                         tank milestones") - the TANK label, its population strip and its badges, all by this */
#define MSP_TBADGE_X0 (MSP_BADGE_X0 - MSP_TSLIDE)
#define MSP_TNAME_X   (92 - MSP_TSLIDE)
#define MSP_TPG_X     (PAGE_BOWL ? 396 : PAGE_NARROW ? 398 : 412)   /* the arrow's column: right of the sixth badge's slop, out to the glass
                                                                    (the bowl: its "new" ring and the page pips inside the circle) */
#define MSP_TPG_HIT_X (MSP_TBADGE_X0 + MSP_TANK_PER_ROW * MSP_BADGE_DX)   /* the arrow's TAP: from the seventh badge's box out to the glass
                                     (0.4.2, Strato's 1.8 11-12: "the arrow is not registering my taps" - a panel's report stops short of
                                     the glass, the 1.8's near x 420 and a keeper's touch map can pull it in further, so a finger ON the
                                     arrow at 416..432 read left of 412 and opened nothing; the 20 px between the last badge and the
                                     arrow belong to the arrow now) */
#define MSP_ARROW_W   40             /* the arrow buttons, inset at the modal's top corners */
#define MSP_ARROW_H   32
#define MSP_ARROW_IN  10
#define MSP_ARROW_HIT 100            /* the hit box: the corner's whole width in from each side, 12 above, 24 below
                                        (a miss closes the modal, so the box is wide) */
/* a gate's modal is taller (two sentence lines, progress, the HOW? button)
   so it sits higher than the badge modal, clear of the CLOSE button */
#define MSP_FRY_MODAL_Y 60
/* a fish's card (0.4.2): the name, the stage, MORE (its story, family and nature: setup_begin_about), the SELL line,
 * RENAME / SELL (| GIVE) at the foot. Its form left for MORE's FAMILY page */
#define MSP_CARD_H       250
#define MSP_CARD_SUB_Y   160
#define MSP_CARD_ICON    44           /* the card's icon buttons (11-08), 24 apart */
#define MSP_CARD_ICON_GAP 24
#define MSP_HOW_W 100                 /* CLOSE-sized (Strato hit the 76 x 26 one a third of the time) */
#define MSP_HOW_H 32
/* its hit box: wide and deep. Fingers on this panel land low and wide of
   where they feel, and a miss here costs the modal (any other tap closes
   it), so the box runs 36 px past each side, 12 above and 28 below - the
   whole foot of the panel, down to its edge. */
#define MSP_HOW_SLOP_X 36
#define MSP_HOW_SLOP_UP 12
#define MSP_HOW_SLOP_DN 28
/* the shop */
#ifdef TANK_ROUND                       /* the bowl: the header and the foot drawn in from the glass */
#define SHP_COIN_X    62
#define SHP_HEAD_X    140               /* "SAND DOLLARS" and the balance */
#define SHP_EARN_X    97
#else
#define SHP_COIN_X    32
#define SHP_HEAD_X    112
#define SHP_EARN_X    32
#endif
#define SHP_COIN_Y    10
#if defined(TANK_ROUND) || defined(TANK_WATCH) || defined(TANK_SQUARE)
#define SHP_ROW_Y0    98
#define SHP_ROW_DY    56
#else                                   /* the 1.8 (0.4.1, Strato 11-04): four rows over the icon foot - the rows start 8 higher (the rule
                                           6 px under the coin) and close up 2 each, so the fourth row's button ends at 284, 28 above the foot */
#define SHP_ROW_Y0    90
#define SHP_ROW_DY    54
#endif
#define SHP_ROW_ICON  32
#define SHP_BTN_X     300
#define SHP_BTN_W     116
#define SHP_BTN_H     32
#define SHP_EARN_W    150
#define SHP_MODAL_X   48             /* wider than the milestones modal (56 / 336): an item's second line runs to 28 chars = 334 px */
#define SHP_MODAL_W   352
#define SHP_MODAL_Y   48
#define SHP_MODAL_H   244
#define SHP_EARN_MODAL_Y 40
/* the caption under the rows sits SHP_BTN_H + 12 under the last row (the third row, the castle, 2026-09-16: it used to be fixed at 224 / 244 and the castle's row ran into it) */
#define SHP_EARN_MODAL_H 272        /* eight lines since the crab's find (0.3.9; seven since the seahorse's snack) */
#define SHP_TWO_GAP 16               /* MOVE and SELL side by side in the modal */
#define SHP_TWO_X0  (SHP_MODAL_X + (SHP_MODAL_W - 2 * MSP_HOW_W - SHP_TWO_GAP) / 2)
#define SHP_TWO_X1  (SHP_TWO_X0 + MSP_HOW_W + SHP_TWO_GAP)
#define SHP_PER_PAGE 4            /* (0.4.1, Strato 11-04: four a page over the icon foot, working-assets/shop-navigation-v1) */
#define SHP_FRY_BOX_W  100            /* the fry modal's three boxes */
#define SHP_FRY_BOX_H  84
#define SHP_FRY_BOX_GAP 10
#define SHP_FRY_BOX_X(k) (SHP_MODAL_X + (SHP_MODAL_W - 3 * SHP_FRY_BOX_W - 2 * SHP_FRY_BOX_GAP) / 2 + (k) * (SHP_FRY_BOX_W + SHP_FRY_BOX_GAP))
#define SHP_FRY_BOX_Y  (SHP_MODAL_Y + 44)
#define SHP_ARROW_W  36
#define SHP_ARROW_H  32
#define SHP_ARROW_Y  (SHP_COIN_Y + 16)
#define SHP_ARROW_X1 (PAGE_W - (PAGE_BOWL ? 66 : 28) - SHP_ARROW_W)   /* next (further in on the bowl) */
#define SHP_ARROW_X0 (SHP_ARROW_X1 - SHP_ARROW_W - 8)     /* previous */
/* the settings page */
#if TANK_WORN                        /* the watch (2026-10-02): the page uses the glass above and below the PAGE
                                        box (page y -67..435 is on the glass; the round corners take the ends of
                                        the first and last 100 px): the title above the box, the rows up by 44,
                                        the foot's pair centred under them */
#define SET_TITLE_Y   (-40)
#define SET_ROW1_Y    14
#define SET_ROW2_Y    64
#define SET_ROW3_Y    132            /* LIGHTS OUT */
#define SET_ROW4_Y    188            /* AUTO FEED */
#define SET_ROW5_Y    244            /* SCREEN: NORMAL / TURNED */
#define SET_NOTE5_Y   286            /* under it: who it is for */
#define SET_FOOT_Y    392            /* UPDATES and CLOSE */
#else
#define SET_TITLE_Y   14
#define SET_ROW1_Y    58             /* BRIGHTNESS: the slider */
#define SET_ROW2_Y    108            /* VOLUME */
#define SET_ROW3_Y    164            /* LIGHTS OUT (0.4.2: the three rows below VOLUME up 12, for the taller icon foot) */
#define SET_ROW4_Y    210            /* AUTO FEED */
#define SET_ROW5_Y    256            /* the tile row: ROTATION | GRAPHICS */
#define SET_FOOT_Y    MSP_CLOSE_Y    /* UPDATES and CLOSE: the milestones page's foot */
#endif
/* THE SETTINGS FOOT (0.4.2, Strato's settings-navigation-v2): UPDATES, LINK, CLOSE as the icon foot's 44 px squares,
 * 36 px sprites 4 px in, one group centred on the page with 24 px between neighbours - page x 134 / 202 / 270 on every
 * board. The 1.8, the bowl and the square: y 298..341 (on the bowl its lower corners stand ~51 px inside the circle);
 * the watch: lower, where its old foot stood, between the lower corners' arcs (they end at page x 119 and 329). The
 * version caption 56 px under the foot's top (the 1.8's y 354, the watch's 424, as before).
 * (SET_FOOT_Y, SET_UPD_X / _W and SET_CLOSE_X are the old text foot's places, kept for the ADVANCED page's) */
#define SET_NAV_SQ    MSP_FOOT_SQ
#define SET_NAV_GAP   24
#define SET_NAV_X(i)  ((PAGE_W - 3 * SET_NAV_SQ - 2 * SET_NAV_GAP) / 2 + (i) * (SET_NAV_SQ + SET_NAV_GAP))   /* 0 UPDATES, 1 LINK, 2 CLOSE */
#if TANK_WORN
#define SET_NAV_Y     368
#else
#define SET_NAV_Y     298
#endif
#define SET_VER_Y     (SET_NAV_Y + 56)
/* the tile row (0.4.2): two 40 px tiles across the label column and the segments' span, 12 apart - ROTATION (the
 * watch: SCREEN) | GRAPHICS - an icon at the left, an 8 px name over a size-2 value */
#define SET_TILE_X0   SET_LABEL_X
#define SET_TILE_GAP  12
#define SET_TILE_W    ((SET_SEG_X + SET_SPAN_W - SET_TILE_X0 - SET_TILE_GAP) / 2)
#define SET_TILE_X1   (SET_TILE_X0 + SET_TILE_W + SET_TILE_GAP)
#define SET_TILE_BOT  (SET_SEG_Y(SET_ROW5_Y) + SET_SEG_H)
/* the taps (render_settings_tap): the tile row's band runs from 12 above the tiles down to the foot's top (at most 12
 * under them: the watch's note stands there); the foot's from its drawn top - no slop above it: the page UPDATES
 * opens has FORGET NETWORK just over that line (update.h; its own reach stops at its bottom edge), and three quick
 * taps there must never open the page, arm SURE? and forget the Wi-Fi - to 14 under it. Across, a foot button
 * reaches half the gap to its neighbour and as far out past the group's ends; a tile half the gap to the other and
 * SET_TILE_OUT out to the side. --selftest-touch-port sweeps every point of each foot button against the page it
 * opens. */
#define SET_TILE_HIT_Y1  (SET_NAV_Y < SET_TILE_BOT + 12 ? SET_NAV_Y : SET_TILE_BOT + 12)
#define SET_NAV_HIT_Y0   SET_NAV_Y
#define SET_NAV_HIT_Y1   (SET_NAV_Y + SET_NAV_SQ + 14)   /* (14 under the buttons: fingers report low; the caption is no button) */
#define SET_TILE_OUT     10
#define SET_LABEL_X   32
#define SET_SEG_X     (PAGE_BOWL ? 172 : PAGE_NARROW ? 178 : 190)   /* first segment (the watch and the bowl: the third one inside the glass -
                                                                    the bowl's circle is narrowest at the BRIGHTNESS row) */
#define SET_SEG_W     76
#define SET_SEG_DX    (PAGE_NARROW || PAGE_BOWL ? 80 : 82)
#define SET_SEG_H     40
#define SET_SEG_Y(row) ((row) - 10)  /* the segment sits on the label's line */
/* LIGHTS OUT (0.3.2): one value between two arrow buttons, across the three
 * segments' span - the row's left half steps back, its right half forward */
#define SET_ARW_W     40
#define SET_SPAN_W    (2 * SET_SEG_DX + SET_SEG_W)
#define SET_SPAN_MID  (SET_SEG_X + SET_SPAN_W / 2)
/* BRIGHTNESS (0.3.9): a slider across the same span - a rail, lit up to a
 * knob that carries the percent; the knob's centre runs SET_BRT_X0 .. X1 for
 * 0 .. 100 % - one scale for every slider, so BRIGHTNESS and VOLUME at the
 * same percent stand one above the other (Strato, 0.4) - BRIGHTNESS from
 * SET_BRT_MIN (its knob stops a little in from the left end) in steps of
 * SET_BRT_STEP */
#define SET_BRT_MIN   5
#define SET_BRT_STEP  5
#define SET_KNOB_W    60
#define SET_BRT_X0    (SET_SEG_X + SET_KNOB_W / 2)
#define SET_BRT_X1    (SET_SEG_X + SET_SPAN_W - SET_KNOB_W / 2)
#define SET_BRT_X(pct) (SET_BRT_X0 + (pct) * (SET_BRT_X1 - SET_BRT_X0) / 100)
/* VOLUME's slider (0.4) on the same rail: OFF (0) .. 100 % in steps of SET_VOL_STEP */
#define SET_VOL_STEP  10           /* (5 was too fine: a fast drag ran its ticks together - Strato on the 1.8) */
#define SET_VOL_X(pct) SET_BRT_X(pct)
/* the old text foot's two places (UPDATES | CLOSE until 0.4.2): the ADVANCED page's CANCEL / BACK stand there */
#define SET_UPD_W     112
#if TANK_WORN                        /* the watch: UPDATES 114..226 and CLOSE 242..334, clear of the lower corners */
#define SET_UPD_X     114
#undef  SET_CLOSE_X
#define SET_CLOSE_X   242
#else
#define SET_UPD_X     (PAGE_BOWL ? 60 : 32)    /* the UPDATES button, bottom left (2026-09-30); in from the glass on the bowl */
#endif


/* ---- link mode (0.4, the tank link: docs/tank-link-0.4.md) ----
 * Drawn over the live tank like a tool in hand: a chip top left (the link
 * glyph + DONE), our badge top centre, an arrow tab on each side glass, the
 * hint and the NEARBY row in the PAGE safe box. A badge is a SHAPE and a
 * COLOR together (circle / triangle / square / diamond) - never hue alone.
 * An arrow is HELD: it fills from the bottom; LINK_ARM_HOLD_S asks to join
 * there, LINK_CLOSE_HOLD_S on an open door closes it. The platform feeds
 * render_link_touch every frame while link mode is up (down = a finger on
 * the glass) and acts on what it returns; render_link_mode draws with the
 * holds it is tracking. render_link_drift is the DRIFTED notice, drawn over
 * the tank whenever the view says so, link mode or not. */
#include "link.h"
#define LINK_ARM_HOLD_S   0.8f
#define LINK_CLOSE_HOLD_S 1.2f
enum { LINK_UI_NONE = 0, LINK_UI_DONE, LINK_UI_ARM_LEFT, LINK_UI_ARM_RIGHT, LINK_UI_CLOSE_LEFT, LINK_UI_CLOSE_RIGHT,
       LINK_UI_DISCONNECT, LINK_UI_SD_LEFT, LINK_UI_SD_RIGHT };   /* SD: the coin by an open door (0.4.2: send sand dollars there) */
/* SAND DOLLAR GIFTS (0.4.2): the coin spins at that door (ease in, two turns, ease out to rest) - on the HUD's button, or over
   the door's badge with the HUD closed; and a line in the HUD's hint place for `secs` */
void render_link_coin_spin(int edge, double clock);
void render_link_sd_line(const char *l1, const char *l2, double clock, float secs);
/* ... what a gift's news (tank_link_sd_send's answer, tank_link_sd_news: TL_SD_*) shows: the words, the spin */
void render_link_sd_news(int kind, int edge, int amount, int verdict, double clock);   /* every door of this tank shuts (the chip top right while a door is open; the DRIFTED notice's button) */
void render_link_mode(const link_view_t *v, uint16_t *fb, int stride, double clock);
int  render_link_touch(const link_view_t *v, float x, float y, bool down, float dt);
void render_link_leave(void);                   /* forget a hold in progress (link mode closed) */
void render_link_set_holds(float left, float right);   /* the tests and the snapshots: a hold's progress, 0..1 */
void render_link_drift(const link_view_t *v, uint16_t *fb, int stride, double clock);
void render_link_doors(const link_view_t *v, uint16_t *fb, int stride, double clock);   /* open doors while link mode is closed: a glow + the badge */
void render_link_badge(uint16_t *fb, int stride, int cx, int cy, int r, int badge);   /* a badge at frame coordinates (the NEARBY row, the doors) */
void render_link_where(int what, float *x, float *y);   /* the tests and the director ask the page: LINK_LEFT / LINK_RIGHT a tab's centre,
                                                         * 2 = DONE, 3 = link mode's DISCONNECT, 4 = the DRIFTED notice's DISCONNECT,
                                                         * 5 / 6 = the left / right door's coin (0.4.2) */
bool render_link_drift_hit(const link_view_t *v, float x, float y);   /* a tap on the DRIFTED notice's DISCONNECT */
bool render_drift_live(const tank_t *t, bool page_up);   /* the DRIFTED notice is SEEN (its DISCONNECT may answer): no page, notice, GIVE modal or gift card over it */
/* ---- MY TANKS + THE LIBRARY: the tank switcher (docs/tank-switcher.md).
 * ART FOR APPROVAL, 2026-10-29: the pages only, drawn from what the sim
 * stages (--shelf-shot); nothing behind them yet. ---- */
enum { SHELF_EMPTY, SHELF_NOW, SHELF_DORMANT, SHELF_PAUSED };
typedef struct {
    int state;                 /* SHELF_* */
    const char *name;
    bool experimental;
    const uint16_t *thumb;     /* render_shelf_thumb_size, RGB565 (native order) */
} shelf_slot_t;
typedef struct {
    const char *name, *line;   /* the tank type, its one plain line */
    const uint16_t *thumb;
    bool experimental, installed;
    int price;                 /* sand dollars for the next tank of it, 0 = free */
    bool full;                 /* no more of it on this board (the jelly tank: one, J3) - INSTALLED, dim, no price */
    bool locked;               /* shown, not open to this board (the jelly tank off the crew, J6): dimmed, CREW ONLY; a tap = the note */
} library_row_t;
/* LOADING (0.3.9.3): a tank's first seconds after a SWAP IN or a power-on, while its grass loop is first baked: the
 * platform shows render_loading and bakes at full speed (render_grass_loop_bake_more: up to n moments, 0 = not ready to
 * bake yet - draw a tank frame so the scene and the loop's look settle) until render_grass_loop_warming says false; line
 * under the name: NULL = FILLING THE TANK (a SWAP IN), else e.g. WAKING THE TANK */
void render_loading(uint16_t *fb, int stride, const char *name, double clock, const char *line);
/* THE BOOT SHOW (common/boot_show.c): the pieces in the tank back to front (their count), the day scene with some
 * left out (hide: a bit per item), the grass loop's baked band nearest the clock (rows from *y0; NULL: none yet) */
/* SWIM DEPTH (0.3.9.4): the block a fish BEHIND or THROUGH the grass works in (render_depth_buf_bytes of PSRAM; NULL:
 * every fish IN FRONT, as before) */
size_t render_depth_buf_bytes(void);
void render_set_depth_buf(void *buf);
void render_debug_crab_stack_old(bool on);   /* (tests: THE CRAB'S STACK off - the crab in its shelter not at the shelter's place among the pieces, as before 0.4) */
void render_debug_climb_stack_old(bool on);   /* (tests: --selftest-crab-climb's control - the crab climbing the volcano out of THE CRAB'S STACK, over every piece, as before 0.4.2) */
void render_debug_climb_grass_old(bool on);   /* (tests: --selftest-crab-depth's control - the crab climbing a volcano BEHIND at its own swim depth against the grass and its old place in the animal pass, as before the fix) */
void render_debug_gl_hold(bool on);   /* (tests: the grass loop locked holds its moment - the frames after are the last frame's moment, to compare) */
void render_debug_roost_pass_old(bool on);   /* (tests: --selftest-roost-pass's control - a piece behind an animal comes back over the crab in its shelter from the scene, as before 0.4) */
void render_debug_gl_skip_old(bool on);   /* (tests: the pieces a bed keeps off - its order - out of the grass loop's look, as before 0.4: a re-ranked plant served the old order) */
void render_debug_gl_odd_refuse(bool on);   /* (tests: the grass loop's plan never gets its odd-frond masks, as a plan held by PSRAM - set before it is planned) */
void render_debug_gl_odd_rule_old(bool on);   /* (tests: the control - live frames draw THROUGH even when the loop in use has no odd masks, as before 0.4.2) */
void render_debug_cuts_old(bool on);        /* (tests: THE RENDER CUTS of 0.4.2 off - the grass loop's moment mask built at every ask, bit by bit, and the seahorse's sprite with nothing kept; --selftest-cuts compares the frames) */
void render_debug_cuts_counts(uint32_t out[6]);   /* (... and what the cuts did since the start: lays that built the mask, asks the frame already had, masks built at an ask, odd fronds added at one, seahorse spirals built, spirals kept) */
void render_debug_crit_counts(uint32_t out[7]);   /* (THE CRITTERS' CUTS, switched with render_debug_cuts_old: urchin cells asked / skipped whole, the crab's tilted sampler's pixels walked / saved, its grid's claw and eye asks made / skipped, the front slot's held pixels put back) */
int  render_debug_crit_diff(const tank_t *t, int what);   /* (--selftest-crit-cuts: the crab's grid (0) / the urchin's tones (1) for t, the old way against the cuts: cells that differ) */
int  render_debug_crab_pose(const tank_t *t, void *out, int cap);   /* (tools/s3crit: the crab's pose for t, as its grid's build takes it; returns its size) */
void render_debug_gl_through_old(bool on);   /* (tests: a fine moment of the grass loop served with its BAND's odd fronds, as before 0.4.2 - a fish THROUGH the grass swapped with frond edges) */
void render_debug_gl_ceil_old(bool on);   /* (tests: THE CEILING off - the grass loop's band keeps its room to grow past the tallest the grass grows, as before 0.4.1) */
void render_depth_off(bool off);
void render_debug_front_keep_old(bool on);   /* (tests: --selftest-front-keep's control, the 0.3.9.5 restore) */
void render_debug_seahorse_stack_off(bool on);   /* (tests: --selftest-seahorse-stack's control, the seahorse over the pieces before its bed as 0.3.9.6 drew it) */
void render_debug_castle_row_only(bool on);   /* (tests / director keeptest old: 0.3.9.6's castle, the front row alone over an animal behind the grass) */
void render_debug_fish_slot_order(bool on);   /* (tests: the animal pass by the old ranks and slots again, whatever their depths - the control of --selftest-fish-pair) */
void render_debug_dep_home_only(bool on);     /* (tests: only the home fish's depths ask for the grass masks, as before 0.4 - the visitor control of --selftest-fish-pair) */
bool render_piece_covers(int item, int x, int y);
#define PC_SHADOW_NEAR 3                 /* a piece's CONTACT SHADOW: its body shifted this far down and right, hard ... */
#define PC_SHADOW_FAR  6                 /* ... and this far, soft (render.c pc_contact_shadow; the fronds over the rim take it too, 0.3.9.4) */
bool render_bed_leaf(int x, int y);   /* (tests: the sword plant IN FRONT's leaves, last frame) */
void render_debug_live_mask(bool on);
void render_debug_rim_off(bool off);
void render_debug_page_animals(bool show);   /* (tests: the creatures drawn under a placement page, which hides them since 0.3.9.4 - the layering checks) */
void render_debug_grass_mask(bool on);  /* (tests: keep the frame's grass mask - the loop's, at its moment - for render_debug_grass_at) */
bool render_debug_grass_at(int x, int y);
void render_debug_root_cut(bool on);   /* (tests: the put-back over an animal cut at DEP_ROOT_Y as in 0.3.9.4 - the control that fails --selftest-depth) */
#define DEP_ROOT_Y ((int)FISH_FLOOR_Y - 8)            /* the roots' first row (the pebbles' edge below it). 0.3.9.4 cut the grass's put-back over an animal here; since 0.3.9.6 the grass comes back on every row (a frond before a critter is before its feet too) - the tests still read it to count the roots apart */    /* (the A/B: a piece's contact shadow NOT falling on the fronds painted over its rim, as before 0.3.9.4) */   /* (tests: a piece drawn live on its page records its mask too, so render_piece_covers reads it) */
int  render_debug_piece_grass(int item, int *bands, int *fines, void *(*alloc)(size_t), void (*release)(void *));   /* (director pkcheck) */   /* (tests: a piece's own body, as the last frame recorded it) */
void render_depth_prof(int64_t *us, int *fish);   /* the depth's own work since the last call, and how many fish did it */   /* (the sim's grass tests: every fish IN FRONT) */
void render_proto_veg_layer(int layer);   /* (exploration: the sim's --depth-shot) */
int  render_boot_pieces(const tank_t *t, int items[DECOR_SLOTS]);
void render_boot_scene(const tank_t *t, uint16_t *dst, uint32_t hide);
void render_boot_scene_done(const tank_t *t);   /* after the last render_boot_scene: the tank's own scene and masks back */
const uint16_t *render_boot_grass(const tank_t *t, int *y0);
bool render_boot_grass_live(const tank_t *t, uint16_t *dst, int *y0);   /* no loop: the same drawn live into dst from *y0 (0.3.9.6) */
bool render_grass_loop_warming(void);
int  render_grass_loop_bake_more(const tank_t *t, int n);
void render_shelf_set_move(bool on, int i, const char *name);   /* MOVE (0.3.9.3): a tank in hand - the page's slot i lifted (-1: it is on another page) */
void render_shelf_set_touch_lock(bool on);     /* THE TOUCH LOCK (0.3.9.3): MY TANKS's padlock button drawn shut (taps locked) */
void render_shelf_lock_note(uint16_t *fb, int stride);   /* the padlock turned ON (0.4.2): what it does and the way back, over MY TANKS; any tap closes it */
void render_touch_lock_hint(uint16_t *fb, int stride);   /* a tap on a locked tank: TOUCH IS LOCKED / PRESS BOOT TWICE TO UNLOCK */
void render_shelf_thumb_size(int *w, int *h);
void render_shelf_free(void);       /* MY TANKS closed: the planks' wood strips freed (drawn again on the next open) */
void render_shelf_thumb(const uint16_t *frame, uint16_t *thumb);   /* a whole frame -> its little-tank picture */
void render_shelf(uint16_t *fb, int stride, const shelf_slot_t slot[8], int page, int pages);
enum { SHELF_TAP_NONE, SHELF_TAP_CLOSE, SHELF_TAP_LIBRARY, SHELF_TAP_PREV, SHELF_TAP_NEXT, SHELF_TAP_TOUCH_LOCK, SHELF_TAP_SETTINGS, SHELF_TAP_SLOT0 = 10 };   /* (SETTINGS: the icon foot's cog, 0.4.2) */
int  render_shelf_tap(float x, float y);                           /* SHELF_TAP_*, a slot = SHELF_TAP_SLOT0 + i (frame coordinates) */
void render_shelf_card(uint16_t *fb, int stride, const shelf_slot_t *s, const char *kind, int shelf_days, int age_days);
/* SWAP IN tapped (0.3.9.4): the card's button pulses and says SWAPPING (t: seconds since the tap, < 0 off) for
 * SWAP_PULSE_S x 1, then the glass fades out over SWAP_FADE_S (render_fade) and the swap runs in the dark */
#define SWAP_PULSE_S 0.35f
#define SWAP_FADE_S  0.30f
void render_shelf_card_swapping(float t);
void render_fade(uint16_t *fb, int stride, int light);   /* the frame at light/256 */
/* MORE: REMOVE (0.3.9.3) - the panel (part 1 its backdrop, drawn once; 2 the panel, every frame while the hold fills: hold
   0..1) and its places: BACK, HOLD TO SELL (held SHM_HOLD_S), outside it */
#define SHM_HOLD_S 2.0f
enum { SHM_TAP_NONE, SHM_TAP_OUTSIDE, SHM_TAP_BACK, SHM_TAP_HOLD };
/* n: how many pieces, critters and backgrounds the refund is for (shelf_refund_value; NULL: none named) - its line names
   only those (0.4.1, audit P6) */
void render_shelf_remove(uint16_t *fb, int stride, const shelf_slot_t *s, int refund, const int n[3], float hold, int part);
int  render_shelf_remove_hit(float x, float y);
/* a tank's RENAME (0.3.9.3): the keyboard's state (the name's two lines, the one being typed, the keys' page), its page
 * (part 1 the backdrop - drawn once with the shelves; 2 the rest, every frame; 3 both) and its touch: a key acts when the
 * finger lifts on the key it landed on (SHR_TICK); BACK / DONE end it (SHR_DONE: render_shelf_rename_name). The keys'
 * places for tests and the director: render_shelf_rename_hit / _key_xy, the characters of a page _keys */
typedef struct { char line[2][8]; int row, page; bool down; float px, py; } shelf_rename_t;   /* two lines of 7 */
enum { SHR_NONE, SHR_TICK, SHR_CANCEL, SHR_DONE };
enum { SHK_NONE, SHK_CHAR0 = 1, SHK_MORE = 20, SHK_DEL, SHK_SPACE, SHK_LINE, SHK_BACK, SHK_DONE, SHK_LINE1, SHK_LINE2, SHK_CLEAR };   /* SHK_CHAR0 + 0..13: a page's keys */
void render_shelf_rename_begin(shelf_rename_t *r, const char *name);
void render_shelf_rename(uint16_t *fb, int stride, const shelf_slot_t *s, const shelf_rename_t *r, int part, double clock);
int  render_shelf_rename_touch(shelf_rename_t *r, float x, float y, bool down);
void render_shelf_rename_name(const shelf_rename_t *r, char *out, size_t cap);   /* the name, stored form: "LINE 1\nLINE 2" */
int  render_shelf_rename_hit(float x, float y);
void render_shelf_rename_key_xy(int key, float *x, float *y);
const char *render_shelf_rename_keys(int page);   /* a page's fourteen characters (0..2) */
enum { SHC_TAP_NONE, SHC_TAP_OUTSIDE, SHC_TAP_DORMANT, SHC_TAP_PAUSED, SHC_TAP_SWAP, SHC_TAP_RENAME, SHC_TAP_MORE, SHC_TAP_SETTINGS };   /* (SETTINGS: the jelly tank on the glass, J7) */
int  render_shelf_card_tap(float x, float y);
void render_library(uint16_t *fb, int stride, const library_row_t *row, int n, int wallet, const char *checked);
enum { LIB_TAP_NONE, LIB_TAP_BACK, LIB_TAP_PREV, LIB_TAP_NEXT, LIB_TAP_ROW0 = 10 };
void render_library_set_page(int page, int pages);   /* (0.4.2) the pager under the rows: page 1.., of pages */
/* a locked row's note (J6): what the tank is and who has it for now, over the library; OK closes it */
void render_library_note(uint16_t *fb, int stride);
void render_library_note_name(const char *name);   /* whose note: the row's name over it (NULL: JELLY TANK) */
enum { LNOTE_TAP_NONE, LNOTE_TAP_OK, LNOTE_TAP_OUTSIDE };
int  render_library_note_tap(float x, float y);
int  render_library_tap(float x, float y, int n);
/* A PICTURE STILL BEING MADE (0.4.2, lib_pics.h): bubbles rising in the empty glass of each row in `rows` (bit i; `locked`:
 * its row is dimmed) / each theme in `themes` (bit k) - over the kept page, every frame; t seconds. The pictures' places
 * (inside the little tank's glass) for tests. render_library_pic / render_jelly_pick_pic: a picture just made, put into
 * the kept page (that one little tank drawn again, exactly as the page draws it) */
void render_library_loading(uint16_t *fb, int stride, unsigned rows, unsigned locked, float t);
void render_library_pic_rect(int row, int *x, int *y, int *w, int *h);
void render_library_pic(uint16_t *fb, int stride, int row, const uint16_t *pic, bool locked);
/* THE JELLY TANK (the crew's experimental tank, docs/jelly-tank-0.6.md "The crew release"):
 * the card and REMOVE panel speak of jellies and name the tank's theme while one is set (NULL: a fish tank) */
void render_shelf_set_jelly(const char *theme);
/* ... and of piranhas or minnows while one of those tanks' is (0.4.2): what swims in it, for REMOVE's words (NULL: not one) */
void render_shelf_set_xt(const char *creatures);
/* PICK A THEME: GET on the jelly row opens it - three tanks (a picture each, JELLY_THEMES order), the
 * picked one ringed and the others dimmed (-1: none yet, NEW TANK dim); the pick is the tank's for good */
void render_jelly_pick(uint16_t *fb, int stride, const char *const names[3], const uint16_t *const thumbs[3], int picked);
enum { JP_TAP_NONE, JP_TAP_BACK, JP_TAP_NEW, JP_TAP_THEME0 = 10 };
int  render_jelly_pick_tap(float x, float y);
void render_jelly_pick_loading(uint16_t *fb, int stride, unsigned themes, int picked, float t);
void render_jelly_pick_pic(uint16_t *fb, int stride, int k, const uint16_t *pic, int picked);
void render_jelly_pick_pic_rect(int k, int *x, int *y, int *w, int *h);

/* SAND DOLLAR GIFTS (0.4.2): the coin on a card (32 px, frame coordinates of its top left), turned `spin` radians about
   its upright axis (0 = at rest, face on) */
void render_sd_coin(uint16_t *fb, int stride, int x, int y, float spin);
void render_milestones_modal_box(const tank_t *t, int *x, int *y, int *w, int *h);   /* the fish card's box (frame coordinates) */

#endif
