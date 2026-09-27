/* q4_host.c — compile the device inference engine on the host and verify the
 * batched path against sequential inference. Build from firmware/host_test:
 *   cc -O2 -I../../common/llm -o q4_host q4_host.c \
 *     ../../common/llm/q4_model.c ../../common/llm/word_tok.c -lm */
#include "q4_model.h"
#include "word_tok.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
static uint8_t *slurp(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *b = malloc(*n); fread(b, 1, *n, f); fclose(f); return b; }
static void *xalloc(size_t n) { return calloc(1, n); }
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: q4_host model_q4.bin tokenizer.bin \"prompt ->\"\n"); return 1; }
    size_t ml, tl; uint8_t *mb = slurp(argv[1], &ml), *tb = slurp(argv[2], &tl);
    if (!mb || !tb) { fprintf(stderr, "could not read model/tokenizer\n"); return 1; }
    q4_model_t *m = q4_model_open(mb, ml, xalloc); if (!m) { fprintf(stderr, "bad model\n"); return 1; }
    word_tok_t tok; if (word_tok_init(&tok, tb, tl, q4_model_config(m)->vocab_size) != 0) { fprintf(stderr, "bad tokenizer\n"); return 1; }
    uint8_t bad[256]; memcpy(bad, mb, sizeof bad); memset(bad + 20, 0, 4); /* n_heads = 0 */
    if (q4_model_open(bad, sizeof bad, xalloc)) { fprintf(stderr, "invalid model header accepted\n"); return 1; }
    int toks[64]; int n = word_tok_encode(&tok, argv[3], toks, 60);
    int out[8], out2[8]; struct timeval a, b, c2; gettimeofday(&a, NULL);
    int nout = q4_model_generate(m, toks, n, out, 6);              /* batched prefill */
    gettimeofday(&b, NULL);
    int nout2 = q4_model_generate_seq(m, toks, n, out2, 6);        /* reference */
    gettimeofday(&c2, NULL);
    printf("%s", argv[3]); for (int i = 0; i < nout; i++) printf("%s", word_tok_piece(&tok, out[i]));
    double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_usec - a.tv_usec) / 1e3;
    double ms2 = (c2.tv_sec - b.tv_sec) * 1e3 + (c2.tv_usec - b.tv_usec) / 1e3;
    int same = nout > 0 && nout == nout2; for (int i = 0; same && i < nout; i++) same = out[i] == out2[i];
    printf("\n[batched: %d prompt + %d gen in %.0f ms | sequential: %.0f ms | %s]\n",
           n, nout, ms, ms2, same ? "IDENTICAL" : "MISMATCH");
    return same ? 0 : 1;
}
