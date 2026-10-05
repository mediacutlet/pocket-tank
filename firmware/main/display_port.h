/* display_port.h — the ONLY platform-specific seam for the renderer.
 * The tank renders into an RGB565 buffer (common/render.c); this port ships
 * it to the panel. QEMU/bring-up: stub. Track 4: SH8601 over QSPI (V1 board)
 * or CO5300 (V2), rotated 90 degrees so the tank is landscape 448x368. */
#ifndef DISPLAY_PORT_H
#define DISPLAY_PORT_H
#include <stdint.h>
#include <stdbool.h>

bool display_port_init(void);
/* push a full TANK_W x TANK_H RGB565 frame; may return before DMA completes */
void display_port_flush(const uint16_t *fb);
void display_port_flush_prof(int64_t *wait_us, int64_t *send_us);   /* since the last call: waiting on the wire, inside the send */
/* power the panel down for device sleep; display_port_wake (or a boot's
 * display_port_init) re-sequences it */
void display_port_sleep(void);
/* re-power and re-init the panel after display_port_sleep, without reboot */
void display_port_wake(void);
/* true = present the frame rotated 180 degrees (device held upside down) */
void display_port_set_inverted(bool inverted);
/* panel brightness 0..255 (DCS 0x51; the init sequence starts at 255). Kept
 * across display_port_wake, which re-inits the panel. */
void    display_port_set_brightness(uint8_t level);
uint8_t display_port_brightness(void);
/* the ROUND board (Waveshare 1.75C, 466 px across), known once
 * display_port_init has probed the I2C bus. The tank is still the 448x368
 * frame; the port presents it on the round glass one of two ways:
 *   FIT  - 4 panel px for 5 of the tank's (area-filtered): 360x296, the whole
 *          frame inside the circle, corners and all
 *   FULL - px for px, centered: the circle crops the frame's four corners
 * display_port_panel_to_tank turns a touch on the panel into the tank px
 * under it as if the picture were upright (the view included; the touch port
 * turns it after its calibration, 2026-10-02; it may land outside the frame -
 * the caller clamps). */
enum { DISPLAY_VIEW_FIT, DISPLAY_VIEW_FULL };
bool board_is_round(void);
/* the round board before a deep sleep: the panel's reset held high (it stays
 * in its sleep-in or deep standby), the touch chip's held low (or high, asleep
 * by its own command), the panel's chip select held
 * high - every other digital pad is isolated by the deep-sleep hold. A boot's
 * display_port_init lets them go. No-op on the 1.8 (its expander holds them). */
void display_port_deep_sleep_pins(bool tp_awake_high);   /* tp_awake_high: the touch chip took its own sleep - hold its reset HIGH, not low */
void display_port_deep_sleep_bus(void);   /* the round board: the QSPI clock + data lines driven low and held - a powered panel's inputs never float */
void display_port_deep_standby(void);   /* the CO5300's 4Fh deep standby (~3 uA vs sleep-in's ~135); only a reset ends it */
bool board_has_expander(void);                  /* the 1.8, positively: its IO expander answered */
/* the WATCH (Waveshare 2.06, 410 x 502 portrait), with GPIO resets like the
 * round board's. Its own build (TANK_WATCH) is a portrait tank that IS the
 * panel, sent unturned; the rectangle image still runs on it the 1.8's way -
 * its 448 x 368 frame turned 90 degrees, centred in the glass. */
bool board_is_watch(void);
/* the 2.16 (480 x 480 square, BOARD_SQ216): board_is_round() is true on it
 * too - the round board's path, with the frame px for px on a bigger square */
bool board_is_sq216(void);
int  board_round_panel(void);
void display_port_set_rotation(int quarter);    /* the 2.16: the picture turned 0..3 quarters clockwise, by the panel (MADCTL) */                   /* the round path's square panel, px across: 466, or 480 on the 2.16 */
int  board_pwr_sense_pin(void);                 /* the GPIO that is high while the PWR key is down (the 1.75C: 3, the watch: 10), -1 = none */
/* a portrait panel bigger than the frame (the rectangle image on the watch):
 * where the frame's corner sits on the panel, in panel px (0, 0 otherwise) */
void display_port_frame_origin(int *px, int *py);
void display_port_set_view(int view);
int  display_port_view(void);
void display_port_panel_to_tank(int px, int py, float *tx, float *ty);
#endif
