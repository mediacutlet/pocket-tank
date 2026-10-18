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
int  imu_port_motion(void);                       /* last poll's movement, counts (director / tuning) */
/* face-down gesture (2026-09-30, the CYD): true ONCE when the screen has lain
 * face down and still for IMU_FACE_HOLD_POLLS (2 s); re-armed only after the
 * device has been seen not face down, so waking it while it still lies there
 * does not put it straight back to sleep. */
bool imu_port_take_face_down(void);
/* one fresh read, for the sleep loop with the tank task stopped: 1 = face
 * down, 0 = not, -1 = no answer (the caller keeps sleeping). */
int  imu_port_face_down_now(void);
void imu_port_last(int16_t out[3], int *motion);  /* the last poll's raw sample + its movement (director `imu`) */
/* the dark (2026-10-08, main.c's enter_dark: screen off, tank task waiting):
 * picked up or tilted? imu_port_rest_begin forgets the rest pose; each
 * imu_port_rest_moved is one fresh read (the caller paces them, about five a
 * second). The board must first lie still for about a second, and that pose
 * is the rest pose - so a tank darkened in the hand does not wake as it is
 * put down; after that, once it has left the pose on two reads in a row, it
 * returns how far (counts, summed over the axes), else 0. Its own state: the
 * flip, handling and face-down votes are untouched. 0 with no IMU, or no
 * answer. */
void imu_port_rest_begin(void);
int  imu_port_rest_moved(void);
/* drowse bracket: quiesce the accel before the panel/touch rails cut (a
 * powered chip beside rail transitions is the latch-up recipe that railed
 * X/Z on 2026-08-31 - a full power-off revived them), then soft-reset +
 * reconfigure on wake so it never resumes on trust. */
void imu_port_sleep(void);
void imu_port_wake(void);
void imu_port_power_down(void);   /* deep sleep only: the 2 MHz clock off too (~50 -> ~20 uA); the wake is a reboot */

#endif
