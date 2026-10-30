/* lang.c - the UI language lookup (lang.h) */
#include "lang.h"
#include <stdlib.h>
#include <string.h>

uint8_t lang_now = LANG_EN, lang_default = LANG_EN;

static const lang_table_t *const TABLES[LANG_N] = { NULL, &LANG_DE_TABLE };
static const char *const CODES[LANG_N] = { "EN", "DE" };

const lang_table_t *lang_table(int lang) { return lang > 0 && lang < LANG_N ? TABLES[lang] : NULL; }
const char *lang_code(int lang) { return CODES[lang > 0 && lang < LANG_N ? lang : 0]; }

static int by_en(const void *key, const void *pair) { return strcmp((const char *)key, ((const lang_pair_t *)pair)->en); }
const char *lang_tr(const char *en) {
    const lang_table_t *t = lang_table(lang_now);
    if (!t || !en || !en[0] || !en[1]) return en;                /* single letters (name wheel) stay */
    const lang_pair_t *p = bsearch(en, t->pairs, (size_t)t->n, sizeof *p, by_en);
#ifdef LANG_AUDIT
    if (!p) lang_audit("MISSING", en);
#endif
    return p ? p->tr : en;
}

#ifdef LANG_AUDIT
#include <stdio.h>
#include <stdbool.h>
#define AUDIT_MAX 512
static char s_audit[AUDIT_MAX][80];
static int  s_audit_n;
void lang_audit(const char *what, const char *s) {
    char line[80]; snprintf(line, sizeof line, "%s: %s", what, s);
    for (int i = 0; i < s_audit_n; i++) if (!strcmp(s_audit[i], line)) return;
    if (s_audit_n < AUDIT_MAX) memcpy(s_audit[s_audit_n++], line, sizeof line);
}
int lang_audit_report(const char *const *names, int n_names) {
    int n = 0;
    for (int i = 0; i < s_audit_n; i++) {
        const char *line = s_audit[i];
        bool made = false;
        if (!strncmp(line, "MISSING: ", 9)) {
            made = strpbrk(line, "0123456789") != NULL;
            for (int k = 0; k < n_names && !made; k++) made = names[k][0] && strstr(line + 9, names[k]);
        }
        if (!made) { printf("lang %s: %s\n", lang_code(lang_now), line); n++; }
    }
    s_audit_n = 0;
    return n;
}
#endif
