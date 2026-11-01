/* boot_show.h - THE BOOT SHOW (2026-10-31): a tank's first seconds after a start or a SWAP IN, while its grass loop is
 * first baked - the tank built up before the keeper's eyes instead of a loading screen. See boot_show.c. */
#ifndef BOOT_SHOW_H
#define BOOT_SHOW_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tank.h"

/* starts it over the tank's current scene (render_scene_buf: a tank frame must have been drawn), in its SWAP IN form
 * (fill: the water rises) or a wake's (the light comes up). Its memory - a frame and half a byte a pixel - from alloc;
 * false: none to spare (the platform shows render_loading instead) */
bool boot_show_begin(const tank_t *t, bool fill, void *(*alloc)(size_t), void (*release)(void *));
bool boot_show_on(void);
/* the frame at ts seconds into it, over the whole of fb (stride TANK_W). loop_whole: the grass loop is baked;
 * bake_frac: how much of it is (0..1). false once it is over: the tank is the tank (boot_show_end) */
bool boot_show_frame(const tank_t *t, uint16_t *fb, float ts, bool loop_whole, float bake_frac);
void boot_show_end(void);

#endif
