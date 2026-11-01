/* advisor_core.h — the platform-agnostic half of the LLM advisor: state
 * encoding (schema v2, and v3 once a v3 tokenizer is loaded), inference with
 * the shipped 4-bit engine, and the distribution layer (sampled goal token,
 * confidence, runner-up). sim/advisor_llm.c and firmware/main/advisor_llm_esp.c
 * only add threading around this. One copy of the encoder = one place for the
 * byte-exact contract with model/gen_traces.py. */
#ifndef ADVISOR_CORE_H
#define ADVISOR_CORE_H
#include "tank.h"
#include "q4_model.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* open model + tokenizer; alloc is the run-state allocator (PSRAM on device).
 * Detects the schema from the tokenizer (a " trust" token = v3). */
bool advisor_core_init(const uint8_t *model_bin, size_t model_len,
                       const uint8_t *tok_bin, size_t tok_len,
                       void *(*alloc)(size_t), uint32_t seed);
int  advisor_core_schema(void);                 /* 2 or 3; 0 if not loaded */
const q4_config_t *advisor_core_config(void);
q4_model_t *advisor_core_model(void);           /* for q4_model_bench */

/* encode fish idx's state line per schema (no trailing " ->") */
void advisor_core_encode(const tank_t *t, int idx, char *out, size_t n);

/* run the model on a state line. Returns the goal (id == GOAL_COUNT if the
 * output was unparseable: keep the previous goal), with confidence and
 * runner_up filled from the goal-token softmax. tokens_out (may be NULL)
 * receives prompt+generated token count for tok/s accounting. */
goal_t advisor_core_infer(const char *state, int *tokens_out);

/* the distribution layer knobs: sampling on (the model's own distribution,
 * T = 1) or greedy. Never a fallback - the model owns the decision either
 * way; sampling just stops collapsing its variety into the mode. */
extern bool  advisor_core_sample;
extern bool advisor_core_two_pass;             /* the goal + " urgency" as one batched pass (default on); off = the old three passes */
extern float advisor_core_temp;
/* last decision's full goal distribution (for debug / narration) */
const float *advisor_core_last_probs(void);
/* true once any decision's odds came back not a number (NaN / inf): a damaged
 * model. Sticky until the next start; the platform raises NOTICE_MODEL_DAMAGED. */
bool advisor_core_damaged(void);
#endif
