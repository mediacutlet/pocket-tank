/* --selftest-evo-kept (0.4.3, THE REVEAL KEPT; render.h render_evo_held). Strato on the square, 11-22: the paragon's
 * reveal ran rough, worse with a busy tank behind it - the live tank was drawn under the page every frame, hidden. The
 * reveal is now a page of its own, held as the lab's pages are. These pin, on two buffers swapped every frame as the
 * boards swap them:
 *   K1  the page paints every pixel itself: nothing of the tank shows
 *   K2  a frame with nothing new draws inside its moving boxes and nowhere else, and does not draw the page whole -
 *       on the shells, mid-spin, CHOOSE ONE at rest, a part tried on, KEEP asked
 *   K3  everything that moves keeps the clock: the held page is, pixel for pixel, the page drawn whole at the same
 *       clock - at rest and mid-spin alike, the fish and the tiles' fish swimming, the spin flicking part to part
 *   K4  every part on every body, tried on at the page's 2x, stays in the fish's box (the held page = the whole page)
 *   K5  the whole reveal, start to CHOOSE ONE at 30 fps, draws the page whole only at its few changes
 *   K6  taps land where they did and the outcome is the same: a tap in the spin hurries it, a tile tries on, KEEP THIS
 *       asks, NOT YET takes it back, YES keeps the part (the word in the save is --selftest-evo's)
 *   K4b the bare fish on every body, on the shells and the spin, across its swim: nothing of it outside its box
 *   K8  the fish next door (TP-7): VISITING NEXT DOOR in KEEP THIS's place, held and whole; home again, KEEP THIS back
 *   K7  the director's LIVE reveal is the old one (over the tank, whole); the bench's loop and LIVE end with the page */
#define EK_FAIL(...) do { printf("FAIL: selftest-evo-kept: " __VA_ARGS__); printf("\n"); return 1; } while (0)
static uint16_t ek_fb[2][TANK_W * TANK_H], ek_ref[TANK_W * TANK_H], ek_was[TANK_W * TANK_H];
static int ek_n;                                       /* frames drawn: buffer ek_n & 1 is the one drawn last */
static uint16_t *ek_frame(double dt) {                 /* one board frame: the next buffer, the clock on, the held page */
    render_frame_begin(); tank.clock += dt; setup_touch(&tank, 0, 0, false);
    uint16_t *fb = ek_fb[++ek_n & 1];
    render_evo_held(&tank, fb, TANK_W, tank.clock);
    return fb;
}
static int ek_diff(const uint16_t *a, const uint16_t *b, int *fx, int *fy) {
    int n = 0;
    for (int i = 0; i < TANK_W * TANK_H; i++) if (a[i] != b[i]) { if (!n) { *fx = i % TANK_W; *fy = i / TANK_W; } n++; }
    return n;
}
/* the page as it is now drawn WHOLE at this clock (a buffer nobody keeps) against the held buffer drawn last: every pixel */
static int ek_same_as_whole(const char *what) {
    render_setup(&tank, ek_ref, TANK_W, tank.clock);
    int x = 0, y = 0, d = ek_diff(ek_fb[ek_n & 1], ek_ref, &x, &y);
    if (d) { printf("FAIL: selftest-evo-kept: K3: %s: the held page differs from the page drawn whole at the same clock (%d px, first at %d,%d)\n", what, d, x, y); return 1; }
    ek_frame(1.0 / 30); ek_frame(1.0 / 30);            /* (the reference draw spoiled both kept buffers: each drawn whole again) */
    return 0;
}
static bool ek_in_boxes(int x, int y) {
    int bx[SETUP_EVO_BANDS][4]; const int nb = setup_evo_bands(&tank, bx);
    for (int i = 0; i < nb; i++) if (x - PAGE_X >= bx[i][0] && x - PAGE_X <= bx[i][2] && y - PAGE_Y >= bx[i][1] && y - PAGE_Y <= bx[i][3]) return true;
    return false;
}
/* K2 on the page that is up: settle, then n still frames - none whole, nothing outside the boxes changed in either buffer */
static int ek_still(const char *what, int n) {
    ek_frame(1.0 / 30); ek_frame(1.0 / 30);
    static uint16_t was2[2][TANK_W * TANK_H];
    memcpy(was2[0], ek_fb[0], sizeof ek_fb[0]); memcpy(was2[1], ek_fb[1], sizeof ek_fb[1]);
    int b0, w0, b1, w1; render_evo_held_counts(&b0, &w0);
    for (int k = 0; k < n; k++) ek_frame(1.0 / 30);
    render_evo_held_counts(&b1, &w1);
    if (w1 != w0 || b1 != b0 + n) EK_FAIL("K2: %s: %d still frames drew %d whole, %d the boxes alone (want 0, %d)", what, n, w1 - w0, b1 - b0, n);
    for (int j = 0; j < 2; j++)
        for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++)
            if (ek_fb[j][y * TANK_W + x] != was2[j][y * TANK_W + x] && !ek_in_boxes(x, y))
                EK_FAIL("K2: %s: a pixel outside the moving boxes changed (%d,%d)", what, x, y);
    return 0;
}
static void ek_tap(float px, float py) { tank.clock += 0.3; pg_touch(px, py, true); pg_touch(px, py, false); }   /* (a keeper's pace: THE TOUCH RULE eats a tap within TAP_W of the step before it) */
static void ek_hold(float px, float py, double s) {   /* a press held s seconds, the polls 1/30 s apart (KEEP?'s YES is a hold) */
    tank.clock += 0.3; pg_touch(px, py, true);
    for (double h = 0; h < s; h += 1.0 / 30) { tank.clock += 1.0 / 30; pg_touch(px, py, true); }
    pg_touch(px, py, false);
}
static int selftest_evo_kept(void) {
    tank_init(&tank, 4343); forms_cast(4);
    for (int i = 0; i < 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
    fish_t *f = &tank.fish[0];
    f->evo = 0; f->evo_offer = 0; progression_set_age(&tank, 0, EVO_AGE + 5); tank_evo_roll(&tank, f);
    if (!tank_evo_ready(f)) EK_FAIL("the fish is not READY TO EVOLVE");
    const uint16_t three = f->evo_offer;
    (void)setup_take_renamed();
    setup_begin_evolve(&tank, 0);
    if (!setup_is_evolve() || !setup_evo_black()) EK_FAIL("K1: the reveal is not a page of its own");
    const double t0 = tank.clock;
    /* K1 */
    for (int j = 0; j < 2; j++) for (int i = 0; i < TANK_W * TANK_H; i++) ek_fb[j][i] = 0xF81F;
    ek_frame(0); ek_frame(0);
    for (int j = 0; j < 2; j++) for (int i = 0; i < TANK_W * TANK_H; i++) if (ek_fb[j][i] == 0xF81F) EK_FAIL("K1: the page left pixel %d,%d to the tank", i % TANK_W, i / TANK_W);
    /* K2 + K3 through the first reveal, page by page (the shells 0..1.2 s, tile 1 spinning 1.2..3.2, tile 2 3.4..6.4,
       tile 3 6.6..11.1, then CHOOSE ONE) */
    {   int bx[SETUP_EVO_BANDS][4];
        tank.clock = t0 + 0.2;
        if (setup_evo_bands(&tank, bx) != 1) EK_FAIL("K2: the shells have %d moving boxes (want the fish's alone)", setup_evo_bands(&tank, bx));
        if (ek_still("the shells", 12)) return 1;
        if (ek_same_as_whole("the shells")) return 1;
        static const struct { double at; const char *what; int boxes; } SP[] = { { 1.5, "tile 1 spinning fast", 2 }, { 2.9, "tile 1 slowing", 2 },
                                                                                { 4.0, "tile 2 spinning, tile 1 landed", 3 }, { 7.0, "tile 3 spinning", 4 }, { 10.7, "tile 3 slowing", 4 } };
        for (int i = 0; i < 5; i++) {
            tank.clock = t0 + SP[i].at;
            ek_frame(0); ek_frame(0);
            if (setup_evo_bands(&tank, bx) != SP[i].boxes) EK_FAIL("K2: %s: %d moving boxes (want %d)", SP[i].what, setup_evo_bands(&tank, bx), SP[i].boxes);
            if (ek_still(SP[i].what, 6)) return 1;      /* (6 frames: 0.2 s, inside one page) */
            if (ek_same_as_whole(SP[i].what)) return 1;
            /* ... and mid-run: 5 frames on from a held page, the parts flicking past, the same as the page whole then */
            for (int k = 0; k < 5; k++) ek_frame(1.0 / 30);
            if (ek_same_as_whole(SP[i].what)) return 1;
        }
        /* the spinning tile changes from frame to frame with nothing touched, and so does the fish: they keep the clock */
        tank.clock = t0 + 7.2; ek_frame(0); ek_frame(0);
        int moved_fish = 0, moved_tile = 0;
        for (int k = 0; k < 10; k++) {
            memcpy(ek_was, ek_fb[ek_n & 1], sizeof ek_was);
            ek_frame(1.0 / 30); ek_frame(1.0 / 30);     /* (two frames on: the same buffer again) */
            const uint16_t *fb = ek_fb[ek_n & 1];
            int df = 0, dt = 0;
            for (int y = EVP_FISH_Y0; y <= EVP_FISH_Y1; y++) for (int x = 0; x < PAGE_W; x++) df += fb[(y + PAGE_Y) * TANK_W + x + PAGE_X] != ek_was[(y + PAGE_Y) * TANK_W + x + PAGE_X];
            for (int y = EVP_TILE_Y; y < EVP_TILE_Y + EVP_TILE_H; y++) for (int x = EVP_TILE_X(2); x < EVP_TILE_X(2) + EVP_TILE_W; x++) dt += fb[(y + PAGE_Y) * TANK_W + x + PAGE_X] != ek_was[(y + PAGE_Y) * TANK_W + x + PAGE_X];
            moved_fish += df > 0; moved_tile += dt > 0;
        }
        if (moved_fish < 8 || moved_tile < 8) EK_FAIL("K3: in 10 frame pairs the fish moved in %d, the spinning tile in %d (they keep the clock)", moved_fish, moved_tile);
        /* CHOOSE ONE at rest, a part tried on, KEEP asked */
        tank.clock = t0 + 13; ek_frame(0); ek_frame(0);
        if (setup_evo_bands(&tank, bx) != 4) EK_FAIL("K2: CHOOSE ONE has %d moving boxes (want the fish's and three tiles)", setup_evo_bands(&tank, bx));
        if (ek_still("CHOOSE ONE at rest", 12) || ek_same_as_whole("CHOOSE ONE at rest")) return 1;
        ek_tap(EVP_TILE_X(1) + EVP_TILE_W / 2, EVP_TILE_Y + EVP_TILE_H / 2);
        if (ek_still("a part tried on", 12) || ek_same_as_whole("a part tried on")) return 1;
        ek_tap(EVP_KEEP_X + EVP_KEEP_W / 2, EVP_BTN_Y + EVP_BTN_H / 2);
        if (setup_evo_bands(&tank, bx) != 2) EK_FAIL("K2: KEEP asked has %d moving boxes (want the fish's - its box covers the tiles - and YES's, its hold's fill)", setup_evo_bands(&tank, bx));
        if (ek_still("KEEP asked", 12) || ek_same_as_whole("KEEP asked")) return 1;
        {   /* K9 (0.4.3 audit r2, R2-L32): YES held part way - its fill drawn in its box, the held page the page drawn whole; let go,
               nothing kept and the fill gone */
            const float yx = EVP_KEEP_X + EVP_KEEP_W / 2, yy = EVP_BTN_Y + EVP_BTN_H / 2;
            static uint16_t empty[TANK_W * TANK_H]; render_setup(&tank, empty, TANK_W, tank.clock);
            tank.clock += 0.3; pg_touch(yx, yy, true);
            for (int k = 0; k < 8; k++) {                /* (0.27 s of 0.5: ek_frame's own touch is a lift - the board's frame, the finger down) */
                render_frame_begin(); tank.clock += 1.0 / 30; pg_touch(yx, yy, true); render_evo_held(&tank, ek_fb[++ek_n & 1], TANK_W, tank.clock); }
            if (f->evo) EK_FAIL("K9: YES held 0.27 s kept already");
            render_setup(&tank, ek_ref, TANK_W, tank.clock);
            {   const char *dir = getenv("PT_EVO_SHOTS"); char path[600];   /* (the hold's look, for Strato: empty, then part way) */
                if (dir) { snprintf(path, sizeof path, "%s/reveal_yes_empty.ppm", dir); write_ppm(path, empty);
                           snprintf(path, sizeof path, "%s/reveal_yes_held.ppm", dir); write_ppm(path, ek_ref); } }
            {   int x = 0, y = 0, d = ek_diff(ek_fb[ek_n & 1], ek_ref, &x, &y);
                if (d) EK_FAIL("K9: YES held part way: the held page differs from the page drawn whole (%d px, first at %d,%d)", d, x, y); }
            {   const int fx = PAGE_X + EVP_KEEP_X + 12, fy = PAGE_Y + EVP_BTN_Y + EVP_BTN_H - 7;   /* (the track's start, inside YES's foot) */
                if (ek_ref[fy * TANK_W + fx] != 0xffff || empty[fy * TANK_W + fx] == 0xffff) EK_FAIL("K9: YES held 0.27 s: its track did not fill (%04x, empty %04x)", ek_ref[fy * TANK_W + fx], empty[fy * TANK_W + fx]); }
            pg_touch(yx, yy, false); ek_frame(0); ek_frame(0);
            if (f->evo || setup_evo_phase(&tank) != SETUP_EVO_KEEP) EK_FAIL("K9: YES let go at 0.27 s: evo %d, phase %d - want nothing kept, the question up", f->evo, setup_evo_phase(&tank));
            if (ek_same_as_whole("YES let go")) return 1;
        }
        {   /* K9b (0.4.3 audit r3b, R3b-L11): HOLD held past its half second with the fish in the doorway - nothing kept, so the bar
               stops short of its end (a full bar is a keep); let go, nothing kept */
            const float yx = EVP_KEEP_X + EVP_KEEP_W / 2, yy = EVP_BTN_Y + EVP_BTN_H / 2;
            f->door = DOOR_ASKED;
            tank.clock += 0.3; pg_touch(yx, yy, true);
            for (int k = 0; k < 24; k++) {               /* (0.8 s: past the 0.5 s hold) */
                render_frame_begin(); tank.clock += 1.0 / 30; pg_touch(yx, yy, true); render_evo_held(&tank, ek_fb[++ek_n & 1], TANK_W, tank.clock); }
            render_setup(&tank, ek_ref, TANK_W, tank.clock);
            {   const char *dir = getenv("PT_EVO_SHOTS"); char path[600];   /* (the waiting bar's look, for Strato) */
                if (dir) { snprintf(path, sizeof path, "%s/reveal_yes_doorway.ppm", dir); write_ppm(path, ek_ref); } }
            const int fy = PAGE_Y + EVP_BTN_Y + EVP_BTN_H - 7;   /* (the track's row, inside HOLD's foot; its inside runs KEEP_X+11 .. +138) */
            const uint16_t mid = ek_ref[fy * TANK_W + PAGE_X + EVP_KEEP_X + 100], end = ek_ref[fy * TANK_W + PAGE_X + EVP_KEEP_X + 137];
            if (f->evo || !setup_is_evolve()) EK_FAIL("K9b: HOLD held 0.8 s in the doorway: evo %d, page %d - want nothing kept, the page up", f->evo, setup_is_evolve());
            if (mid != 0xffff || end == 0xffff) EK_FAIL("K9b: HOLD held 0.8 s in the doorway: the track at 3/4 %04x, at its end %04x - want filled, then short of the end", mid, end);
            {   int x = 0, y = 0, d = ek_diff(ek_fb[ek_n & 1], ek_ref, &x, &y);   /* (the held frame the S3 boards show: the same) */
                if (d) EK_FAIL("K9b: HOLD waiting in the doorway: the held page differs from the page drawn whole (%d px, first at %d,%d)", d, x, y); }
            pg_touch(yx, yy, false); ek_frame(0); ek_frame(0);
            f->door = DOOR_NONE;
            if (f->evo || setup_evo_phase(&tank) != SETUP_EVO_KEEP) EK_FAIL("K9b: let go in the doorway: evo %d, phase %d - want nothing kept, the question up", f->evo, setup_evo_phase(&tank));
        }
        ek_tap(EVP_BACK_X + EVP_BACK_W / 2, EVP_BTN_Y + EVP_BTN_H / 2);             /* NOT YET */
        if (!setup_is_evolve() || f->evo) EK_FAIL("K6: NOT YET: page %d, evo %d", setup_is_evolve(), f->evo);
        if (ek_still("NOT YET, the part still on", 6) || ek_same_as_whole("NOT YET, the part still on")) return 1;
        /* K8 (TP-7): the fish swims next door and back - KEEP THIS goes and comes back: the still part drawn whole each time */
        {   const char *dir = getenv("PT_EVO_SHOTS");   /* (a folder: the page as drawn, home and next door, for the words' approval) */
            char path[600];
            if (dir) { render_setup(&tank, ek_ref, TANK_W, tank.clock); snprintf(path, sizeof path, "%s/reveal_choose_home.ppm", dir); write_ppm(path, ek_ref); }
            f->away = true;
            if (dir) { render_setup(&tank, ek_ref, TANK_W, tank.clock); snprintf(path, sizeof path, "%s/reveal_choose_next_door.ppm", dir); write_ppm(path, ek_ref);
                       setup_debug_evo(&tank, "keep", 0); render_setup(&tank, ek_ref, TANK_W, tank.clock);
                       snprintf(path, sizeof path, "%s/reveal_keep_asked_next_door.ppm", dir); write_ppm(path, ek_ref); setup_debug_evo(&tank, "back", 0);
                       ek_frame(1.0 / 30); ek_frame(1.0 / 30); }
        }
        {   /* the words are there: KEEP's place differs from home's, on CHOOSE ONE and with the question up */
            static uint16_t home[TANK_W * TANK_H];
            for (int q = 0; q < 2; q++) {
                if (q) setup_debug_evo(&tank, "keep", 0);
                f->away = false; render_setup(&tank, home, TANK_W, tank.clock);
                f->away = true;  render_setup(&tank, ek_ref, TANK_W, tank.clock);
                int d = 0;
                for (int y = EVP_BTN_Y; y < EVP_BTN_Y + EVP_BTN_H; y++) for (int x = EVP_KEEP_X; x < EVP_KEEP_X + EVP_KEEP_W; x++) {
                    const int gx = x + PAGE_X, gy = y + PAGE_Y;
                    if (gx >= 0 && gy >= 0 && gx < TANK_W && gy < TANK_H) d += home[gy * TANK_W + gx] != ek_ref[gy * TANK_W + gx];
                }
                if (d < 50) EK_FAIL("K8: next door, %s: KEEP's place is the same as at home (%d px differ) - the reason is not drawn", q ? "the question up" : "CHOOSE ONE", d);
                if (q) setup_debug_evo(&tank, "back", 0);
            }
            ek_frame(1.0 / 30); ek_frame(1.0 / 30);
        }
        if (ek_still("next door, VISITING NEXT DOOR in KEEP's place", 6) || ek_same_as_whole("next door")) return 1;
        f->away = false;
        if (ek_still("home again, KEEP THIS back", 6) || ek_same_as_whole("home again")) return 1;
    }
    /* K4: every part on every body, tried on */
    {   const uint8_t keep_sh = f->shape, keep_fi = f->fins, keep_pat = f->pattern;
        for (int sh = 0; sh < SHAPE_N; sh++) for (int fi = 0; fi < FINS_N; fi++) {
            f->shape = (uint8_t)sh; f->fins = (uint8_t)fi; f->pattern = (uint8_t)((sh + fi) % PATTERN_N);
            for (int p0 = 1; p0 < EVO_N; p0 += 3) {
                int p[3] = { p0, p0 + 1 < EVO_N ? p0 + 1 : 1, p0 + 2 < EVO_N ? p0 + 2 : 2 };
                setup_cancel(&tank); (void)setup_take_renamed();
                f->evo = 0; f->evo_offer = (uint16_t)(p[0] | p[1] << 5 | p[2] << 10 | EVO_SHOWN);
                setup_begin_evolve(&tank, 0);
                if (!setup_is_evolve()) EK_FAIL("K4: the page did not open (%d %d %d)", p[0], p[1], p[2]);
                for (int k = 0; k < 3; k++) {
                    setup_activate(&tank, SETUP_HIT_SLOT0 + k);
                    ek_frame(0); ek_frame(0);
                    for (int i = 0; i < 4; i++) ek_frame(1.0 / 30);
                    char what[80]; snprintf(what, sizeof what, "K4: %s on %s body, %s fins", tank_evo_name(p[k]), SHAPE_NAMES[sh], FINS_NAMES[fi]);
                    if (ek_same_as_whole(what)) return 1;
                }
            }
        }
        setup_cancel(&tank); (void)setup_take_renamed();
        f->shape = keep_sh; f->fins = keep_fi; f->pattern = keep_pat; f->evo = 0; f->evo_offer = three;
    }
    /* K4b: the bare fish on every body, on the shells and spin pages (its box ends above the tiles there), across a whole
       beat of its swim: the held page is the page drawn whole - nothing of it reaches out of its box to go stale */
    {   const uint8_t keep_sh = f->shape, keep_fi = f->fins, keep_pat = f->pattern;
        for (int sh = 0; sh < SHAPE_N; sh++) for (int fi = 0; fi < FINS_N; fi++) {
            f->shape = (uint8_t)sh; f->fins = (uint8_t)fi; f->pattern = (uint8_t)((sh + 2 * fi) % PATTERN_N);
            setup_cancel(&tank); (void)setup_take_renamed();
            f->evo = 0; f->evo_offer = three & 0x7fff;
            setup_begin_evolve(&tank, 0);
            const double t2 = tank.clock;
            static const double AT[3] = { 0.3, 2.0, 7.5 };      /* the shells; tile 1 spinning; tile 3 spinning */
            for (int a = 0; a < 3; a++) {
                tank.clock = t2 + AT[a]; ek_frame(0); ek_frame(0);
                for (int i = 0; i < 8; i++) {                    /* eight points of the swim, 0.07 s apart */
                    ek_frame(0.07); ek_frame(0); 
                    char what[80]; snprintf(what, sizeof what, "K4b: the bare fish, %s body, %s fins, %.2f s", SHAPE_NAMES[sh], FINS_NAMES[fi], tank.clock - t2);
                    render_setup(&tank, ek_ref, TANK_W, tank.clock);
                    int x = 0, y = 0, d = ek_diff(ek_fb[ek_n & 1], ek_ref, &x, &y);
                    if (d) EK_FAIL("K3: %s: the held page differs from the page drawn whole at the same clock (%d px, first at %d,%d)", what, d, x, y);
                    ek_frame(0); ek_frame(0);                    /* (the reference spoiled both kept buffers) */
                    ek_frame(0.07);
                }
            }
        }
        setup_cancel(&tank); (void)setup_take_renamed();
        f->shape = keep_sh; f->fins = keep_fi; f->pattern = keep_pat; f->evo = 0; f->evo_offer = three;
    }
    /* K5: the first reveal whole, at 30 fps */
    {   int b0, w0, b1, w1;
        setup_begin_evolve(&tank, 0);
        render_evo_held_counts(&b0, &w0);
        for (int i = 0; i < 30 * 12; i++) ek_frame(1.0 / 30);
        render_evo_held_counts(&b1, &w1);
        if (w1 - w0 > 12) EK_FAIL("K5: the reveal at 30 fps drew the page whole %d times of %d (its changes: the open, three shells, CHOOSE ONE, two buffers each)", w1 - w0, (w1 - w0) + (b1 - b0));
        printf("selftest-evo-kept: the reveal, 12 s at 30 fps: %d frames whole, %d the boxes alone\n", w1 - w0, b1 - b0);
        setup_cancel(&tank); (void)setup_take_renamed();
        f->evo = 0; f->evo_offer = three;
    }
    /* K6: the taps, through the held page */
    {   setup_begin_evolve(&tank, 0);
        const double t1 = tank.clock;
        tank.clock = t1 + 1.5; ek_frame(0);
        ek_tap(EVP_TILE_X(0) + EVP_TILE_W / 2, EVP_TILE_Y + EVP_TILE_H / 2);         /* in the spin: hurried, nothing chosen */
        if (setup_evo_phase(&tank) == SETUP_EVO_CHOOSE || f->evo) EK_FAIL("K6: a tap in the spin chose (phase %d, evo %d)", setup_evo_phase(&tank), f->evo);
        tank.clock = t1 + 12.0; ek_frame(0); ek_frame(0);
        if (setup_evo_phase(&tank) != SETUP_EVO_CHOOSE) EK_FAIL("K6: CHOOSE ONE is not up at 12 s (phase %d)", setup_evo_phase(&tank));
        ek_tap(EVP_TILE_X(2) + EVP_TILE_W / 2, EVP_TILE_Y + EVP_TILE_H / 2); ek_frame(1.0 / 30);
        ek_tap(EVP_KEEP_X + EVP_KEEP_W / 2, EVP_BTN_Y + EVP_BTN_H / 2); ek_frame(1.0 / 30);
        if (setup_evo_phase(&tank) != SETUP_EVO_KEEP) EK_FAIL("K6: KEEP THIS did not ask (phase %d)", setup_evo_phase(&tank));
        ek_hold(EVP_KEEP_X + EVP_KEEP_W / 2, EVP_BTN_Y + EVP_BTN_H / 2, 0.6); ek_frame(1.0 / 30);   /* (YES: a 0.5 s hold, R2-L32) */
        if (setup_active() || setup_evo_black() || f->evo != evo_offer_at(three, 2) || f->evo_offer)
            EK_FAIL("K6: YES: page %d, held %d, evo %d (want %d), offer %04x", setup_active(), setup_evo_black(), f->evo, evo_offer_at(three, 2), f->evo_offer);
        (void)setup_take_renamed();
    }
    /* K7 */
    {   f->evo = 0; f->evo_offer = three; setup_begin_evolve(&tank, 0);
        setup_debug_evo_live(true);
        if (setup_evo_black() || !setup_is_evolve()) EK_FAIL("K7: the director's LIVE reveal is still a page of its own");
        setup_debug_evo_live(false);
        if (!setup_evo_black()) EK_FAIL("K7: back from LIVE: not held");
        /* the bench left on (loop, live) ends with the page: the next reveal is the keeper's - held, and CHOOSE ONE stays */
        setup_debug_evo(&tank, "loop", 1); setup_debug_evo_live(true);
        setup_cancel(&tank); (void)setup_take_renamed();
        f->evo = 0; f->evo_offer = three & 0x7fff; setup_begin_evolve(&tank, 0);
        if (!setup_evo_black()) EK_FAIL("K7: a LIVE left on outlived the page");
        const double t3 = tank.clock; tank.clock = t3 + 14; ek_frame(0); tank.clock = t3 + 16; ek_frame(0);
        if (setup_evo_phase(&tank) != SETUP_EVO_CHOOSE) EK_FAIL("K7: a loop left on outlived the page (phase %d at 16 s)", setup_evo_phase(&tank));
        setup_cancel(&tank); (void)setup_take_renamed();
    }
    {   /* (PT_EVO_SHOTS) the shop's FRY and LAB rows with a fry's welcome owed and room (R2-L14), for Strato */
        const char *dir = getenv("PT_EVO_SHOTS");
        if (dir) {
            static uint16_t sb[TANK_W * TANK_H]; char path[600];
            while (tank.n_fish > 3) progression_sell_fish(&tank, tank.n_fish - 1);
            while (progression_newborn() >= 0) progression_newborn_done(&tank);
            tank.tank_ms_bits |= TMS_FIRST_SALE; tank.sd_balance = 5000; tank.sd_unlocks |= SD_ITEM_LAB; tank.night = false;
            if (!progression_buy_fry(&tank, 1)) printf("selftest-evo-kept: (shots) the fry was not bought\n");
            for (int r = 0; r < 2; r++) {
                if (shop_goto(r ? SHOP_ROW_LAB : SHOP_ROW_FRY) < 0) continue;
                render_shop(&tank, sb, TANK_W); snprintf(path, sizeof path, "%s/shop_%s_row_welcome.ppm", dir, r ? "lab" : "fry"); write_ppm(path, sb);
            }
            render_shop_leave();
            while (progression_newborn() >= 0) progression_newborn_done(&tank);
        }
    }
    printf("selftest-evo-kept: OK\n");
    return 0;
}
