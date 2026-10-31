/* imu_port.h — QMI8658 accelerometer -> screen orientation (the 180-degree flip,
 * so the landscape tank keeps its aspect ratio either way up; a square picture
 * stands on any of its four sides, imu_port_sides). */
#ifndef IMU_PORT_H
#define IMU_PORT_H
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

bool imu_port_init(i2c_master_bus_handle_t bus);  /* false = no IMU, never inverted */
void imu_port_poll(int64_t now_us);               /* call every frame; rate-limited inside */
bool imu_port_inverted(void);                     /* true = device is upside down */
void imu_port_seed_inverted(bool inv);            /* the way up the board had before this boot (main.c keeps it in NVS) */
/* on a side (0.3.9.3, the square and the pendant): with sides on, the way up is
 * any of four - the picture's quarter turns clockwise, 0..3 (tank_orient) */
void imu_port_sides(bool on);
int  imu_port_quarter(void);
void imu_port_seed_quarter(int q);
void imu_port_side_sign_flip(void);               /* director `turn sign`: the other side is the left one */
/* handling detector (2026-09-15, for the audio port): true while the
 * device has moved within the last IMU_MOTION_HOLD_US - picked up, in a
 * hand, carried. Lying on a table it goes false. */
bool imu_port_moving(void);
/* handling for the tank light (2026-09-15): the same, but the motion must
 * show on two consecutive polls (500 ms) - a pick-up does, a knock on the
 * desk or a mug set down beside it is one spike. */
bool imu_port_handled(void);
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
