/* sleep_setting.h - (this fork) the keeper's choice of what a 320x240 board
 * - the CYD, the Touch-LCD-2 - does when something asks it to sleep: BOOT's
 * short press, the face-down gesture, the PWR key where there is one, the
 * director's deepsleep and poweroff (2026-10-08). The settings page's SLEEP
 * row sets it - NEVER, SCREEN or LIGHT, the numbers SET_SLEEP_* (render.h) -
 * and it is kept in NVS beside the brightness ("tank"/"sleep") and re-saved
 * after a tank reset erases NVS - but only a choice the keeper made: the
 * build's default is never written, so a later build with another default
 * still brings its own. With nothing saved it is the build's Kconfig sleep
 * mode, which is therefore only the factory default: none is NEVER, screen
 * is SCREEN, lightsleep is LIGHT.
 *
 * deepsleep is a build choice only. A 320x240 board built for it has no
 * row, and neither has an AMOLED board (they choose at build time, as
 * upstream does): there SLEEP_SETTING_ROW is not defined, sleep_setting() is
 * -1 and the rest does nothing. The row was the CYD's alone until
 * 2026-10-08; it is every 320x240 board's since, the dark with it. */
#ifndef SLEEP_SETTING_H
#define SLEEP_SETTING_H
#include <stdbool.h>
#include "sdkconfig.h"
/* a 320x240 board built for none, screen or lightsleep: the SLEEP row
   exists, and main.c compiles the dark (enter_dark) for it whatever the
   build's mode */
#if defined(CONFIG_POCKET_TANK_320X240) && (CONFIG_POCKET_TANK_SLEEP_NONE || CONFIG_POCKET_TANK_SLEEP_SCREEN || CONFIG_POCKET_TANK_SLEEP_LIGHT)
#define SLEEP_SETTING_ROW 1
#endif
void sleep_setting_init(void);           /* after nvs_flash_init */
int  sleep_setting(void);                /* SET_SLEEP_* (render.h); -1 where there is no row */
bool sleep_setting_saved(void);          /* the choice was read from NVS (or made since), not the build's default */
void sleep_setting_set(int choice);      /* one of SET_SLEEP_*; saved */
void sleep_setting_save(void);           /* re-save after an NVS erase (reset), if the keeper chose one */
#endif
