/* axp2101_reason.h - why the AXP2101's charger is stopped (0.4.2).
 *
 * The chip says "not charging" (REG 01 [2:0] = 101) and nothing more; the why
 * is spread over six registers. This is the decision, pure, so the host tests
 * it (sim/selftest_charge_reason.c). battery_port_axp2101.c reads the registers.
 *
 * Every bit below was checked against X-power-AXP2101_SWcharge_V1.0.pdf
 * (resources/ESP32-S3-Touch-AMOLED-1.75C/), 2026-11-10:
 *   REG 00  b5 VBUS good, b3 battery present                           (6.13.2.1)
 *   REG 01  [2:0] 000 trickle 001 pre-charge 010 CC 011 CV 100 done
 *           101 not charging                                           (6.13.2.2)
 *   REG 18  b1 cell battery charge enable                              (6.13.2.14)
 *   REG 30  b1 TS channel of the ADC on                                (6.13.2.30)
 *   REG 36/37 TS, 14 bit (36 [5:0] high), 0.5 mV a count               (6.13.2.33-34, table 6-7)
 *   REG 48  b3 battery OVER temperature in charge mode, b2 UNDER       (6.13.2.44)
 *   REG 4A  b2 die over temperature, b1 charger safety timer 1/2
 *           expired, b0 battery over voltage                           (6.13.2.46)
 *   REG 50  b4 0 = TS is the battery's thermistor and stops the charger,
 *           1 = a plain input that doesn't; [3:2] its current source
 *           (00 off). POR of both: EFUSE - it can differ by chip lot   (6.13.2.47)
 *   REG 54  VLTF-CHG = N x 32 mV: TS ABOVE it = too cold (POR 29h, ~0 C)
 *   REG 55  VHTF-CHG = N x 2 mV:  TS BELOW it = too hot  (POR 58h)     (6.7.4.4, 6.13.2.50-51)
 *   REG 67  b2 pre-charge timer on, [1:0] 40/50/60/70 min (POR 60);
 *           b6 charge timer on, [5:4] 5/8/12/20 h (POR 12)             (6.13.2.65)
 * REG 48 / 4A are IRQ status: a bit is set when its event happens AND its
 * enable is set (REG 40 / 42, all on by default; we never write them). The
 * timer bit (4A b1) then stays until 1 is written to it (6.12.1): HISTORY, not
 * the state now. The temperature, over-voltage and die bits also clear by
 * themselves once the thing is back to normal (their rows in 6.13.2.44 / 46),
 * so one that reads set NOW still holds - which is how a battery inside the
 * window's hysteresis (REG 52 / 53, not read here) is still named.
 * The boot clears all three registers (battery_port_key_init, for the
 * power-on press). A pendant that sat dead on the cable runs its pre-charge
 * timer out before the ESP ever boots: the flag is there at the boot and gone
 * after it. So the boot's copy is kept and read here too.
 * The TS window is the one reason that can be read live. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t st1, st2;            /* REG 00, 01 */
    uint8_t chg_en;              /* REG 18 */
    uint8_t adc_en;              /* REG 30 */
    uint8_t irq0, irq2;          /* REG 48, 4A as they read now (since the boot's clear) */
    uint8_t boot_irq0, boot_irq2;/* ... and as the boot found them, before it cleared them */
    uint8_t ts_ctl;              /* REG 50 */
    uint8_t vltf, vhtf;          /* REG 54, 55 */
    uint8_t timer;               /* REG 67 */
    int     ts_raw;              /* REG 36/37, 14 bit (0 is a reading: TS at ground); < 0 = not read */
} axp_chg_regs_t;

typedef enum {
    AXP_STOP_NA = 0,             /* not stopped: no cable, or the chip is charging / done */
    AXP_STOP_NO_BATTERY,
    AXP_STOP_DISABLED,           /* REG 18 b1 = 0 */
    AXP_STOP_TOO_COLD,           /* the TS window, live */
    AXP_STOP_TOO_HOT,
    AXP_STOP_TIMER,              /* 4A b1 since the boot */
    AXP_STOP_TIMER_BOOT,         /* ... or found set at the boot */
    AXP_STOP_COLD_FLAG,          /* 48 b2 / b3 set now: still outside (the chip clears them at normal), by the hysteresis or TS unread */
    AXP_STOP_HOT_FLAG,
    AXP_STOP_COLD_SEEN,          /* ... or found at the boot, and TS can't be read to say it still is */
    AXP_STOP_HOT_SEEN,
    AXP_STOP_OVERVOLT,           /* 4A b0 */
    AXP_STOP_DIE_HOT,            /* 4A b2 */
    AXP_STOP_NO_FLAG,            /* stopped, and no register says why */
    AXP_STOP_N
} axp_stop_t;

static inline bool axp_ts_gates(const axp_chg_regs_t *r)  { return !(r->ts_ctl & 0x10); }
static inline bool axp_ts_live(const axp_chg_regs_t *r)   { return (r->adc_en & 0x02) && (r->ts_ctl & 0x0C) && r->ts_raw >= 0; }
static inline int  axp_ts_mv(const axp_chg_regs_t *r)     { return r->ts_raw / 2; }          /* 0.5 mV a count */
static inline int  axp_ts_cold_mv(const axp_chg_regs_t *r){ return r->vltf * 32; }           /* above: too cold */
static inline int  axp_ts_hot_mv(const axp_chg_regs_t *r) { return r->vhtf * 2; }            /* below: too hot */
static inline int  axp_pre_timer_min(uint8_t timer)       { return 40 + (timer & 3) * 10; }
static inline int  axp_chg_timer_h(uint8_t timer)         { static const int8_t H[4] = { 5, 8, 12, 20 }; return H[(timer >> 4) & 3]; }

static inline axp_stop_t axp_charge_stop(const axp_chg_regs_t *r) {
    if (!(r->st1 & 0x20) || (r->st2 & 0x07) != 0x05) return AXP_STOP_NA;
    if (!(r->st1 & 0x08)) return AXP_STOP_NO_BATTERY;
    if (!(r->chg_en & 0x02)) return AXP_STOP_DISABLED;
    if (axp_ts_gates(r)) {                                         /* the reasons that hold NOW first */
        if (axp_ts_live(r)) {                                      /* (in counts, 0.5 mV each: no half lost) */
            if (r->ts_raw > axp_ts_cold_mv(r) * 2) return AXP_STOP_TOO_COLD;
            if (r->ts_raw < axp_ts_hot_mv(r) * 2)  return AXP_STOP_TOO_HOT;
        }
        if (r->irq0 & 0x04) return AXP_STOP_COLD_FLAG;
        if (r->irq0 & 0x08) return AXP_STOP_HOT_FLAG;
    }
    if (r->irq2 & 0x02) return AXP_STOP_TIMER;
    if (r->boot_irq2 & 0x02) return AXP_STOP_TIMER_BOOT;
    if (axp_ts_gates(r) && !axp_ts_live(r)) {                      /* (TS read and in its window: the boot's flag is over) */
        if (r->boot_irq0 & 0x04) return AXP_STOP_COLD_SEEN;
        if (r->boot_irq0 & 0x08) return AXP_STOP_HOT_SEEN;
    }
    if ((r->irq2 | r->boot_irq2) & 0x01) return AXP_STOP_OVERVOLT;
    if ((r->irq2 | r->boot_irq2) & 0x04) return AXP_STOP_DIE_HOT;
    return AXP_STOP_NO_FLAG;
}
static inline const char *axp_charge_stop_text(axp_stop_t s) {
    static const char *const T[AXP_STOP_N] = {
        "n/a (not stopped)",
        "NO BATTERY (REG 00 b3 = 0)",
        "CHARGER DISABLED (REG 18 b1 = 0)",
        "BATTERY TOO COLD (TS above its window, now)",
        "BATTERY TOO HOT (TS below its window, now)",
        "SAFETY TIMER expired since this boot (REG 4A b1): the chip's battery safe mode - unplug and plug back in",
        "SAFETY TIMER had expired before this boot (REG 4A b1 at boot): the chip's battery safe mode - unplug and plug back in",
        "BATTERY TOO COLD (REG 48 b2 is set: not yet back inside the window)",
        "BATTERY TOO HOT (REG 48 b3 is set: not yet back inside the window)",
        "BATTERY TOO COLD was flagged before this boot (REG 48 b2); TS not readable now",
        "BATTERY TOO HOT was flagged before this boot (REG 48 b3); TS not readable now",
        "BATTERY OVER-VOLTAGE was flagged (REG 4A b0)",
        "DIE TOO HOT was flagged (REG 4A b2)",
        "no flag set: the registers don't say why",
    };
    return s >= 0 && s < AXP_STOP_N ? T[s] : "?";
}
