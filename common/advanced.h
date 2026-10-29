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
 * The map is a gain and an offset per axis in the UPRIGHT frame (the board's
 * own calibration's frame), so a tank turned over later keeps it right. */
#ifndef ADVANCED_H
#define ADVANCED_H
#include <stdbool.h>
#include <stdint.h>

#define ADV_BOOT_HOLD_S   5.0f        /* the chord: BOOT held this long before the finger lands (also the reset prompt's) */
#define ADV_CHECK_S       15.0f       /* the check cross: tapped in this long, or the old touch comes back */
#define ADV_CROSS_IDLE_S  30.0f       /* no press on a cross for this long: the calibration gives up, the old touch stays */
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
void touch_fit_apply(const touch_fit_t *f, float *x, float *y, bool turned);
/* the least-squares map that takes the presses to the targets, both in the picture as shown when the
   presses were taken (turned = it was turned over), returned in the upright frame; false = not a map
   a panel could need (touch_fit_ok) or too few points */
bool touch_fit_solve(const float *tx, const float *ty, const float *px, const float *py, int n, bool turned, touch_fit_t *out);

/* ---- the page ---- */
enum { ADV_PG_NONE = 0, ADV_PG_MAIN, ADV_PG_WARN, ADV_PG_TOUCH, ADV_PG_CROSS, ADV_PG_CHECK };
/* the platform's start-up: the saved size (0 or not in the list = the board's own) and the saved map */
void adv_init(int cell_mah, const touch_fit_t *fit);
int  adv_cell(void);                      /* the size in use */
const touch_fit_t *adv_touch_fit(void);   /* the map EVERY tap goes through now (the port applies it): the saved one,
                                             none while the crosses are up, the new one during the check */
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
int  adv_touch(float x, float y, bool down, bool turned);
void adv_render(uint16_t *fb, int stride);
/* for tests and the director */
int  adv_pick(void);
void adv_cross_target(int i, float *x, float *y);   /* cross i's centre, frame px */
void adv_check_target(float *x, float *y);

/* (this fork) THE BATTERY ROW is for a board whose charger the firmware sets: the AXP2101 on upstream's
 * boards. No 320 x 240 board has one - no AXP2101 answers there, so battery_port_set_cell returns false,
 * and nothing in the firmware reaches the CYD's own LiPo charger - so there the page offers the TOUCH row alone, and
 * no tap reaches a size, SET or the warning. Shown, the row would read the 1.8's numbers (the stock chain
 * above falls through to them) and "set" a charge current nothing applies, a smaller one included.
 * A plain 0 / 1, so a C `if` on it keeps both branches compiled in every world. */
#ifdef CONFIG_POCKET_TANK_320X240
#define ADV_BATTERY_ROW 0
#else
#define ADV_BATTERY_ROW 1
#endif
#endif
