/* linknote_test.inc - --selftest-linknote / --linknote-shot <prefix> (0.4.3, Strato 11-23: "sure those words work").
 * THE LINK DID NOT START: when LINK is tapped on a big screen and its radio can't start (radio_hosted_why: no room,
 * no stack, no answer from the C6), link mode's page opens as this note instead of the LINK page - the NOT YET
 * card's dress (render_library_note's: the tank dimmed, the ink card with its double teal edge, OK, at 2x). OK or a
 * tap outside closes it; nothing restarts. Checked here: the card is the library note's size and place, at 2x and
 * at 1x (its scratch refused); OK answers at its middle and its reach, the card's empty corner does nothing, a tap
 * outside closes; every line fits inside the card. */
#ifdef TANK_P4
static int linknote_check(bool fail) {
    int bad = 0, tlx, tly, brx, bry, okx, oky, lx0, ly0, lx1, ly1;
    static uint16_t fb[TANK_W * TANK_H];
    render_debug_card2x_fail(fail);
    render_tank(&tank, fb, TANK_W);
    render_link_nostart(fb, TANK_W);
    render_big_link_note_where(BIGLN_TL, &tlx, &tly); render_big_link_note_where(BIGLN_BR, &brx, &bry); render_big_link_note_where(BIGLN_OK, &okx, &oky);
    render_debug_card2x_fail(fail);                       /* (the library note's place, drawn the same way, for the size) */
    render_library_note(fb, TANK_W);
    render_big_library_note_where(BIGLN_TL, &lx0, &ly0); render_big_library_note_where(BIGLN_BR, &lx1, &ly1);
    const char *how = fail ? "1x" : "2x";
    if (tlx != lx0 || tly != ly0 || brx != lx1 || bry != ly1) { printf("FAIL: linknote %s: card %d,%d..%d,%d, the library note's %d,%d..%d,%d\n", how, tlx, tly, brx, bry, lx0, ly0, lx1, ly1); bad++; }
    const int w = brx - tlx + 1;
    if (fail ? w > 360 : w < 600) { printf("FAIL: linknote %s: %d px wide\n", how, w); bad++; }
    render_tank(&tank, fb, TANK_W); render_link_nostart(fb, TANK_W);   /* (the taps read the card as last drawn) */
    if (render_link_nostart_tap((float)okx, (float)oky) != LNOTE_TAP_OK) { printf("FAIL: linknote %s: OK's middle %d,%d is not OK\n", how, okx, oky); bad++; }
    if (render_link_nostart_tap((float)tlx + 6, (float)tly + 6) != LNOTE_TAP_NONE) { printf("FAIL: linknote %s: the card's corner did something\n", how); bad++; }
    if (render_link_nostart_tap((float)(tlx > 20 ? tlx - 20 : brx + 20), (float)oky) != LNOTE_TAP_OUTSIDE) { printf("FAIL: linknote %s: a tap outside is not OUTSIDE\n", how); bad++; }
    const int inner = (fail ? w : w / 2) - 24;               /* the card's 1x width, less its edges and a margin */
    for (int i = 0; render_big_link_note_line(i); i++) {
        const int tw = render_text_w(render_big_link_note_line(i), 2);
        if (tw > inner) { printf("FAIL: linknote: \"%s\" is %d px, the card has %d\n", render_big_link_note_line(i), tw, inner); bad++; }
    }
    /* STARTING THE LINK (11-24): the same card in the same place, its words inside it, the same OK */
    for (int i = 0; render_big_link_wait_line(i); i++) {
        const int tw = render_text_w(render_big_link_wait_line(i), 2);
        if (tw > inner) { printf("FAIL: linknote: \"%s\" is %d px, the card has %d\n", render_big_link_wait_line(i), tw, inner); bad++; }
    }
    static uint16_t fw[TANK_W * TANK_H];
    render_tank(&tank, fw, TANK_W); render_link_wait(fw, TANK_W);
    long diff = 0; for (int i = 0; i < TANK_W * TANK_H; i++) diff += fw[i] != fb[i];
    if (!diff) { printf("FAIL: linknote %s: the waiting card draws as THE LINK DID NOT START\n", how); bad++; }
    int wx0, wy0; render_big_link_note_where(BIGLN_TL, &wx0, &wy0);
    if (wx0 != tlx || wy0 != tly) { printf("FAIL: linknote %s: the waiting card moved (%d,%d)\n", how, wx0, wy0); bad++; }
    if (render_link_nostart_tap((float)okx, (float)oky) != LNOTE_TAP_OK) { printf("FAIL: linknote %s: the waiting card's OK is not OK\n", how); bad++; }
    /* the one install's cards: their words fit, UPDATING draws its bar (0% and 100% differ) where OK would be, no OK */
    for (int f = 0; f < 3; f++) for (int i = 0; render_big_link_upd_line(f, i); i++) {
        const int tw = render_text_w(render_big_link_upd_line(f, i), 2);
        if (tw > inner) { printf("FAIL: linknote: \"%s\" is %d px, the card has %d\n", render_big_link_upd_line(f, i), tw, inner); bad++; }
    }
    static uint16_t f0[TANK_W * TANK_H], f1[TANK_W * TANK_H];
    render_tank(&tank, f0, TANK_W); render_link_updating(f0, TANK_W, 0);
    render_tank(&tank, f1, TANK_W); render_link_updating(f1, TANK_W, 100);
    long bd = 0; for (int i = 0; i < TANK_W * TANK_H; i++) bd += f0[i] != f1[i];
    if (bd < 200) { printf("FAIL: linknote %s: UPDATING's bar does not move (%ld px differ)\n", how, bd); bad++; }
    if (f1[(size_t)oky * TANK_W + okx] == fb[(size_t)oky * TANK_W + okx] && f0[(size_t)oky * TANK_W + okx] == fb[(size_t)oky * TANK_W + okx]) { printf("FAIL: linknote %s: UPDATING shows OK\n", how); bad++; }
    /* (H1) STOPPED with no try left says so: not TAP LINK TO TRY AGAIN, and it draws differently from the one with a try */
    for (int i = 0; render_big_link_upd_line(2, i); i++) if (strstr(render_big_link_upd_line(2, i), "TRY AGAIN")) { printf("FAIL: linknote: no try left, yet \"%s\"\n", render_big_link_upd_line(2, i)); bad++; }
    render_tank(&tank, f0, TANK_W); render_link_update_failed(f0, TANK_W, true);
    render_tank(&tank, f1, TANK_W); render_link_update_failed(f1, TANK_W, false);
    bd = 0; for (int i = 0; i < TANK_W * TANK_H; i++) bd += f0[i] != f1[i];
    if (bd < 200) { printf("FAIL: linknote %s: STOPPED with and without a try draw the same\n", how); bad++; }
    if (render_link_nostart_tap((float)okx, (float)oky) != LNOTE_TAP_OK) { printf("FAIL: linknote %s: STOPPED's OK is not OK\n", how); bad++; }
    /* THE RESTART CHAIN's card (Strato 11-25): STARTING LINK, its lines inside the card (the first wrapped), one line taller than the
       plain card, the same width and middle; its OK lower, and answering there (the taps read the card as last drawn); then the
       plain STARTING THE LINK card again exactly as before */
    for (int i = 1; render_big_link_esc_line(i); i++) {
        const int tw = render_text_w(render_big_link_esc_line(i), 2);
        if (tw > inner) { printf("FAIL: linknote: \"%s\" is %d px, the card has %d\n", render_big_link_esc_line(i), tw, inner); bad++; }
    }
    if (render_text_w("YOUR DEVICE MAY RESTART SEVERAL TIMES", 2) <= inner) { printf("FAIL: linknote %s: line 1 fits unwrapped - the card would not be the approved one\n", how); bad++; }
    if (render_text_w("YOUR DEVICE MAY RESTART SEVERAL", 2) <= inner) { printf("FAIL: linknote %s: line 1 wraps later than drawn\n", how); bad++; }
    render_tank(&tank, fw, TANK_W); render_link_escape(fw, TANK_W);
    int ex0, ey0, ex1, ey1, eox, eoy;
    render_big_link_note_where(BIGLN_TL, &ex0, &ey0); render_big_link_note_where(BIGLN_BR, &ex1, &ey1); render_big_link_note_where(BIGLN_OK, &eox, &eoy);
    const int grow = (ey1 - ey0) - (bry - tly), lh = fail ? 18 : 36;   /* (LNOTE_LH: 18 px a line at 1x) */
    if (ex0 != tlx || ex1 != brx || grow < lh - 2 || grow > lh + 2) { printf("FAIL: linknote %s: the chain's card %d,%d..%d,%d is not the plain one a line taller (%d px)\n", how, ex0, ey0, ex1, ey1, grow); bad++; }
    if ((ey0 + ey1) / 2 < (tly + bry) / 2 - 2 || (ey0 + ey1) / 2 > (tly + bry) / 2 + 2) { printf("FAIL: linknote %s: the chain's card is not in the middle\n", how); bad++; }
    if (eoy <= oky) { printf("FAIL: linknote %s: the chain's OK (%d) not below the plain card's (%d)\n", how, eoy, oky); bad++; }
    if (render_link_nostart_tap((float)eox, (float)eoy) != LNOTE_TAP_OK) { printf("FAIL: linknote %s: the chain's OK at %d,%d is not OK\n", how, eox, eoy); bad++; }
    if (render_link_nostart_tap((float)ex0 + 6, (float)ey1 - 4) != LNOTE_TAP_NONE) { printf("FAIL: linknote %s: the chain card's lower corner did something\n", how); bad++; }
    render_tank(&tank, f0, TANK_W); render_link_wait(f0, TANK_W);
    { int px0, py0, px1, py1, pox, poy; render_big_link_note_where(BIGLN_TL, &px0, &py0); render_big_link_note_where(BIGLN_BR, &px1, &py1); render_big_link_note_where(BIGLN_OK, &pox, &poy);
      if (px0 != tlx || py0 != tly || px1 != brx || py1 != bry || pox != okx || poy != oky) { printf("FAIL: linknote %s: after the chain's card the plain STARTING THE LINK moved\n", how); bad++; } }
    render_tank(&tank, f1, TANK_W); render_link_wait(f1, TANK_W);
    bd = 0; for (int i = 0; i < TANK_W * TANK_H; i++) bd += f0[i] != f1[i];
    long ed = 0; for (int i = 0; i < TANK_W * TANK_H; i++) ed += fw[i] != f0[i];
    if (bd || ed < 200) { printf("FAIL: linknote %s: the plain card is not itself, or the chain's draws as it\n", how); bad++; }
    printf("selftest-linknote: %s chain card %d,%d..%d,%d, OK at %d,%d\n", how, ex0, ey0, ex1, ey1, eox, eoy);
    printf("selftest-linknote: %s card %d,%d..%d,%d, OK at %d,%d\n", how, tlx, tly, brx, bry, okx, oky);
    return bad;
}
#endif
static int selftest_linknote(void) {
#ifdef TANK_P4
    static uint16_t scene[TANK_W * TANK_H];
    tank_init(&tank, 77); tank_new_population(&tank); tank.night = false;
    render_set_scene_cache(scene); render_set_vignette_cache(vig_buf); render_set_card_cache(card_buf); render_set_modal_cache(give_buf, gift_buf); render_set_dirty_mask(dirty_buf);
    render_big_pocket_pages(false);
    int bad = 0;
    if (!render_big_link_note_line(0) || strcmp(render_big_link_note_line(0), "THE LINK DID NOT START")) { printf("FAIL: linknote: the title is not Strato's\n"); bad++; }
    if (!render_big_link_note_line(1) || strcmp(render_big_link_note_line(1), "GET HELP AT") || !render_big_link_note_line(2) || strcmp(render_big_link_note_line(2), "POCKETANK.COM/HELP")
        || render_big_link_note_line(3)) { printf("FAIL: linknote: the words are not GET HELP AT / POCKETANK.COM/HELP\n"); bad++; }
    if (!render_big_link_wait_line(0) || !render_big_link_wait_line(2) || render_big_link_wait_line(3)) { printf("FAIL: linknote: the waiting card wants a title and two lines\n"); bad++; }
    if (strcmp(render_big_link_wait_line(0), "STARTING THE LINK")) { printf("FAIL: linknote: the plain card is not STARTING THE LINK\n"); bad++; }
    {   char joined[96] = ""; for (int i = 1; render_big_link_esc_line(i) && i < 3; i++) { if (i > 1) strcat(joined, " "); strcat(joined, render_big_link_esc_line(i)); }
        if (strcmp(render_big_link_esc_line(0), "STARTING LINK") || strcmp(joined, "YOUR DEVICE MAY RESTART SEVERAL TIMES") || !render_big_link_esc_line(3)
            || strcmp(render_big_link_esc_line(3), "THIS IS NORMAL") || render_big_link_esc_line(4)) { printf("FAIL: linknote: the chain's card is not STARTING LINK / YOUR DEVICE MAY RESTART SEVERAL TIMES (wrapped) / THIS IS NORMAL\n"); bad++; } }
    bad += linknote_check(false);
    bad += linknote_check(true);
    render_debug_card2x_fail(false);
    printf("selftest-linknote: %s\n", bad ? "FAIL" : "PASS");
    return bad;
#else
    printf("selftest-linknote: the big screens only\n"); return 0;
#endif
}
static int linknote_shot(const char *prefix) {
#ifdef TANK_P4
    static uint16_t fb[TANK_W * TANK_H], scene[TANK_W * TANK_H]; char path[512];
    tank_init(&tank, 2024); tank_new_population(&tank);
    render_set_scene_cache(scene); render_set_vignette_cache(vig_buf); render_set_card_cache(card_buf); render_set_modal_cache(give_buf, gift_buf); render_set_dirty_mask(dirty_buf);
    render_big_pocket_pages(false);
    for (int i = 0; i < 4 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
    tank.night = false;
    render_tank(&tank, fb, TANK_W); render_link_nostart(fb, TANK_W);
    snprintf(path, sizeof path, "%s_link_did_not_start.ppm", prefix); write_ppm(path, fb);
    printf("linknote-shot: %s\n", path);
    render_tank(&tank, fb, TANK_W); render_link_wait(fb, TANK_W);
    snprintf(path, sizeof path, "%s_starting_the_link.ppm", prefix); write_ppm(path, fb);
    printf("linknote-shot: %s\n", path);
    render_tank(&tank, fb, TANK_W); render_link_escape(fb, TANK_W);
    snprintf(path, sizeof path, "%s_starting_link_chain.ppm", prefix); write_ppm(path, fb);
    printf("linknote-shot: %s\n", path);
    render_tank(&tank, fb, TANK_W); render_link_updating(fb, TANK_W, 40);
    snprintf(path, sizeof path, "%s_updating_the_link.ppm", prefix); write_ppm(path, fb);
    printf("linknote-shot: %s\n", path);
    render_tank(&tank, fb, TANK_W); render_link_update_failed(fb, TANK_W, true);
    snprintf(path, sizeof path, "%s_link_update_stopped.ppm", prefix); write_ppm(path, fb);
    printf("linknote-shot: %s\n", path);
    render_tank(&tank, fb, TANK_W); render_link_update_failed(fb, TANK_W, false);   /* (H1) no try left: no TAP LINK TO TRY AGAIN */
    snprintf(path, sizeof path, "%s_link_update_stopped_no_tries.ppm", prefix); write_ppm(path, fb);
    printf("linknote-shot: %s\n", path);
    return 0;
#else
    (void)prefix; printf("linknote-shot: the big screens only\n"); return 0;
#endif
}
