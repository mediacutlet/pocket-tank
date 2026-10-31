/* imu_port_qmi8658.c — QMI8658 6-axis IMU (I2C 0x6B, alt 0x6A) as an
 * orientation sensor: accel only at 31.25 Hz, gyro off. Polled ~4x/s from the
 * tank task; the inverted flag flips only after the gravity component along
 * the panel's landscape-vertical axis has clearly (>0.5 g) pointed the other
 * way for 3 consecutive polls, and holds its last state while the device lies
 * flat (no axis dominant), so the screen never flaps on a table. */
#include "imu_port.h"
#include "display_port.h"      /* board_is_round */
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* which accel axis is "up" when the tank is held right side up. The boot log
 * prints the live vector ("imu: g=[x y z]") — if the flip is wrong or dead,
 * hold the device upright, read which axis carries ~1 g, and fix these two. */
static int s_up_axis = 1;    /* 0=X 1=Y 2=Z; the 1.8, calibrated 2026-08-28: upright-in-hand = -Y ~16k */
static int s_up_sign = -1;   /* the round 1.75C, 2026-10-01 (Strato holding it upright, USB down): +X ~16.8k (imu_port_init) */
#define IMU_UP_AXIS s_up_axis
#define IMU_UP_SIGN s_up_sign

#define QMI8658_ADDR       0x6B
#define QMI8658_ADDR_ALT   0x6A
#define REG_WHO_AM_I       0x00   /* reads 0x05 */
#define REG_CTRL1          0x02
#define REG_CTRL2          0x03
#define REG_CTRL7          0x08
#define REG_RESET          0x60   /* write 0xB0 = soft reset */
#define REG_AX_L           0x35
#define WHO_AM_I_VAL       0x05

#define POLL_INTERVAL_US   250000
/* 2026-08-31: was 8192 (0.5 g) - that only fired within ~60 deg of vertical,
 * so a device reclined on its back (bench pose: up-axis carries ~0.25 g)
 * never flipped. Now ~0.21 g, but the up-axis must also DOMINATE the other
 * in-screen axis, so lying flat or held sideways still holds last state. */
#define FLIP_THRESH        3500   /* ~0.21 g at +-2g full scale (16384 counts/g) */
#define FLIP_HOLD_POLLS    3      /* ~750 ms the other way up before flipping */
/* on a side (0.3.9.3): a new way up must carry gravity 1.3x the other in-screen axis
 * (~52 degrees from the last), so a tank tilted near 45 degrees never flaps between two */
#define SIDE_MARGIN_X10    13
/* motion = the sum over healthy axes of |a - a_prev| between two polls
 * (250 ms apart). A table reads a few tens of counts of noise; a hand
 * holding still a few hundred; a pick-up thousands. */
#define MOTION_THRESH      220    /* ~0.013 g */
#define IMU_MOTION_HOLD_US 1000000

static const char *TAG = "imu";
static i2c_master_dev_handle_t s_dev;
static int s_q;                   /* the way up, in quarter turns clockwise of the picture: 0 upright, 2 inverted, 1 / 3 on a side */
static bool s_sides;              /* the square and the pendant (0.3.9.3): a side counts too */
static int s_side_sign = 1;       /* gravity's pull along the other in-screen axis that reads + with the glass's left edge down (q 1) */
static int s_streak, s_want;      /* consecutive polls voting for the same new way up, and which */
static int64_t s_next_us;
static int16_t s_prev[3]; static bool s_have_prev;
static int64_t s_moved_us; static int s_motion; static int16_t s_last[3];
static int64_t s_handled_us; static bool s_prev_moved;   /* two polls in a row over the threshold */

static bool wr8(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, 2, 100) == ESP_OK;
}
static bool rdn(uint8_t reg, uint8_t *val, size_t n) {
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, n, 100) == ESP_OK;
}

/* soft reset + full config. The chip sits on an always-on rail, so it keeps
 * whatever state it fell into across reboots and reflashes - 2026-08-31 it
 * was found latched with two axes railed at full scale (garbage that only a
 * reset clears; only a full PMIC power-off ever power-cycles it). Never
 * trust its power-on state. */
static bool imu_reset_config_once(void);
static bool imu_reset_config(void) {
    if (imu_reset_config_once()) return true;
    vTaskDelay(pdMS_TO_TICKS(50));             /* a chip woken from imu_port_power_down can NACK the first round */
    return imu_reset_config_once();
}
static bool imu_reset_config_once(void) {
    (void)wr8(REG_CTRL1, 0x40);                /* its 2 MHz clock back on first (a night in Power-Down leaves it off) */
    vTaskDelay(pdMS_TO_TICKS(5));
    bool rst = wr8(REG_RESET, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(25));
    bool ok = wr8(REG_CTRL1, 0x40)   /* address auto-increment for burst reads */
           && wr8(REG_CTRL2, 0x08)   /* accel +-2g, 31.25 Hz */
           && wr8(REG_CTRL7, 0x01);  /* accel on, gyro off */
    uint8_t c1 = 0xEE, c2 = 0xEE, c7 = 0xEE;   /* readback: is it even listening? */
    rdn(REG_CTRL1, &c1, 1); rdn(REG_CTRL2, &c2, 1); rdn(REG_CTRL7, &c7, 1);
    ESP_LOGI(TAG, "reset %s, ctrl readback 1=0x%02x 2=0x%02x 7=0x%02x (want 40/08/01)",
             rst ? "acked" : "NACKED", c1, c2, c7);
    return ok;
}

bool imu_port_init(i2c_master_bus_handle_t bus) {
    if (!bus) return false;
    uint8_t addr = QMI8658_ADDR;
    if (i2c_master_probe(bus, addr, 50) != ESP_OK) {
        addr = QMI8658_ADDR_ALT;
        if (i2c_master_probe(bus, addr, 50) != ESP_OK) { ESP_LOGW(TAG, "no QMI8658"); return false; }
    }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                .device_address = addr, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) return false;
    uint8_t who = 0;
    if (!rdn(REG_WHO_AM_I, &who, 1) || who != WHO_AM_I_VAL) {
        ESP_LOGW(TAG, "QMI8658 whoami 0x%02x (want 0x05)", who);
        s_dev = NULL; return false;
    }
    if (!imu_reset_config()) { ESP_LOGW(TAG, "QMI8658 config failed"); s_dev = NULL; return false; }
    if (board_is_round()) { s_up_axis = 0; s_up_sign = 1; }
    /* the watch, 2026-10-02 (propped on the desk, the tank right side up): g = [13100 900 -9400] -
       the panel's long axis is X, its foot +X. Nothing reads it: on a wrist the live flip never
       runs (main.c), the way up is settings SCREEN (tank.h) */
    else if (board_is_watch()) { s_up_axis = 0; s_up_sign = 1; }
    /* the square, 2026-10-12 (Strato holding it his usual way, the USB at the foot, tilted back): g = [1166 13033 10739] -
       gravity runs +Y in that grip: up = +Y (the 1.8's is -Y) */
    else if (board_is_square()) { s_up_axis = 1; s_up_sign = 1; }
    ESP_LOGI(TAG, "QMI8658 up at 0x%02x: orientation axis %c%s", addr,
             IMU_UP_SIGN > 0 ? '+' : '-', IMU_UP_AXIS == 0 ? "X" : IMU_UP_AXIS == 1 ? "Y" : "Z");
    return true;
}

void imu_port_poll(int64_t now_us) {
    if (!s_dev || now_us < s_next_us) return;
    s_next_us = now_us + POLL_INTERVAL_US;
    uint8_t raw[6];
    if (!rdn(REG_AX_L, raw, 6)) return;
    int16_t a[3] = { (int16_t)(raw[0] | raw[1] << 8),
                     (int16_t)(raw[2] | raw[3] << 8),
                     (int16_t)(raw[4] | raw[5] << 8) };
    static int logged;
    if (logged < 3) { logged++; ESP_LOGI(TAG, "g=[%d %d %d] quarter=%d", a[0], a[1], a[2], s_q); }
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
    /* railed axis = a channel latched at full scale. Found 2026-08-31: X and
     * Z pegged at +-32767 while Y tracked reality, with clean comms, clean
     * config readback, soft reset no help - damaged channels on the MEMS die.
     * Work with what's healthy: the flip only needs the UP axis. A railed
     * other axis just skips the dominance guard; only a railed UP axis
     * disables the flip (and we keep nudging the chip with soft resets in
     * case it is recoverable stiction rather than damage). */
#define RAILED(x) ((x) <= -32000 || (x) >= 32000)
    static int s_bad; static int64_t s_gate; static bool s_warned;
    if (RAILED(a[IMU_UP_AXIS])) {
        if (++s_bad >= 12 && now_us > s_gate) {              /* ~3 s railed */
            ESP_LOGW(TAG, "up axis railed (g=[%d %d %d]) - soft reset", a[0], a[1], a[2]);
            imu_reset_config();
            s_bad = 0; s_gate = now_us + 5000000;
        }
        return;
    }
    s_bad = 0;
    int v = a[IMU_UP_AXIS] * IMU_UP_SIGN;
    /* the other IN-SCREEN axis (Z is out of the glass): the up-axis must
     * carry more of gravity than it, or we are sideways/flat - hold state.
     * Skipped when that axis is railed - one good axis is enough to flip. */
    int other = a[IMU_UP_AXIS == 0 ? 1 : 0];
    if (RAILED(other) && !s_warned) {
        s_warned = true;
        ESP_LOGW(TAG, "axis %c railed (sensor damage?) - flip runs on the up axis alone",
                 IMU_UP_AXIS == 0 ? 'Y' : 'X');
    }
    int av = v > 0 ? v : -v, ao = other > 0 ? other : -other;
    int want = -1;                                 /* flat, or between two ways up: hold state */
    if (RAILED(other) || av > ao) {
        if (av > FLIP_THRESH && (!s_sides || RAILED(other) || av * 10 > ao * SIDE_MARGIN_X10)) want = v > 0 ? 0 : 2;
    } else if (s_sides && ao > FLIP_THRESH && ao * 10 > av * SIDE_MARGIN_X10) {
        want = other * s_side_sign > 0 ? 1 : 3;    /* on a side (0.3.9.3) */
    }
    if (want < 0 || want == s_q) { s_streak = 0; return; }
    s_streak = want == s_want ? s_streak + 1 : 1; s_want = want;
    if (s_streak >= FLIP_HOLD_POLLS) {
        s_q = want; s_streak = 0;
        static const char *const QN[4] = { "upright", "on its side (the left edge down)", "inverted", "on its side (the right edge down)" };
        ESP_LOGI(TAG, "orientation: %s", QN[s_q]);
    }
}

bool imu_port_inverted(void) { return s_q == 2; }
int  imu_port_quarter(void) { return s_q; }
void imu_port_sides(bool on) {
    s_sides = on;
    /* the side axis's sign, from the up axis: Z runs out of the glass (both boards read +1 g on Z lying face up on
       the desk, 2026-10-30), so the glass's right is up x Z - +X when up is +Y (the square), -Y when up is +X (the
       pendant); its left edge down puts gravity's pull (the reading's sign) on that right */
    s_side_sign = (IMU_UP_AXIS == 1 ? 1 : -1) * IMU_UP_SIGN;
    if (!on && (s_q & 1)) s_q = 0;
}
void imu_port_side_sign_flip(void) { s_side_sign = -s_side_sign; ESP_LOGI(TAG, "side sign now %+d", s_side_sign); }
/* the way up it had before the restart (a swap, a flash, a power-on): kept until gravity clearly says
   otherwise, as the flip always has - a tank reclined on a desk never says, and came up upright (Strato,
   2026-10-29, after a swap: "i need to do a quick flip of the tank up and down") */
void imu_port_seed_inverted(bool inv) { s_q = inv ? 2 : 0; s_streak = 0; }
void imu_port_seed_quarter(int q) { s_q = s_sides ? (q & 3) : (q & 2); s_streak = 0; }
void imu_port_last(int16_t out[3], int *motion) { for (int i = 0; i < 3; i++) out[i] = s_last[i]; if (motion) *motion = s_motion; }
bool imu_port_handled(void) { return s_handled_us && esp_timer_get_time() - s_handled_us < IMU_MOTION_HOLD_US; }
bool imu_port_moving(void) { return s_moved_us && esp_timer_get_time() - s_moved_us < IMU_MOTION_HOLD_US; }
int  imu_port_motion(void) { return s_motion; }

/* drowse bracket (see imu_port.h). Sleep: sensors off, chip quiesced while
 * the neighbouring rails cycle. Wake: never trust what the chip did in the
 * dark - full soft reset + reconfigure. */
void imu_port_sleep(void) {
    if (s_dev) (void)wr8(REG_CTRL7, 0x00);
}
/* the datasheet's Power-Down: CTRL1 sensorDisable (bit 0) with every sensor
 * off; the boot's soft reset undoes it */
void imu_port_power_down(void) {
    if (s_dev) (void)wr8(REG_CTRL1, 0x41);
}
void imu_port_wake(void) {
    if (!s_dev) return;
    if (!imu_reset_config()) ESP_LOGW(TAG, "wake reconfig failed");
}
