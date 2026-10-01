/* imu_chip.h — what imu_port needs from one accelerometer chip.
 *
 * imu_port.c holds the orientation and handling logic (the flip vote, the
 * motion sum, the railed-axis recovery) and talks to a chip only through this:
 * so a second part plugs in without a second copy of that logic, and the
 * QMI8658 path behaves exactly as it did when it was the only one. Every chip reports accel in counts at
 * +-2 g, 16384 counts per g, so the thresholds keep their meaning. */
#ifndef IMU_CHIP_H
#define IMU_CHIP_H
#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "driver/i2c_master.h"

struct imu_chip {
    const char *name;
    bool (*reset_config)(void);      /* soft reset + full config; never trust power-on state */
    bool (*read_accel)(int16_t a[3]);/* counts at +-2 g, the chip's own X Y Z */
    void (*sleep)(void);             /* quiesce before the neighbouring rails switch */
    int up_axis;                     /* 0=X 1=Y 2=Z: the axis along the screen's up, as mounted */
    int up_sign;                     /* +1 or -1: the sign that axis reads when held right side up */
    int out_axis;                    /* the axis out of the glass; the third is the other in-screen one */
    int out_up_sign;                 /* +1 or -1: the sign that axis reads lying flat, screen UP */
};

#if CONFIG_POCKET_TANK_IMU_QMI8658
/* Probe 0x6B then 0x6A, check WHO_AM_I, configure. NULL = no QMI8658. */
const struct imu_chip *imu_qmi8658_probe(i2c_master_bus_handle_t bus);
#endif
#if CONFIG_POCKET_TANK_IMU_MPU6050
/* Probe 0x68 then 0x69, check WHO_AM_I, configure. NULL = no MPU-6050. */
const struct imu_chip *imu_mpu6050_probe(i2c_master_bus_handle_t bus);
#endif

#endif
