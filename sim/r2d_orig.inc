#undef R2D_FAIL
#define R2D_FAIL(...) do { printf("r2-saves: FAIL: D: "); printf(__VA_ARGS__); printf("\n"); return 1; } while (0)
static int r2_refused_runs_orig(void) {   /* D: selftest-shelf's shelf_refused_runs, its `fired` read by the counter (a run of two whose first refusal ends the action never "fired") */
    int seg[8] = {0}, si = 0;
    const int W0 = 1000, P = SHELF_NEW_PRICE;
    char pre[700]; snprintf(pre, sizeof pre, "%s.pre", sim_shelf_dir());
    int hits = 0;
    /* NEW TANK, writes refused (one, or two in a row), then the keeper earns P back (H2a) or spends (S8), then a boot */
    for (int run = 1; run <= 2; run++) { si = run - 1;
        sim_shelf_board(); shelf_reboot(); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
        bc_dir(sim_shelf_dir(), pre);
        for (int k = 0; k < 20; k++) {
            bool past = false;
            for (int act = 0; act < 2 && !past; act++) {
                bc_dir(pre, sim_shelf_dir()); shelf_reboot();
                shelf_port_sim_refused(); shelf_port_sim_fail_n(k, run); int i = progression_buy_tank(&tank, P, clock_port_now_unix()); bool fired = shelf_port_sim_fail(-1) < 0;
                if (!fired) { past = true; break; }
                bool bought = i >= 0; int32_t w = tank.sd_balance;
                if (bought != (w == W0 - P)) R2D_FAIL("refused NEW TANK (write %d, %d in a row): bought %d, the wallet %d", k, run, bought, (int)w);
                progression_sd_grant(&tank, act == 0 ? P : -7);   /* the keeper plays on: P earned back (H2a), or a spend */
                progression_save(&tank);
                int32_t want = tank.sd_balance;
                shelf_reboot(); hits++; seg[si]++;
                if (rf_there(2) != bought || tank.sd_balance != want)
                    R2D_FAIL("refused NEW TANK (write %d, %d in a row), then %s: the boot has the tank %s and %d SD (want %s and %d)", k, run, act ? "a spend" : "its price earned back",
                          rf_there(2) ? "there" : "gone", (int)tank.sd_balance, bought ? "there" : "gone", (int)want);
            }
            if (past) break;
        }
    }
    si = 2;
    /* NEW TANK with a write refused, then REMOVE of that tank at once (H2b: its journal left - no 300 minted at the boot) */
    sim_shelf_board(); shelf_reboot(); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
    bc_dir(sim_shelf_dir(), pre);
    for (int k = 0; k < 20; k++) {
        bc_dir(pre, sim_shelf_dir()); shelf_reboot();
        shelf_port_sim_refused(); shelf_port_sim_fail(k); int i = progression_buy_tank(&tank, P, clock_port_now_unix()); bool fired = shelf_port_sim_fail(-1) < 0;
        if (!fired) break;
        if (i < 0) continue;
        int got = progression_remove_tank(&tank, i); int32_t w = tank.sd_balance;
        shelf_reboot(); hits++; seg[si]++;
        if (tank.sd_balance != w || rf_there(2) != (got < 0))
            R2D_FAIL("NEW TANK (write %d refused), then REMOVE (%d): the boot has %d SD (want %d), the tank %s", k, got, (int)tank.sd_balance, (int)w, rf_there(2) ? "there" : "gone");
    }
    si = 3;
    /* REMOVE, writes refused (one, or two in a row), then the keeper spends, then a boot (S8) */
    for (int run = 1; run <= 2; run++) { si = 2 + run;
        sim_shelf_board(); shelf_reboot();
        tank_init(&tank, 9); tank_new_population(&tank); tank.sd_unlocks = SD_ITEM_CASTLE | SD_ITEM_SNAIL; progression_save(&tank);
        if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) R2D_FAIL("refused REMOVE: staging");
        shelf_reboot(); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
        const int V = shelf_refund_value(shelf_index_of(1), NULL);
        bc_dir(sim_shelf_dir(), pre);
        for (int k = 0; k < 20; k++) {
            bc_dir(pre, sim_shelf_dir()); shelf_reboot();
            shelf_port_sim_refused(); shelf_port_sim_fail_n(k, run); int got = progression_remove_tank(&tank, shelf_index_of(1)); bool fired = shelf_port_sim_fail(-1) < 0;
            if (!fired) break;
            bool paid = tank.sd_balance == W0 + V;
            if ((got >= 0) != paid) R2D_FAIL("refused REMOVE (write %d, %d in a row): it said %d, the wallet %d", k, run, got, (int)tank.sd_balance);
            progression_sd_grant(&tank, -7); progression_save(&tank);
            int32_t want = tank.sd_balance;
            shelf_reboot(); hits++; seg[si]++;
            if (rf_there(1) == paid || tank.sd_balance != want)
                R2D_FAIL("refused REMOVE (write %d, %d in a row), then a spend: the boot has tank 1 %s and %d SD (want %s and %d)", k, run,
                      rf_there(1) ? "there" : "gone", (int)tank.sd_balance, paid ? "gone" : "there", (int)want);
        }
    }
    si = 5;
    /* SWAP IN whose carry delete is refused, then the keeper plays (H3): the next boot never sets the wallet back */
    sim_shelf_board(); shelf_reboot(); progression_setup_done(&tank); tank.sd_balance = 400; progression_save(&tank);
    if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) R2D_FAIL("refused carry: staging");
    bc_dir(sim_shelf_dir(), pre);
    for (int k = 0; k < 12; k++) {
        bc_dir(pre, sim_shelf_dir());
        shelf_port_sim_refused(); shelf_port_sim_fail(k); shelf_reboot(); bool fired = shelf_port_sim_fail(-1) < 0;
        if (!fired) break;
        progression_setup_done(&tank); tank.sd_balance = 100; progression_save(&tank);
        shelf_reboot(); hits++; seg[si]++;
        if (tank.sd_balance != 100) R2D_FAIL("the boot after SWAP IN with write %d refused, then the wallet 100 saved: the next boot set it to %d", k, (int)tank.sd_balance);
    }
    { char cmd[800]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", pre); (void)system(cmd); }
    printf("r2-saves: D-orig runs: NEW TANK x1 %d, x2 %d; buy+REMOVE %d; REMOVE x1 %d, x2 %d; carry %d\n", seg[0], seg[1], seg[2], seg[3], seg[4], seg[5]); printf("r2-saves: D-orig (as in selftest-shelf): refused writes (NEW TANK one or two in a row, its journal then REMOVE, REMOVE one or two in a row, the swap's carry), the keeper playing on: every boot after reads them right (%d runs)\n", hits);
    return 0;
}
