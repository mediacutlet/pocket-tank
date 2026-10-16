/* advisor_llm_esp.c — the LLM advisor on the device. Same shape as
 * sim/advisor_llm.c (pending set + one-slot box + async worker), worker is a
 * FreeRTOS task pinned to core 1; encoding/inference/distribution layer are
 * common/llm/advisor_core.c over the mmap'd flash model. */
#include "advisor_llm_esp.h"
#include "advisor_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "advisor";
static bool g_ok;
static SemaphoreHandle_t g_mx, g_req_sem;
static char g_req_state[400]; static int g_req_fish = -1;
static goal_t g_resp_goal; static int g_resp_fish = -1; static bool g_busy;
static bool g_pending[N_FISH_MAX]; static int g_next;
/* the roster a request was made against (tank_t.roster_gen): a fish sold
 * shifts the slots, and a decision asked for the old ones is dropped */
static uint8_t g_gen, g_req_gen, g_resp_gen;
static uint32_t g_decisions, g_last_ms; static float g_tok_s;

static void *psram_alloc(size_t n) {
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(n);
    return p;
}

/* hot activation buffers live in internal SRAM (~70 KB): the dot kernel then
 * streams only flash weights + SRAM activations, off the contended PSRAM bus */
static void *sram_alloc(size_t n) {
    return heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void worker(void *arg) {
    (void)arg;
    for (;;) {
        xSemaphoreTake(g_req_sem, portMAX_DELAY);
        xSemaphoreTake(g_mx, portMAX_DELAY);
        int fish = g_req_fish; uint8_t gen = g_req_gen; char st[400]; strcpy(st, g_req_state); g_req_fish = -1; g_busy = true;
        xSemaphoreGive(g_mx);
        int ntok = 0; int64_t t0 = esp_timer_get_time();
        memset(q4_prof_us, 0, sizeof q4_prof_us);
        goal_t g = advisor_core_infer(st, &ntok);
        int64_t dt = esp_timer_get_time() - t0;
        g_last_ms = (uint32_t)(dt / 1000); g_tok_s = ntok / (dt / 1e6f);
        ESP_LOGI(TAG, "%s -> %s urgency %.0f p=%.2f  [%lu ms, %.1f tok/s]", st,
                 g.id < GOAL_COUNT ? GOAL_NAMES[g.id] : "?", g.urgency, g.confidence,
                 (unsigned long)g_last_ms, g_tok_s);
        ESP_LOGI(TAG, "prof ms: matmul %lld attn %lld norm/quant %lld rope %lld swiglu %lld decode %lld",
                 q4_prof_us[0] / 1000, q4_prof_us[1] / 1000, q4_prof_us[2] / 1000,
                 q4_prof_us[3] / 1000, q4_prof_us[4] / 1000, q4_prof_us[5] / 1000);
        xSemaphoreTake(g_mx, portMAX_DELAY);
        g_resp_goal = g; g_resp_fish = fish; g_resp_gen = gen; g_busy = false; g_decisions++;
        xSemaphoreGive(g_mx);
    }
}

bool advisor_llm_esp_init(const uint8_t *model_bin, size_t model_len, const uint8_t *tok_bin, size_t tok_len) {
    q4_fast_alloc = sram_alloc;
    if (!advisor_core_init(model_bin, model_len, tok_bin, tok_len, psram_alloc,
                           (uint32_t)esp_timer_get_time() | 1u)) {
        ESP_LOGE(TAG, "bad model/tokenizer"); return false;
    }
    q4_clock_us = esp_timer_get_time;        /* per-stage inference profiling */
    /* the kernels first: an asm dot path that disagrees with the scalar one must
     * be seen before a fish acts on it (PIE on the S3 and the P4; a no-op there
     * is nothing to check). Cheap, and it runs at every boot. */
    { int bad = q4_model_dot_check();
      if (bad == -1) ESP_LOGI(TAG, "kernel check: the PIE dot matches the scalar dot");
      else ESP_LOGE(TAG, "kernel check: %s mismatch in group %d - inference is NOT trustworthy",
                    bad == -2 ? "batch-dot" : "dot", bad < 0 ? 0 : bad); }
    /* one-time kernel/memory bench on boot (numbers for docs/bringup step 9) */
    q4_model_t *bm = advisor_core_model();
    ESP_LOGI(TAG, "bench: dot100k %lld us | batch44 flash %lld us ram %lld us | decode flash %lld us ram %lld us",
             q4_model_bench(bm, 0, 44, esp_timer_get_time),
             q4_model_bench(bm, 1, 44, esp_timer_get_time),
             q4_model_bench(bm, 2, 44, esp_timer_get_time),
             q4_model_bench(bm, 3, 44, esp_timer_get_time),
             q4_model_bench(bm, 4, 44, esp_timer_get_time));
    g_mx = xSemaphoreCreateMutex(); g_req_sem = xSemaphoreCreateBinary();
    /* inference on core 1; render/tank own core 0 */
    xTaskCreatePinnedToCore(worker, "advisor", 16384, NULL, 5, NULL, 1);
    const q4_config_t *c = advisor_core_config();
    ESP_LOGI(TAG, "model dim %d layers %d vocab %d seq %d, schema v%d, %s decoding",
             c->dim, c->n_layers, c->vocab_size, c->seq_len, advisor_core_schema(),
             advisor_core_sample ? "sampled" : "greedy");
    g_ok = true;
    return true;
}

static int pick_next(const tank_t *t) {
    for (int k = 0; k < t->n_fish; k++) {
        int i = (g_next + k) % t->n_fish;
        if (!g_pending[i]) continue;
        const fish_t *f = &t->fish[i];
        if (f->hunger > 8.0f && f->goal.id != GOAL_SEEK_FOOD) return i;
    }
    for (int k = 0; k < t->n_fish; k++) {
        int i = (g_next + k) % t->n_fish;
        if (g_pending[i]) return i;
    }
    return -1;
}

goal_t advisor_llm_esp(const tank_t *t, int fish_idx, bool request) {
    const fish_t *f = &t->fish[fish_idx];
    goal_t out = f->goal;
    if (!g_ok) return out;
    xSemaphoreTake(g_mx, portMAX_DELAY);
    if (t->roster_gen != g_gen) { g_gen = t->roster_gen; memset(g_pending, 0, sizeof g_pending); if (g_resp_gen != g_gen) g_resp_fish = -1; }
    if (g_resp_fish == fish_idx && g_resp_gen != g_gen) g_resp_fish = -1;   /* asked of the slot's last tenant */
    if (g_resp_fish == fish_idx) {
        if (g_resp_goal.id < GOAL_COUNT) out = g_resp_goal;
        g_resp_fish = -1;
    }
    if (request) g_pending[fish_idx] = true;
    if (!g_busy && g_req_fish < 0 && g_pending[fish_idx] && pick_next(t) == fish_idx) {
        g_pending[fish_idx] = false;
        g_next = (fish_idx + 1) % (t->n_fish > 0 ? t->n_fish : 1);
        advisor_core_encode(t, fish_idx, g_req_state, sizeof g_req_state);
        g_req_fish = fish_idx; g_req_gen = g_gen;
        xSemaphoreGive(g_req_sem);
    }
    xSemaphoreGive(g_mx);
    return out;
}

void advisor_llm_esp_stats(uint32_t *decisions, uint32_t *last_ms, float *tok_s) {
    *decisions = g_decisions; *last_ms = g_last_ms; *tok_s = g_tok_s;
}
