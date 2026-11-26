/* linkpage_test.inc - --selftest-linkpage [old] (0.4.3, 11-24): LINK's page on the big screens' kept-frame path.
 *
 * On the 10.1 link mode's page ran at 9.5 fps (23-25 without it): main.c's p4_other said "everything" while
 * link_dev_page_up(), so every frame was turned whole and the scene put back whole under it. The page and its cards
 * are drawn with the signed primitives (render.c THE OVERLAY TILES), so now they go as overlay tiles like the stats
 * card or an announcement. This drives the page through what it shows - the arrows held, a door asked and opened,
 * DONE breathing, the coin turning, a gift's news, fish and a visitor swimming under it, the DRIFTED notice, the sand
 * dollar dial over it, the doors once it closes - and its four cards (STARTING THE LINK, THE LINK DID NOT START,
 * UPDATING THE LINK with its bar moving, THE LINK UPDATE STOPPED), then a soak. Every frame the glass is held against
 * a whole render of the same frame (the tiles test's tl_frame), on the new path. And it counts what each frame costs
 * the port: tiles turned into the DSI buffer plus tiles of scene put back, a tile being 32 x 32 px. A steady page must
 * cost about what the tank alone costs plus its own footprint, never the whole frame's two grids.
 * `old`: the same script under the old rule (everything while the page or a card is up), for the numbers before.
 * PT_LINKPAGE_SHOT=<prefix>: the glass at each named moment as <prefix>_<state>.ppm. */
#ifdef TANK_P4
static const char *lp_shot_prefix;
static void lp_shot(const char *state) {
    if (!lp_shot_prefix) return;
    char path[600]; snprintf(path, sizeof path, "%s_%s.ppm", lp_shot_prefix, state);
    write_ppm(path, tl_dsi[tl_on_glass]);
    printf("selftest-linkpage: shot %s\n", path);
}
/* every frame's glass, folded (PT_LINKPAGE_HASH=1 prints it): two builds that draw alike print the same */
static uint64_t lp_glass_hash = 1469598103934665603ull;
static void lp_hash(void) { const uint16_t *g = tl_dsi[tl_on_glass]; for (int i = 0; i < TANK_W * TANK_H; i++) lp_glass_hash = (lp_glass_hash ^ g[i]) * 1099511628211ull; }
/* the port's cost of a run of frames: tiles turned and tiles put back, per frame */
typedef struct { double turned, restored, ns_tank, ns_link; int frames; } lp_cost_t;
static int lp_run(const char *what, int n, lp_cost_t *cost) {
    for (int k = 0; k < n; k++) {
        const unsigned t0 = s_turned, r0 = s_restored; const double nt = tl_ns_tank, nl = tl_ns_link;
        if (tl_frame(what)) return 1;
        lp_hash();
        if (cost) { cost->turned += s_turned - t0; cost->restored += s_restored - r0; cost->ns_tank += tl_ns_tank - nt; cost->ns_link += tl_ns_link - nl; cost->frames++; }
    }
    return 0;
}
#define LP_RUN(what, n, cost) do { if (lp_run(what, n, cost)) return 1; } while (0)
static double lp_per(const lp_cost_t *c) { return c->frames ? (c->turned + c->restored) / c->frames : 0; }
static int lp_footprint(void) {                   /* the tiles drawn over the tank in the last frame */
    const render_overlay_t *o = render_overlay(); int n = 0;
    for (int ty = 0; ty < RENDER_TILES_Y; ty++) for (int tx = 0; tx < RENDER_TILES_X; tx++) n += o->drawn[ty][tx] != 0;
    return n;
}
static void lp_fish_at(int i, float x, float y, int dir) {   /* a fish carried along, as --selftest-dep-room carries one */
    fish_t *f = &tank.fish[i];
    f->x = x; f->y = y; f->speed = 0; f->target_speed = 0; f->facing = (int8_t)dir; f->heading = dir > 0 ? 0 : 3.14159f; f->yaw = (float)dir;
}
static int selftest_linkpage(bool old) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-linkpage.sav", 1);
    (void)system("rm -f /tmp/pocket-tank-selftest-linkpage.sav");
    lp_shot_prefix = getenv("PT_LINKPAGE_SHOT");
    tl_link_old = old;
    for (int i = 0; i < 2; i++) { tl_fb[i] = calloc(TANK_W * TANK_H, 2); tl_dsi[i] = calloc(TANK_W * TANK_H, 2); }
    tl_ref = calloc(TANK_W * TANK_H, 2);
    static uint16_t scene[TANK_W * TANK_H];
    render_set_scene_cache(scene); render_set_vignette_cache(vig_buf); render_set_card_cache(card_buf); render_set_dirty_mask(dirty_buf);
    { static uint32_t tmask[RENDER_DIRTY_WORDS]; render_set_touched_mask(tmask); }
    render_big_pocket_pages(false);               /* (the board's own cards) */
    tank_init(&tank, 77);
    progression_boot(&tank);
    progression_setup_done(&tank);
    progression_sd_grant(&tank, 5000);
    for (int item = 0; item < SD_ITEM_COUNT; item++) progression_buy(&tank, item);
    tank_decor_set(&tank, 2, 900, DECOR_Z_FRONT); tank_decor_set(&tank, 3, 520, DECOR_Z_BACK); tank_decor_set(&tank, 4, 300, DECOR_Z_FRONT);
    for (int i = 0; i < ALGAE_CELLS; i += 7) tank.algae[i] = (uint8_t)(60 + (i * 37) % 180);
    tank.bubble_x = 250;
    notice_sync(&tank);
    (void)progression_sd_take_award();
    lp_cost_t alone = {0}, steady = {0}, pulsing = {0}, card = {0}, bar = {0};
    LP_RUN("the tank alone, settling", 60, NULL);
    LP_RUN("the tank alone", 100, &alone);
    /* link mode's page up: nobody near, then a tank heard */
    tl_link = true; tl_lpage = true; memset(&tl_lv, 0, sizeof tl_lv); tl_lv.badge = 1; render_link_leave();
    LP_RUN("LINK's page up, nobody near", 30, NULL); lp_shot("page_nobody_near");
    tl_lv.n_near = 1; tl_lv.near_badge[0] = 2;
    LP_RUN("LINK's page, a tank nearby", 20, NULL);
    LP_RUN("LINK's page standing still", 100, &steady);
    const int foot = lp_footprint();
    lp_shot("page_nearby");
    /* the left arrow held: its tab fills from the bottom, then it asks */
    for (int k = 0; k <= 40; k++) { render_link_set_holds(k / 40.0f, 0); LP_RUN("the left arrow held", 1, NULL); if (k == 20) lp_shot("arrow_held"); }
    render_link_set_holds(0, 0); tl_lv.edge[LINK_LEFT].state = LV_ARMED;
    LP_RUN("ours asking (the chevron pulsing)", 30, NULL); lp_shot("asking");
    tl_lv.edge[LINK_LEFT].state = LV_FREE;
    tl_lv.n_near = 2; tl_lv.near_badge[1] = 3;
    tl_lv.edge[LINK_RIGHT].state = LV_ASKED; tl_lv.edge[LINK_RIGHT].badge = 2;
    LP_RUN("a tank asking at the right", 30, NULL); lp_shot("tank_asking");
    for (int k = 0; k <= 30; k++) { render_link_set_holds(0, k / 30.0f); LP_RUN("the asked arrow held", 1, NULL); }
    render_link_set_holds(0, 0);
    tl_lv.edge[LINK_RIGHT].state = LV_TWO; tl_lv.edge[LINK_RIGHT].badge2 = 3;
    LP_RUN("two tanks asking", 15, NULL); lp_shot("two_asking");
    /* the door opens: the neighbor's badge, DONE breathing with its arrow bobbing, DISCONNECT */
    tl_lv.edge[LINK_RIGHT].state = LV_OPEN; tl_lv.edge[LINK_RIGHT].badge = 2;
    LP_RUN("the door opening", 20, NULL); lp_shot("door_open");
    LP_RUN("the door open, DONE breathing", 100, &pulsing);
    /* SAND DOLLAR GIFTS: the coin, it turning, the news */
    tl_lv.edge[LINK_RIGHT].sd = 1;
    LP_RUN("the coin under the door", 20, NULL);
    render_link_sd_news(TL_SD_SENT, LINK_RIGHT, 25, 0, tank.clock);
    LP_RUN("the coin turning, SENT", 25, NULL); lp_shot("coin_turning");
    LP_RUN("the coin at rest again", 30, NULL);
    render_link_sd_news(TL_SD_BUSY, LINK_RIGHT, 0, 0, tank.clock);
    LP_RUN("STILL SENDING THE LAST ONE", 20, NULL);
    /* fish swimming under the page: through the tabs, the badge, the hint lines, NEARBY */
    { const float my = TANK_BOT * 0.5f, ly = TANK_BOT * 0.82f, ty = 40;
      for (int k = 0; k < 90; k++) {
          const float u = k / 89.0f;
          lp_fish_at(0, 30 + u * (TANK_W - 60), my, 1);
          lp_fish_at(1, TANK_W - 30 - u * (TANK_W - 60), ly, -1);
          if (tank.n_fish > 2) lp_fish_at(2, TANK_W * 0.3f + u * TANK_W * 0.4f, ty + 30, 1);
          LP_RUN("fish swimming under the page", 1, NULL);
          if (k == 10) lp_shot("fish_under_tab");
          if (k == 45) lp_shot("fish_under_page");
      } }
    LP_RUN("the fish free again", 20, NULL);
    /* a visitor arriving through the open door, then going home */
    {   fish_t *v = &tank.fish[N_FISH_MAX]; *v = tank.fish[0];
        v->color = 0xffd166; v->accent = 0x4da3ff;
        tank.vis_used[0] = true; tank.vis_badge[0] = 2;
        for (int k = 0; k < 40; k++) { lp_fish_at(N_FISH_MAX, TANK_W - 20 - k * 12.0f, TANK_BOT * 0.5f, -1); LP_RUN("a visitor arriving", 1, NULL); if (k == 8) lp_shot("visitor_arriving"); }
        LP_RUN("a visitor swimming", 30, NULL); }
    progression_sd_grant(&tank, 3);
    LP_RUN("the coin toast under the page", 30, NULL);
    /* the left door opened too, then THE TANKS DRIFTED over link mode */
    tl_lv.edge[LINK_LEFT].state = LV_OPEN; tl_lv.edge[LINK_LEFT].badge = 0;
    LP_RUN("both doors open", 15, NULL);
    tl_lv.drifting = true; tl_lv.drift_ms_left = 30000;
    for (int k = 0; k < 30; k++) { tl_lv.drift_ms_left -= 400; LP_RUN("DRIFTED over the page, its bar running", 1, NULL); if (k == 15) lp_shot("drifted_over_page"); }
    tl_lv.drift_homeward = true; tl_lv.drift_ms_left = 20000; LP_RUN("DRIFTED, the fish swimming home", 15, NULL);
    tl_lv.drifting = tl_lv.drift_homeward = false; LP_RUN("steady again", 15, NULL);
    /* the coin's dial over link mode (the setup flow's page: the link page is not drawn under it) */
    setup_begin_sdgift(&tank, LINK_RIGHT, 2);
    LP_RUN("the sand dollar dial", 20, NULL); TL_UP(setup_active() && setup_is_sdgift(), "the sand dollar dial"); lp_shot("sd_dial");
    setup_touch(&tank, TANK_W / 2.0f, TANK_H / 2.0f, true); LP_RUN("the dial, a finger down", 4, NULL);
    setup_touch(&tank, TANK_W / 2.0f, TANK_H / 2.0f + 40, true); LP_RUN("the dial, the finger dragged", 4, NULL);
    setup_touch(&tank, TANK_W / 2.0f, TANK_H / 2.0f + 40, false); LP_RUN("the dial, the finger up", 4, NULL);
    setup_cancel(&tank); LP_RUN("the dial gone, the page back", 15, NULL);
    /* the page closed: the doors' glow; the visitor home; the page up again */
    tl_lpage = false; render_link_leave(); LP_RUN("the page closed, the doors glowing", 25, NULL); lp_shot("doors_glow");
    tank.vis_used[0] = false; LP_RUN("the visitor gone home", 10, NULL);
    tl_lpage = true; LP_RUN("the page up again", 15, NULL);
    /* the cards: link mode's page opens as one (the radio's start) and becomes the page, or another card */
    tl_lpage = false; tl_link = false;
    tl_lcard = 1; LP_RUN("STARTING THE LINK", 30, &card); lp_shot("card_starting");
    tl_lcard = 0; tl_link = true; tl_lpage = true; LP_RUN("the start answered: the page", 15, NULL);
    tl_lpage = false; tl_link = false;
    tl_lcard = 2; LP_RUN("THE LINK DID NOT START", 30, NULL); lp_shot("card_did_not_start");
    tl_lcard = 0; LP_RUN("the card closed", 10, NULL);
    tl_lcard = 1; LP_RUN("STARTING THE LINK again", 10, NULL);
    for (int k = 0; k <= 100; k++) { tl_lcard = 3; tl_lpct = k; LP_RUN("UPDATING THE LINK, the bar moving", 1, &bar); if (k == 40) lp_shot("card_updating"); }
    tl_lcard = 4; LP_RUN("THE LINK UPDATE STOPPED", 30, NULL); lp_shot("card_update_stopped");
    tl_lcard = 0; LP_RUN("the card closed", 10, NULL);
    /* what else can be on the glass with the page (the blind review, 11-24): an announcement already up when the page
       opens (held, still drawn), a fish's stats card and a tool's chip under the page */
    notice_low_battery(); LP_RUN("an announcement coming up", 20, NULL); TL_UP(notice_current(), "the announcement");
    tl_link = true; tl_lpage = true; LP_RUN("the page opened under an announcement", 30, NULL); lp_shot("page_under_notice");
    (void)notice_dismiss(); LP_RUN("the announcement gone", 10, NULL);
    tl_sel = 0; LP_RUN("a stats card with the page", 20, NULL);
    tank_set_tool(&tank, TOOL_SPONGE); LP_RUN("a tool's chip with the page", 15, NULL);
    tl_sel = -1; tank_set_tool(&tank, TOOL_HAND); LP_RUN("the card and the tool gone", 10, NULL);
    /* the soak: the page's states at random, fish carried through it, cards, the dial, the toast */
    uint32_t rng = 20261124u;
    tl_link = true; tl_lpage = true;
    for (int f = 0; f < 1500; f++) {
        rng = rng * 1664525u + 1013904223u; unsigned r = (rng >> 8) % 1000, q = (rng >> 20) & 15;
        if (r < 40) { int e = q & 1; tl_lv.edge[e].state = (uint8_t)((rng >> 4) % 5); tl_lv.edge[e].badge = (uint8_t)(q >> 1); tl_lv.edge[e].badge2 = (uint8_t)(q >> 2); tl_lv.edge[e].sd = (uint8_t)((rng >> 12) & 1); }
        else if (r < 55) { float h = ((rng >> 6) % 101) / 100.0f; if (q & 1) render_link_set_holds(h, 0); else render_link_set_holds(0, h); }
        else if (r < 60) render_link_set_holds(0, 0);
        else if (r < 70) { tl_lv.n_near = (uint8_t)(q % 4); for (int i = 0; i < 4; i++) tl_lv.near_badge[i] = (uint8_t)((q + i) % LINK_BADGES); }
        else if (r < 76) render_link_coin_spin(q & 1, tank.clock);
        else if (r < 82) render_link_sd_news(q < 8 ? TL_SD_SENT : TL_SD_GOT, q & 1, 5 + (int)q, 0, tank.clock);
        else if (r < 88) { tl_lv.drifting = !tl_lv.drifting; tl_lv.drift_ms_left = 30000; tl_lv.drift_homeward = q < 4; }
        else if (r < 92) { if (tl_lcard) tl_lcard = 0; else { tl_lcard = 1 + (int)(q % 4); tl_lpct = (int)(q * 7); } }
        else if (r < 95) { tl_lpage = !tl_lpage; render_link_leave(); }
        else if (r < 98) { if (setup_active()) setup_cancel(&tank); else if (q < 4) setup_begin_sdgift(&tank, q & 1, 2); }
        else if (r < 104) progression_sd_grant(&tank, 1 + (int)q);
        else if (r < 110) tank.vis_used[0] = !tank.vis_used[0];
        else if (r < 115) tl_sel = q < 4 ? (int)(q & 1) : -1;
        else if (r < 118) notice_low_battery();
        else if (r < 121) (void)notice_dismiss();
        else if (r < 160 && tl_lcard == 3) tl_lpct = (tl_lpct + 3) % 101;
        if (tl_lv.drifting && tl_lv.drift_ms_left > 40) tl_lv.drift_ms_left -= 40;
        if (r >= 500 && r < 520) lp_fish_at((int)(q % 2), (float)(20 + (rng >> 3) % (TANK_W - 40)), (float)(20 + (rng >> 13) % (TANK_BOT - 40)), q & 1 ? 1 : -1);
        if (!tl_frame("the link page's soak")) lp_hash(); else { printf("selftest-linkpage: (soak frame %d: page %d card %d dial %d drift %d edges %d/%d)\n", f, (int)tl_lpage, tl_lcard,
                                                      (int)setup_active(), (int)tl_lv.drifting, tl_lv.edge[0].state, tl_lv.edge[1].state); return 1; }
    }
    if (setup_active()) setup_cancel(&tank);
    tl_sel = -1; tl_lcard = 0; tl_lpage = false; tl_link = false; LP_RUN("the link off", 10, NULL);
    /* what a frame costs the port (tiles of 32 x 32 px: turned into the DSI buffer + put back from the scene) */
    const double whole = 2.0 * RENDER_TILES_X * RENDER_TILES_Y;
    printf("selftest-linkpage: %s, %d x %d tiles; per frame, turned + put back (the whole frame: %.0f):\n", old ? "the OLD rule" : "the kept path", RENDER_TILES_X, RENDER_TILES_Y, whole);
    printf("selftest-linkpage:   the tank alone          %7.1f (turned %.1f, put back %.1f)\n", lp_per(&alone), alone.turned / alone.frames, alone.restored / alone.frames);
    printf("selftest-linkpage:   the page standing still %7.1f (turned %.1f, put back %.1f; the page drew on %d tiles)\n", lp_per(&steady), steady.turned / steady.frames, steady.restored / steady.frames, foot);
    printf("selftest-linkpage:   the door open, pulsing  %7.1f (turned %.1f, put back %.1f)\n", lp_per(&pulsing), pulsing.turned / pulsing.frames, pulsing.restored / pulsing.frames);
    printf("selftest-linkpage:   STARTING THE LINK       %7.1f (turned %.1f, put back %.1f)\n", lp_per(&card), card.turned / card.frames, card.restored / card.frames);
    printf("selftest-linkpage:   UPDATING, the bar moving%7.1f (turned %.1f, put back %.1f)\n", lp_per(&bar), bar.turned / bar.frames, bar.restored / bar.frames);
    {   /* the CPU (this Mac, -O2): the tank's render and LINK's drawing, a frame (a ratio to read against the board's render ms) */
        const lp_cost_t *C[5] = { &alone, &steady, &pulsing, &card, &bar }; static const char *const N[5] = { "the tank alone", "the page standing still", "the door open, pulsing", "STARTING THE LINK", "UPDATING, the bar moving" };
        for (int i = 0; i < 5; i++) printf("selftest-linkpage:   CPU %-24s tank %6.0f us, LINK %6.0f us\n", N[i], C[i]->ns_tank / C[i]->frames / 1e3, C[i]->ns_link / C[i]->frames / 1e3);
    }
    if (!old) {
        /* THE BUDGET: a steady page is the tank's own changes plus its footprint put back under it every frame (its
           blends need their ground) and turned only where it moves - so at most the tank alone + twice the footprint.
           And the footprint must stay small: a page that grew to cover the glass would be a page, not an overlay */
        int bad = 0;
        if (foot > RENDER_TILES_X * RENDER_TILES_Y / 8) { printf("FAIL: selftest-linkpage: the page drew on %d of %d tiles\n", foot, RENDER_TILES_X * RENDER_TILES_Y); bad++; }
        if (lp_per(&steady) > lp_per(&alone) + 2.0 * foot) { printf("FAIL: selftest-linkpage: a steady page costs %.1f tiles a frame, the tank alone %.1f + 2 x %d\n", lp_per(&steady), lp_per(&alone), foot); bad++; }
        if (lp_per(&pulsing) > lp_per(&alone) + 2.0 * foot + 24) { printf("FAIL: selftest-linkpage: the open door's page costs %.1f tiles a frame\n", lp_per(&pulsing)); bad++; }
        if (lp_per(&steady) > whole / 4 || lp_per(&pulsing) > whole / 4) { printf("FAIL: selftest-linkpage: the page costs more than a quarter of the whole frame\n"); bad++; }
        /* a card dims the whole glass, so the scene goes back under every tile each frame (half the whole frame's
           cost) - what it saves is the turn: only the tiles the tank or the bar changed. It must never cost the whole */
        if (lp_per(&card) > whole * 0.9 || lp_per(&bar) > whole * 0.9) { printf("FAIL: selftest-linkpage: a card costs about the whole frame (%.1f, %.1f)\n", lp_per(&card), lp_per(&bar)); bad++; }
        if (bad) return 1;
    }
    if (getenv("PT_LINKPAGE_HASH")) printf("selftest-linkpage: every frame's glass folded: %016llx\n", (unsigned long long)lp_glass_hash);
    printf("selftest-linkpage: %s - %d frames, the glass the frame's own pixel for pixel through LINK's page, its cards and the dial\n", old ? "OK (the old rule)" : "PASS", tl_frames);
    return 0;
}
#endif
