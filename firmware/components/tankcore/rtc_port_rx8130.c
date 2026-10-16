/* rtc_port_rx8130.c - the Tab5's Seiko RX8130CE RTC (I2C 0x32) -> system wall
 * clock. The same job rtc_port_pcf85063.c does for the four Waveshare boards,
 * with this chip's own register map:
 *
 *   0x10 SEC  BCD         0x14 DAY   BCD (1-31)
 *   0x11 MIN  BCD         0x15 MONTH BCD (1-12)
 *   0x12 HOUR BCD, bit 6 = 12/24 h (0 = 24 h)
 *   0x13 WEEK 0-6         0x16 YEAR  BCD (00-99 = 20xx)
 *   0x17 FLAG  bit 7 = VBL: the supply dipped, so this time cannot be trusted
 *   0x1F EXT   bit 6 = RESET (a supplied-in reset state), bit 7 = TEST
 *
 * At boot: if the chip holds a plausible time and its VBL flag is clear, set the
 * system clock from it; if it is unset (year < 2024, e.g. first power-up or
 * after a supply dip), seed it from the firmware build time and write that back,
 * so the ravenous-boot rule has a clock from day one. The progression save
 * stamps clock_port_now_unix(); nothing else needs the RTC.
 *
 * The Tab5 has no backup cell on this chip, so a real power cut does clear it:
 * what survives is a reset, a deep sleep and a reflash - which is exactly what
 * the rules need (an absence nobody can vouch for reads as no time at all). */
#include "rtc_port.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define RX8130_ADDR 0x32
#define REG_SECONDS 0x10     /* sec, min, hour, week, day, month, year (BCD) */
#define REG_FLAG    0x17     /* bit 7 = VBL (voltage low) */
#define REG_EXT     0x1F     /* bit 6 = RESET, bit 7 = TEST */
static const char *TAG = "rtc";
static i2c_master_dev_handle_t s_dev;

static int bcd2bin(uint8_t b) { return (b >> 4) * 10 + (b & 0x0f); }
static uint8_t bin2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
/* a nibble-wise check: a chip fresh from the factory (or one whose supply has
 * dipped) answers with bytes that are not BCD at all - 0xA7 reads as year 107
 * through bcd2bin and would look like a plausible-looking 2107 in the log */
static bool bcd_ok(uint8_t b, int max) {
    if ((b & 0x0f) > 9 || ((b >> 4) & 0x0f) > 9) return false;
    int v = bcd2bin(b);
    return v >= 0 && v <= max;
}

static bool reg_write(uint8_t reg, uint8_t val) {
    uint8_t w[2] = { reg, val };
    return i2c_master_transmit(s_dev, w, 2, 100) == ESP_OK;
}
static bool reg_read(uint8_t reg, uint8_t *val) {
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100) == ESP_OK;
}

static bool rtc_read(struct tm *out) {
    uint8_t reg = REG_SECONDS, b[7];
    if (i2c_master_transmit_receive(s_dev, &reg, 1, b, 7, 100) != ESP_OK) return false;
    if (!bcd_ok(b[0] & 0x7f, 59) || !bcd_ok(b[1] & 0x7f, 59) || !bcd_ok(b[2] & 0x3f, 23) ||
        !bcd_ok(b[4] & 0x3f, 31) || !bcd_ok(b[5] & 0x1f, 12) || !bcd_ok(b[6], 99)) return false;
    memset(out, 0, sizeof *out);
    out->tm_sec = bcd2bin(b[0] & 0x7f); out->tm_min = bcd2bin(b[1] & 0x7f);
    out->tm_hour = bcd2bin(b[2] & 0x3f); out->tm_wday = b[3] & 0x07;
    out->tm_mday = bcd2bin(b[4] & 0x3f); out->tm_mon = bcd2bin(b[5] & 0x1f) - 1;
    out->tm_year = bcd2bin(b[6]) + 100;                 /* 20xx */
    return true;
}
static bool rtc_write(const struct tm *t) {
    /* hour bit 6 stays 0: 24-hour mode, which is also what the read masks for */
    uint8_t w[8] = { REG_SECONDS, bin2bcd(t->tm_sec), bin2bcd(t->tm_min), (uint8_t)(bin2bcd(t->tm_hour) & 0x3f),
                     (uint8_t)t->tm_wday, bin2bcd(t->tm_mday), bin2bcd(t->tm_mon + 1), bin2bcd(t->tm_year - 100) };
    return i2c_master_transmit(s_dev, w, sizeof w, 100) == ESP_OK;
}
/* the chip's own "this time is not trustworthy" flag */
static bool rtc_valid(void) {
    uint8_t f = 0;
    if (!reg_read(REG_FLAG, &f)) return false;
    if (f & 0x80) { reg_write(REG_FLAG, f & (uint8_t)~0x80); return false; }   /* clear VBL, and say no once */
    return true;
}

static time_t build_unix(void) {                        /* "Aug 21 2026" "10:15:00" */
    static const char mon[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char ms[4] = {0}; int d = 1, y = 2026, hh = 0, mm = 0, ss = 0;
    sscanf(__DATE__, "%3s %d %d", ms, &d, &y); sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
    struct tm t; memset(&t, 0, sizeof t);
    const char *m = strstr(mon, ms);
    t.tm_mon = m ? (int)((m - mon) / 3) : 0; t.tm_mday = d; t.tm_year = y - 1900;
    t.tm_hour = hh; t.tm_min = mm; t.tm_sec = ss;
    return mktime(&t);
}
void rtc_port_seed(int64_t not_before) {
    time_t at = build_unix();
    if (not_before > (int64_t)at) at = (time_t)not_before;
    struct timeval tv = { .tv_sec = at }; settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "no RTC chip: the system clock starts at %s (it runs through deep sleep, not through a power cut)",
             not_before > 0 && (time_t)not_before == at ? "the save's stamp" : "the build time");
}
bool rtc_port_init(i2c_master_bus_handle_t bus) {
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = RX8130_ADDR, .scl_speed_hz = 400000 };
    if (!bus || i2c_master_probe(bus, RX8130_ADDR, 50) != ESP_OK) {
        ESP_LOGW(TAG, "no RX8130CE at 0x%02x: the ESP32's own clock (%s)", RX8130_ADDR,
                 time(NULL) > 1700000000 ? "still running from before this boot" : "unset: seeded once the save is loaded");
        return false;
    }
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) { ESP_LOGW(TAG, "no RTC device"); return false; }
    /* out of the supplied-in reset state (0x1F bit 6) and off test mode (bit 7),
     * so the clock actually runs */
    uint8_t ext = 0;
    if (reg_read(REG_EXT, &ext) && (ext & 0xc0)) {
        reg_write(REG_EXT, 0x00);
        ESP_LOGW(TAG, "RX8130CE was in its reset/test state (0x1F = 0x%02x): cleared", ext);
    }
    struct tm t;
    if (rtc_valid() && rtc_read(&t) && t.tm_year + 1900 >= 2024 && t.tm_year + 1900 <= 2100) {
        struct timeval tv = { .tv_sec = mktime(&t) };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "system clock set from the RX8130CE: %04d-%02d-%02d %02d:%02d:%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        time_t secs = build_unix();
        struct timeval tv = { .tv_sec = secs }; settimeofday(&tv, NULL);
        struct tm bt; localtime_r(&secs, &bt);
        if (!rtc_write(&bt)) ESP_LOGW(TAG, "the RX8130CE would not take the time: it will be re-seeded every boot");
        ESP_LOGW(TAG, "RX8130CE was unset (or its supply dipped); seeded from build time %s %s", __DATE__, __TIME__);
    }
    return true;
}
