#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_critshelf.c - --selftest-crit-shelf (0.4.2, docs/release-0.4.2.md item 3, docs/critter-shelf.md): THE CRITTER
 * SHELF and THE CAP OF FIVE. Strato's rules (2026-11-06 / 11-09): a keeper can SHELVE the snail, the shrimp school, the
 * urchin, the seahorse and the crab - each into its own little jar on the shelf, still theirs (nothing bought is ever lost),
 * RESTING there (nothing it does runs: no grazing, no finds, no pay, its counts and days held); PUT BACK brings it back where
 * it was, with a poof. At most CRIT_CAP (5) critters are in the water; the school counts once. SHELVE asks twice (kids use
 * these): the first tap arms it (SURE?), a doubled tap is spent, the second tap a moment later shelves.
 *   A  shelve each critter (progression_shelve): owned still, out of the water (tank_crit_in), its card gone
 *      (render_critter_here), the bubbles where it was; twice is refused; selling one is still refused
 *   B  RESTING: 10 min awake and an 8 h night on the shelf change none of its state - the school's count and food, the
 *      seahorse's days and snack pay, the crab's next-find wait and finds, the snail's and the urchin's grazing - and pay
 *      nothing; the same span in the water moves the seahorse's days (the control)
 *   C  the tank draws exactly as one that never bought them (render_tank, critters shelved vs never owned)
 *   D  the save: the shelf bits and every critter's state survive a save and a load; a stray shelf bit for a critter not
 *      owned loads as nothing
 *   E  PUT BACK (progression_unshelve): in the water again where it was (its spot), its counts as they were, a poof;
 *      twice is refused
 *   F  THE CAP: five in, none to spare; at a lowered cap (the test's tank_debug_crit_cap) a buy and a PUT BACK past it are
 *      refused with the wallet untouched, and one shelved makes the room
 *   G  the shop: an owned critter's modal SHELVE arms (KEPT), a doubled tap is spent, the second tap shelves (SHOP_TAP_SHELVE);
 *      a tap off it disarms; the shelved row's PUT BACK answers SHOP_TAP_PLACE, at the cap it opens the modal instead and
 *      the modal's PUT BACK is dim (KEPT); a full tank's UNLOCK on a critter is dim
 *   H  the critter's card: SHELVE beside NAME (the shrimp's alone), inside the card and clear of NAME, the arrows and
 *      COLLECT; arms (the card draws SURE?), a doubled tap spent, the second shelves; off the button nothing
 * Red before the shelf: A fails (progression_shelve refuses a critter). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tank.h"
#include "advisor.h"
#include "progression.h"
#include "render.h"

const char *sim_shelf_dir(void);
goal_t advisor_rules(const tank_t *t, int fish_idx, bool request);

static tank_t T, U;
static uint16_t fa[TANK_W * TANK_H], fb2[TANK_W * TANK_H];
#define CS_FAIL(...) do { printf("FAIL: crit-shelf: "); printf(__VA_ARGS__); printf("\n"); return 1; } while (0)
enum { I_SNAIL = 1, I_SHRIMP = 5, I_URCHIN = 6, I_SEAHORSE = 7, I_CRAB = 10 };
static const int CRIT[5] = { I_SNAIL, I_SHRIMP, I_URCHIN, I_SEAHORSE, I_CRAB };
static const int CARD[5] = { RENDER_CARD_SNAIL, RENDER_CARD_SHRIMP, RENDER_CARD_URCHIN, RENDER_CARD_SEAHORSE, RENDER_CARD_CRAB };

static int free_bubbles_rising(const tank_t *t) { int n = 0; for (int i = 0; i < MAX_BUBBLE; i++) if (!t->bubble[i].column && t->bubble[i].vy >= 35) n++; return n; }
static void calm_bubbles(tank_t *t) { for (int i = 0; i < MAX_BUBBLE; i++) if (!t->bubble[i].column) t->bubble[i].vy = 0; }
/* the shop, as main.c's helpers walk it (page coordinates in, the frame's to render_shop_tap) */
static int stap(float px, float py) { return render_shop_tap(&T, PAGE_X + px, PAGE_Y + py); }
static int srow(int item) {
    int a, pg, r;
    if (!render_shop_find(&T, item, &a, &pg, &r)) return -1;
    render_shop_leave(); { int fx, fy; render_shop_foot_box(SHF_DECOR + a, &fx, &fy); stap(fx + MSP_FOOT_SQ / 2, fy + MSP_FOOT_SQ / 2); }
    for (int k = 0; k < pg; k++) stap(SHP_ARROW_X1 + SHP_ARROW_W / 2, SHP_ARROW_Y + SHP_ARROW_H / 2);
    return r;
}
#define ROW_Y(r)  (SHP_ROW_Y0 + (r) * SHP_ROW_DY + 20)
#define BTN_Y     (SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H / 2)
#define BTN_X     (SHP_MODAL_X + SHP_MODAL_W / 2)
#define SETTLE()  (T.clock += 0.7)

typedef struct { int shrimp_n, shrimp_food, cr_found, cr_found_paid; float shrimp_cool, cr_find_cool, urchin_grazed; double sh_s; uint32_t sh_snack_pay, sd_snacks_paid, snail_grazed;
                 float snail_x, snail_y, urchin_x, cr_x, sh_x; } held_t;
static held_t held(const tank_t *t) {
    held_t h; memset(&h, 0, sizeof h);
    h.shrimp_n = t->shrimp_n; h.shrimp_food = t->shrimp_food; h.shrimp_cool = t->shrimp_cool; h.cr_found = (int)t->cr_found; h.cr_found_paid = (int)t->cr_found_paid;
    h.cr_find_cool = t->cr_find_cool; h.urchin_grazed = t->urchin_grazed_px; h.sh_s = t->sh_s; h.sh_snack_pay = t->sh_snack_pay; h.sd_snacks_paid = (uint32_t)t->sd_snacks_paid;
    h.snail_grazed = (uint32_t)t->snail_grazed; h.snail_x = t->snail_x; h.snail_y = t->snail_y; h.urchin_x = t->urchin_x; h.cr_x = t->cr_x; h.sh_x = t->sh_x;
    return h;
}
/* every field that moved, not the first (eps: the save's own rounding; the seahorse's spot is never compared - a boot and a
   PUT BACK set it on a blade) */
static const char *held_diff(const held_t *a, const held_t *b, float eps) {
    static char out[400]; out[0] = 0;
    #define HD(cond, name) do { if (cond) { strncat(out, " ", sizeof out - strlen(out) - 1); strncat(out, name, sizeof out - strlen(out) - 1); } } while (0)
    HD(a->shrimp_n != b->shrimp_n, "shrimp_n"); HD(a->shrimp_food != b->shrimp_food, "shrimp_food"); HD(fabsf(a->shrimp_cool - b->shrimp_cool) > eps, "shrimp_cool");
    HD(a->cr_found != b->cr_found, "cr_found"); HD(a->cr_found_paid != b->cr_found_paid, "cr_found_paid"); HD(fabsf(a->cr_find_cool - b->cr_find_cool) > eps, "cr_find_cool");
    HD(fabsf(a->cr_x - b->cr_x) > eps, "cr_x"); HD(fabsf(a->urchin_grazed - b->urchin_grazed) > eps, "urchin_grazed"); HD(fabsf(a->urchin_x - b->urchin_x) > eps, "urchin_x");
    HD(fabs(a->sh_s - b->sh_s) > eps, "sh_s"); HD(a->sh_snack_pay != b->sh_snack_pay, "sh_snack_pay"); HD(a->sd_snacks_paid != b->sd_snacks_paid, "sd_snacks_paid");
    HD(a->snail_grazed != b->snail_grazed, "snail_grazed"); HD(fabsf(a->snail_x - b->snail_x) > eps || fabsf(a->snail_y - b->snail_y) > eps, "snail_xy");
    #undef HD
    return out[0] ? out : NULL;
}
/* nothing finds a shelved critter: not a tap, not a neighbor on the floor, not its card */
static const char *crit_findable(const tank_t *t) {
    if (tank_snail_upright(t)) return "the snail upright on the floor";
    if (t->snail_x >= 0 && tank_snail_hit(t, t->snail_x, t->snail_y)) return "a tap on the snail";
    if (t->urchin_x >= 0 && tank_urchin_hit(t, t->urchin_x, URCHIN_FLOOR_Y - 8)) return "a tap on the urchin";
    if (t->cr_x >= 0 && tank_crab_hit(t, t->cr_x, CR_FLOOR_Y - 10)) return "a tap on the crab";
    if (t->sh_x >= 0 && tank_seahorse_hit(t, t->sh_x, t->sh_y - 20)) return "a tap on the seahorse";
    if (t->shrimp_n > 0 && tank_shrimp_hit(t, t->shrimp[0].x, t->shrimp[0].y)) return "a tap on the school";
    for (int k = 0; k < 5; k++) if (render_critter_here(t, CARD[k])) return "a card";
    return NULL;
}

int selftest_crit_shelf(void) {
    char sav[600]; snprintf(sav, sizeof sav, "%s-critshelf-%dx%d.sav", sim_shelf_dir(), TANK_W, TANK_H);
    setenv("POCKET_TANK_SAVE", sav, 1); remove(sav);   /* (never the real save, never another job's) */
    tank_debug_crit_cap(0);
    tank_init(&T, 5150); progression_boot(&T); progression_setup_done(&T);
    T.sd_balance = 100000;
    for (int k = 0; k < 5; k++) if (!progression_buy(&T, CRIT[k])) CS_FAIL("could not buy critter item %d", CRIT[k]);
    for (int i = 0; i < 600; i++) tank_tick(&T, 1.0f / 30.0f, advisor_rules);
    if (tank_crit_count_in(&T) != 5 || tank_crit_room(&T) || tank_crit_cap() != CRIT_CAP)
        CS_FAIL("five bought: %d in the water, room %d, cap %d (want 5, no room, %d)", tank_crit_count_in(&T), tank_crit_room(&T), tank_crit_cap(), CRIT_CAP);

    /* ---- A. shelve each ---- */
    held_t before = held(&T);   /* (the seahorse is set back on a blade as it goes: its spot is not kept) */
    for (int k = 0; k < 5; k++) {
        const int it = CRIT[k]; const uint32_t bit = SD_ITEMS[it].bit;
        calm_bubbles(&T);
        if (!render_critter_here(&T, CARD[k])) CS_FAIL("critter %d: no card before the shelf", it);
        if (!progression_shelve(&T, it)) CS_FAIL("critter %d: SHELVE refused", it);
        if (!(T.sd_unlocks & bit) || !(T.sd_shelved & bit) || tank_crit_in(&T, bit)) CS_FAIL("critter %d: not on the shelf (owned %d, shelved %d)", it, !!(T.sd_unlocks & bit), !!(T.sd_shelved & bit));
        if (render_critter_here(&T, CARD[k])) CS_FAIL("critter %d: its card still opens from the shelf", it);
        if (free_bubbles_rising(&T) < 8) CS_FAIL("critter %d: no poof (%d bubbles)", it, free_bubbles_rising(&T));
        if (progression_shelve(&T, it)) CS_FAIL("critter %d: shelved twice", it);
        if (progression_sell(&T, it)) CS_FAIL("critter %d: sold from the shelf", it);
        if (tank_crit_count_in(&T) != 4 - k || !tank_crit_room(&T)) CS_FAIL("critter %d: %d in the water after it", it, tank_crit_count_in(&T));
    }
    {   held_t after = held(&T); const char *d = held_diff(&before, &after, 0.01f); if (d) CS_FAIL("shelving changed:%s", d);
        if ((d = crit_findable(&T)) != NULL) CS_FAIL("shelved, still found: %s", d); }

    /* ---- B. resting ---- */
    {
        held_t h0 = held(&T);
        for (int i = 0; i < 600 * 10; i++) tank_tick(&T, 0.1f, advisor_rules);   /* 10 min awake */
        progression_tick(&T, 0.1f);
        tank_tick_sleep(&T, 8 * 3600.0f);                                       /* a night */
        for (int i = 0; i < 300; i++) tank_tick(&T, 0.1f, advisor_rules);
        progression_tick(&T, 0.1f);
        held_t h1 = held(&T);
        const char *d = held_diff(&h0, &h1, 0.01f); if (d) CS_FAIL("changed on the shelf (it rests, it earns nothing):%s", d);
        if ((d = crit_findable(&T)) != NULL) CS_FAIL("after the night on the shelf, still found: %s", d);
        for (int k = 0; k < 5; k++) if (render_critter_here(&T, CARD[k])) CS_FAIL("critter %d: a card after the night on the shelf", CRIT[k]);
        /* the control: the seahorse in the water, the same span - its days move */
        U = T; U.sd_shelved &= ~SD_ITEM_SEAHORSE; double s0 = U.sh_s;
        tank_tick_sleep(&U, 3600.0f);
        if (U.sh_s - s0 < 3500) CS_FAIL("the control: a seahorse in the water did not live its hour (%.0f s)", U.sh_s - s0);
    }

    /* ---- C. the tank draws as one that never had them ---- */
    {   U = T; U.sd_unlocks &= ~(uint32_t)SD_CRIT_BITS; U.sd_shelved &= ~(uint32_t)SD_CRIT_BITS;
        render_tank(&U, fb2, TANK_W); render_tank(&T, fa, TANK_W);
        render_tank(&U, fb2, TANK_W); render_tank(&T, fa, TANK_W);
        int diff = 0; for (int i = 0; i < TANK_W * TANK_H; i++) diff += fa[i] != fb2[i];
        if (diff) CS_FAIL("the tank with its critters on the shelf drew %d px apart from one that never had them", diff);
    }

    /* ---- D. the save ---- */
    {   held_t h0 = held(&T); uint32_t sh = T.sd_shelved, un = T.sd_unlocks;
        progression_save(&T);
        tank_init(&T, 9); progression_wake(&T, 0);
        if (T.sd_shelved != sh || T.sd_unlocks != un) CS_FAIL("the shelf did not survive the save (shelved %#x want %#x)", (unsigned)T.sd_shelved, (unsigned)sh);
        held_t h1 = held(&T);
        const char *d = held_diff(&h0, &h1, 1.01f); if (d) CS_FAIL("the save and load changed on the shelf:%s", d);
        if ((d = crit_findable(&T)) != NULL) CS_FAIL("loaded on the shelf, still found: %s", d);
        T.sd_unlocks &= ~SD_ITEM_SNAIL; T.sd_shelved |= SD_ITEM_SNAIL; progression_save(&T);   /* a stray bit: shelved, not owned */
        tank_init(&T, 9); progression_wake(&T, 0);
        if (T.sd_shelved & SD_ITEM_SNAIL) CS_FAIL("a shelf bit for a critter not owned loaded");
        T.sd_balance += 100000; if (!progression_buy(&T, I_SNAIL)) CS_FAIL("the snail could not be bought back"); progression_shelve(&T, I_SNAIL);
    }

    /* ---- E. PUT BACK ---- */
    for (int k = 0; k < 5; k++) {
        const int it = CRIT[k]; const uint32_t bit = SD_ITEMS[it].bit;
        held_t h0 = held(&T); calm_bubbles(&T);
        if (!progression_unshelve(&T, it)) CS_FAIL("critter %d: PUT BACK refused", it);
        if (!tank_crit_in(&T, bit) || !render_critter_here(&T, CARD[k])) CS_FAIL("critter %d: not back in the water", it);
        if (progression_unshelve(&T, it)) CS_FAIL("critter %d: put back twice", it);
        if (free_bubbles_rising(&T) < 8) CS_FAIL("critter %d: no poof coming back", it);
        held_t h1 = held(&T);
        if (it == I_SNAIL && (h1.snail_x != h0.snail_x || h1.snail_y != h0.snail_y)) CS_FAIL("the snail came back at %.0f,%.0f (was %.0f,%.0f)", h1.snail_x, h1.snail_y, h0.snail_x, h0.snail_y);
        if (it == I_URCHIN && fabsf(h1.urchin_x - h0.urchin_x) > 0.5f) CS_FAIL("the urchin came back at %.0f (was %.0f)", h1.urchin_x, h0.urchin_x);
        if (it == I_CRAB && (fabsf(h1.cr_x - h0.cr_x) > 0.5f || T.cr_found != (uint32_t)h0.cr_found)) CS_FAIL("the crab came back at %.0f (was %.0f)", h1.cr_x, h0.cr_x);
        if (it == I_SEAHORSE && (fabs(h1.sh_s - h0.sh_s) > 0.01 || h1.sh_snack_pay != h0.sh_snack_pay)) CS_FAIL("the seahorse came back with other days or pay");
        if (it == I_SHRIMP && (h1.shrimp_n != h0.shrimp_n || h1.shrimp_food != h0.shrimp_food)) CS_FAIL("the school came back %d strong (was %d)", h1.shrimp_n, h0.shrimp_n);
    }
    for (int i = 0; i < 300; i++) tank_tick(&T, 1.0f / 30.0f, advisor_rules);
    if (tank_crit_count_in(&T) != 5) CS_FAIL("%d critters in the water after PUT BACK", tank_crit_count_in(&T));

    /* ---- F. the cap ---- */
    {   tank_debug_crit_cap(3);
        if (tank_crit_room(&T)) CS_FAIL("five in at a cap of 3: room");
        progression_shelve(&T, I_CRAB); progression_shelve(&T, I_SNAIL); progression_shelve(&T, I_URCHIN);   /* two in: room for one */
        if (!tank_crit_room(&T)) CS_FAIL("two in at a cap of 3: no room");
        if (!progression_unshelve(&T, I_CRAB)) CS_FAIL("PUT BACK into the last place refused");
        int32_t bal = T.sd_balance;
        if (progression_unshelve(&T, I_SNAIL) || tank_crit_in(&T, SD_ITEM_SNAIL)) CS_FAIL("PUT BACK past the cap");
        if (T.sd_balance != bal || !(T.sd_shelved & SD_ITEM_SNAIL)) CS_FAIL("a refused PUT BACK moved something");
        /* a buy past the cap: a fresh tank's two critters at a cap of 2 */
        tank_init(&U, 77); U.sd_balance = 100000;
        tank_debug_crit_cap(2);
        if (!progression_buy(&U, I_SNAIL) || !progression_buy(&U, I_CRAB)) CS_FAIL("two critters at a cap of 2 refused");
        bal = U.sd_balance;
        if (progression_buy(&U, I_URCHIN) || (U.sd_unlocks & SD_ITEM_URCHIN) || U.sd_balance != bal) CS_FAIL("a third critter bought at a cap of 2");
        if (!progression_buy(&U, 2)) CS_FAIL("a castle refused at the critter cap (decorations do not count)");
        progression_shelve(&U, I_CRAB);
        if (!progression_buy(&U, I_URCHIN)) CS_FAIL("the urchin refused with a critter shelved to make room");
        tank_debug_crit_cap(3);
    }

    /* ---- G. the shop ---- */
    {   /* T: crab, shrimp, seahorse in (3 = the cap); snail and urchin on the shelf */
        render_shop_leave(); render_shop(&T, fa, TANK_W);
        int r = srow(I_SEAHORSE); if (r < 0) CS_FAIL("no seahorse row");
        render_shop(&T, fa, TANK_W);
        if (stap(100, ROW_Y(r)) != SHOP_TAP_KEPT) CS_FAIL("the seahorse's row opened nothing");
        render_shop(&T, fa, TANK_W); SETTLE();
        if (stap(BTN_X, BTN_Y) != SHOP_TAP_KEPT || !tank_crit_in(&T, SD_ITEM_SEAHORSE)) CS_FAIL("the first SHELVE tap did more than arm");
        render_shop(&T, fa, TANK_W);
        if (stap(BTN_X, BTN_Y) != SHOP_TAP_KEPT) CS_FAIL("a doubled SHELVE tap shelved (it waits MSP_SELL_WAIT_S)");
        SETTLE(); render_shop(&T, fa, TANK_W);
        if ((r = stap(BTN_X, BTN_Y)) != SHOP_TAP_SHELVE + I_SEAHORSE) CS_FAIL("the second SHELVE tap returned %d", r);
        if (!tank_crit_in(&T, SD_ITEM_SEAHORSE)) CS_FAIL("the shop shelved by itself (the platform calls progression_shelve)");
        /* a tap off the modal disarms */
        SETTLE(); r = srow(I_SEAHORSE); render_shop(&T, fa, TANK_W); stap(100, ROW_Y(r)); SETTLE(); render_shop(&T, fa, TANK_W);
        stap(BTN_X, BTN_Y); SETTLE(); stap(10, 10); SETTLE(); render_shop(&T, fa, TANK_W);
        stap(100, ROW_Y(r)); SETTLE(); render_shop(&T, fa, TANK_W);
        if (stap(BTN_X, BTN_Y) != SHOP_TAP_KEPT) CS_FAIL("SHELVE stayed armed past a tap off the modal");
        render_shop_leave();
        /* a shelved row: PUT BACK; at the cap its modal, dim */
        r = srow(I_SNAIL); render_shop(&T, fa, TANK_W); SETTLE();
        if (stap(SHP_BTN_X + SHP_BTN_W / 2, ROW_Y(r)) != SHOP_TAP_KEPT || render_shop_modal_up() == false) CS_FAIL("a full tank's PUT BACK did not open the modal to say why");
        render_shop(&T, fa, TANK_W); SETTLE();
        if (stap(BTN_X, BTN_Y) != SHOP_TAP_KEPT) CS_FAIL("a full tank's modal PUT BACK answered");
        progression_shelve(&T, I_CRAB);                                          /* room */
        render_shop_leave(); r = srow(I_SNAIL); render_shop(&T, fa, TANK_W); SETTLE();
        if (stap(SHP_BTN_X + SHP_BTN_W / 2, ROW_Y(r)) != SHOP_TAP_PLACE + I_SNAIL) CS_FAIL("the row's PUT BACK with room");
        render_shop(&T, fa, TANK_W); SETTLE();
        if (stap(100, ROW_Y(r)) != SHOP_TAP_KEPT) CS_FAIL("the shelved snail's row");
        render_shop(&T, fa, TANK_W); SETTLE();
        if (stap(BTN_X, BTN_Y) != SHOP_TAP_PLACE + I_SNAIL) CS_FAIL("the modal's PUT BACK with room");
        render_shop_leave();
        /* a full tank's UNLOCK on a critter not owned: dim */
        tank_init(&U, 78); U.sd_balance = 100000; tank_debug_crit_cap(1); progression_buy(&U, I_SNAIL);
        static tank_t K; K = T; T = U;
        r = srow(I_CRAB); render_shop(&T, fa, TANK_W); SETTLE();
        stap(100, ROW_Y(r)); render_shop(&T, fa, TANK_W); SETTLE();
        if (stap(BTN_X, BTN_Y) != SHOP_TAP_KEPT) CS_FAIL("a full tank's critter UNLOCK answered");
        T = K; render_shop_leave(); tank_debug_crit_cap(0);
    }

    /* ---- H. the card ---- */
    {   for (int k = 0; k < 5; k++) if (!tank_crit_in(&T, SD_ITEMS[CRIT[k]].bit)) progression_unshelve(&T, CRIT[k]);
        for (int i = 0; i < 60; i++) tank_tick(&T, 1.0f / 30.0f, advisor_rules);
        for (int k = 0; k < 5; k++) {
            const int card = CARD[k]; int bx, by, bw, bh;
            if (!render_crit_shelve_box(card, &bx, &by, &bw, &bh)) CS_FAIL("card %d: no SHELVE", card);
            if (render_crit_item_of_card(card) != CRIT[k]) CS_FAIL("card %d: item %d", card, render_crit_item_of_card(card));
            int nx, ny, nw, nh;
            if (render_crit_name_box(card, &nx, &ny, &nw, &nh) && !(bx >= nx + nw + 8 || bx + bw + 8 <= nx)) CS_FAIL("card %d: SHELVE overlaps NAME", card);
            const float mx = bx + bw / 2.0f, my = by + bh / 2.0f;
            if (render_critter_name_hit(&T, card, mx, my)) CS_FAIL("card %d: SHELVE's middle is NAME's", card);
            if (render_critter_nav_hit(card, mx, my)) CS_FAIL("card %d: SHELVE's middle is an arrow", card);
            if (card == RENDER_CARD_CRAB && render_crab_collect_hit(&T, mx, my)) CS_FAIL("the crab card: SHELVE is COLLECT");
            if (render_critter_name_hit(&T, card, bx + 2.0f, my) || render_critter_name_hit(&T, card, bx + bw - 2.0f, my)) CS_FAIL("card %d: SHELVE's edge is NAME's", card);
            for (int c = 0; c < 4; c++) {                                        /* inside the glass */
                int px = c & 1 ? bx + bw - 1 : bx, py = c & 2 ? by + bh - 1 : by;
                if (px < 0 || py < 0 || px >= TANK_W || py >= TANK_H) CS_FAIL("card %d: SHELVE's corner off the frame", card);
#ifdef TANK_ROUND
                float dx = px + 0.5f - TANK_W / 2.0f, dy = py + 0.5f - TANK_H / 2.0f;
                if (dx * dx + dy * dy > (TANK_W / 2.0f) * (TANK_W / 2.0f)) CS_FAIL("card %d: SHELVE's corner off the bowl", card);
#endif
            }
            render_tank(&T, fa, TANK_W); render_stats_card(&T, card, fa, TANK_W);
            T.clock += 1.0;
            if (render_critter_shelve_tap(&T, card, mx, my) != CRIT_SHELVE_ARMED || !tank_crit_in(&T, SD_ITEMS[CRIT[k]].bit)) CS_FAIL("card %d: the first tap did more than arm", card);
            render_tank(&T, fb2, TANK_W); render_stats_card(&T, card, fb2, TANK_W);
            if (!memcmp(fa, fb2, sizeof fa)) CS_FAIL("card %d: armed draws as unarmed (SURE?)", card);
            if (render_critter_shelve_tap(&T, card, mx, my) != CRIT_SHELVE_ARMED) CS_FAIL("card %d: a doubled tap went through", card);
            T.clock += 0.7;
            if (render_critter_shelve_tap(&T, card, mx, my) != CRIT_SHELVE_GO) CS_FAIL("card %d: the second tap did not shelve", card);
            if (render_critter_shelve_tap(&T, card, mx, 4) != CRIT_SHELVE_NONE) CS_FAIL("card %d: the top of the glass answered", card);
        }
    }
    remove(sav);
    printf("selftest-crit-shelf (%dx%d): five critters shelved, each out of the water with a poof, resting through 10 min and a night "
           "(no count, day, find or pay moved; the control lived), the tank drawn as one without them, the shelf saved and loaded, "
           "PUT BACK where each was, the cap of %d held for a buy and a PUT BACK, SHELVE asked twice in the shop and on every card\n",
           TANK_W, TANK_H, CRIT_CAP);
    return 0;
}

/* --crit-shelf-shot <prefix> (0.4.2): the critter shelf's pictures for tools/board_sheet.py - the CRITTERS area with the
   crab and the school in their jars, an owned critter's modal (SHELVE), armed (SURE?), a shelved one's (PUT BACK), at the
   cap (FIVE CRITTERS IS THE MOST), the snail's card with NAME | SHELVE and armed, the school's card: <prefix>_<name>.ppm */
static void cs_ppm(const char *prefix, const char *name, const uint16_t *fb) {
    char path[600]; snprintf(path, sizeof path, "%s_%s.ppm", prefix, name);
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        uint16_t v = FB_SWAP16(fb[i]);
        uint8_t px[3] = { (uint8_t)(((v >> 11) & 31) << 3), (uint8_t)(((v >> 5) & 63) << 2), (uint8_t)((v & 31) << 3) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}
int crit_shelf_shot(const char *prefix) {
    char sav[600]; snprintf(sav, sizeof sav, "%s-critshelf-shot-%dx%d.sav", sim_shelf_dir(), TANK_W, TANK_H);
    setenv("POCKET_TANK_SAVE", sav, 1); remove(sav);
    tank_debug_crit_cap(0);
    tank_init(&T, 2024); progression_boot(&T); progression_setup_done(&T);
    T.sd_balance = 100000;
    for (int k = 0; k < 5; k++) progression_buy(&T, CRIT[k]);
    T.sd_balance = 412;
    tank_set_crit_name(&T, CRIT_NAME_SNAIL, "shelly"); tank_set_crit_name(&T, CRIT_NAME_CRAB, "pinchy");
    tank_shrimp_place(&T, 7);
    for (int i = 0; i < 900; i++) tank_tick(&T, 1.0f / 30.0f, advisor_rules);
    progression_shelve(&T, I_CRAB); progression_shelve(&T, I_SHRIMP);
    for (int i = 0; i < 90; i++) tank_tick(&T, 1.0f / 30.0f, advisor_rules);   /* (the poofs gone) */
    int r;
    /* the CRITTERS area, its pages */
    r = srow(I_SNAIL); (void)r; render_shop(&T, fa, TANK_W); cs_ppm(prefix, "shop_critters1", fa);
    stap(SHP_ARROW_X1 + SHP_ARROW_W / 2, SHP_ARROW_Y + SHP_ARROW_H / 2); render_shop(&T, fa, TANK_W); cs_ppm(prefix, "shop_critters2", fa);
    /* the snail's modal: SHELVE, then armed */
    r = srow(I_SNAIL); render_shop(&T, fa, TANK_W); stap(100, ROW_Y(r)); SETTLE(); render_shop(&T, fa, TANK_W); cs_ppm(prefix, "modal_in_tank", fa);
    stap(BTN_X, BTN_Y); render_shop(&T, fa, TANK_W); cs_ppm(prefix, "modal_armed", fa);
    render_shop_leave();
    /* the crab's on the shelf: PUT BACK */
    r = srow(I_CRAB); render_shop(&T, fa, TANK_W); SETTLE(); stap(100, ROW_Y(r)); SETTLE(); render_shop(&T, fa, TANK_W); cs_ppm(prefix, "modal_on_shelf", fa);
    render_shop_leave();
    /* the school's, the same */
    r = srow(I_SHRIMP); render_shop(&T, fa, TANK_W); SETTLE(); stap(100, ROW_Y(r)); SETTLE(); render_shop(&T, fa, TANK_W); cs_ppm(prefix, "modal_school_on_shelf", fa);
    render_shop_leave();
    /* at the cap (a cap of 3 here: three in the water) */
    tank_debug_crit_cap(3);
    r = srow(I_CRAB); render_shop(&T, fa, TANK_W); cs_ppm(prefix, "shop_full", fa);
    SETTLE(); stap(100, ROW_Y(r)); SETTLE(); render_shop(&T, fa, TANK_W); cs_ppm(prefix, "modal_full", fa);
    render_shop_leave();
    {   static tank_t K2; K2 = T;                                                /* a sixth kind's modal, as it would read: the urchin unbought at the cap */
        T.sd_unlocks &= ~SD_ITEM_URCHIN; T.sd_shelved &= ~SD_ITEM_URCHIN; tank_debug_crit_cap(2);
        r = srow(I_URCHIN); render_shop(&T, fa, TANK_W); SETTLE(); stap(100, ROW_Y(r)); SETTLE(); render_shop(&T, fa, TANK_W); cs_ppm(prefix, "modal_full_unbought", fa);
        render_shop_leave(); T = K2; }
    tank_debug_crit_cap(0);
    /* the cards */
    render_tank(&T, fa, TANK_W); render_stats_card(&T, RENDER_CARD_SNAIL, fa, TANK_W); cs_ppm(prefix, "card_snail", fa);
    { int bx, by, bw, bh; render_crit_shelve_box(RENDER_CARD_SNAIL, &bx, &by, &bw, &bh); render_critter_shelve_tap(&T, RENDER_CARD_SNAIL, bx + bw / 2.0f, by + bh / 2.0f); }
    render_tank(&T, fa, TANK_W); render_stats_card(&T, RENDER_CARD_SNAIL, fa, TANK_W); cs_ppm(prefix, "card_snail_armed", fa);
    render_critter_shelve_disarm();
    progression_unshelve(&T, I_CRAB); for (int i = 0; i < 90; i++) tank_tick(&T, 1.0f / 30.0f, advisor_rules);
    {   static const int AC[2] = { RENDER_CARD_SEAHORSE, RENDER_CARD_CRAB }; static const char *const AN[2] = { "card_seahorse_armed", "card_crab_armed" };
        for (int q = 0; q < 2; q++) {
            int bx, by, bw, bh; render_crit_shelve_box(AC[q], &bx, &by, &bw, &bh); T.clock += 5; render_critter_shelve_tap(&T, AC[q], bx + bw / 2.0f, by + bh / 2.0f);
            render_tank(&T, fa, TANK_W); render_stats_card(&T, AC[q], fa, TANK_W); cs_ppm(prefix, AN[q], fa);
            render_critter_shelve_disarm();
        }
    }
    progression_unshelve(&T, I_SHRIMP); for (int i = 0; i < 90; i++) tank_tick(&T, 1.0f / 30.0f, advisor_rules);
    render_tank(&T, fa, TANK_W); render_stats_card(&T, RENDER_CARD_SHRIMP, fa, TANK_W); cs_ppm(prefix, "card_school", fa);
    render_tank(&T, fa, TANK_W); render_stats_card(&T, RENDER_CARD_URCHIN, fa, TANK_W); cs_ppm(prefix, "card_urchin", fa);
    remove(sav);
    printf("crit-shelf-shot: wrote %s_*.ppm\n", prefix);
    return 0;
}
