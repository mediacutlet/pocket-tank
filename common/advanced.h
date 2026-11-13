/* advanced.h - THE ADVANCED PAGE (0.3.9.2): the keeper's own touch
 * calibration and the size of the battery in the tank. Platform-independent
 * like every page in common/; the platform hands it the finger and does what
 * its one-shot actions say (save the size and tell the charger; save the
 * touch map).
 *
 * HIDDEN on purpose (Strato, 2026-10-28: "I really want this to be hard to
 * get to... it's not worth the risk of someone stumbling upon it that
 * shouldn't"): no button anywhere. On the settings page, BOOT held 5 s
 * and, still held, a finger landing on the glass opens it - the reset
 * chord's shape (main.c; on the tank the same chord opens the reset prompt).
 *
 * BATTERY: the size steps between two arrows; SET <size> applies it. A size
 * that charges faster than the board's own cell goes through the WARNING
 * first: its box ticked (the size again, in the keeper's own hand) before
 * CONTINUE lights. A smaller or same-rate size applies at once.
 *
 * TOUCH: START -> nine crosses, a press each (taken with the keeper's map
 * OFF, so the fit is from the board's own map) -> the fit -> ONE MORE CROSS,
 * tapped with the new map within ADV_CHECK_S, or the old map comes back. A
 * map the finger cannot use never stays. RESET TOUCH drops the keeper's map.
 * THE TURN (0.4.2, guided): the first sitting kept (saved at once), TURN IT
 * OVER asks for the tank upside down and a second sitting there, so the two
 * split into the panel's map and the finger's landing (below). The desk tanks
 * and the pendant: the page waits for the IMU to say the tank is upside down
 * from the first sitting (the lock on ROTATION set aside for the turned
 * sitting; a lock the tank was not held at: no turn, SKIP), then the crosses
 * come back by themselves; a finger already down then is no press. The watch can't tell (SCREEN is a setting): the
 * page is drawn the other way up - it reads right once the watch is turned -
 * and GO starts the crosses; after the flow the picture is SCREEN's again.
 * SKIP (or nothing for ADV_TURN_IDLE_S) keeps the first sitting alone, as in
 * 0.4.1. A second sitting that is cut short, missed or won't join the first
 * leaves the first one saved - nothing a stray tap does undoes it.
 * The map is a gain and an offset per axis in the UPRIGHT frame (the board's
 * own calibration's frame), so a tank turned over later keeps it right. */
#ifndef ADVANCED_H
#define ADVANCED_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define ADV_BOOT_HOLD_S   5.0f        /* the chord: BOOT held this long before the finger lands (also the reset prompt's) */
#define ADV_CHECK_S       15.0f       /* the check cross: tapped in this long, or the old touch comes back */
#define ADV_CROSS_IDLE_S  30.0f       /* no press on a cross for this long: the calibration gives up, the old touch stays */
#define ADV_DONE_HOLD_S   0.2f        /* DONE: a press begun this soon after it came is no tap (a double tap on SKIP never closes it unseen) */
#define ADV_TURN_IDLE_S   60.0f       /* TURN IT OVER: nothing for this long - the first sitting stays alone (as SKIP) */
#define ADV_CHECK_HIT_PX  36.0f       /* the check cross: a press this close keeps the new map */
#define ADV_N_CROSS       9

/* ---- the battery: the board's own cell and the sizes a keeper can pick ---- */
#if defined(TANK_SQUARE)
#define ADV_CELL_STOCK    1000        /* mAh, the maker's cell (firmware batlog.h BATLOG_CELL_MAH says the same) */
#define ADV_CHARGE_STOCK  500         /* mA, battery_port_axp2101.c board_charge_ma */
#elif defined(TANK_ROUND)
#define ADV_CELL_STOCK    500
#define ADV_CHARGE_STOCK  400
#elif defined(TANK_WATCH)
#define ADV_CELL_STOCK    400
#define ADV_CHARGE_STOCK  300
#else
#define ADV_CELL_STOCK    200         /* the 1.8 (and the 1.75 / 1.75-B, which run its image at its rate) */
#define ADV_CHARGE_STOCK  150
#endif
#define ADV_CHARGE_MAX    800         /* the ceiling, below the charger's own 1000 mA. 500 in the first build; Strato, 2026-10-28:
                                         "the point of hiding this menu was so that kids cant get to it, i think we need to trust the
                                         system. the adults should be able to make this choice and heed the warnings" - then 800 on
                                         his word the same hour: the charger burns the USB-to-cell drop as heat a few mm from the
                                         glass, a whole fast charge in a closed box was never measured, the 1.25 mm cell plugs are
                                         ~1 A parts, and past ~800 mA the taper comes sooner and saves little */
int  adv_cell_sizes(const int **sizes);   /* the list, smallest first (the board's own is in it) */
bool adv_cell_ok(int mah);                /* one of the list */
int  adv_charge_ma(int mah);              /* the charge current for a cell: the board's own rate for its own cell, else ~0.75 C on the
                                             charger's steps (25 mA to 200, then 100s) up to ADV_CHARGE_MAX - and a cell smaller than
                                             the board's own never faster than the board's own rate */
int  adv_trickle_ma(int mah);             /* the precharge / end-of-charge current: about a tenth of the cell, 25 mA steps, 25..200 */
bool adv_cell_fast(int mah);              /* charges faster than the board's own cell: the warning first */

/* ---- the touch map ---- */
typedef struct { float ax, bx, ay, by; } touch_fit_t;   /* upright frame: x' = ax x + bx, y' = ay y + by */
#define TOUCH_FIT_NONE ((touch_fit_t){ 1, 0, 1, 0 })
bool touch_fit_is_none(const touch_fit_t *f);
bool touch_fit_ok(const touch_fit_t *f);  /* inside the bounds a real panel needs (gain 0.85..1.15, offset within 60 px) */
/* a point on the picture as shown (turned or not) through a map fitted upright */
void touch_fit_apply(const touch_fit_t *f, float *x, float *y, int q);   /* q: the picture's quarter turns (tank_orient) */
/* the least-squares map that takes the presses to the targets, both in the picture as shown when the
   presses were taken (turned = it was turned over), returned in the upright frame; false = not a map
   a panel could need (touch_fit_ok) or too few points */
bool touch_fit_solve(const float *tx, const float *ty, const float *px, const float *py, int n, int q, touch_fit_t *out);
/* ... the same, an axis of a press left out where drop[i] says (bit 0: its x, bit 1: its y, in the picture as shown): a
   press read at the panel's reach in that axis says where the panel ends, not where the finger was (0.4.2: turned over,
   the 1.8 V2's bottom crosses lie past its reach) */
bool touch_fit_solve_masked(const float *tx, const float *ty, const float *px, const float *py, const uint8_t *drop, int n, int q, touch_fit_t *out);

/* THE KEEPER'S CALIBRATION (0.4.2): the sittings it was made from, each fitted upright (the panel's own frame) and tagged
 * with the quarter turn the picture had (tank_orient; -1 = not known: a map kept by 0.4.1). A sitting's offset mixes the
 * panel's own offset (it stays with the glass) and the finger's landing (it turns with the picture: the viewer's "down"),
 * and one sitting cannot tell them apart (docs/touch: only a sitting the other way up can). So:
 *  - one sitting: its map, upright, every way up - the panel's stretch holds turned; a finger's landing off the board's
 *    own model comes back doubled turned over (the 0.4.1 behaviour, unchanged);
 *  - two sittings at different quarters: the gains are the panel's (their mean), the two offsets split into the panel's
 *    and the finger's - one map that holds every way up.
 * A new sitting replaces one taken the same way up, joins one taken another way up, and replaces a 0.4.1 map. */
#define TOUCH_CAL_VER 2
typedef struct { uint8_t ver, n; int8_t q[2]; touch_fit_t s[2]; } touch_cal_t;   /* the "tfit" blob since 0.4.2 (0.4.1's was a touch_fit_t) */
#define TOUCH_CAL_NONE_INIT { TOUCH_CAL_VER, 0, { -1, -1 }, { { 1, 0, 1, 0 }, { 1, 0, 1, 0 } } }
#define TOUCH_CAL_NONE ((touch_cal_t)TOUCH_CAL_NONE_INIT)
bool touch_cal_load(const void *blob, size_t len, touch_cal_t *out);   /* a saved blob, 0.4.1's (a touch_fit_t) or 0.4.2's: false = none */
touch_fit_t touch_cal_at(const touch_cal_t *c, int q);                 /* the map, upright, for the picture turned q */
void touch_cal_merge(const touch_cal_t *old, const touch_fit_t *f, int q, touch_cal_t *out);   /* a new sitting at q kept with the old */

/* ---- the page ---- */
enum { ADV_PG_NONE = 0, ADV_PG_MAIN, ADV_PG_WARN, ADV_PG_TOUCH, ADV_PG_CROSS, ADV_PG_CHECK, ADV_PG_TURN, ADV_PG_DONE };
/* the platform's start-up: the saved size (0 or not in the list = the board's own) and the saved map */
void adv_init(int cell_mah, const touch_fit_t *fit);   /* (fit: a single 0.4.1 map, a sitting of unknown quarter) */
void adv_init_cal(int cell_mah, const touch_cal_t *cal);
int  adv_cell(void);                      /* the size in use */
const touch_fit_t *adv_touch_fit_at(int q);   /* the map EVERY tap goes through now, upright, for the picture turned q (the port
                                             applies it): the saved one, none while the crosses are up, the new one during the check */
const touch_fit_t *adv_touch_fit(void);   /* ... upright (q 0): none = the board's own map (logs, the intro's RESET TOUCH) */
const touch_cal_t *adv_touch_cal(void);   /* what to save at ADV_ACT_FIT (n 0 = none) */
/* the panel's reach on the picture as shown, through the board's own map, as the port sees it now: a cross's press read
   within 2 px of it is left out of that axis's fit (touch_fit_solve_masked). Never set: nothing is left out (the sim) */
void adv_set_reach(float x0, float x1, float y0, float y1);
/* the flow's end (Strato, 11-09): SKIP after the first sitting (or TURN IT OVER left alone) - DONE, one way up (both, if the
   first joined one kept from before); the turned sitting kept - DONE, both (DONE is a page of its own: a check mark, DONE,
   TOUCH CALIBRATION SAVED., OK; adv_touch_msg() still tells the two apart, for tests). The turned sitting cut short, missed or not joining the first - the TOUCH
   page with the line: the first is still saved, alone */
#define ADV_MSG_SAVED_ONE  "SAVED FOR ONE WAY UP"
#define ADV_MSG_SAVED_BOTH "SAVED FOR BOTH WAYS UP"
#define ADV_MSG_TURN_FAIL  "SAVED FOR ONE WAY UP ONLY"
/* EVERY FRAME, before the picture and the touch port take their way up: shown = the way up the platform would show (the
   live one, ROTATION's lock, the watch's SCREEN), live = the IMU's reading now (-1: none to go by - the watch). Returns the
   way up to show and to read the touch in: shown, except during the turned sitting (TURN IT OVER, its crosses, its check):
   the IMU's reading (a lock set aside), or on the watch the first sitting's way up turned over. On TURN IT OVER the IMU
   reaching upside down from the first sitting starts the crosses; turned back mid-sitting, TURN IT OVER again. */
int  adv_quarter(int shown, int live);
bool adv_turned_sitting(void);            /* the second sitting's crosses or check are up (for tests and the log) */
const char *adv_touch_msg(void);          /* the TOUCH page's line now (NULL = none): for tests */
void adv_open(void);                      /* the chord: the page up */
bool adv_up(void);
int  adv_page(void);
void adv_close(void);                     /* the tank goes to sleep, a flow takes the glass: the page goes, the saved map stays */
void adv_tick(float dt);
/* the finger every frame while the page is up: x, y on the picture as shown (through adv_touch_fit),
   turned = the picture is turned over now. Returns an action, one-shot: */
enum { ADV_ACT_NONE = 0,
       ADV_ACT_BACK,                      /* BACK: the settings page again */
       ADV_ACT_CELL,                      /* a size was SET (adv_cell()): save it, tell the charger */
       ADV_ACT_FIT,                       /* the touch map changed for good (adv_touch_fit()): save it */
       ADV_ACT_TICK };                    /* a step or a cross taken: the platform's little click, nothing to save */
int  adv_touch(float x, float y, bool down, int q);
void adv_render(uint16_t *fb, int stride);
/* for tests and the director */
int  adv_pick(void);
void adv_cross_target(int i, float *x, float *y);   /* cross i's centre, frame px */
void adv_check_target(float *x, float *y);
#endif
