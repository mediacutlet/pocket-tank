/* ---- progression reviewer's farm harness (0.4.1 audit phase 2, scratch only) ----
 * --selftest-farm: repeat each money path many times and watch the wallet; probe badges for wrong / double / never. */
#define FM_SAY(...) do { printf("farm: "); printf(__VA_ARGS__); printf("\n"); } while (0)
static char fm_sav[600];
static void fm_fresh(uint32_t seed) {
    (void)remove(fm_sav); collection_reset();
    tank_init(&tank, seed); progression_boot(&tank); progression_setup_done(&tank); progression_tick(&tank, 0.1f);
    notice_sync(&tank);
}
static void fm_reload(uint32_t seed) { memset(&tank, 0, sizeof tank); tank_init(&tank, seed); progression_boot(&tank); notice_sync(&tank); }
static void fm_ticks(int n) { for (int k = 0; k < n; k++) progression_tick(&tank, 0.1f); }
static passport_t fm_other(uint32_t seed) {          /* a fish from another tank */
    static tank_t fr; tank_init(&fr, seed); tank_new_population(&fr); tank_ensure_ids(&fr);
    passport_t pp; passport_from_fish(&pp, &fr.fish[0], 0x5eed0000u + seed, 3600);
    return pp;
}
static int fm_notices(const char *tag, int *pop_fish) {   /* drain the queue: count, and for a tank count notice the fish render.c pictures */
    int n = 0;
    for (int q = 0; q < 20000; q++) {
        notice_tick(&tank, 0.05f, false);
        const notice_t *c = notice_current();
        if (c && c->age == 0) {
            n++;
            printf("  [%s] notice kind %d fish %d bit %05x", tag, c->kind, c->fish, (unsigned)c->bit);
            if (c->kind == NOTICE_TANK_MILESTONE) {
                int bi = 0; while (bi < 31 && !(c->bit & (1u << bi))) bi++;
                printf(" (%s) - the card pictures slot %d (%s)", bi < TMS_COUNT ? TMS_NAMES[bi] : "?", tank.n_fish - 1, tank.fish[tank.n_fish - 1].name);
                if (pop_fish) *pop_fish = tank.n_fish - 1;
            }
            printf("\n");
        }
        if (c && c->age > NOTICE_HOLD_S + 0.1f) notice_dismiss();
        if (!c && !notice_pending()) break;
    }
    return n;
}

/* A: every placeable piece bought and sold back 1000 times (half of them shelved first) */
static int fm_decor(void) {
    fm_fresh(301);
    const int32_t W0 = 100000000; tank.sd_balance = W0;
    long long expect = 0; int loops = 0;
    for (int it = 0; it < SD_ITEM_COUNT; it++) {
        if (!tank_decor_placeable(it) || SD_ITEMS[it].bit == SD_ITEM_PUMPKIN) continue;
        for (int k = 0; k < 1000; k++) {
            if (!progression_buy(&tank, it)) { FM_SAY("A: FAIL buy %d refused at %d", it, k); return 1; }
            if (k & 1) progression_shelve(&tank, it);
            if (k % 3 == 0) progression_lock(&tank, it, true);
            if (!progression_sell(&tank, it)) { FM_SAY("A: FAIL sell %d refused at %d", it, k); return 1; }
            if (progression_sell(&tank, it)) { FM_SAY("A: FAIL sold %d twice", it); return 1; }
            expect += SD_ITEMS[it].price - progression_sell_value(it); loops++;
            progression_tick(&tank, 0.01f);
        }
    }
    long long got = (long long)W0 - tank.sd_balance;
    FM_SAY("A: %d buy/sell loops of the placeable pieces: wallet -%lld (expected -%lld), earned %d", loops, got, expect, (int)tank.sd_earned);
    if (got != expect) { FM_SAY("A: FAIL - the wallet is off by %lld", expect - got); return 1; }
    /* the pumpkin: out of season, a sale is final */
    tank.sd_unlocks |= SD_ITEM_PUMPKIN;
    bool sold = progression_sell(&tank, 8), back = progression_buy(&tank, 8);
    FM_SAY("A: pumpkin owned out of season: sold %d, bought back %d (the shop line says NOT IN THE SHOP NOW)", sold, back);
    return 0;
}
/* B: the seven backgrounds */
static int fm_backgrounds(void) {
    fm_fresh(302);
    tank.sd_balance = 1000;
    int bought = 0;
    for (int r = 0; r < 1000; r++) for (int bg = 0; bg <= BG_N; bg++) bought += progression_bg_buy(&tank, bg);
    int used_unowned = 0;
    tank.sd_unlocks &= ~SD_ITEM_BG(3);
    used_unowned = progression_bg_use(&tank, 3);
    FM_SAY("B: 1000 rounds of BUY over every background: %d bought, wallet %d (want 7 and %d); USE of an unowned one: %d",
           bought, (int)tank.sd_balance, 1000 - 7 * SD_PRICE_BG, used_unowned);
    /* do the background bits ever collide with a shop item's? */
    for (int bg = 1; bg < BG_N; bg++) for (int it = 0; it < SD_ITEM_COUNT; it++)
        if (SD_ITEM_BG(bg) & SD_ITEMS[it].bit) FM_SAY("B: FAIL background %d's bit is item %d's", bg, it);
    return bought != 7 || tank.sd_balance != 1000 - 7 * SD_PRICE_BG || used_unowned;
}
/* C: buy a shop fry, welcome it, sell it - 300 times */
static int fm_fry_loop(void) {
    fm_fresh(303);
    const int32_t W0 = 1000000; tank.sd_balance = W0;
    progression_fry_open(&tank);
    int loops = 0;
    for (int k = 0; k < 300; k++) {
        progression_fry_roll(&tank);
        if (!progression_buy_fry(&tank, k % FRY_OFFER_N)) { FM_SAY("C: FAIL the fry was refused at %d (n %d held %d)", k, tank.n_fish, progression_fish_held(&tank)); return 1; }
        int s = tank.n_fish - 1;
        if (progression_fish_sellable(&tank, s)) { FM_SAY("C: FAIL a fry owed its welcome is sellable"); return 1; }
        progression_newborn_done(&tank);
        fm_ticks(2);
        if (!progression_sell_fish(&tank, s)) { FM_SAY("C: FAIL the sale was refused at %d", k); return 1; }
        loops++;
    }
    int32_t d = W0 - tank.sd_balance;
    FM_SAY("C: %d fry bought and sold: wallet -%d (want -%d), earned %d", loops, (int)d, loops * (SD_PRICE_FRY - SD_FISH_FRY), (int)tank.sd_earned);
    return d != loops * (SD_PRICE_FRY - SD_FISH_FRY);
}
/* D: one fish given away and given back, 1000 times (the other tank's side by its passport): what each trip pays here */
static int fm_gift_pingpong(void) {
    fm_fresh(304);
    passport_t pp = fm_other(77);
    pp.stage = STAGE_ELDER; pp.age_s = STAGE_ELDER_AGE + 10; pp.trust = 10.0f;
    pp.ms_bits = MS_ARRIVED | MS_REACHED_JUV | MS_REACHED_ADULT | MS_REACHED_ELDER;
    int32_t w0 = tank.sd_balance; int pay_meal = 0, pay_other = 0;
    for (int k = 0; k < 1000; k++) {
        int s = progression_gift_adopt(&tank, &pp);
        if (s < 0) { FM_SAY("D: FAIL the gift found no room at %d", k); return 1; }
        int32_t w = tank.sd_balance; fm_ticks(2); pay_other += tank.sd_balance - w;
        tank.fish[s].meal_due = true;               /* the keeper feeds it as it swims in */
        w = tank.sd_balance; fm_ticks(1); pay_meal += tank.sd_balance - w;
        passport_from_fish(&pp, &tank.fish[s], tank.uid, progression_age_s(&tank, s));
        if (!progression_give_out(&tank, s)) { FM_SAY("D: FAIL the give was refused at %d", k); return 1; }
        fm_ticks(1);
    }
    FM_SAY("D: 1000 round trips of an elder at full trust: +%d on arrival (stages/trust), +%d from a meal fed as it arrived (wallet %d -> %d)",
           pay_other, pay_meal, (int)w0, (int)tank.sd_balance);
    return pay_other != 0;
}
/* E: a gift whose temper drifted in its first tank: THE TANK CHANGED SOMEONE here? (the class P3 fixed for mutation) */
static int fm_changed_by_gift(void) {
    fm_fresh(305);
    fm_ticks(10);
    uint32_t before = tank.tank_ms_bits & TMS_CHANGED_SOMEONE;
    passport_t pp = fm_other(78);
    pp.bold0 = 0.30f; pp.bold = 0.60f;                 /* it changed - in the tank it came from */
    int s = progression_gift_adopt(&tank, &pp);
    fm_ticks(2);
    uint32_t after = tank.tank_ms_bits & TMS_CHANGED_SOMEONE;
    FM_SAY("E: a gift drifted elsewhere (bold %.2f, born %.2f) in slot %d: TANK CHANGED SOMEONE before %d, after %d",
           tank.fish[s].bold, tank.fish[s].bold0, s, !!before, !!after);
    (void)fm_notices("E", NULL);
    return 0;
}
/* F: the reset's receipts read a gift's SD_PAID_TRUST (set on arrival, never paid here) as the starting pair's trust paid */
static int fm_receipts_gifts(void) {
    fm_fresh(306);
    for (int i = 0; i < tank.n_fish; i++) tank.fish[i].trust = 5.0f;   /* the pair never trusted: no trust ever paid here */
    for (int k = 0; k < 2; k++) { passport_t pp = fm_other(80 + k); pp.trust = 3.0f; progression_gift_adopt(&tank, &pp); }
    fm_ticks(3);
    int32_t w0 = tank.sd_balance;
    FM_SAY("F: two gifts in, nobody trusted, no trust ever paid (receipts %02x; paid bits %x %x %x %x)",
           tank.sd_receipts, tank.sd_paid_fish[0], tank.sd_paid_fish[1], tank.sd_paid_fish[2], tank.sd_paid_fish[3]);
    progression_reset(&tank, 999); progression_setup_done(&tank);
    for (int i = 0; i < tank.n_fish; i++) tank.fish[i].trust = 10.0f;
    fm_ticks(3);
    FM_SAY("F: after the reset the fresh pair at full trust paid +%d (a NEW TANK's pair pays +%d); receipts %02x",
           (int)(tank.sd_balance - w0), 2 * SD_TRUST, tank.sd_receipts);
    return 0;
}
/* G: a fry of ours born next door, a gift arriving while it is there, then the fry home: which fish the count's card shows */
static passport_t fm_fry_next_door(uint32_t seed) {
    static tank_t there; tank_init(&there, seed); tank_new_population(&there);
    fish_t fry = there.fish[0]; fry.stage = STAGE_FRY; fry.size = 0.4f; fry.ms_bits = MS_CROSSBRED;
    fry.parent_uid[0] = tank.fish[0].uid; fry.parent_uid[1] = 0xc0ffee02u; fry.mutated = 0;
    passport_t pp; passport_from_fish(&pp, &fry, tank.uid, 0);
    return pp;
}
static int fm_pop_card(void) {
    fm_fresh(307);
    fm_notices("G0", NULL);
    passport_t fp = fm_fry_next_door(97);
    int fs = progression_fry_adopt(&tank, &fp, LINK_LEFT);
    fm_ticks(2); fm_notices("G fry next door", NULL);
    passport_t gp = fm_other(79);
    int gs = progression_gift_adopt(&tank, &gp);
    fm_ticks(2); fm_notices("G gift in", NULL);
    FM_SAY("G: fry next door slot %d (%s), gift slot %d (%s); tank row says %d OF %d FISH; bits trio %d quartet %d",
           fs, tank.fish[fs].name, gs, tank.fish[gs].name, tank.n_fish, POP_CAP, !!(tank.tank_ms_bits & TMS_TRIO), !!(tank.tank_ms_bits & TMS_QUARTET));
    tank.fish[fs].away = false; progression_fry_home(&tank, fs);
    fm_ticks(2);
    int pic = -1; fm_notices("G fry home", &pic);
    FM_SAY("G: the fry home: quartet %d; its card pictures slot %d (%s) - the fry that came home is slot %d (%s)",
           !!(tank.tank_ms_bits & TMS_QUARTET), pic, pic >= 0 ? tank.fish[pic].name : "-", fs, tank.fish[fs].name);
    return 0;
}
/* H: the care gates' fifth table (n_fish >= 5: adult, 140 meals, trust 8) - can it ever be the one asked? */
static int fm_gate_default(void) {
    fm_fresh(308);
    int reached = 0;
    for (int k = 0; k < 6; k++) { progression_force_arrival(&tank); if (progression_newborn() >= 0) progression_newborn_done(&tank); }
    fry_req_t out[FRY_REQ_MAX]; bool st;
    int n = progression_next_fry(&tank, out, &st);
    for (int i = 0; i < n; i++) if (out[i].kind == FRY_REQ_FEED && strstr(out[i].words2, "140")) reached = 1;
    FM_SAY("H: %d fish (held %d): the checklist has %d rows, the 140-meal table asked: %d", tank.n_fish, progression_fish_held(&tank), n, reached);
    return 0;
}
/* I: L1 - a visitor's time together with a fish of ours that is then sold: does it ever grow close to another? */
static int fm_bond_sold(void) {
    fm_fresh(309);
    for (int k = 0; k < 2; k++) { progression_force_arrival(&tank); if (progression_newborn() >= 0) progression_newborn_done(&tank); }
    for (int i = 0; i < tank.n_fish; i++) progression_set_age(&tank, i, STAGE_ADULT_AGE + 1);
    fish_t *v = &tank.fish[N_FISH_MAX];
    *v = tank.fish[0]; v->uid = 0xabcd0001u; v->away = false; v->homeward = false; v->stage = STAGE_ADULT;
    tank.vis_used[0] = true; tank.vis_since[0] = tank.clock;
    for (int k = 0; k < 900; k++) {                  /* 90 s side by side with fish 0 */
        v->x = tank.fish[0].x + 10; v->y = tank.fish[0].y;
        progression_tick(&tank, 0.1f);
    }
    uint32_t mate = 0; float b0 = progression_bond(&tank, v->uid, &mate);
    FM_SAY("I: after 90 s beside fish 0 (%08x): bond %.2f, mate %08x", (unsigned)tank.fish[0].uid, b0, (unsigned)mate);
    uint32_t sold_uid = tank.fish[0].uid;
    if (!progression_sell_fish(&tank, 0)) { FM_SAY("I: the sale was refused"); return 0; }
    for (int k = 0; k < 6000; k++) {                 /* 10 min beside fish 0 (now another fish) */
        v->x = tank.fish[0].x + 10; v->y = tank.fish[0].y;
        progression_tick(&tank, 0.1f);
    }
    float b1 = progression_bond(&tank, v->uid, &mate);
    FM_SAY("I: 10 min beside another fish after the sale: bond %.2f, mate %08x (the sold fish %08x), fry_partner %08x fry_mate %08x",
           b1, (unsigned)mate, (unsigned)sold_uid, (unsigned)tank.fry_partner, (unsigned)tank.fry_mate);
    tank.vis_used[0] = false;
    return 0;
}
/* J: the same frame: a fish sold and a fry born (n_fish unchanged): does notice.c re-announce badges for the shifted slots? */
static int fm_same_frame(void) {
    fm_fresh(310);
    for (int k = 0; k < 2; k++) { progression_force_arrival(&tank); if (progression_newborn() >= 0) progression_newborn_done(&tank); }
    tank.fish[1].ms_bits |= MS_FIRST_DART | MS_FIRST_BUBBLES | MS_FIRST_FOLLOW; tank.fish[2].ms_bits |= MS_FIRST_GRASS;
    progression_set_age(&tank, 2, STAGE_ADULT_AGE + 5); fm_ticks(2);
    fm_notices("J setup", NULL);
    progression_sell_fish(&tank, 0);
    progression_force_arrival(&tank);
    int n = fm_notices("J same frame", NULL);
    FM_SAY("J: a sale and a birth before notice_tick ran: %d notices (a trio / quartet are already earned; anything else is a repeat)", n);
    if (progression_newborn() >= 0) progression_newborn_done(&tank);
    return 0;
}
/* K: algae - one film split into many colonies by the keeper's strokes */
static int FM_STEP = 3;
static int fm_algae_split(void) {
    if (getenv("FM_STEP")) FM_STEP = atoi(getenv("FM_STEP"));
    fm_fresh(311);
    memset(tank.algae, 0, sizeof tank.algae);
    for (int y = 2; y < ALGAE_ROWS - 2; y++) for (int x = 2; x < ALGAE_COLS - 2; x++) tank.algae[y * ALGAE_COLS + x] = 120;   /* one big patch */
    int c0 = tank.algae_colonies;
    /* the keeper draws a grid of lines through it first, every 3rd row and column, then wipes each island */
    for (int y = 2; y < ALGAE_ROWS - 2; y += FM_STEP) { float py = y * ALGAE_CELL + ALGAE_CELL * 0.5f; for (float sx = 0; sx < TANK_W; sx += 3) tank_touch_drag(&tank, sx, py); fm_ticks(1); }
    for (int x = 2; x < ALGAE_COLS - 2; x += FM_STEP) { float px = x * ALGAE_CELL + ALGAE_CELL * 0.5f; for (float sy = 0; sy < TANK_H; sy += 3) tank_touch_drag(&tank, px, sy); fm_ticks(1); }
    int mid = tank.algae_colonies - c0, left = 0;
    for (int i = 0; i < ALGAE_CELLS; i++) left += tank.algae[i] != 0;
    for (int y = 0; y < ALGAE_ROWS; y++) for (int x = 0; x < ALGAE_COLS; x++)
        if (tank.algae[y * ALGAE_COLS + x]) { float px = x * ALGAE_CELL + ALGAE_CELL * 0.5f, py = y * ALGAE_CELL + ALGAE_CELL * 0.5f;
            for (float d = -2; d <= 2; d += 1) tank_touch_drag(&tank, px + d, py); fm_ticks(1); }
    int total = tank.algae_colonies - c0;
    fm_fresh(312);
    memset(tank.algae, 0, sizeof tank.algae);
    for (int y = 2; y < ALGAE_ROWS - 2; y++) for (int x = 2; x < ALGAE_COLS - 2; x++) tank.algae[y * ALGAE_COLS + x] = 120;
    c0 = tank.algae_colonies;
    for (int y = 0; y < ALGAE_ROWS; y++) { float py = y * ALGAE_CELL + ALGAE_CELL * 0.5f; for (float sx = 0; sx < TANK_W; sx += 3) tank_touch_drag(&tank, sx, py); fm_ticks(1); }
    int plain = tank.algae_colonies - c0;
    FM_SAY("K: one film wiped row by row = %d colonies; the same film sliced into islands first = %d (%d during the slicing, %d cells left)", plain, total, mid, left);
    return 0;
}
static int selftest_farm(void) {
    const char *d = getenv("PT_SHELF_DIR");
    snprintf(fm_sav, sizeof fm_sav, "%s/farm.sav", d && *d ? d : "/tmp");
    setenv("POCKET_TANK_SAVE", fm_sav, 1);
    const char *only = getenv("FARM_ONLY");
    int bad = 0;
#define FM_RUN(c, f) if (!only || strchr(only, c)) bad += f()
    FM_RUN('A', fm_decor); FM_RUN('B', fm_backgrounds); FM_RUN('C', fm_fry_loop); FM_RUN('D', fm_gift_pingpong);
    FM_RUN('E', fm_changed_by_gift); FM_RUN('F', fm_receipts_gifts); FM_RUN('G', fm_pop_card); FM_RUN('H', fm_gate_default);
    FM_RUN('I', fm_bond_sold); FM_RUN('J', fm_same_frame); FM_RUN('K', fm_algae_split);
    printf("selftest-farm: %s (%d)\n", bad ? "FAILED" : "done", bad);
    return bad != 0;
}
