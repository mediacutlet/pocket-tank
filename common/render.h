/* render.h — software renderer: draws the tank into a raw RGB565 framebuffer.
 * No LVGL/SDL dependency; the same code runs on the ESP32 (the display port
 * just decides where the buffer goes). */
#ifndef RENDER_H
#define RENDER_H

#include <stddef.h>
#include "tank.h"
#include "ui.h"
#include "battery.h"

/* fb is TANK_W x TANK_H, RGB565, stride in PIXELS (usually TANK_W). */
void render_tank(const tank_t *t, uint16_t *fb, int stride);

/* Optional frame profiling: set a microsecond clock and render_tank fills
 * render_prof_us per stage (0 scene-copy, 1 shafts, 2 veg (back), 3 food+
 * bubbles, 4 fish + front veg, 5 vignette sweep, 6 algae film). Accumulates
 * until the caller zeroes it. NULL = off. */
extern int64_t (*render_clock_us)(void);
extern int64_t render_prof_us[7];

/* Optional static-scene cache (TANK_W*TANK_H uint16): the water gradient,
 * pebbles and reef rock are rendered once per lighting state and copied each
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
#define RENDER_DIRTY_WORDS (TANK_H * (TANK_W / 32))
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
void render_stats_card(const tank_t *t, int fish_idx, uint16_t *fb, int stride);
/* Optional card cache (RENDER_CARD_W x RENDER_CARD_H uint16): with the scene
 * cache live, the card is redrawn at most 4x/s and blitted otherwise (~7 ms
 * -> ~1 ms per frame on the device). NULL = draw every frame. */
/* Its pixel-art icons (24 px needs, 16 px trait poles) cannot shrink, so a
 * short tank (the 2.8" CYD's 240) gets the card in two columns instead of
 * one: the needs down the left, the traits and MORE down the right. */
#define RENDER_CARD_COMPACT UI_COMPACT
#if RENDER_CARD_COMPACT
#define RENDER_CARD_X 6
#define RENDER_CARD_Y 6
#define RENDER_CARD_W 224
#define RENDER_CARD_H 144
#else
#define RENDER_CARD_X 14
#define RENDER_CARD_Y 8
#define RENDER_CARD_W 124
#define RENDER_CARD_H 258       /* 228 + the MORE button strip (2026-09-16) */
#endif
/* The card is copied into the frame row by row with no clipping: it has to
 * lie inside the tank, or it writes past the end of the framebuffer. */
_Static_assert(RENDER_CARD_Y + RENDER_CARD_H <= TANK_H && RENDER_CARD_X + RENDER_CARD_W <= TANK_W,
               "the stats card must fit inside the tank");
/* the card's tap hit box (touch ports): the card itself plus slop, most of
 * it BELOW the MORE button - fingers aiming at a button by the foot land
 * low and wide (Strato, 2026-09-16: "I'm not tapping it reliably"), and the
 * water under the card is nothing a tap needs. RENDER_CARD_HIT(x, y) is the test. */
#define RENDER_CARD_HIT_BELOW (RENDER_CARD_COMPACT ? 30 : 56)
#define RENDER_CARD_HIT_SIDE  12
#define RENDER_CARD_HIT(x, y) ((x) >= RENDER_CARD_X - RENDER_CARD_HIT_SIDE && (x) < RENDER_CARD_X + RENDER_CARD_W + RENDER_CARD_HIT_SIDE && \
                               (y) >= RENDER_CARD_Y && (y) < RENDER_CARD_Y + RENDER_CARD_H + RENDER_CARD_HIT_BELOW)
void render_set_card_cache(uint16_t *buf);

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
#define RENDER_BAT_X (TANK_W - RENDER_BAT_W - 28)     /* clear of the curved bezel; the bolt sits left of it */
#define RENDER_BAT_Y 8
#define RENDER_BAT_HIT(x, y) ((x) >= RENDER_BAT_X - 44 && (y) < RENDER_BAT_Y + RENDER_BAT_H + 40)
void render_battery(uint16_t *fb, int stride, float frac, int state, float clock);
/* the battery page (2026-09-24): a panel over the live tank - the battery
 * large with its percent, the state in words, time left / time to full,
 * then since the cable moved, screen-on time on this charge, what a full
 * charge lasts (battery_info fills bi) and the voltage, dim. Any tap closes
 * it (the platforms); it closes itself after a while. */
void render_battery_info(uint16_t *fb, int stride, const bat_info_t *bi, float clock);
/* an announcement over the live tank (notice.h: a milestone the moment it
 * is earned, a stage reached, low battery), in the milestones page's modal
 * style; frac_left (1 -> 0) is its remaining time, drawn as a thin bar */
void render_notice(const tank_t *t, uint16_t *fb, int stride, int kind, int fish, uint32_t bit, float frac_left);

/* Milestones page (separate screen, never on the tank; 2026-09-13 redesign
 * on Strato's pixel-art badges): one row per fish - its sprite at its real
 * size, its name, a growth strip fry -> elder - and six event badges; the
 * tank's row below with the population strip and six tank badges. A locked
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
 * the tank's six, the fry checklist's gates, or, from a fish's name, the
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
       MS_TAP_SHOP = 4 };                                                          /* the sand dollar left of the TANK row opens the shop */
int  render_milestones_tap(const tank_t *t, float x, float y);
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
#define RENDER_CONFIRM_X     UI(56)
#define RENDER_CONFIRM_Y     UI(76)
#define RENDER_CONFIRM_W     UI(336)
#define RENDER_CONFIRM_H     UI(216)
#define RENDER_CONFIRM_BTN_W UI(132)
#define RENDER_CONFIRM_BTN_H UI(56)
#define RENDER_CONFIRM_BTN_Y (RENDER_CONFIRM_Y + UI(112))
#define RENDER_CONFIRM_NO_X  (RENDER_CONFIRM_X + UI(24))
#define RENDER_CONFIRM_YES_X (RENDER_CONFIRM_X + RENDER_CONFIRM_W - UI(24) - RENDER_CONFIRM_BTN_W)
void render_confirm_reset(uint16_t *fb, int stride, float frac);
int  render_confirm_hit(float x, float y);

/* Settings page (2026-09-15; the brightness row left the milestones page
 * for it): BRIGHTNESS 30 / 60 / 100 % and VOLUME OFF / QUIET / NORMAL as
 * segment buttons - tap the one you want - then LIGHTS OUT MANUAL / AUTO
 * (the keeper's double-tap on the glass - the default - or the idle rule)
 * with the idle time
 * under it as one number (swipe it up or down to step the seconds, or tap
 * its chevrons; LIGHT_IDLE_S shows by default), and a CLOSE button bottom
 * right (back to the milestones page, 2026-09-16 - the platform's job).
 * The platform feeds render_settings_touch EVERY FRAME while the page is up
 * (x, y, finger down), as it feeds setup_touch: it classifies taps and the
 * wheel's drags, applies the light settings to the tank itself (and marks
 * the save), and returns what happened: SET_TAP_BRIGHT with *value = the
 * percent, SET_TAP_VOLUME 0..2 (those two are the platform's to apply),
 * SET_TAP_LIGHT (*value 1 = AUTO, the idle rule; 0 = MANUAL, the double-tap),
 * SET_TAP_IDLE (*value = the seconds now set), SET_TAP_CLOSE, or nothing.
 * render_settings_tap is the bare hit test (tests). */
enum { SET_TAP_NONE = 0, SET_TAP_CLOSE = 1, SET_TAP_BRIGHT = 2, SET_TAP_VOLUME = 3, SET_TAP_LIGHT = 4, SET_TAP_IDLE = 5,
       SET_TAP_FLIP = 6, SET_TAP_FACE = 7 };
/* The SCREEN row (UPRIGHT / FLIPPED), on a board with no IMU to turn the
 * picture itself - the CYD. SET_TAP_FLIP carries *value 1 = FLIPPED; the
 * platform turns the display and touch, keeps the choice, and says what it
 * is here so the row shows it. */
void render_settings_set_flip(bool flipped);
/* Once an IMU answers, it turns the picture itself and the SCREEN row has
 * nothing to choose: the same row becomes FACE DOWN, SLEEP / IGNORE - the
 * face-down gesture's switch. SET_TAP_FACE carries *value 1 = SLEEP. The
 * platform says whether an IMU answered and what the switch is. */
void render_settings_set_imu(bool imu, bool face_sleep);
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
/* an adult fish facing right at (x,y), body length ~42 x size px, tail
 * swimming on `clock` - the setup's live preview of a colour choice */
void render_fish_preview(uint16_t *fb, int stride, float x, float y, float size,
                         uint32_t body, uint32_t fin, uint32_t accent, float clock);
/* the same, but AS THE FISH IS: its own stage (a fry shows no markings yet,
 * an elder its long tail), calm and fed - the birth flow's portrait */
void render_fish_portrait(uint16_t *fb, int stride, float x, float y, float size, const fish_t *who, float clock);

#endif
