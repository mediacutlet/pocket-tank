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
extern int64_t render_prof_us[7];

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
/* With a tool in hand and no fish card up, a chip at the top left says so:
 * the tool and DONE. A tap on it (render_tool_chip_hit) puts the tool back
 * in the box. render_tool_chip draws nothing with TOOL_HAND, or when
 * fish_idx is a fish (its card shows the box instead). */
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
#ifdef TANK_ROUND               /* the bowl has no top right corner: top centre, the bolt left of it */
#define RENDER_BAT_X ((TANK_W - RENDER_BAT_W) / 2 + 10)
#define RENDER_BAT_Y 16
#elif defined(TANK_WATCH)       /* the watch: the top right, in from its round corner */
#define RENDER_BAT_X (TANK_W - RENDER_BAT_W - 56)
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
enum { SHOP_TAP_NONE = 0, SHOP_TAP_KEPT = 1, SHOP_TAP_CLOSE = 2, SHOP_TAP_BUY = 16, SHOP_TAP_MOVE = 32, SHOP_TAP_SELL = 64 };   /* BUY / MOVE / SELL + item index */
/* SHOP_TAP_SELL (2026-09-24): an owned placeable piece's modal has SELL next
 * to MOVE; the first tap arms it ("+30 OK?"), the second returns SELL + item
 * and the platform calls progression_sell. Test SELL before MOVE before BUY. */
/* SHOP_TAP_MOVE (2026-09-16): an owned, placeable item's modal carries a MOVE
 * button - the platform closes the shop and opens setup.c's placement page
 * (setup_begin_place), the same page a purchase opens. */
void render_shop(const tank_t *t, uint16_t *fb, int stride);
int  render_shop_tap(const tank_t *t, float x, float y);
void render_shop_leave(void);
int  render_coral_cells(float growth);   /* the coral sprite's filled cells at a growth (the sim's selftest) */
int  render_cluster_cells(float growth); /* the reef cluster's, likewise */
/* the decor scratch (2026-09-24): the coral's and the cluster's tables in one
 * block the platform provides - PSRAM on the board (internal RAM is spoken
 * for); the sim leaves it unset and render calloc's it */
size_t render_decor_scratch_size(void);
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
 * for it): BRIGHTNESS 30 / 60 / 100 % and VOLUME OFF / QUIET / NORMAL as
 * segment buttons - tap the one you want - then LIGHTS OUT, one row since
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
 * and returns what happened: SET_TAP_BRIGHT with *value = the percent,
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
       SET_TAP_ROTATE = 9 };    /* ROTATION (0.3.2): *value 1 = locked; applied and marked for the save */
void render_settings(const tank_t *t, uint16_t *fb, int stride, int bright_pct, int volume);
int  render_settings_tap(float x, float y, int *value);
int  render_settings_touch(tank_t *t, float x, float y, bool down, int *value);

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
#define MSP_SET_X     32                /* the SETTINGS button, bottom left, where the brightness row was */
#define MSP_SET_Y     MSP_CLOSE_Y
#define SHP_CLOSE_X   MSP_CLOSE_X
#define SET_CLOSE_X   MSP_CLOSE_X
#endif
#define MSP_SD_X      36                /* the sand dollar on the TANK row (the shop), centred like the fish portraits */
#ifdef TANK_ROUND
#define MSP_UPG_X     114
#else
#define MSP_UPG_X     178               /* the UPGRADES button, centred between SETTINGS and CLOSE: the shop too (Strato, 2026-09-15) */
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
#define SHP_EARN_MODAL_H 224
#define SHP_TWO_GAP 16               /* MOVE and SELL side by side in the modal */
#define SHP_TWO_X0  (SHP_MODAL_X + (SHP_MODAL_W - 2 * MSP_HOW_W - SHP_TWO_GAP) / 2)
#define SHP_TWO_X1  (SHP_TWO_X0 + MSP_HOW_W + SHP_TWO_GAP)
#define SHP_PER_PAGE 4
#define SHP_PAGES    ((SD_ITEM_COUNT + SHP_PER_PAGE - 1) / SHP_PER_PAGE)
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
#define SET_ROW1_Y    58             /* BRIGHTNESS */
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

#endif
