/* battery_port_axp2101.c — battery fraction + charge state from the AXP2101
 * PMIC's fuel gauge (I2C 0x34): STATUS1 bit3 = battery present, bit5 = VBUS
 * good (the cable), STATUS2 bits[7:5] == 1 = charging, bits[2:0] == 4 = the
 * charge is done, 0xA4 = state of charge in percent. Reads are cached for
 * ~1 s (the pill answers a plug-in within a second); any I2C error or
 * absent battery hides the meter. */
#include "battery_port.h"
#include "axp2101_reason.h"
#include "advanced.h"
#include "display_port.h"      /* board_is_square */
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <strings.h>
#include <string.h>
#include <stdio.h>

#define AXP2101_ADDR      0x34
#define REG_STATUS1       0x00
#define REG_STATUS2       0x01
#define REG_COMMON_CFG    0x10
#define REG_BAT_PERCENT   0xA4

static i2c_master_dev_handle_t s_dev;
static int64_t s_last_us = -1;
static float s_frac; static bool s_charging, s_valid;
static uint8_t s_st1, s_st2;                     /* the last good STATUS1 / STATUS2 */
static bool rd(uint8_t reg, uint8_t *val);
static uint8_t s_boot_irq0, s_boot_irq2;           /* REG 48 / 4A as the boot found them, before key_init cleared them (axp2101_reason.h) */
/* the charge current table (REG 62 bits 4:0): 25 mA steps to 200 mA (code 8),
 * then 100 mA steps from 300 (code 9) to 1000 (code 16) */
static int icc_ma(int code) { return code <= 8 ? code * 25 : code <= 16 ? (code - 6) * 100 : 1000; }
static int icc_code(int ma) { if (ma < 25) ma = 25; if (ma <= 200) return ma / 25; int c = ma / 100 + 6; return c > 16 ? 16 : c; }
/* each board's charge current, by the cell its maker fits (mAh; batlog.h's
 * BATLOG_CELL_MAH says the same): the 1.8 200, the pendant 500, the watch
 * 400, the square 1000. The table has no step between 200 and 300 mA.
 * Measured on the square, 2026-10-13 (director `charge <mA>`, at 83-85%):
 * VBAT 4097 mV at 300 mA, 4150 at 500, 4192 at 700 - ~0.25 ohm in the pack's
 * path, so the faster the current the EARLIER the cell meets 4.2 V and the
 * taper starts (~85% at 500 mA, ~78% at 700, ~70% at 1000): past ~0.75 C
 * the minutes saved are few. The source (a hub port) held 4.82 V at 1000 mA,
 * never throttled; the die read 46 C at 300 mA and 51 C at 1000.
 * The square ships at 500 mA, 0.5 C (Strato, 2026-10-13; 700 was tried on
 * the bench): any USB port gives it with the tank running, and a keeper's
 * own smaller cell on that connector stays at or under 1 C down to 500 mAh.
 * A port that still cannot give it sags, and the chip's VINDPM (4.36 V)
 * throttles the charge to what there is - slower, not a fault
 * (docs/board-amoled-2.16.md). */
static int board_charge_ma(void) {
    if (board_is_square()) return 500;     /* 0.5 C */
    if (board_is_175())    return 150;     /* the 1.75 / 1.75-B: a cell of the keeper's choosing - the 1.8's gentle rate */
    if (board_is_round())  return 400;     /* 0.8 C */
    if (board_is_watch())  return 300;     /* 0.75 C */
    return 150;                            /* 0.75 C */
}
/* the end-of-charge and precharge currents (REG 63 / 61, bits 3:0, 25 mA
 * steps to 200): the chip came up at 125 / 125-150 mA on all four boards
 * (read 2026-10-13). An end current of 125 mA against the 1.8's 150 mA
 * charge stops the charge as the taper begins, well short of full; and a
 * precharge of 150 mA is 0.75 C into a 200 mAh cell found deeply drained.
 * Both about a tenth of the cell, the usual rule. The lower end current
 * makes the taper longer: fuller, not faster. */
static int board_trickle_ma(void) {
    if (board_is_square()) return 100;     /* 1000 mAh */
    if (board_is_175())    return 25;      /* unknown cell */
    if (board_is_round())  return 50;      /* 500 mAh */
    if (board_is_watch())  return 50;      /* 400 mAh */
    return 25;                             /* 200 mAh */
}

bool battery_port_init(i2c_master_bus_handle_t bus) {
    if (!bus || i2c_master_probe(bus, AXP2101_ADDR, 50) != ESP_OK) {
        ESP_LOGW("battery", "no AXP2101");
        return false;
    }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                .device_address = AXP2101_ADDR, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) return false;
    /* charge current: the PMIC's default read back as code 0x11, past the
     * 1000 mA (code 16) top of the table - several C for the 200 mAh 302530
     * cell on this board. 0.5C = 100 mA (code 4; codes 0-8 are 25 mA steps)
     * charges it in ~2.5 h and is kind to it; 200 mA (code 8) is the 1C
     * alternative. CV stays at the default 4.2 V (code 3).
     * 2026-10-13, Strato: "all boards charging in nearly 2 hours". A full
     * charge is about 1/C hours of constant current plus the taper, so ~0.75 C
     * - per board, by its cell (board_charge_ma below). */
    const int code = icc_code(board_charge_ma());
    uint8_t icc, iin = 0xFF;
    rd(0x16, &iin);                               /* the VBUS input limit (read only: 0 = 100 mA, 1 = 500, 2 = 900, 3 = 1000, 4 = 1500, 5 = 2000) */
    if (rd(0x62, &icc)) {
        uint8_t wr[2] = { 0x62, (uint8_t)((icc & 0xE0) | code) };
        bool ok = i2c_master_transmit(s_dev, wr, 2, 100) == ESP_OK;
        if (ok) ESP_LOGI("battery", "AXP2101 fuel gauge up; charge current code %d -> %d (%d mA); VBUS input limit code %d", icc & 0x1F, code, icc_ma(code), iin & 0x07);
        else ESP_LOGI("battery", "AXP2101 fuel gauge up; charge current code %d unchanged (write failed)", icc & 0x1F);
    } else ESP_LOGI("battery", "AXP2101 fuel gauge up");
    const uint8_t tc = (uint8_t)(board_trickle_ma() / 25);
    for (int i = 0; i < 2; i++) {                 /* 61 precharge, 63 end of charge */
        uint8_t reg = i ? 0x63 : 0x61, v = 0;
        if (i2c_master_transmit_receive(s_dev, &reg, 1, &v, 1, 100) != ESP_OK) continue;
        uint8_t w[2] = { reg, (uint8_t)((v & 0xF0) | tc) };
        if (i2c_master_transmit(s_dev, w, 2, 100) == ESP_OK)
            ESP_LOGI("battery", "%s current %d -> %d mA", i ? "end-of-charge" : "precharge", (v & 0x0F) * 25, tc * 25);
    }
    return true;
}

static bool rd(uint8_t reg, uint8_t *val) {
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100) == ESP_OK;
}

/* COMMON_CONFIG bit0 = soft power-off: every rail drops within ms, draw falls
 * to the PMIC's quiescent few uA. A PWR-button press powers the board back on. */
bool battery_port_poweroff(void) {
    if (!s_dev) return false;
    uint8_t v;
    if (!rd(REG_COMMON_CFG, &v)) return false;
    uint8_t wr[2] = { REG_COMMON_CFG, (uint8_t)(v | 0x01) };
    return i2c_master_transmit(s_dev, wr, 2, 100) == ESP_OK;
}

/* ---- the PWR key (2026-09-16): REG 41 IRQ enables, REG 49 IRQ status 1
 * (RW1C; bit 3 short press, bit 2 long press), REG 27 IRQLEVEL 5:4 /
 * OFFLEVEL 3:2 / ONLEVEL 1:0. Polled from the tank task; no IRQ line needed. */
static bool wr(uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    return s_dev && i2c_master_transmit(s_dev, b, 2, 100) == ESP_OK;
}
void battery_port_key_init(void) {
    if (!s_dev) return;
    uint8_t v, v27 = 0;
    /* REG 27: IRQLEVEL 5:4 (the long press: 1 / 1.5 / 2 / 2.5 s), OFFLEVEL 3:2
     * (the PMIC's own cut: 4 / 6 / 8 / 10 s), ONLEVEL 1:0 - how long PWRON must
     * be HELD to power the board on from a power-off (128 / 512 ms, 1 / 2 s).
     * ONLEVEL is the one that decides how a wake from the night feels: at the
     * chip's default a tap is shorter than the hold and does nothing, and the
     * keeper presses again (2026-09-16: "three presses"). 128 ms = a tap. */
    static const char *ON[4] = { "128 ms", "512 ms", "1 s", "2 s" }, *IRQ[4] = { "1 s", "1.5 s", "2 s", "2.5 s" };
    { uint8_t r20 = 0, r21 = 0, r48 = 0, r49 = 0, r4a = 0, r10 = 0;   /* what the PMIC saw before this boot: power-on / power-off source, the pending IRQ flags */
      rd(0x20, &r20); rd(0x21, &r21); rd(0x48, &r48); rd(0x49, &r49); rd(0x4A, &r4a); rd(0x10, &r10);
      ESP_LOGI("battery", "PMIC at boot: power-on source %02x, power-off source %02x, common cfg %02x | IRQ status %02x %02x %02x (49: bit3 short press, bit2 long, bit1 key down, bit0 key up)",
               r20, r21, r10, r48, r49, r4a);
      s_boot_irq0 = r48; s_boot_irq2 = r4a; }         /* kept: the reason a charge stopped before this boot (0.4.2) */
    bool ok = rd(0x27, &v27) && wr(0x27, (uint8_t)((v27 & ~0x0F) | 0x0C));   /* OFFLEVEL 10 s (the firmware's 1.5 s long press saves + cuts first), ONLEVEL 128 ms */
    ok = rd(0x41, &v) && wr(0x41, (uint8_t)(v | 0x0E)) && ok;           /* short + long press IRQs on (the defaults, made sure of), and the
                                                                            key-down edge: a press must come after one (key_poll) */
    ok = wr(0x48, 0xFF) && wr(0x49, 0xFF) && wr(0x4A, 0xFF) && ok;       /* the power-on press itself: cleared */
    ESP_LOGI("battery", "PWR key%s: REG 27 was %02x (power-on hold %s, long press %s) -> power-on hold 128 ms; short press = sleep, %s = power-off, 10 s = the PMIC's own cut",
             ok ? "" : " (a register write FAILED)", v27, ON[v27 & 3], IRQ[(v27 >> 4) & 3], IRQ[(v27 >> 4) & 3]);
}
/* 2026-10-23: the watch turns itself off on USB. Its
   PMIC raises a "long press" nobody made every ~2.5-3.5 min there (logged
   2026-10-06: four in 10 min, the key untouched), and a long press is the
   power-off at once. A real press always starts with the key-down edge
   (REG 49 bit 1, on the PMIC's own clock - `keytime` times presses by it):
   a short or long press counts only with one in the last 4 s (the long
   press lands 1.5 s after it). The flags are latched, so a press must also
   read the same twice - a garbled I2C byte does not. */
#define KEY_EDGE_US 4000000
static int64_t s_key_down_us = -KEY_EDGE_US;
/* the presses nobody polled for (0.4 rc): the boot's TIME CHECK runs before the frame loop, up to 30 s, and a press on
   that page was latched in REG 49 - the tank's first poll then read it as a fresh press and slept or powered off.
   Cleared, with the key-down edge forgotten, once the tank is about to come up */
void battery_port_key_clear(void) {
    if (!s_dev) return;
    uint8_t st = 0; rd(0x49, &st);
    wr(0x49, 0x0F);                                                     /* short, long, key down, key up: cleared (write 1 to clear) */
    s_key_down_us = -KEY_EDGE_US;
    if (st & 0x0F) ESP_LOGI("battery", "PWR key: a press made before the tank was up (REG 49 %02x) - cleared, not acted on", st);
}
int battery_port_key_poll(void) {
    if (!s_dev) return 0;
    uint8_t st, again = 0;
    if (!rd(0x49, &st) || !(st & 0x0E)) return 0;
    if ((st & 0x0C) && (!rd(0x49, &again) || (again & 0x0E) != (st & 0x0E))) {
        ESP_LOGW("battery", "PWR key: REG 49 read %02x, then %02x - a misread, ignored", st, again);
        return 0;
    }
    wr(0x49, (uint8_t)(st & 0x0E));                                     /* clear what was taken */
    int64_t now = esp_timer_get_time();
    if (st & 0x02) s_key_down_us = now;                                 /* the key went down */
    if (!(st & 0x0C)) return 0;
    if (now - s_key_down_us >= KEY_EDGE_US) {
        ESP_LOGW("battery", "PWR key: a %s press with no key-down before it (REG 49 %02x) - noise, ignored", (st & 0x04) ? "long" : "short", st);
        return 0;
    }
    return (st & 0x04) ? 2 : 1;
}

/* bench (director `keytime N`, 2026-09-16): how long are the keeper's presses?
 * Polls REG 49 every ~2 ms for N s and logs each press from the PWRON falling
 * edge (bit 1) to the rising edge (bit 0). The short / long flags are swallowed
 * with them, so a press inside the trace never sleeps the tank. The point: a
 * powered-off PMIC only powers on for a hold longer than ONLEVEL (128 ms, the
 * shortest it offers) - a lighter tap does nothing at all, and the keeper
 * presses again. This tells whether the taps land under it. */
void battery_port_key_trace(int seconds) {
    if (!s_dev) return;
    int64_t end = esp_timer_get_time() + (int64_t)seconds * 1000000, down = 0, last_up = 0; int n = 0;
    uint8_t en = 0; rd(0x41, &en); wr(0x41, (uint8_t)(en | 0x0F));       /* the edge IRQs too, for the trace only */
    wr(0x49, 0xFF);
    ESP_LOGI("battery", "key trace: press the PWR key a few times, naturally, in the next %d s (the tank holds still; nothing sleeps)", seconds);
    while (esp_timer_get_time() < end) {
        uint8_t st; int64_t now = esp_timer_get_time();
        if (rd(0x49, &st) && (st & 0x0F)) {
            wr(0x49, (uint8_t)(st & 0x0F));
            if (st & 0x02) down = now;
            if (st & 0x09) {                                              /* the rising edge, or the short-press flag it comes with */
                n++;
                int held = down ? (int)((now - down) / 1000) : -1;
                ESP_LOGI("battery", "key trace: press %d held %d ms%s%s | gap since the last release %lld ms | status %02x", n, held,
                         held >= 0 && held < 128 ? "  <-- UNDER the 128 ms power-on hold: a powered-off PMIC ignores this one" : "",
                         (st & 0x04) ? " (long)" : "", last_up ? (now - last_up) / 1000 : 0LL, st);
                last_up = now; down = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    wr(0x41, en); wr(0x49, 0xFF);
    ESP_LOGI("battery", "key trace over: %d presses", n);
}

/* ---- diagnostics (2026-09-11, the battery-life pass) ----
 * AXP2101 register map (as XPowersLib reads it): 0x80 DCDC1-5 enables
 * (bits 0-4); 0x82-0x86 DCDC1-5 voltage codes; 0x90 LDO enables (bit0 ALDO1,
 * 1 ALDO2, 2 ALDO3, 3 ALDO4, 4 BLDO1, 5 BLDO2, 6 CPUSLDO, 7 DLDO1), 0x91 bit0
 * DLDO2; 0x92-0x9A LDO voltage codes; 0x34/0x35 VBAT ADC (13-bit, mV); 0x18
 * bit1 charger enable; 0x62 charge current code; 0x64 charge voltage code.
 * Every register is printed raw too, so a decode slip can't hide a rail. */
static int dcdc_mv(int n, uint8_t v) {
    switch (n) {
    case 1: return 1500 + (v & 0x1F) * 100;
    case 2: v &= 0x7F; return v <= 70 ? 500 + v * 10 : 1220 + (v - 71) * 20;
    case 3: v &= 0x7F; return v <= 70 ? 500 + v * 10 : v <= 87 ? 1220 + (v - 71) * 20 : 1600 + (v - 88) * 100;
    case 4: v &= 0x7F; return v <= 70 ? 500 + v * 10 : 1220 + (v - 71) * 20;
    default: return 1400 + (v & 0x1F) * 100;
    }
}
static int ldo_mv(uint8_t v)  { return 500 + (v & 0x1F) * 100; }   /* ALDO1-4, BLDO1-2, DLDO1 */
static int sldo_mv(uint8_t v) { return 500 + (v & 0x1F) * 50; }    /* CPUSLDO, DLDO2 */

/* rail switches: 0x80 DCDC1-5 (bits 0-4), 0x90 ALDO1-4 BLDO1-2 CPUSLDO DLDO1
 * (bits 0-7), 0x91 DLDO2 (bit 0) */
typedef struct { const char *name; uint8_t reg, bit; } rail_t;
static const rail_t RAILS[] = {
    { "dcdc2", 0x80, 1 }, { "dcdc3", 0x80, 2 }, { "dcdc4", 0x80, 3 }, { "dcdc5", 0x80, 4 },
    { "aldo1", 0x90, 0 }, { "aldo2", 0x90, 1 }, { "aldo3", 0x90, 2 }, { "aldo4", 0x90, 3 },
    { "bldo1", 0x90, 4 }, { "bldo2", 0x90, 5 }, { "cpusldo", 0x90, 6 }, { "dldo1", 0x90, 7 },
    { "dldo2", 0x91, 0 },
};
static const char *s_pinned[2]; static int s_pinned_n;   /* the rails this board can never lose (battery_port.h) */
void battery_port_pin_rail(const char *name) {
    bool ok = battery_port_set_rail(name, true);
    if (s_pinned_n < (int)(sizeof s_pinned / sizeof s_pinned[0])) s_pinned[s_pinned_n++] = name;
    else { ok = false; ESP_LOGE("battery", "no room to pin rail %s", name); }
    ESP_LOGI("battery", "rail %s pinned on%s", name, ok ? "" : " (FAILED)");
}
bool battery_port_set_rail(const char *name, bool on) {
    if (!s_dev) return false;
    for (int i = 0; !on && i < s_pinned_n; i++) if (!strcasecmp(name, s_pinned[i])) return true;
    for (size_t i = 0; i < sizeof RAILS / sizeof RAILS[0]; i++) {
        if (strcasecmp(name, RAILS[i].name)) continue;
        uint8_t v;
        if (!rd(RAILS[i].reg, &v)) return false;
        uint8_t nv = on ? (v | (1 << RAILS[i].bit)) : (v & ~(1 << RAILS[i].bit));
        if (nv == v) return true;
        uint8_t wr[2] = { RAILS[i].reg, nv };
        return i2c_master_transmit(s_dev, wr, 2, 100) == ESP_OK;
    }
    ESP_LOGW("battery", "no such rail '%s' (dcdc1 and the RTC LDO are never switched)", name);
    return false;
}
/* boot: everything with no consumer off, plus ALDO1 (audio analog, unused) */
void battery_port_trim_rails(void) {
    static const char *off[] = { "dcdc2", "dcdc3", "dcdc4", "aldo1", "aldo2", "aldo3", "aldo4", "bldo1", "bldo2", "cpusldo", "dldo1", "dldo2" };
    int n = 0;
    for (size_t i = 0; i < sizeof off / sizeof off[0]; i++) n += battery_port_set_rail(off[i], false);
    ESP_LOGI("battery", "rails trimmed: %d of %d unused outputs off (dcdc1 = VCC3V3 and the RTC LDO stay)", n, (int)(sizeof off / sizeof off[0]));
}

int battery_port_vbat_mv(void) {
    uint8_t h, l;
    if (!s_dev || !rd(0x34, &h) || !rd(0x35, &l)) return 0;
    return ((h & 0x1F) << 8) | l;                     /* 13-bit, 1 mV/LSB (XPowersLib readRegisterH5L8) */
}

/* the charger, live (director `charge [mA]`): what is set, what the chip
 * says it is doing, and what the source looks like under it. The three ADC
 * channels (VBUS, VSYS, the die's temperature) are switched on for the read
 * and put back: they are off the rest of the time (the battery pass). */
int battery_port_charge_ma(void) { uint8_t v; return s_dev && rd(0x62, &v) ? icc_ma(v & 0x1F) : 0; }
bool battery_port_set_cell(int mah) {
    if (!s_dev) return false;
    bool own = mah == ADV_CELL_STOCK;                  /* the board's own cell: the board's own rates, as at boot */
    int ma = own ? board_charge_ma() : adv_charge_ma(mah), tr = own ? board_trickle_ma() : adv_trickle_ma(mah);
    uint8_t v = 0; bool ok = rd(0x62, &v) && wr(0x62, (uint8_t)((v & 0xE0) | icc_code(ma)));
    for (int i = 0; i < 2; i++) {                      /* 61 precharge, 63 end of charge */
        uint8_t reg = i ? 0x63 : 0x61;
        ok = rd(reg, &v) && wr(reg, (uint8_t)((v & 0xF0) | (tr / 25))) && ok;
    }
    ESP_LOGI("battery", "cell %d mAh%s: charge %d mA, precharge + end of charge %d mA%s", mah, own ? " (the board's own)" : "", icc_ma(icc_code(ma)), tr, ok ? "" : " - a write FAILED");
    return ok;
}
bool battery_port_set_charge_ma(int ma) {
    uint8_t v; if (!s_dev || !rd(0x62, &v)) return false;
    return wr(0x62, (uint8_t)((v & 0xE0) | icc_code(ma)));
}
static int adc14(uint8_t reg) { uint8_t h = 0, l = 0; return rd(reg, &h) && rd(reg + 1, &l) ? ((h & 0x3F) << 8) | l : 0; }
/* why the charger is stopped, said (0.4.2; Hugo's pendant: PLUGGED at 2.9 V, "not charging", and no word why).
   Reads only: nothing is cleared or written, the ADC's TS channel is read as it is found. The decision and every
   register bit: axp2101_reason.h */
static void charge_stop_line(void) {
    axp_chg_regs_t r = { 0 };
    bool ok = rd(REG_STATUS1, &r.st1) && rd(REG_STATUS2, &r.st2) && rd(0x18, &r.chg_en) && rd(0x30, &r.adc_en) &&
              rd(0x48, &r.irq0) && rd(0x4A, &r.irq2) && rd(0x50, &r.ts_ctl) && rd(0x54, &r.vltf) && rd(0x55, &r.vhtf) && rd(0x67, &r.timer);
    if (!ok) { ESP_LOGW("battery", "charge stop: a register read FAILED"); return; }
    r.boot_irq0 = s_boot_irq0; r.boot_irq2 = s_boot_irq2;
    { uint8_t h = 0, l = 0; r.ts_raw = (r.adc_en & 0x02) && rd(0x36, &h) && rd(0x37, &l) ? ((h & 0x3F) << 8) | l : -1; }   /* (36 b7 / b6 are not data) */
    static const char *const SRC[4] = { "OFF", "on with the TS channel", "on while TS is sampled", "always on" };
    static const int UA[4] = { 20, 40, 50, 60 };
    char ts[72];
    if (axp_ts_live(&r)) snprintf(ts, sizeof ts, "TS %d mV (charges between %d and %d mV)", axp_ts_mv(&r), axp_ts_hot_mv(&r), axp_ts_cold_mv(&r));
    else snprintf(ts, sizeof ts, "TS not read (%s)", !(r.adc_en & 0x02) ? "its ADC channel is off" : !(r.ts_ctl & 0x0C) ? "its current source is off" : "no reading yet");
    axp_stop_t why = axp_charge_stop(&r);
    ESP_LOGI("battery", "charge stop: %s%s", why == AXP_STOP_NA ? "not stopped" : axp_charge_stop_text(why),
             why == AXP_STOP_NA && (r.st1 & 0x20) && ((r.irq2 | r.boot_irq2) & 0x02) ? " - but the SAFETY TIMER flag is set (REG 4A b1)" : "");
    ESP_LOGI("battery", "  raw: 18=%02x | IRQ 48=%02x 4A=%02x, at boot 48=%02x 4A=%02x | 50=%02x: TS %s, source %s %d uA | %s | 67=%02x: pre-charge timer %s %d min, charge timer %s %d h",
             r.chg_en, r.irq0, r.irq2, r.boot_irq0, r.boot_irq2, r.ts_ctl, axp_ts_gates(&r) ? "STOPS the charger out of its window" : "does not affect the charger",
             SRC[(r.ts_ctl >> 2) & 3], UA[r.ts_ctl & 3], ts, r.timer, r.timer & 0x04 ? "on" : "OFF", axp_pre_timer_min(r.timer), r.timer & 0x40 ? "on" : "OFF", axp_chg_timer_h(r.timer));
}
/* the charger's enable (REG 18 b1), for the director's proof of the line above: off shows CHARGER DISABLED, on brings
   the charge back. A restart of the ESP does NOT put it back (its reset is the PMIC's own), so `charge on` always
   follows `charge off`. Writing 1 is also one of the chip's ways out of battery safe mode (6.7.4.2): by hand only */
bool battery_port_set_charger(bool on) {
    uint8_t v; if (!s_dev || !rd(0x18, &v)) return false;
    return wr(0x18, (uint8_t)(on ? v | 0x02 : v & ~0x02));
}
void battery_port_charge_report(void) {
    if (!s_dev) { ESP_LOGW("battery", "no AXP2101"); return; }
    uint8_t adc = 0, st1 = 0, st2 = 0, icc = 0, dpm = 0, iin = 0, pre = 0, term = 0, cv = 0, pct = 0;
    rd(0x30, &adc); wr(0x30, adc | 0x1C);
    /* channels found off keep their LAST reading until a new one lands (the
       1.8, 10-13: die 34.0 C and VSYS 4296 mV on every ask through a whole
       charge), and a channel never read holds 0 (the pendant and the watch:
       "385.7 C"). So: a second and a half for fresh ones, then on until
       none is empty. */
    int vbus = 0, vsys = 0, traw = 0;
    vTaskDelay(pdMS_TO_TICKS(1500));
    for (int i = 0; i < 50; i++) {
        vbus = adc14(0x38); vsys = adc14(0x3A); traw = adc14(0x3C);
        if (traw && vbus && vsys) break;
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    wr(0x30, adc);
    rd(REG_STATUS1, &st1); rd(REG_STATUS2, &st2); rd(0x62, &icc); rd(0x15, &dpm); rd(0x16, &iin);
    rd(0x61, &pre); rd(0x63, &term); rd(0x64, &cv); rd(REG_BAT_PERCENT, &pct);
    static const char *ST[8] = { "trickle", "precharge", "CONSTANT CURRENT", "constant voltage (taper)", "done", "not charging", "?", "?" };
    static const int IIN[8] = { 100, 500, 900, 1000, 1500, 2000, 0, 0 };
    ESP_LOGI("battery", "charge: set %d mA (code %d) | %s%s%s%s | VBUS %d mV (input limit %d mA, VINDPM %d mV) | VSYS %d mV | VBAT %d mV %d%% | die %.1f C%s | precharge %d mA, end %d mA, CV code %d",
             icc_ma(icc & 0x1F), icc & 0x1F, ST[st2 & 7], (st2 >> 3) & 1 ? ", THROTTLED: the source sags (VINDPM)" : "",
             st1 & 1 ? ", at the input current limit" : "", (st1 >> 1) & 1 ? ", THERMAL regulation" : "",
             vbus, IIN[iin & 7], 3880 + (dpm & 0x0F) * 80, vsys, battery_port_vbat_mv(), pct, traw ? 22.0 + (7274 - traw) / 20.0 : 0.0, traw ? "" : " (NO READING: ask again)",
             (pre & 0x0F) * 25, (term & 0x0F) * 25, cv & 7);
    charge_stop_line();
}

void battery_port_dump(void) {
    if (!s_dev) { ESP_LOGW("battery", "no AXP2101"); return; }
    uint8_t st1 = 0, st2 = 0, cfg = 0, dc_en = 0, ldo0 = 0, ldo1 = 0, chg_en = 0, icc = 0, cv = 0, pct = 0;
    uint8_t dcv[5] = {0}, ldov[9] = {0};
    bool ok = rd(REG_STATUS1, &st1) && rd(REG_STATUS2, &st2) && rd(REG_COMMON_CFG, &cfg) &&
              rd(0x80, &dc_en) && rd(0x90, &ldo0) && rd(0x91, &ldo1) && rd(0x18, &chg_en) &&
              rd(0x62, &icc) && rd(0x64, &cv) && rd(REG_BAT_PERCENT, &pct);
    for (int i = 0; i < 5 && ok; i++) ok = rd(0x82 + i, &dcv[i]);
    for (int i = 0; i < 9 && ok; i++) ok = rd(0x92 + i, &ldov[i]);
    if (!ok) { ESP_LOGW("battery", "AXP2101 read failed"); return; }
    { uint8_t pwm = 0, adc = 0;                       /* 81: bits 2-5 force DCDC1-4 to PWM (milliamps at a light load); 30: the ADC channels */
      if (rd(0x81, &pwm) && rd(0x30, &adc))
          ESP_LOGI("battery", "  DCDC mode 81=%02x (DCDC1 %s, CCM %s) | ADC enables 30=%02x", pwm, (pwm >> 2) & 1 ? "FORCED PWM" : "auto PWM/PFM",
                   (dc_en >> 6) & 1 ? "ON" : "off", adc); }
    ESP_LOGI("battery", "AXP2101 raw: st1 %02x st2 %02x cfg %02x | dcdc_en %02x v %02x %02x %02x %02x %02x | ldo_en %02x %02x v %02x %02x %02x %02x %02x %02x %02x %02x %02x | chg %02x icc %02x cv %02x",
             st1, st2, cfg, dc_en, dcv[0], dcv[1], dcv[2], dcv[3], dcv[4], ldo0, ldo1,
             ldov[0], ldov[1], ldov[2], ldov[3], ldov[4], ldov[5], ldov[6], ldov[7], ldov[8], chg_en, icc, cv);
    for (int i = 0; i < 5; i++)
        ESP_LOGI("battery", "  DCDC%d  %-3s %4d mV", i + 1, (dc_en >> i) & 1 ? "ON" : "off", dcdc_mv(i + 1, dcv[i]));
    static const char *ldo_name[9] = { "ALDO1", "ALDO2", "ALDO3", "ALDO4", "BLDO1", "BLDO2", "CPUSLDO", "DLDO1", "DLDO2" };
    for (int i = 0; i < 9; i++) {
        bool on = i < 8 ? (ldo0 >> i) & 1 : ldo1 & 1;
        int mv = (i == 6 || i == 8) ? sldo_mv(ldov[i]) : ldo_mv(ldov[i]);
        ESP_LOGI("battery", "  %-7s %-3s %4d mV", ldo_name[i], on ? "ON" : "off", mv);
    }
    int icc_ma = (icc & 0x1F) <= 8 ? (icc & 0x1F) * 25 : 300 + ((icc & 0x1F) - 9) * 100;
    ESP_LOGI("battery", "  charger %s, %d mA, CV code %d | VBUS %s | battery %s, VBAT %d mV, SoC %d%%, %s",
             (chg_en >> 1) & 1 ? "enabled" : "disabled", icc_ma, cv & 0x07,
             (st1 >> 5) & 1 ? "present" : "absent", (st1 >> 3) & 1 ? "present" : "absent",
             battery_port_vbat_mv(), pct,
             ((st2 >> 5) & 0x07) == 0x01 ? "charging" : ((st2 >> 5) & 0x07) == 0x02 ? "discharging" : "standby");
    charge_stop_line();
}

bool battery_port_read(float *frac, bool *charging) {
    if (!s_dev) return false;
    int64_t now = esp_timer_get_time();
    if (s_last_us < 0 || now - s_last_us > 900000) {
        s_last_us = now;
        uint8_t st1, st2, pct;
        s_valid = rd(REG_STATUS1, &st1) && rd(REG_STATUS2, &st2) && rd(REG_BAT_PERCENT, &pct)
                  && (st1 & 0x08) && pct <= 100;          /* battery present, sane SoC */
        if (s_valid) { s_frac = pct / 100.0f; s_charging = ((st2 >> 5) & 0x07) == 0x01; s_st1 = st1; s_st2 = st2; }
        if (s_valid && s_charging) { s_boot_irq0 = 0; s_boot_irq2 = 0; }   /* a charge since: what the boot found is over (the stop reason, 0.4.2) */
    }
    if (!s_valid) return false;
    *frac = s_frac; *charging = s_charging;
    return true;
}

/* the cable, from the last read (battery_port_read first): charge flowing,
   else VBUS good -> done (or the gauge at 100) = full, otherwise resting */
int battery_port_state(void) {
    if (!s_valid) return BAT_ON_BATTERY;
    if (s_charging) return BAT_CHARGING;
    if (!(s_st1 & 0x20)) return BAT_ON_BATTERY;
    return (s_st2 & 0x07) == 0x04 || s_frac >= 0.995f ? BAT_FULL : BAT_PLUGGED;
}
