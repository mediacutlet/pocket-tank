/* selftest_libload.c - --selftest-libload and --libload-shot (0.4.2, common/lib_pics.h): THE LIBRARY opens at once and its
 * theme pictures are made behind it. Strato 11-10: "When in the tank switcher and I hit + to add a new tank, the library
 * takes 2-3 seconds to load. can we give the user feedback here with some kind of loading animation?"
 *
 * The page's frame here is the boards' (firmware/main/main.c shelf_render, the library's part), line for line: the page
 * drawn once and kept, copied each frame, the bubbles over it while a picture is wanted, a little of the picture's work
 * on every frame that did not draw the page. The pictures and the bubbles are common/ code, the same on a board.
 *   A  the first frame: the whole page - title, both rows, names, lines, buttons, the wallet - with the jelly row's glass
 *      empty; no picture exists and none has been worked on (the next frame sets the tank up - the engine's scratch
 *      is built there, a frame of its own - and the ticks start on the one after). It is the page 0.4.1 drew with no picture, plus the bubbles
 *      inside that glass and nowhere else
 *   B  the bubbles: light shapes on the dark (told by brightness and place, not hue), inside the glass only, moving
 *      from frame to frame; gone the frame the picture is in
 *   C  the work: ticks only inside the frame's budget, the drawing a frame of its own; the made picture goes straight
 *      into the kept page (the page is not drawn again: one long frame a picture, not two); the list makes theme 0 and
 *      no other, however long it stays up
 *   D  the finished list is 0.4.1's finished list, byte for byte (its picture made the old way: all at once)
 *   E  PICK A THEME: opens at once with what is made, the others arrive one at a time in order, each of the three
 *      0.4.1's picture byte for byte, the finished page 0.4.1's; opened before theme 0 is made, all three arrive
 *   F  a tap while a picture is loading acts: the rows' buttons, BACK, a theme, answer as on the finished page
 *   G  closed mid-load (on every frame of a picture's making): every block given back, nothing left; opened again
 *      it starts from nothing and ends on the same page. Left for the shelves mid-load and back: the same
 *   H  no memory for the pictures: the page as 0.4.1 drew it without them, no bubbles, the taps answer
 *   I  whatever the budget (one tick a frame, everything at once), and with a jelly tank's water cache in use (keyed
 *      to another light), the same pictures
 *   J  the locked row (a board off the crew): dim bubbles in its dim glass; its note up: no bubbles over the note,
 *      the picture still arrives under it
 *   K  a tap on every frame (each draws the page again) does not hold a picture up
 *   L  a swap begun, or MY TANKS shut by a page, with a picture half made: no work, every block back, a fresh start;
 *      the opening tap's double is spent as before and the tap after it acts
 * The water and the pictures are pinned to main's (e8d49ea) by hash, a board each (I).
 * Red before 0.4.2: A (the page waited for three pictures - the first frame had them all). PT_LIBLOAD_OLD=1 puts that
 * order back in this file's copy of the page (every picture made as the page opens) and the test fails at A. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tank.h"
#include "render.h"
#include "progression.h"
#include "jelly.h"
#include "lib_pics.h"
#include "touch_gate.h"

#define LF(...) do { printf("FAIL: libload: "); printf(__VA_ARGS__); printf("\n"); return 1; } while (0)
#define NPX (TANK_W * TANK_H)

/* ---- the board's side: its memory, its clock ---- */
static int g_live, g_allocs; static bool g_no_mem;
static void *lp_alloc(size_t n) { if (g_no_mem) return NULL; g_live++; g_allocs++; return malloc(n); }
static void lp_free(void *p) { if (p) { g_live--; free(p); } }
static int64_t g_clk, g_clk_step = 1000;                 /* every look at the clock is 1 ms later: a tick a ms of budget */
#define PER   (LIB_PIC_BUDGET_US / 1000)                  /* ticks a frame at the boards' budget */
#define FIRST (1 + 1 + (LIB_PIC_TICKS + PER - 1) / PER + 1)   /* the frame a picture is made on: the page, its tank set up, the ticks, the drawing */
static bool g_real;                                      /* (--libload-shot's timing: this machine's own clock) */
static int64_t lp_now(void) {
    if (g_real) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000; }
    return g_clk += g_clk_step;
}

typedef struct {
    bool lib, pick, note, baked, crew, have_jelly, jelly_glass;
    int picked, budget;
    lib_pics_t *pics; void *scratch; uint16_t *page;
    int frame, arrived[JELLY_THEME_N], bakes, work_us[JELLY_THEME_N];
    const uint16_t *fish;
} lp_t;
static uint16_t g_jbg[NPX];                              /* a jelly tank's water cache (I) */

static void lp_open(lp_t *h) {                           /* jelly_lib_open */
    if (h->pics || !h->page) return;
    if (!h->scratch) { h->scratch = lp_alloc(jelly_scratch_bytes()); if (h->scratch) { memset(h->scratch, 0, jelly_scratch_bytes()); jelly_set_scratch(h->scratch); } }
    h->pics = h->scratch ? lp_alloc(lib_pics_bytes()) : NULL;
    if (!h->pics) return;
    lib_pics_init(h->pics);
    if (!h->jelly_glass) jelly_set_bg_cache(NULL);
    if (getenv("PT_LIBLOAD_OLD"))                        /* 0.4.1's order - every picture before the page draws - to see this test go red */
        while (lib_pics_waiting(h->pics, 7)) lib_pics_step(h->pics, 7, h->page, lp_now, 1000000, NULL, NULL, NULL);
}
static void lp_close(lp_t *h) {                          /* shelf_free + jelly_lib_free: MY TANKS closed */
    lp_free(h->pics); h->pics = NULL;
    if (h->scratch) { jelly_set_scratch(NULL); lp_free(h->scratch); h->scratch = NULL; }
    lp_free(h->page); h->page = NULL;
    h->baked = false; h->lib = h->pick = h->note = false;
}
static void lib_rows(const lp_t *h, const uint16_t *jth0, library_row_t r[2]) {
    r[0] = (library_row_t){ "FISH TANK", "FISH THAT THINK FOR THEMSELVES. FEED THEM, TEND THE TANK, WATCH THEM GROW.", h->fish, false, true, 300, false, false };
    r[1] = (library_row_t){ "JELLY TANK", "GLOWING JELLIES DRIFTING IN THE DARK. NOTHING TO DO BUT WATCH.", jth0, true, h->have_jelly, 0, h->have_jelly, !h->have_jelly && !h->crew };
}
static void draw_page(const lp_t *h, uint16_t *dst, const uint16_t *const jth[JELLY_THEME_N]) {
    if (h->pick) {
        const char *names[JELLY_THEME_N]; for (int k = 0; k < JELLY_THEME_N; k++) names[k] = JELLY_THEMES[k].name;
        render_jelly_pick(dst, TANK_W, names, jth, h->picked);
    } else if (h->lib) {
        library_row_t r[2]; lib_rows(h, jth[0], r);
        render_library(dst, TANK_W, r, 2, 412, "MORE KINDS OF TANK ARE COMING");
        if (h->note) render_library_note(dst, TANK_W);
    } else {
        shelf_slot_t sl[8]; for (int j = 0; j < 8; j++) sl[j] = (shelf_slot_t){ SHELF_EMPTY, "", false, NULL };
        sl[0] = (shelf_slot_t){ SHELF_NOW, "MY TANK", false, h->fish };
        render_shelf(dst, TANK_W, sl, 1, 1);
    }
}
#define LP_WANT(h) ((h)->pick ? (1u << JELLY_THEME_N) - 1 : 1u)
static void lp_frame(lp_t *h, uint16_t *fb, float at) {  /* shelf_render */
    if (!h->page) h->page = lp_alloc(sizeof(uint16_t) * NPX);
    if (!h->page) return;
    h->frame++;
    if (h->lib && h->pics && lib_pics_waiting(h->pics, LP_WANT(h))) {   /* a little work, this frame its scratch - never the page's first frame */
        int k = lib_pics_step(h->pics, LP_WANT(h), fb, lp_now, h->budget, NULL, NULL, NULL);
        if (k >= 0) {
            h->arrived[k] = h->frame; h->work_us[k] = (int)h->pics->work_us;
            if (h->note) h->baked = false;
            else if (h->baked && h->pick) render_jelly_pick_pic(h->page, TANK_W, k, lib_pics_get(h->pics, k), h->picked);
            else if (h->baked && k == 0) render_library_pic(h->page, TANK_W, 1, lib_pics_get(h->pics, 0), !h->have_jelly && !h->crew);
        }
    } else if (!h->lib) lib_pics_rest(h->pics);
    if (!h->baked) {
        const uint16_t *jth[JELLY_THEME_N] = { NULL, NULL, NULL };
        if (h->lib) { lp_open(h); for (int k = 0; k < JELLY_THEME_N; k++) jth[k] = lib_pics_get(h->pics, k); }
        draw_page(h, h->page, jth);
        h->baked = true; h->bakes++;
    }
    memcpy(fb, h->page, sizeof(uint16_t) * NPX);
    const unsigned jwait = h->lib ? lib_pics_waiting(h->pics, LP_WANT(h)) : 0;
    if (jwait && h->pick) render_jelly_pick_loading(fb, TANK_W, jwait, h->picked, at);
    else if (jwait && !h->note) render_library_loading(fb, TANK_W, 2, !h->have_jelly && !h->crew ? 2 : 0, at);
}
static void lp_free_only(lp_t *h) {                      /* shelf_free alone: MY TANKS shut by a page or the birth flow (the frame loop), its flags as they were */
    lp_free(h->pics); h->pics = NULL;
    if (h->scratch) { jelly_set_scratch(NULL); lp_free(h->scratch); h->scratch = NULL; }
    lp_free(h->page); h->page = NULL; h->baked = false;
}
static void lp_reset(lp_t *h, const uint16_t *fish) {
    memset(h, 0, sizeof *h); h->crew = true; h->picked = -1; h->budget = LIB_PIC_BUDGET_US; h->fish = fish;
    for (int k = 0; k < JELLY_THEME_N; k++) h->arrived[k] = -1;
}

/* ---- 0.4.1's pictures: all three, all at once (main.c jelly_lib_thumbs as it was) ---- */
static void old_pics(uint16_t *frame, uint16_t *out, size_t px) {
    jelly_tank_t *jt = calloc(1, sizeof *jt);
    for (int k = 0; k < JELLY_THEME_N; k++) {
        jelly_theme_start(jt, k);
        for (int i = 0; i < 40; i++) jelly_tick(jt, 0.1f);
        jelly_fb_primed(NULL);
        jelly_render(jt, frame, TANK_W);
        render_shelf_thumb(frame, out + (size_t)k * px);
    }
    jelly_fb_primed(NULL);
    free(jt);
}
static int luma(uint16_t v) { int r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3; return (r * 77 + g * 150 + b * 29) >> 8; }
static bool in_rect(int x, int y, const int q[4]) { return x >= q[0] && x < q[0] + q[2] && y >= q[1] && y < q[1] + q[3]; }
/* fb against the kept page: pixels that differ outside the given glasses (must be 0), and inside (the bubbles) */
static void diff_page(const uint16_t *fb, const uint16_t *page, int nq, int q[][4], int *outside, int *inside, int *bright) {
    *outside = *inside = *bright = 0;
    for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) {
        if (fb[y * TANK_W + x] == page[y * TANK_W + x]) continue;
        bool in = false; for (int k = 0; k < nq; k++) in |= in_rect(x, y, q[k]);
        if (!in) { (*outside)++; continue; }
        (*inside)++; if (luma(fb[y * TANK_W + x]) >= 110) (*bright)++;
    }
}
static uint32_t fnv(const uint16_t *p, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
static bool find_tap(int want, bool pick, float *tx, float *ty) {   /* a place on the page that answers `want` */
    for (int y = 4; y < TANK_H; y += 4) for (int x = 4; x < TANK_W; x += 4)
        if ((pick ? render_jelly_pick_tap(x, y) : render_library_tap(x, y, 2)) == want) { *tx = x; *ty = y; return true; }
    return false;
}

static uint16_t fa[NPX], fb2[NPX], fc[NPX], scratch_frame[NPX];

int selftest_libload(void) {
    int tw, th; render_shelf_thumb_size(&tw, &th);
    const size_t px = (size_t)tw * th;
    uint16_t *fish = malloc(px * 2), *old = malloc(px * 2 * JELLY_THEME_N);
    for (size_t i = 0; i < px; i++) fish[i] = (uint16_t)(0x0200 + (i % tw) / 4);   /* (any picture: the fish tank's snapshot) */
    void *sc0 = calloc(1, jelly_scratch_bytes());
    jelly_set_scratch(sc0); jelly_set_bg_cache(NULL);
    old_pics(scratch_frame, old, px);
    const uint16_t *oldp[JELLY_THEME_N] = { old, old + px, old + 2 * px };
    jelly_set_scratch(NULL); free(sc0);
    lp_t H; lp_reset(&H, fish);
    int q[3][4], out, in, bright;

    /* ---- A, B, C, D: the list ---- */
    H.lib = true;
    lp_frame(&H, fa, 0.0f);
    if (!H.pics) LF("A: the pictures' memory was not asked for as the page opened");
    if (H.pics->have || H.pics->cur != -1 || H.pics->ticks) LF("A: work on a picture (have %d, stage %d, ticks %d) before the first frame was out", H.pics->have, H.pics->cur, H.pics->ticks);
    { const uint16_t *none[JELLY_THEME_N] = { NULL, NULL, NULL }; draw_page(&H, fb2, none); }
    if (memcmp(H.page, fb2, sizeof fb2)) LF("A: the first frame's page is not the whole page with the jelly row's glass empty");
    render_library_pic_rect(1, &q[0][0], &q[0][1], &q[0][2], &q[0][3]);
    if (q[0][2] != tw || q[0][3] != th) LF("A: the jelly row's glass is %dx%d, a picture %dx%d", q[0][2], q[0][3], tw, th);
    diff_page(fa, H.page, 1, q, &out, &in, &bright);
    if (out) LF("A: the first frame differs from the page at %d pixels outside the jelly row's glass", out);
    if (in < 20 || bright < 12) LF("B: no bubbles in the empty glass on the first frame (%d pixels, %d light)", in, bright);
    for (int y = q[0][1]; y < q[0][1] + q[0][3]; y++) for (int x = q[0][0]; x < q[0][0] + q[0][2]; x++)
        if (luma(fb2[y * TANK_W + x]) > 40) LF("B: the empty glass is not dark at %d,%d (the bubbles are told by light on dark)", x, y);
    {   /* the bubbles move: over one turn every frame's are in a new place, and they pass the glass's top, middle and foot */
        uint32_t seen[60]; int rows_hit[3] = { 0, 0, 0 };
        for (int f = 0; f < 60; f++) {
            memcpy(fc, H.page, sizeof fc);
            render_library_loading(fc, TANK_W, 2, 0, f * 0.04f);
            diff_page(fc, H.page, 1, q, &out, &in, &bright);
            if (out || bright < 8) LF("B: the bubbles at %.2f s: %d pixels outside the glass, %d light inside", f * 0.04f, out, bright);
            seen[f] = fnv(fc, NPX);
            for (int g = 0; g < f; g++) if (seen[g] == seen[f] && f - g < 30) LF("B: the bubbles stood still (frames %d and %d alike)", g, f);
            for (int y = q[0][1]; y < q[0][1] + q[0][3]; y++) for (int x = q[0][0]; x < q[0][0] + q[0][2]; x++)
                if (fc[y * TANK_W + x] != H.page[y * TANK_W + x]) rows_hit[(y - q[0][1]) * 3 / q[0][3]] = 1;
        }
        if (!rows_hit[0] || !rows_hit[1] || !rows_hit[2]) LF("B: the bubbles do not rise through the whole glass");
        memcpy(fc, H.page, sizeof fc); render_library_loading(fc, TANK_W, 1, 0, 0.3f);      /* (row 0's glass, were it ever empty) */
        render_library_pic_rect(0, &q[1][0], &q[1][1], &q[1][2], &q[1][3]);
        diff_page(fc, H.page, 1, &q[1], &out, &in, &bright);
        if (out || !bright) LF("B: row 0's bubbles: %d outside its glass, %d light", out, bright);
    }
    int ticks_before = 0, frames = 1;
    while (!H.pics->have && frames < 200) {
        lp_frame(&H, fa, frames * 0.04f); frames++;
        if (H.pics->have) break;
        int did = H.pics->ticks - ticks_before; ticks_before = H.pics->ticks;
        if (did < 0 || did > H.budget / (int)g_clk_step) LF("C: %d ticks in one frame (the budget is %d)", did, H.budget / (int)g_clk_step);
        if (did == 0 && frames != 2) LF("C: a frame with a picture wanted did no work");
        if (frames == 2 && (did || H.pics->cur != 0)) LF("C: the frame that set theme 0's tank up (and built the engine's scratch) also ticked it (%d)", did);
        diff_page(fa, H.page, 1, q, &out, &in, &bright);
        if (out || bright < 8) LF("B: frame %d while loading: %d pixels outside the glass, %d light inside", frames, out, bright);
    }
    if (H.pics->have != 1) LF("C: theme 0's picture never arrived (%d frames)", frames);
    if (H.arrived[0] != FIRST) LF("C: theme 0 arrived on frame %d, not %d (the page, the tank set up, %d ticks a frame, the drawing)", H.arrived[0], FIRST, PER);
    if (!H.baked || H.bakes != 1) LF("C: the page was drawn again for the picture (%d draws): the picture goes straight into the kept page", H.bakes);
    draw_page(&H, fb2, oldp);                            /* the frame that made it already shows it: no second long frame */
    if (memcmp(fa, fb2, sizeof fa)) LF("D: the frame that made the picture is not the finished list (the picture put into the kept page is not the page drawn whole)");
    lp_frame(&H, fa, 1.0f);
    if (memcmp(lib_pics_get(H.pics, 0), old, px * 2)) LF("D: theme 0's picture is not 0.4.1's");
    if (memcmp(fa, fb2, sizeof fa)) LF("D: the finished list is not 0.4.1's finished list (or bubbles stayed on it)");
    for (int f = 0; f < 40; f++) lp_frame(&H, fa, 2.0f + f * 0.04f);
    if (H.pics->have != 1 || H.pics->cur != -1 || H.bakes != 1) LF("C: the list went on working after its one picture (have %d, stage %d, %d page draws)", H.pics->have, H.pics->cur, H.bakes);
    if (memcmp(fa, fb2, sizeof fa)) LF("D: the finished list changed while it stood");
    printf("selftest-libload: the list: frame 1 the whole page, its jelly glass empty with bubbles (%d px, in the glass only), no work before it; theme 0 in on frame %d, the page then 0.4.1's byte for byte (%08x); no other picture made\n", in, H.arrived[0], fnv(fa, NPX));

    /* ---- F: taps while loading; E: the picker ---- */
    float tx, ty;
    if (!find_tap(LIB_TAP_ROW0 + 1, false, &tx, &ty)) LF("F: no place answers the jelly row's GET");
    lp_close(&H); if (g_live) LF("G: %d blocks left after MY TANKS closed", g_live);
    lp_reset(&H, fish); H.lib = true;
    lp_frame(&H, fa, 0); lp_frame(&H, fa, 0.04f); lp_frame(&H, fa, 0.08f);   /* loading: a frame's ticks in */
    if (H.pics->have || H.pics->ticks != PER) LF("F: not mid-load (have %d, ticks %d)", H.pics->have, H.pics->ticks);
    {   float bx, by, fx, fy;
        if (render_library_tap(tx, ty, 2) != LIB_TAP_ROW0 + 1) LF("F: the jelly row's GET does not answer while its picture loads");
        if (!find_tap(LIB_TAP_ROW0, false, &fx, &fy) || !find_tap(LIB_TAP_BACK, false, &bx, &by)) LF("F: the fish row's button or BACK does not answer while a picture loads");
        int x, y, w, h; render_library_pic_rect(1, &x, &y, &w, &h);
        if (render_library_tap(x + w / 2, y + h / 2, 2) != LIB_TAP_NONE) LF("F: the loading glass took a tap (the picture is no button)"); }
    H.pick = true; H.picked = -1; H.baked = false;       /* the tap: PICK A THEME, at once */
    lp_frame(&H, fa, 0.08f);
    for (int k = 0; k < 3; k++) render_jelly_pick_pic_rect(k, &q[k][0], &q[k][1], &q[k][2], &q[k][3]);
    { const uint16_t *none[JELLY_THEME_N] = { NULL, NULL, NULL }; draw_page(&H, fb2, none); }
    if (memcmp(H.page, fb2, sizeof fb2)) LF("E: PICK A THEME, opened before any picture, is not the whole page with three empty glasses");
    diff_page(fa, H.page, 3, q, &out, &in, &bright);
    if (out || bright < 30) LF("E: PICK A THEME loading: %d pixels outside the three glasses, %d light inside", out, bright);
    if (H.pics->ticks != 2 * PER || H.pics->cur != 0) LF("E: opening PICK A THEME threw away theme 0's work (ticks %d, stage %d)", H.pics->ticks, H.pics->cur);
    {   float px0, py0;
        if (!find_tap(JP_TAP_THEME0 + 2, true, &px0, &py0) || !find_tap(JP_TAP_BACK, true, &px0, &py0)) LF("F: a theme or BACK does not answer while the pictures load");
        int x, y, w, h; render_jelly_pick_pic_rect(1, &x, &y, &w, &h);
        if (render_jelly_pick_tap(x + w / 2, y + h / 2) != JP_TAP_THEME0 + 1) LF("F: a loading theme's glass does not pick it"); }
    H.picked = 1; H.baked = false;                       /* SUNSET picked while all three load: the others' bubbles dim */
    int last_have = 0, order[3] = { -1, -1, -1 }, n_order = 0; frames = 0;
    while (H.pics->have != 7 && frames < 400) {
        lp_frame(&H, fa, 0.12f + frames * 0.04f); frames++;
        if (H.pics->have != last_have) {
            for (int k = 0; k < 3; k++) if ((H.pics->have ^ last_have) >> k & 1) order[n_order++] = k;
            last_have = H.pics->have;
            lp_frame(&H, fa, 0.12f + frames * 0.04f); frames++;   /* the page again: the made ones in, the others still empty */
            const uint16_t *part[JELLY_THEME_N]; for (int k = 0; k < 3; k++) part[k] = (last_have >> k & 1) ? oldp[k] : NULL;
            draw_page(&H, fb2, part);
            if (memcmp(H.page, fb2, sizeof fb2)) LF("E: with pictures %d made the page does not show exactly those", last_have);
            int nq = 0, qq[3][4]; for (int k = 0; k < 3; k++) if (!(last_have >> k & 1)) memcpy(qq[nq++], q[k], sizeof qq[0]);
            diff_page(fa, H.page, nq, qq, &out, &in, &bright);
            if (out) LF("E: bubbles over a picture that is in (%d pixels, pictures %d)", out, last_have);
            if (nq && !in) LF("E: no bubbles in the glasses still empty (pictures %d)", last_have);
        }
    }
    if (order[0] != 0 || order[1] != 1 || order[2] != 2) LF("E: the pictures arrived %d %d %d, not one at a time in order", order[0], order[1], order[2]);
    for (int k = 0; k < 3; k++) if (memcmp(lib_pics_get(H.pics, k), oldp[k], px * 2)) LF("E: theme %d's picture is not 0.4.1's", k);
    lp_frame(&H, fa, 9.0f);
    draw_page(&H, fb2, oldp);
    if (memcmp(fa, fb2, sizeof fa)) LF("E: the finished PICK A THEME is not 0.4.1's");
    uint32_t pick_hash = fnv(fa, NPX);
    H.pick = false; H.baked = false; lp_frame(&H, fa, 9.1f);   /* BACK: the list, its picture there at once */
    draw_page(&H, fb2, oldp);
    if (memcmp(fa, fb2, sizeof fa)) LF("E: back from PICK A THEME the list is not the finished list");
    printf("selftest-libload: PICK A THEME: tapped %d ticks into theme 0, up at once with three empty glasses; MOONLIGHT, SUNSET, DEEP in one at a time (frames %d %d %d), each 0.4.1's, the page 0.4.1's (%08x); the taps answered throughout\n",
           PER, H.arrived[0], H.arrived[1], H.arrived[2], pick_hash);

    /* ---- G: closed mid-load, opened again; left for the shelves mid-load ---- */
    lp_close(&H); if (g_live) LF("G: %d blocks left after MY TANKS closed with every picture made", g_live);
    for (int stop = 1; stop < FIRST; stop++) {           /* after the first frame, in the ticks, on the frame before the drawing */
        lp_reset(&H, fish); H.lib = true;
        for (int f = 0; f < stop; f++) lp_frame(&H, fa, f * 0.04f);
        if (H.pics->have) LF("G: not mid-load at frame %d", stop);
        lp_close(&H);
        if (g_live) LF("G: %d blocks left after MY TANKS closed on frame %d of a picture", g_live, stop);
        H.lib = true;                                    /* ... and opened again */
        for (int f = 0; f < FIRST + 1; f++) lp_frame(&H, fa, f * 0.04f);
        if (H.bakes != 2 || H.arrived[0] != stop + FIRST) LF("G: opened again after a close on frame %d, the picture took until frame %d (not a fresh %d)", stop, H.arrived[0] - stop, FIRST);
        draw_page(&H, fb2, oldp);
        if (memcmp(fa, fb2, sizeof fa)) LF("G: opened again after a close on frame %d, the finished list is not 0.4.1's", stop);
        lp_close(&H); if (g_live) LF("G: %d blocks left", g_live);
    }
    lp_reset(&H, fish); H.lib = true;
    for (int f = 0; f < 3; f++) lp_frame(&H, fa, f * 0.04f);
    H.lib = false; H.baked = false;                      /* BACK to the shelves, two frames of ticks in */
    for (int f = 0; f < 3; f++) lp_frame(&H, fa, 0);
    if (H.pics->cur != -1 || H.pics->have) LF("G: the half-made picture was not dropped on the shelves (stage %d)", H.pics->cur);
    { const uint16_t *none[JELLY_THEME_N] = { NULL, NULL, NULL }; draw_page(&H, fb2, none); }
    if (memcmp(fa, fb2, sizeof fa)) LF("G: the shelves, left to mid-load, are not the shelves");
    H.lib = true; H.baked = false; int f0 = H.frame;
    for (int f = 0; f < FIRST + 1; f++) lp_frame(&H, fa, f * 0.04f);
    draw_page(&H, fb2, oldp);
    if (H.arrived[0] - f0 != FIRST - 1 || memcmp(fa, fb2, sizeof fa)) LF("G: back in the library the picture did not start over cleanly (frame %d)", H.arrived[0] - f0);
    lp_close(&H); if (g_live) LF("G: %d blocks left", g_live);

    /* ---- H: no memory ---- */
    lp_reset(&H, fish); H.lib = true;
    H.page = lp_alloc(sizeof(uint16_t) * NPX); g_no_mem = true;
    for (int f = 0; f < 5; f++) lp_frame(&H, fa, f * 0.04f);
    { const uint16_t *none[JELLY_THEME_N] = { NULL, NULL, NULL }; draw_page(&H, fb2, none); }
    if (H.pics || memcmp(fa, fb2, sizeof fa)) LF("H: with no memory for the pictures the page is not the plain page (no pictures, no bubbles)");
    if (render_library_tap(tx, ty, 2) != LIB_TAP_ROW0 + 1) LF("H: the page with no pictures does not take a tap");
    H.pick = true; H.baked = false; lp_frame(&H, fa, 0);
    draw_page(&H, fb2, (const uint16_t *const[JELLY_THEME_N]){ NULL, NULL, NULL });
    if (memcmp(fa, fb2, sizeof fa)) LF("H: PICK A THEME with no memory is not the plain page");
    g_no_mem = false; lp_close(&H); if (g_live) LF("H: %d blocks left", g_live);
    printf("selftest-libload: closed on each of a picture's frames and opened again: every block back, a fresh start, the same page; left for the shelves mid-load and back: the same; no memory: the plain page, no bubbles, the taps answer\n");

    /* ---- I: the budget, a jelly tank's cache ---- */
    static const int BUD[] = { 0, 3000, 1000000 }; static const int WANT_FR[] = { 2 + 40 + 1, 2 + 14 + 1, 2 + 1 + 1 };
    for (int b = 0; b < 3; b++) {
        lp_reset(&H, fish); H.lib = true; H.pick = true; H.budget = BUD[b];
        for (int f = 0; f < 200 && (!H.pics || H.pics->have != 7); f++) lp_frame(&H, fa, f * 0.04f);
        if (H.arrived[0] != WANT_FR[b]) LF("I: budget %d us: theme 0 on frame %d, not %d", BUD[b], H.arrived[0], WANT_FR[b]);
        for (int k = 0; k < 3; k++) if (memcmp(lib_pics_get(H.pics, k), oldp[k], px * 2)) LF("I: budget %d us: theme %d's picture differs", BUD[b], k);
        lp_close(&H);
    }
    lp_reset(&H, fish); H.lib = true; H.pick = true; H.jelly_glass = true;
    {   jelly_tank_t *live = calloc(1, sizeof *live);    /* a jelly tank on the glass: its cache keyed to its own light */
        H.scratch = lp_alloc(jelly_scratch_bytes()); memset(H.scratch, 0, jelly_scratch_bytes()); jelly_set_scratch(H.scratch);
        jelly_set_bg_cache(g_jbg); jelly_theme_start(live, 2); jelly_tick(live, 0.04f); jelly_render(live, scratch_frame, TANK_W);
        uint32_t live_hash = fnv(scratch_frame, NPX);
        for (int f = 0; f < 200 && (!H.pics || H.pics->have != 7); f++) {
            lp_frame(&H, fa, f * 0.04f);
            if (f % 5 == 2) { jelly_fb_primed(NULL); jelly_render(live, scratch_frame, TANK_W); if (fnv(scratch_frame, NPX) != live_hash) LF("I: the jelly tank on the glass drew differently while the library made a picture"); }
        }
        for (int k = 0; k < 3; k++) if (!lib_pics_get(H.pics, k) || memcmp(lib_pics_get(H.pics, k), oldp[k], px * 2)) LF("I: on a jelly tank (its water cache in use) theme %d's picture differs", k);
        /* THE WATER AND THE PICTURES ARE MAIN'S (e8d49ea, before 0.4.2 drew the water sooner): every pixel of each theme's
           tank after its 40 ticks, under every light, on and off, drawn straight and through the cache, and the cache
           itself - one hash, taken from e8d49ea's jelly.c on this Mac for each board, and the three pictures' own. A change
           to the water, the ticks or the shrink that moves one pixel fails here (elsewhere: said, not compared) */
        uint32_t wh = 2166136261u;
        for (int thm = 0; thm < JELLY_THEME_N; thm++) {
            jelly_theme_start(live, thm); for (int i = 0; i < LIB_PIC_TICKS; i++) jelly_tick(live, LIB_PIC_DT);
            for (int lt = 0; lt < JLIGHT_N; lt++) for (int on = 0; on < 2; on++) {
                live->light = lt; live->light_on = on;
                jelly_set_bg_cache(NULL); jelly_render(live, scratch_frame, TANK_W);
                for (int i = 0; i < NPX; i++) { wh ^= scratch_frame[i]; wh *= 16777619u; }
                jelly_set_bg_cache(g_jbg); jelly_render(live, fc, TANK_W);
                if (memcmp(scratch_frame, fc, sizeof fc)) LF("I: theme %d light %d %s: the tank drawn straight and through the cache differ", thm, lt, on ? "on" : "off");
                for (int i = 0; i < NPX; i++) { wh ^= fc[i]; wh *= 16777619u; wh ^= g_jbg[i]; wh *= 16777619u; }
            }
        }
        uint32_t ph[3] = { fnv(old, px), fnv(old + px, px), fnv(old + 2 * px, px) };
#if defined(TANK_ROUND)
        static const uint32_t GOLD[4] = { 0x7b94051f, 0x4c66318e, 0xddb3e1a0, 0xa5527822 };
#elif defined(TANK_WATCH)
        static const uint32_t GOLD[4] = { 0xd3399f81, 0x6d92768d, 0xc5a1f6f5, 0x82a93161 };
#elif defined(TANK_SQUARE)
        static const uint32_t GOLD[4] = { 0x406f6566, 0x3243596e, 0xe14684ce, 0xdd813d36 };
#else
        static const uint32_t GOLD[4] = { 0x37983cb2, 0xc15d6655, 0xd2fcaf1d, 0x80b427be };
#endif
#if defined(__APPLE__) && defined(__aarch64__)
        if (wh != GOLD[0]) LF("I: the jelly tank's water / frames are not main's (hash %08x, e8d49ea's %08x)", wh, GOLD[0]);
        for (int k = 0; k < 3; k++) if (ph[k] != GOLD[1 + k]) LF("I: theme %d's picture is not main's (hash %08x, e8d49ea's %08x)", k, ph[k], GOLD[1 + k]);
        printf("selftest-libload: every pixel of the three tanks under %d lights on and off, straight and cached, and the three pictures: main's e8d49ea (%08x; %08x %08x %08x)\n", JLIGHT_N, wh, ph[0], ph[1], ph[2]);
#else
        (void)GOLD; printf("selftest-libload: the three tanks under %d lights %08x, the pictures %08x %08x %08x (the Mac's are pinned; here said, not compared)\n", JLIGHT_N, wh, ph[0], ph[1], ph[2]);
#endif
        free(live); jelly_set_bg_cache(NULL); }
    lp_close(&H); if (g_live) LF("I: %d blocks left", g_live);

    /* ---- J: the locked row, its note ---- */
    lp_reset(&H, fish); H.lib = true; H.crew = false;
    lp_frame(&H, fa, 0.3f);
    render_library_pic_rect(1, &q[0][0], &q[0][1], &q[0][2], &q[0][3]);
    diff_page(fa, H.page, 1, q, &out, &in, &bright);
    int dim_in = in, dim_max = 0, lit_max = 0;
    for (int i = 0; i < NPX; i++) if (fa[i] != H.page[i] && luma(fa[i]) > dim_max) dim_max = luma(fa[i]);
    memcpy(fc, H.page, sizeof fc); render_library_loading(fc, TANK_W, 2, 0, 0.3f);
    for (int i = 0; i < NPX; i++) if (fc[i] != H.page[i] && luma(fc[i]) > lit_max) lit_max = luma(fc[i]);
    if (out || !dim_in || dim_max >= lit_max || dim_max < 90) LF("J: the locked row's bubbles: %d outside, %d inside, light %d (an open row's %d)", out, dim_in, dim_max, lit_max);
    H.note = true; H.baked = false;                      /* its note: nothing over it, the picture still comes */
    for (int f = 0; f < FIRST + 2; f++) {
        lp_frame(&H, fa, 0.34f + f * 0.04f);
        if (H.baked && memcmp(fa, H.page, sizeof fa)) LF("J: bubbles over the row's note (frame %d)", f);
    }
    if (H.pics->have != 1) LF("J: under the note the picture did not arrive");
    draw_page(&H, fb2, oldp);
    if (memcmp(fa, fb2, sizeof fa)) LF("J: the note over the finished locked list is not 0.4.1's");
    lp_close(&H); if (g_live) LF("J: %d blocks left", g_live);

    /* ---- K: a tap every frame (each draws the page again) does not hold a picture up ---- */
    lp_reset(&H, fish); H.lib = true;
    for (int f = 0; f < FIRST; f++) { lp_frame(&H, fa, f * 0.04f); H.baked = false; }
    if (H.arrived[0] != FIRST) LF("K: with the page drawn again on every frame (a tap each) theme 0 came on frame %d, not %d", H.arrived[0], FIRST);
    lp_frame(&H, fa, 1); draw_page(&H, fb2, oldp);
    if (memcmp(fa, fb2, sizeof fa)) LF("K: after a tap every frame the finished list is not 0.4.1's");
    lp_close(&H);

    /* ---- L: the rest of MY TANKS mid-load ---- */
    lp_reset(&H, fish); H.lib = true;                    /* SWAP IN begun with a picture half made (BACK, a card, SWAP IN: the page */
    for (int f = 0; f < 4; f++) lp_frame(&H, fa, 0);     /* drawn again every frame as its button pulses, then the dark and the swap) */
    if (H.pics->cur != 0 || !H.pics->ticks) LF("L: not mid-load");
    H.lib = false;
    for (int f = 0; f < 12; f++) { H.baked = false; lp_frame(&H, fa, 0); if (H.pics->cur != -1 || H.pics->have) LF("L: picture work during a swap (frame %d)", f); }
    { const uint16_t *none[JELLY_THEME_N] = { NULL, NULL, NULL }; draw_page(&H, fb2, none); }
    if (memcmp(fa, fb2, sizeof fa)) LF("L: the shelves under a swap begun mid-load are not the shelves");
    lp_close(&H); if (g_live) LF("L: %d blocks left after the swap closed MY TANKS", g_live);
    lp_reset(&H, fish); H.lib = true;                    /* shut by a page or the birth flow mid-load: the frame loop frees it, the flags stay */
    for (int f = 0; f < 4; f++) lp_frame(&H, fa, 0);
    lp_free_only(&H); if (g_live) LF("L: %d blocks left after MY TANKS was shut by a page mid-load", g_live);
    H.lib = H.pick = H.note = false;                     /* BOOT twice again (shelf_toggle clears them): the shelves, then the library */
    lp_frame(&H, fa, 0); H.lib = true; H.baked = false; f0 = H.frame;
    for (int f = 0; f < FIRST; f++) lp_frame(&H, fa, f * 0.04f);
    draw_page(&H, fb2, oldp);
    if (H.arrived[0] - f0 != FIRST || memcmp(fa, fb2, sizeof fa)) LF("L: opened again after a page shut MY TANKS mid-load: frame %d, or not the finished list", H.arrived[0] - f0);
    lp_close(&H); if (g_live) LF("L: %d blocks left", g_live);
    {   /* the opening tap's double is spent as before, and only it: the wait is the page's own stamp (touch_gate.h), which
           a picture's making never moves - a press from before the page or inside TAP_W of it is no tap, one after it acts */
        open_wait_t w = { 0, false }; const int64_t t0 = 5000000;
        lp_reset(&H, fish); H.lib = true; open_wait_start(&w, t0);
        for (int f = 0; f < 4; f++) lp_frame(&H, fa, 0);                                   /* loading: the stamp untouched */
        if (w.at != t0) LF("L: the library's opening stamp moved while a picture loaded");
        if (!open_wait_spent_at(&w, t0 - 30000, t0 + 60000) || !open_wait_spent_at(&w, t0 + OPEN_WAIT_US - 1000, t0 + OPEN_WAIT_US + 40000))
            LF("L: the opening tap's double was not spent while a picture loads");
        if (open_wait_spent_at(&w, t0 + OPEN_WAIT_US, t0 + OPEN_WAIT_US + 40000)) LF("L: a tap after the opening wait is held up while a picture loads");
        if (!lib_pics_waiting(H.pics, 1) || render_library_tap(tx, ty, 2) != LIB_TAP_ROW0 + 1) LF("L: that tap does not reach the jelly row mid-load");
        lp_close(&H);
    }
    printf("selftest-libload: a tap every frame holds nothing up; a swap, a page shutting MY TANKS, mid-load: no work, every block back, a fresh start after; the opening tap's double spent as ever, the next tap acts\n");
    printf("selftest-libload: one tick a frame, 3 ms, everything at once, and a jelly tank's water cache in use: the same three pictures (%08x %08x %08x); the locked row's bubbles dimmer (%d < %d) and none over its note\n",
           fnv(old, px), fnv(old + px, px), fnv(old + 2 * px, px), dim_max, lit_max);
    free(fish); free(old);
    printf("selftest-libload: PASS (%d blocks asked for, every one given back)\n", g_allocs);
    return 0;
}

/* --libload-shot <prefix>: ART FOR APPROVAL (tools/libload042_sheets.py makes the board's sheet). The library just opened,
 * the bubbles at three moments, the picture in; PICK A THEME loading, one in, all in; all three loading; the locked row. */
static void ll_ppm(const char *prefix, const char *name, const uint16_t *fb) {
    char path[600]; snprintf(path, sizeof path, "%s_%s.ppm", prefix, name);
    FILE *f = fopen(path, "wb"); if (!f) { printf("libload-shot: cannot write %s\n", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int i = 0; i < NPX; i++) { uint16_t v = fb[i]; unsigned char c[3] = { (unsigned char)((v >> 11) * 255 / 31), (unsigned char)(((v >> 5) & 63) * 255 / 63), (unsigned char)((v & 31) * 255 / 31) }; fwrite(c, 1, 3, f); }
    fclose(f);
}
int libload_shot(const char *prefix) {
    static tank_t T;
    int tw, th; render_shelf_thumb_size(&tw, &th);
    uint16_t *fish = malloc((size_t)tw * th * 2);
    tank_init(&T, 2024); progression_boot(&T); progression_setup_done(&T);
    for (int f = 0; f < 3; f++) render_tank(&T, fa, TANK_W);
    render_shelf_thumb(fa, fish);
    lp_t H; lp_reset(&H, fish); H.lib = true;
    lp_frame(&H, fa, 0.0f);  ll_ppm(prefix, "01_opened", fa);                 /* the frame after the tap */
    lp_frame(&H, fa, 0.5f);  ll_ppm(prefix, "02_loading", fa);
    lp_frame(&H, fa, 1.0f);  ll_ppm(prefix, "03_loading", fa);
    for (int f = 0; f < 10 && !H.pics->have; f++) lp_frame(&H, fa, 1.5f);
    lp_frame(&H, fa, 1.6f);  ll_ppm(prefix, "04_picture_in", fa);
    H.pick = true; H.baked = false;
    lp_frame(&H, fa, 1.7f);  ll_ppm(prefix, "05_pick_opened", fa);            /* PICK A THEME: MOONLIGHT made, two loading */
    for (int f = 0; f < 10 && H.pics->have != 3; f++) lp_frame(&H, fa, 2.3f);
    lp_frame(&H, fa, 2.4f);  ll_ppm(prefix, "06_pick_two_in", fa);
    for (int f = 0; f < 10 && H.pics->have != 7; f++) lp_frame(&H, fa, 2.9f);
    lp_frame(&H, fa, 3.0f);  ll_ppm(prefix, "07_pick_complete", fa);
    lp_close(&H);
    lp_reset(&H, fish); H.lib = true; H.pick = true;                          /* GET tapped before any picture: all three loading */
    lp_frame(&H, fa, 0.8f);  ll_ppm(prefix, "08_pick_all_loading", fa);
    lp_close(&H);
    lp_reset(&H, fish); H.lib = true; H.crew = false;                         /* a board off the crew: the dim row */
    lp_frame(&H, fa, 0.8f);  ll_ppm(prefix, "09_locked_loading", fa);
    lp_close(&H);
    {   /* what the pictures cost on THIS machine (a board logs its own: "jelly: the library's picture ...") */
        g_real = true;
        void *sc = calloc(1, jelly_scratch_bytes()); uint16_t *old = malloc((size_t)tw * th * 2 * JELLY_THEME_N);
        jelly_set_scratch(sc); jelly_set_bg_cache(NULL);
        int64_t t0 = lp_now(); old_pics(scratch_frame, old, (size_t)tw * th); int64_t t1 = lp_now();
        jelly_set_scratch(NULL); free(sc); free(old);
        lp_reset(&H, fish); H.lib = true; H.pick = true;
        for (int f = 0; f < 400 && (!H.pics || H.pics->have != 7); f++) lp_frame(&H, fa, 0);
        printf("libload-shot: on this machine: all three made before the first frame (0.4.1's order) %.1f ms; now nothing before it, then %.2f / %.2f / %.2f ms of work each (frames %d %d %d)\n",
               (t1 - t0) / 1000.0, H.work_us[0] / 1000.0, H.work_us[1] / 1000.0, H.work_us[2] / 1000.0, H.arrived[0], H.arrived[1], H.arrived[2]);
        lp_close(&H); g_real = false;
    }
    free(fish);
    printf("libload-shot: wrote %s_*.ppm\n", prefix);
    return 0;
}
