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
bool    persist_port_erase(void);                          /* EVERY saved tank, parked copies included */
int64_t clock_port_now_unix(void);                         /* 0 if unknown */
/* the release (version.h PT_RELEASE_NUM) of the build that wrote the save
 * the last load read - 0 for a save from before release numbers, or none */
uint32_t progression_loaded_release(void);
int64_t  progression_loaded_unix(void);                    /* the loaded save's wall-clock stamp, 0 = none (a clockless board seeds its clock from it) */
const char *version_port_string(void);                     /* the build's git describe (device: the app
                                                            * descriptor; sim: PT_VERSION) - the settings page */

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
#define SD_MEAL        2
#define SD_STAGE_JUV   5
#define SD_STAGE_ADULT 10
#define SD_STAGE_ELDER 25
#define SD_BIRTH       20
#define SD_TRUST       15
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
#define SD_PRICE_URCHIN 120
#define SD_PRICE_FROGMAN 10       /* 2026-10-10: the frogman, on his own since the afternoon - a drifting diver */
#define SD_PRICE_SUB 60           /* 2026-10-10 night: the submarine - cruises, bubbles, stops to raise a periscope */
#define SD_PRICE_WRECK 200        /* 2026-10-10: the shipwreck - a sunken boat with holes, an anchor, a frogman */       /* 2026-10-02: the grass's snail (the snail is 80, the grass pays 25 per 250 cm) */
/* the species (2026-10-05, docs/species.md): one juvenile a purchase since
 * 2026-10-07 (a pair before), 10 sand dollars a creature (Alvin's call: the
 * creatures are the point of the tank, not days of saving); the exotic-ladder
 * prices (140..400 a pair) are in git before this date. A creature sells back
 * for this at most (progression_fish_value). */
#define SD_PRICE_SP_CRAB      10
#define SD_PRICE_SP_SWORDFISH 10   /* 2026-10-10 night */
#define SD_PRICE_SP_SEAHORSE  10
#define SD_PRICE_SP_PUFFER    10
#define SD_PRICE_SP_SQUID     10
#define SD_PRICE_SP_LOBSTER   10
#define SD_PRICE_SP_JELLYFISH 10
#define SD_PRICE_SP_OCTOPUS   10
#define SD_PRICE_SP_ANGLER    10
#define SD_PRICE_SP_EEL       10
#define SD_PRICE_SP_SHARK     10
typedef struct {
    uint32_t    bit;               /* SD_ITEM_* */
    const char *name;              /* <= 12 chars, the pixel font */
    const char *words, *words2;    /* what it does, two lines of <= 25 chars */
    int         price;
} sd_item_t;
extern const sd_item_t SD_ITEMS[SD_ITEM_COUNT];
/* the shop's sale: false when the balance is short or a thing is already
 * owned; true = unlocked, placed in the tank (tank_plant_place /
 * tank_snail_place) and saved at once. A species' item (SD_ITEM_SP_*) sells
 * ONE juvenile (2026-10-07) as often as there is a free place
 * (progression_has_room_one): it joins by the reef, and the item's bit only
 * records "some of them are in the tank" (sync below) - it never locks the item. */
bool progression_buy(tank_t *t, int item);
/* the species behind a shop item (SD_ITEM_SP_*), or -1 for a thing */
int  progression_item_species(int item);
/* room for a pair: two more creatures under POP_CAP (and N_FISH_MAX) */
bool progression_has_room(const tank_t *t);
bool progression_has_room_one(const tank_t *t);   /* one more place (the shop's single creature, 2026-10-07) */
/* the species' bits follow the tank: set while any of a species lives here
 * (bought, or a surprise hatched), cleared once none do. The shop reads it
 * for the modal's count only (2026-10-07; it used to lock the pair). Runs on
 * load, after a sale, a birth and a buy. */
void progression_species_sync(tank_t *t);
/* director / tests: a species' pair from nowhere - no price, no shop bit
 * asked (one already here may get company); false = no room. Saved. */
bool progression_spawn_pair(tank_t *t, int species);
/* the sale back (2026-09-24, Strato): a placeable piece sells for SD_SELL_PCT
 * of its price (the snail is a permanent resident), goes back to the shop at
 * full price and to its factory state; false = not owned or not for sale.
 * The refund is balance, not earnings; the toast shows it. */
#define SD_SELL_PCT 20
int  progression_sell_value(int item);
bool progression_sell(tank_t *t, int item);
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
int  progression_fish_value(const tank_t *t, int fish);      /* what it would fetch now */
bool progression_fish_sellable(const tank_t *t, int fish);   /* false: the last pair, or a welcome is owed */
bool progression_sell_fish(tank_t *t, int fish);
/* dollars awarded since the last call (the toast over the live tank) */
int  progression_sd_take_award(void);
/* director / tests: dollars from nowhere (negative takes them away) */
void progression_sd_grant(tank_t *t, int n);
/* the earn table for the shop page: one line per source, "+N WORDS" */
#define SD_EARN_LINES 6
const char *const *progression_sd_earn_lines(void);

/* population ceiling. Compile-time so a board can ship lower (the device
 * shipped 5 until its advisor latency was measured, docs/progression-next.md).
 * Since the species (2026-10-05) every board holds N_FISH_MAX (10 then, 25
 * since 2026-10-07): each creature is a turn in the advisor's queue, ~3.7 s a
 * decision on the device, so a full tank of 25 gets a fresh decision every
 * ~90 s (the reflex layer moves them in between). Never above N_FISH_MAX. */
#ifndef POP_CAP
#define POP_CAP N_FISH_MAX
#endif

/* stage thresholds, in seconds of grown life: awake time in full (the light
 * makes no difference), sleep at SLEEP_GROWTH_FRAC. A desk companion is
 * glanced at over weeks, so the arc is hours/days, not minutes. */
/* tended seconds to each stage (2026-08-30, halved from 1/6/48 h: a fresh
 * tank now starts as FRY, so the first growth spurt lands in the keeper's
 * first session and elder stays a multi-day achievement) */
#define STAGE_JUV_AGE    (30 * 60)
#define STAGE_ADULT_AGE  (3 * 3600)
#define STAGE_ELDER_AGE  (24 * 3600)
/* trait drift: one full unit (0.11 of the 0..1 trait) per this many hours of
 * sustained pressure */
#define DRIFT_HOURS      2.0f
#define DRIFT_CHANGE     0.11f  /* CHANGE gate: this much accumulated drift pressure on the
                                 * youngest fish (~26 lit minutes fed and calm, or following) */

/* milestone labels (UI / logs); order = bit order in tank.h */
extern const char *const MS_NAMES[MS_FISH_COUNT];
extern const char *const TMS_NAMES[TMS_COUNT];
#endif
