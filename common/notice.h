/* notice.h - the announcement modals (docs/AUDIO.md section 4a, 2026-09-15):
 * a milestone the moment it is earned, a stage reached, low battery.
 *
 * Milestones are set in several places (progression.c, tank.c), so the
 * queue does not need hooks: notice_tick diffs each fish's ms_bits, the
 * tank's tank_ms_bits and each fish's stage against what it announced
 * last frame. notice_sync adopts the current state silently (boot, a
 * restored save, a reset) so nothing old is announced. A fish that
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
 * doing other things on screen"): for its first NOTICE_HOLD_S a notice
 * ignores taps - notice_dismiss spends the tap (returns true: it does not
 * reach the tank either) and the notice stays up for its full time. After
 * the hold a tap closes it as before. A notice that stepped aside and came
 * back starts its hold again. The lights-out notice is exempt: its tap must
 * reach the tank at once (below).
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
typedef struct { int kind; int fish; uint32_t bit; float age; } notice_t;

#define NOTICE_UP_S   6.0f
#define NOTICE_HOLD_S 2.0f                  /* a tap this soon after it came up is spent, not a close */
#define NOTICE_GAP_S  1.0f
#define NOTICE_QUEUE  8

void notice_sync(const tank_t *t);
void notice_tick(const tank_t *t, float dt, bool blocked);
const notice_t *notice_current(void);      /* NULL = none up */
bool notice_dismiss(void);                 /* a tap: true = the notice took it (closed, or held: still up), false = it goes on to the tank */
void notice_low_battery(void);             /* platform: the gauge fell to the threshold */
void notice_updated(void);                 /* platform: the first boot of a new release (after notice_sync) */
void notice_model_damaged(void);           /* platform: the AI model is damaged (after notice_sync): once per start */
void notice_wrong_board(void);             /* platform: this version is another board's (after notice_sync): a cold start's */
int  notice_take_cue(void);                /* SND_* for a notice that just came up, else -1 */
int  notice_pending(void);                 /* queued, not counting the one up */

#endif
