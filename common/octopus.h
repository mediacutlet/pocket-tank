/* THE OCTOPUS, a sim prototype (octopus.c, docs/octopus.md). One static animal, not in tank_t, not saved. */
#pragma once
#include "octo.h"

void octopus_proto_set(int on, int growth, float x, float floor, float xlo, float xhi);   /* growth 0..255: baby .. adult (twice the baby) */
int  octopus_proto_on(void);
void octopus_tick(float dt);
const octo_grid_t *octopus_grid(void);          /* its picture as it stands (rebuilt only as often as it needs) */
void octopus_cmd(int what, int dir);            /* 0 rest, 1 crawl, 2 flee, 3 go home, 4 come out, 5 match now, 6 a find now; dir -1 / +1 */
void octopus_think(int on);                     /* its own wandering on or off (a stage holds it still) */
float octopus_x(void);
float octopus_size(void);
int  octopus_builds(void);
unsigned octopus_debug_sack_key(int qf, int qb, int qt, int qs);   /* (the sack-replay study) */
unsigned octopus_picture_serial(void);           /* never reset: which picture is drawn */
void octopus_set_async(int on);                  /* the raster on the second core (the board): */
int  octopus_async_prep(void);                   /*   core 0, after the draw: 1 = a build is owed */
void octopus_async_build(void);                  /*   core 1, while the frame is sent; core 0 waits for it before the next tick */
void octopus_set_async_wait(void (*wait)(void)); /*   (the board, audit FW-1) core 0's wait for core 1's raster: a portrait built on core 0 waits first */
extern long octo_perf_us[6];                     /* (a board's stopwatch, `octopus perf`: [0] its tick, [1] its picture built, [2] its picture drawn, [3] frames, [4] its raster on core 1, [5] the render core's wait for it) */
void octopus_set_hz(float hz);                   /* how often it is rebuilt while it moves (0 = every frame): an A/B */
void octopus_set_fav(int item);
int  octopus_fav(void);                          /* the opening it favors: 2 the castle, 9 the volcano */
void octopus_den(int item, float x, float floor_y, int front, float half, int floor_clear);   /* the tank, each tick: its den (-1 none), the opening's middle, the floor inside,
                                                    whether that piece stands IN FRONT and its half width; and whether the sand it is on is clear of every piece IN FRONT */
int  octopus_idle(void);
int  octopus_bound(void);                        /* on its way home or to the crab's door: it keeps no distance then */                         /* at rest on the sand with nothing in hand */
void octopus_goto(float x);                      /* (idle only) a walk to x on the sand */
void octopus_backoff(int dir, float far);        /* startled: far px away to dir, then home if it has one */
void octopus_rival(float x, float gap, int visiting);   /* the tank, each tick: the crab out on the sand (-1: none), the gap to keep, a look at its door under way */
void octopus_front_shelter(int any, float x, float half, float clear_x);   /* ... and the nearest sand clear of every piece IN FRONT */              /* the tank, each tick: a shelter (castle, volcano) stands IN FRONT - it keeps to its front lane */
void octopus_presence(int here, int crab_coin);  /* the tank, each tick: the keeper is here (the tank handled lately); the crab holds a coin up */
int  octopus_coin(float *x, float *y);           /* its sand dollar held up: where (0: none) */
int  octopus_tap(float x, float y);              /* a tap: the sand dollars taken (5), or 0 */
int  octopus_found(void);
int  octopus_hit(float x, float y);              /* a tap lands on its body */
float octopus_find_wait(void);                   /* its next find's wait (0: one is up or being dug; -1: no body) */
void octopus_set_find_wait(float s);
int  octopus_take(void);                         /* its coin taken from its card: 5, or 0 */
const octo_grid_t *octopus_portrait(int growth, int *mx, int *floor_y);   /* its picture for a card or a jar, and the sand under its middle */
void octopus_grow(int growth);                   /* its size, in place (0..255) */
void octopus_sleep(float seconds);               /* the tank slept this long */
int  octopus_cue(void);                           /* once each: 1 its coin came up, 2 its coin was taken (the tank plays the crab's sounds) */
float octopus_find_left(void);
float octopus_digging(void);
float octopus_camo(void);                        /* 0 its own colours .. 1 matched to what it sits at */
void octopus_night(int night);
float octopus_reach(void);
float octopus_mlen(void);
float octopus_floor(void);
int  octopus_lane_front(void);                   /* on the sand: 1 its front lane (before the pieces IN FRONT), 0 its own line behind them */
int  octopus_from_behind(void);
void octopus_hide(int hide);
int  octopus_den_item(void);
void octopus_den_leave(void);                    /* (the tank) its den is to change while it is in it: out of this one first */
void octopus_debug_no_gather(int on);
void octopus_debug_glide(int on);
float octopus_gath(void);
float octopus_den_depth(void);                   /* the den's shade on it, 0..1 over 0.6 s once in (the crab's cr_inn) */                        /* 0..1 its arms drawn in to pass a door */
float octopus_span(void);
float octopus_inn(void);                         /* 0 out on the sand .. 1 inside its den */
float octopus_y(void);
int  octopus_mode(void);                        /* 0 rest, 1 crawl, 2 gather, 3 jet */
