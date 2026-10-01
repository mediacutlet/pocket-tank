/* advisor_core.c — see advisor_core.h. */
#include "advisor_core.h"
#include "word_tok.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static q4_model_t *g_model;
static word_tok_t  g_tok;
static int   g_schema;                /* 2 | 3 | 4 */
static int   g_goal_ids[GOAL_COUNT];  /* token id of each goal word */
static uint32_t g_rng = 0x9E3779B9u;
static float g_last_p[GOAL_COUNT];
bool  advisor_core_sample = true;
float advisor_core_temp   = 1.0f;

/* ---- schema.md state encoding: must match model/gen_traces.py exactly ---- */
/* Bands in pixels of the 448-wide tank the model was trained on, scaled to
 * this build's width: a 320-wide tank (the CYD) would otherwise almost never
 * see "far", and the model would be asked about distances it never learned. */
#define SCHEMA_SCALE (TANK_W / 448.0f)
#define SCHEMA_NEAR (70.0f * SCHEMA_SCALE)
#define SCHEMA_MID  (180.0f * SCHEMA_SCALE)
#define SCHEMA_FAR  (380.0f * SCHEMA_SCALE)

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
    int row = (int)(f->y / (TANK_H / 2.0f)); if (row > 1) row = 1;

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
    sighting(f, t->reef_x, t->reef_y, true, reef_s, sizeof reef_s);

    /* wall: nearest side, world-frame contact point, heading-relative clock */
    float dists[4] = { TANK_W - f->x, f->x, TANK_H - f->y, f->y };     /* 3,9,6,12 o'clock sides */
    float wxs[4] = { TANK_W, 0, f->x, f->x }, wys[4] = { f->y, f->y, TANK_H, 0 };
    int side = 0;
    for (int i = 1; i < 4; i++) if (dists[i] < dists[side]) side = i;
    const char *wb = bucket(dists[side]);
    if (wb && strcmp(wb, "far") != 0)
        snprintf(wall_s, sizeof wall_s, "%s %d", wb, clock_dir(f, wxs[side], wys[side]));
    else snprintf(wall_s, sizeof wall_s, "clear");

    int bold9 = (int)(f->bold * 9.0f + 0.5f), soc9 = (int)(f->sociable * 9.0f + 0.5f);
    if (g_schema >= 4)
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

bool advisor_core_init(const uint8_t *model_bin, size_t model_len,
                       const uint8_t *tok_bin, size_t tok_len,
                       void *(*alloc)(size_t), uint32_t seed) {
    g_model = q4_model_open(model_bin, model_len, alloc);
    if (!g_model) return false;
    if (word_tok_init(&g_tok, tok_bin, tok_len, q4_model_config(g_model)->vocab_size) != 0) return false;
    g_schema = 2;
    for (int i = 0; i < GOAL_COUNT; i++) g_goal_ids[i] = -1;
    for (int id = 0; id < g_tok.vocab_size; id++) {
        const char *p = word_tok_piece(&g_tok, id);
        if (strcmp(p, " trust") == 0 && g_schema < 3) g_schema = 3;
        if (strcmp(p, " bored") == 0) g_schema = 4;      /* v4: no shadow, + bored */
        for (int g = 0; g < GOAL_COUNT; g++)
            if (p[0] == ' ' && strcmp(p + 1, GOAL_NAMES[g]) == 0) g_goal_ids[g] = id;
    }
    for (int g = 0; g < GOAL_COUNT; g++) if (g_goal_ids[g] < 0) return false;   /* not our vocab */
    if (seed) g_rng = seed;
    return true;
}
int advisor_core_schema(void) { return g_model ? g_schema : 0; }
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
