/* p4_tiles.c - ONLY WHAT CHANGED: the home tank's tile bookkeeping (TANK_P4).
 *
 * Not built by itself: INCLUDED by the P4's display port
 * (firmware/main/display_port_jd9365.c) and by the simulator's
 * --selftest-tiles (sim/main.c), the way render_wide.c is included by
 * render.c. Both run the same lines; the port's copies are done by the PPA,
 * the selftest's by memcpy - so the selftest can hold the glass this logic
 * would show against a fresh full render of the same frame, pixel for pixel,
 * while overlays come, move and leave (2026-10-13; the three ghosting bugs of
 * 2026-10-12 were found by Strato's eye, one at a time).
 *
 * The shape (docs/board-p4-10.1.md, "The display path: only what changed"):
 * the tank renders into one of two RENDER buffers, and the frame is turned
 * into the one of two DSI buffers that is not on the glass. A whole frame
 * through the PPA is 50 ms and a whole scene restore 23 ms more, so both are
 * done by RENDER_TILE tiles:
 *   - p4_tiles_changed: this frame's dynamic rectangles (render_changed_rects),
 *     or n < 0 = everything (a page that covers the glass, the scene baked again);
 *   - p4_tiles_overlay: the tiles drawn on after render_tank (render.c, THE
 *     OVERLAY TILES): the toast, a card, an announcement, the setup flow ...,
 *     each with its drawing signed (THE SPARED TURN, below);
 *   - p4_tiles_algae: the film on the glass - a cell that grew or was wiped
 *     is a change (the frame puts the scene back under the blobs themselves,
 *     render.c THE FILM'S GROUND - not the port, 2026-10-20);
 *   - p4_tiles_flush: turn into DSI buffer `idle` this frame's tiles, what was
 *     queued for it, and what its own last frame drew (to take it away);
 *   - p4_tiles_restore: put the scene back into the render buffer the next
 *     frame draws into, where the frame it holds drew.
 * The three rules every change obeys: a change reaches BOTH DSI buffers and
 * BOTH render buffers; what is blended gets its ground back every frame; what
 * leaves is taken away in both. */
#ifdef TANK_P4
#include "tank.h"
#include "render.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define TILE    RENDER_TILE                    /* the renderer's overlay tiles are this grid (render.h) */
#define TILES_X RENDER_TILES_X
#define TILES_Y RENDER_TILES_Y
#define P4_FULL_EVERY 50                       /* a whole frame every so many, for safety */

/* who does the copying: the port's PPA, or the selftest's memcpy */
typedef struct {
    void (*turn)(int dsi, int x0, int y0, int w, int h);       /* the frame's block (frame px) into DSI buffer `dsi` */
    void (*turn_all)(int dsi);                                 /* ... the whole frame */
    void (*put)(uint16_t *fb, const uint16_t *scene, int x0, int y0, int w, int h);   /* the scene's block into a render buffer */
    void (*put_all)(uint16_t *fb, const uint16_t *scene);
} p4_tiles_ops_t;

static uint8_t s_held[2][TILES_Y][TILES_X];   /* the dynamic tiles the frame in each DSI buffer holds */
static bool s_held_full[2] = { true, true };    /* ... or the frame it holds was "everything" (a page): the next frame into it is too */
/* a change seen at frame N must reach BOTH DSI buffers - the one written at N and the one
 * written at N+1 (a film cell that grew, say, is in no later list) - and both render
 * buffers' scene restores: queued per buffer until that buffer has been written */
static uint8_t s_pend[2][TILES_Y][TILES_X];    /* queued for each DSI buffer */
static bool s_pend_full[2];
static uint8_t s_rpend[2][TILES_Y][TILES_X];   /* queued for each render buffer's restore */
static const uint16_t *s_rfb[2];                /* which render buffer is which */
static uint8_t s_rfilm[TILES_Y][TILES_X];      /* the film's tiles this frame: the film is drawn blended over whatever is under it, so the
                                                  scene goes back under every cell EVERY frame, or a tile a fish just left is lighter than its neighbours */
static uint8_t s_now[TILES_Y][TILES_X];        /* this frame's */
static uint8_t s_prev[TILES_Y][TILES_X];       /* the frame before: what the OTHER render buffer holds, for its scene restore */
static bool s_now_full = true, s_prev_full = true;   /* this frame / the one before: everything */
static unsigned s_frames;
/* A KEPT PAGE (0.4.3; render.c THE KEPT PAGE'S BOXES): a page that covers the glass and keeps its own frame says which
 * render buffer it is in (p4_tiles_changed_page), whole (n < 0) or with only its moving boxes. A DSI buffer last written
 * whole from that same render buffer holds the page's ground already: the boxes alone are turned into it. It still
 * holds "everything" for what comes after (the tank back: the whole frame). */
static int s_page_src = -1;                     /* this frame: a kept page drawn into this render buffer (0 / 1); -1: not one */
static int8_t s_dsi_page[2] = { -1, -1 };       /* each DSI buffer: the page it holds was turned from this render buffer; -1: no page */
/* THE SPARED TURN (2026-10-13). A tile that only an overlay drew on this frame - not in
 * the tank's rectangles, no film on it - is the scene with that drawing over it, and the
 * renderer signs the drawing (render_overlay_t.sig). If the DSI buffer about to be written
 * already holds that tile from a frame when it was the overlay's alone, with the same sig,
 * the pixels are the same: it is not turned. An announcement, the setup panel, a page that
 * covers the glass are the same picture frame after frame - only the tiles where something
 * in them moved (a bar running out, a fish in a portrait) go through the PPA. */
static uint8_t  s_tank[TILES_Y][TILES_X];      /* this frame: the tank drew here (its rectangles, the film) */
static uint8_t  s_ov[TILES_Y][TILES_X];        /* this frame: an overlay drew here ... */
static uint32_t s_sig[TILES_Y][TILES_X];       /* ... this, signed */
static uint8_t  s_dsi_ok[2][TILES_Y][TILES_X]; /* each DSI buffer: the tile it holds was an overlay's alone ... */
static uint32_t s_dsi_sig[2][TILES_Y][TILES_X];/* ... with this sig */
static unsigned s_turned, s_spared;            /* tiles turned / spared so far (the selftest, the bench) */
static unsigned s_restored, s_restored_film, s_rect_tiles;   /* scene restores: tiles put back, ... of them only for the film; the rectangles' tiles (the bench) */

static void p4_tiles_clip(int *x0, int *y0, int *x1, int *y1) {      /* frame px -> tiles, inside the grid */
    *x0 /= TILE; *y0 /= TILE; *x1 /= TILE; *y1 /= TILE;
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 >= TILES_X) *x1 = TILES_X - 1;
    if (*y1 >= TILES_Y) *y1 = TILES_Y - 1;
}
static void p4_tiles_film(int x0, int y0, int x1, int y1) {
    p4_tiles_clip(&x0, &y0, &x1, &y1);
    for (int ty = y0; ty <= y1; ty++) for (int tx = x0; tx <= x1; tx++) { s_rfilm[ty][tx] = 1; s_tank[ty][tx] = 1; }
}
static uint8_t s_drift[TILES_Y][TILES_X];      /* this frame: tiles in s_now for the drift alone (turned, not put back) */
static void p4_tiles_mark(int x0, int y0, int x1, int y1) {   /* one more changed box (a film cell that grew or was wiped) */
    p4_tiles_clip(&x0, &y0, &x1, &y1);
    for (int ty = y0; ty <= y1; ty++) for (int tx = x0; tx <= x1; tx++) { s_now[ty][tx] = 1; s_tank[ty][tx] = 1; s_rpend[0][ty][tx] = 1; s_rpend[1][ty][tx] = 1; }
}
static void p4_tiles_changed(const short *xy, int n) {         /* a new frame: its rectangles, or n < 0 = everything */
    s_page_src = -1;
    memcpy(s_prev, s_now, sizeof s_prev); s_prev_full = s_now_full;
    for (int ty = 0; ty < TILES_Y; ty++) for (int tx = 0; tx < TILES_X; tx++) if (s_drift[ty][tx] && !s_ov[ty][tx]) s_prev[ty][tx] = 0;   /* (THE DRIFT'S TILES: turned, not put back - unless an overlay drew there too) */
    memset(s_drift, 0, sizeof s_drift);
    memset(s_now, 0, sizeof s_now); memset(s_tank, 0, sizeof s_tank); memset(s_ov, 0, sizeof s_ov);
    s_now_full = n < 0;
    for (int i = 0; i < n; i++) {
        int x0 = xy[i * 4], y0 = xy[i * 4 + 1], x1 = xy[i * 4 + 2], y1 = xy[i * 4 + 3];
        p4_tiles_clip(&x0, &y0, &x1, &y1);
        for (int ty = y0; ty <= y1; ty++) for (int tx = x0; tx <= x1; tx++) { s_rect_tiles += !s_now[ty][tx]; s_now[ty][tx] = 1; s_tank[ty][tx] = 1; }
    }
    /* THE DRIFT'S TILES (0.4.3; render.c): the tiles a speck of the drift changed in. Turned like any other - but the
       renderer puts the scene back under a speck itself, so a tile that is ONLY the drift's is not the restore's next
       frame (a put-back a tile, a quarter of a millisecond each on the 10.1's chip and a millisecond on the Tab5's). A
       tile the tank's list or the film marks as well is put back as ever. Read every frame (it empties the list) */
    { extern const uint8_t *render_drift_tiles(void);
      const uint8_t *dt = render_drift_tiles();
      if (dt && n >= 0) for (int ty = 0; ty < TILES_Y; ty++) for (int tx = 0; tx < TILES_X; tx++) if (dt[ty * TILES_X + tx]) {
          if (!s_now[ty][tx]) { s_now[ty][tx] = 1; s_drift[ty][tx] = 1; s_rect_tiles++; }
          s_tank[ty][tx] = 1;
      } }
}
static void p4_tiles_changed_page(const short *xy, int n, int src) { p4_tiles_changed(xy, n); s_page_src = src & 1; }   /* A KEPT PAGE's frame */
/* THE FILM on the glass is not in the renderer's list: a cell that grew, or was wiped or grazed, is a change. Until
 * 2026-10-20 the scene also went back under every film tile every frame (the film is blended over what is under it);
 * now the frame does that itself under the blobs' own pixels (render.c THE FILM'S GROUND), and s_rfilm only counts
 * what that spares and keeps a film tile from being spared a turn (s_tank). A cell's blobs sit
 * anywhere in the cell and reach up to 5.7 px past it (draw_algae: r <= 3.5 + 2.2) - FILM_REACH, on both counts. The
 * mark had no reach and the ground 4 px until 2026-10-13: a wiped cell could leave a sliver of film in the tile next
 * door (--selftest-tiles found it). After p4_tiles_changed, every frame. */
#define FILM_REACH 6
static void p4_tiles_algae(const uint8_t *algae) {
    static uint8_t film[ALGAE_CELLS];
    for (int i = 0; i < ALGAE_CELLS; i++) {
        int x0 = (i % ALGAE_COLS) * ALGAE_CELL - FILM_REACH, y0 = (i / ALGAE_COLS) * ALGAE_CELL - FILM_REACH;
        int x1 = x0 + ALGAE_CELL - 1 + 2 * FILM_REACH, y1 = y0 + ALGAE_CELL - 1 + 2 * FILM_REACH;
        if (film[i] != algae[i]) { film[i] = algae[i]; p4_tiles_mark(x0, y0, x1, y1); }
        if (algae[i]) p4_tiles_film(x0, y0, x1, y1);
    }
}
/* what was drawn over the live tank this frame (render_overlay), after p4_tiles_changed.
 * This frame's tiles like the rectangles: turned into both DSI buffers, turned once more in
 * each when the overlay has gone (s_held), and the scene put back under them before the
 * render buffer is drawn into again (s_prev) - a blended backdrop always lands on clean water. */
static void p4_tiles_overlay(const render_overlay_t *o) {
    for (int ty = 0; ty < TILES_Y; ty++) for (int tx = 0; tx < TILES_X; tx++)
        if (o->drawn[ty][tx]) { s_now[ty][tx] = 1; s_ov[ty][tx] = 1; s_sig[ty][tx] = o->sig[ty][tx]; }
}

/* THE BLOCKS (0.4.3). A PPA operation costs a quarter of a millisecond whatever its size (the driver's cache sync, its
 * start, its interrupt): the Tab5's grown tank turned 277 tiles in 69 operations - a run of tiles in a tile row each -
 * in the time a whole frame takes in 8. So runs that stand on one another with the same columns go as ONE block: a
 * bed of grass seven tile rows tall is a block or two, not seven. The same tiles exactly, never one more; a block
 * no bigger than a strip of the whole-frame turn. */
#define P4_BLOCK_TILES 128                     /* a block's most tiles: a whole frame's strip is this big (160 x 800 px), and one transfer of the whole frame flashed (10-12) */
typedef void (*p4_block_fn)(void *arg, int x0, int y0, int w, int h);
static void p4_tiles_blocks(uint8_t need[TILES_Y][TILES_X], p4_block_fn fn, void *arg) {
    for (int ty = 0; ty < TILES_Y; ty++)
        for (int tx = 0; tx < TILES_X; ) {
            if (!need[ty][tx]) { tx++; continue; }
            int tx1 = tx; while (tx1 + 1 < TILES_X && need[ty][tx1 + 1]) tx1++;
            int ty1 = ty;                               /* the rows under it with this very run (neither wider nor narrower) */
            for (;;) {
                int r = ty1 + 1; if (r >= TILES_Y || (tx1 - tx + 1) * (r - ty + 1) > P4_BLOCK_TILES) break;
                bool same = (tx == 0 || !need[r][tx - 1]) && (tx1 == TILES_X - 1 || !need[r][tx1 + 1]);
                for (int x = tx; x <= tx1 && same; x++) same = need[r][x];
                if (!same) break;
                ty1 = r;
            }
            for (int r = ty + 1; r <= ty1; r++) for (int x = tx; x <= tx1; x++) need[r][x] = 0;   /* (taken) */
            fn(arg, tx * TILE, ty * TILE, (tx1 - tx + 1) * TILE, ty1 * TILE + RENDER_TILE_H(ty1) - ty * TILE);
            tx = tx1 + 1;
        }
}
typedef struct { short x0, y0, x1, y1; } p4_box_t;               /* tiles, inclusive */
#define P4_BOXES_MAX 160                       /* blocks a frame taken into THE TURN'S BOXES; past it, the rest go as they are */
typedef struct { int n; p4_box_t b[P4_BOXES_MAX]; const p4_tiles_ops_t *ops; int dsi; } p4_boxes_t;
static void p4_box_add(void *arg, int x0, int y0, int w, int h) {
    p4_boxes_t *bx = arg;
    if (bx->n == P4_BOXES_MAX) { bx->ops->turn(bx->dsi, x0, y0, w, h); return; }
    bx->b[bx->n++] = (p4_box_t){ (short)(x0 / TILE), (short)(y0 / TILE), (short)((x0 + w - 1) / TILE), (short)((y0 + h - 1) / TILE) };
}
#ifdef TANK_TAB5
static void p4_boxes_merge(p4_boxes_t *pb) {   /* THE TURN'S BOXES: blocks that stand close, as one */
#define bx (*pb)
    for (bool again = bx.n > 1; again; ) {
          again = false;
          for (int i = 0; i < bx.n && !again; i++) for (int j = i + 1; j < bx.n; j++) {
              const p4_box_t *a = &bx.b[i], *c = &bx.b[j];
              const int x0 = a->x0 < c->x0 ? a->x0 : c->x0, y0 = a->y0 < c->y0 ? a->y0 : c->y0, x1 = a->x1 > c->x1 ? a->x1 : c->x1, y1 = a->y1 > c->y1 ? a->y1 : c->y1;
              const int area = (x1 - x0 + 1) * (y1 - y0 + 1), sum = (a->x1 - a->x0 + 1) * (a->y1 - a->y0 + 1) + (c->x1 - c->x0 + 1) * (c->y1 - c->y0 + 1);
              if (area > P4_BLOCK_TILES || area * 3 > sum * 4 + 12) continue;
              bx.b[i] = (p4_box_t){ (short)x0, (short)y0, (short)x1, (short)y1 }; bx.b[j] = bx.b[--bx.n]; again = true; break;
          }
      }
#undef bx
}
#endif
typedef struct { const p4_tiles_ops_t *ops; int dsi; uint16_t *fb; const uint16_t *scene; } p4_block_t;
static void p4_block_put(void *arg, int x0, int y0, int w, int h) { const p4_block_t *b = arg; b->ops->put(b->fb, b->scene, x0, y0, w, h); render_p4_restored(b->fb, x0, y0, w, h); }   /* (the renderer is told: THE FILM KEPT) */

/* the frame into DSI buffer `idle` (the one not on the glass); force_full = the bench's "always everything" */
static void p4_tiles_flush(int idle, bool force_full, const p4_tiles_ops_t *ops) {
    for (int k = 0; k < 2; k++) {                   /* this frame's changes, queued for both buffers */
        for (int ty = 0; ty < TILES_Y; ty++) for (int tx = 0; tx < TILES_X; tx++) s_pend[k][ty][tx] |= s_now[ty][tx];
        s_pend_full[k] |= s_now_full;
    }
    const bool kept = s_page_src >= 0 && !s_now_full && s_dsi_page[idle] == s_page_src;   /* A KEPT PAGE's boxes over the ground this buffer holds */
    bool full = s_pend_full[idle] || (s_held_full[idle] && !kept) || (s_page_src >= 0 && !kept) || force_full || s_frames < 3 || (s_frames % P4_FULL_EVERY) == 0;
    s_frames++;
    s_held_full[idle] = s_now_full || s_page_src >= 0;   /* what this buffer holds after this frame: a page (not in the tile list) or not */
    s_dsi_page[idle] = (int8_t)s_page_src;
    s_pend_full[idle] = false;
    if (full) {
        ops->turn_all(idle);
        memcpy(s_held[idle], s_now, sizeof s_now); memset(s_pend[idle], 0, sizeof s_pend[idle]);
        for (int ty = 0; ty < TILES_Y; ty++) for (int tx = 0; tx < TILES_X; tx++) {     /* what it holds now, tile by tile */
            s_dsi_ok[idle][ty][tx] = !s_now_full && s_ov[ty][tx] && !s_tank[ty][tx]; s_dsi_sig[idle][ty][tx] = s_sig[ty][tx];
            if (s_tank[ty][tx]) s_dsi_ok[idle ^ 1][ty][tx] = false;   /* (THE DRIFT'S TILES, below: the tank drew here - the other buffer's spare is over) */
        }
        s_turned += TILES_X * TILES_Y;
        return;
    }
    uint8_t need[TILES_Y][TILES_X];
    for (int ty = 0; ty < TILES_Y; ty++) for (int tx = 0; tx < TILES_X; tx++) {
        need[ty][tx] = s_pend[idle][ty][tx] | s_held[idle][ty][tx];
        if (!need[ty][tx]) continue;
        bool alone = s_ov[ty][tx] && !s_tank[ty][tx];         /* the overlay's alone this frame: the scene and its signed drawing */
        if (alone && s_dsi_ok[idle][ty][tx] && s_dsi_sig[idle][ty][tx] == s_sig[ty][tx]) { need[ty][tx] = 0; s_spared++; continue; }   /* THE SPARED TURN */
        s_dsi_ok[idle][ty][tx] = alone; s_dsi_sig[idle][ty][tx] = s_sig[ty][tx]; s_turned++;
        if (!alone) s_dsi_ok[idle ^ 1][ty][tx] = false;   /* (0.4.3, THE DRIFT'S TILES: the tank drew here - the OTHER buffer's spare is over too. It
                                                             could stand while everything of the tank marked its tile every frame it was there; a speck
                                                             of the drift marks its tile once, when it moves, and stays: the other buffer, a frame
                                                             later, saw an overlay alone on a tile it had "the same" and kept the speck where it was) */
    }
    memcpy(s_held[idle], s_now, sizeof s_now); memset(s_pend[idle], 0, sizeof s_pend[idle]);
    /* THE TURN'S BOXES (0.4.3): the turn may take a tile more than it needs - what the render buffer holds outside this
       frame's tiles is what the glass shows there already - and an operation costs the Tab5's PPA about a millisecond
       whatever its size (its turn of 210 tiles in 30 blocks ran 34 ms: longer than the frame's draw, which then waited
       on it; in 8 boxes the Tab5's own tank draws in 18 ms where it took 22.5, and its worst case is 19.6 - 21.7 fps for 18.9 - 20.8). So blocks that stand close are turned as one box, while the box is not much more than the two (a third,
       and four tiles) and no bigger than a whole-frame strip. The restore keeps its blocks exact. */
    { p4_boxes_t bx; bx.n = 0; bx.ops = ops; bx.dsi = idle; p4_tiles_blocks(need, p4_box_add, &bx);
#ifdef TANK_TAB5                               /* (the Tab5's chip alone: on the 10.1's - silicon v3 - an operation is cheap and the extra tiles cost more
                                                  than they spare: its worst case 21.0 fps in blocks, 20.3 in boxes, 11-16) */
      p4_boxes_merge(&bx);
#endif
      for (int i = 0; i < bx.n; i++) {
          const p4_box_t *q = &bx.b[i];
          ops->turn(idle, q->x0 * TILE, q->y0 * TILE, (q->x1 - q->x0 + 1) * TILE, q->y1 * TILE + RENDER_TILE_H(q->y1) - q->y0 * TILE);   /* (the last tile row may be short: the Tab5) */
      } }
}

/* THE SCENE RESTORE for the render buffer the next frame draws into: it holds the frame
 * before this one, so only that frame's dynamic tiles need the scene put back. `full`: the
 * scene was baked again, or that buffer held a page (the port saw "everything"). */
static void p4_tiles_restore(uint16_t *fb, const uint16_t *scene, bool full, const p4_tiles_ops_t *ops) {
    int ri = s_rfb[0] == fb ? 0 : s_rfb[1] == fb ? 1 : s_rfb[0] ? 1 : 0;   /* which render buffer this is (the first two seen) */
    s_rfb[ri] = fb;
    if (full || s_prev_full) {
        memset(s_rpend[ri], 0, sizeof s_rpend[ri]);
        s_restored += TILES_X * TILES_Y;
        ops->put_all(fb, scene);
        render_p4_restored(fb, 0, 0, -1, -1);
    } else {
        uint8_t need[TILES_Y][TILES_X];
        for (int ty = 0; ty < TILES_Y; ty++) for (int tx = 0; tx < TILES_X; tx++) {
            need[ty][tx] = s_prev[ty][tx] | s_rpend[ri][ty][tx];   /* (not the film's tiles: the frame puts its ground back itself, render.c THE FILM'S GROUND) */
            s_restored += need[ty][tx]; s_restored_film += s_rfilm[ty][tx] && !s_prev[ty][tx] && !s_rpend[ri][ty][tx];
        }
        memset(s_rpend[ri], 0, sizeof s_rpend[ri]);
        /* (THE BLOCKS, exact: boxes as the turn's were tried on the Tab5 11-16 - 11 operations for 30, and 4 ms SLOWER:
           a restore is paid by the tile, and the extra tiles cost more than the operations spared) */
        { p4_block_t b = { ops, 0, fb, scene }; p4_tiles_blocks(need, p4_block_put, &b); }
    }
    memset(s_rfilm, 0, sizeof s_rfilm);
}
#endif /* TANK_P4 */
