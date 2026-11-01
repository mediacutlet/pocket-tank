/* progression.h — the "shaped by attention, never ruined by absence" layer
 * (docs/progression.md + docs/progression-next.md). Platform-agnostic;
 * persistence and wall-clock come in through two tiny port functions.
 *
 *  - Population: a new tank is two contrasting adults; the 3rd..POP_CAP-th
 *    fish ARRIVE as fry when care milestones are met (trust, feedings, a
 *    calm hold, a raised fry, drift). An arrival is staged when earned; a
 *    few seconds later the parents court down in the nursery grass and the
 *    fry is born there, in front of the keeper (2026-09-24 - it used to wait
 *    for the next light-on). A tank put to sleep first has it at the wake.
 *  - Growth: well-fed fish grow (size) and advance fry -> juv -> adult -> elder
 *    with time - every awake second, light on or off (2026-09-14, Strato:
 *    "fish don't stop growing with the light off"), and a slept span at
 *    SLEEP_GROWTH_FRAC of its length; stage changes are silent surprises.
 *  - Trait drift: bold and social creep with experience (hours of pressure).
 *  - Milestones: firsts are DETECTED from what the fish actually did (the
 *    model's own choices), per fish and per tank; render shows them.
 *  - Habits: the tank remembers where you feed and greets you at light-on.
 *  - Ravenous boot: powered off >= 1 h -> fish are starving on return and wait
 *    near the surface (at your usual feeding spot) until the first feeding.
 *    Nothing else is simulated for time away; nothing ever dies.
 *  - Saves: on events (arrival, stage, milestone, light change), coalesced to
 *    >= 30 s apart, plus a 10-minute heartbeat - NVS-friendly. */
#ifndef PROGRESSION_H
#define PROGRESSION_H
#include "tank.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---- platform ports (sim: file + time(); device: NVS + RTC) ---- */
/* where the device keeps the tank: NVS namespace + blob key. Part of the
 * SAVE LAYOUT LOCK (progression.c): every keeper's tank is stored under
 * these two names, so renaming either loses it at the next update.
 * --selftest-saves pins the spelling. */
#define SAVE_NVS_NS  "tank"
#define SAVE_NVS_KEY "save"
bool    persist_port_load(void *buf, size_t max, size_t *got); /* the saved blob, whatever length an older
                                                            * build wrote (*got = it); a NEWER build's longer one
                                                            * loads its first max bytes (*got = max); false = nothing */
bool    persist_port_save(const void *buf, size_t len);
bool    persist_port_erase(void);                          /* the tank on the glass (and a parked copy); the shelf and the board keys stay (D13) */
int64_t clock_port_now_unix(void);                         /* 0 if unknown */
/* the release (version.h PT_RELEASE_NUM) of the build that wrote the save
 * the last load read - 0 for a save from before release numbers, or none */
uint32_t progression_loaded_release(void);
int64_t  progression_loaded_unix(void);                    /* the loaded save's wall-clock stamp, 0 = none (a clockless board seeds its clock from it) */
const char *version_port_string(void);                     /* the build's git describe (device: the app
                                                            * descriptor; sim: PT_VERSION) - the settings page */
/* the board the tank is running on, told from its chips at boot, as a release
 * board id (version.h PT_BOARD) - NULL when it cannot be told. Not PT_BOARD when
 * another board's version was installed (0.3.9.3: the 1.8's image runs on every
 * board): the WRONG VERSION card, and CHECK FOR UPDATES offers this board's.
 * The sim: PT_BOARD, or --hw <id>. */
const char *board_port_hw(void);

/* call after tank_init: restores the saved tank (or creates a new population)
 * and lives through the time since the save was written - since 2026-09-16
 * the same as progression_wake with the port's clock (the cold-boot ravenous
 * rule is gone: a cell that dies in the night is a power-off, and that
 * morning deserves its night too). Returns the hours lived through, -1 for
 * none. */
float progression_boot(tank_t *t);
/* the device waking from DEEP sleep (2026-09-14): restore the save, then live
 * through the dark stretch since it was written - tank_tick_sleep for
 * (now_unix - saved_unix), capped at PROGRESSION_SLEEP_CAP_S. Hunger, energy,
 * grass and algae land where a night of drowse would have put them; a long
 * enough night ends in ravenous begging by itself. Returns the hours
 * simulated, or -1 when no clock was available (restore only). */
#define PROGRESSION_SLEEP_CAP_S (7 * 24 * 3600)
/* how much of a slept span the fish grow through (2026-09-14, Strato: "an
 * eight-hour sleep should be worth 2 hours of growth") */
#define SLEEP_GROWTH_FRAC 0.25f
float progression_wake(tank_t *t, int64_t now_unix);
/* the tank switcher (common/shelf.c): what the board shares goes with a swap -
 * the wallet and the board's settings (docs/tank-switcher.md "The shared
 * data"; the tool LOCK and AUTO FEED stay each tank's own, D14) - taken from
 * the tank leaving the glass and given to the one that comes in */
typedef struct {
    int32_t  sd_balance;
    uint16_t light_idle_s;
    uint8_t  light_auto, light_tip_seen, screen_turned, orient_lock, orient_q, pad;
} progression_carry_t;
void progression_carry_take(const tank_t *t, progression_carry_t *c);
void progression_carry_give(tank_t *t, const progression_carry_t *c);
/* a stored save's saved_unix rewritten in place (the shelf: the wake then lives the time on the shelf) */
bool progression_blob_set_saved_unix(void *blob, size_t len, int64_t unix);
/* a stored save's pieces: its sd_unlocks (0 for a blob that is not a save) - the shelf's REMOVE values them */
uint32_t progression_blob_unlocks(const void *blob, size_t len);
/* live through one stretch of device sleep: tank_tick_sleep, growth at
 * SLEEP_GROWTH_FRAC, and the tank's "first full night's sleep" milestone once
 * a single stretch reaches FULL_NIGHT_S (progression_wake's path; the director's
 * `sleep H` stages the same). */
#define FULL_NIGHT_S (6 * 3600.0f)
void progression_slept(tank_t *t, float seconds);
/* the settings page changed something that rides in the save (the light's
 * idle time / auto-off): an event save follows, coalesced like the others */
void progression_settings_changed(void);
/* call every frame after tank_tick */
void progression_tick(tank_t *t, float dt);
/* fish i is nose to tail with another (the company that warms social) */
bool progression_near_friend(const tank_t *t, int i);
/* fish i is warmed by the keeper's finger: nose to tail with another there, or (0.3.9.3) round it
 * with another - the fish circle a held finger a body apart, not piled under it (tank.c HOLD_RING_*),
 * and a ring neighbour is company: SOCIAL_RING_X the nose-to-tail distance */
bool progression_finger_company(const tank_t *t, int i);
/* call on light-off / shutdown (autosaves on events + heartbeat anyway) */
/* Save the current tank. False means the port rejected the write; the state
 * remains dirty so the periodic saver will retry. */
bool progression_save(tank_t *t);

/* sim/debug: multiply time (aging, drift) - `./fishsim --fast 60` */
extern float progression_time_scale;
/* debug: stage and show an arrival now (sim key R); no-op at the cap */
void progression_force_arrival(tank_t *t);
/* debug: stage an arrival as if its last gate had just closed - the
 * spawning follows (or progression_force_arrival, at once) */
void progression_stage_arrival(tank_t *t);
/* the spawning (2026-09-24, Strato: "new fry comes at next light on" felt
 * unintuitive - there's no real need to turn the light on or off): once an
 * arrival is staged, SPAWN_WAIT_MIN_S..SPAWN_WAIT_MAX_S awake seconds pass
 * (not at once), then the courting pair swims down into the nursery grass;
 * after SPAWN_DANCE_S of circling there together the fry is born between
 * them. Lit or dark. The wait and the dance hold while a page covers the
 * tank (tank_t.ui_cover) or no bed is tall enough to be born in. */
#define SPAWN_WAIT_MIN_S  8.0f
#define SPAWN_WAIT_MAX_S 20.0f   /* + the swim down: the courtship starts within ~30 s */
#define SPAWN_DANCE_S    10.0f
/* the device woke (a deep-sleep boot, or the grace's quick wake): a staged
 * fry that missed its live birth is born now, in the grass */
void progression_woke(tank_t *t);
bool progression_arrival_pending(void);
float progression_age_s(const tank_t *t, int idx);        /* grown seconds */
/* director/debug: put a fish's tended clock at `seconds` and apply the stage
 * and size that go with it now (the silent surprise, on cue) */
void progression_set_age(tank_t *t, int idx, float seconds);
/* director/debug: the new-tank path on a tank_init'd tank - two fry, first
 * milestones, nothing tended yet. Saves over the current save on the next
 * heartbeat: stash it first (firmware director: `stash`). */
void progression_fresh(tank_t *t);
/* the keeper's RESET (device: hold BOOT + tap the glass, then YES on the
 * prompt; sim: X): every save is erased - a director-parked tank too - the
 * tank is re-initialised with `seed`, the new-tank path runs and the fresh
 * pair is saved at once, so a reboot lands on them. Two fry, clean glass,
 * the default garden, nothing tended yet. */
void progression_reset(tank_t *t, uint32_t seed);
/* first-run setup (setup.c, 2026-09-13): a tank born through
 * progression_fresh - a fresh install, a reset - is PENDING setup until the
 * keeper walks the welcome / names / colours flow; the flag rides in the
 * save, so a reboot mid-setup re-opens it. progression_setup_done clears it
 * and saves the names and looks at once. Tanks saved before the flag read as
 * done (nobody's running tank gets the tutorial after an update). */
bool progression_setup_pending(void);
void progression_setup_done(tank_t *t);
/* the birth flow (setup.c, 2026-09-14): every arrival is owed a visit - the
 * announcement, a name, the family page - until the keeper walks it.
 * progression_newborn is the slot still owed (-1 = none); it rides in the
 * save, so a reboot mid-flow brings it back. progression_newborn_done clears
 * it and saves the name at once. */
int  progression_newborn(void);
void progression_newborn_done(tank_t *t);
/* milestones page (2026-09-13): the keeper closed it - everything earned so
 * far counts as seen (badges earned later wear a "new" ring until the next
 * look). Rides in the save. */
void progression_ack_milestones(tank_t *t);
/* the new-fry checklist (2026-09-14, Strato: once the pair starts to grow,
 * the first thing a keeper asks is "how do I get a new fry?" - and the
 * milestones page said nothing). progression_next_fry fills one line per
 * gate of the NEXT arrival: the care gates arrival_conditions counts (the
 * same code, so the list can never disagree with the rule - the population's
 * gates, then the GLASS gate every arrival needs: no fry is conceived in a
 * dirty tank, film on more than ALGAE_DIRTY of the glass, 2026-09-16) plus
 * the nursery bed every arrival needs. Each line: a kind (the renderer
 * picks the art), a title, the words (what to do - a plain sentence over two lines; Strato:
 * "all fish must have a minimum six out of 10 trust score", not a hint), a
 * progress phrase (where it stands), a 0..1 fraction and whether it is met. Returns the count, 0 at the
 * population cap. *staged = every gate is met and the fry is on its way
 * (the spawning). Strings fit the pixel font: <= 25 chars at scale 2. */
enum { FRY_REQ_TRUST, FRY_REQ_FEED, FRY_REQ_HOLD, FRY_REQ_GROW, FRY_REQ_CHANGE, FRY_REQ_GRASS, FRY_REQ_GLASS };
#define FRY_REQ_MAX 5
typedef struct {
    int   kind;
    char  title[12];
    char  words[28], words2[28];   /* what to do, a plain sentence over two lines */
    char  progress[28];            /* where it stands */
    float frac;
    bool  met;
} fry_req_t;
int progression_next_fry(const tank_t *t, fry_req_t out[FRY_REQ_MAX], bool *staged);
/* the tip behind a gate (Strato: "if someone reads 'all fish must have a
 * trust of at least six' they may ask OK how do I do that?"): HOW the
 * keeper moves it, in up to FRY_TIP_LINES lines of <= 26 chars, NULL-
 * terminated. Written from the rules in tank.c / progression.c (trust
 * only rises under a resting finger; quick taps cost it; fish age only
 * while lit; grass regrows by itself; the glass is wiped by a drag). */
#define FRY_TIP_LINES 5
const char *const *progression_fry_tip(int kind);

/* ---- sand dollars (2026-09-15): the points behind the shop ----
 * Care earns them, the shop page (render_shop) spends them. Every award is
 * detected in progression_tick from what the tank already counts - a MEAL
 * (player_feedings), a stage reached (MS_REACHED_*), a birth (do_arrival),
 * full trust (10.0, once per fish), every SD_CHORE_EVERY algae colonies
 * removed and every SD_TRIM_CM of grass trimmed (tank.c's counters) - and the ledger
 * in tank_t (sd_paid_fish, sd_colonies_paid, sd_inches_paid) keeps a save
 * from paying twice. A tank saved before the shop is paid what it already
 * earned on its first boot with it, once (Strato: "yes, pay it once"). */
#define SD_MEAL        1
/* a meal pays SD_MEAL PER FISH, each fish once per SD_MEAL_GAP_S of awake
 * time (0.3.9, Strato 2026-10-23: +1 a fish, was +2 a meal): one gap for the
 * whole tank (0.3.7) left a hungry fish fed by hand unpaid because a
 * tank-mate had just eaten (a keeper on Discord). +1 keeps a 2-fish tank at
 * the 0.3.7 pace and lets a full tank of 5 earn 2.5x. The gap still stops a
 * tap-spam paying a tap (a keeper on Discord, 2026-10-18); every feed gesture
 * the fish eat from is still a MEAL (the fry's feeding gates count them as
 * before). A night's sleep runs each wait down like awake time; a boot
 * starts them clear. */
#define SD_MEAL_GAP_S  300.0f
#define SD_STAGE_JUV   5
#define SD_STAGE_ADULT 10
#define SD_STAGE_ELDER 25
#define SD_BIRTH       20
#define SD_TRUST       15
#define SD_SNACK       10          /* the seahorse's snack, one paid per SH_SNACK_COOL_S (0.3.6) */
#define SD_CRAB_FIND   10          /* a sand dollar the crab dug up and the keeper took (0.3.9; Strato 2026-10-24: a rare event, "worth a bit more") */
#define SD_CRAB_FIND_AGAIN 20      /* ... every one after the first: the badge's reward (Strato 2026-10-24: "double the value of the subsequently found") */
/* what n finds have paid: the crab's card shows this, not the count (0.3.9.1; Strato 10-24: the count of finds read as confusing) */
#define SD_CRAB_FINDS_VALUE(n) ((n) == 0 ? 0u : (unsigned)SD_CRAB_FIND + (unsigned)SD_CRAB_FIND_AGAIN * ((unsigned)(n) - 1u))
#define SD_CHORE       25          /* per SD_CHORE_EVERY colonies / SD_TRIM_CM of grass */
#define SD_CHORE_EVERY 100
/* the grass pays by length cut: 250 cm (2026-09-29, Strato; was 100 inches =
 * 254 cm). The HOW TO EARN line names it - "100 IN OF GRASS CUT" read as the
 * word "in" to a UK keeper (issue #9), and no unit read as 25 for any trim */
#define SD_TRIM_CM     250
#define SD_PRICE_PLANT 40
#define SD_PRICE_SNAIL 80
#define SD_PRICE_CASTLE 150
#define SD_PRICE_CORAL 100
#define SD_PRICE_CLUSTER 240
#define SD_PRICE_SHRIMP 180       /* 2026-09-29, Strato (first 300, then 180) */
#define SD_PRICE_URCHIN 120       /* 2026-10-02: the grass's snail (the snail is 80, the grass pays 25 per 250 cm) */
#define SD_PRICE_SEAHORSE 220     /* 0.3.6, Strato 2026-10-15: between the shrimp (180) and the cluster (240) */
#define SD_PRICE_PUMPKIN 75       /* 0.3.6, Strato 2026-10-17: the seasonal lantern, "affordable" */
#define SD_PRICE_VOLCANO 190      /* 0.3.9: Strato 2026-10-24 (it was the castle's 150 until he named one) */
#define SD_PRICE_CRAB 140         /* 0.3.9: the strawberry crab - Strato 2026-10-24 */
typedef struct {
    uint32_t    bit;               /* SD_ITEM_* */
    const char *name;              /* <= 15 chars: the row draws it at scale 2 from x 76, 178 px, short of the button at 300;
                                    * the modal at scale 3, 267 px in 352 */
    const char *words, *words2;    /* what it does, two lines of <= 25 chars */
    int         price;
} sd_item_t;
extern const sd_item_t SD_ITEMS[SD_ITEM_COUNT];
/* the shop's sale: false when the balance is short or it is already owned;
 * true = unlocked, placed in the tank (tank_plant_place / tank_snail_place)
 * and saved at once */
bool progression_buy(tank_t *t, int item);
/* the seasonal pumpkin (0.3.6): sold while SD_PUMPKIN_IN_SHOP - the first
 * release after October 31 sets it to 0 (Strato: no date check, a board
 * keeps selling it until it is updated). One bought stays in its tank, so
 * the shop keeps its row for the keeper who has one. */
#define SD_PUMPKIN_IN_SHOP 1
bool progression_pumpkin_offered(const tank_t *t);
/* the sale back (2026-09-24, Strato): a placeable piece sells for SD_SELL_PCT
 * of its price (the snail is a permanent resident), goes back to the shop at
 * full price and to its factory state; false = not owned or not for sale.
 * The refund is balance, not earnings; the toast shows it. */
#define SD_SELL_PCT 20
int  progression_sell_value(int item);
bool progression_sell(tank_t *t, int item);
/* the decoration shelf (0.3.9, Strato 2026-10-19: "anything you can MOVE
 * you can SHELVE"; from the community: nothing bought ever disappears). A
 * placeable piece in the tank goes on the shelf: still owned, out of the
 * water, its spot / depth / color / growth kept - and the living ones keep
 * growing there. No money moves. PUT BACK returns it on top of its stack
 * and the platform opens the placement page with the old spot in place.
 * SELL works from the shelf too (progression_sell). false = not owned, not
 * placeable, or already where it is asked to go. */
bool progression_shelve(tank_t *t, int item);
bool progression_unshelve(tank_t *t, int item);
/* the LOCK on a piece (0.3.9, Strato, from the community: a held finger on a
 * decoration keeps opening its placement page by accident). on = a held
 * finger passes the piece by from now on; its page opens from the shop's
 * row (MOVE) only, where the same button reads LOCKED and undoes it. Each
 * piece has its own lock; it rides the shelf and goes with a sale. Saved at
 * once. false = not owned, not placeable, or already as asked. */
bool progression_lock(tank_t *t, int item, bool on);
#define SD_DECOR_BITS (SD_ITEM_PLANT | SD_ITEM_CASTLE | SD_ITEM_CORAL | SD_ITEM_CLUSTER | SD_ITEM_PUMPKIN | SD_ITEM_VOLCANO)   /* the placeable six */
/* a fish sold (2026-10-01, Strato: a SELL button on the milestones page's
 * fish card - "the more mature a fish is, the more sand dollars it is
 * worth. once a fish is sold it opens up a slot for a new fry"). The price
 * is its stage's; the dollars are earnings (the toast shows them). The tank
 * always keeps FISH_KEEP_MIN fish - a pair to court - and a fry still owed
 * its welcome holds every sale until it has been named. The fish after it
 * move down a slot (tank_remove_fish), a staged arrival is called off, and
 * the slot is EARNED again: the next fry's MEALS gate waits for
 * SELL_FRY_MEALS meals more than the tank had eaten at the sale (the first
 * fry's dozen), so a grown tank - every other gate long met - does not hand
 * a fry straight back. Saved at once. */
#define SD_FISH_FRY    5
#define SD_FISH_JUV    15
#define SD_FISH_ADULT  30
#define SD_FISH_ELDER  60
#define FISH_KEEP_MIN  2
#define SELL_FRY_MEALS 12
/* FRY FOR SALE (0.3.5, Strato: "when a player sells a fish for the first
 * time it should award a tank milestone badge and also open up fry
 * purchasing in the shop as long as there is at least one open slot"). The
 * first sale earns TMS_FIRST_SALE (a tank that sold before 0.3.5 gets it at
 * its first boot), and from then on the shop's first row is FRY: three fry,
 * each a look of its own, SD_PRICE_FRY apiece. A bought fry leaves a blank
 * where it stood - any number can be bought while slots and dollars last -
 * and nothing comes back until FRY_OFFER_S of tank time (awake or asleep,
 * no wall clock needed: the pendant may have none) brings a fresh three.
 * A bought fry is a newborn without parents: the birth flow welcomes it
 * (announce, name, family), no birth dollars, its stages pay as any fish's.
 * Taking the tank's last slot calls off a staged arrival.
 * The offer is a seed: each fry's look is a hash of it and its place, so
 * the save keeps two bytes for the seed, two for the clock, one for the
 * blanks - in padding 0.4 (main) leaves free. 0.3.x rolls the colors from
 * 0.4's nine (main: LOOK_COLORS), so a bought fry is an exact genome there;
 * 0.4 rolls the form (shape, fins, pattern) from the same hash. */
#define SD_PRICE_FRY   200
#define FRY_OFFER_N    3
#define FRY_OFFER_S    (24 * 3600.0f)
typedef struct { uint32_t body, accent; bool sold; } fry_offer_t;
bool  progression_fry_for_sale(const tank_t *t);   /* the first sale's badge is earned */
bool  progression_fry_room(const tank_t *t);       /* a slot free and no welcome owed */
void  progression_fry_offer(int k, fry_offer_t *o);
int   progression_fry_left(void);                  /* fry not yet bought in this offer */
float progression_fry_new_in_s(void);              /* tank seconds until a fresh three */
bool  progression_buy_fry(tank_t *t, int k);
void  progression_fry_roll(tank_t *t);             /* director / tests: a fresh three now */
void  progression_fry_open(tank_t *t);             /* director / tests: the shop's fry, as if a fish was sold */
int  progression_fish_value(const tank_t *t, int fish);      /* what it would fetch now */
bool progression_fish_sellable(const tank_t *t, int fish);   /* false: the last pair, or a welcome is owed */
bool progression_sell_fish(tank_t *t, int fish);
/* dollars awarded since the last call (the toast over the live tank) */
int  progression_sd_take_award(void);
/* director / tests: dollars from nowhere (negative takes them away) */
void progression_sd_grant(tank_t *t, int n);
/* the earn table for the shop page: one line per source, "+N WORDS" */
#define SD_EARN_LINES 8
const char *const *progression_sd_earn_lines(void);

/* population ceiling. Compile-time so the device can ship lower until its
 * advisor latency is measured (docs/progression-next.md): firmware passes
 * -DPOP_CAP=5, the sim shows all 6. Never above N_FISH_MAX. */
#ifndef POP_CAP
#define POP_CAP N_FISH_MAX
#endif

/* stage thresholds, in seconds of grown life: awake time in full (the light
 * makes no difference), sleep at SLEEP_GROWTH_FRAC. A desk companion is
 * glanced at over weeks, so the arc is hours/days, not minutes. */
/* tended seconds to each stage (2026-08-30, halved from 1/6/48 h: a fresh
 * tank now starts as FRY, so the first growth spurt lands in the keeper's
 * first session and elder stays a multi-day achievement). 0.3.5 (2026-10-14,
 * Strato: "all of the age stages should last longer, they happen quite fast
 * now"): four times as long - 2 h / 12 h / 4 days. The fry gates read the
 * same numbers (a new fry waits on the youngest growing up), so the tank
 * fills more slowly too. A fish already past a stage under the old numbers
 * keeps it (apply_stage never takes a stage away). */
#define STAGE_JUV_AGE    (2 * 3600)
#define STAGE_ADULT_AGE  (12 * 3600)
#define STAGE_ELDER_AGE  (96 * 3600)
/* trait drift: one full unit (0.11 of the 0..1 trait) per this many hours of
 * sustained pressure */
#define DRIFT_HOURS      2.0f
/* social (2026-10-29): company warms it, and it settles back to the birth temper.
 * The gains stay small against the settle - the model follows almost always at
 * social 7+, so a big follow gain is a runaway to the ceiling - except company
 * the keeper brings: fish gathered together at a held finger */
#define SOCIAL_FOLLOW_GAIN 0.08f /* per DRIFT_HOURS spent following a friend */
#define SOCIAL_NEAR_GAIN   0.04f /* per DRIFT_HOURS spent nose to tail with another fish */
#define SOCIAL_HOLD_GAIN   2.0f  /* ... the two of them gathered at the keeper's finger
                                  * (ten minutes of it: ~half a step of the 0..9 bar for a
                                  * pair, a step for a full tank; 1.0 measured on the 1.8
                                  * 10-29 was a third of a step a pair - too slow to see) */
#define SOCIAL_NEAR_PX     20.0f /* "nose to tail": centers closer than this x the two sizes */
#define SOCIAL_RING_X      1.6f  /* ... and round a held finger, this much further (0.3.9.3) */
#define TRAIT_SETTLE       0.25f /* bold's and social's pull back to the birth temper, per
                                  * DRIFT_HOURS (the gap shrinks ~e-fold in 8 lit hours) */
#define BOLD_CALM_GAIN     0.08f /* bold per DRIFT_HOURS fed + calm (was 0.5: every fish at 0.95
                                  * in a lit day); against the settle a well-kept fish sits
                                  * ~a step bolder than it was born */
#define DRIFT_CHANGE     0.11f  /* CHANGE gate: this much accumulated drift pressure on the
                                 * youngest fish (~26 lit minutes fed and calm, or following) */

/* milestone labels (UI / logs); order = bit order in tank.h */
extern const char *const MS_NAMES[MS_FISH_COUNT];
extern const char *const TMS_NAMES[TMS_COUNT];
#endif
