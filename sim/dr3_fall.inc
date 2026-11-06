/* F-all: every fixture loaded, loaded again, and after a save: the receipts byte */
static int selftest_dr3f(void) {
    static const char *fx[] = { "2026-09-04-1304-device.sav", "2026-09-13-1432-prebubble.sav", "2026-09-24-1656-sim.sav", "2026-09-29-1656.sav", "2026-09-29-1664-shrimp.sav", "2026-09-29-1672-v0.2.0.sav", "2026-10-02-1688-urchin.sav", "2026-10-04-1760-genes.sav", "2026-10-04-1840-ids.sav", "2026-10-05-1864-prints.sav", "2026-10-14-1688-fry.sav", "2026-10-15-1880-seahorse.sav", "2026-10-17-1904-pumpkin.sav", "2026-10-19-1912-shelf.sav", "2026-10-23-1928-lock.sav", "2026-10-24-1936-crab.sav", "2026-10-25-1944-findwait.sav", "2026-11-03-1960-v0.4.0-synth.sav", "2026-11-05-1960-v0.4.1-seam.sav" };
    for (int f = 0; f < 19; f++) {
        sim_shelf_board(); char cmd[1600]; snprintf(cmd, sizeof cmd, "cp testdata/saves/%s '%s'", fx[f], getenv("POCKET_TANK_SAVE")); (void)system(cmd);
        ss_boot(970 + f); uint8_t a = tank.sd_receipts; int n = tank.n_fish; int32_t w = tank.sd_balance;
        snprintf(cmd, sizeof cmd, "cp testdata/saves/%s '%s'", fx[f], getenv("POCKET_TANK_SAVE")); (void)system(cmd);
        ss_boot(990 + f); uint8_t b = tank.sd_receipts;
        progression_save(&tank); ss_boot(1010 + f); uint8_t c = tank.sd_receipts;
        printf("DR3F %-34s fish %d wallet %5d receipts %02x / %02x / after a save %02x\n", fx[f], n, (int)w, a, b, c);
    }
    sim_shelf_board();
    return 0;
}
/* DR2 A, ported: a 0.4.0 tank - fish 0 paid its trust here, fish 1 not; a gift (RECEIVED) marked paid; and the same
   stamped 0.3.9 (pre-0.4: every paid trust counts, the note ignored) */
static int selftest_dr3a(void) {
    static const uint32_t ST[2] = { 0x00040000u, 0x00030904u };
    for (int st = 0; st < 2; st++)
    for (int how = 0; how < 2; how++) {
        sim_shelf_board(); ss_boot(901 + how * 10); progression_setup_done(&tank); tank.sd_balance = 100;
        tank.fish[0].trust = 10.0f; tank.fish[1].trust = 5.0f;
        for (int k = 0; k < 10; k++) progression_tick(&tank, 0.1f);
        int g = x2_gift(902 + how * 10, 0);
        if (g < 0) { printf("DR3A: no gift\n"); return 1; }
        tank.fish[g].trust = how ? 9.6f : 10.0f;
        tank.fish[0].trust = 9.6f;                    /* (the paid fish startled since) */
        progression_save(&tank); x2_stamp(ST[st]); x2_byte(1687, 0);
        ss_boot(903 + how * 10);
        unsigned r1 = SD_RCPT_TRUST(tank.sd_receipts);
        int paid = x2_reset_pays(905 + how * 10);
        printf("DR3A stamp %06x gift trust %s: load: trust receipts %u (want %u); the reset's new pair paid +%d (want +%d)\n", ST[st], how ? "9.6" : "10", r1, st ? 2u : 1u, paid, st ? 0 : 15);
    }
    sim_shelf_board(); return 0;
}
/* G: NEW TANK paid, its journal left (delete refused); the reset CUT at each of its writes; two boots: tank + price once */
static int selftest_dr3g(void) {
    const int W0 = 1000, P = SHELF_NEW_PRICE; int bad = 0, runs = 0;
    char pre[800]; snprintf(pre, sizeof pre, "%s.d3g", sim_shelf_dir());
    sim_shelf_board(); ss_boot(721); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
    ss_copy(sim_shelf_dir(), pre);
    int k1 = -1;
    for (int k = 0; k < 12 && k1 < 0; k++) { ss_copy(pre, sim_shelf_dir()); ss_boot(721); shelf_port_sim_fail(k); int i = progression_buy_tank(&tank, P, clock_port_now_unix()); shelf_port_sim_fail(-1);
        char p[800]; snprintf(p, sizeof p, "%s/frame/buy", sim_shelf_dir()); FILE *f = fopen(p, "rb"); if (f) fclose(f); if (i >= 0 && f) k1 = k; }
    for (int n = 0; n < 30; n++) {
        ss_copy(pre, sim_shelf_dir()); ss_boot(721);
        shelf_port_sim_fail(k1); int i = progression_buy_tank(&tank, P, clock_port_now_unix()); shelf_port_sim_fail(-1);
        uint32_t id = shelf_get(i)->id;
        shelf_port_sim_cut(n); progression_reset(&tank, 723); int left = shelf_port_sim_cut(-1);
        ss_boot(724); ss_boot(725); runs++;
        bool ok = rf_there(id) && tank.sd_balance == W0 - P;
        printf("DR3G reset cut at %d: tank %s, %d SD%s\n", n, rf_there(id) ? "there" : "GONE", (int)tank.sd_balance, ok ? "" : "   <-- WRONG");
        if (!ok) bad++;
        if (left > 0) break;
    }
    printf("DR3G: %d runs, %d wrong\n", runs, bad);
    sim_shelf_board(); return bad != 0;
}
/* H2: SWAP IN; at the boot after it every tank save refused (NVS full), frame writes taken; then a boot at once (a cut,
   the night): the carry's 400 must still come */
static int selftest_dr3h(void) {
    sim_shelf_board(); ss_boot(861); progression_setup_done(&tank); tank.sd_balance = 500; progression_save(&tank);
    int b = shelf_new(0, clock_port_now_unix()); if (b < 0 || !shelf_put_up(b)) return 1;
    ss_boot(862); progression_setup_done(&tank); tank.sd_balance = 400; progression_save(&tank);
    if (!shelf_put_up(shelf_index_of(1))) return 1;
    persist_port_sim_block(true); ss_boot(863); int32_t w1 = tank.sd_balance; persist_port_sim_block(false);
    char p[800]; snprintf(p, sizeof p, "%s/frame/carry", sim_shelf_dir()); FILE *f = fopen(p, "rb"); bool c = f != NULL; if (f) fclose(f);
    ss_boot(864);
    printf("DR3H the SWAP IN boot with the tank's saves refused: wallet %d, carry on the flash %d; the next boot: tank %u with %d (want 400)%s\n",
           (int)w1, c, (unsigned)shelf_now()->id, (int)tank.sd_balance, tank.sd_balance == 400 ? "" : "   <-- WRONG");
    sim_shelf_board(); return tank.sd_balance != 400;
}
/* J: swaps back and forth, the wallet moving each time; two NEW TANKs, the second's price save refused */
static int selftest_dr3j(void) {
    int bad = 0;
    sim_shelf_board(); ss_boot(871); progression_setup_done(&tank); tank.sd_balance = 500; progression_save(&tank);
    int b = shelf_new(0, clock_port_now_unix()); if (b < 0) return 1;
    int32_t w = 500;
    for (int s = 0; s < 4; s++) {
        uint32_t other = 0; for (int q = 0; q < shelf_count(); q++) other = shelf_get(q)->id;
        if (!shelf_put_up(shelf_index_of(other))) return 1;
        ss_boot(872 + s); progression_setup_done(&tank);
        printf("DR3J swap %d: tank %u with %d (want %d)%s\n", s, (unsigned)shelf_now()->id, (int)tank.sd_balance, (int)w, tank.sd_balance == w ? "" : "   <-- WRONG");
        if (tank.sd_balance != w) { bad++; tank.sd_balance = w; }
        tank.sd_balance += 11; w += 11; progression_save(&tank);
    }
    tank.sd_balance = 1000; progression_save(&tank);
    int i1 = progression_buy_tank(&tank, SHELF_NEW_PRICE, clock_port_now_unix());
    persist_port_sim_block(true); int i2 = progression_buy_tank(&tank, SHELF_NEW_PRICE, clock_port_now_unix()); persist_port_sim_block(false);
    int n0 = shelf_count(); int32_t w0 = tank.sd_balance;
    ss_boot(880);
    printf("DR3J two NEW TANKs (the second's save refused: %d): %d tanks %d SD, after a boot %d tanks %d SD%s\n", i2, n0, (int)w0, shelf_count(), (int)tank.sd_balance, (shelf_count() == n0 && tank.sd_balance == w0) ? "" : "   <-- WRONG");
    if (!(shelf_count() == n0 && tank.sd_balance == w0)) bad++;
    (void)i1; sim_shelf_board(); return bad != 0;
}
