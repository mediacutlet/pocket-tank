/* director.h — serial "director" console: scenario setup on cue (filming,
 * bench). Lines typed on the USB serial port (the same port the log comes
 * out of) become tank commands: `hungry 3`, `feed`, `algae 40`, ... Type
 * `help` for the list. The tank owns nothing new: the console only sets
 * state the tank already has and the advisor still decides what fish do. */
#pragma once
#include "tank.h"
void director_init(void);
void director_early(void);       /* the top of app_main: the console + Improv answered through the boot (the installer page's 1.5 s) */
void director_poll(tank_t *t);   /* once per frame, from the tank task; t = NULL in update mode (wifi / ota commands only) */
/* Improv with the radio: provisioning mode right after an install (update_mode.c provision_mode_run) */
void device_provision_request(void);   /* main.c: save, restart into provisioning mode (the page asked a running tank for the networks) */
void director_provision_begin(void);
void director_provision_scan(void);        /* the scan the page asked for before the restart: start it, the list follows */
void director_provision_end(void);
void director_provision_tick(void);        /* the scan's list, the connect's answer */
bool director_provision_form(void);        /* the page's Wi-Fi form has opened: a scan or a network came */
bool director_provision_done(void);        /* a network was connected to and stored */
bool director_provision_busy(void);        /* a scan or a connect is running */
int64_t director_provision_last_us(void);  /* the page's last packet (esp_timer time; 0 = none) */
void device_sleep(int wake_after_s);   /* main.c: the keeper's sleep (0: grace then power-off) or, N > 0, a 5 s grace then deep sleep with an N s timer wake; in the screen and lightsleep modes the dark (N > 0: lit again after N s), in none nothing */
void device_fake_battery(int pct, int state);   /* main.c: the gauge reads pct% with the cable in state BAT_* (battery.h) for the pill, the battery page and the low-battery rule (b-roll); pct < 0 = the real gauge again. Not saved */
void device_battery_log(void);          /* main.c: the battery page's numbers and the learned rates, to the log */
int  device_sleep_cfg(int mask);       /* main.c: what the round board's deep sleep turns off (1 touch chip, 2 panel deep standby, 4 IMU clock, 8 QSPI lines held low); mask < 0 reads it; kept in NVS */
void device_clock_log(void);           /* main.c: the clock - RTC chip or the internet's time (the clockless night), and what tonight will be */
void device_poweroff(void);            /* main.c: save + PMIC cut now */
void device_update_check(void);        /* main.c: as CHECK FOR UPDATES - save, restart into update mode (docs/OTA.md) */
