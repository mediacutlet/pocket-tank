/* piranha_spike.h - THE PIRANHA SPIKE (2026-11-08, docs/piranha-tank.md - THROWAWAY, never released)
 *
 * The Discord #ideas thread "Carnivorous fish - Piranha/sharks" (Braavis, with bluehollow's
 * murky water and mangrove roots, abumaia's food from the top) brought to the glass to see how
 * it looks, the stingray spike's way:
 *   - the room (blackwater murk, the surface seen from below, a leaf-litter floor, mangrove prop
 *     roots far back and two in front) baked ONCE into a scene the frame's DMA prefetch copies;
 *     the front roots also kept as spans that go back over the fish;
 *   - a shoal of red-bellied piranhas, each a 3-D mesh (two flanks with a baked skin, the fins as
 *     thin sheets, a hinged lower jaw with teeth), lit per vertex, sorted back to front;
 *   - a tap (or director `piranha feed`) drops a chunk of meat from the top; the shoal goes from
 *     hovering to a frenzy - lunges, bites, head shakes - and the bites leave a blood cloud that
 *     drifts and fades.
 */
#ifndef PIRANHA_SPIKE_H
#define PIRANHA_SPIKE_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct piranha_spike_s piranha_spike_t;

typedef struct {
    float verts_ms, raster_ms, fx_ms;   /* the last frame's stages: bodies + light, triangles, cloud/meat/motes/front roots */
    int tris_drawn, px_drawn, puffs, fish_busy;
} piranha_spike_stats_t;

size_t piranha_spike_size(void);
/* bake the room into scene (w*h, the frame's byte order) */
void piranha_spike_build(piranha_spike_t *p, uint16_t *scene, int w, int h, int n_fish);
/* a drop of meat at screen x (y is ignored: it always comes from the top) */
void piranha_spike_feed(piranha_spike_t *p, float x);
/* the glass: a tap (press then lift without much travel) feeds */
void piranha_spike_touch(piranha_spike_t *p, float x, float y, bool down);
/* drop food on its own every `every_s` seconds (0 = never) */
void piranha_spike_auto(piranha_spike_t *p, float every_s);
/* one frame: fb already holds the scene when primed, else it is copied first */
void piranha_spike_frame(piranha_spike_t *p, uint16_t *fb, int stride, double clock, bool primed);
void piranha_spike_get_stats(const piranha_spike_t *p, piranha_spike_stats_t *st);
void piranha_spike_set_timer(int64_t (*now_us)(void));
void piranha_spike_set_yield(void (*fn)(void));
/* the baked skin (native RGB565) for sheets */
const uint16_t *piranha_spike_skin(const piranha_spike_t *p, int *w, int *h);

#endif
