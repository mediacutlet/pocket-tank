/* blackbox.h - THE BLACK BOX (0.4.3, the watch going dark; every pocket board has it, the big screens not yet): a record in flash that outlives a
 * hard power-off, and the RTC watchdog.
 *
 *  - At every boot, a BOOT record: the reset reason (ESP-IDF's and the ROM's raw cause per core), the wake cause, the
 *    PMIC's power-on / power-off sources, IRQ status and protection settings as this boot found them, the build, and
 *    what the run before was doing when it stopped (its breadcrumb in RTC memory, when a reset kept that memory).
 *  - A PANIC record after a crash: the PC, the exception, the task and 8 backtrace addresses (the panic handler is
 *    wrapped; the record waits in RTC memory and is written at the next boot).
 *  - HEARTBEATS while running: every 5 min and at each change (the sleep's grace in and out, power-off, deep sleep,
 *    light off / on, link up / down, a panel-rail heal, a burst of I2C failures, a bus recovery): uptime, wall time,
 *    the state, the battery, the heap, the I2C failures and the last chip that failed, the frame rate.
 *  - THE RTC WATCHDOG: on the slow RTC clock, which runs in light sleep, so it sees what the task watchdog cannot - a
 *    light sleep that never wakes, a chip whose fast clocks stopped. Fed with the task watchdog (main.c wdt_feed);
 *    BB_RWDT_S unfed resets the chip's digital system (RTC memory kept, so the breadcrumb says where it was), and
 *    10 s more resets the RTC domain too. Armed with the tank task; off before deep sleep and power-off.
 *
 * Director `blackbox` prints it (tools/director_help.txt). blackbox_ring.h has the flash layout. */
#ifndef BLACKBOX_H
#define BLACKBOX_H
#include <stdbool.h>
#include <stdint.h>
#include "blackbox_ring.h"

#define BB_RWDT_S      90                 /* past the task watchdog's own two stages (20 s panic, 60 s its hardware reset) */
#define BB_BEAT_S      300                /* a heartbeat every 5 min */
#define BB_PANEL_RESTARTS 2               /* the watch's build: restarts in a row for a panel that did not come up, at most */

typedef struct {
    bool night, link, charging, cable, grace, no_panel, locked;
    int  pct, mv;                         /* pct < 0: no reading */
    float fps;
    uint32_t heals;                       /* panel-rail heals since boot */
} bb_state_t;

#ifndef TANK_P4
void blackbox_boot_begin(void);           /* first thing in app_main: the reset reason, the run before's breadcrumb and panic */
void blackbox_boot_pmic(const uint8_t regs[BB_PMIC_N], uint16_t ok);   /* battery_port_init: the PMIC as found */
void blackbox_boot_board(int tries);      /* display_port_init: how many probes it took to know the board (0: never) */
bool blackbox_panel_restart_take(void);   /* the watch's build, no panel at boot: true = restart now (counted; false once BB_PANEL_RESTARTS ran in a row) */
bool blackbox_late_restart_take(void);    /* ... the board answering later in the run: true = restart now (once a power-on) */
void blackbox_boot_commit(bool restarting);   /* the boot record (and a panic record) written; restarting: BB_BF_NO_PANEL */
void blackbox_crumb_phase(int ph);        /* BB_PH_*: where the tank task is (RTC memory) */
void blackbox_frame(void);                /* one frame drawn (RTC memory) */
void blackbox_feed(int64_t now_us);       /* with the task watchdog's feed: the RTC watchdog fed, the breadcrumb's uptime */
void blackbox_rwdt_arm(void);             /* the tank task is up */
void blackbox_rwdt_disarm(void);          /* before deep sleep / a power-off */
void blackbox_tick(int64_t now_us, const bb_state_t *s);   /* ~every 10 s, and each grace slice: the 5-min heartbeat, the edges */
void blackbox_event(int ev, const bb_state_t *s);          /* a heartbeat now, with this event */
void blackbox_print(bool all);            /* director `blackbox [all]` */
void blackbox_test(const char *what);     /* director `blackbox test rwdt|panic` (dev tests: the chip restarts) */
#else   /* the big screens (TANK_P4): no black box and no RTC watchdog in 0.4.3 - they have never run there. The source
           is left out (CMakeLists.txt) and every call is an empty inline, so the callers stay as they are */
static inline void blackbox_boot_begin(void) {}
static inline void blackbox_boot_pmic(const uint8_t regs[BB_PMIC_N], uint16_t ok) { (void)regs; (void)ok; }
static inline void blackbox_boot_board(int tries) { (void)tries; }
static inline bool blackbox_panel_restart_take(void) { return false; }
static inline bool blackbox_late_restart_take(void) { return false; }
static inline void blackbox_boot_commit(bool restarting) { (void)restarting; }
static inline void blackbox_crumb_phase(int ph) { (void)ph; }
static inline void blackbox_frame(void) {}
static inline void blackbox_feed(int64_t now_us) { (void)now_us; }
static inline void blackbox_rwdt_arm(void) {}
static inline void blackbox_rwdt_disarm(void) {}
static inline void blackbox_tick(int64_t now_us, const bb_state_t *s) { (void)now_us; (void)s; }
static inline void blackbox_event(int ev, const bb_state_t *s) { (void)ev; (void)s; }
static inline void blackbox_print(bool all) { (void)all; }
static inline void blackbox_test(const char *what) { (void)what; }
#endif
#endif
