/* advisor_llm.c — LLM advisor: the trained student model drives the fish.
 * PC-sim version; the firmware (main/advisor_llm_esp.c) is the same shape
 * with a FreeRTOS task instead of a pthread.
 *
 * Architecture (mirrors the ESP32 plan):
 *   - the tank asks need-based (tank_tick): a request marks the fish PENDING;
 *   - on any poll, if the worker is free and this fish is next in line
 *     (urgent fish first, then rotation), its state is encoded NOW - fresh -
 *     and handed to the worker thread;
 *   - the finished goal (sampled from the model's distribution, with
 *     confidence + runner-up) is delivered on that fish's next poll.
 * Never blocks the render loop; garbage output keeps the previous goal.
 *
 * Encoding + inference live in common/llm/advisor_core.c — the exact engine,
 * model file and state line that ship on the device. */
#include "advisor.h"
#include "advisor_core.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static bool        g_loaded = false;
static uint8_t    *g_model_bytes = NULL;

/* one-slot request/response box + pending set */
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv = PTHREAD_COND_INITIALIZER;
static char   g_req_state[400];
static int    g_req_fish = -1;        /* -1: box empty */
static goal_t g_resp_goal;
static int    g_resp_fish = -1;
static bool   g_busy = false;
static bool   g_pending[N_FISH_SLOTS];
static int    g_next = 0;             /* rotation pointer over pending fish */
/* the roster a request was made against (tank_t.roster_shift: the slots as they were): a fish sold
 * shifts the slots, and a decision asked for the old ones is dropped */
static uint8_t g_gen, g_req_gen, g_resp_gen;
bool advisor_llm_narrate = false;     /* --narrate: stream states + decisions */

static void *worker(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&g_mx);
        while (g_req_fish < 0) pthread_cond_wait(&g_cv, &g_mx);
        int fish = g_req_fish; uint8_t gen = g_req_gen;
        char state[400];
        strcpy(state, g_req_state);
        g_req_fish = -1;
        g_busy = true;
        pthread_mutex_unlock(&g_mx);

        goal_t g = advisor_core_infer(state, NULL);

        pthread_mutex_lock(&g_mx);
        g_resp_goal = g;
        g_resp_fish = fish; g_resp_gen = gen;
        g_busy = false;
        pthread_mutex_unlock(&g_mx);
    }
    return NULL;
}

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n); if (b && fread(b, 1, *n, f) != *n) { free(b); b = NULL; }
    fclose(f); return b;
}
static void *host_alloc(size_t n) { return calloc(1, n); }

bool advisor_llm_init(const char *model_path, const char *tok_path) {
    size_t ml, tl;
    g_model_bytes = slurp(model_path, &ml);
    uint8_t *tb = slurp(tok_path, &tl);
    if (!g_model_bytes || !tb) return false;
    if (!advisor_core_init(g_model_bytes, ml, tb, tl, host_alloc, 0x1234567u)) {
        fprintf(stderr, "advisor: bad model/tokenizer (%s)\n", model_path); return false;
    }
    free(tb);
    pthread_t th;
    pthread_create(&th, NULL, worker, NULL);
    g_loaded = true;
    printf("advisor: schema v%d, %s decoding\n", advisor_core_schema(),
           advisor_core_sample ? "sampled" : "greedy");
    return true;
}

/* synchronous single decision with prints: warms the mmap'd weights and shows
 * the exact state string + raw model output (debug/selftest only) */
void advisor_llm_debug(const tank_t *t, int fish_idx) {
    char s[400];
    advisor_core_encode(t, fish_idx, s, sizeof s);
    printf("  state: %s\n", s);
    goal_t g = advisor_core_infer(s, NULL);
    const float *p = advisor_core_last_probs();
    printf("  decision: %s urgency %.0f  (p=%.2f, runner-up %s)\n",
           g.id < GOAL_COUNT ? GOAL_NAMES[g.id] : "<no change>", g.urgency, g.confidence,
           g.runner_up < GOAL_COUNT ? GOAL_NAMES[g.runner_up] : "-");
    printf("  dist:");
    for (int i = 0; i < GOAL_COUNT; i++) printf(" %s=%.2f", GOAL_NAMES[i], p[i]);
    printf("\n");
}

/* who goes next: an urgent pending fish (starving) first,
 * otherwise rotation from g_next */
static int pick_next(const tank_t *t) {
    for (int k = 0; k < N_FISH_SLOTS; k++) {               /* (every swimmer: visitors too; a fish next door is not asked) */
        int i = (g_next + k) % N_FISH_SLOTS;
        if (g_pending[i] && !tank_swims(t, i)) g_pending[i] = false;
        if (!g_pending[i]) continue;
        const fish_t *f = &t->fish[i];
        bool urgent = f->hunger > 8.0f && f->goal.id != GOAL_SEEK_FOOD;
        if (urgent) return i;
    }
    for (int k = 0; k < N_FISH_SLOTS; k++) {
        int i = (g_next + k) % N_FISH_SLOTS;
        if (g_pending[i]) return i;
    }
    return -1;
}

goal_t advisor_llm(const tank_t *t, int fish_idx, bool request) {
    const fish_t *f = &t->fish[fish_idx];
    goal_t out = f->goal;                     /* default: no change yet */
    if (!g_loaded) return out;

    pthread_mutex_lock(&g_mx);
    if (t->roster_shift != g_gen) { g_gen = t->roster_shift;   /* (slots shifted - an arrival shifts none: its decisions stand, R-seam F5) */
        memset(g_pending, 0, sizeof g_pending); if (g_resp_gen != g_gen) g_resp_fish = -1; }
    if (g_resp_fish == fish_idx && g_resp_gen != g_gen) g_resp_fish = -1;   /* asked of the slot's last tenant */
    if (g_resp_fish == fish_idx) {            /* finished decision: deliver on any poll */
        if (g_resp_goal.id < GOAL_COUNT) out = g_resp_goal;
        g_resp_fish = -1;
        if (advisor_llm_narrate)
            printf("  \033[1;33m-> %s decides: %s urgency %.0f (p=%.2f%s%s)\033[0m\n\n",
                   t->fish[fish_idx].name,
                   out.id < GOAL_COUNT ? GOAL_NAMES[out.id] : "?", out.urgency, out.confidence,
                   out.confidence < 0.6f ? ", torn vs " : "",
                   out.confidence < 0.6f && out.runner_up < GOAL_COUNT ? GOAL_NAMES[out.runner_up] : "");
    }
    if (request) g_pending[fish_idx] = true;
    if (!g_busy && g_req_fish < 0 && g_pending[fish_idx] && pick_next(t) == fish_idx) {
        g_pending[fish_idx] = false;
        g_next = (fish_idx + 1) % N_FISH_SLOTS;
        advisor_core_encode(t, fish_idx, g_req_state, sizeof g_req_state);   /* fresh state */
        if (advisor_llm_narrate)
            printf("\033[0;36m%s sees:\033[0m %s\n", t->fish[fish_idx].name, g_req_state);
        g_req_fish = fish_idx; g_req_gen = g_gen;
        pthread_cond_signal(&g_cv);
    }
    pthread_mutex_unlock(&g_mx);
    return out;
}
