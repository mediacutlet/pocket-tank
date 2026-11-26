/* bigheld_test.inc - --selftest-bigheld [old] (0.4.3, 11-24): THE REVEAL KEPT AT 2X on the big screens.
 *
 * A keeper evolved a fish on a Tab5 and called the frame rate "pitiful": the big screens drew the paragon's reveal over
 * the tank, at 2x, and main.c's p4_other said "everything" while it was up (setup_zoomed), so the whole glass was turned
 * and the scene put back whole every frame. Now the reveal is a page of its own there too, held at 1x in the zoom's frame
 * and on the glass, and only its moving boxes are put on the glass at 2x and turned (render.c THE REVEAL KEPT AT 2X).
 *
 * This drives a reveal through the tiles test's frame (tl_frame: two render buffers, two DSI buffers, the port's own
 * bookkeeping) - the shells, the spin, CHOOSE ONE, each tile tried on, the reset prompt over it and gone, KEEP THIS,
 * NOT YET, KEEP? with a HOLD part way and a HOLD to the end in the doorway (nothing kept), LATER, the tank back; then a
 * second reveal hurried, kept with a full HOLD, the tank back - and every frame holds the glass against the reveal drawn
 * whole at 2x as it always was (render_setup through a zoom frame of its own). It counts what each frame costs the port
 * (tiles turned + tiles put back, a tile 32 x 32 px) page by page, and holds every held frame to a budget: the tiles
 * of its boxes at 2x (the page's width, from the fish's top to the tile row's foot at most: 28 x 15 = 420 tiles) and a
 * little film (LH_SLACK), never the whole frame's grids. The port's safety frame (a whole turn every P4_FULL_EVERY) is
 * not a held frame's cost and is counted apart.
 * `old`: the same script under the old rule (the reveal over the live tank, whole every frame) for the numbers before.
 * PT_BIGHELD_SHOT=<prefix>: the glass at each named moment as <prefix>_<page>.ppm. */
#ifdef TANK_P4
#define LH_BUDGET 420                             /* (the boxes' tiles at their biggest: CHOOSE ONE's fish box takes in the tile row) */
#define LH_SLACK 40                               /* (film cells that change under the page are turned too: p4_tiles_algae) */
static const char *lh_shot_prefix;
static void lh_shot(const char *page) {
    if (!lh_shot_prefix) return;
    char path[600]; snprintf(path, sizeof path, "%s_%s.ppm", lh_shot_prefix, page);
    write_ppm(path, tl_dsi[tl_on_glass]);
    printf("selftest-bigheld: shot %s\n", path);
}
typedef struct { double ops, us, held_ops; int frames, held, whole, after, safety, worst; } lh_cost_t;
static lh_cost_t lh_ph[SETUP_EVO_PHASES], lh_tank;
static int lh_over;                               /* held frames past the budget */
static bool lh_old, lh_was_whole;
static int lh_frame(const char *what) {
    const unsigned t0 = s_turned, r0 = s_restored, f0 = s_frames;
    const int ph = setup_evo_phase(&tank);
    const bool held = setup_evo_black();
    if (tl_frame(what)) return 1;
    const short *bx; const bool kept = held && !tl_confirm && render_evo_changed(&bx) >= 0;   /* (the port's word: tl_frame's, main.c's) */
    const int ops = (int)(s_turned - t0 + s_restored - r0);
    const bool safety = f0 >= 3 && (f0 % P4_FULL_EVERY) == 0;   /* (p4_tiles_flush: the whole frame, for safety) */
    const bool after = lh_was_whole; lh_was_whole = !kept;
    lh_cost_t *c = ph >= 0 ? &lh_ph[ph] : &lh_tank;
    c->frames++; c->us += tl_draw_us;
    if (safety) { c->safety++; return 0; }
    c->ops += ops;
    if (kept && after) c->after++;     /* (a whole frame reaches both DSI buffers: p4_tiles_flush's s_pend_full - the next one is whole too) */
    else if (kept) {
        c->held++; c->held_ops += ops; if (ops > c->worst) c->worst = ops;
        if (ops > LH_BUDGET + LH_SLACK) { lh_over++; printf("selftest-bigheld: %s: a held frame cost %d tile ops (budget %d)\n", what, ops, LH_BUDGET + LH_SLACK); }
    } else if (ph >= 0) c->whole++;
    return 0;
}
#define LH_RUN(what, n) do { for (int k_ = 0; k_ < (n); k_++) if (lh_frame(what)) return 1; } while (0)
#define LH_GX(x) ((float)((TANK_W - PAGE_W * 2) / 2 + (x) * 2 + 1))   /* (a page point on the glass at 2x: setup.c ZM_*) */
#define LH_GY(y) ((float)((TANK_H - PAGE_H * 2) / 2 + (y) * 2 + 1))
static int lh_tap(int px, int py) { setup_touch(&tank, LH_GX(px), LH_GY(py), true); setup_touch(&tank, LH_GX(px), LH_GY(py), false); return 0; }
#define LH_FAIL(...) do { printf("FAIL: selftest-bigheld: " __VA_ARGS__); printf("\n"); return 1; } while (0)
static int lh_hold(int px, int py, int frames, const char *what, bool door) {   /* a press held through so many frames, the finger polled each one */
    for (int k = 0; k < frames; k++) {
        if (door) tank.fish[0].door = DOOR_ASKED;  /* (in the doorway each poll: the tank's tick moves the door on) */
        setup_touch(&tank, LH_GX(px), LH_GY(py), true); if (lh_frame(what)) return 1;
    }
    return 0;
}
static int selftest_bigheld(bool old) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-bigheld.sav", 1);
    (void)system("rm -f /tmp/pocket-tank-selftest-bigheld.sav");
    lh_shot_prefix = getenv("PT_BIGHELD_SHOT");
    lh_old = old;
    for (int i = 0; i < 2; i++) { tl_fb[i] = calloc(TANK_W * TANK_H, 2); tl_dsi[i] = calloc(TANK_W * TANK_H, 2); }
    tl_ref = calloc(TANK_W * TANK_H, 2); tl_zref = calloc(TANK_W * TANK_H, 2);
    static uint16_t scene[TANK_W * TANK_H];
    render_set_scene_cache(scene); render_set_vignette_cache(vig_buf); render_set_card_cache(card_buf); render_set_modal_cache(give_buf, gift_buf); render_set_dirty_mask(dirty_buf);
    { static uint32_t tmask[RENDER_DIRTY_WORDS]; render_set_touched_mask(tmask); }
    render_big_pocket_pages(false);               /* (the board's own reveal: at 2x) */
    tank_init(&tank, 2024); forms_cast(4);
    progression_boot(&tank); progression_setup_done(&tank);
    for (int i = 0; i < ALGAE_CELLS; i += 7) tank.algae[i] = (uint8_t)(60 + (i * 37) % 180);   /* film on the glass */
    tank.night = false;
    snprintf(tank.fish[0].name, sizeof tank.fish[0].name, "MIRA");
    notice_sync(&tank); (void)progression_sd_take_award();
    LH_RUN("the tank alone", 40);
    fish_t *f = &tank.fish[0];
    be_set(BE_READY); const uint16_t three = f->evo_offer;
    (void)setup_take_renamed();
    setup_begin_evolve(&tank, 0); setup_debug_evo_live(old);   /* (the old rule: the reveal over the live tank, whole every frame - it ends with the page) */
    if (!setup_is_evolve() || !setup_zoomed()) LH_FAIL("the reveal is not up at 2x (up %d, 2x %d)", setup_is_evolve(), setup_zoomed());
    if (setup_evo_black() == old) LH_FAIL("the reveal is %sheld (want %s)", old ? "" : "not ", old ? "the old rule" : "held");
    /* the reveal as it plays: the shells, each spin, CHOOSE ONE */
    const double t0 = tank.clock;
    bool sh_shells = false, sh_spin = false;
    while (tank.clock - t0 < 13.0) {
        const int ph = setup_evo_phase(&tank);
        LH_RUN("the reveal playing", 1);
        if (ph == SETUP_EVO_SHELLS && !sh_shells && tank.clock - t0 > 0.5) { lh_shot("shells"); sh_shells = true; }
        if (ph == SETUP_EVO_SPIN && !sh_spin && tank.clock - t0 > 4.6) { lh_shot("spin"); sh_spin = true; }
    }
    if (setup_evo_phase(&tank) != SETUP_EVO_CHOOSE) LH_FAIL("CHOOSE ONE is not up at 13 s (phase %d)", setup_evo_phase(&tank));
    LH_RUN("CHOOSE ONE", 30); lh_shot("choose");
    /* each tile tried on */
    static const char *const TRY[3] = { "choose_try1", "choose_try2", "choose_try3" };
    for (int k = 0; k < 3; k++) { lh_tap(EVP_TILE_X(k) + EVP_TILE_W / 2, EVP_TILE_Y + EVP_TILE_H / 2); LH_RUN("a tile tried on", 20); lh_shot(TRY[k]); }
    /* the reset prompt over it, and gone */
    tl_confirm = true; tl_confirm_frac = 1; LH_RUN("the reset prompt over the reveal", 12);
    tl_confirm = false; LH_RUN("the reset prompt gone", 12);
    const int KX = EVP_KEEP_X + EVP_KEEP_W / 2, BX = EVP_BACK_X + EVP_BACK_W / 2, BY = EVP_BTN_Y + EVP_BTN_H / 2;
    lh_tap(KX, BY); LH_RUN("KEEP THIS asked", 20);
    if (setup_evo_phase(&tank) != SETUP_EVO_KEEP) LH_FAIL("KEEP THIS did not ask (phase %d)", setup_evo_phase(&tank));
    lh_shot("keep");
    lh_tap(BX, BY); LH_RUN("NOT YET", 20);
    if (setup_evo_phase(&tank) != SETUP_EVO_CHOOSE || f->evo) LH_FAIL("NOT YET: phase %d, evo %d", setup_evo_phase(&tank), f->evo);
    lh_shot("not_yet");
    lh_tap(KX, BY); LH_RUN("KEEP THIS asked again", 20);
    if (lh_hold(KX, BY, 8, "YES held part way", false)) return 1;
    {   const float bar = setup_evo_bar(&tank);
        if (f->evo || bar <= 0.1f || bar >= 1.0f) LH_FAIL("YES held part way: evo %d, the bar %.2f", f->evo, bar); }
    lh_shot("keep_hold_part");
    setup_touch(&tank, LH_GX(KX), LH_GY(BY), false); LH_RUN("YES let go", 20);
    /* in the doorway: the bar runs to its end, nothing kept */
    if (lh_hold(KX, BY, 30, "YES held to the end in the doorway", true)) return 1;
    {   const float bar = setup_evo_bar(&tank);
        if (f->evo || !setup_is_evolve() || bar < 0.9f) LH_FAIL("YES held in the doorway: evo %d, page %d, the bar %.2f", f->evo, setup_is_evolve(), bar); }
    lh_shot("keep_hold_full");
    setup_touch(&tank, LH_GX(KX), LH_GY(BY), false); f->door = DOOR_NONE; LH_RUN("let go in the doorway", 20);
    lh_tap(BX, BY); LH_RUN("NOT YET again", 20);
    lh_shot("later_asked");
    lh_tap(BX, BY); LH_RUN("LATER", 2);
    if (setup_is_evolve() || f->evo) LH_FAIL("LATER: page %d, evo %d", setup_is_evolve(), f->evo);
    LH_RUN("the tank back after LATER", 30);
    /* a second reveal, hurried, kept with a full HOLD */
    f->evo_offer = three; (void)setup_take_renamed();
    setup_begin_evolve(&tank, 0); setup_debug_evo_live(old); LH_RUN("the reveal again", 40);
    lh_tap(EVP_TILE_X(1) + EVP_TILE_W / 2, EVP_TILE_Y + EVP_TILE_H / 2);
    for (int k = 0; k < 13 * 25 && setup_evo_phase(&tank) != SETUP_EVO_CHOOSE; k++) LH_RUN("the spin hurried", 1);
    LH_RUN("CHOOSE ONE again", 20);
    if (setup_evo_phase(&tank) != SETUP_EVO_CHOOSE) LH_FAIL("the hurried spin did not land (phase %d)", setup_evo_phase(&tank));
    lh_tap(EVP_TILE_X(2) + EVP_TILE_W / 2, EVP_TILE_Y + EVP_TILE_H / 2); LH_RUN("a tile tried on", 20);
    lh_tap(KX, BY); LH_RUN("KEEP THIS asked", 20);
    if (lh_hold(KX, BY, 30, "YES held to the end", false)) return 1;
    setup_touch(&tank, LH_GX(KX), LH_GY(BY), false);
    if (setup_active() || f->evo != evo_offer_at(three, 2)) LH_FAIL("YES held: page %d, evo %d (want %d)", setup_active(), f->evo, evo_offer_at(three, 2));
    LH_RUN("the tank back, a paragon", 40);
    (void)setup_take_renamed(); setup_debug_evo_live(false);
    static const char *const PN[SETUP_EVO_PHASES] = { "shells", "spin", "choose", "keep" };
    for (int p = 0; p < SETUP_EVO_PHASES; p++) {
        const lh_cost_t *c = &lh_ph[p]; const int n = c->frames - c->safety;
        printf("selftest-bigheld: %-6s %3d frames: %4.0f tile ops a frame; held %3d at %4.0f (worst %4d); whole %2d, after a whole %2d, safety %d; the draw %.3f ms (host)%s\n",
               PN[p], c->frames, n ? c->ops / n : 0, c->held, c->held ? c->held_ops / c->held : 0, c->worst, c->whole, c->after, c->safety,
               c->frames ? c->us / c->frames / 1000 : 0, old ? " (the OLD rule)" : "");
    }
    printf("selftest-bigheld: the tank alone %3d frames: %6.0f tile ops a frame; the whole frame's grids: %d\n", lh_tank.frames,
           lh_tank.frames - lh_tank.safety ? lh_tank.ops / (lh_tank.frames - lh_tank.safety) : 0, 2 * RENDER_TILES_X * RENDER_TILES_Y);
    if (!old) {
        if (lh_over) LH_FAIL("%d held frames past the budget", lh_over);
        int held = 0, whole = 0, frames = 0;
        for (int p = 0; p < SETUP_EVO_PHASES; p++) { held += lh_ph[p].held; whole += lh_ph[p].whole + lh_ph[p].after; frames += lh_ph[p].frames - lh_ph[p].safety; }
        /* each change of the still part (a shell opening, a tile tried on, KEEP asked and answered, the bar's page, the
           prompt) draws both buffers whole once; everything else is held */
        if (whole * 6 > frames) LH_FAIL("%d of %d reveal frames whole (want the boxes alone nearly always)", whole, frames);
        if (!held) LH_FAIL("no frame held");
    }
    printf("selftest-bigheld: OK (%s: every frame's glass the reveal drawn whole at 2x)\n", old ? "the old rule" : "held");
    return 0;
}
#endif
