/* prefill_hash.c - the batched prefill's logits, hashed bit for bit (RAM move 2, 0.4.2): run it on two builds of
 * q4_model.c and the lines must match. Token runs of every length 2..63 (past Q4_BATCH_FIRST, so the batch's one
 * growth is covered too), each from a fixed LCG, then a continuation at pos0 = its length (the two-pass decision).
 * Build from firmware/host_test:
 *   cc -O2 -I../../common -I../../common/llm -o prefill_hash prefill_hash.c ../../common/llm/q4_model.c -lm
 *   ./prefill_hash ../../model/out/model_q4.bin */
#include "q4_model.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static uint8_t *slurp(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *b = malloc(*n); if (b && fread(b, 1, *n, f) != *n) { free(b); b = NULL; } fclose(f); return b; }
static void *xalloc(size_t n) { return calloc(1, n); }
static uint64_t fnv(const void *p, size_t n, uint64_t h) { const uint8_t *b = p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; } return h; }
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: prefill_hash model_q4.bin\n"); return 1; }
    size_t ml; uint8_t *mb = slurp(argv[1], &ml);
    q4_model_t *m = mb ? q4_model_open(mb, ml, xalloc) : NULL;
    if (!m) { fprintf(stderr, "bad model\n"); return 1; }
    int vocab = q4_model_config(m)->vocab_size, seq = q4_model_config(m)->seq_len;
    uint64_t all = 0xcbf29ce484222325ULL; uint32_t r = 2026;
    for (int n = 2; n < seq; n++) {
        int toks[128];
        for (int i = 0; i < n; i++) { r = r * 1664525u + 1013904223u; toks[i] = 1 + (int)(r >> 8) % (vocab - 1); }
        int a = n - 1 < 2 ? n - 1 : n - 2 > 0 ? n - 2 : 1, b = n - a;         /* a prefill of a tokens, then b more at pos0 = a */
        const float *lg = q4_model_prefill(m, toks, a);
        uint64_t h = lg ? fnv(lg, (size_t)vocab * 4, 0xcbf29ce484222325ULL) : 0;
        lg = q4_model_prefill_at(m, toks + a, b, a);
        h = lg ? fnv(lg, (size_t)vocab * 4, h) : h ^ 1;
        printf("n %2d: %016llx\n", n, (unsigned long long)h);
        all = fnv(&h, sizeof h, all);
    }
    printf("ALL %016llx\n", (unsigned long long)all);
    return 0;
}
