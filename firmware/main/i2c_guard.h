/* i2c_guard.h - THE I2C BUS GUARD (0.4.3, the watch going dark): every transaction on the board's I2C bus passes
 * through here, on every pocket board (the big screens not yet). It
 *   - counts each failed transaction BY THE CHIP'S ADDRESS (the watch logged 102 failed reads one night and no one
 *     could say which chip: working-assets/watch-dark/watch-dark-record-2026-11-24.md, hypothesis 8);
 *   - serializes the bus between tasks with one lock, which a read-modify-write of a register can hold across both
 *     halves (i2c_guard_lock: the PMIC's rail switches, battery_port_axp2101.c);
 *   - after a failure, looks at the two lines: SDA or SCL held low with the bus idle is a stuck bus, and so are
 *     I2C_GUARD_RUN failures in a row; then ESP-IDF's own bus reset (i2c_master_bus_reset: the controller reset plus
 *     its SCL pulses) runs, the PMIC is asked again, and the recovery is logged and counted (the black box keeps it);
 *   - at boot, before the bus is made, clocks a slave stuck mid-byte free: up to nine SCL pulses while SDA is low,
 *     then a STOP - the I2C specification's recovery (UM10204 3.1.16) and ESP-IDF's own software clear
 *     (esp_driver_i2c/i2c_master.c s_i2c_master_clear_bus). A flash's USB reset can cut the chip off mid-transaction
 *     and leave a slave holding SDA (the watch, 11-08 and 11-13: the bus dead after a flash).
 *
 * How: the linker's --wrap (ESP-IDF build-system docs, "wrapping functions") puts __wrap_i2c_master_* in front of the
 * driver's own for every caller - ours, esp_lcd's touch drivers - so no call site changes. */
#ifndef I2C_GUARD_H
#define I2C_GUARD_H
#include <stdbool.h>
#include <stdint.h>
#include "i2c_guard_rule.h"

#ifndef TANK_P4
void     i2c_guard_boot_clear(int sda, int scl);   /* before i2c_new_master_bus: the lock made, the lines freed */
bool     i2c_guard_boot_stuck(void);               /* the boot found SDA or SCL held low */
bool     i2c_guard_boot_freed(void);               /* ... and the pulses let it go */
uint8_t  i2c_guard_prev_inflight(void);            /* the run before this boot stopped inside a transaction to this address (0: none or unknown) */
uint16_t i2c_guard_prev_fails(void);               /* ... and counted this many failures (0 after a power-on) */
void     i2c_guard_lock(void);                     /* the bus for a read-modify-write (recursive: the wrappers take it too) */
void     i2c_guard_unlock(void);
bool     i2c_guard_recover(const char *why);       /* the bus reset + a PMIC probe, by hand (director, a board not found); true = the PMIC answers */
uint32_t i2c_guard_fails(void);                    /* failed transactions since boot (probes that found nobody are not failures) */
uint32_t i2c_guard_recoveries(void);
uint8_t  i2c_guard_last_addr(void);                /* the address of the last failure (0: none yet) */
void     i2c_guard_report(void);                   /* the per-address table, logged (director `blackbox`) */
#else   /* the big screens (TANK_P4): no guard in 0.4.3 (it knows the pocket boards' bus and PMIC). The source and its
           --wrap are left out (CMakeLists.txt); every call is an empty inline */
static inline void     i2c_guard_boot_clear(int sda, int scl) { (void)sda; (void)scl; }
static inline bool     i2c_guard_boot_stuck(void) { return false; }
static inline bool     i2c_guard_boot_freed(void) { return false; }
static inline uint8_t  i2c_guard_prev_inflight(void) { return 0; }
static inline uint16_t i2c_guard_prev_fails(void) { return 0; }
static inline void     i2c_guard_lock(void) {}
static inline void     i2c_guard_unlock(void) {}
static inline bool     i2c_guard_recover(const char *why) { (void)why; return false; }
static inline uint32_t i2c_guard_fails(void) { return 0; }
static inline uint32_t i2c_guard_recoveries(void) { return 0; }
static inline uint8_t  i2c_guard_last_addr(void) { return 0; }
static inline void     i2c_guard_report(void) {}
#endif
#endif
