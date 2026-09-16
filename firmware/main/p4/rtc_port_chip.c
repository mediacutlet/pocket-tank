/* rtc_port_chip.c — the Waveshare 4B has NO external RTC (no PCF85063,
 * no battery to keep one alive): the "wall clock" is the chip's time since
 * boot. NOTE: wall-clock continuity across power-off is UNAVAILABLE on the
 * 4B — progression_wake guards on now > saved, so a fresh boot's epoch
 * (seconds-since-boot, small again) simply resumes from the save rather than
 * living a fake night. Acceptable: the tank's ravenous rule just restarts. */
#include "rtc_port.h"
#include "esp_timer.h"

bool rtc_port_init(i2c_master_bus_handle_t bus) {
    (void)bus;
    return true;   /* nothing to set up: the chip clock is always there */
}

/* seconds since boot (the best time the 4B can offer) */
int64_t clock_port_now_unix(void) { return esp_timer_get_time() / 1000000; }
