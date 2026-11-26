/* v6_test.inc - audit 0.4.3 r1 verifier V6 probes (NOT a selftest as written: prints what it sees).
 *   --selftest-v6 lab    SLB-2: a lab fish given (passport -> another tank) reads THE SHOP; the travel bowl's FROM keeps LAB
 *   --selftest-v6 evo    SLB-3: a paragon whose passport comes back with no evo word (as a 0.4.2 tank writes it) is READY again
 *                        (V6_PP_HEX=<hex of a passport>: adopt that passport instead of zeroing the word in place)
 *   --selftest-v6 fry    SLB-4: progression_fry_adopt keeps a passport's evo (a part / three offers) on a fry
 *   --selftest-v6 big    SLB-5: a 21x15 / 13x6.5 body from the air: what it reads as, its pixels, the reveal page's box */
#define V6P(...) do { printf("V6: " __VA_ARGS__); printf("\n"); } while (0)
static int v6_hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static void v6_hex(const uint8_t *b, int n) { printf("V6HEX "); for (int i = 0; i < n; i++) printf("%02x", b[i]); printf("\n"); }
static void v6_setup(void) {
    shelf_wipe(); shelf_reboot();
    tank_init(&tank, 4343); forms_cast(3); tank_ensure_ids(&tank);
    while (progression_newborn() >= 0) progression_newborn_done(&tank);
    for (int i = 0; i < 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
}
static int v6_lab(void) {
    v6_setup();
    progression_fry_open(&tank);
    tank.sd_balance = 1500 + 250;
    if (!progression_lab_buy(&tank)) { V6P("lab buy refused"); return 1; }
    fish_t r = tank.fish[0]; snprintf(r.name, FISH_NAME_MAX + 1, "lab"); tank_set_body(&r, 19, 10);
    int s = progression_lab_pour(&tank, &r);
    if (s < 0) { V6P("pour refused (why %d)", progression_lab_why(&tank)); return 1; }
    char l1[64]; const char *l2;
    setup_born_lines(&tank, s, l1, sizeof l1, &l2);
    V6P("poured: slot %d from_lab %d, ms_bits BOUGHT %d, born lines '%s %s', family row '%s'", s, progression_from_lab(&tank, s),
        (tank.fish[s].ms_bits & MS_BOUGHT) != 0, l1, l2, setup_from_name(&tank, s, 0));
    /* GIVE: the sender's passport (link.c:327 sets home_tank = the sender's uid) */
    passport_t pp; passport_from_fish(&pp, &tank.fish[s], tank.uid, 3600);
    uint8_t buf[PASSPORT_MAX_BYTES]; int n = passport_write(&pp, buf, sizeof buf);
    passport_t rd; if (!passport_read(&rd, buf, n)) { V6P("read back failed"); return 1; }
    V6P("on the air: home_tank %08x, ms_bits BOUGHT %d (STORY_FROM_LAB is %08x: nothing in the passport carries it)", (unsigned)rd.home_tank, (rd.ms_bits & MS_BOUGHT) != 0, STORY_FROM_LAB);
    const uint32_t uid = tank.fish[s].uid;
    if (!progression_give_out(&tank, s)) { V6P("give_out refused"); return 1; }
    rd.home_tank = 0x5eed1234u;                          /* (as it lands next door: from a tank that is not this one) */
    int g = progression_gift_adopt(&tank, &rd);
    if (g < 0) { V6P("gift_adopt refused"); return 1; }
    setup_born_lines(&tank, g, l1, sizeof l1, &l2);
    V6P("given: uid same %d, from_lab %d, born lines '%s %s', family row '%s', story FROM uid %08x",
        tank.fish[g].uid == uid, progression_from_lab(&tank, g), l1, l2, setup_from_name(&tank, g, 0), (unsigned)progression_fish_from(&tank, &tank.fish[g]));
    return 0;
}
static int v6_evo(void) {
    v6_setup();
    fish_t *f = &tank.fish[0];
    progression_set_age(&tank, 0, EVO_AGE + 5); f->evo = 0; f->evo_offer = 0; tank_evo_roll(&tank, f);
    if (!progression_evo_keep(&tank, 0, 1)) { V6P("evo_keep refused"); return 1; }
    const int part = f->evo;
    V6P("paragon: stage %d evo %d (%s) ready %d value %d", f->stage, f->evo, tank_evo_name(f->evo), tank_evo_ready(f), progression_fish_value(&tank, 0));
    passport_t pp; passport_from_fish(&pp, f, tank.uid, (float)(EVO_AGE + 5));
    uint8_t buf[PASSPORT_MAX_BYTES]; int n = passport_write(&pp, buf, sizeof buf);
    if (!getenv("V6_PP_HEX")) { V6P("the 0.4.3 passport it leaves with (%d bytes, evo word %04x):", n, pp.evo); v6_hex(buf, n); }
    passport_t back;
    const char *hx = getenv("V6_PP_HEX");
    if (hx) {                                            /* what a 0.4.2 tank sent back (v6old's output) */
        uint8_t b2[PASSPORT_MAX_BYTES]; int m = 0;
        for (; hx[2 * m] && hx[2 * m + 1] && m < (int)sizeof b2; m++) b2[m] = (uint8_t)(v6_hexval(hx[2 * m]) << 4 | v6_hexval(hx[2 * m + 1]));
        if (!passport_read(&back, b2, m)) { V6P("the 0.4.2 passport does not read"); return 1; }
        V6P("the 0.4.2 passport back: %d bytes, version %d, evo word %04x, uid same %d, stage %d", m, b2[2], back.evo, back.uid == f->uid, back.stage);
    } else { if (!passport_read(&back, buf, n)) return 1; back.evo = 0; V6P("(no V6_PP_HEX: the word zeroed in place, as a v2 passport reads)"); }
    if (!progression_give_out(&tank, 0)) { V6P("give_out refused"); return 1; }
    back.home_tank = 0x5eed1234u;
    int g = progression_gift_adopt(&tank, &back);
    if (g < 0) { V6P("adopt refused"); return 1; }
    f = &tank.fish[g];
    V6P("home again: evo %d (was %d), ready %d, value %d", f->evo, part, tank_evo_ready(f), progression_fish_value(&tank, g));
    f->hunger = 2;
    for (int i = 0; i < 60; i++) { tank_tick(&tank, 1.0f / 60.0f, advisor_rules); progression_tick(&tank, 1.0f / 60.0f); }
    V6P("a second later (light %s): evo %d, READY TO EVOLVE %d, offers %d %d %d", tank_light_off(&tank) ? "off" : "on", f->evo, tank_evo_ready(f),
        f->evo_offer & 31, f->evo_offer >> 5 & 31, f->evo_offer >> 10 & 31);
    return 0;
}
static int v6_fry(void) {
    v6_setup();
    /* an honest host: a fry born to a paragon parent (tank_add_fish's tank_evo_family) - what its passport carries */
    {   fish_t *pa = &tank.fish[0]; pa->evo = 3;
        int honest_max = 0, n3 = 0;
        for (int k = 0; k < 400; k++) {
            fish_t fry; memset(&fry, 0, sizeof fry);
            tank_evo_family(&tank, &fry, pa, &tank.fish[1]);
            int places = !!(fry.evo_offer & 31) + !!(fry.evo_offer >> 5 & 31) + !!(fry.evo_offer >> 10 & 31);
            if (places > honest_max) honest_max = places; n3 += tank_evo_ready(&fry);
        }
        tank.fish[1].evo = 5;
        for (int k = 0; k < 400; k++) {
            fish_t fry; memset(&fry, 0, sizeof fry);
            tank_evo_family(&tank, &fry, pa, &tank.fish[1]);
            int places = !!(fry.evo_offer & 31) + !!(fry.evo_offer >> 5 & 31) + !!(fry.evo_offer >> 10 & 31);
            if (places > honest_max) honest_max = places; n3 += tank_evo_ready(&fry);
        }
        V6P("honest host, one or two paragon parents, 800 fry: most family places %d, READY fry %d", honest_max, n3);
        pa->evo = 0; tank.fish[1].evo = 0;
    }
    for (int c = 0; c < 2; c++) {
        passport_t pp; passport_from_fish(&pp, &tank.fish[1], 0x5eed1234u, 0);
        pp.uid = 0x77700000u + c; pp.stage = STAGE_FRY;
        pp.parent_uid[0] = tank.fish[0].uid; pp.parent_uid[1] = 0x5eed9999u;
        pp.evo = c == 0 ? (uint16_t)(7 | 31u << 5 | 31u << 10) : (uint16_t)(2 | 4 << 5 | 6 << 10);   /* a part worn / three offered */
        uint8_t buf[PASSPORT_MAX_BYTES]; int n = passport_write(&pp, buf, sizeof buf);
        passport_t rd; if (!passport_read(&rd, buf, n)) { V6P("read failed"); return 1; }
        int s = progression_fry_adopt(&tank, &rd, 0);
        if (s < 0) { V6P("fry_adopt refused"); return 1; }
        fish_t *f = &tank.fish[s];
        V6P("modified host case %s: fry stage %d, evo %d (%s), READY %d, sells for %d (a cross-tank fry is %d)", c ? "three offers" : "a part",
            f->stage, f->evo, f->evo ? tank_evo_name(f->evo) : "-", tank_evo_ready(f), progression_fish_value(&tank, s), SD_FISH_FRY * 2);
        progression_give_out(&tank, s);
    }
    return 0;
}
/* SLB-5: pixels of one fish drawn alone in the tank (diff against the tank without it) */
static int v6_px(fish_t *who, int *w, int *h, double *us) {
    static uint16_t a[TANK_W * TANK_H], b[TANK_W * TANK_H];
    const int keep = tank.n_fish;
    tank.n_fish = 0; render_frame_begin(); render_tank(&tank, a, TANK_W);
    tank.n_fish = 1; tank.fish[0] = *who;
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < 20; i++) { render_frame_begin(); render_tank(&tank, b, TANK_W); }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    *us = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3) / 20;
    int n = 0, x0 = TANK_W, x1 = -1, y0 = TANK_H, y1 = -1;
    for (int i = 0; i < TANK_W * TANK_H; i++) if (a[i] != b[i]) { int x = i % TANK_W, y = i / TANK_W; n++; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    *w = x1 - x0 + 1; *h = y1 - y0 + 1;
    tank.n_fish = keep;
    return n;
}
static int v6_big(void) {
    v6_setup();
    {   fish_t f = tank.fish[0]; float l, d;
        tank_set_body(&f, 21, 15); tank_body(&f, &l, &d); V6P("creator/setter for 21 x 15 gives %.1f x %.1f (area %.0f; BODY_AREA_MAX %.1f)", l, d, l * d, BODY_AREA_MAX);
        tank_set_body(&f, 13, 6.5f); tank_body(&f, &l, &d); V6P("creator/setter for 13 x 6.5 gives %.1f x %.1f (area %.0f; BODY_AREA_MIN %.1f)", l, d, l * d, BODY_AREA_MIN); }
    static const float BL[4][2] = { { 21, 15 }, { 13, 6.5f }, { 21, 11.7f }, { 13, 15 } };
    static const char *const NM[4] = { "air 21x15", "air 13x6.5", "creator max 21x11.7", "BULBOUS 13x15" };
    for (int c = 0; c < 4; c++) {
        passport_t pp; passport_from_fish(&pp, &tank.fish[0], 0x5eed1234u, (float)(EVO_AGE + 5));
        pp.uid = 0x88800000u + c; pp.stage = STAGE_ELDER; pp.size = 1.0f;
        pp.stretch[0] = (uint8_t)(BL[c][0] * BODY_Q + 0.5f); pp.stretch[1] = (uint8_t)(BL[c][1] * BODY_Q + 0.5f);
        pp.stretch[2] = pp.stretch[3] = 0;
        pp.stretch[4] = pp.stretch[5] = pp.stretch[6] = (uint8_t)(1 + FINL_MAX * FINL_Q);          /* the longest fins too */
        pp.stretch[7] = pp.stretch[8] = pp.stretch[9] = 0;
        pp.fins = FINS_VEIL;
        uint8_t buf[PASSPORT_MAX_BYTES]; int n = passport_write(&pp, buf, sizeof buf);
        passport_t rd; if (!passport_read(&rd, buf, n)) { V6P("%s: refused on read", NM[c]); continue; }
        fish_t f = tank.fish[0]; passport_to_fish(&rd, &f);
        f.x = TANK_W * 0.5f; f.y = TANK_H * 0.45f; f.heading = 0; f.away = false; f.size = 1.0f;
        float l, d; tank_body(&f, &l, &d);
        int w, h; double us; int px = v6_px(&f, &w, &h, &us);
        V6P("%-20s reads %.1f x %.1f (area %3.0f) shape %-8s: %5d px, box %3d x %3d, render_tank %.0f us (one fish, host)", NM[c], l, d, l * d, SHAPE_NAMES[f.shape], px, w, h, us);
    }
    /* the reveal page's held boxes (K4b of --selftest-evo-kept) with the air's biggest body, ready to evolve */
    for (int c = 0; c < 2; c++) {
        fish_t *f = &tank.fish[0];
        tank_clear_body(f); f->body[0] = (uint8_t)(BL[c][0] * BODY_Q + 0.5f); f->body[1] = (uint8_t)(BL[c][1] * BODY_Q + 0.5f);
        f->fins = FINS_VEIL; f->finl[0] = f->finl[1] = f->finl[2] = (uint8_t)(1 + FINL_MAX * FINL_Q);
        progression_set_age(&tank, 0, EVO_AGE + 5); f->evo = 0; f->evo_offer = 0; tank_evo_roll(&tank, f);
        f->evo_offer &= 0x7fff;
        setup_cancel(&tank); (void)setup_take_renamed();
        setup_begin_evolve(&tank, 0);
        if (!setup_is_evolve()) { V6P("the reveal did not open"); return 1; }
        const double t2 = tank.clock; int bad = 0, first_x = -1, first_y = -1;
        static const double AT[4] = { 0.3, 2.0, 7.5, 13 };
        for (int a = 0; a < 4; a++) {
            tank.clock = t2 + AT[a]; ek_frame(0); ek_frame(0);
            for (int i = 0; i < 8; i++) {
                ek_frame(0.07); ek_frame(0);
                render_setup(&tank, ek_ref, TANK_W, tank.clock);
                int x = 0, y = 0, d = ek_diff(ek_fb[ek_n & 1], ek_ref, &x, &y);
                if (d && first_x < 0) { first_x = x; first_y = y; }
                bad += d;
                ek_frame(0); ek_frame(0); ek_frame(0.07);
            }
            if (a == 3) {                                /* CHOOSE ONE: try each part on */
                for (int k = 0; k < 3; k++) {
                    ek_tap(EVP_TILE_X(k) + EVP_TILE_W / 2, EVP_TILE_Y + EVP_TILE_H / 2);
                    for (int i = 0; i < 6; i++) ek_frame(1.0 / 30);
                    render_setup(&tank, ek_ref, TANK_W, tank.clock);
                    int x = 0, y = 0, d = ek_diff(ek_fb[ek_n & 1], ek_ref, &x, &y);
                    if (d && first_x < 0) { first_x = x; first_y = y; }
                    bad += d; ek_frame(0); ek_frame(0);
                }
            }
        }
        V6P("reveal page, %s body, veil fins at 1.5: held vs whole differ in %d px (first at %d,%d)", NM[c], bad, first_x, first_y);
        setup_cancel(&tank); (void)setup_take_renamed();
    }
    return 0;
}
static int selftest_v6(const char *which) {
    if (!which) which = "";
    if (!strcmp(which, "lab")) return v6_lab();
    if (!strcmp(which, "evo")) return v6_evo();
    if (!strcmp(which, "fry")) return v6_fry();
    if (!strcmp(which, "big")) return v6_big();
    printf("selftest-v6: lab | evo | fry | big\n"); return 2;
}
