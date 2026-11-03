#ifndef RTC_PORT_H
#define RTC_PORT_H
#include <stdbool.h>
#include "driver/i2c_master.h"
/* call after the board I2C bus exists (display_port_init creates it). True =
 * a chip answered. Its time sets the system clock only when it is plausible;
 * a chip that STOPPED (lost power: no cell and the cable out) leaves the
 * clock unset - rtc_port_stopped() - for main.c to set from the internet or
 * the save, after which rtc_port_store() writes it back into the chip. */
bool rtc_port_init(i2c_master_bus_handle_t bus);
bool rtc_port_stopped(void);                  /* the chip had lost its time at this boot */
bool rtc_port_unread(void);                   /* the chip did not answer at this boot (its time unknown: never seed the save's into it) */
void rtc_port_store(void);                    /* the system clock -> the chip (no chip, or a clock before 2024: nothing) */
/* A board with NO RTC chip (the round 1.75C), or one whose chip STOPPED: the
 * system clock is the ESP32's own RTC timer - it counts through deep sleep
 * and restarts, not through a power cut. At a boot that finds it unset
 * (time() is still 1970) call this once the save is loaded: the clock starts
 * at the build time or at `not_before` (the save's stamp), whichever is
 * later, so the tank's timeline only moves forward and a power cut reads as
 * no time at all - never as an absence nobody can vouch for. */
void rtc_port_seed(int64_t not_before);
#endif
