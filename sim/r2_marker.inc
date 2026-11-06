/* --selftest-r2-marker (0.4.1 audit round 2, docs/audit-0.4.1/round2/FIX-saves2.md): the shelf's records read "paid" /
 * "applied" by the marker the paying save carries, never by the wallet. Included into sim/main.c after ss2_harness.inc
 * (ss_boot, ss_copy, rf_there).
 *   Red without the marker (4de08e7): A (a carry left, a save lands), C (a staged fry + NEW TANK cut: a free tank),
 *   D (a SWAP IN birth's +20 over a carry left), E1-E4 (journals left, the wallet moved onto each misreading figure),
 *   H (a SWAP IN birth, each write refused).
 *   Green on base too - each pins a sibling: B (a record never keyed to a wallet that did not land: DR2's R2), F (the
 *   reset carries the markers: mutation-proven), G (a SWAP IN with no birth gives exactly the carry), I (a fry born at a
 *   cold boot is saved at once: DR2's R3), K (NEW TANK's journal waits for its slot's erase - the third review's
 *   M02/M34: mutation-proven) */
void persist_port_sim_block(bool on);
static int mk_bad;
#define MK_FAIL(...) do { printf("r2-marker: FAIL: "); printf(__VA_ARGS__); printf("\n"); mk_bad++; } while (0)
static bool mk_on_flash(const char *key) { char p[800]; snprintf(p, sizeof p, "%s/frame/%s", sim_shelf_dir(), key); FILE *f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }
/* A (DR2-saves B / ss2 X6): the swap's carry left on the flash after the SWAP IN boot (its delete refused); the keeper
   spends to 55 and a save lands (its cleanup delete refused again); a boot keeps 55 */
static void mk_carry_left_save(void) {
    sim_shelf_board(); ss_boot(931); progression_setup_done(&tank); tank.sd_balance = 400; progression_save(&tank);
    if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) { MK_FAIL("A: staging"); return; }
    progression_carry_t c; memset(&c, 0, sizeof c);
    { char cp[800]; snprintf(cp, sizeof cp, "%s/frame/carry", sim_shelf_dir()); FILE *cf = fopen(cp, "rb"); if (cf) { if (fread(&c, 1, sizeof c, cf) != sizeof c) {} fclose(cf); } }
    ss_boot(932); progression_setup_done(&tank); progression_save(&tank);   /* (the SWAP IN boot: the carry applied, saved, let go) */
    shelf_port_frame_set("carry", &c, sizeof c);                          /* ... as if its delete had been refused */
    shelf_port_sim_fail(0); shelf_carry_done(); shelf_port_sim_fail(-1);  /* (the cleanup owed) */
    tank.sd_balance = 55;
    shelf_port_sim_refused(); shelf_port_sim_fail(0); bool landed = progression_save(&tank); shelf_port_sim_fail(-1);   /* (its delete refused again) */
    bool refused = shelf_port_sim_refused() > 0;
    if (!landed) progression_save(&tank);                                  /* (that refusal was the save itself: this one lands) */
    ss_boot(933);
    if (tank.sd_balance != 55) MK_FAIL("A: the carry (400) left on the flash, the keeper's 55 saved (a refusal %d): the next boot's wallet %d", refused, (int)tank.sd_balance);
    else printf("r2-marker: A a carry left on the flash, the keeper's 55 saved: the next boot keeps 55\n");
}
/* B (DR2-saves C): the carry left, the wallet moved, the tank save itself refused; a boot reads the save on the flash */
static void mk_carry_left_refused_save(void) {
    sim_shelf_board(); ss_boot(941); progression_setup_done(&tank); tank.sd_balance = 400; progression_save(&tank);
    if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) { MK_FAIL("B: staging"); return; }
    int k = 0;
    for (; k < 8; k++) {
        if (k) { sim_shelf_board(); ss_boot(941); progression_setup_done(&tank); tank.sd_balance = 400; progression_save(&tank);
                 if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) { MK_FAIL("B: staging"); return; } }
        shelf_port_sim_fail(k); ss_boot(942); shelf_port_sim_fail(-1);
        if (mk_on_flash("carry")) break;
    }
    progression_setup_done(&tank); progression_save(&tank);
    int32_t on_flash = tank.sd_balance;
    tank.sd_balance = 900;                                   /* (a sale, say: +500) */
    shelf_port_sim_fail(0); persist_port_sim_block(true); bool landed = progression_save(&tank); persist_port_sim_block(false); shelf_port_sim_fail(-1);
    ss_boot(943);
    if (landed || tank.sd_balance != on_flash) MK_FAIL("B: the save of 900 refused (landed %d) over %d on the flash: the next boot's wallet %d", landed, (int)on_flash, (int)tank.sd_balance);
    else printf("r2-marker: B the carry left, the wallet moved and its save refused: the boot reads the save on the flash (%d)\n", (int)on_flash);
}
/* C (DR2-saves E): a fry staged in the save, NEW TANK cut at each of its writes; the boot births the fry (+20) before the
   settle - the settle never reads the wallet the birth moved */
static void mk_staged_fry_buy_cut(void) {
    const int W0 = 1000, P = SHELF_NEW_PRICE;
    char pre[800]; snprintf(pre, sizeof pre, "%s.mkc", sim_shelf_dir());
    sim_shelf_board(); ss_boot(961); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
    ss_copy(sim_shelf_dir(), pre);
    ss_boot(962); progression_stage_arrival(&tank); shelf_port_sim_cut(1000); progression_buy_tank(&tank, P, clock_port_now_unix()); int used = 1000 - shelf_port_sim_cut(-1);
    int bad = 0, runs = 0;
    for (int n = 0; n <= used; n++) {
        ss_copy(pre, sim_shelf_dir()); ss_boot(962); progression_stage_arrival(&tank);
        shelf_port_sim_cut(n); progression_buy_tank(&tank, P, clock_port_now_unix()); shelf_port_sim_cut(-1);
        ss_boot(963); ss_boot(964); runs++;
        int tanks = shelf_count(), w = (int)tank.sd_balance, born = tank.n_fish > 2 ? SD_BIRTH : 0;
        if (!((tanks == 0 && w == W0 + born) || (tanks == 1 && w == W0 - P + born))) { bad++; printf("r2-marker: C cut %d of %d: %d tank(s), wallet %d (the fry %s)\n", n, used, tanks, w, born ? "born" : "not born"); }
    }
    if (bad) MK_FAIL("C: a staged fry and a NEW TANK cut: a tank and its price apart (%d of %d cuts)", bad, runs);
    else printf("r2-marker: C a staged fry and a NEW TANK cut at each of its %d writes: the tank and its price together every time\n", used);
    char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", pre); (void)system(cmd);
}
/* D (DR-saves P2 / ss2 X5): a fry born at the SWAP IN boot keeps its +20 with the carry left on the flash and a boot after */
static void mk_swap_birth_carry_left(void) {
    sim_shelf_board(); ss_boot(621); progression_setup_done(&tank); tank.sd_balance = 300;
    progression_stage_arrival(&tank); progression_save(&tank);
    int bi = shelf_new(0, clock_port_now_unix());
    if (bi < 0 || !shelf_put_up(bi)) { MK_FAIL("D: staging"); return; }
    ss_boot(622); progression_setup_done(&tank); tank.sd_balance = 500; progression_save(&tank);
    if (!shelf_put_up(shelf_index_of(1))) { MK_FAIL("D: the swap back"); return; }
    progression_carry_t c; memset(&c, 0, sizeof c);
    { char cp[800]; snprintf(cp, sizeof cp, "%s/frame/carry", sim_shelf_dir()); FILE *cf = fopen(cp, "rb"); if (cf) { if (fread(&c, 1, sizeof c, cf) != sizeof c) {} fclose(cf); } }
    ss_boot(623);
    int n1 = tank.n_fish; int32_t w1 = tank.sd_balance;
    shelf_port_frame_set("carry", &c, sizeof c);              /* (as if its delete had been refused, then a cut) */
    ss_boot(624);
    if (n1 != 3 || w1 != 500 + SD_BIRTH || tank.n_fish != 3 || tank.sd_balance != 500 + SD_BIRTH)
        MK_FAIL("D: a birth at the SWAP IN boot: %d fish %d, then (the carry left) %d fish %d (want 3 and %d)", n1, (int)w1, tank.n_fish, (int)tank.sd_balance, 500 + SD_BIRTH);
    else printf("r2-marker: D a birth at the SWAP IN boot keeps its +20, the carry left on the flash and a boot after\n");
}
/* E: the first review's unpinned mutations as scenarios - a journal left on the flash (its delete refused, and again at
   the keeper's save, which lands), the wallet moved to exactly the figure that misread it, a boot */
static void mk_journals_moved(void) {
    const int W0 = 1000, P = SHELF_NEW_PRICE; const int e_bad0 = mk_bad;
    /* E1 NEW TANK paid, its journal left; the keeper earns the price back (the wallet = the journal's "before") */
    sim_shelf_board(); ss_boot(701); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
    { int i = -1; for (int k = 0; k < 12 && i < 0; k++) {
          sim_shelf_board(); ss_boot(701); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
          shelf_port_sim_refused(); shelf_port_sim_fail(k); i = progression_buy_tank(&tank, P, clock_port_now_unix()); shelf_port_sim_fail(-1);
          if (i < 0 || !mk_on_flash("buy")) i = -1; }
      if (i < 0) MK_FAIL("E1: no refused write left a paid NEW TANK's journal");
      else { uint32_t id = shelf_get(i)->id; progression_sd_grant(&tank, P); shelf_port_sim_fail(0); if (!progression_save(&tank)) progression_save(&tank); shelf_port_sim_fail(-1);   /* (the first write refused: the cleanup's delete - or, with none owed, the save itself: then the next lands) */
          ss_boot(702);
             if (!rf_there(id) || tank.sd_balance != W0) MK_FAIL("E1: NEW TANK paid, its journal left, the price earned back: tank %s, %d SD (want there, %d)", rf_there(id) ? "there" : "GONE", (int)tank.sd_balance, W0); } }
    /* E2 NEW TANK not paid (its pay refused), its journal left; the keeper spends exactly the price (the wallet = "paid") */
    { bool hit = false;
      for (int k = 0; k < 12 && !hit; k++) {
          sim_shelf_board(); ss_boot(703); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
          shelf_port_sim_refused(); shelf_port_sim_fail_n(k, 3); int i = progression_buy_tank(&tank, P, clock_port_now_unix()); shelf_port_sim_fail(-1);
          if (i >= 0 || !mk_on_flash("buy") || tank.sd_balance != W0) continue;
          hit = true;
          progression_sd_grant(&tank, -P); shelf_port_sim_fail(0); if (!progression_save(&tank)) progression_save(&tank); shelf_port_sim_fail(-1);   /* (the first write refused: the cleanup's delete - or, with none owed, the save itself: then the next lands) */
          ss_boot(704);
          if (shelf_count() != 0 || tank.sd_balance != W0 - P) MK_FAIL("E2: NEW TANK not paid, its journal left, %d spent: %d tank(s), %d SD (want none, %d)", P, shelf_count(), (int)tank.sd_balance, W0 - P);
      }
      if (!hit) MK_FAIL("E2: no refused write left an unpaid NEW TANK's journal"); }
    /* E3 REMOVE not paid (its pay refused), its tank there, its journal left; the keeper earns exactly the refund */
    { bool hit = false;
      for (int k = 0; k < 12 && !hit; k++) {
          sim_shelf_board(); ss_boot(705);
          tank_init(&tank, 9); tank_new_population(&tank); tank.sd_unlocks = SD_ITEM_CASTLE; progression_save(&tank);
          if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) { MK_FAIL("E3: staging"); break; }
          ss_boot(706); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
          const int V = shelf_refund_value(shelf_index_of(1), NULL);
          shelf_port_sim_refused(); shelf_port_sim_fail_n(k, 2); int got = progression_remove_tank(&tank, shelf_index_of(1)); shelf_port_sim_fail(-1);
          if (got >= 0 || !mk_on_flash("rm") || !rf_there(1)) continue;
          hit = true;
          progression_sd_grant(&tank, V); shelf_port_sim_fail(0); if (!progression_save(&tank)) progression_save(&tank); shelf_port_sim_fail(-1);   /* (the first write refused: the cleanup's delete - or, with none owed, the save itself: then the next lands) */
          ss_boot(707);
          if (!rf_there(1) || tank.sd_balance != W0 + V) MK_FAIL("E3: REMOVE not paid, its journal left, the refund earned: tank 1 %s, %d SD (want there, %d)", rf_there(1) ? "there" : "GONE", (int)tank.sd_balance, W0 + V);
      }
      if (!hit) MK_FAIL("E3: no refused write left an unpaid REMOVE's journal with its tank"); }
    /* E4 REMOVE paid, its erase refused for good (the slot still there) and its journal left; the keeper spends the
       refund back (the wallet = "not paid"); a boot erases it */
    { bool hit = false;
      for (int k = 0; k < 12 && !hit; k++) {
          sim_shelf_board(); ss_boot(708);
          tank_init(&tank, 9); tank_new_population(&tank); tank.sd_unlocks = SD_ITEM_CASTLE; progression_save(&tank);
          if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) { MK_FAIL("E4: staging"); break; }
          ss_boot(709); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
          const int V = shelf_refund_value(shelf_index_of(1), NULL);
          shelf_port_sim_refused(); shelf_port_sim_fail_n(k, 3); int got = progression_remove_tank(&tank, shelf_index_of(1)); shelf_port_sim_fail(-1);
          if (got < 0 || !mk_on_flash("rm")) continue;
          hit = true;
          progression_sd_grant(&tank, -V); shelf_port_sim_fail(0); if (!progression_save(&tank)) progression_save(&tank); shelf_port_sim_fail(-1);   /* (the first write refused: the cleanup's delete - or, with none owed, the save itself: then the next lands) */
          ss_boot(710);
          if (rf_there(1) || tank.sd_balance != W0) MK_FAIL("E4: REMOVE paid, its journal left, the refund spent: tank 1 %s, %d SD (want gone, %d)", rf_there(1) ? "BACK" : "gone", (int)tank.sd_balance, W0);
      }
      if (!hit) MK_FAIL("E4: no refused write left a paid REMOVE's journal"); }
    if (mk_bad == e_bad0) printf("r2-marker: E journals left on the flash, the wallet moved onto each misreading figure: every boot right\n");
}
/* F: a reset keeps the marks - NEW TANK paid, its journal left (its delete refused), then the reset in the same session
   with each of its writes refused in turn; where the reset is done and the journal still there, a boot: the tank stays,
   its price paid once */
static void mk_reset_keeps(void) {
    const int W0 = 1000, P = SHELF_NEW_PRICE;
    char pre[800]; snprintf(pre, sizeof pre, "%s.mkf", sim_shelf_dir());
    sim_shelf_board(); ss_boot(721); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
    ss_copy(sim_shelf_dir(), pre);
    int k1 = -1;
    for (int k = 0; k < 12 && k1 < 0; k++) {
        ss_copy(pre, sim_shelf_dir()); ss_boot(721);
        shelf_port_sim_fail(k); int i = progression_buy_tank(&tank, P, clock_port_now_unix()); shelf_port_sim_fail(-1);
        if (i >= 0 && mk_on_flash("buy")) k1 = k;
    }
    if (k1 < 0) { MK_FAIL("F: no refused write left a paid NEW TANK's journal"); return; }
    int runs = 0, bad = 0;
    for (int k = 0; k < 16; k++) {
        ss_copy(pre, sim_shelf_dir()); ss_boot(721);
        shelf_port_sim_fail(k1); int i = progression_buy_tank(&tank, P, clock_port_now_unix()); shelf_port_sim_fail(-1);
        uint32_t id = i >= 0 ? shelf_get(i)->id : 0, old = tank.uid;
        shelf_port_sim_refused(); shelf_port_sim_fail(k); progression_reset(&tank, 723); shelf_port_sim_fail(-1);
        if (!shelf_port_sim_refused()) break;
        if (!mk_on_flash("buy") || tank.uid == old) continue;  /* (the case: the reset done, the journal still there) */
        ss_boot(724); runs++;
        if (!rf_there(id) || tank.sd_balance != W0 - P) { bad++; printf("r2-marker: F reset write %d refused: tank %s, %d SD (want there, %d)\n", k, rf_there(id) ? "there" : "GONE", (int)tank.sd_balance, W0 - P); }
    }
    if (!runs) MK_FAIL("F: no refused write of the reset left the paid NEW TANK's journal");
    else if (bad) MK_FAIL("F: NEW TANK paid, its journal left over a reset: %d of %d runs wrong", bad, runs);
    else printf("r2-marker: F NEW TANK paid, its journal left over the reset (%d runs): the tank stays, paid once\n", runs);
    char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", pre); (void)system(cmd);
}
/* G: a SWAP IN boot with no birth gives the carry exactly, over two boots (nothing added - the old X5 add-on's sibling) */
static void mk_swap_no_birth(void) {
    sim_shelf_board(); ss_boot(731); progression_setup_done(&tank); tank.sd_balance = 300; progression_save(&tank);
    int bi = shelf_new(0, clock_port_now_unix());
    if (bi < 0 || !shelf_put_up(bi)) { MK_FAIL("G: staging"); return; }
    ss_boot(732); progression_setup_done(&tank); tank.sd_balance = 500; progression_save(&tank);
    if (!shelf_put_up(shelf_index_of(1))) { MK_FAIL("G: the swap back"); return; }
    ss_boot(733); int n = tank.n_fish; int32_t w = tank.sd_balance; ss_boot(734);
    if (n != 2 || w != 500 || tank.sd_balance != 500) MK_FAIL("G: no birth: %d fish, the wallet %d, then %d (want 2 and 500)", n, (int)w, (int)tank.sd_balance);
    else printf("r2-marker: G a SWAP IN boot with no birth: the carry's 500 exactly, over two boots\n");
}
/* H: the SWAP IN boot with a staged fry, each of its writes refused in turn, then a boot: the fry once, its +20 kept */
static void mk_swap_birth_refused(void) {
    char pre[800]; snprintf(pre, sizeof pre, "%s.mkh", sim_shelf_dir());
    sim_shelf_board(); ss_boot(741); progression_setup_done(&tank); tank.sd_balance = 300;
    progression_stage_arrival(&tank); progression_save(&tank);
    int bi = shelf_new(0, clock_port_now_unix());
    if (bi < 0 || !shelf_put_up(bi)) { MK_FAIL("H: staging"); return; }
    ss_boot(742); progression_setup_done(&tank); tank.sd_balance = 500; progression_save(&tank);
    if (!shelf_put_up(shelf_index_of(1))) { MK_FAIL("H: the swap back"); return; }
    ss_copy(sim_shelf_dir(), pre);
    int runs = 0, bad = 0;
    for (int k = 0; k < 16; k++) {
        ss_copy(pre, sim_shelf_dir());
        shelf_port_sim_refused(); shelf_port_sim_fail(k); ss_boot(743); shelf_port_sim_fail(-1);
        if (!shelf_port_sim_refused()) break;
        ss_boot(744); runs++;
        if (tank.n_fish != 3 || tank.sd_balance != 500 + SD_BIRTH) { bad++; printf("r2-marker: H write %d refused at the SWAP IN boot: then %d fish, %d SD (want 3, %d)\n", k, tank.n_fish, (int)tank.sd_balance, 500 + SD_BIRTH); }
    }
    if (bad) MK_FAIL("H: a SWAP IN birth with a write refused: %d of %d runs wrong", bad, runs);
    else printf("r2-marker: H each write of the SWAP IN boot refused, then a boot: the fry once, its +20 kept (%d runs)\n", runs);
    char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", pre); (void)system(cmd);
}
/* I (DR2-saves D, R3 gone): a fry staged in the save, born at a cold boot's wake, is saved at once - a cut after it gives
   the same fish at the next boot, not another born again */
static void mk_wake_birth_saved(void) {
    sim_shelf_board(); ss_boot(751); progression_setup_done(&tank); tank.sd_balance = 300;
    progression_stage_arrival(&tank); progression_save(&tank);
    ss_boot(752);
    uint32_t u1 = tank.n_fish > 2 ? tank.fish[2].uid : 0;
    ss_boot(753);                                            /* (the power cut before any later save) */
    uint32_t u2 = tank.n_fish > 2 ? tank.fish[2].uid : 0;
    if (!u1 || u1 != u2 || tank.sd_balance != 300 + SD_BIRTH) MK_FAIL("I: a fry born at a cold boot, then a cut: %08x then %08x, %d SD (want the same fish, %d)", (unsigned)u1, (unsigned)u2, (int)tank.sd_balance, 300 + SD_BIRTH);
    else printf("r2-marker: I a fry born at a cold boot is saved at once: the same fish after a cut\n");
}
/* K (the third review's M02/M34): NEW TANK with two writes in a row refused, from each write in turn - among them its
   price save AND its slot erase. The journal must wait for the erase (deleted first, at once or by the tidy, the slot
   would load at the next boot: a free tank). The keeper's save, with the cleanup's erase refused again, lands; two boots:
   no tank, nothing charged */
static void mk_buy_erase_owed(void) {
    const int W0 = 1000, P = SHELF_NEW_PRICE;
    int hits = 0, bad = 0;
    for (int k = 0; k < 12; k++) {                       /* (every write pair: the price save + the erase among them, write 5 today) */
        sim_shelf_board(); ss_boot(761); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
        shelf_port_sim_refused(); shelf_port_sim_fail_n(k, 2); int i = progression_buy_tank(&tank, P, clock_port_now_unix()); shelf_port_sim_fail(-1);
        int nref = shelf_port_sim_refused();
        if (i >= 0 || nref < 2 || tank.sd_balance != W0) continue;   /* (the journal on the flash or not: M34 deletes it at once) */
        hits++;
        progression_sd_grant(&tank, -7);
        shelf_port_sim_fail(0); bool landed = progression_save(&tank); shelf_port_sim_fail(-1);   /* (the cleanup's erase refused again) */
        if (!landed) progression_save(&tank);
        ss_boot(762); ss_boot(763);
        if (shelf_count() != 0 || tank.sd_balance != W0 - 7) { bad++; MK_FAIL("K: NEW TANK refused at writes %d-%d, then a save: %d tank(s), %d SD (want none, %d)", k, k + 1, shelf_count(), (int)tank.sd_balance, W0 - 7); }
    }
    if (!hits) MK_FAIL("K: no two refused writes left NEW TANK unbought");
    else if (!bad) printf("r2-marker: K NEW TANK refused two writes in a row from each (%d cases, its pay and erase among them): no free tank\n", hits);
}
static int selftest_r2_marker(void) {
    mk_bad = 0;
    mk_carry_left_save();
    mk_carry_left_refused_save();
    mk_staged_fry_buy_cut();
    mk_swap_birth_carry_left();
    mk_journals_moved();
    mk_reset_keeps();
    mk_swap_no_birth();
    mk_swap_birth_refused();
    mk_wake_birth_saved();
    mk_buy_erase_owed();
    sim_shelf_board();
    printf("r2-marker: %s (%d)\n", mk_bad ? "FAILED" : "PASS", mk_bad);
    return mk_bad ? 1 : 0;
}
