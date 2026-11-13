/* jelly.h - THE JELLY TANK, habitat #2 (docs/jelly-tank-0.6.md).
 *
 * 2026-10-18: the render spike (step 0 of the design). A world of its own
 * beside the fish tank: it shares the board geometry (TANK_W / TANK_H and
 * the glass) and nothing else - no fish, no brain, no save yet. Jellies have
 * no goals: they drift in a slow velocity field, pulse against it, turn
 * toward light and glow.
 *
 * Every distance is a fraction of the glass's shorter side (JELLY_S), every
 * time is in seconds, so a lap of the jet or a pulse's push holds on every
 * board (BOARDS.md rule 5).
 */
#ifndef POCKET_TANK_JELLY_H
#define POCKET_TANK_JELLY_H

#include <stdbool.h>
#include <stdint.h>
#include "tank.h"                  /* TANK_W / TANK_H and the board's glass */

#define JELLY_MAX       12         /* array bound; the per-board cap is the design's D1 */
#define JELLY_FIELD_X   8          /* the coarse velocity field over the glass */
#define JELLY_FIELD_Y   8
#define JELLY_SPECKS    48         /* brine shrimp in the water */
#define JELLY_BUBBLES   40         /* the jet's micro-bubbles */
#define JELLY_RIPPLES   4
#define JELLY_TENT_MAX  28         /* tentacles a jelly can have (the moon's fringe) */
#define JELLY_ROPE_N    12         /* points along one tentacle at most (the nettle's) */
#define JELLY_ARM_MAX   8          /* oral arms a jelly can have (the blubber's) */
#define JELLY_ARM_N     7          /* points along one arm */

/* the scale: the glass's shorter side over the 1.8's (368 px) */
#define JELLY_SHORT ((TANK_W) < (TANK_H) ? (TANK_W) : (TANK_H))
#define JELLY_S     ((float)JELLY_SHORT / 368.0f)

enum { JSP_MOON, JSP_NETTLE, JSP_BLUBBER, JSP_N };
enum { JLIGHT_VIOLET, JLIGHT_BLUE, JLIGHT_TEAL, JLIGHT_MAGENTA, JLIGHT_WHITE, JLIGHT_N };
enum { JET_OFF, JET_GENTLE, JET_STEADY };
#define JET_AIMS 5                 /* the jet's directions: along the glass either way, two diagonals, straight in */
#define JELLY_NEON_N 8

extern const char *const JELLY_SPECIES_NAMES[JSP_N];
extern const char *const JELLY_LIGHT_NAMES[JLIGHT_N];
extern const char *const JELLY_NEON_NAMES[JELLY_NEON_N];
extern const uint32_t    JELLY_NEON_RGB[JELLY_NEON_N];

/* a THEME (2026-10-22): a whole tank's look - the LED color, the scatter
   seed and six jellies of a species and a neon each. Strato filmed the three
   side by side on the pendant, the square and the watch for a short and kept
   them to reuse. Director `jelly theme <name>`. */
#define JELLY_THEME_JELLIES 6
typedef struct {
    const char *name;
    uint8_t     light;             /* JLIGHT_* */
    uint32_t    seed;
    uint8_t     mix[JELLY_THEME_JELLIES][2];   /* JSP_*, neon */
} jelly_theme_t;
#define JELLY_THEME_N 3
extern const jelly_theme_t JELLY_THEMES[JELLY_THEME_N];

typedef struct {
    uint8_t  species, neon;
    float    x, y;                 /* the bell's margin center (where the tentacles hang from) */
    float    vx, vy;               /* px/s over the ground */
    float    heading;              /* radians, 0 = bell up, + = clockwise on screen */
    float    spin;                 /* rad/s: the turn has momentum (2026-10-18, Strato: a bell cannot pivot
                                    * on the spot, it has nothing to torque against - it turns as it moves) */
    float    turn_max;             /* this tick's limit on |spin|: speed through the water / turning radius */
    float    target;               /* where the wander wants the heading */
    float    wander_t;             /* seconds to the wander's next pick */
    float    depth;                /* the height it likes to drift at, 0 top .. 1 floor (re-picked with the wander) */
    float    z;                    /* 0 far .. 1 near: size and brightness */
    float    grow;                 /* 0..1 of adult size (a bud starts near 0.35) */
    float    phase, period;        /* the pulse: 0..1 through one beat, seconds per beat */
    float    contract;             /* 0..1 how closed the bell is right now */
    float    flash;                /* bioluminescent burst, decays */
    float    fed;                  /* 0..1, lifts the base glow, decays over hours */
    /* the tentacles as loose strings (2026-10-18, Strato: "they act more like wires than very loose
     * dangling string-like material"): each a chain of points hanging from the margin, every point
     * carrying its own momentum through the water; only the lengths are held */
    float    rope_x[JELLY_TENT_MAX][JELLY_ROPE_N], rope_y[JELLY_TENT_MAX][JELLY_ROPE_N];
    float    rope_px[JELLY_TENT_MAX][JELLY_ROPE_N], rope_py[JELLY_TENT_MAX][JELLY_ROPE_N];
    bool     rope_ready;
    /* the oral arms on strings too (Strato: "the oral arms in the center now look too rigid and stiff
     * ... tentacles dangling down being pulled by gravity and the oral arms nearly perpendicular") */
    float    arm_x[JELLY_ARM_MAX][JELLY_ARM_N], arm_y[JELLY_ARM_MAX][JELLY_ARM_N];
    float    arm_px[JELLY_ARM_MAX][JELLY_ARM_N], arm_py[JELLY_ARM_MAX][JELLY_ARM_N];
    bool     arm_ready;
    uint32_t seed;
} jelly_t;

/* THE CLOCK (0.4.2 audit FW2-1): a float that ran for as long as the board was awake ran fast and slow from a day and
 * a half and stopped at 12 days (2^20 s). It laps every 20000 pi s (~17.5 h): a whole number of turns for the frills'
 * 2.0 and 1.7 and the specks' 1.6 + 0.3 k, so those are seamless, and a second still splits 256 ways. */
#define JELLY_CLOCK_LAP 62831.853f
typedef struct { float x, y, vx, vy, life; bool live; } jelly_mote_t;
typedef struct { float x, y, t; bool live; } jelly_ripple_t;

typedef struct {
    jelly_t        j[JELLY_MAX];
    int            n;
    float          clock;          /* seconds on the bare glass, lapped every JELLY_CLOCK_LAP (never the lifelong clock as a float: tank.h THE CLOCK) */
    float          fu[JELLY_FIELD_Y][JELLY_FIELD_X], fv[JELLY_FIELD_Y][JELLY_FIELD_X];   /* the water, px/s */
    int            jet;            /* JET_*: the pace the keeper picked */
    int            jet_aim;        /* 0..JET_AIMS-1: the direction the keeper picked */
    int            jet_aim_live;   /* the direction the jet pushes now: it follows a new pick at once (Strato, 10-19:
                                    * the jet itself should move right after the finger lifts); it is the WATER that
                                    * slows and turns over in its own time ("as if the water takes some time for the
                                    * current to release its impact") */
    float          jet_level;      /* the push now, eased: 0 still, 1 GENTLE, JET_STEADY_LEVEL */
    float          jet_stub;       /* the nozzle's drawn angle, eased toward the live aim */
    float          ju[JELLY_FIELD_Y][JELLY_FIELD_X], jv[JELLY_FIELD_Y][JELLY_FIELD_X];   /* the live aim's flow at full push */
    float          jw[JELLY_FIELD_Y][JELLY_FIELD_X];   /* 0..1 how much of a cell the plume reaches: the water there follows the jet fast */
    int            ju_aim;         /* the aim ju / jv / jw were worked out for (-1: none yet) */
    /* the jet's fan: hold the nozzle, five arrows open (jelly_touch) */
    bool           ui_open;
    float          ui_t, ui_idle;  /* seconds open; seconds since a finger was on it */
    int            ui_hot;         /* under the finger: 0..JET_AIMS-1 an arrow, JET_AIMS the hub, -1 nothing */
    /* touch, classified here so every board and the sim share it */
    bool           tc_down;
    int            tc_mode;        /* 0 deciding, 1 a resting finger (light), 2 a stroke, 3 on the fan */
    bool           tc_opened;      /* this press opened the fan */
    float          tc_x0, tc_y0, tc_lx, tc_ly;
    double         tc_t0, tc_tl;
    double         tc_tap_t;       /* the last tap (a ripple), for the double tap that is the light */
    float          tc_tap_x, tc_tap_y;
    int            tc_ripples, tc_lights;   /* counts, for the selftest */
    int            light;          /* JLIGHT_* */
    bool           light_on;
    jelly_mote_t   speck[JELLY_SPECKS];
    jelly_mote_t   bubble[JELLY_BUBBLES];
    jelly_ripple_t ripple[JELLY_RIPPLES];
    bool           holding;
    float          hold_x, hold_y, hold_t;
    float          bubble_acc;
    uint32_t       rng;
    int            laps;           /* the clock's laps so far: a rate that is not a whole number of turns a lap (a tentacle's ripple) carries its phase with this */
} jelly_tank_t;

void  jelly_init(jelly_tank_t *t, uint32_t seed);
/* a fresh tank in a theme (JELLY_THEMES index): its seed, its six, its light - the crew's tank is made this way
 * once and keeps the theme for good (docs/jelly-tank-0.6.md J2) */
void  jelly_theme_start(jelly_tank_t *t, int theme);
int   jelly_add(jelly_tank_t *t, int species, int neon);   /* index, or -1 when full */
void  jelly_remove_last(jelly_tank_t *t);
void  jelly_tick(jelly_tank_t *t, float dt);

/* touch: a tap is a ripple (a tap in the top band feeds); a resting finger is
 * a light they turn toward; a stroke is a current */
void  jelly_tap(jelly_tank_t *t, float x, float y);
void  jelly_hold(jelly_tank_t *t, float x, float y, bool down);
void  jelly_swipe(jelly_tank_t *t, float x0, float y0, float x1, float y1, float dt);
void  jelly_feed(jelly_tank_t *t, float x);
/* the finger, raw: down / up at (x, y) on the glass, `now` in seconds - a tap, a resting finger, a stroke,
 * or (holding the nozzle) the jet's fan; call it every poll */
void  jelly_touch(jelly_tank_t *t, float x, float y, bool down, double now);
/* the fan's geometry: the nozzle, the five arrows' centers, an arrow's radius */
void  jelly_jet_geom(const jelly_tank_t *t, float *nx, float *ny, float ax[JET_AIMS], float ay[JET_AIMS], float *chip_r);
void  jelly_jet_set(jelly_tank_t *t, int pace, int aim);   /* the keeper's pick (the water follows in its own time) */

/* the jet's lap time at its outer orbit, seconds (the same on every glass) */
float jelly_jet_lap_s(int jet);
/* the water's velocity at (x, y), px/s */
void  jelly_field_at(const jelly_tank_t *t, float x, float y, float *u, float *v);
/* distance from (x, y) to the glass, px, + inside */
float jelly_glass_dist(float x, float y);
/* the adult bell's half-width for a jelly, px (its size on this glass) */
float jelly_bell_a(const jelly_t *j);
/* a tentacle's points (its count, and its rest length in px) - for the selftest */
int   jelly_tentacles(const jelly_t *j);
int   jelly_rope_points(const jelly_t *j);
int   jelly_arms(const jelly_t *j);
float jelly_arm_len(const jelly_t *j, int m);
float jelly_rope_len(const jelly_t *j, int k);

/* the background (water, LED bar, glass) is baked into this TANK_W x TANK_H
 * buffer whenever the light changes (PSRAM on the device); without one it is
 * drawn straight into the frame every frame */
void  jelly_set_bg_cache(uint16_t *buf);
/* the halo disc, the glass's row spans, a sine table: jelly_scratch_bytes()
 * of PSRAM handed in by the firmware (calloc'd on first use otherwise) */
#include <stddef.h>
size_t jelly_scratch_bytes(void);
void  jelly_set_scratch(void *buf);
void  jelly_render(jelly_tank_t *t, uint16_t *fb, int stride);
/* the device's DMA prefetch (main.c, as for the fish scene): the background
 * as baked for this light (NULL until a frame has baked it), and a frame
 * buffer that already holds it, so the next jelly_render skips the copy */
const uint16_t *jelly_bg_buf(const jelly_tank_t *t);
void  jelly_fb_primed(const uint16_t *fb);

/* per-stage cost, microseconds summed since the caller cleared them:
 * 0 background, 1 halos, 2 tentacles + arms, 3 bells + specks, 4 motes + touch, 5 the tick (the strings) */
extern int64_t (*jelly_clock_us)(void);
extern int64_t jelly_prof_us[6];

#endif
