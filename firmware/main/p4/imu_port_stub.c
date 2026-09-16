/* imu_port_stub.c — the Waveshare 4B has no IMU (no QMI8658): the screen
 * is never auto-flipped (always upright) and the handled/motion accessors
 * report steady. imu_port_init returns false; every state accessor is a
 * no-op returning the "no sensor" value. */
#include "imu_port.h"
#include "esp_log.h"

bool imu_port_init(i2c_master_bus_handle_t bus) {
    (void)bus;
    ESP_LOGI("imu", "no IMU on 4B (never inverted, no motion)");
    return false;
}
void imu_port_poll(int64_t now_us) { (void)now_us; }
bool imu_port_inverted(void) { return false; }
bool imu_port_moving(void) { return false; }
bool imu_port_handled(void) { return false; }
int  imu_port_motion(void) { return 0; }
void imu_port_last(int16_t out[3], int *motion) {
    out[0] = 0; out[1] = 0; out[2] = 0;
    if (motion) *motion = 0;
}
void imu_port_sleep(void) {}
void imu_port_wake(void) {}
