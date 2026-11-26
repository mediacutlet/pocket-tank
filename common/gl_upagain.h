/* THE LOOP PLANNED UP AGAIN's decision (firmware main.c, 0.4; a pure function since 0.4.2 so the sim tests it - GRAPHICS MEDIUM's review,
 * 11-08: at MEDIUM it never fired - the plan's moments against the shape's ready, always short - and the night picture's room, taken at
 * night, set its baseline to the night's 8 moments). A plan that came out short of the board's moments (goal) is planned again once PSRAM
 * free is GAIN more than when it was made and the tank has been bare with the DAY's loop whole for a minute. Plans are counted by the
 * render (render_grass_loop_plans: a plan made, not an allocation), the moments are the day's (render_grass_loop_day_moments), never
 * while the night picture is in use, never at LOW (one band: a short plan shows nothing, as before) */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define GL_UP_GAIN  (128u * 1024)
#define GL_UP_QUIET 60000000LL                   /* (us the conditions hold first) */
typedef struct { int plans_seen, n, from, said_from; bool spent; size_t free_at; int64_t quiet; } gl_up_t;
typedef struct {
    int plans;                  /* render_grass_loop_plans() */
    int planned, shape, ready;  /* render_grass_loop_day_moments() */
    bool night, on, low, busy;  /* MEDIUM's night (gl_up_night); the loop on; GRAPHICS LOW; loading, a page, the setup, a prompt */
    size_t plan_free, ps_free;  /* PSRAM free after the plan's last allocation, and now */
    int64_t now; int goal;      /* us; the board's moments (GRASS_LOOP_MOMENTS) */
} gl_up_in_t;
/* MEDIUM's night, whether its night picture is in use or not (refused, or outgrown and waiting): a replan then lets the day's loop go and
   the light-on rebakes it - MEDIUM never does that (11-08 delta review). HIGH's night as ever: HIGH plans again by night as before */
static inline bool gl_up_night(bool picture_live, bool tank_night, bool medium) { return picture_live || (tank_night && medium); }
#define GL_UP_PLANNED 1         /* a plan was made since the last call after a retry (s->said_from: the moments before; s->spent: no gain) */
#define GL_UP_NOW     2         /* plan it again now */
static inline int gl_up_step(gl_up_t *s, const gl_up_in_t *in) {
    int r = 0;
    if (in->plans != s->plans_seen && in->planned > 0 && !in->night) {   /* a plan was made (taken by day: the night's room is not a plan) */
        s->plans_seen = in->plans; s->free_at = in->plan_free;
        if (s->from) { s->spent = in->planned <= s->from; s->said_from = s->from; r |= GL_UP_PLANNED; }
        else s->spent = false;
        s->from = 0; s->n = in->planned;
    }
    if (in->busy || in->night || in->low || s->spent || s->n >= in->goal || in->planned != s->n || in->ps_free < s->free_at + GL_UP_GAIN
        || !in->on || in->planned <= 0 || in->ready < in->shape) s->quiet = in->now;
    else if (in->now - s->quiet > GL_UP_QUIET) { s->quiet = in->now; s->from = in->planned; r |= GL_UP_NOW; }
    return r;
}
