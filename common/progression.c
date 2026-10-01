#include "progression.h"
#include "version.h"
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#define SAVE_MAGIC 0x50544b32u   /* "PTK2" (PTK1 saves are 4-fish, pre-population: start fresh) */
#define RAVENOUS_GIVE_UP_S 150.0f  /* begging window before the fish give up */
#define SAVE_HEARTBEAT_S 600.0f
#define SAVE_MIN_GAP_S   30.0f

const char *const MS_NAMES[MS_FISH_COUNT] = {
    "arrived", "first meal from you", "first hold-approach", "first dart", "first bubbles",
    "first reef", "(retired)", "(retired)", "first follow",
    "reached juv", "reached adult", "reached elder",
};
const char *const TMS_NAMES[TMS_COUNT] = {
    "a pair", "a trio", "a quartet", "a quintet", "a sextet",
    "first full night's sleep", "first play session", "the tank changed someone", "first feeding",
    "first trimming", "first glass cleaning",
};

typedef struct {
    uint8_t preset, stage; uint16_t pad;
    float size, trust, bold, sociable, bold0, sociable0, hunger, energy, stress, curiosity;
    float age_s, rest_dx, rest_dy;
    int32_t eaten, eaten_player;
    uint32_t ms_bits;
} fish_save_t;

typedef struct {
    uint32_t magic;
    int64_t  saved_unix;
    float    clock;
    bool     light_override, light_on, arrival_pending;
    uint8_t  n_fish;
    float    feed_spot_x;
    int32_t  player_feedings, hold_approaches;
    uint32_t tank_ms_bits;
    fish_save_t fish[N_FISH_MAX];
    /* ---- upkeep tail (2026-08-30). Fields only ever APPEND here: boot falls
     * back to loading the prefix above from an older PTK2 save; the zeroed
     * tail then reads as clean glass and default vegetation (0 is not a legal
     * growth - the floor is VEG_NUB - so restore treats it as "keep the
     * fresh-tank default"). ---- */
    float    veg_growth[VEG_BEDS];
    uint8_t  algae[ALGAE_CELLS];
    int32_t  trims, cells_cleaned;
    /* per-frond heights (2026-09-04); an older save (no tail, or zeros)
     * seeds every frond from its bed's veg_growth */
    float    veg_h[VEG_BEDS][VEG_FRONDS_MAX];
    /* identity tail (2026-09-13, first-run setup): the keeper's names and
     * colours per fish - an empty name / a zero colour = the preset's - and
     * whether the setup flow still owes the keeper a visit. Older saves read
     * zeros: preset looks, setup done. */
    uint8_t  setup_pending, pad_id[3];
    char     names[N_FISH_MAX][FISH_NAME_MAX + 1];
    uint32_t body[N_FISH_MAX], accent[N_FISH_MAX];
    float    bubble_x;                   /* the keeper's bubble column (0 = the default spot) */
    /* seen-milestones tail (2026-09-13, the milestones page): what the
     * keeper has already looked at, so a badge earned since wears a ring.
     * Older saves read zeros: everything earned shows as new once. */
    uint32_t ms_seen[N_FISH_MAX], tank_ms_seen;
    /* family tail (2026-09-14, the birth flow): each fish's parents and the
     * arrival still owed its welcome, all as slot + 1 so an older save's
     * zeros read as "none". */
    uint8_t  newborn_p1, parent_p1[N_FISH_MAX][2], pad_fam[3];
    /* drift-pressure tail (2026-09-15, the CHANGE gate): older saves read
     * zeros, and the youngest simply starts earning it from this build on */
    float    drift_acc[N_FISH_MAX];
    /* light settings tail (2026-09-15, the settings page): 0 seconds = the
     * default (older saves); auto 0 = MANUAL, the double-tap (the default) */
    uint16_t light_idle_s; uint8_t light_auto, light_manual_off;   /* auto 0 = MANUAL, the default */
    /* sand dollar tail (2026-09-15, the shop): the balance, the lifetime
     * total, the unlocks, the paid ledger, the two chore counters, the snail's
     * spot and the sword plant's leaves. Older saves read zeros: no dollars,
     * nothing bought, nothing paid - and the ledger then pays what the tank
     * already earned on the first tick, once. */
    int32_t  sd_balance, sd_earned;
    uint32_t sd_unlocks;
    uint32_t sd_paid_fish[N_FISH_MAX];
    int32_t  sd_colonies_paid, sd_inches_paid;
    int32_t  algae_colonies;
    float    trim_px;
    float    snail_x, snail_y;           /* 0 = not placed yet */
    float    veg_h3[VEG_FRONDS_MAX];     /* bed 3: zeros = VEG_START when it is bought */
    /* placement tail (2026-09-16, the placement page): the plant's centre x
     * (0 = the default spot) and its layer + 1 (0 = MIDDLE, an older save) */
    float    plant_x;
    uint8_t  plant_z1, pad_place[3];
    /* the snail's tally (2026-09-16, its card): cells grazed clean, lifetime.
     * Older saves read 0 - it starts counting from this build on. */
    int32_t  snail_grazed;
    /* the castle's spot (2026-09-16): its centre x (0 = the default) and its
     * layer + 1 (0 = FRONT, an older save or one that never placed it) */
    float    castle_x;
    uint8_t  castle_z1, pad_castle[3];
    /* the coral (2026-09-23): its centre x (0 = the default), its layer + 1
     * (0 = MIDDLE, a save that never placed it) and its colour (0 = the
     * palette's first). Older saves read zeros: no coral until it is bought. */
    float    coral_x;
    uint8_t  coral_z1, pad_coral[3];
    uint32_t coral_rgb;
    float    coral_growth;               /* CORAL_START..1 (0 = a save from before it grew: full) */
    /* the reef cluster (2026-09-24): its centre x (0 = the default), its
     * layer + 1, its look, its growth (0 = full). Older saves: no cluster. */
    float    cluster_x;
    uint8_t  cluster_z1, cluster_scheme, pad_cluster[2];
    float    cluster_growth;
    /* the shrimp school (2026-09-29): how many (0 = an older save, or none
     * bought), the pellets eaten toward the next, the seconds before another
     * may join. Positions are not saved: a load puts them in the grass. */
    uint8_t  shrimp_n, shrimp_food, pad_shrimp[2];
    float    shrimp_cool;
    int32_t  shrimp_eaten;               /* pellets eaten, lifetime (its card): in the tail's padding, so still 1664 B */
    /* the release that wrote it (2026-09-29, version.h PT_RELEASE_NUM:
     * 0x000200 = 0.2.0); 0 = a build from before release numbers */
    uint32_t saved_release;
} save_t;
/* the smallest PTK2 save (pre-upkeep, 2026-08-30): anything shorter is not
 * ours. Every later build wrote sizeof(save_t) of its day - 448, 1112, 1304,
 * 1408, 1440, 1456 ... - and load_save takes ANY such length, so a tail only
 * ever appends and never needs its size recorded here. (Until 2026-09-14 the
 * load walked a fixed list of tail offsets; the seen-masks build's 1440 was
 * 1436 padded to the int64's alignment, no offset matched, and the birth-flow
 * flash replaced a live tank with two fry.) */
#define SAVE_CORE_SIZE   offsetof(save_t, veg_growth)
/* the one field ever added MID-struct: bubble_x (2026-09-14) went in ahead of
 * the seen masks, and the browser installer's first builds (public 09-13 ..
 * 09-14) had already written saves without it - 1432 bytes, the masks
 * starting where bubble_x now sits. load_save slides them into place, so a
 * tank kept since the first install updates clean (its badges stay seen and
 * the bubble column stays put). Never add a field mid-struct again. */
#define SAVE_PRE_BUBBLE_SIZE 1432
/* Only the AMOLED's 448 x 368 tank ever wrote that layout: the save carries the
 * algae grid, whose size follows the tank, so a CYD save (320 x 240) is shaped
 * differently from the first byte of the grid on and was never 1432 bytes. */
#define SAVE_HAS_PRE_BUBBLE (TANK_W == 448 && TANK_H == 368)
#if SAVE_HAS_PRE_BUBBLE
_Static_assert(offsetof(save_t, ms_seen) == offsetof(save_t, bubble_x) + sizeof(float),
               "the pre-bubble migration expects the seen masks right after bubble_x");
_Static_assert(offsetof(save_t, bubble_x) + sizeof(((save_t *)0)->ms_seen) + sizeof(uint32_t) == SAVE_PRE_BUBBLE_SIZE,
               "the pre-bubble migration expects the 1432-byte layout's masks to end at 1432");
#endif

/* ---- SAVE LAYOUT LOCK (2026-09-29): a new field goes at the END of save_t
 * with its own assert here; never edit an existing line here. ----
 * Every tank on a keeper's board is read back through these offsets after a
 * browser update, so they are frozen: a failing assert means the change just
 * moved (scrambled) every saved tank - put the field at the end instead. The
 * array bounds are in here too (N_FISH_MAX, VEG_BEDS, VEG_FRONDS_MAX,
 * ALGAE_CELLS, FISH_NAME_MAX): growing one shifts everything after it, so a
 * bigger bound needs a new tail, not a bigger array. The numbers hold on both
 * builds: no pointers or longs, int64_t aligns to 8 and bool is 1 byte on the
 * device's xtensa gcc and on the 64-bit sim alike (both compilers measured
 * the same 1656 bytes, 2026-09-29). The sizes older builds wrote, all still
 * loadable: 448, 1112, 1304, 1408, 1432 (pre-bubble), 1440, 1456, 1480, 1608,
 * 1616, 1624, 1640, 1656 - sim/testdata/saves holds them. */
_Static_assert(SAVE_MAGIC == 0x50544b32u, "SAVE LAYOUT LOCK: a new magic = every saved tank starts fresh");
_Static_assert(sizeof SAVE_NVS_NS == 5 && sizeof SAVE_NVS_KEY == 5, "SAVE LAYOUT LOCK: the NVS namespace / key are \"tank\" / \"save\"");
/* Every offset below is in the AMOLED's numbering. Only the algae grid
 * follows the tank (28 x 23 = 644 cells at 448 x 368; 20 x 15 = 300 on the
 * CYD's 320 x 240), so on another board everything past it sits by the
 * grids' difference: SAVE_OFF moves an offset past the AMOLED's grid end
 * (1104) by exactly that, and the lock holds the same fields frozen on every
 * board. It stays the identity at 448 x 368. The difference has to keep the
 * struct's 8-byte alignment, or the tail would pad differently and no single
 * shift would describe it. */
#define SAVE_AMOLED_ALGAE_CELLS 644
#define SAVE_OFF(off) ((off) < 1104 ? (off) : (off) - SAVE_AMOLED_ALGAE_CELLS + ALGAE_CELLS)
_Static_assert((ALGAE_CELLS - SAVE_AMOLED_ALGAE_CELLS) % 8 == 0,
               "SAVE LAYOUT LOCK: this tank's algae grid moves the tail off its 8-byte alignment");
#define SAVE_AT(f, off) _Static_assert(offsetof(save_t, f) == SAVE_OFF(off), "SAVE LAYOUT LOCK: save_t." #f " moved")
#define FISH_AT(f, off) _Static_assert(offsetof(fish_save_t, f) == (off), "SAVE LAYOUT LOCK: fish_save_t." #f " moved")
FISH_AT(preset, 0); FISH_AT(stage, 1); FISH_AT(pad, 2); FISH_AT(size, 4); FISH_AT(trust, 8);
FISH_AT(bold, 12); FISH_AT(sociable, 16); FISH_AT(bold0, 20); FISH_AT(sociable0, 24);
FISH_AT(hunger, 28); FISH_AT(energy, 32); FISH_AT(stress, 36); FISH_AT(curiosity, 40);
FISH_AT(age_s, 44); FISH_AT(rest_dx, 48); FISH_AT(rest_dy, 52);
FISH_AT(eaten, 56); FISH_AT(eaten_player, 60); FISH_AT(ms_bits, 64);
_Static_assert(sizeof(fish_save_t) == 68, "SAVE LAYOUT LOCK: fish_save_t is frozen (a new per-fish field = a new save_t tail array)");
SAVE_AT(magic, 0); SAVE_AT(saved_unix, 8); SAVE_AT(clock, 16);
SAVE_AT(light_override, 20); SAVE_AT(light_on, 21); SAVE_AT(arrival_pending, 22); SAVE_AT(n_fish, 23);
SAVE_AT(feed_spot_x, 24); SAVE_AT(player_feedings, 28); SAVE_AT(hold_approaches, 32); SAVE_AT(tank_ms_bits, 36);
SAVE_AT(fish, 40);                                             /* 6 x 68 */
SAVE_AT(veg_growth, 448); SAVE_AT(algae, 460); SAVE_AT(trims, 1104); SAVE_AT(cells_cleaned, 1108);   /* upkeep, 08-30 */
SAVE_AT(veg_h, 1112);                                          /* fronds, 09-04 */
SAVE_AT(setup_pending, 1304); SAVE_AT(pad_id, 1305); SAVE_AT(names, 1308);                            /* identity, 09-13 */
SAVE_AT(body, 1356); SAVE_AT(accent, 1380); SAVE_AT(bubble_x, 1404);
SAVE_AT(ms_seen, 1408); SAVE_AT(tank_ms_seen, 1432);           /* seen masks, 09-13 */
SAVE_AT(newborn_p1, 1436); SAVE_AT(parent_p1, 1437); SAVE_AT(pad_fam, 1449);                          /* family, 09-14 */
SAVE_AT(drift_acc, 1452);                                      /* drift, 09-15 */
SAVE_AT(light_idle_s, 1476); SAVE_AT(light_auto, 1478); SAVE_AT(light_manual_off, 1479);              /* light, 09-15 */
SAVE_AT(sd_balance, 1480); SAVE_AT(sd_earned, 1484); SAVE_AT(sd_unlocks, 1488); SAVE_AT(sd_paid_fish, 1492);   /* shop, 09-15 */
SAVE_AT(sd_colonies_paid, 1516); SAVE_AT(sd_inches_paid, 1520); SAVE_AT(algae_colonies, 1524);
SAVE_AT(trim_px, 1528); SAVE_AT(snail_x, 1532); SAVE_AT(snail_y, 1536); SAVE_AT(veg_h3, 1540);
SAVE_AT(plant_x, 1604); SAVE_AT(plant_z1, 1608); SAVE_AT(pad_place, 1609);                            /* placement, 09-16 */
SAVE_AT(snail_grazed, 1612);
SAVE_AT(castle_x, 1616); SAVE_AT(castle_z1, 1620); SAVE_AT(pad_castle, 1621);                         /* castle, 09-16 */
SAVE_AT(coral_x, 1624); SAVE_AT(coral_z1, 1628); SAVE_AT(pad_coral, 1629); SAVE_AT(coral_rgb, 1632);  /* coral, 09-23 */
SAVE_AT(coral_growth, 1636);
SAVE_AT(cluster_x, 1640); SAVE_AT(cluster_z1, 1644); SAVE_AT(cluster_scheme, 1645);                   /* reef cluster, 09-24 */
SAVE_AT(pad_cluster, 1646); SAVE_AT(cluster_growth, 1648);
/* (the next field: SAVE_AT(its_name, 1652 or its type's alignment past it);) */
_Static_assert(sizeof(save_t) >= SAVE_OFF(1656), "SAVE LAYOUT LOCK: save_t only ever grows");
SAVE_AT(shrimp_n, 1652); SAVE_AT(shrimp_food, 1653); SAVE_AT(pad_shrimp, 1654); SAVE_AT(shrimp_cool, 1656);   /* shrimp, 09-29 */
SAVE_AT(shrimp_eaten, 1660);
_Static_assert(sizeof(save_t) >= SAVE_OFF(1664), "SAVE LAYOUT LOCK: save_t only ever grows");
SAVE_AT(saved_release, 1664);                                                                          /* release stamp, 09-29 (0.2.0) */
_Static_assert(sizeof(save_t) >= SAVE_OFF(1672), "SAVE LAYOUT LOCK: save_t only ever grows");
/* (the next field after the release stamp: SAVE_AT(its_name, 1668 or its type's alignment past it);) */
/* NVS budget: the save is one blob in the nvs partition (0x9000, 0x6000 =
 * 6 pages of 4096 B; tools/make_installer.py pins the row). A page is 126
 * entries of 32 B, and NVS keeps one page free for its garbage collection:
 * 5 x 126 = 630 entries usable. A blob costs an entry per 32 B, a header per
 * chunk (a chunk stays on one page) and an index entry, and nvs_set_blob
 * writes the new copy before it erases the old - on top of the director's
 * parked "bk" copy, three at once at worst. At 4000 B (a page's worth) that
 * is 3 x ~128 = ~390 entries, plus ~10 for the settings, "bat"/"hist" and
 * the batlog's "bed": under 2/3 of the 630, so GC always has room. Past it,
 * saves can start failing for space, and any nvs_flash_init error makes
 * main.c ERASE the partition. 1672 B today (the release stamp, 2026-09-29; ~54
 * entries a copy): 2328 B of headroom. */
#define SAVE_NVS_BUDGET 4000
_Static_assert(sizeof(save_t) <= SAVE_NVS_BUDGET, "the save outgrew its NVS budget - see the math above");

float progression_time_scale = 1.0f;

static float s_age[N_FISH_MAX];      /* seconds of tended life per fish */
static uint32_t s_loaded_release;    /* the release that wrote the save this boot loaded (0 = older / none) */
static float s_since_save, s_dirty_since;
static bool  s_dirty;
static bool  s_ravenous;             /* begging/frenzy active until everyone's fed / give-up */
static float s_ravenous_t;           /* seconds spent begging (dash time excluded) */
static int   s_rav_feedings0;        /* player_feedings when the episode began (tank.ravenous_fed) */
static bool  s_arrival_pending;
static float s_spawn_in = -1;        /* seconds until the spawning starts (-1 = not counting) */
static bool  s_prev_night;
static bool  s_booted;
static bool  s_setup_pending;        /* the first-run flow still owed (setup.c) */
static int   s_newborn = -1;         /* the arrival still owed its birth flow (setup.c), or -1 */
static int   s_sd_pending;           /* dollars awarded and not yet shown (the toast) */
static int32_t s_sd_prev_feedings = -1;   /* player_feedings at the last tick (-1 = adopt at the next) */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static void mark_dirty(void) { if (!s_dirty) { s_dirty = true; s_dirty_since = 0; } }

float progression_age_s(const tank_t *t, int idx) { (void)t; return idx >= 0 && idx < N_FISH_MAX ? s_age[idx] : 0; }
bool  progression_arrival_pending(void) { return s_arrival_pending; }
bool  progression_setup_pending(void)   { return s_setup_pending; }
void  progression_setup_done(tank_t *t) { s_setup_pending = false; progression_save(t); }
int   progression_newborn(void)         { return s_newborn; }
uint32_t progression_loaded_release(void) { return s_loaded_release; }
void  progression_newborn_done(tank_t *t) { s_newborn = -1; progression_save(t); }

static void set_ms(fish_t *f, uint32_t bit) { if (!(f->ms_bits & bit)) { f->ms_bits |= bit; mark_dirty(); } }
/* ---- sand dollars ---- */
const sd_item_t SD_ITEMS[SD_ITEM_COUNT] = {
    { SD_ITEM_PLANT, "SWORD PLANT", "BROAD, VERTICAL LEAVES", "MORE COVER FOR YOUR CRITTERS", SD_PRICE_PLANT },   /* Strato's words (2026-09-16); the second line is 28 chars, the shop modal is 352 wide for it */
    { SD_ITEM_SNAIL, "SNAIL",       "GRAZES THE GLASS CLEAN,",   "EVEN WHILE THE TANK SLEEPS",  SD_PRICE_SNAIL },
    { SD_ITEM_CASTLE, "CASTLE",     "STONE TOWERS AND AN ARCH",  "THE FISH SWIM THROUGH IT",    SD_PRICE_CASTLE },   /* 2026-09-16 */
    { SD_ITEM_CORAL,  "CORAL",      "A BRANCHING REEF CORAL,",   "GROWS FOR WEEKS, YOUR COLOR", SD_PRICE_CORAL },    /* 2026-09-23 */
    { SD_ITEM_CLUSTER, "REEF CLUSTER", "A MATURE REEF ON A ROCK,", "FILLS OUT, THEN IT BLOOMS",  SD_PRICE_CLUSTER },  /* 2026-09-24: the dearest; three looks on its page */
    { SD_ITEM_SHRIMP,  "SHRIMP",    "A SCHOOL OF CHERRY SHRIMP", "THEY EAT SCRAPS AND MULTIPLY", SD_PRICE_SHRIMP },  /* 2026-09-29: a resident, like the snail; Strato: "should mention that they multiply" (28 chars, as the plant's) */
};
static void sd_award(tank_t *t, int n) {
    if (n <= 0) return;
    t->sd_balance += n; t->sd_earned += n; s_sd_pending += n;
    mark_dirty();
}
int  progression_sd_take_award(void) { int n = s_sd_pending; s_sd_pending = 0; return n; }
void progression_sd_grant(tank_t *t, int n) {
    if (n >= 0) sd_award(t, n);
    else { t->sd_balance += n; if (t->sd_balance < 0) t->sd_balance = 0; mark_dirty(); }
}
/* the ledger against the tank: anything earned and not yet paid is paid now.
 * Runs every tick, so it is also the back pay for a save from before the
 * shop (paid bits all zero: the stages and the trust it already has, the
 * hundreds its counters already passed - once). MEALS are the exception: a
 * meal pays as it happens, so the count is adopted, not back-paid. */
static void sd_tick(tank_t *t) {
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i]; uint32_t *paid = &t->sd_paid_fish[i];
        if ((f->ms_bits & MS_REACHED_JUV)   && !(*paid & SD_PAID_JUV))   { *paid |= SD_PAID_JUV;   sd_award(t, SD_STAGE_JUV); }
        if ((f->ms_bits & MS_REACHED_ADULT) && !(*paid & SD_PAID_ADULT)) { *paid |= SD_PAID_ADULT; sd_award(t, SD_STAGE_ADULT); }
        if ((f->ms_bits & MS_REACHED_ELDER) && !(*paid & SD_PAID_ELDER)) { *paid |= SD_PAID_ELDER; sd_award(t, SD_STAGE_ELDER); }
        if (f->trust >= 10.0f && !(*paid & SD_PAID_TRUST))               { *paid |= SD_PAID_TRUST; sd_award(t, SD_TRUST); }
    }
    if (s_sd_prev_feedings < 0) s_sd_prev_feedings = t->player_feedings;
    if (t->player_feedings > s_sd_prev_feedings) sd_award(t, SD_MEAL * (t->player_feedings - s_sd_prev_feedings));
    s_sd_prev_feedings = t->player_feedings;
    int32_t hund = t->algae_colonies / SD_CHORE_EVERY;
    if (hund > t->sd_colonies_paid) { sd_award(t, SD_CHORE * (hund - t->sd_colonies_paid)); t->sd_colonies_paid = hund; }
    hund = (int32_t)(t->trim_px / (PX_PER_CM * SD_TRIM_CM));
    if (hund > t->sd_inches_paid) { sd_award(t, SD_CHORE * (hund - t->sd_inches_paid)); t->sd_inches_paid = hund; }
}
int progression_sell_value(int item) { return item < 0 || item >= SD_ITEM_COUNT ? 0 : SD_ITEMS[item].price * SD_SELL_PCT / 100; }
bool progression_sell(tank_t *t, int item) {
    if (item < 0 || item >= SD_ITEM_COUNT || !tank_decor_placeable(item)) return false;   /* the snail stays */
    const sd_item_t *it = &SD_ITEMS[item];
    if (!(t->sd_unlocks & it->bit)) return false;
    t->sd_unlocks &= ~it->bit;
    int back = progression_sell_value(item);
    t->sd_balance += back; s_sd_pending += back;
    tank_decor_reset(t, item);
    mark_dirty(); progression_save(t);
    return true;
}
bool progression_buy(tank_t *t, int item) {
    if (item < 0 || item >= SD_ITEM_COUNT) return false;
    const sd_item_t *it = &SD_ITEMS[item];
    if ((t->sd_unlocks & it->bit) || t->sd_balance < it->price) return false;
    t->sd_balance -= it->price; t->sd_unlocks |= it->bit;
    if (it->bit == SD_ITEM_PLANT) tank_plant_place(t);
    if (it->bit == SD_ITEM_SNAIL) tank_snail_place(t);
    if (it->bit == SD_ITEM_CASTLE) tank_castle_place(t);
    if (it->bit == SD_ITEM_CORAL) tank_coral_place(t);
    if (it->bit == SD_ITEM_CLUSTER) tank_cluster_place(t);
    if (it->bit == SD_ITEM_SHRIMP) { tank_shrimp_place(t, SHRIMP_START); t->shrimp_food = 0; t->shrimp_cool = 0; }
    progression_save(t);                                   /* a purchase sticks at once */
    return true;
}
const char *const *progression_sd_earn_lines(void) {
    static char lines[SD_EARN_LINES][30]; static const char *ptr[SD_EARN_LINES + 1]; static bool made;
    if (!made) {
        /* <= 25 chars each: the modal is 336 px wide at scale 2 */
        snprintf(lines[0], 30, "+%d  EVERY MEAL EATEN", SD_MEAL);
        snprintf(lines[1], 30, "+%d/%d/%d  A FISH GROWS UP", SD_STAGE_JUV, SD_STAGE_ADULT, SD_STAGE_ELDER);
        snprintf(lines[2], 30, "+%d  A NEW FRY IS BORN", SD_BIRTH);
        snprintf(lines[3], 30, "+%d  A FISH FULLY TRUSTS", SD_TRUST);
        snprintf(lines[4], 30, "+%d  %d ALGAE COLONIES", SD_CHORE, SD_CHORE_EVERY);
        snprintf(lines[5], 30, "+%d  %d CM OF GRASS CUT", SD_CHORE, SD_TRIM_CM);
        for (int i = 0; i < SD_EARN_LINES; i++) ptr[i] = lines[i];
        ptr[SD_EARN_LINES] = NULL; made = true;
    }
    return ptr;
}
static void set_tms(tank_t *t, uint32_t bit) { if (!(t->tank_ms_bits & bit)) { t->tank_ms_bits |= bit; mark_dirty(); } }

static void apply_stage(fish_t *f, float age) {
    stage_t st = age >= STAGE_ELDER_AGE ? STAGE_ELDER : age >= STAGE_ADULT_AGE ? STAGE_ADULT
               : age >= STAGE_JUV_AGE ? STAGE_JUV : STAGE_FRY;
    if (st != f->stage) { f->stage = st; mark_dirty(); }         /* silent surprise */
    if (st >= STAGE_JUV)   set_ms(f, MS_REACHED_JUV);
    if (st >= STAGE_ADULT) set_ms(f, MS_REACHED_ADULT);
    if (st >= STAGE_ELDER) set_ms(f, MS_REACHED_ELDER);
}

/* size: base by stage, plus a meal-fed bonus; never shrinks below stage base */
static void apply_growth(fish_t *f) {
    static const float stage_scale[4] = { 0.55f, 0.78f, 1.04f, 1.23f };  /* elder +14% (2026-08-30) */
    float fed = 1.0f + clampf(f->eaten / 60.0f, 0, 1) * 0.18f;
    f->size = f->base_size * stage_scale[f->stage] * fed;
}

static const uint32_t POP_TMS[N_FISH_MAX + 1] = { 0, 0, TMS_PAIR, TMS_TRIO, TMS_QUARTET, TMS_QUINTET, TMS_SEXTET };

/* the parents: the two most trusting grown fish - or, with fewer than two
 * grown (at two fish no stage is gated), the most trusting of the rest fill
 * in, so there is always a pair to court (2026-09-24: the spawning needs
 * two fish in the grass; tank_add_fish already fell back to fish 0 and 1) */
static float parent_rank(const fish_t *f) { return (f->stage >= STAGE_ADULT ? 100.0f : 0.0f) + f->trust; }
static void pick_parents(const tank_t *t, int *pa, int *pb) {
    int a = -1, b = -1;
    for (int i = 0; i < t->n_fish; i++) {
        float r = parent_rank(&t->fish[i]);
        if (a < 0 || r > parent_rank(&t->fish[a])) { b = a; a = i; }
        else if (b < 0 || r > parent_rank(&t->fish[b])) b = i;
    }
    *pa = a; *pb = b;
}

/* the arrival itself: a fry in the nursery grass, traits inherited from the
 * parents; the stage clock starts from zero */
static void do_arrival(tank_t *t) {
    int a, b;
    pick_parents(t, &a, &b);
    int slot = tank_add_fish(t, a, b);
    s_arrival_pending = false;
    s_spawn_in = -1; t->spawning = false; t->spawn_danced = 0;
    if (slot < 0) return;
    int nb = tank_nursery_bed(t);            /* born in the grass it was courted in */
    if (nb >= 0) {
        float x0, x1; tank_veg_bed(t, nb, &x0, &x1, NULL, NULL);
        t->fish[slot].x = (x0 + x1) * 0.5f; t->fish[slot].y = TANK_H - 16 - 18;
    }
    s_age[slot] = 0;
    t->fish[slot].ms_bits = MS_ARRIVED;
    t->sd_paid_fish[slot] = 0;               /* a new ledger for the new fish */
    if (t->n_fish <= N_FISH_MAX) set_tms(t, POP_TMS[t->n_fish]);
    s_newborn = slot;                        /* owed its welcome: the birth flow (setup.c) */
    sd_award(t, SD_BIRTH);
    mark_dirty();
}

/* care gates (docs/progression-next.md, Act 2): never time alone.
 * Counted, not just checked, so the tank can TELL when it's close: one
 * condition shy of an arrival, the parents-to-be start courting - and the
 * milestones page can LIST them (progression_next_fry, 2026-09-14). One
 * table serves both: `have` / `need` are the numbers behind the words. */
typedef struct { int kind; float have, need, frac; bool met; } gate_t;
#define CARE_GATES_MAX 4                 /* the population's three + the glass */
static int care_gates(const tank_t *t, gate_t g[CARE_GATES_MAX]) {
    float min_trust = 10;
    for (int i = 0; i < t->n_fish; i++)
        if (t->fish[i].trust < min_trust) min_trust = t->fish[i].trust;
    int last = t->n_fish - 1;
    float age = last >= 0 ? s_age[last] : 0;
    int n = 0;
#define GATE(k, h, nd, m) do { g[n].kind = (k); g[n].have = (h); g[n].need = (nd); g[n].met = (m); \
        g[n].frac = g[n].met ? 1.0f : (nd) > 0 ? (h) / (nd) : 0; if (g[n].frac > 1) g[n].frac = 1; n++; } while (0)
    switch (t->n_fish) {
    case 2:
        GATE(FRY_REQ_TRUST, min_trust, 6.0f, min_trust >= 6.0f);
        GATE(FRY_REQ_FEED, (float)t->player_feedings, 12, t->player_feedings >= 12);
        GATE(FRY_REQ_HOLD, (float)t->hold_approaches, 1, t->hold_approaches >= 1);
        break;
    case 3:
        /* 2026-09-15 (Strato): MEALS and CHANGE were one "40 meals OR someone
         * drifted 0.11" row - and the parents had always drifted long before,
         * so it was dead weight. Now three rows, and CHANGE is the YOUNGEST
         * fish's accumulated drift pressure (tank.h drift_acc): the same
         * ~26 lit minutes fed and calm, and impossible to soft-lock for a fry
         * born on a trait clamp (inheritance puts them there). */
        GATE(FRY_REQ_GROW, age, (float)STAGE_JUV_AGE, t->fish[last].stage >= STAGE_JUV);
        GATE(FRY_REQ_FEED, (float)t->player_feedings, 40, t->player_feedings >= 40);
        GATE(FRY_REQ_CHANGE, t->fish[last].drift_acc, DRIFT_CHANGE, t->fish[last].drift_acc >= DRIFT_CHANGE);
        break;
    case 4:
        GATE(FRY_REQ_GROW, age, (float)STAGE_ADULT_AGE, t->fish[last].stage >= STAGE_ADULT);
        GATE(FRY_REQ_FEED, (float)t->player_feedings, 80, t->player_feedings >= 80);
        GATE(FRY_REQ_TRUST, min_trust, 7.0f, min_trust >= 7.0f);
        break;
    default:
        GATE(FRY_REQ_GROW, age, (float)STAGE_ADULT_AGE, t->fish[last].stage >= STAGE_ADULT);
        GATE(FRY_REQ_FEED, (float)t->player_feedings, 140, t->player_feedings >= 140);
        GATE(FRY_REQ_TRUST, min_trust, 8.0f, min_trust >= 8.0f);
        break;
    }
    /* and a clean tank (Strato, 2026-09-16: "fish should not be able to
     * breed in a dirty tank. Some algae is OK but if a certain percentage of
     * glass crosses a threshold it will prevent new fries from spawning
     * unless cleaned"): film on no more than ALGAE_DIRTY of the glass. A
     * care gate like the others, so the parents court while only the glass
     * holds them back, and the checklist lists it. The bar reads the other
     * way from the rest - empty at the growth cap, full at the threshold -
     * so it fills as the keeper wipes. Note the staged fry is NOT held by
     * the glass at the light-on that brings it: a night's sleep films ~25%
     * of the glass, so a fry conceived clean would otherwise never land on
     * a morning (do_arrival waits for the nursery only). */
    {
        float cover = tank_algae_cover(t);
        GATE(FRY_REQ_GLASS, cover, ALGAE_DIRTY, cover <= ALGAE_DIRTY);
        if (!g[n - 1].met) {
            float frac = 1.0f - (cover - ALGAE_DIRTY) / ALGAE_DIRTY;
            g[n - 1].frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
        }
    }
#undef GATE
    return n;
}
static void arrival_conditions(const tank_t *t, int *met, int *total) {
    gate_t g[CARE_GATES_MAX];
    *total = care_gates(t, g); *met = 0;
    for (int i = 0; i < *total; i++) *met += g[i].met;
}

/* how each gate is moved - the words behind the HOW? button */
static const char *const TIP_TRUST[]  = { "REST A FINGER ON THE GLASS", "AND KEEP IT STILL. EVERY", "FISH EARNS TRUST WHILE IT", "RESTS THERE. 3 QUICK TAPS", "SCARE THEM AND COST TRUST.", NULL };
static const char *const TIP_FEED[]   = { "TAP THE WATER AT THE TOP", "OF THE TANK TO DROP FOOD.", "A FEEDING COUNTS AS A MEAL", "ONCE A FISH EATS FROM IT.", NULL };
static const char *const TIP_HOLD[]   = { "REST A FINGER ON THE GLASS", "FOR A FEW SECONDS. A FISH", "THAT TRUSTS YOU SWIMS OVER", "AND STAYS. FEED FIRST: A", "HUNGRY FISH WON'T COME.", NULL };
static const char *const TIP_GROW[]   = { "FISH GROW WITH TIME,", "SLOWER WHEN THE TANK IS", "IN SLEEP MODE.", NULL };
static const char *const TIP_CHANGE[] = { "FISH PERSONALITIES WILL", "NATURALLY DRIFT AS THEY", "INTERACT WITH THE WORLD.", NULL };   /* Strato: intentionally vague */
static const char *const TIP_GRASS[]  = { "GRASS REGROWS ON ITS OWN,", "FASTEST WHILE THE TANK", "SLEEPS.", NULL };
static const char *const TIP_GLASS[]  = { "DRAG A FINGER ACROSS THE", "GLASS TO WIPE IT CLEAN.", "UNLOCKABLE CRITTERS CAN", "HELP KEEP IT CLEAN.", NULL };   /* Strato's words */
const char *const *progression_fry_tip(int kind) {
    switch (kind) {
    case FRY_REQ_TRUST:  return TIP_TRUST;
    case FRY_REQ_FEED:   return TIP_FEED;
    case FRY_REQ_HOLD:   return TIP_HOLD;
    case FRY_REQ_GROW:   return TIP_GROW;
    case FRY_REQ_CHANGE: return TIP_CHANGE;
    case FRY_REQ_GLASS:  return TIP_GLASS;
    default:             return TIP_GRASS;
    }
}

/* the checklist, in words. No %f: the device's printf may be the nano one. */
static const char *const STAGE_WORDS[4] = { "A FRY", "A JUVENILE", "AN ADULT", "AN ELDER" };
int progression_next_fry(const tank_t *t, fry_req_t out[FRY_REQ_MAX], bool *staged) {
    if (staged) *staged = s_arrival_pending;
    if (t->n_fish >= POP_CAP || t->n_fish >= N_FISH_MAX || t->n_fish < 2) return 0;
    gate_t g[CARE_GATES_MAX];
    int n = care_gates(t, g);
    for (int i = 0; i < n; i++) {
        fry_req_t *r = &out[i];
        memset(r, 0, sizeof *r);
        r->kind = g[i].kind; r->frac = g[i].frac; r->met = g[i].met;
        int have = (int)g[i].have, need = (int)g[i].need;
        switch (g[i].kind) {
        case FRY_REQ_TRUST:
            snprintf(r->title, sizeof r->title, "TRUST");
            snprintf(r->words, sizeof r->words, "ALL FISH MUST HAVE TRUST");
            snprintf(r->words2, sizeof r->words2, "OF AT LEAST %d OUT OF 10", need);
            if (r->met) snprintf(r->progress, sizeof r->progress, "EVERY FISH DOES");
            else { int tenths = (int)(g[i].have * 10 + 0.5f);
                   snprintf(r->progress, sizeof r->progress, "LOWEST NOW %d.%d", tenths / 10, tenths % 10); }
            break;
        case FRY_REQ_FEED:
            snprintf(r->title, sizeof r->title, "MEALS");
            snprintf(r->words, sizeof r->words, "FEED THE FISH AT LEAST");
            snprintf(r->words2, sizeof r->words2, "%d TIMES IN ALL", need);
            if (r->met) snprintf(r->progress, sizeof r->progress, "DONE");
            else snprintf(r->progress, sizeof r->progress, "%d OF %d SO FAR", have, need);
            break;
        case FRY_REQ_HOLD:
            snprintf(r->title, sizeof r->title, "HOLD");
            snprintf(r->words, sizeof r->words, "REST A FINGER ON THE GLASS");
            snprintf(r->words2, sizeof r->words2, "UNTIL A FISH SWIMS TO IT");
            snprintf(r->progress, sizeof r->progress, r->met ? "DONE" : "NOT YET");
            break;
        case FRY_REQ_GROW: {
            const fish_t *f = &t->fish[t->n_fish - 1];
            snprintf(r->title, sizeof r->title, "GROW");
            snprintf(r->words, sizeof r->words, "THE YOUNGEST FISH MUST");
            snprintf(r->words2, sizeof r->words2, need >= STAGE_ADULT_AGE ? "GROW INTO AN ADULT" : "GROW INTO A JUVENILE");
            snprintf(r->progress, sizeof r->progress, "%s IS %s", f->name, STAGE_WORDS[f->stage & 3]);
            break; }
        case FRY_REQ_CHANGE: {
            const fish_t *f = &t->fish[t->n_fish - 1];
            snprintf(r->title, sizeof r->title, "CHANGE");
            snprintf(r->words, sizeof r->words, "%s'S PERSONALITY", f->name);
            snprintf(r->words2, sizeof r->words2, "MUST START TO SHIFT");
            if (r->met) snprintf(r->progress, sizeof r->progress, "DONE");
            else snprintf(r->progress, sizeof r->progress, "%d%% THERE", (int)(r->frac * 100 + 0.5f));
            break; }
        case FRY_REQ_GLASS: {
            int pct = (int)(g[i].have * 100 + 0.5f), limit = (int)(ALGAE_DIRTY * 100 + 0.5f);
            snprintf(r->title, sizeof r->title, "GLASS");
            snprintf(r->words, sizeof r->words, "NO MORE THAN %d%%", limit);          /* Strato's words, 2026-09-16 */
            snprintf(r->words2, sizeof r->words2, "ALGAE COVERAGE");
            if (r->met) snprintf(r->progress, sizeof r->progress, "CURRENTLY CLEAN ENOUGH");
            else snprintf(r->progress, sizeof r->progress, "%d%% COVERED NOW", pct);
            break; }
        }
    }
    /* and the nursery: every arrival needs grass to be born in */
    {
        fry_req_t *r = &out[n++];
        memset(r, 0, sizeof *r);
        float best = 0;
        for (int b = 0; b < VEG_BEDS; b++) if (t->veg_growth[b] > best) best = t->veg_growth[b];
        r->kind = FRY_REQ_GRASS; r->met = tank_nursery_bed(t) >= 0;
        r->frac = r->met ? 1.0f : best / VEG_NURSERY;
        snprintf(r->title, sizeof r->title, "GRASS");
        snprintf(r->words, sizeof r->words, "ONE GRASS BED MUST GROW");
        snprintf(r->words2, sizeof r->words2, "TALL ENOUGH TO HIDE IN");
        if (r->met) snprintf(r->progress, sizeof r->progress, "A NURSERY BED IS READY");
        else snprintf(r->progress, sizeof r->progress, "TALLEST BED %d%% THERE", (int)(r->frac * 100 + 0.5f));
    }
    return n;
}

static bool arrival_earned(const tank_t *t) {
    if (t->n_fish >= POP_CAP || t->n_fish >= N_FISH_MAX) return false;
    if (tank_nursery_bed(t) < 0) return false;   /* no grass to be born in */
    int met, total;
    arrival_conditions(t, &met, &total);
    return met == total;
}

void progression_force_arrival(tank_t *t) { s_arrival_pending = true; do_arrival(t); }
void progression_woke(tank_t *t) { if (s_arrival_pending) do_arrival(t); }
void progression_stage_arrival(tank_t *t) { (void)t; if (!s_arrival_pending) { s_arrival_pending = true; mark_dirty(); } }

void progression_fresh(tank_t *t) {
    tank_new_population(t);          /* a new tank: two FRY, contrasting -
                                      * the keeper watches them grow up */
    for (int i = 0; i < N_FISH_MAX; i++) s_age[i] = 0;
    t->tank_ms_bits = TMS_PAIR;
    for (int i = 0; i < t->n_fish; i++) { t->fish[i].ms_bits = MS_ARRIVED; apply_growth(&t->fish[i]); }
    s_arrival_pending = false; s_spawn_in = -1; s_prev_night = t->night;
    s_ravenous = false; s_ravenous_t = 0;
    s_setup_pending = true;          /* a new tank: welcome, names, colours */
    s_newborn = -1;
    s_sd_prev_feedings = 0; s_sd_pending = 0;   /* a fresh ledger (tank_init zeroed the tank's) */
    s_booted = true;
    mark_dirty();
}

void progression_reset(tank_t *t, uint32_t seed) {
    persist_port_erase();
    tank_init(t, seed);
    progression_fresh(t);
    progression_save(t);
}

void progression_set_age(tank_t *t, int idx, float seconds) {
    if (idx < 0 || idx >= t->n_fish) return;
    s_age[idx] = seconds < 0 ? 0 : seconds;
    apply_stage(&t->fish[idx], s_age[idx]);
    apply_growth(&t->fish[idx]);
    mark_dirty();
}

/* restore the saved tank into t; false = no usable save (t untouched).
 * *saved_unix gets the save's wall-clock stamp (0 if unknown). */
static bool load_save(tank_t *t, int64_t *saved_unix) {
    save_t sv; memset(&sv, 0, sizeof sv);
    *saved_unix = 0; s_loaded_release = 0;
    /* an older build's shorter save fills a prefix; the zeroed rest reads as
     * every later tail's defaults (see the tail comments in save_t) */
    size_t got = 0;
    bool loaded = persist_port_load(&sv, sizeof sv, &got) && got >= SAVE_CORE_SIZE && got <= sizeof sv;
    if (!loaded || sv.magic != SAVE_MAGIC || sv.n_fish < 2 || sv.n_fish > N_FISH_MAX) return false;
    if (SAVE_HAS_PRE_BUBBLE && got == SAVE_PRE_BUBBLE_SIZE) {   /* the first public installer's layout: see SAVE_PRE_BUBBLE_SIZE */
        memmove(&sv.ms_seen, &sv.bubble_x, sizeof sv.ms_seen + sizeof sv.tank_ms_seen);
        sv.bubble_x = 0;                       /* = the default column */
    }
    *saved_unix = sv.saved_unix;
    s_loaded_release = sv.saved_release;
    t->n_fish = 0;
    for (int i = 0; i < sv.n_fish; i++) {
        const fish_save_t *s = &sv.fish[i];
        tank_make_fish(t, i, s->preset % tank_roster_count(), s->sociable, s->bold, (stage_t)(s->stage & 3));
        fish_t *f = &t->fish[i];
        f->trust = s->trust; f->bold0 = s->bold0; f->sociable0 = s->sociable0;
        f->hunger = s->hunger; f->energy = s->energy; f->stress = s->stress; f->curiosity = s->curiosity;
        f->eaten = s->eaten; f->eaten_player = s->eaten_player;
        f->ms_bits = s->ms_bits & ~MS_RETIRED_MASK;   /* the shadow milestones, gone with it */
        f->ms_seen = sv.ms_seen[i] & f->ms_bits;
        f->rest_dx = s->rest_dx; f->rest_dy = s->rest_dy;
        f->drift_acc = sv.drift_acc[i];
        s_age[i] = s->age_s;
        t->n_fish = i + 1;
        if (sv.names[i][0]) { sv.names[i][FISH_NAME_MAX] = 0; tank_set_name(t, i, sv.names[i]); }
        tank_set_look(t, i, sv.body[i], sv.accent[i]);        /* zeros keep the preset's */
        f->parent_a = (int8_t)(sv.parent_p1[i][0] - 1); f->parent_b = (int8_t)(sv.parent_p1[i][1] - 1);   /* 0 = none = -1 */
        if (f->parent_a >= sv.n_fish) f->parent_a = -1;
        if (f->parent_b >= sv.n_fish) f->parent_b = -1;
    }
    s_setup_pending = sv.setup_pending != 0;
    s_newborn = sv.newborn_p1 && sv.newborn_p1 <= sv.n_fish ? sv.newborn_p1 - 1 : -1;
    if (sv.bubble_x > 0) tank_set_bubble_x(t, sv.bubble_x);
    t->light_idle_s = sv.light_idle_s ? sv.light_idle_s : LIGHT_IDLE_S;
    t->light_auto = sv.light_auto != 0;
    t->light_manual_off = !t->light_auto && sv.light_manual_off != 0;
    t->light_override = false; t->light_on = true;   /* never restored (2026-09-15): a saved
                                                      * override once froze a tank in permanent day */
    t->feed_spot_x = sv.feed_spot_x; t->player_feedings = sv.player_feedings;
    t->hold_approaches = sv.hold_approaches; t->tank_ms_bits = sv.tank_ms_bits;
    t->tank_ms_seen = sv.tank_ms_seen & t->tank_ms_bits;
    for (int b = 0; b < VEG_BEDS; b++) {
        if (sv.veg_h[b][0] > 0)
            for (int i = 0; i < VEG_FRONDS_MAX; i++) t->veg_h[b][i] = sv.veg_h[b][i];
        else if (sv.veg_growth[b] > 0) tank_veg_set(t, b, sv.veg_growth[b]);
    }
    tank_veg_sync(t);
    memcpy(t->algae, sv.algae, ALGAE_CELLS);
    t->trims = sv.trims; t->cells_cleaned = sv.cells_cleaned;
    /* the sand dollar tail (zeros for an older save: the ledger back-pays) */
    t->sd_balance = sv.sd_balance; t->sd_earned = sv.sd_earned; t->sd_unlocks = sv.sd_unlocks & ((1u << SD_ITEM_COUNT) - 1);
    for (int i = 0; i < N_FISH_MAX; i++) t->sd_paid_fish[i] = i < t->n_fish ? sv.sd_paid_fish[i] : 0;
    t->sd_colonies_paid = sv.sd_colonies_paid; t->sd_inches_paid = sv.sd_inches_paid;
    t->algae_colonies = sv.algae_colonies; t->trim_px = sv.trim_px;
    if (sv.snail_x > 0) { t->snail_x = sv.snail_x; t->snail_y = sv.snail_y; }
    t->snail_grazed = sv.snail_grazed;
    if (t->sd_unlocks & SD_ITEM_PLANT) {
        if (sv.veg_h3[0] > 0) for (int i = 0; i < VEG_FRONDS_MAX; i++) t->veg_h[3][i] = sv.veg_h3[i];
        else tank_plant_place(t);
        tank_veg_sync(t);
    }
    if (sv.plant_x > 0) tank_decor_set(t, 0, sv.plant_x, sv.plant_z1 ? sv.plant_z1 - 1 : DECOR_Z_MIDDLE);
    if (sv.castle_x > 0) tank_decor_set(t, 2, sv.castle_x, sv.castle_z1 ? sv.castle_z1 - 1 : DECOR_Z_FRONT);
    if (sv.coral_x > 0) tank_decor_set(t, 3, sv.coral_x, sv.coral_z1 ? sv.coral_z1 - 1 : DECOR_Z_FRONT);   /* (an AMONG save reads as FRONT) */
    if (sv.coral_rgb) tank_coral_set_rgb(t, sv.coral_rgb);
    t->coral_growth = sv.coral_growth > 0 ? sv.coral_growth : 0;   /* 0 = full (tank_coral_growth) */
    if (sv.cluster_x > 0) tank_decor_set(t, 4, sv.cluster_x, sv.cluster_z1 ? sv.cluster_z1 - 1 : DECOR_Z_FRONT);
    tank_cluster_set_scheme(t, sv.cluster_scheme);
    t->cluster_growth = sv.cluster_growth > 0 ? sv.cluster_growth : 0;
    if (t->sd_unlocks & SD_ITEM_SHRIMP) {             /* the school back in the grass, its count and its progress */
        tank_shrimp_place(t, sv.shrimp_n >= SHRIMP_START ? sv.shrimp_n : SHRIMP_START);
        t->shrimp_food = sv.shrimp_food <= SHRIMP_PER_JOIN ? sv.shrimp_food : SHRIMP_PER_JOIN;
        t->shrimp_cool = sv.shrimp_cool > 0 && sv.shrimp_cool <= SHRIMP_COOLDOWN_S ? sv.shrimp_cool : 0;
        t->shrimp_eaten = sv.shrimp_eaten > 0 ? sv.shrimp_eaten : 0;
    } else { t->shrimp_n = 0; t->shrimp_eaten = 0; }
    s_sd_prev_feedings = t->player_feedings;         /* meals before this boot are not back-paid */
    s_sd_pending = 0;
    s_arrival_pending = sv.arrival_pending; s_spawn_in = -1;
    s_prev_night = t->night;
    return true;
}

/* A cold boot lives the absence exactly as a deep-sleep wake does
   (2026-09-16). It used to restore the save and pin everyone at hunger 9.6
   after an hour away - "the one offline rule" - while only the deep-sleep
   wake simulated the span; so the morning after a cell died in the night
   (a PMIC power-off, hence a cold boot) came up with no algae, no growth
   and no night credited. Now every boot with a clock and a save is a wake;
   a long night ends in ravenous begging by itself (hunger +0.8/h crosses
   the begging line in progression_tick). Without a clock nothing can be
   simulated and the tank simply resumes. Returns the hours lived through,
   -1 for none (no save, no clock). */
float progression_boot(tank_t *t) {
    return progression_wake(t, clock_port_now_unix());
}

void progression_settings_changed(void) { mark_dirty(); }

void progression_slept(tank_t *t, float seconds) {
    if (seconds <= 0) return;
    tank_tick_sleep(t, seconds);
    for (int i = 0; i < t->n_fish; i++) {     /* and they grew, slowly, in the dark */
        s_age[i] += seconds * SLEEP_GROWTH_FRAC;
        apply_stage(&t->fish[i], s_age[i]); apply_growth(&t->fish[i]);
    }
    /* the tank milestone (2026-09-15, was "first quiet night" - every fish
       resting under the old day/night cycle): one unbroken stretch of device
       sleep as long as a night. The cap keeps a week's absence a single span. */
    if (seconds >= FULL_NIGHT_S) set_tms(t, TMS_FIRST_FULL_NIGHT);
    mark_dirty();
}

float progression_wake(tank_t *t, int64_t now_unix) {
    int64_t saved_unix;
    s_booted = true;
    if (!load_save(t, &saved_unix)) { progression_fresh(t); return -1; }
    float slept = -1;
    if (now_unix > 0 && saved_unix > 0 && now_unix > saved_unix) {
        int64_t span = now_unix - saved_unix;
        if (span > PROGRESSION_SLEEP_CAP_S) span = PROGRESSION_SLEEP_CAP_S;
        progression_slept(t, (float)span);
        slept = span / 3600.0f;
    }
    progression_woke(t);                     /* a fry staged before the sleep: born at the wake */
    return slept;
}

void progression_tick(tank_t *t, float dt) {
    if (!s_booted) return;
    float aged = dt * progression_time_scale;                    /* growth: every awake second, lit or not */
    float tended = t->night ? 0 : aged;                          /* drift: pressure only while lit and lived-in */
    int n_dart = 0; bool changed_someone = false;
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        s_age[i] += aged;
        apply_stage(f, s_age[i]);
        apply_growth(f);
        /* trait drift, slow: calm + fed -> bolder/more social; startled -> shyer */
        float k = tended / (DRIFT_HOURS * 3600.0f);          /* full unit per DRIFT_HOURS of pressure */
        float pressure = 0;                                   /* what the traits WANTED to move, before the clamps */
        if (f->stress > 7) { f->bold = clampf(f->bold - k, 0.05f, 0.95f); pressure += k; }
        else if (f->hunger < 4 && f->stress < 2) { f->bold = clampf(f->bold + k * 0.5f, 0.05f, 0.95f); pressure += k * 0.5f; }
        if (f->goal.id == GOAL_FOLLOW_FRIEND) { f->sociable = clampf(f->sociable + k * 0.5f, 0.05f, 0.95f); pressure += k * 0.5f; }
        else if (f->goal.id == GOAL_EXPLORE) { f->sociable = clampf(f->sociable - k * 0.2f, 0.05f, 0.95f); pressure += k * 0.2f; }
        f->drift_acc += pressure;
        if (fabsf(f->bold - f->bold0) >= 0.11f || fabsf(f->sociable - f->sociable0) >= 0.11f) changed_someone = true;

        /* milestones: firsts the fish chose to do */
        if (f->goal.id == GOAL_DART_PLAY && f->goal_age > 1.0f) set_ms(f, MS_FIRST_DART);
        if (f->goal.id == GOAL_VISIT_BUBBLES && tank_dist(f->x, f->y, t->bubble_x, t->bubble_y) < 90) set_ms(f, MS_FIRST_BUBBLES);
        if (f->goal.id == GOAL_INSPECT_REEF && tank_dist(f->x, f->y, t->reef_x, t->reef_y) < 90) set_ms(f, MS_FIRST_REEF);
        if (f->goal.id == GOAL_FOLLOW_FRIEND && f->goal_age > 2.0f) set_ms(f, MS_FIRST_FOLLOW);
        if (f->goal.id == GOAL_DART_PLAY) n_dart++;
    }
    if (n_dart >= 2) set_tms(t, TMS_FIRST_PLAY_SESSION);
    if (changed_someone) set_tms(t, TMS_CHANGED_SOMEONE);
    if (t->player_feedings > 0) set_tms(t, TMS_FIRST_FEEDING);
    /* upkeep milestones + event saves (a chore done deserves to stick) */
    static int32_t s_prev_trims, s_prev_cleaned;
    if (t->trims > 0) set_tms(t, TMS_FIRST_TRIM);
    if (t->cells_cleaned >= 30) set_tms(t, TMS_FIRST_CLEANING);
    if (t->trims != s_prev_trims || t->cells_cleaned != s_prev_cleaned) {
        s_prev_trims = t->trims; s_prev_cleaned = t->cells_cleaned;
        mark_dirty();
    }
    sd_tick(t);                                  /* the sand dollars owed for all of the above */

    /* ravenous: a starving tank with empty water begs at the surface (tank.c
     * renders the wait; the trickle holds off so the keeper's pellets are the
     * event). Entered at boot after a long absence, or live whenever everyone
     * is starving - waking from device sleep lands here naturally. If nobody
     * comes, after a while the fish give up and the tank feeds itself. */
    int any_food = 0; for (int i = 0; i < MAX_FOOD; i++) any_food |= t->food[i].alive;
    if (!s_ravenous && !any_food && t->n_fish > 0) {
        float mn = 10;
        for (int i = 0; i < t->n_fish; i++) if (t->fish[i].hunger < mn) mn = t->fish[i].hunger;
        if (mn >= 8.5f) { s_ravenous = true; s_ravenous_t = 0; s_rav_feedings0 = t->player_feedings; }
    }
    if (s_ravenous) {
        if (!any_food) s_ravenous_t += dt;    /* the wait; a dash for live pellets isn't giving up */
        float mx = 0;
        for (int i = 0; i < t->n_fish; i++) if (t->fish[i].hunger > mx) mx = t->fish[i].hunger;
        if (mx < 7.0f) s_ravenous = false;                    /* everyone got a bite */
        else if (s_ravenous_t > RAVENOUS_GIVE_UP_S) {         /* nobody came: back to life */
            s_ravenous = false;
            tank_scatter_food(t, 2);                          /* so it doesn't re-trigger at once */
        }
    }
    /* tank.c picks the presentation: empty water = beg at the surface; live
     * pellets = feeding-frenzy dash (real starving fish DART at fresh food) */
    t->ravenous = s_ravenous;
    t->ravenous_fed = s_ravenous && t->player_feedings != s_rav_feedings0;

    /* the courtship tell: one condition shy of an arrival (or one staged),
     * the parents-to-be pair up - tank.c stages the episodes. The pair is
     * the one do_arrival will name (pick_parents). */
    t->courting = false; t->court_a = t->court_b = -1;
    if (t->n_fish < POP_CAP && t->n_fish < N_FISH_MAX && tank_nursery_bed(t) >= 0) {
        /* ... and only with a nursery: a bed tall enough to hide in. Shave
         * every bed and the courting stops until one regrows. */
        int met, total;
        arrival_conditions(t, &met, &total);
        if (s_arrival_pending || met >= total - 1) {
            int a, b;
            pick_parents(t, &a, &b);
            if (a >= 0 && b >= 0) { t->courting = true; t->court_a = (int8_t)a; t->court_b = (int8_t)b; }
        }
    }

    /* light-on: greet (the light no longer brings the fry) */
    bool light_on_edge = s_prev_night && !t->night;
    if (s_prev_night != t->night) mark_dirty();
    s_prev_night = t->night;
    if (light_on_edge) t->greet_timer = 6.0f;
    if (!s_arrival_pending && arrival_earned(t)) { s_arrival_pending = true; mark_dirty(); }

    /* the spawning (progression.h): a staged fry is born on its own, a
     * little after the last gate closed, while the keeper can see it. The
     * wait counts real awake seconds (not progression_time_scale) and pauses
     * under a page; a dance cut short (a page, the grass shaved) starts
     * over with a fresh wait. */
    if (!s_arrival_pending || !t->courting || t->ui_cover) {
        if (t->spawning) s_spawn_in = -1;
        t->spawning = false; t->spawn_danced = 0;
    } else if (!t->spawning) {
        if (s_spawn_in < 0) s_spawn_in = tank_randf(t, SPAWN_WAIT_MIN_S, SPAWN_WAIT_MAX_S);
        if ((s_spawn_in -= dt) <= 0) { s_spawn_in = -1; t->spawning = true; t->spawn_danced = 0; }
    } else if (t->spawn_danced >= SPAWN_DANCE_S) {
        do_arrival(t);                       /* between them, in the fronds */
        tank_court_puff(t, 3);
    }

    /* saves: coalesced event saves + heartbeat */
    s_since_save += dt; if (s_dirty) s_dirty_since += dt;
    if ((s_dirty && s_since_save >= SAVE_MIN_GAP_S) || s_since_save >= SAVE_HEARTBEAT_S)
        progression_save(t);
}

bool progression_save(tank_t *t) {
    save_t sv; memset(&sv, 0, sizeof sv);
    sv.magic = SAVE_MAGIC; sv.saved_unix = clock_port_now_unix(); sv.clock = t->clock;
    sv.saved_release = PT_RELEASE_NUM;
    /* light_override / light_on stay zero in the save (2026-09-15) */
    sv.light_idle_s = (uint16_t)t->light_idle_s; sv.light_auto = t->light_auto; sv.light_manual_off = t->light_manual_off;
    sv.arrival_pending = s_arrival_pending; sv.n_fish = (uint8_t)t->n_fish;
    sv.feed_spot_x = t->feed_spot_x; sv.player_feedings = t->player_feedings;
    sv.hold_approaches = t->hold_approaches; sv.tank_ms_bits = t->tank_ms_bits;
    sv.tank_ms_seen = t->tank_ms_seen;
    for (int b = 0; b < VEG_BEDS; b++) {
        sv.veg_growth[b] = t->veg_growth[b];
        for (int i = 0; i < VEG_FRONDS_MAX; i++) sv.veg_h[b][i] = t->veg_h[b][i];
    }
    memcpy(sv.algae, t->algae, ALGAE_CELLS);
    sv.trims = t->trims; sv.cells_cleaned = t->cells_cleaned;
    sv.sd_balance = t->sd_balance; sv.sd_earned = t->sd_earned; sv.sd_unlocks = t->sd_unlocks;
    for (int i = 0; i < N_FISH_MAX; i++) sv.sd_paid_fish[i] = t->sd_paid_fish[i];
    sv.sd_colonies_paid = t->sd_colonies_paid; sv.sd_inches_paid = t->sd_inches_paid;
    sv.algae_colonies = t->algae_colonies; sv.trim_px = t->trim_px;
    sv.snail_x = t->snail_x > 0 ? t->snail_x : 0; sv.snail_y = t->snail_y > 0 ? t->snail_y : 0;
    sv.snail_grazed = t->snail_grazed;
    for (int i = 0; i < VEG_FRONDS_MAX; i++) sv.veg_h3[i] = (t->sd_unlocks & SD_ITEM_PLANT) ? t->veg_h[3][i] : 0;
    sv.plant_x = t->plant_x > 0 ? t->plant_x : 0; sv.plant_z1 = (uint8_t)(t->plant_z + 1);
    sv.castle_x = t->castle_x > 0 ? t->castle_x : 0; sv.castle_z1 = (uint8_t)(t->castle_z + 1);
    sv.coral_x = t->coral_x > 0 ? t->coral_x : 0; sv.coral_z1 = (uint8_t)(t->coral_z + 1); sv.coral_rgb = t->coral_rgb;
    sv.coral_growth = t->coral_growth;
    sv.cluster_x = t->cluster_x > 0 ? t->cluster_x : 0; sv.cluster_z1 = (uint8_t)(t->cluster_z + 1); sv.cluster_scheme = t->cluster_scheme;
    sv.cluster_growth = t->cluster_growth;
    sv.shrimp_n = (t->sd_unlocks & SD_ITEM_SHRIMP) ? t->shrimp_n : 0; sv.shrimp_food = t->shrimp_food;
    sv.shrimp_cool = t->shrimp_cool > 0 ? t->shrimp_cool : 0; sv.shrimp_eaten = t->shrimp_eaten;
    sv.setup_pending = s_setup_pending;
    sv.newborn_p1 = (uint8_t)(s_newborn >= 0 && s_newborn < t->n_fish ? s_newborn + 1 : 0);
    sv.bubble_x = t->bubble_x;
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i]; fish_save_t *s = &sv.fish[i];
        if (strcmp(f->name, tank_roster_name(f->preset))) memcpy(sv.names[i], f->name, FISH_NAME_MAX + 1);
        sv.body[i] = f->color; sv.accent[i] = f->accent;      /* the preset's too: harmless, exact */
        sv.parent_p1[i][0] = (uint8_t)(f->parent_a + 1); sv.parent_p1[i][1] = (uint8_t)(f->parent_b + 1);
        s->preset = (uint8_t)f->preset; s->stage = (uint8_t)f->stage;
        s->size = f->size; s->trust = f->trust; s->bold = f->bold; s->sociable = f->sociable;
        s->bold0 = f->bold0; s->sociable0 = f->sociable0;
        s->hunger = f->hunger; s->energy = f->energy; s->stress = f->stress; s->curiosity = f->curiosity;
        s->age_s = s_age[i]; s->rest_dx = f->rest_dx; s->rest_dy = f->rest_dy;
        s->eaten = f->eaten; s->eaten_player = f->eaten_player; s->ms_bits = f->ms_bits;
        sv.ms_seen[i] = f->ms_seen;
        sv.drift_acc[i] = f->drift_acc;
    }
    if (!persist_port_save(&sv, sizeof sv)) {
        if (!s_dirty) { s_dirty = true; s_dirty_since = 0; }
        s_since_save = 0;                         /* retry after SAVE_MIN_GAP_S */
        return false;
    }
    s_since_save = 0; s_dirty = false; s_dirty_since = 0;
    return true;
}

void progression_ack_milestones(tank_t *t) {
    bool changed = false;
    for (int i = 0; i < t->n_fish; i++)
        if (t->fish[i].ms_seen != t->fish[i].ms_bits) { t->fish[i].ms_seen = t->fish[i].ms_bits; changed = true; }
    if (t->tank_ms_seen != t->tank_ms_bits) { t->tank_ms_seen = t->tank_ms_bits; changed = true; }
    if (changed) mark_dirty();
}
