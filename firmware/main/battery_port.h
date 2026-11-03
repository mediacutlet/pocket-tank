/* battery_port.h — device battery meter source (AXP2101 fuel gauge). */
#ifndef BATTERY_PORT_H
#define BATTERY_PORT_H
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "battery.h"            /* BAT_* */

bool battery_port_init(i2c_master_bus_handle_t bus);   /* false = no PMIC, meter hidden */
bool battery_port_read(float *frac, bool *charging);   /* cached ~1 s; false = hide meter */
int  battery_port_state(void);                          /* BAT_* from the same read: the cable, the charger's phase */
bool battery_port_poweroff(void);                      /* PMIC soft power-off; false = no PMIC */
/* the PWR key (2026-09-16): THE button. It is the AXP2101's PWRON pin, so it
 * is the only key that can bring the board back from a PMIC power-off - and
 * so it is the sleep key too. key_init enables the short/long-press IRQs,
 * clears whatever the power-on press left, stretches the PMIC's own hard
 * cut to 10 s and sets the power-on hold (ONLEVEL) to 128 ms so a TAP boots
 * the tank from a power-off (the chip's own default wants a longer hold, and
 * a tap that does nothing is what "three presses" felt like); key_poll reads
 * + clears them: 0 nothing, 1 short press, 2 long press (IRQLEVEL, 1.5 s). */
void battery_port_key_init(void);
int  battery_port_key_poll(void);
void battery_port_key_clear(void);          /* drop a press latched while nothing polled (the boot's time check): never acted on */
void battery_port_key_trace(int seconds);   /* bench: log each press's length for N s (director `keytime`) */
/* diagnostics: the AXP2101's rail enables + voltages, charger setting, VBAT
 * and state of charge, decoded to the log (read-only). VBAT in mV, 0 = n/a. */
void battery_port_dump(void);
int  battery_port_vbat_mv(void);
/* the charger (director `charge [mA]`): the set current, a live change (not saved: the boot sets the board's own again), a report */
int  battery_port_charge_ma(void);
bool battery_port_set_charge_ma(int ma);
void battery_port_charge_report(void);
/* the cell's size (0.3.9.2, the hidden ADVANCED page): the charge, precharge and end-of-charge currents for a
 * cell of mah (common/advanced.c adv_charge_ma / adv_trickle_ma; the board's own cell keeps the board's own
 * rates below). Called at boot with the saved size and when the keeper SETs one; false = no PMIC */
bool battery_port_set_cell(int mah);
/* rails (2026-09-11, from the board schematic): DCDC1 is VCC3V3 - the ESP32,
 * flash, PSRAM, panel (VCI + VDDIO), touch, expander, IMU, SD, the codec's
 * digital side and the speaker amp - and RTCLDO feeds the RTC; both are
 * untouchable. ALDO1 is A3V3, the codec's ANALOG supply and the microphone:
 * unused by the tank. DCDC2/3/4 (no inductor fitted), ALDO2/3/4, BLDO1/2,
 * CPUSLDO and both DLDOs end at the PMIC's pins with no consumer, yet the
 * power-on defaults leave all of them switched on. battery_port_trim_rails
 * turns the unused ones off at boot; battery_port_set_rail is the director's
 * per-rail switch for experiments (refuses dcdc1 and anything unknown). */
bool battery_port_set_rail(const char *name, bool on);
void battery_port_trim_rails(void);
/* the round 1.75C (2026-10-01): its ES7210 microphone ADC has EVERY supply on
 * A3V3 - the digital and I/O pins too - and with ALDO1 off the dead chip
 * clamps the I2C bus it shares with the PMIC, the touch and the IMU (found at
 * the first boot: the trim took the bus down with the rail). A pinned rail
 * is switched on and stays on: set_rail(name, false) leaves it and says ok.
 * The watch (2.06) pins two: ALDO1 for the same ES7210, and ALDO2 - its
 * panel's power enable is pulled up to that rail (board_pins.h). */
void battery_port_pin_rail(const char *name);

#endif
