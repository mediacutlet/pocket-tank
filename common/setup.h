/* setup.h — first-run setup (2026-09-13): the welcome, a name and colours for
 * each of the two fry, then the care tips - for a fresh install and after a
 * reset. (0.4: each fry's name, then its FORM and COLORS - THE EDITOR below.) Platform-agnostic: the state machine, the gestures, the hit
 * geometry and the drawing all live here (the reset prompt kept its state in
 * each platform's touch code; a five-page flow would have been written
 * twice), built on render.h's UI primitives over the LIVE tank - the fish
 * keep swimming behind the pages and wear the colours as they are picked.
 *
 * The platforms do three things: setup_begin when progression says the flow
 * is pending (boot, a reset YES, the director's cue); setup_touch every
 * frame with the finger's position and state while it is up (taps and the
 * letter-wheel drags are classified here); and render_setup after
 * render_tank while setup_active. Every other gesture is swallowed while it
 * is up. No timeout: it waits for the keeper.
 *
 * PLACE THE BUBBLES (2026-09-13, Strato's idea): right after the welcome,
 * over the live tank - a finger anywhere on the water drags the bubble
 * column to that x (tank_set_bubble_x keeps it clear of the grass corner the fish rest by); a translucent stripe marks the column, the airstone at its foot
 * moves with it, the bubbles already rising shift as one.
 *
 * Naming (third design, Strato: "seeing the fish while naming it is very
 * important" - and a keyboard's keys are too small for a 35 mm glass): NO
 * panel - the tank stays in view with a ring around the fish being named -
 * and seven big letter slots across the middle, arcade high-score style.
 * Touch a slot to pick it, then drag up / down on it (or tap the chevrons
 * above and below) to spin its letter through blank, A..Z. A blank at the
 * end shortens the name; NEXT trims blanks and puts the preset's name back
 * if nothing is left. Targets are a slot wide (44 px) and the whole band
 * tall, so nothing here needs aim.
 *
 * THE BIRTH FLOW (2026-09-14, Strato: an arrival "will be an exciting event
 * for the player"): when progression says a fry is owed its welcome
 * (progression_newborn), setup_poll_birth opens three pages over the live
 * tank - A NEW FRY! (the announcement: the fry ringed wherever it hatched,
 * its parents named), NAME THE NEW FRY (the letter wheel, as in the first
 * run) and the family page (a panel: the fry's portrait on its own, as it
 * is now; whose body and whose markings it wears; its boldness and
 * sociability on bars between its parents' marks). No colour picker - a
 * fry's look is its family's (tank_add_fish). DONE clears the debt and
 * saves the name; a cancel (director `setup off`, sim S) leaves it owed, so
 * it returns at the next boot. */
/* PLACE A PIECE (2026-09-16, Strato: a bought plant or decoration "should
 * allow the player to place the piece wherever they like ... similar to the
 * bubble column placement experience", plus a z-layer): one page over the
 * live tank, opened by the platform right after a purchase (and by the
 * shop's MOVE button for a piece already in the tank). A finger on the
 * water drags the piece to that x (tank_decor_set clamps it inside the
 * window); a BACK / MIDDLE / FRONT row under the title picks its depth -
 * the tank redraws live, so the fish and the grass show the choice at
 * once; DONE saves. A translucent stripe marks the piece's footprint, a
 * chevron at its foot. */
#ifndef POCKET_TANK_SETUP_H
#define POCKET_TANK_SETUP_H
#include "tank.h"
#include "render.h"            /* PAGE_*: the page the layout below is on */
#include <stdint.h>
#include <stdbool.h>

void setup_begin(tank_t *t);             /* page 1 (needs 2 fish; fewer = done at once) */
void setup_begin_birth(tank_t *t, int slot);   /* the birth flow for the fry in `slot` */
/* platforms, every frame while no prompt owns the glass: opens the birth
 * flow once per arrival owed (never over a flow already up); returns the
 * slot it opened for, or -1 */
int  setup_poll_birth(tank_t *t);
/* a fry's welcome is up, or owed and about to open (it is born mid-frame,
 * the flow opens after): the notices wait for it (2026-09-29). A welcome the
 * director dropped is not due - it returns at the next boot. Nor is one
 * waiting for the light (0.4.2: setup_poll_birth opens no welcome in a dark
 * tank, tank_light_off - a bought fry's apart): the notices the dark still
 * shows are not held all night behind it. */
bool setup_birth_due(const tank_t *t);
bool setup_welcome_waits_light(const tank_t *t);   /* (0.4.3 audit r2, R2-L13) a fry is owed its welcome and it waits for the light: the light
                                                      as the keeper set it is off (a page holding it on is not the keeper's light) and
                                                      the fry was not bought - the lab's tube and the shop's modals say LIGHT ON */
const char *setup_lab_pour_word(const tank_t *t);  /* the lab tube's button words (drawn, and read by the selftests) */
void setup_begin_place(tank_t *t, int item);   /* the placement page for SD item `item` (placeable ones only) */
/* RENAME A FISH (2026-10-01, Strato: "when you tap a fish and see its card
 * there should be a change name button"): the letter wheel again, one page
 * over the live tank with the fish ringed on its stage - the name page of
 * the first run and the birth flow, CANCEL / DONE on top. DONE keeps the
 * name and saves it (nothing left = the preset's again); CANCEL, or a flow
 * dropped from outside, puts the old one back. The milestones page's fish
 * card opens it (MS_TAP_RENAME); when it closes, setup_take_renamed gives
 * the platform the fish once (else -1) so it can put that card back up. */
void setup_begin_rename(tank_t *t, int fish);
bool setup_is_rename(void);
/* A FISH'S STORY (0.4.2, Discord #ideas: "a little stat sheet" with its age, size, parents and original owner; "these
 * are perfect already" of the welcome's FAMILY and NATURE pages): MORE on the fish's card (MS_TAP_ABOUT) opens three
 * pages about it - ITS STORY (AGE, FROM, PARENTS, MEALS, SIZE, TRUST), then the FAMILY page and the NATURE page as its
 * welcome had them. BACK / NEXT at the foot, DONE on the last; BACK on the first or DONE hands the card back the way a
 * rename does (setup_take_renamed); dropped from outside (setup_cancel), there is no card to go back to. */
void setup_begin_about(tank_t *t, int fish);
/* THE PARAGON'S REVEAL (0.4.3): the page for a fish whose three parts are rolled (tank_evo_ready): the shells spin and
   land one by one the first time, then CHOOSE ONE - a tile to try it on, KEEP THIS asked twice, LATER keeps the same
   three. Closed, the fish's card comes back (setup_take_renamed) */
void setup_begin_evolve(tank_t *t, int fish);
void setup_begin_evolve_tank(tank_t *t, int fish);   /* ... opened from the tank (a tap on its READY TO EVOLVE card, or on its stat card): closed, the tank again */
bool setup_is_evolve(void);
bool setup_holds_notices(const tank_t *t);
/* (the director's bench, 0.4.3: the reveal's frame rate page by page) the page of the reveal that is up: -1 none,
   SETUP_EVO_SHELLS (nothing spinning yet / between tiles), _SPIN (a tile flicking), _CHOOSE (CHOOSE ONE), _KEEP (KEEP asked) */
enum { SETUP_EVO_SHELLS, SETUP_EVO_SPIN, SETUP_EVO_CHOOSE, SETUP_EVO_KEEP, SETUP_EVO_PHASES };
int  setup_evo_phase(const tank_t *t);
/* op: "spin" the reveal from its start again; "loop" arg 1/0: it starts again a second after the last tile lands (a window
   of nothing but the reveal); "pick" arg: the tile tried on; "keep": KEEP THIS asked; "back": the ask taken back */
bool setup_debug_evo(tank_t *t, const char *op, int arg);
float setup_evo_bar(const tank_t *t);     /* KEEP?'s HOLD bar as drawn, 0..1 (-1: no reveal up) - read by the selftests */
/* THE REVEAL KEPT (render.h render_evo_held): the reveal's pages are a page of their own - no tank drawn under them */
#define SETUP_EVO_BANDS 4
bool setup_evo_black(void);               /* the reveal is up: the platform draws render_evo_held and no tank */
uint32_t setup_evo_key(const tank_t *t);  /* everything its STILL part is drawn from (a change: drawn whole) */
int  setup_evo_bands(const tank_t *t, int box[SETUP_EVO_BANDS][4]);   /* its moving boxes, page coords incl. */
void setup_evo_bands_draw(const tank_t *t, uint16_t *fb, int stride, double clock);   /* ... cleared and drawn again (and its sounds) */
void setup_debug_evo_live(bool on);       /* (the director's before / after) the reveal over the live tank, whole every frame, as it was built */
bool setup_is_about(void);
void setup_debug_stamp(const char *l1, const char *l2, float deg);   /* the starter fish's stamp words and turn (l1 NULL: none - a test) */
bool setup_about_band(const tank_t *t, int box[4]);          /* THE PANEL KEPT (0.4.2): MORE's moving strip, page coords incl. (false: none) */
void setup_about_band_draw(const tank_t *t, uint16_t *fb, int stride, double clock);   /* ... cleared and its fish drawn again */
uint32_t setup_about_key(const tank_t *t);                   /* ... all the still page is drawn from */
#define SETUP_STAMP_IN_X 68             /* its centre from the panel's top left */
#define SETUP_STAMP_IN_Y 62
int  setup_take_renamed(void);
/* NAME A CRITTER (0.4.2): the same page for the snail, urchin, crab or seahorse (k = CRIT_NAME_*; one the tank owns),
 * opened by its card's NAME / RENAME (render_critter_name_hit) - "NAME YOUR SNAIL" the first time, "RENAME YOUR SNAIL"
 * after. The critter stays where it is, ringed; its card's art sits on a plate where a fish would stand. DONE keeps the
 * name, tidied (nothing left = no name) and saves it; CANCEL, or a flow dropped from outside, puts the old one back.
 * setup_crit is the critter the page is about (-1 = none); when it closes, setup_take_crit_renamed gives the platform
 * the critter once (else -1) so it can put that critter's card back up (render_card_of_crit_name). setup_fish is -1 */
void setup_begin_crit_rename(tank_t *t, int k);
/* SAND DOLLAR GIFTS (0.4.2, docs/release-0.4.2.md): the link HUD's coin by an open door brings this up - the letter wheel's
 * page with three digits on it (SEND SAND DOLLARS, CANCEL | NEXT, the balance and the neighbor's badge above the dial),
 * then a confirm page (SEND N SAND DOLLARS? BACK | SEND). NEXT is lit only for 1..the balance. SEND closes it and leaves the
 * amount for the platform (setup_take_sdgift: once; 0 = nothing sent - CANCEL, a page taken away). `edge` is the door's
 * (LINK_LEFT / LINK_RIGHT), `badge` the neighbor's */
void setup_begin_sdgift(tank_t *t, int edge, int badge);
bool setup_is_sdgift(void);
int  setup_take_sdgift(int *edge);
int  setup_sdgift_amount(void);              /* the dial now (tests, the director) */
void setup_sdgift_set(int amount);           /* ... set it (tests, the director, the sim's shots) */
bool setup_sdgift_confirming(void);          /* the confirm page is up */
int  setup_crit(void);
int  setup_take_crit_renamed(void);
bool setup_active(void);
bool setup_is_birth(void);               /* the birth flow, not the first run */
bool setup_is_place(void);               /* the placement page */
bool setup_place_live(void);             /* ... and its piece has been changed on it (dragged, re-ranked, re-depthed, recolored): it is drawn live, out of the scene */
int  setup_item(void);                   /* the placement page's SD item (-1 = none) */
int  setup_fish(void);                   /* the fish the current page is about (-1 = none) */
void setup_cancel(tank_t *t);            /* drop the pages; the save still says pending */
/* the name page's two REJECTED designs of 2026-09-13, kept to be shown (the
 * episode 6 shoot; director `kbd`): 1 = the first cut, a 7 x 4 grid of 46 x 34
 * px keys on a panel with BACK / NEXT under it ("rarely" hittable, DEL kept
 * landing on NEXT); 2 = the second, 5 x 3 keys of 66 x 74 px over half the
 * alphabet, a corner key flipping A-M / N-Z, the nav on top (hittable, but the
 * panel still hid the fish). 0 = the letter wheel, the design that shipped.
 * Not saved: a boot is always the wheel. */
enum { SETUP_KBD_WHEEL, SETUP_KBD_GRID, SETUP_KBD_PAGES };
void setup_set_keyboard(int mode);
int  setup_keyboard(void);
int  setup_stretch_held(void);           /* BODY / FINS (0.4.3): the handle a drag holds (0 none; tests, the sim's shots) */
int  setup_page(void);                   /* SETUP_PG_* while active */
/* the finger, every frame (or poll): x,y in tank coordinates, down = touching.
 * A press then release on one element taps it; a press anywhere on the letter
 * wheel and a vertical drag spins the focused letter (SETUP_SPIN_PX of travel
 * per step; a press on a letter picks it first, unless THE LETTER HOLD keeps it). */
void setup_touch(tank_t *t, float x, float y, bool down);
/* THE WHEEL'S FEEL (0.4.2, setup.c): a drag from anywhere on the dial turns the focused letter once it has moved
 * slop_px up or down; for hold_ms after the finger last changed a letter, a press on the row stays on that letter if it
 * lands within reach_px of it (999 = the whole row). -1 = keep. Not saved (director `wheel`). */
void setup_wheel_tune(int hold_ms, int reach_px, int slop_px);
void setup_wheel_tuning(int *hold_ms, int *reach_px, int *slop_px);
/* the pieces, for tests and the touch log: the element under (x,y) on the
 * current page (0 = none) and a tap on it */
int  setup_hit(float x, float y);
void setup_activate(tank_t *t, int id);
const char *setup_hit_name(int id);      /* "NEXT", "slot 2", "up", "body 3"... */
int  setup_slot(void);                   /* the active letter slot on a name page */
void render_setup(const tank_t *t, uint16_t *fb, int stride, double clock);

/* pages, in order */
/* (0.4, Strato: design a fish before naming it - its colors, then its form,
 * then its name once its chosen form is in view - both fish, then the tank:
 * the bubble column, then the care tips) */
/* (0.4.3 item 10: each fish is BODY, FINS, COLORS, NAME - you build the fish, then paint it. BODY and FINS took the
   FORM page's place: eleven pages, two more than before) */
enum { SETUP_PG_WELCOME, SETUP_PG_BODY_A, SETUP_PG_FINS_A, SETUP_PG_COLORS_A, SETUP_PG_NAME_A,
       SETUP_PG_BODY_B, SETUP_PG_FINS_B, SETUP_PG_COLORS_B, SETUP_PG_NAME_B, SETUP_PG_BUBBLES, SETUP_PG_CARE, SETUP_PG_N };
/* the birth flow's pages, in order (numbered past the first run's) */
enum { SETUP_PG_BORN = SETUP_PG_N, SETUP_PG_NAME_NEW, SETUP_PG_FAMILY, SETUP_PG_NATURE, SETUP_PG_BIRTH_END };
#define SETUP_BIRTH_PAGES (SETUP_PG_BIRTH_END - SETUP_PG_BORN)
/* the placement page (its own one-page flow, numbered past the birth flow's) */
enum { SETUP_PG_PLACE = SETUP_PG_BIRTH_END };
/* the rename page (one page too) */
enum { SETUP_PG_RENAME = SETUP_PG_PLACE + 1 };
/* a fish's story (0.4.2): its own first page, then the birth flow's FAMILY and NATURE */
enum { SETUP_PG_ABOUT = SETUP_PG_RENAME + 1 };
/* THE FISH LAB (0.4.3 item 10, docs/fish-creator-0.4.3.md section 9; progression.h SD_PRICE_LAB): the creator's pages
 * again for a fry made to order - BODY, FINS, COLORS, NAME as they are, then THE TUBE: the fry in its corked tube beside
 * what it will grow into and its recipe, and POUR IT IN (the price said on the button; TANK FULL or the sand dollars
 * short, plainly, when it cannot be). Then the lab closes and the fry drops into the tank from the top (tank_plop) - in:
 * paid for, named, welcomed. BACK on the first page leaves with nothing spent. The pages work on a fish that is not in
 * the tank: slot SETUP_LAB_SLOT, past every fish a tank with room can hold. */
enum { SETUP_PG_LAB_BODY = SETUP_PG_ABOUT + 1, SETUP_PG_LAB_FINS, SETUP_PG_LAB_COLORS, SETUP_PG_LAB_NAME, SETUP_PG_LAB_TUBE };
#define SETUP_LAB_PAGES  5
#define SETUP_LAB_SLOT   (N_FISH_MAX - 1)
#define SETUP_LAB_BTN_X  LAB_COL2_X       /* POUR IT IN: from the right column's edge to LAB_R, standing on the bench's top line, */
#define SETUP_LAB_BTN_Y  LAB_BENCH_Y      /* as tall as the bench's top (its corner clears the round glass) */
#define SETUP_LAB_BTN_W  (LAB_R - LAB_COL2_X)
#define SETUP_LAB_BACK_W 128              /* the tube page's BACK: the rack's own width, over it */
#define SETUP_LAB_BACK_Y 36
#define SETUP_HIT_POUR   191
void setup_begin_lab(tank_t *t);
bool setup_is_lab(void);
fish_t *setup_lab_fish(void);             /* the fry being made (NULL: the lab is not open): the tests, the director */
/* THE LAB KEPT (render.h render_lab_page): the lab's pages are black pages of their own, not over the tank */
#define SETUP_LAB_BANDS 2
bool setup_lab_black(void);
bool setup_zoomed(void);                  /* (0.4.3, the big screens) the page up is drawn at twice its size: the paragon's, THE FISH LAB's */               /* a lab page is up (not the pour): the platform draws render_lab_page and no tank */
#ifdef TANK_P4
/* THE REVEAL KEPT AT 2X (render.c): the page's own frame at 1x (TANK_W stride, NULL: no room), put on the glass whole
   or a box at a time (page px, inclusive; `glass` gets the box on the glass it covered, x1 < x0 when none) */
uint16_t *setup_zoom_buf(void);
void setup_zoom_put(uint16_t *fb, int stride);
void setup_zoom_put_box(uint16_t *fb, int stride, int x0, int y0, int x1, int y1, short glass[4]);
void setup_debug_zoom_buf(uint16_t *b);   /* (a test's reference drawn through this frame instead; NULL: the held one) */
#endif
uint32_t setup_lab_key(const tank_t *t);  /* everything the page's STILL part is drawn from (a change: drawn whole) */
int  setup_lab_bands(const tank_t *t, int box[SETUP_LAB_BANDS][4]);   /* its moving boxes, page coords incl. (0: none - drawn whole every frame) */
void setup_lab_bands_draw(const tank_t *t, uint16_t *fb, int stride, double clock);   /* ... cleared and drawn again */
void setup_lab_goto(tank_t *t, int page); /* (the director, the tests) the lab on SETUP_PG_LAB_BODY .. _TUBE */
void setup_debug_lab_live(bool on);       /* (the director's before / after) the lab drawn as it was first built: over the live tank, whole every frame */
void setup_debug_lab_wiggle(bool on);     /* ... and a handle dragged to and fro by itself (BODY: the length; FINS: the tail) */
#define SETUP_ABOUT_PAGES 3
/* element ids (setup_hit / setup_activate) */
#define SETUP_HIT_NEXT  1
#define SETUP_HIT_BACK  2
#define SETUP_HIT_SELL  5                /* the placement page's SELL (top left): once arms it, twice sells the piece */
#define SETUP_HIT_BCK   6                /* the placement page's order arrows (2026-10-17): one step back in the stack ... */
#define SETUP_HIT_FWD   7                /* ... and one step forward (tank_decor_shift) */
#define SETUP_HIT_SHELVE 8               /* the placement page's SHELVE (0.3.9, beside SELL): one tap puts the piece on the shelf and closes the page */
#define SETUP_HIT_LOCK   9               /* the placement page's LOCK (0.3.9, between SHELVE and DONE): a toggle - LOCKED, a held finger passes the piece by */
#define SETUP_HIT_UP    3                /* the active slot's letter: next (A -> B) */
#define SETUP_HIT_DOWN  4                /* ... previous */
#define SETUP_HIT_SLOT0 10               /* + 0..FISH_NAME_MAX-1: pick that slot */
#define SETUP_HIT_BODY0 40               /* + swatch 0..LOOK_N-1 */
#define SETUP_HIT_ROLL  6                /* the editor's ROLL: the page's genes at random */
#define SETUP_HIT_SWAP  7                /* COLORS: trade the body and markings colors */
#define SETUP_HIT_ACC0  160              /* the editor: + LOOK_ACCENT swatch 0..LOOK_N-1 (MARKINGS) */
#define SETUP_HIT_SHAPE0 170             /* + SHAPE_* */
#define SETUP_HIT_FINS0 175              /* + FINS_* */
#define SETUP_HIT_STAGE 190              /* BODY / FINS (0.4.3): the stage - a drag there stretches, a tap does nothing */
#define SETUP_HIT_PAT0  180              /* + PATTERN_* */
#define SETUP_HIT_KEY0  100              /* legacy keyboards only: + 0..25 = A..Z, + SETUP_KEY_DEL, + SETUP_KEY_PAGE */
#define SETUP_KEY_DEL   26
#define SETUP_KEY_PAGE  27
#define SETUP_KEY_N     28
#define SETUP_HIT_Z0    60               /* + DECOR_Z_BACK..FRONT: the placement page's layer row */
#define SETUP_HIT_ORD0  64               /* + 0 BEHIND / 1 IN FRONT: the sword plant's ORDER SQUARES (0.4) - behind or in front of the piece they show */
#define SETUP_HIT_SG0   66               /* + 0 BEHIND / 1 IN FRONT: the sword plant's SEAGRASS squares (0.4.1) - behind or in front of the seagrass */
#define SETUP_HIT_COLOR0 80              /* + 0..CORAL_N-1: the coral's colour swatches (its placement page) */

/* geometry (tank coordinates), shared by the drawing, the hit test and the
 * sim's selftest. The panelled pages (welcome, colours, care) sit inside the
 * bezel curve (x 32..416, y 16..342); the name page draws straight on the
 * tank. */
#define SETUP_X 32
#define SETUP_Y 16
#define SETUP_W 384
#define SETUP_H 326
#define SETUP_BTN_W 110
#define SETUP_BTN_H 42
#define SETUP_BTN_Y (SETUP_Y + SETUP_H - 12 - SETUP_BTN_H)   /* welcome / care: the foot */
#define SETUP_BACK_X (SETUP_X + 16)
#define SETUP_NEXT_X (SETUP_X + SETUP_W - 16 - SETUP_BTN_W)
#define SETUP_MID_X  (SETUP_X + (SETUP_W - SETUP_BTN_W) / 2)
#define SETUP_TOP_BTN_W 84                                 /* name / look: the top row */
#define SETUP_TOP_BTN_Y (SETUP_Y + 24)
#define SETUP_TOP_BACK_X (SETUP_X + 12)
#define SETUP_TOP_NEXT_X (SETUP_X + SETUP_W - 12 - SETUP_TOP_BTN_W)
/* the placement page's top row (0.3.9): FOUR buttons - SELL | SHELVE | LOCK |
 * DONE - a little narrower than the other pages' two (78 px at a 16 px gap
 * fills the same 360 px, so SELL and DONE keep their corners and the row
 * stays inside every glass, the round one included). SHELVE: Strato, "a held
 * piece opens this page, so the shelf must be here"; LOCK: his ask, "a space
 * between shelve and done" for it. */
#define SETUP_PL_BTN_W 78
#define SETUP_PL_GAP   16
#define SETUP_PL_X(i)  (SETUP_TOP_BACK_X + (i) * (SETUP_PL_BTN_W + SETUP_PL_GAP))   /* 0 SELL, 1 SHELVE, 2 LOCK, 3 DONE */
#define SETUP_TOP_SHELVE_X SETUP_PL_X(1)
#define SETUP_TOP_LOCK_X   SETUP_PL_X(2)
#define SETUP_TOP_DONE_X   SETUP_PL_X(3)                   /* the placement page's DONE (the other pages' NEXT sits at SETUP_TOP_NEXT_X) */
#define SETUP_ORD_W      36                                 /* the order arrows (the placement page): at the two ends of the DEPTH row since 0.3.9
                                                             * (SHELVE took their place on the top row); a two-depth piece's tiles leave the
                                                             * room, the plant has no stack and shows none */
#define SETUP_ORD_X0     SETUP_DEPTH_X
#define SETUP_ORD_X1     (SETUP_DEPTH_X + SETUP_DEPTH_W - SETUP_ORD_W)
#define SETUP_ORD_Y      (SETUP_DEPTH_Y + (SETUP_DEPTH_H - SETUP_BTN_H) / 2)
/* the letter wheel: FISH_NAME_MAX slots of a 6x font (30 x 42 px glyphs) at
 * a 44 px pitch across the middle of the tank, chevrons 40 px above and
 * below the active one; a press in the row band picks the nearest slot, the
 * bands above / below it are the chevrons' */
#define SETUP_SLOT_SCALE 6
#define SETUP_SLOT_PX  44
#define SETUP_SLOT_W   (5 * SETUP_SLOT_SCALE)
#define SETUP_SLOT_H   (7 * SETUP_SLOT_SCALE)
#define SETUP_SLOT_X   ((PAGE_W - ((FISH_NAME_MAX - 1) * SETUP_SLOT_PX + SETUP_SLOT_W)) / 2)
#define SETUP_SLOT_Y   170              /* (150 until 0.4: the band above now shows a young fish's GROWN UP beside it) */
#define SETUP_ARROW_GAP 40
#define SETUP_SPIN_PX   30                                 /* drag travel per letter */
/* THE EDITOR (0.4, docs/genetics-0.4.md; Spore's creature editor in a
 * pocket): per fish, NAME, then FORM (body SHAPE, FINS), then COLORS (BODY,
 * PATTERN, MARKINGS) - over the live tank, BACK / ROLL / NEXT on top. The
 * band under them shows the fry as it is NOW, on stage at the left (the live
 * fish, wearing its body and shape), and the GROWN fish it becomes at the
 * right (fins come in when it is grown, markings as it grows). Under the
 * band the rows: three picture tiles for a form (each a small fish of that
 * option in the fish's own colors), eight swatches for a color. Each row's
 * band runs from its label to halfway into the gap below, the nearest
 * choice along x - no dead space. ROLL picks the page's genes at random. */
#define SETUP_SW_N  LOOK_N              /* nine since 0.4 (charcoal): a wider row, 44 px apart, 40 wide - more
                                       swatch in all than eight at 46 / 42; the bowl's circle is narrower
                                       at the markings row, so there 40 / 36 */
#define SETUP_SW_PX 40                  /* (0.4.3, THE LAB'S GRID: nine swatches from LAB_L to LAB_R on every glass) */
#define SETUP_SW_W  38
#define SETUP_SW_X  ((PAGE_W - (SETUP_SW_N - 1) * SETUP_SW_PX - SETUP_SW_W) / 2)
#define SETUP_ROLL_X ((PAGE_W - SETUP_TOP_BTN_W) / 2)
/* COLORS: SWAP (body <-> markings, Strato 10-04) is a tall button at the
   pattern row's right end, between the two palettes, an arrow pointing up at
   the body row and one down at the markings row; the pattern tiles narrow
   to make its room */
#define SETUP_PAT_TILE_W  98            /* (0.4.3, THE LAB'S GRID: the three tiles and SWAP span LAB_L .. LAB_R) */
#define SETUP_PAT_TILE_PX 106
#define SETUP_SWAP_W      40
#define SETUP_SWAP_GAP    10
#define SETUP_PAT_TILE_X  ((PAGE_W - (2 * SETUP_PAT_TILE_PX + SETUP_PAT_TILE_W + SETUP_SWAP_GAP + SETUP_SWAP_W)) / 2)
#define SETUP_SWAP_X      (SETUP_PAT_TILE_X + 2 * SETUP_PAT_TILE_PX + SETUP_PAT_TILE_W + SETUP_SWAP_GAP)
#define SETUP_SWAP_Y      (SETUP_COLOR_BODY_Y + SETUP_COLOR_SW_H + 8)
#define SETUP_SWAP_H      (SETUP_COLOR_ACC_Y - SETUP_LABEL_DY - 4 - SETUP_SWAP_Y)
#define SETUP_ED_NOW_X      136          /* the fry's stage (page x) ... */
#define SETUP_ED_GROWN_X    312          /* ... and the grown portrait (the pair centred on the page's middle) */
#define SETUP_ED_PREVIEW_Y  110
#define SETUP_ED_CAPTION_Y  140          /* NOW / GROWN UP */
#define SETUP_ED_GROWN_SIZE 1.5f
#define SETUP_TILE_N   3
#define SETUP_TILE_W   112              /* (0.4.3, THE LAB'S GRID: three tiles from LAB_L to LAB_R) */
#define SETUP_TILE_PX  124
#define SETUP_TILE_X   ((PAGE_W - (SETUP_TILE_N - 1) * SETUP_TILE_PX - SETUP_TILE_W) / 2)
/* BODY and FINS (0.4.3 item 10, docs/fish-creator-0.4.3.md): the stage (render.h STG_*) between the top buttons and one
 * row of three named tiles. The fry (NOW) rides at the stage's left, small, in the body being made. A drag anywhere on
 * the stage changes a value: its first SETUP_ST_PICK px choose the direction (sideways: the length, or the tail; up and
 * down: the height, or the top fin from above the fish's middle and the bottom fin from below), SETUP_ST_TRAVEL px is a
 * value's whole range, and a handle sticks SETUP_ST_STICK px either side of a named value. No hold, no wait: the fish
 * follows from the moment the direction is known. The tiles' row was lifted clear of the round glass's corners. */
#define SETUP_ST_NOW_X    64
#define SETUP_ST_NOW_Y    116
#define SETUP_ST_TOP      92
#define SETUP_ST_BOT      266
#define SETUP_ST_HINT_Y   276
#define SETUP_ST_TILE_Y   LAB_BENCH_Y   /* the tiles stand on the bench's top line and are as tall as its top */
#define SETUP_ST_TILE_H   LAB_BENCH_H
#define SETUP_ST_PICK     8.0f
#define SETUP_ST_TRAVEL   120.0f
#define SETUP_ST_STICK    5.0f
#define SETUP_LABEL_DY 16                /* a row's label sits this far above it */
#define SETUP_FORM_TILE_H 56             /* FORM: two rows of named tiles */
#define SETUP_FORM_SHAPE_Y 174
#define SETUP_FORM_FINS_Y  256
#define SETUP_COLOR_SW_H   32            /* COLORS: swatches, tiles, swatches */
#define SETUP_COLOR_PAT_H  44
#define SETUP_COLOR_BODY_Y 174
#define SETUP_COLOR_PAT_Y  230
#define SETUP_COLOR_ACC_Y  LAB_BENCH_Y   /* (the last row stands on the bench's top line) */
/* the family page (birth flow; 0.4 heredity): the fry's name; NOW (the fry,
 * ringed) and the GROWN fish it becomes side by side, as in the editor; then
 * a row per gene - SHAPE, FINS, PATTERN (the option's name), BODY, MARKINGS
 * (a swatch) - and, under FROM, where it came from: a parent's name in its
 * body color, a * when it was that parent's hidden copy (a footnote says
 * so), or NEW! for a mutation. The nature page after it (DONE): BOLD /
 * SOCIAL - the fry's bar, the parents' ticks (heads above / below, TP2-9) - and the
 * PARENTS legend. BACK + NEXT / DONE at the foot of both. */
#define SETUP_FAM_PORTRAIT_Y (SETUP_Y + 82)
#define SETUP_FAM_NOW_X      (SETUP_X + SETUP_W / 2 - 90)
#define SETUP_FAM_GROWN_X    (SETUP_X + SETUP_W / 2 + 80)
#define SETUP_FAM_GROWN_SIZE 1.3f
#define SETUP_FAM_ROW_Y      (SETUP_Y + 134)
#define SETUP_FAM_ROW_DY     24
#define SETUP_FAM_LABEL_X    (SETUP_X + 28)
#define SETUP_FAM_VALUE_X    (SETUP_X + 136)
#define SETUP_FAM_FROM_X     (SETUP_X + 248)
#define SETUP_FAM_NOTE_Y     (SETUP_FAM_ROW_Y + 5 * SETUP_FAM_ROW_DY + 2)
#define SETUP_NAT_ROW_Y      (SETUP_Y + 100)   /* the nature page's rows */
#define SETUP_AB_PORTRAIT_Y  (SETUP_Y + 84)    /* ITS STORY (0.4.2): the fish as it is, under its name ... */
#define SETUP_AB_ROW_Y       (SETUP_Y + 128)   /* ... then a row each: AGE, FROM, PARENTS, MEALS, SIZE, TRUST */
#define SETUP_AB_ROW_DY      24
#define SETUP_NAT_ROW_DY     34
#define SETUP_NAT_HEAD_H     7                 /* a parent's mark's head (TP2-9): 7 rows, 13 px at its widest ... */
#define SETUP_NAT_KEY_W      13                /* ... (the key before a name under PARENTS: the same head) */
#define SETUP_NAT_HEAD_A_Y(by) ((by) - 11)     /* the first parent's head above the bar (by = its top), widest row first */
#define SETUP_NAT_HEAD_B_Y(by) ((by) + SETUP_FAM_BAR_H + 4)   /* the other's below it, widest row last */
#define SETUP_FAM_BAR_W      200
#define SETUP_FAM_BAR_H      8
/* the placement page's DEPTH control (second design, 2026-09-16 - Strato:
 * "initially i thought 'back' button referred to menu navigation .. the
 * z-layer targeting and buttons are a bit ambiguous"): ONE outlined bar of
 * three joined segments under a DEPTH caption - the settings page's idiom,
 * a setting with three values, not three buttons - each segment a picture
 * tile (two small leaves and the keeper's first fish, drawn in that order:
 * the fish over the leaves, woven between them, behind them) with BEHIND /
 * AMONG / IN FRONT under it; a hint line under the bar says what to watch
 * for ("THE FISH SWIM THROUGH IT"). DONE stays alone top right in the go
 * teal, the bar in the calm ink, so "choose" and "finish" read apart. The
 * water below SETUP_PLACE_Y is the drag zone. */
#define SETUP_DEPTH_SEG_W  110
#define SETUP_DEPTH_W      (DECOR_Z_N * SETUP_DEPTH_SEG_W)      /* the plant's three segments; the castle's two (BEHIND / IN
                                                             * FRONT, Strato: no AMONG) are centred the same way - setup.c
                                                             * sizes the bar from tank_decor_z_count */
#define SETUP_DEPTH_X      ((PAGE_W - SETUP_DEPTH_W) / 2)
#define SETUP_DEPTH_Y      (SETUP_TOP_BTN_Y + SETUP_BTN_H + 24)
#define SETUP_DEPTH_H      70
#define SETUP_DEPTH_TILE_H 40
#define SETUP_DEPTH_HINT_Y (SETUP_DEPTH_Y + SETUP_DEPTH_H + 8)
#define SETUP_ORDSQ_X      ((PAGE_W - 2 * SETUP_DEPTH_SEG_W) / 2)   /* the sword plant's ORDER SQUARES (0.4): two segments in the DEPTH bar's place -
                                                                 BEHIND | IN FRONT of the piece beside it in the stack, a universal block for it */
/* the sword plant's page since 0.4.1 (Strato 11-05: BEHIND THE SEAGRASS back): TWO rows of two squares, each under a caption that
   names what they are behind or in front of - THE SEAGRASS in the DEPTH bar's place, then, with a piece beside the plant, the
   ORDER SQUARES (THE PUMPKIN) under it, the arrows at their ends (alone, the arrows stay at the first row's, dim) */
#define SETUP_ORDSQ2_Y     (SETUP_DEPTH_Y + SETUP_DEPTH_H + 30)
#define SETUP_ORD2_Y       (SETUP_ORDSQ2_Y + (SETUP_DEPTH_H - SETUP_BTN_H) / 2)
#define SETUP_PLACE_Y      (SETUP_DEPTH_HINT_Y + 20)
#define SETUP_PLACE_PLANT2_Y (SETUP_ORDSQ2_Y + SETUP_DEPTH_H + 6)   /* (the plant's page with its second row: the drag zone and the stripe start under it) */
/* the coral's placement page (2026-09-23) trades the hint line for a COLOR
 * row: CORAL_N swatches at the colour page's pitch, shorter (42 x 26), the
 * chosen one ringed; the coral on the floor below wears the pick at once.
 * The drag zone starts under the row. */
#define SETUP_COL_N   CORAL_N
#define SETUP_COL_PX  46               /* (the editor's swatches' old pitch - the editor's moved to nine in 0.4) */
#define SETUP_COL_W   42
#define SETUP_COL_H   26
#define SETUP_COL_X   ((PAGE_W - (SETUP_COL_N - 1) * SETUP_COL_PX - SETUP_COL_W) / 2)
#define SETUP_COL_Y   (SETUP_DEPTH_Y + SETUP_DEPTH_H + 22)
#define SETUP_PLACE_CORAL_Y (SETUP_COL_Y + SETUP_COL_H + 8)
/* the reef cluster's page (2026-09-24): a LOOK row of CLUSTER_SCHEME_N tiles
 * in the COLOR row's place, each three bands (the coral, the tubes, the
 * brain) over the look's name; SETUP_HIT_COLOR0 + i picks it */
#define SETUP_LOOK_N   CLUSTER_SCHEME_N
#define SETUP_LOOK_W   110
#define SETUP_LOOK_PX  118
#define SETUP_LOOK_H   36
#define SETUP_LOOK_X   ((PAGE_W - (SETUP_LOOK_N - 1) * SETUP_LOOK_PX - SETUP_LOOK_W) / 2)
#define SETUP_LOOK_Y   SETUP_COL_Y
#define SETUP_PLACE_CLUSTER_Y (SETUP_LOOK_Y + SETUP_LOOK_H + 8)
/* the stage: the clear spot each page leaves for the fish being edited
 * (tank_t.stage_*), top centre between the buttons */
#define SETUP_STAGE_X   (TANK_W / 2)                    /* the stage is in the TANK: the frame's coordinates, under the page's title */
#define SETUP_STAGE_NAME_Y (PAGE_Y + 104)
/* a name page for a fish not yet grown (0.4): it is staged at NOW, the left,
   and its GROWN UP form sits on a plate at the right, as in the editor - the
   keeper names the fish they designed, fins and markings in view */
#define SETUP_NAME_PREVIEW_Y   106
#define SETUP_NAME_GROWN_SIZE  1.3f
#define SETUP_STAGE_EDIT_X (PAGE_X + SETUP_ED_NOW_X)   /* the editor's NOW */
#define SETUP_STAGE_EDIT_Y (PAGE_Y + SETUP_ED_PREVIEW_Y)
#define SETUP_STAGE_ST_X   (PAGE_X + SETUP_ST_NOW_X)   /* BODY / FINS: the fry's place */
#define SETUP_STAGE_ST_Y   (PAGE_Y + SETUP_ST_NOW_Y)
void setup_fish_removed(tank_t *t, int idx);   /* a fish left, the slots shifted: the page follows its fish (or closes) */

/* the words the birth flow says of a fry (0.4.1; the tests read them): its announcement's two lines (FROM THE SHOP, /
   BORN TO A AND B - DOWN IN THE GRASS, or NEXT DOOR. for a fry born next door and come home), and where its gene g came
   from on the family page (a parent's name, THE SHOP, NEW!, or "?" for a parent nobody here knows) */
void setup_born_lines(const tank_t *t, int fish, char *l1, size_t n1, const char **l2);
const char *setup_from_name(const tank_t *t, int fish, int g);

#ifdef TANK_P4
void setup_p4_band_wide(bool on);   /* the big screens: the flow's bands across the glass again (the bench: director `p4 2048`) */
#endif
#ifdef TANK_P4
/* THE BIG SCREENS' KEEPER PAGE (render_big.c): a fish's STORY and TRAITS in the page's pane (setup.c; frame px) */
void setup_big_story(const tank_t *t, int fi, uint16_t *fb, int stride, int x, int y, int dy);
void setup_big_traits(const tank_t *t, int fi, uint16_t *fb, int stride, int x, int y, int dy, int yn, int dyn);
#endif
#endif
