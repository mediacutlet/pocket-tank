/* imu_port.c — an accelerometer as an orientation sensor, and a handling
 * detector. Accel only, polled ~4x/s from the tank task; the inverted flag
 * flips only after the gravity component along the panel's landscape-vertical
 * axis has clearly pointed the other way for 3 consecutive polls, and holds
 * its last state while the device lies flat (no axis dominant), so the screen
 * never flaps on a table.
 *
 * Which chip answers is decided at boot, in the order below, among the ones
 * the build enables (Kconfig): a QMI8658 (imu_qmi8658.c), then an MPU-6050
 * (imu_mpu6050.c). Both report counts at +-2 g, so everything here is the same
 * for either -- see imu_chip.h. (This was imu_port_qmi8658.c, the QMI8658 and
 * this logic in one file, until the CYD needed a second part.) */
#include "imu_port.h"
#include "imu_chip.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POLL_INTERVAL_US   250000
/* 2026-08-31: was 8192 (0.5 g) - that only fired within ~60 deg of vertical,
 * so a device reclined on its back (bench pose: up-axis carries ~0.25 g)
 * never flipped. Now ~0.21 g, but the up-axis must also DOMINATE the other
 * in-screen axis, so lying flat or held sideways still holds last state. */
#define FLIP_THRESH        3500   /* ~0.21 g at +-2g full scale (16384 counts/g) */
#define FLIP_HOLD_POLLS    3      /* ~750 ms the other way up before flipping */
/* motion = the sum over healthy axes of |a - a_prev| between two polls
 * (250 ms apart). A table reads a few tens of counts of noise; a hand
 * holding still a few hundred; a pick-up thousands. */
#define MOTION_THRESH      220    /* ~0.013 g */
#define IMU_MOTION_HOLD_US 1000000
/* face down: the axis out of the glass points away from screen-up by more
 * than half a g (the CYD's breakout reads 0.79 g there, light), while both
 * in-screen axes stay under a third of one - lying flat, not tilted - and
 * nothing moves, for 2 s. */
#define FACE_THRESH        8192   /* 0.5 g */
#define FACE_FLAT          5734   /* 0.35 g */
#define IMU_FACE_HOLD_POLLS 8     /* 2 s at 4 Hz */
/* still enough, for this purpose: a hand steadying the board reads 200-800
 * a poll, which the handling detector's 220 would call moving; lifting it or
 * carrying it reads thousands. Lying face down and level is itself most of
 * the evidence. */
#define FACE_STILL         1000   /* ~0.06 g */

static const char *TAG = "imu";
/* the chip that answered, through imu_chip.h. NULL = no IMU, and every
 * function below is a no-op. */
static const struct imu_chip *s_chip;
static bool s_inverted;
static int s_streak;              /* consecutive polls voting for a flip */
static int64_t s_next_us;
static int16_t s_prev[3]; static bool s_have_prev;
static int64_t s_moved_us; static int s_motion; static int16_t s_last[3];
static int64_t s_handled_us; static bool s_prev_moved;   /* two polls in a row over the threshold */
#if CONFIG_POCKET_TANK_IMU_FACE_DOWN_SLEEP
static int s_face_polls;          /* consecutive still, face-down polls */
static bool s_face_armed;         /* seen not face down since the gesture last fired */
static bool s_face_fired;         /* the gesture, waiting to be taken */

/* the screen is facing the table: the out-of-glass axis reads the other way
 * from screen-up, by a clear margin, and the screen lies level */
static bool face_down(const int16_t a[3]) {
    const int out = s_chip->out_axis, up = s_chip->up_axis, side = 3 - up - out;
    int toward = a[out] * s_chip->out_up_sign;
    int u = a[up] < 0 ? -a[up] : a[up], w = a[side] < 0 ? -a[side] : a[side];
    return toward < -FACE_THRESH && u < FACE_FLAT && w < FACE_FLAT;
}
#endif

/* The QMI8658 first, as it always was; then the MPU-6050 (the CYD's stand-in
 * while its QMI8658C is on the way). Each only where the build enables it. */
bool imu_port_init(i2c_master_bus_handle_t bus) {
    if (!bus) return false;
#if CONFIG_POCKET_TANK_IMU_QMI8658
    if (!s_chip) s_chip = imu_qmi8658_probe(bus);
#endif
#if CONFIG_POCKET_TANK_IMU_MPU6050
    if (!s_chip) s_chip = imu_mpu6050_probe(bus);
#endif
    if (!s_chip) ESP_LOGW(TAG, "no IMU answered -- the screen follows the SCREEN setting alone");
    return s_chip != NULL;
}

void imu_port_poll(int64_t now_us) {
    if (!s_chip || now_us < s_next_us) return;
    s_next_us = now_us + POLL_INTERVAL_US;
    int16_t a[3];
    if (!s_chip->read_accel(a)) return;
    /* the chip's mounting, from imu_chip.h: up along the screen, out of the
     * glass, and the third axis the other in-screen one */
    const int up = s_chip->up_axis, sign = s_chip->up_sign;
    const int side = 3 - up - s_chip->out_axis;
    static int logged;
    if (logged < 3) { logged++; ESP_LOGI(TAG, "g=[%d %d %d] inverted=%d", a[0], a[1], a[2], (int)s_inverted); }
    /* handling: movement since the last poll, railed channels ignored */
    if (s_have_prev) {
        int m = 0;
        for (int i = 0; i < 3; i++) {
            if (a[i] <= -32000 || a[i] >= 32000 || s_prev[i] <= -32000 || s_prev[i] >= 32000) continue;
            int d = a[i] - s_prev[i]; m += d < 0 ? -d : d;
        }
        s_motion = m;
        bool moved = m > MOTION_THRESH;
        if (moved) s_moved_us = now_us;
        if (moved && s_prev_moved) s_handled_us = now_us;
        s_prev_moved = moved;
    }
    for (int i = 0; i < 3; i++) { s_prev[i] = a[i]; s_last[i] = a[i]; }
    s_have_prev = true;
#if CONFIG_POCKET_TANK_IMU_FACE_DOWN_SLEEP
    /* the face-down gesture: still and face down for 2 s, once per episode */
    if (face_down(a) && s_motion <= FACE_STILL) {
        if (++s_face_polls == IMU_FACE_HOLD_POLLS && s_face_armed) {
            s_face_fired = true; s_face_armed = false;
            ESP_LOGI(TAG, "face down and still for 2 s");
        }
    } else {
        s_face_polls = 0;
        if (!face_down(a)) s_face_armed = true;
    }
#endif
    /* railed axis = a channel latched at full scale. Found 2026-08-31: X and
     * Z pegged at +-32767 while Y tracked reality, with clean comms, clean
     * config readback, soft reset no help - damaged channels on the MEMS die.
     * Work with what's healthy: the flip only needs the UP axis. A railed
     * other axis just skips the dominance guard; only a railed UP axis
     * disables the flip (and we keep nudging the chip with soft resets in
     * case it is recoverable stiction rather than damage). */
#define RAILED(x) ((x) <= -32000 || (x) >= 32000)
    static int s_bad; static int64_t s_gate; static bool s_warned;
    if (RAILED(a[up])) {
        if (++s_bad >= 12 && now_us > s_gate) {              /* ~3 s railed */
            ESP_LOGW(TAG, "up axis railed (g=[%d %d %d]) - soft reset", a[0], a[1], a[2]);
            s_chip->reset_config();
            s_bad = 0; s_gate = now_us + 5000000;
        }
        return;
    }
    s_bad = 0;
    int v = a[up] * sign;
    /* the other IN-SCREEN axis (Z is out of the glass): the up-axis must
     * carry more of gravity than it, or we are sideways/flat - hold state.
     * Skipped when that axis is railed - one good axis is enough to flip. */
    int other = a[side];
    if (RAILED(other) && !s_warned) {
        s_warned = true;
        ESP_LOGW(TAG, "axis %c railed (sensor damage?) - flip runs on the up axis alone",
                 "XYZ"[side]);
    }
    bool dominant = RAILED(other) || (v > 0 ? v : -v) > (other > 0 ? other : -other);
    bool wants_flip = dominant && (s_inverted ? (v > FLIP_THRESH) : (v < -FLIP_THRESH));
    s_streak = wants_flip ? s_streak + 1 : 0;      /* flat / sideways: hold state */
    if (s_streak >= FLIP_HOLD_POLLS) {
        s_inverted = !s_inverted; s_streak = 0;
        ESP_LOGI(TAG, "orientation: %s", s_inverted ? "inverted" : "upright");
    }
}

bool imu_port_inverted(void) { return s_inverted; }
void imu_port_last(int16_t out[3], int *motion) { for (int i = 0; i < 3; i++) out[i] = s_last[i]; if (motion) *motion = s_motion; }
bool imu_port_handled(void) { return s_handled_us && esp_timer_get_time() - s_handled_us < IMU_MOTION_HOLD_US; }
bool imu_port_moving(void) { return s_moved_us && esp_timer_get_time() - s_moved_us < IMU_MOTION_HOLD_US; }
int  imu_port_motion(void) { return s_motion; }
#if CONFIG_POCKET_TANK_IMU_FACE_DOWN_SLEEP
bool imu_port_take_face_down(void) { bool f = s_face_fired; s_face_fired = false; return f; }
int  imu_port_face_down_now(void) {
    int16_t a[3];
    if (!s_chip || !s_chip->read_accel(a)) return -1;
    return face_down(a) ? 1 : 0;
}
#else
bool imu_port_take_face_down(void) { return false; }   /* the gesture is not built: never */
int  imu_port_face_down_now(void) { return -1; }
#endif

/* drowse bracket (see imu_port.h). Sleep: sensors off, chip quiesced while
 * the neighbouring rails cycle. Wake: never trust what the chip did in the
 * dark - full soft reset + reconfigure. */
void imu_port_sleep(void) {
    if (s_chip) s_chip->sleep();
}
void imu_port_wake(void) {
    if (!s_chip) return;
    if (!s_chip->reset_config()) ESP_LOGW(TAG, "wake reconfig failed");
}
