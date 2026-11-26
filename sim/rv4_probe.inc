/* RV4 reviewer probes (the fourth review of fix041-r2-saves2, 0.4.1 audit round 2), folded into the sim after
   dr3_fall.inc. R3's stamp (progression_carry_kept) is reverted (F1), so these run main.c's sequence as it is: a refused
   SWAP IN stamps nothing.
     --selftest-rv4 [letters]: make check runs C - a carry left on the flash, then a later SWAP IN refused, the boot keeps
       the keeper's wallet (red with the stamp). A and B (F2: a blob that reads but won't load, a reset after a blocked boot -
       the fresh tank saves zero markers; deferred) run by name.
     --selftest-rv4d, --selftest-rv4e (make check): R3's own case with a second SWAP IN refused at its tidy; a SWAP IN
       refused at its first write after the boot's save was refused (red with the stamp; two refused writes).
     --selftest-rv4s (by name): N5b, a second SWAP IN soak. */
static int v4_bad;
#define V4_FAIL(...) do { printf("RV4: FAIL: "); printf(__VA_ARGS__); printf("\n"); v4_bad++; } while (0)

/* stage a paid NEW TANK (journal left) or a paid REMOVE (journal + erase left); returns id, or 0 for no case */
static uint32_t v4_stage(int kind, int k, int *V) {
    const int W0 = 1000, P = SHELF_NEW_PRICE;
    sim_shelf_board(); ss_boot(801);
    if (kind == 0) {
        progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
        shelf_port_sim_fail(k); int i = progression_buy_tank(&tank, P, clock_port_now_unix()); shelf_port_sim_fail(-1);
        if (i < 0 || !d3_on("buy")) return 0;
        return shelf_get(i)->id;
    }
    tank_init(&tank, 9); tank_new_population(&tank); tank.sd_unlocks = SD_ITEM_CASTLE; progression_save(&tank);
    if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) return 0;
    ss_boot(802); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
    *V = shelf_refund_value(shelf_index_of(1), NULL);
    shelf_port_sim_fail_n(k, 2); int got = progression_remove_tank(&tank, shelf_index_of(1)); shelf_port_sim_fail(-1);
    if (got < 0 || !d3_on("rm")) return 0;
    return 1;
}
static void v4_damage(void) {
    const char *sp = getenv("POCKET_TANK_SAVE"); static uint8_t b[4096]; long n = ss_read(sp, b, sizeof b);
    b[23] = 1; ss_write(sp, b, (size_t)n);
}
static void v4_boot_raw(uint32_t seed) {
    memset(&s_ss_book, 0, sizeof s_ss_book); s_ss_book.persist = true;
    memset(&tank, 0, sizeof tank); tank_init(&tank, seed); gift_attach(&tank, &s_ss_book);
    shelf_boot(clock_port_now_unix()); progression_boot(&tank); progression_shelf_settle(&tank); progression_swap_carry(&tank);
}
/* A: the save's CONTENT will not load (n_fish 1): on the boards the blob reads, so saving is NOT blocked - the fresh tank
   saves over it (marks zero), then boots */
static void v4_content(void) {
    for (int kind = 0; kind < 2; kind++)
    for (int k = 0; k < 14; k++) {
        int V = 0; uint32_t id = v4_stage(kind, k, &V); if (!id) continue;
        v4_damage();
        v4_boot_raw(803);
        bool mid = rf_there(id), jmid = d3_on(kind ? "rm" : "buy");
        progression_setup_done(&tank); bool sv = progression_save(&tank);
        ss_boot(804); ss_boot(805);
        bool there = rf_there(id);
        printf("RV4 A %s (write %d): damaged-content boot: tank %u %s, journal %s; fresh tank saved %d; two boots later: tank %s, journal %d, wallet %d\n",
               kind ? "REMOVE paid" : "NEW TANK paid", k, (unsigned)id, mid ? "kept" : "ERASED", jmid ? "waits" : "gone", sv, there ? "THERE" : "GONE", d3_on(kind ? "rm" : "buy"), (int)tank.sd_balance);
        if (kind == 0 && !there) V4_FAIL("A: a paid NEW TANK erased one boot later (zero markers)");
        if (kind == 1 && there) V4_FAIL("A: a removed (paid) tank back one boot later (zero markers)");
    }
}
/* B: the save will not READ (blocked), the keeper resets in that session (the board's way out of a blocked save) */
static void v4_reset(void) {
    for (int kind = 0; kind < 2; kind++)
    for (int k = 0; k < 14; k++) {
        int V = 0; uint32_t id = v4_stage(kind, k, &V); if (!id) continue;
        v4_damage();
        persist_port_sim_block(true);
        v4_boot_raw(803);
        bool mid = rf_there(id);
        persist_port_sim_block(false);              /* (persist_port_erase clears s_save_blocked on the board) */
        progression_reset(&tank, 77);
        ss_boot(804); ss_boot(805);
        bool there = rf_there(id);
        printf("RV4 B %s (write %d): blocked boot: tank %u %s; reset in the session; two boots later: tank %s, journal %d, wallet %d\n",
               kind ? "REMOVE paid" : "NEW TANK paid", k, (unsigned)id, mid ? "kept" : "ERASED", there ? "THERE" : "GONE", d3_on(kind ? "rm" : "buy"), (int)tank.sd_balance);
        if (kind == 0 && !there) V4_FAIL("B: a paid NEW TANK erased after the reset (zero markers)");
        if (kind == 1 && there) V4_FAIL("B: a removed (paid) tank back after the reset (zero markers)");
    }
}
/* C: R3's stamp overwrites the marker of a carry still on the flash: a SWAP IN that succeeded, its carry applied at the
   boot and its delete refused; the keeper earns +50 and saves (tidy refused, the save lands); a second SWAP IN refused at
   its tidy (carry_kept stamps the new carry's marker); a save (tidy refused again, lands); a boot */
static void v4_carry_overwrite(int second) {
    sim_shelf_board(); ss_boot(861); progression_setup_done(&tank); tank.sd_balance = 500; progression_save(&tank);
    int b = shelf_new(0, clock_port_now_unix());
    if (b < 0 || shelf_new(0, clock_port_now_unix()) < 0) { V4_FAIL("C staging"); return; }
    if (!shelf_put_up(b)) { V4_FAIL("C swap"); return; }
    progression_carry_t c0; memset(&c0, 0, sizeof c0);
    { char cp[800]; snprintf(cp, sizeof cp, "%s/frame/carry", sim_shelf_dir()); FILE *cf = fopen(cp, "rb"); if (cf) { if (fread(&c0, 1, sizeof c0, cf) != sizeof c0) {} fclose(cf); } }
    ss_boot(862);
    int32_t w0 = tank.sd_balance;                     /* the carry applied: 500 */
    shelf_port_frame_set("carry", &c0, sizeof c0);     /* (its delete refused: it stays on the flash, owed) */
    shelf_port_sim_fail(0); shelf_carry_done(); shelf_port_sim_fail(-1);
    progression_sd_grant(&tank, 50);
    shelf_port_sim_fail(0); bool l1 = progression_save(&tank); shelf_port_sim_fail(-1);
    int other = -1; for (int q = 0; q < shelf_count(); q++) if (shelf_get(q)->id != 1) other = q;
    bool sw = true;
    if (second) {                                       /* main.c shelf_swap_in: save (tidy refused, lands), carry_take, swap */
        shelf_port_sim_fail(0); bool s0 = progression_save(&tank); shelf_port_sim_fail(-1);
        progression_carry_t c; progression_carry_take(&tank, &c);
        shelf_port_sim_fail(0); sw = shelf_swap(other, NULL, 0, 0, &c, sizeof c, clock_port_now_unix()); shelf_port_sim_fail(-1);
        (void)s0;
    }
    int32_t want = tank.sd_balance;
    shelf_port_sim_fail(0); bool l2 = progression_save(&tank); shelf_port_sim_fail(-1);
    bool carry = d3_on("carry");
    ss_boot(863); ss_boot(864);
    printf("RV4 C (second SWAP IN refused at its tidy: %d, swap %d): carry applied %d; +50 saved %d; save %d; carry on flash %d; the boot: tank %u with %d (want %d)%s\n",
           second, sw, (int)w0, l1, l2, carry, (unsigned)shelf_now()->id, (int)tank.sd_balance, (int)want, tank.sd_balance != want ? "   <-- WRONG" : "");
    if (tank.sd_balance != want) V4_FAIL("C second=%d: the wallet set back to the carry's", second);
}
static int selftest_rv4(const char *which) {
    v4_bad = 0;
    if (!which) which = "C";
    if (strchr(which, 'A')) v4_content();
    if (strchr(which, 'B')) v4_reset();
    if (strchr(which, 'C')) { v4_carry_overwrite(0); v4_carry_overwrite(1); }
    sim_shelf_board();
    printf("RV4: %s (%d)\n", v4_bad ? "FAILED" : "PASS", v4_bad);
    return v4_bad ? 1 : 0;
}
/* D: R3's own case (a SWAP IN refused in the session, its carry left on the flash, stamped), then the keeper tries SWAP IN
   again and that one is refused at its tidy (the carry's delete refused again): the stamp moves to a carry never written */
static int selftest_rv4d(void) {
    v4_bad = 0;
    for (int extra = 0; extra < 2; extra++) {
        sim_shelf_board(); ss_boot(821); progression_setup_done(&tank); tank.sd_balance = 500; progression_save(&tank);
        int b = shelf_new(0, clock_port_now_unix());
        if (b < 0 || !shelf_put_up(b)) { V4_FAIL("D staging"); return 1; }
        ss_boot(822); progression_setup_done(&tank); tank.sd_balance = 400; progression_save(&tank);
        int hit = -1;
        char pre[800]; snprintf(pre, sizeof pre, "%s.v4d", sim_shelf_dir()); ss_copy(sim_shelf_dir(), pre);
        for (int k = 0; k < 20 && hit < 0; k++) {
            ss_copy(pre, sim_shelf_dir()); ss_boot(822);
            shelf_port_sim_refused(); shelf_port_sim_fail_n(k, 2); bool ok = shelf_put_up(shelf_index_of(1)); shelf_port_sim_fail(-1);
            if (!ok && d3_on("carry") && !d3_on("swap")) hit = k;
        }
        if (hit < 0) { printf("RV4 D: no case\n"); return 1; }
        progression_sd_grant(&tank, -7);
        /* the second SWAP IN (sim shelf_put_up = main.c): its save's tidy refused (lands), the swap's tidy refused */
        bool sw = true;
        if (extra) {
            shelf_port_sim_fail(0); progression_save(&tank); shelf_port_sim_fail(-1);
            progression_carry_t c; progression_carry_take(&tank, &c);
            shelf_port_sim_fail(0); sw = shelf_swap(shelf_index_of(1), NULL, 0, 0, &c, sizeof c, clock_port_now_unix()); shelf_port_sim_fail(-1);
        }
        shelf_port_sim_fail(0); bool landed = progression_save(&tank); shelf_port_sim_fail(-1);
        int32_t want = tank.sd_balance; bool carry = d3_on("carry");
        ss_boot(823); ss_boot(824);
        printf("RV4 D (second SWAP IN refused at its tidy %d, swap %d): first swap refused at write %d, carry left; spent 7; save landed %d; carry on flash %d: the boot has tank %u with %d (want %d)%s\n",
               extra, sw, hit, landed, carry, (unsigned)shelf_now()->id, (int)tank.sd_balance, (int)want, tank.sd_balance != want ? "   <-- WRONG" : "");
        if (tank.sd_balance != want) V4_FAIL("D extra=%d", extra);
        d3_rm(pre);
    }
    printf("RV4D: %s (%d)\n", v4_bad ? "FAILED" : "PASS", v4_bad);
    return v4_bad ? 1 : 0;
}
/* E: the SWAP IN boot's save refused once (progression_swap_carry: the carry stays, no cleanup owed - tidy is clear);
   the keeper earns +50, saves land; a later SWAP IN refused at its first write (the outgoing slot) - before its own carry
   is written: carry_kept stamps a carry never written; a save; a boot */
static int selftest_rv4e(void) {
    v4_bad = 0;
    for (int stamp = 0; stamp < 1; stamp++) {          /* (the reverted stamp was pass 1) */
        sim_shelf_board(); ss_boot(861); progression_setup_done(&tank); tank.sd_balance = 500; progression_save(&tank);
        int b = shelf_new(0, clock_port_now_unix());
        if (b < 0 || shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(b)) { V4_FAIL("E staging"); return 1; }
        persist_port_sim_block(true); ss_boot(862); persist_port_sim_block(false);
        bool carry0 = d3_on("carry"); int32_t w0 = tank.sd_balance;
        progression_sd_grant(&tank, 50); bool l1 = progression_save(&tank);
        int other = -1; for (int q = 0; q < shelf_count(); q++) if (shelf_get(q)->id != 1) other = q;
        progression_save(&tank);
        progression_carry_t c; progression_carry_take(&tank, &c);
        shelf_port_sim_fail(0); bool sw = shelf_swap(other, NULL, 0, 0, &c, sizeof c, clock_port_now_unix()); shelf_port_sim_fail(-1);
        int32_t want = tank.sd_balance; bool l2 = progression_save(&tank); bool carry = d3_on("carry");
        ss_boot(863); ss_boot(864);
        printf("RV4 E (pass %d): SWAP IN boot's save refused: carry left %d, wallet %d; +50 saved %d; the second SWAP IN refused at its slot: %d; save %d, carry on flash %d: the boot has tank %u with %d (want %d)%s\n",
               stamp, carry0, (int)w0, l1, sw, l2, carry, (unsigned)shelf_now()->id, (int)tank.sd_balance, (int)want, tank.sd_balance != want ? "   <-- WRONG" : "");
        if (tank.sd_balance != want) V4_FAIL("E stamp=%d", stamp);
    }
    /* E2: the same carry left, then the later SWAP IN through the sim's shelf_put_up (main.c's sequence: save, take the
       carry, swap) with each one write refused in turn - every refused SWAP IN keeps the keeper's wallet (a stamp put back
       into the refused path goes red here) */
    {   int runs = 0, bad = 0;
        for (int k = 0; k < 24; k++) {                   /* (staged afresh each time: a boot would let the carry go) */
            sim_shelf_board(); ss_boot(865); progression_setup_done(&tank); tank.sd_balance = 500; progression_save(&tank);
            int b = shelf_new(0, clock_port_now_unix());
            if (b < 0 || shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(b)) { V4_FAIL("E2 staging"); return 1; }
            persist_port_sim_block(true); ss_boot(866); persist_port_sim_block(false);
            progression_sd_grant(&tank, 50); progression_save(&tank);
            if (!d3_on("carry")) { V4_FAIL("E2: no carry left"); break; }
            int other = -1; for (int q = 0; q < shelf_count(); q++) if (shelf_get(q)->id != shelf_now()->id) other = q;
            shelf_port_sim_refused(); shelf_port_sim_fail(k); bool sw = shelf_put_up(other); shelf_port_sim_fail(-1);
            if (sw || !shelf_port_sim_refused()) continue;
            runs++;
            int32_t want = tank.sd_balance;
            if (!progression_save(&tank)) progression_save(&tank);
            ss_boot(867); ss_boot(868);
            if (tank.sd_balance != want) { bad++; printf("RV4 E2 SWAP IN refused at write %d: the boot has tank %u with %d (want %d)   <-- WRONG\n", k, (unsigned)shelf_now()->id, (int)tank.sd_balance, (int)want); }
        }
        printf("RV4 E2 a carry left, a later SWAP IN refused at each write: %d runs, %d wrong\n", runs, bad);
        if (!runs) V4_FAIL("E2: no refused SWAP IN");
        if (bad) V4_FAIL("E2: %d refused SWAP IN(s) set the wallet back", bad); }
    printf("RV4E: %s (%d)\n", v4_bad ? "FAILED" : "PASS", v4_bad);
    sim_shelf_board(); return v4_bad ? 1 : 0;
}
/* N5b: N5's first SWAP IN (1..3 refused from each write; a clean boot after a good one), the keeper earns 13 and saves,
   then a SECOND SWAP IN (main.c's sequence) with one write refused at each write; the keeper spends 7; saves land; two
   boots: the wallet the keeper last saw (main.c's SWAP IN as it is: no stamp) */
static void v4_save_lands(void) { if (!progression_save(&tank)) progression_save(&tank); }
static bool v4_swap_in(int i) {
    if (!progression_save(&tank)) return false;
    progression_carry_t c; progression_carry_take(&tank, &c);
    return shelf_swap(i, NULL, 0, 0, &c, sizeof c, clock_port_now_unix());
}
static int selftest_rv4s(void) {
    char pre[800]; snprintf(pre, sizeof pre, "%s.v4s", sim_shelf_dir());
    sim_shelf_board(); ss_boot(841); progression_setup_done(&tank); tank.sd_balance = 500; progression_save(&tank);
    int b = shelf_new(0, clock_port_now_unix());
    if (b < 0 || shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(b)) { printf("RV4 N5b staging\n"); return 1; }
    ss_boot(842); progression_setup_done(&tank); tank.sd_balance = 400; progression_save(&tank);
    ss_copy(sim_shelf_dir(), pre);
    int total_bad = 0;
    for (int stamp = 0; stamp < 1; stamp++) {
        int runs = 0, bad = 0, stuck = 0, shown = 0;
        for (int k = -1; k < 24; k++)
        for (int n = 1; n <= 3; n++)
        for (int k2 = -1; k2 < 16; k2++)
        for (int n2 = 1; n2 <= 2; n2++) {
            if (k < 0 && n > 1) continue;
            if (k2 < 0 && n2 > 1) continue;
            ss_copy(pre, sim_shelf_dir()); ss_boot(842);
            shelf_port_sim_refused();
            if (k >= 0) shelf_port_sim_fail_n(k, n);
            bool ok = v4_swap_in(shelf_index_of(1)); shelf_port_sim_fail(-1);
            if (k >= 0 && !shelf_port_sim_refused()) continue;
            if (ok) ss_boot(843);
            if (shelf_new(0, clock_port_now_unix()) >= 0) { for (int q = shelf_count() - 1; q >= 0; q--) if (shelf_get(q)->id > 3) { shelf_remove(q); break; } }
            else if (shelf_free_places() > 0) { stuck++; continue; }
            progression_sd_grant(&tank, 13); v4_save_lands();
            uint32_t other = 0; for (int q = 0; q < shelf_count(); q++) if (shelf_get(q)->id != shelf_now()->id) other = shelf_get(q)->id;
            shelf_port_sim_refused();
            if (k2 >= 0) shelf_port_sim_fail_n(k2, n2);
            bool ok2 = v4_swap_in(shelf_index_of(other)); shelf_port_sim_fail(-1);
            if (k2 >= 0 && !shelf_port_sim_refused()) continue;
            int32_t want = tank.sd_balance;
            if (ok2) ss_boot(846);
            if (shelf_new(0, clock_port_now_unix()) < 0 && shelf_free_places() > 0) { stuck++; continue; }
            for (int q = shelf_count() - 1; q >= 0; q--) if (shelf_get(q)->id > 3) { shelf_remove(q); break; }
            progression_sd_grant(&tank, -7); want -= 7; v4_save_lands();
            ss_boot(844); ss_boot(845);
            runs++;
            if (tank.sd_balance != want) { bad++; if (shown++ < 12) printf("RV4 N5b stamp%d first k%d n%d (ok %d) second k%d n%d (ok %d): the boot has tank %u with %d (want %d)   <-- WRONG\n",
                stamp, k, n, ok, k2, n2, ok2, (unsigned)shelf_now()->id, (int)tank.sd_balance, (int)want); }
        }
        printf("RV4 N5b (stamp %d): %d runs, %d wrong (%d stuck skipped)\n", stamp, runs, bad, stuck);
        total_bad += bad;
    }
    d3_rm(pre); sim_shelf_board();
    return total_bad != 0;
}
