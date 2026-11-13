/* selftest_board_probe.c - --selftest-board-probe (0.4.2): which board the
 * firmware reads, from the chips that answered on its I2C bus.
 *
 * firmware/main/board_probe.h is the decision display_port_init makes; it is
 * pure, so it is tested here. A 1.75C rev 1.1 straps its ES7210 mic ADC to 0x42,
 * and 0.4.1 asked at 0x40 only: that pendant stayed dark (docs/release-0.4.2.md
 * item 4). The test pins three things:
 *  1. every board we support reads as it did in 0.4.1 (a named table);
 *  2. over EVERY combination of answers, with the ES7210 absent or at 0x40, the
 *     decision is exactly 0.4.1's (old_decide below is 0.4.1's code, verbatim
 *     in logic) - nothing changes for a board that answers as ours do;
 *  3. the ES7210 at 0x41..0x43 reads exactly as it would at 0x40 (the rev 1.1
 *     pendant is the round 1.75C; a watch / square / 1.75-B whose mic chip moved
 *     is still itself), and an address outside 0x40..0x43 is no ES7210.
 * PT_BOARD_PROBE_OLD=1: the control - the decision as 0.4.1 made it (only 0x40
 * counts), so part 3 fails and shows the test can see the bug. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/main/board_probe.h"

typedef struct { bool is175, round, watch, square; const char *id; } want_t;

/* 0.4.1's display_port_init + board_port_hw, the logic as it shipped (only 0x40 is the ES7210) */
static want_t old_decide(bool expander, uint8_t es, bool rtc, bool cst) {
    want_t w = { false, false, false, false, NULL };
    bool es40 = es == 0x40;
    w.is175 = expander && es40;
    bool mic = !expander && es40;
    w.watch = mic && rtc;
    w.round = w.is175 || (mic && !w.watch);
    if (w.watch) { w.square = cst; if (w.square) w.watch = false; }
    w.id = w.square ? "square216" : w.watch ? "watch206" : w.round ? "round175c" : expander ? "amoled18" : NULL;
    return w;
}

static want_t new_decide(bool expander, uint8_t es, bool rtc, bool cst) {
    if (getenv("PT_BOARD_PROBE_OLD")) return old_decide(expander, es, rtc, cst);
    board_chips_t c = { expander, es, false, false };
    /* the asks as display_port_init makes them: the RTC only for a mic board, the CST92xx only for a watch candidate */
    if (board_decide(c).mic) c.rtc = rtc;
    if (board_decide(c).watch) c.cst9217 = cst;
    board_kind_t k = board_decide(c);
    want_t w = { k.is175, k.round, k.watch, k.square, board_kind_id(k, expander) };
    return w;
}

static int same(want_t a, want_t b) {
    return a.is175 == b.is175 && a.round == b.round && a.watch == b.watch && a.square == b.square &&
           ((!a.id && !b.id) || (a.id && b.id && !strcmp(a.id, b.id)));
}
static const char *show(want_t w) { return w.id ? w.id : "(none)"; }

int selftest_board_probe(void) {
    int fail = 0, n = 0;
    /* 1. the boards, by name: expander, ES7210 address, RTC, CST92xx -> what 0.4.1 read and what must still be read */
    struct { const char *name; bool exp; uint8_t es; bool rtc, cst; want_t want; } B[] = {
        { "1.8 (TCA9554, no ES7210)",              true,  0,    true,  false, { false, false, false, false, "amoled18" } },
        { "1.75 / 1.75-B (TCA9554 + ES7210 0x40)",  true,  0x40, true,  false, { true,  true,  false, false, "round175c" } },
        { "1.75C (ES7210 0x40, no RTC)",            false, 0x40, false, false, { false, true,  false, false, "round175c" } },
        { "2.06 watch (ES7210 0x40 + RTC)",         false, 0x40, true,  false, { false, false, true,  false, "watch206" } },
        { "2.16 square (ES7210 0x40 + RTC + CST)",  false, 0x40, true,  true,  { false, false, false, true,  "square216" } },
        { "PMIC alone / dead bus (nothing)",        false, 0,    false, false, { false, false, false, false, NULL } },
        /* the rev 1.1 family: the mic chip at 0x41..0x43 */
        { "1.75C rev 1.1 (ES7210 0x42)",            false, 0x42, false, false, { false, true,  false, false, "round175c" } },
        { "1.75C, ES7210 0x41",                     false, 0x41, false, false, { false, true,  false, false, "round175c" } },
        { "1.75C, ES7210 0x43",                     false, 0x43, false, false, { false, true,  false, false, "round175c" } },
        { "1.75-B, ES7210 0x42",                    true,  0x42, true,  false, { true,  true,  false, false, "round175c" } },
        { "watch, ES7210 0x42",                     false, 0x42, true,  false, { false, false, true,  false, "watch206" } },
        { "square, ES7210 0x43",                    false, 0x43, true,  true,  { false, false, false, true,  "square216" } },
    };
    for (size_t i = 0; i < sizeof B / sizeof B[0]; i++, n++) {
        want_t got = new_decide(B[i].exp, B[i].es, B[i].rtc, B[i].cst);
        int ok = same(got, B[i].want);
        printf("selftest-board-probe: %-40s -> %-10s %s\n", B[i].name, show(got), ok ? "ok" : "WRONG");
        if (!ok) { fail++; printf("selftest-board-probe:   wanted %s (175 %d round %d watch %d square %d)\n", show(B[i].want),
                                  B[i].want.is175, B[i].want.round, B[i].want.watch, B[i].want.square); }
    }
    /* 2 + 3. every combination of answers */
    static const uint8_t ES[] = { 0, 0x40, 0x41, 0x42, 0x43, 0x3F, 0x44, 0x20, 0x34 };
    int same041 = 0, moved = 0, outside = 0;
    for (int exp = 0; exp < 2; exp++) for (size_t e = 0; e < sizeof ES; e++) for (int rtc = 0; rtc < 2; rtc++) for (int cst = 0; cst < 2; cst++) {
        uint8_t es = ES[e];
        want_t got = new_decide(exp, es, rtc, cst);
        want_t want = board_probe_es7210_addr_ok(es) ? old_decide(exp, 0x40, rtc, cst)   /* 0x40..0x43: as 0.4.1 read 0x40 */
                                                      : old_decide(exp, 0, rtc, cst);     /* anything else: no ES7210 */
        if (es == 0 || es == 0x40) same041++;
        else if (board_probe_es7210_addr_ok(es)) moved++;
        else outside++;
        n++;
        if (!same(got, want)) {
            fail++;
            printf("selftest-board-probe: expander %d ES7210 0x%02x RTC %d CST %d -> %s, wanted %s\n", exp, es, rtc, cst, show(got), show(want));
        }
    }
    printf("selftest-board-probe: %d cases (%d as 0.4.1 answered, %d with the mic chip moved, %d outside its range): %s\n",
           n, same041, moved, outside, fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
