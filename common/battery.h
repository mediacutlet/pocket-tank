/* battery.h — the keeper's view of the battery (2026-09-24, Strato: "i cant
 * really tell [when it is charging] at the moment, perhaps its my color
 * blindness" + a page behind a tap on the pill).
 *
 * The pill's four states (render_battery draws them apart by SHAPE and
 * MOTION, never by hue alone), and the history behind the info page:
 * when the cable last went in or out, the screen-on time since, and this
 * cell's own drain / charge rates, learned stretch by stretch. Platform-
 * free: the device feeds it the gauge once a second and keeps bat_hist_t
 * in NVS (its own "bat" namespace - a tank reset leaves it); the sim feeds
 * it a pretend cell. Every number is gauge percent: the AXP2101 has no
 * current sense, so rates are % per hour, measured off the gauge itself. */
#ifndef AQUA_PETS_BATTERY_H
#define AQUA_PETS_BATTERY_H
#include <stdbool.h>
#include <stdint.h>

enum { BAT_ON_BATTERY = 0,      /* no cable */
       BAT_CHARGING   = 1,      /* cable in, charge flowing */
       BAT_FULL       = 2,      /* cable in, the charger done */
       BAT_PLUGGED    = 3 };    /* cable in, the charger resting short of full */
#define BAT_ON_POWER(s) ((s) != BAT_ON_BATTERY)

/* until this cell has been measured: the 2026-09-14 batlog (4 fish, 60%
 * brightness, awake: 96% -> 52% in an hour) and the 09-11 charge at 100 mA
 * (17% -> 100% in under an hour) */
#define BAT_DRAIN_DEFAULT  44.0f        /* gauge % per hour, screen on */
#define BAT_CHARGE_DEFAULT 80.0f        /* gauge % per hour on the cable */
#define BAT_POPUP_S        6            /* the pill shows itself this long when the cable goes in */

typedef struct {                        /* persisted by the platform (a blob; the magic says it is ours) */
    uint32_t magic;
    int64_t  since_unix;                /* the last cable edge (in or out), wall clock; 0 = never seen */
    int16_t  since_pct;                 /* the gauge at that edge; -1 = no stretch begun yet */
    uint8_t  on_power;                  /* which side of the edge the tank is on */
    uint8_t  pad;
    uint32_t awake_s;                   /* screen-on seconds since the edge */
    uint16_t drop_pct;                  /* gauge % lost while awake since the edge (on battery) */
    uint16_t drain_x10;                 /* learned: % per awake hour x 10 (0 = not yet) */
    uint16_t charge_x10;                /* learned: % per hour charging x 10 (0 = not yet) */
    uint16_t pad2;
} bat_hist_t;
#define BAT_HIST_MAGIC 0xBA77E501u

typedef struct {
    bat_hist_t h;                       /* the part that persists */
    int     low;                        /* the lowest gauge seen since the wake (drops count past it only: a gauge that bounces 50/49/50/49 lost 1%) */
    int     state;                      /* the last state fed; -1 = none since boot */
    int     chg_pct; int64_t chg_unix;  /* where the current CHARGING run began (-1 = not in one) */
    float   save_s;                     /* awake seconds since the last save */
    float   awake_frac;                 /* the part-second not yet in h.awake_s */
    bool    dirty;                      /* the platform should persist h */
} bat_t;

/* the page's numbers; -1 = not known */
typedef struct {
    int  pct, mv, state;
    int  left_min;                      /* on battery: until empty; charging: until full */
    int  life_min;                      /* a full charge, screen on */
    int  since_min;                     /* since the last cable edge */
    int  awake_min;                     /* screen on since unplugged */
    bool measured;                      /* the drain rate is this cell's (vs the default) */
} bat_info_t;

void battery_init(bat_t *b, const bat_hist_t *saved);   /* saved = NULL, or a blob that failed its magic: a fresh history */
/* once a second or so while the tank is awake: the wall clock (0 = unknown),
 * the awake seconds since the last call (a nap is NOT awake time - the
 * platform clamps it), the gauge and its state. Returns +1 when the cable
 * went in, -1 when it came out (the first call after boot compares with the
 * saved side: a USB plug-in boots the board), else 0. */
int  battery_tick(bat_t *b, int64_t now_unix, float dt, int pct, int state);
void battery_woke(bat_t *b);            /* back from sleep: what the gauge lost asleep is not screen-on drain */
bool battery_take_save(bat_t *b);       /* true once when h should be written: cable edges, every 10 awake minutes */
void battery_info(const bat_t *b, int64_t now_unix, int pct, int mv, int state, bat_info_t *out);
/* "45M", "2H 15M", "3D 4H" (minutes; < 0 = "-") */
void battery_fmt_dur(char *buf, int n, int min);

#endif
