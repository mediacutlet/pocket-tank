/* lib_pics.c - THE LIBRARY's theme pictures, made a little each frame (lib_pics.h) */
#include <string.h>
#include "lib_pics.h"
#include "render.h"

static size_t pic_px(void) { int w, h; render_shelf_thumb_size(&w, &h); return (size_t)w * h; }
size_t lib_pics_bytes(void) { return sizeof(lib_pics_t) + pic_px() * sizeof(uint16_t) * JELLY_THEME_N; }
void lib_pics_init(lib_pics_t *p) { p->have = 0; p->cur = -1; p->ticks = 0; p->slices = 0; p->work_us = 0; p->began_us = 0; }
const uint16_t *lib_pics_get(const lib_pics_t *p, int theme) {
    return p && theme >= 0 && theme < JELLY_THEME_N && (p->have >> theme & 1) ? p->pic + (size_t)theme * pic_px() : NULL;
}
unsigned lib_pics_waiting(const lib_pics_t *p, unsigned want) { return p ? want & ~(unsigned)p->have & ((1u << JELLY_THEME_N) - 1) : 0; }
void lib_pics_rest(lib_pics_t *p) { if (p) p->cur = -1; }

int lib_pics_step(lib_pics_t *p, unsigned want, uint16_t *frame, int64_t (*now_us)(void), int budget_us,
                  int *work_ms, int *slices, int *wall_ms) {
    unsigned wait = lib_pics_waiting(p, want);
    if (!wait || !frame) return -1;
    int64_t t0 = now_us();
    if (p->cur < 0 || !(wait >> p->cur & 1)) {             /* the lowest theme the page waits for goes on the stage */
        int k = 0; while (!(wait >> k & 1)) k++;
        p->cur = (int8_t)k; p->ticks = 0; p->slices = 1; p->began_us = t0;
        jelly_theme_start(&p->stage, k);                   /* (the first one builds the engine's scratch - its glow disc is 4225 expf: a frame of its own) */
        p->work_us = (uint32_t)(now_us() - t0);
        return -1;
    }
    int k = p->cur;
    p->slices++;
    if (p->ticks < LIB_PIC_TICKS) {                        /* its 4 s, a few steps a frame */
        do { jelly_tick(&p->stage, LIB_PIC_DT); p->ticks++; } while (p->ticks < LIB_PIC_TICKS && now_us() - t0 < budget_us);
        p->work_us += (uint32_t)(now_us() - t0);
        return -1;
    }
    jelly_fb_primed(NULL);                                 /* the picture: drawn whole, shrunk (a frame of its own) */
    jelly_render(&p->stage, frame, TANK_W);
    jelly_fb_primed(NULL);
    render_shelf_thumb(frame, p->pic + (size_t)k * pic_px());
    int64_t t1 = now_us();
    p->work_us += (uint32_t)(t1 - t0);
    p->have |= (uint8_t)(1u << k); p->cur = -1;
    if (work_ms) *work_ms = (int)(p->work_us / 1000);
    if (slices) *slices = p->slices;
    if (wall_ms) *wall_ms = (int)((t1 - p->began_us) / 1000);
    return k;
}
