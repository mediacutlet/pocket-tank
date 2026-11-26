/* radio_hosted.h — the home tank's radio chip, from the P4's side
 * (CONFIG_POCKET_TANK_RADIO_HOSTED, 2026-10-13; docs/board-p4-10.1.md, "The
 * radio"). The ESP32-C6 beside the P4 is reached through Espressif's hosted
 * stack; it comes up the first time something asks (the link, `radio`), not
 * at boot. link_port_hosted.c is the tank link over it; this is the stack's
 * bring-up, the director's `radio` command and the way OUR firmware gets into
 * the C6: staged in the P4's own flash by tools/c6_stage.sh, pushed across
 * the bus by `radio c6 update`. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tank.h"
#include "now_relay.h"

void radio_hosted_boot(void);               /* at boot: greet a C6 that was just updated (the C6's guard waits for it) */
bool radio_hosted_up(void);                 /* the hosted stack up and the C6 answering (idempotent) */
/* why the last radio_hosted_up() said no (0.4.3, 11-23), for LINK's answer on the glass: NO_ROOM = the tank had too
 * little internal RAM to start the stack, so it was not started (radio_mem.h; before this hosted's own assert
 * restarted the tank); NO_START = the stack would not start; NO_ANSWER = the C6 did not answer over SDIO. */
typedef enum { RADIO_OK, RADIO_NO_ROOM, RADIO_NO_START, RADIO_NO_ANSWER } radio_why_t;
radio_why_t radio_hosted_why(void);
bool radio_hosted_alive(void);              /* up, and its bus has not failed since (audit 11-23) */
/* (11-24 audit) the radio is down for the boot: hosted's failure event, or the link counting requests the C6 never
   answered (hosted 3.1.0 can stop receiving without the event). radio_hosted_up() says no at once after it. */
void radio_hosted_mark_dead(const char *why);
typedef struct { uint32_t major, minor, patch; } radio_fwver_t;
radio_fwver_t radio_hosted_ver(void);       /* the C6's hosted firmware, as it said at the bring-up (0.0.0: it did not say) */
bool radio_hosted_ver_said(void);           /* ... it said it (0.0.0 and false: it did not answer that request) */
/* one lock for the whole radio (radio_up.c): the stack's bring-up, Wi-Fi's start, every ask of the relay and each of the
   director's `radio` commands (the scan, the C6 update) run under it, so the link's radio task, the C6 greeting and the
   console never interleave (11-24 audit, finding 8). The tank task never takes it: everything that waits on the C6
   runs on the link's radio task or the console's radio task (radio_hosted.c). */
void radio_lock_init(void);                 /* at boot (radio_hosted_boot) */
void radio_lock(void);
void radio_unlock(void);
/* Wi-Fi through the hosted stack, station mode, no network: started ONCE and left up for the
   boot - the scan and the link share it. (2026-10-20: esp_wifi_deinit and then esp_wifi_init
   again left it unable to start, ESP_ERR_WIFI_NOT_INIT: the link worked once per boot, and
   never after a `radio scan`.) ESP-NOW itself goes up and down in the C6's relay. */
bool radio_hosted_wifi(void);
/* send the C6's relay a message and wait for its NR_MSG_STATE (link_port_hosted.c); false = no relay answered */
bool radio_hosted_ask(uint32_t msg, const void *data, size_t len, nr_state_t *out, int wait_ms);
void radio_hosted_cmd(tank_t *t, int argc, char **argv);   /* director: radio | radio mem | radio scan | radio c6 [update] */

/* the C6 image staged in the P4's flash, a 64-byte header (c6_stage.h, v2 since the one install) then the image (at most
 * 1.5 MB), on BOTH big screens in the
 * app slot the tank does not run from - ota_1 (0xAA0000, 2.5 MB): a USB install always boots ota_0, and the big
 * screens have no Wi-Fi update yet to move it. Until 11-23 the Tab5 staged 1 MB into "storage" (MY TANKS' shelf from
 * 64 KB in: a full shelf lost places 41..104 - tools/test_c6_stage.py) and the 10.1 at 0x1000000, past its table on
 * a 32 MB chip: fine for esptool, but the crew's install page writes the stage through the browser's flasher, and a
 * write past 16 MB there is unproven - wrapped to 0x0 it would take the bootloader, the table and the tank with it.
 * tools/c6_stage.sh refuses a board that boots ota_1; stage_read refuses a stage that is the running app. */
#define C6_STAGE_OFFSET 0xAA0000u
#include "c6_stage.h"

/* THE HANDOVER (0.4.3, the one install; Strato 11-24 "automatic"): the staged relay goes into the C6 by itself. Every
 * decision is c6_ho.h's c6_ho_step (checked exhaustively on the Mac: firmware/host_test/c6_ho_check.c); radio_hosted.c
 * keeps its state, gathers the facts and does what it answers, on a task of its own for the check and the write. The
 * states (radio_hosted_handover_state, HO_*):
 *   CHECKING  the stage is read and verified, the C6 brought up and asked its relay's version (three pings)
 *   READY     needed: the task waits (up to 2 minutes) for the tank task's tick to find a bare glass - then UPDATING
 *   WRITING   the update is going into the C6's spare slot (pct): UPDATING YOUR DEVICE
 *   WRITTEN   the write reached the C6's end: the tank saves and both chips restart; the next boot greets the C6
 *   FAIL      the write stopped before the end: the C6 kept its old firmware - THE LINK UPDATE STOPPED
 *   NOT_NEEDED / NO_IMAGE / NO_ANSWER / NOT_NOW / REFUSED  nothing written (c6_ho.h)
 * The record is kept in NVS ("c6rec") and printed by radio_hosted_report(). */
int  radio_hosted_ho_tick(bool bare, bool idle, bool flow, bool card);   /* the tank task, every poll: C6A_* (cards, LINK, RESTART) */
int  radio_hosted_ho_link(void);            /* LINK tapped with the radio down: C6A_CHECK_WAIT / CARD_WAIT / CARD_UPDATING / LINK */
int  radio_hosted_ho_esc_abort(void);       /* the tank would not save at the chain's restart: C6A_CARD_NOSTART, the chain called off */
int  radio_hosted_ho_link_failed(void);     /* LINK's start failed: C6A_CARD_OUT (the update could not reach this C6) or NONE */
int  radio_hosted_handover_state(int *pct);
/* the end of a finished write (the audit's M4, esp-hosted-mcu#240: the P4's SDMMC can stay stuck across esp_restart):
   the C6 held in reset, then a full system reset of the P4 (esp_rom_software_reset_system) - never returns */
void radio_hosted_restart_clean(void);
/* the radio chip as last seen, from NVS (no bring-up): one block in the log - the director's `state` and `radio report`,
   so the help page's USB tank check carries it */
void radio_hosted_report(void);
