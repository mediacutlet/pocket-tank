/* brightness.h - the panel brightness policy (2026-09-11, the battery pass).
 * A user level, 5 .. 100 % in steps of 5 (0.3.9's slider; 100 / 60 / 30
 * before), picked on the settings page (2026-09-15; before that a row at
 * the foot of the milestones page) or `level N` on the director, and kept
 * in NVS
 * ("tank"/"bright"); x0.6 at the tank's night, where the pixels are already
 * dimmed to 45% so the panel can follow unnoticed. Deliberately NO idle dim:
 * people watch the tank without touching it. The director's `bright N`
 * writes the panel directly and holds until the next policy change. */
#ifndef BRIGHTNESS_H
#define BRIGHTNESS_H
#include <stdbool.h>
void brightness_init(void);              /* after nvs_flash_init */
int  brightness_level(void);             /* 5 .. 100 */
bool brightness_set_level(int pct);      /* a step of 5, 5 .. 100; saved */
void brightness_preview(int pct);        /* the same, NOT saved: the slider under the finger */
void brightness_restore(void);           /* a preview undone: the level last saved, nothing written (the BOOT chord's tap) */
void brightness_cycle(void);             /* 100 -> 60 -> 30 -> 100; saved */
void brightness_save(void);              /* re-save after an NVS erase (reset) */
void brightness_apply(bool night);       /* once per frame: pushes a changed target to the panel */
#endif
