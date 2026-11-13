/* selftest_charge_reason.c - --selftest-charge-reason (0.4.2): why the AXP2101's
 * charger is stopped, from a set of register values.
 *
 * firmware/main/axp2101_reason.h is the decision battery_port_axp2101.c logs; it
 * is pure, so it is tested here. The case that started it: Hugo's pendant
 * (Discord #help 2026-11-09), PLUGGED at 2.9 V with st1 28, st2 15, chg 0a and
 * nothing saying why (docs/charge-stop-reason-0.4.2.md). */
#include <stdio.h>
#include <string.h>
#include "../firmware/main/axp2101_reason.h"

/* Hugo's dump: VBUS good, battery present, "not charging", the charger enabled, VBAT + TS channels on. The rest is
   the chip's power-on state by the datasheet: TS gating the charger with its source on, the window 29h / 58h, the
   timers on at 60 min / 12 h; TS at 500 mV (10 K at 50 uA, 25 C: table 6-5's 3E8h) */
static axp_chg_regs_t hugo(void) {
    axp_chg_regs_t r = { .st1 = 0x28, .st2 = 0x15, .chg_en = 0x0a, .adc_en = 0x03, .ts_ctl = 0x06, .vltf = 0x29, .vhtf = 0x58, .timer = 0xE6, .ts_raw = 0x3E8 };
    return r;
}
int selftest_charge_reason(void) {
    int fail = 0, n = 0;
#define CR(want, what) do { axp_stop_t got = axp_charge_stop(&r); n++; \
        if (got != (want)) { printf("FAIL: charge-reason: %s: got \"%s\", want \"%s\"\n", what, axp_charge_stop_text(got), axp_charge_stop_text(want)); fail++; } } while (0)
    axp_chg_regs_t r;
    /* the numbers the port prints */
    r = hugo();
    if (axp_ts_mv(&r) != 500 || axp_ts_cold_mv(&r) != 1312 || axp_ts_hot_mv(&r) != 176) { printf("FAIL: charge-reason: TS %d mV, window %d..%d (want 500, 176..1312)\n", axp_ts_mv(&r), axp_ts_hot_mv(&r), axp_ts_cold_mv(&r)); fail++; }
    if (axp_pre_timer_min(0xE6) != 60 || axp_chg_timer_h(0xE6) != 12 || axp_pre_timer_min(0x00) != 40 || axp_pre_timer_min(0x01) != 50 || axp_pre_timer_min(0x03) != 70 || axp_chg_timer_h(0x00) != 5 || axp_chg_timer_h(0x10) != 8 || axp_chg_timer_h(0x30) != 20)
        { printf("FAIL: charge-reason: the timers' minutes / hours\n"); fail++; }
    if (!axp_ts_gates(&r) || !axp_ts_live(&r)) { printf("FAIL: charge-reason: the power-on TS setup should gate and read\n"); fail++; }

    /* not stopped at all */
    r = hugo(); r.st1 = 0x08;                       CR(AXP_STOP_NA, "no cable");
    for (int st = 0; st < 5; st++) { r = hugo(); r.st2 = (uint8_t)(0x30 | st); r.boot_irq2 = 0x02; CR(AXP_STOP_NA, "trickle / pre-charge / CC / CV / done, an old timer flag kept from the boot"); }
    /* Hugo's, as dumped after the boot cleared the flags: nothing says why */
    r = hugo();                                     CR(AXP_STOP_NO_FLAG, "Hugo's dump, no flag");
    /* ... and with what the boot most likely found: the pre-charge timer ran out before the ESP was up */
    r = hugo(); r.boot_irq2 = 0x02;                 CR(AXP_STOP_TIMER_BOOT, "the timer flag found at the boot");
    r = hugo(); r.irq2 = 0x02;                      CR(AXP_STOP_TIMER, "the timer flag since the boot");
    r = hugo(); r.irq2 = 0x02; r.boot_irq2 = 0x02;  CR(AXP_STOP_TIMER, "both: since the boot says more");
    /* each single reason */
    r = hugo(); r.st1 = 0x20;                       CR(AXP_STOP_NO_BATTERY, "no battery");
    r = hugo(); r.chg_en = 0x08;                    CR(AXP_STOP_DISABLED, "REG 18 b1 clear");
    r = hugo(); r.chg_en = 0x08; r.irq2 = 0x07;     CR(AXP_STOP_DISABLED, "disabled wins over every flag");
    r = hugo(); r.ts_raw = 2 * 1313;                CR(AXP_STOP_TOO_COLD, "TS 1313 mV, 1 over the cold edge");
    r = hugo(); r.ts_raw = 2 * 1312;                CR(AXP_STOP_NO_FLAG, "TS on the cold edge");
    r = hugo(); r.ts_raw = 2 * 175;                 CR(AXP_STOP_TOO_HOT, "TS 175 mV, 1 under the hot edge");
    r = hugo(); r.ts_raw = 2 * 176;                 CR(AXP_STOP_NO_FLAG, "TS on the hot edge");
    r = hugo(); r.ts_raw = 0x3FFF;                  CR(AXP_STOP_TOO_COLD, "TS open (full scale): reads as too cold");
    r = hugo(); r.ts_raw = 0x3FFF; r.irq2 = 0x02;   CR(AXP_STOP_TOO_COLD, "a live TS reason before an old timer flag");
    r = hugo(); r.irq2 = 0x01;                      CR(AXP_STOP_OVERVOLT, "battery over-voltage");
    r = hugo(); r.boot_irq2 = 0x04;                 CR(AXP_STOP_DIE_HOT, "die too hot, at the boot");
    r = hugo(); r.irq2 = 0x18;                      CR(AXP_STOP_NO_FLAG, "4A b3 / b4 (charge start / done) are not reasons");
    r = hugo(); r.irq0 = 0xF3;                      CR(AXP_STOP_NO_FLAG, "48's other bits (gauge, work-mode temperature) are not reasons");
    /* TS set as a plain input (REG 50 b4): it can't stop the charger, whatever it reads or was flagged */
    r = hugo(); r.ts_ctl = 0x16; r.ts_raw = 0x3FFF; r.irq0 = 0x0C;   CR(AXP_STOP_NO_FLAG, "TS not gating: no temperature reason");
    /* TS gating but not readable (channel off, source off, no reading): the flags are all there is */
    r = hugo(); r.adc_en = 0x01; r.irq0 = 0x04;     CR(AXP_STOP_COLD_FLAG, "TS channel off, the cold flag set now");
    r = hugo(); r.adc_en = 0x01; r.boot_irq0 = 0x04;CR(AXP_STOP_COLD_SEEN, "TS channel off, the cold flag from the boot");
    r = hugo(); r.ts_ctl = 0x02; r.boot_irq0 = 0x08;CR(AXP_STOP_HOT_SEEN, "TS source off, the hot flag from the boot");
    r = hugo(); r.ts_raw = -1; r.irq0 = 0x08;       CR(AXP_STOP_HOT_FLAG, "no TS reading, the hot flag set now");
    r = hugo(); r.adc_en = 0x01; r.boot_irq0 = 0x0C;CR(AXP_STOP_COLD_SEEN, "both seen at the boot, TS unread: cold is named first");
    /* TS at ground is a reading (0), not "no reading": too hot */
    r = hugo(); r.ts_raw = 0;                       CR(AXP_STOP_TOO_HOT, "TS shorted to ground");
    /* half a millivolt over the cold edge counts (2625 counts = 1312.5 mV) */
    r = hugo(); r.ts_raw = 2 * 1312 + 1;            CR(AXP_STOP_TOO_COLD, "TS half a millivolt over the cold edge");
    r = hugo(); r.ts_raw = 2 * 176 - 1;             CR(AXP_STOP_TOO_HOT, "TS half a millivolt under the hot edge");
    /* inside the window but the chip's flag still set: its hysteresis (the chip clears the flag at normal) */
    r = hugo(); r.ts_raw = 2 * 1290; r.irq0 = 0x04; CR(AXP_STOP_COLD_FLAG, "1290 mV, the cold flag still set: inside the hysteresis");
    r = hugo(); r.ts_raw = 2 * 1290; r.irq0 = 0x04; r.irq2 = 0x02;   CR(AXP_STOP_COLD_FLAG, "a flag that holds now before the timer's history");
    /* TS read and inside its window, no flag now: what the boot found is over, not the reason */
    r = hugo(); r.boot_irq0 = 0x0C;                 CR(AXP_STOP_NO_FLAG, "TS in its window: the boot's temperature flags don't count");
    /* the TS current source: any of 01 / 10 / 11 reads, 00 doesn't */
    for (int src = 1; src < 4; src++) { r = hugo(); r.ts_ctl = (uint8_t)(src << 2 | 2); r.ts_raw = 0x3FFF; CR(AXP_STOP_TOO_COLD, "TS source on (01 / 10 / 11): read"); }
    r = hugo(); r.ts_ctl = 0x02; r.ts_raw = 0x3FFF; CR(AXP_STOP_NO_FLAG, "TS source off: its reading is not used");
    /* the order when two hold */
    r = hugo(); r.st1 = 0x20; r.chg_en = 0x08;      CR(AXP_STOP_NO_BATTERY, "no battery before disabled");
    r = hugo(); r.irq2 = 0x03;                      CR(AXP_STOP_TIMER, "the timer before over-voltage");
    r = hugo(); r.irq2 = 0x05;                      CR(AXP_STOP_OVERVOLT, "over-voltage before the die");
    /* every reason has its words */
    for (int s = 0; s < AXP_STOP_N; s++) if (!axp_charge_stop_text((axp_stop_t)s)[0] || !strcmp(axp_charge_stop_text((axp_stop_t)s), "?")) { printf("FAIL: charge-reason: reason %d has no words\n", s); fail++; }
    if (fail) { printf("selftest-charge-reason: FAILED (%d)\n", fail); return 2; }
    printf("selftest-charge-reason: PASS - %d register sets, each named: Hugo's pendant reads \"%s\" once the boot's flags are kept\n", n, axp_charge_stop_text(AXP_STOP_TIMER_BOOT));
    return 0;
}
