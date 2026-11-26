/* i2c_guard_rule.h - when the I2C bus guard (i2c_guard.h) calls a bus stuck and recovers it. Pure, so the host tests
 * it (sim/selftest_blackbox.c). */
#ifndef I2C_GUARD_RULE_H
#define I2C_GUARD_RULE_H
#include <stdbool.h>
#include <stdint.h>

#define I2C_GUARD_RUN      4              /* failures in a row, any chips, with no success between: the controller is wedged */
#define I2C_GUARD_GAP_US   2000000LL      /* recoveries at least this far apart: a dead chip does not turn the bus into a reset loop */

/* after a failed transaction: run the recovery? sda_low / scl_low: the line read low with the bus idle (the lock held,
   the transaction over); run: the failures in a row so far, this one included; since_us: since the last recovery
   (a large number if none yet) */
static inline bool i2c_guard_should_recover(bool sda_low, bool scl_low, int run, int64_t since_us) {
    if (since_us < I2C_GUARD_GAP_US) return false;
    return sda_low || scl_low || run >= I2C_GUARD_RUN;
}
#endif
