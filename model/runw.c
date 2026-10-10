/* runw.c — llama2.c runner with the Aqua Pets WORD tokenizer.
 *
 * Vendored run.c is #included unchanged (its main renamed); only the prompt
 * encoder differs: whitespace split + vocab lookup (tokenizer.bin stores each
 * lexicon word with a leading space). Output format matches run.c so eval.py
 * and teacher_vs_student.py can parse it.
 *
 *   cc -O3 -o runw runw.c -lm
 *   ./runw out/model.bin -z out/tokenizer.bin -t 0 -i "<state line> ->"
 */
#define main llama2c_main_unused
#include "llama2.c/run.c"
#undef main

#include <string.h>

static int word_id(Tokenizer *t, const char *w, size_t n) {
    for (int i = 3; i < t->vocab_size; i++) {
        const char *v = t->vocab[i];
        if (v[0] == ' ' && strncmp(v + 1, w, n) == 0 && v[1 + n] == '\0') return i;
    }
    return 0; /* <unk> */
}

static int word_encode(Tokenizer *t, const char *text, int *out, int max) {
    int n = 0;
    out[n++] = 1; /* BOS */
    const char *p = text;
    while (*p && n < max) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != ' ') p++;
        out[n++] = word_id(t, s, (size_t)(p - s));
    }
    return n;
}

int main(int argc, char **argv) {
    char *ckpt = NULL, *tokp = "tokenizer.bin", *prompt = "";
    float temperature = 0.0f; int steps = 32;
    if (argc < 2) { fprintf(stderr, "usage: runw model.bin [-z tok.bin] [-t temp] [-n steps] -i prompt\n"); return 1; }
    ckpt = argv[1];
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "-z")) tokp = argv[i + 1];
        else if (!strcmp(argv[i], "-t")) temperature = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "-n")) steps = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-i")) prompt = argv[i + 1];
    }
    Transformer tr; Tokenizer tk; Sampler sp;
    build_transformer(&tr, ckpt);
    build_tokenizer(&tk, tokp, tr.config.vocab_size);
    build_sampler(&sp, tr.config.vocab_size, temperature, 0.9f, 1337);

    int toks[256]; int n = word_encode(&tk, prompt, toks, 200);
    printf("%s", prompt);
    long t0 = time_in_ms(); int token = toks[0], pos = 0, gen = 0;
    while (pos < n + steps && pos < tr.config.seq_len - 1) {
        float *logits = forward(&tr, token, pos);
        int next = (pos < n - 1) ? toks[pos + 1] : sample(&sp, logits);
        pos++;
        if (pos >= n) { if (next == 1) break; printf("%s", decode(&tk, token, next)); gen++; }
        token = next;
    }
    long t1 = time_in_ms();
    printf("\nachieved tok/s: %f  (%d prompt + %d generated tokens in %ld ms)\n",
           pos / ((t1 - t0) / 1000.0), n, gen, t1 - t0);
    return 0;
}
