/* linkclock_test.inc - --selftest-linkclock (0.4.3, Strato 11-23): a board's link clock is its uptime in ms (link_dev.c now_ms,
 * esp_timer), and a tank on a cable that is never put to sleep runs past 2^31 ms (24.86 days) and 2^32 ms (49.7 days, the
 * counter wraps). Two tanks' own engines, each on its own uptime, link mode on and the facing doors armed: do they link?
 * Before the fix a door that had not joined in 24.86 days never sent its JOIN, and a tank heard 24.86-49.7 days before came
 * back as NEARBY (a second tank asking at the door). The evidence and the 0.4.2 side: audit-0.4.3-evidence/link-wrap/. */
typedef struct { int to, n; uint8_t b[LINK_FRAME_MAX]; } bc_fr_t;
static bc_fr_t bc_q[512]; static int bc_qn, bc_joins[3]; static int32_t bc_first;   /* (ms into the try the door first opened, -1 never) */
static void bc_send(void *ctx, uint32_t to, const uint8_t *b, int n) {
    (void)to; const int me = (int)(intptr_t)ctx;
    if (n > 3 && b[3] == 2 /* FR_JOIN */) bc_joins[me]++;
    if (bc_qn < 512 && n <= LINK_FRAME_MAX) { bc_q[bc_qn].to = me == 0 ? 1 : 0; bc_q[bc_qn].n = n; memcpy(bc_q[bc_qn].b, b, (size_t)n); bc_qn++; }
}
static int bc_room(void *ctx) { (void)ctx; return 2; }
static link_t *bc_L[3];
static void bc_init(int i, uint32_t tank, uint32_t boot_ms) {
    link_host_t h; memset(&h, 0, sizeof h); h.send = bc_send; h.room = bc_room; h.ctx = (void *)(intptr_t)i;
    link_init(bc_L[i], tank, &h, 7u + (uint32_t)i, boot_ms);
}
/* A (engine 0, its right door) and B (engine 1, its left door) at uptimes a0 / b0: the keepers hold the arrows; 20 s of radio */
static bool bc_try(uint32_t a0, uint32_t b0, uint32_t secs) {
    const uint32_t t0[2] = { a0, b0 }; const link_edge_t door[2] = { LINK_RIGHT, LINK_LEFT };
    bc_qn = 0; bc_joins[0] = bc_joins[1] = 0; bc_first = -1;
    for (int i = 0; i < 2; i++) { link_tick(bc_L[i], t0[i]); link_mode(bc_L[i], true); link_arm(bc_L[i], door[i]); }
    for (uint32_t ms = 0; ms <= secs * 1000u; ms += 20) {
        for (int i = 0; i < 2; i++) { link_tick(bc_L[i], t0[i] + ms); if (ms % 1000 == 0 && ms < 4000) link_arm(bc_L[i], door[i]); }
        int n = bc_qn; bc_qn = 0; static bc_fr_t cp[512]; memcpy(cp, bc_q, sizeof(bc_fr_t) * (size_t)n);
        for (int k = 0; k < n; k++) link_rx(bc_L[cp[k].to], cp[k].b, cp[k].n, -40, t0[cp[k].to] + ms);
        link_ev_t e; for (int i = 0; i < 2; i++) while (link_poll(bc_L[i], &e)) {}
        if (bc_first < 0 && link_neighbor(bc_L[0], LINK_RIGHT) && link_neighbor(bc_L[1], LINK_LEFT)) bc_first = (int32_t)ms;
    }
    return link_neighbor(bc_L[0], LINK_RIGHT) == 0x0bbbbbbbu && link_neighbor(bc_L[1], LINK_LEFT) == 0x0aaaaaaau;
}
static void bc_close(uint32_t a, uint32_t b) {             /* both keepers close the door; a moment of radio */
    link_close(bc_L[0], LINK_RIGHT); link_close(bc_L[1], LINK_LEFT);
    for (uint32_t ms = 0; ms < 500; ms += 20) { link_tick(bc_L[0], a + ms); link_tick(bc_L[1], b + ms); bc_qn = 0; link_ev_t e; for (int i = 0; i < 2; i++) while (link_poll(bc_L[i], &e)) {} }
}
/* R3b-2: the LINK page as link_dev.c opens it - link_mode(L, true) with no tick first: the radio was down for `gap` and nothing ticked
   the engine since its last tick at t0. Then the board ticks every 20 ms and the keeper holds the arrow (the page is up, so ticked).
   Returns the ms into the open when link mode ended (the page shuts: link_dev_poll), or -1; *seen: each tank had the other in its
   NEARBY row 400 ms in, before any arrow (the page's beacon went out); *linked: the door opened */
static int32_t bc_page(uint32_t t0, uint32_t gap, bool *seen, bool *linked) {
    const link_edge_t door[2] = { LINK_RIGHT, LINK_LEFT }; const uint32_t t1[2] = { t0, t0 - 7777u };
    for (int i = 0; i < 2; i++) { bc_init(i, i ? 0x0bbbbbbbu : 0x0aaaaaaau, 1000); link_tick(bc_L[i], t1[i]); }
    bc_qn = 0;
    for (int i = 0; i < 2; i++) link_mode(bc_L[i], true);       /* (the page opens: L->now is t1, gap ago) */
    int32_t shut = -1;
    for (uint32_t ms = 0; ms <= 20000; ms += 20) {
        for (int i = 0; i < 2; i++) {
            link_tick(bc_L[i], t1[i] + gap + ms);
            if (shut < 0 && !link_in_mode(bc_L[i]) && !link_neighbor(bc_L[i], door[i])) shut = (int32_t)ms;
            if (ms >= 500 && ms % 1000 == 500 && ms < 4000 && link_in_mode(bc_L[i])) link_arm(bc_L[i], door[i]);
        }
        int n = bc_qn; bc_qn = 0; static bc_fr_t cp[512]; memcpy(cp, bc_q, sizeof(bc_fr_t) * (size_t)n);
        for (int k = 0; k < n; k++) link_rx(bc_L[cp[k].to], cp[k].b, cp[k].n, -40, t1[cp[k].to] + gap + ms);
        link_ev_t e; for (int i = 0; i < 2; i++) while (link_poll(bc_L[i], &e)) {}
        if (ms == 400) { *seen = true; for (int i = 0; i < 2; i++) { link_peer_info_t pi[LINK_PEERS_MAX]; *seen &= link_peers(bc_L[i], pi, LINK_PEERS_MAX) == 1; } }
    }
    *linked = link_neighbor(bc_L[0], LINK_RIGHT) == 0x0bbbbbbbu && link_neighbor(bc_L[1], LINK_LEFT) == 0x0aaaaaaau;
    return shut;
}
int selftest_linkclock(void) {
    for (int i = 0; i < 3; i++) bc_L[i] = calloc(1, (size_t)link_size());
    if (!bc_L[0] || !bc_L[1] || !bc_L[2]) { printf("FAIL: selftest-linkclock: no memory\n"); return 1; }
    int fails = 0;
#define CCHECK(c, ...) do { if (!(c)) { printf("FAIL: selftest-linkclock: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
    const uint32_t DAY = 86400000u, W31 = 2147483648u;
    const struct { const char *what; uint32_t a, b; } rows[] = {
        { "both up 1 minute", 60000u, 60000u },                   /* (these linked before the fix: they still do) */
        { "both up 24 days", 24 * DAY, 24 * DAY },
        { "both just under 2^31 ms", W31 - 30000u, W31 - 30000u },
        { "A up 25 days, B up 1 day", 25 * DAY, DAY },
        { "both past the wrap (50 days)", 50 * DAY, 50 * DAY },   /* (uint32: 50 d reads as 0.29 d) */
        { "both up 25 days", 25 * DAY, 25 * DAY },                /* (these did not) */
        { "both up 30 days", 30 * DAY, 30 * DAY },
        { "both up 40 days", 40 * DAY, 40 * DAY },
        { "both up 49.7 days (2^32 ms - 60 s)", 0u - 60000u, 0u - 60000u },
    };
    for (int r = 0; r < (int)(sizeof rows / sizeof rows[0]); r++) {
        bc_init(0, 0x0aaaaaaau, 1000); bc_init(1, 0x0bbbbbbbu, 1000);
        CCHECK(bc_try(rows[r].a, rows[r].b, 20), "%s: no link (JOINs sent A %d, B %d)", rows[r].what, bc_joins[0], bc_joins[1]);
    }
    /* that door linked on day 4, closed; both still up on day 30 (26 days since that door's last JOIN) */
    bc_init(0, 0x0aaaaaaau, 1000); bc_init(1, 0x0bbbbbbbu, 1000);
    CCHECK(bc_try(4 * DAY, 4 * DAY, 20), "day 4: no link");
    bc_close(4 * DAY + 21000u, 4 * DAY + 21000u);
    CCHECK(bc_try(30 * DAY, 30 * DAY, 20), "both up 30 days, that door last linked 26 days before: no link (JOINs A %d, B %d)", bc_joins[0], bc_joins[1]);
    /* A heard tank G (link mode, its door to A armed) on day 2, and G went away. A's door linked to B on day 26.5 (its JOIN is
       recent). On day 27 tank B again: G must not count as a second tank asking, nor show as NEARBY */
    bc_init(0, 0x0aaaaaaau, 1000); bc_init(1, 0x0bbbbbbbu, 1000); bc_init(2, 0x0cccccccu, 1000);
    {   link_tick(bc_L[2], 2 * DAY); link_mode(bc_L[2], true); link_arm(bc_L[2], LINK_LEFT); bc_qn = 0;
        for (uint32_t ms = 0; ms < 2000; ms += 20) link_tick(bc_L[2], 2 * DAY + ms);
        link_tick(bc_L[0], 2 * DAY);
        for (int k = 0; k < bc_qn; k++) link_rx(bc_L[0], bc_q[k].b, bc_q[k].n, -40, 2 * DAY + 1000);   /* (A hears G) */
        bc_qn = 0; link_ev_t e; while (link_poll(bc_L[0], &e)) {} }
    CCHECK(bc_try(26 * DAY + DAY / 2, DAY, 20), "day 26.5: no link");
    bc_close(26 * DAY + DAY / 2 + 21000u, DAY + 21000u);
    {   link_tick(bc_L[0], 27 * DAY); link_mode(bc_L[0], true);
        link_peer_info_t pi[LINK_PEERS_MAX]; const int np = link_peers(bc_L[0], pi, LINK_PEERS_MAX);
        bool ghost = false; for (int i = 0; i < np; i++) ghost |= pi[i].tank == 0x0cccccccu;
        CCHECK(!ghost, "day 27: tank G, last heard 25 days before, shows as NEARBY"); }
    CCHECK(bc_try(27 * DAY, DAY + DAY / 2, 20), "day 27: tank G heard 25 days before blocked the link with B (JOINs A %d, B %d)", bc_joins[0], bc_joins[1]);
    /* an open link across 2^31 and across the wrap: it stays open, both ways */
    for (int w = 0; w < 2; w++) {                       /* (B's uptime 7.8 s behind: it crosses a moment later) */
        const uint32_t at = w ? 0u - 15000u : W31 - 15000u;
        bc_init(0, 0x0aaaaaaau, 1000); bc_init(1, 0x0bbbbbbbu, 1000);
        const bool open = bc_try(at, at - 7777u, 40);
        CCHECK(bc_first >= 0 && bc_first < 5000 && open, "%s: the link opened at %d ms (want < 5 s, before it), open after 40 s %d",
               w ? "15 s before the wrap" : "15 s before 2^31 ms", (int)bc_first, open);
    }
    /* R3b-2: the LINK page opened after the radio was down a while, no tick first: link mode stays up and the doors link */
    {   const struct { const char *what; uint32_t gap; } gaps[] = { { "3 minutes", 180000u }, { "1 hour", 3600000u }, { "2 days", 2 * DAY },
            { "25 days", 25 * DAY }, { "49.7 days less 5 minutes", 0u - 300000u } };
        for (int g = 0; g < (int)(sizeof gaps / sizeof gaps[0]); g++) {
            bool seen = false, linked = false; const int32_t shut = bc_page(DAY, gaps[g].gap, &seen, &linked);
            CCHECK(shut < 0 && seen && linked, "the LINK page opened %s after the radio went down: link mode %s (%d ms), the other tank %s in NEARBY at 0.4 s, %s",
                   gaps[g].what, shut >= 0 ? "ENDED" : "stayed up", (int)shut, seen ? "seen" : "NOT seen", linked ? "linked" : "NO LINK");
        }
    }
    {   /* (siblings) an arrow held with no tick first asks for its 5 s from the real time; one let go again before any tick (link mode
           off) is not armed after it */
        link_view_t v; const uint32_t t0 = DAY, gap = 3600000u;
        bc_init(0, 0x0aaaaaaau, 1000); link_tick(bc_L[0], t0);
        link_mode(bc_L[0], true); link_arm(bc_L[0], LINK_RIGHT);
        link_tick(bc_L[0], t0 + gap + 1000u); link_view(bc_L[0], &v);
        CCHECK(link_in_mode(bc_L[0]) && v.edge[LINK_RIGHT].state == LV_ARMED, "an arrow held an hour after the last tick: not asking 1 s later (mode %d, state %d)", link_in_mode(bc_L[0]), v.edge[LINK_RIGHT].state);
        link_tick(bc_L[0], t0 + gap + 6000u); link_view(bc_L[0], &v);
        CCHECK(v.edge[LINK_RIGHT].state != LV_ARMED, "an arrow held an hour after the last tick: still asking 6 s later");
        bc_init(0, 0x0aaaaaaau, 1000); link_tick(bc_L[0], 0u - 300000u);   /* (5 min before the wrap: an arrow time of 0 moved on by the gap
                                                       would read as asking) */
        link_mode(bc_L[0], true); link_arm(bc_L[0], LINK_RIGHT); link_mode(bc_L[0], false); link_mode(bc_L[0], true);
        link_tick(bc_L[0], 0u - 300000u + 60000u); link_view(bc_L[0], &v);
        CCHECK(v.edge[LINK_RIGHT].state != LV_ARMED, "an arrow let go (link mode off) before any tick asks after it");
    }
    for (int i = 0; i < 3; i++) free(bc_L[i]);
    if (fails) return 1;
    printf("selftest-linkclock: tanks up 1 min .. 50 days link (25, 30, 40, 49.7 days too); a tank heard 25 days before is not NEARBY or a second asker; a link stays open across 2^31 ms and the wrap; the LINK page opened 3 min .. 49.7 days after the radio went down stays up and links\n");
    return 0;
#undef CCHECK
}
