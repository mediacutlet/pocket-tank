/* passport.c — a fish packed for the trip next door, and checked on arrival
 * (passport.h has the contract; docs/tank-link-0.4.md the why). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "passport.h"

/* version 1, little-endian. APPEND-ONLY: a new field goes after OFF_MS_SEEN's
 * pair, the CRC moves to the new end, `len` grows - never move these. */
enum {
    OFF_MAGIC = 0, OFF_VERSION = 2, OFF_LEN = 3,
    OFF_UID = 4, OFF_HOME = 8, OFF_PARENT0 = 12, OFF_PARENT1 = 16,
    OFF_NAME = 20,                                     /* FISH_NAME_MAX + 1 = 8 bytes */
    OFF_COLOR = 28, OFF_FIN = 32, OFF_ACCENT = 36,
    OFF_PRESET = 40, OFF_STAGE = 41, OFF_SHAPE = 42, OFF_FINS = 43, OFF_PATTERN = 44,
    OFF_CARRIED = 45,                                  /* PASSPORT_GENES bytes */
    OFF_MUTATED = 50, OFF_FROM_B = 51, OFF_FROM_CARRIED = 52, OFF_PAD = 53,   /* OFF_PAD: the creature's KIND since 0.4.2 (0 = a reef fish; always 0 before) */
    OFF_HUNGER = 54, OFF_ENERGY = 56, OFF_STRESS = 58, OFF_CURIOSITY = 60, OFF_BORED = 62, OFF_TRUST = 64,
    OFF_SOCIABLE = 66, OFF_BOLD = 68, OFF_LAZY = 70, OFF_BOLD0 = 72, OFF_SOCIABLE0 = 74,
    OFF_DRIFT = 76, OFF_SIZE = 78, OFF_AGE = 80, OFF_EATEN = 84, OFF_EATEN_PLAYER = 86,
    OFF_MS_BITS = 88, OFF_MS_SEEN = 90,
    OFF_CRC_V1 = 92,
    /* version 2 (2026-11-01) */
    OFF_HOME_NAME = 92,                                /* PASSPORT_TANK_NAME bytes, zero-padded */
    OFF_CRC_V2 = 108,
    OFF_EVO = 108,                 /* v3: where v2's CRC was; an older reader checks the CRC at its length's end and never reads this */
    OFF_CRC_V3 = 110,
    OFF_STRETCH = 110,             /* v4: where v3's CRC was */
    OFF_CRC_V4 = 120,
    OFF_LAB = 120,                 /* v5 (0.4.3 audit r1, SLB-2): where v4's CRC was - 1 = made in THE FISH LAB */
    OFF_CRC_V5 = 121
};
_Static_assert(OFF_CRC_V2 + 2 == PASSPORT_V2_BYTES, "passport v2 length");
_Static_assert(OFF_CRC_V3 + 2 == PASSPORT_V3_BYTES, "passport v3 length");
_Static_assert(OFF_CRC_V4 + 2 == PASSPORT_V4_BYTES && OFF_STRETCH + PASSPORT_STRETCH == OFF_CRC_V4, "passport v4 length");
_Static_assert(OFF_CRC_V5 + 2 == PASSPORT_V5_BYTES, "passport v5 length");
_Static_assert(FISH_NAME_MAX + 1 == 8, "passport v1 carries an 8-byte name");
_Static_assert(OFF_CRC_V1 + 2 == PASSPORT_V1_BYTES, "passport v1 length");
_Static_assert(MS_FISH_COUNT <= 16, "passport v1 carries 16 badge bits");

#define DRIVE_MAX 10.0f
#define DRIFT_MAX 655.0f
#define SIZE_MAX_ 4.0f
/* the genes' value counts, in GENE_* order (genetics 0.4) */
static const uint8_t GENE_VALUES[PASSPORT_GENES] = { SHAPE_N, FINS_N, PATTERN_N, LOOK_N, LOOK_N };
_Static_assert(PASSPORT_GENES == GENE_N, "the passport carries every gene");
_Static_assert(GENE_SHAPE == 0 && GENE_FINS == 1 && GENE_PATTERN == 2 && GENE_BODY == 3 && GENE_ACCENT == 4, "passport v1's gene order");
_Static_assert(SHAPE_N <= 255 && FINS_N <= 255 && PATTERN_N <= 255 && LOOK_N <= 255, "a gene value is one byte");
#define MS_TRAVELS ((((1u << MS_FISH_COUNT) - 1u) & ~MS_RETIRED_MASK) | MS_CROSSBRED | MS_BOUGHT)

static float clampf_(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static void put16(uint8_t *b, int o, uint32_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *b, int o, uint32_t v) { put16(b, o, v & 0xffff); put16(b, o + 2, v >> 16); }
static uint32_t get16(const uint8_t *b, int o) { return b[o] | (uint32_t)b[o + 1] << 8; }
static uint32_t get32(const uint8_t *b, int o) { return get16(b, o) | get16(b, o + 2) << 16; }
/* a float in 0..max as 0..65535; decode(encode(x)) re-encodes to the same word */
static uint32_t q16(float v, float max) { return (uint32_t)lroundf(clampf_(v, 0, max) / max * 65535.0f); }
static float    dq16(uint32_t q, float max) { return (float)q / 65535.0f * max; }

static uint16_t crc16(const uint8_t *b, int n) {       /* CRC-16/CCITT-FALSE */
    uint16_t c = 0xffff;
    for (int i = 0; i < n; i++) {
        c ^= (uint16_t)b[i] << 8;
        for (int k = 0; k < 8; k++) c = (uint16_t)(c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1);
    }
    return c;
}

/* the name as the tank keeps it: lowercase letters and digits; anything else
 * (another firmware's character, garbage) shows as '?' */
static void clean_name(char *dst, const char *src, int n) {
    int k = 0;
    for (; k < n && src[k]; k++) {
        char c = src[k];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        dst[k] = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ? c : '?';
    }
    dst[k] = 0;
}

void passport_clean_tank_name(char *dst, const char *src) {
    int k = 0;
    for (; k < PASSPORT_TANK_NAME - 1 && src && src[k]; k++) {
        char c = src[k];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c == '\n') c = ' ';
        dst[k] = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(" '.-!?/", c) ? c : '?';
    }
    dst[k] = 0;
    while (k > 0 && dst[k - 1] == ' ') dst[--k] = 0;
}
void passport_home_waters(uint32_t home_tank, char *out, int cap) {
    /* (Strato 11-01: no word that sounds like a gadget - CHANNEL, STREAM, CURRENT, BANK, DOCKS and POINT were swapped out) */
    static const char *const A[64] = { "AMBER","AZURE","CORAL","PEARL","MISTY","SUNNY","QUIET","SILVER","GOLDEN","MOSSY","SANDY","BRIGHT","SALTY","BREEZY","CALM","DEEP",
        "STARRY","LUNAR","MINTY","MAPLE","CEDAR","OPAL","JADE","RUBY","COBALT","INDIGO","VELVET","MERRY","LUCKY","HAPPY","SWIFT","GENTLE","HIDDEN","SLEEPY","DREAMY","COZY",
        "CRISP","FROSTY","HONEY","LEMON","OLIVE","PLUM","TIDAL","PEBBLE","WARM","CLEAR","ROSY","DUSKY","DAWN","NORTH","SOUTH","EAST","WEST","MOON","SUN","STAR","CLOUD",
        "RAIN","SNOW","WIND","SPRING","SUMMER","AUTUMN","WINTER" };
    static const char *const P[64] = { "COVE","REEF","BAY","LAGOON","SHOAL","HOLLOW","HARBOR","INLET","CREEK","BROOK","POOL","POND","RIVER","DELTA","SOUND","KEY","ISLE",
        "ATOLL","TIDEPOOL","BASIN","BEACH","BLUFF","CAPE","CAVE","SHALLOWS","DEPTHS","DUNES","FALLS","FJORD","GARDEN","GROTTO","GULF","HAVEN","LAKE","MARSH","MEADOW","NOOK",
        "OASIS","PIER","COVES","RAPIDS","RIDGE","SANDS","SHORE","STRAIT","GROVE","TIDES","VALLEY","WATERS","WELLS","WHARF","BAYOU","CANAL","EDDY","GLEN","SPRINGS",
        "ISLAND","MOORINGS","LANDING","KELP","WAVES","COAST","SURF","ROCKS" };
    if (cap <= 0) return;
    if (!home_tank) { snprintf(out, (size_t)cap, "FAR WATERS"); return; }
    uint32_t h = home_tank * 0x9e3779b1u; h ^= h >> 15; h *= 0x85ebca77u; h ^= h >> 13;   /* (an ID's low bits alone would pair neighbors) */
    snprintf(out, (size_t)cap, "%s %s", A[h & 63], P[(h >> 6) & 63]);
}

void passport_from_fish(passport_t *p, const fish_t *f, uint32_t home_tank, float age_s) {
    memset(p, 0, sizeof *p);
    p->uid = f->uid; p->home_tank = home_tank;
    p->parent_uid[0] = f->parent_uid[0]; p->parent_uid[1] = f->parent_uid[1];
    clean_name(p->name, f->name, FISH_NAME_MAX);
    p->color = f->color & 0xffffff; p->fin = f->fin & 0xffffff; p->accent = f->accent & 0xffffff;
    p->preset = (uint8_t)f->preset; p->stage = (uint8_t)f->stage;
    p->shape = f->shape; p->fins = f->fins; p->pattern = f->pattern;
    memcpy(p->carried, f->carried, PASSPORT_GENES);
    p->mutated = f->mutated; p->from_b = f->from_b; p->from_carried = f->from_carried;
    p->hunger = f->hunger; p->energy = f->energy; p->stress = f->stress; p->curiosity = f->curiosity;
    p->bored = f->bored; p->trust = f->trust;
    p->sociable = f->sociable; p->bold = f->bold; p->lazy = f->lazy; p->bold0 = f->bold0; p->sociable0 = f->sociable0;
    p->drift_acc = f->drift_acc; p->size = f->size; p->age_s = age_s;
    p->eaten = f->eaten; p->eaten_player = f->eaten_player;
    p->ms_bits = f->ms_bits & MS_TRAVELS; p->ms_seen = f->ms_seen & p->ms_bits;
    p->evo = tank_evo_pack(f);
    p->lab = f->lab ? 1 : 0;
    passport_stretch_from(p->stretch, f);
}
void passport_stretch_from(uint8_t s[PASSPORT_STRETCH], const fish_t *f) {
    memcpy(s, f->body, 2); memcpy(s + 2, f->body_c, 2); memcpy(s + 4, f->finl, 3); memcpy(s + 7, f->finl_c, 3);
}
/* a body: both bytes inside the range, or none. a fin triple: all three set and no longer than the longest, or none */
static void stretch_clean(uint8_t s[PASSPORT_STRETCH]) {
    for (int k = 0; k < 4; k += 2) {
        bool ok = s[k] >= (uint8_t)(BODY_LEN_MIN * BODY_Q) && s[k] <= (uint8_t)(BODY_LEN_MAX * BODY_Q)
               && s[k + 1] >= (uint8_t)(BODY_DEP_MIN * BODY_Q) && s[k + 1] <= (uint8_t)(BODY_DEP_MAX * BODY_Q);
        if (!ok) { s[k] = s[k + 1] = 0; continue; }
        /* (0.4.3 audit r1, SLB-5) and inside the creator's own area, its two trimmed corners: the height gives way, as
           tank_set_body does - a body the creator made comes through to the byte, one from a modified tank is held to it */
        /* (the creator clamps against the length BEFORE it is rounded to a tenth, then rounds both: half a step of slack on
           each, so every body it can store comes through to the byte - the review of 11-22, a 13.54 x 7.534 body) */
        const float h = 0.5f / BODY_Q, len = s[k] / BODY_Q;
        const float lo = BODY_AREA_MIN / (len + h) - h, hi = BODY_AREA_MAX / (len - h) + h;
        float dep = s[k + 1] / BODY_Q;
        if (dep < lo || dep > hi) { dep = dep < lo ? BODY_AREA_MIN / len : BODY_AREA_MAX / len; s[k + 1] = (uint8_t)(dep * BODY_Q + 0.5f); }
    }
    for (int k = 4; k < 10; k += 3) {
        bool ok = s[k] && s[k + 1] && s[k + 2];
        for (int i = 0; i < 3 && ok; i++) ok = s[k + i] <= (uint8_t)(1 + FINL_MAX * FINL_Q);
        if (!ok) s[k] = s[k + 1] = s[k + 2] = 0;
    }
}
void passport_stretch_to(const uint8_t s0[PASSPORT_STRETCH], fish_t *f) {
    uint8_t s[PASSPORT_STRETCH]; memcpy(s, s0, sizeof s); stretch_clean(s);
    memcpy(f->body, s, 2); memcpy(f->body_c, s + 2, 2); memcpy(f->finl, s + 4, 3); memcpy(f->finl_c, s + 7, 3);
    /* the names follow the values, as everywhere: the shown body's, and the carried copy's */
    if (tank_body_set(f)) { float l, d; tank_body(f, &l, &d); f->shape = (uint8_t)tank_body_nearest(l, d); }
    if (f->body_c[0]) f->carried[GENE_SHAPE] = (uint8_t)tank_body_nearest(f->body_c[0] / BODY_Q, f->body_c[1] / BODY_Q);
}

void passport_to_fish(const passport_t *p, fish_t *f) {
    f->uid = p->uid; f->parent_uid[0] = p->parent_uid[0]; f->parent_uid[1] = p->parent_uid[1];
    f->parent_a = f->parent_b = -1;                      /* its parents are not slots of this tank */
    memcpy(f->name, p->name, sizeof f->name); f->name[FISH_NAME_MAX] = 0;
    f->preset = p->preset;
    f->model_name = TRAINED_NAMES[p->preset % N_TRAINED_NAMES];
    f->base_size = tank_roster_size(p->preset); f->turn_rate = tank_roster_turn(p->preset);
    f->size = p->size;
    f->color = p->color; f->fin = p->fin; f->accent = p->accent;
    f->shape = p->shape; f->fins = p->fins; f->pattern = p->pattern;   /* the form's feel is applied as it swims (tank_form_turn) */
    memcpy(f->carried, p->carried, PASSPORT_GENES);
    f->mutated = p->mutated; f->from_b = p->from_b; f->from_carried = p->from_carried;
    f->stage = (stage_t)p->stage;
    f->hunger = p->hunger; f->energy = p->energy; f->stress = p->stress; f->curiosity = p->curiosity;
    f->bored = p->bored; f->trust = p->trust;
    f->sociable = p->sociable; f->bold = p->bold; f->lazy = p->lazy; f->bold0 = p->bold0; f->sociable0 = p->sociable0;
    f->drift_acc = p->drift_acc;
    f->eaten = p->eaten; f->eaten_player = p->eaten_player;
    f->ms_bits = p->ms_bits; f->ms_seen = p->ms_seen;
    tank_evo_unpack(f, p->evo);
    tank_evo_fit(f);                                     /* (below the paragon stage: no part, never READY - SLB-4) */
    f->lab = p->lab ? 1 : 0;
    passport_stretch_to(p->stretch, f);
    f->sig = 0xffffffffu;                                /* the advisor asks about it soon */
}

static int write_as(const passport_t *p, uint8_t *b, int cap, int ver) {
    const int len = ver >= 5 ? PASSPORT_V5_BYTES : ver >= 4 ? PASSPORT_V4_BYTES : PASSPORT_V3_BYTES;
    if (cap < len) return 0;
    memset(b, 0, len);
    put16(b, OFF_MAGIC, PASSPORT_MAGIC); b[OFF_VERSION] = (uint8_t)ver; b[OFF_LEN] = (uint8_t)len;
    put32(b, OFF_UID, p->uid); put32(b, OFF_HOME, p->home_tank);
    put32(b, OFF_PARENT0, p->parent_uid[0]); put32(b, OFF_PARENT1, p->parent_uid[1]);
    clean_name((char *)b + OFF_NAME, p->name, FISH_NAME_MAX);
    put32(b, OFF_COLOR, p->color & 0xffffff); put32(b, OFF_FIN, p->fin & 0xffffff); put32(b, OFF_ACCENT, p->accent & 0xffffff);
    b[OFF_PRESET] = p->preset; b[OFF_STAGE] = p->stage;
    b[OFF_SHAPE] = p->shape; b[OFF_FINS] = p->fins; b[OFF_PATTERN] = p->pattern;
    memcpy(b + OFF_CARRIED, p->carried, PASSPORT_GENES);
    b[OFF_MUTATED] = p->mutated; b[OFF_FROM_B] = p->from_b; b[OFF_FROM_CARRIED] = p->from_carried;
    b[OFF_PAD] = p->kind;                                /* (0.4.2: its kind - 0, a reef fish, as the byte always was) */
    put16(b, OFF_HUNGER, q16(p->hunger, DRIVE_MAX)); put16(b, OFF_ENERGY, q16(p->energy, DRIVE_MAX));
    put16(b, OFF_STRESS, q16(p->stress, DRIVE_MAX)); put16(b, OFF_CURIOSITY, q16(p->curiosity, DRIVE_MAX));
    put16(b, OFF_BORED, q16(p->bored, DRIVE_MAX)); put16(b, OFF_TRUST, q16(p->trust, DRIVE_MAX));
    put16(b, OFF_SOCIABLE, q16(p->sociable, 1)); put16(b, OFF_BOLD, q16(p->bold, 1)); put16(b, OFF_LAZY, q16(p->lazy, 1));
    put16(b, OFF_BOLD0, q16(p->bold0, 1)); put16(b, OFF_SOCIABLE0, q16(p->sociable0, 1));
    put16(b, OFF_DRIFT, q16(p->drift_acc, DRIFT_MAX)); put16(b, OFF_SIZE, q16(p->size, SIZE_MAX_));
    put32(b, OFF_AGE, (uint32_t)lroundf(clampf_(p->age_s, 0, 4.0e9f)));
    put16(b, OFF_EATEN, (uint32_t)(p->eaten < 0 ? 0 : p->eaten > 65535 ? 65535 : p->eaten));
    put16(b, OFF_EATEN_PLAYER, (uint32_t)(p->eaten_player < 0 ? 0 : p->eaten_player > 65535 ? 65535 : p->eaten_player));
    put16(b, OFF_MS_BITS, p->ms_bits & MS_TRAVELS); put16(b, OFF_MS_SEEN, p->ms_seen & p->ms_bits & MS_TRAVELS);
    passport_clean_tank_name((char *)b + OFF_HOME_NAME, p->home_name);
    put16(b, OFF_EVO, p->evo);
    if (ver >= 4) { memcpy(b + OFF_STRETCH, p->stretch, PASSPORT_STRETCH); stretch_clean(b + OFF_STRETCH); }
    if (ver >= 5) b[OFF_LAB] = p->lab ? 1 : 0;
    put16(b, len - 2, crc16(b, len - 2));
    return len;
}
int passport_write(const passport_t *p, uint8_t *b, int cap) { return write_as(p, b, cap, PASSPORT_VERSION); }
int passport_write_v3(const passport_t *p, uint8_t *b, int cap) { return write_as(p, b, cap, 3); }

bool passport_read(passport_t *p, const uint8_t *b, int len) {
    /* what cannot be trusted is refused */
    if (!b || len < PASSPORT_V1_BYTES || len > PASSPORT_MAX_BYTES) return false;
    if (get16(b, OFF_MAGIC) != PASSPORT_MAGIC || b[OFF_VERSION] < 1) return false;
    int n = b[OFF_LEN];
    if (n < PASSPORT_V1_BYTES || n > len) return false;
    if (get16(b, n - 2) != crc16(b, n - 2)) return false;
    memset(p, 0, sizeof *p);
    p->uid = get32(b, OFF_UID); p->home_tank = get32(b, OFF_HOME);
    if (!p->uid || !p->home_tank) return false;
    p->stage = b[OFF_STAGE];
    if (p->stage > STAGE_ELDER) return false;
    /* what can be clamped is clamped */
    p->parent_uid[0] = get32(b, OFF_PARENT0); p->parent_uid[1] = get32(b, OFF_PARENT1);
    char raw[FISH_NAME_MAX + 1];
    memcpy(raw, b + OFF_NAME, FISH_NAME_MAX); raw[FISH_NAME_MAX] = 0;
    clean_name(p->name, raw, FISH_NAME_MAX);
    p->preset = (uint8_t)(b[OFF_PRESET] % tank_roster_count());   /* a later roster's preset: one of ours */
    if (!p->name[0]) clean_name(p->name, tank_roster_name(p->preset), FISH_NAME_MAX);
    p->color = get32(b, OFF_COLOR) & 0xffffff; p->fin = get32(b, OFF_FIN) & 0xffffff; p->accent = get32(b, OFF_ACCENT) & 0xffffff;
    uint8_t g[PASSPORT_GENES] = { b[OFF_SHAPE], b[OFF_FINS], b[OFF_PATTERN], 0, 0 };
    for (int i = 0; i < 3; i++) g[i] = g[i] < GENE_VALUES[i] ? g[i] : 0;   /* an unknown form: the classic one */
    p->shape = g[0]; p->fins = g[1]; p->pattern = g[2];
    for (int i = 0; i < PASSPORT_GENES; i++) {
        uint8_t v = b[OFF_CARRIED + i];
        p->carried[i] = v < GENE_VALUES[i] ? v : 0;
    }
    p->mutated = b[OFF_MUTATED] <= PASSPORT_GENES ? b[OFF_MUTATED] : 0;
    p->from_b = b[OFF_FROM_B] & ((1u << PASSPORT_GENES) - 1);
    p->from_carried = b[OFF_FROM_CARRIED] & ((1u << PASSPORT_GENES) - 1);
    p->kind = b[OFF_PAD];                                /* (0.4.2) read as it comes: nothing is refused by it here */
    p->hunger = dq16(get16(b, OFF_HUNGER), DRIVE_MAX); p->energy = dq16(get16(b, OFF_ENERGY), DRIVE_MAX);
    p->stress = dq16(get16(b, OFF_STRESS), DRIVE_MAX); p->curiosity = dq16(get16(b, OFF_CURIOSITY), DRIVE_MAX);
    p->bored = dq16(get16(b, OFF_BORED), DRIVE_MAX); p->trust = dq16(get16(b, OFF_TRUST), DRIVE_MAX);
    p->sociable = dq16(get16(b, OFF_SOCIABLE), 1); p->bold = dq16(get16(b, OFF_BOLD), 1); p->lazy = dq16(get16(b, OFF_LAZY), 1);
    p->bold0 = dq16(get16(b, OFF_BOLD0), 1); p->sociable0 = dq16(get16(b, OFF_SOCIABLE0), 1);
    p->drift_acc = dq16(get16(b, OFF_DRIFT), DRIFT_MAX);
    p->size = clampf_(dq16(get16(b, OFF_SIZE), SIZE_MAX_), 0.2f, SIZE_MAX_);   /* never an invisible fish */
    p->age_s = (float)get32(b, OFF_AGE);
    p->eaten = (int32_t)get16(b, OFF_EATEN); p->eaten_player = (int32_t)get16(b, OFF_EATEN_PLAYER);
    if (p->eaten_player > p->eaten) p->eaten_player = p->eaten;
    p->ms_bits = get16(b, OFF_MS_BITS) & MS_TRAVELS; p->ms_seen = get16(b, OFF_MS_SEEN) & p->ms_bits;
    if (b[OFF_VERSION] >= 2 && n >= PASSPORT_V2_BYTES) {   /* (v1: no name - its waters stand alone) */
        char raw[PASSPORT_TANK_NAME]; memcpy(raw, b + OFF_HOME_NAME, PASSPORT_TANK_NAME); raw[PASSPORT_TANK_NAME - 1] = 0;
        passport_clean_tank_name(p->home_name, raw);
    }
    if (b[OFF_VERSION] >= 3 && n >= PASSPORT_V3_BYTES) {   /* (v3, 0.4.3) the paragon's word, clamped as a fish would hold it */
        fish_t tmp; tank_evo_unpack(&tmp, (uint16_t)get16(b, OFF_EVO)); p->evo = tank_evo_pack(&tmp);
    }
    if (b[OFF_VERSION] >= 4 && n >= PASSPORT_V4_BYTES) {   /* (v4, 0.4.3) its body and fin values; the names follow them */
        memcpy(p->stretch, b + OFF_STRETCH, PASSPORT_STRETCH); stretch_clean(p->stretch);
        if (p->stretch[0]) p->shape = (uint8_t)tank_body_nearest(p->stretch[0] / BODY_Q, p->stretch[1] / BODY_Q);
        if (p->stretch[2]) p->carried[GENE_SHAPE] = (uint8_t)tank_body_nearest(p->stretch[2] / BODY_Q, p->stretch[3] / BODY_Q);
    }
    if (b[OFF_VERSION] >= 5 && n >= PASSPORT_V5_BYTES) p->lab = b[OFF_LAB] == 1;   /* (anything else: not the lab's) */
    return true;
}
