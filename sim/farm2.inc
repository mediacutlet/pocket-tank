/* ---- progression reviewer, ROUND 2 (0.4.1 audit, c04f87e): the round-1 fixes' blast radius (scratch only) ----
 * --selftest-farm2, FARM2_ONLY=<letters>. Uses farm.inc's fm_* and main.c's aa_* helpers. */
#define F2_SAY(...) do { printf("farm2: "); printf(__VA_ARGS__); printf("\n"); } while (0)
static int f2_bad;
#define F2_CHECK(c, ...) do { if (!(c)) { printf("farm2: FAIL: "); printf(__VA_ARGS__); printf("\n"); f2_bad++; } } while (0)
static void f2_patch_release(uint32_t rel) { FILE *f = fopen(fm_sav, "r+b"); if (!f) return; fseek(f, 1664, SEEK_SET); fwrite(&rel, sizeof rel, 1, f); fclose(f); }
static passport_t f2_fry_next_door(uint32_t seed, int mutated) {   /* a fry of ours (parent = our fish 0), born next door */
    static tank_t there; tank_init(&there, seed); tank_new_population(&there); tank_ensure_ids(&there);
    fish_t fry = there.fish[0]; fry.stage = STAGE_FRY; fry.size = 0.4f; fry.ms_bits = MS_CROSSBRED; fry.mutated = (uint8_t)mutated;
    fry.parent_uid[0] = tank.fish[0].uid; fry.parent_uid[1] = 0xc0ffee02u; fry.uid = 0xf00d0000u + seed;
    passport_t pp; passport_from_fish(&pp, &fry, tank.uid, 0);
    return pp;
}
static int f2_drain(const char *tag, int want_kind, uint32_t want_bit) {   /* -> the fish of the first notice of that kind/bit, -9 none */
    int got = -9;
    for (int q = 0; q < 20000; q++) {
        notice_tick(&tank, 0.05f, false);
        const notice_t *c = notice_current();
        if (c && c->age == 0) {
            printf("  [%s] notice kind %d fish %d bit %05x\n", tag, c->kind, c->fish, (unsigned)c->bit);
            if (c->kind == want_kind && c->bit == want_bit && got == -9) got = c->fish;
        }
        if (c && c->age > NOTICE_HOLD_S + 0.1f) notice_dismiss();
        if (!c && !notice_pending()) break;
    }
    return got;
}
/* L: a fry of ours born next door, home THREE ways (bubbles at a boot, LEV_FISH_BACK, LEV_FISH_RESTORE), each across a
   reboot while it is away: +20 once, FIRST MUTATION iff mutated, its welcome once, its count pictured, nothing twice */
static int f2_fry_home_ways(void) {
    const char *how[3] = { "bubbles after a boot", "LEV_FISH_BACK", "LEV_FISH_RESTORE" };
    for (int w = 0; w < 3; w++) for (int mut = 0; mut <= 1; mut++) {
        fm_fresh(400 + w * 2 + mut);
        while (tank.n_fish < 3) { progression_force_arrival(&tank); if (progression_newborn() >= 0) progression_newborn_done(&tank); }
        uint32_t tms0 = tank.tank_ms_bits & ~(TMS_FIRST_MUTATION);
        tank.tank_ms_bits = tms0; fm_ticks(2); notice_sync(&tank);
        passport_t fp = f2_fry_next_door(410 + w * 2 + mut, mut ? GENE_PATTERN + 1 : 0);
        int s = progression_fry_adopt(&tank, &fp, LINK_LEFT);
        int32_t w0 = tank.sd_balance;
        fm_ticks(5);
        F2_CHECK(s >= 0 && tank.fish[s].born_away, "L(%s): the fry next door (slot %d)", how[w], s);
        F2_CHECK(!(tank.tank_ms_bits & TMS_QUARTET), "L(%s): QUARTET before the fry is home", how[w]);
        F2_CHECK(!(tank.tank_ms_bits & TMS_FIRST_MUTATION), "L(%s): FIRST MUTATION before home", how[w]);
        fm_reload(400 + w * 2 + mut);                    /* the power off and on while it is away */
        F2_CHECK(tank.fish[s].born_away && tank.fish[s].away, "L(%s): after a reboot the fry is born_away %d away %d", how[w], tank.fish[s].born_away, tank.fish[s].away);
        if (w == 0) {
            F2_CHECK(tank.link_restore_at > 0, "L: no bubble restore staged at the boot");
            tank.clock = tank.link_restore_at + 0.01;
            tank_link_tick(&tank, NULL);
        } else {
            passport_t hp; passport_from_fish(&hp, &tank.fish[s], tank.uid, 7200);
            link_ev_t ev; memset(&ev, 0, sizeof ev); ev.type = w == 1 ? LEV_FISH_BACK : LEV_FISH_RESTORE; ev.uid = tank.fish[s].uid; ev.pp = hp; ev.edge = LINK_LEFT;
            tank_link_event(&tank, NULL, &ev);
        }
        int nb = progression_newborn();
        fm_ticks(3);
        int32_t paid = tank.sd_balance - w0;
        F2_CHECK(!tank.fish[s].away && !tank.fish[s].born_away, "L(%s): not home (away %d born_away %d)", how[w], tank.fish[s].away, tank.fish[s].born_away);
        int want = SD_BIRTH;   /* (by the door its passport says 2 h, but it has lived seconds: home as old as it lived, a fry -
                                  no +5 for a juvenile it never was. Was SD_BIRTH + SD_STAGE_JUV until 0.4.2, LK-1) */
        F2_CHECK(paid == want, "L(%s, mutated %d): home paid %d (want %d)", how[w], mut, (int)paid, want);
        F2_CHECK(nb == s, "L(%s): the welcome owed is slot %d (want %d)", how[w], nb, s);
        F2_CHECK(!!(tank.tank_ms_bits & TMS_FIRST_MUTATION) == mut, "L(%s): FIRST MUTATION %d for a fry mutated %d", how[w], !!(tank.tank_ms_bits & TMS_FIRST_MUTATION), mut);
        if (nb >= 0) progression_newborn_done(&tank);
        int pic = f2_drain("L", NOTICE_TANK_MILESTONE, TMS_QUARTET);
        F2_CHECK(pic == s, "L(%s): the QUARTET pictures %d (the fry home is %d)", how[w], pic, s);
        progression_save(&tank);
        int32_t w1 = tank.sd_balance;
        fm_reload(400 + w * 2 + mut); fm_ticks(5);
        F2_CHECK(tank.sd_balance == w1 && progression_newborn() < 0, "L(%s): the reboot after home paid %d / owed welcome %d", how[w], (int)(tank.sd_balance - w1), progression_newborn());
        F2_SAY("L: %s, mutated %d: paid +%d at home, welcome slot %d, FIRST MUTATION %d, QUARTET pictures %d (fry %d)",
               how[w], mut, (int)paid, nb, !!(tank.tank_ms_bits & TMS_FIRST_MUTATION), pic, s);
    }
    return 0;
}
/* M: two fry of ours born next door, both home from bubbles in ONE tick (the boot's restore): which fish each count pictures */
static int f2_two_counts_one_tick(void) {
    fm_fresh(420);
    passport_t a = f2_fry_next_door(421, 0), b = f2_fry_next_door(422, 0);
    int sa = progression_fry_adopt(&tank, &a, LINK_LEFT), sb = progression_fry_adopt(&tank, &b, LINK_RIGHT);
    fm_ticks(2); notice_sync(&tank);
    fm_reload(420);
    tank.clock = tank.link_restore_at + 0.01; tank_link_tick(&tank, NULL);
    while (progression_newborn() >= 0) progression_newborn_done(&tank);
    int p3 = -9, p4 = -9;
    for (int q = 0; q < 20000; q++) {
        notice_tick(&tank, 0.05f, false);
        const notice_t *c = notice_current();
        if (c && c->age == 0 && c->kind == NOTICE_TANK_MILESTONE) { if (c->bit == TMS_TRIO) p3 = c->fish; if (c->bit == TMS_QUARTET) p4 = c->fish; }
        if (c && c->age > NOTICE_HOLD_S + 0.1f) notice_dismiss();
        if (!c && !notice_pending()) break;
    }
    F2_SAY("M: two fry next door (slots %d, %d) home in one tick: TRIO pictures %d, QUARTET pictures %d", sa, sb, p3, p4);
    return 0;
}
/* N: the receipts (927d5be). A 0.4.1 tank: two gifts, nobody trusted; reset -> the pair pays +30; reset again -> +0.
   The same tank saved by 0.4.0 (stamp patched) and loaded: the fold reads the gifts' pre-marked trust */
static int f2_receipts(void) {
    for (int old = 0; old <= 1; old++) {
        fm_fresh(430);
        for (int i = 0; i < tank.n_fish; i++) tank.fish[i].trust = 5.0f;
        { static gift_book_t nb; memset(&nb, 0, sizeof nb); nb.persist = true; gift_clear(&nb);   /* (their RECEIVED notes, as the link
                                                       writes them: round 2 review F1) */
          for (int k = 0; k < 2; k++) { passport_t pp = fm_other(431 + k); pp.trust = 3.0f; if (progression_gift_adopt(&tank, &pp) >= 0) gift_received(&nb, pp.uid, 0x7000u + k); } }
        fm_ticks(3); progression_save(&tank);
        if (old) f2_patch_release(0x00040000u);
        fm_reload(430); fm_ticks(2);
        uint8_t r = tank.sd_receipts;
        int32_t w0 = tank.sd_balance;
        progression_reset(&tank, 999); progression_setup_done(&tank);
        for (int i = 0; i < tank.n_fish; i++) tank.fish[i].trust = 10.0f;
        fm_ticks(3);
        int32_t p1 = tank.sd_balance - w0;
        w0 = tank.sd_balance;
        progression_reset(&tank, 998); progression_setup_done(&tank);
        for (int i = 0; i < tank.n_fish; i++) tank.fish[i].trust = 10.0f;
        fm_ticks(3);
        int32_t p2 = tank.sd_balance - w0;
        F2_SAY("N: saved by %s, two gifts, nobody trusted (receipts at load %02x): first reset's pair paid +%d, second +%d",
               old ? "0.4.0 (stamp 0x00040000)" : "0.4.1", r, (int)p1, (int)p2);
        F2_CHECK(p1 == 2 * SD_TRUST && p2 == 0, "N: %s receipts: +%d then +%d (want +30 then +0)", old ? "0.4.0" : "0.4.1", (int)p1, (int)p2);   /* (0.4.0 too, once
                                                   the fold counts no gift under full trust: round 2) */
    }
    /* the same with the stamp of an early crew build of 0.4.1 (0.4.0.201 = 0x000400c9): every load folds */
    fm_fresh(435);
    for (int i = 0; i < tank.n_fish; i++) tank.fish[i].trust = 5.0f;
    { static gift_book_t nb; memset(&nb, 0, sizeof nb); nb.persist = true; gift_clear(&nb);
      for (int k = 0; k < 2; k++) { passport_t pp = fm_other(436 + k); pp.trust = 3.0f; if (progression_gift_adopt(&tank, &pp) >= 0) gift_received(&nb, pp.uid, 0x7100u + k); } }
    fm_ticks(3); progression_save(&tank); f2_patch_release(0x000400c9u); fm_reload(435);
    F2_SAY("N: an early crew build's stamp (0.4.0.201): receipts at load %02x", tank.sd_receipts);
    F2_CHECK(!SD_RCPT_TRUST(tank.sd_receipts), "N: a crew save's gifts (trust 3) folded in as %u paid trusts", SD_RCPT_TRUST(tank.sd_receipts));
    { uint8_t r1 = tank.sd_receipts; progression_save(&tank); f2_patch_release(0x000400c9u); fm_reload(435);   /* (and loaded again: the same) */
      F2_CHECK(tank.sd_receipts == r1, "N: a crew save loaded twice: receipts %02x then %02x", r1, tank.sd_receipts); }
    return 0;
}
/* O: MS_CAME_CHANGED, a gift that came changed, then the keeper changes ANOTHER fish's... no: the same fish, on the OTHER
   trait, here (R2-3's case, timed): how long until THE TANK CHANGED SOMEONE can light, by play's own settle */
static int f2_came_changed_settle(void) {
    fm_fresh(440);
    for (int i = 0; i < tank.n_fish; i++) { tank.fish[i].bold = tank.fish[i].bold0; tank.fish[i].sociable = tank.fish[i].sociable0; }
    passport_t pp = fm_other(441); pp.bold0 = 0.40f; pp.bold = 0.62f;
    int s = progression_gift_adopt(&tank, &pp);
    int ticks = 0;
    while ((tank.fish[s].ms_bits & MS_CAME_CHANGED) && ticks < 20 * 3600 * 10) { progression_tick(&tank, 0.1f); ticks++; }
    F2_SAY("M2/O: a gift 0.22 off in bold: MS_CAME_CHANGED cleared after %.1f h of ticks (bold %.3f vs %.3f)", ticks / 36000.0, tank.fish[s].bold, tank.fish[s].bold0);
    /* its passport, given away again: bit 15 must not travel */
    passport_t out; tank.fish[s].ms_bits |= MS_CAME_CHANGED; passport_from_fish(&out, &tank.fish[s], tank.uid, 3600);
    F2_CHECK(!(out.ms_bits & MS_CAME_CHANGED), "O: MS_CAME_CHANGED travels in a passport");
    return 0;
}
/* P: sale-only FIRST FISH SOLD with the fry wait - a GIVE and then a sale in one tank: one notice, the shop's fry open,
   the next birth's MEALS wait counted from the later of the two */
static int f2_give_then_sell(void) {
    fm_fresh(450);
    while (tank.n_fish < 4) { progression_force_arrival(&tank); if (progression_newborn() >= 0) progression_newborn_done(&tank); }
    fm_ticks(2); fm_notices("P0", NULL);
    progression_give_out(&tank, 3); fm_ticks(2);
    int sold_after_give = !!(tank.tank_ms_bits & TMS_FIRST_SALE);
    int n1 = fm_notices("P give", NULL);
    progression_sell_fish(&tank, 2); fm_ticks(2);
    int n2 = fm_notices("P sell", NULL);
    F2_SAY("P: a GIVE: FIRST FISH SOLD %d, %d notices; then a sale: FIRST FISH SOLD %d, fry for sale %d, %d notices",
           sold_after_give, n1, !!(tank.tank_ms_bits & TMS_FIRST_SALE), progression_fry_for_sale(&tank), n2);
    F2_CHECK(!sold_after_give && progression_fry_for_sale(&tank), "P: the sale-only rule");
    return 0;
}
/* Q: the money loops again with the round-1 paths in them - a fry of ours born next door, home, sold; 200 rounds: the
   wallet moves by exactly SD_BIRTH + its sale (x2 crossbred) a round */
static int f2_fry_home_loop(void) {
    fm_fresh(460);
    int32_t w0 = tank.sd_balance; int rounds = 0;
    for (int k = 0; k < 200; k++) {
        passport_t fp = f2_fry_next_door(1000 + k, 0);
        int s = progression_fry_adopt(&tank, &fp, LINK_LEFT);
        if (s < 0) { F2_SAY("Q: FAIL no room at %d (held %d)", k, progression_fish_held(&tank)); f2_bad++; break; }
        tank.fish[s].away = false; progression_fry_home(&tank, s);
        progression_fry_home(&tank, s);                  /* (twice: a duplicate event) */
        progression_newborn_done(&tank); fm_ticks(1);
        if (!progression_sell_fish(&tank, s)) { F2_SAY("Q: FAIL the sale at %d", k); f2_bad++; break; }
        fm_ticks(1); rounds++;
    }
    int32_t d = tank.sd_balance - w0;
    F2_SAY("Q: %d fry of ours home from next door and sold: wallet +%d (want +%d)", rounds, (int)d, rounds * (SD_BIRTH + 2 * SD_FISH_FRY));
    F2_CHECK(d == rounds * (SD_BIRTH + 2 * SD_FISH_FRY), "Q: the wallet");
    return 0;
}
/* R: the swap carry (fef5fef) given twice (a boot before any save lands): idempotent? */
static int f2_carry_twice(void) {
    fm_fresh(470);
    tank.sd_balance = 777;
    progression_carry_t c; progression_carry_take(&tank, &c);
    tank.sd_balance = 5;
    progression_carry_give(&tank, &c); progression_carry_give(&tank, &c);
    F2_SAY("R: a carry of 777 given twice: wallet %d", (int)tank.sd_balance);
    F2_CHECK(tank.sd_balance == 777, "R: the carry is not idempotent");
    return 0;
}
/* S: the youngest by s_age vs a gift whose stage is ahead of its age (a gift never demotes) */
static int f2_youngest_gift_stage(void) {
    fm_fresh(480);
    for (int i = 0; i < tank.n_fish; i++) progression_set_age(&tank, i, STAGE_ADULT_AGE + 100);   /* the founders grown */
    progression_force_arrival(&tank); int fry = progression_newborn(); progression_newborn_done(&tank);
    progression_set_age(&tank, fry, 600);            /* our fry, 10 min old */
    passport_t pp = fm_other(481); pp.stage = STAGE_ADULT; pp.age_s = 60;   /* an adult whose passport says 1 min (a demotion-proof stage) */
    pp.ms_bits = MS_ARRIVED | MS_REACHED_JUV | MS_REACHED_ADULT;
    int g = progression_gift_adopt(&tank, &pp);
    fry_req_t req[FRY_REQ_MAX]; int n = progression_next_fry(&tank, req, NULL);
    for (int i = 0; i < n; i++) if (req[i].kind == FRY_REQ_GROW)
        F2_SAY("S: our fry (slot %d, 10 min) and a gifted ADULT whose passport age is 1 min (slot %d, stage %d): youngest %d, GROW %s \"%s\"",
               fry, g, tank.fish[g].stage, progression_youngest(&tank), req[i].met ? "MET" : "unmet", req[i].progress);
    return 0;
}
static int selftest_farm2(void) {
    const char *d = getenv("PT_SHELF_DIR");
    snprintf(fm_sav, sizeof fm_sav, "%s/farm2.sav", d && *d ? d : "/tmp");
    setenv("POCKET_TANK_SAVE", fm_sav, 1);
    const char *only = getenv("FARM2_ONLY");
#define F2_RUN(c, f) if (!only || strchr(only, c)) (void)f()
    F2_RUN('L', f2_fry_home_ways); F2_RUN('M', f2_two_counts_one_tick); F2_RUN('N', f2_receipts); F2_RUN('O', f2_came_changed_settle);
    F2_RUN('P', f2_give_then_sell); F2_RUN('Q', f2_fry_home_loop); F2_RUN('R', f2_carry_twice); F2_RUN('S', f2_youngest_gift_stage);
    printf("selftest-farm2: %s (%d)\n", f2_bad ? "FAILED" : "done", f2_bad);
    return f2_bad != 0;
}
