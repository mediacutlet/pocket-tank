/* imu_port.h — QMI8658 accelerometer -> screen orientation (180-degree flip
 * only, so the landscape tank keeps its aspect ratio either way up). */
#ifndef IMU_PORT_H
#define IMU_PORT_H
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

bool imu_port_init(i2c_master_bus_handle_t bus);  /* false = no IMU, never inverted */
void imu_port_poll(int64_t now_us);               /* call every frame; rate-limited inside */
bool imu_port_inverted(void);                     /* true = device is upside down */
/* handling detector (2026-09-15, for the audio port): true while the
 * device has moved within the last IMU_MOTION_HOLD_US - picked up, in a
 * hand, carried. Lying on a table it goes false. */
bool imu_port_moving(void);
/* handling for the tank light (2026-09-15): the same, but the motion must
 * show on two consecutive polls (500 ms) - a pick-up does, a knock on the
 * desk or a mug set down beside it is one spike. */
bool imu_port_handled(void);
/* a hard shake (2026-10-10): true once per shake (SHAKE_COUNT jolts over SHAKE_THRESH on consecutive polls); main
 * tosses the tank's creatures (tank_shake) */
bool imu_port_shaken(void);
int  imu_port_motion(void);                       /* last poll's movement, counts (director / tuning) */
void imu_port_last(int16_t out[3], int *motion);  /* the last poll's raw sample + its movement (director `imu`) */
/* drowse bracket: quiesce the accel before the panel/touch rails cut (a
 * powered chip beside rail transitions is the latch-up recipe that railed
 * X/Z on 2026-08-31 - a full power-off revived them), then soft-reset +
 * reconfigure on wake so it never resumes on trust. */
void imu_port_sleep(void);
void imu_port_wake(void);
void imu_port_power_down(void);   /* deep sleep only: the 2 MHz clock off too (~50 -> ~20 uA); the wake is a reboot */

#endif
