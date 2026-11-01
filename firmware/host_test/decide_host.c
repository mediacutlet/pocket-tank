/* decide_host.c — THE TWO-PASS DECISION on the host (2026-10-31): the advisor's decision
 * the old way (prefill + a forward per generated token: three passes over the weights) against
 * the new (prefill + the goal and " urgency" as one batched pass: two), over a grid of schema-4
 * states. Greedy: the goal and the urgency must agree. Sampled with the same seed: the goal
 * must agree (the digit may differ where the old path's second token was not " urgency").
 * Build from firmware/host_test:
 *   cc -O2 -I../../common -I../../common/llm -Wl,-undefined,dynamic_lookup -o decide_host decide_host.c \
 *     ../../common/llm/advisor_core.c ../../common/llm/q4_model.c ../../common/llm/word_tok.c -lm
 * (advisor_core_encode's tank calls are never made here: dynamic_lookup leaves them unresolved)
 *   ./decide_host ../../model/out/model_q4.bin ../../model/out/tokenizer.bin */
#include "advisor_core.h"
#include "word_tok.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
static uint8_t *slurp(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *b = malloc(*n); if (b && fread(b, 1, *n, f) != *n) { free(b); b = NULL; } fclose(f); return b; }
static void *xalloc(size_t n) { return calloc(1, n); }
static double now_ms(void) { struct timeval t; gettimeofday(&t, NULL); return t.tv_sec * 1e3 + t.tv_usec / 1e3; }
/* tank.c's list, defined here: advisor_core_init reads it (tank.c is not linked) */
const char *const GOAL_NAMES[GOAL_COUNT] = { "seek_food", "flee_shadow", "visit_bubbles", "follow_friend", "explore", "rest", "dart_play", "inspect_reef" };
#define GOALS GOAL_NAMES
const char *const STAGE_NAMES[4] = { "fry", "juv", "adult", "elder" };
#define STAGES STAGE_NAMES
static const char *SIGHT[] = { "none", "near 3", "near 11", "mid 6", "mid 1", "far 9", "far 12" };
static const char *WALL[] = { "clear", "near 9", "mid 3", "near 12" };
static uint32_t rs = 12345u;
static int rnd(int n) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (int)(rs % (uint32_t)n); }
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: decide_host model_q4.bin tokenizer.bin [states]\n"); return 1; }
    size_t ml, tl; uint8_t *mb = slurp(argv[1], &ml), *tb = slurp(argv[2], &tl);
    if (!mb || !tb) { fprintf(stderr, "could not read model/tokenizer\n"); return 1; }
    if (!advisor_core_init(mb, ml, tb, tl, xalloc, 0x1234567u)) { fprintf(stderr, "bad model/tokenizer\n"); return 1; }
    int ns = argc > 3 ? atoi(argv[3]) : 200;
    int bad_goal = 0, bad_urg = 0, bad_goal_s = 0, bad_urg_s = 0, urg_default_old = 0;
    double t_old = 0, t_new = 0; int first_shown = 0;
    for (int k = 0; k < ns; k++) {
        char st[400];
        snprintf(st, sizeof st,
            "zone %d hunger %d energy %d stress %d curiosity %d bold %d social %d stage %s trust %d bored %d "
            "food %s friend %s bubble %s reef %s wall %s last %s time %s",
            rnd(6) + 1, rnd(10), rnd(10), rnd(10), rnd(10), rnd(10), rnd(10), STAGES[rnd(4)], rnd(10), rnd(10),
            SIGHT[rnd(7)], SIGHT[rnd(7)], SIGHT[1 + rnd(6)], SIGHT[1 + rnd(6)], WALL[rnd(4)], GOALS[rnd(8)],
            rnd(2) ? "day" : "night");
        /* greedy, both ways */
        advisor_core_sample = false;
        advisor_core_two_pass = false; double a = now_ms(); goal_t go = advisor_core_infer(st, NULL); t_old += now_ms() - a;
        advisor_core_two_pass = true;  a = now_ms(); goal_t gn = advisor_core_infer(st, NULL); t_new += now_ms() - a;
        if (go.id != gn.id) bad_goal++;
        if (go.urgency != gn.urgency) { bad_urg++; if (go.urgency == 5) urg_default_old++;
            if (first_shown++ < 5) printf("  differs: %s\n    old %s %.0f  new %s %.0f\n", st,
                go.id < GOAL_COUNT ? GOALS[go.id] : "?", go.urgency, gn.id < GOAL_COUNT ? GOALS[gn.id] : "?", gn.urgency); }
        /* sampled at T=1 with the same seed, both ways: the pick must agree */
        advisor_core_sample = true; advisor_core_temp = 1.0f;
        advisor_core_init(mb, ml, tb, tl, xalloc, 0x9E3779B9u + (uint32_t)k);
        advisor_core_two_pass = false; goal_t so = advisor_core_infer(st, NULL);
        advisor_core_init(mb, ml, tb, tl, xalloc, 0x9E3779B9u + (uint32_t)k);
        advisor_core_two_pass = true;  goal_t sn = advisor_core_infer(st, NULL);
        if (so.id != sn.id) bad_goal_s++;
        if (so.urgency != sn.urgency) bad_urg_s++;
    }
    printf("two-pass vs three-pass over %d states: greedy goal differs %d, urgency differs %d (old had no digit: %d); "
           "sampled goal differs %d, urgency differs %d\n", ns, bad_goal, bad_urg, urg_default_old, bad_goal_s, bad_urg_s);
    printf("host time per decision: old %.1f ms, new %.1f ms (%.0f%%)\n", t_old / ns, t_new / ns, 100.0 * t_new / t_old);
    return bad_goal || bad_goal_s ? 1 : 0;
}
