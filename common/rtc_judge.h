/* rtc_judge.h - what the boot makes of the clock chip's registers (0.4.3).
 *
 * The PCF85063's seven time registers (seconds .. year, BCD; bit 7 of the
 * seconds is its stop flag) are read at boot and the system clock is set from
 * them. Until 0.4.3 ONE read decided: a read that came back wrong but
 * acknowledged - a bit turned on the bus, the registers caught mid-carry -
 * set the clock to whatever it said, and the boot lived the difference
 * (a day field one too high is a day of grass, algae and hunger: C1704 on the
 * 1.8, 2026-11-20, "Weird RTC behavior" - not proven to be this, no log from
 * his board); and one read with the stop flag up had the save's older stamp
 * written over a chip that may have held good time.
 *
 * Now two reads must tell the same story, and the fields must be a date:
 *   - OK:      two reads, both running, both a real date from 2024 on, at most
 *              RTCJ_AGREE_S apart. The second one's time is the clock.
 *   - STOPPED: two reads that both say the chip lost its time (the stop flag,
 *              a year before 2024, or fields that are no date). As before:
 *              the clock is unset, the internet or the save gives it back and
 *              that is written into the chip.
 *   - UNKNOWN: no answer, or reads that do not agree after a third. As a chip
 *              that did not answer (0.4 rc round two): the clock is unset at
 *              this boot and the chip is NEVER written over - it may hold
 *              good time.
 * Pure, so the sim drives it (--selftest-rtc-judge); rtc_port_pcf85063.c is
 * the only caller on a board. */
#ifndef RTC_JUDGE_H
#define RTC_JUDGE_H
#include <stdbool.h>
#include <stdint.h>

enum { RTCJ_UNKNOWN = -1, RTCJ_STOPPED = 0, RTCJ_OK = 1 };
#define RTCJ_AGREE_S 2                  /* two reads 5 ms apart: a second may turn between them */
#define RTCJ_YEAR_MIN 2024

typedef bool (*rtcj_read_fn)(void *ctx, uint8_t b[7]);   /* one read of the seven registers; false = the chip did not answer */

static inline bool rtcj_bcd(uint8_t v, int lo, int hi, int *out) {
    if ((v & 0x0f) > 9 || (v >> 4) > 9) return false;
    const int n = (v >> 4) * 10 + (v & 0x0f);
    if (n < lo || n > hi) return false;
    *out = n; return true;
}
/* one read: RTCJ_OK and its time (UTC seconds), or RTCJ_STOPPED */
static inline int rtcj_one(const uint8_t b[7], int64_t *unix_out) {
    static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int s, mi, h, d, mo, y;
    if (b[0] & 0x80) return RTCJ_STOPPED;                 /* the stop flag: the oscillator stopped, the time is not to be trusted */
    if (!rtcj_bcd(b[0] & 0x7f, 0, 59, &s) || !rtcj_bcd(b[1] & 0x7f, 0, 59, &mi) || !rtcj_bcd(b[2] & 0x3f, 0, 23, &h) ||
        !rtcj_bcd(b[3] & 0x3f, 1, 31, &d) || !rtcj_bcd(b[5] & 0x1f, 1, 12, &mo) || !rtcj_bcd(b[6], 0, 99, &y)) return RTCJ_STOPPED;
    y += 2000;
    if (y < RTCJ_YEAR_MIN) return RTCJ_STOPPED;           /* a chip that lost its power starts again at 2000 */
    if (d > mdays[mo - 1] + (mo == 2 && y % 4 == 0)) return RTCJ_STOPPED;   /* (2000..2099: every fourth year leaps) */
    /* days since 1970-01-01 (the civil-days count; the clock is UTC throughout) */
    const int yy = mo <= 2 ? y - 1 : y, era = yy / 400, yoe = yy - era * 400;
    const int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = (int64_t)era * 146097 + doe - 719468;
    *unix_out = days * 86400 + h * 3600 + mi * 60 + s;
    return RTCJ_OK;
}
static inline bool rtcj_same(int va, int64_t ua, int vb, int64_t ub) {
    if (va != vb) return false;
    if (va != RTCJ_OK) return true;
    return ub >= ua ? ub - ua <= RTCJ_AGREE_S : false;    /* (the later read is never the earlier time) */
}
/* the boot's verdict. `rd` is called up to 3 times for each of up to 3 reads (a read that gets no answer is tried again,
   `pause` between any two calls); *reads counts the answered ones. */
static inline int rtcj_boot(rtcj_read_fn rd, void *ctx, void (*pause)(void), int64_t *unix_out, int *reads) {
    int v[3]; int64_t u[3] = { 0, 0, 0 };
    if (reads) *reads = 0;
    for (int k = 0; k < 3; k++) {
        uint8_t b[7]; bool got = false;
        if (k && pause) pause();
        for (int t = 0; t < 3 && !got; t++) { if (t && pause) pause(); got = rd(ctx, b); }
        if (!got) return RTCJ_UNKNOWN;                    /* three calls, no answer */
        v[k] = rtcj_one(b, &u[k]);
        if (reads) *reads = k + 1;
        if (k >= 1 && rtcj_same(v[k - 1], u[k - 1], v[k], u[k])) { if (v[k] == RTCJ_OK) *unix_out = u[k]; return v[k]; }
    }
    if (rtcj_same(v[0], u[0], v[2], u[2])) { if (v[2] == RTCJ_OK) *unix_out = u[2]; return v[2]; }   /* (the odd one was the middle read) */
    return RTCJ_UNKNOWN;                                  /* three reads, no two alike */
}
#endif
