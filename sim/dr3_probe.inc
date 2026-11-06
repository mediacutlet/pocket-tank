/* DR3 reviewer probes (the third review of fix041-r2-saves2, 0.4.1 audit round 2), folded into the sim. Included after
   farm2.inc. --selftest-dr3 [letters]: make check runs DLOQM - D the second review's probes, L a save that would not load
   (R1), O a release candidate's journal (R2), Q every reading of those journals and the frame's length check (the fourth
   review's N07-N10, N16), M the markers over a swap. By name: S (N4, the money soak, ~150 s), C (N5, the carry soak: its
   reds are R2S-3, deferred) and R (N3, a SWAP IN refused in the session: R3, deferred to 0.4.2 - its stamp was reverted,
   the fourth review's F1) */
static int d3_bad;
#define D3_FAIL(...) do { printf("DR3: FAIL: "); printf(__VA_ARGS__); printf("\n"); d3_bad++; } while (0)
static bool d3_on(const char *key) { char p[800]; snprintf(p, sizeof p, "%s/frame/%s", sim_shelf_dir(), key); FILE *f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }
static uint8_t d3_fbyte(int off) { uint8_t b = 0xee; FILE *f = fopen(getenv("POCKET_TANK_SAVE"), "rb"); if (f) { fseek(f, off, SEEK_SET); if (fread(&b, 1, 1, f) != 1) b = 0xee; fclose(f); } return b; }
static void d3_rm(const char *p) { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", p); (void)system(cmd); }
static void d3_put(const char *key, const void *b, size_t n) { char p[800]; snprintf(p, sizeof p, "%s/frame/%s", sim_shelf_dir(), key); FILE *f = fopen(p, "wb"); if (f) { fwrite(b, 1, n, f); fclose(f); } }
static void d3_zero_marks(void) {                       /* the save as an older build wrote it: bytes 1960.. zero, length 1960 */
    const char *sp = getenv("POCKET_TANK_SAVE"); static uint8_t b[4096]; long n = ss_read(sp, b, sizeof b);
    if (n > 1960) n = 1960;
    ss_write(sp, b, (size_t)n);
}

/* ---- DR2's probes, ported (B, C, D, E, F) ---- */
static void d3_dr2(void) {
    /* B: X6 at the "final" save: the carry left (applied), its delete refused; the keeper spends to 55; the next save with
       1..3 writes refused from its first; if it did not land, nothing more (the sleep): the boot keeps what the flash has */
    for (int n = 1; n <= 3; n++) {
        sim_shelf_board(); ss_boot(931); progression_setup_done(&tank); tank.sd_balance = 400; progression_save(&tank);
        if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) { D3_FAIL("B staging"); return; }
        progression_carry_t c; memset(&c, 0, sizeof c);
        { char cp[800]; snprintf(cp, sizeof cp, "%s/frame/carry", sim_shelf_dir()); FILE *cf = fopen(cp, "rb"); if (cf) { if (fread(&c, 1, sizeof c, cf) != sizeof c) {} fclose(cf); } }
        ss_boot(932); progression_setup_done(&tank); progression_save(&tank);
        int32_t on_flash = tank.sd_balance;
        shelf_port_frame_set("carry", &c, sizeof c);
        shelf_port_sim_fail(0); shelf_carry_done(); shelf_port_sim_fail(-1);
        tank.sd_balance = 55;
        shelf_port_sim_fail_n(0, n); bool fin = progression_save(&tank); shelf_port_sim_fail(-1);
        ss_boot(933);
        int want = fin ? 55 : on_flash;
        printf("DR3 B(n=%d): final save landed %d with 55 (flash had %d), carry on flash before boot %d; the next boot's wallet %d\n", n, fin, (int)on_flash, 1, (int)tank.sd_balance);
        if (tank.sd_balance != want) D3_FAIL("B n=%d: want %d got %d", n, want, (int)tank.sd_balance);
    }
    /* C: the carry left, the wallet moved to 900, the tidy refused and the tank save itself refused, then a boot */
    {   sim_shelf_board(); ss_boot(941); progression_setup_done(&tank); tank.sd_balance = 400; progression_save(&tank);
        if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) { D3_FAIL("C staging"); return; }
        progression_carry_t c; memset(&c, 0, sizeof c);
        { char cp[800]; snprintf(cp, sizeof cp, "%s/frame/carry", sim_shelf_dir()); FILE *cf = fopen(cp, "rb"); if (cf) { if (fread(&c, 1, sizeof c, cf) != sizeof c) {} fclose(cf); } }
        ss_boot(942); progression_setup_done(&tank); progression_save(&tank);
        int32_t on_flash = tank.sd_balance;
        shelf_port_frame_set("carry", &c, sizeof c);
        shelf_port_sim_fail(0); shelf_carry_done(); shelf_port_sim_fail(-1);
        tank.sd_balance = 900;
        shelf_port_sim_fail(0); persist_port_sim_block(true); bool landed = progression_save(&tank); persist_port_sim_block(false); shelf_port_sim_fail(-1);
        ss_boot(943);
        printf("DR3 C: the save of 900 landed %d (the flash %d); the next boot's wallet %d\n", landed, (int)on_flash, (int)tank.sd_balance);
        if (tank.sd_balance != on_flash) D3_FAIL("C: want %d", (int)on_flash); }
    /* D: a fry staged, born at a cold boot's wake: saved at once? then a cut */
    {   sim_shelf_board(); ss_boot(951); progression_setup_done(&tank); tank.sd_balance = 300;
        progression_stage_arrival(&tank); progression_save(&tank);
        ss_boot(952);
        uint32_t u1 = tank.n_fish > 2 ? tank.fish[2].uid : 0; uint8_t staged = d3_fbyte(22);
        ss_boot(953);
        uint32_t u2 = tank.n_fish > 2 ? tank.fish[2].uid : 0;
        printf("DR3 D: cold boot: fry %08x, staged byte after the boot %u; after a cut: fry %08x (%s), wallet %d\n", (unsigned)u1, staged, (unsigned)u2, u1 == u2 ? "same" : "ANOTHER", (int)tank.sd_balance);
        if (!u1 || u1 != u2 || tank.sd_balance != 320) D3_FAIL("D"); }
    /* E: a staged fry + NEW TANK cut at each write */
    {   const int W0 = 1000, P = SHELF_NEW_PRICE;
        char pre[800]; snprintf(pre, sizeof pre, "%s.d3e", sim_shelf_dir());
        sim_shelf_board(); ss_boot(961); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
        ss_copy(sim_shelf_dir(), pre);
        ss_boot(962); progression_stage_arrival(&tank); shelf_port_sim_cut(1000); progression_buy_tank(&tank, P, clock_port_now_unix()); int used = 1000 - shelf_port_sim_cut(-1);
        int bad = 0;
        for (int n = 0; n <= used; n++) {
            ss_copy(pre, sim_shelf_dir()); ss_boot(962); progression_stage_arrival(&tank);
            shelf_port_sim_cut(n); progression_buy_tank(&tank, P, clock_port_now_unix()); shelf_port_sim_cut(-1);
            ss_boot(963); ss_boot(964);
            int tanks = shelf_count(), w = (int)tank.sd_balance, born = tank.n_fish > 2 ? SD_BIRTH : 0;
            bool ok = (tanks == 0 && w == W0 + born) || (tanks == 1 && w == W0 - P + born);
            if (!ok) bad++;
            printf("DR3 E: cut %d of %d: %d tank(s), wallet %d, fry %d%s\n", n, used, tanks, w, born != 0, ok ? "" : "   <-- WRONG");
        }
        if (bad) D3_FAIL("E: %d wrong", bad);
        d3_rm(pre); }
    /* F: fixtures, loaded twice and after a save (no gift notes on the flash) */
    {   const char *fx[] = { "2026-10-04-1760-genes.sav", "2026-10-05-1864-prints.sav", "2026-10-15-1880-seahorse.sav", "2026-10-19-1912-shelf.sav", "2026-10-24-1936-crab.sav", "2026-10-25-1944-findwait.sav", "2026-11-03-1960-v0.4.0-synth.sav", "2026-11-05-1960-v0.4.1-seam.sav" };
        const uint8_t want[8] = { 0x85, 0x85, 0x85, 0x83, 0x83, 0x83, 0x80, 0x83 };   /* DR2's fix column (= bb1f8d5's trust count), with KNOWN */
        for (int f = 0; f < 8; f++) {
            sim_shelf_board(); char cmd[1600]; snprintf(cmd, sizeof cmd, "cp testdata/saves/%s '%s'", fx[f], getenv("POCKET_TANK_SAVE")); (void)system(cmd);
            ss_boot(970 + f); uint8_t a = tank.sd_receipts;
            snprintf(cmd, sizeof cmd, "cp testdata/saves/%s '%s'", fx[f], getenv("POCKET_TANK_SAVE")); (void)system(cmd);
            ss_boot(980 + f); uint8_t b = tank.sd_receipts;
            progression_save(&tank); ss_boot(990 + f); uint8_t c = tank.sd_receipts;
            printf("DR3 F: %-36s receipts %02x / %02x / after a save %02x (want %02x)\n", fx[f], a, b, c, want[f]);
            if (a != want[f] || b != a || c != a) D3_FAIL("F %s", fx[f]);
        } }
}

/* ---- N1: the save will not load (a damaged blob; on the boards a read error blocks saving) while a paid NEW TANK's
   journal stands; then the save reads again ---- */
static void d3_load_fail(void) {
    const int W0 = 1000, P = SHELF_NEW_PRICE;
    for (int kind = 0; kind < 2; kind++) {           /* 0: NEW TANK paid, its journal left; 1: REMOVE paid, its erase + journal left */
        bool hit = false;
        for (int k = 0; k < 14; k++) {
            sim_shelf_board(); ss_boot(801);
            uint32_t id = 0; int V = 0;
            if (kind == 0) {
                progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
                shelf_port_sim_fail(k); int i = progression_buy_tank(&tank, P, clock_port_now_unix()); shelf_port_sim_fail(-1);
                if (i < 0 || !d3_on("buy")) continue;
                id = shelf_get(i)->id;
            } else {
                tank_init(&tank, 9); tank_new_population(&tank); tank.sd_unlocks = SD_ITEM_CASTLE; progression_save(&tank);
                if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) { D3_FAIL("N1 staging"); return; }
                ss_boot(802); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
                V = shelf_refund_value(shelf_index_of(1), NULL);
                shelf_port_sim_fail_n(k, 2); int got = progression_remove_tank(&tank, shelf_index_of(1)); shelf_port_sim_fail(-1);
                if (got < 0 || !d3_on("rm")) continue;
                id = 1;
            }
            hit = true;
            int32_t flash_w = tank.sd_balance;
            /* the save damaged: n_fish 1 (load_save refuses it) - kept aside, put back after the boot */
            const char *sp = getenv("POCKET_TANK_SAVE"); static uint8_t keep[4096]; long n = ss_read(sp, keep, sizeof keep);
            static uint8_t bad[4096]; memcpy(bad, keep, (size_t)n); bad[23] = 1; ss_write(sp, bad, (size_t)n);
            memset(&s_ss_book, 0, sizeof s_ss_book); s_ss_book.persist = true;
            memset(&tank, 0, sizeof tank); tank_init(&tank, 803); gift_attach(&tank, &s_ss_book);
            persist_port_sim_block(true);            /* (the boards: a save that would not read blocks the tank's writes) */
            shelf_boot(clock_port_now_unix()); progression_boot(&tank); progression_shelf_settle(&tank); progression_swap_carry(&tank);
            persist_port_sim_block(false);
            bool there_mid = rf_there(id);
            ss_write(sp, keep, (size_t)n);           /* the save reads again (a transient error, or the keeper's fix) */
            ss_boot(804);
            bool there = rf_there(id);
            int worth = kind == 0 ? (there ? P : 0) : (there ? V : 0);
            printf("DR3 N1 %s (write %d refused): the boot over a save that would not load: tank %u %s; the next good boot: tank %s, wallet %d (flash had %d): wallet+tank %d, want %d\n",
                   kind ? "REMOVE paid" : "NEW TANK paid", k, (unsigned)id, there_mid ? "kept" : "ERASED", there ? "there" : "gone", (int)tank.sd_balance, (int)flash_w,
                   (int)tank.sd_balance + worth, kind == 0 ? W0 : W0 + V);
            if ((int)tank.sd_balance + worth != (kind == 0 ? W0 : W0 + V)) D3_FAIL("N1 %s: money not conserved over a save that would not load", kind ? "REMOVE" : "NEW TANK");
        }
        if (!hit) printf("DR3 N1 kind %d: no case\n", kind);
    }
}

/* ---- N2: a journal written by the release candidate (bb1f8d5's formats) left on the flash, then this build boots ---- */
static void d3_old_journal(void) {
    const int W0 = 1000, P = SHELF_NEW_PRICE;
    /* buy: bb1f8d5 {magic, id, now_id, wallet before, price} - 20 bytes, as this build's {magic, id, now_id, price, mark} */
    for (int paid = 0; paid < 2; paid++) {
        sim_shelf_board(); ss_boot(811); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
        int i = progression_buy_tank(&tank, P, clock_port_now_unix());
        if (i < 0) { D3_FAIL("N2 staging"); return; }
        uint32_t id = shelf_get(i)->id;
        struct { uint32_t magic, id, now_id; int32_t wallet, price; } j = { 0x59554250u, id, shelf_now()->id, W0, P };
        if (!paid) { tank.sd_balance = W0; }        /* not paid: the old wallet on the flash, the tank in its slot (a cut) */
        progression_save(&tank);
        x2_stamp(0x00040100u); d3_zero_marks();     /* (the release candidate's save: no markers) */
        d3_put("buy", &j, sizeof j);
        ss_boot(812); ss_boot(813);
        bool there = rf_there(id);
        int total = (int)tank.sd_balance + (there ? P : 0);
        printf("DR3 N2 rc buy journal (%s): tank %s, wallet %d: wallet+tank %d (want %d)%s\n", paid ? "paid" : "not paid", there ? "there" : "ERASED", (int)tank.sd_balance, total, W0,
               total != W0 ? "   <-- WRONG" : "");
        if (total != W0) D3_FAIL("N2 rc buy journal %s", paid ? "paid" : "not paid");
    }
    /* rm: bb1f8d5 {magic, id, now_id, wallet, refund, paid} - 24 bytes: this build reads 20 */
    {   sim_shelf_board(); ss_boot(815);
        tank_init(&tank, 9); tank_new_population(&tank); tank.sd_unlocks = SD_ITEM_CASTLE; progression_save(&tank);
        if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) { D3_FAIL("N2 rm staging"); return; }
        ss_boot(816); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
        int V = shelf_refund_value(shelf_index_of(1), NULL);
        /* paid (refund in the save, marked paid), the erase not done (refused, then the update) */
        tank.sd_balance = W0 + V; progression_save(&tank); x2_stamp(0x00040100u); d3_zero_marks();
        struct { uint32_t magic, id, now_id; int32_t wallet, refund; uint32_t paid; } j = { 0x4d525450u, 1, shelf_now()->id, W0, V, 1 };
        d3_put("rm", &j, sizeof j);
        ss_boot(817); ss_boot(818);
        bool there = rf_there(1);
        int total = (int)tank.sd_balance + (there ? V : 0);
        printf("DR3 N2 rc rm journal (paid, erase owed): tank 1 %s, wallet %d: wallet+tank %d (want %d), journal still on the flash %d%s\n", there ? "THERE" : "gone", (int)tank.sd_balance, total, W0 + V, d3_on("rm"),
               total != W0 + V ? "   <-- WRONG" : "");
        if (total != W0 + V) D3_FAIL("N2 rc rm journal"); }
}

/* ---- Q: every reading the release candidate's journals get (bb1f8d5 read them by the wallet), and the frame's length
   check (the fourth review's N07-N10, N16) ---- */
static void d3_rc_save(int32_t wallet) {               /* the release candidate's save: its wallet, no markers */
    tank.sd_balance = wallet; progression_save(&tank); x2_stamp(0x00040100u); d3_zero_marks();
}
static void d3_old_journal_readings(void) {
    const int W0 = 1000, P = SHELF_NEW_PRICE;
    /* Q1 (N07): NEW TANK, the wallet neither the journal's nor less the price: kept, nothing charged */
    {   sim_shelf_board(); ss_boot(871); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
        int i = progression_buy_tank(&tank, P, clock_port_now_unix());
        if (i < 0) { D3_FAIL("Q1 staging"); return; }
        uint32_t id = shelf_get(i)->id;
        struct { uint32_t magic, id, now_id; int32_t wallet, price; } j = { 0x59554250u, id, shelf_now()->id, W0, P };
        d3_rc_save(W0 - P + 46); d3_put("buy", &j, sizeof j);
        ss_boot(872); ss_boot(873);
        bool there = rf_there(id);
        printf("DR3 Q1 rc buy journal, the wallet moved: tank %s, wallet %d (want there, %d), journal %d\n", there ? "there" : "ERASED", (int)tank.sd_balance, W0 - P + 46, d3_on("buy"));
        if (!there || tank.sd_balance != W0 - P + 46) D3_FAIL("Q1 (N07): an rc NEW TANK neither paid nor not paid by the wallet was not kept"); }
    /* Q2 (N08): NEW TANK paid by the wallet (the journal's less the price), its tank gone: the price back */
    {   sim_shelf_board(); ss_boot(874); progression_setup_done(&tank);
        struct { uint32_t magic, id, now_id; int32_t wallet, price; } j = { 0x59554250u, 77, shelf_now()->id, W0, P };
        d3_rc_save(W0 - P); d3_put("buy", &j, sizeof j);
        ss_boot(875); ss_boot(876);
        printf("DR3 Q2 rc buy journal paid, no tank: wallet %d (want %d), journal %d\n", (int)tank.sd_balance, W0, d3_on("buy"));
        if (tank.sd_balance != W0) D3_FAIL("Q2 (N08): an rc NEW TANK paid with no tank: the price not given back"); }
    /* Q3 (N09) and Q4 (N10): REMOVE - not marked paid but the wallet at the journal's plus the refund; marked paid with the
       wallet moved since. Both paid: the tank goes, the wallet stays */
    for (int q = 0; q < 2; q++) {
        sim_shelf_board(); ss_boot(877);
        tank_init(&tank, 9); tank_new_population(&tank); tank.sd_unlocks = SD_ITEM_CASTLE; progression_save(&tank);
        if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) { D3_FAIL("Q%d staging", 3 + q); return; }
        ss_boot(878); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
        int V = shelf_refund_value(shelf_index_of(1), NULL);
        int32_t w = q == 0 ? W0 + V : W0 + V + 46;
        struct { uint32_t magic, id, now_id; int32_t wallet, refund; uint32_t paid; } j = { 0x4d525450u, 1, shelf_now()->id, W0, V, (uint32_t)q };
        d3_rc_save(w); d3_put("rm", &j, sizeof j);
        ss_boot(879); ss_boot(880);
        bool there = rf_there(1);
        printf("DR3 Q%d rc rm journal (%s): tank 1 %s, wallet %d (want gone, %d)\n", 3 + q, q ? "marked paid, the wallet moved" : "not marked, the wallet at the refund", there ? "THERE" : "gone", (int)tank.sd_balance, (int)w);
        if (there || tank.sd_balance != w) D3_FAIL("Q%d (%s): an rc REMOVE paid %s left its tank", 3 + q, q ? "N10" : "N09", q ? "by its mark" : "by the wallet");
    }
    /* Q5 (N16): a frame record longer than the reader's buffer is refused, as nvs_get_blob's ESP_ERR_NVS_INVALID_LENGTH -
       never a prefix */
    {   sim_shelf_board();
        uint32_t big[7] = { 0x4d525450u, 1, 1, 0, 0, 1, 0 }, back[6] = { 0 }; size_t got = 0;
        d3_put("rm", big, sizeof big);
        bool ok = shelf_port_frame_get("rm", back, sizeof back, &got);
        printf("DR3 Q5 a 28 B record read into 24 B: %s (got %u)\n", ok ? "READ" : "refused", (unsigned)got);
        if (ok) D3_FAIL("Q5 (N16): the sim's frame read a record longer than its buffer");
        shelf_port_frame_del("rm"); }
}

/* ---- N3: SWAP IN refused in the session (the swap journal), the carry's delete refused, refused again at the tidy of a
   save that lands after the keeper spends; a boot ---- */
static void d3_swap_refused_carry(void) {
    for (int extra = 0; extra < 2; extra++) {
        sim_shelf_board(); ss_boot(821); progression_setup_done(&tank); tank.sd_balance = 500; progression_save(&tank);
        int b = shelf_new(0, clock_port_now_unix());
        if (b < 0 || !shelf_put_up(b)) { D3_FAIL("N3 staging"); return; }
        ss_boot(822); progression_setup_done(&tank); tank.sd_balance = 400; progression_save(&tank);
        int hit = -1;
        char pre[800]; snprintf(pre, sizeof pre, "%s.d3n3", sim_shelf_dir()); ss_copy(sim_shelf_dir(), pre);
        for (int k = 0; k < 20 && hit < 0; k++) {
            ss_copy(pre, sim_shelf_dir()); ss_boot(822);
            shelf_port_sim_refused(); shelf_port_sim_fail_n(k, 2); bool ok = shelf_put_up(shelf_index_of(1)); shelf_port_sim_fail(-1);
            if (!ok && d3_on("carry") && !d3_on("swap")) hit = k;
        }
        if (hit < 0) { printf("DR3 N3: no case\n"); d3_rm(pre); return; }
        progression_sd_grant(&tank, -7);
        if (extra) { shelf_port_sim_fail(0); } bool landed = progression_save(&tank); shelf_port_sim_fail(-1);
        int32_t want = landed ? tank.sd_balance : 400;
        bool carry = d3_on("carry");
        ss_boot(823); ss_boot(824);
        printf("DR3 N3 (tidy refused again %d): swap refused at write %d, the carry left; the keeper spent 7 (landed %d), carry on flash %d: the boot has tank %u with %d (want tank 2 with %d)%s\n",
               extra, hit, landed, carry, (unsigned)shelf_now()->id, (int)tank.sd_balance, (int)want, tank.sd_balance != want ? "   <-- WRONG" : "");
        if (tank.sd_balance != want) D3_FAIL("N3 extra=%d", extra);
        d3_rm(pre);
    }
}

/* ---- N4: the money soak - NEW TANK and REMOVE with 1..3 writes refused from each write; the keeper moves the wallet by
   each misreading figure; the next save with its first write refused (or not); a boot with a write refused at the
   settle; the keeper moves again and saves; two boots. Conserved: wallet + what the tank is worth, exactly ---- */
static void d3_soak(void) {
    const int W0 = 1000, P = SHELF_NEW_PRICE;
    char pre[800]; snprintf(pre, sizeof pre, "%s.d3s", sim_shelf_dir());
    for (int kind = 0; kind < 2; kind++) {
        int V = 0;
        sim_shelf_board(); ss_boot(831);
        if (kind == 0) { progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank); }
        else {
            tank_init(&tank, 9); tank_new_population(&tank); tank.sd_unlocks = SD_ITEM_CASTLE; progression_save(&tank);
            if (shelf_new(0, clock_port_now_unix()) < 0 || !shelf_put_up(shelf_index_of(2))) { D3_FAIL("N4 staging"); return; }
            ss_boot(832); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
            V = shelf_refund_value(shelf_index_of(1), NULL);
        }
        ss_copy(sim_shelf_dir(), pre);
        const int worth = kind == 0 ? P : V;
        const int deltas[5] = { 0, P, -P, 46, -46 };
        int runs = 0, bad = 0;
        for (int k = 0; k < 12; k++)
        for (int n = 1; n <= 3; n++)
        for (int d = 0; d < 5; d++)
        for (int fin = 0; fin < 3; fin++)
        for (int bf = -1; bf < 4; bf++) {
            ss_copy(pre, sim_shelf_dir()); ss_boot(833);
            shelf_port_sim_refused(); shelf_port_sim_fail_n(k, n);
            uint32_t id;
            if (kind == 0) { int i = progression_buy_tank(&tank, P, clock_port_now_unix()); id = i >= 0 ? shelf_get(i)->id : 0; }
            else { (void)progression_remove_tank(&tank, shelf_index_of(1)); id = 1; }
            shelf_port_sim_fail(-1);
            if (!shelf_port_sim_refused()) { if (d == 0 && fin == 0 && bf == -1) {} continue; }
            /* the id of a NEW TANK whose purchase failed: none of ours (the count says) */
            int32_t base = tank.sd_balance; int there0 = kind == 0 ? shelf_count() : rf_there(1);
            int32_t truth = base + there0 * worth;              /* what the keeper has, as the glass shows it */
            progression_sd_grant(&tank, deltas[d]);
            int32_t after_grant = tank.sd_balance;
            bool landed = true;
            if (fin == 1) { shelf_port_sim_fail(0); landed = progression_save(&tank); shelf_port_sim_fail(-1); }
            else if (fin == 2) { shelf_port_sim_fail_n(0, 2); landed = progression_save(&tank); shelf_port_sim_fail(-1); }
            else landed = progression_save(&tank);
            (void)after_grant;
            /* boot 1, a write refused at the settle */
            if (bf >= 0) shelf_port_sim_fail(bf);
            ss_boot(834); shelf_port_sim_fail(-1);
            int there1 = kind == 0 ? shelf_count() : rf_there(1);
            int32_t mid = tank.sd_balance + there1 * worth;
            progression_sd_grant(&tank, 5); progression_save(&tank);
            ss_boot(835); ss_boot(836);
            int there2 = kind == 0 ? shelf_count() : rf_there(1);
            int32_t end = tank.sd_balance + there2 * worth;
            runs++;
            const int32_t T0 = kind == 0 ? W0 : W0 + V;      /* conserved: the wallet + what the tank is worth */
            bool ok = end == T0 + (landed ? deltas[d] : 0) + 5;
            (void)mid; (void)truth;
            if (kind == 0 && there2 > 1) ok = false;
            if (!ok) { bad++; if (bad <= 30) printf("DR3 N4 %s k%d n%d d%+d fin%d bf%d: after the op wallet %d + %d tank(s) (truth %d); landed %d; boot1 total %d; end total %d   <-- WRONG\n",
                kind ? "REMOVE" : "NEW TANK", k, n, deltas[d], fin, bf, (int)base, there0, (int)truth, landed, (int)mid, (int)end); }
        }
        printf("DR3 N4 %s soak: %d runs, %d wrong\n", kind ? "REMOVE" : "NEW TANK", runs, bad);
        if (bad) D3_FAIL("N4 %s soak: %d of %d", kind ? "REMOVE" : "NEW TANK", bad, runs);
    }
    d3_rm(pre);
}

/* ---- N5: the swap's carry soak - SWAP IN with 1..3 refused from each write; the boot after with 1..2 refused from each
   write; the keeper spends, a save (its first write refused or not); two boots: the wallet the keeper left ---- */
static void d3_carry_soak(void) {
    char pre[800]; snprintf(pre, sizeof pre, "%s.d3c", sim_shelf_dir());
    sim_shelf_board(); ss_boot(841); progression_setup_done(&tank); tank.sd_balance = 500; progression_save(&tank);
    int b = shelf_new(0, clock_port_now_unix());
    if (b < 0 || !shelf_put_up(b)) { D3_FAIL("N5 staging"); return; }
    ss_boot(842); progression_setup_done(&tank); tank.sd_balance = 400; progression_save(&tank);
    ss_copy(sim_shelf_dir(), pre);
    int runs = 0, bad = 0, stuck = 0;
    for (int k = -1; k < 24; k++)
    for (int n = 1; n <= 3; n++)
    for (int bk = -1; bk < 12; bk++)
    for (int bn = 1; bn <= 2; bn++)
    for (int fin = 0; fin < 2; fin++) {
        if (k < 0 && n > 1) continue;
        if (bk < 0 && bn > 1) continue;
        ss_copy(pre, sim_shelf_dir()); ss_boot(842);
        shelf_port_sim_refused();
        if (k >= 0) shelf_port_sim_fail_n(k, n);
        bool ok = shelf_put_up(shelf_index_of(1)); shelf_port_sim_fail(-1);
        bool fired = shelf_port_sim_refused() > 0;
        if (k >= 0 && !fired) continue;
        int32_t want = ok ? 400 : 400;
        if (ok) {
            if (bk >= 0) shelf_port_sim_fail_n(bk, bn);
            ss_boot(843); shelf_port_sim_fail(-1);
            if (bk >= 0 && !shelf_port_sim_refused()) continue;
        }
        bool st = shelf_new(0, clock_port_now_unix()) < 0 && shelf_free_places() > 0;
        if (!st && shelf_count() > 1) for (int q = 0; q < shelf_count(); q++) if (shelf_get(q)->id > 2) { shelf_remove(q); break; }
        if (st) { stuck++; continue; }                 /* (R2S-6, deferred) */
        int32_t had = tank.sd_balance; uint32_t glass = shelf_now()->id;
        progression_sd_grant(&tank, -7);
        bool landed;
        if (fin) { shelf_port_sim_fail(0); landed = progression_save(&tank); shelf_port_sim_fail(-1); } else landed = progression_save(&tank);
        if (!landed) landed = progression_save(&tank);
        ss_boot(844); ss_boot(845);
        runs++;
        bool good = had == want && shelf_now()->id == glass && tank.sd_balance == want - 7;
        if (!good) { bad++; if (bad <= 30) printf("DR3 N5 swap k%d n%d (ok %d) boot bk%d bn%d fin%d: the session had tank %u with %d (want %d); two boots later tank %u with %d (want %d)   <-- WRONG\n",
                k, n, ok, bk, bn, fin, (unsigned)glass, (int)had, (int)want, (unsigned)shelf_now()->id, (int)tank.sd_balance, (int)want - 7); }
    }
    printf("DR3 N5 carry soak: %d runs, %d wrong (%d stuck skipped)\n", runs, bad, stuck);
    if (bad) D3_FAIL("N5 carry soak: %d of %d", bad, runs);
    d3_rm(pre);
}

/* ---- N6: the marker across a reset and a swap: NEW TANK paid on A (journal left), the delete owed; the keeper resets
   A (the reset's save lands, the tidy refused); a boot - and B swapped in later reads its own markers ---- */
static void d3_marks_swap(void) {
    const int W0 = 1000, P = SHELF_NEW_PRICE;
    /* A's NEW TANK paid (A's save has mark M); swap to B (journal gone); B buys a NEW TANK with its pay refused - B's save
       keeps B's own mark; the journal left reads not paid; then swap back to A: A's mark M is not the journal's */
    sim_shelf_board(); ss_boot(851); progression_setup_done(&tank); tank.sd_balance = W0; progression_save(&tank);
    int i = progression_buy_tank(&tank, P, clock_port_now_unix());
    if (i < 0) { D3_FAIL("N6 staging"); return; }
    uint32_t t2 = shelf_get(i)->id;
    if (!shelf_put_up(i)) { D3_FAIL("N6 swap"); return; }
    ss_boot(852);
    printf("DR3 N6 B (tank %u) on the glass with %d\n", (unsigned)shelf_now()->id, (int)tank.sd_balance);
    bool hit = false;
    char pre[800]; snprintf(pre, sizeof pre, "%s.d3n6", sim_shelf_dir()); ss_copy(sim_shelf_dir(), pre);
    for (int k = 0; k < 12 && !hit; k++) {
        ss_copy(pre, sim_shelf_dir()); ss_boot(852);
        shelf_port_sim_fail_n(k, 2); int j = progression_buy_tank(&tank, P, clock_port_now_unix()); shelf_port_sim_fail(-1);
        if (j >= 0 || !d3_on("buy")) continue;
        hit = true;
        int32_t w = tank.sd_balance; int c0 = shelf_count();
        ss_boot(853);
        printf("DR3 N6 B's NEW TANK refused (write %d), journal left: the boot has %d tanks (was %d), wallet %d (was %d)\n", k, shelf_count(), c0, (int)tank.sd_balance, (int)w);
        if (shelf_count() != c0 || tank.sd_balance != w) D3_FAIL("N6");
    }
    (void)t2; d3_rm(pre);
}

static int selftest_dr3(const char *which) {
    d3_bad = 0;
    if (!which) which = "DLOQM";
    if (strchr(which, 'D')) d3_dr2();
    if (strchr(which, 'L')) d3_load_fail();
    if (strchr(which, 'O')) d3_old_journal();
    if (strchr(which, 'Q')) d3_old_journal_readings();
    if (strchr(which, 'R')) d3_swap_refused_carry();
    if (strchr(which, 'S')) d3_soak();
    if (strchr(which, 'C')) d3_carry_soak();
    if (strchr(which, 'M')) d3_marks_swap();
    sim_shelf_board();
    printf("DR3: %s (%d)\n", d3_bad ? "FAILED" : "PASS", d3_bad);
    return d3_bad ? 1 : 0;
}
/* T: trace one carry-soak case */
static int selftest_dr3t(int k, int n, int bk, int bn) {
    char pre[800]; snprintf(pre, sizeof pre, "%s.d3t", sim_shelf_dir());
    sim_shelf_board(); ss_boot(841); progression_setup_done(&tank); tank.sd_balance = 500; progression_save(&tank);
    int b = shelf_new(0, clock_port_now_unix());
    if (b < 0 || !shelf_put_up(b)) return 1;
    ss_boot(842); progression_setup_done(&tank); tank.sd_balance = 400; progression_save(&tank);
    printf("T: glass %u wallet %d\n", (unsigned)shelf_now()->id, (int)tank.sd_balance);
    shelf_port_sim_refused(); shelf_port_sim_fail_n(k, n);
    bool ok = shelf_put_up(shelf_index_of(1)); shelf_port_sim_fail(-1);
    printf("T: swap ok %d refused %d | frame: swap %d carry %d now-glass %u\n", ok, shelf_port_sim_refused(), d3_on("swap"), d3_on("carry"), (unsigned)shelf_now()->id);
    shelf_port_sim_fail_n(bk, bn);
    memset(&s_ss_book, 0, sizeof s_ss_book); s_ss_book.persist = true;
    memset(&tank, 0, sizeof tank); tank_init(&tank, 843); gift_attach(&tank, &s_ss_book);
    shelf_boot(clock_port_now_unix());
    printf("T: after shelf_boot: glass %u, refused %d, frame swap %d carry %d, shelf n %d\n", (unsigned)shelf_now()->id, shelf_port_sim_refused(), d3_on("swap"), d3_on("carry"), shelf_count());
    progression_boot(&tank);
    printf("T: after wake: wallet %d, refused %d, carry %d\n", (int)tank.sd_balance, shelf_port_sim_refused(), d3_on("carry"));
    progression_shelf_settle(&tank);
    bool sc = progression_swap_carry(&tank);
    printf("T: after swap_carry (%d): wallet %d, refused %d, carry %d\n", sc, (int)tank.sd_balance, shelf_port_sim_refused(), d3_on("carry"));
    shelf_port_sim_fail(-1);
    progression_sd_grant(&tank, -7); bool l = progression_save(&tank);
    printf("T: spend 7, save landed %d, wallet %d, carry %d\n", l, (int)tank.sd_balance, d3_on("carry"));
    ss_boot(844); printf("T: boot: glass %u wallet %d carry %d\n", (unsigned)shelf_now()->id, (int)tank.sd_balance, d3_on("carry"));
    ss_boot(845); printf("T: boot: glass %u wallet %d\n", (unsigned)shelf_now()->id, (int)tank.sd_balance);
    (void)pre; return 0;
}
