#define _DEFAULT_SOURCE   /* timegm under -std=c11 (glibc) */
/* selftest_rtc_judge.c - --selftest-rtc-judge (0.4.3): what the boot makes of the clock chip (common/rtc_judge.h).
 *   A  a date decodes to the second timegm gives, across 2024..2099 (leap days, month ends, midnight)
 *   B  no date, no clock: the stop flag, a year before 2024, a BCD digit over 9, the 31st of a 30-day month, hour 24
 *   C  the boot: two reads alike are the verdict in two reads; ONE wrong read among three is outvoted wherever it falls
 *      (a day field one too high - the day of grass after two hours off - never sets the clock); a second turning between
 *      the reads agrees; no answer three times, or three reads all apart, is UNKNOWN (the chip is never written over)
 *   D  the control: the old single read takes the wrong read as the time */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "rtc_judge.h"

static int g_fail;
#define RCHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); g_fail++; } } while (0)
static uint8_t bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static void regs(uint8_t b[7], int y, int mo, int d, int h, int mi, int s) {
    b[0] = bcd(s); b[1] = bcd(mi); b[2] = bcd(h); b[3] = bcd(d); b[4] = 0; b[5] = bcd(mo); b[6] = bcd(y - 2000);
}
typedef struct { uint8_t r[9][7]; bool ans[9]; int n, at; } feed_t;
static bool feed_rd(void *ctx, uint8_t b[7]) {
    feed_t *f = ctx;
    if (f->at >= f->n) return false;
    const int k = f->at++;
    if (!f->ans[k]) return false;
    memcpy(b, f->r[k], 7); return true;
}
static void feed_add(feed_t *f, const uint8_t b[7], bool ans) { memcpy(f->r[f->n], b, 7); f->ans[f->n++] = ans; }

int selftest_rtc_judge(void) {
    uint8_t b[7], w[7]; int64_t u = 0; int n = 0;
    /* A */
    for (int y = 2024; y <= 2099; y++)
        for (int mo = 1; mo <= 12; mo++) {
            static const int md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
            const int last = md[mo - 1] + (mo == 2 && y % 4 == 0);
            const int ds[3] = { 1, 15, last };
            for (int q = 0; q < 3; q++) {
                struct tm t; memset(&t, 0, sizeof t);
                t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = ds[q]; t.tm_hour = q == 2 ? 23 : q * 7; t.tm_min = q == 2 ? 59 : 13; t.tm_sec = q == 2 ? 59 : 0;
                regs(b, y, mo, ds[q], t.tm_hour, t.tm_min, t.tm_sec);
                const int v = rtcj_one(b, &u);
                if (v != RTCJ_OK || u != (int64_t)timegm(&t)) { RCHECK(0, "%04d-%02d-%02d: verdict %d, %lld vs timegm %lld", y, mo, ds[q], v, (long long)u, (long long)timegm(&t)); }
                n++;
            }
        }
    printf("selftest-rtc-judge: A %d dates 2024..2099 decode to timegm's second\n", n);
    /* B */
    regs(b, 2026, 11, 20, 9, 30, 0); b[0] |= 0x80; RCHECK(rtcj_one(b, &u) == RTCJ_STOPPED, "the stop flag is STOPPED");
    regs(b, 2000, 1, 1, 0, 0, 5);  RCHECK(rtcj_one(b, &u) == RTCJ_STOPPED, "a chip back at 2000 is STOPPED");
    regs(b, 2023, 12, 31, 23, 59, 59); RCHECK(rtcj_one(b, &u) == RTCJ_STOPPED, "2023 is STOPPED");
    regs(b, 2026, 11, 20, 9, 30, 0); b[1] = 0x6a; RCHECK(rtcj_one(b, &u) == RTCJ_STOPPED, "a BCD digit over 9 is no date");
    regs(b, 2026, 11, 31, 9, 30, 0); RCHECK(rtcj_one(b, &u) == RTCJ_STOPPED, "November 31 is no date");
    regs(b, 2026, 2, 29, 9, 30, 0);  RCHECK(rtcj_one(b, &u) == RTCJ_STOPPED, "February 29 2026 is no date");
    regs(b, 2028, 2, 29, 9, 30, 0);  RCHECK(rtcj_one(b, &u) == RTCJ_OK, "February 29 2028 is a date");
    regs(b, 2026, 11, 20, 9, 30, 0); b[2] = 0x24; RCHECK(rtcj_one(b, &u) == RTCJ_STOPPED, "hour 24 is no date");
    regs(b, 2026, 11, 20, 9, 30, 0); b[3] = 0x00; RCHECK(rtcj_one(b, &u) == RTCJ_STOPPED, "day 0 is no date");
    regs(b, 2026, 11, 20, 9, 30, 0); b[5] = 0x13; RCHECK(rtcj_one(b, &u) == RTCJ_STOPPED, "month 13 is no date");
    /* C */
    int64_t good = 0, got = 0; int reads = 0; feed_t f;
    regs(b, 2026, 11, 20, 9, 30, 10); rtcj_one(b, &good);
    regs(w, 2026, 11, 21, 9, 30, 10);                        /* the day field one too high */
    memset(&f, 0, sizeof f); feed_add(&f, b, true); feed_add(&f, b, true);
    RCHECK(rtcj_boot(feed_rd, &f, NULL, &got, &reads) == RTCJ_OK && got == good && reads == 2 && f.at == 2, "two good reads: OK in two (%d reads)", reads);
    for (int bad = 0; bad < 3; bad++) {
        memset(&f, 0, sizeof f); got = 0;
        for (int k = 0; k < 3; k++) feed_add(&f, k == bad ? w : b, true);
        const int v = rtcj_boot(feed_rd, &f, NULL, &got, &reads);
        RCHECK(v == RTCJ_OK && got == good, "a wrong read at %d of three is outvoted: verdict %d, %lld vs %lld", bad, v, (long long)got, (long long)good);
    }
    { uint8_t b2[7]; regs(b2, 2026, 11, 20, 9, 30, 11);
      memset(&f, 0, sizeof f); feed_add(&f, b, true); feed_add(&f, b2, true);
      RCHECK(rtcj_boot(feed_rd, &f, NULL, &got, &reads) == RTCJ_OK && got == good + 1, "a second turning between the reads agrees, the later one kept"); }
    { uint8_t st[7]; memcpy(st, b, 7); st[0] |= 0x80;
      memset(&f, 0, sizeof f); feed_add(&f, st, true); feed_add(&f, st, true);
      RCHECK(rtcj_boot(feed_rd, &f, NULL, &got, &reads) == RTCJ_STOPPED && reads == 2, "the stop flag twice is STOPPED");
      memset(&f, 0, sizeof f); feed_add(&f, st, true); feed_add(&f, b, true); feed_add(&f, b, true); got = 0;
      RCHECK(rtcj_boot(feed_rd, &f, NULL, &got, &reads) == RTCJ_OK && got == good, "ONE read with the stop flag up, then two good: the chip's time stands, nothing written over it");
      uint8_t z[7]; regs(z, 2000, 1, 1, 0, 0, 3); z[0] |= 0x80;
      memset(&f, 0, sizeof f); feed_add(&f, z, true); feed_add(&f, z, true);
      RCHECK(rtcj_boot(feed_rd, &f, NULL, &got, &reads) == RTCJ_STOPPED, "a chip that lost its power is STOPPED, as before"); }
    memset(&f, 0, sizeof f); feed_add(&f, b, false); feed_add(&f, b, false); feed_add(&f, b, false);
    RCHECK(rtcj_boot(feed_rd, &f, NULL, &got, &reads) == RTCJ_UNKNOWN && reads == 0, "no answer three times is UNKNOWN");
    memset(&f, 0, sizeof f); feed_add(&f, b, false); feed_add(&f, b, true); feed_add(&f, b, false); feed_add(&f, b, false); feed_add(&f, b, true);
    RCHECK(rtcj_boot(feed_rd, &f, NULL, &got, &reads) == RTCJ_OK && got == good, "a bus hiccup before each read is tried again");
    { uint8_t x[7], y2[7]; regs(x, 2026, 11, 22, 9, 30, 10); regs(y2, 2027, 1, 20, 9, 30, 10);
      memset(&f, 0, sizeof f); feed_add(&f, b, true); feed_add(&f, x, true); feed_add(&f, y2, true); got = -1;
      RCHECK(rtcj_boot(feed_rd, &f, NULL, &got, &reads) == RTCJ_UNKNOWN && got == -1 && reads == 3, "three reads all apart is UNKNOWN, no time given"); }
    /* D */
    { int64_t one = 0; RCHECK(rtcj_one(w, &one) == RTCJ_OK && one == good + 86400, "the control: a single read takes the wrong day as the time (a day lived)"); }
    printf("selftest-rtc-judge: %s\n", g_fail ? "FAILED" : "OK - two reads alike or the chip is left alone; one wrong read in three never sets the clock");
    return g_fail ? 1 : 0;
}
