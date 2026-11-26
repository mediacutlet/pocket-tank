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
#ifndef POCKET_TANK_BATTERY_H
#define POCKET_TANK_BATTERY_H
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
    uint8_t  since_owed;                /* 1: that edge was stamped on a clock that OWED time (resumed at a save, running behind:
                                           battery_clock_owed) - the sync that pays the debt moves the stamp with the clock.
                                           It was a zeroed pad byte until 0.4.3: an older blob reads 0, as it should */
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
    bool    owed;                       /* the wall clock runs behind real time (battery_clock_owed): the next edge is stamped so */
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
/* THE CLOCK MOVED (0.4.3; C1704 on the 1.8, 11-20: UNPLUGGED 1 DAY AND 2 HOURS AGO after two hours off). since_unix is a wall
 * clock stamp, so a correction of the clock would read as time on (or off) the cable unless the stamp goes with it:
 * - battery_clock_owed: the platform says whether the clock is behind real time (it was resumed at a save; NVS "netlag"),
 *   at boot and whenever that changes. An edge is stamped with it.
 * - battery_clock_moved(delta_s, how): the clock was just set delta_s ahead (negative: back).
 *   BAT_CLOCK_REBASE = the FIRST sync, the whole clock moving onto real time with nothing lived: every stamp moves with it.
 *   BAT_CLOCK_PAID = a sync that PAID owed time: the stamp moves only if it was taken on the owed clock (since_owed) - an
 *   edge stamped on a good clock, before the tank was off, is where it belongs, and the jump is real time since it.
 *   (An edge stamped between two resumes carries only the first one's debt and still moves by both: the line then reads
 *   short, never long, until the next edge.)
 *   BAT_CLOCK_STEP = an owed clock moved part of the way (up to a restored save's stamp): an owed stamp moves with it and
 *   stays owed - the rest is the paying sync's.
 *   The charging run's start (this boot's, RAM) moves with any change.
 * - battery_clock_unknown: an owed stamp whose debt can no longer be told - the clock it was taken on is gone (lost with
 *   the power, then set afresh), or the clock came right with no sync seen: the stamp is dropped, the line reads "-" until
 *   the next cable edge. Better no line than a wrong one. */
#define BAT_CLOCK_PAID   0
#define BAT_CLOCK_REBASE 1
#define BAT_CLOCK_STEP   2
void battery_clock_owed(bat_t *b, bool owed);
void battery_clock_moved(bat_t *b, int64_t delta_s, int how);
void battery_clock_unknown(bat_t *b);
void battery_info(const bat_t *b, int64_t now_unix, int pct, int mv, int state, bat_info_t *out);
/* "45M", "2H 15M", "3D 4H" (minutes; < 0 = "-") */
void battery_fmt_dur(char *buf, int n, int min);

#endif
