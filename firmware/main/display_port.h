/* display_port.h — the ONLY platform-specific seam for the renderer.
 * The tank renders into an RGB565 buffer (common/render.c); this port ships
 * it to the panel. QEMU/bring-up: stub. Track 4: SH8601 over QSPI (V1 board)
 * or CO5300 (V2), rotated 90 degrees so the tank is landscape 448x368. */
#ifndef DISPLAY_PORT_H
#define DISPLAY_PORT_H
#include <stdint.h>
#include <stdbool.h>
#include "sdkconfig.h"   /* CONFIG_POCKET_TANK_BOARD_4B for the glass-geometry gate below */

/* 4B glass geometry (only for the 4B build; the S3 port takes PANEL_W/PANEL_H
 * from s3/board_pins.h, so these are gated off there). The 448x368 landscape tank
 * is SCALED to FILL the whole 720x720 square glass (no letterbox; the tank
 * aspect is stretched to the square - 720/448 x-scale, 720/368 y-scale). The
 * touch port maps screen coords through the SAME geometry (it defines the
 * tank-side division, this gives the glass side). Pure numbers so any
 * display_port.h consumer compiles without tank.h. */
#if CONFIG_POCKET_TANK_BOARD_4B
#define PANEL_W 720
#define PANEL_H 720
#define SCALED_W   PANEL_W    /* full glass width  */
#define SCALED_H   PANEL_H    /* full glass height */
#define GLASS_X_OFF 0
#define GLASS_Y_OFF 0
#endif

bool display_port_init(void);
/* push a full TANK_W x TANK_H RGB565 frame; may return before DMA completes */
void display_port_flush(const uint16_t *fb);
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
#endif
