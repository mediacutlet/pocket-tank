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
#include "passport.h"
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
bool    persist_port_load(void *buf, size_t max, size_t *got); /* the saved blob, whatever length a build
                                                            * wrote: *got = its stored length; a NEWER build's longer
                                                            * one fills buf with its first max bytes (*got stays the
                                                            * whole: progression keeps the rest); false = nothing */
bool    persist_port_save(const void *buf, size_t len);
bool    persist_port_erase(void);                          /* the tank on the glass (and a parked copy); the shelf and the board keys stay (D13) */
/* a blob of its own beside the tank save (the visitor collection, 0.4): the
 * device keeps it under `key` in SAVE_NVS_NS (so a reset erases it too), the
 * sim in a file beside the save. got = its stored length */
bool    persist_port_blob_load(const char *key, void *buf, size_t max, size_t *got);
bool    persist_port_blob_save(const char *key, const void *buf, size_t len);
bool    persist_port_blob_forget(const char *key);         /* gone (true if it was never there): MY TANKS swaps a tank in without one */
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
/* the tank has changed since its last save: one is due at the next window
   (30 s), not the 10-min heartbeat (the selftests read it) */
bool  progression_dirty(void);
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
/* (0.4.2, audit PR-2) the tank badges the last wake's slept span earned (TMS_*: in practice FIRST FULL NIGHT'S SLEEP, which a
 * board only earns across a boot), taken: 0 after the first call, after any progression_tick (the boot is over) and after
 * a reset. notice_sync takes them and leaves them unadopted, so the boot that ends a tank's first long night says it once
 * (the save's own badges stay adopted: an OTA, a SWAP IN, a restore never say an old one) */
uint32_t progression_wake_badges_take(void);
/* (0.4.2, Discord D-5) the fish the last wake's slept span grew up a stage: from[i] = the stage fish slot i had before the
 * night for each of them, its current stage for every other slot. Returns how many; taken as progression_wake_badges_take
 * (0 after the first call, after any progression_tick, after a reset). notice_sync keeps from[] as the stage it last saw,
 * so the first notice_tick says each one's "IS NOW A ..." card with the stage-up chime: the morning's card for a fish that
 * grew up in the night (a deep sleep, a night's power-off, a DORMANT shelf) */
int progression_wake_grew_take(const tank_t *t, stage_t from[N_FISH_MAX]);
/* (0.4.2, audit PW2-2 / PW2-3: OWED CARDS, notice.h) the cards the save this tank was loaded from still owed, by fish slot as
 * the tank is now (a fish gone owes none), taken: empty after the first call and for a tank not loaded from a save (a fresh
 * one, a reset). *gen = the tank's generation (a new one at every load, reset and fresh tank): notice_sync keeps it, and a
 * save asks notice_owed_pack for the queue only while it matches */
struct notice_owed;
void progression_owed_take(const tank_t *t, struct notice_owed *o, unsigned *gen);
/* the tank switcher (common/shelf.c): what the board shares goes with a swap -
 * the wallet and the board's settings (docs/tank-switcher.md "The shared
 * data"; the tool LOCK and AUTO FEED stay each tank's own, D14) - taken from
 * the tank leaving the glass and given to the one that comes in */
typedef struct {
    int32_t  sd_balance;
    uint16_t light_idle_s;
    uint8_t  light_auto, light_tip_seen, screen_turned, orient_lock, orient_q;
    uint8_t  shared;               /* the board's unlocks (0.4.2; was pad, 0 in every older carry): CARRY_BREED = SELECTIVE
                                    * BREEDING, bought once for every tank (Strato 11-08). Given, never taken away */
    uint8_t  lang1;                /* the language + 1 (lang.h), 0 none */
    uint32_t mark;                 /* the swap's: a fresh marker; the save that applies the carry carries it, and a carry whose
                                    * marker the loaded save already has is not given again (0.4.1 audit round 2, "Marker now") */
} progression_carry_t;
#define CARRY_BREED 0x01u
void progression_carry_take(const tank_t *t, progression_carry_t *c);
void progression_carry_give(tank_t *t, const progression_carry_t *c);
/* a stored save's saved_unix rewritten in place (the shelf: the wake then lives the time on the shelf) */
bool progression_blob_set_saved_unix(void *blob, size_t len, int64_t unix);
/* a stored save's pieces: its sd_unlocks (0 for a blob that is not a save) - the shelf's REMOVE values them */
uint32_t progression_blob_unlocks(const void *blob, size_t len);
int progression_blob_breed_paid(const void *blob, size_t len);
bool     progression_blob_sd_restamp(void *blob, size_t len);     /* ... and their day started over (sdg_out_restamp: the clock was moved); true = changed */
int      progression_blob_sd_out(const void *blob, size_t len);   /* sand dollars in flight in a stored tank's save (0.4.2) */
/* live through one stretch of device sleep: tank_tick_sleep, growth at
 * SLEEP_GROWTH_FRAC, and the tank's "first full night's sleep" milestone once
 * a single stretch reaches FULL_NIGHT_S (progression_wake's path; the director's
 * `sleep H` stages the same). */
#define FULL_NIGHT_S (6 * 3600.0f)
void progression_slept(tank_t *t, float seconds);
/* the settings page changed something that rides in the save (the light's
 * idle time / auto-off): an event save follows, coalesced like the others */
void progression_settings_changed(void);
/* THE FIRST TIME SYNC takes over the clock (main.c net_clock_rebase; 0.4.2 audit r2, PW2-4 / PW2-5): called BEFORE the clock
 * is set to `to_unix`, after progression_boot / _wake. Everything stamped on the old clock moves with it, as the tank on
 * the glass does ("nothing lived for the jump"): the shelf's stamps (shelf_rebase) and the day of a gift in flight
 * (sdg_out_restamp). The caller then sets the clock and calls progression_settings_changed (the save). Returns the jump in
 * seconds (0: unknown - no clock and no save to say where the old clock stood - or under a minute: the shelf not moved) */
int64_t progression_clock_rebase(tank_t *t, int64_t to_unix);
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
/* NEW TANK in MY TANKS' library (0.4.1): bought whole or not at all across a power cut at any write (shelf.h shelf_buy).
   The shelf index of the new tank, or -1 (too poor, the shelves full, or the flash refused: nothing bought, nothing paid).
   REMOVE (0.4.1): shelved tank i's pieces paid and saved, then the tank erased (shelf.h shelf_remove_paid) - the refund,
   or -1 (nothing removed, nothing paid). _shelf_settle: at every boot, after progression_boot / _wake and before a swap's
   carry - a buy or a remove a power cut stopped half way */
int  progression_buy_tank(tank_t *t, int price, int64_t now_unix);
/* GET on an experimental tank's row (0.4.2: the piranha tank, the minnows - shelf.h "BOUGHT ONCE"): one a kind; the first
   costs SHELF_XT_PRICE (journaled as NEW TANK is: the tank and the price, both or neither) and the kind is the board's from
   then on - got again after a REMOVE for nothing. Its shelf index, or -1: one is here already, the wallet is short, the
   shelves are full or the flash refused (nothing made, nothing paid) */
int  progression_get_xt(tank_t *t, int type, int64_t now_unix);
int  progression_remove_tank(tank_t *t, int i);
void progression_shelf_settle(tank_t *t);
int  progression_youngest(const tank_t *t);   /* the youngest fish here by its age (GROW, CHANGE), or -1 */
int  progression_newest(const tank_t *t);     /* the fish whose arrival made the tank's latest count, or the last slot */
void progression_came_in(fish_t *f);      /* a fish from another tank: its drift there is no THE TANK CHANGED SOMEONE here */
bool progression_swap_carry(tank_t *t);   /* the boot after a SWAP IN: the carry given, saved, then let go (false: none) */

/* sim/debug: multiply time (aging, drift) - `./fishsim --fast 60` */
extern float progression_time_scale;
/* debug: stage and show an arrival now (sim key R); no-op at the cap */
void progression_force_arrival(tank_t *t);
/* cross-tank fry (phase 5, docs/tank-link-0.4.md; tank_link.c calls these) */
int  progression_fry_room(const tank_t *t);                             /* room here for a new fry (or a gift) */
int  progression_fish_held(const tank_t *t);   /* the fish here + the places held for gifts and fry we said yes to: what counts against POP_CAP */
/* THE CAP's guard: progression_fish_held above POP_CAP is a bug on some path (Strato 2026-11-03: at most POP_CAP fish,
 * never more, on every path). Checked at every path that adds a fish and once a tick (progression_tick, tank_link_tick);
 * a breach calls the platform's hook with the file, line and path: the sim fails the running selftest, the device logs
 * once. A compare, no frame cost. NULL = not checked */
extern void (*progression_cap_hook)(const tank_t *t, int held, const char *file, int line, const char *path);
void progression_cap_check_at(const tank_t *t, const char *file, int line, const char *path);
#define progression_cap_check(t, path) progression_cap_check_at((t), __FILE__, __LINE__, (path))
/* GIVE (tank link phase 6, gift.h): whether a fish may be given (a sale's rules, and home, not at a door);
   one of ours gone next door for good (no sand dollars); a gift from next door, ours now (its slot) */
bool progression_keeps_two(const tank_t *t, int fish);   /* it may go and two stay: the fish of ours on their way out (gifts) counted gone */
bool progression_fish_givable(const tank_t *t, int fish);
bool progression_give_out(tank_t *t, int fish);
int  progression_gift_adopt(tank_t *t, const passport_t *pp);
int  progression_fry_adopt(tank_t *t, const passport_t *pp, int edge);  /* home: one of ours born next door; its slot or -1 */
void progression_fry_kept(tank_t *t, int vis_k);                        /* host: its home said no - ours after all */
bool progression_fry_welcome(tank_t *t, int slot);                      /* ... all of it but the save (the link: one save a tick) */
void progression_fry_home(tank_t *t, int slot);                         /* home: it swam in - its birth flow */
float progression_bond(const tank_t *t, uint32_t vis, uint32_t *mate);  /* a visitor's time together here, 0..1 of its goal, and with whom */
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
double progression_lived_s(const tank_t *t, int idx);     /* seconds alive (0.4.2, ITS STORY's AGE) */
/* THE TRAVEL BOWL (0.4.2 item 14, docs/move-fish-0.4.2.md; progression.c "THE TRAVEL BOWL"): one fish moved between the
   tanks of one board. _move_out puts a fish of ours in the bowl (its record written first, then the fish out and saved);
   _bowl_let_in lets the one waiting into the tank on the glass. Both answer BOWL_OK or why not; _why the same without
   doing it (RAM only: any frame). The card's taps hold a const tank: they _ask, progression_tick does it, _answer says
   how it went (-1: not yet) */
enum { BOWL_OK, BOWL_NO_FISH, BOWL_NO_LAST_TWO, BOWL_NO_FRY_NAME, BOWL_NO_BUSY, BOWL_NO_AWAY, BOWL_NO_GIVING, BOWL_NO_FULL, BOWL_NO_KIND, BOWL_NO_WRITE,
       BOWL_NO_NOTE };   /* (_NOTE: the tank on the glass holds a gift note for the fish - it gave it away once) */
enum { BOWL_ASK_OUT = 1, BOWL_ASK_IN };
int  progression_move_why(const tank_t *t, int fish);
int  progression_move_out(tank_t *t, int fish);
int  progression_bowl_why(const tank_t *t);
int  progression_bowl_let_in(tank_t *t);
bool progression_bowl_has(void);                   /* a fish waits in the bowl */
const char *progression_bowl_name(void);           /* ... its name ("" = none) */
uint32_t progression_bowl_uid(void);
bool progression_bowl_passport(passport_t *pp);    /* ... who it is (one flash read: the card's opening, never a frame) */
void progression_bowl_ask(int what, uint32_t uid);
int  progression_bowl_answer(void);
bool progression_bowl_asked(void);
bool progression_bowl_offer_due(void);             /* the IS WAITING card has not come up since this start */
void progression_bowl_offered(void);
void progression_bowl_reoffer(void);
void progression_debug_bowl_type(int type);        /* (tests: the tank on the glass read as this SHELF_TYPE_; -1 = its own) */
void progression_debug_bowl_kind(int kind);        /* (tests: the fish in the bowl as another kind of creature) */
uint32_t progression_fish_from(const tank_t *t, const fish_t *f);   /* the tank a gift came from (0 = none known) */
/* director/debug: put a fish's tended clock at `seconds` and apply the stage
 * and size that go with it now (the silent surprise, on cue) */
void progression_set_age(tank_t *t, int idx, float seconds);
/* the same for an age a passport brought (home from a visit, a gift): the
 * stage the fish has is kept, as at a load - only the director demotes */
void progression_age_from_passport(tank_t *t, int idx, float seconds);
/* ... one of ours home from a visit (tank_link welcome_home): the same, never older than the time it has lived (0.4.2, LK-1) */
void progression_age_from_visit(tank_t *t, int idx, float seconds);
/* director/debug: the new-tank path on a tank_init'd tank - two fry, first
 * milestones, nothing tended yet. Saves over the current save on the next
 * heartbeat: stash it first (firmware director: `stash`). */
void progression_fresh(tank_t *t);
/* the tank on the glass is a JELLY TANK (shelf type 1, set before the boot loads it): a fresh save is an empty tank - no fry,
   no welcome; the wallet and the board's settings ride in it as in any (docs/jelly-tank-0.6.md) */
void progression_set_jelly(bool on);
bool progression_is_jelly(void);
/* the keeper's RESET (device: hold BOOT + tap the glass, then YES on the
 * prompt; sim: X): every save is erased - a director-parked tank too - the
 * tank is re-initialised with `seed`, the new-tank path runs and the fresh
 * pair is saved at once, so a reboot lands on them. Two fry, clean glass,
 * the default garden, nothing tended yet. */
void progression_reset(tank_t *t, uint32_t seed);
/* ERASE EVERYTHING (0.4.2, the reset prompt's second choice): the board as a new install has it - every tank on the shelf
 * and the one on the glass, the sand dollars, everything bought or unlocked (they live in the saves and the carry: all
 * gone), the shelf's records and markers, the visitor collection and its books, the KIN book, the gift notes, sand dollars
 * in flight. One fresh tank comes back: two fry, an empty wallet, the welcome owed. forget_wifi: the Wi-Fi name and
 * password too (net_port_creds_forget; the board code and the crew's answer beside them stay).
 * KEPT, as none of it is the tank's: the touch map and the battery's size (ADVANCED), the battery's history and log,
 * brightness, volume, GRAPHICS, the board code and HATCHERY CREW, and how the board is worn (SCREEN, the way up).
 * Also kept: LIGHTS OUT's setting and time (the board's, as a swap's carry has them). Also erased: the raw NVS rescue copy
 * at the start of `storage`, if one was ever made (persist_port_rescue_erase: it holds the old saves and Wi-Fi password).
 * A POWER CUT: `wipe` in the frame (shelf.h) is written before anything is erased and deleted last. A cut before it: the
 * old board, whole. After it: the boot finishes the wipe (shelf_boot the shelf, progression_wake the rest) - a save
 * that is not the fresh tank's own (its ID is in the journal) is erased, so the old wallet never comes back.
 * A WRITE REFUSED with the board running on: the journal stands until everything is done; what is owed is tried again
 * before every save and at every boot, the tank runs meanwhile, MY TANKS waits (progression.c "THE RULE").
 * committed (NULL: none): called once the journal has landed, before anything is erased - the platform's goodbyes.
 * false = the journal was refused: nothing erased, nothing called, everything as it was */
bool progression_erase_all(tank_t *t, uint32_t seed, bool forget_wifi, void (*committed)(void));
/* the boot found ERASE EVERYTHING's journal (finished it, or tried): true once, then false - the platform clears what it
   keeps of its own for the tanks (main.c: the jelly tank's jet and light) */
bool progression_erase_settled_take(void);
/* ERASE EVERYTHING's journal stands on the flash (asked BEFORE shelf_boot: the platform puts ERASING on the glass, then boots) */
bool progression_erase_pending(void);
/* a stored blob begins as a tank save does (its first 4 bytes are enough) - the shelf knows a half-written slot by it */
bool progression_blob_is_save(const void *blob, size_t len);
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
/* THE RESET FARM (0.4.1, Discord #help "Sand-dollar farm"): a reset kept the wallet and the new tank paid again for what
 * every fresh tank starts with - its grass cut (+25 on three boards, a minute a cycle), its pair's full trust (+30), a
 * first meal (+1 a fish). tank_t.sd_receipts: what this tank's starting set has paid, carried to the tank a reset makes
 * (Strato 11-05, option 1). Everything grown or earned after pays as before. A NEW TANK starts with none: it pays once */
#define SD_RCPT_GRASS    0x01u
#define SD_RCPT_TRUST_SH 1
#define SD_RCPT_TRUST(r) (((r) >> SD_RCPT_TRUST_SH) & 3u)
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
#define SD_PRICE_BREED 1000        /* 0.4.2 SELECTIVE BREEDING: the unlock (Strato 11-07: late game, worth a lot) */
#define SD_PRICE_PAIR  50          /* ... and each pair, paid at the pairing (refunded on UNPAIR or a broken pair) */
/* SELECTIVE BREEDING (0.4.2): the keeper picks the next fry's two parents. The care gates still say WHEN; the pair says
   WHO, and wins over a visitor's courtship. A parent must be grown (an adult or elder) before the fry comes: until then
   nothing is staged. One payment, one fry. A pair whose fish leaves (sold, given, gone) is refunded */
enum { BREED_NONE, BREED_WAITING, BREED_FULL, BREED_READY };
bool progression_breed_owned(const tank_t *t);
bool progression_breed_buy(tank_t *t);
bool progression_breed_pair(tank_t *t, int a, int b);     /* two fish of ours (slots): SD_PRICE_PAIR paid, the pair set */
bool progression_breed_unpair(tank_t *t);                /* the pair cleared, its price back */
int  progression_breed_state(const tank_t *t, int *young); /* BREED_*; *young = the slot still growing (WAITING), or -1 */
int  progression_breed_slot(const tank_t *t, int k);     /* the pair's fish k (0/1) here, its slot, or -1 */
int  progression_breed_waits_for(const tank_t *t, bool *away, int *also);   /* the pair fish a set pair waits for before it can court:
                                                            one NEXT DOOR first (*away true; *also = the other, next door too, or -1),
                                                            else one still growing (*away false); -1 = no pair, or both here and grown
                                                            (0.4.2 audit r2: NEW FRY's WAITING FOR <NAME>) */
#define SD_PRICE_BG 100           /* 0.4.1: a background, each (Strato 2026-11-04: the community voted 100; first set at 25) */
/* THE BACKGROUNDS (0.4.1): UNLOCK - SD_PRICE_BG from the wallet, owned by this tank, in use at once, saved (false: owned
   already, or short); USE - an owned one (or Open Water) in use, saved (false: not owned) */
bool progression_bg_buy(tank_t *t, int bg);
bool progression_bg_use(tank_t *t, int bg);
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
#define SD_PUMPKIN_IN_SHOP 0                   /* 0.4.0 (11-03): out of the shop, as promised for the first update in November */
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
/* (0.4.2) THE CRITTER SHELF: the same two take a critter (SD_CRIT_BITS) - shelved into its jar with a poof, RESTING there
 * (nothing it does runs, nothing it earns), back where it was with a poof; unshelve is refused with CRIT_CAP critters in
 * the water (tank_crit_room), as a critter's progression_buy is. A critter has no placement page: the platform opens it
 * only for tank_decor_placeable items. */
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
typedef struct { uint32_t body, accent; bool sold; uint8_t shape, fins, pattern; } fry_offer_t;   /* (0.4, 11-01: its form - genetics in the shop) */
bool  progression_fry_for_sale(const tank_t *t);   /* the first sale's badge is earned */
bool  progression_welcome_owed(const tank_t *t, int fish);   /* this fry is not yet welcomed: it is not sold or given till then */
bool  progression_fry_room_to_buy(const tank_t *t);  /* ... and no welcome owed: the shop may sell */
void  progression_fry_offer(int k, fry_offer_t *o);
int   progression_fry_left(void);                  /* fry not yet bought in this offer */
float progression_fry_new_in_s(void);              /* tank seconds until a fresh three */
bool  progression_buy_fry(tank_t *t, int k);
void  progression_fry_roll(tank_t *t);             /* director / tests: a fresh three now */
void  progression_fry_open(tank_t *t);             /* director / tests: the shop's fry, as if a fish was sold */
int  progression_fish_value(const tank_t *t, int fish);      /* what it would fetch now */
bool progression_fish_sellable(const tank_t *t, int fish);   /* false: the last pair, a welcome is owed, or it is next door */
bool progression_sell_fish(tank_t *t, int fish);
void progression_sd_toast(int n);   /* show +n on the toast (money already moved: a sand dollar gift in, 0.4.2) */
/* dollars awarded since the last call (the toast over the live tank) */
int  progression_sd_take_award(void);
/* director / tests: dollars from nowhere (negative takes them away) */
void progression_sd_grant(tank_t *t, int n);
/* the earn table for the shop page: one line per source, "+N WORDS" */
#define SD_EARN_LINES 8
const char *const *progression_sd_earn_lines(void);

/* the population ceiling, POP_CAP (5), is tank.h's: one cap for the sim and every board */

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
