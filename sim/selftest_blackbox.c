/* selftest_blackbox.c - --selftest-blackbox (0.4.3, the watch going dark): THE BLACK BOX's ring in flash
 * (firmware/main/blackbox_ring.c, built here whole) and the I2C bus guard's rule (firmware/main/i2c_guard_rule.h).
 *
 * The flash is a RAM image that behaves as NOR does: an erase sets a 4 KB sector to FF, a write can only clear bits -
 * and this one also FAILS THE TEST if a write lands on bytes that are not erased (the ring must never write over a
 * record, a torn write or a stranger's bytes). Power cuts are a write that stops half way.
 *
 * The control: PT_BB_NOCRC=1 makes the ring believe any header with the right magic (no CRC): the torn-write case
 * must then fail (the Makefile runs it and wants a failure). */
#define BB_TEST_NO_CRC 1
int bb_test_no_crc;
#include "../firmware/main/blackbox_ring.c"
#include "../firmware/main/i2c_guard_rule.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t g_fl[BB_FLASH_SIZE];
static int g_over, g_fail_write, g_cut_at = -1, g_writes, g_erases;
static bool f_read(void *c, uint32_t off, void *d, uint32_t n) { (void)c; if (off + n > sizeof g_fl) return false; memcpy(d, g_fl + off, n); return true; }
static bool f_write(void *c, uint32_t off, const void *s, uint32_t n) {
    (void)c;
    if (off + n > sizeof g_fl || g_fail_write) return false;
    for (uint32_t i = 0; i < n; i++) if (g_fl[off + i] != 0xFF) { g_over++; break; }
    uint32_t m = n;
    if (g_cut_at >= 0 && g_writes == g_cut_at) m = n / 2;        /* the power cut: half the bytes land */
    for (uint32_t i = 0; i < m; i++) g_fl[off + i] &= ((const uint8_t *)s)[i];
    g_writes++;
    return m == n;
}
static bool f_erase(void *c, uint32_t off) { (void)c; if (off % BB_SECTOR || off + BB_SECTOR > sizeof g_fl) return false; memset(g_fl + off, 0xFF, BB_SECTOR); g_erases++; return true; }
static const bb_flash_t FL = { f_read, f_write, f_erase, NULL };

typedef struct { uint32_t n, first, last; int gaps, types_bad; uint32_t want_type; } walk_t;
static void walk_cb(const void *rec, void *ctx) {
    walk_t *w = ctx; const bb_hdr_t *h = rec;
    if (w->n && h->seq != w->last + 1) w->gaps++;
    if (!w->n) w->first = h->seq;
    w->last = h->seq; w->n++;
    if (w->want_type && h->type != w->want_type) w->types_bad++;
}
static walk_t walk(const bb_ring_t *r, uint32_t last, uint8_t type) { walk_t w = { 0 }; w.want_type = type; bb_ring_walk(r, last, walk_cb, &w); return w; }

static bb_beat_t beat(uint32_t up) { bb_beat_t b; memset(&b, 0, sizeof b); b.up_s = up; b.event = BB_EV_TICK; b.pct = 80; b.mv = 4000; return b; }
#define BEAT_BASE (BB_BOOT_SECTORS * BB_SECTOR)
#define BEATS (BB_BEAT_SECTORS * BB_SECTOR / BB_BEAT_SLOT)

int selftest_blackbox(void) {
    int fail = 0;
    bb_test_no_crc = getenv("PT_BB_NOCRC") && atoi(getenv("PT_BB_NOCRC"));
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: blackbox: " __VA_ARGS__); printf("\n"); fail++; } } while (0)
    bb_ring_t r;

    /* 1. an empty ring, then one heartbeat, then reopened (a reboot): found, and the next continues it */
    memset(g_fl, 0xFF, sizeof g_fl);
    bb_ring_open(&r, &FL, BEAT_BASE, BB_BEAT_SECTORS, BB_BEAT_SLOT);
    CHECK(r.count == 0 && r.head == 0, "an erased ring has records (%u) or a head %u", (unsigned)r.count, (unsigned)r.head);
    bb_beat_t b = beat(1);
    CHECK(bb_ring_append(&r, &b, BB_T_BEAT), "the first append failed");
    bb_ring_open(&r, &FL, BEAT_BASE, BB_BEAT_SECTORS, BB_BEAT_SLOT);
    CHECK(r.count == 1 && r.seq == 1 && r.head == 1, "after a reboot: %u records, seq %u, head %u (want 1, 1, 1)", (unsigned)r.count, (unsigned)r.seq, (unsigned)r.head);

    /* 2. far past full (with a reboot every 97 records): the newest kept, oldest first, no gap, never written over */
    g_over = 0;
    const uint32_t total = BEATS * 3 + 77;
    for (uint32_t i = 2; i <= total; i++) {
        b = beat(i);
        if (!bb_ring_append(&r, &b, BB_T_BEAT)) { CHECK(0, "append %u failed", (unsigned)i); break; }
        if (i % 97 == 0) bb_ring_open(&r, &FL, BEAT_BASE, BB_BEAT_SECTORS, BB_BEAT_SLOT);
    }
    walk_t w = walk(&r, 0, BB_T_BEAT);
    CHECK(w.last == total && w.gaps == 0, "after %u: newest %u (want %u), %d gaps", (unsigned)total, (unsigned)w.last, (unsigned)total, w.gaps);
    CHECK(w.n >= BEATS - BB_SECTOR / BB_BEAT_SLOT && w.n <= BEATS, "kept %u of a %u-slot ring (want at least one sector short of full)", (unsigned)w.n, (unsigned)BEATS);
    CHECK(g_over == 0, "%d writes landed on bytes that were not erased", g_over);
    CHECK(r.count == w.n, "the ring's count says %u kept, the walk finds %u", (unsigned)r.count, (unsigned)w.n);
    walk_t w5 = walk(&r, 5, BB_T_BEAT);
    CHECK(w5.n == 5 && w5.last == total && w5.first == total - 4, "the last 5: %u records, %u..%u", (unsigned)w5.n, (unsigned)w5.first, (unsigned)w5.last);
    /* the boot ring next door is untouched by all that */
    { int dirty = 0; for (uint32_t i = 0; i < BEAT_BASE; i++) if (g_fl[i] != 0xFF) dirty++; CHECK(dirty == 0, "the heartbeat ring wrote %d bytes into the boot ring", dirty); }

    /* 3. a power cut in the middle of a write: the torn record is no record, the next boot continues from the last
          good one, and the torn slot is skipped, not written over */
    memset(g_fl, 0xFF, sizeof g_fl);
    bb_ring_open(&r, &FL, BEAT_BASE, BB_BEAT_SECTORS, BB_BEAT_SLOT);
    for (uint32_t i = 1; i <= 10; i++) { b = beat(i); bb_ring_append(&r, &b, BB_T_BEAT); }
    g_writes = 0; g_cut_at = 0; g_over = 0;
    b = beat(11); bb_ring_append(&r, &b, BB_T_BEAT);           /* cut: half of it lands (then the retry writes it whole in the next slot) */
    g_cut_at = -1;
    /* the board loses power right after the cut write, before the retry: put the image back to "torn only" */
    { const uint32_t torn = BEAT_BASE + 10 * BB_BEAT_SLOT; memset(g_fl + torn + BB_BEAT_SLOT, 0xFF, BB_BEAT_SLOT); }
    bb_ring_open(&r, &FL, BEAT_BASE, BB_BEAT_SECTORS, BB_BEAT_SLOT);
    CHECK(r.count == 10 && r.seq == 10, "after the cut: %u records, seq %u (want 10, 10: the torn one is no record)", (unsigned)r.count, (unsigned)r.seq);
    b = beat(12);
    CHECK(bb_ring_append(&r, &b, BB_T_BEAT), "the append after the cut failed");
    CHECK(g_over == 0, "a write landed on the torn record (%d)", g_over);
    w = walk(&r, 0, BB_T_BEAT);
    CHECK(w.n == 11 && w.last == 11 && w.gaps == 0, "after the cut and one more: %u records up to seq %u, %d gaps (want 11, 11, 0)", (unsigned)w.n, (unsigned)w.last, w.gaps);

    /* 4. a stranger's bytes (the old storage row's data, any layout): no records found, and the first write erases */
    srand(43);
    for (uint32_t i = 0; i < sizeof g_fl; i++) g_fl[i] = (uint8_t)rand();
    for (uint32_t i = BEAT_BASE; i < sizeof g_fl; i += BB_BEAT_SLOT) { g_fl[i] = BB_MAGIC; g_fl[i + 1] = BB_T_BEAT; }   /* even with the right magic and type */
    bb_ring_open(&r, &FL, BEAT_BASE, BB_BEAT_SECTORS, BB_BEAT_SLOT);
    CHECK(r.count == 0, "random bytes read as %u records", (unsigned)r.count);
    g_over = 0; g_erases = 0;
    b = beat(1);
    CHECK(bb_ring_append(&r, &b, BB_T_BEAT) && g_erases == 1 && g_over == 0, "the first write over a stranger's sector: erases %d, written over %d", g_erases, g_over);
    w = walk(&r, 0, BB_T_BEAT);
    CHECK(w.n == 1, "after the first write over random bytes: %u records (want 1)", (unsigned)w.n);

    /* 5. a flash that refuses: false, and nothing believed */
    memset(g_fl, 0xFF, sizeof g_fl);
    bb_ring_open(&r, &FL, BEAT_BASE, BB_BEAT_SECTORS, BB_BEAT_SLOT);
    g_fail_write = 1; b = beat(1);
    CHECK(!bb_ring_append(&r, &b, BB_T_BEAT) && r.seq == 0, "a refused write was taken");
    g_fail_write = 0;

    /* 6. the boot ring: boot and panic records, 64 B, side by side, in order */
    memset(g_fl, 0xFF, sizeof g_fl);
    bb_ring_t br; bb_ring_open(&br, &FL, 0, BB_BOOT_SECTORS, BB_BOOT_SLOT);
    for (int i = 0; i < 200; i++) {
        bb_boot_t bt; memset(&bt, 0, sizeof bt); bt.reset = 1; bt.pmic[1] = 0x02;
        bb_ring_append(&br, &bt, BB_T_BOOT);
        if (i % 7 == 0) { bb_panic_t p; memset(&p, 0, sizeof p); p.pc = 0x42001234; p.nbt = 3; bb_ring_append(&br, &p, BB_T_PANIC); }
    }
    w = walk(&br, 0, 0);
    CHECK(w.gaps == 0 && w.last == br.seq && w.n >= 64, "the boot ring: %u records to seq %u (seq %u), %d gaps", (unsigned)w.n, (unsigned)w.last, (unsigned)br.seq, w.gaps);
    bb_ring_open(&br, &FL, 0, BB_BOOT_SECTORS, BB_BOOT_SLOT);
    CHECK(br.seq == w.last, "the boot ring reopened at seq %u (want %u)", (unsigned)br.seq, (unsigned)w.last);
    { int dirty = 0; for (uint32_t i = BEAT_BASE; i < sizeof g_fl; i++) if (g_fl[i] != 0xFF) dirty++; CHECK(dirty == 0, "the boot ring wrote %d bytes into the heartbeat ring", dirty); }

    /* 7. a bit flipped in a stored record: no longer a record */
    { bb_ring_t r2; bb_ring_open(&r2, &FL, 0, BB_BOOT_SECTORS, BB_BOOT_SLOT);
      const uint32_t n0 = r2.count;
      for (uint32_t i = 0; i < BB_BOOT_SECTORS * BB_SECTOR; i += BB_BOOT_SLOT) if (g_fl[i] == BB_MAGIC) { g_fl[i + 40] ^= 0x04; break; }
      bb_ring_open(&r2, &FL, 0, BB_BOOT_SECTORS, BB_BOOT_SLOT);
      CHECK(r2.count == n0 - 1, "a flipped bit: %u records, want %u", (unsigned)r2.count, (unsigned)(n0 - 1)); }

    /* 8. the I2C bus guard's rule */
    CHECK(!i2c_guard_should_recover(false, false, 1, 1LL << 40), "one failure, lines high: a recovery");
    CHECK(i2c_guard_should_recover(true, false, 1, 1LL << 40), "SDA held low: no recovery");
    CHECK(i2c_guard_should_recover(false, true, 1, 1LL << 40), "SCL held low: no recovery");
    CHECK(i2c_guard_should_recover(false, false, I2C_GUARD_RUN, 1LL << 40), "%d failures in a row: no recovery", I2C_GUARD_RUN);
    CHECK(!i2c_guard_should_recover(false, false, I2C_GUARD_RUN - 1, 1LL << 40), "%d failures in a row: a recovery", I2C_GUARD_RUN - 1);
    CHECK(!i2c_guard_should_recover(true, true, 99, I2C_GUARD_GAP_US - 1), "a recovery again within the gap");

    /* 9. the PMIC's sources, said */
    { char t[96];
      CHECK(strstr(bb_pmic_off_src(0x02, t, sizeof t), "software"), "off-src 02: %s", t);
      CHECK(strstr(bb_pmic_off_src(0x01, t, sizeof t), "PWR key held"), "off-src 01: %s", t);
      CHECK(strstr(bb_pmic_off_src(0x28, t, sizeof t), "VSYS UNDER-VOLTAGE") && strstr(t, "DCDC UNDER-VOLTAGE"), "off-src 28: %s", t);
      CHECK(!strcmp(bb_pmic_off_src(0x00, t, sizeof t), "none"), "off-src 00: %s", t);
      CHECK(strstr(bb_pmic_on_src(0x04, t, sizeof t), "USB plugged in"), "on-src 04: %s", t);
      CHECK(!strcmp(bb_phase_name(BB_PH_LIGHT_SLEEP), "in LIGHT SLEEP (the grace)") && !strcmp(bb_phase_name(99), "?"), "phase names"); }

    printf("blackbox: %s (%d failures)%s\n", fail ? "FAILED" : "all checks pass", fail, bb_test_no_crc ? " [CONTROL: no CRC]" : "");
    return fail ? 1 : 0;
}
