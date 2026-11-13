/* board_probe.h - which board this is, from the chips that answered on the I2C bus (0.4.2).
 *
 * Pure: no ESP-IDF, so the host can table-test it (sim/selftest_board_probe.c,
 * --selftest-board-probe in make check). display_port_init asks the chips and
 * hands the answers here; nothing else decides the board.
 *
 * The ES7210 microphone ADC has two address pins (AD0 / AD1): it answers at
 * 0x40 + 0..3. Every board we know of straps it to 0x40, but a 1.75C rev 1.1
 * answers at 0x42 (a keeper's board, 2026-11-06: docs/release-0.4.2.md item 4).
 * So the probe tries 0x40..0x43 and keeps the first that answers; es7210 below
 * is that address, 0 for none.
 *
 * rtc is only asked with no expander and an ES7210 (the watch / square
 * candidate); cst9217 only for a watch candidate (the square's touch chip). An
 * answer that was never asked is false. */
#ifndef BOARD_PROBE_H
#define BOARD_PROBE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ES7210_ADDR_FIRST 0x40     /* the ES7210's base address (AD0 = AD1 = 0) */
#define ES7210_ADDR_COUNT 4        /* ... + 0..3 */

typedef struct {
    bool    expander;              /* the TCA9554 IO expander (the 1.8, the 1.75 / 1.75-B) */
    uint8_t es7210;                /* the address the ES7210 answered at, 0 = none */
    bool    rtc;                   /* the PCF85063 (the watch, the square) */
    bool    cst9217;               /* the CST92xx touch chip (the square) */
} board_chips_t;

typedef struct {
    bool is175;                    /* the 1.75 / 1.75-B: expander AND an ES7210 (runs the bowl's image) */
    bool round;                    /* the round panel: the 1.75C, or the 1.75 / 1.75-B */
    bool watch;                    /* the 2.06 watch */
    bool square;                   /* the 2.16 square */
    bool mic;                      /* no expander, an ES7210: the 1.75C / watch / square family */
} board_kind_t;

static inline bool board_probe_es7210_addr_ok(uint8_t a) {
    return a >= ES7210_ADDR_FIRST && a < ES7210_ADDR_FIRST + ES7210_ADDR_COUNT;
}

static inline board_kind_t board_decide(board_chips_t c) {
    board_kind_t k = { false, false, false, false, false };
    bool es = board_probe_es7210_addr_ok(c.es7210);
    k.is175 = c.expander && es;
    k.mic   = !c.expander && es;
    k.watch = k.mic && c.rtc;
    k.round = k.is175 || (k.mic && !k.watch);
    if (k.watch && c.cst9217) { k.square = true; k.watch = false; }
    return k;
}

/* the board as a release id (version.h PT_BOARD), NULL when the chips can't tell
 * (the 1.8's path taken on the PMIC alone, or a dead bus) */
static inline const char *board_kind_id(board_kind_t k, bool expander) {
    return k.square ? "square216" : k.watch ? "watch206" : k.round ? "round175c" : expander ? "amoled18" : NULL;
}
#endif
