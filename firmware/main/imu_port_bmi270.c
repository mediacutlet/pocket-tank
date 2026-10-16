/* imu_port_bmi270.c - the Tab5's Bosch BMI270 (I2C 0x68) -> screen orientation.
 * The same job imu_port_qmi8658.c does for the four Waveshare boards, with the
 * same physics (a 180-degree flip only, so the landscape tank keeps its aspect
 * ratio either way up) but this chip's own driver: the BMI270 does not run its
 * data path until an 8 KB configuration blob has been downloaded into it, which
 * is what Espressif's `espressif/bmi270` component is for.
 *
 * The component works in g (floats), the port's contract is raw counts at +-2 g
 * like the other boards (imu_port_last is what the director's `imu` prints), so
 * samples are scaled by 16384 counts/g at the boundary and every threshold below
 * is kept in the same physical units as the QMI8658 port's:
 *     flip   3500 counts = 0.214 g      motion 220 counts = 0.013 g
 *     poll every 250 ms, three polls the other way up before flipping, handled
 *     needs two polls in a row (a knock is one spike, a pick-up is not).
 *
 * The installed orientation (IMU_UP_AXIS/SIGN: which axis carries gravity and
 * which way, so that "upright" reads as about +1 g) is calibrated on the unit -
 * the first polls log the raw sample, exactly as the other boards' ports did. */
#include "imu_port.h"
#include "board_pins.h"
#include "bmi270.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "imu";

#define BMI270_ADDR        BMI270_I2C_ADDRESS_L   /* 0x68 on this board */
#define POLL_INTERVAL_US   250000
#define COUNTS_PER_G       16384.0f               /* +-2 g full scale */
#define FLIP_THRESH        0.214f                 /* g, the QMI8658 port's 3500 counts */
#define FLIP_HOLD_POLLS    3                      /* ~750 ms the other way up before flipping */
#define MOTION_THRESH      0.0134f                /* g, its 220 counts */
#define IMU_MOTION_HOLD_US 1000000

/* calibrated on this unit, 2026-10-16, standing in its stand tilted back about
 * 30 degrees: g = [+0.866 +0.070 -0.514] - the screen's vertical (the panel's
 * short axis, landscape) is X and carries gravity, so up = +X. Change these two
 * when a board is mounted the other way. */
static int s_up_axis = 0;
static int s_up_sign = 1;
#define IMU_UP_AXIS s_up_axis
#define IMU_UP_SIGN s_up_sign

static bmi270_handle_t *s_dev;
static bool s_inverted;
static int s_streak;
static int64_t s_next_us;
static float s_prev[3]; static bool s_have_prev;
static int64_t s_moved_us; static float s_motion; static int16_t s_last[3];
static int64_t s_handled_us; static bool s_prev_moved;

static const bmi270_config_t k_cfg = {
    .acce_odr   = BMI270_ACC_ODR_50_HZ,
    .acce_range = BMI270_ACC_RANGE_2_G,
    .gyro_odr   = BMI270_GYR_ODR_25_HZ,
    .gyro_range = BMI270_GYR_RANGE_2000_DPS,
};

/* the driver's own start/setup, logged with its return so a silent chip is
 * never mistaken for a configured one */
static bool imu_configure(void) {
    esp_err_t r = bmi270_start(s_dev, &k_cfg);
    if (r != ESP_OK) { ESP_LOGW(TAG, "BMI270 could not start: %s", esp_err_to_name(r)); return false; }
    return true;
}

bool imu_port_init(i2c_master_bus_handle_t bus) {
    if (!bus) return false;
    if (i2c_master_probe(bus, BMI270_ADDR, 50) != ESP_OK) { ESP_LOGW(TAG, "no BMI270 at 0x%02x", BMI270_ADDR); return false; }
    const bmi270_driver_config_t dcfg = { .addr = BMI270_ADDR, .interface = BMI270_USE_I2C, .i2c_bus = bus };
    if (bmi270_create(&dcfg, &s_dev) != ESP_OK || !s_dev) { ESP_LOGW(TAG, "BMI270 create failed"); s_dev = NULL; return false; }
    uint8_t id = 0;
    if (bmi270_get_chip_id(s_dev, &id) != ESP_OK || id != BMI270_CHIP_ID) {
        ESP_LOGW(TAG, "BMI270 chip id 0x%02x (want 0x%02x)", id, BMI270_CHIP_ID);
        s_dev = NULL; return false;
    }
    if (!imu_configure()) { s_dev = NULL; return false; }
    ESP_LOGI(TAG, "BMI270 at 0x%02x: orientation axis %c%s, accel 50 Hz +-2 g",
             BMI270_ADDR, IMU_UP_SIGN > 0 ? '+' : '-', IMU_UP_AXIS == 0 ? "X" : IMU_UP_AXIS == 1 ? "Y" : "Z");
    return true;
}

void imu_port_poll(int64_t now_us) {
    if (!s_dev || now_us < s_next_us) return;
    s_next_us = now_us + POLL_INTERVAL_US;
    float f[3];
    if (bmi270_get_acce_data(s_dev, &f[0], &f[1], &f[2]) != ESP_OK) return;
    static int logged;
    if (logged < 4) {
        logged++;
        ESP_LOGI(TAG, "g=[%.3f %.3f %.3f]g = [%d %d %d] counts, inverted=%d",
                 f[0], f[1], f[2], (int)(f[0] * COUNTS_PER_G), (int)(f[1] * COUNTS_PER_G), (int)(f[2] * COUNTS_PER_G),
                 (int)s_inverted);
    }
    /* handling: movement since the last poll */
    if (s_have_prev) {
        float m = 0;
        for (int i = 0; i < 3; i++) m += fabsf(f[i] - s_prev[i]);
        s_motion = m;
        bool moved = m > MOTION_THRESH;
        if (moved) s_moved_us = now_us;
        if (moved && s_prev_moved) s_handled_us = now_us;
        s_prev_moved = moved;
    }
    for (int i = 0; i < 3; i++) {
        s_prev[i] = f[i];
        float c = f[i] * COUNTS_PER_G;
        s_last[i] = (int16_t)(c > 32767 ? 32767 : c < -32768 ? -32768 : lrintf(c));
    }
    s_have_prev = true;
    /* the flip: the up axis must carry more gravity than the other in-screen
     * axis (else the device is flat or on its side - hold state), and it must
     * point the other way for three polls in a row */
    float v = f[IMU_UP_AXIS] * IMU_UP_SIGN;
    float other = f[IMU_UP_AXIS == 0 ? 1 : 0];
    bool dominant = fabsf(v) > fabsf(other);
    bool wants_flip = dominant && (s_inverted ? (v > FLIP_THRESH) : (v < -FLIP_THRESH));
    s_streak = wants_flip ? s_streak + 1 : 0;
    if (s_streak >= FLIP_HOLD_POLLS) {
        s_inverted = !s_inverted; s_streak = 0;
        ESP_LOGI(TAG, "orientation: %s", s_inverted ? "inverted" : "upright");
    }
}

bool imu_port_inverted(void) { return s_inverted; }
void imu_port_last(int16_t out[3], int *motion) { for (int i = 0; i < 3; i++) out[i] = s_last[i]; if (motion) *motion = (int)(s_motion * COUNTS_PER_G); }
bool imu_port_handled(void) { return s_handled_us && esp_timer_get_time() - s_handled_us < IMU_MOTION_HOLD_US; }
bool imu_port_moving(void) { return s_moved_us && esp_timer_get_time() - s_moved_us < IMU_MOTION_HOLD_US; }
int  imu_port_motion(void) { return (int)(s_motion * COUNTS_PER_G); }

/* drowse bracket (see imu_port.h): stop the data path while the neighbouring
 * rails cycle, and never trust what the chip did in the dark - wake starts it
 * again from the same configuration */
void imu_port_sleep(void)      { if (s_dev) bmi270_stop(s_dev); }
void imu_port_power_down(void) { if (s_dev) bmi270_stop(s_dev); }   /* the wake is a reboot */
void imu_port_wake(void)       { if (s_dev && !imu_configure()) ESP_LOGW(TAG, "wake: BMI270 would not restart"); }
