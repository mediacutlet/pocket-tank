/* advisor_llm_esp.h — the LLM advisor on the device: same mailbox shape as
 * sim/advisor_llm.c, but the worker is a FreeRTOS task pinned to core 1 and
 * the model is q4_model over the mmap'd flash partition. */
#ifndef ADVISOR_LLM_ESP_H
#define ADVISOR_LLM_ESP_H
#include "tank.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

bool   advisor_llm_esp_init(const uint8_t *model_bin, size_t model_len,
                            const uint8_t *tok_bin, size_t tok_len);
goal_t advisor_llm_esp(const tank_t *t, int fish_idx, bool request);
/* stats for the log: decisions made, last decision latency (ms), tokens/s */
void   advisor_llm_esp_stats(uint32_t *decisions, uint32_t *last_ms, float *tok_s);
#define ADVISOR_STACK 8192      /* the worker's stack (director `stacks`: 2.9 KB used on the square, 11-02; 16 KB until 0.4) */
size_t advisor_llm_esp_stack_unused(void);
bool   advisor_llm_esp_busy(uint32_t *decisions);   /* a decision running now; *decisions = how many have finished (it moves at each end) */   /* bytes of it never used (0: no worker) */
#endif
