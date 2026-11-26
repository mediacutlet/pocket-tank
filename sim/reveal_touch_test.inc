/* THE REVEAL AND THE LAB THROUGH THE BOARD'S TOUCH PORT (0.4.3 audit round 1: TP-1..6, TP-8, TP-9b, PE-3; the V5 probes made
 * to assert). #included into sim/selftest_touch.c above selftest_touch_port(), which runs reveal_touch_port() in every world.
 * THE TOUCH RULE (touch_gate.h): the second half of a doubled tap - a press within TAP_W of the step it brought up - is no
 * tap; a single tap, slow or firm, acts at once. Each case names its id; its control (one deliberate tap) must still act. */
static void rt_ready(int i, bool shown) {
    fish_t *f = &T.fish[i];
    f->stage = STAGE_ELDER; f->evo = 0; f->away = false;
    f->evo_offer = (uint16_t)(EVO_LURE | EVO_HUMP << 5 | EVO_HORN << 10 | (shown ? EVO_SHOWN : 0));
}
static void rt_spread(void) {                    /* every fish but 0 far to the right */
    for (int i = 1; i < T.n_fish; i++) { T.fish[i].x = TANK_W - 40; T.fish[i].y = TANK_H / 2.0f + 20 * i; }
}
static bool rt_at(int id, float *x, float *y) { return find_hit_read(hit_setup, id, x, y); }
/* the reveal open with every tile landed (the spin already shown), tile 0 picked */
static bool rt_choosing(float *kx, float *ky, float *bx, float *by) {
    bare(); rt_ready(0, true);
    setup_begin_evolve_tank(&T, 0);
    if (!setup_is_evolve()) { TFAIL("reveal: the page did not open"); return false; }
    hold_for(400);
    float sx, sy;
    if (!rt_at(SETUP_HIT_SLOT0, &sx, &sy) || !rt_at(SETUP_HIT_NEXT, kx, ky) || !rt_at(SETUP_HIT_BACK, bx, by)) { TFAIL("reveal: no tile / KEEP / BACK the panel reads"); setup_cancel(&T); return false; }
    tap(sx, sy, 90, 700);
    return true;
}
static void rt_keep(void) {
    float kx, ky, bx, by;
    /* TP-1: a double tap on KEEP THIS brings the KEEP? question up and leaves it up - nothing kept */
    if (rt_choosing(&kx, &ky, &bx, &by)) {
        dbl(kx, ky); lift(300);
        if (T.fish[0].evo) TFAIL("TP-1: a double tap on KEEP THIS kept %s - the KEEP? question was never read", tank_evo_name(T.fish[0].evo));
        else if (setup_evo_phase(&T) != SETUP_EVO_KEEP) TFAIL("TP-1: a double tap on KEEP THIS left phase %d, want the question (%d)", setup_evo_phase(&T), SETUP_EVO_KEEP);
        setup_cancel(&T);
    }
    /* ... its control: one tap, then YES held a moment later, keeps the part (and slow, firm presses on KEEP THIS do too; YES is
       a 0.5 s hold since R2-L32) */
    for (int firm = 0; firm < 2; firm++) if (rt_choosing(&kx, &ky, &bx, &by)) {
        const int ms = firm ? 600 : 90;
        tap(kx, ky, ms, 300);
        if (setup_evo_phase(&T) != SETUP_EVO_KEEP) TFAIL("TP-1 control: a %s tap on KEEP THIS: phase %d, want the question", firm ? "firm" : "single", setup_evo_phase(&T));
        tap(kx, ky, 650, 300);
        if (T.fish[0].evo != EVO_LURE) TFAIL("TP-1 control: a %s tap on KEEP THIS, then YES held: part %d, want %d", firm ? "firm" : "single", T.fish[0].evo, EVO_LURE);
        if (setup_active()) setup_cancel(&T);
        T.fish[0].evo = 0;
    }
    /* ... a quick keeper (tap, then YES held from 0.36 s after the question came up) gets through: the hold counts from its press */
    if (rt_choosing(&kx, &ky, &bx, &by)) {
        tap(kx, ky, 90, 360); tap(kx, ky, 650, 300);
        if (T.fish[0].evo != EVO_LURE) TFAIL("TP-1 control: YES held from 0.36 s after the question: part %d, want %d (a deliberate press counts at once)", T.fish[0].evo, EVO_LURE);
        if (setup_active()) setup_cancel(&T);
        T.fish[0].evo = 0;
    }
    /* TP-2: the question up, a double tap on NOT YET is back on CHOOSE ONE - the reveal stays */
    if (rt_choosing(&kx, &ky, &bx, &by)) {
        tap(kx, ky, 90, 700);
        dbl(bx, by); lift(300);
        if (!setup_is_evolve()) TFAIL("TP-2: a double tap on NOT YET closed the whole reveal");
        else if (setup_evo_phase(&T) != SETUP_EVO_CHOOSE) TFAIL("TP-2: a double tap on NOT YET: phase %d, want CHOOSE ONE (%d)", setup_evo_phase(&T), SETUP_EVO_CHOOSE);
        if (setup_active()) setup_cancel(&T);
    }
    /* ... its control: NOT YET, then LATER a moment on, closes it */
    if (rt_choosing(&kx, &ky, &bx, &by)) {
        tap(kx, ky, 90, 700); tap(bx, by, 90, 700);
        if (setup_evo_phase(&T) != SETUP_EVO_CHOOSE) TFAIL("TP-2 control: one tap on NOT YET: phase %d, want CHOOSE ONE", setup_evo_phase(&T));
        tap(bx, by, 90, 700);
        if (setup_is_evolve()) TFAIL("TP-2 control: LATER a moment after NOT YET did not close the reveal");
        if (setup_active()) setup_cancel(&T);
    }
    T.fish[0].evo = 0; T.fish[0].evo_offer = 0;
}
/* the first spin's landing, from `t0` (or -1 if it never lands in 20 s) */
static double rt_landed(double t0) {
    for (int i = 0; i < 500; i++) { if (setup_evo_phase(&T) == SETUP_EVO_CHOOSE) return T.clock - t0; frame(); }
    return -1;
}
static void rt_open(void) {
    /* TP-5: a READY fish under the card's corner, tapped twice: the first tap is its card, the second half is no tap on it */
    const float fx = RENDER_CARD_X + RENDER_CARD_W / 2.0f, fy = RENDER_CARD_Y + 120;
    bare(); rt_ready(0, false); rt_spread(); T.fish[0].x = fx; T.fish[0].y = fy;
    dbl(fx, fy); lift(300);
    if (setup_is_evolve()) { TFAIL("TP-5: a double tap on a READY fish under its card opened the reveal (the second half on the card)"); setup_cancel(&T); }
    /* ... its control: the card up, a tap on it a moment later opens the reveal */
    bare(); rt_ready(0, false); rt_spread(); T.fish[0].x = TANK_W / 2.0f; T.fish[0].y = TANK_H / 2.0f;
    tap(T.fish[0].x, T.fish[0].y, 90, 700);
    if (touch_port_selected() != 0) { TFAIL("TP-5 control: no card for the READY fish"); return; }
    const float cx = RENDER_CARD_X + RENDER_CARD_W / 2.0f, cy = RENDER_CARD_Y + 60;
    double t1 = T.clock;
    tap(cx, cy, 90, FRAME_MS);
    if (!setup_is_evolve()) { TFAIL("TP-5 control: a tap on the READY fish's card did not open the reveal"); return; }
    const double single = rt_landed(t1);
    setup_cancel(&T);
    /* TP-6: the tap that opened it, doubled: its second half does not hurry the first (once-only) spin */
    bare(); rt_ready(0, false); rt_spread(); T.fish[0].x = TANK_W / 2.0f; T.fish[0].y = TANK_H / 2.0f;
    tap(T.fish[0].x, T.fish[0].y, 90, 700);
    t1 = T.clock;
    dbl(cx, cy);
    if (!setup_is_evolve()) { TFAIL("TP-6: a double tap on the card did not open the reveal"); return; }
    const double dbl_s = rt_landed(t1);
    if (single < 0 || dbl_s < 0) TFAIL("TP-6: the spin never landed (single %.2f, double %.2f)", single, dbl_s);
    else if (dbl_s < single - 0.5) TFAIL("TP-6: the opening tap's double hurried the first spin: all landed %.2f s on, %.2f s from one tap", dbl_s, single);
    setup_cancel(&T);
    /* ... its control: a deliberate tap during the spin still hurries it */
    bare(); rt_ready(0, false); rt_spread(); T.fish[0].x = TANK_W / 2.0f; T.fish[0].y = TANK_H / 2.0f;
    tap(T.fish[0].x, T.fish[0].y, 90, 700);
    t1 = T.clock;
    tap(cx, cy, 90, 1500);
    tap(TANK_W / 2.0f, TANK_H / 2.0f, 90, FRAME_MS);
    const double hurried = rt_landed(t1);
    if (hurried < 0 || hurried > single - 0.5) TFAIL("TP-6 control: a tap 1.6 s into the spin did not hurry it (%.2f s, unhurried %.2f s)", hurried, single);
    setup_cancel(&T);
    T.fish[0].evo_offer = 0;
}
static const char *rt_page(void) {
    int p = setup_page();
    return p == SETUP_PG_LAB_BODY ? "BODY" : p == SETUP_PG_LAB_FINS ? "FINS" : p == SETUP_PG_LAB_COLORS ? "COLORS" : p == SETUP_PG_LAB_NAME ? "NAME" : p == SETUP_PG_LAB_TUBE ? "TUBE" : "?";
}
static void rt_lab(void) {
    /* TP-4: a doubled NEXT / BACK moves one page, as a single one does; POUR IT IN once pours one */
    T.sd_unlocks |= SD_ITEM_LAB; T.sd_balance = 5000; r2_fish(3);
    struct { int page, id, want; const char *what; } C[] = {
        { SETUP_PG_LAB_BODY, SETUP_HIT_NEXT, SETUP_PG_LAB_FINS, "NEXT on BODY" }, { SETUP_PG_LAB_FINS, SETUP_HIT_NEXT, SETUP_PG_LAB_COLORS, "NEXT on FINS" },
        { SETUP_PG_LAB_COLORS, SETUP_HIT_NEXT, SETUP_PG_LAB_NAME, "NEXT on COLORS" }, { SETUP_PG_LAB_NAME, SETUP_HIT_NEXT, SETUP_PG_LAB_TUBE, "NEXT on NAME" },
        { SETUP_PG_LAB_TUBE, SETUP_HIT_BACK, SETUP_PG_LAB_NAME, "BACK on the TUBE" } };
    for (int c = 0; c < 5; c++) for (int d = 0; d < 2; d++) {
        bare(); setup_begin_lab(&T); if (!setup_is_lab()) { TFAIL("TP-4: the lab did not open"); return; }
        setup_lab_goto(&T, C[c].page); hold_for(400);
        float x, y; if (!rt_at(C[c].id, &x, &y)) { TFAIL("TP-4: no %s the panel reads", C[c].what); setup_cancel(&T); continue; }
        if (d) dbl(x, y); else tap(x, y, 90, FRAME_MS);
        lift(300);
        if (!setup_is_lab() || setup_page() != C[c].want) TFAIL("TP-4: a %s %s: lab up %d, page %s - want one page on", d ? "DOUBLE" : "single", C[c].what, setup_is_lab(), setup_is_lab() ? rt_page() : "-");
        if (setup_active()) setup_cancel(&T);
    }
    /* ... two deliberate NEXTs (the second pressed 0.36 s after the first lifted) still move two pages */
    bare(); setup_begin_lab(&T); hold_for(400);
    { float x, y; if (rt_at(SETUP_HIT_NEXT, &x, &y)) { tap(x, y, 90, 360); tap(x, y, 90, 300);
        if (setup_page() != SETUP_PG_LAB_COLORS) TFAIL("TP-4 control: NEXT twice, 0.36 s apart, on BODY: page %s, want COLORS", rt_page()); } }
    if (setup_active()) setup_cancel(&T);
    /* ... POUR IT IN, single and doubled: one fry for 250 */
    for (int d = 0; d < 2; d++) {
        bare(); setup_begin_lab(&T); setup_lab_goto(&T, SETUP_PG_LAB_TUBE); hold_for(400);
        float x, y; if (!rt_at(SETUP_HIT_POUR, &x, &y)) { TFAIL("TP-4: no POUR IT IN"); setup_cancel(&T); return; }
        const int n0 = T.n_fish; const int32_t b0 = T.sd_balance;
        if (d) dbl(x, y); else tap(x, y, 90, FRAME_MS);
        lift(300);
        if (T.n_fish != n0 + 1 || b0 - T.sd_balance != SD_PRICE_LAB_FRY) TFAIL("TP-4: a %s POUR IT IN: fish %d -> %d, balance %d -> %d (want one fry for %d)", d ? "DOUBLE" : "single", n0, T.n_fish, (int)b0, (int)T.sd_balance, SD_PRICE_LAB_FRY);
        if (setup_active()) setup_cancel(&T);
        while (progression_newborn() >= 0) progression_newborn_done(&T);
        while (T.n_fish > 3) progression_sell_fish(&T, T.n_fish - 1);
    }
}
/* PE-3: a fry's welcome owed, room in the tank - the lab says why, never TANK FULL */
static void rt_lab_why(void) {
    r2_fish(3); T.tank_ms_bits |= TMS_FIRST_SALE; T.sd_balance = 5000; T.sd_unlocks |= SD_ITEM_LAB;
    if (!progression_buy_fry(&T, 0)) { TFAIL("PE-3: the shop's fry was not bought"); return; }
    if (progression_newborn() < 0 || !progression_fry_room(&T)) TFAIL("PE-3: no welcome owed with room (newborn %d, room %d) - the case is not set up", progression_newborn(), progression_fry_room(&T));
    else if (progression_lab_why(&T) != LAB_WELCOME) TFAIL("PE-3: a welcome owed and room in the tank: the lab says %d (FULL = %d), want WELCOME (%d)", progression_lab_why(&T), LAB_FULL, LAB_WELCOME);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    if (progression_lab_why(&T) != LAB_OK) TFAIL("PE-3 control: the welcome done, room: the lab says %d, want OK", progression_lab_why(&T));
    r2_fish(5);
    if (progression_lab_why(&T) != LAB_FULL) TFAIL("PE-3 control: five fish: the lab says %d, want FULL", progression_lab_why(&T));   /* (five stay: r2_fish only adds) */
}
/* TP-8 / TP-9b: a notice that comes while the lab, the reveal or MORE's pages are up waits under it and comes up after -
   ticked with setup_holds_notices, the rule main.c and the sim now share (this checks the rule; main.c's whole `blocked`
   expression, ui_cover with it, is not run on the host) */
static void rt_tick(float s) { for (int i = 0; i < (int)(s / 0.04f); i++) { notice_tick(&T, 0.04f, setup_holds_notices(&T)); frame(); } }
static void rt_notices(void) {
    T.sd_unlocks |= SD_ITEM_LAB; T.sd_balance = 5000;   /* (r2_fish only adds: the tank keeps the fish it has) */
    for (int pg = 0; pg < 3; pg++) {
        const char *what = pg == 2 ? "MORE's pages" : pg ? "the reveal" : "the lab";
        bare(); notice_sync(&T); while (notice_current()) { notice_dismiss(); rt_tick(1); }
        if (pg == 2) setup_begin_about(&T, 0); else if (pg) { rt_ready(0, true); setup_begin_evolve_tank(&T, 0); } else setup_begin_lab(&T);
        if (!setup_active()) { TFAIL("TP-8/9b: %s did not open", what); continue; }
        hold_for(300); notice_sync(&T);
        notice_low_battery(); rt_tick(0.5f);
        if (notice_current()) TFAIL("TP-%s: LOW BATTERY came up over %s (drawn over it, or up undrawn under it)", pg == 2 ? "9b (MORE)" : pg ? "9b" : "8", what);
        rt_tick(NOTICE_UP_S + 1);
        if (!setup_active()) TFAIL("TP-8/9b: %s closed by itself", what);
        setup_cancel(&T);
        rt_tick(1.5f);
        if (!notice_current() || notice_current()->kind != NOTICE_LOW_BATTERY) TFAIL("TP-%s: %s closed, LOW BATTERY did not come up after it (it went by unseen)", pg == 2 ? "9b (MORE)" : pg ? "9b" : "8", what);
        while (notice_current()) { notice_dismiss(); rt_tick(1); }
        T.fish[0].evo_offer = 0;
    }
}
/* TP-3: RESET THIS TANK answered over the reveal (main.c reset_tank's calls, in order): the new tank's WELCOME, whole */
static void rt_reset(void) {
    bare(); rt_ready(0, true); setup_begin_evolve_tank(&T, 0); hold_for(200);
    if (!setup_is_evolve()) { TFAIL("TP-3: the reveal did not open"); return; }
    progression_reset(&T, 4242u); notice_sync(&T); setup_begin(&T); hold_for(200);
    if (setup_is_evolve() || !setup_active() || setup_page() != SETUP_PG_WELCOME)
        TFAIL("TP-3: reset over the reveal: evolve %d, setup up %d, page %d - want the new tank's WELCOME (%d)", setup_is_evolve(), setup_active(), setup_page(), SETUP_PG_WELCOME);
    float x, y;
    if (rt_at(SETUP_HIT_NEXT, &x, &y)) {
        tap(x, y, 90, 700);
        if (!setup_active() || setup_page() == SETUP_PG_WELCOME) TFAIL("TP-3: NEXT on the new tank's WELCOME: setup up %d, page %d - want the naming", setup_active(), setup_page());
    } else TFAIL("TP-3: the WELCOME has no NEXT the panel reads");
    /* the sibling: the first run's creator, walked with one tap at a time (0.4 s apart) on NEXT, reaches its end - the
       page stamp (TP-4) eats only a doubled tap's second half */
    for (int k = 0; k < 40 && setup_active(); k++) {
        if (!rt_at(SETUP_HIT_NEXT, &x, &y)) { TFAIL("TP-3 sibling: the creator's page %d has no NEXT the panel reads", setup_page()); break; }
        tap(x, y, 90, 310);
    }
    if (setup_active()) TFAIL("TP-3 sibling: forty NEXTs 0.4 s apart did not walk the first run's creator to its end (page %d)", setup_page());
    else if (progression_setup_pending()) TFAIL("TP-3 sibling: the creator closed with the naming still owed");
    if (setup_active()) setup_cancel(&T);
    progression_setup_done(&T); while (progression_newborn() >= 0) progression_newborn_done(&T);
}
/* TP-7 (0.4.3 audit r1): the fish swims next door while its reveal is up - KEEP is not offered (progression_evo_keep keeps
   only at home): no KEEP THIS, no YES, the reason in their place; the page stays; home again, KEEP THIS is back */
static void rt_away(void) {
    float kx, ky, bx, by;
    if (rt_choosing(&kx, &ky, &bx, &by)) {             /* away on CHOOSE ONE, a part tried on */
        T.fish[0].away = true; hold_for(200);
        tap(kx, ky, 90, 700);
        if (!setup_is_evolve()) TFAIL("TP-7: KEEP THIS with the fish next door closed the reveal");
        else if (setup_evo_phase(&T) != SETUP_EVO_CHOOSE) TFAIL("TP-7: KEEP THIS with the fish next door: phase %d, want CHOOSE ONE (no question asked)", setup_evo_phase(&T));
        tap(kx, ky, 90, 700);
        if (!setup_is_evolve() || T.fish[0].evo) TFAIL("TP-7: twice on KEEP THIS's place next door: reveal up %d, part %d - want up, none", setup_is_evolve(), T.fish[0].evo);
        T.fish[0].away = false; hold_for(200);         /* home: KEEP THIS is back */
        tap(kx, ky, 90, 700);
        if (setup_evo_phase(&T) != SETUP_EVO_KEEP) TFAIL("TP-7 control: home again, KEEP THIS: phase %d, want the question", setup_evo_phase(&T));
        tap(kx, ky, 650, 700);                         /* (YES: a hold, R2-L32) */
        if (T.fish[0].evo != EVO_LURE) TFAIL("TP-7 control: home again, KEEP THIS + YES held: part %d, want %d", T.fish[0].evo, EVO_LURE);
        if (setup_active()) setup_cancel(&T);
        T.fish[0].evo = 0;
    }
    if (rt_choosing(&kx, &ky, &bx, &by)) {             /* the question up, then it leaves: YES keeps nothing, and the page stays */
        tap(kx, ky, 90, 700);
        T.fish[0].away = true; hold_for(200);
        tap(kx, ky, 90, 700);
        if (!setup_is_evolve()) TFAIL("TP-7: YES with the fish gone next door closed the reveal silently (still READY)");
        if (T.fish[0].evo) TFAIL("TP-7: YES with the fish next door kept part %d", T.fish[0].evo);
        tap(bx, by, 90, 700);                          /* NOT YET still works */
        if (!setup_is_evolve() || setup_evo_phase(&T) != SETUP_EVO_CHOOSE) TFAIL("TP-7: NOT YET next door: reveal up %d, phase %d, want CHOOSE ONE", setup_is_evolve(), setup_evo_phase(&T));
        tap(bx, by, 90, 700);                          /* and LATER closes it, the three kept */
        if (setup_is_evolve()) TFAIL("TP-7: LATER next door did not close the reveal");
        if (!tank_evo_ready(&T.fish[0])) TFAIL("TP-7: the fish is no longer READY after LATER next door");
        if (setup_active()) setup_cancel(&T);
    }
    T.fish[0].away = false; T.fish[0].evo = 0; T.fish[0].evo_offer = 0;
}
/* ---- 0.4.3 AUDIT ROUND 2 (R2-4, R2-L11, R2-L14, R2-L16; R2-L32 reports) ----
 * The frame hook runs notice_tick after the port's polls with firmware main.c's own `blocked` (its tank task: hold_light ->
 * ui_cover, setup_holds_notices, the GIVE modal, the gift card, link mode's page), as the board does every frame. */
static bool rc_watch; static int rc_first = -1;     /* the first notice up since rc_watch was set */
static bool rc_blocked(void) {
    T.hold_light = (setup_active() && !setup_is_about()) || touch_port_confirm_up();
    T.ui_cover = T.hold_light || touch_port_milestones() || touch_port_settings() || touch_port_updates() || touch_port_shop()
              || touch_port_collection() || touch_port_battery() || touch_port_shelf() || adv_up();
    return T.ui_cover || setup_holds_notices(&T) || render_give_modal_step(&T) != RENDER_GM_OFF || render_gift_up(&T) || link_dev_page_up();
}
static void rc_hook(void) {
    notice_tick(&T, FRAME_MS / 1000.0f, rc_blocked());
    if (rc_watch && rc_first < 0 && notice_current()) rc_first = notice_current()->kind;
}
static void rc_on(bool on) { g_frame_hook = on ? rc_hook : NULL; if (!on) { T.hold_light = false; T.ui_cover = false; rc_watch = false; } }
static void rc_clear(void) {                        /* nothing up, nothing queued */
    for (int k = 0; k < 40 && (notice_current() || notice_pending()); k++) { notice_dismiss(); hold_for(NOTICE_GAP_S * 1000 + 200); }
}
static const char *rc_kind(int k) {
    return k == NOTICE_LOW_BATTERY ? "LOW BATTERY" : k == NOTICE_WRONG_BOARD ? "WRONG BOARD" : k == NOTICE_PARAGON ? "the PARAGON card" : k == NOTICE_EVOLVE ? "READY TO EVOLVE"
         : k == NOTICE_STAGE ? "the stage card" : k < 0 ? "none" : "a card";
}
/* the reveal of fish 0 on CHOOSE ONE, its own cards said first (only what the case queues waits under it) */
static bool rc_reveal(float *sx, float *sy, float *kx, float *ky, float *bx, float *by) {
    bare(); rt_ready(0, true); hold_for(300); rc_clear();
    setup_begin_evolve_tank(&T, 0); hold_for(400);
    if (!setup_is_evolve() || !rt_at(SETUP_HIT_SLOT0, sx, sy) || !rt_at(SETUP_HIT_NEXT, kx, ky) || !rt_at(SETUP_HIT_BACK, bx, by)) { TFAIL("R2: the reveal did not open, or no tile / KEEP / BACK the panel reads"); if (setup_active()) setup_cancel(&T); return false; }
    return true;
}
/* R2-4: a double tap (100-150 ms apart) on the button that closes a page: the notice held under the page comes up as it
   closes, and the double's second half is no tap on it (THE TOUCH RULE's W, as for a card that stepped aside and came back).
   Control: the second tap 0.7 s on is a tap - it closes the notice at once */
static void rt_close_notices(void) {
    rc_on(true);
    T.sd_unlocks |= SD_ITEM_LAB; T.sd_balance = 5000;
    static const int GAPS[3] = { 100, 150, 700 };
    float mx, my; foot(MSF_CLOSE, &mx, &my);
    int n = 0;
    for (int pg = 0; pg < 3; pg++) for (int which = 0; which < 2; which++) for (int g = 0; g < 3; g++) {
        const char *pn = pg == 0 ? "the reveal's LATER" : pg == 1 ? "the lab's BACK" : "the keeper page's CLOSE";
        const int kind = which ? NOTICE_WRONG_BOARD : NOTICE_LOW_BATTERY;
        float sx, sy, kx, ky, bx = 0, by = 0; bool ok;
        if (pg == 0) ok = rc_reveal(&sx, &sy, &kx, &ky, &bx, &by);
        else { bare(); rc_clear();
               if (pg == 1) { setup_begin_lab(&T); hold_for(400); ok = setup_is_lab() && rt_at(SETUP_HIT_BACK, &bx, &by); }
               else { touch_port_show_milestones(true); hold_for(800); bx = mx; by = my; ok = touch_port_milestones(); } }
        if (!ok) { TFAIL("R2-4: %s did not open, or no button the panel reads", pn); if (setup_active()) setup_cancel(&T); continue; }
        if (which) notice_wrong_board(); else notice_low_battery();
        hold_for(1000);
        if (notice_current() || !notice_pending()) TFAIL("R2-4: %s was not held under %s (up %d, queued %d)", rc_kind(kind), pn, notice_current() != NULL, notice_pending());
        tap(bx, by, 90, GAPS[g]);
        if (pg == 2 ? touch_port_milestones() : setup_active()) TFAIL("R2-4: %s did not close the page", pn);
        tap(bx, by, 90, FRAME_MS); lift(300);
        const notice_t *c = notice_current();
        if (g < 2 && (!c || c->kind != kind))
            TFAIL("R2-4: %s doubled (%d ms apart) with %s held under it: after it %s up - the double's second half took it, unread", pn, GAPS[g], rc_kind(kind), rc_kind(c ? c->kind : -1));
        if (g == 2 && c && c->kind == kind)
            TFAIL("R2-4 control: %s, then a tap 0.7 s on: %s still up - a deliberate tap must close it at once", pn, rc_kind(kind));
        if (setup_active()) setup_cancel(&T);
        touch_port_dismiss(); rc_clear(); T.fish[0].evo_offer = 0; n++;
    }
    /* the PARAGON card YES earns as it closes the reveal (YES a hold since R2-L32: the card comes with the finger still down):
       the hold's lift, and the lift held on a while, leave it up */
    for (int d = 0; d < 2; d++) {
        float sx, sy, kx, ky, bx, by;
        if (!rc_reveal(&sx, &sy, &kx, &ky, &bx, &by)) break;
        tap(sx, sy, 90, 700); tap(kx, ky, 90, 700);                /* a tile, KEEP THIS: the question */
        tap(kx, ky, d ? 1500 : 650, 300);                          /* YES held full (and on, a second) */
        const notice_t *c = notice_current();
        if (!T.fish[0].evo) TFAIL("R2-4: YES held %s kept nothing", d ? "1.5 s" : "0.65 s");
        else if (!c || c->kind != NOTICE_PARAGON) TFAIL("R2-4: YES held %s: after its lift %s up, want the PARAGON card - the hold's lift took it, unread", d ? "1.5 s" : "0.65 s", rc_kind(c ? c->kind : -1));
        if (setup_active()) setup_cancel(&T);
        rc_clear(); T.fish[0].evo = 0; T.fish[0].evo_offer = 0; n++;
    }
    /* another fish's cards (its stage, its READY) queued under the reveal, LATER doubled: the first card that comes stays up */
    for (int d = 0; d < 2; d++) {
        float sx, sy, kx, ky, bx, by;
        bare(); T.fish[1].evo_offer = 0; T.fish[1].stage = STAGE_ADULT; T.fish[1].away = false; hold_for(200); rc_clear();
        if (!rc_reveal(&sx, &sy, &kx, &ky, &bx, &by)) break;
        rt_ready(1, false); hold_for(1000);
        if (!notice_pending()) TFAIL("R2-4: fish 1's cards were not held under the reveal");
        rc_watch = true; rc_first = -1;
        if (d) dbl(bx, by); else tap(bx, by, 90, FRAME_MS);
        lift(300);
        const notice_t *c = notice_current();
        if (!c || c->kind != rc_first) TFAIL("R2-4: LATER %s with fish 1's cards held: %s came up, then %s is up - %s", d ? "doubled" : "single", rc_kind(rc_first), rc_kind(c ? c->kind : -1), "the double's second half took the first, unread");
        rc_watch = false;
        if (setup_active()) setup_cancel(&T);
        rc_clear(); T.fish[0].evo_offer = 0; T.fish[1].evo_offer = 0; T.fish[1].stage = STAGE_ADULT; n++;
    }
    /* siblings: a notice that was UP when the lab opened over it (it stepped aside and comes back) - the window it always had;
       a notice that comes up by itself over the bare tank - no window: a tap 100 ms after it appears closes it (G1 only) */
    {   bare(); rc_clear(); notice_low_battery(); hold_for(400);
        float bx, by;
        if (!notice_current()) TFAIL("R2-4 sibling: LOW BATTERY did not come up over the bare tank");
        setup_begin_lab(&T); hold_for(500);
        if (!rt_at(SETUP_HIT_BACK, &bx, &by)) TFAIL("R2-4 sibling: the lab has no BACK the panel reads");
        else { dbl(bx, by); lift(300);
               if (!notice_current() || notice_current()->kind != NOTICE_LOW_BATTERY) TFAIL("R2-4 sibling: LOW BATTERY up before the lab, lab BACK doubled: it went, unread"); }
        if (setup_active()) setup_cancel(&T);
        rc_clear(); n++;
        bare(); rc_clear(); notice_low_battery();
        for (int k = 0; k < 60 && !notice_current(); k++) frame();
        frame(); tap(TANK_W / 2.0f, TANK_H / 2.0f, 90, 200);   /* (a press begun a frame after it came up: a tap) */
        if (notice_current()) TFAIL("R2-4 sibling: LOW BATTERY up by itself over the bare tank, a tap a frame after: still up - no window is armed without a page");
        rc_clear(); n++;
    }
    rc_on(false);
    printf("selftest-touch-port: R2-4 a page closed by a double tap - %d cases (LOW BATTERY, WRONG BOARD under the reveal / the lab / the keeper page, 100 / 150 / 700 ms; the PARAGON card; another fish's cards)\n", n);
}
/* R2-L11: a READY card queued while its fish was home is held while the fish is next door, and comes when it is home */
static void rt_ready_away(void) {
    rc_on(true);
    bare(); T.fish[0].evo_offer = 0; T.fish[0].stage = STAGE_ELDER; T.fish[0].away = false; hold_for(300); rc_clear();
    setup_begin_lab(&T); hold_for(300);                              /* a page over the tank: the card waits under it */
    rt_ready(0, false); hold_for(500);
    if (!notice_pending()) TFAIL("R2-L11: fish 0's READY was not queued under the lab");
    T.fish[0].away = true;                                           /* it goes next door, the card still queued */
    setup_cancel(&T); hold_for(1500);
    const notice_t *c = notice_current();
    if (c && c->kind == NOTICE_EVOLVE && c->fish == 0) TFAIL("R2-L11: READY TO EVOLVE came up while its fish is next door (its tap opens nothing)");
    T.fish[0].away = false; hold_for(1500);                          /* home again */
    c = notice_current();
    if (!c || c->kind != NOTICE_EVOLVE || c->fish != 0) TFAIL("R2-L11: home again and still READY: %s up, want READY TO EVOLVE", rc_kind(c ? c->kind : -1));
    else {                                                           /* up at home: it leaves while it is up - the card steps aside, and comes back */
        T.fish[0].away = true; hold_for(400);
        c = notice_current();
        if (c && c->kind == NOTICE_EVOLVE) TFAIL("R2-L11: READY TO EVOLVE stayed up as its fish went next door");
        T.fish[0].away = false; hold_for(1500);
        c = notice_current();
        if (!c || c->kind != NOTICE_EVOLVE) TFAIL("R2-L11: the card that stepped aside did not come back when the fish was home");
        else {                                                       /* and its tap opens the reveal */
            const float ex = PAGE_X + MSP_MODAL_X + MSP_MODAL_W / 2.0f, ey = PAGE_Y + MSP_MODAL_Y + MSP_MODAL_H / 2.0f;   /* (the card's middle) */
            if (render_notice_evolve_tap(&T, ex, ey) != 0) TFAIL("R2-L11: the READY card has no place its tap opens the reveal");
            else { lift(700); tap(ex, ey, 90, 300); if (!setup_is_evolve()) TFAIL("R2-L11: home again, the READY card tapped: no reveal"); }
        }
    }
    if (setup_active()) setup_cancel(&T);
    rc_clear(); T.fish[0].away = false; T.fish[0].evo_offer = 0;
    rc_on(false);
}
/* R2-L14: a fry's welcome owed with room - the shop's FRY row and LAB row say the same (the modals say why) */
static void rt_shop_rows(void) {
    while (T.n_fish > 3) progression_sell_fish(&T, T.n_fish - 1);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    T.tank_ms_bits |= TMS_FIRST_SALE; T.sd_balance = 5000; T.sd_unlocks |= SD_ITEM_LAB;
    bool got = false;                                                /* (an offer still for sale: the earlier cases bought the first) */
    for (int k = 0; k < FRY_OFFER_N && !got; k++) { fry_offer_t o; progression_fry_offer(k, &o); if (!o.sold) got = progression_buy_fry(&T, k); }
    if (!got) { TFAIL("R2-L14: no fry was bought from the shop (%d left)", progression_fry_left()); return; }
    const char *fry = render_shop_row_word(&T, SHOP_ROW_FRY), *lab = render_shop_row_word(&T, SHOP_ROW_LAB);
    if (strcmp(fry, lab) || !strcmp(fry, "FULL")) TFAIL("R2-L14: a welcome owed, room in the tank: the FRY row says %s, the LAB row %s - want the same, never FULL", fry, lab);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    fry = render_shop_row_word(&T, SHOP_ROW_FRY); lab = render_shop_row_word(&T, SHOP_ROW_LAB);
    if ((strcmp(fry, "CHOOSE") && strcmp(fry, "SOLD OUT")) || strcmp(lab, "OPEN")) TFAIL("R2-L14 control: the welcome done, room: FRY %s, LAB %s - want CHOOSE (or SOLD OUT), OPEN", fry, lab);
    r2_fish(5);
    fry = render_shop_row_word(&T, SHOP_ROW_FRY); lab = render_shop_row_word(&T, SHOP_ROW_LAB);
    if (strcmp(fry, "FULL") || strcmp(lab, "FULL")) TFAIL("R2-L14 control: five fish: FRY %s, LAB %s - want FULL, FULL", fry, lab);
}
/* R2-L13 (Strato's yes, 11-23): a born fry's welcome owed, the light off by the keeper - the lab holds the light on while it is up,
   and its tube said WELCOME FRY FIRST (the welcome cannot play: the light goes back off as the lab closes). The tube says LIGHT ON
   FIRST, read from the light as the keeper set it; the shop's LAB and FRY modals give the same reason, their rows the same word.
   A bought fry's welcome plays in the dark: WELCOME, wherever it is asked */
static void rt_lab_light(void) {
    while (T.n_fish > 3) progression_sell_fish(&T, T.n_fish - 1);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    T.tank_ms_bits |= TMS_FIRST_SALE; T.sd_balance = 5000; T.sd_unlocks |= SD_ITEM_LAB;
    bool got = false;
    for (int k = 0; k < FRY_OFFER_N && !got; k++) { fry_offer_t o; progression_fry_offer(k, &o); if (!o.sold) got = progression_buy_fry(&T, k); }
    if (!got) { TFAIL("R2-L13: no fry was bought from the shop"); return; }
    const int nb = progression_newborn();
    if (nb < 0 || nb >= T.n_fish) { TFAIL("R2-L13: no welcome owed"); return; }
    const bool la = T.light_auto, lm = T.light_manual_off, lo = T.light_override;
    T.light_override = false; T.light_auto = false;
    for (int bought = 0; bought < 2; bought++) for (int dark = 0; dark < 2; dark++) {
        if (bought) T.fish[nb].ms_bits |= MS_BOUGHT; else T.fish[nb].ms_bits &= ~MS_BOUGHT;   /* (a born fry: not bought) */
        T.light_manual_off = dark != 0;
        const bool waits = dark && !bought;
        /* the lab up on its tube: the page holds the light on (tank_tick: night false) */
        T.hold_light = true; T.night = false;
        const char *tube = setup_lab_pour_word(&T), *want = waits ? "LIGHT ON FIRST" : "WELCOME FRY FIRST";
        if (strcmp(tube, want)) TFAIL("R2-L13: %s fry, light %s, the lab up (the light held on): the tube says %s, want %s", bought ? "a bought" : "a born", dark ? "off" : "on", tube, want);
        /* the shop: not held - the light as it is */
        T.hold_light = false; T.night = dark != 0;
        const char *lm1 = render_shop_why_line(&T, SHOP_ROW_LAB), *fm1 = render_shop_why_line(&T, SHOP_ROW_FRY), *wl = waits ? "LIGHT ON TO NAME THE FRY" : "WELCOME THE NEW FRY";
        if (strcmp(lm1, wl) || strcmp(fm1, wl)) TFAIL("R2-L13: %s fry, light %s: the shop's LAB modal says %s, FRY modal %s - want %s for both", bought ? "a bought" : "a born", dark ? "off" : "on", lm1, fm1, wl);
        const char *lr = render_shop_row_word(&T, SHOP_ROW_LAB), *fr = render_shop_row_word(&T, SHOP_ROW_FRY);   /* (the FRY row: SOLD OUT once the three are bought) */
        if (progression_fry_left() > 0 && strcmp(lr, fr)) TFAIL("R2-L13/L14: %s fry, light %s: the LAB row says %s, the FRY row %s - want the same", bought ? "a bought" : "a born", dark ? "off" : "on", lr, fr);
    }
    T.light_auto = la; T.light_manual_off = lm; T.light_override = lo; T.night = false; T.hold_light = false;
    while (progression_newborn() >= 0) progression_newborn_done(&T);
}
/* R2-L16: the sim's own stat card (sim/main.c): EVOLVE spends the double of the tap that opened the card, as the board's (TP-5) */
extern void sim_card_seen(int sel, uint32_t now_ms);
extern bool sim_card_tap_spent(uint32_t press_ms, uint32_t now_ms);
static void rt_sim_card(void) {
    sim_card_seen(-1, 1000); sim_card_seen(0, 2000);                 /* the card opened at 2.0 s (the first tap's lift) */
    if (!sim_card_tap_spent(2100, 2190)) TFAIL("R2-L16: the sim's card: a press 100 ms after it opened (the double's second half) opened the reveal");
    if (!sim_card_tap_spent(1950, 2190)) TFAIL("R2-L16: the sim's card: a press from before it opened opened the reveal");
    if (sim_card_tap_spent(2700, 2790)) TFAIL("R2-L16 control: the sim's card: a tap 0.7 s after it opened was spent");
    sim_card_seen(0, 9000);                                          /* (the same card still up: its stamp stays) */
    if (sim_card_tap_spent(2700, 9000)) TFAIL("R2-L16 control: the card's stamp moved while it stayed up");
    sim_card_seen(-1, 9500);
}
/* R2-L32 (Strato 11-23: "a short hold for keep this"): KEEP?'s YES keeps on a 0.5 s hold, never on a tap - a mash on KEEP THIS
   at any rate keeps nothing; a tap, a firm press short of the hold, a hold begun as the question came (the double's second half),
   a hold that slides off YES: nothing, and nothing lost; held full it keeps at once, the finger still down, and its lift is
   nothing (the PARAGON card stays up). NOT YET stays a tap */
static void rt_keep_hold(void) {
    static const struct { int down, up, n; const char *hz; } M[] = { { 90, 110, 3, "5 Hz" }, { 90, 110, 6, "5 Hz" }, { 60, 65, 4, "8 Hz" }, { 50, 50, 8, "10 Hz" }, { 90, 160, 5, "4 Hz" }, { 250, 100, 5, "3 Hz firm" } };
    for (int m = 0; m < 6; m++) {
        float kx, ky, bx, by;
        if (!rt_choosing(&kx, &ky, &bx, &by)) return;
        for (int k = 0; k < M[m].n; k++) tap(kx, ky, M[m].down, M[m].up);
        lift(400);
        if (T.fish[0].evo) TFAIL("R2-L32: a mash on KEEP THIS x%d at %s kept %s - the KEEP? question was never read", M[m].n, M[m].hz, tank_evo_name(T.fish[0].evo));
        if (!setup_is_evolve()) TFAIL("R2-L32: a mash on KEEP THIS x%d at %s closed the reveal", M[m].n, M[m].hz);
        if (setup_active()) setup_cancel(&T);
        T.fish[0].evo = 0; T.fish[0].evo_offer = 0;
    }
    float kx, ky, bx, by;
    if (rt_choosing(&kx, &ky, &bx, &by)) {
        tap(kx, ky, 90, 700);                                        /* the question */
        tap(kx, ky, 90, 400);                                        /* a tap on YES */
        if (T.fish[0].evo || setup_evo_phase(&T) != SETUP_EVO_KEEP) TFAIL("R2-L32: a tap on YES: part %d, phase %d - want none, the question still up", T.fish[0].evo, setup_evo_phase(&T));
        tap(kx, ky, 350, 400);                                       /* a firm press, short of the hold */
        if (T.fish[0].evo || setup_evo_phase(&T) != SETUP_EVO_KEEP) TFAIL("R2-L32: YES pressed 350 ms: part %d - want none, the question still up", T.fish[0].evo);
        press(kx, ky, 300); press(TANK_W / 2.0f, 8, 400); lift(400);   /* held, then slid off YES */
        if (T.fish[0].evo) TFAIL("R2-L32: YES held 300 ms then slid off for 400 ms: part %d kept", T.fish[0].evo);
        press(kx, ky, 450);                                          /* not yet full ... */
        if (T.fish[0].evo) TFAIL("R2-L32: YES held 450 ms kept already");
        press(kx, ky, 150);                                          /* ... full: kept at once, the finger still down */
        if (T.fish[0].evo != EVO_LURE) TFAIL("R2-L32: YES held 600 ms: part %d, want %d (kept while held)", T.fish[0].evo, EVO_LURE);
        if (setup_is_evolve()) TFAIL("R2-L32: YES held full: the reveal still up");
        T.hold_active = false; hold_for(900);                        /* still held a while: no hold on the tank (the fish called, a piece's page) */
        if (T.hold_active || setup_active()) TFAIL("R2-L32: the finger left down after YES's hold kept: the fish called %d, a page up %d - the rest of that press is nothing", T.hold_active, setup_active());
        lift(400);                                                   /* then lifted: nothing more */
        if (setup_active() || touch_port_milestones() || s_sel >= 0) TFAIL("R2-L32: the hold's lift after the keep acted: setup %d, keeper page %d, card %d", setup_active(), touch_port_milestones(), s_sel);
        if (setup_active()) setup_cancel(&T);
        T.fish[0].evo = 0; T.fish[0].evo_offer = 0;
    }
    if (rt_choosing(&kx, &ky, &bx, &by)) {                           /* KEEP THIS doubled, its second half held on YES: no hold (W) */
        tap(kx, ky, 90, 100); press(kx, ky, 900); lift(300);
        if (T.fish[0].evo) TFAIL("R2-L32: KEEP THIS doubled, its second half held 0.9 s on YES: kept %d - that press began within TAP_W of the question", T.fish[0].evo);
        else { press(kx, ky, 650); lift(300); if (T.fish[0].evo != EVO_LURE) TFAIL("R2-L32 control: then YES held 650 ms: part %d, want %d", T.fish[0].evo, EVO_LURE); }
        if (setup_active()) setup_cancel(&T);
        T.fish[0].evo = 0; T.fish[0].evo_offer = 0;
    }
    if (rt_choosing(&kx, &ky, &bx, &by)) {                           /* a mash on YES itself, at 10 Hz for 1.2 s */
        tap(kx, ky, 90, 700);
        for (int k = 0; k < 12; k++) tap(kx, ky, 50, 50);
        lift(300);
        if (T.fish[0].evo) TFAIL("R2-L32: a 10 Hz mash on YES for 1.2 s kept %d", T.fish[0].evo);
        tap(bx, by, 90, 400);                                        /* NOT YET: still a tap */
        if (!setup_is_evolve() || setup_evo_phase(&T) != SETUP_EVO_CHOOSE) TFAIL("R2-L32: NOT YET (a tap): reveal %d, phase %d - want CHOOSE ONE", setup_is_evolve(), setup_evo_phase(&T));
        if (setup_active()) setup_cancel(&T);
        T.fish[0].evo = 0; T.fish[0].evo_offer = 0;
    }
}
/* R3-L1 (0.4.3 audit r3): KEEP?'s HOLD with the reveal's fish in the doorway (DOOR_ASKED / DOOR_PASSING, ~1-3 s a trip):
   progression refuses the keep there (R2-L2), so the page must not close as if kept. The hold fills and WAITS, full, the
   page up; the moment the fish is out of the doorway at home, the same finger keeps. Gone next door: VISITING, the hold
   empties, nothing kept across the visit. A new deliberate hold at home keeps at once (no added wait) */
static void rt_door_clear(void) { T.fish[0].door = DOOR_NONE; T.fish[0].away = false; }
static void rt_doorway(void) {
    for (int d = 0; d < 2; d++) {
        const char *dn = d ? "PASSING" : "ASKED";
        float kx, ky, bx, by;
        if (!rt_choosing(&kx, &ky, &bx, &by)) return;
        tap(kx, ky, 90, 700);                                        /* the question */
        T.fish[0].door = d ? DOOR_PASSING : DOOR_ASKED;
        press(kx, ky, 650);                                          /* past the 0.5 s hold, in the doorway */
        if (!setup_is_evolve() || setup_evo_phase(&T) != SETUP_EVO_KEEP || T.fish[0].evo)
            TFAIL("R3-L1: HOLD held 650 ms with the fish in the doorway (%s): reveal up %d, phase %d, part %d - want the question still up, nothing kept (nothing may look kept)", dn, setup_is_evolve(), setup_evo_phase(&T), T.fish[0].evo);
        float bar = setup_evo_bar(&T);                               /* R3b-L11 (Strato 11-23): the bar stops short - a full bar means kept */
        if (!(bar >= 0.85f && bar < 0.95f)) TFAIL("R3b-L11: HOLD held 650 ms in the doorway (%s), nothing kept: the bar drawn at %.3f - want just short of full (0.85..0.95)", dn, bar);
        press(kx, ky, 400);                                          /* still held, still in the doorway */
        if (!setup_is_evolve() || T.fish[0].evo) TFAIL("R3-L1: HOLD held 1.05 s in the doorway (%s): reveal up %d, part %d", dn, setup_is_evolve(), T.fish[0].evo);
        bar = setup_evo_bar(&T);
        if (!(bar >= 0.85f && bar < 0.95f)) TFAIL("R3b-L11: HOLD held 1.05 s in the doorway (%s), nothing kept: the bar drawn at %.3f - want just short of full", dn, bar);
        T.fish[0].door = DOOR_NONE;                                  /* out of the doorway, at home: the same finger keeps at once */
        press(kx, ky, FRAME_MS * 2);
        if (T.fish[0].evo != EVO_LURE || setup_is_evolve()) TFAIL("R3-L1: HOLD still held full as the fish left the doorway (%s) at home: part %d, reveal up %d - want kept at once", dn, T.fish[0].evo, setup_is_evolve());
        lift(300);
        if (setup_active()) setup_cancel(&T);
        rt_door_clear(); T.fish[0].evo = 0; T.fish[0].evo_offer = 0;
    }
    float kx, ky, bx, by;
    if (rt_choosing(&kx, &ky, &bx, &by)) {                           /* held full in the doorway, then let go: nothing; home, a fresh hold keeps */
        tap(kx, ky, 90, 700);
        T.fish[0].door = DOOR_ASKED; press(kx, ky, 700);
        if (!(setup_evo_bar(&T) >= 0.85f && setup_evo_bar(&T) < 0.95f)) TFAIL("R3b-L11: HOLD held 700 ms in the doorway: the bar drawn at %.3f - want just short of full", setup_evo_bar(&T));
        lift(300);
        if (T.fish[0].evo || !setup_is_evolve() || setup_evo_phase(&T) != SETUP_EVO_KEEP) TFAIL("R3-L1: HOLD held full in the doorway, let go: part %d, reveal %d, phase %d - want nothing kept, the question up", T.fish[0].evo, setup_is_evolve(), setup_evo_phase(&T));
        if (setup_evo_bar(&T) != 0) TFAIL("R3b-L11: let go in the doorway: the bar drawn at %.3f - want empty", setup_evo_bar(&T));
        T.fish[0].door = DOOR_NONE; press(kx, ky, 300);              /* sibling: at home the bar fills as before (no cap below the keep) */
        { const float b = setup_evo_bar(&T); if (!(b >= 0.5f && b < 0.85f)) TFAIL("R3b-L11 sibling: home, HOLD held 300 ms: the bar drawn at %.3f - want about 0.6, as before", b); }
        {   int polls = 0; float top = 0;                            /* full only as it is kept: every frame up to the keep draws less than full */
            while (setup_is_evolve() && polls < 40) { press(kx, ky, FRAME_MS); polls++; const float b = setup_evo_bar(&T); if (setup_is_evolve() && b > top) top = b; }
            if (setup_is_evolve() || T.fish[0].evo != EVO_LURE) TFAIL("R3b-L11 control: home, HOLD held on frame by frame: reveal %d, part %d - want kept", setup_is_evolve(), T.fish[0].evo);
            if (top >= 1 || (int)(128 * top) >= 128) TFAIL("R3b-L11: home, a frame before the keep drew the bar at %.4f - its last pixel lit (a full bar is a keep)", top); }
        if (T.fish[0].evo != EVO_LURE) TFAIL("R3-L1 control: home again, a fresh HOLD of 560 ms: part %d, want %d", T.fish[0].evo, EVO_LURE);
        lift(300); if (setup_active()) setup_cancel(&T);
        rt_door_clear(); T.fish[0].evo = 0; T.fish[0].evo_offer = 0;
    }
    if (rt_choosing(&kx, &ky, &bx, &by)) {                           /* passing, then next door mid-hold: VISITING, the hold gone; home with the finger still down: nothing */
        tap(kx, ky, 90, 700);
        T.fish[0].door = DOOR_PASSING; press(kx, ky, 650);
        T.fish[0].door = DOOR_NONE; T.fish[0].away = true; press(kx, ky, 300);
        if (T.fish[0].evo || !setup_is_evolve()) TFAIL("R3-L1: the fish went next door under a full HOLD: part %d, reveal %d - want nothing kept, the page up (VISITING)", T.fish[0].evo, setup_is_evolve());
        if (setup_evo_bar(&T) != 0) TFAIL("R3b-L11: gone next door under a HOLD: the bar drawn at %.3f - want empty (VISITING)", setup_evo_bar(&T));
        T.fish[0].away = false; press(kx, ky, 700);
        if (T.fish[0].evo) TFAIL("R3-L1: a HOLD begun before the visit kept on the fish's return (part %d) - the visit ends that press's hold", T.fish[0].evo);
        lift(300); press(kx, ky, 560); lift(300);
        if (T.fish[0].evo != EVO_LURE) TFAIL("R3-L1 control: home, a new HOLD of 560 ms: part %d, want %d", T.fish[0].evo, EVO_LURE);
        if (setup_active()) setup_cancel(&T);
        rt_door_clear(); T.fish[0].evo = 0; T.fish[0].evo_offer = 0;
    }
    if (rt_choosing(&kx, &ky, &bx, &by)) {                           /* the direct answer (setup_activate) in the doorway: no close either */
        tap(kx, ky, 90, 700);
        T.fish[0].door = DOOR_ASKED; setup_activate(&T, SETUP_HIT_NEXT);
        if (T.fish[0].evo || !setup_is_evolve()) TFAIL("R3-L1: setup_activate(NEXT) on KEEP? in the doorway: part %d, reveal %d - want nothing kept, the page up", T.fish[0].evo, setup_is_evolve());
        T.fish[0].door = DOOR_NONE; setup_activate(&T, SETUP_HIT_NEXT);
        if (T.fish[0].evo != EVO_LURE || setup_is_evolve()) TFAIL("R3-L1 control: setup_activate(NEXT) at home: part %d, reveal %d", T.fish[0].evo, setup_is_evolve());
        if (setup_active()) setup_cancel(&T);
        rt_door_clear(); T.fish[0].evo = 0; T.fish[0].evo_offer = 0;
    }
}
/* R3-L2 (0.4.3 audit r3): the reveal's fish leaves mid-hold (a trade finishing, a gift committing: the page closes BETWEEN
   polls, setup_fish_removed) - the rest of that press, and its lift, are nothing: no fish called, no piece's page. Siblings:
   a press begun on the bare water still calls the fish; the next deliberate tap acts at once */
static void rt_gone_mid(void) {
    for (int v = 0; v < 2; v++) {                                    /* v 1: a piece under HOLD - its placement page must not open */
        float kx, ky, bx, by;
        if (!rt_choosing(&kx, &ky, &bx, &by)) return;
        tap(kx, ky, 90, 700);
        int item = -1;
        if (v) { item = 2; T.sd_unlocks |= SD_ITEMS[item].bit; T.sd_shelved &= ~SD_ITEMS[item].bit; tank_decor_set(&T, item, kx, DECOR_Z_BACK);
                 if (tank_decor_hit(&T, kx, ky) != item) { TFAIL("R3-L2: the castle does not reach HOLD's spot in this world - the piece case tests nothing"); item = -1; } }
        press(kx, ky, 200);
        progression_sell_fish(&T, 0);                                /* (stands in for any removal while the page is up) */
        if (setup_is_evolve()) TFAIL("R3-L2: the reveal's fish left: the page still up");
        T.hold_active = false; press(kx, ky, 1500);
        if (T.hold_active || setup_active()) TFAIL("R3-L2: the reveal's fish left mid-hold (%s): the rest of the press called the fish %d, opened a page %d - want nothing", v ? "over a piece" : "on the water", T.hold_active, setup_active());
        lift(400);
        if (setup_active() || touch_port_milestones() || s_sel >= 0) TFAIL("R3-L2: the lift of that press acted: setup %d, keeper page %d, card %d", setup_active(), touch_port_milestones(), s_sel);
        if (setup_active()) setup_cancel(&T);
        if (item >= 0) { T.sd_unlocks &= ~SD_ITEMS[item].bit; }
        r2_fish(4); rt_spread();
        T.hold_active = false; press(TANK_W / 2.0f, TANK_H / 2.0f, 900);   /* sibling: a fresh press on the water calls the fish */
        if (!T.hold_active) TFAIL("R3-L2 sibling: a fresh 0.9 s press on the bare water called no fish");
        lift(400); T.hold_active = false;
    }
}
/* R3-L2's siblings (the review of the fix): with a tool in hand the rest of that press wipes / arms no slash; and its lift is
   no tap on a notice that came up as the page went (no close unread, no reveal from a READY card) */
static void rt_gone_mid_more(void) {
    float kx, ky, bx, by;
    for (int tool = TOOL_SPONGE; tool <= TOOL_SCISSORS; tool++) {
        if (!rt_choosing(&kx, &ky, &bx, &by)) return;
        tap(kx, ky, 90, 700);
        T.drag_active = T.drag_has_prev = false; T.drag_dist = 0; T.slash_armed = T.slash_engaged = false;   /* (the harness runs no tank_tick to end old strokes) */
        T.tool = (uint8_t)tool;
        press(kx, ky, 200);
        progression_sell_fish(&T, 0);
        for (int k = 1; k <= 12; k++) press(kx - 14.0f * k, ky - 6.0f * k, FRAME_MS * 2);   /* the finger slides on over the water */
        const float dd = T.drag_dist; const bool armed = T.slash_armed || T.slash_engaged;
        lift(300);
        if (dd > 0 || armed) TFAIL("R3-L2: %s in hand, the reveal's fish left mid-hold: the rest of the press stroked the tank (drag %.0f px, slash armed %d) - want nothing", tool == TOOL_SPONGE ? "the sponge" : "the scissors", dd, armed);
        T.tool = TOOL_HAND; T.drag_active = T.drag_has_prev = false; T.drag_dist = 0; T.slash_armed = T.slash_engaged = false;
        if (setup_active()) setup_cancel(&T);
        r2_fish(4); rt_spread();
    }
    rc_on(true);
    for (int v = 0; v < 2; v++) {                                    /* v 1: the control - a deliberate tap 0.7 s on closes it at once */
        float sx, sy;
        if (!rc_reveal(&sx, &sy, &kx, &ky, &bx, &by)) break;
        tap(sx, sy, 90, 700); tap(kx, ky, 90, 700);                  /* the question */
        notice_low_battery(); hold_for(600);                         /* held under the page */
        press(kx, ky, 80);
        progression_sell_fish(&T, 0);
        press(kx, ky, 40); lift(v ? 700 : 300);
        const notice_t *c = notice_current();
        if (!v && (!c || c->kind != NOTICE_LOW_BATTERY)) TFAIL("R3-L2: the reveal's fish left under a quick press: its lift took %s (want LOW BATTERY still up, unread)", rc_kind(c ? c->kind : -1));
        if (v) { float nx = TANK_W / 2.0f, ny = TANK_H / 2.0f; tap(nx, ny, 90, 300); c = notice_current();
                 if (c && c->kind == NOTICE_LOW_BATTERY) TFAIL("R3-L2 control: a deliberate tap 0.7 s on left LOW BATTERY up"); }
        if (setup_active()) setup_cancel(&T);
        touch_port_dismiss(); rc_clear(); r2_fish(4); rt_spread();
    }
    rc_on(false);
}
static void reveal_touch_port(void) {
    const int f0 = fails;
    r2_fish(4);
    rt_keep();
    rt_away();
    rt_open();
    rt_lab();
    rt_lab_why();
    rt_notices();
    rt_close_notices();
    rt_ready_away();
    rt_shop_rows();
    rt_lab_light();
    rt_sim_card();
    rt_keep_hold();
    rt_doorway();
    rt_gone_mid();
    rt_gone_mid_more();
    rt_reset();
    printf("selftest-touch-port: the reveal and the lab (0.4.3 audit r1 TP-1..6, TP-8, TP-9b, PE-3; r2 R2-4, R2-L11, R2-L13, R2-L14, R2-L16, R2-L32; r3 R3-L1, R3-L2) - %s\n", fails == f0 ? "all hold" : "FAILED");
}
