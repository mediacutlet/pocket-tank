/* word_tok.h — Aqua Pets word tokenizer (model/train_tokenizer.py), C side.
 * tokenizer.bin: int32 max_len, then vocab_size x { float score, int32 len, bytes }.
 * ids 0..2 are <unk>/BOS/EOS; ids >= 3 are lexicon words stored with a leading space. */
#ifndef WORD_TOK_H
#define WORD_TOK_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    int vocab_size;
    char **vocab;          /* vocab_size strings (leading space on words) */
} word_tok_t;

/* parse tokenizer.bin from memory (copies strings into heap) */
int  word_tok_init(word_tok_t *t, const uint8_t *bin, size_t bin_len, int vocab_size);
/* BOS + one id per whitespace word; returns count */
int  word_tok_encode(const word_tok_t *t, const char *text, int *out, int max);
const char *word_tok_piece(const word_tok_t *t, int id);   /* " seek_food" */
#endif
