/* imu_mpu6050.c — an MPU-6050 as imu_port's accelerometer (see imu_chip.h).
 *
 * The stand-in for the CYD while its QMI8658C is on the way: a GY-521-style
 * breakout on the board's shared I2C bus (SDA IO16, SCL IO15, beside the touch
 * at 0x38 and the codec at 0x18), at 0x68 with AD0 low or 0x69 with it high.
 * Only SDA and SCL are wired: everything imu_port does is polled, so no INT.
 *
 * Accel only, as the QMI8658 path: +-2 g (16384 counts/g, the same scale), a
 * 10 Hz low-pass so a table's noise stays well under MOTION_THRESH, the gyros
 * in standby. Register facts are from the MPU-6000/6050 register map. */
#include "sdkconfig.h"
#include "imu_chip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MPU_ADDR          0x68
#define MPU_ADDR_ALT      0x69
#define REG_SMPLRT_DIV    0x19
#define REG_CONFIG        0x1A   /* DLPF_CFG in bits 2:0 */
#define REG_ACCEL_CONFIG  0x1C   /* AFS_SEL in bits 4:3; 0 = +-2 g */
#define REG_ACCEL_CONFIG2 0x1D   /* the accel's own DLPF, on 6500-family dies only */
#define REG_ACCEL_XOUT_H  0x3B   /* X Y Z, high byte first */
#define REG_SIGNAL_RESET  0x68
#define REG_PWR_MGMT_1    0x6B   /* b7 reset, b6 sleep, b3 temperature off, b2:0 clock */
#define REG_PWR_MGMT_2    0x6C   /* b2:0 gyro X Y Z standby */
#define REG_WHO_AM_I      0x75

/* which accel axis is "up" when the CYD is held right side up, and which
 * points out of the glass. The breakout is hand-mounted, so these are bench
 * facts: hold the board upright, run the director's `imu`, and read which axis
 * carries ~16k and with what sign (docs/CYD.md). */
#define UP_AXIS   CONFIG_POCKET_TANK_IMU_MPU6050_UP_AXIS
#define OUT_AXIS  CONFIG_POCKET_TANK_IMU_MPU6050_OUT_AXIS
/* a Kconfig bool that is off is not defined at all, so test it, not its value */
#ifdef CONFIG_POCKET_TANK_IMU_MPU6050_UP_NEGATIVE
#define UP_SIGN   (-1)
#else
#define UP_SIGN   1
#endif
#ifdef CONFIG_POCKET_TANK_IMU_MPU6050_OUT_NEGATIVE
#define OUT_UP_SIGN (-1)
#else
#define OUT_UP_SIGN 1
#endif
_Static_assert(UP_AXIS != OUT_AXIS, "the up axis cannot also be the one out of the glass");

static const char *TAG = "imu";
static i2c_master_dev_handle_t s_dev;
/* A 6500-family die answering as a "6050" keeps the accel low-pass in its own
 * register; set there too, or the accel runs at ~218 Hz bandwidth. */
static bool s_6500_family;

static bool wr8(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, 2, 100) == ESP_OK;
}
static bool rdn(uint8_t reg, uint8_t *val, size_t n) {
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, n, 100) == ESP_OK;
}

/* Reset, then configure. The chip comes out of reset ASLEEP, and asleep its
 * data registers read zero - g=[0 0 0], no flip, no motion, no error - so the
 * wake below is the step that matters. */
static bool mpu_reset_config(void) {
    bool rst = wr8(REG_PWR_MGMT_1, 0x80);
    vTaskDelay(pdMS_TO_TICKS(100));
    (void)wr8(REG_SIGNAL_RESET, 0x07);   /* gyro, accel, temperature paths */
    vTaskDelay(pdMS_TO_TICKS(10));
    bool ok = wr8(REG_PWR_MGMT_1, 0x08)       /* awake, internal 8 MHz clock, temperature off */
           && wr8(REG_PWR_MGMT_2, 0x07)       /* gyros in standby: accel only */
           && wr8(REG_SMPLRT_DIV, 31)         /* 1 kHz / 32 = 31.25 Hz, the QMI8658's rate */
           && wr8(REG_CONFIG, 0x05)           /* DLPF 10 Hz */
           && wr8(REG_ACCEL_CONFIG, 0x00);    /* +-2 g */
    if (ok && s_6500_family) ok = wr8(REG_ACCEL_CONFIG2, 0x05);
    uint8_t p1 = 0xEE, p2 = 0xEE, cf = 0xEE, ac = 0xEE;   /* readback: is it even listening? */
    rdn(REG_PWR_MGMT_1, &p1, 1); rdn(REG_PWR_MGMT_2, &p2, 1);
    rdn(REG_CONFIG, &cf, 1); rdn(REG_ACCEL_CONFIG, &ac, 1);
    ESP_LOGI(TAG, "MPU-6050 reset %s, readback pwr1=0x%02x pwr2=0x%02x config=0x%02x accel=0x%02x (want 08/07/05/00)",
             rst ? "acked" : "NACKED", p1, p2, cf, ac);
    return ok;
}

static bool mpu_read_accel(int16_t a[3]) {
    uint8_t raw[6];
    if (!rdn(REG_ACCEL_XOUT_H, raw, 6)) return false;
    a[0] = (int16_t)(raw[0] << 8 | raw[1]);   /* big-endian, where the QMI8658 is little */
    a[1] = (int16_t)(raw[2] << 8 | raw[3]);
    a[2] = (int16_t)(raw[4] << 8 | raw[5]);
    return true;
}

/* a part that answered but is not ours: give its slot on the bus back */
static void forget(void) {
    if (s_dev) (void)i2c_master_bus_rm_device(s_dev);
    s_dev = NULL;
}

static void mpu_sleep(void) {
    (void)wr8(REG_PWR_MGMT_1, 0x48);          /* asleep, temperature off: a few uA */
}

static const struct imu_chip s_chip = {
    .name = "MPU-6050",
    .reset_config = mpu_reset_config,
    .read_accel = mpu_read_accel,
    .sleep = mpu_sleep,
    .up_axis = UP_AXIS,
    .up_sign = UP_SIGN,
    .out_axis = OUT_AXIS,
    .out_up_sign = OUT_UP_SIGN,
};

const struct imu_chip *imu_mpu6050_probe(i2c_master_bus_handle_t bus) {
    if (!bus) return NULL;
    uint8_t addr = MPU_ADDR;
    if (i2c_master_probe(bus, addr, 50) != ESP_OK) {
        addr = MPU_ADDR_ALT;
        if (i2c_master_probe(bus, addr, 50) != ESP_OK) { ESP_LOGW(TAG, "no MPU-6050 either"); return NULL; }
    }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                .device_address = addr, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) return NULL;
    uint8_t who = 0;
    if (!rdn(REG_WHO_AM_I, &who, 1)) { ESP_LOGW(TAG, "0x%02x answers but WHO_AM_I does not", addr); forget(); return NULL; }
    /* 0x68 is a genuine 6050. Clones sold as "6050" answer with other dies'
     * values; their accel registers match, so take them, and say so. */
    switch (who) {
    case 0x68: break;
    case 0x70: case 0x71: case 0x73: case 0x98:
        s_6500_family = true;
        ESP_LOGW(TAG, "WHO_AM_I 0x%02x: a 6500-family die sold as a 6050 - using it", who);
        break;
    case 0x72:
        ESP_LOGW(TAG, "WHO_AM_I 0x72: a 6050 clone - using it");
        break;
    default:
        ESP_LOGW(TAG, "0x%02x WHO_AM_I 0x%02x: not an MPU-6050", addr, who);
        forget(); return NULL;
    }
    if (!mpu_reset_config()) { ESP_LOGW(TAG, "MPU-6050 config failed"); forget(); return NULL; }
    ESP_LOGI(TAG, "MPU-6050 up at 0x%02x: orientation axis %c%c, out of the glass %c", addr,
             UP_SIGN > 0 ? '+' : '-', "XYZ"[UP_AXIS], "XYZ"[OUT_AXIS]);
    return &s_chip;
}
