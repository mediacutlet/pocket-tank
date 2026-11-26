/* blackbox_ring.h - THE BLACK BOX's records and the ring they are kept in (0.4.3, the watch going dark).
 *
 * Pure: no ESP-IDF, so the host tests it (sim/selftest_blackbox.c, --selftest-blackbox in make check). blackbox.c
 * hands it a flash (read / write / erase over offsets) and fills the records; nothing here knows what a board is.
 *
 * WHY A RING IN FLASH: the watch's unexplained dark (working-assets/watch-dark/watch-dark-record-2026-11-24.md, kind 4)
 * ends in a hard power-off, which wipes RTC memory (the batlog, the improv trace) and every register. Only flash
 * outlives it. NVS was not used: it holds the keeper's tank, it is 24 KB, and a heartbeat every few minutes would wear
 * it and crowd the save. The ring lives in the unpartitioned gap between otadata and ota_1 (docs/OTA.md: "0xA92000..
 * 0xAA0000 stays a gap"), from 0xA96000: the first 16 KB of the gap was the front of the pre-0.3 storage row, where an
 * old board may still hold an NVS rescue copy; it is never touched. No partition moves, no table changes.
 *
 * THE FLASH RULES (NOR): an erase sets a 4 KB sector to FF; a write can only clear bits. So a record is written once
 * into erased slots; a sector is erased when the head first enters it (its records were the oldest); a slot found not
 * erased at the head (a write cut by a power loss, or bytes from an older layout) is skipped, never written over.
 *
 * A RECORD: 8 bytes of header (magic, type, CRC-16 of the whole record with the CRC field zero, a sequence number that
 * only grows), then its body. A record whose magic, type, size or CRC is wrong is not a record (a torn write, an old
 * layout, a stray byte): it is never shown and its sequence number is never believed.
 *
 * WEAR: the heartbeat ring is 8 sectors of 128 slots (32 B). A heartbeat every 5 min and the events between them is
 * ~300 a day: a sector erase every ~10 h, each sector erased every ~3.4 days - about 10,000 years to 100,000 cycles. */
#ifndef BLACKBOX_RING_H
#define BLACKBOX_RING_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BB_SECTOR        4096u
#define BB_FLASH_BASE    0xA96000u        /* the gap, past the old storage row's rescue copy */
#define BB_FLASH_SIZE    0xA000u          /* .. 0xAA0000 (ota_1): 10 sectors */
#define BB_BOOT_SECTORS  2u               /* region 0: the boot ring, 64 B slots - 128 boots */
#define BB_BEAT_SECTORS  8u               /* region 1: the heartbeat ring, 32 B slots - 1024 heartbeats (~3 days) */
#define BB_BOOT_SLOT     64u
#define BB_BEAT_SLOT     32u

#define BB_MAGIC         0xB7u
enum { BB_T_BOOT = 'B', BB_T_PANIC = 'P', BB_T_BEAT = 'H' };

typedef struct __attribute__((packed)) {
    uint8_t  magic, type;
    uint16_t crc;                         /* CRC-16/CCITT of the whole record, this field taken as 0 */
    uint32_t seq;                         /* grows by one per record in its ring; never 0 or FFFFFFFF in a real record */
} bb_hdr_t;

/* the reasons a boot can name for the run before it (bb_boot_t.crumb_phase): where the tank task last said it was */
enum { BB_PH_NONE = 0, BB_PH_BOOT, BB_PH_FRAME, BB_PH_GRACE, BB_PH_LIGHT_SLEEP, BB_PH_POWEROFF, BB_PH_DEEP_SLEEP,
       BB_PH_RESTART, BB_PH_PANEL_RESTART, BB_PH_COUNT };
/* boot flags */
#define BB_BF_CRUMB      0x01             /* the run before left its breadcrumb in RTC memory (not a power-on) */
#define BB_BF_PANIC      0x02             /* ... and a panic record (a P record follows this boot's B) */
#define BB_BF_PMIC       0x04             /* the PMIC registers were read (pmic_ok says which) */
#define BB_BF_BUS_STUCK  0x08             /* the boot's I2C clear found a line held low */
#define BB_BF_BUS_FREED  0x10             /* ... and its pulses let it go */
#define BB_BF_NO_BOARD   0x20             /* the board was not identified at the first try (the watch's build: retried) */
#define BB_BF_NO_PANEL   0x40             /* this boot restarts at once: the panel did not come up (the watch's build) */

#define BB_PMIC_N 16                      /* the PMIC registers a boot keeps, in this order (bb_pmic_regs) */
typedef struct __attribute__((packed)) {
    bb_hdr_t h;
    uint32_t build;                       /* the first 4 bytes of the app's ELF SHA-256 */
    char     ver[8];                      /* the release, as many characters as fit */
    uint8_t  reset, rom0, rom1, wake;     /* esp_reset_reason(), the ROM's raw reset cause per core, the wake cause */
    uint8_t  pmic[BB_PMIC_N];             /* as read at this boot, before anything here wrote them */
    uint16_t pmic_ok;                     /* bit i: pmic[i] was read */
    uint8_t  flags;                       /* BB_BF_* */
    uint8_t  crumb_phase;                 /* the run before: BB_PH_* */
    uint32_t crumb_up_s;                  /* ... its uptime at its last breadcrumb, s */
    uint32_t crumb_frames;                /* ... frames drawn */
    uint8_t  crumb_i2c;                   /* ... an I2C address in flight when it stopped (0 = none) */
    uint8_t  tries;                       /* board-identification tries this boot (1 = the first answered) */
    uint8_t  panel_restarts;              /* restarts in a row for a panel that did not come up (the watch's build) */
    uint8_t  pad;
    uint16_t crumb_i2c_fails;             /* ... I2C failures counted in that run */
    uint8_t  spare[6];
} bb_boot_t;

typedef struct __attribute__((packed)) {
    bb_hdr_t h;
    uint32_t pc, excvaddr;
    uint8_t  exccause, core, exception, nbt;
    char     task[12];
    uint32_t bt[8];                       /* backtrace PCs; nbt of them are real */
} bb_panic_t;

/* heartbeat events */
enum { BB_EV_TICK = 0, BB_EV_BOOTED, BB_EV_GRACE_IN, BB_EV_GRACE_OUT, BB_EV_POWEROFF, BB_EV_DEEP_SLEEP, BB_EV_LIGHT_OFF,
       BB_EV_LIGHT_ON, BB_EV_LINK_UP, BB_EV_LINK_DOWN, BB_EV_PANEL_HEAL, BB_EV_I2C_BURST, BB_EV_I2C_RECOVER, BB_EV_RESTART,
       BB_EV_COUNT };
/* heartbeat state bits */
#define BB_S_NIGHT    0x01                /* the tank light is off */
#define BB_S_LINK     0x02                /* the tank link is up */
#define BB_S_CHARGING 0x04
#define BB_S_CABLE    0x08                /* VBUS good */
#define BB_S_GRACE    0x10                /* in the sleep's grace (light sleep slices) */
#define BB_S_NO_PANEL 0x20                /* the panel never came up this boot */
#define BB_S_LOCKED   0x40                /* the touch lock */
typedef struct __attribute__((packed)) {
    bb_hdr_t h;
    uint32_t up_s;                        /* uptime, s */
    uint32_t wall;                        /* unix time, 0 = not known */
    uint8_t  event, state;                /* BB_EV_*, BB_S_* */
    uint8_t  pct, heap_kb;                /* battery %, 255 = no reading; internal heap free, KB (255 = 255 or more) */
    uint16_t mv;                          /* VBAT */
    uint16_t i2c_fails;                   /* since boot (saturates) */
    uint8_t  i2c_recov, i2c_last;         /* bus recoveries since boot; the address of the last failure */
    uint8_t  fps, heals;                  /* frames a second (rounded); panel rail heals since boot */
    uint16_t boot_seq;                    /* the low half of this boot's B record seq: ties the beat to its boot */
    uint16_t spare;
} bb_beat_t;

_Static_assert(sizeof(bb_hdr_t) == 8, "bb_hdr_t");
_Static_assert(sizeof(bb_boot_t) == BB_BOOT_SLOT, "bb_boot_t is one boot slot");
_Static_assert(sizeof(bb_panic_t) == BB_BOOT_SLOT, "bb_panic_t is one boot slot");
_Static_assert(sizeof(bb_beat_t) == BB_BEAT_SLOT, "bb_beat_t is one heartbeat slot");

/* the flash, as blackbox.c (a registered partition) or the host test (a RAM image) gives it: offsets from the box's base */
typedef struct {
    bool (*read)(void *ctx, uint32_t off, void *dst, uint32_t n);
    bool (*write)(void *ctx, uint32_t off, const void *src, uint32_t n);
    bool (*erase)(void *ctx, uint32_t off);                  /* one 4 KB sector at off */
    void *ctx;
} bb_flash_t;

typedef struct {
    const bb_flash_t *f;
    uint32_t base, sectors, slot;         /* the region: offset, 4 KB sectors, slot size */
    uint32_t head;                        /* the next slot to write (an index) */
    uint32_t seq;                         /* the last sequence number written or found */
    uint32_t count;                       /* valid records found at open + written since */
} bb_ring_t;

uint16_t bb_crc16(const void *p, size_t n);
void     bb_seal(void *rec, uint32_t size, uint8_t type, uint32_t seq);   /* header filled, CRC set */
bool     bb_valid(const void *rec, uint32_t size);                       /* magic, a known type, CRC */
/* find the head: the slot after the highest valid seq (slot 0 of an empty ring). Reads every slot's bytes */
void     bb_ring_open(bb_ring_t *r, const bb_flash_t *f, uint32_t base, uint32_t sectors, uint32_t slot);
/* write one record (size == the slot), sealed with the next seq. False: the flash refused (the ring is unchanged
   but for a sector erased or a slot skipped) */
bool     bb_ring_append(bb_ring_t *r, void *rec, uint8_t type);
/* the valid records, oldest first: fn(rec, ctx) for each; the newest `last` only (0 = all) */
void     bb_ring_walk(const bb_ring_t *r, uint32_t last, void (*fn)(const void *rec, void *ctx), void *ctx);

/* the boot ring's PMIC registers, in bb_boot_t.pmic's order */
extern const uint8_t bb_pmic_regs[BB_PMIC_N];
const char *bb_phase_name(int ph);
const char *bb_event_name(int ev);
/* the AXP2101's power-on (REG 20) / power-off (REG 21) source bits, said (XPowersLib's isXxx() accessors):
   into buf, "none" for 0 */
const char *bb_pmic_on_src(uint8_t v, char *buf, size_t n);
const char *bb_pmic_off_src(uint8_t v, char *buf, size_t n);
#endif
