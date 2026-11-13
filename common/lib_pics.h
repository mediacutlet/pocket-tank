/* lib_pics.h - THE LIBRARY's theme pictures, made a little each frame (0.4.2, Strato 11-10: "the library takes 2-3
 * seconds to load").
 *
 * Until 0.4.2 the page made all three of the jelly tank's theme pictures before it drew its first frame. Now the page
 * draws at once with an empty picture frame, and a picture is made behind it: its tank is set up in one frame (the
 * first also builds the engine's scratch), ticked a few steps a frame inside a time budget, then drawn and shrunk in a
 * frame of its own. Only the pictures the page shows are made (the
 * list: theme 0; PICK A THEME: all three, each appearing as it is ready). The pictures are the ones 0.4.1 drew, pixel
 * for pixel: the same tank, the same LIB_PIC_TICKS steps, the same render.
 *
 * One block of the caller's memory (PSRAM on the device) holds everything: lib_pics_bytes() of it, made ready by
 * lib_pics_init (it need not be zeroed).
 * The sim and the firmware both make the pictures here, so they cannot drift apart. */
#ifndef LIB_PICS_H
#define LIB_PICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "jelly.h"

#define LIB_PIC_TICKS  40        /* a theme's tank after 4 s: 40 steps of LIB_PIC_DT, as 0.4.1 made it */
#define LIB_PIC_DT     0.1f
#define LIB_PIC_BUDGET_US 8000   /* a frame's share of the ticking: ~1-2 ms a tick on the 1.8, so 4-8 a frame and the page near its own rate */

typedef struct {
    uint8_t  have;               /* bit k: theme k's picture is made */
    int8_t   cur;                /* the theme on the stage (-1: none) */
    uint8_t  ticks;              /* its steps so far */
    uint16_t slices;             /* frames that worked on it */
    uint32_t work_us;            /* what they spent */
    int64_t  began_us;           /* when it went on the stage */
    jelly_tank_t stage;          /* the tank being ticked */
    uint16_t pic[];              /* JELLY_THEME_N pictures, render_shelf_thumb_size each */
} lib_pics_t;

size_t lib_pics_bytes(void);
void   lib_pics_init(lib_pics_t *p);                           /* a fresh block: no picture, nothing on the stage */
const uint16_t *lib_pics_get(const lib_pics_t *p, int theme);  /* a made picture, else NULL (p NULL: NULL) */
unsigned lib_pics_waiting(const lib_pics_t *p, unsigned want); /* the wanted themes (bit k) not made yet (p NULL: 0) */
void   lib_pics_rest(lib_pics_t *p);                           /* the page left: the half-ticked tank is dropped (the made pictures stay) */
/* One frame's work on the lowest wanted theme not made yet. `frame` is a whole frame of scratch (TANK_W x TANK_H, stride
 * TANK_W): it is drawn over only by the call that finishes a picture, which returns that theme (else -1) and, through
 * work_ms / slices / wall_ms (each may be NULL), what the picture cost. A call sets the theme's tank up, or ticks it until
 * budget_us is spent (one tick at least), or draws it: never two of these. */
int    lib_pics_step(lib_pics_t *p, unsigned want, uint16_t *frame, int64_t (*now_us)(void), int budget_us,
                     int *work_ms, int *slices, int *wall_ms);

#endif
