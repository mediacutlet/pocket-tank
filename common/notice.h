/* notice.h - the announcement modals (docs/AUDIO.md section 4a, 2026-09-15):
 * a milestone the moment it is earned, a stage reached, low battery.
 *
 * Milestones are set in several places (progression.c, tank.c), so the
 * queue does not need hooks: notice_tick diffs each fish's ms_bits, the
 * tank's tank_ms_bits and each fish's stage against what it announced
 * last frame. notice_sync adopts the current state silently (boot, a
 * restored save, a reset) so nothing old is announced - all but the tank
 * badges the boot's own wake earned (0.4.2, audit PR-2: FIRST FULL NIGHT'S
 * SLEEP, earned across a boot on a board, was never said; progression_wake_
 * badges_take): those the first notice_tick says, once. A fish that
 * appears (an arrival) is adopted silently too - the birth flow is its
 * announcement - but the population milestone it brings is queued.
 *
 * One notice is up at a time for NOTICE_UP_S, or until a tap; the next
 * waits NOTICE_GAP_S. Nothing shows while `blocked` (setup / birth flow /
 * reset prompt up, or a fry's welcome due - setup_birth_due):
 * the queue holds, and a notice already up steps aside and comes back after
 * (without its cue again). 2026-09-29, Strato: "the milestone achievement
 * notification and the new fry welcome message overlap. the milestone should
 * wait until the 'meet it' sequence is done" - the fry is born mid-frame and
 * its population badge came up the frame before the flow opened.
 * Each notice carries the cue to play when it comes up; the platform takes
 * it with notice_take_cue.
 *
 * THE HOLD (2026-10-23, Strato + the community: "they accidentally close
 * those modals and don't have a chance to read them if they are tapping and
 * doing other things on screen"): a press that began before the notice
 * came is spent (notice_dismiss_held returns true: it does not reach the
 * tank either) and the notice stays; any later tap closes it (THE TOUCH
 * RULE, touch_gate.h, 11-06: the 2 s hold before it is gone). One back from
 * under a page a tap closed also spends that tap's double (the touch port).
 * The lights-out notice is exempt: its tap must
 * reach the tank at once (below).
 *
 * THE DARK HOLDS NOTICES (0.4.2, a keeper's dark tank lit up at 5 am for a
 * fry's NAME page; Strato: "tank light off should be treated largely as a
 * notification suppressor too"): while the light is off (tank_light_off)
 * nothing comes up and nothing chimes. The diffing runs on, so everything is
 * queued in order and comes up, with its chime, once the light is on; one up
 * when the light goes off steps aside as under a page. Four are shown all the
 * same, from wherever they are in the queue: LIGHTS OUT (it explains the
 * dark), LOW BATTERY, AI MODEL DAMAGED and the WRONG VERSION card - and a full
 * queue of held ones makes room for them (the newest held one goes: its ring
 * is on the page). One the dark stepped aside keeps the time it was shown: it
 * comes back for the rest of its NOTICE_UP_S (no chime, the touch rule's
 * clock from zero), and one shown NOTICE_SEEN_S or more is done - so a tank
 * on AUTO at 5 s, dark again before a card's 6 s are up, still gets through
 * its queue, a card a wake. The queue lives in RAM, but what it still owes is
 * saved (OWED CARDS, below): what is still held when the tank sleeps is said at
 * the next start, once the light is on.
 *
 * OWED CARDS (0.4.2, audit round 2 PW2-2 + PW2-3; Strato 11-13: "save the owed
 * cards until the light is on"): a tank that boots dark (LIGHTS OUT MANUAL, the
 * light left off at bedtime) held the night's cards - FIRST FULL NIGHT'S SLEEP,
 * "IS NOW A ..." - and a news card earned awake in the dark, and a sleep before
 * the light came on dropped them for good. Every save now writes what is still
 * owed (notice_owed_pack: the queue and the sync's owed badges not yet queued)
 * - the tank badges, each fish's badges,
 * a stage card per fish - and the boot that loads it owes them again
 * (progression_owed_take, in notice_sync, as the wake's own). Only the kinds a
 * tank earns: the boot's own cards (UPDATED, LOW BATTERY, ...) are not saved.
 * A card owed on the flash is saved the frame it comes up (progression_tick),
 * so a power cut can't say it twice. A reset, ERASE EVERYTHING and a sync with
 * no load in between owe nothing, as before. The record counts only under the
 * head it was written with (progression.c owed_sig): a save another build
 * rewrote - a rollback - owes nothing.
 *
 * NOTICE_LIGHTS_OUT (2026-10-03, Strato: "in case it surprises someone the
 * first time it happens"): the first double-tap that turns the light off
 * (tank.h light_tip_seen, saved) says so, once, without a chime. A tap on it
 * closes it AND reaches the tank (notice_dismiss returns false), so the
 * double-tap it asks for works with the notice still up.
 *
 * NOTICE_MODEL_DAMAGED (2026-10-21, a keeper's fish parked in one corner for
 * days: an erased stretch of the AI model made every decision's odds not a
 * number, and the sampler fell through to inspect_reef). The platform raises
 * it when the boot check finds a model body we never shipped, or the first
 * time a decision comes back with odds that are not numbers
 * (advisor_core_damaged). Once per start; it stays up until a tap (a keeper
 * who looked away would miss six seconds) and comes back at the next start
 * until a reinstall fixes the model. The fish keep doing what the model says:
 * the notice tells the keeper, it does not decide for the fish.
 *
 * NOTICE_WRONG_BOARD (0.3.9.3, a Discord keeper put the 1.8's version on a
 * watch): the 1.8's image runs on every board, small and on its side, and
 * the boot's chip probe knows the board (board_port_hw). When that is not the
 * board this version is for, a cold start (not a wake from sleep) says so and
 * names the fix; it waits for a tap, as the damaged model does. */
#ifndef NOTICE_H
#define NOTICE_H
#include <stdbool.h>
#include <stdint.h>
#include "tank.h"

enum { NOTICE_MILESTONE, NOTICE_TANK_MILESTONE, NOTICE_STAGE, NOTICE_LOW_BATTERY, NOTICE_UPDATED, NOTICE_LIGHTS_OUT, NOTICE_MODEL_DAMAGED, NOTICE_WRONG_BOARD };
typedef struct { int kind; int fish; uint32_t bit; float age;   /* age: the time it has been shown, in all (queued: < 0 = it stepped aside) */
                 float since; } notice_t;                        /* (0.4.2) up: the time since it came up or came back - the touch rule's clock.
                                                                    Queued: the time one the dark stepped aside had already been shown */

#define NOTICE_UP_S   6.0f
#define NOTICE_HOLD_S 2.0f                  /* (no longer a tap guard since THE TOUCH RULE, touch_gate.h: notice_dismiss_held) */
#define NOTICE_GAP_S  1.0f
#define NOTICE_SEEN_S 4.0f                  /* (0.4.2) shown this long when the light went off: it has been read, it does not come back */
#define NOTICE_QUEUE  12                   /* (0.4.2, D-5: was 8) a morning's boot may queue UPDATED, WRONG BOARD, the night's badge, a stage card per fish, LOW BATTERY */

void notice_sync(const tank_t *t);
void notice_tick(const tank_t *t, float dt, bool blocked);
const notice_t *notice_current(void);      /* NULL = none up */
void notice_fish_removed(int idx);         /* a fish left, the slots shifted: notices follow their fish (or go) */
bool notice_dismiss(void);
bool notice_dismiss_held(float held_s);
bool notice_came_back(void);              /* the notice up stepped aside under something and came back (a tap closed that page: the port gives it its window) */    /* the same, for a press held_s long: one begun before the notice came is spent */                 /* a tap: true = the notice took it (closed, or held: still up), false = it goes on to the tank */
void notice_low_battery(void);             /* platform: the gauge fell to the threshold */
void notice_updated(void);                 /* platform: the first boot of a new release (after notice_sync) */
void notice_model_damaged(void);           /* platform: the AI model is damaged (after notice_sync): once per start */
void notice_wrong_board(void);             /* platform: this version is another board's (after notice_sync): a cold start's */
int  notice_take_cue(void);                /* SND_* for a notice that just came up, else -1 */
bool notice_card_room(const tank_t *t);     /* nothing else has the glass: a card may come up by itself (the travel bowl's IS WAITING) */
int  notice_pending(void);                 /* queued, not counting the one up */
/* OWED CARDS (0.4.2, above): what is still to be said, by fish slot as the tank is now. tms: tank badges (TMS_*), ms: each
   fish's badges (MS_*: the badges are bits < MS_FISH_COUNT), stage: bit i = fish slot i's "IS NOW A ..." card */
typedef struct notice_owed { uint32_t tms; uint16_t ms[N_FISH_MAX]; uint8_t stage; } notice_owed_t;
bool notice_owed_pack(const tank_t *t, unsigned gen, notice_owed_t *o);   /* false (o empty) = the queue is not this tank's: no sync
                                                                             since tank generation gen began (progression's) */

#endif
