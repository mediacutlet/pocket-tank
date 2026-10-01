/* imu_qmi8658.c — a QMI8658 6-axis IMU (I2C 0x6B, alt 0x6A) as imu_port's
 * accelerometer (see imu_chip.h): accel only at +-2 g and 31.25 Hz, gyro off.
 * The AMOLED board's own IMU, and the part the CYD is waiting for. */
#include "sdkconfig.h"
#include "imu_chip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* which accel axis is "up" when the tank is held right side up, and which is
 * out of the glass. The boot log prints the live vector ("imu: g=[x y z]") —
 * if the flip is wrong or dead, hold the device upright, read which axis
 * carries ~1 g, and set these in menuconfig. The defaults are the AMOLED's,
 * calibrated 2026-08-28: upright-in-hand = -Y ~16k, Z out of the glass. */
#define UP_AXIS   CONFIG_POCKET_TANK_IMU_QMI8658_UP_AXIS
#define OUT_AXIS  CONFIG_POCKET_TANK_IMU_QMI8658_OUT_AXIS
/* a Kconfig bool that is off is not defined at all, so test it, not its value */
#ifdef CONFIG_POCKET_TANK_IMU_QMI8658_UP_NEGATIVE
#define UP_SIGN   (-1)
#else
#define UP_SIGN   1
#endif
#ifdef CONFIG_POCKET_TANK_IMU_QMI8658_OUT_NEGATIVE
#define OUT_UP_SIGN (-1)
#else
#define OUT_UP_SIGN 1
#endif
_Static_assert(UP_AXIS != OUT_AXIS, "the up axis cannot also be the one out of the glass");

#define QMI8658_ADDR       0x6B
#define QMI8658_ADDR_ALT   0x6A
#define REG_WHO_AM_I       0x00   /* reads 0x05 */
#define REG_CTRL1          0x02
#define REG_CTRL2          0x03
#define REG_CTRL7          0x08
#define REG_RESET          0x60   /* write 0xB0 = soft reset */
#define REG_AX_L           0x35
#define WHO_AM_I_VAL       0x05

static const char *TAG = "imu";
static i2c_master_dev_handle_t s_dev;

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
static bool qmi_reset_config(void) {
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

static bool qmi_read_accel(int16_t a[3]) {
    uint8_t raw[6];
    if (!rdn(REG_AX_L, raw, 6)) return false;
    a[0] = (int16_t)(raw[0] | raw[1] << 8);   /* little-endian */
    a[1] = (int16_t)(raw[2] | raw[3] << 8);
    a[2] = (int16_t)(raw[4] | raw[5] << 8);
    return true;
}

/* sensors off, chip quiesced while the neighbouring rails cycle */
static void qmi_sleep(void) { (void)wr8(REG_CTRL7, 0x00); }

static const struct imu_chip s_chip = {
    .name = "QMI8658",
    .reset_config = qmi_reset_config,
    .read_accel = qmi_read_accel,
    .sleep = qmi_sleep,
    .up_axis = UP_AXIS,
    .up_sign = UP_SIGN,
    .out_axis = OUT_AXIS,
    .out_up_sign = OUT_UP_SIGN,
};

/* a part that answered but is not ours: give its slot on the bus back */
static void forget(void) {
    if (s_dev) (void)i2c_master_bus_rm_device(s_dev);
    s_dev = NULL;
}

const struct imu_chip *imu_qmi8658_probe(i2c_master_bus_handle_t bus) {
    if (!bus) return NULL;
    uint8_t addr = QMI8658_ADDR;
    if (i2c_master_probe(bus, addr, 50) != ESP_OK) {
        addr = QMI8658_ADDR_ALT;
        if (i2c_master_probe(bus, addr, 50) != ESP_OK) { ESP_LOGW(TAG, "no QMI8658"); return NULL; }
    }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                .device_address = addr, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) return NULL;
    uint8_t who = 0;
    if (!rdn(REG_WHO_AM_I, &who, 1) || who != WHO_AM_I_VAL) {
        ESP_LOGW(TAG, "QMI8658 whoami 0x%02x (want 0x05)", who);
        forget(); return NULL;
    }
    if (!qmi_reset_config()) { ESP_LOGW(TAG, "QMI8658 config failed"); forget(); return NULL; }
    ESP_LOGI(TAG, "QMI8658 up at 0x%02x: orientation axis %c%c, out of the glass %c", addr,
             UP_SIGN > 0 ? '+' : '-', "XYZ"[UP_AXIS], "XYZ"[OUT_AXIS]);
    return &s_chip;
}
