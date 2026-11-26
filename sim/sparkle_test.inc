/* --selftest-sparkle (0.4.3, THE HOUR BEFORE; Strato 11-24 on the glass: "the sparkles operate as one whole animated
 * sprite so when the fish turns, the whole cluster of sparkles flips to the other side"). An elder in its last hour
 * swims right, turns through the yaw turn (head-on halfway), and swims left; then the same kind of fish swims as the
 * tank moves it (its own turns, the tail a beat behind). The stars are read as the difference between the frame and
 * the same frame with that fish not sparkling (another uid, out of its hour), so nothing else in the tank counts:
 *   S1  swimming right, the stars trail it: most of them left of the fish
 *   S2  through the turn no frame moves the stars' middle more than a star's own drift could: nothing jumps across
 *   S3  just after the turn the old stars are still where they were left (most of them on the old side, now ahead
 *       of the fish), and from one frame to the next most star pixels stay put
 *   S4  two seconds on, the old ones are gone and the new ones trail it on its new side (right of the fish)
 *   S5  with the light off: no star; a fish out of its hour: no star (the reference frame is the tank itself)
 *   S6  swimming as the tank moves it, through at least two of its own turns: no frame moves the stars' middle more
 *       than S2 allows; and every star is gone two seconds after its hour ends
 *   S7  four fish in their hour at once (the pool holds three in full): every one of them still trails stars (a star
 *       is never taken back before its fade: a fish waits for a place)
 * PT_SPARKLE_SHOTS=<prefix>: <prefix>_spk_NN.ppm through the scripted turn, <prefix>_nat_NN.ppm through the first
 * natural one (Strato's sheet). */
#define SPK_FAIL(...) do { printf("FAIL: selftest-sparkle: " __VA_ARGS__); printf("\n"); fails++; } while (0)
static uint16_t spk_fa[TANK_W * TANK_H], spk_fb[TANK_W * TANK_H];
typedef struct { int n, left, right; float cx; } spk_seen_t;
static float spk_age = EVO_AGE - 600;                   /* the sparkler's age while it sparkles */
/* the stars of fish i in this frame: the frame against the same frame with fish i not sparkling */
static spk_seen_t spk_look(tank_t *t, int i, uint8_t *mask) {
    fish_t *f = &t->fish[i];
    render_tank(t, spk_fa, TANK_W);
    const uint32_t uid = f->uid; f->uid ^= 0x5a5a5a5au;
    const float sz = f->size; progression_set_age(t, i, EVO_AGE - 3 * EVO_SPARKLE_S); f->size = sz;   /* (the age sets no size until a tick) */
    render_tank(t, spk_fb, TANK_W);
    f->uid = uid; progression_set_age(t, i, spk_age); f->size = sz;
    spk_seen_t s = { 0, 0, 0, 0 }; double sx = 0;
    for (int k = 0; k < TANK_W * TANK_H; k++) {
        const bool on = spk_fa[k] != spk_fb[k];
        if (mask) mask[k] = on;
        if (!on) continue;
        const int x = k % TANK_W; s.n++; sx += x;
        if (x < f->x) s.left++; else s.right++;
    }
    s.cx = s.n ? (float)(sx / s.n) : f->x;
    return s;
}
static int spk_brightest(const tank_t *t) {             /* (the sheet's fish: the lightest body, the stars read against it) */
    int b = 0, bl = -1;
    for (int i = 0; i < t->n_fish; i++) { const uint32_t c = t->fish[i].color; const int l = (int)((c >> 16) & 255) + (int)((c >> 8) & 255) + (int)(c & 255); if (l > bl) { bl = l; b = i; } }
    return b;
}
static int selftest_sparkle(void) {
    int fails = 0;
    static uint8_t m0[TANK_W * TANK_H], m1[TANK_W * TANK_H];
    const char *shots = getenv("PT_SPARKLE_SHOTS"); char path[512]; int nshot = 0;
    tank_init(&tank, 2024); forms_cast(4);                         /* (four grown fish, the roster's colors) */
    render_set_vignette_cache(vig_buf); render_set_card_cache(card_buf);
    for (int i = 0; i < 120; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
    memset(tank.algae, 0, sizeof tank.algae); tank.night = false;
#define SPK_SHOT(kind) do { if (shots) { snprintf(path, sizeof path, "%s_" kind "_%02d.ppm", shots, nshot++); write_ppm(path, spk_fa); } } while (0)
    /* S6: the whole tank, the sparkler swimming as the tank moves it */
    const int b = spk_brightest(&tank);
    fish_t *g = &tank.fish[b];
    g->evo = 0; g->evo_offer = 0; g->away = 0; g->stage = STAGE_ELDER;
    progression_set_age(&tank, b, spk_age);
    for (int i = 0; i < 30; i++) { tank_tick(&tank, 1.0f / 60.0f, advisor_rules); tank.night = false; }   /* (its size, from its age) */
    /* (THE SPEED, 11-24: a whole frame drawn twice a frame for 90 s took the big screens past 900 s. A star can only jump
       where the stars' side can change - the fish's yaw or its heading crossing - so: a first pass ticks the swim alone
       and notes those frames, then the swim is played again from the same state, drawn only from 2 s before each one
       (so the stars are set down as on the glass) and looked at from just before it to 1 s after. The swim is the tank's
       own and seeded: the replay is checked to be the same swim, else every frame is drawn and looked at) */
    static tank_t s6_start; s6_start = tank;
    enum { S6_MAX = 30 * 90, S6_LEAD = 60, S6_PRE = 4, S6_POST = 30 };
    static int16_t cross[64]; int nc = 0, turns = 0, k_end = S6_MAX; float x_end = 0;
    {   float yprev = g->yaw, cprev = cosf(g->heading);
        for (int k = 0; k < S6_MAX; k++) {
            tank_tick(&tank, 1.0f / 30.0f, advisor_rules); tank.night = false;
            const float c = cosf(g->heading);
            const bool yx = (g->yaw >= 0) != (yprev >= 0), hx = (c >= 0) != (cprev >= 0);
            if ((yx || hx) && nc < 64) cross[nc++] = (int16_t)k;
            if (yx) turns++;
            yprev = g->yaw; cprev = c;
            if (turns >= 2 && (k_end == S6_MAX)) k_end = k + S6_POST + (shots ? 48 : 0) + 1;
            if (k + 1 >= k_end) { x_end = g->x; k_end = k + 1; break; }
        }
        if (k_end == S6_MAX) x_end = g->x;
    }
    tank = s6_start; progression_set_age(&tank, b, spk_age);
    float worst = 0, cx_prev = 0; int nprev = 0, frames = 0, drawn = 0, shot_left = 0, turns2 = 0; long px_sum = 0; spk_seen_t s = { 0 };
    bool every = false;                                           /* (the replay strayed: draw and look at every frame from there) */
    {   float yprev = g->yaw;
        for (int k = 0; k < k_end; k++) {
            tank_tick(&tank, 1.0f / 30.0f, advisor_rules); tank.night = false;
            if (!progression_evo_sparkle(&tank, b)) { SPK_FAIL("S6: the fish left its hour at frame %d", k); break; }
            int near = 1 << 30;                                       /* frames to the nearest crossing (negative: before it) */
            for (int i = 0; i < nc; i++) { const int d = k - cross[i]; if (abs(d) < abs(near)) near = d; }
            const bool yx = (g->yaw >= 0) != (yprev >= 0);
            if (yx && !turns2++ && shots) shot_left = 48;
            if (yx && !every) { bool known = false; for (int i = 0; i < nc; i++) known |= cross[i] == k; if (!known) { every = true; printf("sparkle S6: the replay is not the first pass's swim at frame %d - every frame drawn from here\n", k); } }
            yprev = g->yaw;
            const bool look = every || shot_left > 0 || (near >= -S6_PRE && near <= S6_POST);
            if (!look) {
                if (every || (near >= -S6_LEAD && near < 0)) { render_tank(&tank, spk_fa, TANK_W); drawn++; }   /* (the stars set down, nothing compared) */
                nprev = 0; continue;
            }
            s = spk_look(&tank, b, NULL); frames++; drawn += 2; px_sum += s.n;
            if (s.n && nprev) { const float j = fabsf(s.cx - cx_prev); if (j > worst) worst = j; }
            cx_prev = s.cx; nprev = s.n;
            if (shot_left > 0) { if (shot_left-- % 3 == 0) SPK_SHOT("nat"); }
        }
        if (!every && fabsf(g->x - x_end) > 0.01f) printf("sparkle S6: the replay ended at x %.2f, the first pass at %.2f\n", g->x, x_end);
    }
    printf("sparkle S6: the tank's own swim, %d turns (%d side changes) in %d frames, %d looked at, %d drawn: the stars' middle moved at most %.1f px in a frame; %.0f star pixels a frame looked at\n",
           turns, nc, k_end, frames, drawn, worst, frames ? (double)px_sum / frames : 0.0);
    if (turns < 2) SPK_FAIL("S6: the fish turned %d times in 90 s (the test needs two)", turns);
    if (worst > 20) SPK_FAIL("S6: the stars jumped %.1f px in one frame of the tank's own swim", worst);
    /* its hour over: two seconds on, nothing of it left */
    spk_age = EVO_AGE - 3 * EVO_SPARKLE_S; progression_set_age(&tank, b, spk_age);
    for (int k = 0; k < 64; k++) { tank_tick(&tank, 1.0f / 30.0f, advisor_rules); tank.night = false; }
    render_tank(&tank, spk_fa, TANK_W);
    {   const uint32_t u = g->uid; g->uid ^= 0x5a5a5a5au; render_tank(&tank, spk_fb, TANK_W); g->uid = u;
        int n = 0; for (int k = 0; k < TANK_W * TANK_H; k++) n += spk_fa[k] != spk_fb[k];
        printf("sparkle S6: two seconds after its hour, %d star pixels\n", n);
        if (n) SPK_FAIL("S6: %d star pixels two seconds after its hour", n); }
    spk_age = EVO_AGE - 600;
    /* S7: four at once */
    {   int starved = 0, popped = 0, nf = tank.n_fish < 4 ? tank.n_fish : 4;
        for (int i = 0; i < nf; i++) { tank.fish[i].evo = 0; tank.fish[i].evo_offer = 0; tank.fish[i].away = 0; tank.fish[i].stage = STAGE_ELDER; progression_set_age(&tank, i, spk_age); }
        for (int i = 0; i < 30; i++) { tank_tick(&tank, 1.0f / 60.0f, advisor_rules); tank.night = false; }
        int least = 1 << 30;                                       /* (the fewest star pixels a sparkling fish has at the end) */
        for (int k = 0; k < 150; k++) {
            tank_tick(&tank, 1.0f / 30.0f, advisor_rules); tank.night = false;
            if (k < 90) continue;                                     /* (the last two seconds: every fish has had time to set its stars down) */
            if (k < 149) { render_tank(&tank, spk_fa, TANK_W); continue; }   /* (drawn, so the stars are set down; looked at once, at the end) */
            for (int i = 0; i < nf; i++) {
                if (!progression_evo_sparkle(&tank, i)) continue;
                const spk_seen_t q = spk_look(&tank, i, NULL);
                if (q.n < least) least = q.n;
                if (k == 149 && q.n == 0) starved++;
            }
        }
        (void)popped;
        printf("sparkle S7: %d fish sparkling at once: %d with no stars at 5 s; the fewest star pixels a fish had %d\n", nf, starved, least);
        if (starved) SPK_FAIL("S7: %d of %d sparkling fish trail no stars", starved, nf);
        spk_age = EVO_AGE - 3 * EVO_SPARKLE_S; for (int i = 0; i < nf; i++) progression_set_age(&tank, i, spk_age);
        for (int k = 0; k < 64; k++) { tank_tick(&tank, 1.0f / 30.0f, advisor_rules); tank.night = false; }
        spk_age = EVO_AGE - 600; }
    {   const int b = spk_brightest(&tank); if (b) { const fish_t tmp = tank.fish[0]; tank.fish[0] = tank.fish[b]; tank.fish[b] = tmp; } }
    tank.n_fish = 1;                                              /* (one fish: the stars are its own) */
    fish_t *f = &tank.fish[0];
    f->evo = 0; f->evo_offer = 0; f->away = 0;
    progression_set_age(&tank, 0, spk_age); f->stage = STAGE_ELDER; f->size = 1.3f;
    if (!progression_evo_sparkle(&tank, 0)) { printf("FAIL: selftest-sparkle: fish 0 is not in its hour\n"); return 1; }
    const float dt = 1.0f / 30.0f, v = 34, y0 = TANK_H * 0.45f;
    float x = TANK_W * 0.22f, yaw = 1;
    f->y = y0; f->pitch_b = f->pitch_t = 0;
#define SPK_POSE() do { f->x = x; f->yaw = yaw; f->yaw_tail = yaw; f->heading = yaw >= 0 ? 0 : 3.14159265f; f->speed = v * fabsf(yaw); \
                        f->beat += dt * 9; tank.clock += dt; } while (0)
    /* S5 first: before its hour no star (the reference is the same fish: the frames are equal) */
    progression_set_age(&tank, 0, EVO_AGE - 2 * EVO_SPARKLE_S); f->size = 1.3f;
    {   SPK_POSE(); render_tank(&tank, spk_fa, TANK_W); const uint32_t u = f->uid; f->uid ^= 0x5a5a5a5au; render_tank(&tank, spk_fb, TANK_W); f->uid = u;
        if (memcmp(spk_fa, spk_fb, sizeof spk_fa)) SPK_FAIL("S5: a fish out of its hour differs from itself under another uid (the reference is not clean)"); }
    progression_set_age(&tank, 0, spk_age); f->size = 1.3f;
    /* S1: two seconds swimming right */
    nshot = 0;
    for (int k = 0; k < 60; k++) { x += v * dt; SPK_POSE();
        if (k < 59 && !(shots && k >= 54)) { render_tank(&tank, spk_fa, TANK_W); continue; }   /* (drawn: the stars are set down; looked at at the end) */
        s = spk_look(&tank, 0, NULL); if (k >= 54) SPK_SHOT("spk"); }
    printf("sparkle S1: swimming right, %d star pixels, %d left of the fish, %d right, middle %.0f (fish %.0f)\n", s.n, s.left, s.right, s.cx, x);
    if (s.n < 40 || s.left < 3 * s.right) SPK_FAIL("S1: swimming right the stars do not trail it (%d left, %d right)", s.left, s.right);
    /* S2: the turn - 0.6 s, the yaw through 0, the swim slowing into it and away out of it */
    worst = 0; cx_prev = s.cx; nprev = s.n;
    for (int k = 0; k < 18; k++) {
        const float u = (k + 1) / 18.0f; yaw = cosf(u * 3.14159265f);
        x += v * yaw * dt; SPK_POSE(); s = spk_look(&tank, 0, k & 1 ? m1 : m0);
        if (s.n && nprev) { const float j = fabsf(s.cx - cx_prev); if (j > worst) worst = j; }
        cx_prev = s.cx; nprev = s.n;
        SPK_SHOT("spk");
    }
    printf("sparkle S2: through the turn the stars' middle moved at most %.1f px in a frame\n", worst);
    if (worst > 20) SPK_FAIL("S2: the stars jumped %.1f px in one frame of the turn (they flip with the fish)", worst);
    /* S3: a tenth of a second after the turn */
    for (int k = 0; k < 3; k++) { x -= v * dt; SPK_POSE(); s = spk_look(&tank, 0, k & 1 ? m1 : m0); SPK_SHOT("spk"); }
    int kept = 0, n1 = 0;
    for (int i = 0; i < TANK_W * TANK_H; i++) if (m0[i]) { n1++; kept += m1[i]; }   /* (m0: the frame before the last) */
    printf("sparkle S3: just after the turn %d star pixels, %d on the old side (left, now ahead), %d behind; %d of %d stayed put frame to frame\n",
           s.n, s.left, s.right, kept, n1);
    if (s.left <= s.right) SPK_FAIL("S3: the old stars went with the fish (%d left where they were left, %d on its new side)", s.left, s.right);
    if (n1 && kept * 2 < n1) SPK_FAIL("S3: from one frame to the next only %d of %d star pixels stayed put", kept, n1);
    /* S4: two seconds on */
    for (int k = 0; k < 66; k++) { x -= v * dt; SPK_POSE();
        if (k < 65 && !(shots && k % 6 == 5)) { render_tank(&tank, spk_fa, TANK_W); continue; }
        s = spk_look(&tank, 0, NULL); if (k % 6 == 5) SPK_SHOT("spk"); }
    printf("sparkle S4: two seconds on, %d star pixels, %d right of the fish (behind), %d left\n", s.n, s.right, s.left);
    if (s.n < 40 || s.right < 3 * s.left) SPK_FAIL("S4: the stars do not trail it on its new side (%d right, %d left)", s.right, s.left);
    /* S5: the light off */
    tank.night = true; x -= v * dt; SPK_POSE(); s = spk_look(&tank, 0, NULL); tank.night = false;
    printf("sparkle S5: with the light off %d star pixels\n", s.n);
    if (s.n) SPK_FAIL("S5: %d star pixels with the light off", s.n);
#undef SPK_POSE
#undef SPK_SHOT
    printf("selftest-sparkle: %s\n", fails ? "FAIL" : "the stars stay where they were left in the water through a turn: ok");
    return fails ? 1 : 0;
}
