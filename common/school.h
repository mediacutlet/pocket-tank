/* school.h - THE SCHOOL TANK, an experiment (2026-11-01, docs/school-tank.md).
 *
 * A world of its own beside the fish tank, like the jelly spike: it shares
 * the board geometry (TANK_W / TANK_H and the glass) and nothing else - no
 * brain, no save. A school of tiny fish, each one steering only by what its
 * nearest few neighbors are doing (a topological flock: the seven nearest,
 * as starlings and herring do), plus its own small goals - a crumb of food
 * it can see, a spot a few "scouts" set out for, the glass ahead, a tap that
 * frightens it. What the school does as a whole is never decided anywhere;
 * it ripples out of those local choices.
 *
 * Every distance is in body lengths (BL) of the glass's fish, every time in
 * seconds, so a school's spacing and a fright's wave hold on every board
 * (BOARDS.md rule 5).
 */
#ifndef POCKET_TANK_SCHOOL_H
#define POCKET_TANK_SCHOOL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "tank.h"                  /* TANK_W / TANK_H and the board's glass */

#define SCHOOL_MAX      640        /* array bound; the per-board count is the experiment's question */
#define SCHOOL_K        7          /* neighbors each fish attends to */
#define SCHOOL_PELLETS  48
#define SCHOOL_RIPPLES  4

/* the scale: the glass's shorter side over the 1.8's (368 px) */
#define SCHOOL_SHORT ((TANK_W) < (TANK_H) ? (TANK_W) : (TANK_H))
#define SCHOOL_S     ((float)SCHOOL_SHORT / 368.0f)

typedef struct {
    float    x, y, z;              /* px; z is depth, 0 far .. SCHOOL_DEPTH near */
    float    vx, vy, vz;           /* px/s */
    float    cruise;               /* this fish's own easy pace, px/s */
    float    size;                 /* body length, px (at middle depth) */
    float    phase;                /* the tail beat, 0..1 */
    float    fear;                 /* 0..1: frightened (decays) */
    float    burst;                /* 0..1: the escape dash (decays fast) */
    float    startle_in;           /* > 0: seconds until a startle copied from a neighbor fires */
    float    sx, sy;               /* that startle's direction */
    float    refractory;           /* seconds before it can be startled again */
    float    regroup;              /* 0..1: the pull back to the crowd after a fright (decays slowly) */
    float    appetite;             /* 0..1 */
    float    glint;                /* 0..1: the flank catching the light as it turns */
    float    heading;              /* screen angle of travel, for the turn rate (glint) */
    float    wander;               /* its own slow wandering angle offset */
    float    goal_t;               /* > 0: a scout - seconds left heading for (gx, gy) */
    float    gx, gy;
    float    zbias;                /* the depth it drifts toward */
    float    kth2;                 /* last tick's squared distance to its Kth neighbor (the scan's head start) */
    uint16_t nn;                   /* neighbors seen this tick (for the regroup bias / tests) */
    uint8_t  hue;                  /* small per-fish tint step */
} sfish_t;

typedef struct {
    float x, y, z, vy, rest;       /* rest: seconds lying on the sand */
    bool  live;
} spellet_t;

typedef struct { float x, y, t; bool live; } sripple_t;

typedef struct {
    int       n;
    sfish_t   f[SCHOOL_MAX];
    spellet_t p[SCHOOL_PELLETS];
    sripple_t r[SCHOOL_RIPPLES];
    float     t;                   /* seconds since init, lapped every SCHOOL_CLOCK_LAP */
    uint32_t  ticks;
    float     scout_t;             /* seconds to the next scouting idea */
    bool      holding;             /* a finger held on the glass: curiosity */
    bool      dragging;            /* a finger moving through the water: the school parts round it */
    float     drag_x, drag_y, drag_vx, drag_vy;   /* where it is, px; how it moves, px/s */
    float     hold_x, hold_y, hold_t, hold_dir;   /* hold_dir: which way the mill turns (+1 / -1) */
    bool      light_on;
    uint32_t  rng;
    /* tunables (the sim's keys move them) */
    float     w_align, w_cohere, w_separate, w_gather, w_wall;
    float     body_bl;             /* body length on this glass, px */
    /* counters for the tests */
    int       eaten, startles;
} school_t;

void  school_init(school_t *s, uint32_t seed, int n);
void  school_set_count(school_t *s, int n);              /* add or remove fish (new ones join the crowd) */
void  school_tick(school_t *s, float dt);
void  school_render(const school_t *s, uint16_t *fb, int stride);
void  school_tap(school_t *s, float x, float y);         /* a tap on the glass: a fright where it lands */
void  school_feed(school_t *s, float x);                 /* a pinch of food at the surface above x */
void  school_hold(school_t *s, float x, float y, bool down);  /* a held finger: the school grows curious */
void  school_drag(school_t *s, float x, float y, float vx, float vy, bool down);   /* a finger dragged through the water */
void  school_touch(school_t *s, float x, float y, bool down, double now);  /* the gestures from raw touch */

float school_glass_dist(float x, float y);               /* px inside the water (< 0 outside) */
void  school_set_bg_cache(uint16_t *buf);                /* the baked water (PSRAM on the device; NULL = draw each frame) */
const uint16_t *school_bg_buf(const school_t *s);        /* the baked water if ready, for the prefetch */
void  school_fb_primed(const uint16_t *fb);                /* fb already holds the baked water (the device's DMA prefetch): the next render skips the copy */
void  school_set_scratch(void *buf);                     /* the grid + tables (PSRAM on the device; NULL = calloc) */
size_t school_scratch_bytes(void);

/* the shape of the crowd, for the tests and the sim's readout */
typedef struct {
    int   groups;                  /* clusters (fish within 2.5 BL chain together) */
    int   biggest;                 /* fish in the biggest one */
    float polar;                   /* 0..1: how much they all head one way */
    float nnd_bl;                  /* median nearest-neighbor distance, body lengths */
} school_shape_t;
school_shape_t school_shape(const school_t *s);

/* profiling (the sim's bench, the device's display line): microseconds per stage */
extern int64_t (*school_clock_us)(void);
extern int64_t  school_prof_us[6];  /* 0 grid, 1 steer, 2 move, 3 bg, 4 fish, 5 extras */

#endif
