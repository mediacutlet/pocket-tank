/* stretch_test.inc - --selftest-stretch (0.4.3 item 10, docs/fish-creator-0.4.3.md): THE BODY AND FIN VALUES.
 *   S1  the three named bodies through the values are the table's rows (under a thousandth of a px); a fish with no
 *       values reads its row itself
 *   S2  the swim feel at a named body's values is the named body's (turn, tail beat, trail), with every fin set; between
 *       two names it lies between theirs
 *   S3  the limits: clamped, the two corners trimmed, the name follows the values; a fin length's ends (CLASSIC's bottom: none)
 *   S4  heredity: a fry's values are one parent's copy within the drift, never a blend; a mutation shows a named body;
 *       a family nobody stretched breeds fry with no values
 *   S5  the passport: 122 bytes, version 4, the ten bytes there and back; a broken pair or triple is none; v3's 112 bytes
 *       carry none and still read
 *   S6  the save: 2280 B, the values back by the fish's ID after a restart, a plain fish's stay none
 *   S7  the travel bowl: out and in again with every value
 *   S8  the save's neighbours: a 2216-byte save loads with no values; a longer save's extra bytes are written back as they were */
#define ST_FAIL(...) do { printf("FAIL: selftest-stretch: " __VA_ARGS__); printf("\n"); return 1; } while (0)
static int selftest_stretch(void) {
    /* S1 */
    float worst = render_debug_stretch_worst();
    if (worst > 0.001f) ST_FAIL("S1: a named body through the values is %.6f px off its row", worst);
    /* S2 */
    tank_init(&tank, 4343); forms_cast(4);
    for (int sh = 0; sh < SHAPE_N; sh++) for (int fi = 0; fi < FINS_N; fi++) for (int st = STAGE_JUV; st <= STAGE_ELDER; st++) {
        fish_t a = tank.fish[0], b;
        a.shape = (uint8_t)sh; a.fins = (uint8_t)fi; a.stage = (stage_t)st; a.speed = 40; tank_clear_body(&a); tank_clear_fin_len(&a);
        b = a; tank_set_body(&b, BODY_LEN[sh], BODY_DEP[sh]); tank_set_fin_len(&b, 0, 1);
        if (b.shape != sh || !tank_body_set(&b) || !tank_finl_set(&b)) ST_FAIL("S2: %s by its values is called %s", SHAPE_NAMES[sh], SHAPE_NAMES[b.shape]);
        if (fabsf(tank_form_turn(&a) - tank_form_turn(&b)) > 1e-4f || fabsf(tank_fish_beat_rate(&a) - tank_fish_beat_rate(&b)) > 1e-3f
            || fabsf(tank_form_trail(&a) - tank_form_trail(&b)) > 1e-4f)
            ST_FAIL("S2: %s x %s stage %d: turn %.4f / %.4f, beat %.4f / %.4f, trail %.4f / %.4f", SHAPE_NAMES[sh], FINS_NAMES[fi], st,
                    tank_form_turn(&a), tank_form_turn(&b), tank_fish_beat_rate(&a), tank_fish_beat_rate(&b), tank_form_trail(&a), tank_form_trail(&b));
    }
    {   fish_t m = tank.fish[0], c = m, s = m; m.fins = c.fins = s.fins = FINS_CLASSIC; m.speed = c.speed = s.speed = 40;
        tank_clear_fin_len(&m); tank_clear_fin_len(&c); tank_clear_fin_len(&s); tank_clear_body(&c); tank_clear_body(&s);
        c.shape = SHAPE_CLASSIC; s.shape = SHAPE_SLENDER; tank_set_body(&m, 19, 8.2f);
        if (!(tank_form_turn(&m) > tank_form_turn(&c) && tank_form_turn(&m) < tank_form_turn(&s))) ST_FAIL("S2: between classic and slender turns %.4f (%.4f .. %.4f)", tank_form_turn(&m), tank_form_turn(&c), tank_form_turn(&s));
        fish_t v = c; v.fins = FINS_VEIL; v.stage = STAGE_ADULT; fish_t vl = v; tank_set_fin_len(&vl, 0, 1.5f);
        if (!(tank_form_trail(&vl) > tank_form_trail(&v) && tank_form_turn(&vl) < tank_form_turn(&v) && tank_fish_beat_rate(&vl) < tank_fish_beat_rate(&v)))
            ST_FAIL("S2: a longer veil does not drag more");
        if (tank_form_turn(&vl) < 0.85f || tank_form_turn(&m) > 1.2f) ST_FAIL("S2: the feel left 0.85 .. 1.20");
    }
    /* S3 */
    {   fish_t f = tank.fish[0]; float l, d;
        tank_set_body(&f, 99, 99); tank_body(&f, &l, &d);
        if (l != BODY_LEN_MAX || l * d > BODY_AREA_MAX + 2.2f) ST_FAIL("S3: the biggest body is %.1f x %.1f (area %.0f, at most %.0f)", l, d, l * d, BODY_AREA_MAX);
        tank_set_body(&f, 0, 0); tank_body(&f, &l, &d);
        if (l != BODY_LEN_MIN || l * d < BODY_AREA_MIN - 1.4f) ST_FAIL("S3: the smallest body is %.1f x %.1f (area %.0f, at least %.0f)", l, d, l * d, BODY_AREA_MIN);
        tank_set_body(&f, 21, 6.5f); if (f.shape != SHAPE_SLENDER) ST_FAIL("S3: 21 x 6.5 is called %s", SHAPE_NAMES[f.shape]);
        tank_set_body(&f, 13, 15);   if (f.shape != SHAPE_BULBOUS) ST_FAIL("S3: 13 x 15 is called %s", SHAPE_NAMES[f.shape]);
        tank_set_body(&f, 17.5f, 10.5f); if (f.shape != SHAPE_CLASSIC) ST_FAIL("S3: 17.5 x 10.5 is called %s", SHAPE_NAMES[f.shape]);
        float fl[3];
        f.fins = FINS_CLASSIC; tank_clear_fin_len(&f); tank_fin_len(&f, fl); if (fl[0] != 1 || fl[1] != 1 || fl[2] != 0) ST_FAIL("S3: CLASSIC's own lengths %.2f %.2f %.2f", fl[0], fl[1], fl[2]);
        tank_set_fin_len(&f, 2, -5); tank_set_fin_len(&f, 0, 9); tank_fin_len(&f, fl); if (fl[0] != FINL_MAX || fl[1] != 1 || fl[2] != 0) ST_FAIL("S3: clamped to %.2f %.2f %.2f", fl[0], fl[1], fl[2]);
        f.fins = FINS_SAIL; tank_clear_fin_len(&f); tank_set_fin_len(&f, 2, 0); tank_fin_len(&f, fl); if (fl[2] != FINL_MIN) ST_FAIL("S3: a SAIL's bottom fin went to %.2f", fl[2]);
    }
    /* S4 */
    {   tank_init(&tank, 99); tank.n_fish = 2;
        fish_t *pa = &tank.fish[0], *pb = &tank.fish[1];
        tank_set_body(pa, 20, 9); tank_set_body(pb, 14, 13);
        pa->body_c[0] = 180; pa->body_c[1] = 80; pa->carried[GENE_SHAPE] = (uint8_t)tank_body_nearest(18, 8);
        pb->body_c[0] = pb->body_c[1] = 0; pb->carried[GENE_SHAPE] = SHAPE_CLASSIC;
        pa->fins = FINS_VEIL; tank_set_fin_len(pa, 0, 1.4f); pb->fins = FINS_SAIL; tank_clear_fin_len(pb);
        static const float SRC[3][2] = { { 20, 9 }, { 14, 13 }, { 18, 8 } };
        int from[4] = {0}, named = 0, mut = 0;
        for (int k = 0; k < 3000; k++) {
            tank.n_fish = 2; if (k % 40 == 39) tank.mutate_next = true;
            int s = tank_add_fish(&tank, 0, 1); if (s < 0) ST_FAIL("S4: no fry");
            const fish_t *f = &tank.fish[s];
            if (f->mutated == GENE_SHAPE + 1) { mut++; if (tank_body_set(f)) ST_FAIL("S4: a body mutation kept values"); continue; }
            if (!tank_body_set(f)) { named++; if (f->shape != SHAPE_CLASSIC) ST_FAIL("S4: a fry with no values is %s (the only named copy is CLASSIC)", SHAPE_NAMES[f->shape]); continue; }
            float l, d; tank_body(f, &l, &d); int hit = -1;
            for (int j = 0; j < 3; j++) if (fabsf(l / SRC[j][0] - 1) <= BODY_DRIFT + 0.006f && fabsf(d / SRC[j][1] - 1) <= BODY_DRIFT + 0.012f) hit = j;
            if (hit < 0) ST_FAIL("S4: fry %d is %.1f x %.1f: nobody's copy (a blend?)", k, l, d);
            from[hit]++;
            if (f->shape != tank_body_nearest(l, d)) ST_FAIL("S4: the fry's name does not follow its body");
            if (f->fins == FINS_VEIL && f->mutated != GENE_FINS + 1 && !(f->from_carried >> GENE_FINS & 1)) {
                float fl[3]; tank_fin_len(f, fl);
                if (tank_finl_set(f) && fabsf(fl[0] / 1.4f - 1) > BODY_DRIFT + 0.01f) ST_FAIL("S4: a veil fry's tail is %.2f (the parent's 1.40)", fl[0]);
            }
        }
        if (!from[0] || !from[1] || !from[2] || !named || !mut) ST_FAIL("S4: copies shown %d / %d / %d, named %d, mutations %d: one never came down", from[0], from[1], from[2], named, mut);
        tank_init(&tank, 99); tank.n_fish = 2;
        for (int i = 0; i < 2; i++) { fish_t *q = &tank.fish[i]; tank_clear_body(q); tank_clear_fin_len(q); memset(q->body_c, 0, 2); memset(q->finl_c, 0, 3); }   /* (tank_init leaves no fish: these two are the last block's) */
        for (int k = 0; k < 500; k++) { tank.n_fish = 2; int s = tank_add_fish(&tank, 0, 1); const fish_t *f = &tank.fish[s];
            if (tank_body_set(f) || tank_finl_set(f) || f->body_c[0] || f->finl_c[0]) ST_FAIL("S4: plain parents bred a fry with values"); }
    }
    /* S5 */
    {   tank_init(&tank, 4343); forms_cast(4);
        fish_t a = tank.fish[0], g; a.fins = FINS_SAIL; tank_set_body(&a, 19.5f, 11); tank_set_fin_len(&a, 1, 1.3f);
        a.body_c[0] = 140; a.body_c[1] = 140; a.finl_c[0] = 51; a.finl_c[1] = 101; a.finl_c[2] = 151;
        passport_t pp, qq; uint8_t buf[PASSPORT_MAX_BYTES];
        passport_from_fish(&pp, &a, 0x1234u, 1000);
        int n = passport_write(&pp, buf, sizeof buf);
        if (n != PASSPORT_V5_BYTES || n != 123 || buf[2] != 5 || !passport_read(&qq, buf, n)) ST_FAIL("S5: the passport: %d bytes, version %d", n, buf[2]);
        g = tank.fish[1]; passport_to_fish(&qq, &g);
        if (memcmp(g.body, a.body, 2) || memcmp(g.body_c, a.body_c, 2) || memcmp(g.finl, a.finl, 3) || memcmp(g.finl_c, a.finl_c, 3)) ST_FAIL("S5: the passport changed the values");
        if (g.shape != a.shape || g.carried[GENE_SHAPE] != SHAPE_BULBOUS) ST_FAIL("S5: the names did not follow (%d, carried %d)", g.shape, g.carried[GENE_SHAPE]);
        n = passport_write_v3(&pp, buf, sizeof buf);
        if (n != 112 || buf[2] != 3 || !passport_read(&qq, buf, n)) ST_FAIL("S5: v3's form: %d bytes, version %d", n, buf[2]);
        g = tank.fish[1]; passport_to_fish(&qq, &g);
        if (tank_body_set(&g) || tank_finl_set(&g) || g.body_c[0] || g.finl_c[0] || g.shape != a.shape) ST_FAIL("S5: v3's form carried values, or lost the name");
        uint8_t bad[PASSPORT_STRETCH] = { 200, 0, 255, 255, 60, 0, 90, 200, 100, 100 };
        memcpy(pp.stretch, bad, sizeof bad); n = passport_write(&pp, buf, sizeof buf);
        if (!passport_read(&qq, buf, n)) ST_FAIL("S5: a passport with broken values was refused");
        for (int i = 0; i < PASSPORT_STRETCH; i++) if (qq.stretch[i]) ST_FAIL("S5: broken values got through (byte %d = %d)", i, qq.stretch[i]);
    }
    /* S6 */
    {   shelf_wipe(); shelf_reboot();
        tank_init(&tank, 4343); forms_cast(4);
        fish_t *f = &tank.fish[2]; uint32_t uid = f->uid; f->fins = FINS_VEIL;
        tank_set_body(f, 15.2f, 12.4f); tank_set_fin_len(f, 0, 0.7f); tank_set_fin_len(f, 2, 1.45f);
        f->body_c[0] = 205; f->body_c[1] = 70; f->carried[GENE_SHAPE] = SHAPE_SLENDER;
        fish_t was = *f;
        if (!progression_save(&tank)) ST_FAIL("S6: the save refused");
        shelf_reboot();
        int found = 0;
        for (int i = 0; i < tank.n_fish; i++) {
            const fish_t *q = &tank.fish[i];
            if (q->uid == uid) { found++;
                if (memcmp(q->body, was.body, 2) || memcmp(q->body_c, was.body_c, 2) || memcmp(q->finl, was.finl, 3) || memcmp(q->finl_c, was.finl_c, 3) || q->shape != was.shape)
                    ST_FAIL("S6: the save changed the values (%d %d / %d %d %d, %s)", q->body[0], q->body[1], q->finl[0], q->finl[1], q->finl[2], SHAPE_NAMES[q->shape]); }
            else if (tank_body_set(q) || tank_finl_set(q) || q->body_c[0] || q->finl_c[0]) ST_FAIL("S6: a plain fish came back with values");
        }
        if (found != 1) ST_FAIL("S6: the stretched fish did not come back");
        /* S7 */
        int idx = -1; for (int i = 0; i < tank.n_fish; i++) if (tank.fish[i].uid == uid) idx = i;
        int why = progression_move_out(&tank, idx);
        if (why != 0) ST_FAIL("S7: the bowl refused the fish (%d)", why);
        for (int i = 0; i < tank.n_fish; i++) if (tank.fish[i].uid == uid) ST_FAIL("S7: the fish is still in the tank");
        shelf_reboot();
        if (!progression_bowl_has()) ST_FAIL("S7: nothing in the bowl after a restart");
        why = progression_bowl_let_in(&tank);
        if (why != 0) ST_FAIL("S7: LET IN refused (%d)", why);
        found = 0;
        for (int i = 0; i < tank.n_fish; i++) { const fish_t *q = &tank.fish[i];
            if (q->uid == uid) { found++;
                if (memcmp(q->body, was.body, 2) || memcmp(q->body_c, was.body_c, 2) || memcmp(q->finl, was.finl, 3) || memcmp(q->finl_c, was.finl_c, 3) || q->shape != was.shape)
                    ST_FAIL("S7: the bowl changed the values (%d %d / %d %d %d)", q->body[0], q->body[1], q->finl[0], q->finl[1], q->finl[2]); } }
        if (found != 1) ST_FAIL("S7: the fish did not come out of the bowl");
    }
    /* S8 (the save's neighbours, docs/release-0.4.3.md "Save bytes: who appends where"): a 2216-byte save - the paragon's,
       before this tail - loads with every fish's values not set; a save LONGER than ours - a later build's tail past 2304 (the octopus's 2288..2303 since 0.4.3) -
       is written back with those bytes as they were, and our own tail under them is still read */
#ifndef TANK_P4   /* (the big screens' save is laid out past 2560 behind its fish tail: --selftest-ten checks it, ten fish's values there and back) */
    {   tank_init(&tank, 4343); forms_cast(4); tank_ensure_ids(&tank);
        const char *sav = "/tmp/pocket-tank-selftest-stretch.sav"; setenv("POCKET_TANK_SAVE", sav, 1); remove(sav);
        fish_t *f = &tank.fish[1]; const uint32_t uid = f->uid; f->fins = FINS_SAIL;
        tank_set_body(f, 20.3f, 7.4f); tank_set_fin_len(f, 1, 1.4f);
        const fish_t was = *f;
        if (!progression_save(&tank)) ST_FAIL("S8: the save refused");
        static uint8_t b[4096], b2[4096]; FILE *fp = fopen(sav, "rb"); size_t n = fp ? fread(b, 1, sizeof b, fp) : 0; if (fp) fclose(fp);
        if (n != 2304) ST_FAIL("S8: the save is %zu B (want 2304)", n);
        fp = fopen(sav, "wb"); fwrite(b, 1, 2216, fp); fclose(fp);
        tank_init(&tank, 9); progression_wake(&tank, 0);
        int found = 0;
        for (int i = 0; i < tank.n_fish; i++) { const fish_t *q = &tank.fish[i]; found += q->uid == uid;
            if (tank_body_set(q) || tank_finl_set(q) || q->body_c[0] || q->finl_c[0]) ST_FAIL("S8: a 2216-byte save gave fish %d values", i);
            if (q->uid == uid && (q->shape != was.shape || q->fins != was.fins)) ST_FAIL("S8: a 2216-byte save lost the fish's named body or fins (%d / %d)", q->shape, q->fins); }
        if (found != 1) ST_FAIL("S8: the fish did not load from a 2216-byte save");
        for (int i = 0; i < 24; i++) b[2304 + i] = (uint8_t)(0xC0 + i);
        fp = fopen(sav, "wb"); fwrite(b, 1, 2328, fp); fclose(fp);
        tank_init(&tank, 9); progression_wake(&tank, 0);
        found = 0;
        for (int i = 0; i < tank.n_fish; i++) if (tank.fish[i].uid == uid) { found++;
            if (memcmp(tank.fish[i].body, was.body, 2) || memcmp(tank.fish[i].finl, was.finl, 3)) ST_FAIL("S8: under a later build's tail the values did not load"); }
        if (found != 1) ST_FAIL("S8: the fish did not load from a longer save");
        if (!progression_save(&tank)) ST_FAIL("S8: the longer save refused");
        fp = fopen(sav, "rb"); size_t n2 = fp ? fread(b2, 1, sizeof b2, fp) : 0; if (fp) fclose(fp);
        if (n2 != 2328 || memcmp(b + 2304, b2 + 2304, 24)) ST_FAIL("S8: a later build's tail was not written back as it was (%zu B)", n2);
        if (memcmp(b + 2216, b2 + 2216, 64)) ST_FAIL("S8: our own tail changed under a later build's");
        remove(sav);
    }
#endif
    printf("selftest-stretch: PASS (the named bodies %.6f px from their rows; feel, limits, heredity, passport v4, the save, the bowl, a shorter and a longer save)\n", worst);
    return 0;
}

/* --selftest-lab (0.4.3 item 10, THE FISH LAB): the shop's row and modal answer UNLOCK then OPEN; 1500 buys it once; the
 * pages open on a fish that is not in the tank; POUR IT IN is refused short of 250 and in a full tank; then the lab closes
 * and the fry drops in from the top - its look, its name, 250 paid, FROM the lab, no welcome owed, its carried copies rolled;
 * BACK on the first page spends nothing; the save keeps the unlock and the fry */
static int selftest_lab(void) {
    shelf_wipe(); shelf_reboot();
    tank_init(&tank, 4343); forms_cast(3); tank_ensure_ids(&tank);
    while (progression_newborn() >= 0) progression_newborn_done(&tank);
    progression_fry_open(&tank);
    tank.sd_balance = 1499;
    if (progression_lab_owned(&tank) || progression_lab_buy(&tank) || progression_lab_why(&tank) != LAB_NOT_OWNED) ST_FAIL("L1: 1499 bought the lab");
    setup_begin_lab(&tank); if (setup_active()) ST_FAIL("L1: the lab opened unowned");
    int row = shop_goto(SHOP_ROW_LAB); if (row < 0) ST_FAIL("L1: no FISH LAB row in the shop");
    int r = shop_tap(SHOP_ROW_X, SHP_ROW_Y0 + 20 + SHP_ROW_DY * row); SHOP_SETTLE();
    if ((r = shop_tap(SHOP_BTN_X, SHOP_BTN_Y)) != SHOP_TAP_KEPT) ST_FAIL("L1: UNLOCK short of the price answered %d", r);
    tank.sd_balance = 1600;
    shop_goto(SHOP_ROW_LAB); shop_tap(SHOP_ROW_X, SHP_ROW_Y0 + 20 + SHP_ROW_DY * row); SHOP_SETTLE();
    if ((r = shop_tap(SHOP_BTN_X, SHOP_BTN_Y)) != SHOP_TAP_BUY + SHOP_ITEM_LAB) ST_FAIL("L1: UNLOCK answered %d", r);
    if (!progression_lab_buy(&tank) || tank.sd_balance != 100 || progression_lab_buy(&tank) || tank.sd_balance != 100) ST_FAIL("L1: the unlock: balance %d", tank.sd_balance);
    shop_goto(SHOP_ROW_LAB); shop_tap(SHOP_ROW_X, SHP_ROW_Y0 + 20 + SHP_ROW_DY * row); SHOP_SETTLE();
    if ((r = shop_tap(SHOP_BTN_X, SHOP_BTN_Y)) != SHOP_TAP_KEPT) ST_FAIL("L1: OPEN short of a fry's price answered %d (the card says YOU HAVE)", r);
    tank.sd_balance = 400;
    {   const int keep = tank.n_fish;                      /* a full tank: the card says so, OPEN stands back */
        while (tank.n_fish < POP_CAP) { tank_make_fish(&tank, tank.n_fish, tank.n_fish % tank_roster_count(), 0.5f, 0.5f, STAGE_ADULT); tank.n_fish++; }
        shop_goto(SHOP_ROW_LAB); shop_tap(SHOP_ROW_X, SHP_ROW_Y0 + 20 + SHP_ROW_DY * row); SHOP_SETTLE();
        if ((r = shop_tap(SHOP_BTN_X, SHOP_BTN_Y)) != SHOP_TAP_KEPT) ST_FAIL("L1: OPEN in a full tank answered %d", r);
        tank.n_fish = keep; }
    shop_goto(SHOP_ROW_LAB); shop_tap(SHOP_ROW_X, SHP_ROW_Y0 + 20 + SHP_ROW_DY * row); SHOP_SETTLE();
    if ((r = shop_tap(SHOP_BTN_X, SHOP_BTN_Y)) != SHOP_TAP_LAB) ST_FAIL("L1: OPEN answered %d", r);
    render_shop_leave(); tank.sd_balance = 100;
    /* L2: the pages */
    const int n0 = tank.n_fish;
    setup_begin_lab(&tank);
    if (!setup_is_lab() || setup_page() != SETUP_PG_LAB_BODY || tank.stage_fish != -1 || tank.n_fish != n0) ST_FAIL("L2: the lab did not open on BODY (page %d)", setup_page());
    setup_activate(&tank, SETUP_HIT_BACK);
    if (setup_active() || tank.sd_balance != 100 || tank.n_fish != n0) ST_FAIL("L2: BACK on the first page did not leave clean");
    setup_begin_lab(&tank);
    pg_touch(200, 200, true); for (int i = 1; i <= 38; i++) pg_touch(200 + i, 200, true); pg_touch(238, 200, false);   /* 30 px longer: 19 */
    setup_activate(&tank, SETUP_HIT_NEXT); setup_activate(&tank, SETUP_HIT_FINS0 + FINS_VEIL);
    setup_activate(&tank, SETUP_HIT_NEXT); setup_activate(&tank, SETUP_HIT_BODY0 + 5); setup_activate(&tank, SETUP_HIT_PAT0 + PATTERN_MASK);
    setup_activate(&tank, SETUP_HIT_NEXT);
    if (setup_page() != SETUP_PG_LAB_NAME) ST_FAIL("L2: not on the name page (%d)", setup_page());
    snprintf(setup_lab_fish()->name, FISH_NAME_MAX + 1, "pip");
    setup_activate(&tank, SETUP_HIT_NEXT);
    if (setup_page() != SETUP_PG_LAB_TUBE) ST_FAIL("L2: not at the tube (%d)", setup_page());
    if (pg_hit(SETUP_LAB_BTN_X + 20, SETUP_LAB_BTN_Y + 20) != SETUP_HIT_POUR || pg_hit(SETUP_TOP_BACK_X + 10, SETUP_TOP_BTN_Y + 10) != SETUP_HIT_BACK) ST_FAIL("L2: the tube's hit-test");
    /* L3: refused short, refused full */
    if (progression_lab_why(&tank) != LAB_SHORT) ST_FAIL("L3: 100 sand dollars is %d (want SHORT)", progression_lab_why(&tank));
    pg_touch(SETUP_LAB_BTN_X + 20, SETUP_LAB_BTN_Y + 20, true); pg_touch(SETUP_LAB_BTN_X + 20, SETUP_LAB_BTN_Y + 20, false);
    if (setup_page() != SETUP_PG_LAB_TUBE || tank.n_fish != n0) ST_FAIL("L3: POUR went ahead short of the price");
    tank.sd_balance = 400;
    {   const int keep = tank.n_fish;
        while (tank.n_fish < POP_CAP) { tank_make_fish(&tank, tank.n_fish, tank.n_fish % tank_roster_count(), 0.5f, 0.5f, STAGE_ADULT); tank.n_fish++; }
        memset(&tank.fish[SETUP_LAB_SLOT], 0xA5, sizeof(fish_t));          /* (a fry from next door made in the pages' slot for a moment: the lab's fish is its own copy) */
        if (progression_lab_why(&tank) != LAB_FULL) ST_FAIL("L3: a full tank is %d (want FULL)", progression_lab_why(&tank));
        setup_activate(&tank, SETUP_HIT_POUR);
        if (setup_page() != SETUP_PG_LAB_TUBE) ST_FAIL("L3: POUR went ahead in a full tank");
        tank.n_fish = keep; }
    /* L4: POUR IT IN - the lab closes at once and the fry DROPS IN from the top (tank_plop): heading down and fast, a
       splash on the waterline, the free bubbles down its plunge, and it is THE NEW ONE for PLOP_SHINE_S, then nothing */
    pg_touch(SETUP_LAB_BTN_X + 20, SETUP_LAB_BTN_Y + 20, true); pg_touch(SETUP_LAB_BTN_X + 20, SETUP_LAB_BTN_Y + 20, false);
    if (setup_active() || tank.n_fish != n0 + 1 || tank.sd_balance != 150) ST_FAIL("L4: after POUR IT IN: active %d, %d fish (want %d), balance %d (want 150)", setup_active(), tank.n_fish, n0 + 1, tank.sd_balance);
    const fish_t *f = &tank.fish[n0]; float len, dep; tank_body(f, &len, &dep);
    if (fabsf(len - 19) > 0.11f || dep != 10 || f->fins != FINS_VEIL || f->pattern != PATTERN_MASK || f->color != LOOK_BODY[5] || strcmp(f->name, "pip") || f->stage != STAGE_FRY)
        ST_FAIL("L4: the fry is %.1f x %.1f, fins %d, pattern %d, named '%s'", len, dep, f->fins, f->pattern, f->name);
    if (!progression_from_lab(&tank, n0) || progression_welcome_owed(&tank, n0) || progression_newborn() >= 0 || f->parent_uid[0] || !f->uid || f->body_c[0])
        ST_FAIL("L4: from the lab %d, welcome owed %d, newborn %d", progression_from_lab(&tank, n0), progression_welcome_owed(&tank, n0), progression_newborn());
    {   const float top = tank_glass_top(f->x);
        if (f->y > top + 12 || fabsf(f->x - TANK_W * 0.5f) > 0.19f * TANK_W || sinf(f->heading) < 0.9f || tank.plop_vy < 200)
            ST_FAIL("L4: the fry is at %.0f,%.0f heading %.2f falling at %.0f - not dropping in from the top", f->x, f->y, f->heading, tank.plop_vy);
        if (!tank_new_one(&tank, f) || tank.splash_x != f->x || tank.clock - tank.splash_at > 0.01) ST_FAIL("L4: no splash, or the fry does not shine");
        int nb = 0; for (int i = 0; i < MAX_BUBBLE; i++) nb += !tank.bubble[i].column && fabsf(tank.bubble[i].x - f->x) <= 16 && tank.bubble[i].y > f->y && tank.bubble[i].y < f->y + 80;
        if (nb < 8) ST_FAIL("L4: %d bubbles down the plunge (a plume wants more)", nb);
        const float y0 = f->y;
        for (int i = 0; i < 2 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        if (f->y < y0 + 0.2f * TANK_H || f->y > TANK_BOT - 30 || fabsf(tank.plop_vy) > 60 || !tank_new_one(&tank, f))   /* (in the water, well under the top, the plunge spent) */
            ST_FAIL("L4: 2 s on: y %.0f (from %.0f), still falling at %.0f, shining %d", f->y, y0, tank.plop_vy, tank_new_one(&tank, f));
        for (int i = 0; i < (int)(PLOP_SHINE_S * 60); i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        if (tank_new_one(&tank, f) || tank.clock - tank.splash_at < PLOP_SPLASH_S) ST_FAIL("L4: the shine or the splash never ended");
        for (int i = 0; i < tank.n_fish - 1; i++) if (tank_new_one(&tank, &tank.fish[i])) ST_FAIL("L4: another fish shines");
    }
    /* L5: the save */
    const uint32_t uid = f->uid;
    shelf_reboot();
    int at = -1; for (int i = 0; i < tank.n_fish; i++) if (tank.fish[i].uid == uid) at = i;
    if (at < 0 || !progression_lab_owned(&tank) || !progression_from_lab(&tank, at) || tank.sd_balance != 150) ST_FAIL("L5: after a restart: fish %d, owned %d, balance %d", at, progression_lab_owned(&tank), tank.sd_balance);
    tank_body(&tank.fish[at], &len, &dep); if (fabsf(len - 19) > 0.11f || strcmp(tank.fish[at].name, "pip")) ST_FAIL("L5: the fry came back %.1f long, named '%s'", len, tank.fish[at].name);
    /* L6: THE LAB KEPT (Strato 11-22: a black page like the other menus, only what changes drawn again). The lab's
       page paints every pixel itself - nothing of the tank shows; held, a frame with nothing new draws inside its
       moving box and nowhere else, and so does a drag; NAME is held too; the FIRST RUN's creator still leaves the tank showing */
    {   static uint16_t fb[TANK_W * TANK_H], was[TANK_W * TANK_H];
        const uint16_t SENT = 0xF81F;
        #define LK_FRAME() do { render_frame_begin(); tank.clock += 1.0 / 30; setup_touch(&tank, 0, 0, false); render_lab_page(&tank, fb, TANK_W, tank.clock); } while (0)
        #define LK_OUTSIDE(what) do { int bx[SETUP_LAB_BANDS][4]; int nb = setup_lab_bands(&tank, bx); \
            for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) { bool in = false; \
                for (int i = 0; i < nb; i++) in |= x - PAGE_X >= bx[i][0] && x - PAGE_X <= bx[i][2] && y - PAGE_Y >= bx[i][1] && y - PAGE_Y <= bx[i][3]; \
                if (!in && fb[y * TANK_W + x] != was[y * TANK_W + x]) ST_FAIL("L6: %s: a pixel outside the moving boxes changed (%d,%d)", what, x, y); } } while (0)
        setup_begin_lab(&tank);
        if (!setup_is_lab() || !setup_lab_black()) ST_FAIL("L6: the lab's page is not a black page of its own");
        tank_set_body(setup_lab_fish(), 18, 10);                 /* (not exactly a named body: a drag from here changes no tile's frame) */
        for (int i = 0; i < TANK_W * TANK_H; i++) fb[i] = SENT;
        LK_FRAME();
        for (int i = 0; i < TANK_W * TANK_H; i++) if (fb[i] == SENT) ST_FAIL("L6: the lab's page left pixel %d,%d to the tank", i % TANK_W, i / TANK_W);
        int b0, w0, b1, w1; render_lab_page_counts(&b0, &w0);
        memcpy(was, fb, sizeof fb);
        for (int k = 0; k < 12; k++) LK_FRAME();
        render_lab_page_counts(&b1, &w1);
        if (w1 != w0 || b1 != b0 + 12) ST_FAIL("L6: 12 still frames: %d whole, %d the boxes alone (want 0, 12)", w1 - w0, b1 - b0);
        LK_OUTSIDE("a still frame");
        /* THE TILES' FISH KEEP THE CLOCK (Strato on the square, 11-22: in the still page they stood frozen, and raced when
           a drag redrew it). The row is a moving box now: it changes from frame to frame with nothing touched, and the held
           page shows it exactly as a page drawn whole at that clock would - one pace for every fish on the page - idle and
           in the middle of a drag alike */
        {   static uint16_t ref[TANK_W * TANK_H], prev[TANK_W * TANK_H];
            int bx[SETUP_LAB_BANDS][4]; if (setup_lab_bands(&tank, bx) != 2) ST_FAIL("L6: BODY has %d moving boxes (want the stage and the tiles' row)", setup_lab_bands(&tank, bx));
            #define LK_ROW_SAME(a, b, n) do { n = 0; for (int y = bx[1][1]; y <= bx[1][3]; y++) for (int x = bx[1][0]; x <= bx[1][2]; x++) \
                                              n += (a)[(y + PAGE_Y) * TANK_W + x + PAGE_X] != (b)[(y + PAGE_Y) * TANK_W + x + PAGE_X]; } while (0)
            int moved = 0, d;
            for (int k = 0; k < 10; k++) { memcpy(prev, fb, sizeof fb); LK_FRAME(); LK_ROW_SAME(prev, fb, d); moved += d > 0; }
            if (moved < 8) ST_FAIL("L6: the tiles' fish moved in %d of 10 still frames (they keep the clock)", moved);
            for (int pass = 0; pass < 2; pass++) {
                if (pass) { pg_touch(200, 200, true); for (int i = 1; i <= 14; i++) { render_frame_begin(); tank.clock += 1.0 / 30; pg_touch(200 + i, 200, true); render_lab_page(&tank, fb, TANK_W, tank.clock); } }
                render_setup(&tank, ref, TANK_W, tank.clock);       /* (a buffer nobody keeps: the page whole, at this clock) */
                LK_ROW_SAME(ref, fb, d);
                if (d) ST_FAIL("L6: %s, the held page's tiles differ from a page drawn whole at the same clock (%d px)", pass ? "mid-drag" : "idle", d);
                if (pass) pg_touch(214, 200, false);
            }
            #undef LK_ROW_SAME
            LK_FRAME(); LK_FRAME(); render_lab_page_counts(&b1, &w1); memcpy(was, fb, sizeof fb);   /* (the reference draws spoiled the kept buffer: settled again) */
        }
        float l0, d0, l1, d1; tank_body(setup_lab_fish(), &l0, &d0);
        pg_touch(200, 200, true);                                /* a drag: the length, 20 px - the fish changes, and nothing outside its box is drawn */
        for (int i = 1; i <= 28; i++) { render_frame_begin(); tank.clock += 1.0 / 30; pg_touch(200 + i, 200, true); render_lab_page(&tank, fb, TANK_W, tank.clock); }
        tank_body(setup_lab_fish(), &l1, &d1);
        render_lab_page_counts(&b0, &w0);
        if (l1 < l0 + 1.0f) ST_FAIL("L6: the drag did not stretch the body (%.1f -> %.1f)", l0, l1);
        if (w0 != w1) ST_FAIL("L6: a drag drew the page whole %d time(s)", w0 - w1);
        LK_OUTSIDE("a drag");
        pg_touch(228, 200, false);
        static const int PGS[3] = { SETUP_PG_LAB_FINS, SETUP_PG_LAB_COLORS, SETUP_PG_LAB_TUBE };
        for (int p = 0; p < 3; p++) {
            setup_lab_goto(&tank, PGS[p]); LK_FRAME(); memcpy(was, fb, sizeof fb);
            render_lab_page_counts(&b0, &w0);
            for (int k = 0; k < 8; k++) LK_FRAME();
            render_lab_page_counts(&b1, &w1);
            if (w1 != w0 || b1 != b0 + 8) ST_FAIL("L6: lab page %d, 8 still frames: %d whole, %d the boxes alone", PGS[p] - SETUP_PG_LAB_BODY, w1 - w0, b1 - b0);
            LK_OUTSIDE("a held page");
        }
        /* NAME is held too (the letter wheel: its letters and its cursor's blink are in the key): over 2 s only the blink
           draws it whole - a few times - and every other frame only the two fish over the letters */
        setup_lab_goto(&tank, SETUP_PG_LAB_NAME); LK_FRAME(); render_lab_page_counts(&b0, &w0);
        for (int k = 0; k < 60; k++) LK_FRAME();
        render_lab_page_counts(&b1, &w1);
        if (w1 - w0 > 6 || b1 - b0 < 54) ST_FAIL("L6: NAME, 60 frames: %d whole, %d the boxes alone (the blink alone redraws it whole)", w1 - w0, b1 - b0);
        {   char was_n[FISH_NAME_MAX + 1]; memcpy(was_n, setup_lab_fish()->name, sizeof was_n);     /* a letter changed: whole, once, then held again */
            snprintf(setup_lab_fish()->name, FISH_NAME_MAX + 1, "zed"); render_lab_page_counts(&b0, &w0);
            LK_FRAME(); render_lab_page_counts(&b1, &w1);
            if (w1 != w0 + 1) ST_FAIL("L6: a new letter did not draw NAME whole");
            memcpy(setup_lab_fish()->name, was_n, sizeof was_n); LK_FRAME(); }
        for (int i = 0; i < TANK_W * TANK_H; i++) fb[i] = SENT;
        snprintf(setup_lab_fish()->name, FISH_NAME_MAX + 1, "pipa");              /* (a new letter: the page whole into the scribbled buffer) */
        LK_FRAME(); for (int i = 0; i < TANK_W * TANK_H; i++) if (fb[i] == SENT) ST_FAIL("L6: NAME left a pixel to the tank");
        setup_debug_lab_live(true); if (setup_lab_black()) ST_FAIL("L6: the director's LIVE lab is still black"); setup_debug_lab_live(false);
        setup_cancel(&tank);
        if (setup_lab_black() || setup_is_lab()) ST_FAIL("L6: the lab left: still a lab page");
        setup_begin(&tank); setup_activate(&tank, SETUP_HIT_NEXT);                /* the first run's BODY page: the tank behind it, as built */
        if (setup_page() != SETUP_PG_BODY_A || setup_lab_black() || setup_is_lab()) ST_FAIL("L6: the first run's creator is a lab page");
        for (int i = 0; i < TANK_W * TANK_H; i++) fb[i] = SENT;
        render_setup(&tank, fb, TANK_W, tank.clock);
        int left = 0; for (int i = 0; i < TANK_W * TANK_H; i++) left += fb[i] == SENT;
        if (left < TANK_W * TANK_H / 20) ST_FAIL("L6: the first run's BODY page covers the tank (%d px of it left)", left);
        setup_cancel(&tank);
        #undef LK_FRAME
        #undef LK_OUTSIDE
    }
    printf("selftest-lab: PASS (the row and modal, 1500 once, the pages, refused short and full, the pour, the fry, the save; the black page held)\n");
    return 0;
}
/* --lab-shot <prefix>: the lab's pages and the pour, for the eye */
static int lab_shot(const char *prefix) {
    static uint16_t fb[TANK_W * TANK_H], scene[TANK_W * TANK_H];
    char path[512]; int n = 0;
    shelf_wipe(); shelf_reboot();
    tank_init(&tank, 77); tank_new_population(&tank); tank_ensure_ids(&tank);
    while (progression_newborn() >= 0) progression_newborn_done(&tank);
    render_set_scene_cache(scene); render_set_vignette_cache(vig_buf); render_set_card_cache(card_buf); render_set_modal_cache(give_buf, gift_buf); render_set_dirty_mask(dirty_buf);
    for (int i = 0; i < 6 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
    tank.night = false; tank.sd_balance = 1840;
    #define LSHOT(name) do { snprintf(path, sizeof path, "%s_%02d_%s.ppm", prefix, ++n, name); write_ppm(path, fb); } while (0)
    for (int own = 0; own < 2; own++) {
        if (own) tank.sd_unlocks |= SD_ITEM_LAB;
        int row = shop_goto(SHOP_ROW_LAB); render_shop(&tank, fb, TANK_W); LSHOT(own ? "row_owned" : "row_for_sale");
        tank.clock += 5; shop_tap(SHOP_ROW_X, SHP_ROW_Y0 + 20 + SHP_ROW_DY * row); render_shop(&tank, fb, TANK_W); LSHOT(own ? "modal_owned" : "modal_for_sale");
        render_shop_leave();
    }
    {   const int keep = tank.n_fish; int row = shop_goto(SHOP_ROW_LAB);
        while (tank.n_fish < POP_CAP) { tank_make_fish(&tank, tank.n_fish, tank.n_fish % tank_roster_count(), 0.5f, 0.5f, STAGE_ADULT); tank.n_fish++; }
        render_shop(&tank, fb, TANK_W); LSHOT("row_full");
        tank.clock += 5; shop_tap(SHOP_ROW_X, SHP_ROW_Y0 + 20 + SHP_ROW_DY * row); render_shop(&tank, fb, TANK_W); LSHOT("modal_full");
        render_shop_leave(); tank.n_fish = keep; }
    setup_begin_lab(&tank);
    #define LPAGE(name) do { render_tank(&tank, fb, TANK_W); render_setup(&tank, fb, TANK_W, tank.clock); LSHOT(name); } while (0)
    tank_set_body(setup_lab_fish(), 19.5f, 8.2f); setup_touch(&tank, 0, 0, false); LPAGE("body");
    setup_activate(&tank, SETUP_HIT_NEXT); setup_activate(&tank, SETUP_HIT_FINS0 + FINS_VEIL); tank_set_fin_len(setup_lab_fish(), 0, 1.3f); setup_touch(&tank, 0, 0, false); LPAGE("fins");
    setup_activate(&tank, SETUP_HIT_NEXT); setup_activate(&tank, SETUP_HIT_BODY0 + 5); setup_activate(&tank, SETUP_HIT_PAT0 + PATTERN_MASK); setup_activate(&tank, SETUP_HIT_ACC0 + 8); LPAGE("colors");
    setup_activate(&tank, SETUP_HIT_NEXT); snprintf(setup_lab_fish()->name, FISH_NAME_MAX + 1, "pip"); setup_touch(&tank, 0, 0, false); LPAGE("name");
    setup_activate(&tank, SETUP_HIT_NEXT); LPAGE("tube");
    tank.sd_balance = 90; LPAGE("tube_short"); tank.sd_balance = 1840;
    setup_activate(&tank, SETUP_HIT_POUR);
    static const float AT[] = { 0.05f, 0.2f, 0.4f, 0.8f, 1.5f, 3.0f, 6.0f, 8.5f, 10.0f };
    double t0 = tank.clock;
    for (unsigned k = 0; k < sizeof AT / sizeof *AT; k++) {
        while (tank.clock - t0 < AT[k]) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
        char nm[24]; snprintf(nm, sizeof nm, "drop_%04d", (int)(AT[k] * 100)); render_tank(&tank, fb, TANK_W); LSHOT(nm);
    }
    printf("lab-shot: wrote %d (%s_*.ppm), %d fish, balance %d\n", n, prefix, tank.n_fish, tank.sd_balance);
    return 0;
}
/* --lab-clip <dir>: THE DROP IN as frames (25 a second, <dir>/f_0000.ppm ..): the last moment of the tube page, then the
   fry in from the top, its splash, plume and shine, until a second after the shine ends */
static int lab_clip(const char *dir) {
    static uint16_t fb[TANK_W * TANK_H], scene[TANK_W * TANK_H];
    char path[512]; int n = 0;
    shelf_wipe(); shelf_reboot();
    tank_init(&tank, 77); tank_new_population(&tank); tank_ensure_ids(&tank);
    while (progression_newborn() >= 0) progression_newborn_done(&tank);
    render_set_scene_cache(scene); render_set_vignette_cache(vig_buf); render_set_card_cache(card_buf); render_set_modal_cache(give_buf, gift_buf); render_set_dirty_mask(dirty_buf);
    for (int i = 0; i < 6 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
    tank.night = false; tank.sd_balance = 1840; tank.sd_unlocks |= SD_ITEM_LAB;
    setup_begin_lab(&tank);
    fish_t *lf = setup_lab_fish(); tank_set_body(lf, 19.5f, 8.2f); lf->fins = FINS_VEIL; lf->pattern = PATTERN_MASK;
    tank_set_look(&tank, SETUP_LAB_SLOT, LOOK_BODY[5], LOOK_ACCENT[8]); lf->color = tank.fish[SETUP_LAB_SLOT].color; lf->fin = tank.fish[SETUP_LAB_SLOT].fin; lf->accent = tank.fish[SETUP_LAB_SLOT].accent;
    snprintf(lf->name, FISH_NAME_MAX + 1, "pip"); setup_touch(&tank, 0, 0, false);
    setup_lab_goto(&tank, SETUP_PG_LAB_TUBE);
    #define CFRAME() do { snprintf(path, sizeof path, "%s/f_%04d.ppm", dir, n++); write_ppm(path, fb); } while (0)
    for (int k = 0; k < 20; k++) { for (int i = 0; i < 2; i++) tank_tick(&tank, 1.0f / 50.0f, advisor_rules); setup_touch(&tank, 0, 0, false); render_frame_begin(); render_setup(&tank, fb, TANK_W, tank.clock); CFRAME(); }
    setup_activate(&tank, SETUP_HIT_POUR);
    for (int k = 0; k < (int)((PLOP_SHINE_S + 1.5f) * 25); k++) { for (int i = 0; i < 2; i++) tank_tick(&tank, 1.0f / 50.0f, advisor_rules); render_tank(&tank, fb, TANK_W); CFRAME(); }
    printf("lab-clip: wrote %d frames (%s/f_*.ppm)\n", n, dir);
    return 0;
}
/* --lab-probe: how often a drag draws the lab's page whole (each whole draw moves the still part's one moment) */
static int lab_probe(void) {
    static uint16_t fb[TANK_W * TANK_H];
    shelf_wipe(); shelf_reboot(); tank_init(&tank, 4343); forms_cast(3); tank_ensure_ids(&tank);
    while (progression_newborn() >= 0) progression_newborn_done(&tank);
    tank.sd_unlocks |= SD_ITEM_LAB; tank.sd_balance = 900;
    setup_begin_lab(&tank);
    for (int pg = 0; pg < 2; pg++) for (int ax = 0; ax < 2; ax++) {
        setup_lab_goto(&tank, SETUP_PG_LAB_BODY + pg);
        for (int k = 0; k < 3; k++) { render_frame_begin(); tank.clock += 1.0 / 25; setup_touch(&tank, 0, 0, false); render_lab_page(&tank, fb, TANK_W, tank.clock); }
        int b0, w0, b1, w1; render_lab_page_counts(&b0, &w0);
        float x = 220, y = ax ? 130 : 200; pg_touch(x, y, true);
        for (int i = 1; i <= 100; i++) {                       /* 4 s of a finger going out and back, wobbling */
            float d = 70 * sinf(i * 0.09f) + 3 * sinf(i * 1.7f);
            render_frame_begin(); tank.clock += 1.0 / 25;
            pg_touch(ax ? x + 2 * sinf(i * 0.9f) : x + d, ax ? y - d : y + 2 * sinf(i * 0.9f), true);
            render_lab_page(&tank, fb, TANK_W, tank.clock);
        }
        pg_touch(x, y, false);
        render_lab_page_counts(&b1, &w1);
        printf("lab-probe: page %s, a %s drag, 100 frames: %d whole, %d the boxes alone\n", pg ? "FINS" : "BODY", ax ? "vertical" : "sideways", w1 - w0, b1 - b0);
    }
    return 0;
}
#undef ST_FAIL
