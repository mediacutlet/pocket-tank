/* lang.h - the keeper's language (2026-10-04): English, or Spanish.
 * Every on-screen string is written as TR("ENGLISH", "ESPAÑOL") and picks
 * its side at the moment it is drawn, so a change in settings lands on the
 * next frame. The choice is the tank's (tank_t.lang), kept in the save and
 * through a reset; pt_lang mirrors it for code that holds no tank. A table
 * of strings is two tables, chosen the same way. English is the default:
 * a save from before the setting (its byte was padding, zero) reads English. */
#ifndef POCKET_TANK_LANG_H
#define POCKET_TANK_LANG_H
#include <stdint.h>
enum { LANG_EN = 0, LANG_ES = 1, LANG_N };
extern uint8_t pt_lang;
#define TR(en, es) (pt_lang == LANG_ES ? (es) : (en))
#endif
