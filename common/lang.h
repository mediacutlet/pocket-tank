/* lang.h - the UI language (issue #33). Pages stay written in English.
 * draw_text / text_w look each string up in the language's table; no entry
 * shows English. Names are drawn as typed (render_name). A snprintf line is
 * translated by its format: lang_tr("%d MORE PELLETS").
 * The choice is saved (save_t.lang1) and kept out of tank_t, so a reset keeps it.
 * New language: a table like lang_de.c and one line in lang.c.
 * Keeping it current: sim/fishsim --selftest-lang strict lists new or too
 * wide strings; English shows until they are added. */
#ifndef POCKET_TANK_LANG_H
#define POCKET_TANK_LANG_H
#include <stdint.h>

enum { LANG_EN = 0, LANG_DE = 1, LANG_N };

extern uint8_t lang_now;                /* set by the settings chip, kept in the save */
extern uint8_t lang_default;            /* for a save that never chose one: English (sim: --lang) */
const char *lang_tr(const char *en);    /* en in the current language, or en */
const char *lang_code(int lang);        /* "EN", "DE" */

typedef struct { const char *en, *tr; } lang_pair_t;
typedef struct { const lang_pair_t *pairs; int n; } lang_table_t;   /* sorted by en (strcmp) */
extern const lang_table_t LANG_DE_TABLE;
const lang_table_t *lang_table(int lang);                /* NULL for English */

#ifdef LANG_AUDIT                       /* sim only: missing strings, text too wide */
void lang_audit(const char *what, const char *s);
/* prints each finding once, returns the count. MISSING lines with a digit or
 * a fish name come from a translated format and are skipped. */
int  lang_audit_report(const char *const *names, int n_names);
#endif
#endif
