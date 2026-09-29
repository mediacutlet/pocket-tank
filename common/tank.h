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
#ifndef POCKET_TANK_TANK_H
#define POCKET_TANK_TANK_H

#include <stdbool.h>
#include <stdint.h>

/* The tank's size in pixels, landscape. The Waveshare AMOLED's 448 x 368 unless
 * the board says otherwise: the 2.8" CYD (ES3C28P, CONFIG_POCKET_TANK_BOARD_CYD28)
 * is 320 x 240, and the sim takes -DTANK_W / -DTANK_H to preview it.
 * Everything in common/ is laid out against these two. */
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#if CONFIG_POCKET_TANK_BOARD_CYD28
#define TANK_W 320
#define TANK_H 240
#endif
#endif
#ifndef TANK_W
#define TANK_W 448
#endif
#ifndef TANK_H
#define TANK_H 368
#endif

#define N_FISH_MAX 6            /* array bound; the live count is tank_t.n_fish */
#define N_FISH_START 2          /* a new tank: two contrasting adults */
#define N_TRAINED_NAMES 4       /* name tokens the v2 model was trained on */
#define FISH_NAME_MAX 7         /* the keeper's name for a fish (first-run setup) */
#define LOOK_N 8                /* body / accent swatches the keeper can pick from */
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
#define VEG_FRONDS_MAX 16                      /* per-bed frond slots (reef bed: 11-15) */
#define VEG_SEGS_FULL 107                      /* frond segments at growth 1: the tip
                                                * of the tallest frond touches y~10,
                                                * just under the surface (render.c
                                                * VEG_SEG_DY 3.2 px pitch from y=352) */
#define ALGAE_CELL 16                          /* px per glass-film grid cell */
#define ALGAE_COLS (TANK_W / ALGAE_CELL)       /* 28 */
#define ALGAE_ROWS (TANK_H / ALGAE_CELL)       /* 23 */
#define ALGAE_CELLS (ALGAE_COLS * ALGAE_ROWS)
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
    MS_FIRST_DART = 1u << 3, MS_FIRST_BUBBLES = 1u << 4, MS_FIRST_REEF = 1u << 5,
    /* bits 6 and 7 were the shadow milestones (survived / shrugged off);
     * the shadow was removed 2026-09-13 and the bits stay reserved so old
     * saves keep their layout - cleared on load, never set, never shown */
    MS_RETIRED_6 = 1u << 6, MS_RETIRED_7 = 1u << 7, MS_FIRST_FOLLOW = 1u << 8,
    MS_REACHED_JUV = 1u << 9, MS_REACHED_ADULT = 1u << 10, MS_REACHED_ELDER = 1u << 11,
    MS_FISH_COUNT = 12
};
#define MS_RETIRED_MASK (MS_RETIRED_6 | MS_RETIRED_7)
/* tank-level milestone bits */
enum {
    TMS_PAIR = 1u << 0, TMS_TRIO = 1u << 1, TMS_QUARTET = 1u << 2, TMS_QUINTET = 1u << 3,
    TMS_SEXTET = 1u << 4, TMS_FIRST_FULL_NIGHT = 1u << 5,   /* the device slept a full night (was: quiet night, until 2026-09-15) */
    TMS_FIRST_PLAY_SESSION = 1u << 6,
    TMS_CHANGED_SOMEONE = 1u << 7, TMS_FIRST_FEEDING = 1u << 8,
    TMS_FIRST_TRIM = 1u << 9, TMS_FIRST_CLEANING = 1u << 10,
    TMS_COUNT = 11
};

typedef struct {
    char   name[FISH_NAME_MAX + 1]; /* display name: the roster preset's until the
                                     * keeper renames it (setup.c); saved per fish */
    const char *model_name; /* one of TRAINED_NAMES: the token the v2 model sees */
    int    preset;          /* roster index (colors, base size, temperament) */
    float x, y;
    float heading;          /* radians */
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
} fish_t;

typedef struct { float x, y, age; bool alive, from_player; } food_t;
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
    bool     slash_engaged;        /* ... and has travelled sideways enough to be scissors */
    bool     wipe_sounded;         /* this stroke's wipe cue has fired */
    bool     slash_cut;            /* ... and has cut at least one frond (trims++ once) */
    float    slash_x0, slash_y0;   /* stroke start (the pre-engage travel is cut retroactively) */
    float    slash_h, slash_v;     /* travel this stroke: horizontal / vertical */
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
     * PX_PER_INCH to the inch. Both saved. */
    int32_t  algae_colonies;
    float    trim_px;
    /* sand dollars (progression.c owns the economy; tank.c reads the unlocks):
     * the balance, the lifetime total, what the shop has sold (SD_ITEM_*),
     * and the ledger that keeps an award from paying twice - per fish (bits
     * SD_PAID_*), and how many hundreds of colonies / inches have been paid.
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
    int32_t  snail_grazed;         /* algae cells it has grazed clean, lifetime (its card,
                                    * 2026-09-16; saved) */
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
/* (re)build slot from a roster preset: used by persistence to restore a fish */
void  tank_make_fish(tank_t *t, int slot, int preset, float sociable, float bold, stage_t stage);
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
#define LIGHT_IDLE_MAX_S 999       /* three digits on the settings wheel */
void  tank_handled(tank_t *t);
void  tank_toggle_light(tank_t *t);
void  tank_light_auto(tank_t *t);

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
#define FEED_ZONE_Y 26.0f
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
 * x is clamped clear of the reef rock and the glass (grass is fine), the
 * column's live bubbles shift with it, and everything that knows the column
 * - the play loop, the advisor's sighting, the milestone, the airstone -
 * reads tank_t.bubble_x. Saved per tank; the model only ever sees the
 * column as a distance bucket and a bearing, never a position. */
#define BUBBLE_X_DEFAULT (TANK_W * 0.8f)
void  tank_set_bubble_x(tank_t *t, float x);

/* ---- the shop (2026-09-15): sand dollars buy things for the tank ----
 * The items are bits in tank_t.sd_unlocks; progression.c sells them
 * (progression_buy) and tank.c gives them their place. A bought thing is in
 * the tank for good. */
enum { SD_ITEM_PLANT = 1u << 0, SD_ITEM_SNAIL = 1u << 1, SD_ITEM_CASTLE = 1u << 2, SD_ITEM_CORAL = 1u << 3, SD_ITEM_CLUSTER = 1u << 4, SD_ITEM_COUNT = 5 };
/* per-fish paid bits (sd_paid_fish) */
enum { SD_PAID_JUV = 1u << 0, SD_PAID_ADULT = 1u << 1, SD_PAID_ELDER = 1u << 2, SD_PAID_TRUST = 1u << 3 };
#define PX_PER_INCH 24.0f          /* the tank reads as ~15 in tall; a fish ~1.7 in */
/* the live bed count: VEG_BEDS, or VEG_BEDS_MAX with the sword plant bought;
 * every loop over beds runs to this. tank_veg_kind is the species (render). */
int   tank_veg_beds(const tank_t *t);
veg_kind_t tank_veg_kind(const tank_t *t, int b);
/* the purchases land: the sword plant at VEG_START on the open floor; the
 * snail on the glass, bottom left */
void  tank_plant_place(tank_t *t);
void  tank_snail_place(tank_t *t);
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
#define DECOR_MARGIN    30                 /* the snail's margin: inside the panel's rounded bezel */
#define PLANT_HALF_W    21                 /* four leaves at a 14 px pitch: centre to the outer leaf */
#define PLANT_X_DEFAULT (208.0f + PLANT_HALF_W)   /* the open floor between the reef bed and bed 2 */
/* the castle (2026-09-16, Strato's castle-v2 mockup, drawn procedurally in
 * render.c): ~184 px wide on the floor, a swim-through arch. Its depths are
 * BEHIND and IN FRONT only (Strato: "no among"), and they mean the PLANT
 * LAYER: BACK = behind the grass and the fish, a backdrop the fish pass in
 * front of; FRONT = in front of the grass, and the fish swim THROUGH the arch
 * (the keep behind them, the gate wall and the front towers over them). */
#define CASTLE_HALF_W   92
#define CASTLE_X_DEFAULT 300.0f
/* the coral (2026-09-23, Strato's coral-single.png, drawn procedurally in
 * render.c): a branching fan ~60 px wide and ~90 tall on the floor, item 3.
 * All three depths; the default is AMONG - nestled in the reef bed's grass,
 * the fish in front of it. Its colour is the keeper's: CORAL_PAL is the
 * swatch row on its placement page (the fish colour page's idiom), the
 * pick saved as RGB so a palette change never recolours a tank. */
#define CORAL_HALF_W    30
#define CORAL_X_DEFAULT 150.0f
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
#define CLUSTER_X_DEFAULT 330.0f
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
 * on the floor it is SNAIL_FLOOR_Y, the foot on the sand line. */
#define SNAIL_FLOOR_Y (TANK_H - 24.0f)
bool  tank_snail_upright(const tank_t *t);
/* a tap on the snail (its card, 2026-09-16): placed, and within a fingertip
 * of the sprite's centre. Platforms test the fish first. */
bool  tank_snail_hit(const tank_t *t, float x, float y);

#endif
