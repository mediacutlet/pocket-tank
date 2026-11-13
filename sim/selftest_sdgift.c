#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_sdgift.c — SAND DOLLAR GIFTS (0.4.2): the sim's pictures and checks.
 *   ./fishsim --sdgift-shot <prefix>   the looks, for Strato's approval */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tank.h"
#include "progression.h"
#include "collection.h"
#include "render.h"
#include "advisor.h"

static tank_t s_sg;
static void sg_ppm(const char *path, const uint16_t *fb) {
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        uint16_t p = FB_SWAP16(fb[i]);
        uint8_t rgb[3] = { (uint8_t)(((p >> 11) & 31) << 3), (uint8_t)(((p >> 5) & 63) << 2), (uint8_t)((p & 31) << 3) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
#include "setup.h"
#include "link.h"
#include "tank_link.h"
#include "sdgift.h"
int sdgift_shot(const char *prefix) {
    static uint16_t fb[TANK_W * TANK_H], scene[TANK_W * TANK_H], card[RENDER_CARD_W * RENDER_CARD_H], give[GIFT_CARD_W * GIFT_CARD_H], gift[GIFT_CARD_W * GIFT_CARD_H];
    static uint8_t vig[TANK_W * TANK_H]; static uint32_t dirty[RENDER_DIRTY_WORDS];
    char path[600];
    tank_init(&s_sg, 2026); tank_new_population(&s_sg); tank_ensure_ids(&s_sg);
    for (int i = 0; i < 2 * 60; i++) tank_tick(&s_sg, 1.0f / 60.0f, advisor_rules);
    render_set_scene_cache(scene); render_set_vignette_cache(vig); render_set_card_cache(card); render_set_modal_cache(give, gift); render_set_dirty_mask(dirty);
    s_sg.sd_balance = 340; s_sg.link_badge = 1;
    link_view_t v; memset(&v, 0, sizeof v);
    v.badge = 1; v.edge[LINK_LEFT].state = LV_FREE; v.edge[LINK_RIGHT].state = LV_OPEN; v.edge[LINK_RIGHT].badge = 2; v.edge[LINK_RIGHT].sd = 1;
    double clk = s_sg.clock;
    /* 1: the HUD, linked at the right: the coin under that door's tab */
    render_link_leave(); render_tank(&s_sg, fb, TANK_W); render_link_mode(&v, fb, TANK_W, clk);
    snprintf(path, sizeof path, "%s_1_hud.ppm", prefix); sg_ppm(path, fb);
    /* 1b: both doors open */
    { link_view_t w = v; w.edge[LINK_LEFT].state = LV_OPEN; w.edge[LINK_LEFT].badge = 3; w.edge[LINK_LEFT].sd = 1;
      render_tank(&s_sg, fb, TANK_W); render_link_mode(&w, fb, TANK_W, clk);
      snprintf(path, sizeof path, "%s_1b_hud_two.ppm", prefix); sg_ppm(path, fb); }
    /* 2: the dial, 25 */
    setup_begin_sdgift(&s_sg, LINK_RIGHT, 2); setup_sdgift_set(25);
    render_tank(&s_sg, fb, TANK_W); render_setup(&s_sg, fb, TANK_W, clk);
    snprintf(path, sizeof path, "%s_2_dial.ppm", prefix); sg_ppm(path, fb);
    /* 3: more than the balance */
    setup_sdgift_set(500); render_tank(&s_sg, fb, TANK_W); render_setup(&s_sg, fb, TANK_W, clk);
    snprintf(path, sizeof path, "%s_3_dial_over.ppm", prefix); sg_ppm(path, fb);
    /* 4: the confirm page */
    setup_sdgift_set(25); setup_activate(&s_sg, SETUP_HIT_NEXT);
    render_tank(&s_sg, fb, TANK_W); render_setup(&s_sg, fb, TANK_W, clk);
    snprintf(path, sizeof path, "%s_4_confirm.ppm", prefix); sg_ppm(path, fb);
    setup_activate(&s_sg, SETUP_HIT_NEXT); int e; int n = setup_take_sdgift(&e);
    /* 5: sent - the giver's HUD, the coin mid-turn, the news line */
    s_sg.sd_balance -= n;
    render_link_coin_spin(LINK_RIGHT, clk - 0.55); render_link_sd_line("SENT 25 SAND DOLLARS", "THEY HAVE THEM NOW", clk, 4);
    render_tank(&s_sg, fb, TANK_W); render_link_mode(&v, fb, TANK_W, clk);
    snprintf(path, sizeof path, "%s_5_sent.ppm", prefix); sg_ppm(path, fb);
    /* 6: the neighbor's glass, the HUD closed: the coin turns over the door's badge, +25 on the toast */
    { link_view_t w; memset(&w, 0, sizeof w); w.badge = 2; w.edge[LINK_LEFT].state = LV_OPEN; w.edge[LINK_LEFT].badge = 1; w.edge[LINK_LEFT].sd = 1;
      progression_sd_toast(25); render_link_coin_spin(LINK_LEFT, clk - 0.7);
      render_tank(&s_sg, fb, TANK_W); render_link_doors(&w, fb, TANK_W, clk); render_sd_toast(&s_sg, fb, TANK_W);
      snprintf(path, sizeof path, "%s_6_received.ppm", prefix); sg_ppm(path, fb); }
    printf("sdgift-shot: %s_{1_hud,1b_hud_two,2_dial,3_dial_over,4_confirm,5_sent,6_received}.ppm (sent %d at edge %d)\n", prefix, n, e);
    return 0;
}

/* ---- --selftest-sdgift: the book's rules, the dial, the HUD's coin, then two engines over a bad radio ---- */
static int g_fail;
#define SCHECK(c, ...) do { if (!(c)) { if (g_fail < 15) { printf("FAIL sdgift: "); printf(__VA_ARGS__); printf("\n"); } g_fail++; } } while (0)

static void book_rules(void) {
    sdg_book_t a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    int32_t ba = 100, bb = 0; uint32_t n1 = 0, n2 = 0; bool fresh;
    SCHECK(sdg_out_begin(&a, &ba, 0xB, 0, 0, &n1) == SDG_E_BAD && ba == 100, "a gift of 0 was taken");
    SCHECK(sdg_out_begin(&a, &ba, 0xB, 1000, 0, &n1) == SDG_E_BAD && ba == 100, "a gift past 999 was taken");
    SCHECK(sdg_out_begin(&a, &ba, 0, 5, 0, &n1) == SDG_E_BAD && ba == 100, "a gift to nobody was taken");
    SCHECK(sdg_out_begin(&a, &ba, 0xB, 101, 0, &n1) == SDG_E_FUNDS && ba == 100 && !sdg_out_pending(&a), "more than the balance was taken");
    { int32_t z = 0; sdg_book_t c; memset(&c, 0, sizeof c); SCHECK(sdg_out_begin(&c, &z, 0xB, 1, 0, &n1) == SDG_E_FUNDS && z == 0, "a balance of 0 gave"); }
    SCHECK(sdg_out_begin(&a, &ba, 0xB, 100, 0, &n1) == SDG_SENT && ba == 0 && n1 == 1 && sdg_out_pending(&a), "all of it: not taken (%d, nonce %u)", ba, n1);
    SCHECK(sdg_out_begin(&a, &ba, 0xB, 1, 0, &n2) == SDG_E_BUSY, "a second gift while one is in flight");
    /* the receiver: once, the same answer to every copy */
    SCHECK(sdg_in(&b, &bb, 0xA, n1, 100, &fresh) == SDG_V_OK && fresh && bb == 100, "credit: %d", bb);
    for (int i = 0; i < 5; i++) SCHECK(sdg_in(&b, &bb, 0xA, n1, 100, &fresh) == SDG_V_OK && !fresh && bb == 100, "a copy credited again: %d", bb);
    SCHECK(sdg_out_settle(&a, &ba, 0xB, n1 + 1, SDG_V_OK) == -1 && sdg_out_pending(&a), "a stranger's answer settled the note");
    SCHECK(sdg_out_settle(&a, &ba, 0xC, n1, SDG_V_OK) == -1, "another tank's answer settled the note");
    SCHECK(sdg_out_settle(&a, &ba, 0xB, n1, SDG_V_OK) == 0 && ba == 0 && !sdg_out_pending(&a), "OK: the money came back or the note stayed");
    SCHECK(sdg_out_settle(&a, &ba, 0xB, n1, SDG_V_OK) == -1, "a second answer settled again");
    /* a late copy of an older gift after a newer one: no answer, no money */
    ba = 50; SCHECK(sdg_out_begin(&a, &ba, 0xB, 20, 0, &n2) == SDG_SENT && n2 == 2, "the second gift's nonce %u", n2);
    SCHECK(sdg_in(&b, &bb, 0xA, n2, 20, &fresh) == SDG_V_OK && bb == 120, "the second gift");
    SCHECK(sdg_in(&b, &bb, 0xA, n1, 100, &fresh) == SDG_V_OLD && !fresh && bb == 120, "a late copy of the first gift was credited");
    /* refused: the receiver's cap - the money comes back */
    sdg_out_settle(&a, &ba, 0xB, n2, SDG_V_OK);
    { int32_t full = SDG_BALANCE_CAP - 5; uint32_t n3;
      SCHECK(sdg_out_begin(&a, &ba, 0xB, 10, 0, &n3) == SDG_SENT && ba == 20, "a third gift");
      SCHECK(sdg_in(&b, &full, 0xA, n3, 10, &fresh) == SDG_V_FULL && full == SDG_BALANCE_CAP - 5, "past the cap was credited");
      SCHECK(sdg_in(&b, &full, 0xA, n3, 10, &fresh) == SDG_V_FULL && !fresh, "a copy of a refusal changed its mind");
      SCHECK(sdg_out_settle(&a, &ba, 0xB, n3, SDG_V_FULL) == 10 && ba == 30, "a refusal: the money did not come back (%d)", ba);
      int32_t edge = SDG_BALANCE_CAP - 10; sdg_book_t c; memset(&c, 0, sizeof c);
      SCHECK(sdg_in(&c, &edge, 0xA, 1, 10, &fresh) == SDG_V_OK && edge == SDG_BALANCE_CAP, "exactly to the cap was refused"); }
    SCHECK(sdg_in(&b, &bb, 0xA, 99, 0, &fresh) == SDG_V_BAD && bb == 120, "a gift of 0 was taken in");
    SCHECK(sdg_in(&b, &bb, 0xA, 100, 5000, &fresh) == SDG_V_BAD && bb == 120, "a gift past 999 was taken in");
    SCHECK(sdg_in(&b, &bb, 0, 7, 5, &fresh) == SDG_V_NONE && sdg_in(&b, &bb, 0xA, 0, 5, &fresh) == SDG_V_NONE, "a zero id was answered");
    /* the book holds SDG_SEEN_N tanks: a seventh forgets the least recent, never a recent one */
    { sdg_book_t c; memset(&c, 0, sizeof c); int32_t x = 0;
      for (uint32_t k = 1; k <= SDG_SEEN_N; k++) sdg_in(&c, &x, 0x100 + k, 1, 1, &fresh);
      sdg_in(&c, &x, 0x101, 1, 1, &fresh);                 /* (the first one again: a copy, and no fresher - age stays) */
      sdg_in(&c, &x, 0x102, 2, 1, &fresh);                 /* the second: a new gift - now the most recent */
      sdg_in(&c, &x, 0x200, 1, 1, &fresh);
      bool has101 = false, has102 = false, has200 = false;
      for (int i = 0; i < SDG_SEEN_N; i++) { has101 |= c.seen[i].peer == 0x101; has102 |= c.seen[i].peer == 0x102; has200 |= c.seen[i].peer == 0x200; }
      SCHECK(!has101 && has102 && has200 && x == SDG_SEEN_N + 2, "the book forgot the wrong tank (101 %d 102 %d 200 %d, x %d)", has101, has102, has200, x); }
    /* a giver brought back from an older save (the backup page): its count goes back, the clock does not - its next
       gift is a new one at the receiver, not an old verdict or a stale copy (the blind review's 3) */
    { sdg_book_t g, r; memset(&g, 0, sizeof g); memset(&r, 0, sizeof r); int32_t gb = 100, rb = 0; uint32_t n;
      sdg_out_begin(&g, &gb, 0xD, 10, 1800000000u, &n); SCHECK(n == 1800000000u, "the clock is not the floor: %u", n);
      sdg_in(&r, &rb, 0xE, n, 10, &fresh); sdg_out_settle(&g, &gb, 0xD, n, SDG_V_OK);
      sdg_book_t old; memset(&old, 0, sizeof old); old.seq = 3;            /* the older save: three gifts ago */
      sdg_out_begin(&old, &gb, 0xD, 10, 1800000600u, &n);
      SCHECK(n == 1800000600u && sdg_in(&r, &rb, 0xE, n, 10, &fresh) == SDG_V_OK && fresh && rb == 20, "a restored giver's gift was not new (nonce %u, %d)", n, rb);
      uint32_t m; sdg_out_settle(&old, &gb, 0xD, n, SDG_V_OK); sdg_out_begin(&old, &gb, 0xD, 1, 1800000600u, &m);
      SCHECK(m == n + 1, "two gifts in one second share a nonce (%u, %u)", n, m);
      /* ... and with NO clock its number is old there: OLD, nothing kept, the money back - never STILL SENDING (the review) */
      sdg_book_t old2; memset(&old2, 0, sizeof old2); old2.seq = 3; int32_t ob = 40, rb0 = rb; uint32_t k;
      sdg_out_begin(&old2, &ob, 0xD, 15, 0, &k);
      int v = sdg_in(&r, &rb, 0xE, k, 15, &fresh);
      SCHECK(v == SDG_V_OLD && !fresh && rb == rb0 && sdg_out_settle(&old2, &ob, 0xD, k, v) == 15 && ob == 40, "a restored giver's old number: %d, %d", v, ob); }
    /* NOTHING IS LOST (Strato 11-08): unanswered a day, it comes back - not an hour sooner, not on a clock that went back */
    { sdg_book_t g; memset(&g, 0, sizeof g); int32_t gb = 50; uint32_t n, t0 = 1800003600u;
      sdg_out_begin(&g, &gb, 0xD, 30, t0, &n);
      SCHECK(sdg_out_expire(&g, &gb, t0 + 23 * 3600) == 0 && gb == 20 && sdg_out_pending(&g), "given back before a day");
      SCHECK(sdg_out_expire(&g, &gb, t0 - 5 * 3600) == 0 && sdg_out_pending(&g), "a clock gone back gave it back");
      SCHECK(sdg_out_expire(&g, &gb, t0 - 5 * 3600 + 23 * 3600) == 0 && sdg_out_pending(&g), "the day did not start again after the clock went back");
      SCHECK(sdg_out_expire(&g, &gb, t0 - 5 * 3600 + 24 * 3600) == 30 && gb == 50 && !sdg_out_pending(&g), "not back after a day (%d)", gb);
      SCHECK(sdg_out_expire(&g, &gb, t0 + 99 * 3600) == 0 && gb == 50, "given back twice");
      sdg_out_begin(&g, &gb, 0xD, 10, 0, &n);                 /* no clock when it left: the first look that has one starts the day */
      SCHECK(sdg_out_expire(&g, &gb, 0) == 0 && sdg_out_expire(&g, &gb, t0) == 0 && sdg_out_expire(&g, &gb, t0 + 23 * 3600) == 0 && sdg_out_pending(&g), "no-clock gift back early");
      SCHECK(sdg_out_expire(&g, &gb, t0 + 24 * 3600) == 10 && gb == 50, "no-clock gift not back after a day"); }
}

static void dial_rules(void) {
    static tank_t t; tank_init(&t, 5); t.sd_balance = 40;
    setup_begin_sdgift(&t, LINK_RIGHT, 2);
    SCHECK(setup_is_sdgift() && setup_sdgift_amount() == 0, "the dial did not open at 000");
    setup_activate(&t, SETUP_HIT_NEXT); SCHECK(!setup_sdgift_confirming(), "NEXT at 000 went on");
    setup_activate(&t, SETUP_HIT_UP); SCHECK(setup_sdgift_amount() == 1, "the ones digit first: %d", setup_sdgift_amount());
    setup_activate(&t, SETUP_HIT_DOWN); setup_activate(&t, SETUP_HIT_DOWN); SCHECK(setup_sdgift_amount() == 0, "below 000: %d", setup_sdgift_amount());
    /* the odometer (Strato 11-08): 9 up is 10, 99 up is 100, 10 down is 9, 100 down is 99; the hundreds by 100; 999 stays */
    setup_sdgift_set(9);  setup_activate(&t, SETUP_HIT_UP);   SCHECK(setup_sdgift_amount() == 10, "9 up: %d (want 10)", setup_sdgift_amount());
    setup_sdgift_set(99); setup_activate(&t, SETUP_HIT_UP);   SCHECK(setup_sdgift_amount() == 100, "99 up: %d (want 100)", setup_sdgift_amount());
    setup_activate(&t, SETUP_HIT_DOWN); SCHECK(setup_sdgift_amount() == 99, "100 down: %d (want 99)", setup_sdgift_amount());
    setup_sdgift_set(10); setup_activate(&t, SETUP_HIT_DOWN); SCHECK(setup_sdgift_amount() == 9, "10 down: %d (want 9)", setup_sdgift_amount());
    setup_sdgift_set(999); setup_activate(&t, SETUP_HIT_UP);  SCHECK(setup_sdgift_amount() == 999, "999 up: %d", setup_sdgift_amount());
    setup_activate(&t, SETUP_HIT_SLOT0 + 1); setup_sdgift_set(95); setup_activate(&t, SETUP_HIT_UP); SCHECK(setup_sdgift_amount() == 105, "the tens past 9: %d (want 105)", setup_sdgift_amount());
    setup_sdgift_set(5); setup_activate(&t, SETUP_HIT_DOWN); SCHECK(setup_sdgift_amount() == 5, "tens down from 5: %d (stays)", setup_sdgift_amount());
    setup_sdgift_set(9); setup_activate(&t, SETUP_HIT_SLOT0); setup_activate(&t, SETUP_HIT_UP); SCHECK(setup_sdgift_amount() == 109, "the hundreds: %d", setup_sdgift_amount());
    setup_activate(&t, SETUP_HIT_NEXT); SCHECK(!setup_sdgift_confirming(), "NEXT past the balance went on");
    setup_sdgift_set(40); setup_activate(&t, SETUP_HIT_NEXT); SCHECK(setup_sdgift_confirming(), "NEXT at the whole balance stayed");
    setup_activate(&t, SETUP_HIT_BACK); SCHECK(setup_is_sdgift() && !setup_sdgift_confirming(), "BACK on the confirm closed the dial");
    setup_activate(&t, SETUP_HIT_NEXT); t.sd_balance = 30;   /* (spent meanwhile: SEND checks again) */
    setup_activate(&t, SETUP_HIT_NEXT); SCHECK(setup_is_sdgift() && !setup_sdgift_confirming() && setup_take_sdgift(NULL) == 0, "SEND past a balance that fell");
    setup_sdgift_set(25); setup_activate(&t, SETUP_HIT_NEXT); setup_activate(&t, SETUP_HIT_NEXT);
    int e = -1, n = setup_take_sdgift(&e);
    SCHECK(!setup_active() && n == 25 && e == LINK_RIGHT && setup_take_sdgift(NULL) == 0 && t.sd_balance == 30, "SEND: %d at %d (once), balance %d untouched", n, e, t.sd_balance);
    setup_begin_sdgift(&t, LINK_LEFT, 1); setup_sdgift_set(5); setup_activate(&t, SETUP_HIT_BACK);
    SCHECK(!setup_active() && setup_take_sdgift(NULL) == 0, "CANCEL sent");
    setup_begin_sdgift(&t, LINK_LEFT, 1); setup_sdgift_set(5); setup_activate(&t, SETUP_HIT_NEXT); setup_cancel(&t);
    SCHECK(!setup_active() && setup_take_sdgift(NULL) == 0, "a page taken away sent");
    /* by touch, as the glass does: a drag on the ones digit turns it, the SEND on the foot */
    setup_begin_sdgift(&t, LINK_RIGHT, 2);
    float x = PAGE_X + (PAGE_W - (2 * SETUP_SLOT_PX + SETUP_SLOT_W)) / 2 + 2 * SETUP_SLOT_PX + SETUP_SLOT_W / 2, y = PAGE_Y + SETUP_SLOT_Y + 20;
    setup_touch(&t, x, y, true); for (int k = 1; k <= 3 * SETUP_SPIN_PX + 4; k++) setup_touch(&t, x, y - k, true); setup_touch(&t, x, y - 3 * SETUP_SPIN_PX - 4, false);
    SCHECK(setup_sdgift_amount() == 3, "a drag up three steps: %d", setup_sdgift_amount());
    { float cy = PAGE_Y + SETUP_SLOT_Y - SETUP_ARROW_GAP;   /* the letter wheel's feel (0.4.2): a drag from the chevron turns it too */
      setup_touch(&t, x, cy, true); for (int k = 1; k <= 2 * SETUP_SPIN_PX + 4; k++) setup_touch(&t, x, cy + k, true); setup_touch(&t, x, cy + 2 * SETUP_SPIN_PX + 4, false);
      SCHECK(setup_sdgift_amount() == 1, "a drag down two steps from the chevron: %d", setup_sdgift_amount());
      for (int k = 0; k < 2; k++) { setup_touch(&t, x, y, true); for (int j = 1; j <= SETUP_SPIN_PX + 4; j++) setup_touch(&t, x, y - j, true); setup_touch(&t, x, y - SETUP_SPIN_PX - 4, false); } }
    SCHECK(setup_sdgift_amount() == 3, "back up to 3: %d", setup_sdgift_amount());
    float nx = PAGE_X + SETUP_TOP_NEXT_X + SETUP_TOP_BTN_W / 2, ny = PAGE_Y + SETUP_TOP_BTN_Y + SETUP_BTN_H / 2;
    setup_touch(&t, nx, ny, true); setup_touch(&t, nx, ny, false); SCHECK(setup_sdgift_confirming(), "a tap on NEXT");
    setup_touch(&t, nx, ny, true); setup_touch(&t, nx, ny, false); SCHECK(setup_sdgift_confirming(), "a second tap where NEXT was sent it");
    float sx = PAGE_X + SETUP_NEXT_X + SETUP_BTN_W / 2, sy = PAGE_Y + SETUP_BTN_Y + SETUP_BTN_H / 2;
    setup_touch(&t, sx, sy, true); setup_touch(&t, sx, sy, false);
    SCHECK(!setup_active() && setup_take_sdgift(NULL) == 3, "SEND by touch");
}

static void hud_rules(void) {
    link_view_t v; memset(&v, 0, sizeof v);
    v.edge[LINK_RIGHT].state = LV_OPEN; v.edge[LINK_RIGHT].sd = 1; v.edge[LINK_LEFT].state = LV_FREE;
    float x, y; render_link_where(6, &x, &y);
    render_link_leave(); render_link_touch(&v, x, y, true, 0.03f);
    SCHECK(render_link_touch(&v, x, y, false, 0.03f) == LINK_UI_SD_RIGHT, "a tap on the right door's coin");
    v.edge[LINK_RIGHT].sd = 0; render_link_leave(); render_link_touch(&v, x, y, true, 0.03f);
    SCHECK(render_link_touch(&v, x, y, false, 0.03f) != LINK_UI_SD_RIGHT, "a coin answered for a neighbor that takes no gifts (0.4.1)");
    v.edge[LINK_RIGHT].sd = 1; v.edge[LINK_RIGHT].state = LV_FREE; render_link_leave(); render_link_touch(&v, x, y, true, 0.03f);
    SCHECK(render_link_touch(&v, x, y, false, 0.03f) != LINK_UI_SD_RIGHT, "a coin answered on a closed door");
    /* the tab above it still holds (the coin takes nothing of the tab) */
    v.edge[LINK_RIGHT].state = LV_OPEN; float tx, ty; render_link_where(LINK_RIGHT, &tx, &ty);
    render_link_leave(); int fired = 0; for (int i = 0; i < 60 && !fired; i++) fired = render_link_touch(&v, tx, ty, true, 0.03f) == LINK_UI_CLOSE_RIGHT;
    render_link_touch(&v, tx, ty, false, 0.03f); render_link_leave();
    SCHECK(fired, "the open door's tab no longer closes on a hold");
    /* inside the glass on every board: the coin's corners */
    render_link_where(5, &x, &y);
    for (int k = 0; k < 2; k++) { float cx, cy; render_link_where(5 + k, &cx, &cy);
        SCHECK(cx - 23 >= tank_glass_x0(cy + 23) && cx + 23 <= tank_glass_x1(cy + 23) && cy + 23 < TANK_H, "door %d's coin off the glass (%.0f, %.0f)", k, cx, cy); }
}

/* two engines, two tanks (the real tank_link handlers), a radio that drops, repeats and reorders, doors that close, tanks
   that crash: every gift lands exactly once or comes back, and the money adds up after every step */
typedef struct { uint8_t b[LINK_FRAME_MAX]; int len, to; uint32_t at; } sg_frame_t;
static sg_frame_t sg_q[512]; static int sg_qn; static uint32_t sg_now, sg_rng = 1;
static float sg_drop = 0.2f, sg_dup = 0.05f;
static uint32_t sg_rand(void) { sg_rng = sg_rng * 1664525u + 1013904223u; return sg_rng >> 8; }
static float sg_rf(void) { return (sg_rand() & 0xffff) / 65536.0f; }
typedef struct { tank_t t; link_t *L; bool radio; sdg_book_t saved; int32_t saved_bal; float save_fail; } sg_tank_t;
static sg_tank_t SG[2];
static int sg_who(tank_t *t) { return t == &SG[0].t ? 0 : 1; }
static bool sg_save(tank_t *t) { sg_tank_t *s = &SG[sg_who(t)]; if (sg_rf() < s->save_fail) return false; s->saved = t->sdg; s->saved_bal = t->sd_balance; return true; }
static void sg_send(void *ctx, uint32_t to, const uint8_t *buf, int len) {
    int from = (int)(intptr_t)ctx; if (!SG[from].radio) return;
    for (int c = 0; c < (sg_rf() < sg_dup ? 2 : 1); c++) {
        if (sg_rf() < sg_drop || sg_qn >= 512) continue;
        sg_frame_t *f = &sg_q[sg_qn++]; memcpy(f->b, buf, (size_t)len); f->len = len; f->to = 1 - from; f->at = sg_now + 1 + sg_rand() % 120;
        (void)to;
    }
}
static int sg_room(void *ctx) { (void)ctx; return 0; }
static bool sg_vis(void *ctx, uint32_t uid, passport_t *pp) { (void)ctx; (void)uid; (void)pp; return false; }
typedef struct { uint32_t nonce; int got, settled, back, late; } sg_rec_t;   /* a gift of tank k: credits at the other, its answers */
static sg_rec_t sg_rec[2][2048]; static int sg_nrec[2];
static sg_rec_t *sg_of(int k, uint32_t nonce) {
    for (int i = 0; i < sg_nrec[k]; i++) if (sg_rec[k][i].nonce == nonce) return &sg_rec[k][i];
    if (sg_nrec[k] >= 2048) return NULL;
    sg_rec_t *r = &sg_rec[k][sg_nrec[k]++]; memset(r, 0, sizeof *r); r->nonce = nonce; return r;
}
static uint32_t sg_clock_base;   /* 0: no clock (the count alone); else the seconds the soak's clock started at */
static uint32_t sg_clock(void) { return sg_clock_base ? sg_clock_base + sg_now / 1000 * 3600 : 0; }   /* (a soak hour a second: a day in 24 s) */
static int sg_doubles, sg_late;   /* gifts given back after a day: in all, and the ones the other tank HAD taken (the chosen trade) */
static void sg_boot(int k) {
    link_host_t h = { (void *)(intptr_t)k, sg_send, sg_room, sg_vis, NULL, NULL, NULL, NULL };
    link_init(SG[k].L, SG[k].t.uid, &h, sg_rand(), sg_now);
    SG[k].radio = true;
}
static void sg_crash(int k) {   /* the power goes: everything not saved is gone; the engine starts over */
    SG[k].t.sdg = SG[k].saved; SG[k].t.sd_balance = SG[k].saved_bal;
    for (int i = 0; i < sg_qn; ) { if (sg_q[i].to == k) sg_q[i] = sg_q[--sg_qn]; else i++; }   /* (frames in its air: lost) */
    sg_boot(k);
}
static void sg_link(void) {
    link_mode(SG[0].L, true); link_mode(SG[1].L, true);
    for (int i = 0; i < 400 && !(link_neighbor(SG[0].L, LINK_RIGHT) && link_neighbor(SG[1].L, LINK_LEFT)); i++) {
        if (i % 20 == 0) { link_arm(SG[0].L, LINK_RIGHT); link_arm(SG[1].L, LINK_LEFT); }
        sg_now += 20;
        for (int q = 0; q < sg_qn; ) { if ((int32_t)(sg_q[q].at - sg_now) <= 0) { sg_frame_t f = sg_q[q]; sg_q[q] = sg_q[--sg_qn]; if (SG[f.to].radio) link_rx(SG[f.to].L, f.b, f.len, -40, sg_now); } else q++; }
        for (int k = 0; k < 2; k++) { link_tick(SG[k].L, sg_now); link_ev_t e; while (link_poll(SG[k].L, &e)) { tank_link_event(&SG[k].t, SG[k].L, &e); tank_link_sd_news(NULL, NULL, NULL); } }
    }
    link_mode(SG[0].L, false); link_mode(SG[1].L, false);
}
static int32_t sg_total0;
static void sg_check_money(const char *when) {
    /* the money: both balances, plus each gift in flight that its receiver has not credited */
    int64_t sum = (int64_t)SG[0].t.sd_balance + SG[1].t.sd_balance;
    for (int k = 0; k < 2; k++) {
        const sdg_book_t *b = &SG[k].t.sdg;
        if (!sdg_out_pending(b)) continue;
        bool credited = false;
        for (int i = 0; i < SDG_SEEN_N; i++) { const sdg_entry_t *e = &SG[1 - k].t.sdg.seen[i];
            if (e->peer == SG[k].t.uid && e->nonce == b->out.nonce && e->verdict == SDG_V_OK) credited = true; }
        if (!credited) sum += b->out.amount;
    }
    SCHECK(sum == sg_total0, "%s: the money does not add up (%lld, want %d; A %d B %d)", when, (long long)sum, sg_total0, SG[0].t.sd_balance, SG[1].t.sd_balance);
}
static void sg_step(void) {
    sg_now += 10;
    for (int q = 0; q < sg_qn; ) { if ((int32_t)(sg_q[q].at - sg_now) <= 0) { sg_frame_t f = sg_q[q]; sg_q[q] = sg_q[--sg_qn]; if (SG[f.to].radio) link_rx(SG[f.to].L, f.b, f.len, -40, sg_now); } else q++; }
    for (int k = 0; k < 2; k++) {
        link_tick(SG[k].L, sg_now);
        SG[k].t.clock = sg_now / 10.0;                          /* (tank time 100x: the give-back looks once a minute of it) */
        { int32_t b0 = SG[k].t.sd_balance; uint32_t n0 = SG[k].t.sdg.out.nonce; int a0 = SG[k].t.sdg.out.amount;
          tank_link_tick(&SG[k].t, SG[k].L);
          int ne, na, nv, nk = tank_link_sd_news(&ne, &na, &nv);
          if (nk == TL_SD_BACK_LATE) {
              sg_late++;
              SCHECK(na == a0 && SG[k].t.sd_balance == b0 + a0 && !sdg_out_pending(&SG[k].t.sdg) && !link_sd_busy(SG[k].L), "a day's give-back: %d of %d, still sending %d", SG[k].t.sd_balance - b0, a0, link_sd_busy(SG[k].L));
              bool took = false;
              for (int i = 0; i < SDG_SEEN_N; i++) { const sdg_entry_t *q = &SG[1 - k].t.sdg.seen[i];
                  if (q->peer == SG[k].t.uid && q->nonce == n0 && q->verdict == SDG_V_OK) took = true; }
              if (took) { sg_doubles++; sg_total0 += a0; }   /* (the trade Strato chose: both keep it - counted, never hidden) */
              sg_rec_t *r = sg_of(k, n0); if (r) r->late++;
          } }
        link_ev_t e;
        while (link_poll(SG[k].L, &e)) {
            tank_link_event(&SG[k].t, SG[k].L, &e);
            int ne, na, nv, nk = tank_link_sd_news(&ne, &na, &nv);
            sg_rec_t *r;
            if (e.type == LEV_SD_IN && nk == TL_SD_GOT && (r = sg_of(1 - k, e.nonce))) {   /* (a credit here: the other's gift) */
                SCHECK(!r->late, "gift %u credited AFTER it came back to its giver", e.nonce); r->got++; }
            if (e.type == LEV_SD_DONE && (nk == TL_SD_SENT || nk == TL_SD_BACK) && (r = sg_of(k, e.nonce))) { r->settled++; if (nk == TL_SD_BACK) r->back++; }
        }
    }
    sg_check_money("a step");
}
static void soak(int runs, uint32_t seed) {
    tank_link_sd_save = sg_save; tank_link_sd_clock = sg_clock;
    SG[0].L = calloc(1, (size_t)link_size()); SG[1].L = calloc(1, (size_t)link_size());
    int gifts = 0, refused = 0, crashes = 0, unlinks = 0; sg_late = sg_doubles = 0;
    for (int run = 0; run < runs; run++) {
        sg_rng = seed + (uint32_t)run * 7919u; sg_qn = 0; sg_now = 1000;
        sg_nrec[0] = sg_nrec[1] = 0; sg_clock_base = run % 2 ? 1790000000u + (uint32_t)run * 1000 : 0;
        for (int k = 0; k < 2; k++) {
            tank_init(&SG[k].t, 100 + run * 2 + k); SG[k].t.uid = 0xA0000000u + (uint32_t)run * 4 + (uint32_t)k + 1;
            SG[k].t.sd_balance = (int32_t)(sg_rand() % 3000);
            if (run % 9 == 4 && k == 1) SG[k].t.sd_balance = SDG_BALANCE_CAP - (int32_t)(sg_rand() % 400);   /* near the cap: refusals */
            if (run % 11 == 5 && k == 0) SG[k].t.sd_balance = 0;                                              /* nothing to give */
            SG[k].save_fail = run % 5 == 2 ? 0.15f : 0;
            SG[k].saved = SG[k].t.sdg; SG[k].saved_bal = SG[k].t.sd_balance;
            sg_boot(k);
        }
        sg_total0 = SG[0].t.sd_balance + SG[1].t.sd_balance;
        sg_drop = run % 3 == 0 ? 0.0f : run % 3 == 1 ? 0.2f : 0.5f; sg_dup = run % 4 == 0 ? 0.3f : 0.05f;
        sg_link();
        SCHECK(link_neighbor(SG[0].L, LINK_RIGHT) && link_neighbor(SG[1].L, LINK_LEFT), "run %d: never linked", run);
        SCHECK(link_peer_sd(SG[0].L, SG[1].t.uid) && link_peer_sd(SG[1].L, SG[0].t.uid), "run %d: a 0.4.2 tank does not say it takes gifts", run);
        for (int step = 0; step < 4000; step++) {
            for (int k = 0; k < 2; k++) {                        /* a keeper presses SEND now and then - both at once too */
                if (sg_rf() < 0.01f) {
                    int amt = 1 + (int)(sg_rand() % (sg_rf() < 0.2f ? 999 : 60));
                    int32_t b0 = SG[k].t.sd_balance; bool pend = sdg_out_pending(&SG[k].t.sdg);
                    uint32_t want = sg_rf() < 0.05f ? 0x5eed : sg_rf() < 0.5f ? SG[1 - k].t.uid : 0;   /* (now and then a dial opened for another tank) */
                    int r = tank_link_sd_send(&SG[k].t, SG[k].L, k == 0 ? LINK_RIGHT : LINK_LEFT, want, amt);
                    SCHECK(want != 0x5eed || r == TL_SD_NO_DOOR, "run %d: sent through a door that leads to another tank now (%d)", run, r);
                    tank_link_sd_news(NULL, NULL, NULL);
                    if (r == TL_SD_SENDING) { gifts++; SCHECK(SG[k].t.sd_balance == b0 - amt, "run %d: SEND took %d of %d", run, b0 - SG[k].t.sd_balance, amt); }
                    else { SCHECK(SG[k].t.sd_balance == b0, "run %d: a refused SEND (%d) moved money", run, r);
                           SCHECK(r != TL_SD_BUSY || pend, "run %d: BUSY with nothing in flight", run);
                           SCHECK(r != TL_SD_NO_FUNDS || amt > b0, "run %d: NO FUNDS with %d for %d", run, b0, amt); }
                }
            }
            if (sg_rf() < 0.002f) { sg_crash((int)(sg_rand() & 1)); crashes++; sg_link(); }
            if (sg_rf() < 0.002f) {                              /* a door closes mid-send (the keeper, a drift, sleep), opens later */
                link_close(SG[sg_rand() & 1].L, LINK_RIGHT); link_close(SG[0].L, LINK_RIGHT); link_close(SG[1].L, LINK_LEFT); unlinks++;
                for (int w = 0, n = sg_rf() < 0.15f ? 2600 : 50 + (int)(sg_rand() % 300); w < n; w++) sg_step();   /* (sometimes past a day) */
                sg_link();
            }
            if (sg_rf() < 0.001f) { int k = (int)(sg_rand() & 1); SG[k].radio = false; for (int w = 0; w < 200; w++) sg_step(); SG[k].radio = true; }   /* deaf a while */
            sg_step();
        }
        /* settle: a good radio, saves that work, the doors open - everything in flight lands */
        sg_drop = 0; sg_dup = 0; SG[0].save_fail = SG[1].save_fail = 0;
        if (!link_neighbor(SG[0].L, LINK_RIGHT)) sg_link();
        for (int w = 0; w < 3000 && (sdg_out_pending(&SG[0].t.sdg) || sdg_out_pending(&SG[1].t.sdg)); w++) sg_step();
        SCHECK(!sdg_out_pending(&SG[0].t.sdg) && !sdg_out_pending(&SG[1].t.sdg), "run %d: a gift never settled", run);
        SCHECK(SG[0].t.sd_balance + SG[1].t.sd_balance == sg_total0, "run %d: money made or lost (%d + %d != %d)", run, SG[0].t.sd_balance, SG[1].t.sd_balance, sg_total0);
        for (int k = 0; k < 2; k++)
            for (int i = 0; i < sg_nrec[k]; i++) {
                const sg_rec_t *r = &sg_rec[k][i];   /* each gift of k's: credited once and not given back, or given back and never credited */
                SCHECK(r->got <= 1, "run %d: gift %u of tank %d credited %d times", run, r->nonce, k, r->got);
                SCHECK(!(r->got && r->back), "run %d: gift %u of tank %d credited AND given back", run, r->nonce, k);
                SCHECK(!(r->late && (r->back || r->settled)), "run %d: gift %u of tank %d given back after a day AND answered", run, r->nonce, k);
                refused += r->back > 0;
            }
    }
    /* a 0.4.1 neighbor: no coin, nothing sent, nothing moved */
    { sg_rng = seed ^ 0x41; sg_qn = 0; sg_drop = 0; sg_dup = 0;
      for (int k = 0; k < 2; k++) { tank_init(&SG[k].t, 7 + k); SG[k].t.uid = 0xB0000001u + (uint32_t)k; SG[k].t.sd_balance = 500; SG[k].save_fail = 0;
                                    SG[k].saved = SG[k].t.sdg; SG[k].saved_bal = 500; sg_boot(k); }
      link_test_as_041(SG[1].L, true); sg_total0 = 1000;
      sg_link();
      link_view_t v; link_view(SG[0].L, &v);
      SCHECK(v.edge[LINK_RIGHT].state == LV_OPEN && !v.edge[LINK_RIGHT].sd, "0.4.1: linked %d, a coin shown %d", v.edge[LINK_RIGHT].state, v.edge[LINK_RIGHT].sd);
      SCHECK(tank_link_sd_send(&SG[0].t, SG[0].L, LINK_RIGHT, 0, 10) == TL_SD_NO_DOOR && SG[0].t.sd_balance == 500, "0.4.1: a gift was sent to it");
      link_sd_send(SG[0].L, SG[1].t.uid, 77, 10);            /* (forced through anyway: it drops the frames - no answer, nothing there) */
      for (int w = 0; w < 300; w++) sg_step();
      SCHECK(SG[1].t.sd_balance == 500 && link_sd_busy(SG[0].L), "0.4.1: it took sand dollars"); }
    tank_link_sd_save = NULL; tank_link_sd_clock = NULL;
    free(SG[0].L); free(SG[1].L);
    printf("selftest-sdgift soak: %d runs, %d gifts sent, %d came back (%d after a day, %d of those also taken next door), %d crashes, %d doors closed mid-run\n",
           runs, gifts, refused, sg_late, sg_doubles, crashes, unlinks);
    SCHECK(sg_late > 0, "the soak never gave a gift back after a day (the test does not reach it)");
}

/* the book rides the save (2016..2103), with the balance; an older save has none */
static void save_rules(void) {
    const char *sav = getenv("POCKET_TANK_SAVE");
    if (!sav) { setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-sdgift.sav", 1); sav = getenv("POCKET_TANK_SAVE"); }
    static tank_t t; tank_init(&t, 31); tank_new_population(&t); tank_ensure_ids(&t);
    t.sd_balance = 1234; t.sdg.seq = 7; t.sdg.out.peer = 0xBEEF; t.sdg.out.nonce = 7; t.sdg.out.amount = 55;
    t.sdg.seen[2].peer = 0xCAFE; t.sdg.seen[2].nonce = 3; t.sdg.seen[2].amount = 9; t.sdg.seen[2].verdict = SDG_V_OK; t.sdg.seen[2].age = 4;
    sdg_book_t want = t.sdg;
    SCHECK(progression_save(&t), "the save failed");
    tank_init(&t, 31); SCHECK(!sdg_out_pending(&t.sdg) && !t.sdg.seq, "tank_init kept a gift");
    progression_wake(&t, 0);
    SCHECK(!memcmp(&t.sdg, &want, sizeof want) && t.sd_balance == 1234, "the book did not come back from the save (seq %u, out %u, bal %d)", t.sdg.seq, t.sdg.out.nonce, t.sd_balance);
    static uint8_t f[4096]; FILE *fp = fopen(sav, "rb"); size_t n = fp ? fread(f, 1, sizeof f, fp) : 0; if (fp) fclose(fp);
    SCHECK(n == 2200, "the save is %zu B (want 2200)", n);
    uint32_t seq; memcpy(&seq, f + 2016 + 12, 4); SCHECK(seq == 7, "the giving count is not at 2028 (%u)", seq);
    fp = fopen(sav, "wb"); if (fp) { fwrite(f, 1, 2008, fp); fclose(fp); }   /* the save before this build: no book */
    tank_init(&t, 31); progression_wake(&t, 0);
    SCHECK(!sdg_out_pending(&t.sdg) && !t.sdg.seq && !t.sdg.seen[2].peer && t.sd_balance == 1234, "an older save gave a gift book");
    /* the keeper's RESET with a gift in flight: the new tank can never settle it - back into the wallet at once */
    t.sdg = want; t.sd_balance = 1234; progression_save(&t);
    progression_reset(&t, 77);
    SCHECK(!sdg_out_pending(&t.sdg) && t.sd_balance == 1234 + 55, "a reset lost the gift in flight (balance %d)", t.sd_balance);
    { static uint8_t blob[4096];
      memset(blob, 0, sizeof blob); memcpy(blob, f, n);                 /* a stored tank's save with the gift in flight: REMOVE's refund */
      SCHECK(progression_blob_sd_out(blob, n) == 55 && progression_blob_sd_out(blob, 2008) == 0, "REMOVE does not see a stored tank's gift in flight (%d)", progression_blob_sd_out(blob, n)); }
}

int selftest_sdgift(void) {
    g_fail = 0;
    save_rules();
    book_rules();
    dial_rules();
    hud_rules();
    const char *ns = getenv("SDGIFT_SOAK");
    soak(ns ? atoi(ns) : 40, 2026);
    printf("selftest-sdgift: %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
