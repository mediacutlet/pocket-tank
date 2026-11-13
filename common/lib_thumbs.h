/* lib_thumbs.h - THE LIBRARY's pictures of the piranha tank and the minnows (0.4.2), baked into the app by
 * tools/gen_lib_thumbs.py: the piranha room takes ~3 s to bake on a board and the school wants its whole scratch, so
 * these two are not made behind the page as the jelly tank's is (lib_pics.h) - they are there on the page's first frame.
 * which: 0 the piranha tank, 1 the minnows; out: w x h of native RGB565 (render_shelf_thumb_size); false = not this
 * board's size (nothing written) */
#ifndef POCKET_TANK_LIB_THUMBS_H
#define POCKET_TANK_LIB_THUMBS_H
#include <stdbool.h>
#include <stdint.h>
enum { LIB_THUMB_PIRANHA, LIB_THUMB_MINNOWS };
bool lib_thumb(int which, uint16_t *out, int w, int h);
#endif
