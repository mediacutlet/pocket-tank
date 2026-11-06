/* ---- --selftest-saves-audit (0.4.1 audit phase 2, reviewer "saves") ---- */
#define SA_FAIL(...) do { printf("saves-audit: FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } while (0)
static void sa_fw_boot(void) { shelf_reboot(); }
#if 0
    tank_init(&tank, 5);
    shelf_boot(clock_port_now_unix());
    progression_boot(&tank);
    progression_shelf_settle(&tank);
    progression_carry_t c;
    if (shelf_take_carry(&c, sizeof c)) { progression_carry_give(&tank, &c); progression_save(&tank); }
#endif
static void sa_rm(const char *p) { char cmd[800]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", p); (void)system(cmd); }
/* H1: the boot right after a SWAP IN, cut at each write: the carried wallet must survive */
static int sa_swap_carry(void) {
    int fails = 0;
    sim_shelf_board(); sa_fw_boot(); progression_setup_done(&tank);
    tank.sd_balance = 50; progression_save(&tank);              /* tank 1: 50 */
    int b = shelf_new(0, clock_port_now_unix());
    if (b < 0 || !shelf_put_up(b)) { SA_FAIL("H1 staging: the first swap"); return fails; }
    sa_fw_boot(); progression_setup_done(&tank);                 /* tank 2 on the glass, the wallet 50 carried */
    tank.sd_balance = 1000; progression_save(&tank);             /* tank 2 earns: 1000 (tank 1's shelved save still says 50) */
    if (!shelf_put_up(shelf_index_of(1))) { SA_FAIL("H1 staging: the swap back"); return fails; }
    char pre[700]; snprintf(pre, sizeof pre, "%s.pre", sim_shelf_dir());
    bc_dir(sim_shelf_dir(), pre);                                /* the board as the restart finds it */
    int lost = 0, runs = 0;
    for (int m = 0; m < 12; m++) {
        bc_dir(pre, sim_shelf_dir());
        shelf_port_sim_cut(m); sa_fw_boot(); int left = shelf_port_sim_cut(-1);
        sa_fw_boot(); runs++;                                    /* ... and a clean boot */
        if (shelf_now()->id != 1 || tank.sd_balance != 1000) {
            lost++;
            SA_FAIL("H1: the boot after SWAP IN cut at its write %d: tank %u on the glass, the wallet %d (want tank 1 and 1000)", m, (unsigned)shelf_now()->id, (int)tank.sd_balance);
        }
        if (left > 0) break;                                     /* the boot needed fewer writes: every one covered */
    }
    printf("saves-audit: H1 swap-carry: %d boot cuts, %d lost the carried wallet\n", runs, lost);
    sa_rm(pre);
    return fails;
}
/* H2: NEW TANK whose journal delete was refused; the wallet back to its old figure by the next boot: the tank must stay */
static int sa_buy_stale(void) {
    int fails = 0;
    const int W0 = 1000, P = SHELF_NEW_PRICE;
    sim_shelf_board(); sa_fw_boot(); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
    char pre[700]; snprintf(pre, sizeof pre, "%s.pre", sim_shelf_dir()); bc_dir(sim_shelf_dir(), pre);
    int hit = 0;
    for (int k = 0; k < 20; k++) {
        bc_dir(pre, sim_shelf_dir()); sa_fw_boot();
        shelf_port_sim_fail(k); int i = progression_buy_tank(&tank, P, clock_port_now_unix()); int f = shelf_port_sim_fail(-1);
        if (f >= 0) break;                                       /* (never fired: past the buy's writes) */
        if (i < 0 || !bc_journal()) continue;                    /* we want: bought, paid, the journal left */
        hit++;
        uint32_t id = shelf_get(i)->id;
        char snap[700]; snprintf(snap, sizeof snap, "%s.h2", sim_shelf_dir()); bc_dir(sim_shelf_dir(), snap);
        progression_sd_grant(&tank, P);                          /* (a) the keeper earns (or sells) exactly the price back */
        progression_save(&tank);
        sa_fw_boot();
        bool there = false; for (int s = 0; s < shelf_count(); s++) there |= shelf_get(s)->id == id;
        if (!there) SA_FAIL("H2a: NEW TANK bought (write %d refused: the journal's delete), %d SD earned back: the boot erased the paid tank (wallet %d)", k, P, (int)tank.sd_balance);
        else printf("saves-audit: H2a: write %d refused, the tank kept\n", k);
        sa_rm(snap);
        bc_dir(pre, sim_shelf_dir()); sa_fw_boot();              /* (b) the same buy, then the keeper REMOVEs the fresh tank (refund 0) */
        shelf_port_sim_fail(k); i = progression_buy_tank(&tank, P, clock_port_now_unix()); shelf_port_sim_fail(-1);
        int got = i >= 0 ? progression_remove_tank(&tank, i) : -1;
        int w_before = (int)tank.sd_balance;
        sa_fw_boot();
        if (got >= 0 && tank.sd_balance != w_before) SA_FAIL("H2b: NEW TANK (write %d refused: the journal's delete), then REMOVE it (+%d): the boot paid %d SD back (wallet %d -> %d)", k, got, (int)tank.sd_balance - w_before, w_before, (int)tank.sd_balance);
    }
    if (!hit) SA_FAIL("H2: no write left the journal behind (harness)");
    sa_rm(pre);
    return fails;
}
/* H3: a carry whose delete is refused: the next boot sets the wallet back */
static int sa_carry_stale(void) {
    int fails = 0;
    sim_shelf_board(); sa_fw_boot(); progression_setup_done(&tank);
    tank.sd_balance = 400; progression_save(&tank);
    int b = shelf_new(0, clock_port_now_unix());
    if (b < 0 || !shelf_put_up(b)) { SA_FAIL("H3 staging"); return fails; }
    char pre[700]; snprintf(pre, sizeof pre, "%s.pre", sim_shelf_dir()); bc_dir(sim_shelf_dir(), pre);
    int hit = 0;
    for (int k = 0; k < 12; k++) {
        bc_dir(pre, sim_shelf_dir());
        shelf_port_sim_fail(k); sa_fw_boot(); int f = shelf_port_sim_fail(-1);
        if (f >= 0) break;
        char p[700]; snprintf(p, sizeof p, "%s/frame/carry", sim_shelf_dir()); FILE *fp = fopen(p, "rb"); if (!fp) continue; fclose(fp);
        hit++;
        progression_setup_done(&tank);
        tank.sd_balance = 100; progression_save(&tank);
        sa_fw_boot();
        if (tank.sd_balance != 100) SA_FAIL("H3: the boot after SWAP IN with write %d refused (the carry's delete): the next boot set the wallet %d -> %d", k, 100, (int)tank.sd_balance);
    }
    printf("saves-audit: H3: %d boots left the carry behind\n", hit);
    sa_rm(pre);
    return fails;
}
/* H4: a 0.4.0 save (1960 B: the 0.4.1 bytes zero, its release 0.4.0) loads and saves back the same, byte for byte, but
   for the stamp, the release and the 0.4.1 fields */
static bool sa_read(const char *p, uint8_t *b, size_t *n) { FILE *f = fopen(p, "rb"); if (!f) return false; *n = fread(b, 1, 8192, f); fclose(f); return true; }
static bool sa_write(const char *p, const uint8_t *b, size_t n) { FILE *f = fopen(p, "wb"); if (!f) return false; fwrite(b, 1, n, f); fclose(f); return true; }
static int sa_fixture_040(void) {
    int fails = 0;
    sim_shelf_board(); sa_fw_boot(); progression_setup_done(&tank);
    while (tank.n_fish < 4) tank_add_fish(&tank, 0, 1);
    tank.sd_balance = 5000;
    for (int k = 0; k < SD_ITEM_COUNT; k++) progression_buy(&tank, k);
    progression_shelve(&tank, 2); progression_lock(&tank, 3, true);
    tank.sd_balance = 1234; progression_save(&tank);
    tank_init(&tank, 5); shelf_boot(clock_port_now_unix()); progression_boot(&tank); progression_save(&tank);   /* (settled: what a load derives) */
    char sp[700]; snprintf(sp, sizeof sp, "%s/tank.sav", sim_shelf_dir());
    static uint8_t a[8192], b[8192]; size_t na = 0, nb = 0;
    if (!sa_read(sp, a, &na) || na != 1976) { SA_FAIL("H4: the save is %u B (want 1976)", (unsigned)na); return fails; }
    na = 1960;                                                             /* (0.4.0 wrote 1960 B: the shelf's markers came in 0.4.1 round 2) */
    a[1609] = 0; a[1687] = 0; a[1861] = 0; a[1956] = 0;                  /* 0.4.0 wrote zeros there */
    uint32_t r040 = 0x00040000u; memcpy(a + 1664, &r040, 4);
    sa_write(sp, a, na);
    tank_init(&tank, 5); shelf_boot(clock_port_now_unix()); progression_boot(&tank);
    if (progression_loaded_release() != r040) SA_FAIL("H4: loaded release %08x", (unsigned)progression_loaded_release());
    progression_save(&tank);
    sa_read(sp, b, &nb);
    int diffs = 0;
    for (size_t i = 0; i < na && i < nb; i++) {
        if (a[i] == b[i]) continue;
        if ((i >= 8 && i < 16) || (i >= 1664 && i < 1668) || (i >= 16 && i < 20)) continue;   /* stamp, release, clock */
        if (i == 1687 && b[i] == (a[i] | SD_RCPT_KNOWN)) continue;   /* (the receipts: folded once, marked known - round 2 FOLD) */
        if (diffs++ < 12) printf("saves-audit: H4: byte %u: 0.4.0 %02x, saved back %02x\n", (unsigned)i, a[i], b[i]);
    }
    if (nb != 1976) SA_FAIL("H4: saved back %u B (want 1976)", (unsigned)nb);
    for (size_t i = 1960; i < nb; i++) if (b[i]) { SA_FAIL("H4: byte %u of the new tail is %02x (want zeros)", (unsigned)i, b[i]); break; }
    if (diffs) SA_FAIL("H4: %d bytes changed through a 0.4.1 load and save", diffs);
    else printf("saves-audit: H4: a 0.4.0 save (1960 B, every item, a shelved piece, a lock) loads and saves back unchanged\n");
    return fails;
}
/* H1b: the same window, the tank coming in fresh (a NEW TANK never on the glass): no save to fall back on */
static int sa_swap_carry_fresh(void) {
    int fails = 0;
    sim_shelf_board(); sa_fw_boot(); progression_setup_done(&tank);
    tank.sd_balance = 1000; progression_save(&tank);
    int b = progression_buy_tank(&tank, SHELF_NEW_PRICE, clock_port_now_unix());
    if (b < 0 || !shelf_put_up(b)) { SA_FAIL("H1b staging"); return fails; }
    char pre[700]; snprintf(pre, sizeof pre, "%s.pre", sim_shelf_dir()); bc_dir(sim_shelf_dir(), pre);
    int lost = 0, runs = 0;
    for (int m = 0; m < 12; m++) {
        bc_dir(pre, sim_shelf_dir());
        shelf_port_sim_cut(m); sa_fw_boot(); int left = shelf_port_sim_cut(-1);
        sa_fw_boot(); runs++;
        if (tank.sd_balance != 1000 - SHELF_NEW_PRICE) { lost++; SA_FAIL("H1b: SWAP IN to a fresh tank, its boot cut at write %d: the wallet %d (want %d)", m, (int)tank.sd_balance, 1000 - SHELF_NEW_PRICE); }
        if (left > 0) break;
    }
    printf("saves-audit: H1b swap-carry (fresh tank): %d boot cuts, %d lost the wallet\n", runs, lost);
    sa_rm(pre);
    return fails;
}
static int selftest_saves_audit(void) {
    int f = 0;
    f += sa_swap_carry();
    f += sa_swap_carry_fresh();
    f += sa_buy_stale();
    f += sa_carry_stale();
    f += sa_fixture_040();
    printf("saves-audit: %s (%d)\n", f ? "FAILED" : "PASS", f);
    return f ? 1 : 0;
}
