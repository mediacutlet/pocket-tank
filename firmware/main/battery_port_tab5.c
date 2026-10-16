/* battery_port_tab5.c - the Tab5's battery: a 2S Li-ion pack watched by the
 * board's INA226 (I2C 0x41, verified by its TI ids 0x5449/0x2260). The Waveshare
 * boards use an AXP2101 fuel gauge; this board has no PMIC, so everything here
 * is the INA226 plus arithmetic.
 *
 * WHAT IT READS (measured on the bench unit, 2026-10-16):
 *   bus voltage 8173 mV  - a 2S pack, 4.09 V/cell, so ~90% charged
 *   shunt       -1392.5 uV = -278 mA at the assumed 5 mOhm = 2.3 W
 * The INA226 is a shunt monitor, so its current and power registers need the
 * board's shunt resistance. 5 mOhm is *assumed* (the schematic is not in this
 * repo) and the register value agrees with it (current reg -278 mA matches the
 * shunt arithmetic), but the sign convention - which way round IN+/IN- sit -
 * cannot be read off the chip. `CHARGING_SIGN` below is the hypothesis that a
 * *negative* current means charge flowing in, which is what the -278 mA reading
 * showed with the cable in and the tank drawing 2.3 W; the tank's own `batlog`
 * history is what settles it (unplug for a minute, plug back, then read
 * `batlog`: the segment with no cable must read ON BATTERY). If it comes out
 * the other way, flip that one constant.
 *
 * The percentage is a 2S Li-ion open-circuit table (8.40 V = 100% ... 6.00 V =
 * 0%) interpolated linearly, with the pack's internal resistance compensated
 * for while discharging so a load does not read as a flat battery:
 * V_open = V_measured + I_discharge * PACK_ESR. */
#include "battery_port.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include <string.h>

static const char *TAG = "battery";

#define INA226_ADDR    0x41
#define REG_CONFIG     0x00
#define REG_SHUNT_V    0x01     /* 2.5 uV/LSB, signed */
#define REG_BUS_V      0x02     /* 1.25 mV/LSB */
#define REG_POWER      0x03
#define REG_CURRENT    0x04
#define REG_CAL        0x05
#define REG_MFG_ID     0xFE     /* 0x5449 = "TI" */
#define REG_DIE_ID     0xFF     /* 0x2260 */

#define SHUNT_MOHM     5        /* ASSUMED (see the header) */
#define CAL_VALUE      (int)(0.00512f / (0.001f * (SHUNT_MOHM / 1000.0f)))
#define CONFIG_VALUE   0x4927   /* AVG=16, 1.1 ms conversions, shunt+bus continuous */

/* a negative current means charge flowing in (see the header) */
#define CHARGING_SIGN  (-1)
#define CHARGE_MA_MIN  30       /* below this the current is read as "nothing much" */
#define FULL_MV        8300     /* 2S at 4.15 V/cell: the charger's target is close */
#define PACK_ESR_MOHM  150      /* the pack's own resistance, for the load sag */

/* 2S Li-ion, open circuit. Linear in between; a 2S pack spends most of its life
 * between 7.8 and 7.2 V, which is where the steps are finest. */
static const struct { int mv, pct; } k_curve[] = {
    { 8400, 100 }, { 8250, 95 }, { 8100, 88 }, { 8000, 80 }, { 7900, 70 },
    { 7800, 60 },  { 7700, 50 }, { 7600, 42 }, { 7500, 35 }, { 7400, 28 },
    { 7300, 22 },  { 7200, 16 },  { 7000, 9 },  { 6800, 4 },  { 6000, 0 },
};
#define CURVE_N (sizeof k_curve / sizeof k_curve[0])

static i2c_master_dev_handle_t s_dev;
static int64_t s_next_us;
static int s_bus_mv;            /* everything below is cached ~1 s, like the AXP2101 port */
static int s_cur_ma;            /* signed: see CHARGING_SIGN */
static int s_pct = -1;

static bool rd16(uint8_t reg, uint16_t *out) {
    uint8_t b[2];
    if (i2c_master_transmit_receive(s_dev, &reg, 1, b, 2, 100) != ESP_OK) return false;
    *out = (uint16_t)((b[0] << 8) | b[1]);
    return true;
}
static bool wr16(uint8_t reg, uint16_t val) {
    uint8_t b[3] = { reg, (uint8_t)(val >> 8), (uint8_t)val };
    return i2c_master_transmit(s_dev, b, 3, 100) == ESP_OK;
}

static int curve_pct(int oc_mv) {
    if (oc_mv >= k_curve[0].mv) return 100;
    if (oc_mv <= k_curve[CURVE_N - 1].mv) return 0;
    for (size_t i = 0; i + 1 < CURVE_N; i++) {
        int hi = k_curve[i].mv, lo = k_curve[i + 1].mv;
        if (oc_mv <= hi && oc_mv >= lo) {
            int span = hi - lo;
            int f = span ? (oc_mv - lo) * 100 / span : 100;
            return k_curve[i + 1].pct + (k_curve[i].pct - k_curve[i + 1].pct) * f / 100;
        }
    }
    return 0;
}

static void refresh(void) {
    int64_t now = esp_timer_get_time();
    if (now < s_next_us) return;
    s_next_us = now + 1000000;
    uint16_t bus = 0, sh = 0;
    if (!rd16(REG_BUS_V, &bus) || !rd16(REG_SHUNT_V, &sh)) { s_pct = -1; return; }
    s_bus_mv = (int)(bus * 1.25f);                          /* 1.25 mV/LSB */
    /* uV / mOhm = mA, and the sign convention turns it into charge-in-positive */
    s_cur_ma = CHARGING_SIGN * (int)((float)(int16_t)sh * 2.5f / SHUNT_MOHM);
    int oc_mv = s_bus_mv + (s_cur_ma < 0 ? -s_cur_ma : 0) * PACK_ESR_MOHM / 1000;   /* discharging: add the sag back */
    s_pct = curve_pct(oc_mv);
}

bool battery_port_init(i2c_master_bus_handle_t bus) {
    if (!bus || i2c_master_probe(bus, INA226_ADDR, 50) != ESP_OK) {
        ESP_LOGW(TAG, "no INA226 at 0x%02x: no gauge", INA226_ADDR);
        return false;
    }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = INA226_ADDR, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) return false;
    uint16_t mfg = 0, die = 0;
    if (!rd16(REG_MFG_ID, &mfg) || !rd16(REG_DIE_ID, &die) || mfg != 0x5449 || die != 0x2260) {
        ESP_LOGW(TAG, "INA226 ids 0x%04x/0x%04x (want 0x5449/0x2260)", mfg, die);
        s_dev = NULL; return false;
    }
    wr16(REG_CONFIG, CONFIG_VALUE);
    wr16(REG_CAL, (uint16_t)CAL_VALUE);
    refresh();
    ESP_LOGI(TAG, "INA226 at 0x%02x: 2S pack %d mV, %+d mA (%s), %d%% (shunt assumed %d mOhm; sign convention %s)",
             INA226_ADDR, s_bus_mv, s_cur_ma, s_cur_ma >= CHARGE_MA_MIN ? "charging" : "discharging or idle",
             s_pct, SHUNT_MOHM, CHARGING_SIGN < 0 ? "negative = charge in" : "positive = charge in");
    return true;
}

bool battery_port_read(float *frac, bool *charging) {
    if (!s_dev) return false;
    refresh();
    if (s_pct < 0) return false;
    if (frac) *frac = s_pct / 100.0f;
    if (charging) *charging = s_cur_ma >= CHARGE_MA_MIN;
    return true;
}
int battery_port_state(void) {
    if (!s_dev) return BAT_ON_BATTERY;
    refresh();
    if (s_cur_ma >= CHARGE_MA_MIN) return (s_bus_mv >= FULL_MV) ? BAT_FULL : BAT_CHARGING;
    if (s_cur_ma <= -CHARGE_MA_MIN) return BAT_ON_BATTERY;
    return BAT_PLUGGED;                 /* the cable is in, the charger is resting */
}
int battery_port_vbat_mv(void) { if (!s_dev) return 0; refresh(); return s_bus_mv; }
bool battery_port_poweroff(void) { return false; }            /* no PMIC to cut */

/* the side button is PWR/RST/BOOT in hardware; the tank gets no key events */
void battery_port_key_init(void) {}
int  battery_port_key_poll(void) { return 0; }
void battery_port_key_trace(int seconds) { (void)seconds; }

/* no PMIC rails: the Tab5's rails are fixed. The audio and display ports on this
 * board do not call these (board-tab5.md), so they only have to be honest. */
bool battery_port_set_rail(const char *name, bool on) { (void)name; (void)on; return false; }
void battery_port_trim_rails(void) {}
void battery_port_pin_rail(const char *name) { (void)name; }

/* the charger is the board's own IC; the tank can only report what the monitor
 * sees, not set a current (`charge <mA>` on the director says so) */
int  battery_port_charge_ma(void) { if (!s_dev) return 0; refresh(); return s_cur_ma > 0 ? s_cur_ma : 0; }
bool battery_port_set_charge_ma(int ma) { (void)ma; return false; }
void battery_port_charge_report(void) {
    if (!s_dev) { ESP_LOGI(TAG, "no gauge"); return; }
    refresh();
    ESP_LOGI(TAG, "pack %d mV, %+d mA (%s) - the charger is the board's own IC, not settable from here",
             s_bus_mv, s_cur_ma, s_cur_ma >= CHARGE_MA_MIN ? "charging" : s_cur_ma <= -CHARGE_MA_MIN ? "on battery" : "idle");
}

void battery_port_dump(void) {
    if (!s_dev) { ESP_LOGI(TAG, "no INA226"); return; }
    uint16_t cfg = 0, bus = 0, sh = 0, cur = 0, pwr = 0, cal = 0;
    rd16(REG_CONFIG, &cfg); rd16(REG_BUS_V, &bus); rd16(REG_SHUNT_V, &sh);
    rd16(REG_CURRENT, &cur); rd16(REG_POWER, &pwr); rd16(REG_CAL, &cal);
    float shunt_uv = (float)(int16_t)sh * 2.5f;
    ESP_LOGI(TAG, "INA226 cfg 0x%04x cal %u | bus %d mV (%d%%) | shunt %.1f uV = %+d mA at %d mOhm | "
                  "current reg %+d mA | power reg %u x25 mW | state %d (assumed shunt: check the schematic)",
             cfg, cal, (int)(bus * 1.25f), s_pct, shunt_uv, (int)(shunt_uv / SHUNT_MOHM), SHUNT_MOHM,
             (int)(int16_t)cur, pwr, battery_port_state());
}
