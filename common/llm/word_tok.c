#include "word_tok.h"
#include <stdlib.h>
#include <string.h>

static int fail(word_tok_t *t, int code) {
    if (t->vocab) {
        for (int i = 0; i < t->vocab_size; i++) free(t->vocab[i]);
        free(t->vocab);
    }
    t->vocab = NULL; t->vocab_size = 0;
    return code;
}

int word_tok_init(word_tok_t *t, const uint8_t *bin, size_t bin_len, int vocab_size) {
    if (!t || !bin || bin_len < 4 || vocab_size <= 0) return -1;
    memset(t, 0, sizeof *t);
    const uint8_t *p = bin, *end = bin + bin_len;
    p += 4;                                   /* max_token_length */
    t->vocab_size = vocab_size;
    t->vocab = calloc(vocab_size, sizeof(char *));
    if (!t->vocab) return -1;
    for (int i = 0; i < vocab_size; i++) {
        if ((size_t)(end - p) < 8) return fail(t, -2);
        p += 4;                               /* score (unused) */
        int32_t len; memcpy(&len, p, 4); p += 4;
        if (len < 0 || (size_t)len > (size_t)(end - p)) return fail(t, -3);
        t->vocab[i] = malloc((size_t)len + 1);
        if (!t->vocab[i]) return fail(t, -1);
        memcpy(t->vocab[i], p, len); t->vocab[i][len] = 0;
        p += len;
    }
    return 0;
}

int word_tok_encode(const word_tok_t *t, const char *text, int *out, int max) {
    int n = 0;
    if (n < max) out[n++] = 1;               /* BOS */
    const char *p = text;
    while (*p && n < max) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *s = p; while (*p && *p != ' ') p++;
        size_t len = (size_t)(p - s); int id = 0;
        for (int i = 3; i < t->vocab_size; i++) {
            const char *v = t->vocab[i];
            if (v[0] == ' ' && strncmp(v + 1, s, len) == 0 && v[1 + len] == '\0') { id = i; break; }
        }
        out[n++] = id;
    }
    return n;
}

const char *word_tok_piece(const word_tok_t *t, int id) {
    return (id >= 0 && id < t->vocab_size) ? t->vocab[id] : "";
}
