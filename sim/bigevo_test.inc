/* bigevo_test.inc - THE PARAGON ON THE BIG SCREENS (0.4.3, found 11-24 on a Tab5). #included into sim/main.c.
 *
 * --selftest-bigevo: the big KEEPER page's fish pane shows the paragon's three states with the pocket card's words:
 *   SOMETHING IS HAPPENING (its last hour before), ELDER and a big EVOLVE in the pane's top right (its three wait),
 *   PARAGON - <part> (evolved); a plain ELDER otherwise; the list reads PARAGON for an evolved fish. EVOLVE: inside the pane over the
 *   name, at least RENAME's height, bright as a plate; its middle, corners and a finger's reach past its edges answer
 *   MS_TAP_EVOLVE + the fish, nothing else in the pane does, nor its place when the fish is not ready, is next door, or
 *   is a visitor. Its frames are kept; each change of state draws the page again: a kept frame is the whole one.
 * --selftest-bigreveal: the reveal the EVOLVE opens is drawn at 2x through the big screens' frame (setup.c, setup_zoomed),
 *   so its taps come off the glass at 2x. --selftest-evo-kept is the pocket tanks' (their held page) and the touch port's
 *   reveal cases (reveal_touch_test.inc) ride the pocket panel: neither runs here. This one taps the 2x page where it is
 *   drawn: the spin hurried, a tile tried on, KEEP THIS asked, NOT YET, a double tap on KEEP THIS (nothing kept), YES held
 *   part way (nothing kept, the bar part full), held in the doorway (nothing kept, the bar short of its end), YES held
 *   (kept).
 * --bigevo-shot <prefix>: the KEEPER page with a fish in each state, <prefix>_keeper_<state>.ppm, and the sheet for
 *   approval, <prefix>_keeper_final.ppm (EVOLVE, SOMETHING IS HAPPENING, PARAGON - <part>, the list with a paragon) */
#ifdef TANK_P4
#define BE_PANE_X0 776                     /* (render_big.c: WD_X, WD_W, WC_Y and the pane's lines) */
#define BE_PANE_X1 (776 + 472)
#define BE_NAME_Y1 (32 + 136 + 28)         /* the name's foot (scale 4) */
#define BE_STRIP_Y0 (32 + 208 - 8)         /* the growth strip's top (its biggest glyph) */
#define BE_BTN_Y0  (32 + 250)              /* RENAME / SELL */
static const uint16_t BE_GOLD = (uint16_t)(((0xffd166 >> 19) & 0x1f) << 11 | ((0xffd166 >> 10) & 0x3f) << 5 | ((0xffd166 >> 3) & 0x1f));
static uint16_t be_fa[TANK_W * TANK_H], be_fb[TANK_W * TANK_H], be_scene[TANK_W * TANK_H];
static uint16_t be_three(void) { return (uint16_t)(EVO_LURE | EVO_WHISKERS << 5 | EVO_BANNER << 10); }
static void be_stage(void) {
    tank_init(&tank, 2024); forms_cast(4);
    render_set_scene_cache(be_scene); render_set_vignette_cache(vig_buf); render_set_card_cache(card_buf); render_set_modal_cache(give_buf, gift_buf); render_set_dirty_mask(dirty_buf);
    render_big_pocket_pages(false);
    for (int i = 0; i < 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
    tank.night = false;
    snprintf(tank.fish[0].name, sizeof tank.fish[0].name, "MIRA");
}
enum { BE_PLAIN, BE_SPARKLE, BE_READY, BE_PARAGON, BE_STATES };
static const char *const BE_NAME[BE_STATES] = { "elder", "something_is_happening", "elder_evolve", "paragon" };
static void be_set(int s) {                 /* fish 0 in state s */
    fish_t *f = &tank.fish[0];
    f->stage = STAGE_ELDER; f->away = false; f->evo = 0; f->evo_offer = 0;
    progression_set_age(&tank, 0, s == BE_PLAIN ? EVO_AGE - EVO_SPARKLE_S - 600 : EVO_AGE - 600);
    if (s == BE_READY) f->evo_offer = be_three();
    if (s == BE_PARAGON) f->evo = EVO_BANNER;
}
static void be_page(uint16_t *fb, int slot) { render_frame_begin(); render_milestones(&tank, fb, TANK_W); render_big_select_fish(slot); render_frame_begin(); render_milestones(&tank, fb, TANK_W); }
static int be_gold_in(const uint16_t *fb, int y0, int y1) {   /* gold pixels in the pane between rows y0..y1 */
    int n = 0;
    for (int y = y0; y <= y1; y++) for (int x = BE_PANE_X0; x < BE_PANE_X1; x++) n += FB_SWAP16(fb[y * TANK_W + x]) == BE_GOLD;
    return n;
}
#endif
static int selftest_bigevo(void) {
#ifdef TANK_P4
    #define BF(...) do { printf("FAIL: selftest-bigevo: " __VA_ARGS__); printf("\n"); bad++; } while (0)
    int bad = 0;
    be_stage();
    char want[40];
    static const char *const WORD[BE_STATES] = { "ELDER", "SOMETHING IS HAPPENING", "ELDER", NULL };
    int ex = 0, ey = 0, ew = 0, eh = 0;
    for (int s = 0; s < BE_STATES; s++) {
        render_milestones_leave(); be_set(s); be_page(be_fa, 0);
        const char *line = render_big_stage_line(&tank);
        if (WORD[s]) snprintf(want, sizeof want, "%s", WORD[s]); else snprintf(want, sizeof want, "PARAGON - %s", tank_evo_name(EVO_BANNER));
        if (!line || strcmp(line, want)) BF("%s: the stage line is \"%s\", want \"%s\"", BE_NAME[s], line ? line : "(none)", want);
        int x, y, w, h; const bool box = render_big_evolve_box(&tank, &x, &y, &w, &h);
        if (box != (s == BE_READY)) BF("%s: EVOLVE %s", BE_NAME[s], box ? "shown" : "missing");
        const int gold = be_gold_in(be_fa, BE_NAME_Y1, BE_STRIP_Y0);
        if (gold) BF("%s: %d gold px on the stage line (it is plain words now)", BE_NAME[s], gold);
        if (box) { ex = x; ey = y; ew = w; eh = h; }
        if (line && render_text_w(line, 3) > 472 - 48 && render_text_w(line, 2) > 472 - 48) BF("%s: \"%s\" is wider than the pane", BE_NAME[s], line);
        printf("selftest-bigevo: %s: \"%s\"%s\n", BE_NAME[s], line ? line : "", box ? ", EVOLVE" : "");
    }
    if (!ew) { BF("no EVOLVE box to tap"); return bad; }
    /* READY: EVOLVE in the pane's top right - inside the pane, over the name, as tall as RENAME at least; its middle,
       corners and a finger's reach past its edges answer EVOLVE; nothing else above the buttons does */
    do {
        render_milestones_leave(); be_set(BE_READY); be_page(be_fa, 0);
        const char L = 'E';                                   /* (EVOLVE, in the messages) */
        int x, y, w, h;
        if (!render_big_evolve_box(&tank, &x, &y, &w, &h)) { BF("%c: no EVOLVE", L); break; }
        if (x < BE_PANE_X0 + 16 || x + w > BE_PANE_X1 - 16 || y < 32 + 8 || y + h > 32 + 136) BF("%c: EVOLVE %d,%d %dx%d is not in the pane's top right, over the name", L, x, y, w, h);
        if (h < 48 || w < 140) BF("%c: EVOLVE is %dx%d (want at least RENAME's 48 tall)", L, w, h);
        if (FB_SWAP16(be_fa[(y + h / 2) * TANK_W + x + 2]) != BE_GOLD || FB_SWAP16(be_fa[(y + 2) * TANK_W + x + w / 2]) != BE_GOLD) BF("%c: EVOLVE's gold is not where its box says", L);
        {   int lum = 0, n = 0;                                   /* (not the hue alone: a plate that stands out by brightness) */
            for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++) { const uint16_t p = FB_SWAP16(be_fa[yy * TANK_W + xx]); lum += (p >> 11) * 2 + ((p >> 5) & 63) + (p & 31); n++; }
            if (lum / n < 30) BF("%c: EVOLVE's plate is dark on the whole (%d)", L, lum / n); }
        const struct { int px, py; const char *what; } ON[] = {
            { x + w / 2, y + h / 2, "middle" }, { x, y, "top left" }, { x + w - 1, y, "top right" }, { x, y + h - 1, "bottom left" },
            { x + w - 1, y + h - 1, "bottom right" }, { x - 10, y + h / 2, "10 px left" }, { x + w / 2, y - 10, "10 px over" }, { x + w / 2, y + h + 10, "10 px under" },
            { BE_PANE_X1 - 2, y + h / 2, "the pane's edge" } };
        for (int i = 0; i < (int)(sizeof ON / sizeof ON[0]); i++) {
            const int r = render_milestones_tap(&tank, (float)ON[i].px, (float)ON[i].py);
            if (r != MS_TAP_EVOLVE + 0) BF("%c: EVOLVE's %s (%d,%d) answers %d, want %d", L, ON[i].what, ON[i].px, ON[i].py, r, MS_TAP_EVOLVE);
        }
        int stray = 0, fx = -1, fy = -1;
        for (int py = 32; py < BE_BTN_Y0 - 12; py += 3) for (int px = BE_PANE_X0; px < BE_PANE_X1; px += 3) {
            const bool near = px >= x - 12 && py >= y - 12 && py < y + h + 12;
            if (near) continue;
            if (render_milestones_tap(&tank, (float)px, (float)py) >= MS_TAP_EVOLVE) { if (!stray) { fx = px; fy = py; } stray++; }
        }
        if (stray) BF("%c: %d places off EVOLVE answer EVOLVE (first %d,%d)", L, stray, fx, fy);
        for (int py = BE_BTN_Y0 + 60; py < 32 + 600; py += 7) for (int px = BE_PANE_X0; px < BE_PANE_X1; px += 7)   /* (the tabs, the list under them) */
            if (render_milestones_tap(&tank, (float)px, (float)py) >= MS_TAP_EVOLVE) { BF("%c: %d,%d under the buttons answers EVOLVE", L, px, py); py = 9999; break; }
        render_big_tab(0);
        if (render_milestones_tap(&tank, (float)(x + w / 2), (float)(BE_BTN_Y0 + 24)) >= MS_TAP_EVOLVE) BF("%c: the buttons' row answers EVOLVE", L);   /* (SELL armed: the next leave disarms it) */
        /* its frames are kept (its stars twinkle in their own box), and a kept frame is the whole one */
        const uint8_t sh = tank.fish[0].shape, fi = tank.fish[0].fins;
        tank.fish[0].shape = SHAPE_BULBOUS; tank.fish[0].fins = FINS_CLASSIC;   /* (a fish in its list cell: a classic elder's tail leaves it, 11-24 - its frames would all be whole; its full size here: the portrait's box beside EVOLVE holds it) */
        render_milestones_leave(); be_page(be_fa, 0);
        extern void render_big_keeper_counts(unsigned *kept, unsigned *whole);
        unsigned k0, k1, whole; render_big_keeper_counts(&k0, &whole);
        for (int k = 0; k < 12; k++) { tank.clock += 0.13; render_frame_begin(); render_milestones(&tank, be_fa, TANK_W); }
        render_big_keeper_counts(&k1, &whole);
        if (k1 - k0 < 11) BF("%c: %u of 12 frames kept", L, k1 - k0);
        render_debug_page_whole(true); render_milestones(&tank, be_fb, TANK_W); render_debug_page_whole(false);
        if (memcmp(be_fa, be_fb, sizeof be_fa)) BF("%c: a kept frame is not the whole one", L);
        tank.fish[0].shape = sh; tank.fish[0].fins = fi;
        printf("selftest-bigevo: %c: EVOLVE %d,%d %dx%d, its taps and kept frames checked\n", L, x, y, w, h);
    } while (0);
    /* the list: an evolved fish reads PARAGON under its name, the others their stage; a fish that evolves while another
       fish's pane is up draws the list again (a kept frame is the whole one) */
    {   render_milestones_leave(); be_set(BE_READY);
        const uint8_t sh = tank.fish[0].shape, fi = tank.fish[0].fins;
        tank.fish[0].shape = SHAPE_BULBOUS; tank.fish[0].fins = FINS_CLASSIC;
        be_page(be_fa, 1);
        if (!render_big_list_word(&tank, 0) || strcmp(render_big_list_word(&tank, 0), "ELDER")) BF("the list: a ready fish reads \"%s\", want ELDER", render_big_list_word(&tank, 0));
        extern void render_big_keeper_counts(unsigned *kept, unsigned *whole);
        unsigned k0, k1, whole; render_big_keeper_counts(&k0, &whole);
        for (int k = 0; k < 4; k++) { tank.clock += 0.04; render_frame_begin(); render_milestones(&tank, be_fa, TANK_W); }
        render_big_keeper_counts(&k1, &whole);
        if (k1 - k0 < 3) BF("the list: only %u of 4 frames kept (the test sees no kept frame)", k1 - k0);
        tank.fish[0].evo = EVO_BANNER; tank.fish[0].evo_offer = 0;
        render_frame_begin(); render_milestones(&tank, be_fa, TANK_W);
        render_debug_page_whole(true); render_milestones(&tank, be_fb, TANK_W); render_debug_page_whole(false);
        if (memcmp(be_fa, be_fb, sizeof be_fa)) BF("the list: fish 0 evolved under fish 1's pane - the kept frame is not the whole one");
        if (!render_big_list_word(&tank, 0) || strcmp(render_big_list_word(&tank, 0), "PARAGON")) BF("the list: an evolved fish reads \"%s\", want PARAGON", render_big_list_word(&tank, 0));
        if (!render_big_list_word(&tank, 1) || strcmp(render_big_list_word(&tank, 1), "ADULT")) BF("the list: an adult reads \"%s\"", render_big_list_word(&tank, 1));
        if (render_big_list_word(&tank, N_FISH_MAX)) BF("the list: a visitor has a home fish's word");
        tank.fish[0].shape = sh; tank.fish[0].fins = fi; be_set(BE_PLAIN);
    }
    /* not ready: the same place is not EVOLVE */
    for (int s = 0; s < BE_STATES; s++) {
        if (s == BE_READY) continue;
        render_milestones_leave(); be_set(s); be_page(be_fa, 0);
        if (render_milestones_tap(&tank, (float)(ex + ew / 2), (float)(ey + eh / 2)) >= MS_TAP_EVOLVE) BF("%s: EVOLVE's place answers EVOLVE", BE_NAME[s]);
    }
    /* next door: ready, but away - it is not in the list, so its pane is never up (the pane settles on the first fish
       here, an adult): no EVOLVE */
    render_milestones_leave(); be_set(BE_READY); tank.fish[0].away = true; be_page(be_fa, 0);
    {   const char *line = render_big_stage_line(&tank);
        if (!line || strcmp(line, "ADULT")) BF("next door: the pane is not the first fish home's (\"%s\")", line ? line : "(none)"); }
    if (render_big_evolve_box(&tank, &ex, &ey, &ew, &eh) || render_milestones_tap(&tank, (float)(ex + ew / 2), (float)(ey + eh / 2)) >= MS_TAP_EVOLVE) BF("next door: EVOLVE answers");
    tank.fish[0].away = false;
    /* a visitor that is ready where it lives: its pane keeps its own lines, no EVOLVE */
    {   fish_t *v = &tank.fish[N_FISH_MAX]; *v = tank.fish[0]; v->evo = 0; v->evo_offer = be_three(); v->uid = 0x383bbe40u;
        snprintf(v->name, sizeof v->name, "KELP");
        tank.vis_used[0] = true; tank.vis_badge[0] = 1; tank.vis_home[0] = 0x113afc50u; tank.vis_since[0] = tank.clock - 245;
        tank.fish[0].evo_offer = 0;
        render_milestones_leave(); be_page(be_fa, N_FISH_MAX);
        if (render_big_stage_line(&tank)) BF("a visitor: the own fish's stage line on its pane");
        if (render_big_evolve_box(&tank, &ex, &ey, &ew, &eh)) BF("a visitor: EVOLVE on its pane");
        if (render_milestones_tap(&tank, (float)(ex + ew / 2), (float)(ey + eh / 2)) >= MS_TAP_EVOLVE) BF("a visitor: EVOLVE's place answers EVOLVE");
        if (be_gold_in(be_fa, BE_NAME_Y1, BE_STRIP_Y0)) BF("a visitor: gold on its stage line");
        tank.vis_used[0] = false; render_milestones_leave();
    }
    /* every part's PARAGON line fits */
    for (int p = 1; p < EVO_N; p++) {
        be_set(BE_PARAGON); tank.fish[0].evo = (uint8_t)p; be_page(be_fa, 0);
        const char *line = render_big_stage_line(&tank);
        snprintf(want, sizeof want, "PARAGON - %s", tank_evo_name(p));
        if (!line || strcmp(line, want)) BF("part %d: \"%s\"", p, line ? line : "(none)");
        else if (render_text_w(line, 3) > 472 - 48 && render_text_w(line, 2) > 472 - 48) BF("\"%s\" is wider than the pane", line);
    }
    /* the kept frame: each change of state draws again (a kept frame is the whole one) */
    extern void render_big_keeper_counts(unsigned *kept, unsigned *whole);
    {   static const int SEQ[] = { BE_PLAIN, BE_SPARKLE, BE_READY, BE_PARAGON, BE_PLAIN };
        /* (a fish that swims out of its list cell costs a whole frame each frame - on 11-24 this cast's classic elder did, by
           2 px at the cell's left: a bulbous body here, so the frames are kept and a stale stage line would show) */
        tank.fish[0].shape = SHAPE_BULBOUS; tank.fish[0].fins = FINS_CLASSIC;
        render_milestones_leave(); be_set(SEQ[0]); be_page(be_fa, 0);
        for (int i = 1; i < (int)(sizeof SEQ / sizeof SEQ[0]); i++) {
            unsigned k0, k1, whole;
            render_big_keeper_counts(&k0, &whole);
            for (int k = 0; k < 4; k++) { tank.clock += 0.04; render_frame_begin(); render_milestones(&tank, be_fa, TANK_W); }
            render_big_keeper_counts(&k1, &whole);
            if (k1 - k0 < 3) BF("%s: only %u of 4 still frames kept (the test sees no kept frame)", BE_NAME[SEQ[i - 1]], k1 - k0);
            if (SEQ[i] == BE_PLAIN) { tank.fish[0].evo = 0; progression_set_age(&tank, 0, EVO_AGE - EVO_SPARKLE_S - 600); }   /* (as a fresh elder: its sparkle ends) */
            else be_set(SEQ[i]);
            render_frame_begin(); render_milestones(&tank, be_fa, TANK_W);
            render_debug_page_whole(true); render_milestones(&tank, be_fb, TANK_W); render_debug_page_whole(false);
            if (memcmp(be_fa, be_fb, sizeof be_fa)) {
                int n = 0, fx = -1, fy = -1; for (int j = 0; j < TANK_W * TANK_H; j++) if (be_fa[j] != be_fb[j]) { if (!n) { fx = j % TANK_W; fy = j / TANK_W; } n++; }
                BF("%s -> %s: the kept frame is not the whole one (%d px, first at %d,%d)", BE_NAME[SEQ[i - 1]], BE_NAME[SEQ[i]], n, fx, fy);
            }
        }
    }
    render_milestones_leave();
    printf("selftest-bigevo: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
    #undef BF
#else
    printf("selftest-bigevo: the big screens only\n"); return 0;
#endif
}
#ifdef TANK_P4
/* the reveal at 2x: a page point's place on the glass (setup.c ZM_*: the page centred at twice its size), the middle
   of its 2 x 2 block */
#define BR_GX(x) ((float)((TANK_W - PAGE_W * 2) / 2 + (x) * 2 + 1))
#define BR_GY(y) ((float)((TANK_H - PAGE_H * 2) / 2 + (y) * 2 + 1))
static void br_frame(double dt) { render_frame_begin(); tank.clock += dt; setup_touch(&tank, 0, 0, false); render_tank(&tank, be_fa, TANK_W); render_setup(&tank, be_fa, TANK_W, tank.clock); }
static void br_touch(int px, int py, bool down) { setup_touch(&tank, BR_GX(px), BR_GY(py), down); }
static void br_tap(int px, int py) { tank.clock += 0.3; br_touch(px, py, true); br_touch(px, py, false); }
static void br_hold(int px, int py, double s) {   /* a press held s seconds, a board frame each 1/30 s (the finger stays down) */
    tank.clock += 0.3; br_touch(px, py, true);
    for (double h = 0; h < s; h += 1.0 / 30) { render_frame_begin(); tank.clock += 1.0 / 30; br_touch(px, py, true); render_tank(&tank, be_fa, TANK_W); render_setup(&tank, be_fa, TANK_W, tank.clock); }
}
#endif
static int selftest_bigreveal(void) {
#ifdef TANK_P4
    #define RF(...) do { printf("FAIL: selftest-bigreveal: " __VA_ARGS__); printf("\n"); bad++; } while (0)
    int bad = 0;
    be_stage(); be_set(BE_READY);
    fish_t *f = &tank.fish[0];
    const uint16_t three = f->evo_offer;
    (void)setup_take_renamed();
    setup_begin_evolve(&tank, 0);
    if (!setup_is_evolve() || !setup_zoomed()) { RF("the reveal is not up at 2x (up %d, 2x %d)", setup_is_evolve(), setup_zoomed()); return 1; }
    const double t1 = tank.clock;
    const int KX = EVP_KEEP_X + EVP_KEEP_W / 2, BX = EVP_BACK_X + EVP_BACK_W / 2, BY = EVP_BTN_Y + EVP_BTN_H / 2;
    /* what is drawn at 2x is the page: the buttons and the tiles, block for block, where the taps go (the pocket page drawn
       at 1x, the same clock, against the 2x frame) */
    tank.clock = t1 + 12.0; br_frame(0); br_frame(0);
    if (setup_evo_phase(&tank) != SETUP_EVO_CHOOSE) RF("CHOOSE ONE is not up at 12 s (phase %d)", setup_evo_phase(&tank));
    {   render_big_pocket_pages(true);                    /* (be_fa holds the 2x frame br_frame drew) */ render_tank(&tank, be_fb, TANK_W); render_setup(&tank, be_fb, TANK_W, tank.clock); render_big_pocket_pages(false);
        static const struct { int x, y, w, h; const char *what; } R[] = {
            { EVP_KEEP_X, EVP_BTN_Y, EVP_KEEP_W, EVP_BTN_H, "KEEP THIS" }, { EVP_BACK_X, EVP_BTN_Y, EVP_BACK_W, EVP_BTN_H, "LATER" },
            { 0, EVP_TILE_Y, EVP_TILE_W, EVP_TILE_H, "tile 1" }, { 1, EVP_TILE_Y, EVP_TILE_W, EVP_TILE_H, "tile 2" }, { 2, EVP_TILE_Y, EVP_TILE_W, EVP_TILE_H, "tile 3" } };
        for (int i = 0; i < 5; i++) {
            const int x0 = i >= 2 ? EVP_TILE_X(R[i].x) : R[i].x; int d = 0;
            for (int y = R[i].y; y < R[i].y + R[i].h; y++) for (int x = x0; x < x0 + R[i].w; x++) {
                const uint16_t p = be_fb[(PAGE_Y + y) * TANK_W + PAGE_X + x];
                const int gx = (TANK_W - PAGE_W * 2) / 2 + x * 2, gy = (TANK_H - PAGE_H * 2) / 2 + y * 2;
                if (gy < 0 || gy + 1 >= TANK_H) continue;   /* (the Tab5 leaves the page's top and bottom rows out) */
                d += be_fa[gy * TANK_W + gx] != p || be_fa[(gy + 1) * TANK_W + gx + 1] != p;
            }
            if (d) RF("%s: the 2x frame is not the page there (%d px)", R[i].what, d);
        }
    }
    setup_cancel(&tank); (void)setup_take_renamed(); f->evo = 0; f->evo_offer = three;
    /* the taps */
    setup_begin_evolve(&tank, 0);
    const double t2 = tank.clock;
    tank.clock = t2 + 1.5; br_frame(0);
    br_tap(EVP_TILE_X(0) + EVP_TILE_W / 2, EVP_TILE_Y + EVP_TILE_H / 2);                  /* in the spin: hurried, nothing chosen */
    if (setup_evo_phase(&tank) == SETUP_EVO_CHOOSE || f->evo) RF("a tap in the spin chose (phase %d, evo %d)", setup_evo_phase(&tank), f->evo);
    tank.clock = t2 + 12.0; br_frame(0); br_frame(0);
    if (setup_evo_phase(&tank) != SETUP_EVO_CHOOSE) RF("CHOOSE ONE is not up at 12 s (phase %d)", setup_evo_phase(&tank));
    br_tap(EVP_TILE_X(2) + EVP_TILE_W / 2, EVP_TILE_Y + EVP_TILE_H / 2); br_frame(1.0 / 30);
    br_tap(KX, BY); br_frame(1.0 / 30);
    if (setup_evo_phase(&tank) != SETUP_EVO_KEEP) RF("KEEP THIS did not ask (phase %d)", setup_evo_phase(&tank));
    br_tap(BX, BY); br_frame(1.0 / 30);                                                    /* NOT YET */
    if (!setup_is_evolve() || f->evo || setup_evo_phase(&tank) != SETUP_EVO_CHOOSE) RF("NOT YET: page %d, evo %d, phase %d", setup_is_evolve(), f->evo, setup_evo_phase(&tank));
    /* a double tap on KEEP THIS: the question up and left up, nothing kept (THE TOUCH RULE: its second half is no tap) */
    tank.clock += 0.3; br_touch(KX, BY, true); br_touch(KX, BY, false); tank.clock += 0.1; br_touch(KX, BY, true); tank.clock += 0.09; br_touch(KX, BY, false); br_frame(0.3);
    if (f->evo || setup_evo_phase(&tank) != SETUP_EVO_KEEP) RF("a double tap on KEEP THIS: evo %d, phase %d - want nothing kept, the question up", f->evo, setup_evo_phase(&tank));
    /* YES held part way: nothing kept, the bar part full; let go, the question still up */
    br_hold(KX, BY, 0.27);
    {   const float bar = setup_evo_bar(&tank);
        if (f->evo || bar <= 0.2f || bar >= 1.0f) RF("YES held 0.27 s: evo %d, the bar %.2f - want nothing kept, part full", f->evo, bar); }
    br_touch(KX, BY, false); br_frame(1.0 / 30);
    if (f->evo || setup_evo_phase(&tank) != SETUP_EVO_KEEP) RF("YES let go at 0.27 s: evo %d, phase %d", f->evo, setup_evo_phase(&tank));
    /* ... held 0.8 s in the doorway: nothing kept, the bar short of its end */
    f->door = DOOR_ASKED;
    br_hold(KX, BY, 0.8);
    {   const float bar = setup_evo_bar(&tank);
        if (f->evo || !setup_is_evolve() || bar < 0.5f || bar >= 1.0f) RF("YES held 0.8 s in the doorway: evo %d, page %d, the bar %.2f - want nothing kept, short of its end", f->evo, setup_is_evolve(), bar); }
    br_touch(KX, BY, false); br_frame(1.0 / 30); f->door = DOOR_NONE;
    if (f->evo || setup_evo_phase(&tank) != SETUP_EVO_KEEP) RF("let go in the doorway: evo %d, phase %d", f->evo, setup_evo_phase(&tank));
    /* YES held: kept */
    br_hold(KX, BY, 0.6); br_touch(KX, BY, false); br_frame(1.0 / 30);
    if (setup_active() || f->evo != evo_offer_at(three, 2) || f->evo_offer) RF("YES held 0.6 s: page %d, evo %d (want %d), offer %04x", setup_active(), f->evo, evo_offer_at(three, 2), f->evo_offer);
    (void)setup_take_renamed();
    printf("selftest-bigreveal: %s (the 2x page block for block; the spin, a tile, KEEP THIS, NOT YET, a double tap, YES part way, the doorway, YES)\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
    #undef RF
#else
    printf("selftest-bigreveal: the big screens only\n"); return 0;
#endif
}
#ifdef TANK_P4
/* the sheet for Strato: four cuts of the page, two across, two down - the pane (EVOLVE with its stars, the hour before,
   evolved) and the list with an evolved fish - each under a label */
#define BS_W 480
#define BS_H 400
#define BS_GAP 16
#define BS_SW (2 * BS_W + 3 * BS_GAP)
#define BS_SH (2 * BS_H + 3 * BS_GAP)
static uint16_t bs_sheet[BS_SW * BS_SH];
static void bs_put(int cell, const char *label, int x0) {
    render_rect(be_fa, TANK_W, x0 - PAGE_X, 0 - PAGE_Y, BS_W, 30, 0x000000);   /* (page px: the frame's less PAGE_X / PAGE_Y) */
    render_text(be_fa, TANK_W, x0 + 6 - PAGE_X, 5 - PAGE_Y, 3, 0xffffff, label);
    const int ox = BS_GAP + (cell % 2) * (BS_W + BS_GAP), oy = BS_GAP + (cell / 2) * (BS_H + BS_GAP);
    for (int y = 0; y < BS_H && y < TANK_H; y++) for (int x = 0; x < BS_W && x0 + x < TANK_W; x++) bs_sheet[(oy + y) * BS_SW + ox + x] = be_fa[y * TANK_W + x0 + x];
}
#endif
static int bigevo_shot(const char *prefix) {
#ifdef TANK_P4
    char path[512];
    be_stage();
    for (int s = 0; s < BE_STATES; s++) {
        render_milestones_leave(); be_set(s); be_page(be_fa, 0);
        snprintf(path, sizeof path, "%s_keeper_%s.ppm", prefix, BE_NAME[s]); write_ppm(path, be_fa);
        printf("bigevo-shot: %s\n", path);
    }
    for (int i = 0; i < BS_SW * BS_SH; i++) bs_sheet[i] = 0x4208;   /* (a grey between the cuts) */
    render_milestones_leave(); be_set(BE_READY); be_page(be_fa, 0);
    tank.clock += 0.7; render_frame_begin(); render_milestones(&tank, be_fa, TANK_W);   /* (the stars part way through a twinkle) */
    snprintf(path, sizeof path, "%s_keeper_evolve.ppm", prefix); write_ppm(path, be_fa); printf("bigevo-shot: %s\n", path);
    bs_put(0, "READY: EVOLVE", 772);
    render_milestones_leave(); be_set(BE_SPARKLE); be_page(be_fa, 0); bs_put(1, "THE HOUR BEFORE", 772);
    render_milestones_leave(); be_set(BE_PARAGON); be_page(be_fa, 0); bs_put(2, "EVOLVED", 772);
    render_milestones_leave(); be_page(be_fa, 1); bs_put(3, "THE LIST: MIRA EVOLVED", 264);   /* (another fish's pane up: MIRA's row as the list shows it) */
    snprintf(path, sizeof path, "%s_keeper_final.ppm", prefix);
    FILE *fp = fopen(path, "wb");
    if (fp) {
        fprintf(fp, "P6\n%d %d\n255\n", BS_SW, BS_SH);
        for (int i = 0; i < BS_SW * BS_SH; i++) { const uint16_t p = FB_SWAP16(bs_sheet[i]); const unsigned char rgb[3] = { (unsigned char)((p >> 11) * 255 / 31), (unsigned char)(((p >> 5) & 63) * 255 / 63), (unsigned char)((p & 31) * 255 / 31) }; fwrite(rgb, 1, 3, fp); }
        fclose(fp); printf("bigevo-shot: %s\n", path);
    }
    render_milestones_leave();
    return 0;
#else
    (void)prefix; printf("bigevo-shot: the big screens only\n"); return 0;
#endif
}
