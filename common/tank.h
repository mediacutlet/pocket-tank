/* tank.h — platform-agnostic reflex layer for the pocket fish tank.
 *
 * Ported from the browser prototype (llm-fishtank-v0_2.html); same fish,
 * same goals, same drive dynamics, rescaled to the 448x368 landscape tank.
 * World geometry (bubble column, reef, zones) matches model/gen_traces.py so
 * the advisor model sees the world it was trained on.
 *
 * Population (docs/progression-next.md): the tank holds up to POP_CAP fish (5,
 * N_FISH_MAX the array bound) and only fish[0..n_fish-1] are alive/active. A new tank starts with two
 * adults of contrasting personality; the rest arrive later as fry (the
 * progression layer decides when). Every loop in this file, the renderer and
 * the advisors iterates n_fish, never N_FISH_MAX.
 *
 * No I/O, no floats-to-strings, no OS calls: this file and tank.c compile
 * unchanged for the LVGL PC sim and the ESP32-S3 firmware.
 */
#ifndef POCKET_TANK_TANK_H
#define POCKET_TANK_TANK_H

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#ifdef TANK_ROUND               /* the round 466 px glass (Waveshare 1.75C, 2026-10-01): the frame is the square around a bowl */
#define TANK_W 466
#define TANK_H 466
#endif
#ifdef TANK_WATCH               /* the watch (Waveshare 2.06, 2026-10-02): its 410 x 502 glass as it is worn - a PORTRAIT tank
                                 * (Strato, with the first landscape picture on his wrist: "I have to turn my head") */
#define TANK_W 410
#define TANK_H 502
#endif
#ifdef TANK_SQUARE              /* the square (Waveshare 2.16, 2026-10-12): a 480 x 480 glass in a desk box, USB at its foot -
                                 * the rectangle's world 32 px wider and 112 px taller, the pages centred in it */
#define TANK_W 480
#define TANK_H 480
#endif
/* a tank WORN on a wrist (2026-10-02): the picture's way up is the keeper's
 * setting (settings SCREEN), never the live IMU's - the arm swings through
 * every angle (tank_screen_*) */
#ifdef TANK_WATCH
#define TANK_WORN 1
#else
#define TANK_WORN 0
#endif
#ifndef TANK_W                  /* a build may override both (-DTANK_W=480 -DTANK_H=480: a square panel) */
#define TANK_W 448
#define TANK_H 368
#endif

/* ---- the glass (2026-10-01). In the rectangle the side walls are x = 0 and
 * TANK_W and the bottom is TANK_H; everything on the floor measures from
 * them. A ROUND build is a BOWL: water fills the circle, and sand lies flat
 * across its bottom - the floor is the chord at TANK_BOT - 16 (the sand
 * line, what TANK_H - 16 is in the rectangle) from TANK_FX0 to TANK_FX1,
 * with sand under it down to the glass. Floor things (the beds, a resting
 * pellet, the snail's walk, the decor) measure from TANK_BOT / TANK_FX0 /
 * TANK_FX1 on both builds - in the rectangle those ARE TANK_H / 0 / TANK_W.
 * Swimmers meet the glass itself: tank_glass_* (tank.c). */
#ifdef TANK_ROUND
#define TANK_RAD  233.0f        /* the circle: centre (TANK_RAD, TANK_RAD) */
#define TANK_BOT  396           /* sand line y 380: the glass is 180.8 px either side of the centre there */
#define TANK_FX0  54
#define TANK_FX1  412
#elif defined(TANK_WATCH)       /* the watch's glass is the rectangle with ROUND CORNERS: ~8 mm of radius on the
                                 * maker's drawing, 100 px. The bottom stays flat (TANK_BOT is TANK_H); the two
                                 * lower corners take the ends of the floor - the sand line (y 486) meets the
                                 * glass 46 px in - so the floor things keep to TANK_FX0..TANK_FX1, and the
                                 * swimmers, the film and the snail meet the arcs (tank.c, tank_glass_*) */
#define TANK_CORNER_R 100.0f
#define TANK_BOT  TANK_H
#define TANK_FX0  44
#define TANK_FX1  366
#elif defined(TANK_SQUARE)      /* the square's glass has the 1.8's kind of corner, ~40 px by the maker's pictures (not
                                 * measured): the sand line (y 464) meets the arcs 8 px in, so the floor keeps 10 px off */
#define TANK_CORNER_R 40.0f
#define TANK_BOT  TANK_H
#define TANK_FX0  10
#define TANK_FX1  470
#else
#define TANK_BOT  TANK_H
#define TANK_FX0  0
#define TANK_FX1  TANK_W
#endif
#define TANK_FW   (TANK_FX1 - TANK_FX0)
/* a floor x given as the rectangle's (0..448), on this build's floor */
#define TANK_FLOOR_X(x448) (TANK_FX0 + (x448) * TANK_FW / 448.0f)
float tank_glass_x0(float y);                          /* the side glass at height y (0 / TANK_W in the rectangle; a corner's arc on the watch) */
float tank_glass_x1(float y);
float tank_glass_top(float x);                         /* the glass above x (0 in the rectangle) */
void  tank_glass_clamp(float *x, float *y, float m);   /* (x,y) brought to at least m px inside the glass and above the bottom */

#define N_FISH_MAX 6            /* array bound; the live count is tank_t.n_fish */
/* THE CAP (Strato 2026-11-03): a keeper's tank holds at most POP_CAP fish, never more, on every path - the sim and
 * every board alike (the device shipped 5 while the sim ran 6, the array bound, and the cap's leaks hid behind it).
 * N_FISH_MAX stays the array bound: a fry is born into the slot past the fish before it waits for its home, visitors
 * have their own slots after. Every count against the cap is the fish here AND the places held for gifts and fry we
 * said yes to (progression.c fish_held; progression_cap_check guards it). Defined here only: */
#ifdef POP_CAP
#error "POP_CAP is the keeper's cap, 5 on every build: tank.h defines it, nothing else"
#endif
#define POP_CAP 5
#if POP_CAP >= N_FISH_MAX
#error "a fry is born into the slot past the cap before it goes to its home: N_FISH_MAX must stay above POP_CAP"
#endif
#define N_VISIT_MAX 4           /* the tank link (0.4): visitors from a neighbor tank, in their own slots after
                                 * the home fish - fish[N_FISH_MAX + k] - so every loop over n_fish (the save,
                                 * progression, selling, the cards) leaves them out unless it asks for them */
#define N_FISH_SLOTS (N_FISH_MAX + N_VISIT_MAX)
#define TANK_FRY_HOLDS 2        /* places held at home for fry courting next door: one per neighbor (tank_t.fry_hold) */
#define N_FISH_START 2          /* a new tank: two contrasting adults */
#define N_TRAINED_NAMES 4       /* name tokens the v2 model was trained on */
#define FISH_NAME_MAX 7         /* the keeper's name for a fish (first-run setup) */
#define LOOK_N 9                /* the colors the keeper picks a body and markings from (one palette, 0.4) */
/* the genes of form (0.4, docs/genetics-0.4.md): a body shape, a fin
 * arrangement, a pattern of the accent color. 0 is the only form before 0.4
 * (CLASSIC / CLASSIC / BARS), so a fish nothing has touched draws exactly as
 * it always has. Render-only: the model never sees a look. */
enum { SHAPE_CLASSIC, SHAPE_SLENDER, SHAPE_BULBOUS, SHAPE_N };
enum { FINS_CLASSIC, FINS_VEIL, FINS_SAIL, FINS_N };
/* PATTERN_SOLID (Strato 10-04): no markings - a solid fish; a gene like the
 * others (inherited, carried, a mutation's possible pick). The editor shows
 * the three markings as tiles and reaches SOLID by deselecting the picked one */
enum { PATTERN_BARS, PATTERN_MASK, PATTERN_EYEBAND, PATTERN_SOLID, PATTERN_N };
#define PATTERN_TILES 3
extern const char *const SHAPE_NAMES[SHAPE_N], *const FINS_NAMES[FINS_N], *const PATTERN_NAMES[PATTERN_N];
/* heredity (0.4): five genes. Every fish SHOWS one copy of each and CARRIES a
 * second it does not show. A fry takes one copy from each parent - the one
 * that parent shows, or GENE_PASS_CARRIED of the time the one it carries -
 * shows one of the two (a coin) and carries the other, so a trait can skip a
 * generation. GENE_MUTATION of fry show, in one gene, a value neither parent
 * shows. The colour genes are LOOK_BODY / LOOK_ACCENT indexes (the fish keeps
 * the RGB it shows in color / accent, as before 0.4). */
enum { GENE_SHAPE, GENE_FINS, GENE_PATTERN, GENE_BODY, GENE_ACCENT, GENE_N };
#define GENE_PASS_CARRIED 0.25f
#define GENE_MUTATION     0.05f
extern const char *const GENE_NAMES[GENE_N];
int   tank_gene_values(int g);                       /* how many values gene g has */
/* motion feel (0.4, Strato's pick: looks + motion, no effect on the economy
 * or the model): the form scales the fish's turn rate - the heading's swing
 * and the yaw turn alike, and the tail beat's pace (tank_fish_beat_rate).
 * CLASSIC x CLASSIC is exactly 1: today's fish steer as they always have. */
#define TURN_SLENDER 1.10f      /* a long low body: quick */
#define TURN_BULBOUS 1.05f      /* a short body pivots */
#define TURN_VEIL    0.95f      /* the long fins drag a little */
#define BEAT_SLENDER 1.15f      /* the tail beat: a slender's quicker */
#define BEAT_BULBOUS 0.90f      /* ... a bulbous one's slower */
#define BEAT_VEIL    0.92f      /* ... the long fins drag it a little */
#define MAX_FOOD   8
#define MAX_BUBBLE 24

/* upkeep: the vegetation beds keep growing - up toward the surface and out,
 * wider - and algae films the glass with time (both faster while the device
 * drowses); the keeper's thumb is the cure. Every frond has its own height
 * (2026-09-04, Strato: trimming was a chunk at a time): a sideways stroke
 * through a canopy cuts exactly the fronds it crosses, at the height where
 * it crosses them (never below nubs - little bits of green always remain);
 * a drag elsewhere wipes algae.
 * Vegetation is a comfort system (2026-09-04 rework): growth IS height - a
 * bed at growth g stands g of the way from the floor to the surface, so only
 * the tank ceiling limits it. Fish like cover: any canopy calms them (more
 * inside it), and NO cover anywhere
 * (every bed scalped) is a mild unease. Only a tank being truly smothered
 * presses back: two or more beds past VEG_SMOTHER height. Stress is already
 * in the advisor's schema, so the model reacts without any schema change. */
#define VEG_BEDS   3                           /* reef bed + two decor beds (the SAVE's
                                                * bed count: baked into save_t's arrays) */
#define VEG_BEDS_MAX 4                         /* + the shop's sword plant, bed 3 (2026-09-15:
                                                * live only once bought; tank_veg_beds) */
typedef enum { VEG_KIND_GRASS, VEG_KIND_SWORD } veg_kind_t;
#define VEG_START  0.35f                       /* a fresh tank (and a bought plant): comfortable cover */
#define VEG_NUB    0.03f                       /* trim floor: ~13 px green stubble */
#define VEG_BARE   0.10f                       /* tallest bed under this = no cover
                                                * anywhere: mild unease (relieved the
                                                * moment one tuft regrows past it) */
#define VEG_NURSERY 0.12f                      /* a bed this tall (~47 px, hides an adult
                                                * body) is a NURSERY: courtship happens
                                                * low in it and a fry is only born with
                                                * one somewhere (2026-09-04) */
#define VEG_SMOTHER 0.85f                      /* the SECOND-tallest bed past this much
                                                * of its OWN ceiling (tank_veg_cap) =
                                                * smothered: real stress, trim it back.
                                                * One bed at the ceiling is just a
                                                * good place to hide. */
/* Every frond has its own ceiling (2026-09-23): a hashed height in
 * VEG_CAP_LO..VEG_CAP_HI it grows toward and never passes, so a grown bed is
 * a ragged skyline in the top third of the glass instead of a wall touching
 * the surface. Growth also eases off over the last VEG_CAP_TAPER of the way
 * up, so the fronds settle one by one. Fixed per slot (bed, frond): nothing
 * to save. The keeper (director `veg`, staging) may still set a frond
 * higher; growth never pulls it back down. Height 1 = the surface. */
#define VEG_CAP_LO  0.72f
#define VEG_CAP_HI  0.95f
#define VEG_CAP_TAPER 0.12f
#define VEG_PACE_SPREAD 0.15f                  /* each frond grows at its own pace, 0.85..1.15x
                                                * (hashed per slot like its ceiling): a bed cut
                                                * flat by the scissors comes back ragged, not
                                                * as a hedge (2026-10-05) */
#define VEG_GLASS_GAP 12                       /* px a ceiling keeps under the glass, where the glass closes in over a frond
                                                * (the bowl, the watch's upper corners: tank_veg_cap) */
#define VEG_FRONDS_MAX 16                      /* per-bed frond slots (reef bed: 11-15) */
#define VEG_SEGS_FULL 107                      /* frond segments at growth 1: the tip
                                                * of the tallest frond touches y~10,
                                                * just under the surface (render.c
                                                * VEG_SEG_DY 3.2 px pitch from y=352) */
#ifdef TANK_ROUND                              /* the bowl: 26 x 22 cells of 18 px over the circle down to TANK_BOT -
                                                * 572, inside the 644 the save keeps (progression.c) */
#define ALGAE_CELL 18
#define ALGAE_COLS 26
#define ALGAE_ROWS 22
#elif defined(TANK_WATCH)                      /* the watch: 23 x 28 cells of 18 px (414 x 504: the last column and row hang
                                                * 4 / 2 px off the glass) - 644 cells, what the save keeps */
#define ALGAE_CELL 18
#define ALGAE_COLS 23
#define ALGAE_ROWS 28
#elif defined(TANK_SQUARE)                     /* the square: 24 x 24 cells of 20 px = 576, the 480 px glass exactly (16 px cells
                                                * would be 900 - the save keeps 644) */
#define ALGAE_CELL 20
#define ALGAE_COLS 24
#define ALGAE_ROWS 24
#else
#define ALGAE_CELL 16                          /* px per glass-film grid cell */
#define ALGAE_COLS (TANK_W / ALGAE_CELL)       /* 28 */
#define ALGAE_ROWS (TANK_H / ALGAE_CELL)       /* 23 */
#endif
#define ALGAE_CELLS (ALGAE_COLS * ALGAE_ROWS)
/* a frond segment's height. 3.2 px on the 1.8 puts a full frond's tip just
 * under the surface; the watch's portrait tank is 118 px taller, so its
 * segments are too (107 x 4.3 from y 486: the tip at 26) - growth is still
 * "this much of the way to the surface". What a trim PAYS is counted at the
 * 1.8's pitch on every build (VEG_PAY_PX): the same cut, the same sand dollars. */
#ifdef TANK_WATCH
#define VEG_SEG_PX 4.3f
#elif defined(TANK_SQUARE)                     /* ... and the square's 112 px taller: 107 x 4.2 from y 464, the tip at 14 */
#define VEG_SEG_PX 4.2f
#else
#define VEG_SEG_PX 3.2f
#endif
#define VEG_PAY_PX 3.2f
int tank_algae_cells(void);                    /* the cells that are on the glass: all of them in the rectangle, the circle's in the bowl */
#define ALGAE_DIRTY 0.15f                      /* film on more of the glass than this =
                                                * a DIRTY tank: no fry is conceived in
                                                * it until it is wiped back under (a
                                                * fry gate, 2026-09-16). Some film is
                                                * fine; growth stops claiming cells at
                                                * ALGAE_COVER_CAP (tank.c, 0.30), and one
                                                * night's sleep films ~17% (selftest-tend;
                                                * ~25% before 2026-09-23) */

typedef enum {
    GOAL_SEEK_FOOD, GOAL_FLEE_SHADOW, GOAL_VISIT_BUBBLES, GOAL_FOLLOW_FRIEND,
    GOAL_EXPLORE, GOAL_REST, GOAL_DART_PLAY, GOAL_INSPECT_REEF,
    GOAL_COUNT
} goal_id_t;

extern const char *const GOAL_NAMES[GOAL_COUNT];   /* schema.md lowercase names */

/* SWIM DEPTH (0.3.9.4, Strato 10-31: "its a decision the fish makes. To swim in front, behind, or through"): a
 * fish IN FRONT of the back grass, BEHIND every frond, or THROUGH it (every other frond over it). A decoration's
 * depth orders the decorations; the fish's own is this. docs/swim-depth.md */
enum { FISH_DEPTH_FRONT = 0, FISH_DEPTH_BEHIND = 1, FISH_DEPTH_THROUGH = 2 };
typedef enum { STAGE_FRY, STAGE_JUV, STAGE_ADULT, STAGE_ELDER } stage_t;
extern const char *const STAGE_NAMES[4];           /* schema.md v2 stage tokens */
extern const char *const TRAINED_NAMES[N_TRAINED_NAMES]; /* mira bolt kelp nori */

typedef struct {
    goal_id_t id;
    float     urgency;      /* 0..9, scales speed/turn gain */
    /* distribution layer (LLM advisor only; rules leave confidence = 1):
     * confidence = probability the advisor put on this goal; runner_up = the
     * second choice. The reflex layer renders low confidence as a visible
     * hesitation (pause + glance at the runner-up's target) - the decision is
     * never altered, only its certainty is shown. */
    float     confidence;
    goal_id_t runner_up;    /* GOAL_COUNT when unknown */
} goal_t;

/* per-fish milestone bits (progression.c detects, render reads) */
enum {
    MS_ARRIVED = 1u << 0,  MS_FIRST_MEAL_FROM_YOU = 1u << 1, MS_FIRST_HOLD_APPROACH = 1u << 2,
    MS_FIRST_DART = 1u << 3, MS_FIRST_BUBBLES = 1u << 4,
    /* bit 5 was the badge "first reef" until 0.3.0 (2026-10-02): the "reef" was
     * only ever the grass corner. It is no badge now - the reef is the reef
     * cluster and a TANK milestone (TMS_FIRST_REEF), the fish's badge in its
     * place is MS_FIRST_GRASS - but the bit is still set, silently, the first
     * time a fish pokes around its landmark: the card's curiosity slider is
     * revealed by it, as ever */
    MS_INSPECTED = 1u << 5,
    /* bits 6 and 7 were the shadow milestones (survived / shrugged off);
     * the shadow was removed 2026-09-13 and the bits stay reserved so old
     * saves keep their layout - cleared on load, never set, never shown */
    MS_RETIRED_6 = 1u << 6, MS_RETIRED_7 = 1u << 7, MS_FIRST_FOLLOW = 1u << 8,
    MS_REACHED_JUV = 1u << 9, MS_REACHED_ADULT = 1u << 10, MS_REACHED_ELDER = 1u << 11,
    MS_FIRST_GRASS = 1u << 12,                               /* rested (or hid) inside a seagrass canopy (2026-10-02) */
    MS_FISH_COUNT = 13,                                      /* (the badges; the bits past it are no badge) */
    /* born of a visitor and one of the tank it was born in (cross-tank fry,
       2026-10-05): no badge, never a notice - Strato: "worth double" (its
       SELL). It travels in the passport and is saved like the badges */
    MS_CROSSBRED = 1u << 13
};
#define MS_RETIRED_MASK (MS_RETIRED_6 | MS_RETIRED_7)
/* tank-level milestone bits */
enum {
    TMS_PAIR = 1u << 0, TMS_TRIO = 1u << 1, TMS_QUARTET = 1u << 2, TMS_QUINTET = 1u << 3,
    TMS_SEXTET = 1u << 4, TMS_FIRST_FULL_NIGHT = 1u << 5,   /* the device slept a full night (was: quiet night, until 2026-09-15) */
    TMS_FIRST_PLAY_SESSION = 1u << 6,
    TMS_CHANGED_SOMEONE = 1u << 7, TMS_FIRST_FEEDING = 1u << 8,
    TMS_FIRST_TRIM = 1u << 9, TMS_FIRST_CLEANING = 1u << 10,
    TMS_FULL_SCHOOL = 1u << 11,                              /* the shrimp school reached SHRIMP_MAX (2026-09-30) */
    TMS_FIRST_REEF = 1u << 12,                               /* a fish went to look at the reef cluster (2026-10-02) */
    TMS_FIRST_MUTATION = 1u << 13,                           /* a fry was born with a mutation (0.4, heredity) */
    TMS_FIRST_SALE = 1u << 14,                               /* a fish sold: fry for sale in the shop (0.3.5) */
    TMS_SEAHORSE_FED = 1u << 15,                             /* the seahorse's first snack (0.3.6); its badge shows once it is bought */
    TMS_CRAB_FIND = 1u << 16,                                /* the crab's first sand dollar, taken (0.3.9); its badge shows once it is bought */
    TMS_COUNT = 17
};

typedef struct {
    char   name[FISH_NAME_MAX + 1]; /* display name: the roster preset's until the
                                     * keeper renames it (setup.c); saved per fish */
    const char *model_name; /* one of TRAINED_NAMES: the token the v2 model sees */
    int    preset;          /* roster index (colors, base size, temperament) */
    float x, y;
    float heading;          /* radians */
    float yaw;              /* which way it faces, -1 (left) .. +1 (right), seen
                             * from the side: a U-turn swings it through 0 (the
                             * fish head-on) and the swim follows it. Not saved. */
    float yaw_tail;         /* the tail's yaw, a beat behind the head's: mid-turn
                             * the body bends (render.c draws both) */
    /* the trail (0.4, Strato 11-02: a long fish "moves with it on the pivot
     * point faster than would be possible in water"): the body's pitch as
     * drawn, the tail's - a spring behind it, so a long tail keeps its way
     * through a turn and catches up, a touch past - their swing rates, the
     * tail yaw's, and the speed the fins have felt (a burst streams them
     * back, a stop lets them billow). tank_fish_trail; render.c bends the
     * rear body by the gap. Not saved */
    float pitch_b, pitch_bv, pitch_t, pitch_tv, yaw_tv, speed_t;
    float beat;             /* the tail beat's phase, radians under 2 pi: its rate x each frame's dt (tank_fish_beat_rate),
                             * so a change of speed changes its pace and never jumps it (0.4: it was the clock x the
                             * rate - a jump that grew with the clock, a thrash after an overnight wake). Not saved */
    float beg;              /* begging under the feed spot: its darts' phase, at a rate its boldness sets. Not saved */
    int8_t facing;          /* the side it swims on, +1 right / -1 left: changes only
                             * by a committed turn (or the glass). Not saved. */
    float behind;           /* seconds its way has lain behind it (a turn commits
                             * past a threshold); below 0 = just turned, settling */
    float speed, target_speed;
    float wander;           /* wander phase */
    float size;             /* ~1.0 */
    float base_size;        /* preset size; progression scales by stage/meals */
    float turn_rate;        /* rad/s */
    /* drives, 0..10 as in the prototype (schema encoding clamps to 0..9) */
    float hunger, energy, stress, curiosity;
    /* personality 0..1 (schema v2 encodes bold/sociable as 0-9) */
    float sociable, bold, lazy;
    float bold0, sociable0; /* values at creation: drift is measured from here */
    stage_t stage;
    goal_t goal;
    float  goal_age;        /* seconds since last goal change */
    float  ask_age;         /* seconds since the advisor was last asked about it */
    float  dart_timer;      /* DART_PLAY burst countdown */
    float  dart_x, dart_y;
    float  hesitate;        /* seconds of visible deliberation left (0 = none) */
    /* boredom (2026-09-14): how stale the current pastime is, 0..10 (schema
     * v4 sends it to the model as `bored 0-9`). Rises while the fish keeps
     * the same leisure goal, relieved by a genuinely new goal or by entering a
     * zone it has not seen for a while; eating and night rest never bore.
     * Not saved: a boot starts fresh. Strato (2026-09-14): "the fish often
     * seem to get stuck in loops following_friend or playing in the bubble
     * column ... a boredom component where a fish will be somewhat pushed to
     * do something new". */
    float  bored;
    goal_id_t goal_prev;    /* the goal before the current one: returning to it is no relief */
    int8_t zone_last;       /* schema zone 0..5 the fish was in last frame; -1 = none yet */
    bool   at_bubbles;      /* inside the column's play radius on a VISIT_BUBBLES goal (the bubbles cue fires on entry) */
    float  breather;        /* the breather (2026-10-21): seconds spent ducking into the nearest canopy
                             * because the fish is spent (energy under BREATHER_AT by day), 0 = none.
                             * The advisor keeps its goal; the body answers it again with its wind back.
                             * Not saved: a boot starts fresh. */
    uint8_t gdepth, gdepth_want, gdepth_rest;   /* SWIM DEPTH (0.3.9.4): this fish against the back grass - FISH_DEPTH_*,
                             * the one its goal and temperament chose (depth_pick: 0xff = not yet), the one
                             * a breather hides at; taken up only in the clear of every bed. Not saved */
    uint8_t pdepth, pdepth_want;   /* ... and against the pieces (phase 2): FISH_DEPTH_FRONT or _BEHIND, whatever
                             * a piece's own depth (that orders the pieces); taken up only in the clear of
                             * every piece. Never BEHIND at rest (D2: a resting fish stays in sight) */
    bool   hold_far;        /* was HOLD_APPROACH_FROM+ px from the finger when this hold's draw
                             * began (transient): only such a fish can earn a hold-approach */
    bool   hold_call;       /* answering a held finger (the last frame's steering, transient): it wants to be in
                             * sight - in front of the grass and the pieces - and a piece hiding it is swum out
                             * from round its nearest side first (Strato 11-01: "the fish would not be able to see
                             * my finger through the volcano"); its jump to the front may cross the seagrass, never
                             * a piece (tank.c piece_exit) */
    double zone_seen[6];    /* tank clock when the fish was last in each zone */
    float  explore_x, explore_y; /* explore's destination - a point in a stale zone */
    bool   explore_set;     /* ... valid; cleared when explore is (re)chosen or reached */
    int    eaten;
    int    eaten_player;    /* pellets that came from the keeper's hand */
    bool   meal_due;        /* ate from the keeper's hand since progression last looked (the meal pay) */
    float  meal_pay_wait;   /* seconds before this fish's meal pays again (SD_MEAL_GAP_S). Not saved */
    bool   starve_flagged;  /* diagnostic: starving-ignored-food episode counted */
    float  trust;           /* 0..10: gates approach-to-finger; drifts with treatment */
    float  drift_acc;       /* lifetime personality PRESSURE: every unit bold/sociable wanted
                             * to move, clamp or not (saved; the CHANGE gate reads the
                             * youngest's, so a fish born on a clamp can still earn it) */
    float  rest_dx, rest_dy;/* this fish's own spot by the reef (individuation) */
    uint32_t sig;           /* coarse state signature at the last advisor ask */
    uint32_t ms_bits;       /* MS_* milestones reached */
    uint32_t ms_seen;       /* MS_* the keeper has looked at on the milestones page (new = bits & ~seen) */
    /* colors as 0xRRGGBB, used by render only: the preset's, or the keeper's
     * picks from LOOK_BODY / LOOK_ACCENT (setup.c); saved per fish */
    uint32_t color, fin, accent;
    /* family (2026-09-14, the birth flow): the slots of the two fish it
     * inherited from - its body colour is parent_a's, its markings (accent)
     * parent_b's, bold / sociable the pair's average with a nudge. -1 = one
     * of the founding pair (or an older save). Saved per fish. */
    int8_t parent_a, parent_b;
    /* its form (SHAPE_* / FINS_* / PATTERN_*; 0.4). The body shape shows from
     * birth, the pattern comes in at JUVENILE (as the bars always have), the
     * fin arrangement at ADULT (a fry and a juvenile have the plain tail).
     * Saved in the genes tail (progression.c). */
    uint8_t shape, fins, pattern;
    /* heredity (GENE_*): the copy of each gene it carries and does not show;
     * the gene that mutated at its birth (+ 1; 0 = none); and where each shown
     * copy came from - bit g of from_b: parent_b (else parent_a), of
     * from_carried: that parent's CARRIED copy (a family secret surfacing) */
    uint8_t carried[GENE_N], mutated, from_b, from_carried;
    /* identity (0.4, the tank link - docs/tank-link-0.4.md on its branch; the
     * names are the link's, lifted from its 5d1230a so it rebases clean): a
     * random 32-bit ID made when the fish is made, never reused, never 0, and
     * the IDs of the two fish it came from (0 = a founder, or not known). A
     * slot is where a fish lives in THIS tank; the ID is who it is anywhere -
     * kept for life (a sale, a rename, a move to another tank). Saved in the
     * identity tail (progression.c); a pre-0.4 save's fish get fresh IDs at
     * load (tank_ensure_ids), their parents' from the parent slots. */
    uint32_t uid, parent_uid[2];
    /* the tank link's doors (0.4, docs/tank-link-0.4.md). away: one of ours
     * visiting next door - not simulated, not drawn, not aged here (saved, so a
     * crash brings it home). door: where it is with a door - DOOR_NONE, HEADING
     * (exploring toward it), ASKED (at the glass, the neighbor asked), PASSING
     * (through the glass - no walls), ENTERING (coming in from a door). Not
     * saved. homeward: a visitor told to go home (the drift rule). bolt_home:
     * a visitor the keeper spooked (the triple tap) - home through its door
     * at a flight's speed (Strato 2026-10-05: "it should high tail it home") */
    bool     away, homeward, door_sent, bolt_home;
    bool     born_away;         /* home: a fry born next door, swimming home - its birth flow waits for it (phase 5) */
    uint32_t follow_uid;        /* host: a visiting parent trails its fry home - this one, while it is still here (phase 5) */
    bool     deported;          /* host: the keeper sent it home (DEPORT) - briskly, no fright; home: a fish GIVEN away, off to the door the same way */   /* door_sent: the OFFER for this trip is out (tank_link.c) */
    int8_t   door, door_edge;
    double   door_seen;         /* tank clock of its last visit next door (the neighbor ages like a zone) */
    double   door_refused;      /* tank clock until which a door refused it is glass for it */
} fish_t;
enum { DOOR_NONE, DOOR_HEADING, DOOR_ASKED, DOOR_PASSING, DOOR_ENTERING };

/* a pellet: it sinks FOOD_SINK_PX_S from the surface to the floor (~43 s on
 * the 1.8, ~60 on the watch), then RESTS there FOOD_FLOOR_S before it
 * dissolves (2026-09-29, for the shrimp school - every tank alike; measured:
 * the fish's hunger is unchanged). floor_s = seconds on the floor so far,
 * nibbled = seconds of shrimp pecking (SHRIMP_PECK_S eats it). */
typedef struct { float x, y, age; bool alive, from_player; float floor_s, nibbled; } food_t;
#define FOOD_FLOOR_Y   (TANK_BOT - 14)  /* where a pellet comes to rest */
#define FISH_FLOOR_Y   (TANK_BOT - 16)  /* the sand line: a fish is never DRAWN below it (render.c) - a fin
                                         * that reaches the floor touches it instead of showing through it
                                         * (2026-10-24; the pellets rest 2 px into the sand, so a fish eating
                                         * off the floor lies on it) */
#define FOOD_SINK_PX_S 8.0f
/* in the water: gone at this age if it never reached the floor - a cap that
 * every pellet's fall fits under, the deepest drop on the glass plus a margin.
 * (0.3.7: it was 45 s, the 1.8's fall; the watch's, the square's and the
 * bowl's middle are 46-60 s, so pellets vanished mid-water there before the
 * shrimp could reach them - a keeper on the watch, 2026-10-18) */
#define FOOD_LIFE_S    (FOOD_FLOOR_Y / FOOD_SINK_PX_S + 5.0f)
#define FOOD_FLOOR_S  30.0f          /* ... and how long it rests there (15 from 2026-09-29; doubled 2026-10-24, Strato testing the crab: "it's quite
                                      * challenging to have the crab get any food ... he can't get there quick enough") */

/* the shrimp school (2026-09-29, the shop's sixth item, SD_ITEM_SHRIMP): cherry
 * shrimp that swim as a loose school with a pull toward the grass, swarm the
 * pellets that sink below the grass's top (falling or on the floor), drift up
 * to the canopy now and then, and scatter when the keeper taps near them.
 * Bought as SHRIMP_START grown adults; every SHRIMP_PER_JOIN pellets the school
 * eats brings one more, up to SHRIMP_MAX, never two within SHRIMP_COOLDOWN_S
 * (the count stops at SHRIMP_PER_JOIN while it waits - no burst). Over
 * SHRIMP_REFUSE_COVER of the glass fouled they refuse food: they stay in the
 * grass and turn away from pellets, so the school stops growing (never shrinks).
 * Strato's words throughout (2026-09-29): "they should just swarm pellets that
 * hit the ground", "a user cant spam this", "when there is too much algae,
 * shrimp will refuse to eat". Drawn procedurally (render.c draw_shrimp). */
#define SHRIMP_START        4
#define SHRIMP_MAX          10
#define SHRIMP_PER_JOIN     10
#define SHRIMP_COOLDOWN_S   (15.0f * 60.0f)
#define SHRIMP_REFUSE_COVER 0.5f
#define SHRIMP_PECK_S       8.0f     /* shrimp-seconds of pecking to finish a pellet: a huddle of
                                      * 3-4 takes 2-3 s - long enough to see them swarm it */
typedef struct {
    float  x, y, vx, vy;             /* centre of the body; px/s */
    float  yaw;                      /* -1..1 as the fish's: the turn foreshortens it */
    float  behind;                   /* s its way has been behind it (a committed turn, as the fish) */
    float  ph;                       /* animation phase: legs, the peck, the bob */
    float  dart;                     /* s left of a tail-flick escape */
    int8_t facing;
    uint8_t pecking;                 /* at a floor pellet this frame */
} shrimp_t;
typedef struct { float x, y, vy, wobble; bool column; } bubble_t;

typedef struct tank {
    fish_t   fish[N_FISH_SLOTS];       /* home fish 0..n_fish-1, visitors at N_FISH_MAX + k (vis_used) */
    int      n_fish;               /* live fish = fish[0..n_fish-1] */
    food_t   food[MAX_FOOD];
    bubble_t bubble[MAX_BUBBLE];
    float    bubble_x, bubble_y;   /* bubble column anchor */
    float    reef_x, reef_y;
    double   clock;                /* seconds since start - a double (0.4): a float stopped at 2^20 s (~12 days awake;
                                    * rounding every frame from ~3 days), and every hold, wait and sway with it. Every
                                    * stamp it is compared with is a double too; a drawing takes tank_anim's float */
    bool     night;                /* the tank light is off: fish rest, the palette dims */
    float    idle_s;               /* seconds since the device was last HANDLED - moved
                                    * (IMU) or touched. The light goes off once this
                                    * passes LIGHT_IDLE_S (2026-09-15: the 240 s day/night
                                    * cycle is gone; a tank left on the desk goes dark and
                                    * the fish sleep, a pick-up or a touch wakes it). */
    int      light_idle_s;         /* the keeper's idle time (settings page): seconds still
                                    * before lights-out; LIGHT_IDLE_S by default, saved */
    bool     light_auto;           /* settings LIGHTS OUT = AUTO: the idle rule turns the light
                                    * off by itself. Off by default (Strato, 2026-09-15): the
                                    * keeper's double-tap runs the light unless they opt in */
    bool     light_manual_off;     /* MANUAL: the keeper's last double-tap left it off (saved) */
    bool     light_tip_seen;       /* a double-tap has turned the light off once: its notice (notice.h
                                    * NOTICE_LIGHTS_OUT) came up then, and never again (saved) */
    bool     screen_turned;        /* a worn tank (TANK_WORN): settings SCREEN = TURNED (saved) */
    bool     orient_lock;          /* settings ROTATION = locked (0.3.2, saved): the picture keeps the way up it
                                    * had when the keeper locked it, however the tank is turned (tank_orient) */
    uint8_t  orient_q;             /* the picture's way up now, in quarter turns clockwise (0..3; 2 = turned
                                    * over, 1 and 3 = on a side, the square and the pendant only): the
                                    * platform's live reading, or what the lock froze (saved with the lock) */
    bool     autofeed_off;         /* settings AUTO FEED = OFF (0.3.2, saved): the tank's own trickle never
                                    * drops a pellet - feeding is the keeper's alone. Nobody dies of it; a
                                    * fish left starving in a lit tank slowly loses trust (progression.c) */
    bool     light_override;       /* director / sim took manual control of the light.
                                    * Not saved (a saved override once froze a tank in
                                    * permanent day and starved a milestone). */
    bool     light_on;
    /* touch / tap interaction (docs/progression.md). Reflex-layer only: the
     * advisor never sees taps directly, only their effect on stress. */
    bool     hold_active;          /* finger held on the glass this frame */
    float    hold_x, hold_y;
    float    hold_time;            /* seconds the current hold has lasted */
    bool     hold_approached;      /* this hold already counted (hold_approaches); the
                                    * per-fish milestone is NOT gated on it */
    float    hold_ph, hold_turn_s;  /* the fish round a held finger (0.3.9.3): where their loose ring has got to,
                                     * and how long until it may turn back (tank.c HOLD_RING_*) */
    int8_t   hold_dir;              /* ... which way it drifts */
    int      tap_count;            /* taps in the current burst */
    float    tap_burst_t;          /* seconds since last tap */
    float    tap_x, tap_y;
    bool     startled;             /* aggressive-tap flee mode engaged */
    float    startle_x, startle_y;
    float    startle_cooldown;     /* seconds of calm needed to disengage */
    /* glass wipe (algae cleaning) + canopy slash (vegetation trim): platform
     * re-asserts drag_active every frame a finger is on the glass; tank.c
     * consumes it like hold_active */
    bool     drag_active;
    bool     drag_has_prev;
    float    drag_px, drag_py;     /* previous drag point */
    float    drag_dist;            /* travel in this stroke; wiping engages past a threshold */
    bool     slash_armed;          /* the stroke STARTED on a bed's canopy */
    int8_t   slash_wall;           /* ... on no frond, but beside a wall frond: -1 left wall, +1 right (it runs on into that wall) */
    bool     slash_engaged;        /* ... and has travelled sideways enough to be scissors */
    bool     wipe_sounded;         /* this stroke's wipe cue has fired */
    bool     slash_cut;            /* ... and has cut at least one frond (trims++ once) */
    float    slash_x0, slash_y0;   /* stroke start (the pre-engage travel is cut retroactively) */
    float    slash_h, slash_v;     /* travel this stroke: horizontal / vertical */
    /* the toolbox under the stats card (2026-10-01): TOOL_HAND = a stroke
     * guesses (the wipe / slash rules above); TOOL_SPONGE = it only wipes,
     * TOOL_SCISSORS = it only cuts. Not saved; it goes back in the box after
     * TOOL_IDLE_S without a stroke. A tool in hand owns the glass (2026-10-04,
     * Strato: "it should prevent all other taps and interactions until the
     * done button is pressed"): tank_touch_tap and tank_touch_hold do nothing,
     * and the touch ports answer only the chip's DONE - no card, no feed, no
     * light, no placement page. */
    uint8_t  tool;
    float    tool_idle;            /* seconds since the tool last touched the glass */
    bool     tools_locked;         /* the toolbox's LOCK (0.3.5, saved): bare fingers never wipe or
                                    * cut - only a tool from the box does (tank_lock_tools) */
    /* upkeep state (persisted by progression.c) */
    float    veg_h[VEG_BEDS_MAX][VEG_FRONDS_MAX]; /* per-frond height, VEG_NUB..1 (fraction
                                               * of the way from the floor to the surface) */
    float    veg_growth[VEG_BEDS_MAX]; /* per-bed canopy = MEAN frond height, VEG_NUB..1,
                                    * derived (tank.c veg_sync) - read-only outside;
                                    * set a bed with tank_veg_set */
    uint8_t  algae[ALGAE_CELLS];   /* glass film per cell, 0..255 */
    float    algae_acc;            /* seconds toward the next algae growth step */
    int32_t  trims;                /* lifetime bed trims (milestone + save) */
    int32_t  cells_cleaned;        /* lifetime algae cells wiped (milestone + save) */
    /* the chore counters behind the sand dollars (2026-09-15): a COLONY is a
     * connected patch of film the keeper's wipe took the last cell of (the
     * snail's grazing never counts); trim_px is frond length actually cut,
     * PX_PER_CM to the centimeter. Both saved. */
    int32_t  algae_colonies;
    float    trim_px;
    /* sand dollars (progression.c owns the economy; tank.c reads the unlocks):
     * the balance, the lifetime total, what the shop has sold (SD_ITEM_*),
     * and the ledger that keeps an award from paying twice - per fish (bits
     * SD_PAID_*), and how many payouts of colonies / grass have been made
     * (sd_inches_paid: named for the inches it counted until 2026-09-29).
     * All saved. */
    int32_t  sd_balance, sd_earned;
    uint32_t sd_unlocks;
    uint32_t sd_shelved;           /* the decoration shelf (0.3.9): owned pieces put away for later -
                                    * a subset of sd_unlocks, placeable bits only. A shelved piece keeps
                                    * its spot / depth / color / growth and keeps GROWING, but is not in
                                    * the water: tank_decor_in says what is. Saved. */
    uint32_t sd_locked;            /* LOCKED pieces (0.3.9, Strato: "the ability to 'lock' decorations
                                    * in place so you can't accidentally enter the move menu when
                                    * tapping and holding"): a subset of sd_unlocks, placeable bits.
                                    * A held finger passes a locked piece by (tank_decor_hit); its
                                    * placement page opens the long way only - keeper menu, UPGRADES,
                                    * its row, MOVE - and that page's LOCK button undoes it. Kept
                                    * across the shelf, cleared by a sale. Saved. */
    uint32_t sd_paid_fish[N_FISH_MAX];
    int32_t  sd_colonies_paid, sd_inches_paid;
    int32_t  sd_snacks_paid;       /* the seahorse's snacks paid so far (0.3.6) */
    /* the snail (SD_ITEM_SNAIL): crawls the glass toward the nearest film and
     * grazes it - the one creature in the tank that is not the model's. Its
     * position is saved; heading / target are not. */
    float    snail_x, snail_y, snail_heading;
    int16_t  snail_cell;           /* the algae cell it is heading for, -1 = wandering */
    float    snail_dy;             /* px DOWN from SNAIL_FLOOR_Y on its floor walk: in its front lane inside a piece IN FRONT's footprint it eases
                                    * onto that piece's base line (DECOR_FY_FRONT), like the urchin and the crab (Strato 10-24) */
    float    snail_graze;          /* seconds on the current cell */
    bool     snail_front;          /* walking the floor, it passes IN FRONT of a reef cluster or coral placed
                                    * IN FRONT (0.3.2, Strato: "it marches to the beat of its own drum" - the
                                    * big cluster hid it for most of its walk). Its own choice, made again at
                                    * each end of the floor; off the glass it lands in front, where it was.
                                    * Not saved. */
    int32_t  snail_grazed;         /* algae cells it has grazed clean, lifetime (its card,
                                    * 2026-09-16; saved) */
    float    snail_sleep_acc;      /* the night shift's part-cells: a nap's few minutes still count
                                    * (not saved; a boot lives the whole absence in one call) */
    /* the urchin (SD_ITEM_URCHIN, 2026-10-02): grazes the tall GRASS down on
     * the floor, as the snail grazes the glass. Its x is saved (the floor is
     * its y); the frond it is after, its appetite and its chewing are not. */
    float    urchin_x;
    int8_t   urchin_bed, urchin_frond; /* the frond it is after, -1 = none: it rests or ambles */
    float    urchin_appetite;      /* frond height it is due to eat (URCHIN_* in tank.c) */
    float    urchin_chew;          /* seconds at the frond's foot so far */
    float    urchin_to;            /* an amble's destination x, < 0 = resting */
    float    urchin_rest;          /* seconds left of the rest */
    float    urchin_grazed_px;     /* grass it has eaten, lifetime, in the trim's px (VEG_PAY_PX:
                                    * the same length on every board; its card shows cm). Saved */
    float    urchin_dy;            /* px DOWN from URCHIN_FLOOR_Y: in its front lane inside a piece IN FRONT's footprint it eases onto that piece's
                                    * base line (DECOR_FY_FRONT) - in front of the castle it stands on the castle's floor, not over it (Strato 10-24) */
    bool     urchin_front;         /* its LANE: in front of the pieces placed IN FRONT (render's front slot) or behind them; rolled 60/40
                                    * at a move's start, only while it is clear of every such piece (tank_floor_clear) - never a pop */
    /* the strawberry crab (SD_ITEM_CRAB, 0.3.9, 2026-10-23; Vulcanite's crab
     * idea, drawn after Strato's strawberry-crab sheets: render.c draw_crab).
     * A red dome with cream seeds: it walks the sand in profile, digs and
     * nibbles where it stops, comes out for food on the floor, and roosts in
     * the castle's arch or the volcano's cave FACING THE GLASS, tucked in on
     * itself. Its x and its seed pattern (cr_variant, drawn at purchase) are
     * saved; what it is doing is not. The model never sees it. */
    float    cr_x;                 /* its middle x on the floor; < 0 = not placed */
    float    cr_y;                 /* its height over the sand: 0 but on the volcano's flank (CR_CLIMB) */
    float    cr_tilt;              /* radians, the slope under it (the climb); 0 on the sand */
    int8_t   cr_facing;            /* 1 = it faces right, -1 = left (the profile; the roost faces the glass) */
    uint8_t  cr_variant;           /* its seed pattern (saved; any value) */
    uint8_t  cr_mode;              /* CR_* */
    int8_t   cr_cave;              /* the shelter it heads for / sits in: 2 the castle's arch, 9 the volcano's cave, -1 none */
    int8_t   cr_food;              /* the pellet it is after / eating, -1 none */
    int8_t   cr_side;              /* the climb's flank: -1 the left, 1 the right */
    float    cr_to;                /* a walk's destination x */
    float    cr_timer;             /* seconds left in the current mode */
    float    cr_mode_s;            /* seconds in the current mode (render: the roost settles in) */
    float    cr_poke;              /* seconds left of its claws up after a tap */
    float    cr_gait;              /* the gait's phase, radians: advances with the ground it covers (render: the stride) */
    float    cr_climb;             /* the climb's progress: 0..1 up the flank, 1..2 down again */
    float    cr_nibble;            /* seconds left of a stop on the way, nibbling the rock (0.3.9.1); the climb waits */
    uint8_t  cr_nibble_i;          /* the next stop (CR_NIBBLE_AT) this climb */
    float    cr_outing;            /* seconds out of a shelter so far: when enough have passed it looks for one */
    float    cr_since_climb;       /* seconds since it last climbed the volcano (not saved: a rare thing) */
    uint16_t cr_eaten;             /* pellets it has found and eaten, lifetime (saved; its card) */
    uint8_t  cr_fidget_kind;       /* CR_FID_*: the small thing it is doing while it rests, digs or roosts */
    float    cr_fidget;            /* seconds left of it */
    float    cr_next_fidget;       /* seconds until the next one */
    float    cr_shuffle_to;        /* a shuffle's x (CR_FID_SHUFFLE) */
    uint8_t  cr_taps;              /* quick taps so far (tank_crab_tap): the third sends it running */
    bool     cr_scurry;            /* running for a shelter: CR_SCURRY_PX_S, food ignored, until the next mode */
    double   cr_tap_t;             /* the clock at the last tap */
    float    cr_hide;              /* seconds left backed into its shelter's dark (scared) */
    float    cr_back;              /* how far back into the dark it is, 0 (at the mouth) .. 1: eases in over CR_BACK_IN_S while cr_hide runs,
                                    * out over CR_BACK_OUT_S after (render: smaller, higher, darker with it - it BACKS UP, never a switch; Strato 10-24) */
    float    cr_startle_cool;      /* seconds until a fish passing over it can raise its claws again (not saved) */
    float    cr_find;              /* seconds left holding a SAND DOLLAR up for the keeper (0 = none; not saved): a tap on it takes it */
    float    cr_find_cool;         /* seconds of lived time, asleep too, until the next find is due (saved, 0.3.9.1: CR_FIND_*) */
    uint16_t cr_found;             /* sand dollars it has dug up and the keeper took, lifetime (saved; its card, the badge) */
    uint16_t cr_found_paid;        /* ... of which the ledger has paid (saved: a find is never paid twice) */
    bool     cr_was_night;         /* the light at the last tick: it comes out when the light goes off, heads in when it comes on */
    bool     cr_stood;             /* it has stood off and stepped aside for this snail approach: it holds its ground now (the snail turns back) */
    bool     cr_lane_front;        /* its LANE for this walk and the stop after it: in front of the pieces IN FRONT, or behind (render's front
                                    * slot); rolled 60/40 at a walk's start while clear of every such piece; a dig is ALWAYS in front (Strato 10-24) */
    bool     cr_next_dig;          /* the stop this walk ends in digs (decided at the walk's start, so the lane can be chosen for it) */
    int8_t   cr_claim;             /* the pellet it has claimed (0.3.9.1): shooed the shrimp off it, or eating it - not theirs while it holds (-1 none) */
    float    cr_shoo;              /* seconds left standing, claws up at the shrimp, before it walks on to the food */
    uint8_t  cr_meals;             /* pellets since it was last full (not saved) */
    float    cr_full;              /* seconds before a fallen pellet tempts it again (CR_MEALS_FULL, then CR_FULL_*; not saved) */
    /* the seahorse (SD_ITEM_SEAHORSE, 0.3.6, 2026-10-15): the tame one. It
     * holds a grass blade with its tail (a BEHIND-layer frond, so it is never
     * covered), drifts to another now and then, and when a finger rests on
     * the glass it lets go, swims over and curls its tail at the finger's
     * edge - while it hangs there every fish earns trust twice as fast
     * (SEAHORSE_TRUST_X in tank.c: helpers amplify a keeper act, never
     * replace it). Its tally - seconds with the keeper, hitches, the trust it
     * passed on - is saved; where it is and what it is doing is not (a boot
     * puts it back on a blade). The model never sees it (schema v4 frozen). */
    float    sh_x, sh_y;           /* its anchor: the coil's centre on the blade / at the finger / adrift */
    int8_t   sh_bed, sh_frond;     /* a BLADE anchor: the frond it holds, -1 = none */
    uint8_t  sh_akind;             /* what it holds or heads for: SH_ANCHOR_* (tank.h, below) */
    int8_t   sh_aitem;             /* a POST anchor: the shop item it is on (castle 2, coral 3, cluster 4) */
    float    sh_ax, sh_ay, sh_ahw; /* the anchor: the post's centre at the coil row and its half-width
                                    * (a blade's are refreshed from the blade; the sand's is the floor) */
    float    sh_aref;              /* a POST anchor: the piece's x when it took hold (moved = let go) */
    bool     sh_hidden;            /* director `seahorse off`: not drawn (it still lives) - the frame-rate A/B. Not saved */
    bool     sh_front;             /* its LANE: false = drawn with the fish (a hold on something behind them:
                                    * an even frond, a piece placed BEHIND); true = drawn after the pieces in
                                    * front, like the snail's front lane (a hold on an odd frond or a piece IN
                                    * FRONT). Chosen with each hold (Strato 10-15: a seahorse can swim to a
                                    * different z-layer and anchor) */
    bool     sh_front_want;        /* ... the lane its hold asks for (sh_take): taken up clear of every piece, or as it settles
                                    * on the hold - never a pop through a piece (0.4); a held finger's call asks for the front
                                    * (Strato 11-01). Not saved */
    uint8_t  sh_gdepth, sh_gdepth_want, sh_drift_rolled;   /* SWIM DEPTH phase 3 (0.3.9.4): against the back grass - THROUGH coiled on a blade, in front
                                    * holding a piece or the sand, its own roll drifting; taken up clear of every bed (its lane
                                    * stays its choice against the pieces). Not saved */
    int8_t   sh_obed;              /* THE BED'S STACK (0.3.9.7, a keeper's square on #help 11-01 "Seahorse hold": holding a
                                    * seagrass blade behind the castle, a sword plant's leaf behind the volcano, it was drawn
                                    * over the piece): the bed whose frond it is coiled on - every piece that stands before that
                                    * bed (render.c veg_skip_for) stands before it too, whatever its lane. -1 = its lane alone.
                                    * Taken up when it heads for a frond and dropped once it heads elsewhere, either way only
                                    * clear of every piece (nothing pops through one) - or as it settles on the frond. Not saved */
    int8_t   sh_opost;             /* THE PIECE'S STACK (0.4, a keeper's pendant 11-01: holding the castle's rear tower, the
                                    * seahorse was drawn under a volcano the castle stands before): the piece whose post it holds
                                    * (item, -1 none) - it is drawn right before that piece, under every piece that stands before
                                    * it; sh_orow: the castle's rear tower, behind the castle's own front row too. Taken up as
                                    * sh_obed is. Not saved */
    bool     sh_orow;
    uint8_t  sh_mode;              /* SH_PERCH / SH_DRIFT / SH_HITCH (tank.h, below) */
    int8_t   sh_facing;            /* +1 = its snout points right */
    bool     sh_hitched;           /* at the finger's edge now: the trust boost is on */
    bool     sh_credited;          /* this hold already counted a hitch */
    float    sh_tx, sh_ty;         /* where it is heading (drift / the finger) */
    float    sh_vx, sh_vy;         /* its velocity, px/s: it swims with momentum and the water's drag (the
                                    * renderer pitches its head forward with speed). Not saved */
    uint8_t  sh_pose;              /* the tail's last hold: SH_POSE_BLADE / SH_POSE_GRIP (the finger); the
                                    * renderer morphs the open curl into it over sh_land (not saved) */
    float    sh_land;              /* seconds into that hold, 0..SH_LAND_S: counts up settled, down once it
                                    * lets go - so the coil closes and opens instead of snapping (Strato
                                    * 10-15: "jump and click / lock into place when it goes to hitch") */
    float    sh_stay;              /* seconds left on this blade, or of the linger after a hold */
    float    sh_look;              /* on the sand: seconds to its next look for something better (a new
                                    * plant or piece takes it off the bottom at once, not at the rest's end) */
    double   sh_s;                 /* seconds with the keeper, awake and asleep (its card: days). Saved (a float at 1864 for
                                    * older builds, its whole seconds at 1952: a float stops adding a frame at ~12 days) */
    int32_t  sh_hitches;           /* times it has curled on the finger, lifetime. Saved */
    float    sh_trust_passed;      /* trust the fish earned over the base rate while it hung there, lifetime. Saved */
    /* its SNACKS (Strato 10-15): it never chases food like the fish or the
     * shrimp, but a pellet that drifts within SH_REACH of its snout is
     * eaten on the spot. Every snack counts (its card); the first one in
     * SH_SNACK_COOL_S earns SD_SNACK sand dollars (progression.c pays
     * sh_snack_pay against its ledger sd_snacks_paid), the rest in that
     * window are just lunch - so a tap-spammer cannot farm it. All saved. */
    int32_t  sh_eaten;             /* pellets snapped, lifetime */
    int32_t  sh_snack_pay;         /* snacks that earned (the ledger pays these) */
    float    sh_snack_cool;        /* seconds until the next snack earns again */
    float    sh_snap;              /* the SNATCH (Strato 10-15: "its neck / head should jut out slightly to snatch
                                    * it"): seconds into it, < 0 = none; the head juts SH_JUT px forward over
                                    * SH_SNAP_S and the pellet is taken at SH_SNAP_AT, with the fish's eat cue */
    int8_t   sh_snap_food;         /* the pellet it is snatching, -1 once taken (or missed) */
    float    sh_turn;              /* a beat after it turned to face a pellet on its other side, before the snatch */
    /* where the keeper put the decor (2026-09-16, the placement page): the
     * sword plant's centre x along the floor (<= 0 = the default spot) and
     * its depth layer (DECOR_Z_*). Both saved. */
    float    plant_x;
    uint8_t  plant_z;
    /* the castle (2026-09-16): its centre x on the floor (<= 0 = the default
     * spot) and its depth - BACK or FRONT only (see tank_decor_z_count). Both saved. */
    float    castle_x;
    uint8_t  castle_z;
    /* the coral (2026-09-23): its centre x on the floor (<= 0 = the default
     * spot), its depth (all three), and its colour as 0xRRGGBB (0 = the
     * default, CORAL_PAL[0]) - the keeper picks it on the placement page. All saved. */
    float    coral_x;
    uint8_t  coral_z;
    uint32_t coral_rgb;
    float    coral_growth;         /* CORAL_START..CORAL_FULL: a stub to the full fan over ~CORAL_GROW_S,
                                    * then the crown, awake or asleep alike (Strato, 2026-09-23:
                                    * "something at a much different pace showing progression"). Saved. */
    float    coral_acc;            /* awake seconds not yet applied: a frame's 1/60 s over 30 days is
                                    * under float precision next to the growth, so it lands by the minute */
    /* the reef cluster (2026-09-24, Strato's coral-cluster.png): a mature
     * cluster on a rock - the branching coral, purple tube sponges, a brain
     * coral, weed - bought big and still growing: its size first, then more
     * and more swaying tentacles. Its centre x (<= 0 = the default), depth,
     * colour SCHEME (CLUSTER_SCHEME_N preset looks) and growth. All saved. */
    float    cluster_x;
    uint8_t  cluster_z;
    uint8_t  cluster_scheme;
    float    cluster_growth;       /* CLUSTER_START..CLUSTER_FULL; 0..1 = it fills out, 1..FULL = the tentacles come */
    float    cluster_acc;          /* awake seconds pooled, as coral_acc */
    /* the pumpkin lantern (0.3.6, the first seasonal piece): its centre x
     * on the floor (<= 0 = the default spot) and its depth, BACK or FRONT.
     * Both saved. */
    float    pumpkin_x;
    uint8_t  pumpkin_z;
    /* the bubble volcano (0.3.9): its centre x on the floor (<= 0 = the default
     * spot) and its depth, BACK or FRONT. Both saved. */
    float    volcano_x;
    uint8_t  volcano_z;
    /* the ORDER of the pieces (2026-10-17, Strato: a stack of the pumpkin,
     * the grass, the sword plant and a coral broke): every placeable piece
     * has a rank; among pieces at one depth the higher rank is in front.
     * By slot (tank_decor_slot): castle, coral, cluster, pumpkin, plant,
     * volcano (DECOR_SLOTS). Saved. */
    uint8_t  decor_rank[6];
    /* the shrimp school (SD_ITEM_SHRIMP, see SHRIMP_*): how many (saved), the
     * pellets eaten toward the next one (0..SHRIMP_PER_JOIN, saved), the seconds
     * before another may join (saved), each shrimp's motion (not saved: a load
     * places them in the grass), and the school's current goal and its timer. */
    uint8_t  shrimp_n;
    uint8_t  shrimp_food;
    float    shrimp_cool;
    int32_t  shrimp_eaten;         /* pellets the school has eaten, lifetime (its card; saved) */
    uint8_t  shrimp_taps;          /* quick taps on the school in a row (the third scares them; not saved) */
    float    shrimp_tap_t;         /* seconds since the last one */
    float    shrimp_flee;          /* seconds left of the school's flight from the crab (0.3.9.1): SHRIMP_FLEE_X its cruise, food ignored (not saved) */
    uint8_t  cr_gdepth, cr_gdepth_want, snail_gdepth, snail_gdepth_want, urchin_gdepth, urchin_gdepth_want;   /* SWIM DEPTH phase 3c (0.3.9.4): the
                                   floor critters against the back grass, wanted with each lane's roll, taken up clear of every bed (their LANES
                                   stay their choice against the pieces; the sword plant IN FRONT stays over them). Not saved */
    uint8_t  shr_gdepth, shr_gdepth_want, shr_pdepth, shr_pdepth_want;   /* SWIM DEPTH phase 3 (0.3.9.4): the school's depth against the back grass
                                   (FISH_DEPTH_*) and the pieces, with each goal; taken up only with the whole school clear (not saved) */
    float    shrimp_avoid_x;       /* where the crab shooed them: no pellet within CR_SHOO_ZONE_PX of it is theirs, no goal of theirs in it ... */
    float    shrimp_avoid_s;       /* ... for this many seconds more (not saved) */
    int8_t   shrimp_avoid_side;    /* ... and the side of it the shoo sent them to (+1 right, -1 left): their goals stay there */
    shrimp_t shrimp[SHRIMP_MAX];
    float    shrimp_tx, shrimp_ty, shrimp_tt;
    int8_t   shrimp_fp;            /* the pellet the school is after this frame (-1 none): the crab reads it (0.3.9.1) */
    /* keeper habits the tank remembers (persisted by progression.c) */
    float    feed_spot_x;          /* where the keeper usually feeds (EMA); <0 = unknown */
    int      player_feedings;      /* MEALS: feedings the fish ate from (2026-09-14, Strato: a tap
                                    * nobody eats from is not a meal) - a feed gesture opens one,
                                    * the first player pellet eaten after it counts it */
    bool     feed_open;            /* a feed gesture not yet eaten from (transient, not saved) */
    int      hold_approaches;      /* calm holds that drew a fish all the way in */
    float    greet_timer;          /* light-on greeting: trusting fish come up front */
    /* courtship tell (progression.c decides, tank.c performs): when the tank
     * is one care-condition away from earning an arrival - or one is already
     * staged - the two most-trusting grown fish occasionally circle together
     * near the reef. "Something is close", said in fish. */
    bool     courting;
    int8_t   court_a, court_b;     /* the parents-to-be (-1 = fewer than 2 grown fish) */
    float    court_cool;           /* seconds until the next courtship episode */
    float    court_active;         /* seconds left of the current episode */
    /* the spawning (2026-09-24): a staged arrival is born in front of the
     * keeper, not at a light-on. progression.c waits a few awake seconds,
     * then sets `spawning`: the courting pair dives into the nursery grass
     * and circles there with no episode clock; tank.c counts `spawn_danced`,
     * the seconds both have spent circling in the fronds, and progression.c
     * delivers the fry at SPAWN_DANCE_S. Not saved (a staged arrival is; a
     * wake delivers it at once). */
    bool     spawning;
    float    spawn_danced;
    bool     ravenous;             /* starving tank: with empty water the fish
                                    * beg at the surface; the moment pellets
                                    * land they DASH for them (feeding frenzy).
                                    * Trickle holds off throughout. progression.c
                                    * owns entry/exit; tank.c renders both
                                    * phases; ends when everyone has eaten. */
    bool     hold_light;           /* platform: a setup page or a prompt is up - the
                                    * light stays on however still the device is
                                    * (2026-09-13, Strato: the tank went dark mid-name).
                                    * Not saved; a light override still wins. */
    bool     ui_cover;             /* platform: a page covers the tank (setup, a prompt,
                                    * milestones, settings, the shop) - a fry's spawning
                                    * waits, so the keeper never misses it (2026-09-24).
                                    * Set every frame; not saved. */
    uint8_t  roster_gen;           /* ticks when a fish leaves and the slots shift (tank_remove_fish):
                                    * the advisors drop what they were asked about the old slots.
                                    * Not saved. */
    int8_t   removed_slot;         /* the slot the fish left from at the last roster_gen tick (-1: that tick was an
                                    * arrival): a platform's own slot (the stat card's fish) follows its fish. Not saved. */
    int8_t   stage_fish;          /* setup: this fish is being named / coloured - it swims
                                    * a slow loop at (stage_x, stage_y), the clear spot the
                                    * page leaves for it, so it is never behind the UI
                                    * (2026-09-13). -1 = nobody. Not saved. */
    float    stage_x, stage_y;
    bool     trickle_off;          /* director/test knob: the tank's own trickle
                                    * holds off entirely (staged hunger for a
                                    * shot). Not saved. */
    bool     ravenous_fed;         /* the keeper HAS fed during this episode: whoever
                                    * is still starving keeps begging, but the trickle
                                    * no longer holds off (one fish gobbling every
                                    * pellet must not leave a slower one begging for
                                    * the whole give-up valve). progression.c sets it. */
    uint32_t tank_ms_bits;         /* TMS_* milestones reached */
    uint32_t tank_ms_seen;         /* TMS_* looked at on the milestones page */
    /* advisor scheduling (need-based, see tank_tick) */
    int      ask_rr;               /* rotating start index for fairness */
    uint32_t advisor_asks;         /* diagnostic: requests issued */
    uint32_t rng;                  /* xorshift state, deterministic */
    uint32_t gene_rng;             /* heredity's own stream (0.4): genes never move what rng draws */
    bool     mutate_next;          /* the next fry is a mutation, whatever the odds (the director's `arrive mutant`;
                                    * not saved, cleared by that birth) */
    uint32_t uid;                  /* this tank's ID (0.4, the tank link): random, never 0; see fish_t.uid */
    /* the tank link's side of the tank (tank_link.c): which side doors are open
     * (and not paused), the visitors, a boot's restore of fish that were away */
    bool     door_open[2];         /* [0] left, [1] right (link.h LINK_LEFT / LINK_RIGHT) */
    bool     vis_used[N_VISIT_MAX];
    uint32_t vis_home[N_VISIT_MAX];  /* the visitor's home tank */
    char     vis_home_name[N_VISIT_MAX][16];   /* ... what its keeper named it ("" = a default name; passport v2's home_name) */
    float    vis_age[N_VISIT_MAX];   /* its tended age (its passport's, growing here) */
    double   vis_since[N_VISIT_MAX]; /* tank clock it arrived */
    int8_t   vis_badge[N_VISIT_MAX]; /* its home tank's badge (link.h LINK_BADGES), drawn small above it; -1 = not known */
    /* cross-tank fry (phase 5, docs/tank-link-0.4.md). Host side: the visitor
       picked to court one of ours (0 = none), whether its home has room
       (-1 unknown), the visitors already given their coin since the last
       birth; a fry born here for the visitor's home waits in visitor slot
       fry_give_k (vis_pending: not swimming, not drawn) until its home says */
    uint32_t deport_uid;         /* a DEPORT for tank_link to tell the engine (refused at our door a while) */
    int8_t   link_badge;         /* this tank's badge while the link is up (-1: none) - the passport card's stamp */
    uint32_t fry_partner;
    uint32_t fry_mate;           /* the host fish the partner grew close to (time together), 0 = our best */
    double   fry_ask_at;         /* tank clock of the last room ask (asked again every TANK_LINK_FRY_ASK_S, yes or no) */
    double   fry_room_at;        /* ... of its home's last answer: a yes older than TANK_LINK_FRY_FRESH_S is not known any more */
    uint32_t fry_ask_uid;        /* the visitor last asked about: when the pair is off, its home lets go of the place */
    /* time together (Strato 2026-10-06): a visitor and the fish of ours it
       swims beside build up time, across visits; at a goal of its own (5-10
       min, random) the pair may court - the cross-tank fry's own path, the
       host's care gates not needed. Not saved */
    struct { uint32_t vis, mate; float s, need; } bond[6];
    int8_t   fry_home_room, fry_give_k;
    int8_t   fry_force;          /* the director / selftests: the next cross-tank fry's coin (1 = its home's, 0 = ours, -1 = the coin) */
    bool     fry_asked, fry_give_sent;
    uint32_t fry_give_parent;
    uint32_t fry_rolled[N_VISIT_MAX];
    bool     vis_pending[N_VISIT_MAX];
    /* home side (2026-11-03, the cap): a place HELD for a fry of ours courting next door - the host asked for room and
       we said yes. fish_held counts it, so nothing else takes it (a birth here, a gift, another fry, a shop fry). It
       ends when the fry comes (LEV_FRY_IN), when the host says the pair is off, when that fish of ours is no longer
       next door at that tank, or TANK_LINK_FRY_HOLD_S after the host's last ask. One a neighbor (a host courts one
       visitor at a time); not saved (a boot opens no doors) */
    struct { uint32_t peer, parent; double until; } fry_hold[TANK_FRY_HOLDS];
    int8_t   cap_over;           /* a save that came back with more than POP_CAP fish (an older build's leak - the sim ran 6,
                                    a cross-tank fry made a sixth): its count, which THE CAP's guard allows until it falls
                                    to the cap (fish leave on their own: a sale, a gift); 0 = none. Nobody is taken away */
    double   link_restore_at;      /* > 0: at this clock, fish still away come home from bubbles (a boot found them away) */
    uint32_t uid_n;                /* IDs this tank has handed out (mixed into the next when no entropy hook is set) */
} tank_t;

/* Advisor interface (Track 1 model, rule stub, or firmware LLM core).
 * Called EVERY frame for every live fish. `request` is true when the tank
 * wants a (re)decision: the fish's coarse state signature changed and the
 * minimum interval passed, it hit the idle ceiling, or something urgent
 * happened (starving with food in view). When false it's
 * a poll: an async advisor returns a completed decision the moment it's
 * ready; otherwise return the fish's current goal unchanged. */
typedef goal_t (*advisor_fn)(const tank_t *t, int fish_idx, bool request);

#define ADVISOR_MIN_INTERVAL 2.0f   /* s between asks for one fish (signature changed) */
/* s: re-ask even if nothing changed. 9 s kept the device's LLM core saturated
 * (a decision every 3.65 s back to back, even with two fish) for answers that
 * repeat the standing goal ~90% of the time; 45 s (with the calmer signature
 * in state_signature) leaves the core idle most of the time on battery.
 * Reactions still come through the 2 s change path - hunger band, food
 * appearing, night - and the urgent path (2026-09-11). */
#define ADVISOR_IDLE_CEILING 45.0f   /* 5 fish x 3.65 s per decision at 25 s would still be 70% busy */

/* boredom (fish_t.bored; the same numbers live in model/gen_traces.py) */
#define BORED_PER_S        0.15f  /* 0 -> 9 in a minute on one pastime */
#define BORED_NEW_GOAL     4.0f   /* relief for a goal that is not the one just left */
#define BORED_NEW_ZONE     1.5f   /* relief for entering a zone unseen for BORED_ZONE_STALE_S */
#define BORED_ZONE_STALE_S 20.0f
#define BORED_RELIEF_PER_S 0.3f   /* while eating, or resting at night */

void  tank_init(tank_t *t, uint32_t seed);
/* identity (0.4): IDs come from tank_entropy when the platform sets it (the
 * firmware: the chip's hardware RNG; the sim: the OS), else from a mix of the
 * tank's seed, clock and count that never draws from t->rng - a seeded run
 * (the selftests) draws the same numbers it always has. Never returns 0,
 * never an ID already in this tank. */
extern uint32_t (*tank_entropy)(void);
uint32_t tank_new_uid(tank_t *t);
/* every fish (and the tank) without an ID gets one, and a fish whose parents
 * live here learns their IDs from its parent slots: after a load (a pre-0.4
 * save has none) and after a fresh tank */
void  tank_ensure_ids(tank_t *t);
/* is fish slot i swimming in this tank now: a home fish not away, or a visitor */
static inline bool tank_swims(const tank_t *t, int i) {
    return i < t->n_fish ? !t->fish[i].away : (i >= N_FISH_MAX && i < N_FISH_SLOTS && t->vis_used[i - N_FISH_MAX]);
}
static inline bool tank_is_visitor(int i) { return i >= N_FISH_MAX && i < N_FISH_SLOTS; }
/* THE CLOCK'S DRAWING FACE (0.4): t->clock is a double, and every hold, wait and stamp reads it as one. A drawing
 * takes it as a float that laps every TANK_ANIM_WRAP = 200 pi s (~10.5 min), where a float still splits a second
 * 16000 ways and sinf's argument stays small. The rates it drives - a sway's 1.3, a flicker's 11.3, a wobble's 0.37 -
 * are tenths and hundredths of a radian a second: a lap of 200 pi s is a whole number of their turns, so it is
 * seamless. A rate that is not (a beat with the speed in it, a period of 2.9 s) takes its phase from the double:
 * tank_phase, or keeps its own (fish_t.beat). */
#define TANK_ANIM_WRAP 628.31853071795864769
static inline float tank_anim(double clock) { return (float)fmod(clock, TANK_ANIM_WRAP); }
static inline float tank_phase(double clock, double rate) { return (float)fmod(clock * rate, 6.28318530717958648); }   /* radians, 0..2 pi */
/* the door band: the middle of the water at the side glass, where a fish may
 * pass when the door is open; and the poof (a burst of bubbles) */
#define LINK_BOOT_RESTORE_S 10.0f /* a boot that finds fish away: home from bubbles this long after (link.h LINK_RESTORE_MS) */
#define DOOR_BAND_H   (TANK_BOT * 2 / 5)
#define DOOR_Y        (TANK_BOT * 0.5f)
/* the roster's per-preset body (a fish arriving in a passport is rebuilt
 * from its preset like any other fish) */
float tank_roster_size(int preset);
float tank_roster_turn(int preset);
/* Population. tank_init leaves the tank empty (n_fish = 0); the progression
 * layer either restores a save or calls tank_new_population for a fresh tank:
 * two random roster presets, personalities rolled with a guaranteed contrast,
 * adults. tank_add_fish appends the next unused preset (name, temperament)
 * as a fry whose bold/social are inherited from two live fish (+ noise) and
 * whose look is theirs too - body from one, markings from the other (the
 * fish records which in parent_a / parent_b); returns the index or -1 if
 * the tank is full. */
void  tank_new_population(tank_t *t);
int   tank_add_fish(tank_t *t, int parent_a, int parent_b);
/* a fry from the shop (0.3.5): no parents, the offered look, in at the
 * surface; the slot, or -1 with no room */
int   tank_add_bought_fry(tank_t *t, uint32_t body, uint32_t accent);
/* a fish leaves the tank (2026-10-01, the milestones card's SELL): the fish
 * after it move down a slot - the order stays the order of arrival, so the
 * last fish is still the youngest - and everything the tank keeps by slot
 * moves with them (the sand dollar ledger, the others' parent slots: a child
 * of the fish that left reads "none", their spots by the reef). The courtship
 * and a spawning under way are dropped; roster_gen ticks so an advisor can
 * drop a decision asked for the old slots. false = no such fish. The caller
 * (progression_sell_fish) keeps the rules: who may leave, and its own
 * per-slot state. */
bool  tank_remove_fish(tank_t *t, int idx);
/* (re)build slot from a roster preset: used by persistence to restore a fish */
void  tank_make_fish(tank_t *t, int slot, int preset, float sociable, float bold, stage_t stage);
/* face the fish the way its heading points, at once (no turn plays): for
 * code that places a fish by hand - the sim's scenes, the wake restore */
void  tank_fish_face(fish_t *f);
void  tank_tick(tank_t *t, float dt, advisor_fn advise);
/* Sleep metabolism (device drowse mode: screen dark, fish asleep). Advances
 * ONLY slow physiology - hunger up, energy recovered, stress gone - at
 * real-hours scale; no movement, no goals, no eating, no trickle. Sleeping
 * fish make no decisions, so the advisor contract is untouched. Safe to call
 * with hours at a time. */
void  tank_tick_sleep(tank_t *t, float seconds);
/* Tank light (2026-09-15). It is on while the device is being handled and
 * goes off LIGHT_IDLE_S after the last handling - the tank sitting still on
 * a desk is night: the fish rest, the palette dims, the panel dims. The
 * platform calls tank_handled whenever the device moves (the IMU's motion
 * detector) - a touch counts by itself (every tank_touch_* / tank_feed call
 * is handling) - and a setup page or prompt holds the light (hold_light).
 * tank_toggle_light / tank_light_auto are the director's and the sim's
 * manual override (b-roll, tests); never saved. */
/* Settings' LIGHTS OUT = MANUAL (light_auto) turns the idle rule
 * off and gives the keeper the old double-tap instead: two quick taps on the
 * glass, then a pause, flip light_manual_off. AUTO clears it. */
#define LIGHT_IDLE_S     15        /* the default, seconds (tank_t.light_idle_s) */
#define LIGHT_IDLE_MIN_S 5
#define LIGHT_IDLE_MAX_S 1800      /* the settings page's longest choice, 30 min (0.3.2: its choices are
                                      LIGHT_IDLE_CHOICES; until then a wheel of 5..999 s) */
/* the settings page's LIGHTS OUT choices (0.3.2): the double-tap (MANUAL,
 * the default), or AUTO after one of these many seconds still. A save from
 * the wheel's days holds any 5..999: it is honoured as it is and shown as
 * the nearest choice until the keeper steps the row. */
#define LIGHT_IDLE_N 8
extern const int LIGHT_IDLE_CHOICES[LIGHT_IDLE_N];
int   tank_light_choice(const tank_t *t);            /* 0 = MANUAL, 1..LIGHT_IDLE_N = AUTO after LIGHT_IDLE_CHOICES[n - 1] */
void  tank_light_choice_set(tank_t *t, int choice);  /* (clamped; the light comes on either way) */
void  tank_handled(tank_t *t);
void  tank_toggle_light(tank_t *t);
void  tank_light_auto(tank_t *t);

/* The picture's way up on a WORN tank (TANK_WORN, 2026-10-02). A watch can
 * go on either wrist, buttons toward the hand or the elbow: the second way
 * shows the tank upside down. Which way it is worn does not change through
 * the day, so it is the keeper's setting - settings SCREEN: NORMAL / TURNED -
 * not the live IMU flip the desk tanks run. (An AUTO that learned it from the
 * IMU at each tap was built and dropped the same day: the tilt of a wrist
 * raised to read it depends on the arm, not on the way the watch is strapped
 * on - Strato's own wrist read "top down" both ways round.)
 * tank_screen_set: the settings page's choice (save it:
 * progression_settings_changed). Every other build: never turned. */
bool  tank_screen_turned(const tank_t *t);
void  tank_screen_set(tank_t *t, bool turned);
/* The way up on a desk tank or the pendant (0.3.2, @brandonn5371's ask): the
 * picture follows the IMU unless settings' ROTATION is locked. The platform
 * hands tank_orient its live reading every frame - quarter turns clockwise,
 * 0..3 - and shows what comes back: the reading itself, or - locked - the way
 * up the picture had at the lock (saved, so a locked tank boots the same way
 * up). tank_orient_lock is the settings page's toggle. A quarter on a side (1,
 * 3) needs a square picture (0.3.9.3: the square and the pendant stood on a
 * side); elsewhere it is read as its turned-over half (TANK_QUARTER). */
#define TANK_QUARTER(q) ((TANK_W == TANK_H) ? ((q) & 3) : ((q) & 2))
int   tank_orient(tank_t *t, int live_q);
void  tank_orient_lock(tank_t *t, bool lock);
/* a point between the picture and the glass held upright (q = the picture's
 * quarter turns): the touch port's calibration lives upright, the taps on the
 * picture as shown */
void  tank_quarter_from_upright(int q, float *x, float *y);
void  tank_quarter_to_upright(int q, float *x, float *y);

/* Touch input (platform feeds these; sim = mouse, device = FT3168):
 *  tank_touch_hold: call EVERY FRAME while a finger rests on the glass at x,y.
 *    After ~3 s of contact, high-trust fish drift over to investigate (the
 *    delay keeps taps from twitching the school); low-trust or
 *    strongly hungry fish keep to their own business.
 *  tank_touch_tap:  call once per tap. A tap on the water surface (y below
 *    FEED_ZONE_Y) is a FEED gesture: pellets drop there, nothing else happens.
 *    Elsewhere: 2 quick taps then a pause flip the light - only with settings'
 *    LIGHTS OUT on MANUAL (light_auto); 3+ quick taps =
 *    aggressive -> nearby fish flee the spot and stay spooked while taps
 *    continue (cooldown resets on each tap); after the cooldown, single taps
 *    are harmless again and it takes 3 quick taps to re-trigger. Spooking
 *    costs trust; calm holds earn it.
 *  tank_feed: the keeper drops n pellets at x (surface). Player feeding is what
 *    progression counts; the tank's own trickle feed is not. */
#ifdef TANK_ROUND
#define FEED_ZONE_Y 45.0f   /* the bowl's surface is its rim: 26 px there is 2.5 mm of glass and a fingertip scatters 9 px (2026-10-03) -
                               a feed tap that fell short was a tank tap, and two of them the light's double tap */
#else
#define FEED_ZONE_Y 26.0f
#endif
void  tank_touch_hold(tank_t *t, float x, float y);
void  tank_touch_tap(tank_t *t, float x, float y);
/* tank_touch_drag: call EVERY FRAME while a finger is down at x,y (moving or
 * not; tank.c tracks travel). Once a stroke has moved far enough it becomes a
 * WIPE: algae cells along the path are squeegeed clean. A mostly-HORIZONTAL
 * stroke that STARTS on a vegetation bed is a SLASH: every frond it crosses
 * is cut to the height where the stroke crosses it (a lower pass cuts again;
 * nothing ever cuts below nubs) - a short sideways flick takes one or two
 * fronds, a sweep along the floor mows the bed. The finger is a pad: the
 * stroke reaches a few px past where it lands and where it lifts, so the
 * outer frond of a bed by the glass falls to a sweep that stops just short
 * of its spine (2026-09-14). Deliberately more travel than a tap, so aiming
 * at a fish can never shear the garden. */
void  tank_touch_drag(tank_t *t, float x, float y);
/* the toolbox (2026-10-01; folks found it too hard to scrub algae without
 * shearing the grass): a keeper who wants a precise hand picks a tool from
 * the box under the stats card. SPONGE: a stroke wipes and never cuts.
 * SCISSORS: a stroke cuts and never wipes, and it need not begin on a frond -
 * any mostly-sideways stroke cuts every frond it crosses, at the height it
 * crosses it. A tap still selects, feeds, flips the light. */
enum { TOOL_HAND = 0, TOOL_SPONGE, TOOL_SCISSORS };
#define TOOL_IDLE_S 120.0f
void  tank_set_tool(tank_t *t, int tool);
/* the LOCK beside the toolbox (0.3.5, 2026-10-14, Strato: "people only want
 * to be able to trim or clean algae when they are selected"): locked, a
 * bare-finger stroke neither wipes nor cuts - the guessing stroke is off and
 * only the SPONGE and the SCISSORS tend the glass. Taps, the drag-down feed
 * and the hold are untouched. Unlocked (the default, and every older save)
 * is the guessing stroke as before. Saved. */
void  tank_lock_tools(tank_t *t, bool lock);
/* a finger on the glass that has travelled like a stroke, not a press that
 * may yet be a tap (WIPE_ENGAGE_PX): the DONE chip steps aside for it */
bool  tank_stroking(const tank_t *t);
void  tank_feed(tank_t *t, float x, int n);

/* Diagnostic: episodes where a starving fish ignored available food >4s.
 * Never alters behavior - the advisor owns every decision. */
extern int tank_reflex_overrides;
void  tank_scatter_food(tank_t *t, int n);      /* the tank's own trickle (random x) */
/* upkeep hooks. tank_veg_bed gives bed b's canopy geometry - x span, top y
 * of its tallest frond, frond count - and tank_veg_frond one frond's spine x
 * and segment count, shared by render (what you see) and tank.c physics
 * (slow swimming inside, the cut test), so they can't drift apart.
 * tank_veg_set puts every frond of a bed at height g (tests, the sim's demo
 * key, old saves). tank_grow_algae runs n growth steps now (the same steps
 * time runs on its own; tests and the sim's demo key use it directly). */
void  tank_veg_bed(const tank_t *t, int b, float *x0, float *x1, float *top_y, int *fronds);
int   tank_veg_frond(const tank_t *t, int b, int i, float *x);
void  tank_veg_set(tank_t *t, int b, float g);
/* the tallest bed at VEG_NURSERY or better, -1 if none (progression gates
 * courtship and arrivals on it; tank.c stages the courtship there) */
int   tank_nursery_bed(const tank_t *t);
/* is (x, y) inside a SEAGRASS canopy with real cover (a bed past VEG_BARE;
 * the sword plant is no grass)? progression's MS_FIRST_GRASS asks */
bool  tank_in_grass(const tank_t *t, float x, float y);
/* the fish's "reef" (the model's `reef` sighting, the inspect_reef goal): the
 * reef cluster once the keeper owns one - the point a fish circles, by its
 * upper half - and until then the old spot over the grass corner
 * (reef_x, reef_y), which is still where the fish rest and the left bed
 * stands. The fish circle REEF_ORBIT_UP above the point. */
#define REEF_ORBIT_UP 35.0f
void  tank_reef_spot(const tank_t *t, float *x, float *y);
/* a flirt of n bubbles rising from where the pair courts (the nursery) */
void  tank_court_puff(tank_t *t, int n);
/* the share of the glass wearing film, 0..1 (cells with any algae over all
 * cells - what the keeper sees covered, not how thick). > ALGAE_DIRTY = a
 * dirty tank: the fry checklist's GLASS gate (progression.c) */
float tank_algae_cover(const tank_t *t);
void  tank_veg_sync(tank_t *t);                 /* veg_growth[] from veg_h[][] (after a load) */
float tank_veg_cap(int b, int i);               /* frond i of bed b: the height it grows toward */
void  tank_grow_algae(tank_t *t, int steps);

/* helpers shared with advisor/render/progression */
float tank_dist(float ax, float ay, float bx, float by);
int   tank_nearest_food(const tank_t *t, const fish_t *f, float *dist_out);
int   tank_nearest_friend(const tank_t *t, int fish_idx, float *dist_out);
/* fish i has come to a resting finger (the hold is drawing fish, and it is at the finger's edge) */
bool  tank_at_finger(const tank_t *t, int fish_idx);
float tank_randf(tank_t *t, float lo, float hi);
/* roster preset count (6) and a preset's display name, for UI */
int   tank_roster_count(void);
const char *tank_roster_name(int preset);
/* the keeper's say over a fish's identity (first-run setup, 2026-09-13; the
 * birth flow names an arrival, 2026-09-14). tank_set_name copies up to FISH_NAME_MAX
 * chars (empty = back to the preset's name); tank_set_look sets the body and
 * accent colours - the fin follows the body (the preset's own fin when the
 * body is a roster colour, a darkened body otherwise - a LIGHTENED one for
 * a dark body, whose darkened fins would vanish in the water). ONE palette
 * for the body and the markings (0.4, Strato: so SWAP can trade them
 * exactly, and a fry inherits exactly what it shows): the six roster bodies,
 * blue, silver and charcoal; the roster's markings are on it too, so an
 * untouched fish always sits on a swatch. LOOK_BODY / LOOK_ACCENT name it
 * for the row it fills. */
extern const uint32_t LOOK_COLORS[LOOK_N];
#define LOOK_BODY   LOOK_COLORS
#define LOOK_ACCENT LOOK_COLORS
void  tank_set_name(tank_t *t, int slot, const char *name);
/* a gene's shown value (GENE_*: the colours as their palette index) and
 * setting it (the colours through tank_set_look: the fin follows the body,
 * the markings never match it) */
int   tank_gene(const fish_t *f, int g);
void  tank_set_gene(tank_t *t, int slot, int g, int v);
/* roll the copies a fish carries at random (a founding fish: the keeper
 * designs what it shows, the tank keeps a secret; a fish from before 0.4,
 * once, at its first 0.4 load) */
void  tank_roll_carried(tank_t *t, int slot);
/* the form's turn factor (TURN_*; 1 for a classic) */
float tank_form_turn(const fish_t *f);
float tank_form_trail(const fish_t *f);              /* 0 (CLASSIC x CLASSIC) .. 1: how far its tail and fins trail the body */
void  tank_fish_trail(fish_t *f, float dt);          /* the trail's springs, a step (the swim's last word) */
float tank_fish_beat_rate(const fish_t *f);          /* the tail beat's radians a second: faster the faster it swims, by its form */
extern bool tank_trail_off;                          /* the sim's A/B (--fluid): the rigid fish of 0.3, tail yaw and all */
void  tank_set_look(tank_t *t, int slot, uint32_t body, uint32_t accent);
uint32_t tank_fin_for(uint32_t body);   /* the fin tank_set_look gives that body (the shop's fry previews) */
/* the bubble column is the keeper's to place (first-run setup, 2026-09-13):
 * x is clamped clear of the grass corner (the fish's resting spot) and the glass (grass is fine), the
 * column's live bubbles shift with it, and everything that knows the column
 * - the play loop, the advisor's sighting, the milestone, the airstone -
 * reads tank_t.bubble_x. Saved per tank; the model only ever sees the
 * column as a distance bucket and a bearing, never a position. */
#define BUBBLE_X_DEFAULT (TANK_FX0 + TANK_FW * 0.8f)
void  tank_set_bubble_x(tank_t *t, float x);

/* ---- the shop (2026-09-15): sand dollars buy things for the tank ----
 * The items are bits in tank_t.sd_unlocks; progression.c sells them
 * (progression_buy) and tank.c gives them their place. A bought thing is the
 * keeper's for good: in the tank, or on the shelf (sd_shelved, 0.3.9) until
 * they put it back. */
enum { SD_ITEM_PLANT = 1u << 0, SD_ITEM_SNAIL = 1u << 1, SD_ITEM_CASTLE = 1u << 2, SD_ITEM_CORAL = 1u << 3, SD_ITEM_CLUSTER = 1u << 4, SD_ITEM_SHRIMP = 1u << 5,
       SD_ITEM_URCHIN = 1u << 6, SD_ITEM_SEAHORSE = 1u << 7, SD_ITEM_PUMPKIN = 1u << 8, SD_ITEM_VOLCANO = 1u << 9, SD_ITEM_CRAB = 1u << 10,
       SD_ITEM_COUNT = 11 };
/* per-fish paid bits (sd_paid_fish) */
enum { SD_PAID_JUV = 1u << 0, SD_PAID_ADULT = 1u << 1, SD_PAID_ELDER = 1u << 2, SD_PAID_TRUST = 1u << 3 };
#define PX_PER_INCH 24.0f          /* the tank reads as ~15 in tall; a fish ~1.7 in */
#define PX_PER_CM   (PX_PER_INCH / 2.54f)   /* the shop pays grass by the centimeter (SD_TRIM_CM) */
/* the live bed count: VEG_BEDS, or VEG_BEDS_MAX with the sword plant bought
 * AND in the water (a plant on the shelf is no bed: not drawn, not cover, not
 * cut, not grazed - but it still grows, see veg_grow); every loop over beds
 * runs to this. tank_veg_kind is the species (render). */
int   tank_veg_beds(const tank_t *t);
veg_kind_t tank_veg_kind(const tank_t *t, int b);
/* the purchases land: the sword plant at VEG_START on the open floor; the
 * snail on the glass, bottom left */
void  tank_plant_place(tank_t *t);
void  tank_snail_place(tank_t *t);
/* the shrimp: the bought school (SHRIMP_START in the grass), or n of them
 * (a load, the sim's key); refusing = the glass is too fouled for them to eat */
void  tank_shrimp_place(tank_t *t, int n);
bool  tank_shrimp_refusing(const tank_t *t);
/* a tap on the school (2026-09-29, Strato: "one tap should bring up a card
 * ... it should be a triple tap which scares them (same as for fish)"). Hit:
 * within a fingertip of any shrimp (the platforms test the fish and the snail
 * first). Tap: counts quick taps on the school - its own count, so a double tap
 * there never toggles the light - and returns the count: 1 = toggle its card,
 * 2 = nothing, 3+ = the startle, exactly the water's triple tap (the fish
 * spooked, the shrimp scatter): the platform drops the card. */
bool  tank_shrimp_hit(const tank_t *t, float x, float y);
int   tank_shrimp_tap(tank_t *t, float x, float y);
void  tank_castle_place(tank_t *t);
void  tank_coral_place(tank_t *t);
void  tank_cluster_place(tank_t *t);
void  tank_pumpkin_place(tank_t *t);
void  tank_volcano_place(tank_t *t);
/* placing the decor (2026-09-16, Strato: a bought piece "should allow the
 * player to place the piece wherever they like", with a depth choice): a
 * placeable item has a centre x along the floor - clamped inside the
 * visible window, DECOR_MARGIN from the glass - and a depth layer: BACK =
 * behind the fish and the grass, MIDDLE = woven with them (alternate fronds
 * in front, the beds' own look), FRONT = over everything. The plant is the
 * one placeable item so far; the snail goes where it likes. setup.c's
 * placement page and the shop's MOVE button drive these; the save keeps them. */
enum { DECOR_Z_BACK = 0, DECOR_Z_MIDDLE = 1, DECOR_Z_FRONT = 2, DECOR_Z_N = 3 };
/* ONE STACK (render.c dep_draw): how much of a piece at depth z stands over an animal at grass depth g and piece depth p - 0
   none, 1 the castle's front row (it swims between the keep and the wall), 2 all of it. A piece IN FRONT stands before the
   grass, so behind or through the grass is behind it too - the castle's keep included (0.3.9.7, #help "Castle Tower
   Oddness": a fish behind the grass drawn over the rear tower, the fronds that tower hides drawn over the fish); a piece
   BEHIND stands behind the grass, so only behind both. A change of either depth that changes this over a piece is a pop
   (tank.c pieces_keep, 0.3.9.7) */
static inline int depth_piece_cover(int z, bool castle, int g, int p) {
    const bool behind = z == DECOR_Z_FRONT ? (g != FISH_DEPTH_FRONT || p == FISH_DEPTH_BEHIND) : (g == FISH_DEPTH_BEHIND && p == FISH_DEPTH_BEHIND);
    return !behind ? 0 : castle && !(z == DECOR_Z_FRONT && g != FISH_DEPTH_FRONT) ? 1 : 2;
}
#if defined(TANK_ROUND) || defined(TANK_WATCH)
#define DECOR_MARGIN    8                  /* the bowl's floor is short and its glass hides nothing (the watch's floor ends where its corners begin) */
#elif defined(TANK_SQUARE)
#define DECOR_MARGIN    14                 /* the square's box hides little of its glass: in from the corner arcs */
#else
#define DECOR_MARGIN    30                 /* the snail's margin: inside the panel's rounded bezel */
#endif
#define PLANT_HALF_W    21                 /* four leaves at a 14 px pitch: centre to the outer leaf */
#ifdef TANK_ROUND
#define PLANT_X_DEFAULT 218.0f             /* the gap between the reef bed and bed 2 on the bowl's floor */
#elif defined(TANK_WATCH)
#define PLANT_X_DEFAULT 196.0f             /* the gap between the reef bed and bed 2 on the watch's floor */
#else
#define PLANT_X_DEFAULT (208.0f + PLANT_HALF_W)   /* the open floor between the reef bed and bed 2 */
#endif
/* the castle (2026-09-16, Strato's castle-v2 mockup, drawn procedurally in
 * render.c): ~184 px wide on the floor, a swim-through arch. Its depths are
 * BEHIND and IN FRONT only (Strato: "no among"), and they mean the PLANT
 * LAYER: BACK = behind the grass and the fish, a backdrop the fish pass in
 * front of; FRONT = in front of the grass, and the fish swim THROUGH the arch
 * (the keep behind them, the gate wall and the front towers over them). */
#define CASTLE_HALF_W   92
#define CASTLE_X_DEFAULT TANK_FLOOR_X(300.0f)
/* the coral (2026-09-23, Strato's coral-single.png, drawn procedurally in
 * render.c): a branching fan ~60 px wide and ~90 tall on the floor, item 3.
 * All three depths; the default is AMONG - nestled in the reef bed's grass,
 * the fish in front of it. Its colour is the keeper's: CORAL_PAL is the
 * swatch row on its placement page (the fish colour page's idiom), the
 * pick saved as RGB so a palette change never recolours a tank. */
#define CORAL_HALF_W    30
#define CORAL_X_DEFAULT TANK_FLOOR_X(150.0f)
/* both corals stand BEHIND or IN FRONT of the grass (no AMONG, like the
 * castle) and are anchored: their base sits DECOR_SINK px down into the
 * pebbles and a low mound of floor stones is drawn round it (render.c
 * draw_floor_mound) - Strato, 2026-09-24: "it actually looks like they are
 * hovering / pasted onto the [floor]" */
#define DECOR_SINK      4
#define CORAL_N         8
#define CORAL_START     0.45f              /* a bought coral is young but ESTABLISHED: the trunk and the two
                                            * low branches (Strato, 2026-09-23: a nub "is not satisfying ...
                                            * some higher level of instant gratification"); ~17 days to the
                                            * fan from here, the crown a week after */
#define CORAL_FULL      1.25f              /* growth runs past the fan: 1.0 = the fan complete, then a
                                            * ring of delicate tentacles sprouts from the top and reaches
                                            * its full spread at CORAL_FULL (Strato, 2026-09-23) */
#define CORAL_GROW_S    (30.0f * 86400.0f) /* stub -> the full fan in ~30 days of real time, the crown
                                            * ~a week more; the branches come in one by one (render.c
                                            * CORAL_SEGS' growth windows), a slow arc next to the grass's
                                            * day. The fan tops out ~76 px up the glass, a quarter of the
                                            * grass's ceilings - a modest thing, never a wall. */
float    tank_coral_growth(const tank_t *t);      /* CORAL_START..CORAL_FULL (CORAL_FULL when unset) */
/* the reef cluster (2026-09-24): item 4, the dearest thing in the shop. ~144
 * x 120 px on the floor, all three depths, AMONG by default. It arrives
 * MATURE (Strato: "the initial stage needs to look somewhat impressive and
 * large") at CLUSTER_SIZE_MIN of its full size and fills out to full over
 * the first CLUSTER_GROW_S, then from 1 to CLUSTER_FULL more and more
 * tentacles sway from the tube mouths, the coral's tips and the brain. Three
 * preset LOOKS (CLUSTER_SCHEMES: the coral / the tubes / the brain each), the
 * placement page's row of three tiles picks one. */
#define CLUSTER_HALF_W    72
#define CLUSTER_X_DEFAULT TANK_FLOOR_X(330.0f)
#define CLUSTER_START     0.0f
#define CLUSTER_FULL      2.0f
#define CLUSTER_SIZE_MIN  0.85f            /* its size on the day it is bought, of the full */
#define CLUSTER_GROW_S    (14.0f * 86400.0f) /* two weeks to full size, two more for every tentacle */
#define CLUSTER_SCHEME_N  3
typedef struct { const char *name; uint32_t coral, tube, brain; } cluster_scheme_t;
extern const cluster_scheme_t CLUSTER_SCHEMES[CLUSTER_SCHEME_N];
float    tank_cluster_growth(const tank_t *t);    /* CLUSTER_START..CLUSTER_FULL (CLUSTER_FULL when unset) */
/* the pumpkin lantern (0.3.6, Strato 2026-10-17: "a seasonal decoration
 * which we will drop from the shop after October", 75 sand dollars): a
 * carved jack-o'-lantern ~88 x 82 px with a candle inside, drawn
 * procedurally in render.c. With the light ON it is a pumpkin with a lit
 * face; with the light OFF the candle has the tank to itself and its warm
 * glow lights the water, the floor, the grass, the decor and every critter
 * near it ("finally a good reason to turn the tank light off"). Item 8,
 * BEHIND or IN FRONT of the plant layer, like the corals. The shop sells it
 * while SD_PUMPKIN_IN_SHOP (the first release after October 31 drops it);
 * one bought stays for good. */
#define PUMPKIN_HALF_W    44
/* the bubble volcano (0.3.9, DonB's #ideas thread; the reference is
 * working-assets/volcano/Volcano Sprite.png): a cone of stacked boulders
 * ~184 px wide (the castle's width) and ~200 tall over its floor line, a
 * lava pool in the crater, cracks down the flanks, a cave mouth at the foot,
 * drawn procedurally in render.c. Lit bubbles rise from the crater; with the
 * light off the lava and the bubbles glow on their own. Item 9, BEHIND or
 * IN FRONT of the plant layer, like the castle. */
#define VOLCANO_HALF_W    92
#define VOLCANO_X_DEFAULT TANK_FLOOR_X(224.0f)   /* the open floor between the reef bed and bed 2 */
/* DEPTH on the sand (0.3.9, Strato 2026-10-23: a piece behind another showed
 * its base under it, "which would never happen in reality ... we're also
 * losing depth"): the sand strip is the stage, and a piece stands on a floor
 * line BY ITS DEPTH, not by what it is - BEHIND at the back of the strip,
 * IN FRONT at its front edge, 8 px lower - so a piece in front always sits
 * lower and its base covers the base of anything behind it. With it a piece
 * BEHIND is hazed a little more with the water and one IN FRONT a little
 * less, and every piece drops a short contact shadow on what stands behind
 * it (render.c, the pieces). The grass, the plant layer, is in between. */
#define DECOR_FY_BACK   (TANK_BOT - 16)
#define DECOR_FY_FRONT  (TANK_BOT - 8)
#define DECOR_FY(z)     ((z) == DECOR_Z_FRONT ? DECOR_FY_FRONT : DECOR_FY_BACK)
#define PUMPKIN_X_DEFAULT TANK_FLOOR_X(222.0f)   /* the open floor between the reef bed and bed 2 (the plant's gap) */
int      tank_cluster_scheme(const tank_t *t);    /* 0..CLUSTER_SCHEME_N-1 */
void     tank_cluster_set_scheme(tank_t *t, int i);
extern const uint32_t CORAL_PAL[CORAL_N];
uint32_t tank_coral_rgb(const tank_t *t);          /* the colour, the default when unset */
void     tank_coral_set_rgb(tank_t *t, uint32_t rgb);
bool  tank_decor_placeable(int item);      /* SD item index: has an x and a layer */
int   tank_decor_z_count(int item);        /* depths the item offers: 2 (BACK/FRONT) for every piece since 0.3.9.3 */
int   tank_decor_z_at(int item, int i);    /* the i-th offered depth (the placement bar's segment i) */
int   tank_decor_z_index(int item, int z); /* the inverse: which segment shows depth z */
int   tank_bed_z(const tank_t *t, int b);   /* a bed's depth: the seagrass always BACK, the sword plant (b 3) BACK or FRONT */
void  tank_decor_set(tank_t *t, int item, float x, int z);
float tank_decor_x(const tank_t *t, int item);   /* the centre, default when unplaced */
int   tank_decor_z(const tank_t *t, int item);
void  tank_debug_depth_pop_old(bool on);   /* (tests: the 0.3.9.6 depth rule, the pops back - tank.c pieces_keep) */
void  tank_debug_hold_emerge_off(bool on);   /* (tests: a held finger's call off - no way out round a piece, the goal's depth) */
float tank_decor_half_w(int item);         /* half the footprint, for the page's clamp / highlight */
float tank_decor_fy(const tank_t *t, int item);   /* the floor line it stands on, by its depth (DECOR_FY) */
float tank_decor_height(int item);         /* its highest pixel over that line (the plant: tank_veg_bed) */
/* the owned, placeable piece under (x, y) - its footprint box on the floor -
 * or -1 (2026-09-24: a tap-and-hold on a piece opens its page); the smaller
 * pieces win a tie. tank_decor_reset puts a SOLD piece back to its factory
 * state so a later purchase starts fresh. */
int   tank_decor_hit(const tank_t *t, float x, float y);
void  tank_decor_reset(tank_t *t, int item);
/* the order of the pieces (2026-10-17): render.c paints the placeable pieces
 * by depth, then by RANK (tank_t.decor_rank). A bought piece goes on top of
 * its depth (tank_decor_raise); the placement page's two arrows swap it with
 * its neighbour at the same depth (tank_decor_shift). Ties - every older
 * save - keep the old fixed order: castle, coral, cluster, pumpkin, then the
 * plant (the volcano, 0.3.9, after them). The grass beds are the plant layer itself (a piece BEHIND is under
 * every blade, a piece IN FRONT over them); only the sword plant, bed 3, is
 * a piece with a rank of its own at BACK or FRONT. */
#define DECOR_SLOTS 6
int   tank_decor_slot(int item);                  /* 0..DECOR_SLOTS-1 (the tie order), or -1 */
int   tank_decor_slot_item(int slot);
bool  tank_decor_owned(const tank_t *t, int item);                /* bought (in the tank or on the shelf) */
bool  tank_decor_in(const tank_t *t, int item);                   /* bought and in the water (0.3.9: not on the shelf) - the word
                                                                   * every "is the piece here" test reads; the shop reads owned */
bool  tank_decor_locked(const tank_t *t, int item);               /* owned and LOCKED in place (0.3.9): a hold on it opens nothing */
/* a poof (0.4's tank link, brought to 0.3.9): a burst of the free bubbles
 * where something was - a fish's spot, or a piece's whole footprint as it
 * goes on the shelf (Strato 2026-10-19: "pop away into a plume of bubbles") */
void  tank_poof(tank_t *t, float x, float y);
void  tank_decor_poof(tank_t *t, int item);
int   tank_decor_rank(const tank_t *t, int item);
int   tank_decor_order(const tank_t *t, int items[DECOR_SLOTS]);   /* the pieces IN the tank, back to front (rank, then slot); the count */
bool  tank_decor_above(const tank_t *t, int a, int b);             /* a is painted over b: a greater depth, or at one depth the higher rank */
void  tank_decor_raise(tank_t *t, int item);                       /* on top: the highest rank of all + 1 */
int   tank_decor_neighbor(const tank_t *t, int item, int dir);     /* the piece just in front (+1) or behind (-1) at its depth, or -1 */
bool  tank_decor_shift(tank_t *t, int item, int dir);              /* swap ranks with that neighbour; false = none */
/* the snail has two poses (Strato's sprites, 2026-09-15): UPRIGHT, walking
 * the tank floor (nothing to graze: it comes down and ambles along the
 * bottom, turning at the ends), and flat ON THE GLASS (crawling to film and
 * grazing it, its underside to the viewer). snail_y is the sprite's centre;
 * on the floor it is SNAIL_FLOOR_Y, the sole (6 px under the centre) on the
 * line the grass is rooted at, TANK_BOT - 14: at - 24 it stood 4 px above the
 * frond roots, and the roots of the fronds BEHIND it showed under its foot -
 * a snail afloat (Strato, 2026-10-02). */
#define SNAIL_FLOOR_Y (TANK_BOT - 20.0f)
bool  tank_snail_upright(const tank_t *t);
/* a tap on the snail (its card, 2026-09-16): placed, and within a fingertip
 * of the sprite's centre. Platforms test the fish first. */
bool  tank_snail_hit(const tank_t *t, float x, float y);
/* the urchin (2026-10-02, the episode 5 promise: "an urchin that keeps the
 * grass trimmed"): a rule-based grazer on the floor, drawn by code (render.c
 * draw_urchin, from Strato's sea-urchin-v1). Awake it gets hungry slowly,
 * crawls to the grass frond standing tallest over URCHIN_KEEP and chews it
 * down a bite, then rests or ambles the sand. Asleep it works the night
 * shift: a share of every grass frond's height over URCHIN_KEEP, so a
 * morning's grass is lower than it would be - never below the keep line, and
 * never the sword plant. Like the snail it makes the chores lighter, never
 * gone, and its eating never counts as the keeper's trimming. Its centre is
 * URCHIN_FLOOR_Y, its base on the root line. */
#define URCHIN_FLOOR_Y (TANK_BOT - 21.0f)
#define URCHIN_KEEP     0.40f      /* it never takes a frond below this (a fresh tank's cover is 0.35) */
void  tank_urchin_place(tank_t *t);
bool  tank_urchin_hit(const tank_t *t, float x, float y);
bool  tank_urchin_chewing(const tank_t *t);   /* at a frond's foot, eating (render: the spines work) */
/* the strawberry crab (0.3.9, SD_ITEM_CRAB; tank_t.cr_*). Strato's rules from
 * the first crab (10-23): it "meanders slowly, typically doesn't move around
 * too much or too fast .. finds a spot and digs, nibbles at the ground. it
 * should come out for food when food reaches the ground ... should definitely
 * look to shelter in the cave and castle .. rarely scale the volcano"; and
 * for this one: "roost or rest in the castle or volcano ... facing forward,
 * straight toward the glass tucked in on itself". So: its feet on CR_FLOOR_Y;
 * it rests long and digs and nibbles where it stops (CR_REST, CR_DIG), walks
 * a short way now and then at a slow crab's pace, its feet planted (CR_WALK),
 * heads for the castle's arch or the volcano's cave and roosts there facing
 * the glass for a long while (CR_SHELTER, then CR_ROOST), comes out to any
 * pellet resting on the floor and eats it (CR_FEED, CR_EAT), raises its claws
 * for a moment when tapped (cr_poke; a walk stops) - three quick taps send it
 * running into its shelter (Strato 10-23) - and once in a long while
 * climbs the volcano's flank to the crater and back (CR_CLIMB;
 * tank_volcano_half is the rock it follows). Its small life (Strato 10-24): a
 * fish passing over it gets its claws up (cr_startle_cool), the snail coming
 * at it a standoff before the step aside (CR_FID_STANDOFF), and it is a
 * NIGHT crab - roosts by day, out most of the night, comes out when the light
 * goes off and heads in when it comes on. Its purpose: the SAND DOLLAR FIND
 * (CR_FIND_*). Its shelter is always the FRONTMOST of the castle and the volcano
 * in the water by the depth scheme (Strato 11-02: "always choose the uppermost
 * roosting spot"), not the nearer; a change of that order while it roosts or
 * walks in sends it over (tank.c crab_cave). */
enum { CR_REST = 0, CR_WALK, CR_DIG, CR_SHELTER, CR_ROOST, CR_FEED, CR_EAT, CR_CLIMB };
/* its idle life (Strato 10-23 on the first flash: "it stays completely motionless ... for too long"):
 * every few seconds while it rests or digs it grooms (a claw past its mouth), shuffles a few px,
 * turns about or settles lower; roosting it stretches its claws open now and then */
enum { CR_FID_NONE = 0, CR_FID_GROOM, CR_FID_SHUFFLE, CR_FID_SETTLE, CR_FID_STRETCH, CR_FID_STANDOFF };   /* STANDOFF: claws up at the snail, then the step aside (Strato 10-24) */
#define CR_PICK_S   2.4f       /* a dig's (and a meal's) pick: down to the sand, pinch, a bite up to its face */
#define CR_NIBBLE_S 3.6f       /* a stop on the volcano's flank: a pick and a half at the rock (0.3.9.1, Strato: "stop along the way and nibble the volcanic rocks") */
#define CR_GROOM_S  1.6f
#define CR_SETTLE_S 1.2f
#define CR_STRETCH_S 1.8f
#define CR_STANDOFF_S 1.5f
#define CR_BACK_IN_S  1.5f       /* scared in its roost: it backs into the dark over this long ... */
#define CR_BACK_OUT_S 3.0f       /* ... and comes forward again, slowly, over this (Strato 10-24: "slowly come back out and reveal itself") */
/* THE LANES (Strato 2026-10-24, a keeper on Discord: "I'm worried I might not see my crab half of the time like the
 * sea urchin"): a decoration placed IN FRONT is in front of the FISH and the FRONDS, always - but the critters that
 * roam the sand (the crab, the urchin, the snail on its floor walk) each take a lane per move, 60 in front / 40
 * behind, and a lane changes only while the critter overlaps no piece IN FRONT (tank_floor_clear), so it never
 * pops through one. The crab digging for sand dollars is in front whatever the lane. */
#define LANE_FRONT_ODDS 0.6f
/* the SAND DOLLAR FIND (Strato 2026-10-24: "I want it to be fun and seem like a really nice reward"): once in a
 * while a dig turns one up; it holds it up in its claw, glinting, for CR_FIND_HOLD_S - a tap on the crab takes it
 * (SD_CRAB_FIND, progression's ledger: cr_found / cr_found_paid) - or it buries it again. The first comes soon
 * after the purchase, a find taken waits the longest, one missed comes round sooner.
 * 0.3.9.1 (Strato 10-25, keepers on Discord whose crab never found one): the wait is SAVED and runs on LIVED time,
 * asleep too - in 0.3.9 it ran on awake time only and every boot started it again at 2-4 h, so a battery tank woken
 * for an hour or two at a time never saw a find. And a find that is due WAITS FOR THE KEEPER (crab_find_due): the
 * light on and the tank handled in the last CR_FIND_PRESENT_S; then the crab comes out and digs it up (a roost cut
 * to CR_FIND_COME_S, no shelter, no climb, the next walk ends in a dig) and holds it 3 minutes, not one. The keeper
 * always taps: nothing is collected for them. */
#define CR_FIND_HOLD_S      180.0f
#define CR_FIND_PRESENT_S   300.0f   /* the keeper is here: the tank was touched, picked up or woken this recently */
#define CR_FIND_COME_S      20.0f    /* a due find calls the crab out of its roost this soon */
#define CR_FIND_FIRST_LO_S  (45 * 60.0f)
#define CR_FIND_FIRST_HI_S  (90 * 60.0f)
#define CR_FIND_NEXT_LO_S   (6 * 3600.0f)
#define CR_FIND_NEXT_HI_S   (8 * 3600.0f)
#define CR_FIND_MISSED_LO_S (2 * 3600.0f)
#define CR_FIND_MISSED_HI_S (4 * 3600.0f)
/* THE CRAB'S PELLET (0.3.9.1; keepers on Discord: the shrimp take every pellet that reaches the floor, the crab never eats).
 * Going for a pellet the school is after too, a shrimp within CR_SHOO_PX of its body's edge on the sand: it stops, claws up
 * (CR_SHOO_S), the shrimp near it dart off tail first and the school leaves that pellet (cr_claim) - then it walks on to it.
 * Only that close: a school that reached the pellet first may finish it before the crab gets there (Strato: "sometimes
 * enough time to eat the food and some scenarios where they dont"). A pellet it is eating is claimed too. After
 * CR_MEALS_FULL pellets it is full for CR_FULL_LO_S..HI_S: fallen food does not tempt it (Strato: 3 pellets, 5-10 min). */
#define CR_REACH        12.0f    /* a pellet this far ahead of its middle is under its claw */
#define CR_SHOO_PX      6.0f
#define CR_SHOO_S       0.9f
#define CR_SHOO_POKE    1.4f     /* the claws stay up a little into the walk on */
#define CR_SHOO_AWAY_LO 90.0f    /* the school's next goal: this far past the crab, on their side (the other, against a wall) ... */
#define CR_SHOO_AWAY_HI 140.0f
#define CR_SHOO_KEEP_LO_S 8.0f   /* ... held this long: its walk on and its meal (CR_EAT_S 7) */
#define CR_SHOO_KEEP_HI_S 12.0f
#define CR_SHOO_FLEE_S  2.5f     /* the school's flight after the dart: fast, then the slow swim on to that goal */
#define CR_SHOO_ZONE_PX 80.0f    /* and the AREA it was shooed from stays the crab's: no pellet this near the one it claimed is the school's,
                                  * no goal of theirs this near it (Strato on the square: pellets land in clusters; the school took the next
                                  * one over and "turned right back, never truly fled the area") ... */
#define CR_SHOO_ZONE_S  25.0f    /* ... for this long */
#define CR_SHOO_BAND    24.0f    /* a shrimp this far over the floor at most: on the sand with it, not swimming above */
#define CR_MEALS_FULL   3
#define CR_FULL_LO_S    (5 * 60.0f)
#define CR_FULL_HI_S    (10 * 60.0f)
#define CR_FLOOR_Y  (TANK_BOT - 15.0f)
#define CR_HALF_W   22.0f
void  tank_crab_place(tank_t *t);
bool  tank_floor_clear(const tank_t *t, float x, float half);   /* no piece IN FRONT over [x - half, x + half] on the floor: a lane may change here */
bool  tank_crab_hit(const tank_t *t, float x, float y);
bool  tank_crab_find_hit(const tank_t *t, float x, float y);   /* a sand dollar held up: its tap first (0.3.9.4) */
bool  tank_crab_climb(tank_t *t);                                  /* the director, the sim: the rare climb now - to the volcano's near foot, then up (false = no crab / no volcano / climbing already) */
bool  tank_crab_pellet_race(tank_t *t, float gap, int n);   /* the director's / the sim's stage (0.3.9.1): n pellets (1..4, a cluster) gap px off
                                                              the crab, the school huddled on the first, the crab going for it - near enough it shoos them */
void  tank_crab_scare(tank_t *t, float fx); /* run for cover now (the tank's triple-tap startle calls it: a knock anywhere on the glass) */
int   tank_crab_tap(tank_t *t, float fx);  /* a tap on it (fx: the finger's x): claws up; the third quick tap sends it running
                                             * into its shelter, the frontmost of the castle and the volcano (away from the finger with neither). Returns the taps so far;
                                             * 0 = the tap TOOK the sand dollar it was holding up (no card) */
bool  tank_crab_walking(const tank_t *t);   /* its legs are striding (render; a shuffle too) */
bool  tank_crab_roost(tank_t *t, int cave);  /* go roost now: 2 the castle, 9 the volcano, -1 the frontmost (director, sim; it always ends in the frontmost) */
void  tank_debug_roost_nearer(bool on);      /* (tests: the 0.3.9 pick - the nearer shelter, blind to the depth) */
/* the volcano's cone, half its width in sprite cells (2 px) at height y
 * (cells up from the sprite's bottom row, which sits on tank_decor_fy) on
 * side s (-1 left, 1 right): the same rock render.c vo_half draws; the crab
 * climbs it (tank.c). */
float tank_volcano_half(float py, int s);
/* the seahorse (0.3.6; the fields above have the story). Its anchor
 * (sh_x, sh_y) is the centre of its tail's coil: on a blade that is the
 * blade's spine at the grip row (the renderer adds the sway); adrift and at
 * the finger it is where the coil would be. The body stands ~30 px over it.
 * SH_GRIP_FRAC of the way up the blade, on a blade at least SH_MIN_H tall
 * (so the coil is well clear of the sand); only EVEN fronds, the layer
 * drawn behind the fish. A hold brings it to SH_HITCH_OFF px beside the
 * finger (a fingertip covers ~120 px of this glass: under it, nobody would
 * see it), toward the middle of the tank. */
enum { SH_PERCH = 0, SH_DRIFT = 1, SH_HITCH = 2 };
/* THE UNIVERSAL ANCHOR (Strato 10-15: "some kind of universal anchor shape
 * that you can apply to any object or decoration"): everything the seahorse
 * holds is a vertical POST - a centre x, the row the coil sits on, and a
 * half-width. The coil wraps that post the one way (render.c: the upper half
 * of each loop passes behind it, so whatever the post is shows through), so
 * a new hitching point is a line in sh_posts(), never new drawing. Kinds:
 * BLADE (a grass or sword-plant frond, swaying: the post follows it), POST
 * (a solid part of a decoration at any depth: a castle tower, the coral's
 * trunk, a tube sponge on the reef - a hold in front of the fish puts it in
 * its front lane, sh_front) and SAND (the floor - when there is
 * nothing to hold it rests on the bottom with its tail curled under it, and
 * lifts off to look again now and then). Blades first, posts second, the
 * sand last. */
enum { SH_ANCHOR_NONE = 0, SH_ANCHOR_BLADE = 1, SH_ANCHOR_POST = 2, SH_ANCHOR_SAND = 3 };
enum { SH_POSE_BLADE = 1, SH_POSE_GRIP = 2 };
#define SH_LAND_S     0.7f
#define SH_GRIP_FRAC  0.55f
#define SH_MIN_H      0.26f
#define SH_HITCH_OFF  46.0f
#define SH_BODY_UP    18.0f        /* the body's centre above the anchor (the hit test, the card's ring) */
#define SH_REACH      13.0f        /* a pellet this close to its (jutted) snout is taken */
#define SH_SENSE      26.0f        /* a pellet this close to its head starts a snatch - on either side: it turns to it first */
#define SH_SNAP_S     0.32f        /* the snatch, head out and back */
#define SH_SNAP_AT    0.14f        /* ... the pellet is taken at the head's furthest */
#define SH_JUT        4.0f         /* px the head and neck jut forward */
#define SH_TURN_S     0.18f        /* the beat after a turn */
#define SH_SNAP_REST  1.0f         /* after a bite: no new snatch for this long (Strato 10-15: one neck movement
                                    * per pellet, never a loop through a cluster) */
float tank_seahorse_jut(const tank_t *t);          /* the head's jut right now, px (the renderer) */
#define SH_SNACK_COOL_S (10.0f * 60.0f)   /* one PAID snack per this long (Strato: "10 minute cooldown so it can't be abused") */
void  tank_seahorse_place(tank_t *t);              /* fresh in the tank: on a blade, or adrift by the grass */
bool  tank_seahorse_hold(tank_t *t, int b, int i);
bool  tank_seahorse_post(tank_t *t, int item, int n);   /* (staging) coiled on post n of that piece, settled */ /* coiled on frond i of bed b, settled, in that bed's stack (the director, the tests) */
const char *tank_seahorse_holding(const tank_t *t); /* its card: A BLADE / A LEAF / THE CASTLE / THE CORAL / THE REEF / THE VOLCANO / THE SAND */
bool  tank_seahorse_hit(const tank_t *t, float x, float y);
float tank_seahorse_days(const tank_t *t);        /* its card: whole days with the keeper */

#endif
