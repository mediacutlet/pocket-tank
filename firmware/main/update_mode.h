/* update_mode.h - update mode on the tank (docs/OTA.md, 2026-09-30).
 *
 * The settings page's CHECK FOR UPDATES does not start the radio: the tank
 * saves, main.c calls update_mode_request, and the board restarts. At the
 * top of app_main, once the display, the glass and the PMIC are up and
 * before anything else exists, update_mode_pending says whether this boot
 * was asked for; update_mode_run then owns the board - the pages in
 * common/update.c over net_port_esp.c - until the keeper is done. It
 * returns when the outcome is "back to the tank" (the radio is already
 * off) and never returns on an install (it restarts into the new slot).
 *
 * The request rides in RTC memory (RTC_NOINIT, the batlog's pattern: a
 * magic word and its complement), so it survives the software restart and
 * nothing else - a power-off clears it, and it can never get stuck. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
void update_mode_request(void);            /* save the tank FIRST; this restarts the board */
bool update_mode_pending(void);            /* at boot: asked for? (reads and clears the word) */
void update_mode_run(uint16_t *fb, int way_up);   /* the loop; returns on BACK, restarts on an install */
/* way_up (0.3.9.3, iamsebastijan: "screen rotation doesn't work in the Updates
 * view"): the way up the tank showed before the restart - its quarter turns
 * (tank_orient), + UPD_WAY_FIXED when it was not the IMU's to change (the
 * ROTATION lock, the watch's SCREEN setting). Unfixed, the pages turn with
 * the IMU as the tank does; main.c keeps it in NVS ("uq") */
#define UPD_WAY_FIXED 4
void update_mode_hold_way(int q);          /* director `ota flip`: this way up, whatever the IMU says */
/* Provisioning at install (2026-10-03). A boot the installer page caused
 * (a reset over USB) on a tank with no network saved does not start the
 * tank yet: the glass stays black, the radio is free, and the page's Wi-Fi
 * step gets the real network list and a real connection test. The same
 * mode answers the page's Connect to / Change Wi-Fi on a RUNNING tank: the
 * tank saves and restarts into it (provision_mode_request). It ends when
 * a network is stored, when the page never asks (PROVISION_WAIT_S), when it
 * goes quiet (the keeper skipped it: PROVISION_IDLE_S once its form has
 * opened, PROVISION_OPEN_S before that), or at a touch on
 * the glass. Returns if the radio was never used; restarts otherwise. */
void provision_mode_request(void);         /* a running tank was asked for the networks: save FIRST; this restarts into the mode, which answers that scan */
bool provision_mode_wanted(void);
void provision_mode_run(uint16_t *fb);
