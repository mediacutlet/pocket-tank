/* tank.h — platform-agnostic reflex layer for the pocket fish tank.
 *
 * Ported from the browser prototype (llm-fishtank-v0_2.html); same fish,
 * same goals, same drive dynamics, rescaled to the 448x368 landscape tank.
 * World geometry (bubble column, reef, zones) matches model/gen_traces.py so
 * the advisor model sees the world it was trained on.
 *
 * Population (docs/progression-next.md): the tank holds up to N_FISH_MAX fish
 * but only fish[0..n_fish-1] are alive/active. A new tank starts with two
 * adults of contrasting personality; the rest arrive later as fry (the
 * progression layer decides when). Every loop in this file, the renderer and
 * the advisors iterates n_fish, never N_FISH_MAX.
 *
 * No I/O, no floats-to-strings, no OS calls: this file and tank.c compile
 * unchanged for the LVGL PC sim and the ESP32-S3 firmware.
 */
#ifndef AQUA_PETS_TANK_H
#define AQUA_PETS_TANK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef TANK_ROUND               /* the round 466 px glass (Waveshare 1.75C, 2026-10-01): the frame is the square around a bowl */
#define TANK_W 466
#define TANK_H 466
#endif
#ifdef TANK_WATCH               /* the watch (Waveshare 2.06, 2026-10-02): its 410 x 502 glass as it is worn - a PORTRAIT tank
                                 * (Strato, with the first landscape picture on his wrist: "I have to turn my head") */
#define TANK_W 410
#define TANK_H 502
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

#define N_FISH_MAX 25           /* array bound; the live count is tank_t.n_fish (6 until the species,
                                 * 2026-10-05, then 10; 25 since 2026-10-07: fish 7..N_FISH_MAX save in
                                 * their own tail, progression.c - the tail grows with this number) */
#define N_FISH_START 2          /* a new tank: two contrasting adults */
#define N_TRAINED_NAMES 4       /* name tokens the v2 model was trained on */
#define FISH_NAME_MAX 7         /* the keeper's name for a fish (first-run setup) */
#define LOOK_N 8                /* body / accent swatches the keeper can pick from */
#define MAX_FOOD   16           /* 2026-10-10: five portions at a time (five surface taps of three pellets), a slot to spare for the trickle; 8 before (two and a half taps) */
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

typedef enum { STAGE_FRY, STAGE_JUV, STAGE_ADULT, STAGE_ELDER } stage_t;
extern const char *const STAGE_NAMES[4];           /* schema.md v2 stage tokens */
extern const char *const TRAINED_NAMES[N_TRAINED_NAMES]; /* mira bolt kelp nori */

/* ---- species (2026-10-05, docs/species.md) ----
 * The classic fish and nine creatures that breed like it. The model picks
 * every creature's goal (schema 5 also hears its species); the species
 * decides how the goal is carried out (its locomotion, its own spots) and
 * how it looks (render.c). SP_FISH is 0, so every older save reads as fish. */
typedef enum { SP_FISH, SP_SEAHORSE, SP_OCTOPUS, SP_PUFFER, SP_ANGLER, SP_EEL, SP_SHARK, SP_SQUID,
               SP_CRAB, SP_LOBSTER,     /* the floor's two (2026-10-05, the same day) */
               SP_JELLYFISH,           /* appended: persisted species IDs never move */
               SP_SWORDFISH,           /* 2026-10-10 night: a fast open-water cruiser with a bill */
               SP_COUNT } species_t;
#define SP_VARIANTS 4            /* designs a species (fish_t.variant) */
#define SP_MUTATE_P 0.04f        /* a classic fish's fry hatching as a new species */
/* how a species moves (docs/species.md "How they move") */
typedef enum {
    LOCO_FIN,        /* the classic fish: committed U-turns, a tail beat */
    LOCO_UPRIGHT,    /* seahorse: upright, slow, vertical, anchors its tail */
    LOCO_JET,        /* octopus / squid: pulsed jets, mantle first; the octopus crawls the floor */
    LOCO_HOVER,      /* pufferfish: sculls, stops, turns on the spot, backs up */
    LOCO_AMBUSH,     /* anglerfish: still and low, short fast lunges */
    LOCO_UNDULATE,   /* electric eel: a travelling wave, swims backward, breathes air */
    LOCO_CRUISE,     /* hammerhead: never stops, wide turns, the head sweeps */
    LOCO_SIDEWALK,   /* crab: walks the floor SIDEWAYS on its legs, climbs rock, scuttles off when startled */
    LOCO_WALK        /* lobster: walks the floor head first, a backward tail-flip to escape */
} loco_t;
typedef struct { const char *name; uint32_t color, fin, accent; } sp_variant_t;
typedef struct {
    const char  *name;            /* the card's / shop's word, upper case */
    const char  *token;           /* schema-5 word: fish seahorse octopus puffer angler eel shark squid crab lobster */
    const char  *names[4];        /* a newborn's default names (<= FISH_NAME_MAX) */
    sp_variant_t var[SP_VARIANTS];
    float size_lo, size_hi;       /* base size (1.0 = the classic fish's ~42 px) */
    float bold_lo, bold_hi, soc_lo, soc_hi;
    float curiosity, lazy, turn_rate;
    loco_t loco;
    float speed_k;                /* cruise speed x (the goal's speed) */
    float burst_k;                /* dart / startle speed x */
    float vert_k;                 /* climb preference: the classic fish's 0.72 flattens climbs; > 1 favours them */
    float min_speed;              /* never slower (the hammerhead's ram breathing); 0 = can stop */
    float hit_r;                  /* tap radius at size 1, px */
    bool  big;                    /* the small ones keep their distance (SP_AVOID_R) */
} species_def_t;
extern const species_def_t SPECIES[SP_COUNT];
#define SP_AVOID_R 70.0f          /* px at size 1: a small creature's berth around a big one */

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
    MS_FISH_COUNT = 13
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
    TMS_COUNT = 13
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
    bool   hold_far;        /* was HOLD_APPROACH_FROM+ px from the finger when this hold's draw
                             * began (transient): only such a fish can earn a hold-approach */
    float  zone_seen[6];    /* tank clock when the fish was last in each zone */
    float  explore_x, explore_y; /* explore's destination - a point in a stale zone */
    bool   explore_set;     /* ... valid; cleared when explore is (re)chosen or reached */
    int    eaten;
    int    eaten_player;    /* pellets that came from the keeper's hand */
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
    /* species (2026-10-05): what it is and which of the species' designs
     * (saved, in fish_save_t's spare bytes); the rest is live state for the
     * species' own motion and looks, not saved.
     *
     * THE CONVENTION (2026-10-05, the motion pass), for every species:
     *  - (x, y) is the middle of the body; heading is the direction of
     *    TRAVEL, always (atan2 of the velocity on the glass).
     *  - facing / yaw say which way the HEAD points (+1 right), and may
     *    disagree with the travel: a creature whose heading lies on the far
     *    side of its facing is going BACKWARD (tank_fish_backward) - the
     *    eel, the puffer and the squid backing a little, the lobster's
     *    tail-flip, and every JET: an octopus or a squid jetting
     *    (tank_fish_jetting) travels mantle first, its arms - its "head" for
     *    facing - trailing, so facing is opposite the travel the whole jet.
     *  - the body's axis is the heading taken on the facing side (the climb
     *    or dive of the travel, the head where facing says); tank_fish_head
     *    is where the mouth is, tank_body_half_len half the length the motion
     *    keeps inside the glass (the eel 1.6 x a fish's for its size).
     *  - the seahorse is upright (its axis vertical, the snout on the facing
     *    side at the top); the crab is drawn face-on to the viewer - facing is
     *    only the side it is walking toward (its legs' ripple), yaw eases to 0.
     *  - jet is a phase 0..1 that wraps: the jet pulse (thrust while < 0.3,
     *    then the glide), the octopus's / squid's fin or arm ripple between
     *    jets, the eel's body wave, the seahorse's dorsal flutter, the
     *    puffer's sculling, the crab's leg ripple (by distance walked), the
     *    shark's tail beat, the angler's fin walk; the LOBSTER's only during a
     *    tail-flip (sp_mode SPM_FLIP): each stroke runs 0..1, the abdomen
     *    curled under while > 0.5, 0 when it walks.
     *  - puff: the pufferfish's inflation; the CRAB's claws-up threat (0..1). */
    uint8_t species;        /* species_t */
    uint8_t variant;        /* 0..SP_VARIANTS-1 */
    float  puff;            /* pufferfish: 0 flat .. 1 a spiny ball; crab: its claws raised 0..1 */
    float  ink;             /* octopus / squid: seconds the ink cloud has left */
    float  jet;             /* a phase 0..1 (see THE CONVENTION above) */
    float  camo;            /* octopus: 0 its own colours .. 1 the colour it sits on */
    uint32_t camo_rgb;      /* ... that colour */
    float  air_s;           /* eel: seconds until it needs a gulp of air; <= 0 = on the trip up for it
                             * (the seconds of the trip so far, negative), reset to 60..120 back down */
    float  spark;           /* eel: seconds of a startle's spark left */
    float  lure;            /* anglerfish: lure glow 0..1 */
    int8_t anchor;          /* seahorse: the frond its tail holds (bed * 16 + frond), -1 = none; anchored,
                             * the tail grips the stem at (x + facing * 5 * size, y + 10 * size) */
    uint8_t sp_mode;        /* the species' reflex in play (SPM_*): a jet, the eel's breath, the crab's claws, the flip */
    float  sp_t;            /* its timer (and the walkers' stop-and-go, the seahorse's snick, the angler's lunge) */
    float flourish_wait, surface_s; /* transient: idle event countdown; positive rise/float, negative return */
    uint16_t flourish_cycle;       /* staggered events; never persisted */
    float  sp_x;            /* the anglerfish's home spot along the floor (-1 = not yet) */
    /* tossed by a shake (2026-10-10, tank_shake): seconds left tumbling, the throw's velocity
       (px/s, damped), the spin (rad/s). Steering is off while it lasts. Not saved. */
    float toss, toss_vx, toss_vy, toss_spin;
} fish_t;
/* fish_t.sp_mode (not saved) */
enum { SPM_NONE, SPM_JET, SPM_CLING, SPM_RISE, SPM_GULP, SPM_SINK, SPM_CLAWS, SPM_SCUTTLE, SPM_FLIP, SPM_LUNGE };

/* a pellet: it sinks ~43 s from the surface to the floor, then RESTS there
 * FOOD_FLOOR_S before it dissolves (2026-09-29, for the shrimp school - every
 * tank alike; measured: the fish's hunger is unchanged). floor_s = seconds on
 * the floor so far, nibbled = seconds of shrimp pecking (SHRIMP_PECK_S eats it). */
typedef struct { float x, y, age; bool alive, from_player; float floor_s, nibbled; } food_t;
#define FOOD_LIFE_S   45.0f          /* in the water: gone at this age if it never reached the floor */
#define FOOD_FLOOR_Y  (TANK_BOT - 14)  /* where a pellet comes to rest */
#define FOOD_FLOOR_S  15.0f          /* ... and how long it rests there (Strato, 2026-09-29) */

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
    fish_t   fish[N_FISH_MAX];
    int      n_fish;               /* live fish = fish[0..n_fish-1] */
    food_t   food[MAX_FOOD];
    bubble_t bubble[MAX_BUBBLE];
    float    bubble_x, bubble_y;   /* bubble column anchor */
    float    reef_x, reef_y;
    float    clock;                /* seconds since start */
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
    uint8_t  theme;                /* theme_id_t: presentation only; persisted in an append-only tail */
    bool     screen_turned;        /* a worn tank (TANK_WORN): settings SCREEN = TURNED (saved) */
    bool     orient_lock;          /* settings ROTATION = locked (0.3.2, saved): the picture keeps the way up it
                                    * had when the keeper locked it, however the tank is turned (tank_orient) */
    bool     orient_inv;           /* the picture's way up now, true = turned over: the platform's live flip,
                                    * or what the lock froze (saved with the lock) */
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
    uint32_t sd_paid_fish[N_FISH_MAX];
    int32_t  sd_colonies_paid, sd_inches_paid;
    /* the snail (SD_ITEM_SNAIL): crawls the glass toward the nearest film and
     * grazes it - the one creature in the tank that is not the model's. Its
     * position is saved; heading / target are not. */
    float    snail_x, snail_y, snail_heading;
    int16_t  snail_cell;           /* the algae cell it is heading for, -1 = wandering */
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
    /* the shipwreck (2026-10-10, item 7): a small sunken boat on the floor with holes the
     * creatures swim through, an anchor on its chain, a bobbing frogman trailing a mist of
     * bubbles. Its centre x (<= 0 = the default) and depth, BACK or FRONT. Both saved. */
    float    wreck_x;
    uint8_t  wreck_z;
    /* the frogman (2026-10-10, item 8; he hung from the wreck's stern before): a little diver
       who floats SIDEWAYS, horizontal, drifting across the water from glass to glass, turning
       at each end, easing to a new lane now and then, waving at times. His suit is the
       theme's (render.c). Only the bit is saved: a load puts him mid-tank. */
    float    frog_x, frog_y;
    float    frog_yaw;             /* -1 facing left .. 1 facing right; thin through a turn */
    int8_t   frog_dir;             /* the way he is going */
    float    frog_lane;            /* the y he eases toward */
    float    frog_lane_t;          /* seconds until he picks another lane */
    float    frog_toss, frog_vx, frog_vy;   /* tossed by a shake (tank_shake): seconds left, the throw */
    /* the submarine (2026-10-10 night, item 9): a little sub that cruises the water from glass to
       glass like the frogman, propeller turning, bubbles streaming from the stern - and every
       SUB_CRUISE_LO..HI s it stops, a periscope rises, scans, and sinks before it goes on. The
       hull is the theme's (render.c). Only the bit is saved: a load puts it mid-tank. */
    float    sub_x, sub_y;
    float    sub_yaw;              /* -1 facing left .. 1 facing right */
    int8_t   sub_dir;
    float    sub_lane, sub_lane_t;
    float    sub_v;                /* px/s along its heading: eases to SUB_SPEED, or to 0 for a stop */
    float    sub_stop;             /* seconds left of a stop (0 = cruising) */
    float    sub_next;             /* seconds until the next stop */
    float    sub_peri;             /* the periscope: 0 down .. 1 up */
    float    sub_look;             /* seconds the periscope has been up (it scans on this) */
    float    sub_toss, sub_vx, sub_vy;
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
    shrimp_t shrimp[SHRIMP_MAX];
    float    shrimp_tx, shrimp_ty, shrimp_tt;
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
 * picture follows the IMU's 180-degree flip unless settings' ROTATION is
 * locked. The platform hands tank_orient its live reading every frame and
 * shows what comes back: the reading itself, or - locked - the way up the
 * picture had at the lock (saved, so a locked tank boots the same way up).
 * tank_orient_lock is the settings page's toggle. */
bool  tank_orient(tank_t *t, bool live_inverted);
void  tank_orient_lock(tank_t *t, bool lock);

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
float tank_randf(tank_t *t, float lo, float hi);
/* roster preset count (6) and a preset's display name, for UI */
int   tank_roster_count(void);
const char *tank_roster_name(int preset);
/* species (docs/species.md). tank_make_fish is the classic fish;
 * tank_set_species turns slot into a species' creature in place - its
 * design, size range, temperament constants and default name - keeping the
 * bold / sociable it has (a load re-applies it). tank_add_species_pair
 * brings two juveniles of a species (the shop), -1 when there is no room
 * for both. tank_species_n counts a species' living members. */
const species_def_t *tank_species(const fish_t *f);
void  tank_set_species(tank_t *t, int slot, int species, int variant);
int   tank_add_species_pair(tank_t *t, int species);
int   tank_add_species_n(tank_t *t, int species, int n);   /* n juveniles of a species (the shop sells one, 2026-10-07); the first's slot or -1 */
int   tank_species_n(const tank_t *t, int species);
/* a tap's reach around a creature (the touch ports, the sim): its species'
 * hit_r at its size, never under a fingertip's TANK_HIT_MIN_R */
#define TANK_HIT_MIN_R 32.0f
float tank_fish_hit_r(const fish_t *f);
bool  tank_poke(tank_t *t, int idx);        /* a double tap on creature idx: its trick (puff, ink); false = it has none */
void  tank_wreck_place(tank_t *t);           /* the shipwreck bought: its default spot (item 7) */
void  tank_frogman_place(tank_t *t);         /* the frogman bought (or loaded): mid-tank, heading right (item 8) */
void  tank_sub_place(tank_t *t);             /* the submarine bought (or loaded): mid-tank, heading left (item 9) */
/* a hard shake of the device (2026-10-10; the IMU's shake detector, the sim's 1 key): every
 * creature is thrown - a random velocity and spin, tumbling off the glass, the species'
 * startle reactions on the way (the puffer puffs, the squid and octopus ink, the crab's
 * claws), the shrimp scatter, the frogman tumbles - and settles back over TOSS_S seconds
 * (the throw damps out, steering returns). strength ~1 for a hard shake. Stress rises, a
 * little trust goes. */
void  tank_shake(tank_t *t, float strength);
#define TOSS_S 3.2f
bool  tank_frogman_hit(const tank_t *t, float x, float y);   /* within a fingertip of him */
bool  tank_sub_hit(const tank_t *t, float x, float y);       /* within a fingertip of the sub */
#define FROG_SPEED     9.0f                  /* px/s across the tank */
#define FROG_MARGIN    (DECOR_MARGIN + 24)   /* he turns this far from the glass */
#define FROG_LANE_LO   (TANK_H * 0.07f)      /* the water he drifts in: all of it (2026-10-10 night, Alvin: "float freely */
#define FROG_LANE_HI   (TANK_BOT - 46)       /*   anywhere in the tank"; a mid-water band before), the sand and the surface kept */
#define SUB_SPEED      13.0f                 /* px/s, cruising */
#define SUB_MARGIN     (DECOR_MARGIN + 30)
#define SUB_LANE_LO    (TANK_H * 0.07f)      /* the whole water column, like the frogman's (2026-10-10 night) */
#define SUB_LANE_HI    (TANK_BOT - 50)
#define SUB_STOP_S     8.0f                  /* a stop: the periscope up for the middle six seconds */
#define SUB_CRUISE_LO  22.0f                 /* seconds of cruising between stops */
#define SUB_CRUISE_HI  40.0f
float tank_species_size(const fish_t *f);   /* its base size: from its personality, so a load gets the same */
/* the species' motion, for the renderer and the tests (see THE CONVENTION in
 * fish_t). tank_ground_y: the top of what a floor walker stands on at x -
 * the sand line, or the reef cluster's rock where it is placed (the crab,
 * the lobster, the octopus crawl it; their middle stands tank_walk_off above
 * it). */
float tank_body_half_len(const fish_t *f);
void  tank_fish_head(const fish_t *f, float *hx, float *hy);
bool  tank_fish_backward(const fish_t *f);
bool  tank_fish_jetting(const fish_t *f);
float tank_ground_y(const tank_t *t, float x);
float tank_walk_off(const fish_t *f);
/* the keeper's say over a fish's identity (first-run setup, 2026-09-13; the
 * birth flow names an arrival, 2026-09-14). tank_set_name copies up to FISH_NAME_MAX
 * chars (empty = back to the preset's name); tank_set_look sets the body and
 * accent colours - the fin follows the body (the preset's own fin when the
 * body is a roster colour, a darkened body otherwise). The swatch palettes
 * hold every roster colour, so an untouched fish always sits on a swatch. */
extern const uint32_t LOOK_BODY[LOOK_N];
extern const uint32_t LOOK_ACCENT[LOOK_N];
void  tank_set_name(tank_t *t, int slot, const char *name);
void  tank_set_look(tank_t *t, int slot, uint32_t body, uint32_t accent);
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
 * (progression_buy) and tank.c gives them their place. A bought thing is in
 * the tank for good. */
enum { SD_ITEM_PLANT = 1u << 0, SD_ITEM_SNAIL = 1u << 1, SD_ITEM_CASTLE = 1u << 2, SD_ITEM_CORAL = 1u << 3, SD_ITEM_CLUSTER = 1u << 4, SD_ITEM_SHRIMP = 1u << 5,
       SD_ITEM_URCHIN = 1u << 6,
       /* the species (2026-10-05, docs/species.md): one juvenile a purchase (2026-10-07;
          a pair before), in species order (item SD_ITEM_SP_FIRST + species - 1). The
          bit means the species is in the tank (progression_species_sync); it never
          locks the item - a creature sells whenever there is a free place. */
       SD_ITEM_SP_SEAHORSE = 1u << 7, SD_ITEM_SP_OCTOPUS = 1u << 8, SD_ITEM_SP_PUFFER = 1u << 9, SD_ITEM_SP_ANGLER = 1u << 10,
       SD_ITEM_SP_EEL = 1u << 11, SD_ITEM_SP_SHARK = 1u << 12, SD_ITEM_SP_SQUID = 1u << 13, SD_ITEM_SP_CRAB = 1u << 14,
       SD_ITEM_SP_LOBSTER = 1u << 15, SD_ITEM_SP_JELLYFISH = 1u << 16,
       SD_ITEM_WRECK = 1u << 17,   /* the shipwreck (2026-10-10): item index 7, after the urchin - a thing, before the species */
       SD_ITEM_FROGMAN = 1u << 18, /* the frogman (2026-10-10, later that day): item index 8, a resident like the snail */
       SD_ITEM_SUB = 1u << 19,     /* the submarine (2026-10-10, that night): item index 9, a resident that cruises the water */
       SD_ITEM_SP_SWORDFISH = 1u << 20,   /* the swordfish (2026-10-10 night): the last species item */
       SD_ITEM_COUNT = 21 };
#define SD_ITEM_SP_FIRST 10        /* the item index of the first species (the seahorse) */
#define SD_ITEM_WRECK_IDX 7        /* the shipwreck's item index */
#define SD_ITEM_FROGMAN_IDX 8      /* the frogman's item index */
#define SD_ITEM_SUB_IDX 9          /* the submarine's item index */
/* per-fish paid bits (sd_paid_fish) */
enum { SD_PAID_JUV = 1u << 0, SD_PAID_ADULT = 1u << 1, SD_PAID_ELDER = 1u << 2, SD_PAID_TRUST = 1u << 3 };
#define PX_PER_INCH 24.0f          /* the tank reads as ~15 in tall; a fish ~1.7 in */
#define PX_PER_CM   (PX_PER_INCH / 2.54f)   /* the shop pays grass by the centimeter (SD_TRIM_CM) */
/* the live bed count: VEG_BEDS, or VEG_BEDS_MAX with the sword plant bought;
 * every loop over beds runs to this. tank_veg_kind is the species (render). */
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
/* placing the decor (2026-09-16, Strato: a bought piece "should allow the
 * player to place the piece wherever they like", with a depth choice): a
 * placeable item has a centre x along the floor - clamped inside the
 * visible window, DECOR_MARGIN from the glass - and a depth layer: BACK =
 * behind the fish and the grass, MIDDLE = woven with them (alternate fronds
 * in front, the beds' own look), FRONT = over everything. The plant is the
 * one placeable item so far; the snail goes where it likes. setup.c's
 * placement page and the shop's MOVE button drive these; the save keeps them. */
enum { DECOR_Z_BACK = 0, DECOR_Z_MIDDLE = 1, DECOR_Z_FRONT = 2, DECOR_Z_N = 3 };
#if defined(TANK_ROUND) || defined(TANK_WATCH)
#define DECOR_MARGIN    8                  /* the bowl's floor is short and its glass hides nothing (the watch's floor ends where its corners begin) */
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
/* the shipwreck (2026-10-10): ~128 px wide, ~80 tall at the mast, item 7; BEHIND or IN
 * FRONT like the castle; the holes in its hull are see-through, the fish pass behind them */
#define WRECK_HALF_W    64
#define WRECK_X_DEFAULT TANK_FLOOR_X(130.0f)
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
int      tank_cluster_scheme(const tank_t *t);    /* 0..CLUSTER_SCHEME_N-1 */
void     tank_cluster_set_scheme(tank_t *t, int i);
extern const uint32_t CORAL_PAL[CORAL_N];
uint32_t tank_coral_rgb(const tank_t *t);          /* the colour, the default when unset */
void     tank_coral_set_rgb(tank_t *t, uint32_t rgb);
bool  tank_decor_placeable(int item);      /* SD item index: has an x and a layer */
int   tank_decor_z_count(int item);        /* depths the item offers: 3 (BACK/MIDDLE/FRONT) or 2 (BACK/FRONT) */
int   tank_decor_z_at(int item, int i);    /* the i-th offered depth (the placement bar's segment i) */
int   tank_decor_z_index(int item, int z); /* the inverse: which segment shows depth z */
void  tank_decor_set(tank_t *t, int item, float x, int z);
float tank_decor_x(const tank_t *t, int item);   /* the centre, default when unplaced */
int   tank_decor_z(const tank_t *t, int item);
float tank_decor_half_w(int item);         /* half the footprint, for the page's clamp / highlight */
/* the owned, placeable piece under (x, y) - its footprint box on the floor -
 * or -1 (2026-09-24: a tap-and-hold on a piece opens its page); the smaller
 * pieces win a tie. tank_decor_reset puts a SOLD piece back to its factory
 * state so a later purchase starts fresh. */
int   tank_decor_hit(const tank_t *t, float x, float y);
void  tank_decor_reset(tank_t *t, int item);
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

#endif
