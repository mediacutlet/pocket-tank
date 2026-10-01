/* orientation.h - the keeper's choice of which way up the screen is, for a
 * board with no IMU to decide it (the 2.8" CYD). The settings page's SCREEN
 * row sets it; it is kept in NVS beside the brightness ("tank"/"flip") and
 * re-saved after a tank reset erases NVS, as the brightness is. main.c turns
 * the display and touch by it, combined with the IMU's own flip where there
 * is one. */
#ifndef ORIENTATION_H
#define ORIENTATION_H
#include <stdbool.h>
void orientation_init(void);          /* after nvs_flash_init */
bool orientation_flipped(void);
void orientation_set(bool flipped);   /* saved */
void orientation_save(void);          /* re-save after an NVS erase (reset) */
/* the face-down gesture's switch (2026-09-30): screen down for 2 s sleeps the
 * tank. On by default; kept beside the flip ("tank"/"facedn"). */
bool orientation_face_sleep(void);
void orientation_set_face_sleep(bool on);   /* saved */
#endif
