/* link_dev.h — the tank link on the director, with ghost fish (link_dev.c). */
#pragma once
#include <stdbool.h>
#include "tank.h"
void link_dev_cmd(tank_t *t, int argc, char **argv);   /* director: link on|off|arm|close|ghost|stay|drift|status */
void link_dev_poll(tank_t *t);                         /* every frame, from the tank task */
void link_dev_status(void);
bool link_dev_on(void);
void link_dev_sleep(void);
/* link mode's page (settings -> LINK, docs/tank-link-0.4.md): it owns the glass while up */
bool link_dev_page_up(void);
void link_dev_page_open(tank_t *t);
void link_dev_page_close(void);
int  link_dev_touch(float x, float y, bool down, float dt);   /* every frame while the page is up: LINK_UI_* (render.h) */
void link_dev_draw(uint16_t *fb, int stride, double clock);    /* the page while up, the DRIFTED notice while drifting */
void link_dev_disconnect(void);                               /* every door of this tank shuts */
bool link_dev_drift_hit(float x, float y);                    /* a tap on the DRIFTED notice's DISCONNECT */                             /* before any sleep or power-off: goodbye, radio down */
