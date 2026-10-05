/* battery_port_axp2101.c — battery fraction + charge state from the AXP2101
 * PMIC's fuel gauge (I2C 0x34): STATUS1 bit3 = battery present, bit5 = VBUS
 * good (the cable), STATUS2 bits[7:5] == 1 = charging, bits[2:0] == 4 = the
 * charge is done, 0xA4 = state of charge in percent. Reads are cached for
 * ~1 s (the pill answers a plug-in within a second); any I2C error or
 * absent battery hides the meter. */
#include "battery_port.h"
#include "display_port.h"   /* board_is_sq216 */
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <strings.h>
#include <string.h>

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
     * The 2.16 (2026-10-04): the tank awake draws ~75 mA, so 100 mA left ~25
     * for the cell: 1% per 5 minutes, "full in 9 h" on the glass. Its cell is
     * Waveshare's 1000 mAh, and their own example for the board charges at
     * 400 mA (code 10 - past 200 the steps are 100 mA): 0.4C, kind to it, and
     * with the tank's draw still inside a plain USB port's 500 mA. */
    const int code = board_is_sq216() ? 10 : 4, ma = code <= 8 ? code * 25 : 300 + (code - 9) * 100;
    uint8_t icc;
    if (rd(0x62, &icc)) {
        uint8_t wr[2] = { 0x62, (uint8_t)((icc & 0xE0) | code) };
        bool ok = i2c_master_transmit(s_dev, wr, 2, 100) == ESP_OK;
        if (ok) ESP_LOGI("battery", "AXP2101 fuel gauge up; charge current code %d -> %d (%d mA)", icc & 0x1F, code, ma);
        else ESP_LOGI("battery", "AXP2101 fuel gauge up; charge current code %d unchanged (write failed)", icc & 0x1F);
    } else ESP_LOGI("battery", "AXP2101 fuel gauge up");
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
      ESP_LOGI("battery", "PMIC at boot: power-on source %02x, power-off source %02x, common cfg %02x | IRQ status %02x %02x %02x (49: bit3 short press, bit2 long, bit1 release, bit0 press)",
               r20, r21, r10, r48, r49, r4a); }
    bool ok = rd(0x27, &v27) && wr(0x27, (uint8_t)((v27 & ~0x0F) | 0x0C));   /* OFFLEVEL 10 s (the firmware's 1.5 s long press saves + cuts first), ONLEVEL 128 ms */
    ok = rd(0x41, &v) && wr(0x41, (uint8_t)(v | 0x0C)) && ok;           /* short + long press IRQs on (the defaults, made sure of) */
    ok = wr(0x48, 0xFF) && wr(0x49, 0xFF) && wr(0x4A, 0xFF) && ok;       /* the power-on press itself: cleared */
    ESP_LOGI("battery", "PWR key%s: REG 27 was %02x (power-on hold %s, long press %s) -> power-on hold 128 ms; short press = sleep, %s = power-off, 10 s = the PMIC's own cut",
             ok ? "" : " (a register write FAILED)", v27, ON[v27 & 3], IRQ[(v27 >> 4) & 3], IRQ[(v27 >> 4) & 3]);
}
int battery_port_key_poll(void) {
    if (!s_dev) return 0;
    uint8_t st;
    if (!rd(0x49, &st) || !(st & 0x0C)) return 0;
    wr(0x49, (uint8_t)(st & 0x0C));                                     /* clear what was taken */
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
