/* advisor_core.c — see advisor_core.h. */
#include "advisor_core.h"
#include "word_tok.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static q4_model_t *g_model;
static word_tok_t  g_tok;
static int   g_schema;                /* 2 | 3 | 4 | 5 */
static bool  g_sp_known[SP_COUNT];    /* v5: the species words this vocab has (a later species falls back to `fish`) */
static int   g_goal_ids[GOAL_COUNT];  /* token id of each goal word */
static uint32_t g_rng = 0x9E3779B9u;
static float g_last_p[GOAL_COUNT];
bool  advisor_core_sample = true;
float advisor_core_temp   = 1.0f;

/* ---- schema.md state encoding: must match model/gen_traces.py exactly ---- */
#define SCHEMA_NEAR 70.0f
#define SCHEMA_MID  180.0f
#define SCHEMA_FAR  380.0f

static const char *bucket(float d) {
    if (d < SCHEMA_NEAR) return "near";
    if (d < SCHEMA_MID)  return "mid";
    if (d < SCHEMA_FAR)  return "far";
    return NULL;
}
static int clock_dir(const fish_t *f, float ox, float oy) {
    float rel = atan2f(oy - f->y, ox - f->x) - f->heading;
    while (rel < 0) rel += 6.2831853f;
    int hour = (int)(rel / 6.2831853f * 12.0f + 0.5f) % 12;
    return hour == 0 ? 12 : hour;
}
static void sighting(const fish_t *f, float ox, float oy, bool present, char *out, size_t n) {
    const char *b = present ? bucket(tank_dist(f->x, f->y, ox, oy)) : NULL;
    if (!b) snprintf(out, n, "none");
    else    snprintf(out, n, "%s %d", b, clock_dir(f, ox, oy));
}
static int drive9(float v) { return v < 0 ? 0 : v > 9 ? 9 : (int)v; }

void advisor_core_encode(const tank_t *t, int idx, char *out, size_t n) {
    const fish_t *f = &t->fish[idx];
    int col = (int)(f->x / (TANK_W / 3.0f)); if (col > 2) col = 2;
    int row = (int)(f->y / (TANK_BOT / 2.0f)); if (row > 1) row = 1;

    char food_s[24], shadow_s[24], friend_s[40], bubble_s[24], reef_s[24], wall_s[24], frs[24];
    float fd; int fi = tank_nearest_food(t, f, &fd);
    sighting(f, fi >= 0 ? t->food[fi].x : 0, fi >= 0 ? t->food[fi].y : 0, fi >= 0, food_s, sizeof food_s);
    /* the shadow was removed from the game (2026-09-13); the field stays in
     * the frozen schema and always reads `none` (the model was trained on
     * plenty of shadow-none states) */
    snprintf(shadow_s, sizeof shadow_s, "none");
    int fr = tank_nearest_friend(t, idx, NULL);
    sighting(f, fr >= 0 ? t->fish[fr].x : 0, fr >= 0 ? t->fish[fr].y : 0, fr >= 0, frs, sizeof frs);
    if (strcmp(frs, "none") == 0) snprintf(friend_s, sizeof friend_s, "none");
    else if (g_schema >= 3) snprintf(friend_s, sizeof friend_s, "%s", frs);            /* v3: no names */
    else snprintf(friend_s, sizeof friend_s, "%s %s", t->fish[fr].model_name, frs);
    sighting(f, t->bubble_x, t->bubble_y, true, bubble_s, sizeof bubble_s);
    { float rx, ry; tank_reef_spot(t, &rx, &ry);   /* the cluster once the keeper owns one, else the grass corner's spot */
      sighting(f, rx, ry, true, reef_s, sizeof reef_s); }

    /* wall: nearest side, world-frame contact point, heading-relative clock */
    /* (the glass at the fish's own height and column: the frame's edges in the rectangle, the circle in a bowl) */
    float gx0 = tank_glass_x0(f->y), gx1 = tank_glass_x1(f->y), gy0 = tank_glass_top(f->x);
    float dists[4] = { gx1 - f->x, f->x - gx0, TANK_BOT - f->y, f->y - gy0 };     /* 3,9,6,12 o'clock sides */
    float wxs[4] = { gx1, gx0, f->x, f->x }, wys[4] = { f->y, f->y, TANK_BOT, gy0 };
    int side = 0;
    for (int i = 1; i < 4; i++) if (dists[i] < dists[side]) side = i;
    const char *wb = bucket(dists[side]);
    if (wb && strcmp(wb, "far") != 0)
        snprintf(wall_s, sizeof wall_s, "%s %d", wb, clock_dir(f, wxs[side], wys[side]));
    else snprintf(wall_s, sizeof wall_s, "clear");

    int bold9 = (int)(f->bold * 9.0f + 0.5f), soc9 = (int)(f->sociable * 9.0f + 0.5f);
    if (g_schema >= 5)          /* v5 (docs/species.md): v4 + `species <word>` after stage */
        snprintf(out, n,
            "zone %d hunger %d energy %d stress %d curiosity %d bold %d social %d stage %s species %s trust %d bored %d "
            "food %s friend %s bubble %s reef %s wall %s last %s time %s",
            row * 3 + col + 1, drive9(f->hunger), drive9(f->energy), drive9(f->stress), drive9(f->curiosity),
            bold9, soc9, STAGE_NAMES[f->stage], advisor_core_species_word(f), drive9(f->trust), drive9(f->bored),
            food_s, friend_s, bubble_s, reef_s, wall_s,
            GOAL_NAMES[f->goal.id], t->night ? "night" : "day");
    else if (g_schema >= 4)
        snprintf(out, n,
            "zone %d hunger %d energy %d stress %d curiosity %d bold %d social %d stage %s trust %d bored %d "
            "food %s friend %s bubble %s reef %s wall %s last %s time %s",
            row * 3 + col + 1, drive9(f->hunger), drive9(f->energy), drive9(f->stress), drive9(f->curiosity),
            bold9, soc9, STAGE_NAMES[f->stage], drive9(f->trust), drive9(f->bored),
            food_s, friend_s, bubble_s, reef_s, wall_s,
            GOAL_NAMES[f->goal.id], t->night ? "night" : "day");
    else if (g_schema >= 3)
        snprintf(out, n,
            "zone %d hunger %d energy %d stress %d curiosity %d bold %d social %d stage %s trust %d "
            "food %s shadow %s friend %s bubble %s reef %s wall %s last %s time %s",
            row * 3 + col + 1, drive9(f->hunger), drive9(f->energy), drive9(f->stress), drive9(f->curiosity),
            bold9, soc9, STAGE_NAMES[f->stage], drive9(f->trust),
            food_s, shadow_s, friend_s, bubble_s, reef_s, wall_s,
            GOAL_NAMES[f->goal.id], t->night ? "night" : "day");
    else
        snprintf(out, n,
            "fish %s zone %d hunger %d energy %d stress %d curiosity %d bold %d social %d stage %s "
            "food %s shadow %s friend %s bubble %s reef %s wall %s last %s time %s",
            f->model_name, row * 3 + col + 1,
            drive9(f->hunger), drive9(f->energy), drive9(f->stress), drive9(f->curiosity),
            bold9, soc9, STAGE_NAMES[f->stage],
            food_s, shadow_s, friend_s, bubble_s, reef_s, wall_s,
            GOAL_NAMES[f->goal.id], t->night ? "night" : "day");
}

/* the loaded vocab's schema: v3 has " trust", v4 " bored", v5 " species"
 * (each a superset of the one before); and every goal word's id */
static bool detect_schema(void) {
    bool trust = false, bored = false, species = false;
    for (int i = 0; i < GOAL_COUNT; i++) g_goal_ids[i] = -1;
    for (int s = 0; s < SP_COUNT; s++) g_sp_known[s] = false;
    for (int id = 0; id < g_tok.vocab_size; id++) {
        const char *p = word_tok_piece(&g_tok, id);
        for (int s = 0; s < SP_COUNT; s++)
            if (p[0] == ' ' && strcmp(p + 1, SPECIES[s].token) == 0) g_sp_known[s] = true;
        if (strcmp(p, " trust") == 0) trust = true;
        if (strcmp(p, " bored") == 0) bored = true;      /* v4: no shadow, + bored */
        if (strcmp(p, " species") == 0) species = true;  /* v5: + species <word> */
        for (int g = 0; g < GOAL_COUNT; g++)
            if (p[0] == ' ' && strcmp(p + 1, GOAL_NAMES[g]) == 0) g_goal_ids[g] = id;
    }
    g_schema = species && bored ? 5 : bored ? 4 : trust ? 3 : 2;
    for (int g = 0; g < GOAL_COUNT; g++) if (g_goal_ids[g] < 0) return false;   /* not our vocab */
    return true;
}

bool advisor_core_init(const uint8_t *model_bin, size_t model_len,
                       const uint8_t *tok_bin, size_t tok_len,
                       void *(*alloc)(size_t), uint32_t seed) {
    g_model = q4_model_open(model_bin, model_len, alloc);
    if (!g_model) return false;
    if (word_tok_init(&g_tok, tok_bin, tok_len, q4_model_config(g_model)->vocab_size) != 0) return false;
    if (!detect_schema()) return false;
    if (seed) g_rng = seed;
    return true;
}

bool advisor_core_init_encoder(const uint8_t *tok_bin, size_t tok_len) {
    /* no model to say the vocab size: count the tokenizer's entries */
    size_t off = 4; int n = 0;
    while (off + 8 <= tok_len) {
        int32_t len; memcpy(&len, tok_bin + off + 4, 4);
        if (len < 0 || off + 8 + (size_t)len > tok_len) return false;
        off += 8 + (size_t)len; n++;
    }
    if (n == 0 || off != tok_len) return false;
    g_model = NULL;
    if (word_tok_init(&g_tok, tok_bin, tok_len, n) != 0) return false;
    return detect_schema();
}

/* the species word the loaded vocab can hear: a species added after the
 * model's vocabulary (the jellyfish, 2026-10-09, is id 65 of a 66-word
 * tokenizer; v5m's has 65) is sent as the classic fish - its traits still
 * shape the decision, and the reflex layer still moves it its own way */
const char *advisor_core_species_word(const fish_t *f) {
    int s = f->species < SP_COUNT ? f->species : SP_FISH;
    return g_sp_known[s] ? SPECIES[s].token : SPECIES[SP_FISH].token;
}

int advisor_core_unknown_words(const char *line) {
    if (!g_tok.vocab) return -1;
    int toks[96]; int n = word_tok_encode(&g_tok, line, toks, 96), unk = 0;
    for (int i = 1; i < n; i++) unk += toks[i] == 0;     /* toks[0] is BOS */
    return unk;
}

int advisor_core_schema(void) { return g_tok.vocab ? g_schema : 0; }
q4_model_t *advisor_core_model(void) { return g_model; }
const q4_config_t *advisor_core_config(void) { return g_model ? q4_model_config(g_model) : NULL; }
const float *advisor_core_last_probs(void) { return g_last_p; }

goal_t advisor_core_infer(const char *state, int *tokens_out) {
    goal_t g = { GOAL_COUNT, 5, 1.0f, GOAL_COUNT };   /* GOAL_COUNT = "no change" */
    if (!g_model) return g;
    char prompt[400];
    snprintf(prompt, sizeof prompt, "%s ->", state);
    int toks[64]; int n = word_tok_encode(&g_tok, prompt, toks, 60);
    int out[8];
    float temp = advisor_core_sample ? advisor_core_temp : 0;
    /* max_out 3 = goal + " urgency" + digit: everything the parser reads;
       each extra token would cost a full weight pass */
    int nout = q4_model_decide(g_model, toks, n, g_goal_ids, GOAL_COUNT, temp, &g_rng,
                               g_last_p, out, 3);
    if (tokens_out) *tokens_out = n + nout;
    /* first token is the goal (one of the candidates); then "urgency <d>" */
    if (nout >= 1)
        for (int i = 0; i < GOAL_COUNT; i++) if (out[0] == g_goal_ids[i]) { g.id = (goal_id_t)i; break; }
    if (g.id < GOAL_COUNT) {
        g.confidence = g_last_p[g.id];
        int ru = -1;
        for (int i = 0; i < GOAL_COUNT; i++)
            if (i != (int)g.id && (ru < 0 || g_last_p[i] > g_last_p[ru])) ru = i;
        g.runner_up = ru >= 0 ? (goal_id_t)ru : GOAL_COUNT;
    }
    for (int i = 0; i + 1 < nout; i++) {
        const char *p = word_tok_piece(&g_tok, out[i]);
        if (strcmp(p, " urgency") == 0) {
            const char *d = word_tok_piece(&g_tok, out[i + 1]);
            if (d[0] == ' ' && d[1] >= '0' && d[1] <= '9' && d[2] == 0) g.urgency = (float)(d[1] - '0');
            break;
        }
    }
    return g;
}
