/* q4_model.h — on-device inference for the Aqua Pets student model.
 *
 * Reads the "version 3" 4-bit export (model/export_q4.py) straight from a
 * memory-mapped flash partition: weights are never copied to RAM. Forward pass
 * follows llama2.c (runq.c): int8 activation groups x 4-bit weight groups,
 * fp16 weight scales, fp32 norms, RoPE, KV cache. Single-threaded reference
 * kernels first; ESP-DSP / dual-core matmul split is an optimization pass.
 *
 * Sizes for the shipped 14.3M model: weights 7.6 MB (flash), KV cache 1.5 MB
 * (PSRAM, seq 64), activations < 64 KB. */
#ifndef Q4_MODEL_H
#define Q4_MODEL_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    int dim, hidden_dim, n_layers, n_heads, n_kv_heads, vocab_size, seq_len;
} q4_config_t;

typedef struct q4_model q4_model_t;

/* bin: pointer to the whole model file (mmap'd partition); returns NULL on a
 * bad header. Allocates run state + KV cache with the given allocator (use
 * heap_caps_malloc(MALLOC_CAP_SPIRAM) on device). */
q4_model_t *q4_model_open(const uint8_t *bin, size_t len, void *(*alloc)(size_t));
const q4_config_t *q4_model_config(const q4_model_t *m);

/* optional fast-memory allocator for the hot activation buffers (the int8
 * quantized activations the dot kernel streams every group). Set BEFORE
 * q4_model_open to place them in internal SRAM on the device, away from
 * PSRAM bus contention with the renderer. NULL = use the main allocator. */
extern void *(*q4_fast_alloc)(size_t);

/* optional inference profiling (mirrors render_clock_us): set a µs clock and
 * the engine fills q4_prof_us per stage — 0 batched matmuls, 1 attention,
 * 2 rmsnorm+quantize, 3 RoPE, 4 swiglu/misc, 5 decode forward() total.
 * Accumulates until the caller zeroes it. NULL = off. */
extern int64_t (*q4_clock_us)(void);
extern int64_t q4_prof_us[6];

/* micro-benchmark for hardware tuning (clock in µs supplied by the caller):
 * 0 = 100k group-dot kernel calls (SRAM), 1 = batched w1 matmul from mapped
 * flash, 2 = same with the weights copied to RAM first, 3 = single-token w1
 * matmul from flash, 4 = same from RAM. Returns elapsed µs (-1 on alloc fail). */
int64_t q4_model_bench(q4_model_t *m, int which, int n_tok, int64_t (*clock_us)(void));
/* run one token at position pos; returns logits[vocab_size] (valid until next call) */
float *q4_model_forward(q4_model_t *m, int token, int pos);
/* batched prefill: run all n prompt tokens with ONE pass over the weights per
 * layer (fills the KV cache for positions 0..n-1); returns logits of the last
 * token, or NULL if n is out of range / no memory */
float *q4_model_prefill(q4_model_t *m, const int *toks, int n);
/* greedy decode: prefill the prompt (batched), then generate until BOS/max;
 * returns count of generated ids written to out */
int q4_model_generate(q4_model_t *m, const int *prompt, int n_prompt, int *out, int max_out);
/* reference token-by-token path (verification / fallback) */
int q4_model_generate_seq(q4_model_t *m, const int *prompt, int n_prompt, int *out, int max_out);
/* decide: prefill the prompt, then pick the FIRST generated token among the
 * n_cand candidate ids (the goal words) by sampling their softmax at `temp`
 * (temp <= 0 or rng == NULL: greedy) with the xorshift32 state *rng; the
 * candidates' normalized probabilities land in probs_out[n_cand] (may be NULL)
 * so the caller knows how confident the decision was and what the runner-up
 * was. The remaining tokens (urgency) are greedy as in q4_model_generate.
 * Costs n_cand exps on top of the prefill - the logits already exist. */
int q4_model_decide(q4_model_t *m, const int *prompt, int n_prompt,
                    const int *cand, int n_cand, float temp, uint32_t *rng,
                    float *probs_out, int *out, int max_out);
#endif
