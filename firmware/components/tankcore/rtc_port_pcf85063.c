/* rtc_port_pcf85063.c — PCF85063 RTC (I2C 0x51) -> system wall clock.
 * At boot: if the RTC holds a plausible time, set the system clock from it.
 * A chip that STOPPED (its OS flag, or a year before 2024: a board with no
 * cell that was unplugged, or a first power-up) leaves the clock UNSET - 0
 * from clock_port_now_unix - and main.c gives it back the way the clockless
 * 1.75C gets its time: the internet at boot (the time away lived from the
 * save), or the save's own stamp (rtc_port_seed; the stretch is owed). It
 * used to seed BUILD TIME here and write it into the chip (2026-08 .. 0.3.8):
 * older than any save, so a battery-less tank unplugged for the night woke
 * with nothing lived - "unplugging 7+ hours doesn't count as a full night's
 * sleep" (abumaia on Discord, 2026-10-24) - and a wrong clock for good, since
 * a board with a chip never asked the internet. rtc_port_store writes the
 * system clock back into the chip once something has set it. The
 * progression save stamps clock_port_now_unix(); nothing else needs RTC. */
#include "rtc_port.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define PCF85063_ADDR 0x51
#define REG_SECONDS   0x04     /* sec, min, hour, day, weekday, month, year (BCD) */
static const char *TAG = "rtc";
static i2c_master_dev_handle_t s_dev;
static bool s_stopped;                                /* the chip answered but had lost its time at this boot (stays set: the boot log tells it) */
static bool s_unread;                                 /* the chip did not answer the read at this boot: its time unknown, never written over */

static int bcd2bin(uint8_t b) { return (b >> 4) * 10 + (b & 0x0f); }
static uint8_t bin2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

enum { RTC_NO_ANSWER = -1, RTC_STOPPED = 0, RTC_OK = 1 };
static int rtc_read(struct tm *out) {
    uint8_t reg = REG_SECONDS, b[7];
    if (i2c_master_transmit_receive(s_dev, &reg, 1, b, 7, 100) != ESP_OK) return RTC_NO_ANSWER;
    memset(out, 0, sizeof *out);
    out->tm_sec = bcd2bin(b[0] & 0x7f); out->tm_min = bcd2bin(b[1] & 0x7f);
    out->tm_hour = bcd2bin(b[2] & 0x3f); out->tm_mday = bcd2bin(b[3] & 0x3f);
    out->tm_wday = b[4] & 0x07; out->tm_mon = bcd2bin(b[5] & 0x1f) - 1;
    out->tm_year = bcd2bin(b[6]) + 100;                 /* 20xx */
    return (b[0] & 0x80) ? RTC_STOPPED : RTC_OK;         /* OS flag set = clock invalid */
}
static bool rtc_write(const struct tm *t) {
    uint8_t w[8] = { REG_SECONDS, bin2bcd(t->tm_sec), bin2bcd(t->tm_min), bin2bcd(t->tm_hour),
                     bin2bcd(t->tm_mday), (uint8_t)t->tm_wday, bin2bcd(t->tm_mon + 1), bin2bcd(t->tm_year - 100) };
    return i2c_master_transmit(s_dev, w, sizeof w, 100) == ESP_OK;
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
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = PCF85063_ADDR, .scl_speed_hz = 400000 };
    /* a board with no RTC chip (the round 1.75C): the system clock stays unset,
       so clock_port_now_unix says 0 = unknown - a build-time seed with nothing
       to keep it would read as a real clock and every flash as time away */
    if (bus && i2c_master_probe(bus, PCF85063_ADDR, 50) != ESP_OK) {
        ESP_LOGW(TAG, "no RTC chip on this board: the ESP32's own clock (%s)", time(NULL) > 1700000000 ? "still running from before this boot" : "unset: seeded once the save is loaded");
        return false;
    }
    if (!bus || i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) { ESP_LOGW(TAG, "no RTC"); return false; }
    struct tm t;
    int r = rtc_read(&t);
    for (int k = 0; k < 2 && r == RTC_NO_ANSWER; k++) { esp_rom_delay_us(5000); r = rtc_read(&t); }   /* a bus hiccup: try again */
    if (r == RTC_NO_ANSWER) {
        /* no answer three times: UNKNOWN, not stopped (0.4 rc round two) - the clock stays unset as for a stopped
           chip, but main.c never seeds the save's older stamp into it (rtc_port_unread): it may hold good time */
        s_unread = true;
        ESP_LOGW(TAG, "RTC chip did not answer the read: the clock is unset at this boot, the chip is left as it is");
    } else if (r == RTC_OK && t.tm_year + 1900 >= 2024) {
        struct timeval tv = { .tv_sec = mktime(&t) };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "system clock set from RTC: %04d-%02d-%02d %02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    } else {
        /* the chip lost its power (no cell, the cable out; or a first power-up):
           the clock stays UNSET - the internet or the save's stamp sets it
           (main.c), then rtc_port_store puts it back into the chip */
        s_stopped = true;
        ESP_LOGW(TAG, "RTC chip STOPPED (it lost power): the clock is unset until the internet or the save gives it back");
    }
    return true;
}
bool rtc_port_stopped(void) { return s_stopped; }
bool rtc_port_unread(void) { return s_unread; }
void rtc_port_store(void) {
    if (!s_dev) return;
    time_t now = time(NULL);
    if (now < 1704067200) return;                       /* 2024-01-01: nothing worth keeping */
    struct tm t; gmtime_r(&now, &t);                    /* the clock is UTC throughout (mktime/gmtime pair: TZ is never set) */
    if (rtc_write(&t)) { ESP_LOGI(TAG, "RTC chip set from the system clock (%04d-%02d-%02d %02d:%02d)", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min); }
    else ESP_LOGW(TAG, "RTC chip: the write failed");
}
