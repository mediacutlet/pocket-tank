/* battery_port_stub.c — the Waveshare 4B is USB-C powered: no battery,
 * no AXP2101 PMIC, no PWR key. The no-PMIC stub: battery_port_init returns
 * false so main.c takes the no-PMIC path (meter hidden, sleep key = BOOT,
 * power-off = deep sleep), every rail/key/dump accessor is a no-op. */
#include "battery_port.h"
#include "esp_log.h"

static const char *TAG = "battery";

bool battery_port_init(i2c_master_bus_handle_t bus) {
    (void)bus;
    ESP_LOGI(TAG, "no PMIC on 4B (USB-C powered)");
    return false;
}
bool battery_port_read(float *frac, bool *charging) {
    (void)frac; (void)charging;
    return false;
}
int  battery_port_vbat_mv(void) { return 0; }
bool battery_port_set_rail(const char *name, bool on) {
    (void)name; (void)on;
    return false;
}
void battery_port_trim_rails(void) {}
void battery_port_key_init(void) {}
int  battery_port_key_poll(void) { return 0; }
bool battery_port_poweroff(void) { return false; }
void battery_port_dump(void) { ESP_LOGI(TAG, "no PMIC on 4B"); }
