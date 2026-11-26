/* setup.c — first-run setup flow (see setup.h). */
#include "collection.h"
#include "setup.h"
#include "sdgift.h"
#include "tank_events.h"
#include "render.h"
#include "progression.h"
#include "touch_gate.h"
#include "passport.h"
#include "shelf.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#ifdef TANK_P4
#define ZM_S 2
#define ZM_OX ((TANK_W - PAGE_W * ZM_S) / 2)
#define ZM_OY ((TANK_H - PAGE_H * ZM_S) / 2)
#endif
#ifdef TANK_P4
/* THE BIG SCREENS' BANDS ARE AS WIDE AS THE PAGE (2026-10-13), a margin each side, not across 1280 px of glass.
   A band is blended over the live tank every frame, and there its cost follows its width: across the glass the
   colour page ran at 12.8 fps, the name page at 15.8 (docs/board-p4-10.1.md, "What an overlay costs"). Strato, with
   both to choose from: "i think this is the move and i think it would look better too." The pocket tanks' bands stay
   across their glass, exactly as before. Director `p4 2048` is the old band across the glass, for the bench. */
#define SU_BAND_MARGIN 24
static bool s_band_wide;
void setup_p4_band_wide(bool on) { s_band_wide = on; }
#define SU_LEFT   (s_band_wide ? -PAGE_X : -SU_BAND_MARGIN)
#define SU_FULL_W (s_band_wide ? TANK_W : PAGE_W + 2 * SU_BAND_MARGIN)
#else
#define SU_LEFT   (-PAGE_X)       /* the glass's left edge, in page units ... */
#define SU_FULL_W TANK_W          /* ... and its whole width: the bands across the tank */
#endif

/* the reset prompt's palette: ink panel, teal edge, calm / lit buttons */
#define C_PANEL  0x04141a
#define C_EDGE   0x9fd8e2
#define C_INNER  0x1c2f36
#define C_DIM    0x2a3f45
#define C_KEY    0x0e2229
#define C_TEXT   0xffffff
#define C_CAPT   0x9fd8e2
#define C_GO     0x155e58      /* BEGIN: the teal of a charging battery */
#define C_GO_E   0x38dcc7
#define C_FILL   0x5f8a92      /* the family page's trait bars: a muted teal the parents' ticks stand out on */
#define C_NEW    0xffd166      /* a mutation (0.4): gold - NEW!, the announcement's line and ring */

static bool s_active;
static bool s_birth;           /* the birth flow (three pages about s_fish), not the first run */
static bool s_place;           /* the placement page (one page about s_item) */
static bool s_moved;           /* ... and its piece has been dragged, re-ranked, re-depthed or recolored on it: drawn LIVE until the page
                                  closes (render_tank). Until then it stays in the scene as it was - opening a page, reading it and DONE
                                  cost nothing (0.3.9.4, Strato on the square: a page up dropped the frame rate "to a crawl") */
static bool s_about;
static const char *s_stamp_l1 = "STARTER", *s_stamp_l2 = "FISH";   /* a starter fish's stamp (0.4.2; setup_debug_stamp: a test draws without it) */
static float s_stamp_deg = 35;
void setup_debug_stamp(const char *l1, const char *l2, float deg) { s_stamp_l1 = l1; s_stamp_l2 = l2; s_stamp_deg = deg; }
static double s_about_at;                               /* the tank clock when MORE's pages came, or turned (setup_touch's spent window) */           /* a fish's story (0.4.2): ITS STORY, FAMILY, NATURE about s_fish, from its card */
static bool s_rename;          /* the rename page (one page about s_fish: the letter wheel, CANCEL / DONE) */
static char s_old_name[FISH_NAME_MAX + 1];   /* ... the name it came in with (CANCEL puts it back) */
static int  s_renamed;         /* a rename page just closed: the fish's slot + 1, until the platform takes it */
static int  s_crit = -1;       /* the rename page is about a critter (0.4.2): its CRIT_NAME_*, -1 = a fish (s_fish) */
static int  s_sdg;             /* SAND DOLLAR GIFTS (0.4.2): 0 = off, 1 = the dial, 2 = the confirm page */
static bool s_evo;             /* THE PARAGON'S REVEAL (0.4.3): the page is up (s_fish: whose) */
static double s_evo_at;        /* the tank clock when it opened */
static double s_evo_step;      /* ... when it opened, or its KEEP? question came or went: THE TOUCH RULE's stamp (touch_gate.h) - a press
                                  within TAP_W of it is the double of the tap that brought it (0.4.3 audit r1 TP-1, TP-2, TP-6) */
static double s_step_at = -1e9;   /* the tank clock when a tap turned a setup page (the lab's, the naming's): the same stamp (TP-4);
                                     each flow begins with none */
/* KEEP?'s YES is a HOLD (0.4.3 audit r2, R2-L32; Strato 11-23: "a short hold for keep this is a nice solution"): a part is kept
   for life, and a mash of taps (any rate) answered the question unread. The finger held on YES for EVO_KEEP_HOLD_S - the
   letter wheel's hold, 500 ms, Strato's pick on the 1.8 - keeps it, as the reset prompt's HOLD TO ERASE: only polls that saw
   the finger on YES add (each at most EVO_HOLD_STEP_S), off YES or lifted it empties and nothing is lost. A press from
   before the question, or within TAP_W of it (THE TOUCH RULE), is no hold. NOT YET stays a tap */
#define EVO_KEEP_HOLD_S 0.5f
#define EVO_HOLD_STEP_S 0.1f
#define EVO_WAIT_FILL 0.9f    /* ... its bar while the keep waits (the doorway): just short of the end - a full bar is a keep (R3b-L11) */
static float  s_evo_hold = -1;  /* this press, held on YES: seconds (-1: no hold running) */
static double s_evo_hold_at;    /* ... the tank clock of the last poll that added */
static float s_evo_open[3], s_evo_land[3];   /* each tile: when its shell opens and when it lands, seconds after s_evo_at */
static int8_t s_evo_sel; static bool s_evo_confirm;
static bool s_evo_loop, s_evo_live;  /* (the director's bench: the reveal again and again; drawn as first built - both end with the page) */
static bool s_evo_back;        /* opened from the keeper page's fish card: closed, that card comes back (else the tank) */
static int  s_evo_tick[3]; static bool s_evo_chimed[3];
static int  s_sdg_dig[3], s_sdg_edge, s_sdg_badge, s_sdg_sent;   /* the dial's digits (hundreds first), the door, its badge, SEND's amount */
static int  s_crit_renamed;    /* a critter's rename page just closed: its CRIT_NAME_* + 1, until the platform takes it */
static int  s_item;            /* placement: the SD item being placed */
static int  s_ref = -1;        /* placement: the piece the ORDER SQUARES show (the sword plant's page, 0.4) - the one it last passed, -1 = the default */
static int  order_ref(const tank_t *t);   /* (below, with the squares) */
static int  s_fish;            /* birth flow: the newborn's slot */
static int  s_birth_offered = -1;   /* setup_poll_birth: the arrival already opened for (-1 = none since boot) */
static int  s_page;
static int  s_slot;            /* name pages: the slot the wheel turns */
/* the finger (setup_touch) */
static bool  s_down;
static float s_px, s_py;       /* press point */
static float s_ly;             /* last y, for the wheel */
static float s_acc;            /* vertical travel toward the next step */
static bool  s_spun;           /* this press has turned the wheel (or moved the column): no tap on release */
/* THE WHEEL'S FEEL (0.4.2; a keeper: "tapping the next letter when trying to adjust the current one, or changing the
 * current one when trying to go to the next"). Two rules, shared by every wheel on these pages (wheel_t below):
 * - DRAG ANYWHERE: a press anywhere in the dial - the letter, the chevrons, just above or below - becomes a drag once it
 *   has moved s_wh_slop px up or down; a drag never taps. Only a press that lifts (nearly) where it began is a tap.
 * - THE LETTER HOLD: for s_wh_hold_ms after the finger last CHANGED a letter, a press on the row cannot move to another
 *   letter - it goes to the one being turned (a drag turns it, a tap does nothing). s_wh_reach px: how far into each
 *   neighbour the turned letter reaches while held (WHEEL_REACH_ROW = the whole row; less = a press squarely on another
 *   letter still picks it at once). The CURRENT letter answers at once, always; the hold never restarts on a press it
 *   kept, only on a real change. Not saved: director `wheel` tunes all three on the glass. */
#define WHEEL_NEVER     (-1e18)
#define WHEEL_REACH_ROW 999
static int    s_wh_hold_ms = 500;    /* 0 = no hold */
static int    s_wh_reach = WHEEL_REACH_ROW;
static int    s_wh_slop = 10;
static int    s_wh;                  /* this press: SETUP_HIT_SLOT0 + i / _UP / _DOWN on a wheel, 0 = not the wheel */
static bool   s_wh_held;             /* ... a press on another letter the hold kept on the turned one: no pick, no tap */
static double s_wh_changed = WHEEL_NEVER;   /* the tank clock when the finger last changed a letter */
static bool  s_sell_armed;     /* the placement page's SELL tapped once (2026-09-24) */
static double s_press_clock;   /* the tank clock at setup_touch's press (0.4.1 audit round 2, R2-1): SELL's wait is read there */
static bool   s_tap_press;     /* ... named for the release's setup_activate (a direct call has none: its press is its lift) */
static double s_sell_clock;    /* ... when: a second tap within SETUP_SELL_WAIT_S is the same press doubled, not the yes (0.4, the
                                  11-02 sweep: a double-tap sold the piece - after the pumpkin leaves the shop, for good) */
#define SETUP_SELL_WAIT_S TAP_W_S   /* as the fish card's SELL (render.c MSP_SELL_WAIT_S) */
static int   s_same_note;      /* the editor (0.4): frames left of "SAME AS ITS BODY" after a tap on the struck-out swatch */
/* the legacy keyboards (setup.h, SETUP_KBD_*) */
static int  s_kbd;             /* the name page's design: the wheel unless the director says otherwise */
static bool s_typed;           /* this name page: the keeper has typed (the default is gone) */
static int  s_kb_page;         /* SETUP_KBD_PAGES: 0 = A-M, 1 = N-Z */

static bool s_lab;             /* THE FISH LAB (0.4.3): the creator's pages about the fish in SETUP_LAB_SLOT, then the tube */
static fish_t s_lab_fish;      /* ... the fish itself (lab_in / lab_out) */
static bool s_lab_live, s_lab_wiggle;                 /* (the director's before / after, setup.h) */
static uint32_t s_lab_key_last; static double s_lab_still;   /* THE LAB KEPT: the still part's key, and the moment it was last new (both buffers draw that one moment) */
static int page_fish(void) { return s_lab ? SETUP_LAB_SLOT : s_birth || s_rename || s_about ? s_fish : s_place ? 0 : s_page <= SETUP_PG_NAME_A ? 0 : 1; }
/* the name a name page turns: the fish's, or the critter's (0.4.2) - both FISH_NAME_MAX + 1 bytes, lowercase inside */
static char *page_name(tank_t *t) { return s_crit >= 0 ? t->crit_name[s_crit] : t->fish[page_fish()].name; }
static const char *page_name_c(const tank_t *t) { return s_crit >= 0 ? t->crit_name[s_crit] : t->fish[page_fish()].name; }
static bool page_is_name(void) { return s_page == SETUP_PG_NAME_A || s_page == SETUP_PG_NAME_B || s_page == SETUP_PG_NAME_NEW || s_page == SETUP_PG_RENAME || s_page == SETUP_PG_LAB_NAME; }
static bool page_is_body(void) { return s_page == SETUP_PG_BODY_A || s_page == SETUP_PG_BODY_B || s_page == SETUP_PG_LAB_BODY; }
static bool page_is_fins(void) { return s_page == SETUP_PG_FINS_A || s_page == SETUP_PG_FINS_B || s_page == SETUP_PG_LAB_FINS; }
static bool page_is_stretch(void) { return page_is_body() || page_is_fins(); }   /* (0.4.3 item 10) the stage and its handles */
static bool page_is_colors(void) { return s_page == SETUP_PG_COLORS_A || s_page == SETUP_PG_COLORS_B || s_page == SETUP_PG_LAB_COLORS; }
static bool page_is_look(void) { return page_is_stretch() || page_is_colors(); }   /* the editor's three pages */
static bool page_is_last(void) { return s_page == SETUP_PG_CARE || s_page == SETUP_PG_NATURE; }
static bool page_one_button(void) { return s_page == SETUP_PG_WELCOME || s_page == SETUP_PG_BORN; }   /* a lone NEXT at the foot */
static int  first_page(void) { return s_lab ? SETUP_PG_LAB_BODY : s_about ? SETUP_PG_ABOUT : s_rename ? SETUP_PG_RENAME : s_place ? SETUP_PG_PLACE : s_birth ? SETUP_PG_BORN : SETUP_PG_WELCOME; }
static bool nav_on_top(void) { return (page_is_name() && s_kbd != SETUP_KBD_GRID) || page_is_look() || s_page == SETUP_PG_BUBBLES || s_page == SETUP_PG_PLACE; }
static bool s_plant_two;                  /* the sword plant's page has its second row (a piece beside it) - as last drawn, for the hit test */
static int  place_y(void) { return s_item == 3 ? SETUP_PLACE_CORAL_Y : s_item == 4 ? SETUP_PLACE_CLUSTER_Y : s_item == 0 && s_plant_two ? SETUP_PLACE_PLANT2_Y : SETUP_PLACE_Y; }   /* where the drag zone starts */
/* a fish's parent w (0 = parent_uid[0] / parent_a, 1 = the other), wherever it
   lives (2026-10-06): one of ours by its ID - away or not - else a fish of
   another tank by name (kin: a visitor, a cross-tank fry's other parent),
   else "?". The slot index alone named a parent from next door "?" */
typedef struct { const char *name; uint32_t color; float bold, sociable; bool known; } parent_t;
static parent_t parent_of(const tank_t *t, const fish_t *f, int w) {
    parent_t p = { "?", C_DIM, 0, 0, false };
    uint32_t uid = f->parent_uid[w & 1];
    int slot = w ? f->parent_b : f->parent_a;
    const fish_t *q = slot >= 0 && slot < t->n_fish ? &t->fish[slot] : NULL;
    if (!q && uid) for (int i = 0; i < t->n_fish && !q; i++) if (t->fish[i].uid == uid) q = &t->fish[i];
    if (q) { p.name = q->name; p.color = q->color; p.bold = q->bold; p.sociable = q->sociable; p.known = true; return p; }
    const kin_t *k = kin_find(uid);
    if (k) { p.name = k->name; p.color = k->color; p.bold = k->bold; p.sociable = k->sociable; p.known = true; }
    return p;
}
/* where a fry came from (0.4.1): a fry from the shop is marked so at the sale (MS_BOUGHT, audit P10 - no parent slot
   and no ID was the test, and a fish born here whose parents were both sold before 0.4 has neither: its FAMILY said THE
   SHOP; with no parent known it reads "?", as 0.4.0). A fry born next door and come home (a cross-tank fry,
   MS_CROSSBRED) has two parent IDs and no slot here (passport_to_fish gives none); its parents are named, wherever they live */
static bool from_shop(const fish_t *f) { return (f->ms_bits & MS_BOUGHT) != 0; }
/* where a fish with no parents came from, in two words: the shop's fry, or one made in THE FISH LAB (0.4.3: its FROM says so) */
static const char *shop_word(const tank_t *t, const fish_t *f) { return progression_from_lab(t, (int)(f - t->fish)) ? "THE LAB" : "THE SHOP"; }
static bool founder(const fish_t *f) { return !(f->ms_bits & MS_BOUGHT) && !f->parent_uid[0] && !f->parent_uid[1] && f->parent_a < 0 && f->parent_b < 0; }
static bool born_next_door(const fish_t *f) { return (f->ms_bits & MS_CROSSBRED) && f->parent_a < 0 && f->parent_b < 0 && !from_shop(f); }
void setup_born_lines(const tank_t *t, int fish, char *l1, size_t n1, const char **l2) {
    const fish_t *f = &t->fish[fish];
    bool shop = from_shop(f);                                         /* bought (0.3.5): no parents, in at the surface */
    if (shop) snprintf(l1, n1, "FROM %s,", shop_word(t, f));
    else snprintf(l1, n1, "BORN TO %s AND %s", parent_of(t, f, 0).name, parent_of(t, f, 1).name);
    *l2 = shop ? "IN AT THE SURFACE." : born_next_door(f) ? "NEXT DOOR." : "DOWN IN THE GRASS.";
}
const char *setup_from_name(const tank_t *t, int fish, int g) {
    const fish_t *f = &t->fish[fish];
    if (f->mutated == g + 1) return "NEW!";
    if (from_shop(f)) return shop_word(t, f);                              /* (0.4.1: was "?" on every row) */
    return parent_of(t, f, (f->from_b >> g & 1) ? 1 : 0).name;
}
/* a mutation in words for the announcement (0.4): "SOMETHING NEW: SAIL FINS";
   false = none */
static bool mutation_words(const fish_t *f, char *out, size_t n) {
    if (!f->mutated || f->mutated > GENE_N) return false;
    static const char *const PAT[PATTERN_N] = { "BARS", "A MASK", "AN EYE BAND", "NO MARKINGS" };
    switch (f->mutated - 1) {
    case GENE_SHAPE:   snprintf(out, n, "SOMETHING NEW: A %s BODY", SHAPE_NAMES[f->shape]); break;
    case GENE_FINS:    snprintf(out, n, "SOMETHING NEW: %s FINS", FINS_NAMES[f->fins]); break;
    case GENE_PATTERN: snprintf(out, n, "SOMETHING NEW: %s", PAT[f->pattern < PATTERN_N ? f->pattern : 0]); break;
    case GENE_BODY:    snprintf(out, n, "SOMETHING NEW: ITS BODY COLOR"); break;
    default:           snprintf(out, n, "SOMETHING NEW: ITS MARKINGS"); break;
    }
    return true;
}
static void stage(tank_t *t);
static void sdg_end(tank_t *t, int sent);
static void evo_end(tank_t *t);
static void tidy_name(tank_t *t, int fish);
/* a name page whose fish is not grown yet shows its GROWN UP form beside it */
static bool name_beside(const tank_t *t) { int f = page_fish(); return s_lab || (f >= 0 && f < t->n_fish && t->fish[f].stage < STAGE_ADULT); }

void setup_begin(tank_t *t) {
    if (t->n_fish < 2) { progression_setup_done(t); s_active = false; return; }   /* nothing to name */
    s_sdg = 0; s_evo = false; s_evo_loop = false; s_evo_live = false; s_step_at = -1e9; s_active = true; s_lab = false; s_birth = false; s_place = false; s_rename = false; s_about = false; s_fish = -1; s_page = SETUP_PG_WELCOME; s_slot = 0; s_wh_changed = WHEEL_NEVER; s_down = false;
    s_crit = -1;
    tank_emit(TEV_WELCOME, -1);
}
void setup_begin_birth(tank_t *t, int slot) {
    if (slot < 0 || slot >= t->n_fish) { s_active = false; return; }
    s_sdg = 0; s_evo = false; s_evo_loop = false; s_evo_live = false; s_step_at = -1e9; s_active = true; s_lab = false; s_birth = true; s_place = false; s_rename = false; s_about = false; s_fish = slot; s_page = SETUP_PG_BORN; s_slot = 0; s_wh_changed = WHEEL_NEVER; s_down = false;
    s_crit = -1;
}
void setup_begin_rename(tank_t *t, int fish) {
    if (fish < 0 || fish >= t->n_fish || s_active) return;
    s_sdg = 0; s_evo = false; s_evo_loop = false; s_evo_live = false; s_step_at = -1e9; s_active = true; s_lab = false; s_birth = false; s_place = false; s_rename = true; s_about = false; s_fish = fish; s_page = SETUP_PG_RENAME; s_slot = 0; s_wh_changed = WHEEL_NEVER; s_down = false;
    s_typed = false; s_kb_page = 0; s_renamed = 0;
    s_crit = -1; s_crit_renamed = 0;
    memcpy(s_old_name, t->fish[fish].name, sizeof s_old_name);
    stage(t);
}
/* NAME / RENAME A CRITTER (0.4.2): the same page about tank_t.crit_name[k] - the critter stays where it is, ringed */
void setup_begin_crit_rename(tank_t *t, int k) {
    if (k < 0 || k >= CRIT_NAME_N || s_active || !(t->sd_unlocks & tank_crit_name_item(k))) return;
    s_sdg = 0; s_evo = false; s_evo_loop = false; s_evo_live = false; s_step_at = -1e9; s_active = true; s_lab = false; s_birth = false; s_place = false; s_rename = true; s_about = false; s_fish = -1; s_page = SETUP_PG_RENAME; s_slot = 0; s_wh_changed = WHEEL_NEVER; s_down = false;
    s_typed = false; s_kb_page = 0; s_renamed = 0;
    s_crit = k; s_crit_renamed = 0;
    memcpy(s_old_name, t->crit_name[k], sizeof s_old_name);
    stage(t);
}
bool setup_is_rename(void) { return s_active && s_rename; }
void setup_begin_about(tank_t *t, int fish) {
    if (fish < 0 || fish >= t->n_fish || s_active) return;
    s_sdg = 0; s_evo = false; s_evo_loop = false; s_evo_live = false; s_step_at = -1e9; s_active = true; s_lab = false; s_birth = false; s_place = false; s_rename = false; s_about = true; s_fish = fish; s_page = SETUP_PG_ABOUT; s_slot = 0; s_down = false;
    s_crit = -1; s_renamed = 0; s_about_at = t->clock;
    stage(t);
}
bool setup_is_about(void) { return s_active && s_about; }
static void about_end(tank_t *t) {                      /* BACK on the first page, DONE on the last: the card back up */
    s_renamed = s_fish >= 0 && s_fish < t->n_fish ? s_fish + 1 : 0;
    s_active = false; s_about = false; stage(t);
}
int  setup_take_renamed(void) { int f = s_renamed - 1; s_renamed = 0; return f; }
int  setup_crit(void) { return s_active && s_rename ? s_crit : -1; }
int  setup_take_crit_renamed(void) { int k = s_crit_renamed - 1; s_crit_renamed = 0; return k; }
void setup_begin_place(tank_t *t, int item) {
    if (!tank_decor_placeable(item)) return;
    s_sdg = 0; s_evo = false; s_evo_loop = false; s_evo_live = false; s_step_at = -1e9; s_active = true; s_lab = false; s_birth = false; s_place = true; s_rename = false; s_about = false; s_item = item; s_fish = -1; s_page = SETUP_PG_PLACE; s_slot = 0; s_wh_changed = WHEEL_NEVER; s_down = false;
    s_crit = -1;
    s_sell_armed = false; s_moved = false; s_ref = -1;
    s_plant_two = item == 0 && order_ref(t) >= 0;
    stage(t);
}
/* (0.4.2, the dark holds notices - Strato: "tank light off should be treated largely as a notification suppressor too")
   a fry's welcome waits for the light: its pages hold the light on, and a fry home from next door lit a dark tank at
   5 am for its NAME. Owed, never marked offered. A fry the keeper just bought in the shop does not wait: that is the
   keeper's own tap, answered at once. */
static bool birth_waits(const tank_t *t, int nb) {
    return tank_light_off(t) && !(nb >= 0 && nb < t->n_fish && (t->fish[nb].ms_bits & MS_BOUGHT));
}
const char *setup_lab_pour_word(const tank_t *t) {
    const int why = progression_lab_why(t);
    /* (R2-L13) LIGHT ON FIRST from the light the keeper set: the lab's page holds it on, so the held light never showed it, and
       the light went back off as the lab closed with the welcome still waiting for it */
    return why == LAB_OK ? "POUR IT IN" : why == LAB_FULL ? "TANK FULL" : why == LAB_WELCOME ? (setup_welcome_waits_light(t) ? "LIGHT ON FIRST" : "WELCOME FRY FIRST") : "NEEDS";
}
bool setup_welcome_waits_light(const tank_t *t) {
    const int nb = progression_newborn();
    return nb >= 0 && tank_light_off_kept(t) && !(nb < t->n_fish && (t->fish[nb].ms_bits & MS_BOUGHT));   /* (birth_waits' rule, the kept light) */
}
int setup_poll_birth(tank_t *t) {
    int nb = progression_newborn();
    if (nb < 0) { s_birth_offered = -1; return -1; }    /* nothing owed: a later arrival in the same slot is new */
    if (s_active || nb == s_birth_offered) return -1;   /* a flow is up, or this one was offered (and dropped) already */
    if (birth_waits(t, nb)) return -1;                  /* (0.4.2) dark: owed, not offered - it opens once the light is on */
    s_birth_offered = nb;
    if (nb >= t->n_fish) { progression_newborn_done(t); return -1; }   /* a stale debt (a staged tank) */
    setup_begin_birth(t, nb);
    return nb;
}
bool setup_birth_due(const tank_t *t) {                    /* a welcome up, or owed and about to open */
    int nb = progression_newborn();
    return setup_is_birth() || (nb >= 0 && nb != s_birth_offered && !birth_waits(t, nb));   /* (0.4.2: one waiting for the light
                                                              blocks nothing - LOW BATTERY still shows in the dark) */
}
bool setup_active(void) { return s_active; }
/* THE NOTICES UNDER A SETUP PAGE (0.4.3 audit r1 TP-8, TP-9b): every page here holds them - a notice that comes steps aside
   and comes up once the page is gone (notice_tick's `blocked`). One rule, the board's main.c and the sim's alike. (The board
   already held them under the lab and the reveal through ui_cover's hold_light, which the audit's harness left out; it did
   not under MORE's pages - no hold_light there, no notice drawn over them: one came and went unseen. Now it waits.) */
bool setup_holds_notices(const tank_t *t) { return s_active || setup_birth_due(t); }
bool setup_is_birth(void) { return s_active && s_birth; }
bool setup_is_place(void) { return s_active && s_place; }
bool setup_place_live(void) { return s_active && s_place && s_moved; }
int  setup_item(void)     { return s_active && s_place ? s_item : -1; }
int  setup_fish(void) { return s_active && (page_is_name() || page_is_look() || s_birth) ? page_fish() : -1; }
/* the rename page closes: DONE keeps the wheel's name (tidied; nothing left =
   the preset's) and saves it, anything else puts the old one back. Either
   way the platform is owed the way back to the fish's card (setup_take_renamed). */
static void rename_end(tank_t *t, bool keep) {
    if (s_crit >= 0) {                                  /* a critter's (0.4.2): DONE tidies it (nothing left = no name), CANCEL the old one */
        tank_set_crit_name(t, s_crit, keep ? t->crit_name[s_crit] : s_old_name);
        if (keep) progression_save(t);
        s_crit_renamed = s_crit + 1; s_crit = -1;
    } else if (s_fish >= 0 && s_fish < t->n_fish) {
        if (keep) { tidy_name(t, s_fish); progression_save(t); }
        else tank_set_name(t, s_fish, s_old_name);
        s_renamed = s_fish + 1;
    }
    s_active = false; s_rename = false; stage(t);
}
static void stage(tank_t *t) {                          /* who is on stage, and where */
    if (s_active && s_crit < 0 && !s_lab && (page_is_name() || page_is_look())) {   /* (a critter's name page: it stays where it is; the lab's fry is not in the tank) */
        t->stage_fish = (int8_t)page_fish();
        bool beside = !page_is_name() || name_beside(t);    /* NOW at the left, GROWN UP at the right */
        t->stage_x = beside ? SETUP_STAGE_EDIT_X : SETUP_STAGE_X;
        t->stage_y = !page_is_name() ? SETUP_STAGE_EDIT_Y : beside ? PAGE_Y + SETUP_NAME_PREVIEW_Y : SETUP_STAGE_NAME_Y;
        if (page_is_stretch()) { t->stage_x = SETUP_STAGE_ST_X; t->stage_y = SETUP_STAGE_ST_Y; }
    } else t->stage_fish = -1;
}
void setup_cancel(tank_t *t) {
    if (s_active && s_sdg) { sdg_end(t, 0); return; }   /* (nothing sent) */
    if (s_active && s_evo) { evo_end(t); s_renamed = 0; return; }   /* (its three wait as they are) */
    if (s_active && s_rename) { rename_end(t, false); s_renamed = 0; s_crit_renamed = 0; return; }   /* the old name back, no card to return to */
    if (s_active && s_place) progression_save(t);       /* the piece stays where it was dragged */
    s_active = false; s_about = false; s_lab = false; stage(t);   /* (the lab left mid-way, even mid-pour: nothing poured, nothing spent) */
}
/* a fish left and the slots shifted (progression: a sale, a gift): the page about a fish follows it; a page about the one
   that left closes - its rename's CANCEL never names another fish (the 11-02 sweep) */
void setup_fish_removed(tank_t *t, int idx) {
    if (s_birth_offered == idx) s_birth_offered = -1; else if (s_birth_offered > idx) s_birth_offered--;   /* (0.4.2: a fish may leave with a welcome owed) */
    if (!s_active || !(s_birth || s_rename || s_about || s_evo) || s_fish < 0) return;
    if (s_fish == idx) { s_active = false; s_birth = false; s_rename = false; s_about = false; s_evo = false; s_renamed = 0; s_fish = -1; stage(t); return; }
    if (s_fish > idx) { s_fish--; stage(t); }
}
int  setup_page(void)   { return s_page; }
int  setup_slot(void)   { return s_slot; }
void setup_set_keyboard(int mode) { s_kbd = mode == SETUP_KBD_GRID || mode == SETUP_KBD_PAGES ? mode : SETUP_KBD_WHEEL; s_typed = false; s_kb_page = 0; }
int  setup_keyboard(void) { return s_kbd; }

/* ---- the legacy keyboards: geometry and hit test as they were on 2026-09-13 ----
 * GRID: A-Z in 7 columns at a 50 x 38 px pitch, the last row V..Z + a
 * double-width DEL. PAGES: 5 x 3 cells at 72 x 80, the page's 13 letters,
 * then DEL, then the flip in the corner. Both: a tap anywhere over the block
 * picks the NEAREST key. */
#define KG_COLS 7
#define KG_X  (SETUP_X + 17)
#define KG_Y  (SETUP_Y + 116)
#define KG_PX 50
#define KG_PY 38
#define KG_W  46
#define KG_H  34
#define KP_COLS 5
#define KP_ROWS 3
#define KP_PX 72
#define KP_PY 80
#define KP_W  66
#define KP_H  74
#define KP_X  (SETUP_X + (SETUP_W - (KP_COLS - 1) * KP_PX - KP_W) / 2)
#define KP_Y  (SETUP_Y + 76)
#define KP_HALF 13
static int kg_row(int k) { return k == SETUP_KEY_DEL ? 3 : k / KG_COLS; }
static int kg_col(int k) { return k == SETUP_KEY_DEL ? 5 : k % KG_COLS; }
static int kg_w(int k)   { return k == SETUP_KEY_DEL ? KG_W + KG_PX : KG_W; }
static void kg_center(int k, float *x, float *y) {
    *x = KG_X + kg_col(k) * KG_PX + kg_w(k) * 0.5f;
    *y = KG_Y + kg_row(k) * KG_PY + KG_H * 0.5f;
}
static int kp_key(int cell) { return cell < KP_HALF ? cell + s_kb_page * KP_HALF : cell == KP_HALF ? SETUP_KEY_DEL : SETUP_KEY_PAGE; }
static int kbd_hit(float x, float y) {
    int best = -1; float bd = 1e9f;
    if (s_kbd == SETUP_KBD_GRID) {
        int kw = KG_COLS * KG_PX - (KG_PX - KG_W), kh = 4 * KG_PY - (KG_PY - KG_H);
        if (x < KG_X - 6 || x >= KG_X + kw + 6 || y < KG_Y - 6 || y >= KG_Y + kh + 6) return 0;
        for (int k = 0; k <= SETUP_KEY_DEL; k++) {
            float kx, ky; kg_center(k, &kx, &ky);
            float dx = x - kx, dy = (y - ky) * 1.3f;    /* rows are tighter than columns */
            if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = k; }
        }
    } else {
        int kw = (KP_COLS - 1) * KP_PX + KP_W, kh = (KP_ROWS - 1) * KP_PY + KP_H;
        if (x < KP_X - 16 || x >= KP_X + kw + 16 || y < KP_Y - 16 || y >= KP_Y + kh + 16) return 0;
        for (int cell = 0; cell < KP_COLS * KP_ROWS; cell++) {
            float dx = x - (KP_X + (cell % KP_COLS) * KP_PX + KP_W * 0.5f), dy = y - (KP_Y + (cell / KP_COLS) * KP_PY + KP_H * 0.5f);
            if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = kp_key(cell); }
        }
    }
    return best < 0 ? 0 : SETUP_HIT_KEY0 + best;
}
static void kbd_key(char *nm, int k) {
    int n = (int)strlen(nm);
    if (k == SETUP_KEY_PAGE) { s_kb_page ^= 1; return; }
    if (k == SETUP_KEY_DEL) {
        if (!s_typed) { nm[0] = 0; s_typed = true; }                  /* the default goes in one stroke */
        else if (n) nm[n - 1] = 0;
        return;
    }
    if (!s_typed) { nm[0] = 0; n = 0; s_typed = true; }              /* first letter replaces the default */
    if (n < FISH_NAME_MAX) { nm[n] = (char)('a' + k); nm[n + 1] = 0; }   /* lowercase inside; the font draws upper */
}

const char *setup_hit_name(int id) {
    static char buf[12];
    if (id == SETUP_HIT_NEXT) return "NEXT";
    if (id == SETUP_HIT_BACK) return "BACK";
    if (id == SETUP_HIT_SELL) return "SELL";
    if (id == SETUP_HIT_ROLL) return "ROLL";
    if (id == SETUP_HIT_SWAP) return "SWAP";
    if (id >= SETUP_HIT_ACC0 && id < SETUP_HIT_ACC0 + LOOK_N) { snprintf(buf, sizeof buf, "markings %d", id - SETUP_HIT_ACC0); return buf; }
    if (id >= SETUP_HIT_SHAPE0 && id < SETUP_HIT_SHAPE0 + SHAPE_N) return SHAPE_NAMES[id - SETUP_HIT_SHAPE0];
    if (id >= SETUP_HIT_FINS0 && id < SETUP_HIT_FINS0 + FINS_N) return FINS_NAMES[id - SETUP_HIT_FINS0];
    if (id >= SETUP_HIT_PAT0 && id < SETUP_HIT_PAT0 + PATTERN_N) return PATTERN_NAMES[id - SETUP_HIT_PAT0];
    if (id == SETUP_HIT_SHELVE) return "SHELVE";
    if (id == SETUP_HIT_LOCK) return "LOCK";
    if (id == SETUP_HIT_BCK) return "SEND BACK";
    if (id == SETUP_HIT_FWD) return "BRING FORWARD";
    if (id == SETUP_HIT_ORD0) return "BEHIND (the order squares)";
    if (id == SETUP_HIT_ORD0 + 1) return "IN FRONT (the order squares)";
    if (id == SETUP_HIT_UP)   return "up";
    if (id == SETUP_HIT_DOWN) return "down";
    if (id >= SETUP_HIT_SLOT0 && id < SETUP_HIT_SLOT0 + FISH_NAME_MAX) { snprintf(buf, sizeof buf, "slot %d", id - SETUP_HIT_SLOT0); return buf; }
    if (id == SETUP_HIT_KEY0 + SETUP_KEY_DEL) return "DEL";
    if (id == SETUP_HIT_KEY0 + SETUP_KEY_PAGE) return "page flip";
    if (id >= SETUP_HIT_KEY0 && id < SETUP_HIT_KEY0 + 26) { snprintf(buf, sizeof buf, "key %c", 'A' + id - SETUP_HIT_KEY0); return buf; }
    if (id >= SETUP_HIT_BODY0 && id < SETUP_HIT_BODY0 + LOOK_N) { snprintf(buf, sizeof buf, "body %d", id - SETUP_HIT_BODY0); return buf; }
    if (id >= SETUP_HIT_Z0 && id < SETUP_HIT_Z0 + DECOR_Z_N) return id == SETUP_HIT_Z0 ? "BEHIND" : id == SETUP_HIT_Z0 + 1 ? "AMONG" : "IN FRONT";
    return "nothing";
}

/* ---- the letter wheel over a fish's name ----
 * A slot holds one of 27 values: blank, A..Z (stored lowercase). The name string carries the
 * slots up to the last letter (blanks inside it are spaces); NEXT / BACK
 * tidy it - trailing blanks dropped, inner ones closed up, nothing left =
 * the preset's name again. */
static int slot_val(const char *nm, int i) {            /* 0 = blank, 1..26 = A..Z */
    if (i >= (int)strlen(nm)) return 0;
    char ch = nm[i];
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    return ch >= 'A' && ch <= 'Z' ? ch - 'A' + 1 : 0;
}
static void slot_set(char *nm, int i, int v) {
    int n = (int)strlen(nm);
    if (v == 0) {
        if (i >= n) return;
        nm[i] = ' ';
        while (n > 0 && nm[n - 1] == ' ') nm[--n] = 0;  /* a blank at the end shortens */
        return;
    }
    while (n < i) nm[n++] = ' ';                        /* (unreachable: slots snap to <= len) */
    nm[i] = (char)('a' + v - 1);                        /* names are lowercase inside (tank_set_name) */
    if (i >= n) nm[i + 1] = 0;
}
static int name_len(const char *nm) { return (int)strlen(nm); }
static void pick_slot(const char *nm, int i) {          /* the first blank is the last pickable slot */
    int n = name_len(nm);
    if (i > n) i = n;
    if (i > FISH_NAME_MAX - 1) i = FISH_NAME_MAX - 1;
    if (i < 0) i = 0;
    s_slot = i;
}
static void spin(char *nm, int dir) {
    tank_emit(TEV_WHEEL_TICK, -1);
    if (name_len(nm) < s_slot) pick_slot(nm, s_slot);
    slot_set(nm, s_slot, (slot_val(nm, s_slot) + 27 + dir) % 27);
}
static void name_spin(tank_t *t, int dir) { spin(page_name(t), dir); }
static void name_pick(tank_t *t, int i) { pick_slot(page_name(t), i); }
static void tidy_name(tank_t *t, int fish) {
    fish_t *f = &t->fish[fish];
    char out[FISH_NAME_MAX + 1]; int n = 0;
    for (const char *p = f->name; *p; p++) if (*p != ' ' && n < FISH_NAME_MAX) out[n++] = *p;
    out[n] = 0;
    tank_set_name(t, fish, out);                        /* empty = the preset's */
}

/* a swatch that cannot be picked: a bold ink X corner to corner (the editor's
   markings the body's own color) - a shape, so it reads without color */
static void struck_out(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t under) {
    const int n = w > h ? w : h;
    uint32_t r = under >> 16 & 255, g = under >> 8 & 255, b = under & 255;
    uint32_t ink = r * 299 + g * 587 + b * 114 < 100 * 1000 ? C_CAPT : C_PANEL;   /* light on a dark swatch (charcoal), ink on the rest */
    for (int i = 2; i <= n - 3; i++) {
        int px = x + 3 + (w - 6) * i / n, py = y + 3 + (h - 6) * i / n, qy = y + h - 3 - (h - 6) * i / n;
        render_rect(fb, stride, px - 2, py - 2, 4, 4, ink);
        render_rect(fb, stride, px - 2, qy - 2, 4, 4, ink);
    }
}
/* SWAP (COLORS): a tall key between the palettes, a double-headed arrow -
   up at the body row, down at the markings row: they trade places */
static void swap_button(uint16_t *fb, int stride) {
    const int x = SETUP_SWAP_X, y = SETUP_SWAP_Y, w = SETUP_SWAP_W, h = SETUP_SWAP_H, cx = x + w / 2;
    render_rect(fb, stride, x, y, w, h, C_KEY);
    render_rect_edge(fb, stride, x, y, w, h, C_EDGE);
    const int head = 9;                                  /* each arrowhead: rows 2..18 px wide */
    for (int i = 0; i < head; i++) {
        int half = 1 + i;
        render_rect(fb, stride, cx - half, y + 6 + i, 2 * half, 1, C_TEXT);          /* up, its tip at the top */
        render_rect(fb, stride, cx - half, y + h - 7 - i, 2 * half, 1, C_TEXT);      /* down, its tip at the foot */
    }
    render_rect(fb, stride, cx - 2, y + 6 + head, 4, h - 12 - 2 * head, C_TEXT);    /* the shaft between them */
}
/* the editor's rows (setup.h, THE EDITOR): where each sits and what a pick is */
typedef struct { int y, h, x, px, w, n, hit0, gene; const char *label; } edit_row_t;
static const edit_row_t COLOR_ROWS[] = {
    { SETUP_COLOR_BODY_Y, SETUP_COLOR_SW_H,  SETUP_SW_X,   SETUP_SW_PX,   SETUP_SW_W,   LOOK_N,    SETUP_HIT_BODY0, GENE_BODY,    "BODY" },
    { SETUP_COLOR_PAT_Y,  SETUP_COLOR_PAT_H, SETUP_PAT_TILE_X, SETUP_PAT_TILE_PX, SETUP_PAT_TILE_W, PATTERN_TILES, SETUP_HIT_PAT0, GENE_PATTERN, "PATTERN" },
    { SETUP_COLOR_ACC_Y,  SETUP_COLOR_SW_H,  SETUP_SW_X,   SETUP_SW_PX,   SETUP_SW_W,   LOOK_N,    SETUP_HIT_ACC0,  GENE_ACCENT,  "MARKINGS" },
};
static int edit_rows(const edit_row_t **r) {
    *r = COLOR_ROWS; return (int)(sizeof COLOR_ROWS / sizeof *COLOR_ROWS);
}
/* the page's genes at random (ROLL): never markings that match the body */
static void edit_roll(tank_t *t, int fish) {
    const edit_row_t *r; int n = edit_rows(&r);
    for (int k = 0; k < n; k++) {
        int nv = r[k].gene == GENE_PATTERN ? PATTERN_N : r[k].n;   /* ROLL can land on SOLID too */
        int v = (int)tank_randf(t, 0, nv - 0.001f);
        if (r[k].gene == GENE_ACCENT)
            for (int guard = 0; guard < 16 && LOOK_ACCENT[v] == t->fish[fish].color; guard++) v = (int)tank_randf(t, 0, r[k].n - 0.001f);
        tank_set_gene(t, fish, r[k].gene, v);
    }
}

/* ---- BODY and FINS (0.4.3 item 10, setup.h SETUP_ST_*) ------------------------------------------------- */
static const uint8_t ST_SHAPES[SHAPE_N] = { SHAPE_SLENDER, SHAPE_CLASSIC, SHAPE_BULBOUS };   /* the BODY tiles: the range, left to right */
typedef struct { uint8_t shape, fins, body[2], finl[3]; } st_look_t;
static st_look_t s_st_undo; static bool s_st_can_undo;     /* ROLL reads UNDO until the next change: a stray tap never loses a fish someone made */
static int   s_st_axis;            /* the handle this drag has taken: 0 none yet, 1 length / tail, 2 height / top fin, 3 bottom fin */
static bool  s_st_press;           /* this press began on the stage */
static float s_st_v0, s_st_last;   /* the value where the drag took it; the last one set (the tick at a named value) */
static st_look_t st_look(const fish_t *f) { st_look_t l = { f->shape, f->fins, { f->body[0], f->body[1] }, { f->finl[0], f->finl[1], f->finl[2] } }; return l; }
static void st_wear(fish_t *f, const st_look_t *l) { f->shape = l->shape; f->fins = l->fins; memcpy(f->body, l->body, 2); memcpy(f->finl, l->finl, 3); }
static void st_roll(tank_t *t, int fish) {
    fish_t *f = &t->fish[fish];
    if (s_st_can_undo) { st_look_t now = st_look(f); st_wear(f, &s_st_undo); s_st_undo = now; s_st_can_undo = false; return; }   /* UNDO */
    s_st_undo = st_look(f);
    if (page_is_body()) {
        float len, dep;
        do { len = tank_randf(t, BODY_LEN_MIN, BODY_LEN_MAX); dep = tank_randf(t, BODY_DEP_MIN, BODY_DEP_MAX); }
        while (len * dep < BODY_AREA_MIN || len * dep > BODY_AREA_MAX);
        tank_set_body(f, len, dep);
    } else {
        tank_set_gene(t, fish, GENE_FINS, (int)tank_randf(t, 0, FINS_N - 0.001f));
        for (int k = 0; k < 3; k++) tank_set_fin_len(f, k, tank_randf(t, tank_fin_len_min(f->fins, k), FINL_MAX));
    }
    s_st_can_undo = true;
}
/* a value under a drag: where it was taken plus the travel, sticking at the named ones on the way */
static float st_dragged(float v0, float px, float lo, float hi, const float *named, int n) {
    float k = (hi - lo) / SETUP_ST_TRAVEL, v = v0 + px * k;
    for (int i = 0; i < n; i++) if (fabsf(v - named[i]) < SETUP_ST_STICK * k) v = named[i];
    return v < lo ? lo : v > hi ? hi : v;
}
static void st_drag(tank_t *t, float x, float y) {
    fish_t *f = &t->fish[page_fish()];
    float dx = x - s_px, dy = y - s_py, len, dep, fl[3];
    tank_body(f, &len, &dep); tank_fin_len(f, fl);
    if (!s_st_axis) {
        if (fabsf(dx) < SETUP_ST_PICK && fabsf(dy) < SETUP_ST_PICK) return;      /* (a tap, or a finger settling: nothing yet) */
        s_st_axis = fabsf(dx) >= fabsf(dy) ? 1 : page_is_fins() && s_py - PAGE_Y > STG_Y ? 3 : 2;
        s_st_v0 = s_st_last = page_is_body() ? (s_st_axis == 1 ? len : dep) : fl[s_st_axis - 1];
        s_px = x; s_py = y; dx = dy = 0;                 /* the fish follows from here: the value starts where it was, no jump */
        s_spun = true; s_st_can_undo = false;
    }
    float v; bool named = false;
    if (page_is_body()) {
        if (s_st_axis == 1) {                            /* the nose's handle: right is longer */
            float lo = fmaxf(BODY_LEN_MIN, BODY_AREA_MIN / dep), hi = fminf(BODY_LEN_MAX, BODY_AREA_MAX / dep);
            v = st_dragged(s_st_v0, dx, BODY_LEN_MIN, BODY_LEN_MAX, BODY_LEN, SHAPE_N); v = v < lo ? lo : v > hi ? hi : v;
            for (int i = 0; i < SHAPE_N; i++) named |= v == BODY_LEN[i];
            tank_set_body(f, v, dep);
        } else {                                         /* the back's handle: up is taller */
            float lo = fmaxf(BODY_DEP_MIN, BODY_AREA_MIN / len), hi = fminf(BODY_DEP_MAX, BODY_AREA_MAX / len);
            v = st_dragged(s_st_v0, -dy, BODY_DEP_MIN, BODY_DEP_MAX, BODY_DEP, SHAPE_N); v = v < lo ? lo : v > hi ? hi : v;
            for (int i = 0; i < SHAPE_N; i++) named |= v == BODY_DEP[i];
            tank_set_body(f, len, v);
        }
    } else {
        static const float ONE[1] = { 1 };
        const int k = s_st_axis - 1;                     /* the tail: left is longer. the top fin: up. the bottom fin: down */
        v = st_dragged(s_st_v0, k == 0 ? -dx : k == 1 ? -dy : dy, tank_fin_len_min(f->fins, k), FINL_MAX, ONE, 1);
        named = v == 1;
        tank_set_fin_len(f, k, v);
    }
    if (v != s_st_last && named) tank_emit(TEV_WHEEL_TICK, -1);   /* onto a named value: felt, so CLASSIC can be found again */
    s_st_last = v;
}
int  setup_stretch_held(void) { return s_active && page_is_stretch() ? s_st_axis : 0; }

/* ---- THE FISH LAB (0.4.3 item 10, setup.h) ------------------------------------------------------------ */
bool setup_is_lab(void) { return s_active && s_lab; }
bool setup_lab_black(void) { return s_active && s_lab && !s_lab_live; }
void setup_debug_lab_live(bool on) { s_lab_live = on; }
void setup_debug_lab_wiggle(bool on) { s_lab_wiggle = on; }
void setup_lab_goto(tank_t *t, int page) {
    if (!s_active || !s_lab || page < SETUP_PG_LAB_BODY || page > SETUP_PG_LAB_TUBE) return;
    s_page = page; s_slot = 0; s_st_can_undo = false; s_st_axis = 0; s_st_press = false; stage(t);
}
void setup_begin_lab(tank_t *t) {
    if (s_active || !progression_lab_owned(t) || t->n_fish > SETUP_LAB_SLOT) return;   /* (a full tank of five still opens it: the tube says TANK FULL) */
    fish_t *f = &t->fish[SETUP_LAB_SLOT];
    memset(f, 0, sizeof *f);
    bool used[64] = { false }; int preset = 0;           /* a name of its own to fall back on: the first one no fish here has */
    for (int i = 0; i < t->n_fish; i++) if (t->fish[i].preset >= 0 && t->fish[i].preset < 64) used[t->fish[i].preset] = true;
    while (preset < tank_roster_count() - 1 && preset < 63 && used[preset]) preset++;
    f->preset = preset; f->stage = STAGE_FRY; f->size = 0.5f; f->parent_a = f->parent_b = -1; f->pattern = PATTERN_BARS;
    tank_set_look(t, SETUP_LAB_SLOT, LOOK_BODY[0], LOOK_ACCENT[4]);
    s_sdg = 0; s_evo = false; s_step_at = -1e9; s_active = true; s_lab = true; s_birth = false; s_place = false; s_rename = false; s_about = false; s_fish = -1; s_crit = -1;
    s_page = SETUP_PG_LAB_BODY; s_slot = 0; s_wh_changed = WHEEL_NEVER; s_down = false; s_typed = false; s_kb_page = 0; s_same_note = 0;
    s_st_can_undo = false; s_st_axis = 0; s_st_press = false;
    s_lab_fish = *f;
    stage(t);
}
static void lab_end(tank_t *t) { s_active = false; s_lab = false; stage(t); }
/* the pages' fish is kept HERE; the slot is only where the pages read and write it. With five fish its slot is also
   the one a fry from next door is made in for a moment (tank_add_fish), so the slot is written again from the copy
   at every touch and frame, and the copy taken back after: nothing that passes through the slot changes the fry
   being made (the review, 11-21). A sixth fish in the slot: the lab closes, nothing spent */
fish_t *setup_lab_fish(void) { return s_active && s_lab ? &s_lab_fish : NULL; }
static bool lab_in(tank_t *t) {
    if (!s_lab) return true;
    if (t->n_fish > SETUP_LAB_SLOT) { lab_end(t); return false; }
    t->fish[SETUP_LAB_SLOT] = s_lab_fish;
    return true;
}
static void lab_out(const tank_t *t) { if (s_lab && s_active) s_lab_fish = t->fish[SETUP_LAB_SLOT]; }
/* POUR IT IN: the lab closes and the fry drops into the tank from the top (tank_plop) - or, if its place or its price
   went while the keeper was at the pages, nothing happened */
static void lab_pour(tank_t *t) {
    fish_t r = s_lab_fish;
    if (!strcmp(r.name, tank_roster_name(r.preset))) r.name[0] = 0;   /* left unnamed: its own preset's name, as any fry's - never one a later fry will be given */
    lab_end(t);
    progression_lab_pour(t, &r);
}
/* a lab fry's recipe in words: its body by its nearest name, or by what was stretched; its fins by their set and tail */
static void lab_words(const fish_t *f, char *body, size_t nb, char *fins, size_t nf) {
    float len, dep, fl[3]; tank_body(f, &len, &dep); tank_fin_len(f, fl);
    const char *l = len > 18.6f ? "LONG" : len < 15.4f ? "SHORT" : NULL, *d = dep > 11.6f ? "TALL" : dep < 8.4f ? "LOW" : NULL;
    if (!tank_body_set(f) || (!l && !d)) snprintf(body, nb, "%s", SHAPE_NAMES[f->shape < SHAPE_N ? f->shape : 0]);
    else snprintf(body, nb, "%s%s%s", l ? l : "", l && d ? ", " : "", d ? d : "");
    snprintf(fins, nf, "%s%s", FINS_NAMES[f->fins < FINS_N ? f->fins : 0], !tank_finl_set(f) ? "" : fl[0] > 1.15f ? ", LONG" : fl[0] < 0.85f ? ", SHORT" : "");
}
/* the fry a page is about, at page (x, y) under its ring: in the tank on stage, or - the lab's - drawn there */
static void now_fry(const tank_t *t, uint16_t *fb, int stride, const fish_t *f, int x, int y, float r, double clock);

static bool in_box(float x, float y, int bx, int by, int bw, int bh, int m) {
    return x >= bx - m && x < bx + bw + m && y >= by - m && y < by + bh + m;
}

/* ---- THE WHEEL'S GESTURE (0.4.2, THE WHEEL'S FEEL above): one wheel = n slots from page x x0, a spin of the focused
 * slot and a pick of another; the name pages' (fish and critters) and any other dial on these pages share it */
typedef struct {
    int n, x0;                                  /* slots, slot 0's left edge (page x) */
    void (*spin)(tank_t *t, int dir);           /* the focused slot one step: +1 = up = the next */
    void (*pick)(tank_t *t, int i);             /* focus slot i */
} wheel_t;
static int wheel_slot_at(const wheel_t *w, float x) {
    int i = (int)((x - w->x0 + (SETUP_SLOT_PX - SETUP_SLOT_W) * 0.5f) / SETUP_SLOT_PX);
    return i < 0 ? 0 : i >= w->n ? w->n - 1 : i;
}
/* page coordinates: the row band picks a slot; above / below it the chevrons over the focused slot and its neighbours
 * (a hand wide); further along the row, above / below another letter is that letter's column. 0 = off the dial. */
static int wheel_hit(const wheel_t *w, float x, float y) {
    int rw = (w->n - 1) * SETUP_SLOT_PX + SETUP_SLOT_W;
    if (x < w->x0 - 24 || x >= w->x0 + rw + 24) return 0;
    const int top = SETUP_SLOT_Y - 16, bot = SETUP_SLOT_Y + SETUP_SLOT_H + 20;
    if (y >= top && y < bot) return SETUP_HIT_SLOT0 + wheel_slot_at(w, x);
    if (y < top - 70 || y >= bot + 70) return 0;
    float cx = w->x0 + s_slot * SETUP_SLOT_PX + SETUP_SLOT_W * 0.5f;
    if (x >= cx - 66 && x <= cx + 66) return y < top ? SETUP_HIT_UP : SETUP_HIT_DOWN;
    return SETUP_HIT_SLOT0 + wheel_slot_at(w, x);
}
static bool wheel_held(const tank_t *t) {
    double since = t->clock - s_wh_changed;
    return s_wh_hold_ms > 0 && since >= 0 && since < s_wh_hold_ms / 1000.0;
}
static bool is_wheel_hit(const wheel_t *w, int h) { return h == SETUP_HIT_UP || h == SETUP_HIT_DOWN || (h >= SETUP_HIT_SLOT0 && h < SETUP_HIT_SLOT0 + w->n); }
/* the press: h = setup_hit there. A letter is picked on touch (the drag turns it) - unless the hold keeps the press on
 * the letter being turned: inside its reach, while the last change is fresh */
static void wheel_press(tank_t *t, const wheel_t *w, float x, int h) {
    s_wh = is_wheel_hit(w, h) ? h : 0; s_wh_held = false;
    if (!s_wh || s_wh == SETUP_HIT_UP || s_wh == SETUP_HIT_DOWN) return;
    int i = s_wh - SETUP_HIT_SLOT0;
    if (i != s_slot && wheel_held(t)) {
        float cx = w->x0 + s_slot * SETUP_SLOT_PX + SETUP_SLOT_W * 0.5f, px = x - PAGE_X;
        if (s_wh_reach >= WHEEL_REACH_ROW || fabsf(px - cx) <= SETUP_SLOT_PX * 0.5f + s_wh_reach) { s_wh_held = true; return; }
    }
    w->pick(t, i);
}
/* the finger down and moving: vertical travel turns the focused letter, wherever on the dial the press began */
static void wheel_drag(tank_t *t, const wheel_t *w, float y) {
    if (!s_wh) return;
    if (fabsf(y - s_py) > s_wh_slop) s_spun = true;                     /* a drag now: no tap on release */
    s_acc += y - s_ly;
    while (s_acc <= -SETUP_SPIN_PX) { w->spin(t, +1); s_acc += SETUP_SPIN_PX; s_spun = true; s_wh_changed = t->clock; }   /* up = the next */
    while (s_acc >=  SETUP_SPIN_PX) { w->spin(t, -1); s_acc -= SETUP_SPIN_PX; s_spun = true; s_wh_changed = t->clock; }
}
/* the release of a press that did not drag: true = tap it (setup_activate); a chevron's step is a change */
static bool wheel_tap(const tank_t *t, int h) {
    if (s_wh_held && h >= SETUP_HIT_SLOT0) return false;               /* kept on the turned letter: nothing to pick */
    if (h == SETUP_HIT_UP || h == SETUP_HIT_DOWN) s_wh_changed = t->clock;
    return true;
}
/* the name pages' wheel: the fish's or the critter's name (name_spin / name_pick, by spin above) */
static const wheel_t NAME_WHEEL = { FISH_NAME_MAX, SETUP_SLOT_X, name_spin, name_pick };
void setup_wheel_tune(int hold_ms, int reach_px, int slop_px) {
    if (hold_ms >= 0) s_wh_hold_ms = hold_ms > 5000 ? 5000 : hold_ms;
    if (reach_px >= 0) s_wh_reach = reach_px >= WHEEL_REACH_ROW ? WHEEL_REACH_ROW : reach_px;
    if (slop_px >= 0) s_wh_slop = slop_px > 60 ? 60 : slop_px;
}
void setup_wheel_tuning(int *hold_ms, int *reach_px, int *slop_px) {
    if (hold_ms) *hold_ms = s_wh_hold_ms;
    if (reach_px) *reach_px = s_wh_reach;
    if (slop_px) *slop_px = s_wh_slop;
}

/* ---- SAND DOLLAR GIFTS (0.4.2): the letter wheel's page with three digits (setup.h) ----
 * The same wheel as a name's: a slot picked by a touch, turned by vertical travel (SETUP_SPIN_PX a step, up = the next
 * digit) or by the chevrons, the tick on every step. Digits wrap 9 -> 0. */
#define SDG_SLOTS 3
static int sdg_x(int i) { return (PAGE_W - ((SDG_SLOTS - 1) * SETUP_SLOT_PX + SETUP_SLOT_W)) / 2 + i * SETUP_SLOT_PX; }
static int sdg_amount(void) { return s_sdg_dig[0] * 100 + s_sdg_dig[1] * 10 + s_sdg_dig[2]; }
static bool sdg_ok(const tank_t *t) { int n = sdg_amount(); return n > 0 && n <= t->sd_balance; }
void setup_begin_sdgift(tank_t *t, int edge, int badge) {
    if (s_active) return;
    s_sdg = 1; s_evo = false; s_evo_loop = false; s_evo_live = false; s_active = true; s_lab = false; s_birth = false; s_place = false; s_rename = false; s_about = false; s_fish = -1; s_crit = -1;
    s_page = SETUP_PG_RENAME; s_slot = SDG_SLOTS - 1; s_wh_changed = WHEEL_NEVER; s_wh = 0; s_down = false;   /* (the ones digit first: "1" is one swipe away) */
    s_sdg_dig[0] = s_sdg_dig[1] = s_sdg_dig[2] = 0; s_sdg_edge = edge; s_sdg_badge = badge; s_sdg_sent = 0;
    stage(t);
}
bool setup_is_sdgift(void) { return s_active && s_sdg; }
bool setup_sdgift_confirming(void) { return s_active && s_sdg == 2; }
int  setup_sdgift_amount(void) { return sdg_amount(); }
void setup_sdgift_set(int n) { n = n < 0 ? 0 : n > 999 ? 999 : n; s_sdg_dig[0] = n / 100; s_sdg_dig[1] = n / 10 % 10; s_sdg_dig[2] = n % 10; }
int  setup_take_sdgift(int *edge) { int n = s_sdg_sent; s_sdg_sent = 0; if (edge) *edge = s_sdg_edge; return n; }
static void sdg_end(tank_t *t, int sent) { s_sdg_sent = sent; s_sdg = 0; s_active = false; stage(t); }
/* an odometer (Strato 11-08: "99 scrolls up to 100"): a digit turned past 9 carries into the one before it, past 0 borrows
   from it - the whole number steps by that digit's place, held to 0..999 (at an end: no step, no tick) */
static void sdg_spin(int dir) {
    static const int PLACE[SDG_SLOTS] = { 100, 10, 1 };
    int n = sdg_amount() + dir * PLACE[s_slot];
    if (n < 0 || n > SDG_MAX_GIFT) return;
    tank_emit(TEV_WHEEL_TICK, -1);
    setup_sdgift_set(n);
}
static void sdg_spin_t(tank_t *t, int dir) { (void)t; sdg_spin(dir); }
static void sdg_pick(tank_t *t, int i) { (void)t; s_slot = i < 0 ? 0 : i >= SDG_SLOTS ? SDG_SLOTS - 1 : i; }
static const wheel_t SDG_WHEEL = { SDG_SLOTS, (PAGE_W - ((SDG_SLOTS - 1) * SETUP_SLOT_PX + SETUP_SLOT_W)) / 2, sdg_spin_t, sdg_pick };
/* the confirm page: a panel, the foot's BACK | SEND (far from the dial's NEXT: a doubled tap cannot send) */
static int sdg_hit(float x, float y) {
    const int m = 10;
    if (s_sdg == 2) {
        if (in_box(x, y, SETUP_NEXT_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_NEXT;
        if (in_box(x, y, SETUP_BACK_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_BACK;
        return 0;
    }
    if (in_box(x, y, SETUP_TOP_NEXT_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_NEXT;
    if (in_box(x, y, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_BACK;
    return wheel_hit(&SDG_WHEEL, x, y);                 /* the dial: the letter wheel's own (wheel_t) */
}
static void sdg_activate(tank_t *t, int id) {
    if (s_sdg == 2) {
        if (id == SETUP_HIT_BACK) { s_sdg = 1; return; }
        if (id == SETUP_HIT_NEXT) { int n = sdg_amount(); if (sdg_ok(t)) { tank_emit(TEV_CONFIRM, -1); sdg_end(t, n); } else s_sdg = 1; }
        return;
    }
    if (id == SETUP_HIT_BACK) { sdg_end(t, 0); return; }
    if (id == SETUP_HIT_NEXT) { if (sdg_ok(t)) s_sdg = 2; return; }   /* (dim: nothing - the line under the dial says why) */
    if (id >= SETUP_HIT_SLOT0 && id < SETUP_HIT_SLOT0 + SDG_SLOTS) s_slot = id - SETUP_HIT_SLOT0;
    else if (id == SETUP_HIT_UP) sdg_spin(+1);
    else if (id == SETUP_HIT_DOWN) sdg_spin(-1);
}
static void sdg_touch(tank_t *t, float x, float y, bool down) {   /* setup_touch's wheel, the letter wheel's feel (wheel_t) */
    if (down && !s_down) {
        s_px = x; s_py = y; s_ly = y; s_acc = 0; s_spun = false; s_press_clock = t->clock;
        int h = setup_hit(x, y);
        if (s_sdg == 1) wheel_press(t, &SDG_WHEEL, x, h); else s_wh = 0;
    } else if (down && s_sdg == 1) {
        wheel_drag(t, &SDG_WHEEL, y);
        s_ly = y;
    } else if (!down && s_down && !s_spun) {
        int h = s_wh ? s_wh : setup_hit(s_px, s_py);
        if (h && h == setup_hit(x, y) && (!s_wh || wheel_tap(t, h))) sdg_activate(t, h);
    }
    s_down = down;
}

/* ---- THE PARAGON'S REVEAL (0.4.3, docs/elder-plus-0.4.3.md) -------------------------------------------------------
 * The three parts were rolled and saved before this page ever opens (tank_evo_roll): the spin is a show over a fixed
 * result, so leaving, a power cut or opening it again can never change them. The first time, the shells open one at
 * a time and each tile flicks through parts, slows and lands (about 2, 3 and 4.5 s); a tap during it hurries the tile
 * that is spinning - a tap never does nothing, and never chooses. Once all three have landed it is CHOOSE ONE: a tile
 * to try it on the big fish, KEEP THIS, then the same question once more. EVO_SHOWN (saved) skips the spin next time. */
static const float EVO_OPEN0[3] = { 1.2f, 3.4f, 6.6f }, EVO_LAND0[3] = { 3.2f, 6.4f, 11.1f };
static const int   EVO_TICKS[3] = { 14, 20, 28 };
static float evo_now(const tank_t *t) { return (float)(t->clock - s_evo_at); }
static bool evo_all_landed(const tank_t *t) { return evo_now(t) >= s_evo_land[2]; }
static void evo_restart(const tank_t *t) {
    s_evo_at = t->clock; s_evo_sel = -1; s_evo_confirm = false;
    for (int k = 0; k < 3; k++) { s_evo_open[k] = EVO_OPEN0[k]; s_evo_land[k] = EVO_LAND0[k]; s_evo_tick[k] = -1; s_evo_chimed[k] = false; }
}
static void evo_bench_tick(const tank_t *t) { if (s_evo_loop && evo_now(t) > s_evo_land[2] + 1.0f) evo_restart(t); }   /* (the bench's loop only) */
int setup_evo_phase(const tank_t *t) {
    if (!setup_is_evolve()) return -1;
    if (s_evo_confirm) return SETUP_EVO_KEEP;
    if (evo_all_landed(t)) return SETUP_EVO_CHOOSE;
    const float T = evo_now(t);
    for (int k = 0; k < 3; k++) if (T >= s_evo_open[k] && T < s_evo_land[k]) return SETUP_EVO_SPIN;
    return SETUP_EVO_SHELLS;
}
bool setup_debug_evo(tank_t *t, const char *op, int arg) {
    if (!setup_is_evolve()) return false;
    if (!strcmp(op, "spin")) evo_restart(t);
    else if (!strcmp(op, "loop")) { s_evo_loop = arg != 0; if (s_evo_loop) evo_restart(t); }
    else if (!strcmp(op, "pick")) { if (!evo_all_landed(t) || arg < 0 || arg > 2) return false; s_evo_sel = (int8_t)arg; s_evo_confirm = false; }
    else if (!strcmp(op, "keep")) { if (s_evo_sel < 0) return false; s_evo_confirm = true; }
    else if (!strcmp(op, "back")) s_evo_confirm = false;
    else return false;
    return true;
}
void setup_begin_evolve(tank_t *t, int fish) {
    if (fish < 0 || fish >= t->n_fish || s_active || !tank_evo_ready(&t->fish[fish]) || t->fish[fish].away) return;
    s_sdg = 0; s_evo = true; s_step_at = -1e9; s_active = true; s_lab = false; s_birth = false; s_place = false; s_rename = false; s_about = false; s_fish = fish; s_crit = -1;
    s_page = SETUP_PG_RENAME; s_slot = 0; s_wh_changed = WHEEL_NEVER; s_wh = 0; s_down = false; s_renamed = 0;
    s_evo_at = s_evo_step = t->clock; s_evo_sel = -1; s_evo_confirm = false; s_evo_back = true;
    const bool shown = (t->fish[fish].evo_offer & EVO_SHOWN) != 0;
    for (int k = 0; k < 3; k++) { s_evo_open[k] = shown ? -1 : EVO_OPEN0[k]; s_evo_land[k] = shown ? -1 : EVO_LAND0[k]; s_evo_tick[k] = -1; s_evo_chimed[k] = shown; }
    stage(t);
}
void setup_begin_evolve_tank(tank_t *t, int fish) { if (s_active) return; setup_begin_evolve(t, fish); if (setup_is_evolve()) s_evo_back = false; }
bool setup_is_evolve(void) { return s_active && s_evo; }
static void evo_end(tank_t *t) {
    s_evo_loop = false; s_evo_live = false;             /* (the bench's flags end with the page: the next reveal is the keeper's) */
    if (s_fish >= 0 && s_fish < t->n_fish && evo_now(t) >= s_evo_land[0]) progression_evo_shown(t, s_fish);   /* (once a tile has landed the three are
                                                               known: opened again, no second spin - it would read as a re-roll) */
    s_renamed = s_evo_back && s_fish >= 0 && s_fish < t->n_fish ? s_fish + 1 : 0;      /* the fish's card back up (from the tank: the tank) */
    s_evo = false; s_active = false; stage(t);
}
/* a tap while it spins: the tile that is spinning (or the next shell) lands in half a second, the rest follow sooner */
static void evo_hurry(const tank_t *t) {
    float T = evo_now(t);
    for (int k = 0; k < 3; k++) {
        if (T >= s_evo_land[k]) continue;
        float land = T < s_evo_open[k] ? T + 1.0f : T + 0.5f, d = s_evo_land[k] - land;
        if (d <= 0) return;
        if (T < s_evo_open[k]) s_evo_open[k] = T;
        s_evo_land[k] = land;
        for (int j = k + 1; j < 3; j++) { s_evo_open[j] -= d; s_evo_land[j] -= d; }
        return;
    }
}
static int evo_flick(uint32_t uid, int k, int i) {     /* the parts that flick past: from the whole set, the same for a fish every time */
    uint32_t h = uid * 2654435761u ^ (uint32_t)(k * 97 + i + 1) * 40503u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return 1 + (int)(h % (EVO_N - 1));
}
static int evo_tick_at(const tank_t *t, int k) {       /* which tick tile k is on: fast, then slower and slower (ease out) */
    float u = (evo_now(t) - s_evo_open[k]) / fmaxf(0.05f, s_evo_land[k] - s_evo_open[k]);
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    int i = (int)(EVO_TICKS[k] * (1 - (1 - u) * (1 - u)));
    return i >= EVO_TICKS[k] ? EVO_TICKS[k] - 1 : i;
}
static void evo_fill(const tank_t *t, evo_page_t *pg) {
    memset(pg, 0, sizeof *pg);
    const fish_t *f = &t->fish[s_fish];
    float T = evo_now(t);
    pg->fish = s_fish; pg->sel = s_evo_sel; pg->confirm = s_evo_confirm; pg->choosing = evo_all_landed(t); pg->away = f->away;
    pg->hold = s_evo_hold > 0 ? (s_evo_hold >= EVO_KEEP_HOLD_S ? 1.0f : s_evo_hold / EVO_KEEP_HOLD_S) : 0;   /* (KEEP?'s YES, held) */
    /* (0.4.3 audit r3b, R3b-L11; Strato 11-23: "yes make the bar stop short of full in the doorway") a full bar means kept: while
       the keep waits - the fish in the doorway (R3-L1), or a full hold refused - the bar stops at EVO_WAIT_FILL; a keep closes
       the page on its poll, so the page never draws the end. Timing, what is kept and the touch rule are as they were */
    if ((f->door == DOOR_ASKED || f->door == DOOR_PASSING || s_evo_hold >= EVO_KEEP_HOLD_S) && pg->hold > EVO_WAIT_FILL) pg->hold = EVO_WAIT_FILL;
    for (int k = 0; k < 3; k++) {
        int real = evo_offer_at(f->evo_offer, k);
        if (T < s_evo_open[k]) pg->state[k] = EVP_CLOSED;
        else if (T < s_evo_land[k]) {
            int i = evo_tick_at(t, k), p = evo_flick(f->uid, k, i);
            /* (11-21, Strato on the glass: it clicked once more to a part he never saw) the LAST tick is the answer: the reel
               stops on the part it lands on, and the name and the chime follow. The tick before it is never the same part */
            if (i >= EVO_TICKS[k] - 1) p = real;
            else if (i == EVO_TICKS[k] - 2 && p == real) p = p % (EVO_N - 1) + 1;
            pg->state[k] = EVP_SPIN; pg->part[k] = (uint8_t)p;
            pg->ghost[k][0] = (uint8_t)evo_flick(f->uid, k, i - 1); pg->ghost[k][1] = (uint8_t)evo_flick(f->uid, k, i + 1);
        } else { pg->state[k] = EVP_LANDED; pg->part[k] = (uint8_t)real; pg->landed_s[k] = s_evo_chimed[k] && s_evo_land[k] < 0 ? 99 : T - s_evo_land[k]; }
        for (int w = 0; w < 2 && !pg->family[k]; w++) {   /* a parent's part: a parent that lives here (or is visiting) and wears it */
            uint32_t uid = f->parent_uid[w];
            for (int i = 0; i < N_FISH_SLOTS && uid; i++) {
                bool here = i < t->n_fish || (tank_is_visitor(i) && t->vis_used[i - N_FISH_MAX]);
                if (here && t->fish[i].uid == uid && t->fish[i].evo == real) { pg->family[k] = true; pg->from[k] = t->fish[i].name; break; }
            }
        }
    }
}
static int evo_hit(float x, float y) {
    const int m = 6;
    if (in_box(x, y, EVP_BACK_X, EVP_BTN_Y, EVP_BACK_W, EVP_BTN_H, 10)) return SETUP_HIT_BACK;
    if (in_box(x, y, EVP_KEEP_X, EVP_BTN_Y, EVP_KEEP_W, EVP_BTN_H, 10)) return SETUP_HIT_NEXT;
    if (s_evo_confirm) return 0;
    for (int k = 0; k < 3; k++) if (in_box(x, y, EVP_TILE_X(k) - EVP_TILE_GAP / 2 + m, EVP_TILE_Y, EVP_TILE_W + EVP_TILE_GAP - 2 * m, EVP_TILE_H, m)) return SETUP_HIT_SLOT0 + k;
    return 0;
}
static void evo_activate(tank_t *t, int id) {
    if (s_fish < 0 || s_fish >= t->n_fish) { evo_end(t); return; }
    if (!evo_all_landed(t)) { evo_hurry(t); return; }
    progression_evo_shown(t, s_fish);
    /* (0.4.3 audit r1 TP-7) the fish next door: KEEP is kept only at home (progression_evo_keep) - neither KEEP THIS nor YES is
       offered, the page says why in their place and stays up; the tiles, NOT YET and LATER still answer */
    if (s_evo_confirm) {
        if (id == SETUP_HIT_BACK) { s_evo_confirm = false; s_evo_step = t->clock; }   /* (NOT YET: CHOOSE ONE again, its LATER under the finger) */
        else if (id == SETUP_HIT_NEXT && s_evo_sel >= 0 && !t->fish[s_fish].away && progression_evo_keep(t, s_fish, s_evo_sel)) { tank_emit(TEV_CONFIRM, -1); evo_end(t); }
                                                        /* (0.4.3 audit r3, R3-L1: refused - the fish in the doorway - the page stays up: nothing looks kept) */
        return;
    }
    if (id >= SETUP_HIT_SLOT0 && id < SETUP_HIT_SLOT0 + 3) s_evo_sel = (int8_t)(id - SETUP_HIT_SLOT0);
    else if (id == SETUP_HIT_BACK) evo_end(t);
    else if (id == SETUP_HIT_NEXT && s_evo_sel >= 0 && !t->fish[s_fish].away) { s_evo_confirm = true; s_evo_step = t->clock; }   /* (the question: its YES under the finger) */
}
static bool evo_yes_here(const tank_t *t, float x, float y) {   /* KEEP?'s YES is up and (x, y) is on it */
    return s_evo_confirm && s_evo_sel >= 0 && evo_all_landed(t) && !t->fish[s_fish].away && setup_hit(x, y) == SETUP_HIT_NEXT;
}
static void evo_touch(tank_t *t, float x, float y, bool down) {
    if (down && !s_down) {
        s_px = x; s_py = y; s_press_clock = t->clock; s_evo_hold = -1;
        if (evo_yes_here(t, x, y) && !hold_spent_at(&s_evo_step, s_press_clock, t->clock, TAP_W_S)) { s_evo_hold = 0; s_evo_hold_at = t->clock; }   /* (G1 + W: no hold) */
    } else if (down && s_down && s_evo_hold >= 0) {    /* YES held: the polls that see the finger on it add up */
        if (!evo_yes_here(t, x, y)) s_evo_hold = -1;   /* off it (or the question went): empty, this press is done */
        else {
            double d = t->clock - s_evo_hold_at; s_evo_hold_at = t->clock;
            s_evo_hold += d < 0 ? 0 : d > EVO_HOLD_STEP_S ? EVO_HOLD_STEP_S : (float)d;
            if (s_evo_hold >= EVO_KEEP_HOLD_S) {       /* full: kept, the page closes - the finger still down (the port spends its lift) */
                s_evo_hold = EVO_KEEP_HOLD_S;
                progression_evo_shown(t, s_fish);
                if (progression_evo_keep(t, s_fish, s_evo_sel)) { s_evo_hold = -1; s_down = down; tank_emit(TEV_CONFIRM, -1); evo_end(t); return; }
                /* (0.4.3 audit r3, R3-L1) refused - the fish in the doorway (DOOR_ASKED / PASSING, R2-L2): the hold waits, full,
                   the page up, and keeps on the first poll the fish is out of the doorway at home; gone next door, VISITING
                   (evo_yes_here: the hold empties). Let go meanwhile: nothing kept, nothing lost */
            }
        }
    }
    else if (!down && s_down) {
        const bool on_yes = s_evo_confirm && setup_hit(s_px, s_py) == SETUP_HIT_NEXT;
        s_evo_hold = -1;                                /* lifted before full: nothing kept, nothing lost */
        if (on_yes) { progression_evo_shown(t, s_fish); s_down = down; return; }   /* a tap on YES is no answer: only the hold keeps */
        if (hold_spent_at(&s_evo_step, s_press_clock, t->clock, TAP_W_S)) { s_down = down; return; }   /* THE TOUCH RULE: the second half of the tap
                                                           that opened the page or brought the question up, or took it away - no tap (no restart) */
        if (!evo_all_landed(t)) evo_hurry(t);           /* anywhere on the glass */
        else { int h = setup_hit(s_px, s_py); if (h && h == setup_hit(x, y)) evo_activate(t, h); else progression_evo_shown(t, s_fish); }
    }
    s_down = down;
}
static void evo_sounds(const tank_t *t, const evo_page_t *pg) {   /* (once per change: a frame drawn twice never ticks twice) */
    for (int k = 0; k < 3; k++) {                       /* its sounds: a tick for each part that flicks past, a chime as it lands */
        if (pg->state[k] == EVP_SPIN) { int i = evo_tick_at(t, k); if (i != s_evo_tick[k]) { s_evo_tick[k] = i; tank_emit(TEV_WHEEL_TICK, -1); } }
        if (pg->state[k] == EVP_LANDED && !s_evo_chimed[k]) { s_evo_chimed[k] = true; tank_emit(TEV_CONFIRM, -1); }
    }
}
static void render_evolve(const tank_t *t, uint16_t *fb, int stride, double clock) {
    if (s_fish < 0 || s_fish >= t->n_fish) return;
    evo_bench_tick(t);
    evo_page_t pg; evo_fill(t, &pg);
    evo_sounds(t, &pg);
    render_evo_page(t, fb, stride, clock, &pg);
}
float setup_evo_bar(const tank_t *t) {
    if (!s_active || !s_evo || s_fish < 0 || s_fish >= t->n_fish) return -1;
    evo_page_t pg; evo_fill(t, &pg); return pg.hold;
}
/* THE REVEAL KEPT (setup.h, render.h render_evo_held) */
#ifdef TANK_P4
bool setup_evo_black(void) { return s_active && s_evo && !s_evo_live && !render_big_pocket(); }   /* (the big screens: held too, at 2x - render.c
                                                           THE REVEAL KEPT AT 2X; under the suites' pocket pages it is drawn over the tank, as before) */
#else
bool setup_evo_black(void) { return s_active && s_evo && !s_evo_live; }
#endif
void setup_debug_evo_live(bool on) { s_evo_live = on; }
int setup_evo_bands(const tank_t *t, int box[SETUP_EVO_BANDS][4]) {
    if (!setup_evo_black() || s_fish < 0 || s_fish >= t->n_fish) return 0;
    evo_page_t pg; evo_fill(t, &pg);
    return render_evo_boxes(&pg, box);
}
void setup_evo_bands_draw(const tank_t *t, uint16_t *fb, int stride, double clock) {
    if (!setup_evo_black() || s_fish < 0 || s_fish >= t->n_fish) return;
    evo_page_t pg; evo_fill(t, &pg);
    evo_sounds(t, &pg);
    render_evo_bands(t, fb, stride, clock, &pg);
}
int setup_hit(float x, float y) {
    if (!s_active) return 0;
    x -= PAGE_X; y -= PAGE_Y;                           /* the page's own coordinates (render.h) */
    if (s_sdg) return sdg_hit(x, y);
    if (s_evo) return evo_hit(x, y);
    const int m = 10;                                   /* a fingertip's slop, as on the reset prompt */
    if (s_lab && s_page == SETUP_PG_LAB_TUBE) {
        if (in_box(x, y, LAB_L, SETUP_LAB_BACK_Y, SETUP_LAB_BACK_W, SETUP_BTN_H, m)) return SETUP_HIT_BACK;
        return in_box(x, y, SETUP_LAB_BTN_X, SETUP_LAB_BTN_Y, SETUP_LAB_BTN_W, SETUP_BTN_H, m) ? SETUP_HIT_POUR : 0;
    }
    if (page_one_button())
        return in_box(x, y, SETUP_MID_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, m) ? SETUP_HIT_NEXT : 0;
    if (nav_on_top()) {
        if (s_place) {                                  /* the four-button row (0.3.9): SELL | SHELVE | LOCK | DONE, a narrower width */
            if (in_box(x, y, SETUP_TOP_DONE_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_NEXT;
            if (in_box(x, y, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_SELL;
            if (in_box(x, y, SETUP_TOP_SHELVE_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_SHELVE;
            if (in_box(x, y, SETUP_TOP_LOCK_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_LOCK;
        }
        else if (in_box(x, y, SETUP_TOP_NEXT_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_NEXT;
        const bool plant = s_place && s_item == 0, two = plant && s_plant_two;   /* (the plant's page: two rows with a piece beside it) */
        const int ord_y = two ? SETUP_ORD2_Y : SETUP_ORD_Y;
        if (s_place && in_box(x, y, SETUP_ORD_X0, ord_y, SETUP_ORD_W, SETUP_BTN_H, 6)) return SETUP_HIT_BCK;   /* (every piece, the sword plant too) */
        if (plant && in_box(x, y, SETUP_ORDSQ_X, SETUP_DEPTH_Y, 2 * SETUP_DEPTH_SEG_W, SETUP_DEPTH_H, 6))
            return SETUP_HIT_SG0 + (x >= SETUP_ORDSQ_X + SETUP_DEPTH_SEG_W);    /* the plant's SEAGRASS squares (0.4.1) */
        if (two && in_box(x, y, SETUP_ORDSQ_X, SETUP_ORDSQ2_Y, 2 * SETUP_DEPTH_SEG_W, SETUP_DEPTH_H, 6))
            return SETUP_HIT_ORD0 + (x >= SETUP_ORDSQ_X + SETUP_DEPTH_SEG_W);   /* ... and its ORDER SQUARES, the row under them */
        if (s_place && in_box(x, y, SETUP_ORD_X1, ord_y, SETUP_ORD_W, SETUP_BTN_H, 6)) return SETUP_HIT_FWD;
        if (!s_place && in_box(x, y, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_BACK;
        if (page_is_look() && in_box(x, y, SETUP_ROLL_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_ROLL;
    } else {
        if (in_box(x, y, SETUP_NEXT_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_NEXT;
        if (in_box(x, y, SETUP_BACK_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_BACK;
    }
    if (page_is_name() && s_kbd) return kbd_hit(x, y);
    if (page_is_name()) return wheel_hit(&NAME_WHEEL, x, y);   /* (the wheel's gesture, above) */
    if (s_page == SETUP_PG_PLACE) {
        if (s_item == 4) {                              /* the cluster's LOOK row: the nearest tile along x */
            int rw = (SETUP_LOOK_N - 1) * SETUP_LOOK_PX + SETUP_LOOK_W;
            if (in_box(x, y, SETUP_LOOK_X, SETUP_LOOK_Y, rw, SETUP_LOOK_H, 12)) {
                int i = (int)((x - SETUP_LOOK_X + (SETUP_LOOK_PX - SETUP_LOOK_W) * 0.5f) / SETUP_LOOK_PX);
                if (i < 0) i = 0;
                if (i >= SETUP_LOOK_N) i = SETUP_LOOK_N - 1;
                return SETUP_HIT_COLOR0 + i;
            }
        }
        if (s_item == 3) {                              /* the coral's COLOR row: the nearest swatch along x, a band 12 px around */
            int rw = (SETUP_COL_N - 1) * SETUP_COL_PX + SETUP_COL_W;
            if (in_box(x, y, SETUP_COL_X, SETUP_COL_Y, rw, SETUP_COL_H, 12)) {
                int i = (int)((x - SETUP_COL_X + (SETUP_COL_PX - SETUP_COL_W) * 0.5f) / SETUP_COL_PX);
                if (i < 0) i = 0;
                if (i >= SETUP_COL_N) i = SETUP_COL_N - 1;
                return SETUP_HIT_COLOR0 + i;
            }
        }
        /* the DEPTH bar: a band 8 px around it, the segment under the finger
           (the item's own depths: the plant's three, the castle's two) */
        int n = tank_decor_z_count(s_item), bw = n * SETUP_DEPTH_SEG_W, bx = (PAGE_W - bw) / 2;
        if (n < 2 || !in_box(x, y, bx, SETUP_DEPTH_Y, bw, SETUP_DEPTH_H, 8)) return 0;   /* (the plant: one depth, no bar - 0.4) */
        int i = (int)((x - bx) / SETUP_DEPTH_SEG_W);
        if (i < 0) i = 0;
        if (i >= n) i = n - 1;
        return SETUP_HIT_Z0 + tank_decor_z_at(s_item, i);
    }
    if (page_is_stretch()) {                           /* (0.4.3) the tiles' row - a band round it, the nearest along x - else the stage */
        const int rw = (SETUP_TILE_N - 1) * SETUP_TILE_PX + SETUP_TILE_W;
        if (y >= SETUP_ST_TILE_Y - 6 && y < SETUP_ST_TILE_Y + SETUP_ST_TILE_H + 18 && x >= SETUP_TILE_X - 12 && x < SETUP_TILE_X + rw + 12) {
            int i = (int)((x - SETUP_TILE_X + (SETUP_TILE_PX - SETUP_TILE_W) * 0.5f) / SETUP_TILE_PX);
            i = i < 0 ? 0 : i >= SETUP_TILE_N ? SETUP_TILE_N - 1 : i;
            return page_is_body() ? SETUP_HIT_SHAPE0 + ST_SHAPES[i] : SETUP_HIT_FINS0 + i;
        }
        return y >= SETUP_ST_TOP && y < SETUP_ST_BOT ? SETUP_HIT_STAGE : 0;
    }
    if (page_is_colors() && in_box(x, y, SETUP_SWAP_X, SETUP_SWAP_Y, SETUP_SWAP_W, SETUP_SWAP_H, 6)) return SETUP_HIT_SWAP;   /* before the rows' bands */
    if (page_is_look()) {
        /* the rows: a band from a row's label to halfway into the gap under
           it (the last one 24 px past its foot - fingers land low), the
           nearest choice along x */
        const edit_row_t *r; int n = edit_rows(&r);
        for (int k = 0; k < n; k++) {
            int top = r[k].y - SETUP_LABEL_DY - 4;
            int bot = k + 1 < n ? (r[k].y + r[k].h + r[k + 1].y - SETUP_LABEL_DY - 4) / 2 : r[k].y + r[k].h + 24;
            if (k + 1 < n && bot < r[k].y + r[k].h) bot = r[k].y + r[k].h;
            int rw = (r[k].n - 1) * r[k].px + r[k].w;
            if (y < top || y >= bot || x < r[k].x - 12 || x >= r[k].x + rw + 12) continue;
            int i = (int)((x - r[k].x + (r[k].px - r[k].w) * 0.5f) / r[k].px);
            if (i < 0) i = 0;
            if (i >= r[k].n) i = r[k].n - 1;
            return r[k].hit0 + i;
        }
    }
    return 0;
}

static void setup_activate_in(tank_t *t, int id);
void setup_activate(tank_t *t, int id) {
    if (s_active && !lab_in(t)) return;
    setup_activate_in(t, id);
    lab_out(t);
}
static void setup_activate_in(tank_t *t, int id) {
    if (!s_active || !id) return;
    if (s_sdg) { sdg_activate(t, id); return; }
    if (s_evo) { evo_activate(t, id); return; }
    fish_t *f = s_crit >= 0 ? NULL : &t->fish[page_fish()];   /* (a critter's rename page: its name below, no fish) */
    if (s_place) {                                      /* DONE saves the spot; a layer button sets the depth; SELL twice sells */
        if (id == SETUP_HIT_SELL) {
            if (!s_sell_armed) { s_sell_armed = true; s_sell_clock = t->clock; return; }
            if (hold_spent_at(&s_sell_clock, s_tap_press ? s_press_clock : t->clock, t->clock, SETUP_SELL_WAIT_S)) return;   /* a doubled tap (or a mash, or a
                                                               firm second press: read at its PRESS, R2-1): still armed (THE TOUCH RULE: not restarted) */
            s_sell_armed = false;
            if (progression_sell(t, s_item)) { s_active = false; tank_emit(TEV_CONFIRM, -1); }
            return;
        }
        if (id == SETUP_HIT_SHELVE) {                       /* the shelf (0.3.9): one tap, nothing to arm - it is undone from the shop's row */
            s_sell_armed = false;
            if (progression_shelve(t, s_item)) { s_active = false; tank_emit(TEV_CONFIRM, -1); }
            return;
        }
        if (id == SETUP_HIT_LOCK) {                         /* the LOCK (0.3.9): a toggle; the page stays up, the button reads LOCKED.
                                                               Once DONE closes it a held finger passes the piece by - the page
                                                               opens from the shop's row only (MOVE), and this button undoes it */
            s_sell_armed = false;
            if (progression_lock(t, s_item, !tank_decor_locked(t, s_item))) tank_emit(TEV_CONFIRM, -1);
            return;
        }
        s_sell_armed = false;                           /* any other tap stands SELL down */
        if (id == SETUP_HIT_NEXT) { s_active = false; tank_emit(TEV_CONFIRM, -1); progression_save(t); }
        else if (id == SETUP_HIT_BCK || id == SETUP_HIT_FWD) {                       /* the stack: a step; DONE saves it */
            const int dir = id == SETUP_HIT_FWD ? 1 : -1, past = tank_decor_neighbor(t, s_item, dir);
            if (tank_decor_shift(t, s_item, dir)) { s_moved = true; s_ref = past; }      /* (the squares follow the piece just passed) */
        }
        else if (s_item == 0 && (id == SETUP_HIT_SG0 || id == SETUP_HIT_SG0 + 1)) {   /* the SEAGRASS squares: the plant behind / in front of it */
            const bool under = id == SETUP_HIT_SG0;
            if (tank_plant_under(t) != under) { tank_plant_set_under(t, under); s_moved = true; }
        }
        else if (id == SETUP_HIT_ORD0 || id == SETUP_HIT_ORD0 + 1) {               /* the ORDER SQUARES: behind / in front of their piece - the arrow's step */
            const int ref = order_ref(t); const bool front = id == SETUP_HIT_ORD0 + 1;
            for (int k = 0; ref >= 0 && k < DECOR_SLOTS && tank_decor_above(t, s_item, ref) != front; k++)
                if (tank_decor_shift(t, s_item, front ? 1 : -1)) s_moved = true; else break;
            if (ref >= 0) s_ref = ref;
        }
        else if (id >= SETUP_HIT_Z0 && id < SETUP_HIT_Z0 + DECOR_Z_N) {
            if (tank_decor_z(t, s_item) != id - SETUP_HIT_Z0) s_moved = true;
            tank_decor_set(t, s_item, tank_decor_x(t, s_item), id - SETUP_HIT_Z0);
        } else if (s_item == 3 && id >= SETUP_HIT_COLOR0 && id < SETUP_HIT_COLOR0 + CORAL_N) {
            if (tank_coral_rgb(t) != CORAL_PAL[id - SETUP_HIT_COLOR0]) s_moved = true;
            tank_coral_set_rgb(t, CORAL_PAL[id - SETUP_HIT_COLOR0]);   /* the coral wears it at once; DONE saves it */
        } else if (s_item == 4 && id >= SETUP_HIT_COLOR0 && id < SETUP_HIT_COLOR0 + CLUSTER_SCHEME_N) {
            if (tank_cluster_scheme(t) != id - SETUP_HIT_COLOR0) s_moved = true;
            tank_cluster_set_scheme(t, id - SETUP_HIT_COLOR0);
        }
        return;
    }
    if (s_about) {                                      /* a fish's story: STORY > FAMILY > NATURE, nothing to change on them */
        static const int ORDER[SETUP_ABOUT_PAGES] = { SETUP_PG_ABOUT, SETUP_PG_FAMILY, SETUP_PG_NATURE };
        int k = 0; while (k < SETUP_ABOUT_PAGES - 1 && ORDER[k] != s_page) k++;
        if (id == SETUP_HIT_NEXT) { if (k == SETUP_ABOUT_PAGES - 1) { about_end(t); tank_emit(TEV_CONFIRM, -1); } else s_page = ORDER[k + 1]; }
        else if (id == SETUP_HIT_BACK) { if (k == 0) about_end(t); else s_page = ORDER[k - 1]; }
        s_about_at = t->clock;                          /* (a new page: the tap that turned it, doubled, is spent - setup_touch) */
        return;
    }
    if (s_rename) {                                     /* one page: DONE keeps the name, CANCEL the old one; the wheel below */
        if (id == SETUP_HIT_NEXT) { rename_end(t, true); tank_emit(TEV_CONFIRM, -1); return; }
        if (id == SETUP_HIT_BACK) { rename_end(t, false); return; }
    }
    if (s_lab && s_page == SETUP_PG_LAB_TUBE) {          /* BACK to the name; POUR IT IN, when there is a place and the price */
        if (id == SETUP_HIT_BACK) s_page = SETUP_PG_LAB_NAME;
        else if (id == SETUP_HIT_POUR && progression_lab_why(t) == LAB_OK) lab_pour(t);
        return;
    }
    if (s_lab && id == SETUP_HIT_BACK && s_page == SETUP_PG_LAB_BODY) { lab_end(t); return; }   /* out, nothing spent */
    if (id == SETUP_HIT_NEXT) {
        if (page_is_name()) tidy_name(t, page_fish());
        if (page_is_last()) {                           /* BEGIN / DONE: the debt is paid, the names saved */
            s_active = false; stage(t);
            tank_emit(TEV_CONFIRM, -1);
            if (s_birth) progression_newborn_done(t); else progression_setup_done(t);
            return;
        }
        s_page++; s_slot = 0; s_wh_changed = WHEEL_NEVER; s_typed = false; s_kb_page = 0; s_same_note = 0; s_st_can_undo = false; stage(t);
        return;
    }
    if (id == SETUP_HIT_BACK) {
        if (page_is_name()) tidy_name(t, page_fish());
        if (s_page > first_page()) s_page--;
        s_slot = 0; s_wh_changed = WHEEL_NEVER; s_typed = false; s_kb_page = 0; s_same_note = 0; s_st_can_undo = false; stage(t);
        return;
    }
    if (page_is_stretch()) {                            /* (0.4.3) ROLL / UNDO, a named body or set in one tap; the stage answers a drag only */
        if (id == SETUP_HIT_ROLL) { st_roll(t, page_fish()); tank_emit(TEV_WHEEL_TICK, -1); }
        else if (page_is_body() && id >= SETUP_HIT_SHAPE0 && id < SETUP_HIT_SHAPE0 + SHAPE_N) { tank_set_gene(t, page_fish(), GENE_SHAPE, id - SETUP_HIT_SHAPE0); s_st_can_undo = false; }
        else if (page_is_fins() && id >= SETUP_HIT_FINS0 && id < SETUP_HIT_FINS0 + FINS_N) { tank_set_gene(t, page_fish(), GENE_FINS, id - SETUP_HIT_FINS0); s_st_can_undo = false; }
        return;
    }
    if (page_is_name() && s_kbd) {
        if (id >= SETUP_HIT_KEY0 && id < SETUP_HIT_KEY0 + SETUP_KEY_N) kbd_key(page_name(t), id - SETUP_HIT_KEY0);
        return;
    }
    if (page_is_name()) {
        if (id >= SETUP_HIT_SLOT0 && id < SETUP_HIT_SLOT0 + FISH_NAME_MAX) pick_slot(page_name(t), id - SETUP_HIT_SLOT0);
        else if (id == SETUP_HIT_UP)   spin(page_name(t), +1);
        else if (id == SETUP_HIT_DOWN) spin(page_name(t), -1);
        return;
    }
    if (page_is_look()) {                               /* the fish wears a pick at once: the fry on stage, the grown portrait */
        if (id == SETUP_HIT_ROLL) { edit_roll(t, page_fish()); tank_emit(TEV_WHEEL_TICK, -1); return; }
        if (id == SETUP_HIT_SWAP && page_is_colors()) {       /* body <-> markings: one palette, so exact */
            int b = tank_gene(f, GENE_BODY), a = tank_gene(f, GENE_ACCENT);
            tank_set_look(t, page_fish(), LOOK_COLORS[a], LOOK_COLORS[b]);
            tank_emit(TEV_WHEEL_TICK, -1); return;
        }
        const edit_row_t *r; int n = edit_rows(&r);
        for (int k = 0; k < n; k++)
            if (id >= r[k].hit0 && id < r[k].hit0 + r[k].n) {
                int v = id - r[k].hit0;
                if (r[k].gene == GENE_ACCENT && LOOK_ACCENT[v] == f->color) { s_same_note = 75; return; }   /* struck out: say why (~3 s) */
                if (r[k].gene == GENE_PATTERN && v == f->pattern) v = PATTERN_SOLID;   /* the picked tile again: deselected - solid */
                tank_set_gene(t, page_fish(), r[k].gene, v);
                return;
            }
    }
}

static void setup_touch_in(tank_t *t, float x, float y, bool down);
void setup_touch(tank_t *t, float x, float y, bool down) {
#ifdef TANK_P4
    if (setup_zoomed()) { x = (x - ZM_OX) / ZM_S + PAGE_X; y = (y - ZM_OY) / ZM_S + PAGE_Y; }   /* (the glass back to the page's own coordinates) */
#endif
    if (s_active && !lab_in(t)) { s_down = down; return; }
    setup_touch_in(t, x, y, down);
    lab_out(t);
}
static void setup_touch_in(tank_t *t, float x, float y, bool down) {
    stage(t);                                           /* every frame: the tank keeps the fish on stage */
    if (!s_active) { s_down = down; return; }
    if (s_sdg) { sdg_touch(t, x, y, down); return; }
    if (s_evo) { evo_touch(t, x, y, down); return; }
    if (s_lab && s_lab_wiggle && page_is_stretch()) {   /* (the director's measure: a handle dragged to and fro by itself) */
        fish_t *f = &t->fish[SETUP_LAB_SLOT]; float len, dep; tank_body(f, &len, &dep);
        const float u = sinf((float)fmod(t->clock, 1000.0) * 2.5f);
        if (page_is_body()) { tank_set_body(f, 17 + 3.5f * u, dep); s_st_axis = 1; } else { tank_set_fin_len(f, 0, 1 + 0.45f * u); s_st_axis = 1; }
    }
    if (down && !s_down) {                              /* press */
        s_px = x; s_py = y; s_ly = y; s_acc = 0; s_spun = false; s_press_clock = t->clock;
        int h = setup_hit(x, y);
        if (page_is_name() && !s_kbd) wheel_press(t, &NAME_WHEEL, x, h);   /* a letter picked on touch (the drag turns it), or held */
        else s_wh = 0;
        s_st_press = page_is_stretch() && h == SETUP_HIT_STAGE; s_st_axis = 0;
        if (s_page == SETUP_PG_BUBBLES && !h && y - PAGE_Y > SETUP_TOP_BTN_Y + SETUP_BTN_H + 8) {
            tank_set_bubble_x(t, x); s_spun = true;         /* the column comes to the finger */
        }
        if (s_page == SETUP_PG_PLACE && !h && y - PAGE_Y > place_y()) {
            tank_decor_set(t, s_item, x, tank_decor_z(t, s_item)); s_spun = true; s_moved = true;   /* the piece comes to the finger */
        }
    } else if (down && s_page == SETUP_PG_BUBBLES && s_spun) {
        tank_set_bubble_x(t, x);                            /* ... and follows it */
    } else if (down && s_page == SETUP_PG_PLACE && s_spun) {
        tank_decor_set(t, s_item, x, tank_decor_z(t, s_item)); s_moved = true;
    } else if (down && page_is_stretch()) {             /* (0.4.3) a drag that began on the stage: the body, or a fin */
        if (s_st_press) st_drag(t, x, y);
    } else if (down && page_is_name()) {                /* the wheel: vertical travel spins the letter, from anywhere on the dial */
        wheel_drag(t, &NAME_WHEEL, y);
        s_ly = y;
    } else if (!down && s_down) {                       /* release: a tap, unless the wheel turned */
        if (!s_spun) {
            int h = s_wh ? s_wh : setup_hit(s_px, s_py);   /* (a wheel press: what it was at the press - a pick moves the chevrons) */
            if (s_about && s_press_clock - s_about_at < TAP_W_S) h = 0;   /* MORE's pages (0.4.2, the blind review): a press begun before the
                                                                          page came, or within TAP_W of it, is the double of the tap that
                                                                          brought it (MORE onto NEXT, NEXT onto NEXT) - spent, never restarted */
            if (hold_spent_at(&s_step_at, s_press_clock, t->clock, TAP_W_S)) h = 0;   /* (0.4.3 audit r1 TP-4) the same for a page a tap turned: a doubled
                                                                          NEXT on the lab's BODY skipped FINS, a doubled BACK on the tube landed on COLORS */
            if (h && h == setup_hit(x, y) && (!s_wh || wheel_tap(t, h))) {
                const int pg = s_page;
                s_tap_press = true; setup_activate(t, h); s_tap_press = false;
                if (s_active && s_page != pg) s_step_at = t->clock;
            }
        }
    }
    if (!down) { s_st_axis = 0; s_st_press = false; }
    s_down = down;
}

/* ---- drawing ---- */
static void text_c(uint16_t *fb, int stride, int cx, int y, int scale, uint32_t rgb, const char *s) {
    render_text(fb, stride, cx - render_text_w(s, scale) / 2, y, scale, rgb, s);
}
static void lines_c(uint16_t *fb, int stride, int y, int pitch, uint32_t rgb, const char *const *ls, int n) {
    for (int i = 0; i < n; i++) text_c(fb, stride, SETUP_X + SETUP_W / 2, y + i * pitch, 2, rgb, ls[i]);
}
static void nav(uint16_t *fb, int stride, bool top, const char *next_label, bool go) {
    int y = top ? SETUP_TOP_BTN_Y : SETUP_BTN_Y, w = top ? SETUP_TOP_BTN_W : SETUP_BTN_W;
    render_button(fb, stride, top ? SETUP_TOP_BACK_X : SETUP_BACK_X, y, w, SETUP_BTN_H, C_KEY, C_DIM, s_rename ? "CANCEL" : "BACK", 2);
    render_button(fb, stride, top ? SETUP_TOP_NEXT_X : SETUP_NEXT_X, y, w, SETUP_BTN_H,
                  go ? C_GO : C_INNER, go ? C_GO_E : C_EDGE, next_label, 2);
}
/* page dots along the foot: where the keeper is in the flow */
static void dots(uint16_t *fb, int stride, int y) {
    if (s_place || s_rename) return;                    /* one page: nothing to count */
    int n = s_lab ? SETUP_LAB_PAGES : s_about ? SETUP_ABOUT_PAGES : s_birth ? SETUP_BIRTH_PAGES : SETUP_PG_N, cur = s_page - first_page();
    if (s_about) cur = s_page == SETUP_PG_ABOUT ? 0 : s_page == SETUP_PG_FAMILY ? 1 : 2;
    int w = n * 10 - 4, x0 = (PAGE_W - w) / 2;
    for (int i = 0; i < n; i++) render_rect(fb, stride, x0 + i * 10, y, 6, 3, i == cur ? C_EDGE : C_DIM);
}
/* a ring round a fish where it swims: the fish is in the TANK, the drawing on
   the page - on a glass the page does not start at (the bowl, the watch) the
   ring sat PAGE_X / PAGE_Y off its fish until 2026-10-01 */
static void fish_ring(uint16_t *fb, int stride, const fish_t *f, float r, uint32_t rgb) {
    render_ring(fb, stride, f->x - PAGE_X, f->y - PAGE_Y, r, rgb);
}
static void now_fry(const tank_t *t, uint16_t *fb, int stride, const fish_t *f, int x, int y, float r, double clock) {
    (void)t;
    if (!s_lab) { fish_ring(fb, stride, f, r, f->color); return; }
    render_fish_portrait(fb, stride, x, y, f->size * 1.6f, f, 0, clock);
    render_ring(fb, stride, x, y, r, f->color);
}
/* a caption with a name in it, the name in its own colour: `pre` NAME `post` */
static void text_named(uint16_t *fb, int stride, int x, int y, const char *pre, const char *name, uint32_t name_rgb, const char *post) {
    render_text(fb, stride, x, y, 2, C_CAPT, pre); x += render_text_w(pre, 2);
    render_text(fb, stride, x, y, 2, name_rgb, name); x += render_text_w(name, 2);
    if (post) render_text(fb, stride, x, y, 2, C_CAPT, post);
}
static uint32_t dim(uint32_t c, int pct) {              /* c toward the ink, pct% of it left */
    return ((c >> 16 & 255) * pct / 100) << 16 | ((c >> 8 & 255) * pct / 100) << 8 | (c & 255) * pct / 100;
}
/* a fish's own colour as the colour of its name on the dark panel: a dark fish (the ink accent comes through
 * as a body) has its name in light gray instead - black on the panel could not be read (0.3.9.3, Strato) */
static uint32_t name_rgb(uint32_t c) {
    int luma = ((c >> 16 & 255) * 299 + (c >> 8 & 255) * 587 + (c & 255) * 114) / 1000;
    return luma < 90 ? 0xd0d8db : c;
}
/* ITS NATURE's parent marks (0.4.2 audit TP2-9, Strato chose POINTERS 11-13: he is colour blind, and two parents of
   one body colour - or of one nature, one tick over the other - could not be told apart): each parent's tick has a head,
   the first parent's above the bar pointing down, the other's below pointing up, and the same head is the key before
   its name under PARENTS. The colour stays, as the second cue */
static const uint8_t NAT_HEAD[SETUP_NAT_HEAD_H] = { 13, 11, 9, 7, 5, 3, 1 };   /* rows, widest first (pointing down) */
static void nat_head(uint16_t *fb, int stride, int cx, int y0, bool up, uint32_t rgb, bool ink) {
    for (int i = 0; i < SETUP_NAT_HEAD_H; i++) {
        int y = y0 + (up ? SETUP_NAT_HEAD_H - 1 - i : i), h = NAT_HEAD[i] / 2;
        if (ink) render_rect(fb, stride, cx - h - 1, y - 1, NAT_HEAD[i] + 2, 3, C_PANEL);   /* (1 px of ink all round: it reads on the fill) */
        else render_rect(fb, stride, cx - h, y, NAT_HEAD[i], 1, rgb);
    }
}
/* parent p's mark at tx on the bar whose top is by: the tick on an ink outline, its head above (p 0) or below (p 1) */
static void nature_mark(uint16_t *fb, int stride, int tx, int by, int p, uint32_t rgb) {
    const int y0 = p ? SETUP_NAT_HEAD_B_Y(by) : SETUP_NAT_HEAD_A_Y(by);
    render_rect(fb, stride, tx - 2, by - 5, 5, SETUP_FAM_BAR_H + 10, C_PANEL);
    nat_head(fb, stride, tx, y0, p != 0, 0, true);
    render_rect(fb, stride, tx - 1, by - 4, 3, SETUP_FAM_BAR_H + 8, rgb);
    nat_head(fb, stride, tx, y0, p != 0, rgb, false);
}
/* a chevron of 4x4 blocks, `up` pointing up, apex at (cx, y) */
static void chevron(uint16_t *fb, int stride, int cx, int y, bool up, uint32_t rgb) {
    for (int i = 0; i < 5; i++) {
        int yy = up ? y + i * 4 : y - i * 4;
        render_rect(fb, stride, cx - 4 - i * 4, yy, 4, 4, rgb);
        render_rect(fb, stride, cx + i * 4, yy, 4, 4, rgb);
    }
}
/* a small sword leaf for the depth tiles: `h` px tall on the spine x, the
 * plant's own lanceolate taper (widest at 40% up) in its yellow-green */
static void leaf_glyph(uint16_t *fb, int stride, int cx, int y_bot, int h, uint32_t rgb) {
    for (int i = 0; i < h; i++) {
        float u = i / (float)(h - 1);
        float half = 0.6f + 4.4f * (u < 0.4f ? u / 0.4f : (1 - u) / 0.6f);
        int w = (int)(half * 2 + 0.5f); if (w < 1) w = 1;
        render_rect(fb, stride, cx - w / 2, y_bot - i, w, 1, rgb);
    }
}
/* a thin seagrass frond for the sword plant's depth tiles (0.3.9.4): 2 px wide, a gentle wave, the seagrass's green */
static void frond_glyph(uint16_t *fb, int stride, int cx, int y_bot, int h, uint32_t rgb) {
    for (int i = 0; i < h; i++) render_rect(fb, stride, cx + (int)lroundf(1.6f * sinf(i * 0.28f + cx * 0.7f)), y_bot - i, 2, 1, rgb);
}
/* a little castle for the castle's depth tiles: two towers, a wall with
 * merlons, the arch - the stone tones of the real one */
/* a little coral for its depth tiles: a trunk and four branches, rounded
 * with 2 px blocks, in the keeper's colour with a lit tip */
static void coral_glyph(uint16_t *fb, int stride, int cx, int y_bot, uint32_t rgb) {
    uint32_t lit = ((rgb >> 16 & 255) * 3 / 4 + 64) << 16 | ((rgb >> 8 & 255) * 3 / 4 + 64) << 8 | ((rgb & 255) * 3 / 4 + 40);
    render_rect(fb, stride, cx - 2, y_bot - 26, 4, 27, rgb);                       /* the trunk */
    render_rect(fb, stride, cx - 2, y_bot - 28, 4, 2, lit);
    static const int8_t br[4][3] = { { -1, 8, 7 }, { 1, 13, 6 }, { -1, 17, 5 }, { 1, 21, 4 } };   /* side, height, length */
    for (int k = 0; k < 4; k++) {
        int s = br[k][0], y = y_bot - br[k][1], n = br[k][2];
        for (int i = 0; i < n; i++) render_rect(fb, stride, cx + (s > 0 ? 2 + i * 2 : -4 - i * 2), y - i, 2, 3, i == n - 1 ? lit : rgb);
    }
}
/* a little reef cluster for its depth tiles: stones, a tube, a coral sprig,
 * a brain lump, in the look's colours */
static void cluster_glyph(uint16_t *fb, int stride, int cx, int y_bot, const cluster_scheme_t *sc) {
    render_rect(fb, stride, cx - 20, y_bot - 5, 40, 6, 0x7c756a);
    render_rect(fb, stride, cx - 16, y_bot - 8, 10, 4, 0x9a9284); render_rect(fb, stride, cx + 4, y_bot - 8, 12, 4, 0x9a9284);
    render_rect(fb, stride, cx + 8, y_bot - 24, 6, 20, sc->tube); render_rect(fb, stride, cx + 9, y_bot - 25, 4, 2, 0x100418);
    render_rect(fb, stride, cx + 14, y_bot - 17, 5, 13, sc->tube); render_rect(fb, stride, cx + 15, y_bot - 18, 3, 2, 0x100418);
    coral_glyph(fb, stride, cx - 9, y_bot - 4, sc->coral);
    render_rect(fb, stride, cx - 4, y_bot - 12, 12, 8, sc->brain); render_rect(fb, stride, cx - 2, y_bot - 14, 8, 2, sc->brain);
}
static void castle_glyph(uint16_t *fb, int stride, int cx, int y_bot, uint32_t wall, uint32_t roof) {
    const uint32_t tower = 0x87795f, dark = 0x0b1a22, trim = 0xc25f38;
    render_rect(fb, stride, cx - 14, y_bot - 14, 29, 15, wall);
    for (int x = cx - 14; x + 2 <= cx + 14; x += 6) render_rect(fb, stride, x, y_bot - 17, 3, 3, wall);
    render_rect(fb, stride, cx - 21, y_bot - 24, 8, 25, tower);
    for (int i = 0; i < 7; i++) render_rect(fb, stride, cx - 17 - i / 2, y_bot - 31 + i, 1 + i, 1, roof);
    render_rect(fb, stride, cx + 13, y_bot - 20, 8, 21, tower);
    for (int i = 0; i < 6; i++) render_rect(fb, stride, cx + 17 - i / 2, y_bot - 26 + i, 1 + i, 1, roof);
    render_rect(fb, stride, cx - 5, y_bot - 9, 11, 1, trim);
    render_rect(fb, stride, cx - 3, y_bot - 8, 7, 9, dark);
    render_rect(fb, stride, cx - 4, y_bot - 6, 9, 7, dark);
}
/* a little pumpkin lantern for its depth tiles (0.3.6): a squat orange
 * body in three ribs, a stem, a lit face */
static void pumpkin_glyph(uint16_t *fb, int stride, int cx, int y_bot) {
    render_rect(fb, stride, cx - 11, y_bot - 13, 22, 11, 0xe8641a);
    render_rect(fb, stride, cx - 9, y_bot - 15, 18, 15, 0xe8641a);
    render_rect(fb, stride, cx - 13, y_bot - 11, 26, 7, 0xc2420e);
    render_rect(fb, stride, cx - 12, y_bot - 12, 24, 9, 0xe8641a);
    render_rect(fb, stride, cx - 4, y_bot - 14, 1, 13, 0xa83410);    /* the ribs */
    render_rect(fb, stride, cx + 4, y_bot - 14, 1, 13, 0xa83410);
    render_rect(fb, stride, cx - 8, y_bot - 12, 3, 3, 0xf88a2c);     /* the light, upper left */
    render_rect(fb, stride, cx - 1, y_bot - 19, 3, 5, 0x66702a);     /* the stem */
    render_rect(fb, stride, cx - 7, y_bot - 10, 3, 2, 0xffd470);     /* the face, lit */
    render_rect(fb, stride, cx + 4, y_bot - 10, 3, 2, 0xffd470);
    render_rect(fb, stride, cx - 5, y_bot - 6, 10, 2, 0xffa83a);
}
/* a little volcano for its depth tiles (0.3.9): a dark cone, the lava pool
 * at the crater, a crack down the flank, the cave at the foot */
static void volcano_glyph(uint16_t *fb, int stride, int cx, int y_bot) {
    for (int i = 0; i < 24; i++) {                                   /* the cone, wider at the foot */
        int half = 3 + (23 - i) * 15 / 23;
        render_rect(fb, stride, cx - half, y_bot - i, half * 2 + 1, 1, i % 5 == 2 ? 0x362e3d : 0x52475a);
    }
    render_rect(fb, stride, cx - 4, y_bot - 24, 9, 3, 0xffa826);     /* the pool */
    render_rect(fb, stride, cx - 2, y_bot - 24, 5, 1, 0xffe070);
    render_rect(fb, stride, cx - 3, y_bot - 21, 2, 6, 0xf26a1c);     /* a crack, then another */
    render_rect(fb, stride, cx - 4, y_bot - 15, 2, 5, 0xf26a1c);
    render_rect(fb, stride, cx + 2, y_bot - 21, 2, 8, 0xf26a1c);
    render_rect(fb, stride, cx - 3, y_bot - 6, 7, 7, 0x1e0c0c);      /* the cave */
    render_rect(fb, stride, cx - 2, y_bot - 1, 5, 2, 0x9a3012);
}
/* a depth tile: the plant's - two leaves and the keeper's first fish, in
 * the order the choice means - BEHIND (the plant behind the fish): leaves,
 * then the fish over them; AMONG: one leaf, the fish, the other leaf; IN
 * FRONT: the fish, then both leaves over it. The castle's (2026-09-16):
 * the castle and two leaves - BEHIND: the castle, the leaves over it; IN
 * FRONT: the leaves, the castle over them (its depths mean the plant layer) */
static void depth_tile(const tank_t *t, uint16_t *fb, int stride, int x, int y, int w, int h, int z, int item, double clock) {
    const int cx = x + w / 2, cy = y + h / 2, lh = h - 6, yb = y + h - 3;
    const uint32_t la = 0x8dbb48, lb = 0x6c9d38;
    const fish_t *who = t->n_fish > 0 ? &t->fish[0] : NULL;
    if (item == 8) {                                    /* the pumpkin: the castle's reading, the lantern between two leaves */
        if (z == DECOR_Z_BACK) pumpkin_glyph(fb, stride, cx, yb);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) pumpkin_glyph(fb, stride, cx, yb);
        return;
    }
    if (item == 9) {                                    /* the volcano: the castle's reading */
        if (z == DECOR_Z_BACK) volcano_glyph(fb, stride, cx, yb);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) volcano_glyph(fb, stride, cx, yb);
        return;
    }
    if (item == 2) {
        if (z == DECOR_Z_BACK) castle_glyph(fb, stride, cx, yb, 0xa99b7b, 0xc4a95e);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) castle_glyph(fb, stride, cx, yb, 0xa99b7b, 0xc4a95e);
        return;
    }
    if (item == 4) {                                    /* the cluster: the castle's reading - the plants before it, or it before them (0.3.9.4,
                                                           Strato: no fish in a piece's tiles - the fish choose their own depth now) */
        const cluster_scheme_t *sc = &CLUSTER_SCHEMES[tank_cluster_scheme(t)];
        if (z == DECOR_Z_BACK) cluster_glyph(fb, stride, cx, yb, sc);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) cluster_glyph(fb, stride, cx, yb, sc);
        return;
    }
    if (item == 3) {                                    /* the coral: likewise */
        uint32_t rgb = tank_coral_rgb(t);
        if (z == DECOR_Z_BACK) coral_glyph(fb, stride, cx, yb, rgb);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) coral_glyph(fb, stride, cx, yb, rgb);
        return;
    }
    /* the sword plant (0.3.9.4, Strato: "the sword plant should not be an outlier and should have the same layer stacking UI as
       the other decorations"): its broad leaves read against the seagrass, as the castle's against the plants - not against a
       fish, which chooses its own depth now (docs/swim-depth.md) */
    (void)who; (void)cy; (void)clock;
    const uint32_t sg = 0x3f8f5a;
    if (z == DECOR_Z_BACK) { leaf_glyph(fb, stride, cx - 8, yb, lh, la); leaf_glyph(fb, stride, cx + 8, yb, lh, lb); }
    for (int k = -2; k <= 2; k++) frond_glyph(fb, stride, cx + k * 9 - 1, yb, lh - 4 - (k & 1) * 6, sg);
    if (z != DECOR_Z_BACK) { leaf_glyph(fb, stride, cx - 8, yb, lh, la); leaf_glyph(fb, stride, cx + 8, yb, lh, lb); }
}
/* THE ORDER SQUARES (0.4, Strato 11-02: "it looks odd with no examples when there is something like 'behind or in front of x' ...
   a universal object in front / behind squares"): the page's piece against its neighbor in the stack, drawn as ONE universal
   object for every piece - a plain block, gray with a lighter top and a dark rim, read by its shape and value, never its hue.
   BEHIND: the piece's own glyph (the plant's two broad leaves, the pumpkin, ...) and the block over it; IN FRONT: the glyph over
   the block. The real piece is named on the hint line under the squares */
static void block_glyph(uint16_t *fb, int stride, int cx, int y_bot, int w, int h) {
    const int x0 = cx - w / 2, y0 = y_bot - h + 1;
    render_rect(fb, stride, x0 + 1, y0, w - 2, h, 0x1c2430);                      /* the rim, its corners cut */
    render_rect(fb, stride, x0, y0 + 1, w, h - 2, 0x1c2430);
    render_rect(fb, stride, x0 + 2, y0 + 1, w - 4, h - 2, 0x8a919c);              /* the face */
    render_rect(fb, stride, x0 + 1, y0 + 2, w - 2, h - 4, 0x8a919c);
    render_rect(fb, stride, x0 + 2, y0 + 2, w - 4, 3, 0xc9ced6);                  /* its lit top */
    render_rect(fb, stride, x0 + 2, y_bot - 3, w - 4, 2, 0x5f6670);               /* its shaded foot */
}
static void order_piece_glyph(const tank_t *t, uint16_t *fb, int stride, int item, int cx, int yb, int lh) {
    if (item == 8) pumpkin_glyph(fb, stride, cx, yb);
    else if (item == 9) volcano_glyph(fb, stride, cx, yb);
    else if (item == 2) castle_glyph(fb, stride, cx, yb, 0xa99b7b, 0xc4a95e);
    else if (item == 4) cluster_glyph(fb, stride, cx, yb, &CLUSTER_SCHEMES[tank_cluster_scheme(t)]);
    else if (item == 3) coral_glyph(fb, stride, cx, yb, tank_coral_rgb(t));
    else { leaf_glyph(fb, stride, cx - 6, yb, lh, 0x8dbb48); leaf_glyph(fb, stride, cx + 6, yb, lh, 0x6c9d38); }   /* the sword plant */
}
/* one tile: the page's piece behind (front = false) or in front of the block */
static void order_tile(const tank_t *t, uint16_t *fb, int stride, int x, int y, int w, int h, int item, bool front) {
    const int cx = x + w / 2, yb = y + h - 3, lh = h - 6;
    const int bw = item == 0 ? 36 : 30, bh = item == 0 ? 20 : 16;
    const int bx = cx + (item == 0 ? 0 : 9), px = cx - (item == 0 ? 0 : 7);          /* (a piece glyph and the block side by side, overlapping) */
    if (front) { block_glyph(fb, stride, bx, yb, bw, bh); order_piece_glyph(t, fb, stride, item, px, yb, lh); }
    else { order_piece_glyph(t, fb, stride, item, px, yb, lh); block_glyph(fb, stride, bx, yb, bw, bh); }
}
/* the squares' piece (Strato 11-02, the plant's page): with several pieces they toggle the plant against ONE - the one it last
   passed on this page (an arrow's step or a square's), at first the one just in front of it, else just behind; the arrows walk
   the whole stack and the squares follow, so IN FRONT never lights another piece's BEHIND. -1 = no piece beside it */
static int order_ref(const tank_t *t) {
    const int below = tank_decor_neighbor(t, s_item, -1), above = tank_decor_neighbor(t, s_item, 1);
    if (s_ref >= 0 && (s_ref == below || s_ref == above)) return s_ref;
    return above >= 0 ? above : below;
}
/* the two squares: BEHIND | IN FRONT of `ref`, the lit one the page's piece's place; the hint line under them names ref ("BEHIND
   THE PUMPKIN" - Strato 11-02: no caption over them too, it said the same) */
static void order_bar(const tank_t *t, uint16_t *fb, int stride, int bx, int by, int segw, int bh, int tile_h, int item, int ref) {
    const bool front = tank_decor_above(t, item, ref);
    render_rect(fb, stride, bx, by, 2 * segw, bh, C_KEY);
    static const char *const W[2] = { "BEHIND", "IN FRONT" };
    for (int i = 0; i < 2; i++) {
        const int sx = bx + i * segw; const bool on = (i == 1) == front;
        if (on) { render_rect(fb, stride, sx, by, segw, bh, C_INNER); render_rect_edge(fb, stride, sx + 1, by + 1, segw - 2, bh - 2, C_EDGE); }
        else if (i > 0) render_rect(fb, stride, sx, by + 6, 1, bh - 12, C_DIM);
        order_tile(t, fb, stride, sx, by + 4, segw, tile_h, item, i == 1);
        text_c(fb, stride, sx + segw / 2, by + tile_h + 10, 2, on ? C_TEXT : dim(C_CAPT, 45), W[i]);
    }
    render_rect_edge(fb, stride, bx, by, 2 * segw, bh, C_EDGE);
}
/* an order arrow (the placement page): a chevron in a small button, lit or dim */
static void ord_arrow(uint16_t *fb, int stride, int x, int y, bool right, bool on) {
    uint32_t e = on ? C_EDGE : dim(C_CAPT, 45);
    render_button(fb, stride, x, y, SETUP_ORD_W, SETUP_BTN_H, C_INNER, e, "", 2);
    int cx = x + SETUP_ORD_W / 2, cy = y + SETUP_BTN_H / 2;
    for (int i = 0; i < 7; i++) {
        int xx = right ? cx - 3 + i : cx + 3 - i;
        render_rect(fb, stride, xx, cy - 6 + i, 2, 1, e);
        render_rect(fb, stride, xx, cy + 6 - i, 2, 1, e);
    }
}
static void panel(uint16_t *fb, int stride) {
    render_rect(fb, stride, SETUP_X, SETUP_Y, SETUP_W, SETUP_H, C_PANEL);
    render_rect_edge(fb, stride, SETUP_X, SETUP_Y, SETUP_W, SETUP_H, C_EDGE);
    render_rect_edge(fb, stride, SETUP_X + 1, SETUP_Y + 1, SETUP_W - 2, SETUP_H - 2, C_INNER);
}

/* the name page's title: the first run's, the birth flow's, a fish's rename, a critter's (0.4.2: NAME the first time,
   RENAME once it has one - as it came in, so CANCEL's way back reads the same) */
static const char *name_title(char *buf, size_t n) {
    static const char *const CRIT_WORD[CRIT_NAME_N] = { "SNAIL", "SEA URCHIN", "CRAB", "SEAHORSE", "OCTOPUS" };   /* (CRIT_NAME_*) */
    if (s_rename && s_crit >= 0) { snprintf(buf, n, "%s YOUR %s", s_old_name[0] ? "RENAME" : "NAME", CRIT_WORD[s_crit]); return buf; }
    return s_lab ? "NAME YOUR FRY" : s_rename ? "RENAME YOUR FISH" : s_birth ? "NAME THE NEW FRY" : page_fish() == 0 ? "NAME THE FIRST FISH" : "NAME THE SECOND FISH";
}
/* ITS STORY's words (0.4.2) */
#define ABOUT_INCHES 2.0f                               /* a fish's size (~1.0 grown) in inches: a fry ~1, an elder ~3 */
static void about_age(const tank_t *t, int fish, char *out, size_t n) {
    double h = progression_lived_s(t, fish) / 3600.0;
    if (h < 1) snprintf(out, n, "UNDER AN HOUR");
    else if (h < 24) snprintf(out, n, "%d HOUR%s", (int)h, (int)h == 1 ? "" : "S");
    else snprintf(out, n, "%d DAY%s", (int)(h / 24), (int)(h / 24) == 1 ? "" : "S");
}
static void about_from(const tank_t *t, const fish_t *f, char *out, size_t n) {
    char w[PASSPORT_WATERS_MAX + 1];
    uint32_t from = progression_fish_from(t, f);
    if (from_shop(f)) snprintf(out, n, "%s", shop_word(t, f));
    else if (from && from != t->uid) { passport_home_waters(from, w, sizeof w); snprintf(out, n, "%s", w); }   /* a gift: the tank it came from */
    else if (born_next_door(f)) snprintf(out, n, "NEXT DOOR");
    else {                                              /* this tank: the name its keeper gave it, else its HOME WATERS */
        char nm[PASSPORT_TANK_NAME + 1]; shelf_keeper_name(nm);
        if (nm[0]) snprintf(out, n, "%s", nm); else { passport_home_waters(t->uid, w, sizeof w); snprintf(out, n, "%s", w); }
    }
}

/* SAND DOLLAR GIFTS (0.4.2): the dial over the live tank, as a name's wheel; the confirm page a panel */
static void sdg_coin_line(uint16_t *fb, int stride, int cy, const char *pre, int n, const char *post) {
    char num[16]; snprintf(num, sizeof num, "%d", n);
    int w = render_text_w(pre, 2) + 4 + 32 + 4 + render_text_w(num, 2) + (post ? render_text_w(post, 2) : 0);
    int x = (PAGE_W - w) / 2;
    render_text(fb, stride, x, cy - 7, 2, C_CAPT, pre); x += render_text_w(pre, 2) + 4;
    render_sd_coin(fb, stride, x + PAGE_X, cy - 16 + PAGE_Y, 0); x += 36;
    render_text(fb, stride, x, cy - 7, 2, C_TEXT, num); x += render_text_w(num, 2);
    if (post) render_text(fb, stride, x, cy - 7, 2, C_CAPT, post);
}
static void sdg_to_line(uint16_t *fb, int stride, int cy) {   /* TO <badge> NEXT DOOR: the shape, never the color alone */
    const char *a = "TO THE", *b = "TANK NEXT DOOR";
    int w = render_text_w(a, 2) + 8 + 18 + 8 + render_text_w(b, 2), x = (PAGE_W - w) / 2;
    render_text(fb, stride, x, cy - 7, 2, C_CAPT, a); x += render_text_w(a, 2) + 8;
    render_link_badge(fb, stride, x + 9 + PAGE_X, cy + PAGE_Y, 9, s_sdg_badge); x += 18 + 8;
    render_text(fb, stride, x, cy - 7, 2, C_CAPT, b);
}
static void render_sdgift(const tank_t *t, uint16_t *fb, int stride, double clock) {
    const int CX = PAGE_W / 2;
    const int n = sdg_amount(), bal = t->sd_balance;
    if (s_sdg == 2) {
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + 16, 2, C_CAPT, "SEND SAND DOLLARS?");
        char num[8]; snprintf(num, sizeof num, "%d", n);
        int w = 64 + 12 + render_text_w(num, 6), x = CX - w / 2, y = SETUP_Y + 52;
        render_sd_coin(fb, stride, x + 16 + PAGE_X, y + 5 + PAGE_Y, 0);
        render_sd_coin(fb, stride, x + PAGE_X, y + 9 + PAGE_Y, 0);   /* (two coins, a little pile) */
        render_text(fb, stride, x + 64 + 12, y, 6, C_TEXT, num);
        sdg_to_line(fb, stride, SETUP_Y + 132);
        char left[40]; snprintf(left, sizeof left, "YOU WILL HAVE %d LEFT", bal - n);
        text_c(fb, stride, CX, SETUP_Y + 162, 2, C_CAPT, left);
        text_c(fb, stride, CX, SETUP_Y + 196, 2, C_TEXT, "THEY KEEP THEM");
        text_c(fb, stride, CX, SETUP_Y + 218, 2, C_TEXT, "FOR GOOD");
        render_button(fb, stride, SETUP_BACK_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, C_KEY, C_DIM, "BACK", 2);
        render_button(fb, stride, SETUP_NEXT_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, C_GO, C_GO_E, "SEND", 2);
        return;
    }
    render_rect_blend(fb, stride, SU_LEFT, SETUP_SLOT_Y - SETUP_ARROW_GAP - 2, SU_FULL_W, SETUP_SLOT_H + 2 * SETUP_ARROW_GAP + 36, C_PANEL, 150);
    {   /* the title names the door: SEND SAND DOLLARS TO <badge> - the shape, as on the tab (never the color alone) */
        const char *tt = "SEND SAND DOLLARS TO";
        int w = render_text_w(tt, 2) + 8 + 16, x = CX - w / 2;
        render_text(fb, stride, x, SETUP_Y + 7, 2, C_CAPT, tt);
        render_link_badge(fb, stride, x + render_text_w(tt, 2) + 8 + 8 + PAGE_X, SETUP_Y + 14 + PAGE_Y, 8, s_sdg_badge);
    }
    render_button(fb, stride, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, C_KEY, C_DIM, "CANCEL", 2);
    bool ok = sdg_ok(t);
    if (ok) render_button(fb, stride, SETUP_TOP_NEXT_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, C_GO, C_GO_E, "NEXT", 2);
    else {   /* dim: an outline and gray letters (the line under the dial says why) */
        render_rect_edge(fb, stride, SETUP_TOP_NEXT_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, C_DIM);
        text_c(fb, stride, SETUP_TOP_NEXT_X + SETUP_TOP_BTN_W / 2, SETUP_TOP_BTN_Y + (SETUP_BTN_H - 14) / 2, 2, dim(C_CAPT, 45), "NEXT");
    }
    sdg_coin_line(fb, stride, (SETUP_TOP_BTN_Y + SETUP_BTN_H + SETUP_SLOT_Y - SETUP_ARROW_GAP) / 2, "YOU HAVE", bal, NULL);   /* (between the buttons and the chevron) */
    bool lead = true;
    for (int i = 0; i < SDG_SLOTS; i++) {
        int x = sdg_x(i), v = s_sdg_dig[i];
        bool on = i == s_slot;
        lead = lead && v == 0 && i < SDG_SLOTS - 1;          /* a leading zero: dimmed, as a name's unreached slot */
        uint32_t col = on ? C_NEW : lead ? C_DIM : dim(C_NEW, 70);
        char ch[2] = { (char)('0' + v), 0 };
        render_text(fb, stride, x, SETUP_SLOT_Y, SETUP_SLOT_SCALE, col, ch);
        render_rect(fb, stride, x, SETUP_SLOT_Y + SETUP_SLOT_H + 8, SETUP_SLOT_W, 4, on ? C_TEXT : col);
        if (on) {
            chevron(fb, stride, x + SETUP_SLOT_W / 2, SETUP_SLOT_Y - SETUP_ARROW_GAP, true, C_EDGE);
            chevron(fb, stride, x + SETUP_SLOT_W / 2, SETUP_SLOT_Y + SETUP_SLOT_H + SETUP_ARROW_GAP + 4, false, C_EDGE);
        }
    }
    char why[40];
    if (n > bal) snprintf(why, sizeof why, "YOU HAVE ONLY %d", bal < 0 ? 0 : bal);
    else snprintf(why, sizeof why, "SWIPE A NUMBER UP OR DOWN");
    text_c(fb, stride, CX, SETUP_SLOT_Y + SETUP_SLOT_H + 96, 2, n > bal ? C_TEXT : C_CAPT, why);
    (void)clock;
}
/* ---- THE PANEL KEPT (0.4.2; Strato 11-08 on the watch: "the framerate looks slow ... there is only the fins moving, is
 * everything being redrawn?" - it was: 8 fps, the whole panel every frame) ----
 * MORE's pages are still but for their fish. render_keeper_about draws a page whole once a buffer (setup_about_key: all
 * it shows), then each frame only the BAND: the strip between the title and the rows, cleared to the panel and its
 * fish drawn again (setup_about_band_draw). Nothing else is ever drawn in the band - the whole page draws the same
 * helpers into it - so a held frame is the whole page pixel for pixel (--selftest-fishmore). ITS NATURE has no fish:
 * its held frames draw nothing. */
static void about_portrait(const tank_t *t, uint16_t *fb, int stride, double clock) {
    const fish_t *f = &t->fish[page_fish()];
    float ps = f->size * 1.5f; if (ps > 1.5f) ps = 1.5f;
    render_fish_portrait(fb, stride, PAGE_W / 2, SETUP_AB_PORTRAIT_Y, ps, f, FISH_ROOM_FAM, clock);
}
static void fam_portraits(const tank_t *t, uint16_t *fb, int stride, double clock) {
    const fish_t *f = &t->fish[page_fish()];
    float ps = f->size * 2.0f; if (ps > 1.2f) ps = 1.2f;
    if (s_about && f->stage >= STAGE_ADULT)                       /* its story: a grown fish is the GROWN UP already - the one portrait */
        render_fish_portrait(fb, stride, PAGE_W / 2, SETUP_FAM_PORTRAIT_Y, SETUP_FAM_GROWN_SIZE, f, FISH_ROOM_FAM, clock);
    else {
        render_ring(fb, stride, SETUP_FAM_NOW_X, SETUP_FAM_PORTRAIT_Y, 17 * ps + 6, f->color);
        render_fish_portrait(fb, stride, SETUP_FAM_NOW_X, SETUP_FAM_PORTRAIT_Y, ps, f, FISH_ROOM_FAM, clock);
        text_c(fb, stride, (SETUP_FAM_NOW_X + SETUP_FAM_GROWN_X) / 2 - 6, SETUP_FAM_PORTRAIT_Y - 10, 3, C_CAPT, ">");
        render_fish_preview(fb, stride, SETUP_FAM_GROWN_X, SETUP_FAM_PORTRAIT_Y, SETUP_FAM_GROWN_SIZE, f->color, f->fin, f->accent, f, FISH_ROOM_FAM, clock);
    }
}
/* the starter fish's stamp: the panel's top left (Strato's place); a starter FRY shows NOW > GROWN UP there, so its stamp
   goes to the right of the table, where a starter fish has no FROM column */
static void stamp_at(const fish_t *f, int *x, int *y) {
    if (f->stage >= STAGE_ADULT) { *x = SETUP_X + SETUP_STAMP_IN_X; *y = SETUP_Y + SETUP_STAMP_IN_Y; }
    else { *x = SETUP_X + SETUP_W - SETUP_STAMP_IN_X - 4; *y = SETUP_FAM_ROW_Y + 2 * SETUP_FAM_ROW_DY + 7; }
}
bool setup_about_band(const tank_t *t, int box[4]) {
    if (!setup_is_about() || s_page == SETUP_PG_NATURE) return false;
    box[0] = SETUP_X + 3; box[2] = SETUP_X + SETUP_W - 4;
    box[1] = SETUP_Y + 44;                                        /* (under the name: scale 3 at +20 ends at +41) */
    box[3] = s_page == SETUP_PG_ABOUT ? SETUP_AB_ROW_Y - 3 : SETUP_FAM_ROW_Y - 23;   /* (over the rows; FAMILY: over FROM) */
    if (s_page == SETUP_PG_FAMILY && s_stamp_l1) {                /* a grown starter fish's stamp at the top left: the band starts right of it */
        const fish_t *f = &t->fish[page_fish()];
        if (founder(f) && f->stage >= STAGE_ADULT) box[0] = SETUP_X + SETUP_STAMP_IN_X + 66;
    }
    return true;
}
void setup_about_band_draw(const tank_t *t, uint16_t *fb, int stride, double clock) {
    int b[4]; if (!setup_about_band(t, b)) return;
    render_rect(fb, stride, b[0], b[1], b[2] - b[0] + 1, b[3] - b[1] + 1, C_PANEL);
    if (s_page == SETUP_PG_ABOUT) about_portrait(t, fb, stride, clock); else fam_portraits(t, fb, stride, clock);
}
static uint32_t key_add(uint32_t h, uint32_t v) { for (int b = 0; b < 4; b++) { h ^= (v >> (b * 8)) & 255; h *= 16777619u; } return h; }
static uint32_t key_str(uint32_t h, const char *s) { for (; s && *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; } return key_add(h, 0); }
uint32_t setup_about_key(const tank_t *t) {                       /* everything the still page is drawn from (a change: drawn whole) */
    if (!setup_is_about()) return 0;
    const int fi = page_fish(); const fish_t *f = &t->fish[fi];
    uint32_t h = 2166136261u; char v[40];
    h = key_add(h, (uint32_t)s_page); h = key_add(h, (uint32_t)fi); h = key_add(h, f->uid); h = key_add(h, (uint32_t)t->n_fish);
    h = key_str(h, f->name); h = key_add(h, (uint32_t)f->stage); h = key_add(h, (uint32_t)(f->size * 40.0f));
    h = key_add(h, f->color); h = key_add(h, f->accent); h = key_add(h, (uint32_t)f->shape << 16 | f->fins << 8 | f->pattern);
    h = key_add(h, (uint32_t)f->body[0] << 8 | f->body[1]); h = key_add(h, (uint32_t)f->finl[0] << 16 | f->finl[1] << 8 | f->finl[2]);   /* (0.4.3 item 10) */
    h = key_add(h, (uint32_t)f->mutated << 16 | f->from_b << 8 | f->from_carried); h = key_add(h, f->ms_bits);
    about_age(t, fi, v, sizeof v); h = key_str(h, v);
    about_from(t, f, v, sizeof v); h = key_str(h, v);
    h = key_add(h, (uint32_t)f->eaten); h = key_add(h, (uint32_t)f->eaten_player); h = key_add(h, (uint32_t)(f->trust * 8.0f));
    h = key_add(h, (uint32_t)(f->bold * SETUP_FAM_BAR_W)); h = key_add(h, (uint32_t)(f->sociable * SETUP_FAM_BAR_W));
    for (int w = 0; w < 2; w++) {
        parent_t p = parent_of(t, f, w);
        h = key_str(h, p.name); h = key_add(h, p.color); h = key_add(h, (uint32_t)p.known);
        h = key_add(h, (uint32_t)(p.bold * SETUP_FAM_BAR_W)); h = key_add(h, (uint32_t)(p.sociable * SETUP_FAM_BAR_W));
    }
    return h;
}
#define C_CRIT C_EDGE                                   /* a critter's ring and letters: its card's teal (render.c) */
/* a page's MOVING parts (THE LAB KEPT redraws these alone; every other page draws them with the rest) */
static void st_moving(const tank_t *t, uint16_t *fb, int stride, double clock) {       /* BODY / FINS: the stage, the fry */
    const fish_t *f = &t->fish[page_fish()];
    render_stretch_stage(fb, stride, f, !page_is_body(), s_st_axis, clock, setup_lab_black());
    now_fry(t, fb, stride, f, SETUP_ST_NOW_X, SETUP_ST_NOW_Y, 20, clock);
    text_c(fb, stride, SETUP_ST_NOW_X, SETUP_ST_NOW_Y + 26, 2, C_CAPT, "NOW");
    text_c(fb, stride, STG_PLATE_X + STG_PLATE_W - 60, SETUP_ST_TOP + 4, 2, C_CAPT, "GROWN UP");
}
/* BODY / FINS: the three named tiles, each with its little grown fish. The fish swim, so on the lab's held page the row
   is a moving box like the stage - at the SAME clock as the stage's fish (Strato on the square, 11-22: kept in the
   still page they stood frozen, and jumped to a new pose each time a drag changed a tile's frame - "2 or 3 times the
   speed it should be") */
static void st_tiles(const tank_t *t, uint16_t *fb, int stride, double clock) {
    const fish_t *f = &t->fish[page_fish()];
    const bool body = page_is_body();
    float len, dep, fl[3]; tank_body(f, &len, &dep); tank_fin_len(f, fl);
    for (int i = 0; i < SETUP_TILE_N; i++) {
        const int x = SETUP_TILE_X + i * SETUP_TILE_PX, y = SETUP_ST_TILE_Y, w = SETUP_TILE_W, h = SETUP_ST_TILE_H;
        fish_t form = *f; tank_clear_body(&form); tank_clear_fin_len(&form);       /* a tile shows the named one */
        bool near, exact; const char *name;
        if (body) { int sh = ST_SHAPES[i]; form.shape = (uint8_t)sh; form.fins = FINS_CLASSIC; name = SHAPE_NAMES[sh];
                    near = f->shape == sh; exact = near && fabsf(len - BODY_LEN[sh]) < 0.06f && fabsf(dep - BODY_DEP[sh]) < 0.06f; }
        else      { form.fins = (uint8_t)i; name = FINS_NAMES[i];
                    near = f->fins == i; exact = near && fabsf(fl[0] - 1) < 0.006f && fabsf(fl[1] - 1) < 0.006f && fabsf(fl[2] - (i == FINS_CLASSIC ? 0 : 1)) < 0.006f; }
        render_rect(fb, stride, x, y, w, h, C_KEY);
        render_rect_edge(fb, stride, x, y, w, h, near ? C_TEXT : C_DIM);           /* the nearest name: framed ... */
        if (exact) render_rect_edge(fb, stride, x + 1, y + 1, w - 2, h - 2, C_TEXT);   /* ... exactly it: the double frame */
        render_fish_preview(fb, stride, x + w / 2.0f, y + 12, 0.8f, f->color, f->fin, f->accent, &form, FISH_ROOM_ROW, clock);
        text_c(fb, stride, x + w / 2, y + h - 17, 2, near ? C_TEXT : dim(C_CAPT, 60), name);
    }
}
/* COLORS: the three PATTERN tiles, each a little grown fish wearing that pattern - swimming too, so the same */
static void pat_tiles(const tank_t *t, uint16_t *fb, int stride, double clock) {
    const fish_t *f = &t->fish[page_fish()];
    for (int i = 0; i < PATTERN_TILES; i++) {
        const int x = SETUP_PAT_TILE_X + i * SETUP_PAT_TILE_PX, y = SETUP_COLOR_PAT_Y, w = SETUP_PAT_TILE_W, h = SETUP_COLOR_PAT_H;
        const bool on = i == f->pattern;
        fish_t form = *f; form.pattern = (uint8_t)i;
        render_rect(fb, stride, x, y, w, h, C_KEY);
        render_rect_edge(fb, stride, x, y, w, h, on ? C_TEXT : C_DIM);
        if (on) render_rect_edge(fb, stride, x + 1, y + 1, w - 2, h - 2, C_TEXT);
        render_fish_preview(fb, stride, x + w / 2.0f, y + h / 2.0f, 1.4f, f->color, f->fin, f->accent, &form, FISH_ROOM_ROW, clock);
    }
}
static void col_moving(const tank_t *t, uint16_t *fb, int stride, double clock) {      /* COLORS: the fry and the grown fish */
    const fish_t *f = &t->fish[page_fish()];
    now_fry(t, fb, stride, f, SETUP_ED_NOW_X, SETUP_ED_PREVIEW_Y, 17 * f->size + 6, clock);
    if (setup_lab_black()) render_rect(fb, stride, SETUP_ED_GROWN_X - 72, SETUP_ED_PREVIEW_Y - 26, 144, 54, C_PANEL);
    else render_rect_blend(fb, stride, SETUP_ED_GROWN_X - 72, SETUP_ED_PREVIEW_Y - 26, 144, 54, C_PANEL, 150);   /* clear of the buttons */
    render_fish_preview(fb, stride, SETUP_ED_GROWN_X, SETUP_ED_PREVIEW_Y, SETUP_ED_GROWN_SIZE, f->color, f->fin, f->accent, f, FISH_ROOM_CARD, clock);
    text_c(fb, stride, (SETUP_ED_NOW_X + SETUP_ED_GROWN_X) / 2, SETUP_ED_PREVIEW_Y - 10, 3, C_CAPT, ">");
    if (setup_lab_black()) pat_tiles(t, fb, stride, clock);          /* (the held page: its second moving box) */
}
static void name_moving(const tank_t *t, uint16_t *fb, int stride, double clock) {     /* NAME (the lab's): the fry and the grown fish over the letters */
    const fish_t *f = &t->fish[page_fish()];
    now_fry(t, fb, stride, f, SETUP_ED_NOW_X, SETUP_NAME_PREVIEW_Y, 17 * f->size + 6, clock);
    render_rect(fb, stride, SETUP_ED_GROWN_X - 64, SETUP_NAME_PREVIEW_Y - 24, 128, 48, C_PANEL);
    render_fish_preview(fb, stride, SETUP_ED_GROWN_X, SETUP_NAME_PREVIEW_Y, SETUP_NAME_GROWN_SIZE, f->color, f->fin, f->accent, f, FISH_ROOM_FAM, clock);
    text_c(fb, stride, (SETUP_ED_NOW_X + SETUP_ED_GROWN_X) / 2, SETUP_NAME_PREVIEW_Y - 10, 3, C_CAPT, ">");
}
static void tube_moving(const tank_t *t, uint16_t *fb, int stride, double clock) {     /* THE TUBE: the tube, and what it will grow into */
    const fish_t *f = &t->fish[SETUP_LAB_SLOT];
    render_lab_tube(fb, stride, f, f->name, clock);
    render_fish_preview(fb, stride, (LAB_COL2_X + LAB_R) / 2 + 9, 154, 1.5f, f->color, f->fin, f->accent, f, FISH_ROOM_CARD, clock);
}
/* THE LAB KEPT (render.h): the boxes that move, what the rest is drawn from */
int setup_lab_bands(const tank_t *t, int box[SETUP_LAB_BANDS][4]) {
    (void)t;
    if (!setup_lab_black()) return 0;
    #define BOX(i, x0, y0, x1, y1) do { box[i][0] = (x0); box[i][1] = (y0); box[i][2] = (x1); box[i][3] = (y1); } while (0)
    if (page_is_stretch()) { BOX(0, 20, SETUP_ST_TOP - 2, STG_PLATE_X + STG_PLATE_W + 3, SETUP_ST_BOT - 1);   /* the stage, its handles, the fry at its left */
                             BOX(1, SETUP_TILE_X, SETUP_ST_TILE_Y - 9, SETUP_TILE_X + (SETUP_TILE_N - 1) * SETUP_TILE_PX + SETUP_TILE_W - 1, SETUP_ST_TILE_Y + SETUP_ST_TILE_H - 1); return 2; }   /* the tiles' row: their fish swim (a tall sail stands a few px over its tile's top: the box takes it in) */
    if (page_is_colors())  { BOX(0, SETUP_ED_NOW_X - 44, SETUP_ED_PREVIEW_Y - 27, SETUP_ED_GROWN_X + 75, SETUP_ED_PREVIEW_Y + 28);
                             BOX(1, SETUP_PAT_TILE_X, SETUP_COLOR_PAT_Y, SETUP_PAT_TILE_X + (PATTERN_TILES - 1) * SETUP_PAT_TILE_PX + SETUP_PAT_TILE_W - 1, SETUP_COLOR_PAT_Y + SETUP_COLOR_PAT_H - 1); return 2; }
    if (s_page == SETUP_PG_LAB_TUBE) { BOX(0, LAB_TUBE_X - 30, LAB_TUBE_Y - 32, LAB_TUBE_X + LAB_TUBE_W + 30, LAB_TUBE_Y + LAB_TUBE_H + 28);
                                       BOX(1, LAB_COL2_X + 24, 122, 392, 186); return 2; }
    /* NAME, on the letter wheel: the letters only change with a pick and the cursor with its blink - both in the key - so
       the page is held too, and only the fry and the grown fish over it move (the legacy keyboards: whole, every frame) */
    if (s_page == SETUP_PG_LAB_NAME && !s_kbd) { BOX(0, SETUP_ED_NOW_X - 44, SETUP_NAME_PREVIEW_Y - 25, SETUP_ED_GROWN_X + 66, SETUP_NAME_PREVIEW_Y + 25); return 1; }
    #undef BOX
    return 0;
}
void setup_lab_bands_draw(const tank_t *t, uint16_t *fb, int stride, double clock) {
    int box[SETUP_LAB_BANDS][4]; const int n = setup_lab_bands(t, box);
    for (int i = 0; i < n; i++) render_lab_bg(fb, stride, s_page == SETUP_PG_LAB_TUBE, box[i]);   /* the room again, inside the box alone */
    if (!n) return;
    if (page_is_stretch()) { st_moving(t, fb, stride, clock); st_tiles(t, fb, stride, clock); }
    else if (page_is_colors()) col_moving(t, fb, stride, clock);
    else if (s_page == SETUP_PG_LAB_NAME) name_moving(t, fb, stride, clock);
    else tube_moving(t, fb, stride, clock);
}
uint32_t setup_lab_key(const tank_t *t) {
    if (!setup_lab_black()) return 0;
    const fish_t *f = &t->fish[SETUP_LAB_SLOT];
    uint32_t h = key_add(2166136261u, (uint32_t)s_page);
    h = key_add(h, (uint32_t)s_st_can_undo | (uint32_t)(s_same_note > 0) << 1);
    if (!page_is_stretch()) {                           /* (BODY / FINS: every fish on them - the stage's, the fry, the tiles' - is in a moving box, so
                                                           nothing a drag changes is in the still part: a drag never draws the page whole) */
        h = key_add(h, f->color); h = key_add(h, f->accent); h = key_add(h, (uint32_t)f->shape << 16 | f->fins << 8 | f->pattern);
        h = key_add(h, (uint32_t)f->body[0] << 8 | f->body[1]); h = key_add(h, (uint32_t)f->finl[0] << 16 | f->finl[1] << 8 | f->finl[2]);
        for (int i = 0; i < FISH_NAME_MAX; i++) h = key_add(h, (uint8_t)f->name[i]);
    }
    if (s_page == SETUP_PG_LAB_TUBE) h = key_add(h, (uint32_t)progression_lab_why(t));
    if (s_page == SETUP_PG_LAB_NAME) {                  /* the slot picked, the keyboard, and the cursor's blink under an empty slot */
        const bool empty = !slot_val(f->name, s_slot);
        h = key_add(h, (uint32_t)s_slot | (uint32_t)s_kbd << 8 | (uint32_t)(empty && ((int)(t->clock * 2) & 1)) << 16);
    }
    if (h != s_lab_key_last) { s_lab_key_last = h; s_lab_still = t->clock; }
    return h;
}
uint32_t setup_evo_key(const tank_t *t) {             /* THE REVEAL KEPT: everything its still part is drawn from */
    if (!setup_evo_black() || s_fish < 0 || s_fish >= t->n_fish) return 0;
    evo_bench_tick(t);                                  /* (the bench's loop restarts here, first in the frame: the key sees it) */
    evo_page_t pg; evo_fill(t, &pg);
    const fish_t *f = &t->fish[s_fish];
    uint32_t h = key_add(2166136261u, 0x45564fu);       /* ("EVO": never a lab page's key) */
    h = key_add(h, (uint32_t)s_fish | (uint32_t)(uint8_t)pg.sel << 8 | (uint32_t)pg.confirm << 16 | (uint32_t)pg.choosing << 17
                   | (uint32_t)(pg.state[0] == EVP_CLOSED) << 18 | (uint32_t)(pg.state[1] == EVP_CLOSED) << 19 | (uint32_t)(pg.state[2] == EVP_CLOSED) << 20);
    h = key_add(h, pg.sel >= 0 ? pg.part[pg.sel] : 0);  /* (KEEP THE <PART>?) */
    h = key_add(h, pg.away);                            /* (VISITING NEXT DOOR in KEEP's place, TP-7) */
    h = key_add(h, f->uid);
    for (int i = 0; i < FISH_NAME_MAX; i++) h = key_add(h, (uint8_t)f->name[i]);
    return h;
}
static void render_setup_in(const tank_t *t, uint16_t *fb, int stride, double clock);
#ifdef TANK_P4
/* THE BIG SCREENS' PAGE AT TWICE ITS SIZE (0.4.3): the pages the release added with no wide layout of their own - the
   paragon's (the shells, the spin, CHOOSE ONE, KEEP THIS) and THE FISH LAB's - are drawn as the pocket page, into a frame
   of their own, then put on the glass at twice their size, centred (the 10.1's 896 x 736 of 1280 x 800; on the Tab5's 720
   rows the page's top and bottom eight are left out), the rest of the glass in the page's own edge colour. A finger on the
   glass is taken back to the page's own coordinates (setup_touch). Pixel art at 2x: every pixel a 2 x 2 block. */
#include <stdlib.h>
static uint16_t *s_zoom;
bool setup_zoomed(void) { return s_active && (s_evo || setup_lab_black()) && !render_big_pocket(); }   /* (not under the suites' pocket pages) */
static uint16_t *s_zoom_dbg;                            /* (a test's reference: drawn through a frame of its own, the held one left alone) */
void setup_debug_zoom_buf(uint16_t *b) { s_zoom_dbg = b; }
static uint16_t *zoom_buf(void) {
    if (s_zoom_dbg) return s_zoom_dbg;
    if (!s_zoom) s_zoom = calloc((size_t)TANK_W * TANK_H, sizeof *s_zoom);
    return s_zoom;
}
uint16_t *setup_zoom_buf(void) { return zoom_buf(); }
/* One box of the page (page px, inclusive) put on the glass at 2x: the same pixels zoom_put puts there */
void setup_zoom_put_box(uint16_t *fb, int stride, int x0, int y0, int x1, int y1, short glass[4]) {
    const uint16_t *z = zoom_buf();
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > PAGE_W - 1) x1 = PAGE_W - 1;
    if (y1 > PAGE_H - 1) y1 = PAGE_H - 1;
    int gy0 = ZM_OY + y0 * ZM_S, gy1 = ZM_OY + y1 * ZM_S + ZM_S - 1;
    if (gy0 < 0) gy0 = 0;
    if (gy1 > TANK_H - 1) gy1 = TANK_H - 1;
    glass[0] = (short)(ZM_OX + x0 * ZM_S); glass[1] = (short)gy0; glass[2] = (short)(ZM_OX + x1 * ZM_S + ZM_S - 1); glass[3] = (short)gy1;
    if (!z || x0 > x1 || gy0 > gy1) { glass[2] = (short)(glass[0] - 1); return; }   /* (nothing put: an empty box) */
    for (int y = gy0; y <= gy1; y++) {
        const uint16_t *s = z + (size_t)(PAGE_Y + (y - ZM_OY) / ZM_S) * TANK_W + PAGE_X + x0;
        uint16_t *o = fb + (size_t)y * stride + ZM_OX + x0 * ZM_S;
        for (int x = x0; x <= x1; x++) { o[0] = o[1] = *s++; o += 2; }
    }
}
static void zoom_put(uint16_t *fb, int stride) {
    const uint16_t *zb = zoom_buf();
    const uint16_t edge = zb[PAGE_Y * TANK_W + PAGE_X];
    for (int y = 0; y < TANK_H; y++) {
        uint16_t *d = fb + (size_t)y * stride;
        const int py = (y - ZM_OY) / ZM_S;
        if (y < ZM_OY || py >= PAGE_H) { for (int x = 0; x < TANK_W; x++) d[x] = edge; continue; }
        const uint16_t *s = zb + (size_t)(PAGE_Y + py) * TANK_W + PAGE_X;
        for (int x = 0; x < ZM_OX; x++) d[x] = edge;
        uint16_t *o = d + ZM_OX;
        for (int x = 0; x < PAGE_W; x++) { o[0] = o[1] = s[x]; o += 2; }
        for (int x = ZM_OX + PAGE_W * ZM_S; x < TANK_W; x++) d[x] = edge;
    }
}
void setup_zoom_put(uint16_t *fb, int stride) { if (zoom_buf()) zoom_put(fb, stride); }   /* the zoom's frame on the glass whole, as render_setup puts it */
#else
bool setup_zoomed(void) { return false; }
#endif
void render_setup(const tank_t *t, uint16_t *fb, int stride, double clock) {
#ifdef TANK_P4
    if (setup_zoomed()) {
        uint16_t *zb = zoom_buf();
        if (zb && fb != zb) {                           /* (a draw INTO the zoom's frame is the page at 1x: the reveal held at 2x, render.c) */
            uint16_t *out = fb; const int ostride = stride; fb = zb; stride = TANK_W;
            if (!setup_lab_black()) { memcpy(zb, out, (size_t)TANK_W * TANK_H * sizeof *zb); render_setup_in(t, fb, stride, clock); }   /* (the page over the frame as it was) */
            else {
                int box[SETUP_LAB_BANDS][4]; const bool bands = setup_lab_bands(t, box) > 0;
                setup_lab_key(t);
                render_lab_bg(fb, stride, s_page == SETUP_PG_LAB_TUBE, NULL);
                render_setup_in(t, fb, stride, bands ? s_lab_still : clock);
                if (bands) setup_lab_bands_draw(t, fb, stride, clock);
            }
            zoom_put(out, ostride);
            return;
        }
    }
#endif
    if (!setup_lab_black()) { render_setup_in(t, fb, stride, clock); return; }
    /* a lab page, whole: black, the still part at ONE moment (the same in both buffers: a tile's little fish never
       flickers between two poses), then the moving boxes at this frame's */
    int box[SETUP_LAB_BANDS][4]; const bool bands = setup_lab_bands(t, box) > 0;
    setup_lab_key(t);
    render_lab_bg(fb, stride, s_page == SETUP_PG_LAB_TUBE, NULL);                /* the lab's room: once, into the held page */
    render_setup_in(t, fb, stride, bands ? s_lab_still : clock);
    if (bands) setup_lab_bands_draw(t, fb, stride, clock);
}
static void render_setup_in(const tank_t *t, uint16_t *fb, int stride, double clock) {
    if (s_same_note > 0) s_same_note--;
    if (!s_active) return;
    if (s_sdg) { render_sdgift(t, fb, stride, clock); return; }
    if (s_evo) { render_evolve(t, fb, stride, clock); return; }
    const int CX = PAGE_W / 2;
    const int FLOOR = TANK_BOT - 16 - PAGE_Y;           /* the tank's sand line, in the page's coordinates */
    if (s_page == SETUP_PG_WELCOME) {
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + 36, 3, C_TEXT, "WELCOME");
        static const char *const ls[] = {
            "TWO FRY HAVE MOVED IN.",
            "THEY EAT, PLAY, REST AND",
            "GROW UP WHILE YOU WATCH.",
            "EACH ONE THINKS FOR ITSELF.",
            "FIRST, LET'S MEET THEM.",
        };
        lines_c(fb, stride, SETUP_Y + 96, 24, C_CAPT, ls, 5);
        render_button(fb, stride, SETUP_MID_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, C_INNER, C_EDGE, "NEXT", 2);
        dots(fb, stride, SETUP_Y + SETUP_H - 8);
    } else if (s_page == SETUP_PG_BUBBLES) {
        /* the live tank with a stripe down the column: drag it anywhere */
        text_c(fb, stride, CX, SETUP_Y + 7, 2, C_CAPT, "PLACE THE BUBBLES");
        nav(fb, stride, true, "NEXT", false);
        int bx = (int)t->bubble_x - PAGE_X;
        render_rect_blend(fb, stride, bx - 22, SETUP_TOP_BTN_Y + SETUP_BTN_H + 10, 44, FLOOR - (SETUP_TOP_BTN_Y + SETUP_BTN_H + 10), C_EDGE, 46);
        chevron(fb, stride, bx, FLOOR - 34, true, C_EDGE);
        text_c(fb, stride, CX, 296, 2, C_CAPT, "DRAG THEM LEFT OR RIGHT");
        dots(fb, stride, PAGE_H - 14);
    } else if (s_page == SETUP_PG_PLACE) {
        /* the live tank with a stripe over the piece's footprint: drag it
           anywhere on the water; the layer row picks its depth (the tank
           under the page redraws with it, so the fish and grass show the choice) */
        char title[40]; snprintf(title, sizeof title, "PLACE THE %s", SD_ITEMS[s_item].name);
        text_c(fb, stride, CX, SETUP_Y + 7, 2, C_CAPT, title);
        render_button(fb, stride, SETUP_TOP_DONE_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, C_GO, C_GO_E, "DONE", 2);
        /* SELL, top left (2026-09-24): a tap arms it and names the refund, a
           second tap sells the piece back to the shop and closes the page */
        if (s_sell_armed) { char ok[16]; snprintf(ok, sizeof ok, "+%d OK?", progression_sell_value_of(t, s_item));
                            render_button(fb, stride, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, C_GO, C_GO_E, ok, 2); }
        else render_button(fb, stride, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, C_INNER, C_DIM, "SELL", 2);
        /* SHELVE beside it (0.3.9): put the piece away for later, kept as it is */
        render_button(fb, stride, SETUP_TOP_SHELVE_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, C_INNER, C_EDGE, "SHELVE", 2);
        /* LOCK between SHELVE and DONE (0.3.9): the word says the state - LOCK
           (open, an outlined button) or LOCKED (shut, a filled one) - never the
           color alone (Strato is color blind) */
        if (tank_decor_locked(t, s_item)) render_button(fb, stride, SETUP_TOP_LOCK_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, C_FILL, C_TEXT, "LOCKED", 2);
        else render_button(fb, stride, SETUP_TOP_LOCK_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, C_INNER, C_EDGE, "LOCK", 2);
        /* the ORDER arrows (2026-10-17): a step back / forward in the stack of
           pieces at this depth, lit when there is a piece to pass; the relation
           is named on the hint line ("UNDER THE CORAL"). At the ends of the
           DEPTH row since 0.3.9; the plant (no stack, three tiles) has none */
        int below = tank_decor_neighbor(t, s_item, -1), above = tank_decor_neighbor(t, s_item, 1);
        s_plant_two = s_item == 0 && order_ref(t) >= 0;                    /* (the plant: its order squares a row down, the arrows with them) */
        const int ord_y = s_plant_two ? SETUP_ORD2_Y : SETUP_ORD_Y;
        ord_arrow(fb, stride, SETUP_ORD_X0, ord_y, false, below >= 0);   /* (the sword plant too since 0.3.9.4: a piece in the stack like the rest) */
        ord_arrow(fb, stride, SETUP_ORD_X1, ord_y, true, above >= 0);
        /* the relation, in the depth bar's own words (Strato 2026-10-17: "behind
           the coral" / "in front of the coral"); "THE" goes when the line is too
           long for where it sits (the hint line, or beside COLOR / LOOK) */
        char rel[40] = "";
        int rel_avail = PAGE_W - 2 * 56 - 8;                               /* the hint line: the box every glass shows whole */
        if (s_item == 3) rel_avail = (SETUP_COL_N - 1) * SETUP_COL_PX + SETUP_COL_W - render_text_w("COLOR", 2) - 14;
        if (s_item == 4) rel_avail = (SETUP_LOOK_N - 1) * SETUP_LOOK_PX + SETUP_LOOK_W - render_text_w("LOOK", 2) - 14;
        for (int pass = 0; pass < 2; pass++) {
            const char *the = pass ? "" : "THE ";
            if (above >= 0) snprintf(rel, sizeof rel, "BEHIND %s%s", the, SD_ITEMS[above].name);
            else if (below >= 0) snprintf(rel, sizeof rel, "IN FRONT OF %s%s", the, SD_ITEMS[below].name);
            if (render_text_w(rel, 2) <= rel_avail) break;
        }
        /* the DEPTH bar: one outlined box, three joined segments, the chosen
           one lit; a picture tile on each and the word under it */
        static const char *const zl[DECOR_Z_N] = { "BEHIND", "AMONG", "IN FRONT" };
        static const char *const hint[DECOR_Z_N] = { "IN FRONT OF THE SEAGRASS", "", "IN FRONT OF THE SEAGRASS" };   /* (the sword plant:
                                                                   one depth since 0.4, over the seagrass - the fish choose their own) */
        static const char *const castle_hint[DECOR_Z_N] = { "THE PLANTS GROW IN FRONT OF IT", "", "IT STANDS IN FRONT OF THE PLANTS" };
        static const char *const coral_hint[DECOR_Z_N] = { "BEHIND THE GRASS AND THE FISH", "", "IN FRONT OF EVERYTHING" };
        int z = tank_decor_z(t, s_item), zi = tank_decor_z_index(s_item, z);
        int n = tank_decor_z_count(s_item), bw = n * SETUP_DEPTH_SEG_W, bx = (PAGE_W - bw) / 2;   /* the item's own depths */
        if (n > 1) {                                                       /* (the plant has one depth: no bar, 0.4) */
        text_c(fb, stride, CX, SETUP_DEPTH_Y - 18, 2, C_CAPT, "DEPTH");
        render_rect(fb, stride, bx, SETUP_DEPTH_Y, bw, SETUP_DEPTH_H, C_KEY);
        for (int i = 0; i < n; i++) {
            int sx = bx + i * SETUP_DEPTH_SEG_W, zi_ = tank_decor_z_at(s_item, i);
            if (i == zi) { render_rect(fb, stride, sx, SETUP_DEPTH_Y, SETUP_DEPTH_SEG_W, SETUP_DEPTH_H, C_INNER);
                           render_rect_edge(fb, stride, sx + 1, SETUP_DEPTH_Y + 1, SETUP_DEPTH_SEG_W - 2, SETUP_DEPTH_H - 2, C_EDGE); }
            else if (i > 0) render_rect(fb, stride, sx, SETUP_DEPTH_Y + 6, 1, SETUP_DEPTH_H - 12, C_DIM);   /* a divider */
            depth_tile(t, fb, stride, sx, SETUP_DEPTH_Y + 4, SETUP_DEPTH_SEG_W, SETUP_DEPTH_TILE_H, zi_, s_item, clock);
            text_c(fb, stride, sx + SETUP_DEPTH_SEG_W / 2, SETUP_DEPTH_Y + SETUP_DEPTH_TILE_H + 10, 2, i == zi ? C_TEXT : dim(C_CAPT, 45), zl[zi_]);
        }
        render_rect_edge(fb, stride, bx, SETUP_DEPTH_Y, bw, SETUP_DEPTH_H, C_EDGE);
        }
        if (s_item == 0) {                                 /* THE SWORD PLANT (0.4.1, Strato 11-05): the SEAGRASS squares in the DEPTH bar's place - BEHIND |
                                                               IN FRONT of the seagrass, the old plant tiles (its leaves under or over the fronds) - under THE
                                                               SEAGRASS; with a piece beside it, the ORDER SQUARES a row down under that piece's name. Each row's
                                                               caption names what it is behind or in front of: no hint line */
            const bool under = tank_plant_under(t);
            text_c(fb, stride, CX, SETUP_DEPTH_Y - 18, 2, C_CAPT, "THE SEAGRASS");
            render_rect(fb, stride, SETUP_ORDSQ_X, SETUP_DEPTH_Y, 2 * SETUP_DEPTH_SEG_W, SETUP_DEPTH_H, C_KEY);
            static const char *const W[2] = { "BEHIND", "IN FRONT" };
            for (int i = 0; i < 2; i++) {
                const int sx = SETUP_ORDSQ_X + i * SETUP_DEPTH_SEG_W; const bool on = (i == 0) == under;
                if (on) { render_rect(fb, stride, sx, SETUP_DEPTH_Y, SETUP_DEPTH_SEG_W, SETUP_DEPTH_H, C_INNER);
                          render_rect_edge(fb, stride, sx + 1, SETUP_DEPTH_Y + 1, SETUP_DEPTH_SEG_W - 2, SETUP_DEPTH_H - 2, C_EDGE); }
                else if (i > 0) render_rect(fb, stride, sx, SETUP_DEPTH_Y + 6, 1, SETUP_DEPTH_H - 12, C_DIM);
                depth_tile(t, fb, stride, sx, SETUP_DEPTH_Y + 4, SETUP_DEPTH_SEG_W, SETUP_DEPTH_TILE_H, i == 0 ? DECOR_Z_BACK : DECOR_Z_FRONT, 0, clock);
                text_c(fb, stride, sx + SETUP_DEPTH_SEG_W / 2, SETUP_DEPTH_Y + SETUP_DEPTH_TILE_H + 10, 2, on ? C_TEXT : dim(C_CAPT, 45), W[i]);
            }
            render_rect_edge(fb, stride, SETUP_ORDSQ_X, SETUP_DEPTH_Y, 2 * SETUP_DEPTH_SEG_W, SETUP_DEPTH_H, C_EDGE);
            const int ref = order_ref(t);
            if (ref >= 0) {
                char cap[40]; snprintf(cap, sizeof cap, "THE %s", SD_ITEMS[ref].name);
                text_c(fb, stride, CX, SETUP_ORDSQ2_Y - 18, 2, C_CAPT, cap);
                order_bar(t, fb, stride, SETUP_ORDSQ_X, SETUP_ORDSQ2_Y, SETUP_DEPTH_SEG_W, SETUP_DEPTH_H, SETUP_DEPTH_TILE_H, s_item, ref);
            }
            rel[0] = 0;
        }
        if (s_item == 4) {
            /* the cluster: a LOOK row of three tiles - the coral, the tubes and
               the brain as three bands over the look's name - the pick ringed */
            int look = tank_cluster_scheme(t);
            render_rect_blend(fb, stride, SU_LEFT, SETUP_LOOK_Y - 22, SU_FULL_W, SETUP_LOOK_H + 30, C_PANEL, 110);
            render_text(fb, stride, SETUP_LOOK_X + 2, SETUP_LOOK_Y - 18, 2, C_CAPT, "LOOK");
            if (rel[0]) render_text(fb, stride, SETUP_LOOK_X + SETUP_LOOK_N * SETUP_LOOK_PX - SETUP_LOOK_PX + SETUP_LOOK_W - 2 - render_text_w(rel, 2), SETUP_LOOK_Y - 18, 2, C_CAPT, rel);
            for (int i = 0; i < SETUP_LOOK_N; i++) {
                int x = SETUP_LOOK_X + i * SETUP_LOOK_PX; bool on = i == look;
                const cluster_scheme_t *sc = &CLUSTER_SCHEMES[i];
                render_rect(fb, stride, x, SETUP_LOOK_Y, SETUP_LOOK_W, SETUP_LOOK_H, C_KEY);
                int bw = (SETUP_LOOK_W - 6) / 3;
                render_rect(fb, stride, x + 3, SETUP_LOOK_Y + 3, bw, 14, sc->coral);
                render_rect(fb, stride, x + 3 + bw, SETUP_LOOK_Y + 3, bw, 14, sc->tube);
                render_rect(fb, stride, x + 3 + 2 * bw, SETUP_LOOK_Y + 3, SETUP_LOOK_W - 6 - 2 * bw, 14, sc->brain);
                text_c(fb, stride, x + SETUP_LOOK_W / 2, SETUP_LOOK_Y + 20, 2, on ? C_TEXT : dim(C_CAPT, 45), sc->name);
                render_rect_edge(fb, stride, x, SETUP_LOOK_Y, SETUP_LOOK_W, SETUP_LOOK_H, on ? C_TEXT : C_DIM);
                if (on) render_rect_edge(fb, stride, x + 1, SETUP_LOOK_Y + 1, SETUP_LOOK_W - 2, SETUP_LOOK_H - 2, C_TEXT);
            }
        } else if (s_item == 3) {
            /* the coral: no hint line - a COLOR row instead (the fish colour
               page's swatches, shorter), the pick ringed, the hint under the
               row's caption */
            uint32_t rgb = tank_coral_rgb(t);
            render_rect_blend(fb, stride, SU_LEFT, SETUP_COL_Y - 22, SU_FULL_W, SETUP_COL_H + 30, C_PANEL, 110);   /* a band the swatches read on, like the colour page's */
            render_text(fb, stride, SETUP_COL_X + 2, SETUP_COL_Y - 18, 2, C_CAPT, "COLOR");
            if (rel[0]) render_text(fb, stride, SETUP_COL_X + (SETUP_COL_N - 1) * SETUP_COL_PX + SETUP_COL_W - 2 - render_text_w(rel, 2), SETUP_COL_Y - 18, 2, C_CAPT, rel);
            (void)coral_hint;
            for (int i = 0; i < SETUP_COL_N; i++) {
                int x = SETUP_COL_X + i * SETUP_COL_PX;
                bool on = CORAL_PAL[i] == rgb;
                render_rect(fb, stride, x, SETUP_COL_Y, SETUP_COL_W, SETUP_COL_H, CORAL_PAL[i]);
                render_rect_edge(fb, stride, x, SETUP_COL_Y, SETUP_COL_W, SETUP_COL_H, on ? C_TEXT : C_DIM);
                if (on) render_rect_edge(fb, stride, x + 1, SETUP_COL_Y + 1, SETUP_COL_W - 2, SETUP_COL_H - 2, C_TEXT);
            }
        } else if (s_item != 0) text_c(fb, stride, CX, SETUP_DEPTH_HINT_Y, 2, C_CAPT, rel[0] ? rel : s_item == 2 || s_item == 8 || s_item == 9 ? castle_hint[z] : hint[z]);
        float x0 = tank_decor_x(t, s_item) - tank_piece_half_w(t, s_item) - 10, x1 = tank_decor_x(t, s_item) + tank_piece_half_w(t, s_item) + 10, top = TANK_BOT - 16 - 40;
        if (s_item == 0) tank_veg_bed(t, 3, NULL, NULL, &top, NULL);   /* the leaves' reach */
        else top = tank_decor_fy(t, s_item) - tank_piece_height(t, s_item);   /* its highest pixel on its depth's floor line (0.3.9) */
        x0 -= PAGE_X; x1 -= PAGE_X; top -= PAGE_Y;                     /* the piece is in the tank; the stripe is drawn on the page */
        int sy = (int)top - 8; if (sy < place_y()) sy = place_y();
        render_rect_blend(fb, stride, (int)x0, sy, (int)(x1 - x0), FLOOR - sy + 4, C_EDGE, 46);
        chevron(fb, stride, (int)((x0 + x1) * 0.5f), FLOOR - 34, true, C_EDGE);
        text_c(fb, stride, CX, 296, 2, C_CAPT, "DRAG IT LEFT OR RIGHT");
    } else if (page_is_name() && s_kbd) {
        /* the two rejected designs, drawn as they were: a panel over the tank,
           the name as underlined slots (the next free one blinks; an untouched
           default sits dimmed, as a placeholder does), the keys */
        const fish_t *f = s_crit >= 0 ? NULL : &t->fish[page_fish()];
        const char *nm = page_name_c(t);
        const uint32_t nrgb = f ? name_rgb(f->color) : C_CRIT;
        bool grid = s_kbd == SETUP_KBD_GRID;
        panel(fb, stride);
        char tb[32]; text_c(fb, stride, CX, SETUP_Y + (grid ? 12 : 7), 2, C_CAPT, name_title(tb, sizeof tb));
        if (grid && f) render_fish_preview(fb, stride, SETUP_X + 88, SETUP_Y + 72, 2.0f, f->color, f->fin, f->accent, f, FISH_ROOM_CARD, clock);
        else if (grid) render_crit_portrait(fb, stride, t, s_crit, SETUP_X + 88, SETUP_Y + 72);
        const int SL = 22, NX = grid ? SETUP_X + 160 : CX - FISH_NAME_MAX * SL / 2 + 2, NY = grid ? SETUP_Y + 58 : SETUP_TOP_BTN_Y + (SETUP_BTN_H - 28) / 2;
        int n = name_len(nm);
        for (int i = 0; i < FISH_NAME_MAX; i++) {
            int x = NX + i * SL;
            bool cursor = i == n && ((int)(clock * 2) & 1);
            render_rect(fb, stride, x, NY + 26, SL - 4, 2, cursor ? C_EDGE : i < n ? nrgb : C_DIM);
            if (i < n) { char ch[2] = { nm[i], 0 }; render_text(fb, stride, x + 3, NY, 3, s_typed ? C_TEXT : C_CAPT, ch); }
        }
        if (grid) for (int k = 0; k <= SETUP_KEY_DEL; k++) {
            int x = KG_X + kg_col(k) * KG_PX, y = KG_Y + kg_row(k) * KG_PY, w = kg_w(k);
            render_rect(fb, stride, x, y, w, KG_H, C_KEY);
            render_rect_edge(fb, stride, x, y, w, KG_H, C_DIM);
            char lab[4] = { (char)('A' + k), 0 };
            if (k == SETUP_KEY_DEL) strcpy(lab, "DEL");
            render_text(fb, stride, x + (w - render_text_w(lab, 2)) / 2, y + (KG_H - 14) / 2, 2, C_TEXT, lab);
        } else for (int cell = 0; cell < KP_COLS * KP_ROWS; cell++) {
            int k = kp_key(cell), x = KP_X + (cell % KP_COLS) * KP_PX, y = KP_Y + (cell / KP_COLS) * KP_PY;
            bool special = k >= 26;
            render_rect(fb, stride, x, y, KP_W, KP_H, special ? C_INNER : C_KEY);
            render_rect_edge(fb, stride, x, y, KP_W, KP_H, special ? C_EDGE : C_DIM);
            char lab[4] = { (char)('A' + k), 0 };
            if (k == SETUP_KEY_DEL) strcpy(lab, "DEL");
            if (k == SETUP_KEY_PAGE) strcpy(lab, s_kb_page ? "A-M" : "N-Z");
            render_text(fb, stride, x + (KP_W - render_text_w(lab, 3)) / 2, y + (KP_H - 21) / 2, 3, C_TEXT, lab);
        }
        nav(fb, stride, !grid, s_rename ? "DONE" : "NEXT", s_rename);
        if (grid) dots(fb, stride, SETUP_Y + SETUP_H - 8);
    } else if (page_is_name()) {
        /* straight on the tank: the fish being named wears a ring in its own
           colour, its name spans the middle in that colour, the active slot
           bright with the chevrons, the rest dimmed; slots past the first
           blank are just faint underlines */
        const fish_t *f = s_crit >= 0 ? NULL : &t->fish[page_fish()];
        const char *nm = page_name_c(t);
        const uint32_t nrgb = f ? name_rgb(f->color) : C_CRIT;
        if (f && setup_lab_black()) { /* (the lab's held page: the fry and the grown fish are its moving box - name_moving) */ }
        else if (f) now_fry(t, fb, stride, f, SETUP_ED_NOW_X, SETUP_NAME_PREVIEW_Y, 17 * f->size + 6, clock);
        else {                                                  /* a critter (0.4.2): ringed where it is, as on its card; its card's art
                                                                   on a plate where a fish would stand on stage */
            float rx, ry, rr;
            if (render_crit_spot(t, s_crit, &rx, &ry, &rr)) render_ring(fb, stride, rx - PAGE_X, ry - PAGE_Y, rr, C_CRIT);
            const int px = SETUP_STAGE_X - PAGE_X, py = SETUP_STAGE_NAME_Y - PAGE_Y;
            render_rect_blend(fb, stride, px - 64, py - 24, 128, 48, C_PANEL, 150);
            render_crit_portrait(fb, stride, t, s_crit, px, py);
        }
        if (name_beside(t) && !setup_lab_black()) {             /* the form they chose, in view while they name it (0.4) */
            render_rect_blend(fb, stride, SETUP_ED_GROWN_X - 64, SETUP_NAME_PREVIEW_Y - 24, 128, 48, C_PANEL, 150);
            render_fish_preview(fb, stride, SETUP_ED_GROWN_X, SETUP_NAME_PREVIEW_Y, SETUP_NAME_GROWN_SIZE, f->color, f->fin, f->accent, f, FISH_ROOM_FAM, clock);
            text_c(fb, stride, (SETUP_ED_NOW_X + SETUP_ED_GROWN_X) / 2, SETUP_NAME_PREVIEW_Y - 10, 3, C_CAPT, ">");
        }
        render_rect_blend(fb, stride, SU_LEFT, SETUP_SLOT_Y - SETUP_ARROW_GAP - 2, SU_FULL_W, SETUP_SLOT_H + 2 * SETUP_ARROW_GAP + 36, C_PANEL, 150);
        char tb[32]; text_c(fb, stride, CX, SETUP_Y + 7, 2, C_CAPT, name_title(tb, sizeof tb));
        nav(fb, stride, true, s_rename ? "DONE" : "NEXT", s_rename);
        int n = name_len(nm);
        if (s_slot > n) s_slot = n;
        for (int i = 0; i < FISH_NAME_MAX; i++) {
            int x = SETUP_SLOT_X + i * SETUP_SLOT_PX;
            bool on = i == s_slot, reach = i <= n;
            uint32_t col = on ? nrgb : reach ? dim(nrgb, 55) : C_DIM;
            int v = slot_val(nm, i);
            if (v) { char ch[2] = { (char)('A' + v - 1), 0 }; render_text(fb, stride, x, SETUP_SLOT_Y, SETUP_SLOT_SCALE, col, ch); }
            render_rect(fb, stride, x, SETUP_SLOT_Y + SETUP_SLOT_H + 8, SETUP_SLOT_W, 4,
                        on && !v && ((int)(clock * 2) & 1) ? C_TEXT : col);
            if (on) {
                chevron(fb, stride, x + SETUP_SLOT_W / 2, SETUP_SLOT_Y - SETUP_ARROW_GAP, true, C_EDGE);
                chevron(fb, stride, x + SETUP_SLOT_W / 2, SETUP_SLOT_Y + SETUP_SLOT_H + SETUP_ARROW_GAP + 4, false, C_EDGE);
            }
        }
        text_c(fb, stride, CX, LAB_BENCH_Y + (LAB_BENCH_H - 14) / 2, 2, C_CAPT, "SWIPE A LETTER UP OR DOWN");   /* (the lab: centred in the bench's top) */
        dots(fb, stride, PAGE_H - 14);
    } else if (s_lab && s_page == SETUP_PG_LAB_TUBE) {
        /* THE TUBE: the fry in its tube at the left, what it will grow into and its recipe at the right, one button */
        const fish_t *f = &t->fish[SETUP_LAB_SLOT];
        if (!setup_lab_black()) render_rect_blend(fb, stride, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, C_PANEL, 240);
        /* THE LAB'S GRID: the heading, BACK and the rack share the left margin, BACK is the rack's width; the right column's
           words, the recipe and POUR IT IN share LAB_COL2_X; POUR and the rack's foot stand on the bench's top line */
        render_text(fb, stride, LAB_L + (PAGE_BOWL ? 24 : 0), PAGE_BOWL ? 18 : 14, 2, C_CAPT, "THE FISH LAB");   /* (the bowl: in from the circle's edge) */
        render_button(fb, stride, LAB_L, SETUP_LAB_BACK_Y, SETUP_LAB_BACK_W, SETUP_BTN_H, C_KEY, C_DIM, "BACK", 2);
        if (!setup_lab_black()) tube_moving(t, fb, stride, clock);
        render_text(fb, stride, LAB_COL2_X, LAB_TUBE_Y - 30, 2, C_CAPT, "IT WILL GROW INTO");
        char wb[24], wf[24]; lab_words(f, wb, sizeof wb, wf, sizeof wf);
        const char *const row[4][2] = { { "BODY", wb }, { "FINS", wf }, { "PATTERN", PATTERN_NAMES[f->pattern < PATTERN_N ? f->pattern : 0] }, { "HIDDEN", "A SURPRISE" } };
        for (int i = 0; i < 4; i++) {
            render_text(fb, stride, LAB_COL2_X, 200 + i * 22, 2, C_CAPT, row[i][0]);
            render_text(fb, stride, LAB_COL2_X + 96, 200 + i * 22, 2, C_TEXT, row[i][1]);
        }
        const int why = progression_lab_why(t);
        /* the button's words, then the PRICE as the shop writes one - its sand dollar coin and the number (Strato 11-22:
           "250 is ambiguous") - the pair centred in the button. A full tank has no price to say */
        const bool ok = why == LAB_OK;
        const char *words = setup_lab_pour_word(t);
        render_button(fb, stride, SETUP_LAB_BTN_X, SETUP_LAB_BTN_Y, SETUP_LAB_BTN_W, SETUP_BTN_H, ok ? C_GO : C_KEY, ok ? C_GO_E : C_DIM, "", 2);   /* said plainly; never by a hue alone */
        {   const bool price = why != LAB_FULL && why != LAB_WELCOME;
            const int ww = render_text_w(words, 2), gap = 12, w = ww + (price ? gap + render_price_w(SD_PRICE_LAB_FRY) : 0);
            const int x = SETUP_LAB_BTN_X + (SETUP_LAB_BTN_W - w) / 2, y = SETUP_LAB_BTN_Y + (SETUP_BTN_H - 14) / 2;
            render_text(fb, stride, x, y, 2, C_TEXT, words);
            if (price) render_price(fb, stride, x + ww + gap, y, SD_PRICE_LAB_FRY, C_TEXT);
        }
        dots(fb, stride, PAGE_H - 14);
    } else if (page_is_stretch()) {
        /* BODY and FINS (0.4.3 item 10, setup.h SETUP_ST_*): straight on the tank; the fry ringed at the left (NOW), the
           grown fish large on the stage with its handles, one row of named tiles under it */
        const bool body = page_is_body();
        char cap[40]; snprintf(cap, sizeof cap, "%s FOR %s", body ? "A BODY" : "FINS", s_lab ? "YOUR FRY" : page_fish() ? "THE SECOND FISH" : "THE FIRST FISH");
        text_c(fb, stride, CX, SETUP_Y + 7, 2, C_CAPT, cap);
        nav(fb, stride, true, "NEXT", false);
        render_button(fb, stride, SETUP_ROLL_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, C_KEY, C_DIM, s_st_can_undo ? "UNDO" : "ROLL", 2);
        /* the band under the hint and the tiles only: the fry at the left swims in clear water, as on the other pages */
        if (!setup_lab_black()) render_rect_blend(fb, stride, -PAGE_X, SETUP_ST_HINT_Y - 6, TANK_W, SETUP_ST_TILE_Y + SETUP_ST_TILE_H + 8 - (SETUP_ST_HINT_Y - 6), C_PANEL, 150);
        if (!setup_lab_black()) st_moving(t, fb, stride, clock);          /* (the lab's black page: its boxes draw these - setup_lab_bands_draw) */
        text_c(fb, stride, CX, SETUP_ST_HINT_Y, 2, C_CAPT, body ? "DRAG TO STRETCH. OR PICK ONE:" : "DRAG A FIN. OR PICK A SET:");
        if (!setup_lab_black()) st_tiles(t, fb, stride, clock);         /* (the lab's held page: a moving box of their own) */
        dots(fb, stride, PAGE_H - 14);
    } else if (page_is_look()) {
        /* THE EDITOR (setup.h): straight on the tank, the fry ringed on stage
           (NOW), the grown fish on a dark plate (GROWN UP), the rows under a
           panel band */
        const fish_t *f = &t->fish[page_fish()];
        char cap[40]; snprintf(cap, sizeof cap, "COLORS FOR %s", s_lab ? "YOUR FRY" : page_fish() ? "THE SECOND FISH" : "THE FIRST FISH");   /* not named yet (0.4) */
        text_c(fb, stride, CX, SETUP_Y + 7, 2, C_CAPT, cap);
        nav(fb, stride, true, "NEXT", false);
        render_button(fb, stride, SETUP_ROLL_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, C_KEY, C_DIM, "ROLL", 2);
        const edit_row_t *r; int n = edit_rows(&r);
        int band = SETUP_ED_CAPTION_Y - 6;                       /* from the captions down: fish swim behind them */
        render_rect_blend(fb, stride, SU_LEFT, band, SU_FULL_W, r[n - 1].y + r[n - 1].h + 10 - band, C_PANEL, 110);
        if (!setup_lab_black()) col_moving(t, fb, stride, clock);
        text_c(fb, stride, SETUP_ED_NOW_X, SETUP_ED_CAPTION_Y, 2, C_CAPT, "NOW");
        text_c(fb, stride, SETUP_ED_GROWN_X, SETUP_ED_CAPTION_Y, 2, C_CAPT, "GROWN UP");
        for (int k = 0; k < n; k++) {
            int cur = tank_gene(f, r[k].gene);
            char lab[32];
            if (r[k].gene == GENE_PATTERN) snprintf(lab, sizeof lab, "%s  %s", r[k].label, PATTERN_NAMES[cur]);   /* its tiles carry no names */
            else if (r[k].gene == GENE_ACCENT && s_same_note > 0) snprintf(lab, sizeof lab, "%s  SAME AS ITS BODY", r[k].label);
            else snprintf(lab, sizeof lab, "%s", r[k].label);
            bool unused = r[k].gene == GENE_ACCENT && f->pattern == PATTERN_SOLID;   /* a solid fish wears no markings */
            render_text(fb, stride, r[k].x + 2, r[k].y - SETUP_LABEL_DY, 2, unused ? dim(C_CAPT, 45) : C_CAPT, lab);
            if (r[k].gene == GENE_PATTERN) { if (!setup_lab_black()) pat_tiles(t, fb, stride, clock); continue; }   /* (its tiles swim: one way to draw them) */
            for (int i = 0; i < r[k].n; i++) {
                int x = r[k].x + i * r[k].px, y = r[k].y, w = r[k].w, h = r[k].h;
                bool on = i == cur;
                if (r[k].gene == GENE_BODY || r[k].gene == GENE_ACCENT) {
                    uint32_t rgb = r[k].gene == GENE_BODY ? LOOK_BODY[i] : LOOK_ACCENT[i];
                    bool off = r[k].gene == GENE_ACCENT && rgb == f->color;   /* markings the body's own color would vanish */
                    render_rect(fb, stride, x, y, w, h, rgb);                 /* in its own color, so it can be told (Strato
                                                                                 10-04: dimmed, it was a mystery color) */
                    render_rect_edge(fb, stride, x, y, w, h, on ? C_TEXT : C_DIM);
                    if (on) render_rect_edge(fb, stride, x + 1, y + 1, w - 2, h - 2, C_TEXT);
                    if (off) struck_out(fb, stride, x, y, w, h, rgb);              /* a shape says "not this one", never a hue */
                    continue;
                }
                /* a tile: a small grown fish of that option, in this fish's colors */
                fish_t form = *f;
                if (r[k].gene == GENE_SHAPE) form.shape = (uint8_t)i;
                else if (r[k].gene == GENE_FINS) form.fins = (uint8_t)i;
                else form.pattern = (uint8_t)i;
                render_rect(fb, stride, x, y, w, h, C_KEY);
                render_rect_edge(fb, stride, x, y, w, h, on ? C_TEXT : C_DIM);
                if (on) render_rect_edge(fb, stride, x + 1, y + 1, w - 2, h - 2, C_TEXT);
                if (r[k].gene == GENE_PATTERN)
                    render_fish_preview(fb, stride, x + w / 2.0f, y + h / 2.0f, 1.4f, f->color, f->fin, f->accent, &form, FISH_ROOM_ROW, clock);
                else {
                    render_fish_preview(fb, stride, x + w / 2.0f, y + 21, 1.0f, f->color, f->fin, f->accent, &form, FISH_ROOM_ROW, clock);
                    text_c(fb, stride, x + w / 2, y + h - 17, 2, on ? C_TEXT : dim(C_CAPT, 60),
                           r[k].gene == GENE_SHAPE ? SHAPE_NAMES[i] : FINS_NAMES[i]);
                }
            }
            if (unused)                                     /* a solid fish: the markings row stands back (still a pick away -
                                                               choosing a pattern brings them back) */
                render_rect_blend(fb, stride, r[k].x - 2, r[k].y - 2, (r[k].n - 1) * r[k].px + r[k].w + 4, r[k].h + 4, C_PANEL, 150);
        }
        if (page_is_colors()) swap_button(fb, stride);
        dots(fb, stride, PAGE_H - 14);
    } else if (s_page == SETUP_PG_BORN) {
        /* the announcement, over the live tank: the fry wears a double ring
           wherever it hatched (no stage - it stays in the grass it was born
           in), its parents are named, one button leads on */
        const fish_t *f = &t->fish[page_fish()];
        char news[40]; bool mut = mutation_words(f, news, sizeof news);
        float r = 17 * f->size + 6 + 3 * sinf(tank_anim(clock) * 3);   /* a ring that breathes */
        fish_ring(fb, stride, f, r, f->color);
        fish_ring(fb, stride, f, r + 8, mut ? C_NEW : C_EDGE);     /* a mutation: a gold outer ring */
        render_rect_blend(fb, stride, SU_LEFT, 92, SU_FULL_W, mut ? 138 : 116, C_PANEL, 150);
        text_c(fb, stride, CX, 104, 3, C_TEXT, "A NEW FRY!");
        char l1[FISH_NAME_MAX * 2 + 24]; const char *l2;
        setup_born_lines(t, page_fish(), l1, sizeof l1, &l2);
        text_c(fb, stride, CX, 140, 2, C_CAPT, l1);
        text_c(fb, stride, CX, 162, 2, C_CAPT, l2);
        text_c(fb, stride, CX, 184, 2, C_CAPT, "GO AND SAY HELLO.");
        if (mut) text_c(fb, stride, CX, 206, 2, C_NEW, news);      /* 0.4: neither parent shows it */
        render_button(fb, stride, SETUP_MID_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, C_GO, C_GO_E, "MEET IT", 2);
        dots(fb, stride, PAGE_H - 14);
    } else if (s_page == SETUP_PG_FAMILY) {
        /* the family page (setup.h): NOW and GROWN UP, then each gene and
           where it came from - a parent (* = that parent's hidden copy), or
           NEW! (a mutation) */
        const fish_t *f = &t->fish[page_fish()];
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + 20, 3, C_TEXT, f->name);
        float ps = f->size * 2.0f;                                   /* the fry, twice life size ... */
        if (ps > 1.2f) ps = 1.2f;                                    /* ... a juvenile's ring clear of the name */
        const bool first = s_about && founder(f);                     /* a first fish (its story, 0.4.2): its genes are its own */
        (void)ps;
        fam_portraits(t, fb, stride, clock);                          /* (THE PANEL KEPT: the band, as a held frame draws it) */
        if (!first) render_text(fb, stride, SETUP_FAM_FROM_X, SETUP_FAM_ROW_Y - 20, 2, dim(C_CAPT, 60), "FROM");
        bool hidden = false;
        for (int g = 0; g < GENE_N; g++) {
            int y = SETUP_FAM_ROW_Y + g * SETUP_FAM_ROW_DY, vx = SETUP_FAM_VALUE_X;
            static const char *const LABEL[GENE_N] = { "SHAPE", "FINS", "PATTERN", "BODY", "MARKINGS" };
            render_text(fb, stride, SETUP_FAM_LABEL_X, y, 2, C_CAPT, LABEL[g]);
            if (g == GENE_BODY || g == GENE_ACCENT) {
                uint32_t rgb = g == GENE_BODY ? f->color : f->accent;
                render_rect(fb, stride, vx, y - 1, 16, 16, rgb); render_rect_edge(fb, stride, vx, y - 1, 16, 16, C_INNER);
            } else render_text(fb, stride, vx, y, 2, C_TEXT, g == GENE_SHAPE ? SHAPE_NAMES[f->shape] : g == GENE_FINS ? FINS_NAMES[f->fins] : PATTERN_NAMES[f->pattern]);
            if (first) continue;
            if (f->mutated == g + 1) { render_text(fb, stride, SETUP_FAM_FROM_X, y, 2, C_NEW, "NEW!"); continue; }
            if (from_shop(f)) { render_text(fb, stride, SETUP_FAM_FROM_X, y, 2, C_CAPT, shop_word(t, f)); continue; }   /* (0.4.1: was "?" on every row) */
            parent_t p = parent_of(t, f, (f->from_b >> g & 1) ? 1 : 0);
            bool sec = f->from_carried >> g & 1;
            hidden |= sec;
            text_named(fb, stride, SETUP_FAM_FROM_X, y, "", p.name, p.known ? p.color : C_DIM, sec ? " *" : NULL);
        }
        if (hidden)                                     /* (Strato 11-08: two words in the gap between BACK and NEXT - the long line squeezed
                                                           the rows - one row's pitch under the table, so it reads with it) */
            text_c(fb, stride, SETUP_X + SETUP_W / 2, SETUP_FAM_ROW_Y + 5 * SETUP_FAM_ROW_DY, 2, dim(C_CAPT, 70), "* HIDDEN");
        if (first && s_stamp_l1) {                      /* (Strato 11-08: a STARTER FISH stamp, turned up the way a hand stamps it) */
            int sx, sy; stamp_at(f, &sx, &sy);
            render_stamp(fb, stride, sx, sy, s_stamp_deg, 0xe8c872, s_stamp_l1, s_stamp_l2, 2);
        }
        nav(fb, stride, false, "NEXT", false);
        dots(fb, stride, SETUP_Y + SETUP_H - 8);
    } else if (s_page == SETUP_PG_NATURE) {
        /* the nature page: how bold and sociable it is next to its parents'
           marks, and who they are */
        const fish_t *f = &t->fish[page_fish()];
        parent_t PA = parent_of(t, f, 0), PB = parent_of(t, f, 1);
        const parent_t *pa = PA.known ? &PA : NULL, *pb = PB.known ? &PB : NULL;
        bool shop = from_shop(f);                                    /* bought (0.3.5): its look is the shop's */
        const char *na = shop ? shop_word(t, f) : PA.name, *nb = shop ? shop_word(t, f) : PB.name;
        uint32_t ca = pa ? name_rgb(pa->color) : shop ? C_CAPT : C_DIM, cb = pb ? name_rgb(pb->color) : shop ? C_CAPT : C_DIM;
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + 20, 3, C_TEXT, f->name);
        text_c(fb, stride, CX, SETUP_Y + 56, 2, C_CAPT, "ITS NATURE");
        int y = SETUP_NAT_ROW_Y, vx = SETUP_FAM_VALUE_X + 12;
        static const char *const TRAIT[2] = { "BOLD", "SOCIAL" };
        for (int k = 0; k < 2; k++, y += SETUP_NAT_ROW_DY) {          /* the fry's own bar, the parents' marks */
            float v = k ? f->sociable : f->bold;
            float va = pa ? (k ? pa->sociable : pa->bold) : -1, vb = pb ? (k ? pb->sociable : pb->bold) : -1;
            int by = y + 3;
            render_text(fb, stride, SETUP_FAM_LABEL_X, y, 2, C_CAPT, TRAIT[k]);
            render_rect(fb, stride, vx, by, SETUP_FAM_BAR_W, SETUP_FAM_BAR_H, C_DIM);
            render_rect(fb, stride, vx, by, (int)(SETUP_FAM_BAR_W * v + 0.5f), SETUP_FAM_BAR_H, C_FILL);
            for (int p = 0; p < 2; p++) {                            /* a parent's mark (none for a parent not known) */
                float vp = p ? vb : va; if (vp < 0) continue;
                nature_mark(fb, stride, vx + (int)(SETUP_FAM_BAR_W * vp), by, p, p ? cb : ca);
            }
        }
        render_text(fb, stride, SETUP_FAM_LABEL_X, y, 2, C_CAPT, "PARENTS");
        if (shop) render_text(fb, stride, vx, y, 2, C_CAPT, "NONE HERE");
        else if (s_about && founder(f)) render_text(fb, stride, vx, y, 2, C_CAPT, "STARTER FISH");
        else {
            int x = vx;                                              /* each name after its mark's head (a parent not known: no mark, no key) */
            if (pa) { nat_head(fb, stride, x + SETUP_NAT_KEY_W / 2, y + 3, false, ca, false); x += SETUP_NAT_KEY_W + 5; }
            render_text(fb, stride, x, y, 2, ca, na);
            x += render_text_w(na, 2) + 16;
            if (pb) { nat_head(fb, stride, x + SETUP_NAT_KEY_W / 2, y + 3, true, cb, false); x += SETUP_NAT_KEY_W + 5; }
            render_text(fb, stride, x, y, 2, cb, nb);
        }
        nav(fb, stride, false, "DONE", true);
        dots(fb, stride, SETUP_Y + SETUP_H - 8);
    } else if (s_page == SETUP_PG_ABOUT) {
        /* ITS STORY (0.4.2): the fish as it is, then a row each - how long it has lived, where it came from, its
           parents, its meals (and how many from the keeper's hand), its size, and how much it trusts the keeper */
        const int fi = page_fish(); const fish_t *f = &t->fish[fi];
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + 20, 3, C_TEXT, f->name);
        about_portrait(t, fb, stride, clock);          /* (THE PANEL KEPT: the band, as a held frame draws it) */
        static const char *const LABEL[7] = { "AGE", "FROM", "PARENTS", "MEALS", "SIZE", "TRUST", "EVOLVED" };
        char v[40];
        /* THE PARAGON (0.4.3): a fish that has evolved has a seventh line, EVOLVED and its part, after its parents - the
           seven rows 21 apart in the six rows' room (a plain fish's page is as it was) */
        const bool par = f->evo > EVO_NONE && f->evo < EVO_N;
        static const int8_t ROWS6[6] = { 0, 1, 2, 3, 4, 5 }, ROWS7[7] = { 0, 1, 2, 6, 3, 4, 5 };
        const int8_t *rows = par ? ROWS7 : ROWS6; const int nrows = par ? 7 : 6, dy = par ? 21 : SETUP_AB_ROW_DY;
        for (int k = 0; k < nrows; k++) {
            const int r = rows[k];
            int y = SETUP_AB_ROW_Y + k * dy - (par ? 2 : 0), vx = SETUP_FAM_VALUE_X;
            render_text(fb, stride, SETUP_FAM_LABEL_X, y, 2, C_CAPT, LABEL[r]);
            if (r == 6) { render_text(fb, stride, vx, y, 2, C_NEW, tank_evo_name(f->evo)); continue; }
            if (r == 0) { about_age(t, fi, v, sizeof v); render_text(fb, stride, vx, y, 2, C_TEXT, v); }
            else if (r == 1) { about_from(t, f, v, sizeof v); render_text(fb, stride, vx, y, 2, C_TEXT, v); }
            else if (r == 2) {
                if (from_shop(f)) render_text(fb, stride, vx, y, 2, C_TEXT, "NONE HERE");
                else if (founder(f)) render_text(fb, stride, vx, y, 2, C_TEXT, "STARTER FISH");
                else {
                    parent_t pa = parent_of(t, f, 0), pb = parent_of(t, f, 1);
                    render_text(fb, stride, vx, y, 2, pa.known ? name_rgb(pa.color) : C_DIM, pa.name);
                    render_text(fb, stride, vx + render_text_w(pa.name, 2) + 16, y, 2, pb.known ? name_rgb(pb.color) : C_DIM, pb.name);
                }
            }
            else if (r == 3) {
                if (f->eaten_player > 0) snprintf(v, sizeof v, "%d, %d FROM YOU", f->eaten, f->eaten_player);
                else snprintf(v, sizeof v, "%d", f->eaten);
                render_text(fb, stride, vx, y, 2, C_TEXT, v);
            }
            else if (r == 4) { snprintf(v, sizeof v, "%.1f INCHES", f->size * ABOUT_INCHES); render_text(fb, stride, vx, y, 2, C_TEXT, v); }
            else render_hearts(fb, stride, vx, y - 3, (int)(f->trust / 2 + 0.0001f), 2);
        }
        nav(fb, stride, false, "NEXT", false);
        dots(fb, stride, SETUP_Y + SETUP_H - 8);
    } else {                                                        /* SETUP_PG_CARE */
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + 22, 3, C_TEXT, "CARING FOR THEM");
        static const char *const ls[] = {
            "TAP THE SURFACE TO FEED",
            "HOLD A FINGER AND THEY VISIT",
            "TAP A FISH TO CHECK ON IT",
            "ITS CARD HOLDS THE TOOLS",
            "SPONGE WIPES, SCISSORS TRIM",
            "TWO TAPS FLIP THE LIGHT",
            "THEY CHOOSE. YOU CARE.",
        };
        lines_c(fb, stride, SETUP_Y + 66, 24, C_CAPT, ls, 7);
        nav(fb, stride, false, "BEGIN", true);
        dots(fb, stride, SETUP_Y + SETUP_H - 8);
    }
}

#ifdef TANK_P4
/* THE BIG SCREENS' KEEPER PAGE (render_big.c, 0.4.3): a fish's STORY and TRAITS, what MORE's three pages show on a
 * pocket tank, drawn into the page's detail pane - the same words from the same helpers, in FRAME px (x, y: the
 * first row's label), a row every dy. */
void setup_big_story(const tank_t *t, int fi, uint16_t *fb, int stride, int x, int y, int dy) {
    const fish_t *f = &t->fish[fi];
    static const char *const LABEL[6] = { "AGE", "FROM", "PARENTS", "MEALS", "SIZE", "TRUST" };
    const int lx = x - PAGE_X, vx = lx + 128;
    char v[40];
    for (int r = 0; r < 6; r++) {
        const int py = y + r * dy - PAGE_Y;
        render_text(fb, stride, lx, py, 2, C_CAPT, LABEL[r]);
        if (r == 0) { about_age(t, fi, v, sizeof v); render_text(fb, stride, vx, py, 2, C_TEXT, v); }
        else if (r == 1) { about_from(t, f, v, sizeof v); render_text(fb, stride, vx, py, 2, C_TEXT, v); }
        else if (r == 2) {
            if (from_shop(f)) render_text(fb, stride, vx, py, 2, C_TEXT, "NONE HERE");
            else if (founder(f)) render_text(fb, stride, vx, py, 2, C_TEXT, "STARTER FISH");
            else {
                parent_t pa = parent_of(t, f, 0), pb = parent_of(t, f, 1);
                render_text(fb, stride, vx, py, 2, pa.known ? name_rgb(pa.color) : C_DIM, pa.name);
                render_text(fb, stride, vx + render_text_w(pa.name, 2) + 16, py, 2, pb.known ? name_rgb(pb.color) : C_DIM, pb.name);
            }
        }
        else if (r == 3) {
            if (f->eaten_player > 0) snprintf(v, sizeof v, "%d, %d FROM YOU", f->eaten, f->eaten_player);
            else snprintf(v, sizeof v, "%d", f->eaten);
            render_text(fb, stride, vx, py, 2, C_TEXT, v);
        }
        else if (r == 4) { snprintf(v, sizeof v, "%.1f INCHES", f->size * ABOUT_INCHES); render_text(fb, stride, vx, py, 2, C_TEXT, v); }
        else render_hearts(fb, stride, vx, py - 3, (int)(f->trust / 2 + 0.0001f), 2);
    }
}
/* ITS FORM (each gene, and the parent it came from - * that parent's hidden copy, NEW! a mutation) from y, a row every
   dy; ITS NATURE (its own bars, the parents' marks, who they are) from yn, a row every dyn */
void setup_big_traits(const tank_t *t, int fi, uint16_t *fb, int stride, int x, int y, int dy, int yn, int dyn) {
    const fish_t *f = &t->fish[fi];
    const int lx = x - PAGE_X, vx = lx + 128, fx = lx + 264;
    const bool first = founder(f);
    render_text(fb, stride, lx, y - PAGE_Y, 2, dim(C_CAPT, 70), "ITS FORM");
    if (!first) render_text(fb, stride, fx, y - PAGE_Y, 2, dim(C_CAPT, 60), "FROM");
    bool hidden = false;
    for (int g = 0; g < GENE_N; g++) {
        const int py = y + (g + 1) * dy - PAGE_Y;
        static const char *const LABEL[GENE_N] = { "SHAPE", "FINS", "PATTERN", "BODY", "MARKINGS" };
        render_text(fb, stride, lx, py, 2, C_CAPT, LABEL[g]);
        if (g == GENE_BODY || g == GENE_ACCENT) {
            uint32_t rgb = g == GENE_BODY ? f->color : f->accent;
            render_rect(fb, stride, vx, py - 1, 16, 16, rgb); render_rect_edge(fb, stride, vx, py - 1, 16, 16, C_INNER);
        } else render_text(fb, stride, vx, py, 2, C_TEXT, g == GENE_SHAPE ? SHAPE_NAMES[f->shape] : g == GENE_FINS ? FINS_NAMES[f->fins] : PATTERN_NAMES[f->pattern]);
        if (first) continue;
        if (f->mutated == g + 1) { render_text(fb, stride, fx, py, 2, C_NEW, "NEW!"); continue; }
        if (from_shop(f)) { render_text(fb, stride, fx, py, 2, C_CAPT, "THE SHOP"); continue; }
        parent_t p = parent_of(t, f, (f->from_b >> g & 1) ? 1 : 0);
        bool sec = f->from_carried >> g & 1;
        hidden |= sec;
        text_named(fb, stride, fx, py, "", p.name, p.known ? p.color : C_DIM, sec ? " *" : NULL);
    }
    if (hidden) render_text(fb, stride, fx, y + (GENE_N + 1) * dy - PAGE_Y, 2, dim(C_CAPT, 70), "* HIDDEN");
    else if (first) render_text(fb, stride, fx, y + dy - PAGE_Y, 2, dim(C_CAPT, 70), "A STARTER FISH:");
    if (first && !hidden) render_text(fb, stride, fx, y + 2 * dy - PAGE_Y, 2, dim(C_CAPT, 70), "ITS OWN");
    /* its nature */
    parent_t PA = parent_of(t, f, 0), PB = parent_of(t, f, 1);
    const parent_t *pa = PA.known ? &PA : NULL, *pb = PB.known ? &PB : NULL;
    bool shop = from_shop(f);
    const char *na = shop ? "THE SHOP" : PA.name, *nb = shop ? "THE SHOP" : PB.name;
    uint32_t ca = pa ? name_rgb(pa->color) : shop ? C_CAPT : C_DIM, cb = pb ? name_rgb(pb->color) : shop ? C_CAPT : C_DIM;
    render_text(fb, stride, lx, yn - PAGE_Y, 2, dim(C_CAPT, 70), "ITS NATURE");
    static const char *const TRAIT[2] = { "BOLD", "SOCIAL" };
    const int bw = 240; int py = yn + dyn - PAGE_Y;
    for (int k = 0; k < 2; k++, py += dyn) {
        float v = k ? f->sociable : f->bold;
        float va = pa ? (k ? pa->sociable : pa->bold) : -1, vb = pb ? (k ? pb->sociable : pb->bold) : -1;
        int by = py + 3;
        render_text(fb, stride, lx, py, 2, C_CAPT, TRAIT[k]);
        render_rect(fb, stride, vx, by, bw, SETUP_FAM_BAR_H, C_DIM);
        render_rect(fb, stride, vx, by, (int)(bw * v + 0.5f), SETUP_FAM_BAR_H, C_FILL);
        for (int p = 0; p < 2; p++) { float vp = p ? vb : va; if (vp < 0) continue; nature_mark(fb, stride, vx + (int)(bw * vp), by, p, p ? cb : ca); }
    }
    render_text(fb, stride, lx, py, 2, C_CAPT, "PARENTS");
    if (shop) render_text(fb, stride, vx, py, 2, C_CAPT, "NONE HERE");
    else if (first) render_text(fb, stride, vx, py, 2, C_CAPT, "STARTER FISH");
    else {
        int xx = vx;
        if (pa) { nat_head(fb, stride, xx + SETUP_NAT_KEY_W / 2, py + 3, false, ca, false); xx += SETUP_NAT_KEY_W + 5; }
        render_text(fb, stride, xx, py, 2, ca, na);
        xx += render_text_w(na, 2) + 16;
        if (pb) { nat_head(fb, stride, xx + SETUP_NAT_KEY_W / 2, py + 3, true, cb, false); xx += SETUP_NAT_KEY_W + 5; }
        render_text(fb, stride, xx, py, 2, cb, nb);
    }
}
#endif
