#ifndef TOUCH_PORT_H
#define TOUCH_PORT_H
#include <stdbool.h>
#include <stdint.h>
#include "tank.h"
bool touch_port_init(void);
bool touch_port_deep_sleep(void);   /* the night's deep sleep: the chip's own sleep, its reset left high (true = it took it); the wake is a reboot */
void touch_port_poll(tank_t *t);
int  touch_port_selected(void);   /* tapped fish for the stats card, RENDER_CARD_SNAIL for the snail's, -1 = none */
/* milestones page: a tap ON the open stats card (its MORE button, or any of
 * it) flips to it; it has no
 * auto-dismiss - the next tap anywhere closes it (and the card, if still up).
 * touch_port_show_milestones is the director's cue (needs no card). */
bool touch_port_milestones(void);
void touch_port_show_milestones(bool on);
void touch_port_dismiss(void);    /* drop the card and the page: a flow (the birth flow) took the glass */
void touch_port_set_inverted(bool inverted);   /* mirror coords when the screen is flipped */
void touch_port_set_quarter(int q);            /* ... or turned a quarter at a time (tank_orient; 0.3.9.3) */
/* reset confirm prompt (render_confirm_reset): opened by main.c's chord -
 * BOOT held, then a finger lands on the glass - or the director's `reset`.
 * While it is up every other gesture is swallowed; a press AND release on
 * the same button answers it, and it answers NO by itself after 20 s (or
 * when the tank goes to sleep). The answer is one-shot: main.c takes it
 * once per frame and a YES wipes the tank. */
void touch_port_confirm_open(void);            /* the chord: its first page, the choice (0.4.2: render.h RESET_PG_CHOOSE) */
void touch_port_confirm_open_page(int page);   /* ... or any of its pages (RESET_PG_*: the director, the selftests) */
bool touch_port_confirm_answer(int ans);       /* RESET_ANS_*: +1 reset this tank / -1 no / 2, 3 erase everything (3: the Wi-Fi too); false = no prompt up */
bool touch_port_confirm_up(void);
float touch_port_confirm_frac(void);           /* time left before it gives up, 1 -> 0 */
int  touch_port_confirm_take(void);            /* the answer (RESET_ANS_*) once, then 0 */
bool touch_port_pressed_since(int64_t us);     /* a finger is down and landed after `us` */
/* the settings page (2026-09-15): opened from the milestones page's SETTINGS
 * button; a tap on a segment or a slider is handed to main as SET_TAP_BRIGHT / _VOLUME (and their _LIVE)
 * with its value (one-shot), CLOSE ends the page */
bool touch_port_settings(void);
void touch_port_show_settings(bool on);
void touch_port_settings_from_shelf(void);   /* (0.4.2) the page just shown came from MY TANKS's cog: its CLOSE goes back there */
bool touch_port_take_shelf_return(void);     /* ... that CLOSE, once: main.c opens MY TANKS again */
/* THE JELLY TANK on the glass: the bare glass goes to cb (jelly_touch's shape), pages keep theirs; NULL = the fish tank */
void touch_port_set_jelly(void (*cb)(float x, float y, bool down, double now));
int  touch_port_take_setting(int *value);       /* SET_TAP_* or 0 */
void touch_port_push_setting(int what, int value);   /* a setting changed off the glass (the director's `gfx`, `vignette`: SET_TAP_GFX / SET_TAP_VIG only): handed to main
                                                        by touch_port_take_setting after the glass's own, one a kind kept - never in the place of another */
/* the ADVANCED page (0.3.9.2, common/advanced.c): opened by main.c's chord on the settings page (BOOT held 5 s,
 * then a finger lands); while it is up adv_up() is true and it owns the glass. take_adv hands main ADV_ACT_CELL /
 * ADV_ACT_FIT once (save it; tell the charger) */
int  touch_port_open_advanced(void);   /* the BOOT chord: ADVANCED up; returns the slider its tap jumped (SET_TAP_BRIGHT /
                                        _VOLUME, unsaved: main.c puts the saved level back), else 0 */
int  touch_port_take_adv(void);
/* the shop page (2026-09-15): up / show it; an UNLOCK or MOVE tapped hands
 * the raw tap code (SHOP_TAP_BUY / SHOP_TAP_MOVE + item; 0 = none) to main
 * once, which buys (progression_buy) or opens the placement page */
bool touch_port_shop(void);
bool touch_port_collection(void);   /* the VISITOR COLLECTION page is up (2026-10-06) */
void touch_port_show_collection(bool on);   /* the director's cue (0.4 pages: coll page N) */
void touch_port_show_shop(bool on);
int  touch_port_take_shop(void);
/* the battery pill and its page (2026-09-24): main.c says each frame whether
 * it drew the pill (a tap in RENDER_BAT_HIT counts only then); the page is up
 * until any release, or 30 s; touch_port_show_battery is the director's cue */
void touch_port_set_pill(bool up);
/* THE TOUCH LOCK (0.3.9.3): while on, nothing a finger does reaches the tank or opens a page - only MY TANKS
 * (BOOT twice) takes taps, its padlock undoes it. A touch shows the way back (touch_port_lock_hint) */
bool touch_port_finger(float *x, float *y);   /* the finger this poll, calibrated (false: none - x, y its last place) */
void touch_port_set_touch_lock(bool on);
bool touch_port_touch_lock(void);
void touch_port_set_quiet(bool on);   /* LOADING (the boot show): taps over the bare tank go nowhere (0.4) */
bool touch_port_lock_hint(void);               /* a touch on the locked glass in the last 2 s: draw render_touch_lock_hint */
bool touch_port_battery(void);
void touch_port_show_battery(bool on);
/* the UPDATES page (2026-09-30, docs/OTA.md): from the settings page's
 * UPDATES button; its CHECK is handed to main once (UPD_TAP_CHECK), which
 * saves and restarts into update mode; CLOSE returns to the settings page */
bool touch_port_updates(void);
void touch_port_show_updates(bool on);
bool touch_port_shelf(void);                  /* MY TANKS (the tank switcher, docs/tank-switcher.md): BOOT twice */
void touch_port_show_shelf(bool on);
void touch_port_shelf_back(void);             /* main.c: THE LIBRARY, a card or MOVE closed, MY TANKS's main page is back under the finger -
                                                 its open wait again (0.4.1 audit round 2, R2T-5: a doubled BACK shut MY TANKS, a doubled
                                                 outside tap locked the touch) */
bool touch_port_take_shelf_tap(float *x, float *y, int64_t *press_us);   /* a tap on MY TANKS, once (frame coordinates; when its press began:
                                                                        main.c's waits read it, touch_gate.h READ AT THE PRESS) */
void touch_port_spend_press(void);              /* main.c took the finger down now as a hold (MY TANKS's MOVE, REMOVE): its lift is no tap (0.4.1) */
int  touch_port_take_update(void);              /* UPD_TAP_CHECK or UPD_TAP_ADD (0.4.2, ADD A NETWORK) once, else 0 */
/* update mode's raw read (no tank, no pages): the finger in tank space, true while down */
bool touch_port_read_raw(float *x, float *y);
void touch_port_set_bias(int px);              /* finger-landing correction: reported y moves up by px */
int  touch_port_bias(void);
void touch_port_set_log(bool on);              /* one log line per press, at its release (director `touch log on|off`) */
/* the round board's lift rule (2026-10-03): silence of ms inside a press is a
 * lift (0 = keep), and said 1/0 = a "no finger" report lifts at once or not
 * (-1 = keep); director `touch lift <ms>` / `touch said on|off`, not saved */
void touch_port_set_lift(int ms, int said);
int  touch_port_lift_ms(void);
bool touch_port_lift_said(void);
void touch_port_set_mirror(int m);               /* the CST92xx boards: bit 0 x mirrored, bit 1 y mirrored, bit 2 axes swapped (the bench) */
int  touch_port_mirror(void);
void touch_port_raw_seen(int *x0, int *x1, int *y0, int *y1);   /* the raw report's extremes since boot, in tank space (x1 < x0: no touch yet) */
#endif
