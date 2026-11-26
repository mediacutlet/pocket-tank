/* blackbox_ring.c - THE BLACK BOX's ring in flash (blackbox_ring.h has the rules). Pure C: the firmware and the host
 * test build the same file. */
#include "blackbox_ring.h"
#include <stdio.h>
#include <string.h>

/* the order bb_boot_t.pmic keeps (the AXP2101; names from XPowersLib's AXP2101Constants.h): power-on / power-off
   status, the common config, the gauge+watchdog control, the watchdog config, PWROFF_EN, the DCDC under/over-voltage
   power-off enables, the VSYS power-off level, the IRQ/OFF/ON levels, IRQ status 1..3, the DCDC enables, the LDO
   enables 1..2, the IRQ enable 2 (the PWR key's) */
const uint8_t bb_pmic_regs[BB_PMIC_N] = { 0x20, 0x21, 0x10, 0x18, 0x19, 0x22, 0x23, 0x24, 0x27, 0x48, 0x49, 0x4A, 0x80, 0x90, 0x91, 0x41 };

uint16_t bb_crc16(const void *p, size_t n) {           /* CRC-16/CCITT-FALSE */
    const uint8_t *b = p; uint16_t c = 0xFFFF;
    while (n--) { c ^= (uint16_t)(*b++ << 8); for (int i = 0; i < 8; i++) c = (uint16_t)(c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1); }
    return c;
}
static uint16_t rec_crc(const void *rec, uint32_t size) {
    uint8_t tmp[BB_BOOT_SLOT];
    if (size > sizeof tmp) return 0;
    memcpy(tmp, rec, size); tmp[2] = tmp[3] = 0;
    return bb_crc16(tmp, size);
}
void bb_seal(void *rec, uint32_t size, uint8_t type, uint32_t seq) {
    bb_hdr_t *h = rec;
    h->magic = BB_MAGIC; h->type = type; h->seq = seq; h->crc = 0;
    h->crc = rec_crc(rec, size);
}
static bool type_fits(uint8_t type, uint32_t size) {
    return ((type == BB_T_BOOT || type == BB_T_PANIC) && size == BB_BOOT_SLOT) || (type == BB_T_BEAT && size == BB_BEAT_SLOT);
}
bool bb_valid(const void *rec, uint32_t size) {
    const bb_hdr_t *h = rec;
#ifdef BB_TEST_NO_CRC
    extern int bb_test_no_crc;                          /* the host test's control: a ring that believes any header */
    if (bb_test_no_crc) return h->magic == BB_MAGIC && type_fits(h->type, size) && h->seq != 0 && h->seq != 0xFFFFFFFFu;
#endif
    return h->magic == BB_MAGIC && type_fits(h->type, size) && h->seq != 0 && h->seq != 0xFFFFFFFFu && h->crc == rec_crc(rec, size);
}
static uint32_t nslots(const bb_ring_t *r) { return r->sectors * (BB_SECTOR / r->slot); }
static uint32_t slot_off(const bb_ring_t *r, uint32_t i) { return r->base + i * r->slot; }
static bool erased(const uint8_t *b, uint32_t n) { while (n--) if (*b++ != 0xFF) return false; return true; }

void bb_ring_open(bb_ring_t *r, const bb_flash_t *f, uint32_t base, uint32_t sectors, uint32_t slot) {
    memset(r, 0, sizeof *r);
    r->f = f; r->base = base; r->sectors = sectors; r->slot = slot;
    uint8_t buf[BB_BOOT_SLOT];
    uint32_t best = 0; bool any = false;
    for (uint32_t i = 0; i < nslots(r); i++) {
        if (!f->read(f->ctx, slot_off(r, i), buf, slot) || !bb_valid(buf, slot)) continue;
        uint32_t s = ((bb_hdr_t *)buf)->seq;
        r->count++;
        if (!any || s > r->seq) { r->seq = s; best = i; any = true; }
    }
    r->head = any ? (best + 1) % nslots(r) : 0;
}

bool bb_ring_append(bb_ring_t *r, void *rec, uint8_t type) {
    const uint32_t per = BB_SECTOR / r->slot, n = nslots(r);
    uint8_t buf[BB_BOOT_SLOT];
    for (uint32_t tries = 0; tries < n; tries++) {
        const uint32_t i = r->head;
        r->head = (i + 1) % n;
        if (i % per == 0) {                             /* a new sector: its records were the oldest */
            uint32_t gone = 0;                          /* (the count says what is kept: the erased sector's records leave it) */
            for (uint32_t k = 0; k < per; k++) if (r->f->read(r->f->ctx, slot_off(r, i + k), buf, r->slot) && bb_valid(buf, r->slot)) gone++;
            if (!r->f->erase(r->f->ctx, r->base + (i / per) * BB_SECTOR)) return false;
            r->count = r->count > gone ? r->count - gone : 0;
        } else if (!r->f->read(r->f->ctx, slot_off(r, i), buf, r->slot) || !erased(buf, r->slot)) continue;   /* torn or foreign: skipped */
        bb_seal(rec, r->slot, type, r->seq + 1);
        if (!r->f->write(r->f->ctx, slot_off(r, i), rec, r->slot)) return false;
        if (!r->f->read(r->f->ctx, slot_off(r, i), buf, r->slot) || !bb_valid(buf, r->slot)) continue;      /* did not take: the next slot */
        r->seq++; r->count++;
        return true;
    }
    return false;
}

void bb_ring_walk(const bb_ring_t *r, uint32_t last, void (*fn)(const void *rec, void *ctx), void *ctx) {
    const uint32_t n = nslots(r);
    uint8_t buf[BB_BOOT_SLOT];
    uint32_t total = 0;
    for (uint32_t k = 0; k < n; k++)
        if (r->f->read(r->f->ctx, slot_off(r, (r->head + k) % n), buf, r->slot) && bb_valid(buf, r->slot)) total++;
    uint32_t skip = last && total > last ? total - last : 0;
    for (uint32_t k = 0; k < n; k++) {                  /* from the head on: the oldest first */
        if (!r->f->read(r->f->ctx, slot_off(r, (r->head + k) % n), buf, r->slot) || !bb_valid(buf, r->slot)) continue;
        if (skip) { skip--; continue; }
        fn(buf, ctx);
    }
}

const char *bb_phase_name(int ph) {
    static const char *const N[BB_PH_COUNT] = { "none", "booting", "drawing frames", "in the sleep's grace (awake)", "in LIGHT SLEEP (the grace)",
                                                "powering off", "going into deep sleep", "restarting", "restarting (no panel)" };
    return ph >= 0 && ph < BB_PH_COUNT ? N[ph] : "?";
}
const char *bb_event_name(int ev) {
    static const char *const N[BB_EV_COUNT] = { "tick", "booted", "grace in", "grace out (woken)", "power-off", "deep sleep", "light off",
                                                "light on", "link up", "link down", "PANEL RAIL HEALED", "I2C FAILURES", "I2C BUS RECOVERY", "restart" };
    return ev >= 0 && ev < BB_EV_COUNT ? N[ev] : "?";
}
static const char *bits(uint8_t v, const char *const name[8], char *buf, size_t n) {
    size_t o = 0; buf[0] = 0;
    for (int b = 0; b < 8; b++) if (v & (1 << b)) o += (size_t)snprintf(buf + o, o < n ? n - o : 0, "%s%s", o ? ", " : "", name[b]);
    if (!v) snprintf(buf, n, "none");
    return buf;
}
/* XPowersLib XPowersAXP2101.hpp, isPoweronAlwaysHighSource() .. isBatOverTemperature-style accessors (REG 20 / 21) */
const char *bb_pmic_on_src(uint8_t v, char *buf, size_t n) {
    static const char *const N[8] = { "PWR key", "IRQ pin", "USB plugged in", "battery charged", "battery inserted", "EN mode", "bit6", "bit7" };
    return bits(v, N, buf, n);
}
const char *bb_pmic_off_src(uint8_t v, char *buf, size_t n) {
    static const char *const N[8] = { "PWR key held", "software (the firmware)", "PWR key low (EN mode)", "VSYS UNDER-VOLTAGE", "VBUS OVER-VOLTAGE",
                                      "DCDC UNDER-VOLTAGE", "DCDC OVER-VOLTAGE", "DIE OVER-TEMPERATURE" };
    return bits(v, N, buf, n);
}
