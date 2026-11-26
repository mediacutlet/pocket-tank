/* --selftest-ss2 (0.4.1 audit ROUND 2, seam-saves): round 1's new state across the save paths, beside
 * --selftest-seam-saves (whose ss_boot / ss_copy / sv_u32 it uses). #included into sim/main.c before main().
 *   X1  the receipts fold: a 0.4.0 save with a GIFT (its ledger says trust paid), the pair never paid its trust
 *   X2  the receipts fold on a crew build's stamp (0.4.0.201): it runs at every load (R2-5)
 *   X3  MS_CAME_CHANGED: save/load, a swap out and back, the passport (never travels), its clear saved
 *   X4  born_away after a slot shift (a sale before it): save/load
 *   X5  a birth staged when the tank was shelved: born at the SWAP IN boot, its +20 vs the swap's carry
 *   X6  the swap's carry: its delete refused at the boot AND at the next save's tidy, that save lands, then a boot
 *   X7  REMOVE: its "paid" mark refused, the rest lands - is the journal written back after it was deleted?
 *   X8  MS_BOUGHT / MS_CAME_CHANGED / born_away through a NEW TANK swap and a reset (gone where they should be) */
static int x2_bad;
#define X2_FAIL(...) do { printf("ss2: FAIL: "); printf(__VA_ARGS__); printf("\n"); x2_bad++; } while (0)
static void x2_stamp(uint32_t rel) {
    FILE *f = fopen(getenv("POCKET_TANK_SAVE"), "r+b");
    if (f) { fseek(f, 1664, SEEK_SET); fwrite(&rel, 4, 1, f); fclose(f); }
}
static void x2_byte(long off, uint8_t v) {
    FILE *f = fopen(getenv("POCKET_TANK_SAVE"), "r+b");
    if (f) { fseek(f, off, SEEK_SET); fwrite(&v, 1, 1, f); fclose(f); }
}
static int x2_gift(uint32_t seed, float dbold) {    /* a gift from a made-up tank; dbold: how far its bold is off its birth temper */
    static tank_t fr; tank_init(&fr, seed); tank_new_population(&fr); tank_ensure_ids(&fr);
    fish_t g = fr.fish[0]; g.bold = g.bold0 + dbold; if (g.bold > 1) { g.bold0 -= dbold; g.bold = g.bold0 + dbold; }
    passport_t pp; passport_from_fish(&pp, &g, 0x5eed0000u | seed, 3600);
    int slot = progression_gift_adopt(&tank, &pp);
    if (slot >= 0 && gift_of(&tank)) gift_received(gift_of(&tank), pp.uid, 0x6000u | seed);   /* (its RECEIVED note, as the link writes it
                                                       - what tells a 0.4.0 save's gift: round 2 review F1) */
    return slot;
}
static int x2_reset_pays(uint32_t seed) {          /* reset, the new pair at full trust: what it paid */
    progression_reset(&tank, seed); ss_boot(seed + 1); progression_setup_done(&tank);
    int32_t w0 = tank.sd_balance;
    for (int i = 0; i < tank.n_fish; i++) tank.fish[i].trust = 10.0f;
    for (int k = 0; k < 10; k++) progression_tick(&tank, 0.1f);
    return tank.sd_balance - w0;
}
static int x2_slot(uint32_t uid) { for (int i = 0; i < tank.n_fish; i++) if (tank.fish[i].uid == uid) return i; return -1; }

static int selftest_ss2(void) {   /* (folded into the sim from the round-2 seam-saves harness, 0.4.1 audit) */
    /* ---- X1: a 0.4.0 save holding a gift, its own pair never at full trust ---- */
    sim_shelf_board(); ss_boot(91); progression_setup_done(&tank); tank.sd_balance = 100;
    for (int i = 0; i < tank.n_fish; i++) tank.fish[i].trust = 2.0f;
    if (x2_gift(0x91, 0) < 0) X2_FAIL("X1: the gift");
    for (int k = 0; k < 10; k++) progression_tick(&tank, 0.1f);
    tank.sd_receipts = 0; progression_save(&tank); x2_stamp(0x00040000u); x2_byte(1687, 0);   /* (as 0.4.0 wrote it) */
    ss_boot(92);
    printf("ss2: X1 a 0.4.0 save with a gift: loaded receipts %02x (trust paid %u; no trust was ever paid in this tank)\n", tank.sd_receipts, SD_RCPT_TRUST(tank.sd_receipts));
    { int paid = x2_reset_pays(93);
      printf("ss2: X1 ... its reset's new pair at full trust paid +%d (a fresh pair: +%d)\n", paid, 2 * SD_TRUST);
      if (paid != 2 * SD_TRUST) X2_FAIL("X1: a 0.4.0 tank's gift folded in as a paid trust: the reset's new pair paid +%d of %d", paid, 2 * SD_TRUST); }

    /* ---- X2: the crew build's stamp (0.4.0.201 = 0x000400c9): every load folds again ---- */
    sim_shelf_board(); ss_boot(95); progression_setup_done(&tank); tank.sd_balance = 100;
    for (int i = 0; i < tank.n_fish; i++) tank.fish[i].trust = 2.0f;
    if (x2_gift(0x95, 0) < 0) X2_FAIL("X2: the gift");
    for (int k = 0; k < 10; k++) progression_tick(&tank, 0.1f);
    printf("ss2: X2 a 0.4.1 tank with a gift: receipts %02x in RAM\n", tank.sd_receipts);
    progression_save(&tank); x2_stamp(0x000400c9u);
    ss_boot(96);
    printf("ss2: X2 ... the same save stamped 0.4.0.201 (a crew build's): loaded receipts %02x\n", tank.sd_receipts);
    if (SD_RCPT_TRUST(tank.sd_receipts)) X2_FAIL("X2 (= R2-5, known): a crew-stamped save folds its gift's ledger in at every load (receipts %02x)", tank.sd_receipts);

    /* ---- X3: MS_CAME_CHANGED ---- */
    sim_shelf_board(); ss_boot(101); progression_setup_done(&tank); tank.sd_balance = 2000;
    { int g = x2_gift(0xa3, 0.2f); uint32_t gu = g >= 0 ? tank.fish[g].uid : 0;
      if (g < 0 || !(tank.fish[g].ms_bits & MS_CAME_CHANGED)) X2_FAIL("X3: a gift 0.2 off was not marked (slot %d)", g);
      progression_save(&tank); ss_boot(102);
      g = x2_slot(gu);
      if (g < 0 || !(tank.fish[g].ms_bits & MS_CAME_CHANGED)) X2_FAIL("X3: the mark lost over a reboot");
      else printf("ss2: X3 MS_CAME_CHANGED kept over a reboot (bits %05x)\n", (unsigned)tank.fish[g].ms_bits);
      if (tank.tank_ms_bits & TMS_CHANGED_SOMEONE) X2_FAIL("X3: the gift lit THE TANK CHANGED SOMEONE");
      /* a swap out (NEW TANK) and back */
      int bi = shelf_new(0, clock_port_now_unix());
      if (bi < 0 || !shelf_put_up(bi)) X2_FAIL("X3: the swap out");
      ss_boot(103); progression_setup_done(&tank);
      if (!shelf_put_up(shelf_index_of(1))) X2_FAIL("X3: the swap back");
      ss_boot(104); g = x2_slot(gu);
      if (g < 0 || !(tank.fish[g].ms_bits & MS_CAME_CHANGED)) X2_FAIL("X3: the mark lost over a swap out and back");
      else printf("ss2: X3 MS_CAME_CHANGED kept over a swap out and back\n");
      /* the passport never carries it */
      if (g >= 0) { passport_t pp; passport_from_fish(&pp, &tank.fish[g], tank.uid, 0);
        uint8_t b[PASSPORT_MAX_BYTES]; int n = passport_write(&pp, b, sizeof b); passport_t q; bool ok = n > 0 && passport_read(&q, b, n);
        if (!ok || (pp.ms_bits & MS_CAME_CHANGED) || (q.ms_bits & MS_CAME_CHANGED)) X2_FAIL("X3: the passport carries MS_CAME_CHANGED (%d)", ok);
        else printf("ss2: X3 the passport leaves it behind (bits %04x)\n", (unsigned)q.ms_bits); }
      /* settled back: cleared, and the clear saved */
      if (g >= 0) { tank.fish[g].bold = tank.fish[g].bold0; tank.fish[g].sociable = tank.fish[g].sociable0;
        progression_tick(&tank, 0.1f);
        if (tank.fish[g].ms_bits & MS_CAME_CHANGED) X2_FAIL("X3: settled back, still marked");
        bool dirty = progression_dirty();
        progression_save(&tank); ss_boot(105); g = x2_slot(gu);
        if (g < 0 || (tank.fish[g].ms_bits & MS_CAME_CHANGED)) X2_FAIL("X3: the clear lost over a reboot");
        else printf("ss2: X3 settled back: cleared (marked dirty %d) and the clear kept over a reboot\n", dirty); } }

    /* ---- X4: born_away after a sale shifts the slots ---- */
    sim_shelf_board(); ss_boot(111); progression_setup_done(&tank);
    while (tank.n_fish < 4) tank_add_fish(&tank, 0, 1);
    while (progression_newborn() >= 0) progression_newborn_done(&tank);
    { static tank_t there; tank_init(&there, 0x5eed); tank_new_population(&there);
      fish_t fry = there.fish[0]; fry.stage = STAGE_FRY; fry.size = 0.4f; fry.ms_bits = MS_CROSSBRED;
      fry.parent_uid[0] = tank.fish[0].uid; fry.parent_uid[1] = 0xc0ffee02u;
      passport_t pp; passport_from_fish(&pp, &fry, tank.uid, 0);
      int s = progression_fry_adopt(&tank, &pp, LINK_LEFT);
      uint32_t fu = s >= 0 ? tank.fish[s].uid : 0;
      if (s != 4) X2_FAIL("X4: the fry next door in slot %d", s);
      if (!progression_sell_fish(&tank, 1)) X2_FAIL("X4: the sale of slot 1");
      int now = x2_slot(fu);
      progression_save(&tank); ss_boot(112);
      int after = x2_slot(fu), others = 0;
      for (int i = 0; i < tank.n_fish; i++) if (i != after && tank.fish[i].born_away) others++;
      if (after < 0 || !tank.fish[after].born_away || !tank.fish[after].away || others) X2_FAIL("X4: after the sale and a reboot the fry is in slot %d born_away %d away %d, %d others marked", after, after >= 0 ? tank.fish[after].born_away : -1, after >= 0 ? tank.fish[after].away : -1, others);
      else printf("ss2: X4 a sale shifted the fry next door to slot %d: born_away and away kept on it alone over a reboot\n", now); }

    /* ---- X5: a birth staged when the tank was shelved, born at its SWAP IN ---- */
    sim_shelf_board(); ss_boot(121); progression_setup_done(&tank); tank.sd_balance = 300;
    progression_stage_arrival(&tank); progression_save(&tank);       /* A: two fish, a fry staged (the pair about to court) */
    { int bi = shelf_new(0, clock_port_now_unix());
      if (bi < 0 || !shelf_put_up(bi)) X2_FAIL("X5: the swap to B");
      ss_boot(122); progression_setup_done(&tank); tank.sd_balance = 500; progression_save(&tank);   /* B: the wallet 500 */
      if (!shelf_put_up(shelf_index_of(1))) X2_FAIL("X5: the swap back to A");
      (void)progression_sd_take_award();
      ss_boot(123);
      int toast = progression_sd_take_award();
      printf("ss2: X5 A back: %d fish (2 before), wallet %d, the toast owed +%d, a welcome owed %d\n", tank.n_fish, (int)tank.sd_balance, toast, progression_newborn());
      if (tank.n_fish == 3 && tank.sd_balance != 500 + SD_BIRTH) X2_FAIL("X5: a birth at the SWAP IN boot paid +%d (the toast says +%d), the carry put the wallet at %d (want %d)", (int)tank.sd_balance - 500, toast, (int)tank.sd_balance, 500 + SD_BIRTH);
      ss_boot(124); printf("ss2: X5 ... after one more boot: %d fish, wallet %d\n", tank.n_fish, (int)tank.sd_balance); }

    /* ---- X6: the swap's carry, its delete refused at the boot (R-saves S3's state: the carry on the flash, its delete owed
       to shelf_tidy) and refused again at the next save's tidy, while that save lands; then a boot ---- */
    { sim_shelf_board(); ss_boot(131); progression_setup_done(&tank); tank.sd_balance = 400; progression_save(&tank);
      if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) X2_FAIL("X6: staging");
      progression_carry_t c; memset(&c, 0, sizeof c);      /* (the swap's own carry, as it is on the flash - round 2's marker: a carry
                                                              left behind is the one the boot applied, not a new one) */
      { char cp0[800]; snprintf(cp0, sizeof cp0, "%s/frame/carry", sim_shelf_dir()); FILE *cf0 = fopen(cp0, "rb"); if (cf0) { if (fread(&c, 1, sizeof c, cf0) != sizeof c) {} fclose(cf0); } }
      ss_boot(132); progression_setup_done(&tank); progression_save(&tank);   /* B on the glass: the carry (400) given, saved, let go */
      shelf_port_frame_set("carry", &c, sizeof c);         /* ... as if its delete had been refused: */
      shelf_port_sim_fail(0); shelf_carry_done(); shelf_port_sim_fail(-1);   /* (refused: s_open.del_carry - shelf_tidy's) */
      tank.sd_balance = 100;                               /* the keeper spends */
      shelf_port_sim_fail(0); bool landed = progression_save(&tank); shelf_port_sim_fail(-1);   /* the tidy's delete refused once more */
      char cp[800]; snprintf(cp, sizeof cp, "%s/frame/carry", sim_shelf_dir()); FILE *cf = fopen(cp, "rb"); bool carry_still = cf != NULL; if (cf) fclose(cf);
      ss_boot(133);
      printf("ss2: X6 the save with 100 landed %d, the carry still on the flash %d: the next boot's wallet %d\n", landed, carry_still, (int)tank.sd_balance);
      if (landed && tank.sd_balance != 100) X2_FAIL("X6: a save landed while shelf_tidy could not delete the carry: the next boot set the wallet %d -> %d", 100, (int)tank.sd_balance); }

    /* ---- X6b: NEW TANK, its journal's delete refused and refused again at the next save's tidy while that save lands
       (the keeper earned the price back meanwhile); then a boot ---- */
    { char pre[800]; snprintf(pre, sizeof pre, "%s.x6b", sim_shelf_dir());
      const int W0 = 1000, P = SHELF_NEW_PRICE;
      sim_shelf_board(); ss_boot(135); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
      ss_copy(sim_shelf_dir(), pre);
      shelf_port_sim_cut(1000); progression_buy_tank(&tank, P, clock_port_now_unix()); int used = 1000 - shelf_port_sim_cut(-1);
      int bad = 0, runs = 0;
      for (int k = 0; k < used; k++) {
          ss_copy(pre, sim_shelf_dir()); ss_boot(135);
          shelf_port_sim_fail(k); int i = progression_buy_tank(&tank, P, clock_port_now_unix()); int was = shelf_port_sim_fail(-1);
          if (was != -1 || i < 0) continue;                  /* (only a refusal that left the tank bought) */
          bool j0; { char jp[800]; snprintf(jp, sizeof jp, "%s/frame/buy", sim_shelf_dir()); FILE *f = fopen(jp, "rb"); j0 = f != NULL; if (f) fclose(f); }
          if (!j0) continue;                                 /* (the journal went: not this case) */
          progression_sd_grant(&tank, P);                    /* the keeper earns the price back */
          shelf_port_sim_fail(0); bool landed = progression_save(&tank); shelf_port_sim_fail(-1);   /* the tidy's delete refused again */
          int32_t want = tank.sd_balance; uint32_t bid = shelf_get(i) ? shelf_get(i)->id : 0;
          ss_boot(136); runs++;
          printf("ss2: X6b write %d refused (bought, the journal left), the price earned back and saved (landed %d): the boot has the tank %s, %d SD (want there, %d)\n",
                 k, landed, rf_there(bid) ? "there" : "GONE", (int)tank.sd_balance, (int)want);
          if (landed && (!rf_there(bid) || tank.sd_balance != want)) bad++;
      }
      if (bad) X2_FAIL("X6b: a NEW TANK paid for, its journal's delete refused twice around a save that landed: the boot undid the purchase (%d of %d runs)", bad, runs);
      char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", pre); (void)system(cmd); }

    /* ---- X7: REMOVE, its paid mark refused, the erase and the delete landing: is the journal written back? ---- */
    { char pre[800]; snprintf(pre, sizeof pre, "%s.x7", sim_shelf_dir());
      sim_shelf_board(); ss_boot(141);
      tank_init(&tank, 9); tank_new_population(&tank); tank.sd_unlocks = SD_ITEM_CASTLE | SD_ITEM_SNAIL; progression_save(&tank);
      if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) X2_FAIL("X7: staging");
      ss_boot(142); progression_setup_done(&tank); tank.sd_balance = 1000; progression_save(&tank);
      ss_copy(sim_shelf_dir(), pre);
      int resurrect = 0;
      for (int k = 0; k < 12; k++) {
          ss_copy(pre, sim_shelf_dir()); ss_boot(142);
          shelf_port_sim_fail(k); int got = progression_remove_tank(&tank, shelf_index_of(1)); bool fired = shelf_port_sim_fail(-1) < 0;
          if (!fired) break;
          char rp[800]; snprintf(rp, sizeof rp, "%s/frame/rm", sim_shelf_dir());
          FILE *f = fopen(rp, "rb"); bool j0 = f != NULL; if (f) fclose(f);
          progression_save(&tank);
          f = fopen(rp, "rb"); bool j1 = f != NULL; if (f) fclose(f);
          if (!j0 && j1) { resurrect++; printf("ss2: X7 write %d refused (REMOVE said %d): no journal after REMOVE, one again after the next save (shelf_tidy wrote the paid mark back)\n", k, got); }
          int32_t w = tank.sd_balance; ss_boot(143);
          if (tank.sd_balance != w || rf_there(1) != (got < 0)) X2_FAIL("X7: write %d refused: the boot has %d SD (want %d), tank 1 %s", k, (int)tank.sd_balance, (int)w, rf_there(1) ? "there" : "gone");
      }
      printf("ss2: X7 a finished REMOVE's journal written back by the tidy: %d of the cases (harmless if the boot only deletes it)\n", resurrect);
      char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", pre); (void)system(cmd); }

    if (getenv("SS2_X9")) {   /* (X9 is deferred to 0.4.2: run only when asked - not a make check gate) */
    /* ---- X9: two welcomes owed at once - a birth here, and a fry of ours home from next door - then a reboot ---- */
    { sim_shelf_board(); ss_boot(151); progression_setup_done(&tank);
      static tank_t there; tank_init(&there, 0x5eee); tank_new_population(&there);
      fish_t fry = there.fish[0]; fry.stage = STAGE_FRY; fry.size = 0.4f; fry.ms_bits = MS_CROSSBRED;
      fry.parent_uid[0] = tank.fish[0].uid; fry.parent_uid[1] = 0xc0ffee03u;
      passport_t pp; passport_from_fish(&pp, &fry, tank.uid, 0);
      int s = progression_fry_adopt(&tank, &pp, LINK_LEFT); uint32_t fu = s >= 0 ? tank.fish[s].uid : 0;
      ss_boot(152);                                        /* (the load sets its homecoming from bubbles) */
      progression_stage_arrival(&tank); progression_woke(&tank);   /* a birth here: its welcome owed first */
      int nb = progression_newborn(); uint32_t bu = nb >= 0 ? tank.fish[nb].uid : 0;
      tank.clock = tank.link_restore_at; tank_link_tick(&tank, NULL); progression_tick(&tank, 0.1f);   /* the fry home: its welcome queued */
      int fs = x2_slot(fu);
      printf("ss2: X9 a birth here (slot %d, its welcome up) and our fry home from next door (slot %d, born_away %d): owed %d\n", nb, fs, fs >= 0 ? tank.fish[fs].born_away : -1, progression_newborn());
      progression_save(&tank); ss_boot(153);
      int w1 = progression_newborn(); bool first_ok = w1 >= 0 && tank.fish[w1].uid == bu;
      if (w1 >= 0) progression_newborn_done(&tank);
      int w2 = progression_newborn(); bool second_ok = w2 >= 0 && tank.fish[w2].uid == fu;
      printf("ss2: X9 after a reboot: first welcome %d (the birth's %d), then %d (the fry's %d)\n", w1, first_ok, w2, second_ok);
      if (!first_ok || !second_ok) X2_FAIL("X9: two welcomes owed, then a reboot: the queued one is lost (first %d ok %d, then %d ok %d)", w1, first_ok, w2, second_ok); }

    }
    sim_shelf_board();
    gift_attach(&tank, NULL);
    printf("ss2: %s (%d)\n", x2_bad ? "FAILED" : "PASS", x2_bad);
    return x2_bad ? 1 : 0;
}
