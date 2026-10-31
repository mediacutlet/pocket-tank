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
void render_set_vignette_cache(uint8_t *buf);

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
void render_stats_card(const tank_t *t, int fish_idx, uint16_t *fb, int stride);
/* Optional card cache (RENDER_CARD_W x RENDER_CARD_H uint16): with the scene
 * cache live, the card is redrawn at most 4x/s and blitted otherwise (~7 ms
 * -> ~1 ms per frame on the device). NULL = draw every frame. */
#ifdef TANK_ROUND               /* the bowl: where the circle is tall enough for the card and the toolbox under it */
#define RENDER_CARD_X 78
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
void render_crab_cache(bool on);        /* the crab's pose grids kept (on, the default) or built every pose change: the selftest's A/B (0.3.9.1) */
int  render_crab_builds(void);          /* the crab's grid builds so far */
/* The TOOLBOX (2026-10-01): under a fish's card, a little apart from it so
 * MORE keeps its own ground, a box in the card's dress with two big buttons
 * - the SPONGE and the SCISSORS (tank.h TOOL_*). render_stats_card draws it
 * with a fish's card; the one in hand is lit (a white double edge on a
 * lighter fill - brightness, not hue). render_tools_hit(x, y) is the tap
 * test while a fish's card is up: TOOL_SPONGE / TOOL_SCISSORS, or -1 (the
 * box's half, from its top edge down to the bezel, plus side slop). */
#define RENDER_TOOLS_X RENDER_CARD_X
#define RENDER_TOOLS_Y (RENDER_CARD_Y + RENDER_CARD_H + RENDER_TOOLS_GAP)
#define RENDER_TOOLS_W RENDER_CARD_W
#define RENDER_TOOLS_H 70
int  render_tools_hit(float x, float y);
/* the LOCK (0.3.5): a small box of its own just right of the toolbox, the
 * padlock open (bare fingers wipe and cut, as before) or shut (only the
 * tools do: tank_lock_tools). Shut is lit like a tool in hand - a white
 * double edge on a lighter fill - and the shackle's shape says it too,
 * never the hue alone. render_lock_hit is its tap test while a fish's card
 * is up: its column, from the toolbox's top edge down to the bezel. */
#define RENDER_SIDE_GAP 6          /* the lock off the toolbox and the NEXT arrow off the card: one gap (0.3.9; the lock was 12) */
#define RENDER_LOCK_X (RENDER_TOOLS_X + RENDER_TOOLS_W + RENDER_SIDE_GAP)
#define RENDER_LOCK_W 44
#define RENDER_LOCK_H 44
#define RENDER_LOCK_Y (RENDER_TOOLS_Y + (RENDER_TOOLS_H - RENDER_LOCK_H) / 2)
bool render_lock_hit(float x, float y);
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
                                   arrow's */
#define RENDER_BAT_X 320
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
void render_battery(uint16_t *fb, int stride, float frac, int state, float clock);
/* the battery page (2026-09-24): a panel over the live tank - the battery
 * large with its percent, the state in words, time left / time to full,
 * then since the cable moved, screen-on time on this charge, what a full
 * charge lasts (battery_info fills bi) and the voltage, dim. Any tap closes
 * it (the platforms); it closes itself after a while. */
void render_battery_info(uint16_t *fb, int stride, const bat_info_t *bi, float clock);
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
/* a tap on the page (2026-09-13, Strato: with this much to tap, a stray tap
 * must not drop the whole page): MS_TAP_CLOSE = the CLOSE button, bottom
 * right - the ONLY way out by touch (caller closes the page, then
 * progression_ack_milestones + render_milestones_leave); MS_TAP_KEPT = a
 * badge / name / strip opened the detail modal, or the modal was up and
 * this tap closed it; MS_TAP_NONE = nothing here (the caller may try the
 * brightness row). */
enum { MS_TAP_NONE = 0, MS_TAP_KEPT = 1, MS_TAP_CLOSE = 2, MS_TAP_SETTINGS = 3,   /* SETTINGS: the button bottom left (2026-09-15) opens the settings page */
       MS_TAP_SHOP = 4,                                                            /* the sand dollar left of the TANK row opens the shop */
       MS_TAP_RENAME = 16, MS_TAP_SELL = 32 };                                     /* a fish's card (2026-10-01): + the fish's index */
/* MS_TAP_RENAME + fish: the card's RENAME - the caller leaves the page (ack +
 * leave, as for CLOSE) and opens setup_begin_rename; when that flow ends
 * (setup_take_renamed) it brings the page back with render_milestones_show_fish.
 * MS_TAP_SELL + fish: the card's SELL, confirmed (its second tap) - the
 * caller calls progression_sell_fish; the page stays up, a row shorter. */
int  render_milestones_tap(const tank_t *t, float x, float y);
void render_milestones_show_fish(const tank_t *t, int fish);   /* put a fish's card up (the page must be showing) */
/* where things are, in the FRAME's coordinates as a tap arrives - for the
 * tests and the director, so neither copies a board's layout: a fish's card
 * (false = none up) with the centres of RENAME and SELL; a point on a row's name */
bool render_milestones_card(const tank_t *t, int *fish, int *rename_x, int *sell_x, int *btn_y);
bool render_milestones_arrow(const tank_t *t, bool right, int *x, int *y);   /* the modal's left / right arrow (false = no modal up) */
void render_milestones_row(int row, int *name_x, int *y);
/* a sideways swipe on the milestones page (release - press dx): along the
 * TANK row, with more badges than one row holds, it turns the row's page
 * (2026-09-30). True = it did (or was a page swipe at the row's end);
 * false = not a page swipe. */
bool render_milestones_swipe(const tank_t *t, float x, float y, float dx);
void render_milestones_leave(void);

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
       SHOP_TAP_FRY = 128, SHOP_TAP_SHELVE = 256, SHOP_TAP_PLACE = 512 };   /* BUY / MOVE / SELL / SHELVE / PLACE + item index; FRY + the fry's place in the offer (0.3.5) */
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
void render_shop(const tank_t *t, uint16_t *fb, int stride);
int  render_shop_tap(const tank_t *t, float x, float y);
void render_shop_leave(void);
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
void     render_set_grass_loop(void *(*alloc)(size_t), void (*release)(void *), size_t budget);
void     render_grass_loop_on(bool on);
bool     render_grass_loop_is_on(void);
bool     render_grass_loop_parts(float clock, const uint16_t **scene, size_t *top_bytes, const uint16_t **band, size_t *band_off, size_t *band_bytes, unsigned *ep);
void     render_grass_loop_prof(int *n, int *bakes, int64_t *us, int *rows);   /* moments, bakes and their time since the last call, the band's rows */
void     render_grass_loop_bypass(bool on);   /* (the sim's tests: the loop kept but not used - the old drawing) */
void     render_force_veg_clock(float c);     /* (... and the sway pinned to a moment; -1 = the frame's) */
int      render_grass_loop_n(void);
int      render_grass_loop_ready(void);       /* moments baked and current */
void     render_grass_loop_lock(int moments);
int      render_grass_loop_moment(void);      /* the last frame's moment (stepping), -1 before one */
void     render_grass_loop_arena(int *used_kb, int *cap_kb);   /* the fine moments' differences: KB used of KB held */
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
#define RENDER_CONFIRM_Y     76
#define RENDER_CONFIRM_W     336
#define RENDER_CONFIRM_H     216
#define RENDER_CONFIRM_BTN_W 132
#define RENDER_CONFIRM_BTN_H 56
#define RENDER_CONFIRM_BTN_Y (RENDER_CONFIRM_Y + 112)
#define RENDER_CONFIRM_NO_X  (RENDER_CONFIRM_X + 24)
#define RENDER_CONFIRM_YES_X (RENDER_CONFIRM_X + RENDER_CONFIRM_W - 24 - RENDER_CONFIRM_BTN_W)
void render_confirm_reset(uint16_t *fb, int stride, float frac);
int  render_confirm_hit(float x, float y);

/* Settings page (2026-09-15; the brightness row left the milestones page
 * for it): BRIGHTNESS, a slider since 0.3.9 (a keeper sensitive to light
 * found 30 %, the lowest of the three buttons it had, too bright): a knob
 * carrying the percent, 5 .. 100 % in steps of 5, dragged along the row or
 * tapped where wanted; VOLUME OFF / QUIET / NORMAL as segment buttons - tap
 * the one you want - then LIGHTS OUT, one row since
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
 * SET_TAP_VOLUME 0..2 (those two are the platform's to apply),
 * SET_TAP_LIGHT (the row stepped to or from MANUAL: *value 1 = AUTO),
 * SET_TAP_IDLE (AUTO's time stepped: *value = the seconds now set),
 * SET_TAP_FEED (*value 1 = AUTO FEED on), SET_TAP_ROTATE (*value 1 =
 * locked), SET_TAP_CLOSE, or nothing.
 * A worn tank (TANK_WORN, the watch) has SCREEN NORMAL / TURNED
 * (tank_screen_*) where the others have ROTATION, applied here like the light's.
 * render_settings_tap is the bare hit test (tests). */
enum { SET_TAP_NONE = 0, SET_TAP_CLOSE = 1, SET_TAP_BRIGHT = 2, SET_TAP_VOLUME = 3, SET_TAP_LIGHT = 4, SET_TAP_IDLE = 5,
       SET_TAP_UPDATES = 6,     /* the UPDATES button, bottom left (2026-09-30): the platform opens the updates page (update.h) */
       SET_TAP_SCREEN = 7,      /* a worn tank's SCREEN row (2026-10-02): *value 1 = TURNED, already applied and marked
                                   for the save - the platform only logs it (the picture turns on the next frame) */
       SET_TAP_FEED = 8,        /* AUTO FEED (0.3.2): *value 1 = ON; applied and marked for the save */
       SET_TAP_ROTATE = 9,      /* ROTATION (0.3.2): *value 1 = locked; applied and marked for the save */
       SET_TAP_BRIGHT_LIVE = 10,
       SET_TAP_TANKS = 11 };        /* MY TANKS (0.3.9.3): the platform opens the tank switcher (docs/tank-switcher.md) */  /* the BRIGHTNESS slider under a finger (0.3.9): *value = the percent, the panel's to
                                       show now; SET_TAP_BRIGHT follows with the last one when the finger lifts */
void render_settings(const tank_t *t, uint16_t *fb, int stride, int bright_pct, int volume);
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
       ADV_HIT_BOX, ADV_HIT_CANCEL, ADV_HIT_CONTINUE, ADV_HIT_START, ADV_HIT_RESET };
void render_advanced(uint16_t *fb, int stride, int pick_mah, int pick_ma, int set_mah, bool stock, bool fast);
int  render_advanced_tap(float x, float y);
void render_battery_warning(uint16_t *fb, int stride, int cell_mah, int charge_ma, bool checked);
int  render_battery_warning_tap(float x, float y);
void render_touch_intro(uint16_t *fb, int stride, bool own_in_use, const char *msg);
int  render_touch_intro_tap(float x, float y, bool own_in_use);
void render_touch_cross(uint16_t *fb, int stride, int i, int n, int tx, int ty);
void render_touch_check(uint16_t *fb, int stride, int tx, int ty, float secs_left, float secs_total);
int  render_settings_tap(float x, float y, int *value);
int  render_settings_touch(tank_t *t, float x, float y, bool down, int *value);
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
 * swimming on `clock` - the setup's live preview of a colour choice */
void render_fish_preview(uint16_t *fb, int stride, float x, float y, float size,
                         uint32_t body, uint32_t fin, uint32_t accent, float clock);
/* the same, but AS THE FISH IS: its own stage (a fry shows no markings yet,
 * an elder its long tail), calm and fed - the birth flow's portrait */
void render_fish_portrait(uint16_t *fb, int stride, float x, float y, float size, const fish_t *who, float clock);

/* ---- the pages' layouts (2026-10-01). These lived in render.c; they are here
 * so the sim's selftests tap where the layout says on every board instead of
 * carrying the rectangle's pixels. PAGE coordinates unless a line says the
 * frame's; render.c has the words that go with each page. ---- */
/* the snail's and the shrimp school's cards: centered on the glass (the frame's coordinates) */
#define SNAIL_CARD_W 336
#define SNAIL_CARD_H 176
#define SHRIMP_CARD_W 336
#define SHRIMP_CARD_H 190
#define URCHIN_CARD_W 336
#define URCHIN_CARD_H 194
#define CRAB_CARD_W 336          /* the strawberry crab's (0.3.9): the urchin's size */
#define CRAB_CARD_H 194
#define SEAHORSE_CARD_W 336
#define SEAHORSE_CARD_H 188
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
 * the coin, MSP_TANK_Y + 48) to the buttons' top, the lower half is the buttons' */
#define MSP_TANK_BOT     (MSP_TANK_Y + 48)
#define MSP_FOOT_HIT_Y   ((MSP_TANK_BOT + MSP_FOOT_Y) / 2)
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
#define MSP_SD_X      36                /* the sand dollar on the TANK row (the shop), centred like the fish portraits */
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
#define MSP_TPG_X     (PAGE_BOWL ? 396 : PAGE_NARROW ? 398 : 412)   /* the arrow's column: right of the sixth badge's slop, out to the glass
                                                                    (the bowl: its "new" ring and the page pips inside the circle) */
#define MSP_ARROW_W   40             /* the arrow buttons, inset at the modal's top corners */
#define MSP_ARROW_H   32
#define MSP_ARROW_IN  10
#define MSP_ARROW_HIT 100            /* the hit box: the corner's whole width in from each side, 12 above, 24 below
                                        (a miss closes the modal, so the box is wide) */
/* a gate's modal is taller (two sentence lines, progress, the HOW? button)
   so it sits higher than the badge modal, clear of the CLOSE button */
#define MSP_FRY_MODAL_Y 60
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
#define SHP_ROW_Y0    98
#define SHP_ROW_DY    56
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
#define SHP_PER_PAGE 4
#define SHP_PAGES    ((SD_ITEM_COUNT + SHP_PER_PAGE - 1) / SHP_PER_PAGE)   /* without the FRY row (render.c: shp_pages) */
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
#define SET_NOTE_Y    102
#define SET_ROW3_Y    132            /* LIGHTS OUT */
#define SET_ROW4_Y    188            /* AUTO FEED */
#define SET_ROW5_Y    244            /* SCREEN: NORMAL / TURNED */
#define SET_NOTE5_Y   286            /* under it: who it is for */
#define SET_FOOT_Y    392            /* UPDATES and CLOSE */
#else
#define SET_TITLE_Y   14
#define SET_ROW1_Y    58             /* BRIGHTNESS: the slider */
#define SET_ROW2_Y    108            /* VOLUME */
#define SET_NOTE_Y    146            /* "FISH ARE QUIET AT NIGHT" */
#define SET_ROW3_Y    176            /* LIGHTS OUT */
#define SET_ROW4_Y    222            /* AUTO FEED */
#define SET_ROW5_Y    268            /* ROTATION */
#define SET_FOOT_Y    MSP_CLOSE_Y    /* UPDATES and CLOSE: the milestones page's foot */
#endif
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
 * SET_BRT_MIN .. 100 % in steps of SET_BRT_STEP */
#define SET_BRT_MIN   5
#define SET_BRT_STEP  5
#define SET_KNOB_W    60
#define SET_BRT_X0    (SET_SEG_X + SET_KNOB_W / 2)
#define SET_BRT_X1    (SET_SEG_X + SET_SPAN_W - SET_KNOB_W / 2)
#define SET_BRT_X(pct) (SET_BRT_X0 + ((pct) - SET_BRT_MIN) * (SET_BRT_X1 - SET_BRT_X0) / (100 - SET_BRT_MIN))
/* ROTATION: one icon button where the first segment stands, its word beside it */
#define SET_ROT_WORD_X (SET_SEG_X + SET_SEG_W + 14)
#define SET_UPD_W     112
#if TANK_WORN                        /* the watch: UPDATES 114..226 and CLOSE 242..334, clear of the lower corners */
#define SET_UPD_X     114
#undef  SET_CLOSE_X
#define SET_CLOSE_X   242
#else
#define SET_UPD_X     (PAGE_BOWL ? 60 : 32)    /* the UPDATES button, bottom left (2026-09-30); in from the glass on the bowl */
#endif

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
} library_row_t;
/* LOADING (0.3.9.3): a tank's first seconds after a SWAP IN or a power-on, while its grass loop is first baked: the
 * platform shows render_loading and bakes at full speed (render_grass_loop_bake_more: up to n moments, 0 = not ready to
 * bake yet - draw a tank frame so the scene and the loop's look settle) until render_grass_loop_warming says false */
void render_loading(uint16_t *fb, int stride, const char *name, float clock);
bool render_grass_loop_warming(void);
int  render_grass_loop_bake_more(const tank_t *t, int n);
void render_shelf_set_move(bool on, int i, const char *name);   /* MOVE (0.3.9.3): a tank in hand - the page's slot i lifted (-1: it is on another page) */
void render_shelf_set_touch_lock(bool on);     /* THE TOUCH LOCK (0.3.9.3): MY TANKS's padlock button drawn shut (taps locked) */
void render_touch_lock_hint(uint16_t *fb, int stride);   /* a tap on a locked tank: TOUCH IS LOCKED / PRESS BOOT TWICE TO UNLOCK */
void render_shelf_thumb_size(int *w, int *h);
void render_shelf_thumb(const uint16_t *frame, uint16_t *thumb);   /* a whole frame -> its little-tank picture */
void render_shelf(uint16_t *fb, int stride, const shelf_slot_t slot[8], int page, int pages);
enum { SHELF_TAP_NONE, SHELF_TAP_CLOSE, SHELF_TAP_LIBRARY, SHELF_TAP_PREV, SHELF_TAP_NEXT, SHELF_TAP_TOUCH_LOCK, SHELF_TAP_SLOT0 = 10 };
int  render_shelf_tap(float x, float y);                           /* SHELF_TAP_*, a slot = SHELF_TAP_SLOT0 + i (frame coordinates) */
void render_shelf_card(uint16_t *fb, int stride, const shelf_slot_t *s, const char *kind, int shelf_days, int age_days);
/* MORE: REMOVE (0.3.9.3) - the panel (part 1 its backdrop, drawn once; 2 the panel, every frame while the hold fills: hold
   0..1) and its places: BACK, HOLD TO SELL (held SHM_HOLD_S), outside it */
#define SHM_HOLD_S 2.0f
enum { SHM_TAP_NONE, SHM_TAP_OUTSIDE, SHM_TAP_BACK, SHM_TAP_HOLD };
void render_shelf_remove(uint16_t *fb, int stride, const shelf_slot_t *s, int refund, float hold, int part);
int  render_shelf_remove_hit(float x, float y);
/* a tank's RENAME (0.3.9.3): the keyboard's state (the name's two lines, the one being typed, the keys' page), its page
 * (part 1 the backdrop - drawn once with the shelves; 2 the rest, every frame; 3 both) and its touch: a key acts when the
 * finger lifts on the key it landed on (SHR_TICK); BACK / DONE end it (SHR_DONE: render_shelf_rename_name). The keys'
 * places for tests and the director: render_shelf_rename_hit / _key_xy, the characters of a page _keys */
typedef struct { char line[2][8]; int row, page; bool down; float px, py; } shelf_rename_t;   /* two lines of 7 */
enum { SHR_NONE, SHR_TICK, SHR_CANCEL, SHR_DONE };
enum { SHK_NONE, SHK_CHAR0 = 1, SHK_MORE = 20, SHK_DEL, SHK_SPACE, SHK_LINE, SHK_BACK, SHK_DONE, SHK_LINE1, SHK_LINE2, SHK_CLEAR };   /* SHK_CHAR0 + 0..13: a page's keys */
void render_shelf_rename_begin(shelf_rename_t *r, const char *name);
void render_shelf_rename(uint16_t *fb, int stride, const shelf_slot_t *s, const shelf_rename_t *r, int part, float clock);
int  render_shelf_rename_touch(shelf_rename_t *r, float x, float y, bool down);
void render_shelf_rename_name(const shelf_rename_t *r, char *out, size_t cap);   /* the name, stored form: "LINE 1\nLINE 2" */
int  render_shelf_rename_hit(float x, float y);
void render_shelf_rename_key_xy(int key, float *x, float *y);
const char *render_shelf_rename_keys(int page);   /* a page's fourteen characters (0..2) */
enum { SHC_TAP_NONE, SHC_TAP_OUTSIDE, SHC_TAP_DORMANT, SHC_TAP_PAUSED, SHC_TAP_SWAP, SHC_TAP_RENAME, SHC_TAP_MORE };
int  render_shelf_card_tap(float x, float y);
void render_library(uint16_t *fb, int stride, const library_row_t *row, int n, int wallet, const char *checked);
enum { LIB_TAP_NONE, LIB_TAP_BACK, LIB_TAP_ROW0 = 10 };
int  render_library_tap(float x, float y, int n);

#endif
