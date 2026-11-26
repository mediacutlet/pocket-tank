/* oldfish_test.inc - --selftest-oldfish (0.4.3, fix043-watch-courting): THE CREATOR LEAVES OLD FISH EXACT.
 * Every fish in every save made before the fish creator has no body or fin values (stretch "not set"). Such a tank must
 * swim, eat, breed and draw random numbers bit for bit as it did before the creator (cd2ae68a): no new random draw, no
 * new float path, nothing more per frame. Two seeded tanks of roster fish run OF_TICKS ticks each, with a feeding and a
 * fry born from the pair in the middle; every tick folds both random streams and each fish's state into one hash, which
 * must equal the hash recorded for this world.
 * The record: the creator left old fish unchanged (proven at cd2ae68a with this same file: the OLD hashes below); then
 * THE LOOK BACK, SIDE-ON (fix043-glance, Strato 11-22: "ditch the 'look' completely") removed the part turn toward the
 * glass ON PURPOSE, so old fish swim differently from there on: the hashes were re-recorded at that commit (WANT). Proof
 * the look was the only change: with the old glance put back (tank_debug_look_old) the run still gives the OLD hashes
 * on every world - that is checked here too, every run.
 *   control: the same run with one fish given body values must NOT give the hash (the hash sees a stretched fish).
 * A hash that moves is a behavior change for every keeper's tank: find what changed before you touch the numbers. */
#define OF_TICKS 6000
static void of_fold(uint32_t *h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { *h ^= b[i]; *h *= 16777619u; }
}
/* stretch: -1 none (an old fish); else fish 0 gets body values before the first tick (the control) */
static uint32_t of_run(uint32_t seed, int stretch) {
    const float DT = 1.0f / 60.0f;
    uint32_t h = 2166136261u;
    tank_init(&tank, seed);
    /* four roster fish, every named body and fin set among them, three stages (a seeded tank starts empty) */
    static const struct { int sh, fi, pat; stage_t stage; } CAST[4] = {
        { SHAPE_CLASSIC, FINS_CLASSIC, PATTERN_BARS, STAGE_ADULT }, { SHAPE_SLENDER, FINS_VEIL, PATTERN_MASK, STAGE_ADULT },
        { SHAPE_BULBOUS, FINS_SAIL, PATTERN_EYEBAND, STAGE_ELDER }, { SHAPE_CLASSIC, FINS_SAIL, PATTERN_BARS, STAGE_JUV } };
    for (int i = 0; i < 4; i++) {
        tank_make_fish(&tank, i, i, 0.6f, 0.6f, CAST[i].stage); tank.n_fish = i + 1;
        tank.fish[i].shape = (uint8_t)CAST[i].sh; tank.fish[i].fins = (uint8_t)CAST[i].fi; tank.fish[i].pattern = (uint8_t)CAST[i].pat;
    }
#ifdef OF_HAS_STRETCH
    if (stretch >= 0) tank_set_body(&tank.fish[0], 19.0f, 9.0f);
#else
    (void)stretch;
#endif
    for (int i = 0; i < OF_TICKS; i++) {
        if (i == 1500) tank_feed(&tank, tank.fish[0].x, 3);
        if (i == 3000 && tank_add_fish(&tank, 0, 1) < 0) return 0;   /* (no fry: a hash no world wants) */
        tank_tick(&tank, DT, advisor_rules);
        of_fold(&h, &tank.rng, sizeof tank.rng); of_fold(&h, &tank.gene_rng, sizeof tank.gene_rng);
        of_fold(&h, &tank.n_fish, sizeof tank.n_fish);
        for (int q = 0; q < tank.n_fish; q++) {
            const fish_t *f = &tank.fish[q];
            const float v[] = { f->x, f->y, f->heading, f->speed, f->target_speed, f->size, f->hunger, f->energy, f->stress, f->curiosity };
            of_fold(&h, v, sizeof v);
            const int s[] = { (int)f->stage, (int)f->goal.id, f->eaten, (int)f->shape, (int)f->fins, (int)f->pattern };
            of_fold(&h, s, sizeof s);
        }
    }
    return h;
}
static int selftest_oldfish(void) {
    /* per world, seeds 7 and 2024: WANT as recorded at fix043-glance (the look back removed), OLD the pre-creator
       code's (cd2ae68a) - which the old glance must still give */
#if defined(TANK_TAB5)
    static const uint32_t WANT[2] = { 0xc7c8c1c2u, 0xdf266404u }, OLD[2] = { 0u, 0u };   /* (the Tab5: as the 10.1's, below) */
#elif defined(TANK_P4)
    /* the big screens (home-043-rel, 11-22): their own water and pace - recorded there, at the release's code with the look back
       removed; no pre-creator run of this file exists for them (OLD 0: not asked) */
    static const uint32_t WANT[2] = { 0xd2afd727u, 0xf643aa71u }, OLD[2] = { 0u, 0u };
#elif defined(TANK_WATCH)
    static const uint32_t WANT[2] = { 0xe5f2d3a1u, 0x341fb3f8u }, OLD[2] = { 0x1c78b6a7u, 0x02006b5cu };
#elif defined(TANK_ROUND)
    static const uint32_t WANT[2] = { 0x3970fc88u, 0x672a2c2bu }, OLD[2] = { 0x87516b1eu, 0x08ec396cu };
#elif defined(TANK_SQUARE)
    static const uint32_t WANT[2] = { 0x044ea80cu, 0x2462b123u }, OLD[2] = { 0xa5c217bdu, 0x5032e3ccu };
#else
    static const uint32_t WANT[2] = { 0xf41c7b25u, 0x97efb1fcu }, OLD[2] = { 0x6a5b463cu, 0x7665f9dfu };
#endif
    static const uint32_t SEED[2] = { 7, 2024 };
    uint32_t got[2];
    for (int k = 0; k < 2; k++) got[k] = of_run(SEED[k], -1);
    printf("selftest-oldfish: %d ticks of roster fish, no values set: %08x %08x (want %08x %08x)\n", OF_TICKS, got[0], got[1], WANT[0], WANT[1]);
#ifdef OF_HAS_STRETCH
#if defined(__APPLE__) && defined(__aarch64__)          /* (the hashes are the Mac's: glibc's float math runs the fish another way -
                                                           elsewhere they are said, not compared, as --selftest-bg-golden's) */
    for (int k = 0; k < 2; k++)
        if (got[k] != WANT[k]) { printf("FAIL: selftest-oldfish: seed %u: a tank of old fish no longer runs as recorded at fix043-glance (%08x, want %08x)\n", SEED[k], got[k], WANT[k]); return 1; }
    if (OLD[0]) {
    tank_debug_look_old(true);                       /* the proof, kept: the old glance gives the pre-creator hashes */
    for (int k = 0; k < 2; k++) { const uint32_t o = of_run(SEED[k], -1);
        if (o != OLD[k]) { tank_debug_look_old(false); printf("FAIL: selftest-oldfish: seed %u: with the old glance put back, old fish no longer run as before the creator (%08x, want %08x) - something besides the look changed\n", SEED[k], o, OLD[k]); return 1; } }
    tank_debug_look_old(false); }
#else
    (void)OLD; printf("selftest-oldfish: the hashes are the Mac's: said, not compared here\n");
#endif
    uint32_t ctl = of_run(SEED[0], 0);
    if (ctl == got[0]) { printf("FAIL: selftest-oldfish: control: a stretched fish ran to the old fish's hash (%08x) - the hash sees nothing\n", ctl); return 1; }
    if (of_run(SEED[0], -1) != got[0]) { printf("FAIL: selftest-oldfish: the same seed ran to two hashes\n"); return 1; }
    printf("selftest-oldfish: old fish run exactly as recorded (the look back removed; with it put back, exactly as before the creator); control: a stretched fish runs to %08x\n", ctl);
#endif
    return 0;
}
